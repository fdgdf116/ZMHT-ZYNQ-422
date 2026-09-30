#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/zmht-queued-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
g++ -DNETWORK_MALLOC_BENCHMARK=${NETWORK_MALLOC_BENCHMARK:-0} -std=c++11 -ffunction-sections -fdata-sections \
 -Iinclude -Iinclude/common -Isrc/hardware -Isrc/network -Isrc/system \
 tests/network_queued_test.cpp src/network/tcp_socket.cpp src/network/thread.cpp \
 -Wl,--gc-sections -pthread -o "$test_dir/test"
python3 - "$test_dir/test" <<'PY'
import subprocess, socket, time, sys, tempfile, re, struct
with tempfile.TemporaryFile(mode='w+') as log:
    proc = subprocess.Popen([sys.argv[1], sys.argv[1]+".buffer"], stdout=log, stderr=log)
    def output():
        log.seek(0)
        return log.read()
    try:
        deadline=time.monotonic()+5
        while 'READY ' not in output():
            assert proc.poll() is None and time.monotonic()<deadline, output()
            time.sleep(.02)
        port=int(re.search(r'READY (\d+)',output())[1])
        time.sleep(2.1)
        assert '[NET 9016 RX]' not in output()
        def frame(body):
            return struct.pack('!IIHHHH', 0x04ccf0ff, 4+len(body), 1, 0x0204, 0, 0) + bytes.fromhex('12345678') + body
        # A fragmented, incomplete header on a previous connection must not leak.
        with socket.create_connection(('127.0.0.1',port)) as sock:
            sock.sendall(frame(b'bad')[:9])
        time.sleep(.1)
        # Reject lengths that cannot hold a channel, and reset a partial channel.
        for length in range(4):
            with socket.create_connection(('127.0.0.1',port)) as sock:
                sock.sendall(struct.pack('!IIHHHH', 0x04ccf0ff, length, 1, 0x0204, 0, 0))
                sock.settimeout(2)
                assert sock.recv(1) == b''
        with socket.create_connection(('127.0.0.1',port)) as sock:
            sock.sendall(frame(b'bad')[:18])
        time.sleep(.1)
        ring_size=32*1024*1024
        # Non-periodic tail makes wrap placement distinguishable from old data.
        data=b'a'*ring_size + b'wrap-tail-'*103 + b'END'
        tail=data[ring_size:]
        with socket.create_connection(('127.0.0.1',port)) as sock:
            # Coalesced requests with different body lengths, including 128 KiB.
            lengths = [65536] * 32 + [1024, 131072, 131076]
            parts = []
            pos = 0
            index = 0
            while pos < len(data):
                length = lengths[index % len(lengths)]
                body = data[pos:pos+length]
                parts.append(frame(body))
                pos += len(body)
                index += 1
            wire = b''.join(parts)
            sock.sendall(wire[:3]); time.sleep(.02)
            sock.sendall(wire[3:17]); time.sleep(.02)
            sock.sendall(wire[17:19]); time.sleep(.02)
            sock.sendall(wire[19:])
            time.sleep(2.2)
            with open(sys.argv[1]+'.buffer','rb') as f:
                memory=f.read()
            assert memory[:len(tail)]==tail
            assert memory[len(tail):ring_size]==b'a'*(ring_size-len(tail))
            assert memory[ring_size:]==bytes([0xa5])*16
            assert 'wraps=1' in output(), output()
            amounts=[int(x) for x in re.findall(r' bytes=(\d+)',output())]
            assert sum(amounts)==len(data), output()
            count=output().count('[NET 9016 RX]')
            time.sleep(2.1)
            assert output().count('[NET 9016 RX]')==count, output()
        with socket.create_connection(('127.0.0.1',port)) as sock:
            body=bytes.fromhex('12345678') + b'x'*4092
            sock.sendall(frame(b'') + frame(body))
            time.sleep(2.2)
            assert 'bytes=4096 ' in output(), output()
            with open(sys.argv[1]+'.buffer','rb') as f:
                memory=f.read()
            assert memory[:4096]==body  # disconnected partial block restarts at its beginning
            assert memory[ring_size:]==bytes([0xa5])*16
        print('TCP queued tests passed: direct mapped writes, queue consumer, bounds, reconnect partial discard, rate counts, 16-byte header and 4-byte channel stripping, 64 KiB payloads, fragmented/coalesced requests, invalid lengths, retained payload prefix')
    finally:
        proc.terminate(); proc.wait(timeout=5)
PY
