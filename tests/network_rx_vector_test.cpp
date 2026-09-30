#include <cassert>
#include <thread>
#include <vector>
#include "../src/system/system.cpp"
#include "tcp_socket.h"
#include "cmd_packet.h"

int main() {
    std::vector<unsigned char> memory(RECV_DMA_DATA_SIZE);
    for(size_t i=0;i<memory.size();++i) memory[i]=(i*17+i/251)&255;
    auto& rx=rx_data_info[0];
    rx.sgdma_tx.mem_vir_base=memory.data();
    rx.sgdma_tx.map_size=memory.size();
    auto* rb=ringbuffer_create(16*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
    assert(rb);
    rx.data_stream.dma_data_rb=rb;
    image_frame_info_t frame={};
    frame.frame_size=memory.size();
    rx_queue_publish(rb,&frame);
    unsigned char* payload=nullptr;
    assert(rx_sgdma_data_get(0,&payload)==(int)memory.size());
    assert(payload==memory.data() && ringbuffer_len(rb)==1);

    int sockets[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    network::TcpSocket sender(sockets[0]);
    sender.SetSendBUfSize(1024);
    Response header={};
    header.cmd_code=htonl(UP_CODE);
    header.index=htons(0x03);
    header.length=htonl(4+memory.size());
    uint32_t channel=htonl(0);
    iovec packet[]={{&header,sizeof(header)},{&channel,4},{payload,memory.size()}};
    std::vector<unsigned char> received(sizeof(header)+4+memory.size());
    std::thread reader([&] {
        size_t count=0;
        while(count<received.size()) {
            ssize_t n=read(sockets[1],received.data()+count,
                           (std::min)(size_t(777),received.size()-count));
            assert(n>0);
            count+=n;
        }
    });
    assert(sender.SendVectorFully(packet,3));
    reader.join();
    assert(memcmp(received.data(),&header,sizeof(header))==0);
    assert(memcmp(received.data()+sizeof(header),&channel,4)==0);
    assert(memcmp(received.data()+sizeof(header)+4,memory.data(),memory.size())==0);
    assert(ringbuffer_len(rb)==1);
    rx_sgdma_data_release(0);
    assert(rx_sgdma_data_get(0,&payload)==0);
    sender.Close();
    close(sockets[1]);

    rx_queue_publish(rb,&frame);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    network::TcpSocket disconnected(sockets[0]);
    close(sockets[1]);
    assert(!disconnected.SendVectorFully(packet,3));
    assert(rx_sgdma_data_get(0,&payload)==(int)memory.size());
    assert(payload==memory.data());
    rx_sgdma_data_release(0);
    disconnected.Close();
    ringbuffer_deinit(rb);
    puts("9014 vector tests passed: wire format, partial writes, mapped pointer ownership, disconnected frame retention");
}
