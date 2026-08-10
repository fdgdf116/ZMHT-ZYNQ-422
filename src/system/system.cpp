
#include "sync_send.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>

#include "system.h"
#include "ringbuffer.h"
#include "xutils.h"
#include "dma_utils.h"
#include "zmuav_pl2ps_irq.h"

using namespace network;

#define ON_DMA_SIZE_MAX (2*1024*1024)

#define ON_DATA_SIZE_H (1*1024*1024)
#define DMA_SIZE_MAX_H (80*1024*1024)
#define DMA_DATA_PKG_MAX_H (80*1023)
#define DMARX_DATA_PKG_MAX_H (39)

#if 0
#define ON_DATA_SIZE_L (4*1024)
#define DMA_SIZE_MAX_L (8*1024*1024)
#define DMA_DATA_PKG_MAX_L (8*1024)
#else

#define ON_DATA_SIZE_L (1024)
#define DMA_DATA_PKG_MAX_L (32*1024)
#define DMARX_DATA_PKG_MAX_L (16)
#define DMA_SIZE_MAX_L ((DMARX_DATA_PKG_MAX_L+1)*RECV_DMA_DATA_SIZE)

#endif

#define ZMUAV_PL2PS_IRQ_DEVICE_NAME "/dev/zmuav_pl2ps_irq_0"
#define ZMUAV_PS2PL_IRQ_DEVICE_NAME "/dev/zmuav_pl2ps_irq_1"
#define DMA_RINGBUFFER_NUM_MAX (4*1024)

#define VFIFI_CAPACITY (0x4000000)
#define VFIFO_NUM (16)
typedef struct {
    ring_buffer_t *dma_data_rb;
}data_stream_t;

static pthread_mutex_t data_mutex[SGDMA_NUM];

struct sgdma_data_info {
	struct sgdma_info sgdma_tx;
	data_stream_t data_stream;
	unsigned int ringbuf_offset;
	unsigned int ringbuf_count;
	unsigned int done_count;

	unsigned int dma_offset;
	unsigned int dma_size;
	unsigned int dma_flag;
	unsigned int dma_stop_flag;

};

static unsigned int dma_phy_size = 0x1000;
#if 0
static unsigned int dma_phy_addr[SGDMA_NUM] = {0xa0090000, 0xa0091000, 0xa0092000, 0xa0093000, 0xa0094000, 0xa0095000, 0xa0096000, 0xa0097000,
		0xa0098000, 0xa0099000, 0xa009a000, 0xa009b000, 0xa009c000, 0xa009d000, 0xa009e000, 0xa009f000};
#else
static unsigned int dma_phy_addr[SGDMA_NUM] = {0xa2001000};
static unsigned int rx_dma_phy_addr[SGDMA_NUM] = {0xa2000000};
#endif
static struct sgdma_data_info data_info[SGDMA_NUM];
static struct sgdma_data_info rx_data_info[SGDMA_NUM];
static unsigned int sgdma_tx_thread_chn[SGDMA_NUM];

// FILE* fp_write_test = NULL;
// FILE* fp_net_dma_tx = NULL;

