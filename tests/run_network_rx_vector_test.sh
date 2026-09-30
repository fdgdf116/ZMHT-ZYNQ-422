#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/zmht-rx-vector-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
gcc -Iinclude/common -Isrc/hardware -c src/hardware/ringbuffer.c -o "$test_dir/ringbuffer.o"
g++ -std=c++11 -ffunction-sections -fdata-sections \
 -Iinclude -Iinclude/common -Isrc/hardware -Isrc/network -Isrc/system \
 tests/network_rx_vector_test.cpp src/network/tcp_socket.cpp "$test_dir/ringbuffer.o" \
 -Wl,--gc-sections -pthread -o "$test_dir/test"
"$test_dir/test"
