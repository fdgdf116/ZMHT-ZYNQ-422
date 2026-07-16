#ifndef __NETWORK_MAIN_H_
#define __NETWORK_MAIN_H_
#include <iostream>
#include "thread.h"
#include "cmd_packet.h"
#include "tcp_socket.h"
#include "net_common.h"
namespace network {
using namespace std;
#define CMD_PORT 9013
#define ENVENT_PORT 9015
#define DATA_DOWN_PORT 9016
#define DATA_1553B_PORT 9017

class NetServer {
public:
	NetServer();
    int Init(int port);
private:
	typedef int (NetServer::*Callback)(Request *);
	typedef struct{
		unsigned short index;
		Callback func;
	}AnswerMapEntry;
	static AnswerMapEntry mapEntry_[];
	static AnswerMapEntry DatamapEntry_[];

    Thread thread_;
    Thread daemod_thread_;
	TcpSocket *server_;
	TcpSocket *client_;
	TcpSocket *client_data_;
    Mutex mutex_;
	int port_;
	char pc_ip[INET_ADDRSTRLEN] = {0};
	void DataServiceLoop(Thread *thread);
    void ServiceLoop(Thread *thread);
	void DaemodLoop(Thread *thread);
	void DataProcMessage(char * msg);
	void ProcMessage(char * msg);
	int SendResponse(Response *response);
	int SendResponse(ClassResponse *response);
	void ReportUnknowRequest(Request* request);

	int WriteFpgaRegister(Request *request);
	int ReadFpgaRegister(Request *request);
	int DDR_Phy_Get(Request *request);
	int Send1553BData(Request *request);
	int Read1553BData(Request *request);
	int NetDmaMM2S(Request *request);
	int FIFOSendData(Request *request);
	int NetDmaMM2SControl(Request *request);
};


} //namespace network

#endif // __NETWORK_MAIN_H_
