#include <thread>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include "network_main.h"
#include "n_event.h"
#include "fifo_engine.h"
#include "system.h"
#include "common.h"
#include "1553B_engine.h"

#ifndef htonll
#include <stdint.h>
static inline uint64_t htonll(uint64_t host64) {
    // Split 64-bit value into high/low 32-bit parts
    uint32_t high = (host64 >> 32) & 0xFFFFFFFF;
    uint32_t low = host64 & 0xFFFFFFFF;
    // Convert each part to network byte order and recombine
    return ((uint64_t)htonl(low) << 32) | htonl(high);
}
#endif

#define AXIFIFO_TX_PAYLOAD_MAX (508)

namespace network {
NetServer::AnswerMapEntry NetServer::mapEntry_[] = {
        { (unsigned short)0x0201, &NetServer::NetDmaMM2SControl },
        { (unsigned short)0x0300, &NetServer::Send1553BData },
        { (unsigned short)0x0301, &NetServer::Read1553BData },
        { (unsigned short)0x0302, &NetServer::DDR_Phy_Get },
		{ (unsigned short)0xff00, &NetServer::WriteFpgaRegister},
		{ (unsigned short)0xff01, &NetServer::ReadFpgaRegister},

	    { (unsigned short)0, NULL },
};
NetServer::AnswerMapEntry NetServer::DatamapEntry_[] = {
        { (unsigned short)0x0204, &NetServer::NetDmaMM2S },
        { (unsigned short)0x0006, &NetServer::FIFOSendData },
	    { (unsigned short)0, NULL },
};

NetServer::NetServer(){
    server_=nullptr;client_=nullptr;mutex_.Init();
}
void handle_pipe(int sig) {
    //不做任何处理即可
}

int NetServer::Init(int port){
    struct sigaction action;
    action.sa_handler = &handle_pipe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGPIPE, &action, NULL);

    /* init server */
    MutexScopedLocker locker(mutex_);
    if(server_) return 0;
    port_ = port;
    server_ = new TcpSocket;
    int ret = server_->CreateServer(port);
    if ( ret ) {
        printf("1.create port:%d server socket fail %d \n", port, ret);
        return -1;
    }
    printf("1.create port:%d server ok...... \n",port);
    if((port == CMD_PORT) || (port == DATA_1553B_PORT)) {
        return thread_.Run(&NetServer::ServiceLoop,this,Thread::PRIORITY_HIGH);
    } 
    else if(port == DATA_DOWN_PORT) {
        // Strip the 16-byte request header; queue all following body bytes.
        return thread_.Run(&NetServer::DataServiceLoop,this,Thread::PRIORITY_NORMAL);
    }
    else
    {
        return -1;
    }
}

