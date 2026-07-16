
/*
 * Important:
 *
 * This file using "UTF-8 65001 with BOM" coding.
 * Choose editor coding if you could not see words below.
 *
 * “防控烦人的乱码，编辑器使用上述编码才看得到这句话！”
 * “这句就是故意给编辑器看的，它一般会识别头部，勿删。”
 */
/*
 * File      : ringbuffer.h
 * This file is customized ringbuffer for multi thread header 
 * COPYRIGHT (C) 2019 zmvision
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-03-24     fengyong    初始版本
 * 2024-03-18     fengyong    重构代码，优化元素结构体2的幂限制  
 * 2024-03-20     fengyong    新增fifo空间、容量的辅助接口;增加注释
 */

#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "common.h"

typedef struct {
	uint8_t *data;		
	uint32_t in;
	uint32_t out;
	uint32_t mask;
	uint32_t esize;
}ring_buffer_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @method ringbuffer_create
 * 创建ring_buffer需要的缓冲区及对象指针。
 * 
 * @param {uint32_t} size 元素缓冲区总大小。
 * @param {uint32_t} esize 元素数据结构大小。
 * @return {ring_buffer_t*} 返回rb对象。
 */
ring_buffer_t* ringbuffer_create(uint32_t size, uint32_t esize);
/**
 * @method ringbuffer_is_full
 * 判断rb对象是否满。
 * 
 * @param {ring_buffer_t *} rb rb对象。 
 * @return {bool} 返回true表示满，否则不满。
 */
bool ringbuffer_is_full(ring_buffer_t *rb);

/**
 * @method ringbuffer_is_empty
 * 判断rb对象空间是否空。
 * 
 * @param {ring_buffer_t *} rb rb对象。 
 * @return {bool} 返回true表示空，否则不空。
 */
bool ringbuffer_is_empty(ring_buffer_t *rb);

/**
 * @method ringbuffer_put
 * 将元素数据存入fifo中。
 * 
 * @param {ring_buffer_t *} rb rb对象。 
 * @param {const void *} buf  需要存入fifo中的元素数据指针。
 * @param {uint32_t} len 元素结构体大小/个数。
 * @return {uint32_t} 返回存入fifo中元素的个数。
 */
uint32_t ringbuffer_put(ring_buffer_t *rb, const void *buf, uint32_t len);

/**
 * @method ringbuffer_get
 * 获取rb对象的fifo首元素数据。
 * 
 * @param {ring_buffer_t *} rb rb对象。 
 * @param {void *} buf  存放元素数据指针。
 * @param {uint32_t} len 元素结构体大小/个数。
 * @return {uint32_t} 返回获取到的fifo中元素的个数。
 */
uint32_t ringbuffer_get(ring_buffer_t *rb, void *buf, uint32_t len);

/**
 * @method ringbuffer_len
 * 获取rb对象的fifo中已有的元素的个数。
 * 
 * @param {ring_buffer_t *} rb rb对象。  
 * @return {uint32_t} 返回rb对象中的fifo已有的元素的个数。
 */
uint32_t ringbuffer_len(ring_buffer_t *rb);

/**
 * @method ringbuffer_cap
 * 获取rb对象的fifo中空间容量(元素的个数)。
 * 
 * @param {ring_buffer_t *} rb rb对象。  
 * @return {uint32_t} 返回rb对象的fifo中空间容量(元素的个数)。
 */
uint32_t ringbuffer_cap(ring_buffer_t *rb);

/**
 * @method ringbuffer_avail
 * 获取rb对象的fifo中空间可存元素的个数。
 * 
 * @param {ring_buffer_t *} rb rb对象。  
 * @return {uint32_t} 返回rb对象的fifo中空间可存元素的个数。
 */
uint32_t ringbuffer_avail(ring_buffer_t *rb);

/**
 * @method ringbuffer_reset
 * 重置rb对象的fifo中in、out为0，可用空间为最大容量。
 * 
 * @param {ring_buffer_t *} rb rb对象。  
 * @return {void} 
 */
void ringbuffer_reset(ring_buffer_t *rb);

void ringbuffer_deinit(ring_buffer_t *rb);

void pcie_data_to_queue(ring_buffer_t *rb, image_frame_info_t *frame_info);

#ifdef __cplusplus
}
#endif

#endif /* _RINGBUFFER_H_ */
