#ifndef SRC_SYNC_SEND_H_
#define SRC_SYNC_SEND_H_

#include "eth_send.h"
#include "thread.h"
#include "cmd_packet.h"
namespace network {
#define DATA_PORT 9014
#define DATA_PORT_BENCHMARK_MODE 0
class SyncSend: public EthSend {
public:
	static SyncSend* GetInstance();
    // 禁止拷贝构造和赋值
    SyncSend(const SyncSend&) = delete;
    SyncSend& operator=(const SyncSend&) = delete;
	int Init();
	void DeInit();
	bool IsActive();
	int SendResponse(Response *response);
	int SendResponse(ClassResponse *response);
	int Send(const char* buf, int size);
private:
	SyncSend(const char* ip, int port);
	virtual ~SyncSend();

	Thread daemond_thread_;
	void DaemodLoop(Thread * thread);
    // 静态实例指针
    static SyncSend* instance_;
};

} /* namespace network */

#endif /* SRC_SYNC_SEND_H_ */
