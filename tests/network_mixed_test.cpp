#include <cassert>
#include <thread>
#include <vector>
#include <map>
#include <iostream>
#include <atomic>
#include <string>
#include <sys/mman.h>
#include <fcntl.h>
#define private public
#define protected public
#include "network_main.h"
#include "sync_send.h"
#undef protected
#undef private
// Only hardware VFIFO registers are simulated. DMA/FIFO software queues,
// protocol parsing, capacity serialization and both network loops are real.
#define sgdma_vfifo_query_capacity hardware_vfifo_query_capacity
#include "../src/system/system.cpp"
#undef sgdma_vfifo_query_capacity
#undef min
#undef max
extern "C" int sgdma_vfifo_query_capacity(Cache_report_inf_t* capacity) {
    for(int i=0; i<FREE_SPACE_NUM_MAX; ++i) capacity[i].vfifo_space=0x4000000-i*4;
    return 0;
}
#include "../src/hardware/fifo_engine.cpp"
#include "../src/network/network_main.cpp"
#include "../src/network/sync_send.cpp"

static std::vector<unsigned char> hardware_rx;
static unsigned hardware_offset;
extern "C" unsigned int stream_fifo_read_data_len(stream_fifo_reg_t*) {
    return hardware_rx.size() - hardware_offset;
}
extern "C" unsigned int stream_fifo_read_data(stream_fifo_reg_t*, unsigned char* data, int size) {
    assert(hardware_offset + size <= hardware_rx.size());
    memcpy(data, hardware_rx.data() + hardware_offset, size);
    hardware_offset += size;
    return size;
}

static void init_fifo(fifo_data_info_t& fifo, unsigned slots) {
    fifo.map_size=(slots+1)*512;
    fifo.data=(unsigned char*)calloc(1,fifo.map_size);
    fifo.fifo_data_rb=ringbuffer_create(slots*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
    assert(fifo.data && fifo.fifo_data_rb);
}
static unsigned port_of(network::TcpSocket* socket) {
    sockaddr_in address={}; socklen_t size=sizeof(address);
    assert(getsockname(socket->GetSocketId(),(sockaddr*)&address,&size)==0);
    return ntohs(address.sin_port);
}
static void record(FILE* file, const void* data, unsigned size) {
    assert(fwrite(&size,4,1,file)==1);
    assert(fwrite(data,1,size,file)==size);
    fflush(file);
}
int main(int argc, char** argv) {
    assert(argc==2);
    std::string path=argv[1];
    pthread_mutex_init(&data_mutex[0],nullptr);
    data_info[0].sgdma_tx.mem_vir_base=(unsigned char*)calloc(1,NETWORK_RX_RING_SIZE);
    data_info[0].sgdma_tx.map_size=NETWORK_RX_RING_SIZE;
    g_axififo_info=(axififo_info_t*)calloc(1,sizeof(axififo_info_t));
    for(int ch=0; ch<FIFO_NUM; ++ch) {
        init_fifo(g_axififo_info->fifo_tx_data_info[ch],4);
        init_fifo(g_axififo_info->fifo_data_info[ch],128);
        // More than one burst, so DMA must run even with FIFO data pending.
        for(unsigned i=0; i<70; ++i) {
            unsigned char payload[17];
            memset(payload,(ch*100+i)&255,sizeof(payload));
            fifo_memcpy_data(ch,payload,sizeof(payload));
        }
    }
    // Verify an RDFO snapshot larger than both the old 1024-byte stack buffer
    // and a 508-byte queue payload survives intact and drains in one pass.
    hardware_rx.resize(2501);
    for(unsigned i=0; i<hardware_rx.size(); ++i) hardware_rx[i]=i&255;
    axififo_receive_pending(15);
    assert(hardware_offset==hardware_rx.size());
    // Remove the preloaded upload frames, then validate the hardware burst.
    auto& hardware_fifo=g_axififo_info->fifo_data_info[15];
    for(unsigned i=0; i<70; ++i) {
        unsigned char* data=nullptr;
        assert(rx_fifo_data_get(15,&data)==17);
    }
    std::vector<unsigned char> received;
    while(!ringbuffer_is_empty(hardware_fifo.fifo_data_rb)) {
        unsigned char* data=nullptr;
        int size=rx_fifo_data_get(15,&data);
        assert(size>0 && size<=508);
        received.insert(received.end(),data,data+size);
    }
    assert(received==hardware_rx);
    for(unsigned i=0; i<70; ++i) {
        unsigned char payload[17];
        memset(payload,(15*100+i)&255,sizeof(payload));
        fifo_memcpy_data(15,payload,sizeof(payload));
    }
    auto& rx=rx_data_info[0];
    rx.sgdma_tx.mem_vir_base=(unsigned char*)calloc(17,RECV_DMA_DATA_SIZE);
    rx.sgdma_tx.map_size=17*RECV_DMA_DATA_SIZE;
    rx.data_stream.dma_data_rb=ringbuffer_create(16*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
    for(unsigned i=0; i<2; ++i) {
        image_frame_info_t frame={};
        frame.frame_offset=i*RECV_DMA_DATA_SIZE;
        frame.frame_size=i ? 1024 : RECV_DMA_DATA_SIZE;
        memset(rx.sgdma_tx.mem_vir_base+frame.frame_offset,0xd0+i,frame.frame_size);
        pcie_data_to_queue(rx.data_stream.dma_data_rb,&frame);
    }
    FILE* dma_log=fopen((path+".dma").c_str(),"wb");
    FILE* fifo_log[FIFO_NUM];
    for(int ch=0; ch<FIFO_NUM; ++ch) fifo_log[ch]=fopen((path+".fifo"+std::to_string(ch)).c_str(),"wb");
    std::thread([&] {
        while(true) {
            NetworkDescriptor descriptor;
            if(network_dma_dequeue(&descriptor)) {
                record(dma_log,data_info[0].sgdma_tx.mem_vir_base+descriptor.offset,descriptor.length);
                network_dma_release(descriptor);
            }
            if(access((path+".pause_fifo").c_str(),F_OK)!=0) {
                for(int ch=0; ch<FIFO_NUM; ++ch) {
                    auto& fifo=g_axififo_info->fifo_tx_data_info[ch];
                    if(!ringbuffer_is_empty(fifo.fifo_data_rb)) {
                        image_frame_info_t frame={};
                        ringbuffer_get(fifo.fifo_data_rb,&frame,sizeof(frame));
                        record(fifo_log[ch],fifo.data+frame.frame_offset,frame.frame_size);
                    }
                }
            }
            usleep(1000);
        }
    }).detach();
    network::NetServer server;
    server.server_=new network::TcpSocket;
    assert(server.server_->CreateServer(0,"127.0.0.1")==0);
    auto* upload=network::SyncSend::GetInstance();
    upload->server_=new network::TcpSocket;
    assert(upload->server_->CreateServer(0,"127.0.0.1")==0);
    std::thread([&] { server.DaemodLoop(nullptr); }).detach();
    std::thread([&] { upload->DaemodLoop(nullptr); }).detach();
    printf("READY %u %u\n",port_of(server.server_),port_of(upload->server_));
    fflush(stdout);
    server.DataServiceLoop(nullptr);
}