/* 系统相关初始化 */
int data_stream_init(unsigned char chn_id, data_stream_t *data_stream,unsigned int ringbuffer_num)
{
    int ret;

    memset(data_stream, 0,sizeof(*data_stream));

    /* 初始化对应的ringbuffer  */
	data_stream->dma_data_rb = ringbuffer_create((ringbuffer_num)*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
	if (!data_stream->dma_data_rb) {
		printf("XF:create pcie cl data rb buffer failed\n");
		ret = -2;
		goto dma_data_rb_err;
	}
    return 0;

dma_data_rb_err:
    return ret;
}

void sgdma_mm2s_start(int chn)
{
	data_info[chn].dma_stop_flag = 0;
	data_info[chn].dma_flag = 0;
}

void sgdma_mm2s_stop(int chn)
{
	data_info[chn].dma_stop_flag = 1;
	data_info[chn].dma_flag = 0;
}

void is_sgdma_mm2s_stop(int chn)
{
	ring_buffer_t *rb;
	unsigned int ringbuff_num = 1;
	rb = data_info[chn].data_stream.dma_data_rb;
	ringbuff_num = ringbuffer_len(rb);
	while (ringbuff_num)
	{
		ringbuff_num = ringbuffer_len(rb);
		usleep(100);
	}
	data_info[chn].ringbuf_offset = 0;
	data_info[chn].ringbuf_count = 0;

	printf("Debug ringbuffer ok \n");
}

int sgdma_mm2s_query_capacity(Cache_report_inf_t* capacity)
{
	ring_buffer_t *rb;
	unsigned int ringbuff_num;
	for(int chn_id = 0; chn_id < SGDMA_NUM; chn_id++){
		rb = data_info[chn_id].data_stream.dma_data_rb;
		ringbuff_num = ringbuffer_avail(rb);
//		if((chn_id == 8) || (chn_id == 9)){
//			printf("chn:%d ringbuff num:%d \n", chn_id, ringbuff_num);
//		}
	    if(chn_id >= SGDMA_NUM){
	    	capacity[chn_id].dmaddr_space = ringbuff_num;
	    } else {
	    	capacity[chn_id].dmaddr_space = ringbuff_num;
	    }
	}
	for(int index = 0; index < FREE_SPACE_NUM_MAX; index++)
	{
		capacity[index].dmaddr_space = capacity[0].dmaddr_space;
	}
	return 0;
}

int sgdma_vfifo_query_capacity(Cache_report_inf_t* capacity)
{
	int ret = 1;
	int fd = open("/dev/mem",O_RDWR);
	if(fd<0){
		perror("open /dev/mem fail\n");ret=-1;
		return ret;
	}
	unsigned long aligment_addr = (GENERAL_REG/4096)*4096;
	int* vir_addr = (int*)mmap(NULL,GENERAL_REG_SZIE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,aligment_addr);
	if( vir_addr == MAP_FAILED ) {
		perror("mmap fail.!!!!!!\n");
		return ret;
	}
	for(int chn_id = 0; chn_id < VFIFO_NUM; chn_id++)
	{
		capacity[chn_id].vfifo_space = VFIFI_CAPACITY - *(vir_addr + (0xb0 + chn_id*0x04)/4) * 4;
		// printf("chn:%d VFIFI_CAPACITY:%x vfifo space:%x free:%x\n", chn_id, VFIFI_CAPACITY, *(vir_addr + (0xb0 + chn_id*0x04)/4)*4,capacity[chn_id].vfifo_space);
	}
	close(fd);
	munmap((void*)vir_addr,GENERAL_REG_SZIE);
	return 0;
}

int rx_sgdma_data_get(unsigned char chn_id, u_int8_t** data)
{
	image_frame_info_t frame_info;
	struct sgdma_info *sgdma_rx;
	ring_buffer_t *rb;
	int len = 0;
	static unsigned long ringbuffer_full_cnt = 0;

	sgdma_rx = &rx_data_info[chn_id].sgdma_tx;
	rb = rx_data_info[chn_id].data_stream.dma_data_rb;
	if(ringbuffer_is_empty(rb)) {
		if((ringbuffer_full_cnt % 10000) == 0){
			// printf("dma%d ringbuffer empty cnt:%ld \n", chn_id, ringbuffer_full_cnt);
		}
		usleep(10);
		ringbuffer_full_cnt += 1;
		return 0;
	}
	ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(frame_info));
	*data = sgdma_rx->mem_vir_base + frame_info.frame_offset;
    if(*data == NULL)
    {
        printf("data is empty\n");
        return 0;
    }
	
	return frame_info.frame_size;
}

void rx_sgdma_memcpy_data(unsigned char chn_id, unsigned int data_szie)
{
}

