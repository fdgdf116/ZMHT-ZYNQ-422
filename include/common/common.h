
/*
 * Important:
 *
 * This file using "UTF-8 65001 with BOM" coding.
 * Choose editor coding if you could not see words below.
 *
 * “防控烦人的乱码，编辑器使用上述编码才看得到这句话！”
 * “这句就是故意给编辑器看的，它一般会识别头部，勿删。”
 */

#ifndef COMMON_H
#define COMMON_H

// Disable 1553B allocation, service and command handlers during DMA benchmarking.
#ifndef ENABLE_1553B
#define ENABLE_1553B 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define		TEST_AUTO_TIMING        1  //测试时序文件输出时序还是老的时序输出测试 1: new   0 old

//地面设备控制台指令
typedef enum {
	//文件操作
	FILE_DELETE = 0x01,//修改全局变量
	UPLOAD_FILE_CHECK,//上载文件校验
	FILE_CHANGE,//文件修改
	FILE_DOWNLOAD,//文件下行
	FILE_UPLOAD_SELECT,//文件加载选择
	FILE_EXCHANGE,//文件替换
	
	OC_CONTROL,//风扇,离子泵，相机，其他电路

	TIMING_SERIES_SETTING,//时序参数设置
	SCI_FPLL_SETTING,//锁频参数设置
	PERIPHERALS_SETTING,//外设参数设置
	SCM_SETTING_SEND,//单片机参数发送
	IONIC_PUMP_SETTING_SEND,//离子泵参数发送
	CAMERA_SETTING_SEND,//相机参数发送

	MODE_SWITCH = 0x11,//各个模式之间的切换（空闲，科学实验（1等待，2实验），在轨维护）

	//科学实验模式指令
	OUTPUT_SCI_TIMING_SERIES,//输出实验时序
	SCI_STOP,//停止实验
	SCI_FPLL_CTL,//锁频（脱锁）指令
	VOUT_INIT,//电压初始化
	RS485_OUTO_INIT,//485一键初始化

	//维护模式
	MAINTAIN_SOFTWARE_UPLOAD_TO_FLASH,//程序手动加载指令
	
	//通用
	SYSTEM_REBOOT //重启（主控FPGA，主控，单片机）

}GROUND_CONTROL_CONSOLE_DEVICE_CMD;

//地面设备控制台指令
typedef enum {
	SYSTEM_DEVICE_CMD = 0xC1,//设备控制台指令
	SYSTEM_TIME_SERIES_FILE_UPLOAD = 0xD1,//时序文件
	SYSTEM_UPLOAD_UPDATE_SOFTWARE = 0xD2 //升级软件包上传

}GROUND_CONTROL_CONSOLE_CMD;

#pragma pack(1)
typedef struct{
    unsigned char chn_id;
    unsigned int can_space;
    unsigned int pulse_space;
    unsigned int simulation_space;
    unsigned int dmaddr_space;
    unsigned int vfifo_space;
    unsigned int axififo_space;
}Cache_report_inf_t;
#pragma pack()

typedef struct {
    volatile unsigned int frame_size;
    volatile unsigned int frame_offset;
}image_frame_info_t;

#define FREE_SPACE_NUM_MAX (16)
#define GENERAL_REG 0xA1000000
#define GENERAL_REG_SZIE 0x1000
#define MEM_DEV_NAME "/dev/mem"
#define ZMUAV_WRMEM_DEVICE_NAME "/dev/zmuav_wrmem"
#ifdef __cplusplus
}
#endif


#endif // COMMON_H
