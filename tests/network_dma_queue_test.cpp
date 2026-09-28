#include <cassert>
#include "../src/system/system.cpp"
struct EndWorker {};
static unsigned submitted, waits, acknowledgements;
extern "C" void mm2s_dma_enable(struct sgdma_info*) {}
extern "C" void mm2s_dma_disable(struct sgdma_info*) { ++acknowledgements; }
extern "C" void push_mm2s_dma(struct sgdma_info*,unsigned int offset,int size) {
    assert(offset==submitted*RECV_DMA_DATA_SIZE && size==RECV_DMA_DATA_SIZE);
    assert(network_blocks[submitted]==BLOCK_IN_FLIGHT);
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
    static unsigned char memory[NETWORK_RX_RING_SIZE+16];
    memset(memory,0xa5,sizeof(memory));
    pthread_mutex_init(&data_mutex[0],NULL);
    data_info[0].sgdma_tx.mem_vir_base=memory;
    data_info[0].sgdma_tx.map_size=NETWORK_RX_RING_SIZE;
    NetworkDescriptor descriptor;
    assert(!network_dma_dequeue(&descriptor));
    fill(RECV_DMA_DATA_SIZE-1,1);
    assert(!network_dma_dequeue(&descriptor));
    fill(1,1);
    for(unsigned i=1;i<16;++i) fill(RECV_DMA_DATA_SIZE,i+1);
    unsigned char* address; unsigned length;
    assert(network_count==16 && network_dma_reserve(&address,&length)==1);
    assert(network_dma_dequeue(&descriptor) && descriptor.offset==0);
    assert(network_dma_reserve(&address,&length)==1); // queued is not free until completion
    assert(!network_dma_dequeue(&descriptor));
    network_dma_release(descriptor);
    fill(RECV_DMA_DATA_SIZE,42);
    assert(network_count==16 && network_dma_reserve(&address,&length)==1);
    sgdma_mm2s_stop(0);
    assert(!network_dma_dequeue(&descriptor) && network_dma_reserve(&address,&length)==1);
    sgdma_mm2s_start(0);
    for(unsigned i=1;i<=16;++i) {
        assert(network_dma_dequeue(&descriptor));
        unsigned block=i%16;
        assert(descriptor.offset==block*RECV_DMA_DATA_SIZE && descriptor.length==RECV_DMA_DATA_SIZE);
        for(unsigned j=0;j<RECV_DMA_DATA_SIZE;++j)
            assert(memory[descriptor.offset+j]==(block ? block+1 : 42));
        network_dma_release(descriptor);
    }
    fill(123,9); network_dma_abort_partial();
    assert(network_count==0 && network_dma_reserve(&address,&length)==0);
    assert(address==memory+RECV_DMA_DATA_SIZE);
    network_dma_abort_partial();
    // Test production DMA worker timeout ownership and fixed-size submissions.
    network_cursor=0; network_head=network_tail=0;
    fill(RECV_DMA_DATA_SIZE,1); fill(RECV_DMA_DATA_SIZE,2);
    try { sgdma_network_queue_pthread(NULL); } catch(const EndWorker&) {}
    assert(submitted==2 && waits==2 && acknowledgements==1);
    assert(network_blocks[0]==BLOCK_FREE && network_blocks[1]==BLOCK_IN_FLIGHT);
    for(unsigned i=NETWORK_RX_RING_SIZE;i<sizeof(memory);++i) assert(memory[i]==0xa5);
    puts("DMA offset queue tests passed: FIFO, partial blocks, backpressure, wrap, stop/start, timeout ownership");
}
