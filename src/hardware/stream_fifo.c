#include <stdint.h>
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

#include "stream_fifo.h"

static uint32_t stream_fifo_rd32(void* addr)
{
    uint32_t regVal = *(volatile uint32_t *) addr;
    return (regVal);
}

static void stream_fifo_wr32(void* addr, uint32_t value)
{
    *(volatile uint32_t *) addr = value;
    return;
}

/*
	函数:void stream_fifo_reset(stream_fifo_reg_t *stream_fifo_reg)
	作用: stream fifo 复位
	参数:
		stream_fifo_reg: fifo 寄存器结构体
	返回值:
		无
*/
void stream_fifo_reset(stream_fifo_reg_t *stream_fifo_reg)
{
	/*
	fifo复位
	./reg_rw /dev/xdma0_control 0x40008 32 0xa5
	./reg_rw /dev/xdma0_control 0x40018 32 0xa5
	./reg_rw /dev/xdma0_control 0x40028 32 0xa5
	*/
	stream_fifo_reg->cfg->TDFR = 0xa5;
	stream_fifo_reg->cfg->RDFR = 0xa5;
	stream_fifo_reg->cfg->SRR = 0xa5;
}


/*
	函数: void stream_fifo_read_data_len(stream_fifo_reg_t *stream_fifo_reg)
	作用: 读取数据长度
	参数:
		stream_fifo_reg: fifo 寄存器结构体
	返回值:
		实际读取到的长度
*/
unsigned int stream_fifo_read_data_len(stream_fifo_reg_t *stream_fifo_reg)
{

	unsigned int stream_fifo_inlen = 0;
#if 1
 	volatile uint32_t read_start = 0;
	/*读取fifo接收长度xdma0_control 0x40024*/
    read_start = stream_fifo_reg->cfg->RDFO;
    // if(read_start > 0){
    //     stream_fifo_inlen = stream_fifo_reg->cfg->RLR;
    //     stream_fifo_inlen &= 0x007FFFFF;
    //     stream_fifo_inlen = stream_fifo_inlen / 4;
    // }
#else

	stream_fifo_inlen = stream_fifo_reg->RLR;
	stream_fifo_inlen &= 0x007FFFFF;
	stream_fifo_inlen = stream_fifo_inlen / 4;

#endif
	return read_start;
}


/*
	函数: stream_fifo_read_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length)
	作用: 读取数据
	参数:
		stream_fifo_reg: fifo 寄存器结构体
		buffer:存放buffer的地址
		length:读取buffer的长度
	返回值:
		实际读取到的数据长度
*/
unsigned int stream_fifo_read_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length)
{
	int i = 0;
	unsigned int data;
	for(i = 0; i < length; i++) { //0x100
		data = stream_fifo_rd32(stream_fifo_reg->recv_data);
		buffer[i]= (data & 0xff);
	}
    return i;
}

/*
	函数: stream_fifo_write_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length)
	作用: 写入数据
	参数:
		stream_fifo_reg: fifo 寄存器结构体
		buffer:存放buffer的地址
		length:写入buffer的长度
	返回值:
		实际写入的数据长度
*/
int stream_fifo_write_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length)
{
	int i = 0;
	unsigned int tmp = 0;
	unsigned int free_size = stream_fifo_reg->cfg->TDFV;
	if(free_size >= 4){
		free_size -= 4;
	}
	else{
	    free_size = 0;
	}
	unsigned int send_size = length < free_size ? length : free_size;
	// if(send_size){
		for(i = 0; i < send_size; i++) {
			tmp = buffer[i];
			/*
			写数据
			./reg_rw /dev/xdma0_control 0x50000
			*/
			stream_fifo_wr32(stream_fifo_reg->send_data, tmp);
		}
		/*
		写数据长度
		./reg_rw /dev/xdma0_control 0x40014
		*/
        stream_fifo_reg->cfg->TLR=send_size*4;
	// 	printf("[Debug] %s write len:(%d) \n", __func__, send_size);
	// }
	// else
	// {
	// 	printf("[Debug] TDFV no ready\n");
	// }
	//SLOGI("[Debug] %s send len:(%d-%d) \n", __func__, length, i);
	return i;
}
