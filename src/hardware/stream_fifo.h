#ifndef SRC_STREAM_FIFO_H_
#define SRC_STREAM_FIFO_H_

#ifdef __cplusplus
extern "C"
{
#endif

/*
	相关寄存器说明参考 axi-fifo的手册
*/
typedef struct {
	volatile unsigned int ISR; 
	volatile unsigned int IER;
	volatile unsigned int TDFR;
	volatile unsigned int TDFV;
	volatile unsigned int TDFD;
	volatile unsigned int TLR;
	volatile unsigned int RDFR;
	volatile unsigned int RDFO;
	volatile unsigned int RDFD;
	volatile unsigned int RLR;
	volatile unsigned int SRR;
	volatile unsigned int TDR;
	volatile unsigned int RDR;
} config_reg_t;

typedef struct {
	config_reg_t* cfg; /*配置寄存器地址*/
	void *recv_data; /*读取数据地址*/
	void *send_data; /*写入数据地址*/
}stream_fifo_reg_t;



void stream_fifo_reset(stream_fifo_reg_t *stream_fifo_reg);
unsigned int stream_fifo_read_data_len(stream_fifo_reg_t *stream_fifo_reg);
unsigned int stream_fifo_read_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length);
int stream_fifo_write_data(stream_fifo_reg_t *stream_fifo_reg, unsigned char *buffer, int length);

#ifdef __cplusplus
}
#endif

#endif