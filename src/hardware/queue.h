#ifndef QUEUE_H_
#define QUEUE_H_

typedef unsigned char alt_u8;

typedef struct{
    alt_u8 * m_dataBuf; //队列总缓存数据区
    alt_u8 * m_tmpBuf;  //临时缓存Front函数用
    int m_length;   //内存队列长度
    int m_unitmaxsize;  //front函数返回内存块最大长度
    int m_validNum; //队列中有效数据个数
    long int m_incount;  //总入队字节数
    long int m_outcount; //总出队字节数
    long int m_curBlockPoint;    //当前数据缓存获取位置
}QueueStruct;
    
    //初始化设置分配内存队列容量 _total为容量大小（字节） _outMaxSize为每次出队列的最大内存大小
    int QueueInitial(QueueStruct *pQueue,int _total, int _outMaxSize);

    //释放分配内存
    int QueueFree(QueueStruct *pQueue);

    //清空内存队列，但不释放内存
    int QueueReset(QueueStruct *pQueue);

    //将队列内存数据整体赋值为_val
    int QueueMemset(QueueStruct *pQueue,alt_u8 _val);

    //将内存块_buf插入缓存，内存块大小为_size
    int QueuePush(QueueStruct *pQueue,alt_u8 * _buf, int _size);

    //将内存块_len个字节数据出队
    int QueuePop(QueueStruct *pQueue,int _len);

    //访问队首内存块，_buf为内存指针,返回值为0表示正常
    int QueueFront(QueueStruct *pQueue,alt_u8 ** pBuf, int _size);

    //获取总入队字节数
    long int QueueGetInCount(QueueStruct *pQueue);

    //获取总出队字节数
    long int QueueGetOutCount(QueueStruct *pQueue);

    //获取缓存队列内存容量
    int QueueGetSpace(QueueStruct *pQueue);

    //获取队列中有效数据大小
    int QueueGetUsingSpace(QueueStruct *pQueue);

    //获取队列中剩余空间大小
    int QueueGetLeavingSpace(QueueStruct *pQueue);

    //顺序获取_size字节数据缓存
    alt_u8 * GetBlockPointer(QueueStruct *pQueue,int _size);

#endif /*QUEUE_H_*/
