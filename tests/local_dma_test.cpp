#include <cassert>
#include "../src/system/system.cpp"
struct EndWorker {};
static unsigned int submissions, completions, waits, active_offset;
static unsigned char snapshot[SYNTHETIC_TX_SIZE];
static void check_frame(const unsigned char* p, uint64_t count)
{
    const unsigned char sync[] = {0x49,0x96,0x02,0xd2,0,0,3,0xc0};
    const unsigned char tail[] = {0x49,0x95,1,0xd1};
    assert(memcmp(p,sync,8)==0 && memcmp(p+60,tail,4)==0);
    for(unsigned int offset : {8u,24u})
        for(unsigned int j=0;j<8;++j) assert(p[offset+j]==((count>>(56-8*j))&255));
    for(unsigned int i=16;i<24;++i) assert(p[i]==0);
    for(unsigned int i=32;i<60;++i) assert(p[i]==0);
    for(unsigned int i=0;i<960;++i) assert(p[64+i]==static_cast<unsigned char>(i));
}
extern "C" void mm2s_dma_enable(struct sgdma_info*) {}
extern "C" void mm2s_dma_disable(struct sgdma_info*) { ++completions; }
extern "C" void push_mm2s_dma(struct sgdma_info* dma,unsigned int offset,int size)
{
    assert(size==2*1024*1024 && offset==(submissions%2)*SYNTHETIC_TX_SIZE);
    assert(submissions==completions && synthetic_tx_busy[0]);
    for(unsigned int i=0;i<2048;++i) check_frame(dma->mem_vir_base+offset+i*1024,submissions*2048+i);
    active_offset=offset;
    memcpy(snapshot,dma->mem_vir_base+offset,sizeof(snapshot));
    if(++submissions==3) throw EndWorker();
}
extern "C" int SelectBlock(struct sgdma_info* dma)
{
    assert(memcmp(snapshot,dma->mem_vir_base+active_offset,sizeof(snapshot))==0);
    for(unsigned int i=0;i<2048;++i)
        check_frame(dma->mem_vir_base+SYNTHETIC_TX_SIZE-active_offset+i*1024,submissions*2048+i);
    return ++waits==1 ? 0 : 1;
}
int main()
{
    static unsigned char memory[2*SYNTHETIC_TX_SIZE+16];
    memset(memory,0xa5,sizeof(memory));
    pthread_mutex_init(&data_mutex[0],NULL);
    data_info[0].sgdma_tx.mem_vir_base=memory;
    data_info[0].sgdma_tx.map_size=2*SYNTHETIC_TX_SIZE;
    unsigned int channel=0;
    try { sgdma_synthetic_tx_pthread(&channel); } catch(const EndWorker&) {}
    assert(submissions==3 && completions==2 && waits==3);
    for(unsigned int i=2*SYNTHETIC_TX_SIZE;i<sizeof(memory);++i) assert(memory[i]==0xa5);
    init_synthetic_dma_block(memory);
    uint64_t count=0xffffffffULL;
    update_synthetic_dma_counters(memory,count);
    for(unsigned int i=0;i<2048;++i) check_frame(memory+i*1024,0xffffffffULL+i);
    memset(memory,0xa5,SYNTHETIC_TX_SIZE);
    update_synthetic_dma_counters(memory,count);
    for(unsigned int i=0;i<SYNTHETIC_TX_SIZE;++i) {
        unsigned int offset=i%1024;
        if((offset>=8 && offset<16)||(offset>=24 && offset<32)) continue;
        assert(memory[i]==0xa5);
    }
    puts("Local 2 MiB ping-pong DMA tests passed");
}
