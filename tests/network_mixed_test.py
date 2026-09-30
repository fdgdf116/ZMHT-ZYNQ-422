import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import re

binary, base = sys.argv[1:]
def wait_for(check, timeout=5):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        result=check()
        if result:
            return result
        time.sleep(.01)
    raise AssertionError('condition timed out')

def receive(sock, size):
    data=b''
    while len(data)<size:
        part=sock.recv(size-len(data))
        if not part:
            raise EOFError()
        data+=part
    return data

def request(index, channel, payload):
    return struct.pack('!IIHHHHI',0x04ccf0ff,4+len(payload),0,index,0,0,channel)+payload

def records(suffix):
    try:
        data=open(base+suffix,'rb').read()
    except FileNotFoundError:
        return []
    result=[]
    while len(data)>=4:
        size=struct.unpack('=I',data[:4])[0]
        if len(data)<4+size:
            break
        result.append(data[4:4+size])
        data=data[4+size:]
    return result

class Downlink:
    def __init__(self, port):
        self.sock=socket.create_connection(('127.0.0.1',port),timeout=5)
        self.reports=[]
        self.errors=[]
        self.closed=False
        def reader():
            try:
                while True:
                    head=struct.unpack('!IIHHHH',receive(self.sock,16))
                    assert head[:2]==(0x05ccf0ff,400) and head[3]==1, head
                    body=receive(self.sock,400)
                    report=[struct.unpack_from('!BIIIIII',body,i*25) for i in range(16)]
                    assert [r[0] for r in report]==list(range(16))
                    assert all(r[1:4]==(0,0,0) for r in report)
                    assert all(r[4]==report[0][4] for r in report)
                    assert [r[5] for r in report]==[0x4000000-i*4 for i in range(16)]
                    assert all(0 <= r[6] <= 2048 and r[6] % 512 == 0 for r in report)
                    self.reports.append(report)
            except (EOFError,OSError):
                pass
            except Exception as error:
                self.errors.append(error)
            finally:
                self.closed=True
        self.reader=threading.Thread(target=reader,daemon=True)
        self.reader.start()
    def close(self):
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self.reader.join(2)
        assert not self.reader.is_alive()
        assert not self.errors,self.errors

