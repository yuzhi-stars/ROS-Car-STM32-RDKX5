# RDK X5 串口通信使用说明

本模块按桌面的 `x5car_protocol.hpp`、`x5car_protocol.cpp` 实现，与原上位机解析器进行过原生互通测试。尚未进行实板串口、电机和传感器联调。

## 文件与接入位置

- `Core/Inc/rdk_uart.h`：配置、数据有效位、接口和诊断统计。
- `Core/Src/rdk_uart.c`：字节编码、CRC、流式解析、接收中断重启、反馈发送、命令解锁与超时停车。
- `Core/Src/imu_attitude.c`、`Core/Inc/imu_attitude.h`：Mahony 六轴姿态融合，输出传感器系到 z 向上世界系的单位四元数。
- `Core/Src/imu_calibration.c`、`Core/Inc/imu_calibration.h`：静止零偏窗口、独立新采样资格检查和后台重试。
- `Core/Inc/motor_speed_filter.h`：保持 0.35 低通系数，使用浮点状态消除停车时 ±2 cps 残留。
- `Core/Src/uart_dma_rx.c`、`Core/Inc/uart_dma_rx.h`：RDK/ELRS 共用的循环 DMA、位置/接收时间事件与溢出恢复。
- `Core/Src/runtime_clock.c`、`Core/Inc/runtime_clock.h`：DWT 高分辨率计时、解析预算和编码器测速时间基准。
- `Core/Src/drive_control.c`、`Core/Inc/drive_control.h`：CH5/CH6 运行时控制权、切换停车、手动目标快照与数据超时。
- `main.c`：初始化，提交新 IMU 采样，调用 `RDK_UART_Process()`，转发三个 HAL UART 回调。
- `stm32f4xx_it.c`：SysTick 执行 200 ms 超时检查；电机 20 ms 中断在运行 PID 前提交四轮目标。
- `Motor.c/.h`：提供编码器采样时刻，以及清除目标、PID 积分状态和 PWM 的停车接口。
- `IMU.c/.h`：新增 `IMU_ReadMotionRaw()`，根据传感器新数据标志采样，避免旧读数反复标为有效。
- `Menu.c`：RDK 控制时禁用 TARGET 页调速，PID 参数编辑和数据显示仍可使用。

已加入 CMake 源文件列表和 Keil 工程的新文件条目。验证使用 CMake/GCC，未验证 Keil 构建。

## 串口与帧格式

使用 USART3：PB10=TX，PB11=RX，115200、8N1、无流控。TX 接上位机 RX，RX 接上位机 TX，并共地；使用电平兼容的 UART 接口。

```text
AA 55 | TYPE | 固定长度数据 | CRC_LO CRC_HI
```

| TYPE | 方向 | 内容 | 总长度 |
|---|---|---|---:|
| 03 | RDK → STM32 | FL、FR、RL、RR 四轮目标角速度 | 21 字节 |
| 90 | STM32 → RDK | 四轮实际角速度 + 有效位 | 22 字节 |
| 91 | STM32 → RDK | 四元数 + 三轴角速度 + 三轴加速度 + 有效位 | 46 字节 |

浮点数为 IEEE 754 float32 小端；不发送结构体、double、文本、长度字段、帧尾或 ACK。CRC 为 CRC-16/CCITT-FALSE，初值 FFFF，多项式 1021，无反射、无最终异或；只覆盖 TYPE 和数据区，低字节先发。

每 20 ms 用一次 `HAL_UART_Transmit_DMA()` 连续发送 90、91 两帧，共 68 字节。缓冲区为模块静态内存，发送完成前不会重写。USART3 RX 用 DMA1 Stream1/Channel4 循环接收，TX 用 Stream3/Channel4 普通 DMA；UART5 RX 用 Stream0/Channel4。保留 UART 和 DMA IRQ，接收使用 `HAL_UARTEx_RxEventCallback()` 转发 IDLE/HT/TC 事件，发送完成和错误仍转发相应 HAL 回调。

ISR 只记录 DMA 位置/时间，不执行 CRC 和浮点解析。1 ms SysTick 补充连续无 IDLE 流的位置记录；主循环每次解析最多 128 字节，且逐字节检查 500 us 时间预算。错误清除/Abort/重新接收均在后台执行，积压/事件环溢出会丢弃残帧、清空目标并等待新有效命令。时间以接收事件为准，过期命令不会因延迟解析而刷新超时。主循环较慢时反馈频率仍会低于 50 Hz，命令超时由 SysTick 独立检查。不能让其他模块同时使用 USART3 发送。

## 控制权与启动

