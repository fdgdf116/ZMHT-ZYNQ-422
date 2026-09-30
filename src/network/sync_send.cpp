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
    return SendResponse(res);
}

void SyncSend::DaemodLoop(Thread * thread){
	(void)thread;
	int size;
	unsigned char* data_buf = nullptr;
	unsigned char** ptr = &data_buf;
	const size_t payload_capacity = RECV_DMA_DATA_SIZE;
	struct UploadHeader {
		Response response;
		uint32_t channel;
	} header = {};
	static_assert(sizeof(UploadHeader) == 20, "9014 wire header must be 20 bytes");
	Response* response = &header.response;
	response->cmd_code = htonl(UP_CODE);
	response->index = htons(0x03);
	unsigned long long interval_bytes = 0;
	unsigned long long interval_payload_bytes = 0;
	unsigned long long interval_frames = 0;
	std::chrono::steady_clock::time_point rate_start =
		std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point last_alive_check = rate_start;
	printf("[NET 9014] DMA vector sender ready, max payload: %u bytes\n",
		   (unsigned int)payload_capacity);

	while(!is_interrupt_){
		if(exc_client_ == nullptr){
			exc_client_ = server_->Accept(10);
			if(exc_client_) {
				printf("[NET 9014] data client connected\n");
				exc_client_->SetSendBUfSize(4*1024*1024);
				exc_client_->SetSendTimeout(3000);
				exc_client_->set_keepalive(200, 60, 20);
				interval_bytes = 0;
				interval_payload_bytes = 0;
				interval_frames = 0;
				rate_start = std::chrono::steady_clock::now();
				last_alive_check = rate_start;
			} else {
			}
			continue;
		}

		std::chrono::steady_clock::time_point now =
			std::chrono::steady_clock::now();
		if(std::chrono::duration<double>(now - last_alive_check).count() >= 1.0) {
			last_alive_check = now;
			if(!exc_client_->IsAlive()) {
				printf("[NET 9014] data client disconnected\n");
				exc_client_->Close();
				delete exc_client_;
				exc_client_ = nullptr;
				continue;
			}
		}

		bool did_work = false;
		bool send_failed = false;
		for(int dma_id = 0; dma_id < SGDMA_NUM; ++dma_id) {
			size = rx_sgdma_data_get(dma_id, ptr);
			if(size > 0) {
				did_work = true;
				if((size_t)size > payload_capacity) {
					printf("[NET 9014] DMA frame too large: %d, buffer: %u\n",
						   size, (unsigned int)payload_capacity);
					continue;
				}
				response->length = htonl((uint32_t)(sizeof(uint32_t) + size));
				header.channel = htonl((uint32_t)dma_id);
				struct iovec packet[] = {
					{&header, sizeof(header)},
					{data_buf, (size_t)size}
				};
				int packet_size = sizeof(header) + size;
				// Keep this frame until sendmsg has consumed every byte. With 17 RX
				// blocks and 16 queued descriptors, the producer cannot wrap onto
				// this block before this sole consumer takes the next descriptor.
				if(!exc_client_->SendVectorFully(packet, 2)) {
					printf("[NET 9014] SendVectorFully failed, data client disconnected\n");
					exc_client_->Close();
					delete exc_client_;
					exc_client_ = nullptr;
					send_failed = true;
					break;
				}
				interval_bytes += packet_size;
				interval_payload_bytes += size;
				++interval_frames;
			}
		}
		if(send_failed) {
			continue;
		}

		now = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now - rate_start).count();
		if(elapsed >= 2.0) {
			printf("[NET 9014 TX] payload %.2f MiB/s %.2f Mbit/s "
				   "with_header %.2f MiB/s %.2f Mbit/s "
				   "payload_bytes=%llu sent_bytes=%llu frames=%llu interval=%.3fs\n",
				   interval_payload_bytes / elapsed / 1048576.0,
				   interval_payload_bytes * 8.0 / elapsed / 1000000.0,
				   interval_bytes / elapsed / 1048576.0,
				   interval_bytes * 8.0 / elapsed / 1000000.0,
				   interval_payload_bytes, interval_bytes, interval_frames, elapsed);
			fflush(stdout);
			interval_bytes = 0;
			interval_payload_bytes = 0;
			interval_frames = 0;
			rate_start = now;
		}

		if(!did_work) {
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}

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

} /* namespace network */
