MICA 2
======

A fast in-memory key-value store.

The current version should be compiled with DPDK v20.11.7 (with MLX5 PMD) and gcc-10.

Requirements
------------

 * Linux x86\_64 >= 3.0
 * Intel CPU >= Sandy Bridge (Haswell or more recent recommended)
 * Hugepage (2 GiB) support

Reserve Hugepages
------------

This repo works with 2-MB hugepages. If you are using 1-GB hugepages, you have to modify the commands related to hugepages in `/etc/default/grub` as follows:
```bash
default_hugepagesz=2MB hugepagesz=2M hugepages=65536
```

Dependencies for compilation
----------------------------

 * g++ >= 5.3
 * cmake >= 2.8
 * make >= 3.81
 * libnuma-dev >= 2.0
 * libcurl-dev >= 7.35
 * DPDK >= 16.11
 * libsqlite3-dev >= 3.0 (with -DSQLITE=ON)

Dependencies for execution
--------------------------

 * bash >= 4.0
 * python >= 3.4
 * etcd >= 2.2

Compiling DPDK
--------------

         * cd dpdk-20.11.7
         * meson build
         * ninja -C build
         * sudo ninja -C build install
         * sudo ldconfig
         # Optimization: try to increase "CONFIG_RTE_MEMPOOL_CACHE_MAX_SIZE" to 4096 in build/.config (but it can also break mempool initialization)

Compiling MICA
--------------

         * cd mica2/build
         * export PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:/usr/local/lib/x86_64-linux-gnu/pkgconfig
         * cmake ..
         * make -j

Setting up the general environment
----------------------------------

         * cd mica2/build
         * ln -s src/mica/test/*.json .
         * ../script/setup.sh 8192 8192    # 2 NUMA nodes, 16 Ki pages (32 GiB)
         * killall etcd
         * ../script/start-etcd.sh & # Skip next section. Adjust IPs according to you system. Use an iterface that won't be used by DPDK

Setting up etcd cluster
----------------------------------

You need to setup an etcd cluster for client and server via the following commands:

```bash
# Node 0
etcd --name infra0 --initial-advertise-peer-urls http://192.168.3.13:2380 \
  --listen-peer-urls http://192.168.3.13:2380 \
  --listen-client-urls http://192.168.3.13:2379,http://localhost:2379 \
  --advertise-client-urls http://192.168.3.13:2379 \
  --initial-cluster-token etcd-cluster-1 \
  --initial-cluster infra0=http://192.168.3.13:2380,infra1=http://192.168.3.27:2380 \
  --initial-cluster-state new
# Node 1
etcd --name infra1 --initial-advertise-peer-urls http://192.168.3.27:2380 \
  --listen-peer-urls http://192.168.3.27:2380 \
  --listen-client-urls http://192.168.3.27:2379,http://localhost:2379 \
  --advertise-client-urls http://192.168.3.27:2379 \
  --initial-cluster-token etcd-cluster-1 \
  --initial-cluster infra0=http://192.168.3.13:2380,infra1=http://192.168.3.27:2380 \
  --initial-cluster-state new
```

**You may need to change the IP addresses.**


Setting up the DPDK environment
-------------------------------

         * sudo modprobe uio
         * sudo insmod dpdk/build/kmod/igb_uio.ko
         * dpdk/tools/dpdk_nic_bind.py --status
         * sudo dpdk/tools/dpdk_nic_bind.py --force -b igb_uio 0000:02:00.0 0000:02:00.1 0000:04:00.0 0000:04:00.1 0000:83:00.0 0000:83:00.1 0000:84:00.0 0000:84:00.1
         * sudo dpdk/tools/dpdk_nic_bind.py --force -b igb_uio 0000:01:00.0 0000:01:00.1 0000:03:00.0 0000:03:00.1 0000:42:00.0 0000:42:00.1 0000:43:00.0 0000:43:00.1
         * sudo dpdk/tools/dpdk_nic_bind.py --force -b igb_uio 0000:03:00.0 0000:03:00.1

Running microbench
------------------

         * cd mica2/build
         * sudo ./microbench 0.00          # 0.00 = uniform key popularity

Running Server
--------------
      
        * cd mica2/build
        * sudo ./server                   # Before this, adjust server.json according to your system configuration

Running netbench client
-----------------------

        * cd mica2/build
        * sudo ./netbench 0.00          # Run netbench client with uniform key popularity. Before running this adjust netbench.json

Authors
-------

Hyeontaek Lim (hl@cs.cmu.edu)

License
-------

        Copyright 2014, 2015, 2016, 2017 Carnegie Mellon University

        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

            http://www.apache.org/licenses/LICENSE-2.0

        Unless required by applicable law or agreed to in writing, software
        distributed under the License is distributed on an "AS IS" BASIS,
        WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
        See the License for the specific language governing permissions and
        limitations under the License.