void sgdma_memcpy_data(unsigned char chn_id, unsigned char* data)
{
	image_frame_info_t frame_info;
	struct sgdma_info *sgdma_tx;

	ring_buffer_t *rb;
	unsigned char* dma_addr;
	static unsigned long ringbuffer_full_cnt = 0;

	if(data_info[chn_id].dma_stop_flag){
		printf("[%s] chn:%d dma stop \n", __func__, chn_id);
		return ;
	}

	if(chn_id >= SGDMA_NUM){
		printf("[%s] chn id(%d) >= chn max(%d) error \n", __func__, chn_id, SGDMA_NUM);
		return ;
	}

	sgdma_tx = &data_info[chn_id].sgdma_tx;
	rb = data_info[chn_id].data_stream.dma_data_rb;


#if 0
	unsigned int ringbuff_num;
	unsigned int ringbuff_num_max;
	while(1){
		ringbuff_num = ringbuffer_len(rb);
	    if((chn_id == 8) || (chn_id == 9)){
	    	ringbuff_num_max = DMA_DATA_PKG_MAX_H;
	    } else {
	    	ringbuff_num_max = DMA_DATA_PKG_MAX_L;
	    }

	    if((ringbuff_num + DMA_RINGBUFFER_NUM_MAX) > ringbuff_num_max){
	        usleep(10);
	        ringbuffer_full_cnt += 1;
	    } else {
	    	break;
	    }

    	if((ringbuffer_full_cnt % 10000) == 0){
    		printf("[%s,%d] chn%d ringbuffer full cnt:%ld \n",__func__,__LINE__, chn_id, ringbuffer_full_cnt);
    	}
	}
#else

    while(ringbuffer_is_full(rb)) {
    	if((ringbuffer_full_cnt % 10000) == 0){
    		printf("[%s,%d] chn%d ringbuffer full cnt:%ld \n",__func__,__LINE__, chn_id, ringbuffer_full_cnt);
    	}
        usleep(10);
        ringbuffer_full_cnt += 1;
    }
#endif
	if(data_info[chn_id].ringbuf_offset >= sgdma_tx->map_size){
		data_info[chn_id].ringbuf_offset = 0;
	}
	// if(data_info[chn_id].ringbuf_offset > 0x1e00000)
	// {
	// 	printf("[%s-%d]--Debug-chn_id:%d- dma offset:0x%x size:0x%x\n",__func__,__LINE__,chn_id,data_info[chn_id].ringbuf_offset,sgdma_tx->map_size);
	// }

	dma_addr = sgdma_tx->mem_vir_base + data_info[chn_id].ringbuf_offset;
	memcpy(dma_addr, data, ON_DATA_SIZE);
	// fwrite(dma_addr, 1, ON_DATA_SIZE, fp_net_dma_tx);
	// fflush(fp_net_dma_tx);

	// frame_info.frame_index = data_info[chn_id].ringbuf_count;
	frame_info.frame_offset = data_info[chn_id].ringbuf_offset;
	frame_info.frame_size = ON_DATA_SIZE;
	pthread_mutex_lock(&data_mutex[0]);
	pcie_data_to_queue(rb, &frame_info);
	pthread_mutex_unlock(&data_mutex[0]);

	// data_info[chn_id].dma_stop_flag = 0;

	data_info[chn_id].ringbuf_offset += ON_DATA_SIZE;
	data_info[chn_id].ringbuf_count += 1;

}

int sgdna_tx_init(unsigned char chn_id)
{
	int ret;
	size_t reserve_mem_size;
	struct dma_addr_info dma_addr;
	struct sgdma_info *sgdma_tx;
	if(chn_id >= SGDMA_NUM){
		printf("dma chn id error \n");
		return -1;
	}

	memset(&data_info[chn_id], 0x0, sizeof(data_info[chn_id]));

	ret = data_stream_init(chn_id, &data_info[chn_id].data_stream,DMA_DATA_PKG_MAX_L);
	if(ret != 0){
		printf("system init error \n");
		return -2;
	}

	sgdma_tx = &data_info[chn_id].sgdma_tx;

	dma_addr.phy_addr = dma_phy_addr[chn_id];
	dma_addr.size = dma_phy_size;

	if(chn_id >= SGDMA_NUM){
		reserve_mem_size = DMA_SIZE_MAX_H;
	} else {
		reserve_mem_size = DMA_SIZE_MAX_L;
	}

	ret = sgdma_init(chn_id, ZMUAV_PS2PL_IRQ_DEVICE_NAME, &dma_addr, reserve_mem_size, sgdma_tx);
	if(ret != 0){
		printf("sgdma%d init error \n", chn_id);
		ringbuffer_deinit(data_info[chn_id].data_stream.dma_data_rb);
		return -3;
	}

	return 0;
}


unsigned int sgdma_SelectBlock(unsigned chn_id)
{
	int ret;
	struct sgdma_info *sgdma_tx;
	sgdma_tx = &data_info[chn_id].sgdma_tx;
	while(1){
		ret = SelectBlock(sgdma_tx);
		if(ret > 0) { // 被Stop接口停止的时候ret = 0
//			unsigned int now_count = get_mm2s_irq_count(sgdma_tx);
//			if(now_count != data_info[chn_id].done_count){
//				data_info[chn_id].done_count =now_count;
//				break;
//			}
			break;
		}

		if(data_info[chn_id].dma_stop_flag){
			break;
		}

		usleep(1);
	}

	return 0;
}

void free_ringbuffer(ring_buffer_t *rb)
{
	image_frame_info_t frame_info;
	while(1){
		if(ringbuffer_is_empty(rb)){
			break;
		}
		ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
	}
}

