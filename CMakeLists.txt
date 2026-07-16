cmake_minimum_required(VERSION 3.5.1)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER /home/wang/ZMHT-Tool/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER /home/wang/ZMHT-Tool/gcc-arm-linux-gnueabi/bin/arm-linux-gnueabihf-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 项目信息
project(pxie_dma_sample)

# 设置可执行文件目标
set(TARGETS "pxie_dma_sample")

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_FLAGS "-O2 -g -std=c++11 -Wno-error")

add_definitions(-D_GNU_SOURCE -DLINUX)

include_directories(
	${CMAKE_CURRENT_LIST_DIR}/include/
	${CMAKE_CURRENT_LIST_DIR}/include/common/

	${CMAKE_CURRENT_LIST_DIR}/src/hardware/
	${CMAKE_CURRENT_LIST_DIR}/src/network/
	${CMAKE_CURRENT_LIST_DIR}/src/system/
)

link_directories(	
)
# 查找src目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
aux_source_directory(./src DIR_SRCS)
aux_source_directory(./src/hardware DIR_SRCS_HARDWARE)
aux_source_directory(./src/network DIR_SRCS_NETWORK)
aux_source_directory(./src/system DIR_SRCS_SYSTEN)

# 指定生成目标
add_executable(${TARGETS} ${DIR_SRCS} ${DIR_SRCS_HARDWARE} ${DIR_SRCS_NETWORK} ${DIR_SRCS_SYSTEN})

target_link_libraries(${TARGETS}      
    pthread
)
