#include <thread>
#include <iostream>
#include <map>
#include <cassert>
#include <sys/mman.h>
#include <fcntl.h>
#define private public
#include "../src/network/network_main.h"
#undef private
#include "../src/network/network_main.cpp"
#include "../src/system/system.cpp"
static unsigned char* mapped;

int main(int argc, char** argv) {
    assert(argc == 2);
    int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0 && ftruncate(fd, NETWORK_RX_RING_SIZE + 16) == 0);
    mapped = static_cast<unsigned char*>(mmap(NULL, NETWORK_RX_RING_SIZE + 16,
                                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(mapped != MAP_FAILED);
    memset(mapped, 0xa5, NETWORK_RX_RING_SIZE + 16);
    pthread_mutex_init(&data_mutex[0],NULL);
    data_info[0].sgdma_tx.mem_vir_base=mapped;
    data_info[0].sgdma_tx.map_size=NETWORK_RX_RING_SIZE;
    // Emulate completion only after dequeuing; real TCP uses production queue APIs.
    std::thread([] {
        while(true) {
            NetworkDescriptor descriptor;
            if(network_dma_dequeue(&descriptor)) network_dma_release(descriptor);
            else usleep(100);
        }
    }).detach();
    network::NetServer server;
    server.server_ = new network::TcpSocket;
    assert(server.server_->CreateServer(0, "127.0.0.1") == 0);
    sockaddr_in address = {};
    socklen_t size = sizeof(address);
    assert(getsockname(server.server_->GetSocketId(), reinterpret_cast<sockaddr*>(&address), &size) == 0);
    printf("READY %u\n", ntohs(address.sin_port));
    fflush(stdout);
    server.DataServiceLoop(nullptr);
}
