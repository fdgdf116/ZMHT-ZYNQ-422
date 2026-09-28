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

#include "dma_buffer_mem.h"
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
    if(g_1553b_info) return 0;
    g_1553b_info = calloc(1, sizeof(*g_1553b_info));
    if(!g_1553b_info) return -1;
    struct data_1553b_info_t* rx = &g_1553b_info->data_rx_1553b_info;
    rx->mem_fd = -1;
    if(dma_buffer_map(DMA_BUFFER_1553B, MEM_SIZE_1553B, &rx->mem_vir_base, &rx->mem_phy_addr)) {
        free(g_1553b_info);
        g_1553b_info = NULL;
        return -1;
    }
    rx->map_size = MEM_SIZE_1553B;
    // Both directions share exactly one mapping; close it only once.
    g_1553b_info->data_tx_1553b_info = *rx;
    return 0;
}

unsigned long long get_1553B_phy()
{
    if(!g_1553b_info) return 0;
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
    if (offset > data_rx_1553b_info->map_size || len > data_rx_1553b_info->map_size - offset) {
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
    if (offset > data_tx_1553b_info->map_size || len > data_tx_1553b_info->map_size - offset) {
        printf("write out of bounds! offset+len=0x%x > map_size=0x%x\n", offset+len, data_tx_1553b_info->map_size);
        return -1;
    }
    
    memcpy(data_tx_1553b_info->mem_vir_base + offset, data, len);
    
    // 修复：添加返回值
    return 0;
}

int close_1553B()
{
    if(!g_1553b_info) return 0;
    struct data_1553b_info_t* rx = &g_1553b_info->data_rx_1553b_info;
    if(rx->mem_vir_base && dma_buffer_unmap(rx->mem_vir_base) != 0) return -1;
    free(g_1553b_info);
    g_1553b_info = NULL;
    return 0;
}
