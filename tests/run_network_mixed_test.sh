#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/zmht-mixed-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
gcc -Iinclude/common -Isrc/hardware -c src/hardware/ringbuffer.c -o "$test_dir/ringbuffer.o"
g++ -std=c++11 -ffunction-sections -fdata-sections \
 -Iinclude -Iinclude/common -Isrc/hardware -Isrc/network -Isrc/system \
 tests/network_mixed_test.cpp src/network/tcp_socket.cpp src/network/thread.cpp "$test_dir/ringbuffer.o" \
 -Wl,--gc-sections -pthread -o "$test_dir/test"
python3 tests/network_mixed_test.py "$test_dir/test" "$test_dir/data"
