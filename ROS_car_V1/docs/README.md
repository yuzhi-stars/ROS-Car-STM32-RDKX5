# 文档索引与阅读顺序

## 第一次复刻

1. [VS Code＋DAPLink 从零编译烧录](01_vscode_daplink.md)：工具安装、唯一的本机路径配置、任务、F8/F5 和常见构建错误。
2. [硬件接线与底盘参数](02_hardware.md)：按当前源码逐项核对引脚、供电逻辑、轮序、计数和几何尺寸。
3. [固件代码结构与工作流程](03_firmware.md)：主循环、20 ms 控制、中断/DMA、姿态与单位转换。
4. [首次联调与常见故障](04_bringup.md)：从上电反馈到手动建图、切上位机、诊断停在哪一层。
5. [维护、测试与源码分发](05_maintenance.md)：CubeMX 再生成、参数联动、测试边界和导出压缩包。

## 当前实现的专题文档

| 文档 | 内容 |
|---|---|
| [rdk_uart.md](rdk_uart.md) | AA55 帧、四轮角速度、四元数/角速度/加速度、CRC 和有效位 |
| [runtime_control.md](runtime_control.md) | CH5/CH6、无需上位机握手的接管、超时、失联锁定与 OLED 状态 |
| [imu_attitude.md](imu_attitude.md) | 六轴 Mahony、姿态约定、初始化、零偏和算法测试 |
| [06_materials.md](06_materials.md) | 文件整理结果、保留项、归档项和验证记录 |

以上文档按当前源码维护。历史文档中“必须新零速解锁”“每次切上位机必须 CH5 低后高”等旧行为，已经由当前 [运行时控制说明](runtime_control.md) 取代。

## 历史记录

| 文档 | 用途 |
|---|---|
| [最初中断与 CPU 核查](history/interrupt_performance_review.md) | 理解原始风险和优化动机，不能当作当前实现清单 |
| [DMA/OLED/时间基准优化](history/realtime_optimization.md) | 查看最初一轮改动 |
| [第二轮边界核查](history/followup_review.md) | TX 回调丢失、DMA 恢复、HAL I2C 修复等 |
| [IMU 日志问题修复](history/imu_log_fixes.md) | flags=06、校准重试、姿态初始化和静止轮速问题的来由 |

历史记录保留原有分析和测试语境；学习时优先看当前教程，再回看相应修复。
