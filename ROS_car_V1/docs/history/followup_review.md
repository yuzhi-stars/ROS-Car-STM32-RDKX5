# 第二轮串口、DMA 与实时性核查

> 历史过程记录。当前编译烧录与控制行为以[文档索引](../README.md)和[运行时控制说明](../runtime_control.md)为准。

本轮继续核查现有实现，并修复能够从代码和故障测试确认的边界问题。没有烧录或连接开发板；编译、主机端模拟测试通过，不等于已证明实板的最坏中断时延或 CPU 占用。

## 本轮已修复

| 问题 | 原先可能出现的表现 | 当前处理 |
|---|---|---|
| RDK TX 完成回调丢失 | `s_tx_busy` 永久保持，后续不再发反馈 | 主循环发现发送超过 40 ms 未完成后中止 TX；失败每 10 ms 重试，未完成停止前保留源缓冲 |
| RX DMA 错误同时影响 TX | HAL 结束 UART TX 状态，但另一条 TX DMA 仍可能运行，过早复用发送缓冲 | ISR 只提出 TX 恢复请求；后台中止、确认 DMA 停止后才允许复用缓冲，错误标记与新发送启动同步 |
| DMA 中止超时后的句柄残留 | 第二次 UART Abort 返回成功，但 DMA 仍处于 TIMEOUT/BUSY，随后一直启动失败 | RX/TX 共用 `UART_DMA_ResetStopped()`，先检查真实 EN 位；流停止后重新初始化异常 DMA 句柄，不在 ISR 或关中断区轮询 |
| OLED 错误后请求关屏 | 软件误认为已经关屏，实际面板仍保持之前的 AF 开屏状态 | 1 秒退避后重新发送包含 AE 的配置，即使用户当前要求关屏也执行；正常启动仍保持熄屏 |
| ELRS 断链后直接到新帧 | 新帧先刷新时间，旧 armed 状态绕过断链清理 | 解析前先清理超时状态，重新累计 3 帧；`IsArmed()` 同时检查链路时效；残帧超过 10 ms 字节间隔后重新找帧头 |
| 里程计快照、重置与角度处理 | 主循环读取跨周期字段；设置位姿的编码器基线被中断打断；极大角度/Inf 使减法循环耗时很长或不退出 | 读取、重置和基线提交采用保存/恢复 PRIMASK 的短临界区；拒绝非有限输入，在临界区外用 `fmodf` 归一化外部角度 |
| HAL I2C 错误分支空指针 | TX DMA 已完成、I2C DMAEN 尚未清除时发生错误，访问未配置的 `hdmarx`，可能 HardFault | 为 TX/RX 句柄加空指针检查；无活动 DMA 的 TX-only 错误直接结束事务，关闭相关 I2C 中断并通知错误/中止完成 |

40 ms 是后台检查阈值，恢复发生在主循环重新获得执行机会时，不是硬件保证的截止时间。HAL Abort 的故障轮询也在后台，解析的 500 us 预算不包含这段恢复成本。

HAL 修复位于 `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c` 的 `I2C_ITError()`，由 `Project fix` 注释标记。替换 STM32 HAL 驱动库或重新生成覆盖驱动文件时需要保留/复核这段补丁。此处没有给 `hdmarx` 绑定假的 DMA 句柄。

## 已验证的故障场景