#if 1
void* sgdma_h_tx_pthread(void* arg)
{
	int ret;
	unsigned int i;
	int chn_id;
	struct sgdma_info *sgdma_tx;
	unsigned int ringbuff_num, dma_size, frame_offset;
	image_frame_info_t frame_info;
	chn_id = *((unsigned int*)arg);
	unsigned int* dma_addr = NULL;
	if(chn_id < 0 || chn_id >= SGDMA_NUM) {
		printf("[%s] invalid channel id: %d, max: %d\n",
			   __func__, chn_id, SGDMA_NUM);
		return NULL;
	}

	sgdma_tx = &data_info[chn_id].sgdma_tx;
	printf("[%s-%d]-101-1008-1616+-Debug-chn_id:%d--\n",__func__, __LINE__, chn_id);
	ring_buffer_t *rb = data_info[chn_id].data_stream.dma_data_rb;
	if(rb == NULL){
		printf("[%s-%d]--Debug-chn_id:%d-rb error-\n",__func__, __LINE__, chn_id);
		return NULL;
	}

	while(1){
        while(ringbuffer_is_empty(rb)) {
            usleep(1);
        }
		ringbuff_num = ringbuffer_len(rb) ;
		pthread_mutex_lock(&data_mutex[0]);
		ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
		pthread_mutex_unlock(&data_mutex[0]);

		if(data_info[chn_id].dma_stop_flag){
			free_ringbuffer(rb);
			continue;
		}

		frame_offset = frame_info.frame_offset;
		if(!data_info[chn_id].dma_flag){
			data_info[chn_id].dma_flag = 1;
			data_info[chn_id].dma_offset = frame_info.frame_offset;
		}

		dma_size = ringbuff_num * ON_DATA_SIZE;

		if((frame_info.frame_offset + dma_size) >= sgdma_tx->map_size){
			dma_size = sgdma_tx->map_size - frame_info.frame_offset;
		}

		if(dma_size > ON_DMA_SIZE_MAX){
			dma_size = ON_DMA_SIZE_MAX;
		}

		ringbuff_num = dma_size / ON_DATA_SIZE;
		for(i = 1; i < ringbuff_num; i++){
			pthread_mutex_lock(&data_mutex[0]);
			ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
			pthread_mutex_unlock(&data_mutex[0]);
		}

		data_info[chn_id].dma_size += dma_size;

		if(data_info[chn_id].dma_stop_flag == 0){
			if(chn_id >= SGDMA_NUM){
				if(data_info[chn_id].dma_size >= ON_DATA_SIZE_H){
					data_info[chn_id].dma_flag = 0;
				} else if((frame_offset + dma_size) >= sgdma_tx->map_size){
					data_info[chn_id].dma_flag = 0;
				} else {
					continue;
				}
			} else {
				if(data_info[chn_id].dma_size >= ON_DATA_SIZE_L){
					data_info[chn_id].dma_flag = 0;
				} else if((frame_offset + dma_size) >= sgdma_tx->map_size){
					data_info[chn_id].dma_flag = 0;
				} else {
					continue;
				}
			}
		} else {
			data_info[chn_id].dma_flag = 0;
			free_ringbuffer(rb);
			continue;
		}

		mm2s_dma_enable(sgdma_tx);
		push_mm2s_dma(sgdma_tx, data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
		ret = sgdma_SelectBlock(chn_id);
		if(!ret){
			mm2s_dma_disable(sgdma_tx);
			// printf("dma offset:0x%x size:0x%x \n", data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
		}
		data_info[chn_id].dma_size = 0;
		xt_nanosleep(0, 100);
	}
}
#else
void* sgdma_h_tx_pthread(void* arg)
{
	int ret;
	unsigned int i;
	int chn_id;
	struct sgdma_info *sgdma_tx;
	unsigned int ringbuff_num, frame_offset;
	image_frame_info_t frame_info;
	chn_id = *((unsigned int*)arg);

	sgdma_tx = &data_info[chn_id].sgdma_tx;
	printf("[%s-%d]-101-1008-1616+-Debug-chn_id:%d--\n",__func__, __LINE__, chn_id);
	ring_buffer_t *rb = data_info[chn_id].data_stream.dma_data_rb;
	if(rb == NULL){
		printf("[%s-%d]--Debug-chn_id:%d-rb error-\n",__func__, __LINE__, chn_id);
	}
	// FILE *fp = fopen("/tmp/dma_tx.raw", "wb+");

	while(1){
        while(ringbuffer_is_empty(rb)) {
            usleep(1);
        }
		if(data_info[chn_id].dma_stop_flag){
			free_ringbuffer(rb);
			continue;
		}

		ringbuff_num = ringbuffer_len(rb);
		ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
		data_info[chn_id].dma_offset = frame_info.frame_offset;
		if(data_info[chn_id].dma_offset + ringbuff_num*ON_DATA_SIZE > sgdma_tx->map_size){
			data_info[chn_id].dma_size = sgdma_tx->map_size - data_info[chn_id].dma_offset;
			ringbuff_num = data_info[chn_id].dma_size / ON_DATA_SIZE;
		}

		for(i = 1; i < ringbuff_num; i++){
			ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
		}
		data_info[chn_id].dma_size = ON_DATA_SIZE*ringbuff_num;

		if(data_info[chn_id].dma_stop_flag){
			data_info[chn_id].dma_flag = 0;
			free_ringbuffer(rb);
			continue;
		}

		mm2s_dma_enable(sgdma_tx);
		push_mm2s_dma(sgdma_tx, data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
		// fwrite((void *)(sgdma_tx->mem_vir_base + data_info[chn_id].dma_offset), 1, data_info[chn_id].dma_size, fp);
		// fflush(fp);

		ret = sgdma_SelectBlock(chn_id);
		if(!ret){
			mm2s_dma_disable(sgdma_tx);
			printf("dma offset:0x%x size:0x%x \n", data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
		}
		data_info[chn_id].dma_size = 0;
	}
	// fclose(fp);
}
#endif


void* sgdma_l_tx_pthread(void* arg)
{
	int ret;
	int chn_id;
	unsigned int ringbuff_num;
	struct sgdma_info *sgdma_tx;
	image_frame_info_t frame_info;
	chn_id = *((unsigned int*)arg);

	sgdma_tx = &data_info[chn_id].sgdma_tx;
	printf("[%s-%d]-101-1008-1616+-Debug-chn_id:%d--\n",__func__, __LINE__, chn_id);
	ring_buffer_t *rb = data_info[chn_id].data_stream.dma_data_rb;
	if(rb == NULL){
		printf("[%s-%d]--Debug-chn_id:%d-rb error-\n",__func__, __LINE__, chn_id);
	}

	while(1){
        while(ringbuffer_is_empty(rb)) {
            usleep(1);
        }

		ringbuff_num = ringbuffer_len(rb) ;
		if(ringbuff_num != 1){
			printf("[Debug] chn:%d ringbuff_num: %d \n", chn_id, ringbuff_num);
		}

		ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(image_frame_info_t));
		if(data_info[chn_id].dma_stop_flag){
			free_ringbuffer(rb);
			continue;
		}

		data_info[chn_id].dma_offset = frame_info.frame_offset;
		data_info[chn_id].dma_size = ON_DATA_SIZE;

#if TEST
		printf("dma offset:0x%x size:0x%x \n", data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
#else
		mm2s_dma_enable(sgdma_tx);
		push_mm2s_dma(sgdma_tx, data_info[chn_id].dma_offset, data_info[chn_id].dma_size);

		ret = sgdma_SelectBlock(chn_id);
		if(!ret){
			mm2s_dma_disable(sgdma_tx);
			printf("dma offset:0x%x size:0x%x \n", data_info[chn_id].dma_offset, data_info[chn_id].dma_size);
		}
#endif
		data_info[chn_id].dma_size = 0;
	}
}

int system_init(void)
{
	int ret;
	int num;
	pthread_t sgdma_tid[SGDMA_NUM];

	pthread_attr_t attr;
	struct sched_param param;
	// 初始化线程属性
    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        perror("pthread_attr_init 失败");
        return 1;
    }
	// 设置线程调度策略（如 SCHED_FIFO 或 SCHED_RR，只有实时策略支持优先级）
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret != 0) {
        perror("设置调度策略失败");
        pthread_attr_destroy(&attr);
        return 1;
    }
	// 设置优先级（范围：1-99，值越大优先级越高）
    param.sched_priority = 30;  // 优先级为 50
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret != 0) {
        perror("设置优先级失败");
        pthread_attr_destroy(&attr);
        return 1;
    }

	for(num = 0; num < SGDMA_NUM; num++){
		pthread_mutex_init(&data_mutex[num], NULL);
		ret = sgdna_tx_init(num);
		if(ret < 0){
			printf("sgdma%d tx init error \n", num);
			return 2;
		} else if(!ret){
			sgdma_tx_thread_chn[num] = (unsigned int)num;
			if(num < SGDMA_NUM){
				ret = pthread_create(&sgdma_tid[num], &attr, sgdma_h_tx_pthread,
								 &sgdma_tx_thread_chn[num]);
				if(ret != 0){
					printf("[Debug] sgdma_h_tx_pthread create error \n");
					return 3;
				}
			} else {
				ret = pthread_create(&sgdma_tid[num], NULL, sgdma_l_tx_pthread,
								 &sgdma_tx_thread_chn[num]);
				if(ret != 0){
					printf("[Debug] sgdma_l_tx_pthread create error \n");
					return 3;
				}
			}
		}
		usleep(10000);
	}

	pthread_attr_destroy(&attr);
	return 0;
}