void NetServer::DaemodLoop(Thread * thread){
    int ret = 0;
	while( daemod_thread_.IsInterrupted() == false ){
		if(client_ == nullptr){
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		} else {
			if( !client_->IsAlive() ) {
				printf("-----------------2.Cache report client leaving...\n");
				client_->Close();
				client_ = nullptr;
				continue;
			}
            Cache_report_inf_t dma_capacity[FREE_SPACE_NUM_MAX];
            memset(dma_capacity,0x00,sizeof(Cache_report_inf_t)*FREE_SPACE_NUM_MAX);
            sgdma_mm2s_query_capacity(dma_capacity);
            sgdma_vfifo_query_capacity(dma_capacity);
            axififo_query_capacity(dma_capacity);
            for(int chn = 0; chn < FREE_SPACE_NUM_MAX; chn++) {
                // printf("1.Cache report chn:%d can_space:%d pulse_space:%d simulation_space:%d dmaddr_space:%d vfifo_space:%d axififo_space:%d \n",
                //     chn,
                //     dma_capacity[chn].can_space,
                //     dma_capacity[chn].pulse_space,
                //     dma_capacity[chn].simulation_space,
                //     dma_capacity[chn].dmaddr_space,
                //     dma_capacity[chn].vfifo_space,
                //     dma_capacity[chn].axififo_space);
                dma_capacity[chn].chn_id = chn;
                dma_capacity[chn].can_space = htonl(dma_capacity[chn].can_space);
                dma_capacity[chn].pulse_space = htonl(dma_capacity[chn].pulse_space);
                dma_capacity[chn].simulation_space = htonl(dma_capacity[chn].simulation_space);
                dma_capacity[chn].dmaddr_space = htonl(dma_capacity[chn].dmaddr_space);
                dma_capacity[chn].vfifo_space = htonl(dma_capacity[chn].vfifo_space);
                dma_capacity[chn].axififo_space = htonl(dma_capacity[chn].axififo_space);
            }
            /* response */
            ClassResponse response(sizeof(Cache_report_inf_t)*FREE_SPACE_NUM_MAX);
            response.WriteType(0x01);
            response.WritePayload(dma_capacity, 0, sizeof(Cache_report_inf_t)*FREE_SPACE_NUM_MAX);
            ret = SendResponse(&response);

		}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void NetServer::DataServiceLoop(Thread *thread){
    unsigned char* buffer = network_rx_dma_buffer();
    if(!buffer) {
        fprintf(stderr, "[NET 9016 RX] 32 MiB DMA mapping unavailable\n");
        return;
    }
    unsigned int offset = 0;
    uint64_t wraps = 0;
    while(!thread_.IsInterrupted()) {
        TcpSocket* connection = server_->Accept(1);
        if(!connection) continue;
        connection->SetRecvTimeout(0);
        connection->SetRecvBUfSize(4 * 1024 * 1024);
        connection->set_keepalive(3, 3, 3);
        uint64_t bytes = 0;
        Request header = {};
        static_assert(sizeof(Request) == 16, "9016 request header must be 16 bytes");
        unsigned int header_received = 0;
        uint32_t body_remaining = 0;
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while(!thread_.IsInterrupted()) {
            struct pollfd fd = {connection->GetSocketId(), POLLIN, 0};
            int ready = poll(&fd, 1, 100);
            if(ready < 0) {
                if(errno == EINTR) continue;
                break;
            }
            if(ready > 0) {
                if(fd.revents & POLLIN) {
                    unsigned char* destination = NULL;
                    unsigned int length = 0;
                    const bool reading_header = body_remaining == 0;
                    int reserved = 0;
                    if(reading_header) {
                        destination = reinterpret_cast<unsigned char*>(&header) + header_received;
                        length = sizeof(header) - header_received;
                    } else {
                        reserved = network_dma_reserve(&destination, &length);
                        if(length > body_remaining) length = body_remaining;
                    }
                    if(reserved < 0) break;
                    if(reserved > 0) {
                        // All blocks busy (or DMA paused): leave data in TCP for backpressure.
                        usleep(1000);
                    } else {
                        int received = connection->Recv(destination, length);
                        if(received <= 0) {
                            if(received < 0 && errno == EINTR) continue;
                            break;
                        }
                        if(reading_header) {
                            header_received += received;
                            if(header_received == sizeof(header)) {
                                body_remaining = ntohl(static_cast<uint32_t>(header.length));
                                header_received = 0;
                                // Request.length is a signed 32-bit protocol field.
                                if(body_remaining > INT32_MAX) {
                                    fprintf(stderr, "[NET 9016 RX] invalid request length=%u; closing connection\n", body_remaining);
                                    break;
                                }
                            }
                        } else {
                            body_remaining -= received;
                            bytes += received;
                            offset = static_cast<unsigned int>(destination - buffer) + received;
                            network_dma_received(received);
                            if(offset == NETWORK_RX_RING_SIZE) {
                                offset = 0;
                                ++wraps;
                            }
                        }
                    }
                } else if(fd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            }
            clock_gettime(CLOCK_MONOTONIC, &now);
            double seconds = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
            if(seconds >= 2.0) {
                if(bytes) {
                    double rate = bytes / seconds;
                    printf("[NET 9016 RX] devmem-queued payload %.2f MiB/s %.2f Mbit/s bytes=%llu interval=%.3fs buffer_bytes=%u offset=%u wraps=%llu\n",
                           rate / 1048576.0, rate * 8.0 / 1e6,
                           (unsigned long long)bytes, seconds, NETWORK_RX_RING_SIZE, offset,
                           (unsigned long long)wraps);
                    fflush(stdout);
                }
                bytes = 0;
                start = now;
            }
        }
        network_dma_abort_partial();
        connection->Close();
        delete connection;
    }
}
void NetServer::ServiceLoop(Thread *thread){
    int cpu = 0;//控制面跑在cpu0上
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask)<0){
        perror("1.pthread_setaffinity_np!!!!!!\n");return;
    }
    sleep(3);
    printf("1.ServerLoop start \n");
    while( thread_.IsInterrupted() == false ) {
        if(client_==nullptr) {
            client_ = server_->Accept(10);
            if(client_!=nullptr) {
				/* Command connections may stay idle; keep receive blocking. */
				client_->SetRecvTimeout(0);
                client_->SetSendTimeout(3000);
                client_->set_keepalive(3, 3, 3);

				struct sockaddr_in peerAddr;
                socklen_t  peerLen = sizeof(peerAddr);
                int peerfd = client_->GetSocketId();
                getpeername(peerfd, (struct sockaddr *)&peerAddr, &peerLen);
              
                inet_ntop(AF_INET, &peerAddr.sin_addr, pc_ip, sizeof(pc_ip));
                printf("1.cmd client pc address = %s\n",pc_ip);

                /* add event channel */
                Exception::GetInstance()->Init(pc_ip,ENVENT_PORT);

            } else {
            }
        }
        if(client_!=nullptr) {
            char recv_buf[256] = {0} ;
            char * common_buffer = (char*)malloc(4096);
            int ret = client_->RecvFully(&recv_buf,16);
            if(ret>0) {
                Request * req_head = (Request *)&recv_buf;
                req_head->cmd_code = ntohl(req_head->cmd_code);
                req_head->length = ntohl(req_head->length);
                if(req_head->cmd_code == CMD_CODE) {
                    if(req_head->length>0){
                        char * msg = common_buffer;
                        if( (req_head->length+16) > 4096 ) {
                            msg = (char *)malloc(16+req_head->length);
                        }
                        ret = client_->RecvFully((void *)(msg+16),req_head->length);
                        if(ret>0) { 
                            memcpy(msg, &recv_buf, sizeof(Request));
                            
                            ProcMessage(msg);
                            
                        } else {
                            printf("1.client leaving...when recv msg.!!!!!! \n");
                            if(client_){client_->Close();delete client_; client_=nullptr;}
                        }
                    } else {
                        ProcMessage((char*)&recv_buf);
                    }
                } else {
                    printf("1.cmd_code %#x error.!!!!!! \n",req_head->cmd_code);
                }
            } else {
                printf("1.client leaving...when recv head.!!!!!! \n");
                if(client_){client_->Close();delete client_; client_=nullptr;}
            }
            free(common_buffer);
        }
    }
}
void NetServer::ProcMessage(char * msg){
    Request *request = (Request *)msg;
    request->index = ntohs(request->index);
    // printf("1.Start ProcMessage, length 0x%x count 0x%x cmd 0x%x \n",request->length,ntohs(request->count),request->index);
    const AnswerMapEntry *map_entry = mapEntry_;
    while (map_entry->index != 0 ) {
        if (map_entry->index == request->index) {
            (this->*map_entry->func)(request);
            return;
        }
        map_entry++;
    }
    ReportUnknowRequest(request);
}
void NetServer::DataProcMessage(char * msg){
    Request *request = (Request *)msg;
    request->index = ntohs(request->index);
    //printf("1.Start ProcMessage, length 0x%x count 0x%x cmd 0x%x \n",request->length,ntohs(request->count),request->index);
    const AnswerMapEntry *map_entry = DatamapEntry_;
    while (map_entry->index != 0 ) {
        if (map_entry->index == request->index) {
            (this->*map_entry->func)(request);
            return;
        }
        map_entry++;
    }
    ReportUnknowRequest(request);
}
int NetServer::SendResponse(Response *response){
    if (client_ != NULL) {
        int size = sizeof(Response) + ntohl(response->length);
        int ret = client_->Send((void*)response, size);
        //按字节打印出发送数据
        if(ret <= 0) {
            printf("1.socket disconnecting...when send response.!!!!!! \n");
            client_ = NULL;
        }
        return 0;
    } else {
        printf("1.No client connected !!!!!! \n");
        return FAIL;
    }
}
int NetServer::SendResponse(ClassResponse *response){
    Response *res = response->GetResponse();
    SendResponse(res);
    return 0;
}
void NetServer::ReportUnknowRequest(Request* request){
    printf("1.ReportUnknowRequest 0x%x \n", request->index);
    ClassRequest Request_t(request);
    ClassResponse response(sizeof(int));
    int ret = 0;
    ret = htonl(ret);
    response.WritePayload(&ret, 0, sizeof(int));
    response.WriteCount(Request_t.GetCount());
    ret = SendResponse(&response);
}

int NetServer::Send1553BData(Request *request){
	int ret = 0 ;
	ClassRequest Request_t(request);
	unsigned char* data_buf;
	int size = Request_t.GetPayloadSize();
    if( size !=  request->length ) {
        printf("1.Data length not same,opcode = 0x%x size:%d ON_DATA_SIZE:%d !!!!!! \n",request->index,size,request->length);
    }
    /* logic */
    int offset,length = 0 ;
    Request_t.GetPayload(&offset,0,4);
    offset=ntohl(offset);
    Request_t.GetPayload(&length,4,2);
    length=ntohs(length);
    //printf("+++read chn:0x%x +++++\n", chn);

    data_buf = (unsigned char*)Request_t.GetPayloadAddr();
    write_1553B(offset, data_buf+6, length);

    /* response */
	ClassResponse response(sizeof(int));
	response.WriteType(request->index);
	ret = htonl(ret);
	response.WritePayload(&ret, 0, sizeof(int));
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
	return ret;
}

int NetServer::DDR_Phy_Get(Request *request){
	int ret = 0 ;
	ClassRequest Request_t(request);
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x size:%d len:%d !!!!!! \n",request->index,size,request->length);
    }
    /* logic */
    //printf("+++read chn:0x%x +++++\n", chn);
    unsigned long long phy_addr = 0;
    phy_addr = get_1553B_phy();
    phy_addr = htonll(phy_addr);

    /* response */
	ClassResponse response(sizeof(phy_addr));
	response.WriteType(request->index);
	response.WritePayload(&phy_addr, 0, sizeof(unsigned long long));
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
	return ret;
}