- 发送缺少完成回调、40 ms 前继续保持缓冲、Abort 失败及退避、DMA TIMEOUT 句柄重建、RX DMA 错误发生在 TX 期间。
- RX 句柄 TIMEOUT，硬件流仍启用时拒绝重新开始接收，流实际停止后恢复。
- ELRS 超过 300 ms 没有后台 Poll 后直接恢复来帧，必须重新累计 3 帧；长残帧后 11 ms 到来的合法帧可以重新同步。
- OLED 开屏时传输出错，用户随后要求关屏，恢复时确实向模拟面板发出 AE；继续保持关屏时没有重复刷新。
- 里程计使用实际采样周期，重置/设置位姿同步编码器基线，多字段读取只取一个快照，进入时已关中断则退出时保持原状态，拒绝 NaN/Inf，处理极大有限角度。
- 从工程 HAL 源文件直接提取 `I2C_ITError()` 编译测试，覆盖 TX-only 晚到错误、Abort 完成、TX/RX 活动 DMA 和空句柄。分别测试 `USE_HAL_I2C_REGISTER_CALLBACKS=0/1`。寄存器和周边 HAL 服务为模拟，未在真实 I2C 外设注入故障。
- 保留原有协议互通、CRC、零速解锁、200 ms 停车、DMA 环回/溢出/时间戳及 Mahony 数值测试。

运行命令：

```powershell
cmake --build --preset Debug --parallel 4
cmake --build --preset Release --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Release
```

## 仍然存在、需要后续处理或实板确认的项

| 优先级 | 证据与影响 | 后续方案 |
|---|---|---|
| 高 | `HardFault_Handler()` 等故障处理只有死循环；`Error_Handler()` 关中断后死循环。硬件 PWM 可能保持故障前的输出，SysTick 超时停车无法覆盖这些状态；未启用独立看门狗 | 加不依赖 HAL 等待的 PWM/驱动硬件关断，再按驱动电路确定复位与 IWDG 健康喂狗策略 |
| 高 | `IMU.c` 两种 SPI 事务仍使用 100 ms 超时；`INA219.c` 寄存器访问 20 ms；`Flash.c` FRAM 保存及重新探测使用 100 ms。外设故障时仍可拖延后台解析、姿态更新和发帧 | 分别测正常事务耗时，设置合理的短超时与离线退避，必要时改异步状态机；FRAM 启动读和运行保存分开预算。不能把这些 HAL 超时值简单相加当成实测耗时 |
| 中 | `IMU_Init()` 同时初始化 LSM6DSR 和 LIS3MDL，main 用其总返回值决定 `imu_available`；当前六轴融合不使用磁力计，但磁力计的 SPI 配置失败仍会阻止融合。初始化也未校验 WHO_AM_I，SPI HAL_OK 本身不能证明器件存在 | 拆分运动传感器与磁力计初始化状态，核验身份及关键配置，运动数据故障后按退避恢复；上板确认传感器实际型号与量程 |
| 中 | HAL `I2C_DMAAbort()` 在 DMA 中断路径等待 STOP 复位；OLED 的应用回调虽然很短，HAL 内部仍可能忙等。物理 SDA/SCL 卡低时，仅检查 BUSY 并跳过访问不能完成总线解锁 | 注入 NACK/拔插等故障，测量 I2C IRQ 最长时间；必要时把 STOP 等待改成后台恢复，设计有时限的总线复位/时钟解锁流程 |
| 中 | `Peripheral.c`、`Menu.c` 仍有直接 disable/enable IRQ 配对，未保存原 PRIMASK；当前主要调用在主循环，但嵌套调用有提前开中断的风险 | 分模块替换为保存/恢复原屏蔽状态，并检验最长临界区；不要把绘制、HAL 等待或算法包进关中断区 |
| 待实测 | TIM13 内仍做里程计三角函数；灯带仍以约 200 Hz 更新；无完整任务/ISR 耗时统计和栈水位证据 | DWT/GPIO 测 TIM13 周期、最大执行时间、IRQ 延迟、Mahony 单次耗时和主循环最长间隔；据结果调整灯带频率、里程计调度和栈大小 |

当前无限轮询主循环一直占用处理器执行机会，不能仅凭“CPU 活跃率高”归因于 Mahony。RDK 在主循环前部 Poll 一次，`RDK_UART_Process()` 内还 Poll 一次；每次解析最多 128 字节、约 500 us 的检查预算并不等于整个主循环只有 500 us。

建议下一轮先做故障关 PWM，以及 SPI/I2C 故障超时与退避，再补实板计时。当前版本不能据此宣布所有中断故障和实时性风险已消除。
