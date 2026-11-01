# 实时性优化与验证记录

> 历史过程记录。当前编译烧录与控制行为以[文档索引](../README.md)和[运行时控制说明](../runtime_control.md)为准。

后续边界检查、已修复的问题及当前仍存在的项见 [第二轮核查记录](followup_review.md)。该记录包含 DMA 中止超时恢复、发送回调丢失恢复、ELRS 断链状态、OLED 错误后关屏、里程计同步及 HAL I2C 空指针修复。

根据实板上位机日志进一步修改的姿态初始化、校准后台重试、IMU 诊断页及轮速滤波小数状态，见 [日志问题修复与验收](imu_log_fixes.md)。

当前已增加 [CH5/CH6 运行时切换](../runtime_control.md)，统一四轮目标与两个来源的超时，主循环移除了方形/角度/开环测试和语音驾驶分支。下文保留最初 DMA 优化的实现记录，控制权以运行时切换说明为准。

本次落实用户确认的修改：USART1 暂停使用；UART5 错误恢复；OLED 默认熄屏、按键开启、变化区域异步分片刷新；RDK/ELRS 循环 DMA 接收、RDK DMA 发送；两处时间处理修正；Mahony 路径精简。没有改变三种线协议报文格式、20 ms 控制周期或轮速低通系数/PID 参数。

## OLED 与 I2C

- OLED 初始化配置不发送 AF 开屏命令，校准期间也保持熄屏。只有菜单旋钮按键请求打开后，才上传隐藏显示 RAM，最后开启面板，避免先显示未清理的随机内容。
- 绘字、清行、清屏、反显均只操作 8×128 内存画布，清屏不再执行 1024 次 I2C 事务。
- 状态页以 100 ms（10 Hz）更新内容；菜单交互可即时更新内存画布。
- 与已发送快照比较，实际只上传变化区，每块最多 32 个像素字节。位置命令和数据分别使用 I2C1 TX DMA，没有轮询等待整屏刷新。
- DMA 使用静态传输快照。传输期间画布变化不会修改 DMA 正在读取的内存；发送完成后只提交该次快照，后续再补发新变化。
- 无内容变化时走快速返回，不在每次主循环重复扫描整屏。
- 每次完成让出一次主循环，INA219/FRAM/语音访问通过 `OLED_I2CAvailable()` 在同一 I2C1 总线上串行安排，避免把 OLED 占用误判为外设掉线。
- OLED 事务达到 5 ms 尝试异步中止；10 ms 仍未结束则在后台停止 DMA 并重初始化 I2C。错误后 1 秒退避。回调不执行总线恢复或绘制。
- 启动的 OLED 硬件初始化仍有一次约 100 ms 等待和一次短配置事务，此时正常控制尚未启用。运行期已消除全屏清除阻塞。

## 串口 DMA 与优先级

| 功能 | DMA 资源 | DMA 模式 | NVIC 抢占优先级 |
|---|---|---|---:|
| UART5 / ELRS RX | DMA1 Stream0 Channel4 | Circular | 2 |
| USART3 / RDK RX | DMA1 Stream1 Channel4 | Circular | 3 |
| USART3 / RDK TX | DMA1 Stream3 Channel4 | Normal | 3 |
| UART4 / 原 VOFA TX | DMA1 Stream4 Channel4 | Normal | 3 |
| I2C1 / OLED TX | DMA1 Stream6 Channel1 | Normal | 6 |
| TIM1 / 灯带 | DMA2 Stream2 Channel6 | Normal | 7 |

电机 TIM13 仍为优先级 1。UART5/USART3 的 UART IRQ 与各自 RX DMA IRQ 保持同一优先级；I2C1 EV/ER 为 6。USART1 当前既不初始化也不启动接收，其生成函数保留，但 NVIC 不启用。

`usart.c`、`i2c.c`、`dma.c`、IRQ 处理与 `.ioc` 的 DMA/NVIC 项已经同步。CubeMX 再生成后，仍需保留主循环中的用户模块调用、USART1 暂停初始化以及新增的 CMake 用户源文件；原 `.ioc` 仍保留 USART1 的引脚配置以便将来恢复。

## RX 处理约定

