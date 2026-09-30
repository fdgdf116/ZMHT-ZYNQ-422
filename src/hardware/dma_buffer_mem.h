#ifndef ZMHT_DMA_BUFFER_MEM_H
#define ZMHT_DMA_BUFFER_MEM_H
#ifndef NETWORK_MALLOC_BENCHMARK
#define NETWORK_MALLOC_BENCHMARK 0
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum dma_buffer_region { DMA_BUFFER_TX, DMA_BUFFER_RX, DMA_BUFFER_1553B, DMA_BUFFER_COUNT };
// Startup/shutdown only. wrmem holds allocation; /dev/mem supplies the user mapping.
int dma_buffer_map(enum dma_buffer_region region, unsigned int bytes,
                   unsigned char** address, unsigned int* physical);
int dma_buffer_unmap(unsigned char* address);
#ifdef __cplusplus
}
#endif
#endif