void system_exit(void)
{
	struct sgdma_info *sgdma_tx;
	for(int num = 0; num < SGDMA_NUM; num++){
		sgdma_tx = &data_info[num].sgdma_tx;

		if(data_info[num].data_stream.dma_data_rb) {
			ringbuffer_deinit(data_info[num].data_stream.dma_data_rb);
		}

		if(sgdma_tx->mem_vir_base){
			sgdma_exit(sgdma_tx);
		}
	}
}

static int zmuav_pl2ps_irq_get_count(int fd)
{
    int rc;
    unsigned int irq_count;

    rc = ioctl(fd, ZMUAV_PL2PS_IRQ_GET_IRQ_COUNT, &irq_count);
    if (rc) {
        return 0;
    }

    return irq_count;
}

static int zmuav_pl2ps_irq_clean_count(int fd)
{
    unsigned int irq_count;

    irq_count = 0;

    return ioctl(fd, ZMUAV_PL2PS_IRQ_CLEAN_IRQ_COUNT, &irq_count);;
}

static int zmuav_pl2ps_irq_poll(struct pollfd* fds ,int poll_timeout_ms)
{
    int pollrc;
    
    while (1) {
        pollrc = poll(fds,1,poll_timeout_ms);
        if (pollrc > 0) {
            return 0;
        } else {
            return -1;
        }
    }

    return -1;
}