int NetServer::Read1553BData(Request *request){
	int ret = 0 ;
	ClassRequest Request_t(request);
	unsigned char* data_buf = (unsigned char*)malloc(LEN_1553B);
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x size:%d len:%d !!!!!! \n",request->index,size,request->length);
    }
    /* logic */
    int offset,length = 0 ;
    Request_t.GetPayload(&offset,0,4);
    offset=ntohl(offset);
    //printf("+++read chn:0x%x +++++\n", chn);

    read_1553B(offset, data_buf, LEN_1553B);

    /* response */
	ClassResponse response(LEN_1553B);
	response.WriteType(request->index);
	response.WritePayload(data_buf, 0, LEN_1553B);
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
    free(data_buf);
	return ret;
}

int NetServer::NetDmaMM2S(Request *request){
    // The synthetic DMA worker owns both TX buffers; ignore network DMA payloads.
    (void)request;
    return 0;
}

int NetServer::FIFOSendData(Request *request){
	int ret = 0 ;
	ClassRequest Request_t(request);
	unsigned char* data_buf;
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x !!!!!! \n",request->index);
    }
    /* logic */
    int chn,flag = 0 ;
    Request_t.GetPayload(&chn,0,4);
    chn=ntohl(chn);

    data_buf = (unsigned char*)Request_t.GetPayloadAddr();
    // axififo_pure_data_send(chn,data_buf+4,size-4);
    int payload_size = size - 4;
    int offset = 0;
    while(offset < payload_size) {
        int send_size = payload_size - offset;
        if(send_size > AXIFIFO_TX_PAYLOAD_MAX) {
            send_size = AXIFIFO_TX_PAYLOAD_MAX;
        }
        // printf("FIFOSendData chn:%d offset:%d size:%d\n",chn,offset,send_size);
        fifo_tx_memcpy_data(chn,data_buf+4+offset,send_size);
        offset += send_size;
    }
    /* response */
	// ClassResponse response(sizeof(int));
	// response.WriteType(request->index);
	// ret = htonl(ret);
	// response.WritePayload(&ret, 0, sizeof(int));
	// response.WriteCount(Request_t.GetCount());
	// ret = SendResponse(&response);
	return ret;
}