`rdk_uart.h` 中 `RDK_UART_CONTROL_ENABLE` 默认 **1**，表示编译上位机控制能力；日常无需修改宏或反复烧录，通过 CH6 选择上位机/停车/手动。操作流程见 [运行时切换与遥控建图](runtime_control.md)。

- `drive_control` 统一提交目标；方形/角度/开环测试及语音驾驶分支已从主循环移除，菜单 TARGET 页只读，PID 参数仍可编辑。
- CH6 低位上位机、中位停车、高位遥控。用过遥控后 CH5 为两种模式的总解锁。选上位机且 CH5 高位即可授权，无需额外低后高；切入手动仍需重解锁与回中。不曾收到遥控数据的上电场景保留独立上位机方式。
- 上电保持零目标和零 PWM。
- 上位机接管时丢弃旧接收积压/残帧，之后新收到的合法速度帧可直接驱动，包括非零速度，不要求上位机零速握手；非上位机模式的速度帧只统计为忽略。RDK 持续重发的旧目标在串口上仍属于新收到的帧，协议无法区分其导航意图；拨到上位机档即授权当前 RDK 输出。
- 四轮在同一个控制周期提交；RDK 直接提供四轮 rad/s，手动模式通过原麦轮换算获得四轮目标。
- 上位机模式的合法命令刷新 200 ms 超时。CRC 错误、NaN/Inf、超速、反馈回显均不刷新。到时清零目标、PID 累积输出并关闭 PWM，控制权有效时下一新有效帧可恢复，不要求零速。手动模式不受 RDK 命令超时影响。
- 收到过遥控数据后，遥控数据失效/年龄达到 300 ms，在两种模式下均停止驱动，不自动交给另一来源；恢复后需重新解锁。该策略为断开驱动输出，车体仍可能惯性滑行。

USART3 在所有模式继续上传轮速与 IMU，不发送额外控制权 ACK/文本；上位机代码无需修改。运行时控制器用 `RDK_UART_GrantFreshControl()` 授权，用 `RDK_UART_SetControlEnabled(0)` 撤销；其他模块不应争抢调用。旧 `RDK_UART_SetControlEnabled(1)` 仍保留零速握手策略，用于严格授权接口兼容性；当前运行时控制不采用该策略。

联调可先每 20 ms 发送零速帧：

```text
AA 55 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CA F6
```

然后发送四轮 `[1,-2,3,-4] rad/s` 命令：

```text
AA 55 03 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 FD 02
```

持续运动需持续发送合法命令；停止发送会触发超时。确认电机与编码器正负方向后再使用运动报文。

## 轮速参数

协议顺序为左前、右前、左后、右后，与当前 `Motor_ID_t` 一致。正方向为车轮向车体前方滚动。

当前沿用 `Odom.c` 的每车轮一圈 **1333 个有效计数**，以及原麦轮控制的 **10000 cps** 上限，得到约 **47.14 rad/s** 的串口命令上限。这是现有工程配置，不代表已测定的电机额定能力。实车参数应在 `rdk_uart.h` 校准，并与上位机限速、里程计保持一致。

```text
实际 rad/s = 实测 cps × 2π / 1333
目标 cps   = round(目标 rad/s × 1333 / 2π)
```

只有新的、未超过 200 ms 的编码器采样可以标为有效；停车时新采样为 0 也有效。四轮正常为 `0F`，否则按位标记。串口不会把目标值作为反馈。

## IMU 数据

采用传感器原始右手坐标轴，角速度为 rad/s，加速度为 m/s²，保留重力。当前 LSM6DSR 配置为 ±250 dps、±2 g：

```text
gyro_rad_s = (raw - bias_raw) × 0.00875 × π / 180
accel_m_s2 = raw × 0.000061 × 9.80665
```

