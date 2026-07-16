#ifndef __EVENT_PORT_H_
#define __EVENT_PORT_H_

#include "tcp_socket.h"
#include "singleton.h"
#include "thread.h"
#include <queue>

namespace network {

using namespace network;
using namespace std;

typedef struct node_ {
	int code;
	int size;
	unsigned short type;
	unsigned short id;
	unsigned short time;
	unsigned short crc;
	char *buf;
} node;

class Exception{
public:
	virtual ~Exception();
	int Init(const char *address, int port);
	void Send(unsigned short type,unsigned short id, const char *buf,int size);
	bool IsConnected();
	void Disconnect();
private:
	queue<node>Message_Que;
	TcpSocket *exc_client_;
	Mutex mutex_;
	Thread exceptionthread_;
	ThreadCond cond_;
	bool connected_;
	char address_[128];
	int port_;
	bool kill_client_;
	void SendException(Thread *thread);
	DECLARE_SINGLETON(Exception);
};

} //namespace network

#define SEND_EVENT(TYPE, ID,fmt,...) \
	do{SendEvent(TYPE,ID,"[%s:%d:] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);}while(0)

void SendEvent(int type, int id,const char *format, ...);

namespace event{

enum {
	EVENT_IFNO = 0, // 提示
	EVENT_WARN,     // 注意
	EVENT_ERROR,    // 错误
	EVENT_PANIC,    // 严重错误
};
enum{
	/* system init */
	RESERVED_MEM_ERROR = 0x0102,
	/* file */
	CHANNEL_SEND_BYTE = 0x0402,
	CHANNEL_SEND_STAUS = 0x0403,
	MESSAGE_HEARTBEAT = 0x0501,
	MESSAGE_TEMPERATURE = 0x0502,
};

} // namespace event

#endif /* __EVENT_PORT_H_ */