- RDK 与 ELRS 共用 `uart_dma_rx.c/.h`，每口 1024 字节环形接收缓冲和 64 项元数据事件环。保留 HT 和 TC 中断。
- IDLE/HT/TC 回调只快照当前 NDTR 位置与 HAL 时间。使用实时 NDTR，而不盲用可能延后到达的旧 HT 回调 Size，避免位置倒退被误当成整圈数据。
- 1 ms SysTick 也记录新增字节的位置，给连续无 IDLE 的流补充时间边界；零增量不入队。时间是 DMA/节拍观察时间，不是硬件逐字节时间戳，精度受中断响应和 HAL 毫秒粒度影响。
- 生产者以短临界区同步；ISR 不执行 CRC、浮点转换、遥控解包或 HAL Abort。
- `RDK_UART_PollRx()`、`ELRS_Poll()` 在后台每次最多解析 128 字节，且在每字节处理后检查 500 us 的 DWT 预算。一次完整 CRC/帧处理不会被中途截断，实际单次上限包含最后一帧的工作与抢占时间。
- 消费者以半缓冲容量作为保护水位，给尚未报告的 DMA 半块保留空间。积压/元数据事件溢出会丢弃残帧；RDK 重新要求零速解锁，ELRS 清除解锁并重新累计稳定开关帧。
- 错误 IRQ 只标记恢复请求；后台中止接收、清错、启动 ReceiveToIdle DMA，失败后每 10 ms 重试，不会永久停止收包。
- 延后解析使用接收事件时间更新有效帧时间。RDK 超过 200 ms、ELRS 超过 300 ms 的旧帧被拒绝，不能用后台处理时间刷新它们。
- `ELRS_Poll()` 不写电机目标，在 RDK 模式下仍服务遥控接收/报警状态；只有本地控制分支调用驱动接口。

保持环形 DMA 可靠仍要求 DMA 中断能够在缓冲覆盖前被服务；全局关中断或最高优先级 ISR 若长到跨越多个完整 DMA 周期，单靠 NDTR 无法恢复丢失的圈数。当前修改缩短 ISR 并检测后台积压，最终余量需上板量测。

RDK TX 每 20 ms 用静态 68 字节缓冲发送两帧，DMA 未完成时不改写。还需要 USART3 TC IRQ 来完成 HAL 发送状态切换，不能只保留 DMA IRQ。

## 两处时间修正

`runtime_clock.c/.h` 在系统时钟建立后启用 Cortex-M4 DWT CYCCNT，初始化放在 main 的 USER CODE 区。

1. `Motor_UpdateEncoderSpeed()` 每个控制节拍按 DWT 实际周期差采样，不再因 HAL 毫秒差为 19 而跳过。计数/秒使用实际时间换算；里程计速度也使用对应周期。控制周期、PID 和低通系数保持原值。
2. `RDK_UART_Process()` 在取得编码器快照之后重新读取当前时间计算年龄，避免被新采样抢占后无符号差值回绕、误标无效。ELRS 也在判断链路时使用当前时间，避免旧的主循环 now 与新的接收时间产生同类问题。

CYCCNT 在 168 MHz 下约 25.6 秒回绕，正常 20 ms 周期使用无符号减法可跨越回绕。它不提供跨长时间调试暂停的墙上时间；实际周期验证应在正常运行状态完成。

## Mahony 优化

- 正常小转角传播使用四阶 sinc/cos 近似；较大转角保留三角函数路径，更新后仍归一化。
- 静止角速度门限使用平方模比较，减少一次平方根；加速度归一化共用一次倒数。
- 外部四元数先做分量界限与有限性检查，再以 float/sqrtf 处理，不再引入双精度辅助计算。
- 内部 Mahony 已验证/归一化的结果直接提交，打包不再重复归一化；外部覆盖 API 和公开打包 API 仍检查异常输入。
- 没有启用 fast-math，保留 NaN/Inf 拒绝语义。

## 验证

```powershell
cmake --build --preset Debug --parallel 4
cmake --build --preset Release --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Release
```

软件检查包含：实际 Mahony C 实现的两条传播路径、三轴/组合旋转和异常输入；与原始上位机协议互通；DMA 回绕、溢出、过期帧、预算退出、错误恢复与时间快照抢占；ELRS CRC/解锁/恢复；OLED 启动熄屏、变化/反显、传输快照、总线让出与超时恢复。

尚未烧录、验证真实 OLED 面板/旋钮及 UART DMA 波形，也没有实测 CPU 占用、IRQ 最大耗时或最坏控制抖动。现有 SPI/INA219/FRAM 的阻塞故障超时、故障处理/独立看门狗及其他历史核查建议没有在本次全部重构，应结合 [历史核查报告](interrupt_performance_review.md) 继续上板评估。
