#include "n_event.h"
#include <iostream>
#include <fstream>
#include <stdarg.h>

void SendEvent(int type, int id, const char *format, ...) {
	if(format == NULL)
		network::Exception::GetInstance()->Send((unsigned short)type,id,NULL,0);
	else {
        char buf[512];
        va_list ap;
        va_start(ap, format);
        vsprintf(buf, format, ap);
        printf("buf = %s,strlen(buf) = %ld\n",buf,strlen(buf));
        network::Exception::GetInstance()->Send((unsigned short)type,id,buf,strnlen(buf,512)+1);
        va_end(ap);
	}
}

namespace network {
using namespace event;

#define MESSAGE_HEADER 16
#define EVENT_CODE 0x06CCF0FF

Exception::Exception() {
	exc_client_ = new TcpSocket();
	cond_.Init();   // sem_init();
	mutex_.Init();
	connected_ = false;
	port_ = 0;
	memset(address_,0,sizeof(address_));
}

Exception::~Exception() {
	if(NULL != exc_client_) { delete exc_client_; exc_client_=nullptr; }
}

int Exception::Init(const char *address, int port) {
	if( exceptionthread_.IsAlive() ){
		printf("is have a event port.\n");
		return -1;
	}
	bool ret = false;
	strcpy(address_, address);
	port_= port;
	return exceptionthread_.Run(&Exception::SendException, this);
}

void Exception::Disconnect() {
	printf("Exception::Disconnect\n");
	exceptionthread_.Interrupt();
	exc_client_->Close();connected_=false;kill_client_=true;
	sleep(1);
	exceptionthread_.Kill();
}

void Exception::Send(unsigned short type,unsigned short id, const char *buf,int size) {
	node tmp = {0};
	if(size > 0) {
		tmp.buf = (char *)malloc(size);
		if(tmp.buf == NULL) {
			fprintf(stderr,"malloc failed!");
			return;
		}
		memset(tmp.buf,0,size);
		memcpy(tmp.buf,buf,size);
	}

	tmp.code = htonl(EVENT_CODE);
	tmp.type = htons(type);
	tmp.size = htonl(size);
	tmp.id = htons(id);

	MutexScopedLocker locker(mutex_);
	Message_Que.push(tmp);
	cond_.PostCond();                                   // 生产
	return;
}

bool Exception::IsConnected() {
	return connected_;
}
void Exception::SendException(Thread *thread) {
	int ret =0; node tmp; int size =0;
	while( !exceptionthread_.IsInterrupted() ) {
		printf("EVENT PORT thread 1!! %x\n",this);
		kill_client_;
		while( !exc_client_->IsConnected() ) {
			ret = exc_client_->Connect(address_,port_,800);
			if(ret == true) {
				printf("1.---event connected...... \n");
				connected_ = true;
				break;
			} else {
				// printf("1.---EVENT PORT no connect !!!!!! \n");
			}
			if(kill_client_==true)break;
			sleep(1);
		}

		cond_.WaitCond(1);                           // 消费一个 定时1秒 1秒内消费完毕

		if(exc_client_->IsConnected() && connected_) {
			printf("connected_ %d\n",Message_Que.empty());
			while(!Message_Que.empty()) {
				tmp = Message_Que.front();
				printf("Event send MESSAGE_HEADER\n");
				ret = exc_client_->Send((char*)&tmp,MESSAGE_HEADER);
				if(ret < 0) {
					fprintf(stderr,"send exception message header failed!\n");
					connected_ = false;
					exc_client_->Close();
					break;
				}

				size = ntohl(tmp.size);

				if(size > 0) {
					ret = exc_client_->Send(tmp.buf,size);
					if(ret == size) {
						free(tmp.buf);
						tmp.buf = NULL;
						printf("free %d\n",size);
					} else {
						fprintf(stderr,"Event send %d,but %d actually",size,ret);
						connected_ = false;
						exc_client_->Close();
						break;
					}
				}

				{
					MutexScopedLocker locker(mutex_);  // 加大括号是为了尽快解锁
					Message_Que.pop();
				}
			}
	    } else {
		  	printf(" client not connected !!\n");
		}
	}
}

IMPLEMENT_SINGLETON(Exception);
} //namespace network