转换比例和 STATUS_REG 的 GDA/XLDA 标志依据 [ST LSM6DSR 数据手册](https://www.st.com/resource/en/datasheet/lsm6dsr.pdf)，对应灵敏度表与 STATUS_REG 章节。若更改传感器量程，需要同步修改模块的换算比例。

主循环统一读取新陀螺仪/加速度数据，再交给 `RDK_UART_UpdateImuRaw()`，后者自动运行 Mahony 六轴融合并填入四元数。保留约 3 秒启动校准尝试，要求至少 200 个新陀螺仪样本和至少 2 秒静止窗口。加速度可独立到达，资格检查仅使用 30 ms 内的新加速度，绝不重复累计陀螺仪旧值。启动未成功时，后台在四轮目标均为零、测得轮速均不超过 ±10 cps 时继续尝试；运动/异常窗口清除后退避 250 ms。成功后不在行驶中重新学习零偏。

校准成功后还需 32 个新近静止样本完成姿态对准。初始化期间短暂缺少加速度只暂停累计，不重复使用旧观测；两次加速度间隔超过 30 ms、检测到运动或数据异常仍重置对准。建议上电保持静止约 5 秒，实际以有效位或 OLED 诊断状态为准。启动时相对航向设为 0，横滚和俯仰由重力方向初始化。

正常解算后，IMU 三组数据全部有效时 flags 为 **`07`**。初始化、校准失败或姿态重对准期间，角速度与加速度正常而姿态不可用时为 `06`。校准失败时不自动宣称姿态有效；保持静止且零目标时会后台重试，无需重启。读取整体失败（fresh_flags=0）清空待发有效位；分组新采样在发送前累计，陀螺仪和加速度分时到达不会互相覆盖待发标志。融合只使用本次的新陀螺仪/加速度，发送后消耗有效位，超龄数据不标为有效，避免旧缓存重复保活。主界面/航向控制使用的 `imu_yaw_deg` 也取自该四元数。

默认无参数 `imu_feedback_usable()` 在 flags=07 时可以通过。初始化阶段若只需角速度和加速度，可按实际用途检查：

```cpp
protocol.imu_feedback_usable(
    x5car::SerialProtocol::kAngularVelocityValid |
    x5car::SerialProtocol::kLinearAccelerationValid);
```

默认无参数 `imu_feedback_usable()` 要求三组全有效，因此在 flags=06 时返回 false。

串口布局保持不变：`qx,qy,qz,qw,gx,gy,gz,ax,ay,az,flags`。四元数按 xyzw 顺序、小端 float32 上传，无需修改原上位机协议。滤波没有使用尚未完整校准的磁力计，**yaw 为相对航向，长期仍可能漂移**。详细设计、参数及测试见 [姿态解算说明](imu_attitude.md)。

正常运行已自动解算，不需要主循环手工提交四元数。如果未来用外部 AHRS 替换当前算法，可在每次新估计后覆盖当前采样：

```c
float q[4] = {qx, qy, qz, qw};
RDK_UART_UpdateOrientation(q, 1U);
```

四元数必须表示传感器系相对 z 向上世界系的姿态，与三轴数据使用一致坐标轴；模长误差须不超过 0.01。函数检查并归一化。不要把持续不变的假姿态当成新测量提交。

## 验证与诊断

```powershell
cmake --build --preset Debug --parallel 4
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1
```

原生测试默认从桌面读取原始两个上位机协议文件。其他位置可指定：

```powershell
./tests/rdk_uart/run_tests.ps1 -ProtocolDirectory 'D:/path/to/upstream'
```

测试使用本机 gcc/g++ 和 HAL/电机模拟接口，编译实际 `rdk_uart.c`、`imu_attitude.c`，并直接链接原始 `x5car_protocol.cpp`。覆盖姿态初始化、三轴与组合旋转、变采样间隔、异常加速度、归一化和原始数据到串口的四元数互通；同时覆盖完整示例帧、分块和粘包、CRC/数值拒绝、零速解锁、超时和重解锁、UART 错误恢复、发送缓冲区生命周期、新采样有效性、单位换算、时钟回绕及 20000 字节噪声输入。

运行时切换测试不依赖原上位机文件，可单独执行 `powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_runtime_control_tests.ps1 -Configuration Debug`，Release 同理；覆盖无零速接管、遥控器在线复位、旧接收缓冲隔离、超时恢复和手动/失联保护。

`RDK_UART_GetStatistics()` 可读取合法命令数、CRC 错误、异常数值、严格授权策略下的零速前命令、超时、UART 错误、DMA 缓冲区溢出、过期命令和收发失败计数；`RDK_UART_IsCommandReady()` 查看是否已有未超时的有效命令。诊断只在本地读取，不混入 USART3 正式报文。OLED、UART5 恢复及时间优化的说明见 [实时性优化记录](history/realtime_optimization.md)。

按旋钮开启 OLED，在主页选择 **IMU DIAG**，可查看校准状态/最近拒绝原因、样本数/丢弃窗口数、姿态对准计数/重置数、陀螺仪与加速度采样年龄、超过 50 ms 的陀螺仪断采重置数和最长采样间隔。诊断页 10 Hz 更新，按下返回主页。`RDK_UART_GetImuDiagnostics()` 提供同样的本地姿态诊断；`UINT32_MAX` 年龄表示尚未收到该组采样。详细联调步骤见 [日志问题修复与验收](history/imu_log_fixes.md)。
