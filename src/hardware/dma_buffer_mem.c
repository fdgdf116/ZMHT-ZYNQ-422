#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include "dma_buffer_mem.h"
#include "common.h"
#include "zmuav_wrmem.h"
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
struct allocation {
    unsigned char* address;
    unsigned int length;
    unsigned int physical;
    int owner_fd;
    int malloced;
};
static struct allocation allocations[DMA_BUFFER_COUNT];

static void release_owner(int fd)
{
    if(ioctl(fd, AXIS_FIFO_FREE_MALLOC_PHY) != 0)
        perror("wrmem free allocation");
    close(fd);
}

int dma_buffer_map(enum dma_buffer_region region, unsigned int bytes,
                   unsigned char** address, unsigned int* physical)
{
    unsigned int capacity = 0;
    unsigned long base = 0; // Matches AXIS_FIFO_GET_MALLOC_PHY's ioctl ABI.
    int allocated = 0;
    if(!address || !physical || region < 0 || region >= DMA_BUFFER_COUNT || !bytes) return -1;
    *address = NULL;
    *physical = 0;
    if(allocations[region].address) {
        fprintf(stderr, "DMA buffer region %d already mapped\n", region);
        return -1;
    }
    int owner = open(ZMUAV_WRMEM_DEVICE_NAME, O_RDWR);
    if(owner < 0) { perror("open wrmem allocator"); return -1; }
    if(ioctl(owner, AXIS_FIFO_SET_MALLOC_SIZE, &bytes) != 0) {
        perror("wrmem allocate"); goto fail;
    }
    allocated = 1;
    if(ioctl(owner, AXIS_FIFO_GET_MALLOC_SIZE, &capacity) != 0 ||
       ioctl(owner, AXIS_FIFO_GET_MALLOC_PHY, &base) != 0) {
        perror("wrmem query allocation"); goto fail;
    }
    long page = sysconf(_SC_PAGESIZE);
    if(page <= 0 || !base || base > UINT32_MAX || capacity < bytes ||
       (uint64_t)base + capacity > (UINT64_C(1) << 32) || base % page) {
        fprintf(stderr, "wrmem returned invalid physical range: base=0x%lx size=%u requested=%u\n", base, capacity, bytes);
        goto fail;
    }
    for(unsigned int i = 0; i < DMA_BUFFER_COUNT; ++i) {
        if(allocations[i].address && (uint64_t)base < (uint64_t)allocations[i].physical + allocations[i].length &&
           (uint64_t)base + bytes > allocations[i].physical) {
            fprintf(stderr, "wrmem allocation overlaps active region %u\n", i);
            goto fail;
        }
    }

    void* mapping =  mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, owner, base);
    //mmap64(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, owner, (off64_t)base);
    if(mapping == MAP_FAILED) {
        fprintf(stderr, "mmap /dev/mem phys=0x%lx size=%u\n", base, bytes);
        goto fail;
    }
    allocations[region] = (struct allocation){mapping, bytes, (unsigned int)base, owner, 0};
    *address = mapping;
    *physical = (unsigned int)base;
    printf("[MEM] allocator=%s mapping=%s region=%d phys=0x%08x size=%u virtual=%p\n",
           ZMUAV_WRMEM_DEVICE_NAME, MEM_DEV_NAME, region, *physical, bytes, mapping);

    // int fd = open(MEM_DEV_NAME, O_RDWR | O_SYNC);
    // if(fd < 0) { perror("open /dev/mem"); goto fail; }
    // void* mapping = mmap64(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off64_t)base);
    // int saved_errno = errno;
    // close(fd);
    // if(mapping == MAP_FAILED) {
    //     fprintf(stderr, "mmap /dev/mem phys=0x%lx size=%u: %s\n", base, bytes, strerror(saved_errno));
    //     goto fail;
    // }
    // allocations[region] = (struct allocation){mapping, bytes, (unsigned int)base, owner, 0};
    // *address = mapping;
    // *physical = (unsigned int)base;
    // printf("[MEM] allocator=%s mapping=%s region=%d phys=0x%08x size=%u virtual=%p\n",
    //        ZMUAV_WRMEM_DEVICE_NAME, MEM_DEV_NAME, region, *physical, bytes, mapping);
    return 0;
fail:
    if(allocated) release_owner(owner);
    else close(owner);
    return -1;
}

/* Monotonic time in microseconds, matching the supplied cache test. */
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000u;
}

/* Keep the original function name and parameters from the cache test. */
int do_sync(int fd, unsigned long cmd, uint32_t off, uint32_t size,
                   uint64_t *cost_us)
{
    struct cache_sync s = { .offset = off, .size = size };
    uint64_t t0 = now_us();
    int ret = ioctl(fd, cmd, &s);
    int saved_errno = errno;

    if(cost_us)
        *cost_us = now_us() - t0;
    errno = saved_errno;
    return ret;
}

int dma_buffer_owner_fd(unsigned char* address)
{
    for(unsigned int i = 0; i < DMA_BUFFER_COUNT; ++i) {
        if(address && allocations[i].address == address)
            return allocations[i].owner_fd;
    }
    errno = EINVAL;
    return -1;
}

int dma_buffer_unmap(unsigned char* address)
{
    if(!address) return 0;
    for(unsigned int i = 0; i < DMA_BUFFER_COUNT; ++i) {
        struct allocation* buffer = &allocations[i];
        if(buffer->address != address) continue;
        if(buffer->malloced) {
            free(buffer->address);
        } else {
            // Keep the driver allocation valid until the user mapping is gone.
            if(munmap(buffer->address, buffer->length) != 0) {
                perror("munmap DMA buffer"); return -1;
            }
            release_owner(buffer->owner_fd);
        }
        memset(buffer, 0, sizeof(*buffer));
        return 0;
    }
    fprintf(stderr, "Unknown DMA mapping %p\n", (void*)address);
    return -1;
}
