# ZMHT-ZYNQ-422

当前版本同时支持 DMA 和16 路 AXI FIFO 收发，数据流见 [DATA_FLOW.md](DATA_FLOW.md)。

- **9016 下发**：`index=0x0204` 使用现有 DMA 路径，正文直接写入 DMA 映射内存；16 个 2 MiB 块，收满后入队，MM2S 完成后释放。单次 `recv` 上限为 256 KiB。
- **9016 FIFO 下发**：`index=0x0006`，正文为 4 字节大端通道号和数据；支持通道 0～15。完整接收当前请求后，按最多 508 字节复制到原 FIFO TX 队列，再由原硬件发送线程发送。不会混入 DMA 块。
- **9016 容量上报**：独立线程通过同一 TCP 连接发送，采集发送后等待 20 ms。沿用 `index=0x0001`、400 字节正文、16 项记录。DMA 容量单位为 1 KiB，FIFO 容量为剩余槽位数乘 512 字节，16 路均上报。上位机必须同时读取反向容量报文。
- **9014 DMA 上传**：保持 20 字节协议头与 DMA 指针的 `sendmsg` 分散发送，最大正文为 2 MiB，不增加正文 memcpy，不拆分 DMA 应用报文。
- **9014 FIFO 上传**：沿用 `index=0x06`、4 字节通道号加 FIFO 数据的协议。16 个 FIFO 通道轮询，每轮最多 64 个 FIFO 报文，再处理一个完整 DMA 帧。
- **FIFO 硬件接收**：根据 RDFO 占用量、临时缓冲区大小和软件队列空闲槽位限制本轮读取量，再按最多 508 字节拆入 RX 队列；无空闲槽位时跳过该通道，9014 线程取出上传。
- DMA 队列满或暂停时使用 TCP 背压；未满 2 MiB 的尾块继续等待，断连时丢弃。FIFO 队列满也暂停接收；断连丢弃未入队的暂存数据，已入队的数据保留。
- DMA 内存由 `/dev/zmuav_wrmem` 申请并映射；原测速日志中的 `devmem-queued` 是历史名称，该组日志已在下述修改中删除。
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

## 工程修改记录

本节记录工作区代码相对 Git 提交的变化、影响及记录时间。后续修改可按日期追加记录。

### 2026-10-08 11:13:16（北京时间，UTC+08:00）

- 记录时间：2026-10-08 11:13:16，Asia/Shanghai。
- 对比基准：Git 提交 `90c00a0`（记录时的 HEAD）。
- 对比对象：记录时尚未提交的工作区修改。
- 时间说明：上述时间是本次分析记录的生成时间，不代表这些代码实际修改或编译的时间；实际发生时间尚未确认。
- 本次文档操作：修改记录已合并至 README.md，没有修改源码，也没有重新编译。

#### 涉及文件

| 文件 | 变化 |
| --- | --- |
| `src/hardware/fifo_engine.cpp` | 新增 211 行、删除 55 行 |
| `src/network/network_main.cpp` | 删除 25 行 |
| `build/CMakeFiles/pxie_dma_sample.dir/src/hardware/fifo_engine.cpp.o` | 二进制内容变化，46572 → 49296 字节 |
| `build/CMakeFiles/pxie_dma_sample.dir/src/network/network_main.cpp.o` | 二进制内容变化，154964 → 151628 字节 |
| `build/pxie_dma_sample` | 二进制内容变化，410408 → 410436 字节 |

以上统计不包含文档变更。二进制文件仅确认内容和大小变化，未验证是否与当前源码对应。

#### 修改前后对比

| 位置 | 修改前 | 修改后 | 影响 |
| --- | --- | --- | --- |
| `network_main.cpp`：`DataServiceLoop()` | 统计接收字节数、recv/poll 次数，并约每 2 秒输出吞吐率 | 删除相关统计、计时、日志及日志刷新 | 不再输出这组 `[NET 9016 RX]` 测速日志，减少统计和日志开销；协议解析、入队及背压逻辑未因此改变 |
| `fifo_engine.cpp`：`rx_fifo_data_get()` | 队列为空时休眠 10 微秒 | 空队列直接返回 | 减少轮询空通道的等待；实际性能和 CPU 占用需要测量 |
| `fifo_memcpy_data()` | 已支持最多 508 字节分块、512 字节槽位、回绕及满队列等待 | 保留上述行为，展开代码，增加异常输入、未初始化及队列满日志、注释和禁用代码 | 主要是日志和表达方式变化，分块能力不是本次新增 |
| `axififo_recv_data_pthread()` | 根据临时缓冲区大小和空闲槽位容量限制读取量 | 保留原逻辑，增加注释并调整代码写法 | 主要行为基本一致 |
| `fifo_tx_memcpy_data()` | 调用 `fifo_tx_try_memcpy_data()`，队列满时重试 | 独立实现等待、复制、入队，增加满队列日志 | 正常输入下行为大体相同，但不再继承辅助函数的完整参数和初始化检查 |
| `fifo_tx_try_memcpy_data()` | 检查映射空间至少容纳一个槽位 | 删除 `map_size < AXI_FIFO_BUF_UNIT_SIZE` 检查 | 异常初始化时缓冲区容量保护减少 |
| `axififo_recv()` | 未限制单次读取为 1024 字节；非法参数返回 -1 | 单次读取最多 1024 字节；非法参数返回 0 | 增加读取上限，但错误与无数据返回值不再区分；当前源码未发现调用点 |
| `axififo_query_capacity()` | 检查参数和全局初始化状态，预先清零 FIFO 容量字段 | 删除上述检查及清零 | 接口防护减少；当前网络调用方已有数组零初始化 |

分块示例：收到 514 字节时，修改前后均按 508 + 6 字节入队，不能将该能力视为本次新增修复。

#### 需要关注的变化

1. `axififo_send()`、`axififo_data_send()`、`axififo_get_send_size()`、`axififo_pure_data_send()`、`rx_fifo_data_get()`、`fifo_memcpy_rb_avail()` 等函数移除了入口处的部分参数或初始化检查。异常输入或未初始化状态下，可能访问无效指针或越界下标。
2. 展开后的 `fifo_tx_memcpy_data()` 缺少原有的全局初始化、数据指针、非正长度、队列和缓冲区有效性等检查。当前网络调用方已限制通道和分块长度，因此这些风险不等于正常网络请求必然出错。
3. 吞吐率日志删除后，无法继续通过原有日志观察接收速率和 recv/poll 调用次数。
4. 大量新增行属于注释、日志、展开实现和 `#if 0` 禁用代码，新增行数不代表新增功能规模。

#### 分析结论与验证范围

本次主要变化是删除网络测速统计、减少 FIFO 空队列等待，以及调整 FIFO 实现写法；同时多处防御性检查被删除，接口健壮性有所下降。

本记录基于 Git 差异和相关调用代码的静态检查，未执行编译、自动化测试或上板测试。实际吞吐率、CPU 占用及硬件运行行为尚未验证。
