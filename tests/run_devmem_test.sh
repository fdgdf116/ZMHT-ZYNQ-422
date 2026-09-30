#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/zmht-devmem-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
cc -std=gnu11 -ffunction-sections -fdata-sections \
 -Iinclude -Iinclude/common -Isrc/hardware -Isrc/system \
 tests/devmem_test.c -Wl,--gc-sections -pthread -o "$test_dir/test"
"$test_dir/test"