int NetServer::NetDmaMM2SControl(Request *request){
	int ret = 0 ;
	ClassRequest Request_t(request);
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x !!!!!! \n",request->index);
    }
    /* logic */
    int chn,flag = 0 ;
    Request_t.GetPayload(&chn,0,4);
    chn=ntohl(chn);
    Request_t.GetPayload(&flag,4,4);
    flag=ntohl(flag);
    switch (flag)
    {
    case 0xaa:
        printf("[Debug] chn:%d dma start \n",chn);
        sgdma_mm2s_start(chn);
        break;
    case 0xbb:
        printf("[Debug] chn:%d dma stop start \n",chn);
        sgdma_mm2s_stop(chn);
        is_sgdma_mm2s_stop(chn);
        printf("[Debug] chn:%d dma stop ok \n",chn);
        break;
    default:
        printf("[Debug] unknow operate\n");
        break;
    }
    /* response */
	ClassResponse response(sizeof(int));
	response.WriteType(request->index);
	ret = htonl(ret);
	response.WritePayload(&ret, 0, sizeof(int));
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
	return ret;
}

int NetServer::WriteFpgaRegister(Request *request)
{
    int ret = 0 ;
	ClassRequest Request_t(request);
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x !!!!!!\n",request->index);
    }
    /* logic */
    unsigned long phy_addr;unsigned int high,low,value;
    for(int i=0;i<size/12;i++){
        Request_t.GetPayload(&high,i*12,4);
        Request_t.GetPayload(&low,i*12+4,4);
        Request_t.GetPayload(&value,i*12+8,4);
        high = ntohl(high);
        low = ntohl(low);
        value = ntohl(value);
        phy_addr = high;
        phy_addr = phy_addr<<32;
        phy_addr |= low;
        int fd = open("/dev/mem",O_RDWR);
        if(fd<0){
            perror("open /dev/mem fail\n");ret=-1;
            ClassResponse response(sizeof(int));
            response.WriteType(request->index);
            response.WriteCount(Request_t.GetCount());
            ret = htonl(ret);
            response.WritePayload(&ret, 0, sizeof(int));
            ret = SendResponse(&response);
            return ret;
        }
        unsigned long aligment_addr = (phy_addr/4096)*4096;
        int* vir_addr = (int*)mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,fd,aligment_addr);
        if( vir_addr == MAP_FAILED ) {
            perror("mmap fail.!!!!!!\n");
            ret=-1;
            ClassResponse response(sizeof(int));
            response.WriteType(request->index);
            response.WriteCount(Request_t.GetCount());
            ret = htonl(ret);
            response.WritePayload(&ret, 0, sizeof(int));
            ret = SendResponse(&response);
            close(fd);
            return ret;
        }
        printf("write 0x%lx reg value 0x%x \n", phy_addr, value);
        *(vir_addr + (phy_addr-aligment_addr)/4) = value;
        close(fd);
        munmap((void*)vir_addr,8192);
    }
    /* response */
	ClassResponse response(sizeof(int));
	response.WriteType(request->index);
	ret = htonl(ret);
	response.WritePayload(&ret, 0, sizeof(int));
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
	return ret;
}

