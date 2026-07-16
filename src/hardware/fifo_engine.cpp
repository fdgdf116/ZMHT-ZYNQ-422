#include <stdint.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <memory.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <inttypes.h>
#include <sys/time.h>

#include "stream_fifo.h"
#include "fifo_engine.h"
#include "ringbuffer.h"
#include "sync_send.h"
#include "xutils.h"
#include "pcie_reg_rw.h"

using namespace network;

#define FPGA_PCIE_AXIFIFO_PHY (0x83c60000)
#define FPGA_PCIE_AXIFIFO_LEN (0x20000)
#define FPGA_PCIE_AXIFIFO_INTERVAL (0x40000)


#define FPGA_PCIE_DATA_PHY (0x83c80000)
#define FPGA_PCIE_DATA_LEN (0x20000)
#define FPGA_PCIE_READ_DATA_OFFSET (0x1000)

#define AXIFIFO_ONCE_SEND_DATA_SIZE_MAX (512)
#define PCIE_AXIFIFO_BUF_SIZE_MAX (1024)
#define AXIFIFO_HEAD_SIZE (2)
#define AXIFIFO_LENGH_SIZE (1)
#define AXI_FIFO_DATA_SIZE ((16*1024+1)*512)
#define AXI_FIFO_BUF_SIZE (16*1024)
#define AXI_FIFO_BUF_UNIT_SIZE (512)
struct fifo_data_info_t {
	u_int8_t* data;
	ring_buffer_t *fifo_data_rb;
	unsigned int map_size;
	unsigned int ringbuf_offset;
	unsigned int ringbuf_count;
	unsigned int done_count;

	unsigned int dma_offset;
	unsigned int dma_size;
	unsigned int dma_flag;
	unsigned int dma_stop_flag;
};

typedef struct {
	struct fifo_data_info_t fifo_data_info[FIFO_NUM];
	struct fifo_data_info_t fifo_tx_data_info[FIFO_NUM];
	uint8_t axififo_data[1024];
	stream_fifo_reg_t fifo_reg[FIFO_NUM];
} axififo_info_t;

static axififo_info_t *g_axififo_info;

static unsigned int axififo_count = 0;
static unsigned int axififo_index = 0;
static unsigned char msg_head_1 = 0xec;
static unsigned char msg_head_2 = 0x91;
static unsigned char local_buffer[PCIE_AXIFIFO_BUF_SIZE_MAX];

static unsigned int upgrade_num;
static unsigned int upgrade_size;
static unsigned char* upgrade_buffer = NULL;
static char upgrade_file_name[128];
static FILE * upgrade_fp;
static FILE * fp_fifo_rx;
static unsigned int upgrade_state;

typedef struct {
	unsigned short head;
	unsigned int len;
	unsigned char mode;
	unsigned char data;
	unsigned char sum;
}uartlist_send_info_t;

static int axififo_data_send(int fifo_id, unsigned char mode, unsigned char* data,int len);

/****************************************************************************************/
/*
	函数: static void axififo_send(unsigned char mode, unsigned int data)
	作用: axififo 发送函数
	参数:
		mode：帧类型
		data:发送的数据
	返回值:
		无
*/
static void axififo_send(int fifo_id, unsigned char mode, unsigned int data)
{
	int ret;
	unsigned int send_size;
	uartlist_send_info_t send_info;

	send_info.head = htons(0xec91);
	send_info.len = htonl(1);
	send_info.mode = mode;
	send_info.data = data;
	send_info.sum = check_sum((unsigned char*)&send_info.data, sizeof(send_info.data));

	send_size = 0;
	while(1){
		ret = stream_fifo_write_data(&g_axififo_info->fifo_reg[fifo_id],(uint8_t *)(&send_info + send_size),sizeof(send_info) - send_size);
		if(ret == (sizeof(send_info) - send_size)){
			break;
		}
		send_size += ret;
		//usleep(1000);
		xt_nanosleep(0, 100);
	}
	printf("send: ");
	for(int i = 0;i < sizeof(send_info);i++)
	{
		printf("%x ",*((uint8_t *)&send_info + i));
	}
	printf("\n");

	return ;
}

