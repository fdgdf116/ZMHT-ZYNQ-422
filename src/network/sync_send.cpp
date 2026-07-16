#include "sync_send.h"
#include "fifo_engine.h"
#include "system.h"

namespace network {

SyncSend* SyncSend::instance_ = nullptr;

SyncSend* SyncSend::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new SyncSend(nullptr, DATA_PORT);
    }
    return instance_;
}

SyncSend::SyncSend(const char* ip, int port):EthSend(ip, port){
	server_ = nullptr;
	exc_client_ = nullptr;
}

SyncSend::~SyncSend() {

}

int SyncSend::Init(){
	bool ret = false;
	if(server_)return 0;
	server_ = new TcpSocket();

	is_interrupt_ = false;
	ret = server_->CreateServer(port_);
	if( ret ) {
		printf("create data server socket fail ret = %d!!!!!!\n", ret);
		return FAIL;
	}
	printf("create data server success......\n");
	return daemond_thread_.Run(&SyncSend::DaemodLoop,this,Thread::PRIORITY_HIGH);
}
void SyncSend::DeInit() {
	if(server_){
		server_->Close();delete server_;server_=nullptr;
	}
	daemond_thread_.Interrupt();
	daemond_thread_.Join();
}

int SyncSend::SendResponse(Response *response){
        int size = sizeof(Response) + ntohl(response->length);
        int ret = Send((char*)response, size);
		return ret;
}
int SyncSend::SendResponse(ClassResponse *response){
    Response *res = response->GetResponse();
    SendResponse(res);
    return 0;
}

void SyncSend::DaemodLoop(Thread * thread){
	int size,chn_id = 0;
	unsigned char* data_buf = nullptr;//(unsigned char*)malloc(RECV_DMA_DATA_SIZE);
	// unsigned char* fifo_data_buf = nullptr;
	unsigned char** ptr = &data_buf;
	// unsigned char RecvBuffer[AXIFIFO_RECV_DATA_SIZE];
	while(!is_interrupt_){
		if(exc_client_ == nullptr){
			TcpSocket * client = server_->Accept(10);
			if(client) {
				printf("-----------------accept data client......\n");
				exc_client_ = client;
				exc_client_->SetRecvTimeout(3000);
				exc_client_->SetSendTimeout(3000);
				exc_client_->set_keepalive(200, 60, 20);
			} else {
				printf("2.data server no accept client connect request!!!!!!\n");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		} else {
			if( !exc_client_->IsAlive() ) {
				printf("-----------------2.client leaving...\n");
				exc_client_->Close();
				exc_client_ = nullptr;
				continue;
			}
			for(int dma_id = 0; dma_id < SGDMA_NUM; dma_id++)
			{
				size = rx_sgdma_data_get(dma_id, ptr);
				// size = recv_dma(data_buf);
				if(size > 0) {
					ClassResponse response(size+4);
					response.WriteReqcode(UP_CODE);
					response.WriteType(0x03);
					chn_id = htonl(dma_id);
					response.WritePayload(&chn_id, 0, 4);
					response.WritePayload(data_buf, 4, size);
					SendResponse(&response);
					// dma_disable();
					// printf("send sgdma data_id:%d size = %d\n",dma_id,size);
				}
			}
			for(int fifo_id = 0; fifo_id < FIFO_NUM; fifo_id++)
			{
				size = rx_fifo_data_get(fifo_id, ptr);
				// size = axififo_recv(fifo_id, RecvBuffer);
				if(size > 0) {
					ClassResponse response(size+4);
					response.WriteReqcode(UP_CODE);
					response.WriteType(0x06);
					chn_id = htonl(fifo_id);
					response.WritePayload(&chn_id, 0, 4);
					response.WritePayload(data_buf, 4, size);
					SendResponse(&response);
					// printf("send fifo_id:%d data size = %d\n",fifo_id,size);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	// free(data_buf);
}

bool SyncSend::IsActive(){
	if(exc_client_)
		return exc_client_->IsAlive();
	else
		return false;
}

int SyncSend::Send(const char * buf, int size){
	if( exc_client_ ) {
		bool ret = exc_client_->SendFully(buf, size);
		if(!ret) {
			printf("-----------------2.client leaving...\n");
			exc_client_->Close();
			exc_client_ = nullptr;
			return FAIL;
		}
	}
	return OK;
}

int SyncSend::DmaSend(char * buf, int size, int chn_id){
	ClassResponse response(size+4);
	response.WriteReqcode(UP_CODE);
	response.WriteType(0x03);
	response.WritePayload(&chn_id, 0, 4);
	response.WritePayload(buf, 4, size);
	SendResponse(&response);
	// printf("send dma data size = %d\n",size);
	return OK;
}

} /* namespace network */