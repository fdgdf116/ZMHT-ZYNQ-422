#include "network_main.h"
#include "fifo_engine.h"
#include "system.h"
#include "1553B_engine.h"
#include "common.h"
#include "sync_send.h"
using namespace network;

void prog_exit(int sig_no);

int main(int argc, char* argv[]){
    int ret;

#if 1
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, prog_exit);
    signal(SIGKILL, prog_exit);
    signal(SIGTERM, prog_exit);
#else
    signal(SIGINT, prog_exit);
#endif

#if DATA_PORT_BENCHMARK_MODE
	printf("9014 benchmark mode: synthetic data uses the DMA ring buffer\n");
#endif
#if ENABLE_1553B
	ret = init_1553B();
	if(ret != 0)
	{
		printf("recv_dma_init error \n");
		return ret;
	}

#endif
	ret = recv_dma_init();
	if(ret != 0)
	{
		printf("recv_dma_init error \n");
		return ret;
	}

	ret = system_init();
	if(ret != 0)
	{
		printf("system init error \n");
		return ret;
	}

	ret = axififo_data_init();
	if(ret != 0)
	{
		printf("axififo_data_init error \n");
		return ret;
	}

	SyncSend::GetInstance()->Init();
	NetServer cmd_server;
	cmd_server.Init(CMD_PORT);
#if ENABLE_1553B
	NetServer data_1553b_server;
	data_1553b_server.Init(DATA_1553B_PORT);
#endif
	NetServer data_server;
	data_server.Init(DATA_DOWN_PORT);

	while(1) {
		sleep(1);
	}
	return 0;
}
void prog_exit(int sig_no){
	system_exit();
	recv_dma_exit();
#if ENABLE_1553B
	close_1553B();
#endif
	exit(0);
}