/****************************************************************************************/
/*
	函数: static void axififo_data_send( unsigned int* data,int len)
	作用: axififo 发送任意长度数据函数
	参数:
		mode：帧类型
		data:发送的数据
	返回值:
		无
*/
static int axififo_data_send(int fifo_id, unsigned char mode, unsigned char* data,int len)
{
	int ret;
	unsigned int send_size;
	uint8_t* buff = (uint8_t*)malloc(len + 8);
	uartlist_send_info_t* send_info = (uartlist_send_info_t*)buff;

	send_info->head = htons(0xec91);
	send_info->len = htonl(len);
	send_info->mode = mode;
	memcpy(&send_info->data,data,len);
	*(buff + 7 + len) = check_sum((unsigned char*)&send_info->data,len);

	send_size = 0;
	while(1){
		ret = stream_fifo_write_data(&g_axififo_info->fifo_reg[fifo_id],(uint8_t *)(buff + send_size),len + 8 - send_size);
		if(ret == (len + 8 - send_size)){
			break;
		}
		send_size += ret;
		//usleep(1000);
		xt_nanosleep(0, 100);
	}

	return 0;
}

int axififo_get_send_size(int fifo_id)
{
	unsigned int free_size = g_axififo_info->fifo_reg[fifo_id].cfg->TDFV;
	return free_size;
}

int axififo_pure_data_send(int fifo_id, unsigned char* data,int len)
{
	int ret;
	unsigned int send_size;

	send_size = 0;
	while(1){
		ret = stream_fifo_write_data(&g_axififo_info->fifo_reg[fifo_id],(uint8_t *)(data + send_size),len - send_size);
		// fwrite(data + send_size, 1, ret, fp_fifo_rx);
		// fflush(fp_fifo_rx);
		if(ret == (len - send_size)){
			break;
		}
		send_size += ret;
		if(ret <= 0){
			usleep(10);
		}
		// xt_nanosleep(0, 100);
	}

	return 0;
}

/*
	函数: axififo_frame_data_analyse(unsigned char* frame_buffer, unsigned int frame_size)
	作用: 完整大帧的解析
	参数:
		frame_buffer:完整大帧的数据buffer
		frame_size:完整大帧的数据长度
	返回值:
		无
*/
static void axififo_frame_data_analyse(unsigned char* frame_buffer, unsigned int frame_size)
{
}
/******************************************************************************************/

