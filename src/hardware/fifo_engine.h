#ifndef FIFO_ENGINE_H_
#define FIFO_ENGINE_H_


#ifdef __cplusplus
extern "C" {
#endif
#include "common.h"

#define RECV_PAK_OK    0x55
#define RECV_PAK_ERROR 0x5a
#define COMD_PROC_SUCCESS   0xaa
#define COMD_PROC_FAILED    0xa5
#define FIFO_NUM 2 // Active AXI FIFO channels: 0 and 1

#define AXIFIFO_RECV_DATA_SIZE (1024)

int run_test();
int run_destory();
int axififo_data_init(void);
int axififo_recv(unsigned int fifo_id, unsigned char* data);
int rx_fifo_data_get(unsigned char chn_id, u_int8_t** data);
void fifo_tx_memcpy_data(unsigned char chn_id, unsigned char* data, int size);
int axififo_pure_data_send(int fifo_id ,unsigned char* data,int len);
void axififo_data_destory(void);
int axififo_query_capacity(Cache_report_inf_t* capacity);

#ifdef __cplusplus
}
#endif
#endif /* GPU_GPU_INTERFACE_H_ */
