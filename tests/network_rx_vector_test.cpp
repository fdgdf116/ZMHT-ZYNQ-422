#include <algorithm>
#include <cassert>
#include <thread>
#include <vector>
#include "../src/system/system.cpp"
#include "tcp_socket.h"
#include "cmd_packet.h"

// Force short writes at header boundaries and in the payload, plus EINTR.
static bool scripted = false;
static size_t step = 0;
static unsigned char* mapped_payload;
static std::vector<unsigned char> scripted_wire;
extern "C" ssize_t __real_sendmsg(int, const struct msghdr*, int);
extern "C" ssize_t __wrap_sendmsg(int fd, const struct msghdr* msg, int flags) {
    if(!scripted) return __real_sendmsg(fd, msg, flags);
    assert(flags & MSG_NOSIGNAL);
    if(step++ == 0) {
        assert(msg->msg_iovlen == 2);
        assert(msg->msg_iov[1].iov_base == mapped_payload);
        errno = EINTR;
        return -1;
    }
    const size_t limits[] = {7, 13, 513, 65536};
    size_t budget = limits[(std::min)(step - 2, size_t(3))];
    size_t count = 0;
    for(size_t i=0; i<msg->msg_iovlen && budget; ++i) {
        size_t n = (std::min)(budget, msg->msg_iov[i].iov_len);
        auto* data = static_cast<unsigned char*>(msg->msg_iov[i].iov_base);
        scripted_wire.insert(scripted_wire.end(), data, data+n);
        budget -= n;
        count += n;
    }
    return count;
}

int main() {
    std::vector<unsigned char> memory(17 * RECV_DMA_DATA_SIZE, 0xa5);
    for(size_t i=0; i<RECV_DMA_DATA_SIZE; ++i) memory[i]=(i*17+i/251)&255;
    auto& rx=rx_data_info[0];
    rx.sgdma_tx.mem_vir_base=memory.data();
    rx.sgdma_tx.map_size=memory.size();
    auto* rb=ringbuffer_create(16*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
    assert(rb);
    rx.data_stream.dma_data_rb=rb;
    for(unsigned int i=0; i<16; ++i) {
        image_frame_info_t frame={};
        frame.frame_size=RECV_DMA_DATA_SIZE;
        frame.frame_offset=i*RECV_DMA_DATA_SIZE;
        pcie_data_to_queue(rb,&frame);
    }
    unsigned char* payload=nullptr;
    assert(rx_sgdma_data_get(0,&payload)==RECV_DMA_DATA_SIZE);
    assert(payload==memory.data() && ringbuffer_len(rb)==15);
    // The producer can fill the spare block, then must stop before wrapping
    // onto block 0 while the single consumer sends its current frame.
    image_frame_info_t spare={};
    spare.frame_size=RECV_DMA_DATA_SIZE;
    spare.frame_offset=16*RECV_DMA_DATA_SIZE;
    memset(memory.data()+spare.frame_offset,0x5a,RECV_DMA_DATA_SIZE);
    pcie_data_to_queue(rb,&spare);
    assert(ringbuffer_is_full(rb));

    struct Header { Response response; uint32_t channel; } header={};
    static_assert(sizeof(header)==20,"wire header size");
    header.response.cmd_code=htonl(UP_CODE);
    header.response.index=htons(0x03);
    header.response.length=htonl(4+RECV_DMA_DATA_SIZE);
    header.channel=htonl(0);
    iovec packet[]={{&header,sizeof(header)},{payload,RECV_DMA_DATA_SIZE}};
    std::vector<unsigned char> expected(sizeof(header)+RECV_DMA_DATA_SIZE);
    memcpy(expected.data(),&header,sizeof(header));
    memcpy(expected.data()+sizeof(header),payload,RECV_DMA_DATA_SIZE);

    network::TcpSocket mock;
    mapped_payload=payload;
    scripted=true;
    assert(mock.SendVectorFully(packet,2));
    scripted=false;
    assert(scripted_wire==expected && step>4);
    assert(packet[0].iov_base==&header && packet[0].iov_len==20);
    assert(packet[1].iov_base==payload && packet[1].iov_len==RECV_DMA_DATA_SIZE);

    int sockets[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    network::TcpSocket sender(sockets[0]);
    sender.SetSendBUfSize(1024);
    std::vector<unsigned char> received(expected.size());
    std::thread reader([&] {
        size_t count=0;
        while(count<received.size()) {
            ssize_t n=read(sockets[1],received.data()+count,
                           (std::min)(size_t(777),received.size()-count));
            assert(n>0);
            count+=n;
        }
    });
    assert(sender.SendVectorFully(packet,2));
    reader.join();
    assert(received==expected);
    assert(ringbuffer_is_full(rb));
    assert(memcmp(payload,expected.data()+20,RECV_DMA_DATA_SIZE)==0);
    // Only after the frame completes does the sender take the next block.
    assert(rx_sgdma_data_get(0,&payload)==RECV_DMA_DATA_SIZE);
    assert(payload==memory.data()+RECV_DMA_DATA_SIZE);
    sender.Close();
    close(sockets[1]);

    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    network::TcpSocket disconnected(sockets[0]);
    close(sockets[1]);
    assert(!disconnected.SendVectorFully(packet,2));
    disconnected.Close();

    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    network::TcpSocket stalled(sockets[0]);
    stalled.SetSendBUfSize(1024);
    stalled.SetSendTimeout(20);
    assert(!stalled.SendVectorFully(packet,2));
    stalled.Close();
    close(sockets[1]);
    assert(mock.SendVectorFully(nullptr,0));
    assert(!mock.SendVectorFully(nullptr,1));
    ringbuffer_deinit(rb);
    puts("9014 vector tests passed: wire bytes, DMA pointer, short writes, EINTR, RX ring wrap protection, disconnect and timeout");
}
