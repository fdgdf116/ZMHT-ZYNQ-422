#ifndef _DMA_UTILS_H
#define _DMA_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

// #define SGDMA_NUM (10)

struct dma_addr_info {
	unsigned int phy_addr; /* ps内存物理地址 */
	unsigned int size;     /* ps物理地址长度 */
};

struct sgdma_info {
	int fd;
	int mem_fd;
	int * dma_base_addr;
	unsigned char *mem_vir_base; /* ps内存虚拟地址 */
	unsigned int map_size;     /* ps物理地址长度 */
	unsigned int mem_phy_addr; /* ps内存物理地址 */
	struct dma_addr_info addr_info;
};

int sgdma_init(int chn_id, const char* devicename,struct dma_addr_info* addr_info, unsigned int dma_data_size, struct sgdma_info *dma_info);
void sgdma_exit(struct sgdma_info *dma_info);
void push_mm2s_dma(struct sgdma_info *dma_info, unsigned int offset, int size);
void get_s2mm_dma(struct sgdma_info *dma_info, unsigned int offset, int size);
void mm2s_dma_enable(struct sgdma_info *dma_info);
void mm2s_dma_disable(struct sgdma_info *dma_info);
void s2mm_dma_enable(struct sgdma_info *dma_info);
void s2mm_dma_disable(struct sgdma_info *dma_info);
void close_dma(struct sgdma_info *dma_info);
int SelectBlock(struct sgdma_info *dma_info);
int SelectBlock2(struct sgdma_info *dma_info);
unsigned int getdatacount(struct sgdma_info *dma_info);
// unsigned int get_mm2s_irq_count(struct sgdma_info *dma_info);
#if 0
struct sgdma_info {
	int fd;
	int chn_id;
	unsigned int mmap_size;
	unsigned char* data_addr;
};

int sgdma_init(size_t reserve_mem_size, int direction, struct sgdma_info *dma_info);
int SetDmaMode(struct sgdma_info *dma_info);
void StopDma(struct sgdma_info *dma_info);
void ResetDma(struct sgdma_info *dma_info);
unsigned int GetDmaCount(struct sgdma_info *dma_info);
void SetBDOffset(struct sgdma_info *dma_info, unsigned int bd_offset);
int SelectBlock(struct sgdma_info *dma_info);
int SendDmaInfo(dma_info_packet* info, struct sgdma_info *dma_info);

#endif

#ifdef __cplusplus
}
#endif

#endif
