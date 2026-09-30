#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/zmht-local-dma-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
g++ -DENABLE_SYNTHETIC_DMA_TX=1 -std=c++11 -ffunction-sections -fdata-sections \
 -Iinclude -Iinclude/common -Isrc/hardware -Isrc/network -Isrc/system \
 tests/local_dma_test.cpp -Wl,--gc-sections -pthread -o "$test_dir/test"
"$test_dir/test"
