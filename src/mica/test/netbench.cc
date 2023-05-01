#include "mica/datagram/datagram_client.h"
#include "mica/util/lcore.h"
#include "mica/util/hash.h"
#include "mica/util/zipf.h"

#define MAX_SAMPLES 10000000000UL
#define MAX_SAMPLES_PER_THREAD MAX_SAMPLES / 4

int comp(const void *a, const void *b) {
	uint64_t ua = *((uint64_t *)a);
	uint64_t ub = *((uint64_t *)b);

	if (ua > ub) return 1;
	if (ua < ub) return -1;

	return 0;
}

typedef ::mica::alloc::HugeTLBFS_SHM Alloc;

struct DPDKConfig : public ::mica::network::BasicDPDKConfig {
  static constexpr bool kVerbose = true;
};

struct DatagramClientConfig
    : public ::mica::datagram::BasicDatagramClientConfig {
  typedef ::mica::network::DPDK<DPDKConfig> Network;
  // static constexpr bool kSkipRX = true;
  // static constexpr bool kIgnoreServerPartition = true;
  // static constexpr bool kVerbose = true;
};

typedef ::mica::datagram::DatagramClient<DatagramClientConfig> Client;

typedef ::mica::table::Result Result;

template <typename T>
static uint64_t hash(const T* key, size_t key_length) {
  return ::mica::util::hash(key, key_length);
}

class ResponseHandler
    : public ::mica::datagram::ResponseHandlerInterface<Client> {
 public:
  void handle(Client::RequestDescriptor rd, Result result, const char* value,
              size_t value_length, const Argument& arg) {
    (void)rd;
    (void)result;
    (void)value;
    (void)value_length;
    (void)arg;
  }
};

struct Args {
  uint16_t lcore_id;
  ::mica::util::Config* config;
  Alloc* alloc;
  Client* client;
  double zipf_theta;
  uint64_t *num_responses;
  uint64_t *rtts;
  int run_time;
} __attribute__((aligned(128)));

int worker_proc(void* arg) {
  auto args = reinterpret_cast<Args*>(arg);

  Client& client = *args->client;
  *args->num_responses = 0;

  ::mica::util::lcore.pin_thread(args->lcore_id);

  printf("worker running on lcore %" PRIu16 "\n", args->lcore_id);

  client.probe_reachability();

  ResponseHandler rh;

  size_t num_items = 192 * 1048576;

  // double get_ratio = 0.95;
  double get_ratio = 0.50;

  uint32_t get_threshold = (uint32_t)(get_ratio * (double)((uint32_t)-1));

  ::mica::util::Rand op_type_rand(static_cast<uint64_t>(args->lcore_id) + 1000);
  ::mica::util::ZipfGen zg(num_items, args->zipf_theta,
                           static_cast<uint64_t>(args->lcore_id));
  ::mica::util::Stopwatch sw;
  sw.init_start();
  sw.init_end();

  uint64_t key_i;
  uint64_t key_hash;
  size_t key_length = sizeof(key_i);
  char* key = reinterpret_cast<char*>(&key_i);

  uint64_t value_i;
  size_t value_length = sizeof(value_i);
  char* value = reinterpret_cast<char*>(&value_i);

  bool use_noop = false;
  // bool use_noop = true;

  uint64_t expererimet_start_time = sw.now();

  uint64_t last_handle_response_time = sw.now();
  // Check the response after sending some requests.
  // Ideally, packets per batch for both RX and TX should be similar.
  uint64_t response_check_interval = 20 * sw.c_1_usec();

  uint64_t seq = 0;
  while (sw.diff(sw.now(), expererimet_start_time) < args->run_time) {
    // Determine the operation type.
    uint32_t op_r = op_type_rand.next_u32();
    bool is_get = op_r <= get_threshold;

    // Generate the key.
    key_i = zg.next();
    key_hash = hash(key, key_length);

    uint64_t now = sw.now();
    while (!client.can_request(key_hash) ||
           sw.diff_in_cycles(now, last_handle_response_time) >=
               response_check_interval) {
      last_handle_response_time = now;
      client.handle_response(rh, args->rtts, args->num_responses);
    }

    if (!use_noop) {
      if (is_get)
        client.get(key_hash, key, key_length);
      else {
        value_i = seq;
        client.set(key_hash, key, key_length, value, value_length, true);
      }
    } else {
      if (is_get)
        client.noop_read(key_hash, key, key_length);
      else {
        value_i = seq;
        client.noop_write(key_hash, key, key_length, value, value_length);
      }
    }

    seq++;
  }

  return 0;
}

