#ifndef ZMUAV_WRMEM_H
#define ZMUAV_WRMEM_H

#define ZMUAV_WRMEM_IOCTL_MAGIC 'M'
#define ZMUAV_WRMEM_NUM_IOCTLS 7

struct cache_sync {
    unsigned int offset;
    unsigned int size;
};

#define AXIS_FIFO_SET_MALLOC_SIZE   _IOW(ZMUAV_WRMEM_IOCTL_MAGIC, 0,  unsigned int)
#define AXIS_FIFO_GET_MALLOC_SIZE   _IOR(ZMUAV_WRMEM_IOCTL_MAGIC, 1,  unsigned int)
#define AXIS_FIFO_GET_MALLOC_PHY    _IOR(ZMUAV_WRMEM_IOCTL_MAGIC, 2,  unsigned long)
#define AXIS_FIFO_FREE_MALLOC_PHY   _IO(ZMUAV_WRMEM_IOCTL_MAGIC, 3)


#define AXIS_FIFO_SET_RDWR_DIRECTION  _IOW(ZMUAV_WRMEM_IOCTL_MAGIC, 4, unsigned int)

#define AXIS_FIFO_SYNC_FOR_CPU      _IOW(ZMUAV_WRMEM_IOCTL_MAGIC, 5, struct cache_sync)
#define AXIS_FIFO_SYNC_FOR_DEVICE   _IOW(ZMUAV_WRMEM_IOCTL_MAGIC, 6, struct cache_sync)

#endif
