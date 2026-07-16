/*
 * File      : utils.h
 * This file is utils header  
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-1-12     fengyong    first version
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * General Purpose Utilities
 */
#define min(X, Y)				\
	({ typeof(X) __x = (X);			\
		typeof(Y) __y = (Y);		\
		(__x < __y) ? __x : __y; })

#define max(X, Y)				\
	({ typeof(X) __x = (X);			\
		typeof(Y) __y = (Y);		\
		(__x > __y) ? __x : __y; })

#define __compiler_offsetof(a,b) __builtin_offsetof(a,b)
#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

double what_time_is_it_now();
unsigned char check_sum(unsigned char *buff, int length);
unsigned char check_sum_xor(unsigned char *buff, int length);
unsigned long diff_timespec(struct timespec *end_tm, struct timespec *start_tm);
void xt_nanosleep(int sec,long nsec);
int save_image_data(char *data_buf, unsigned int image_size, const char *file_name);  
int rt_priority_thread_create(pthread_t *thread, int priority,void *(*start_routine) (void *), void *arg);
int read_data_to_buf(unsigned char *data_buf, const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* _UTILS_H_ */


