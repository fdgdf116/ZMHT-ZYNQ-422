#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#define private public
#include "network_main.h"
#undef private
#include "../src/system/system.cpp"
#undef min
#undef max
#include "../src/hardware/fifo_engine.cpp"
#include "../src/network/network_main.cpp"

static unsigned resets = 0, syncs = 0, fifo_resets = 0;
static bool fail_reset = false;
extern "C" int reset_dma_channel(sgdma_info* dma, unsigned int offset) {
    assert(buffer_clear_requested() && buffer_control().active == 0);
    assert(dma == &data_info[0].sgdma_tx || dma == &rx_data_info[0].sgdma_tx);
    assert(offset == (dma == &data_info[0].sgdma_tx ? 0u : 0x30u));
    ++resets;
    return fail_reset ? -1 : 0;
}
extern "C" int do_sync(int fd, unsigned long cmd, uint32_t off, uint32_t bytes, uint64_t*) {
    assert(resets >= 2 && buffer_clear_requested() && off == 0);
    auto& dma = fd == 42 ? data_info[0].sgdma_tx : rx_data_info[0].sgdma_tx;
    assert(bytes == dma.map_size);
    if(cmd == AXIS_FIFO_SYNC_FOR_DEVICE)
        for(unsigned i = 0; i < bytes; ++i) assert(dma.mem_vir_base[i] == 0);
    else assert(cmd == AXIS_FIFO_SYNC_FOR_CPU && fd == 43);
    ++syncs;
    return 0;
}
extern "C" void stream_fifo_reset(stream_fifo_reg_t*) {
    assert(buffer_clear_requested() && buffer_control().active == 0);
    ++fifo_resets;
}
static void initialize_fifo(fifo_data_info_t& f) {
    f.map_size = 5 * 512;
    f.data = static_cast<unsigned char*>(malloc(f.map_size));
    memset(f.data, 0xa5, f.map_size);
    f.fifo_data_rb = ringbuffer_create(4*sizeof(image_frame_info_t), sizeof(image_frame_info_t));
    image_frame_info_t frame = {}; frame.frame_size = 17;
    ringbuffer_put(f.fifo_data_rb, &frame, sizeof(frame));
    f.ringbuf_offset = 512; f.ringbuf_count = 1;
}
static void request(network::NetServer& server, int peer, unsigned length, uint32_t token,
                    uint32_t expected) {
    struct { Request head; uint32_t token; } req = {};
    req.head.length = length; // ServiceLoop already converts this field.
    req.head.index = htons(0xff06);
    req.head.count = htons(0x1234);
    req.token = htonl(token);
    server.ProcMessage(reinterpret_cast<char*>(&req));
    struct { Response head; uint32_t result; } response = {};
    ssize_t got = recv(peer, &response, sizeof(response), MSG_DONTWAIT);
    assert(got == sizeof(response));
    assert(ntohl(response.head.cmd_code) == RES_CODE);
    assert(ntohl(response.head.length) == 4 && ntohs(response.head.index) == 0xff06);
    assert(ntohs(response.head.count) == 0x1234 && ntohl(response.result) == expected);
}
int main() {
    pthread_mutex_init(&data_mutex[0], nullptr);
    std::vector<unsigned char> tx(NETWORK_RX_RING_SIZE, 0xa5), rx(DMA_SIZE_MAX_L, 0x5a);
    data_info[0].sgdma_tx.mem_vir_base = tx.data();
    data_info[0].sgdma_tx.map_size = tx.size(); data_info[0].sgdma_tx.mem_fd = 42;
    rx_data_info[0].sgdma_tx.mem_vir_base = rx.data();
    rx_data_info[0].sgdma_tx.map_size = rx.size(); rx_data_info[0].sgdma_tx.mem_fd = 43;
    for(auto* info : {&data_info[0], &rx_data_info[0]}) {
        info->data_stream.dma_data_rb = ringbuffer_create(16*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
        info->ringbuf_offset = RECV_DMA_DATA_SIZE;
    }
    g_axififo_info = static_cast<axififo_info_t*>(calloc(1, sizeof(axififo_info_t)));
    config_reg_t regs[FIFO_NUM] = {};
    for(int ch=0; ch<FIFO_NUM; ++ch) {
        g_axififo_info->fifo_reg[ch].cfg = &regs[ch];
        initialize_fifo(g_axififo_info->fifo_data_info[ch]);
        initialize_fifo(g_axififo_info->fifo_tx_data_info[ch]);
    }
    unsigned char* address; unsigned length;
    assert(network_dma_reserve(&address, &length) == 0);
    network_dma_received(123); // Partial TCP payload must be discarded too.
    assert(rx_cache_reserve());
    image_frame_info_t frame = {}; frame.frame_size = 4;
    rx_cache_submit(frame);
    network_ready_count = 1; network_dma_busy = true;
    data_info[0].dma_stop_flag = 1; // Preserve a deliberately stopped TX channel.
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    network::NetServer server;
    server.client_ = new network::TcpSocket(sockets[0]);
    request(server, sockets[1], 4, 0, 0);
    request(server, sockets[1], 3, 0xAAAAAAAA, 0);
    assert(resets == 0 && tx[0] == 0xa5 && !buffer_clear_requested());

    int downlink[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, downlink) == 0);
    server.client_data_ = new network::TcpSocket(downlink[0]);
    network::buffer_data_server.store(&server);

    // Clearing waits until a buffer owner has finished, and denies new owners.
    std::atomic<bool> held{false}, release{false};
    std::thread owner([&] {
        BufferWork work; assert(work); held = true;
        while(!release.load()) std::this_thread::yield();
    });
    while(!held.load()) std::this_thread::yield();
    std::thread unlocker([&] {
        while(!buffer_clear_requested()) std::this_thread::yield();
        BufferWork denied; assert(!denied);
        assert(resets == 0 && tx[0] == 0xa5);
        release = true;
    });
    request(server, sockets[1], 4, 0xAAAAAAAA, 0xBBBBBBBB);
    owner.join(); unlocker.join();
    char byte;
    assert(recv(downlink[1], &byte, 1, MSG_DONTWAIT) == 0);
    delete server.client_data_; server.client_data_ = nullptr;
    close(downlink[1]); network::buffer_data_server.store(nullptr);
    assert(resets == 2 && syncs == 3 && fifo_resets == FIFO_NUM);
    assert(!buffer_clear_requested() && buffer_epoch() == 1);
    for(auto v : tx) assert(v == 0);
    for(auto v : rx) assert(v == 0);
    assert(network_count == 0 && network_ready_count == 0 && !network_dma_busy);
    assert(network_filling == -1 && network_filled == 0 && network_cursor == 0);
    assert(rx_cache_count == 0 && rx_cache_outstanding == 0);
    assert(data_info[0].dma_stop_flag == 1);
    for(int ch=0; ch<FIFO_NUM; ++ch) {
        for(auto* f : {&g_axififo_info->fifo_data_info[ch], &g_axififo_info->fifo_tx_data_info[ch]}) {
            assert(ringbuffer_is_empty(f->fifo_data_rb) && f->ringbuf_offset == 0);
            for(unsigned i=0; i<f->map_size; ++i) assert(f->data[i] == 0);
        }
    }
    // Repeated clears work; hardware reset failure cannot return success.
    request(server, sockets[1], 4, 0xAAAAAAAA, 0xBBBBBBBB);
    fail_reset = true;
    request(server, sockets[1], 4, 0xAAAAAAAA, 0);
    assert(buffer_clear_requested() && buffer_epoch() == 2);
    fail_reset = false;
    request(server, sockets[1], 4, 0xAAAAAAAA, 0xBBBBBBBB);
    assert(!buffer_clear_requested() && buffer_epoch() == 3);
    delete server.client_; close(sockets[1]);
    puts("Buffer clear tests passed: command dispatch, token/count/response, quiescence, zeroing, queues, cache order, repeat and failure recovery");
}