static void *zmuav_pl2ps_irq_recv_pthread(void* parameter)
{
    int ret = 0;
    unsigned int irq_count;
	struct pollfd fds[SGDMA_NUM];
	struct sgdma_info* sgdma_rx = NULL;

	image_frame_info_t frame_info;
	int data_szie = RECV_DMA_DATA_SIZE;

	ring_buffer_t *rb;
	unsigned char* dma_addr = NULL;
	unsigned long ringbuffer_full_cnt = 0;
	unsigned int last1_done_count = 0;
	u_int8_t* data = (u_int8_t*)malloc(data_szie);
	sgdma_rx = &rx_data_info[0].sgdma_tx;
	rb = rx_data_info[0].data_stream.dma_data_rb;
	unsigned int frame_cnt = 0;
	FILE *fp = fopen("/tmp/dma_addr.raw", "wb+");

	long seconds, microseconds;
	double elapsed;
	struct timeval start, end;
    while (1) {
		if(rx_data_info[0].ringbuf_offset + RECV_DMA_DATA_SIZE > sgdma_rx->map_size){
			rx_data_info[0].ringbuf_offset = 0;
		}
		while (ringbuffer_is_full(rb))
		{
			ringbuffer_full_cnt++;
			if(ringbuffer_full_cnt >= 40000){
				printf("[%s]ringbuffer is full.!!!!!!!!!!\n", __func__);
				ringbuffer_full_cnt = 0;
			}
			/* Keep all queued frames and let the 9014 sender release a slot. */
			usleep(50);
		}
		

		int recv_count = data_szie;
		if(recv_count != 0) {
			gettimeofday(&start, NULL);
			s2mm_dma_enable(sgdma_rx);
			get_s2mm_dma(sgdma_rx,rx_data_info[0].ringbuf_offset,data_szie);
			unsigned int wait_timeout_count = 0;
			do {
				ret = SelectBlock(sgdma_rx);
				if(ret == 0) {
					unsigned int status = *(sgdma_rx->dma_base_addr + 0x34/4);
					/* IOC may be set even if userspace missed the IRQ notification. */
					if((status & 0x1000U) != 0U) {
						ret = 1;
						break;
					}
					wait_timeout_count++;
					if(wait_timeout_count >= 4) {
						wait_timeout_count = 0;
					}
				}
			} while(ret == 0);
			if(ret > 0)
			{
				recv_count = getdatacount(sgdma_rx);
				if((recv_count>RECV_DMA_DATA_SIZE) && (recv_count%0x400 != 0)){
					printf("recv recv_count:0x%x err.!!!!!!!!!!",recv_count);
				}
				// printf("[%s %d]recv_count:0x%x\n",__func__,__LINE__,recv_count);
				dma_addr = sgdma_rx->mem_vir_base + rx_data_info[0].ringbuf_offset;
				//计算DmaSend发送耗时，精确到us
				frame_info.frame_offset = rx_data_info[0].ringbuf_offset;
				frame_info.frame_size = recv_count;
				pcie_data_to_queue(rb, &frame_info);
				// gettimeofday(&end, NULL);
				// seconds = end.tv_sec - start.tv_sec;
				// microseconds = end.tv_usec - start.tv_usec;
				// elapsed = seconds + microseconds*1e-6;
				// printf("DmaSend time:%f\n",elapsed);

				rx_data_info[0].ringbuf_offset += RECV_DMA_DATA_SIZE;
				s2mm_dma_disable(sgdma_rx);
			}
		}
    }
	fclose(fp);
    return NULL;
}