/*
	函数: axififo_recv_data_analysis(unsigned char *buffer, unsigned int length)
	作用: 对接收到的数据进行解析，解析出来一帧完整帧，在做判断
	参数:
		buffer:接收到的数据
		length:接收到的数据长度
	返回值:
		返回0表示有一帧完整帧，其他表示没有完整帧
*/
static int axififo_recv_data_analysis(unsigned char *buffer, unsigned int length)
{

	uint8_t sum;
	unsigned int cmd_val;
	int i = 0, j = 0, index = 0;
	int ret = -1;
	unsigned char frame_length;
	unsigned char *frame_buff;
	
	for(index = 0; index < length; index++) {
		local_buffer[(axififo_count + index) % PCIE_AXIFIFO_BUF_SIZE_MAX] = buffer[index];
	}

	axififo_count += length;

	if((axififo_count - axififo_index) < (AXIFIFO_HEAD_SIZE + AXIFIFO_LENGH_SIZE)){
		return 4;
	}

	for(i = axififo_index; i < (axififo_count - 2); i++) {
		if(local_buffer[i % PCIE_AXIFIFO_BUF_SIZE_MAX] == msg_head_1 && local_buffer[(i + 1) % PCIE_AXIFIFO_BUF_SIZE_MAX] == msg_head_2) {    
			frame_length = (local_buffer[(i + 2) % PCIE_AXIFIFO_BUF_SIZE_MAX] << 24) + (local_buffer[(i + 3) % PCIE_AXIFIFO_BUF_SIZE_MAX] << 16) + (local_buffer[(i + 4) % PCIE_AXIFIFO_BUF_SIZE_MAX] << 8) + local_buffer[(i + 5) % PCIE_AXIFIFO_BUF_SIZE_MAX];
			if(frame_length < (axififo_count - i - 6)){
			
				frame_buff = g_axififo_info->axififo_data;
				for(j = 0; j < frame_length; j++){
					frame_buff[j] = local_buffer[(i + j + 6) % PCIE_AXIFIFO_BUF_SIZE_MAX];
				}

				sum = check_sum(frame_buff, frame_length);
				if(sum == local_buffer[(i + frame_length + 6) % PCIE_AXIFIFO_BUF_SIZE_MAX]){
					axififo_frame_data_analyse(frame_buff, frame_length);
				} else {
					printf("uartlist frame data sum error,len:%d sum(0x%x 0x%x) \n", frame_length, \
						sum, local_buffer[(i + frame_length + 6) % PCIE_AXIFIFO_BUF_SIZE_MAX]);
					cmd_val = RECV_PAK_ERROR;
					axififo_send(0,frame_buff[0], cmd_val);
				}
	 
				axififo_index = i + frame_length + 7;
				i = axififo_index - 1;
				ret = 0;
			} else {
				axififo_index = i;
				ret = 2;
				break;
			}
			
		} else {
			ret = 3;
		}
	}

	return ret;
}

int rx_fifo_data_get(unsigned char chn_id, u_int8_t** data)
{
    image_frame_info_t frame_info;
    ring_buffer_t *rb;
    int len = 0;
    static unsigned long ringbuffer_full_cnt = 0;
    struct fifo_data_info_t *fifo_data_info = &g_axififo_info->fifo_data_info[chn_id];
    
    if(chn_id >= FIFO_NUM || !fifo_data_info || !data) {
        printf("Invalid parameters\n");
        return 0;
    }
    
    rb = fifo_data_info->fifo_data_rb;
    if(!rb) {
        printf("Ring buffer not initialized\n");
        return 0;
    }
    
    if(ringbuffer_is_empty(rb)) {
        if((ringbuffer_full_cnt % 100000) == 0){
            // printf("fifo%d ringbuffer empty cnt:%ld \n", chn_id, ringbuffer_full_cnt);
        }
        usleep(10);
        ringbuffer_full_cnt += 1;
        return 0;
    }
    
    ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(frame_info));
    *data = fifo_data_info->data + frame_info.frame_offset;
    if(*data == NULL)
    {
        printf("data is empty\n");
        return 0;
    }
	len = frame_info.frame_size;
    return len;
}

int fifo_memcpy_rb_avail(unsigned char chn_id)
{
	struct fifo_data_info_t *fifo_data_info = &g_axififo_info->fifo_data_info[chn_id];
	return ringbuffer_avail(fifo_data_info->fifo_data_rb);
}

