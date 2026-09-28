# ZMHT-ZYNQ-422

当前恢复为 `devmem-queued` 网络到 DMA 测速版本：

- `/dev/zmuav_wrmem` 申请并持有 DMA 内存，`/dev/mem` 映射驱动返回的物理地址。
- 9016 按现有请求格式接收，剥离每个请求的 16 字节网络头；按头中大端 length 字段接收后续数据并直接写入映射地址。头后的全部内容（若有 4 字节通道号也保留）进入 DMA，不造数，没有额外的应用层负载复制。支持 128 KiB 等不同请求长度以及 TCP 拆包、粘包。
- 使用前 32 MiB，分为 16 个 2 MiB 块。收满一块后将偏移和长度入队；DMA 完成后才允许接收线程复用该块。
- 队列满时暂停读取 TCP；不足 2 MiB 继续等待，断开连接时丢弃未满块。
- 每两秒打印 `[NET 9016 RX] devmem-queued payload` 去头后的接收速率（窗口内有数据时）和 `[DMA TX]` 完成速率。
- 按该历史版本，FIFO 状态上报线程不启动；9014 使用原来的上传路径。

恢复前工作区已备份到 Git stash `6bf476939d950b5194469a239db22000b47381af`。
此版本从历史备份重建，工作区代码为恢复结果，并非仅切换到一个提交号。

验证：`bash tests/run_network_dma_queue_test.sh`、`bash tests/run_network_queued_test.sh`、
`bash tests/run_devmem_test.sh`；ARM 编译：`cmake -S . -B build && cmake --build build -j4`。
实际硬件速率需上板测量。