static void *synthetic_dma_rx_pthread(void* parameter)
{
	(void)parameter;
	const unsigned int frame_size = RECV_DMA_DATA_SIZE;
	struct sgdma_info* sgdma_rx = &rx_data_info[0].sgdma_tx;
	ring_buffer_t *rb = rx_data_info[0].data_stream.dma_data_rb;

	if(rb == NULL || sgdma_rx->mem_vir_base == NULL ||
	   sgdma_rx->map_size < frame_size) {
		printf("[DMA TEST] synthetic buffer is not initialized\n");
		return NULL;
	}

	/* Fill the DMA mapping once. The producer only queues descriptors later. */
	memset(sgdma_rx->mem_vir_base, 0x5a, sgdma_rx->map_size);
	printf("[DMA TEST] synthetic producer started, frame: %u bytes, map: %u bytes\n",
		   frame_size, sgdma_rx->map_size);

	while(1) {
		while(ringbuffer_is_full(rb)) {
			usleep(50);
		}

		if(rx_data_info[0].ringbuf_offset + frame_size > sgdma_rx->map_size) {
			rx_data_info[0].ringbuf_offset = 0;
		}

		image_frame_info_t frame_info;
		frame_info.frame_offset = rx_data_info[0].ringbuf_offset;
		frame_info.frame_size = frame_size;
		pcie_data_to_queue(rb, &frame_info);
		rx_data_info[0].ringbuf_offset += frame_size;
	}

	return NULL;
}

unsigned int last1_done_cnt = 0;
int recv_dma(unsigned char* addr)
{
	static int recv_size_sum = 0;
	static char fill_data = 0x00;
	int ret,recv_size = 0;
	int data_szie = RECV_DMA_DATA_SIZE;
	struct sgdma_info* sgdma_rx = &rx_data_info[0].sgdma_tx;
	// memset(sgdma_rx->mem_vir_base,fill_data,data_szie);
	// printf("[Debug] ### (0x%x-0x%x-0x%x-0x%x) ###\n", sgdma_rx->mem_vir_base[8192], sgdma_rx->mem_vir_base[8193], sgdma_rx->mem_vir_base[8194], sgdma_rx->mem_vir_base[8195]);
	// Cache_report_inf_t dma_capacity[FREE_SPACE_NUM_MAX];
	// sgdma_vfifo_query_capacity(dma_capacity);
	// int valide_size = VFIFI_CAPACITY - dma_capacity[15].vfifo_space;
	// if(valide_size > 1024)
	// {
		s2mm_dma_enable(sgdma_rx);
		get_s2mm_dma(sgdma_rx,0,data_szie);
		ret = SelectBlock(sgdma_rx);
		if(ret > 0)
		{
			int now_inc=zmuav_pl2ps_irq_get_count(sgdma_rx->fd);
			int inc = now_inc - last1_done_cnt ;
			last1_done_cnt = now_inc;
			// printf("now_inc:%d inc:%d last1_done_cnt:%d\n",now_inc,inc,last1_done_cnt);
			recv_size = getdatacount(sgdma_rx);
			// printf("recv_size:%d\n",recv_size);
			recv_size_sum += recv_size;
			// *addr = sgdma_rx->mem_vir_base;
			memcpy(addr, sgdma_rx->mem_vir_base, recv_size);
			// if(recv_size_sum < 0x6400000)
			// {
			// 	fwrite(addr,recv_size,1,fp_write_test);
			// 	fflush(fp_write_test);
			// }
			// if(recv_size > 1024)
			// {
			// 	printf("======recv_size:%d fill_data:%d recv_size_sum:%d mem_vir_base:0x%x mem_phy_addr:0x%x==(0x%x-0x%x-0x%x-0x%x)=====\n",recv_size,fill_data,recv_size_sum,sgdma_rx->mem_vir_base,sgdma_rx->mem_phy_addr,
			// 		sgdma_rx->mem_vir_base[8192], sgdma_rx->mem_vir_base[8193], sgdma_rx->mem_vir_base[8194], sgdma_rx->mem_vir_base[8195]);
			// }
			// fill_data++;
			// s2mm_dma_disable(sgdma_rx);
		}
	// } else {
	// 	printf("[Debug] ### valide_size:%d ###\n",valide_size);
	// }
	return recv_size;
}