void fifo_memcpy_data(unsigned char chn_id, unsigned char* data, int size)
{
	image_frame_info_t frame_info;

	ring_buffer_t *rb;
	unsigned char* fifo_addr;
	unsigned long ringbuffer_full_cnt = 0;
	struct fifo_data_info_t *fifo_data_info = &g_axififo_info->fifo_data_info[chn_id];

	if(size>508)
	{
		printf("[%s %d] chn:%d size(%d) > 508 error \n", __func__, __LINE__, chn_id, size);
		return ;
	}

	if(chn_id >= FIFO_NUM){
		printf("[%s] chn id(%d) >= chn max(%d) error \n", __func__, chn_id, FIFO_NUM);
		return ;
	}

	rb = fifo_data_info->fifo_data_rb;


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


#endif
	if(fifo_data_info->ringbuf_offset + AXI_FIFO_BUF_UNIT_SIZE > fifo_data_info->map_size){
		fifo_data_info->ringbuf_offset = 0;
	}

    while(ringbuffer_is_full(rb)) {
    	// if((ringbuffer_full_cnt % 10000) == 0){
    		printf("[%s,%d] chn%d ringbuffer full cnt:%ld \n",__func__,__LINE__, chn_id, ringbuffer_full_cnt);
    	// }
        // usleep(10);
        // ringbuffer_full_cnt += 1;
    }

	fifo_addr = fifo_data_info->data + fifo_data_info->ringbuf_offset;
	memcpy(fifo_addr, data, size);

	// frame_info.frame_index = fifo_data_info->ringbuf_count;
	frame_info.frame_offset = fifo_data_info->ringbuf_offset;
	frame_info.frame_size = size;
	pcie_data_to_queue(rb, &frame_info);

	// data_info[chn_id].dma_stop_flag = 0;

	fifo_data_info->ringbuf_offset += AXI_FIFO_BUF_UNIT_SIZE;
	fifo_data_info->ringbuf_count += 1;
}

void fifo_tx_memcpy_data(unsigned char chn_id, unsigned char* data, int size)
{
	image_frame_info_t frame_info;

	ring_buffer_t *rb;
	unsigned char* fifo_addr;
	unsigned long ringbuffer_full_cnt = 0;
	struct fifo_data_info_t *fifo_data_info = &g_axififo_info->fifo_tx_data_info[chn_id];

	if(size>508)
	{
		printf("[%s %d] chn:%d size(%d) > 508 error \n", __func__, __LINE__, chn_id, size);
		return ;
	}

	if(chn_id >= FIFO_NUM){
		printf("[%s] chn id(%d) >= chn max(%d) error \n", __func__, chn_id, FIFO_NUM);
		return ;
	}

	rb = fifo_data_info->fifo_data_rb;


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
	if(fifo_data_info->ringbuf_offset + AXI_FIFO_BUF_UNIT_SIZE > fifo_data_info->map_size){
		fifo_data_info->ringbuf_offset = 0;
	}

	fifo_addr = fifo_data_info->data + fifo_data_info->ringbuf_offset;
	memcpy(fifo_addr, data, size);

	// frame_info.frame_index = fifo_data_info->ringbuf_count;
	frame_info.frame_offset = fifo_data_info->ringbuf_offset;
	frame_info.frame_size = size;
	pcie_data_to_queue(rb, &frame_info);

	// data_info[chn_id].dma_stop_flag = 0;

	fifo_data_info->ringbuf_offset += AXI_FIFO_BUF_UNIT_SIZE;
	fifo_data_info->ringbuf_count += 1;
}

/*
	函数: static void *axififo_recv_data_pthread(void* parameter)
	作用: 数据接收线程
	参数:
		parameter:线程参数
	返回值:
		线程返回值
*/
static void *axififo_recv_data_pthread(void* parameter)
{
	int recv_cnt;
	unsigned char RecvBuffer[AXIFIFO_RECV_DATA_SIZE];
	while(1){
		for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
			/*
			读fifo长度
			./reg_rw /dev/xdma0_control 0x40024
			*/
			if(fifo_memcpy_rb_avail(fifo_id) < AXI_FIFO_BUF_SIZE - 1024)
				continue;
			recv_cnt = stream_fifo_read_data_len(&g_axififo_info->fifo_reg[fifo_id]);
			if(recv_cnt > 0){
				// printf("func:%s line:%d recv_cnt:%d \n",__func__,__LINE__, recv_cnt);
				/*
				读数据
				./reg_rw /dev/xdma0_control 0x51000
				*/
				stream_fifo_read_data(&g_axififo_info->fifo_reg[fifo_id], RecvBuffer, recv_cnt);
				// SyncSend::GetInstance()->Send((char *)RecvBuffer, recv_cnt);
				// fwrite(RecvBuffer, 1, recv_cnt, fp_fifo_rx);
				// fflush(fp_fifo_rx);
				fifo_memcpy_data(fifo_id, RecvBuffer, recv_cnt);
			}
			//usleep(10);
		}
		usleep(1000);
	}

	return NULL;
}

