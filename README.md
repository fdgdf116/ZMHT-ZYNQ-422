# ZMHT-ZYNQ-422

当前版本同时支持 DMA 和16 路 AXI FIFO 收发，数据流见 [DATA_FLOW.md](DATA_FLOW.md)。

- **9016 下发**：`index=0x0204` 使用现有 DMA 路径，正文直接写入 DMA 映射内存；16 个 2 MiB 块，收满后入队，MM2S 完成后释放。单次 `recv` 上限为 256 KiB。
- **9016 FIFO 下发**：`index=0x0006`，正文为 4 字节大端通道号和数据；支持通道 0～15。完整接收当前请求后，按最多 508 字节复制到原 FIFO TX 队列，再由原硬件发送线程发送。不会混入 DMA 块。
- **9016 容量上报**：独立线程通过同一 TCP 连接发送，采集发送后等待 20 ms。沿用 `index=0x0001`、400 字节正文、16 项记录。DMA 容量单位为 1 KiB，FIFO 容量为剩余槽位数乘 512 字节，16 路均上报。上位机必须同时读取反向容量报文。
- **9014 DMA 上传**：保持 20 字节协议头与 DMA 指针的 `sendmsg` 分散发送，最大正文为 2 MiB，不增加正文 memcpy，不拆分 DMA 应用报文。
- **9014 FIFO 上传**：沿用 `index=0x06`、4 字节通道号加 FIFO 数据的协议。16 个 FIFO 通道轮询，每轮最多 64 个 FIFO 报文，再处理一个完整 DMA 帧。
- **FIFO 硬件接收**：读完本轮 RDFO 指示的全部数据，再按最多 508 字节拆入 RX 队列；队列满则等待，9014 线程取出上传。
- DMA 队列满或暂停时使用 TCP 背压；未满 2 MiB 的尾块继续等待，断连时丢弃。FIFO 队列满也暂停接收；断连丢弃未入队的暂存数据，已入队的数据保留。
- DMA 内存由 `/dev/zmuav_wrmem` 申请并映射；当前日志中的 `devmem-queued` 是历史名称。
- `FIFO_NUM=16`；1553B/9017、DMA TX/RX 造数默认关闭。FPGA 位流未修改。

## 验证

```sh
bash tests/run_network_mixed_test.sh
bash tests/run_network_dma_queue_test.sh
bash tests/run_network_queued_test.sh
bash tests/run_network_rx_vector_test.sh
cmake -S . -B build
cmake --build build -j4
```

混合测试使用真实 TCP 和实际软件队列，模拟硬件 VFIFO 寄存器与 DMA/FIFO 消费。实际板上收发速率、硬件背压和缓存一致性仍需上板验证。
