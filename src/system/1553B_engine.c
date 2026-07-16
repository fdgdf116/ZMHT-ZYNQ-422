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

#include "zmuav_wrmem.h"
#include "1553B_engine.h"
#include "common.h"
#include "ringbuffer.h"


#define MEM_SIZE_1553B (32*1024*1024)
struct data_1553b_info_t {
	int mem_fd;
	unsigned char *mem_vir_base; /* ps内存虚拟地址 */
	unsigned int map_size;     /* ps物理地址长度 */
	unsigned int mem_phy_addr; /* ps内存物理地址 */
};  

typedef struct {
	struct data_1553b_info_t data_rx_1553b_info;
	struct data_1553b_info_t data_tx_1553b_info;
} _1553b_info_t;

static _1553b_info_t *g_1553b_info = NULL;

int init_1553B()
{
	int ret = 0;
	g_1553b_info = (_1553b_info_t*)malloc(sizeof(_1553b_info_t));
	if (g_1553b_info == NULL) {
		printf("malloc g_1553b_info failed\n");
		return 1;
	}
	memset(g_1553b_info, 0, sizeof(_1553b_info_t));

	struct data_1553b_info_t* data_rx_1553b_info = &(g_1553b_info->data_rx_1553b_info);
	data_rx_1553b_info->mem_fd = open(ZMUAV_WRMEM_DEVICE_NAME, O_RDWR | O_SYNC);
	if (data_rx_1553b_info->mem_fd <= 0) {
		printf("open /dev/mem failed\n");
		goto err_1;
	}

	data_rx_1553b_info->map_size = MEM_SIZE_1553B;
#if PL_DDR
#else
    ret = ioctl(data_rx_1553b_info->mem_fd, AXIS_FIFO_SET_MALLOC_SIZE, &data_rx_1553b_info->map_size);
    if (ret) {
        printf("AXIS_FIFO_SET_MALLOC_SIZE ioctl error\n");
        goto err_2;
    }

    ret = ioctl(data_rx_1553b_info->mem_fd, AXIS_FIFO_GET_MALLOC_SIZE, &data_rx_1553b_info->map_size);
    if (ret) {
        printf("AXIS_FIFO_GET_MALLOC_SIZE ioctl error\n");
        goto err_2;
    }

    ret = ioctl(data_rx_1553b_info->mem_fd, AXIS_FIFO_GET_MALLOC_PHY, &data_rx_1553b_info->mem_phy_addr);
    if (ret) {
        printf("AXIS_FIFO_GET_MALLOC_PHY ioctl error\n");
        goto err_2;
    }
#endif

    data_rx_1553b_info->mem_vir_base = (unsigned char *)mmap(NULL, data_rx_1553b_info->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, data_rx_1553b_info->mem_fd,  data_rx_1553b_info->mem_phy_addr); 
    if (data_rx_1553b_info->mem_vir_base == NULL) {
        printf("phy 0x%x mmap error \n", data_rx_1553b_info->mem_phy_addr);
        goto err_3;
    }
    printf("rx data vir:0x%x phy:0x%x size:0x%x \n", data_rx_1553b_info->mem_vir_base, data_rx_1553b_info->mem_phy_addr, data_rx_1553b_info->map_size);

	struct data_1553b_info_t* data_tx_1553b_info = &(g_1553b_info->data_tx_1553b_info);
	data_tx_1553b_info->mem_phy_addr = data_rx_1553b_info->mem_phy_addr;
	data_tx_1553b_info->map_size = data_rx_1553b_info->map_size;

    data_tx_1553b_info->mem_vir_base = data_rx_1553b_info->mem_vir_base; 
    if (data_tx_1553b_info->mem_vir_base == NULL) {
        printf("phy 0x%x mmap error \n", data_tx_1553b_info->mem_phy_addr);
        goto err_6;
    }
    printf("tx data vir:0x%x phy:0x%x size:0x%x \n", data_tx_1553b_info->mem_vir_base, data_tx_1553b_info->mem_phy_addr, data_tx_1553b_info->map_size);
	close(data_tx_1553b_info->mem_fd);

	return 0;
err_6:
	if(data_tx_1553b_info->mem_vir_base)
	{
		munmap(data_tx_1553b_info->mem_vir_base, data_tx_1553b_info->map_size);
		data_tx_1553b_info->mem_vir_base = NULL;
		ret = ioctl(data_tx_1553b_info->mem_fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
err_5:
err_4:
err_3:
	if(data_rx_1553b_info->mem_vir_base)
	{
		munmap(data_rx_1553b_info->mem_vir_base, data_rx_1553b_info->map_size);
		data_rx_1553b_info->mem_vir_base = NULL;
		ret = ioctl(data_rx_1553b_info->mem_fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
err_2:
	close(data_rx_1553b_info->mem_fd);
err_1:
	if(g_1553b_info)
	{
		free(g_1553b_info);
		g_1553b_info = NULL;
	}
	return 2;
}

unsigned long long get_1553B_phy()
{
	struct data_1553b_info_t* data_rx_1553b_info = &(g_1553b_info->data_rx_1553b_info);
	unsigned long long phy_addr = 0;
	phy_addr = data_rx_1553b_info->mem_phy_addr;
	printf("1553b phy:0x%llx \n", phy_addr);
	return phy_addr;
}



int read_1553B(u_int32_t offset, u_int8_t* data, u_int32_t len)
{
    // 检查全局结构体是否初始化
    if (g_1553b_info == NULL) {
        printf("g_1553b_info is NULL, init_1553B not called?\n");
        return -1;
    }
    
    struct data_1553b_info_t* data_rx_1553b_info = &(g_1553b_info->data_rx_1553b_info);
    
    // 检查接收通道内存映射是否有效
    if (data_rx_1553b_info->mem_vir_base == NULL) {
        printf("data_rx_1553b_info->mem_vir_base is NULL\n");
        return -1;
    }
    
    // 检查内存访问范围是否越界
    if (offset + len > data_rx_1553b_info->map_size) {
        printf("read out of bounds! offset+len=0x%x > map_size=0x%x\n", offset+len, data_rx_1553b_info->map_size);
        return -1;
    }
    
    printf("read 1553b offset:0x%x len:0x%x \n", (uintptr_t)(data_rx_1553b_info->mem_vir_base + offset), len);
    memcpy(data, data_rx_1553b_info->mem_vir_base + offset , len);
    
    // 添加函数返回值
    return 0;
}


int write_1553B(u_int32_t offset, u_int8_t* data, u_int32_t len)
{
    // 修复：增加空指针检查
    if (g_1553b_info == NULL) {
        printf("g_1553b_info is NULL, init_1553B not called?\n");
        return -1;
    }
    
    struct data_1553b_info_t* data_tx_1553b_info = &(g_1553b_info->data_tx_1553b_info);
    // 修复：检查内存映射是否有效
    if (data_tx_1553b_info->mem_vir_base == NULL) {
        printf("data_tx_1553b_info->mem_vir_base is NULL\n");
        return -1;
    }
    
    printf("write 1553b offset:0x%x len:0x%x \n", (uintptr_t)(data_tx_1553b_info->mem_vir_base + offset), len);
    
    // 修复：检查内存访问范围
    if (offset + len > data_tx_1553b_info->map_size) {
        printf("write out of bounds! offset+len=0x%x > map_size=0x%x\n", offset+len, data_tx_1553b_info->map_size);
        return -1;
    }
    
    memcpy(data_tx_1553b_info->mem_vir_base + offset, data, len);
    
    // 修复：添加返回值
    return 0;
}

int close_1553B()
{
	int ret = 0;
	struct data_1553b_info_t* data_rx_1553b_info = &(g_1553b_info->data_rx_1553b_info);
	struct data_1553b_info_t* data_tx_1553b_info = &(g_1553b_info->data_tx_1553b_info);

	if(data_tx_1553b_info->mem_vir_base)
	{
		munmap(data_tx_1553b_info->mem_vir_base, data_tx_1553b_info->map_size);
		data_tx_1553b_info->mem_vir_base = NULL;
		ret = ioctl(data_tx_1553b_info->mem_fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
	close(data_tx_1553b_info->mem_fd);
	if(data_rx_1553b_info->mem_vir_base)
	{
		munmap(data_rx_1553b_info->mem_vir_base, data_rx_1553b_info->map_size);
		data_rx_1553b_info->mem_vir_base = NULL;
		ret = ioctl(data_rx_1553b_info->mem_fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
	close(data_rx_1553b_info->mem_fd);
	if(g_1553b_info)
	{
		free(g_1553b_info);
		g_1553b_info = NULL;
	}
}