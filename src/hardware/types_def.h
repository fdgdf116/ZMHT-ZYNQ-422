/*
 * File      : types_def.h
 * This file is type def header  
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-1-12     fengyong    first version
 * 2020-4-11     fengyong    second version    
 */

#ifndef _TYPES_DEF_H
#define _TYPES_DEF_H

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <wchar.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>


#ifndef _INT8_T_DECLARED
typedef signed char int8_t;
#define _INT8_T_DECLARED
#endif

#ifndef _INT16_T_DECLARED
typedef signed short int16_t;
#define _INT16_T_DECLARED
#endif

#ifndef _INT32_T_DECLARED
typedef signed int int32_t;
#define _INT32_T_DECLARED
#endif

#if 0
#ifndef _INT64_T_DECLARED
typedef signed long long int int64_t;
#define _INT64_T_DECLARED
#endif
#endif 

#ifndef _UINT8_T_DECLARED
typedef unsigned char uint8_t;
#define _UINT8_T_DECLARED
#endif

#ifndef _UINT16_T_DECLARED
typedef unsigned short uint16_t;
#define _UINT16_T_DECLARED
#endif


#ifndef _UINT32_T_DECLARED
typedef unsigned int uint32_t;
#define _UINT32_T_DECLARED
#endif

#if 0
#ifndef _UINT64_T_DECLARED
typedef unsigned long long int uint64_t;
#define _UINT64_T_DECLARED
#endif
#endif 

#define XF_OK                   0

typedef enum {    
    RET_OK                      = XF_OK,
    RET_EOK                     =0,
    RET_NOK                     = -1,
    RET_ERR_UNKNOW              = -2,
    RET_ERR_NULL_PTR            = -3,
    RET_ERR_MALLOC              = -4,
    RET_ERR_OPEN_FILE           = -5,
    RET_ERR_VALUE               = -6,
    RET_ERR_READ_BIT            = -7,
    RET_ERR_TIMEOUT             = -8,
    RET_ERR_PERM                = -9,
    RET_ERR_FAILED              = -10,

    RET_ERR_BASE                = -1000,

    /* The error in stream processing */
    RET_ERR_LIST_STREAM         = RET_ERR_BASE - 1,
    RET_ERR_INIT                = RET_ERR_BASE - 2,
    RET_ERR_STREAM              = RET_ERR_BASE - 4,
    RET_ERR_FATAL_THREAD        = RET_ERR_BASE - 5,   
} XF_RET;

#define log_warn(format, args...) printf(format, ##args) 

#define goto_error_if_fail(p)                           \
  if (!(p)) {                                           \
    log_warn("%s:%d " #p "\n", __FUNCTION__, __LINE__); \
    goto error;                                         \
  }

#define break_if_fail(p)                                \
  if (!(p)) {                                           \
    log_warn("%s:%d " #p "\n", __FUNCTION__, __LINE__); \
    break;                                              \
  }

#define return_if_fail(p)                               \
  if (!(p)) {                                           \
    log_warn("%s:%d " #p "\n", __FUNCTION__, __LINE__); \
    return;                                             \
  }

#define return_value_if_fail(p, value)                  \
  if (!(p)) {                                           \
    log_warn("%s:%d " #p "\n", __FUNCTION__, __LINE__); \
    return (value);                                     \
  }

#endif /* _TYPES_DEF_H */


