#include <cassert>
#include <glob.h>
#include <sys/stat.h>
#include "../src/system/system.cpp"
struct EndWorker {};
static unsigned submitted, waits, acknowledgements;
static unsigned sync_calls, synced;
extern "C" int do_sync(int fd, unsigned long cmd, uint32_t offset,
                        uint32_t bytes, uint64_t* cost_us) {
    assert(fd == 42 && cmd == AXIS_FIFO_SYNC_FOR_DEVICE && cost_us == NULL);
    assert(offset == submitted * NETWORK_TX_BLOCK_SIZE && bytes == NETWORK_TX_BLOCK_SIZE);
    assert(network_blocks[submitted] == BLOCK_IN_FLIGHT);
    assert(pthread_mutex_trylock(&data_mutex[0]) == 0);
    pthread_mutex_unlock(&data_mutex[0]);
    if(++sync_calls == 1) { errno = EIO; return -1; }
    ++synced;
    return 0;
}
extern "C" void mm2s_dma_enable(struct sgdma_info*) { assert(synced == submitted + 1); }
extern "C" void mm2s_dma_disable(struct sgdma_info*) { ++acknowledgements; }
extern "C" void push_mm2s_dma(struct sgdma_info*,unsigned int offset,int size) {
    assert(offset==submitted*NETWORK_TX_BLOCK_SIZE && size==NETWORK_TX_BLOCK_SIZE);
    assert(network_blocks[submitted]==BLOCK_IN_FLIGHT);
    // Verify the whole range was saved before the actual DMA submission.
    glob_t paths = {};
    assert(glob(NETWORK_DMA_CAPTURE_ROOT "/dma_tx_capture_*/part_000000.bin", 0, NULL, &paths) == 0);
    assert(paths.gl_pathc == 1);
    int fd = open(paths.gl_pathv[0], O_RDONLY);
    assert(fd >= 0);
    struct stat info;
    assert(fstat(fd, &info) == 0);
    assert(info.st_size == (submitted + 1) * NETWORK_TX_BLOCK_SIZE);
    unsigned char saved[4096];
    for(unsigned int position = 0; position < NETWORK_TX_BLOCK_SIZE; position += sizeof(saved)) {
        assert(pread(fd, saved, sizeof(saved), offset + position) == sizeof(saved));
        for(unsigned int i = 0; i < sizeof(saved); ++i) assert(saved[i] == submitted + 1);
    }
    close(fd);
    globfree(&paths);
    if(++submitted==2) throw EndWorker();
}
extern "C" int SelectBlock(struct sgdma_info*) {
    assert(network_dma_busy && network_blocks[0]==BLOCK_IN_FLIGHT);
    return ++waits==1 ? 0 : 1;
}
static void fill(unsigned bytes,unsigned char value) {
    while(bytes) {
        unsigned char* address; unsigned length;
        assert(network_dma_reserve(&address,&length)==0);
        if(length>bytes) length=bytes;
        memset(address,value,length);
        network_dma_received(length);
        bytes-=length;
    }
}
int main() {
    static_assert(NETWORK_TX_BLOCK_SIZE == 2u * 1024u * 1024u, "9016 sends 2 MiB blocks");
    static_assert(NETWORK_DMA_BLOCKS == 16, "TX ring has 16 blocks");
    static_assert(RECV_DMA_DATA_SIZE == 2 * 1024 * 1024, "RX remains 2 MiB");
    static unsigned char memory[NETWORK_RX_RING_SIZE+16];
    memset(memory,0xa5,sizeof(memory));
    pthread_mutex_init(&data_mutex[0],NULL);
    data_info[0].sgdma_tx.mem_vir_base=memory;
    data_info[0].sgdma_tx.mem_fd=42;
    data_info[0].sgdma_tx.map_size=NETWORK_RX_RING_SIZE;
    NetworkDescriptor descriptor;
    assert(!network_dma_dequeue(&descriptor));
    fill(NETWORK_TX_BLOCK_SIZE-1,1);
    assert(!network_dma_dequeue(&descriptor));
    fill(1,1);
    for(unsigned i=1;i<NETWORK_DMA_BLOCKS;++i) fill(NETWORK_TX_BLOCK_SIZE,i+1);
    unsigned char* address; unsigned length;
    assert(network_count==NETWORK_DMA_BLOCKS && network_dma_reserve(&address,&length)==1);
    assert(network_dma_dequeue(&descriptor) && descriptor.offset==0);
    assert(network_dma_reserve(&address,&length)==1); // queued is not free until completion
    assert(!network_dma_dequeue(&descriptor));
    network_dma_release(descriptor);
    fill(NETWORK_TX_BLOCK_SIZE,42);
    assert(network_count==NETWORK_DMA_BLOCKS && network_dma_reserve(&address,&length)==1);
    sgdma_mm2s_stop(0);
    assert(!network_dma_dequeue(&descriptor) && network_dma_reserve(&address,&length)==1);
    sgdma_mm2s_start(0);
    for(unsigned i=1;i<=NETWORK_DMA_BLOCKS;++i) {
        assert(network_dma_dequeue(&descriptor));
        unsigned block=i%NETWORK_DMA_BLOCKS;
        assert(descriptor.offset==block*NETWORK_TX_BLOCK_SIZE && descriptor.length==NETWORK_TX_BLOCK_SIZE);
        for(unsigned j=0;j<NETWORK_TX_BLOCK_SIZE;++j)
            assert(memory[descriptor.offset+j]==(block ? block+1 : 42));
        network_dma_release(descriptor);
    }
    fill(123,9); network_dma_abort_partial();
    assert(network_count==0 && network_dma_reserve(&address,&length)==0);
    assert(address==memory+NETWORK_TX_BLOCK_SIZE);
    network_dma_abort_partial();
    // Test production DMA worker timeout ownership and fixed-size submissions.
    network_cursor=0; network_head=network_tail=0;
    fill(NETWORK_TX_BLOCK_SIZE,1); fill(NETWORK_TX_BLOCK_SIZE,2);
    try { sgdma_network_queue_pthread(NULL); } catch(const EndWorker&) {}
    assert(submitted==2 && waits==2 && acknowledgements==1);
    assert(sync_calls == 3 && synced == 2);
    assert(network_blocks[0]==BLOCK_FREE && network_blocks[1]==BLOCK_IN_FLIGHT);
    for(unsigned i=NETWORK_RX_RING_SIZE;i<sizeof(memory);++i) assert(memory[i]==0xa5);
    // A write failure must propagate instead of reporting capture success.
    {
        NetworkDmaCapture failed;
        strcpy(failed.directory, "/tmp");
        failed.fd = open("/dev/null", O_RDONLY);
        assert(failed.fd >= 0);
        assert(!network_dma_capture(failed, memory, NETWORK_TX_BLOCK_SIZE));
    }
    puts("DMA offset queue tests passed: FIFO, partial blocks, backpressure, wrap, stop/start, timeout ownership");
}
