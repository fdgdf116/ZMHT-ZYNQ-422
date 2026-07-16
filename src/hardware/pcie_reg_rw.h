#ifndef _PCIE_REG_RW_H
#define _PCIE_REG_RW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    unsigned int phy_address;
    unsigned int size;
    void* user_addr;
} pcie_bar_mem_info_t;

typedef struct {
    unsigned int time_h;
    unsigned int time_l;
} pcie_timestamp_t;


int pcie_bar_open(void);
void* pcie_bar_mmap(unsigned int phy_address, unsigned int size);

unsigned int pcie_bar_read_reg(void* addr, unsigned int offset);
void pcie_bar_write_reg(void* addr, unsigned int offset, unsigned int value);

void pcie_bar_munmap(void* user_addr, unsigned int size);
void pcie_bar_close(void);


void set_pcie_timestamp_addr(void* addr);
void get_pcie_timestamp(pcie_timestamp_t* timestamp);
unsigned long pcie_diff_timestamp(pcie_timestamp_t *end_tm, pcie_timestamp_t *start_tm);

#ifdef __cplusplus
}
#endif

#endif