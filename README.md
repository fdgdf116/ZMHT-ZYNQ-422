# ZMHT-ZYNQ-422

当前恢复为 `devmem-queued` 网络到 DMA 测速版本：

- `/dev/zmuav_wrmem` 申请并持有 DMA 内存，`/dev/mem` 映射驱动返回的物理地址。
- 9016 按现有请求格式接收，剥离每个请求的 16 字节网络头；按头中大端 length 字段接收后续数据并直接写入映射地址。DMA 请求（index=0x0204）额外剥离正文开头的 4 字节通道号，仅有效数据进入 DMA；64 KiB 数据对应 length=65540，总报文长度=65556 字节，不造数，没有额外的应用层负载复制。支持 128 KiB 等不同请求长度以及 TCP 拆包、粘包。
- 使用前 32 MiB，分为 16 个 2 MiB 块。收满一块后将偏移和长度入队；DMA 完成后才允许接收线程复用该块。
- 队列满时暂停读取 TCP；不足 2 MiB 继续等待，断开连接时丢弃未满块。
- 每两秒打印 `[NET 9016 RX] devmem-queued payload` 去头后的接收速率（窗口内有数据时）和 `[DMA TX]` 完成速率。
- 按该历史版本，FIFO 状态上报线程不启动；9014 使用原来的上传路径。

恢复前工作区已备份到 Git stash `6bf476939d950b5194469a239db22000b47381af`。
此版本从历史备份重建，工作区代码为恢复结果，并非仅切换到一个提交号。

验证：`bash tests/run_network_dma_queue_test.sh`、`bash tests/run_network_queued_test.sh`、
`bash tests/run_devmem_test.sh`；ARM 编译：`cmake -S . -B build && cmake --build build -j4`。
实际硬件速率需上板测量。

当前测速优化：`include/common/common.h` 中 `ENABLE_1553B=0`，不初始化1553B、不申请其32 MiB内存、不启动9017服务，9013不再分发0x0300/0x0301/0x0302命令。需要恢复时改为1并重新编译。
9016读取长度取请求剩余量与DMA块剩余空间的较小值，取消原64 KiB上限；短读仍继续累计，2 MiB提交和缓冲所有权规则不变。

AXI FIFO软件仅启用通道0、1（`FIFO_NUM=2`）：初始化、收发轮询及缓冲申请均为2路，通道2～15不再访问。容量查询中未启用通道的AXI FIFO容量为0；VFIFO和DMA通道配置不变。FPGA位流未修改。

当前线程路径：
- 9016 `DataServiceLoop` → 2 MiB偏移队列 → `sgdma_network_queue_pthread`。
- PL接收 `zmuav_pl2ps_irq_recv_pthread` → 9014 `SyncSend::DaemodLoop`。
- 9013 `ServiceLoop` 保留控制命令。
- AXI FIFO收、发各一个线程，只轮询通道0、1。
- 1553B/9017关闭；FIFO容量上报线程默认编译排除。
- 旧 `sgdma_h_tx_pthread`、`sgdma_l_tx_pthread` 及专用等待辅助函数使用 `#if 0` 隔离，不参与编译。
- 本地TX造数用 `ENABLE_SYNTHETIC_DMA_TX` 控制，默认0；RX造数使用原 `DATA_PORT_BENCHMARK_MODE` 开关，默认关闭。测试脚本显式开启TX造数辅助代码。
旧线程此前也未启动，本次整理主要消除冗余实现和不可达线程创建分支，不代表硬件吞吐提升。


网络DMA TX使能后直接调用push_mm2s_dma提交，不再调用缓存同步ioctl；保留原内存顺序屏障与完成等待。
