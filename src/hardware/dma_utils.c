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
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>
#include "common.h"
#include "dma_utils.h"

static void MapAddrPhy2Vir(struct sgdma_info *dma_info)
{
	dma_info->dma_base_addr = NULL;

    int fd = open(MEM_DEV_NAME,O_RDWR);
    if(fd<0){
        printf("open %s fail \n", MEM_DEV_NAME);
        return ;
    }
#if TEST
	dma_info->dma_base_addr = (int*)malloc(dma_info->addr_info.size);
#else
    dma_info->dma_base_addr = (int*)mmap(NULL,dma_info->addr_info.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,dma_info->addr_info.phy_addr);
#endif
    if(!dma_info->dma_base_addr) {
    	printf("map %s fail \n", MEM_DEV_NAME);
		close(fd);
    	return ;
    }
    close(fd);
    return ;
}

static int UnMapAddrPhy2Vir(volatile void* phy_addr, unsigned long size)
{
    printf("unmem map %p \n",phy_addr);
    return munmap((void *)phy_addr, size);
}



int sgdma_init(int chn_id, char* devicename,struct dma_addr_info* addr_info, unsigned int dma_data_size, struct sgdma_info *dma_info)
{
	int ret;

    dma_info->addr_info.phy_addr = addr_info->phy_addr;
    dma_info->addr_info.size = addr_info->size;

	MapAddrPhy2Vir(dma_info);
	if(!dma_info->dma_base_addr){
    	printf("dma ctrl mmap error .!!!!!!\n");
    	ret = 2;
    	goto err_1;
	}

	dma_info->fd = open(devicename, O_RDONLY);
	if (dma_info->fd < 0) {
		printf("Open %s read failed with error: %s\n", devicename, strerror(errno));
		fclose(dma_info->fd);
		goto err_2;
	}
	dma_info->mem_fd =open(ZMUAV_WRMEM_DEVICE_NAME,O_RDWR);
	if(dma_info->mem_fd <0) {
    	printf("Open %s read failed with error: %s\n", ZMUAV_WRMEM_DEVICE_NAME, strerror(errno));
    	goto err_3;
	}

#if PL_DDR
#else
    ret = ioctl(dma_info->mem_fd, AXIS_FIFO_SET_MALLOC_SIZE, &dma_data_size);
    if (ret) {
        printf("AXIS_FIFO_SET_MALLOC_SIZE ioctl error\n");
        goto err_2;
    }

    ret = ioctl(dma_info->mem_fd, AXIS_FIFO_GET_MALLOC_SIZE, &dma_info->map_size);
    if (ret) {
        printf("AXIS_FIFO_GET_MALLOC_SIZE ioctl error\n");
        goto err_2;
    }

    ret = ioctl(dma_info->mem_fd, AXIS_FIFO_GET_MALLOC_PHY, &dma_info->mem_phy_addr);
    if (ret) {
        printf("AXIS_FIFO_GET_MALLOC_PHY ioctl error\n");
        goto err_2;
    }
#endif

    dma_info->mem_vir_base = (unsigned char *)mmap(NULL, dma_info->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_info->mem_fd,  dma_info->mem_phy_addr); 
    if (dma_info->mem_vir_base == NULL) {
        printf("phy 0x%x mmap error \n", dma_info->mem_phy_addr);
        goto err_4;
    }
    printf("devicename:%s data vir:0x%x phy:0x%x size:0x%x \n", devicename, dma_info->mem_vir_base, dma_info->mem_phy_addr, dma_info->map_size);

#if 0
	int fd = open("/dev/mem",O_RDWR);
	if (fd < 0) {
		printf("Open /dev/mem read failed with error: %s\n", strerror(errno));
		fclose(dma_info->mem_fd);
		goto err_2;
	}

    dma_info->mem_vir_base = (unsigned char *)mmap(NULL, dma_info->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,  dma_info->mem_phy_addr); 
    if (dma_info->mem_vir_base == NULL) {
        printf("phy 0x%x mmap error \n", dma_info->mem_phy_addr);
        goto err_4;
    }
    printf("### devicename:%s data vir:0x%x phy:0x%x size:0x%x \n", devicename, dma_info->mem_vir_base, dma_info->mem_phy_addr, dma_info->map_size);
	
	close(fd);
#endif

    return 0;

err_4:
	if(dma_info->mem_vir_base)
	{
		munmap(dma_info->mem_vir_base, dma_info->map_size);
		ret = ioctl(dma_info->fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
err_3:
	close(dma_info->mem_fd);
err_2:
	close(dma_info->fd);
err_1:
	UnMapAddrPhy2Vir(dma_info->dma_base_addr, addr_info->size);
	return ret;
}



void sgdma_exit(struct sgdma_info *dma_info)
{
	if(dma_info->mem_vir_base){
		munmap(dma_info->mem_vir_base, dma_info->map_size);
		int ret = ioctl(dma_info->fd, AXIS_FIFO_FREE_MALLOC_PHY);
		if (ret) {
			printf("AXIS_FIFO_FREE_MALLOC_PHY ioctl error\n");
		}
	}
	if(dma_info->dma_base_addr){
		UnMapAddrPhy2Vir(dma_info->dma_base_addr, dma_info->addr_info.size);
	}
	if(dma_info->mem_fd > 0){
		close(dma_info->mem_fd);
	}
	if(dma_info->fd > 0){
		close(dma_info->fd);
	}

	return ;
}

void push_mm2s_dma(struct sgdma_info *dma_info, unsigned int offset, int size)
{ /* 添加一个dma帧到发送fifo中 */
	unsigned long long addr;
	addr = (unsigned long long )dma_info->mem_phy_addr + offset;
    *(dma_info->dma_base_addr+0x1c/4) = (int)(addr >> 32);
	*(dma_info->dma_base_addr+0x18/4) = (int)(addr&0xffffffff);
	*(dma_info->dma_base_addr+0x28/4) = size;
}
void get_s2mm_dma(struct sgdma_info *dma_info, unsigned int offset, int size)
{ /* 添加一个dma帧到发送fifo中 */
	unsigned long long addr;
	addr = (unsigned long long)dma_info->mem_phy_addr + offset;
    *(dma_info->dma_base_addr+0x4c/4) = (int)(addr >> 32);
	*(dma_info->dma_base_addr+0x48/4) = (int)(addr&0xffffffff);
	*(dma_info->dma_base_addr+0x58/4) = size;
}

unsigned int getdatacount(struct sgdma_info *dma_info) { 
    unsigned int count_rec=*(dma_info->dma_base_addr+0x58/4);
    return count_rec;
}

void mm2s_dma_enable(struct sgdma_info *dma_info)
{
	*dma_info->dma_base_addr = 0x17003;
}
void mm2s_dma_disable(struct sgdma_info *dma_info)
{
	*(dma_info->dma_base_addr + 4/4) = 0x7000;
}
void s2mm_dma_enable(struct sgdma_info *dma_info)
{
	// *(dma_info->dma_base_addr+0x30/4) = 0x17003;
	*(dma_info->dma_base_addr+0x30/4) = 0x15003;
}
void s2mm_dma_disable(struct sgdma_info *dma_info)
{
    *(dma_info->dma_base_addr+0x34/4) = 0x7000;
}
void close_dma(struct sgdma_info *dma_info)
{
    *dma_info->dma_base_addr = 4;
}

// unsigned int get_mm2s_irq_count(struct sgdma_info *dma_info)
// {/* data to ps */
//     unsigned int count = 0;
//     ioctl(dma_info->fd, XCMEM_IOC_GET_IRQ_COUNT, &count);
// 	if(count < 0){
//     	printf("dma ioctl fail \n");
// 	}
// 	return count;
// }

int SelectBlock(struct sgdma_info *dma_info)
{
	fd_set fdset;
	int fd_max = 0;
	struct timeval timeout;
	while(1) {
		FD_ZERO(&fdset);
		FD_SET(dma_info->fd, &fdset);
		fd_max = dma_info->fd +1;

	    timeout.tv_sec= 0;
	    timeout.tv_usec = 500000;
		int ret = select(fd_max,&fdset,NULL,NULL,&timeout);
		if(ret == 0) {
			int k = 0;
			if(FD_ISSET(dma_info->fd, &fdset)){
				return 1;
			}
			return 0;
		} else if(ret > 0) {
			return 1;
		}
		usleep(1);
	}
	return 0;
}

int SelectBlock2(struct sgdma_info *dma_info)
{
	fd_set fdset;
	int fd_max = 0;
	struct timeval timeout;
	while(1) {
		FD_ZERO(&fdset);
		FD_SET(dma_info->fd, &fdset);
		fd_max = dma_info->fd +1;

	    timeout.tv_sec= 0;
	    timeout.tv_usec = 100;
		int ret = select(fd_max,&fdset,NULL,NULL,-1);
		if(ret == 0) {
			int k = 0;
			if(FD_ISSET(dma_info->fd, &fdset)){
				return 1;
			}
			return 0;
		} else if(ret > 0) {
			return 1;
		}
		usleep(1);
	}
	return 0;
}

#if 0

int sgdma_init(size_t reserve_mem_size, int direction, struct sgdma_info *dma_info)
{
	int fd;
	char devicename[100];
	if(direction == MM2S){
		snprintf(devicename,sizeof(devicename), "/dev/dma_proxy_tx_%d", dma_info->chn_id);
	} else{
		snprintf(devicename,sizeof(devicename), "/dev/dma_proxy_rx_%d", dma_info->chn_id);
	}

	fd = open(devicename,O_RDWR);
	if(fd<0) {
    	printf("open %s fail.!!!!!!",devicename);
    	return -1;
	}

	dma_info->fd = fd;
	dma_info->mmap_size = reserve_mem_size;

	dma_info->data_addr= (unsigned char*)mmap(NULL,dma_info->mmap_size, PROT_READ|PROT_WRITE,MAP_SHARED,fd, 0);
	if(dma_info->data_addr == NULL) {
    	printf("3.mmap fail.!!!!!!\n");
    	return -2;
	}

	//printf("3.data_addr_ = 0x%p, size = 0x%lx mmap ok......", data_addr_, reserve_mem_size);

	ioctl(fd, STOP, NULL);
	ioctl(fd, PORT_RESET, NULL);

	return 0;
}

int SetDmaMode(struct sgdma_info *dma_info)
{
	dma_mode_info_t dma_mode;
	dma_mode.type = DMA_MODE_PS | DMA_MODE_ONE;
	int ret = ioctl(dma_info->fd, SET_DMA_MODE, &dma_mode);
	if(ret < 0) {
		printf("dma ioctl fail.!!!!!! \n");
		return 1;
	}

	return 0;
}

void StopDma(struct sgdma_info *dma_info)
{
	ioctl(dma_info->fd, STOP, NULL);
}

void ResetDma(struct sgdma_info *dma_info)
{
	ioctl(dma_info->fd, STOP, NULL);
	ioctl(dma_info->fd, PORT_RESET, NULL);
}


unsigned int GetDmaCount(struct sgdma_info *dma_info)
{
	unsigned long dma_count = 0;
	int ret = ioctl(dma_info->fd, GET_DMA_COUNT, &dma_count);
	if(ret < 0) {
		printf("dma ioctl fail.!!!!!! \n");
	}

	return dma_count;
}

void SetBDOffset(struct sgdma_info *dma_info, unsigned int bd_offset)
{
	unsigned long offset = bd_offset;
	unsigned int ret = ioctl(dma_info->fd, SET_DMA_BD_OFFSET, &offset);
	if(ret < 0)
		printf("dma ioctl fail.!!!!!! \n");
	return ;
}

int SelectBlock(struct sgdma_info *dma_info)
{
	fd_set fdset;
	int fd_max = 0;
	struct timeval timeout;
	while(1) {
		FD_ZERO(&fdset);
		FD_SET(dma_info->fd, &fdset);
		fd_max = dma_info->fd +1;

	    timeout.tv_sec= 0;
	    timeout.tv_usec = 100;
		int ret = select(fd_max,&fdset,NULL,NULL,&timeout);
		if(ret <= 0) {
			return 0;
		} else if(ret > 0) {
			return 1;
		}
		usleep(1);
	}
	return 0;
}

int SendDmaInfo(dma_info_packet* info, struct sgdma_info *dma_info)
{
	if(dma_info->fd < 0){
		printf("sgdma[%d] not init.!!!!!! \n", dma_info->chn_id);
		return -1;
	}

	int ret = write(dma_info->fd, (void*)info, sizeof(dma_info_packet));
	if(ret) {
		printf("dma SendDmaInfo fail send 0x%x!!!!!! \n",ret);
		return -1;
	}

	return 0;
}

#endif

