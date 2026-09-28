#ifndef _SYSTEM_H
#define _SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif
#include "common.h"

#define SGDMA_NUM (1)
#define ON_DATA_SIZE (1024)
#define RECV_DMA_DATA_SIZE (2*1024*1024)

#define NETWORK_RX_RING_SIZE (32u * 1024u * 1024u)
unsigned char* network_rx_dma_buffer(void);
// Single producer: acquire writable span, commit actual bytes, abort partial on disconnect.
int network_dma_reserve(unsigned char** address, unsigned int* length);
void network_dma_received(unsigned int bytes);
void network_dma_abort_partial(void);

void sgdma_memcpy_data(unsigned char chn_id, unsigned char* data);
int system_init(void);
void system_exit(void);
int recv_dma_init(void);
void recv_dma_exit(void);
void sgdma_mm2s_start(int chn);
void sgdma_mm2s_stop(int chn);
int rx_sgdma_data_get(unsigned char chn_id, u_int8_t** data);
int recv_dma(unsigned char* addr);
int dma_disable();

void is_sgdma_mm2s_stop(int chn);
int sgdma_mm2s_query_capacity(Cache_report_inf_t* capacity);
int sgdma_vfifo_query_capacity(Cache_report_inf_t* capacity);

#ifdef __cplusplus
}
#endif
#endif
