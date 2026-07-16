/*
 * This file is part of the Xilinx DMA IP Core driver tools for Linux
 *
 * Copyright (c) 2016-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is licensed under BSD-style license (found in the
 * LICENSE file in the root directory of this source tree)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <byteswap.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/mman.h>

/* ltoh: little endian to host */
/* htol: host to little endian */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ltohl(x)       (x)
#define ltohs(x)       (x)
#define htoll(x)       (x)
#define htols(x)       (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ltohl(x)     __bswap_32(x)
#define ltohs(x)     __bswap_16(x)
#define htoll(x)     __bswap_32(x)
#define htols(x)     __bswap_16(x)
#endif
/*
 * This file is part of the Xilinx DMA IP Core driver tools for Linux
 *
 * Copyright (c) 2016-present,  Xilinx, Inc.
 * All rights reserved.
 *
 * This source code is licensed under BSD-style license (found in the
 * LICENSE file in the root directory of this source tree)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <byteswap.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/mman.h>

/* ltoh: little endian to host */
/* htol: host to little endian */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ltohl(x)       (x)
#define ltohs(x)       (x)
#define htoll(x)       (x)
#define htols(x)       (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define ltohl(x)     __bswap_32(x)
#define ltohs(x)     __bswap_16(x)
#define htoll(x)     __bswap_32(x)
#define htols(x)     __bswap_16(x)
#endif


#define PCIE_CONTROL_DEV   "/dev/mem"
#include "pcie_reg_rw.h"

static int gs_regrw_fd=0;

#define PCIE_GET_TIME_OFFSE_H (0x0C)
#define PCIE_GET_TIME_OFFSE_L (0x10)

static void* timestamp_addr;



static uint32_t pcie_reg_rd32(void* addr)
{
	uint32_t regVal = *(volatile uint32_t *) addr;
	return (regVal);
}

static void pcie_reg_wr32(void* addr, uint32_t value)
{
	*(volatile uint32_t *) addr = value;
	return;
}

#define PCIE_BAR_WRITE_REG(BaseAddress, RegOffset, Data) \
	pcie_reg_wr32((BaseAddress) + (RegOffset), (uint32_t)(Data))

#define PCIE_BAR_READ_REG(BaseAddress, RegOffset) \
	pcie_reg_rd32((BaseAddress) + (RegOffset))

int pcie_bar_open(void)
{
	const char *device=PCIE_CONTROL_DEV;

	if ((gs_regrw_fd = open(PCIE_CONTROL_DEV, O_RDWR | O_SYNC)) == -1) {
		printf("character device %s opened failed: %s.\n",
			PCIE_CONTROL_DEV, strerror(errno));
		return -errno;
	}

	return 0;
}

void* pcie_bar_mmap(unsigned int phy_address, unsigned int size)
{
	void* user_addr;
	if(size <= 0){
		printf("phy:0x%x mmap size 0x%x error \n", phy_address, size);
		return NULL;
	}
	user_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, gs_regrw_fd,
			   	phy_address);
	if (user_addr == (void *)-1) {
		printf("Memory 0x%lx mapped failed: %s.\n",
			phy_address, strerror(errno));
		//goto close;
		return NULL;
	}

	printf("Memory 0x%lx mapped len:0x%x at address %p.\n", phy_address, size, user_addr);

	return user_addr;
}

unsigned int pcie_bar_read_reg(void* addr, unsigned int offset)
{
	return PCIE_BAR_READ_REG(addr, offset);
}

void pcie_bar_write_reg(void* addr, unsigned int offset, unsigned int value)
{
	PCIE_BAR_WRITE_REG(addr,offset,value);
	return ;
}



void pcie_bar_munmap(void* user_addr, unsigned int size)
{
	if(user_addr != NULL){
		if (munmap(user_addr,size) == -1) {
			printf("Memory 0x%lx mapped failed: %s.\n",
				0, strerror(errno));
		}
	}
	return ;
}

void pcie_bar_close(void)
{
	close(gs_regrw_fd);
	return ;
}

void set_pcie_timestamp_addr(void* addr)
{
	timestamp_addr = addr;
}

void get_pcie_timestamp(pcie_timestamp_t* timestamp)
{
	if(!timestamp_addr){
		timestamp->time_h = 0;
		timestamp->time_l = 0;
		return ;
	}

	timestamp->time_h = pcie_bar_read_reg(timestamp_addr, PCIE_GET_TIME_OFFSE_H);
	timestamp->time_l = pcie_bar_read_reg(timestamp_addr, PCIE_GET_TIME_OFFSE_L);
}

unsigned long pcie_diff_timestamp(pcie_timestamp_t *end_tm, pcie_timestamp_t *start_tm)
{

	unsigned long time, time_end, time_start;

	time_start = ((unsigned long)start_tm->time_h << 32) | start_tm->time_l;
	time_end = ((unsigned long)end_tm->time_h << 32) | end_tm->time_l;
	time = (unsigned long)((time_end - time_start) * 6.6);

	return time;
}