with tempfile.TemporaryFile(mode='w+') as log:
    proc=subprocess.Popen([binary,base],stdout=log,stderr=log)
    def output():
        log.seek(0)
        return log.read()
    try:
        ready=wait_for(lambda: re.search(r'READY (\d+) (\d+)',output()))
        down_port,up_port=map(int,ready.groups())
        # Mixed upload uses production loop: old FIFO wire format, unfragmented
        # DMA vector sends, per-channel order, and a bounded FIFO burst.
        with socket.create_connection(('127.0.0.1',up_port),timeout=5) as up:
            fifo=[[] for _ in range(16)]
            dma=[]
            dma_positions=[]
            for position in range(16*70+2):
                head=struct.unpack('!IIHHHH',receive(up,16))
                assert head[0]==0x01ccf0ff and head[2]==0
                body=receive(up,head[1])
                channel=struct.unpack('!I',body[:4])[0]
                if head[3]==3:
                    assert channel==0
                    dma.append(body[4:]); dma_positions.append(position)
                else:
                    assert head[3]==6 and channel in range(16)
                    fifo[channel].append(body[4:])
            assert dma==[bytes([0xd0])*(2*1024*1024),bytes([0xd1])*1024]
            assert dma_positions==[64,129],dma_positions
            for ch in range(16):
                assert fifo[ch]==[bytes([(ch*100+i)&255])*17 for i in range(70)]
        down=Downlink(down_port)
        wait_for(lambda: down.reports)
        assert down.reports[-1][0][4]==32768
        assert [r[6] for r in down.reports[-1]]==[2048]*16
        # A partial DMA block survives interleaved FIFO requests.
        down.sock.sendall(request(0x0204,0,b'A'*123))
        wait_for(lambda: any(r[0][4]==32767 for r in down.reports))
        payload=bytes(range(251))*4+b'B'*96
        assert len(payload)==1100
        wire=request(6,0,payload)+request(6,1,b'channel-one')
        for left,right in [(0,3),(3,17),(17,19),(19,531),(531,len(wire))]:
            down.sock.sendall(wire[left:right]); time.sleep(.01)
        down.sock.sendall(request(0x0204,0,b'Z'*(2*1024*1024-123)))
        wait_for(lambda: len(records('.fifo0'))==3 and len(records('.fifo1'))==1 and records('.dma'))
        assert records('.fifo0')==[payload[:508],payload[508:1016],payload[1016:]]
        assert records('.fifo1')==[b'channel-one']
        assert records('.dma')==[b'A'*123+b'Z'*(2*1024*1024-123)]
        # No FIFO chunk is visible until the entire request has arrived.
        whole=bytes(range(256))*9
        wire=request(6,15,whole)
        down.sock.sendall(wire[:-1])
        time.sleep(.15)
        assert records('.fifo15')==[]
        down.sock.sendall(wire[-1:])
        wait_for(lambda: len(records('.fifo15'))==5)
        assert b''.join(records('.fifo15'))==whole
        for ch in range(2,15):
            down.sock.sendall(request(6,ch,bytes([ch])*19))
        wait_for(lambda: all(records('.fifo'+str(ch))==[bytes([ch])*19] for ch in range(2,15)))
        # An incomplete request larger than one chunk must publish nothing.
        down.sock.sendall(request(6,14,b'incomplete'*200)[:-1])
        time.sleep(.1)
        down.close()
        assert records('.fifo14')==[bytes([14])*19]
        down=Downlink(down_port)
        # Backpressure retains the pending FIFO chunk and reports a full queue.
        open(base+'.pause_fifo','w').close()
        start=len(down.reports)
        blocked=bytes([0x7d])*(5*508+37)
        down.sock.sendall(request(6,0,blocked))
        wait_for(lambda: any(r[0][6]==0 for r in down.reports[start:]))
        assert len(records('.fifo0'))==3
        os.unlink(base+'.pause_fifo')
        wait_for(lambda: len(records('.fifo0'))==9)
        assert b''.join(records('.fifo0')[3:])==blocked
        # Empty FIFO requests and a short incomplete request do not enter DMA.
        down.sock.sendall(request(6,1,b'')+request(6,1,b'x'*100)[:30])
        time.sleep(.1)
        down.close()
        assert len(records('.fifo1'))==1
        # Reject inactive FIFO channels/short metadata/unknown indexes, while
        # allowing the capacity thread to send safely during each disconnect.
        invalid=[request(6,16,b'bad'),request(6,0xffffffff,b'bad'),request(7,0,b'bad')]
        invalid += [struct.pack('!IIHHHH',0x04ccf0ff,n,0,6,0,0)+b'x'*n for n in range(4)]
        for wire in invalid:
            client=Downlink(down_port)
            client.sock.sendall(wire)
            wait_for(lambda: client.closed)
            client.close()
        client=Downlink(down_port)
        wait_for(lambda: client.reports)
        client.sock.sendall(request(6,1,b'reconnected'))
        wait_for(lambda: len(records('.fifo1'))==2)
        assert records('.fifo1')[-1]==b'reconnected'
        client.close()
        # A disconnect while FIFO is full must unblock the receive loop and
        # release the shared socket safely for the capacity reporter.
        before=len(records('.fifo0'))
        open(base+'.pause_fifo','w').close()
        client=Downlink(down_port)
        client.sock.sendall(request(6,0,b'Q'*(6*508)))
        wait_for(lambda: any(r[0][6]==0 for r in client.reports))
        client.close()
        replacement=Downlink(down_port)
        wait_for(lambda: replacement.reports)
        assert replacement.reports[-1][0][6]==0
        os.unlink(base+'.pause_fifo')
        wait_for(lambda: len(records('.fifo0'))==before+4)
        replacement.sock.sendall(request(6,1,b'after-full-disconnect'))
        wait_for(lambda: len(records('.fifo1'))==3)
        assert records('.fifo1')[-1]==b'after-full-disconnect'
        replacement.close()
        assert len(records('.dma'))==1
        print('Mixed tests passed: DMA/FIFO routing, wire formats, 16 channels, 508-byte chunks, full queue, capacity units, upload fairness, invalid input and reconnect')
    except Exception:
        print(output())
        raise
    finally:
        proc.terminate()
        proc.wait(timeout=5)