int axififo_recv(unsigned int fifo_id, unsigned char* data)
{
		/*
		读fifo长度
		./reg_rw /dev/xdma0_control 0x40024
		*/
		int recv_cnt = stream_fifo_read_data_len(&g_axififo_info->fifo_reg[fifo_id]);
		if(recv_cnt > 0){
			/*
			读数据
			./reg_rw /dev/xdma0_control 0x51000
			*/
			stream_fifo_read_data(&g_axififo_info->fifo_reg[fifo_id], data, recv_cnt);
			// printf("recv: ");
			// for(int i = 0;i<recv_cnt;i++)
			// {
			// 	printf("%x ",RecvBuffer[i]);
			// }
			// printf("\n");
			// fifo_memcpy_data(fifo_id, RecvBuffer, recv_cnt);
			// axififo_recv_data_analysis(RecvBuffer,recv_cnt);
		}
		return recv_cnt;
}

/*
	函数: static void *axififo_send_data_pthread(void* parameter)
	作用: 数据接收线程
	参数:
		parameter:线程参数
	返回值:
		线程返回值
*/
static void *axififo_send_data_pthread(void* parameter)
{
    image_frame_info_t frame_info;
    ring_buffer_t *rb;
    int len = 0;
    static unsigned long ringbuffer_full_cnt = 0;
	// long seconds, microseconds,data_sum = 0;
	// double elapsed;
	// struct timeval start, end;
	// gettimeofday(&start, NULL);
	while (1)
	{
		for(int chn_id = 0;chn_id < FIFO_NUM;chn_id++){
			struct fifo_data_info_t *fifo_data_info = &g_axififo_info->fifo_tx_data_info[chn_id];
			u_int8_t* data = NULL;
			if(chn_id >= FIFO_NUM || !fifo_data_info) {
				printf("Invalid parameters\n");
				return nullptr;
			}
			
			rb = fifo_data_info->fifo_data_rb;
			if(!rb) {
				printf("Ring buffer not initialized\n");
				return nullptr;
			}

			if(ringbuffer_is_empty(rb)) {
				if((ringbuffer_full_cnt % 100000) == 0){
					// printf("fifo%d ringbuffer empty cnt:%ld \n", chn_id, ringbuffer_full_cnt);
				}
				usleep(10);
				ringbuffer_full_cnt += 1;
				continue;
			}

			if(axififo_get_send_size(chn_id) <= 4)
			{
				continue;
			}
			
			ringbuffer_get(rb, (uint8_t *)&frame_info, sizeof(frame_info));
			data = fifo_data_info->data + frame_info.frame_offset;
			if(data == NULL)
			{
				printf("data is empty\n");
				return nullptr;
			}
			len = frame_info.frame_size;
			if(len > 0){
				axififo_pure_data_send(chn_id,data, len);
				// gettimeofday(&end, NULL);
				// data_sum += len;
				// seconds = end.tv_sec - start.tv_sec;
				// microseconds = end.tv_usec - start.tv_usec;
				// elapsed = seconds + microseconds*1e-6;
				// printf("axififo_pure_data_send time:%f data_size:%d speed:%fBps\n",elapsed, data_sum, len/elapsed);
				// memcpy(&start,&end,sizeof(struct timeval));
			}
			len = 0;
		}
		// std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

/*
	函数: int axififo_data_init(void)
	作用: axi-fifo相关的资源初始化
	参数:
		无
	返回值:
		正常返回0，错误返回其他
*/
int axififo_data_init(void)
{
	int ret;
	pthread_t recv_tid;
	pthread_t send_tid;

	// fp_fifo_rx = fopen("/tmp/fifo_rx.raw","wb+");
	// if(fp_fifo_rx == NULL){
	// 	printf("[ERROR] open fifo_rx.raw error \n");
	// 	return -1;
	// }

	ret = pcie_bar_open();
	if(ret != 0){
		printf("[ERROR] pcie bar mmap error \n");
		return -1;
	}

	g_axififo_info = (axififo_info_t*)malloc(sizeof(axififo_info_t));
	if(NULL == g_axififo_info){
		printf("[ERROR] axififo info malloc error \n");
		ret = 1;
		goto err_1;
	}

	memset(g_axififo_info, 0, sizeof(axififo_info_t));
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

	/*初始化axi-fifo的配置地址*/
	for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
		g_axififo_info->fifo_data_info[fifo_id].ringbuf_count = 0;
		g_axififo_info->fifo_data_info[fifo_id].ringbuf_offset = 0;
		g_axififo_info->fifo_data_info[fifo_id].map_size = AXI_FIFO_DATA_SIZE;
		g_axififo_info->fifo_data_info[fifo_id].data = (u_int8_t*)malloc(g_axififo_info->fifo_data_info[fifo_id].map_size);
		if(g_axififo_info->fifo_data_info[fifo_id].data == NULL){
			printf("[ERROR] axififo data malloc error \n");
			ret = 2;
			goto err_2;
		}
		g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb = ringbuffer_create(AXI_FIFO_BUF_SIZE*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
		if(g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb == NULL){
			printf("[ERROR] axififo data ringbuffer create error \n");
			ret = 3;
			goto err_3;
		}

		g_axififo_info->fifo_tx_data_info[fifo_id].ringbuf_count = 0;
		g_axififo_info->fifo_tx_data_info[fifo_id].ringbuf_offset = 0;
		g_axififo_info->fifo_tx_data_info[fifo_id].map_size = AXI_FIFO_DATA_SIZE;
		g_axififo_info->fifo_tx_data_info[fifo_id].data = (u_int8_t*)malloc(g_axififo_info->fifo_tx_data_info[fifo_id].map_size);
		if(g_axififo_info->fifo_tx_data_info[fifo_id].data == NULL){
			printf("[ERROR] axififo data malloc error \n");
			ret = 2;
			goto err_2;
		}
		g_axififo_info->fifo_tx_data_info[fifo_id].fifo_data_rb = ringbuffer_create(AXI_FIFO_BUF_SIZE*sizeof(image_frame_info_t),sizeof(image_frame_info_t));
		if(g_axififo_info->fifo_tx_data_info[fifo_id].fifo_data_rb == NULL){
			printf("[ERROR] axififo data ringbuffer create error \n");
			ret = 3;
			goto err_3;
		}

		g_axififo_info->fifo_reg[fifo_id].cfg = (config_reg_t *)pcie_bar_mmap(FPGA_PCIE_AXIFIFO_PHY + fifo_id * FPGA_PCIE_AXIFIFO_INTERVAL, FPGA_PCIE_AXIFIFO_LEN);
		if(g_axififo_info->fifo_reg[fifo_id].cfg == NULL){
			printf("[ERROR] pcie axififo config reg error \n");
			ret = 4;
			goto err_4;
		}

#if 1
		/*初始化axi-fifo的发送数据的buffer地址*/
		g_axififo_info->fifo_reg[fifo_id].send_data = (config_reg_t *)pcie_bar_mmap(FPGA_PCIE_DATA_PHY + fifo_id * FPGA_PCIE_AXIFIFO_INTERVAL, FPGA_PCIE_DATA_LEN);
		if(g_axififo_info->fifo_reg[fifo_id].send_data == NULL){
			printf("[ERROR] pcie axififo data reg error \n");
			ret = 5;
			goto err_5;
		}

		/*初始化axi-fifo的接收数据的buffer地址*/
		g_axififo_info->fifo_reg[fifo_id].recv_data = (void*)((unsigned long)g_axififo_info->fifo_reg[fifo_id].send_data + FPGA_PCIE_READ_DATA_OFFSET);
		stream_fifo_reset(&g_axififo_info->fifo_reg[fifo_id]);
#else
		g_axififo_info->fifo_reg[fifo_id].send_data = g_axififo_info->fifo_reg[fifo_id].cfg->TDFD;
		g_axififo_info->fifo_reg[fifo_id].recv_data = g_axififo_info->fifo_reg[fifo_id].cfg->RDFD;
#endif
	}

	ret = pthread_create(&recv_tid,&attr,axififo_recv_data_pthread,NULL);
	if (ret != 0) {
		printf("[ERROR] Create axififo_recv_data_pthread thread failed %d\n",__func__,ret);
		ret = 6;
		goto err_6;
	}

	ret = pthread_create(&send_tid, &attr, axififo_send_data_pthread,NULL);
	if(ret != 0){
		printf("[Debug] sgdma_h_tx_pthread create error \n");
		return 3;
	}

	pthread_attr_destroy(&attr);

	return 0;
err_6:
err_5:
	for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
		if (g_axififo_info->fifo_reg[fifo_id].send_data) {
			pcie_bar_munmap(g_axififo_info->fifo_reg[fifo_id].send_data, FPGA_PCIE_AXIFIFO_LEN);
		}
	}
err_4:
	for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
		if (g_axififo_info->fifo_reg[fifo_id].cfg) {
			pcie_bar_munmap(g_axififo_info->fifo_reg[fifo_id].cfg, FPGA_PCIE_AXIFIFO_LEN);   
		}
	}
err_3:
	for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
		if(g_axififo_info->fifo_data_info[fifo_id].data){
			free(g_axififo_info->fifo_data_info[fifo_id].data);
		}
	}
