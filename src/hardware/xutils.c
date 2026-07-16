/*
 * File      : utils.c
 * This file is utils file  
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-1-12     fengyong    first version
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "xutils.h"

unsigned long diff_timespec(struct timespec *end_tm, struct timespec *start_tm)
{
	struct timespec r;
	int i = 0;
	unsigned long ret = 0;

	r.tv_sec = end_tm->tv_sec - start_tm->tv_sec;
	if (end_tm->tv_nsec < start_tm->tv_nsec) {
		r.tv_nsec = end_tm->tv_nsec + 1000000000L - start_tm->tv_nsec;
		r.tv_sec--;
	} else {
		r.tv_nsec = end_tm->tv_nsec - start_tm->tv_nsec ;
	}

	ret = r.tv_nsec;
	for (i = 0; i < (int)(r.tv_sec); i++)
		ret += 1000000000L;
	
	return ret;
}


void xt_nanosleep(int sec,long nsec)
{
	struct timespec req = {.tv_sec = sec,
						   .tv_nsec = nsec};
	nanosleep(&req,NULL);
}

int save_image_data(char *data_buf, unsigned int image_size, const char *file_name)
{
	FILE *fp = fopen(file_name, "wb+");	
	if (!fp) {
		printf("(Error):%s Cannot open file %s\n",__func__,file_name);
		return 1;
	}

	int ret = fwrite(data_buf, sizeof(unsigned char), image_size,fp);
	if(ret != (int)image_size) {
		printf("write %s failed\n", file_name);
        fclose(fp);    
        return 2;
	}

	fclose(fp);
	return 0;
}

void save_file_data_append(const char *fileName, const unsigned char *imageData, int size) 
{
	FILE *fp = fopen(fileName, "ab+");
	if (fp == NULL) {
		return;
	}
	
	fwrite(imageData, size, 1, fp);
	fclose(fp);
}

int rt_priority_thread_create(pthread_t *thread, int priority,void *(*start_routine) (void *), void *arg)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

	struct sched_param param;
	param.sched_priority = priority; 
	pthread_attr_setschedparam(&attr, &param);

    return pthread_create(thread,&attr,start_routine,arg);
}

static size_t file_size_get(const char* filename) 
{
  struct stat st;
  //return_value_if_fail(filename != NULL, 0);

  if (stat(filename, &st) == 0) {
    return st.st_size;
  }

  return 0;
}

int read_data_to_buf(unsigned char *data_buf, const char *filename)
{
    int ret = 0;    
	FILE *fp = fopen(filename, "rb");	
    if (fp != NULL) {
        int len = file_size_get(filename);
        printf("file %s size=%d\n",filename,len);
        ret = fread((void *)data_buf,len, 1,fp);
        if (1 == ret ) {			
			ret = len;
		}
		else {
			ret = 0;
		}

        fclose(fp);
        return ret;
    }
    
    return 0;	
}

double what_time_is_it_now()
{
    struct timeval time;
    if (gettimeofday(&time,NULL)){
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}


unsigned char check_sum(unsigned char *buff, int length)
{
	unsigned char tmp = 0;
	int i = 0;

	for(i = 0; i < length; i++) {
		tmp += buff[i];
	}

	return tmp;
}

unsigned char check_sum_xor(unsigned char *buff, int length)
{
	unsigned char tmp = 0;
	int i = 0;

	for(i = 0; i < length; i++) {
		tmp ^= buff[i];
	}

	return tmp;
}
