#!/bin/bash

HOST1_IP=10.10.2.2 # This server
HOST2_IP=10.10.2.1 # Other server

etcd --name infra0 --initial-advertise-peer-urls http://${HOST1_IP}:2380 \
  --listen-peer-urls http://${HOST1_IP}:2380 \
  --listen-client-urls http://${HOST1_IP}:2379,http://localhost:2379 \
  --advertise-client-urls http://${HOST1_IP}:2379 \
  --initial-cluster-token etcd-cluster-1 \
  --initial-cluster infra0=http://${HOST1_IP}:2380,infra1=http://${HOST2_IP}:2380 \
  --initial-cluster-state new