err_2:
	for(int fifo_id = 0;fifo_id < FIFO_NUM;fifo_id++){
		if(g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb){
			ringbuffer_deinit(g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb);
		}
	}
err_1:
	free(g_axififo_info);

	return ret;
}

/*
	函数: void axififo_data_destory(void)
	作用: axi-fifo相关的资源释放
	参数:
		无
	返回值:
		无
*/
void axififo_data_destory(void)
{
	int fifo_id;
	if(NULL != g_axififo_info){
		for(fifo_id = 0; fifo_id < FIFO_NUM; fifo_id++){
			if (g_axififo_info->fifo_reg[fifo_id].send_data) {
				pcie_bar_munmap(g_axififo_info->fifo_reg[fifo_id].send_data, FPGA_PCIE_AXIFIFO_LEN);
			}
			if (g_axififo_info->fifo_reg[fifo_id].cfg) {
				pcie_bar_munmap(g_axififo_info->fifo_reg[fifo_id].cfg, FPGA_PCIE_AXIFIFO_LEN);   
			}
			if(g_axififo_info->fifo_data_info[fifo_id].data){
				free(g_axififo_info->fifo_data_info[fifo_id].data);
			}
			if(g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb){
				ringbuffer_deinit(g_axififo_info->fifo_data_info[fifo_id].fifo_data_rb);
			}
		}
		free(g_axififo_info);
	}
	return ;
}

int axififo_query_capacity(Cache_report_inf_t* capacity)
{
	int ret = 1;
	struct fifo_data_info_t* fifo_data_info = NULL;

	ring_buffer_t *rb;
	unsigned int ringbuff_num;
	for(int chn_id = 0; chn_id < FIFO_NUM; chn_id++){
		fifo_data_info = &g_axififo_info->fifo_tx_data_info[chn_id];
		rb = fifo_data_info->fifo_data_rb;
		ringbuff_num = ringbuffer_avail(rb);

		capacity[chn_id].axififo_space = ringbuff_num*AXI_FIFO_BUF_UNIT_SIZE;
	}
	return 0;
}