int dma_disable()
{
	struct sgdma_info* sgdma_rx = &rx_data_info[0].sgdma_tx;
	s2mm_dma_disable(sgdma_rx);
}

int sgdna_rx_init(unsigned char chn_id)
{
	int ret;
	size_t reserve_mem_size;
	struct dma_addr_info dma_addr;
	struct sgdma_info *sgdma_rx;
	if(chn_id >= SGDMA_NUM){
		printf("dma chn id error \n");
		return -1;
	}

	memset(&rx_data_info[chn_id], 0x0, sizeof(rx_data_info[chn_id]));

	ret = data_stream_init(chn_id, &rx_data_info[chn_id].data_stream,DMARX_DATA_PKG_MAX_L);
	if(ret != 0){
		printf("system init error \n");
		return -2;
	}

	sgdma_rx = &rx_data_info[chn_id].sgdma_tx;

	dma_addr.phy_addr = rx_dma_phy_addr[chn_id];
	dma_addr.size = dma_phy_size;

	if(chn_id >= SGDMA_NUM){
		reserve_mem_size = DMA_SIZE_MAX_H;
	} else {
		reserve_mem_size = DMA_SIZE_MAX_L;
	}
	

	ret = sgdma_init(chn_id, ZMUAV_PL2PS_IRQ_DEVICE_NAME,&dma_addr, reserve_mem_size, sgdma_rx);
	if(ret != 0){
		printf("sgdma%d init error \n", chn_id);
		ringbuffer_deinit(data_info[chn_id].data_stream.dma_data_rb);
		return -3;
	}

	return 0;
}

void recv_dma_exit(void)
{
	struct sgdma_info *sgdma_rx;
	for(int num = 0; num < SGDMA_NUM; num++){
		sgdma_rx = &rx_data_info[num].sgdma_tx;

		if(rx_data_info[num].data_stream.dma_data_rb) {
			ringbuffer_deinit(rx_data_info[num].data_stream.dma_data_rb);
		}

		if(sgdma_rx->mem_vir_base){
			sgdma_exit(sgdma_rx);
		}
	}
}

int recv_dma_init(void)
{
    int ret = 0;
    pthread_t recv_tid;
	pthread_attr_t attr;
	struct sched_param param;
	int num;
	// fp_write_test = fopen("/tmp/recv_dma.raw", "wb+");
	// fp_net_dma_tx = fopen("/tmp/fp_net_dma_tx.raw", "wb+");
	// 初始化线程属性
    ret = pthread_attr_init(&attr);
    if (ret != 0) {
        perror("pthread_attr_init 失败");
        return 1;
    }
	// 设置线程调度策略（如 SCHED_FIFO 或 SCHED_RR，只有实时策略支持优先级）
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret != 0) {
        perror("设置调度策略失败");
        pthread_attr_destroy(&attr);
        return 1;
    }
	// 设置优先级（范围：1-99，值越大优先级越高）
    param.sched_priority = 30;  // 优先级为 50
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret != 0) {
        perror("设置优先级失败");
        pthread_attr_destroy(&attr);
        return 1;
    }

	for(num = 0; num < SGDMA_NUM; num++){
		ret = sgdna_rx_init(num);
		if(ret < 0){
			printf("sgdma%d rx init error \n", num);
			return 2;
		}
	}
#if DATA_PORT_BENCHMARK_MODE
	for(num = 0; num < SGDMA_NUM; ++num) {
		s2mm_dma_disable(&rx_data_info[num].sgdma_tx);
	}
	ret = pthread_create(&recv_tid, &attr, synthetic_dma_rx_pthread, NULL);
	if (ret != 0) {
		printf("synthetic dma producer pthread_create error \n");
		return 3;
	}
#else
	ret = pthread_create(&recv_tid, &attr, zmuav_pl2ps_irq_recv_pthread, NULL);
    if (ret != 0) {
        printf("irq recv pthread_create error \n");
        return 3;
    }
#endif
	pthread_attr_destroy(&attr);
    return 0;
}