int main(int argc, const char* argv[]) {
  if (argc != 3) {
    printf("%s ZIPF-THETA BENCH-TIME\n", argv[0]);
    return EXIT_FAILURE;
  }

  double zipf_theta = atof(argv[1]);
  int run_time = atoi(argv[2]);

  ::mica::util::lcore.pin_thread(0);

  auto config = ::mica::util::Config::load_file("netbench.json");

  Alloc alloc(config.get("alloc"));

  DatagramClientConfig::Network network(config.get("network"));
  network.start();

  Client::DirectoryClient dir_client(config.get("dir_client"));

  Client client(config.get("client"), &network, &dir_client);
  client.discover_servers();

  uint16_t lcore_count =
      static_cast<uint16_t>(::mica::util::lcore.lcore_count());

  uint16_t lcore_enabled = 0;

  std::vector<Args> args(lcore_count);
  std::vector<uint64_t> num_responses(lcore_count);
  for (uint16_t lcore_id = 0; lcore_id < lcore_count; lcore_id++) {
    args[lcore_id].lcore_id = lcore_id;
    args[lcore_id].config = &config;
    args[lcore_id].alloc = &alloc;
    args[lcore_id].client = &client;
    args[lcore_id].zipf_theta = zipf_theta;
    args[lcore_id].num_responses = &num_responses[lcore_id];
    args[lcore_id].run_time = run_time;
  }

  for (uint16_t lcore_id = 1; lcore_id < lcore_count; lcore_id++) {
    if (!rte_lcore_is_enabled(static_cast<uint8_t>(lcore_id))) continue;
    lcore_enabled++;
    args[lcore_id].rtts = (uint64_t *)malloc(sizeof(uint64_t) * MAX_SAMPLES_PER_THREAD);
    rte_eal_remote_launch(worker_proc, &args[lcore_id], lcore_id);
  }
  lcore_enabled++;
  args[0].rtts = (uint64_t *)malloc(sizeof(uint64_t) * MAX_SAMPLES_PER_THREAD);
  worker_proc(&args[0]);

  // wait for lcores to finish
  rte_eal_mp_wait_lcore();

  // collect rtt of all packets
  uint64_t *all_rtts = (uint64_t*)malloc(sizeof(uint64_t) * MAX_SAMPLES);
  uint64_t total_cycles = 0;
  auto sw = client.get_stopwatch();

  uint64_t included_samples = 0;
  for (uint16_t lcore_id = 0; lcore_id < lcore_enabled; lcore_id++) {
    fprintf(stdout, "lcore %" PRIu16 ": %" PRIu64 " responses\n", lcore_id,
           num_responses[lcore_id]);
	  for (uint64_t i = 0.1 * num_responses[lcore_id]; i < num_responses[lcore_id] * 0.9; i++) {	
      total_cycles += args[lcore_id].rtts[i];
      all_rtts[included_samples++] = args[lcore_id].rtts[i];
    }
  }
	
  /* Measure p50 and p99 latency */
	qsort(all_rtts, included_samples, sizeof(uint64_t), comp);
	uint64_t p50_cycles = all_rtts[(int)(included_samples * 0.5)];
	uint64_t p99_cycles = all_rtts[(int)(included_samples * 0.99)];

	fprintf(stdout, "mean latency (us): %f\n", (float) total_cycles /
      (included_samples * sw.c_1_usec()));
	fprintf(stdout, "median latency (us): %f\n", p50_cycles / sw.c_1_usec());
	fprintf(stdout, "99th latency (us): %f\n", p99_cycles / sw.c_1_usec());
  fflush(stdout);

  network.stop();

  return EXIT_SUCCESS;
}
