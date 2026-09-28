#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <termios.h>
#include <poll.h>
#include <sched.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <memory.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include <stdarg.h>
#include "../src/hardware/zmuav_wrmem.h"
static int descriptors, mappings, fail_mapping, fail_mem_open, fail_alloc;
static unsigned long physical_next=0x10000000;
struct fd_record { int type, allocated; unsigned size; unsigned long physical; };
static struct fd_record fds[16];
struct map_record { void* pointer; off64_t physical; };
static struct map_record maps[16];
static int mock_open(const char* path,int flags,...) {
    int type = strcmp(path,"/dev/zmuav_wrmem")==0 ? 1 : strcmp(path,"/dev/mem")==0 ? 2 : 3;
    if(type==2) {
        assert((flags & O_SYNC)==O_SYNC);
        if(fail_mem_open) { errno=EACCES; return -1; }
    }
    for(int fd=0;fd<16;++fd) if(!fds[fd].type) {
        fds[fd]=(struct fd_record){.type=type}; ++descriptors; return fd;
    }
    abort();
}
static int mock_close(int fd) {
    assert(fd>=0 && fds[fd].type && !fds[fd].allocated);
    memset(&fds[fd],0,sizeof(fds[fd])); --descriptors; return 0;
}
static int mock_ioctl(int fd,unsigned long request,...) {
    assert(fd>=0 && fds[fd].type==1);
    if(request==AXIS_FIFO_FREE_MALLOC_PHY) {
        assert(fds[fd].allocated);
        for(int i=0;i<16;++i) assert(!maps[i].pointer || maps[i].physical!=fds[fd].physical);
        fds[fd].allocated=0; return 0;
    }
    va_list args; va_start(args,request); void* value=va_arg(args,void*); va_end(args);
    if(request==AXIS_FIFO_SET_MALLOC_SIZE) {
        if(fail_alloc) { errno=ENOMEM; return -1; }
        fds[fd].allocated=1; fds[fd].size=*(unsigned*)value;
        fds[fd].physical=physical_next; physical_next+=0x04000000;
    } else if(request==AXIS_FIFO_GET_MALLOC_SIZE) *(unsigned*)value=fds[fd].size;
    else if(request==AXIS_FIFO_GET_MALLOC_PHY) *(unsigned long*)value=fds[fd].physical;
    else abort();
    return 0;
}
static void* mock_mmap(void* addr,size_t size,int prot,int flags,int fd,off64_t offset) {
    assert(fds[fd].type==2 && offset%4096==0);
    if(fail_mapping) { errno=ENOMEM; return MAP_FAILED; }
    for(int i=0;i<16;++i) if(!maps[i].pointer) {
        maps[i]=(struct map_record){malloc(size),offset}; ++mappings; return maps[i].pointer;
    }
    abort();
}
static int mock_munmap(void* ptr,size_t size) {
    for(int i=0;i<16;++i) if(maps[i].pointer==ptr) {
        free(ptr); maps[i].pointer=NULL; --mappings; return 0;
    }
    abort();
}
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define mmap64 mock_mmap
#define munmap mock_munmap
#include "../src/hardware/dma_buffer_mem.c"
#include "../src/hardware/dma_utils.c"
#include "../src/system/1553B_engine.c"
#undef open
#undef close
#undef ioctl
#undef mmap64
#undef munmap
int main(void) {
    unsigned char* memory; unsigned physical;
    assert(dma_buffer_map(DMA_BUFFER_TX,4096,&memory,&physical)==0);
    assert(physical==0x10000000 && descriptors==1 && mappings==1);
    assert(fds[0].allocated); // fd 0 retained through mapping lifetime
    unsigned char* duplicate; unsigned ignored;
    assert(dma_buffer_map(DMA_BUFFER_TX,4096,&duplicate,&ignored)!=0);
    assert(dma_buffer_unmap(memory)==0 && descriptors==0 && mappings==0);
    fail_alloc=1;
    assert(dma_buffer_map(DMA_BUFFER_TX,4096,&memory,&physical)!=0);
    assert(descriptors==0 && mappings==0); fail_alloc=0;
    fail_mem_open=1;
    assert(dma_buffer_map(DMA_BUFFER_TX,4096,&memory,&physical)!=0);
    assert(descriptors==0 && mappings==0); fail_mem_open=0;
    fail_mapping=1;
    assert(dma_buffer_map(DMA_BUFFER_TX,4096,&memory,&physical)!=0);
    assert(descriptors==0 && mappings==0); fail_mapping=0;
    physical_next=0x80000000;
    struct dma_addr_info registers={0xa2001000,4096};
    struct sgdma_info dma;
    assert(sgdma_init(0,"/dev/zmuav_pl2ps_irq_1",&registers,34*1024*1024,&dma)==0);
    assert(dma.mem_phy_addr==0x80000000 && descriptors==2 && mappings==2);
    assert(init_1553B()==0 && descriptors==3 && mappings==3);
    unsigned char input[16],output[16]; memset(input,0x5a,16);
    assert(write_1553B(32,input,16)==0 && read_1553B(32,output,16)==0);
    assert(memcmp(input,output,16)==0);
    assert(read_1553B(UINT32_MAX,output,16)==-1);
    assert(close_1553B()==0 && close_1553B()==0);
    sgdma_exit(&dma); sgdma_exit(&dma);
    assert(descriptors==0 && mappings==0);
    puts("allocated /dev/mem tests passed: allocator lifetime, separate mapping, failures, fd=0, shared cleanup, high addresses");
}