int NetServer::ReadFpgaRegister(Request *request)
{
    int ret = 0 ;
	ClassRequest Request_t(request);
	int size = Request_t.GetPayloadSize();
    if( size != request->length ) {
        printf("1.Data length not same,opcode = 0x%x !!!!!!\n",request->index);
    }
    /* logic */
    int * value = (int *)malloc(size);int pos=0;
    memset(value,0,size);
    for(int i=0;i<size/8;i++){
        unsigned long phy_addr;unsigned int high,low;
        Request_t.GetPayload(&high, (i*8)+0, 4);
        Request_t.GetPayload(&low, (i*8)+4, 4);
        high = ntohl(high);
        low = ntohl(low);
        phy_addr = high;
        phy_addr = phy_addr<<32;
        phy_addr |= low;
        int fd = open("/dev/mem",O_RDWR);
        if(fd<0){
            perror("open /dev/mem fail\n");ret=-1;
            ClassResponse response(sizeof(int));
            response.WriteType(request->index);
            response.WriteCount(Request_t.GetCount());
            ret = htonl(ret);
            response.WritePayload(&ret, 0, sizeof(int));
            ret = SendResponse(&response);
            return ret;
        }
        unsigned long aligment_addr = (phy_addr/4096)*4096;
        int* vir_addr = (int*)mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,fd,aligment_addr);
        if( vir_addr == MAP_FAILED ) {
            perror("mmap fail.!!!!!!\n");
            ret=-1;
            ClassResponse response(sizeof(int));
            response.WriteType(request->index);
            response.WriteCount(Request_t.GetCount());
            ret = htonl(ret);
            response.WritePayload(&ret, 0, sizeof(int));
            ret = SendResponse(&response);
            close(fd);
            return ret;
        }
        value[pos++] = htonl(*(vir_addr + (phy_addr-aligment_addr)/4));
        close(fd);
        munmap((void*)vir_addr,8192);
    }
    /* response */
	ClassResponse response(pos*4);
	response.WriteType(request->index);
	response.WritePayload(value, 0, pos*4);
	response.WriteCount(Request_t.GetCount());
	ret = SendResponse(&response);
	return ret;
}




} //namespace network
