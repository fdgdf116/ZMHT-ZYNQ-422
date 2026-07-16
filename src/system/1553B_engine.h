#ifndef ENGINE_1553B_H_
#define ENGINE_1553B_H_


#ifdef __cplusplus
extern "C" {
#endif
#define LEN_1553B 128

int init_1553B();
int read_1553B(u_int32_t offset, u_int8_t* data, u_int32_t len);
int write_1553B(u_int32_t offset, u_int8_t* data, u_int32_t len);
unsigned long long get_1553B_phy();

#ifdef __cplusplus
}
#endif
#endif /* GPU_GPU_INTERFACE_H_ */
