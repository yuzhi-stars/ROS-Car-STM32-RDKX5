# ROS 小车控制器：STM32F405RGT6 ＋ RDK X5

这是四轮麦克纳姆底盘的 STM32 下位机工程。RDK X5 上运行 ROS 2、slam_toolbox 和 Nav2；STM32 负责四轮编码器测速、20 ms 电机 PID、IMU 六轴 Mahony 姿态、串口反馈，以及 ELRS 手动/上位机控制切换。

**第一次复刻，先读 [VS Code＋DAPLink 编译烧录教程](docs/01_vscode_daplink.md)，再按 [硬件接线](docs/02_hardware.md) 和 [首次联调](docs/04_bringup.md) 操作。** 文件夹本身就是工程根目录，在 VS Code 里打开这一层。

## 文档入口

| 要做什么 | 阅读位置 |
|---|---|
| 从零安装工具、配置 VS Code、F8 编译烧录 | [完整环境教程](docs/01_vscode_daplink.md) |
| 确认 SWD、RDK 串口、电机、编码器和传感器接线 | [硬件与参数表](docs/02_hardware.md) |
| 学习代码结构、控制流程、定时器和 DMA | [固件结构说明](docs/03_firmware.md) |
| 首次通电、遥控建图、切导航、排查不动 | [首次联调与故障排查](docs/04_bringup.md) |
| 看字节协议、CRC、单位和有效标志 | [RDK 串口协议](docs/rdk_uart.md) |
| 配置遥控器 CH5/CH6，理解接管和失联行为 | [运行时控制说明](docs/runtime_control.md) |
| 学习四元数、零偏校准和 Mahony 参数 | [姿态解算说明](docs/imu_attitude.md) |
| 修改代码、重新生成 CubeMX、运行测试、导出资料 | [维护和发布说明](docs/05_maintenance.md) |
| 找历史排查记录或了解本次文件整理 | [文档索引](docs/README.md)、[资料整理说明](docs/06_materials.md) |

## 工程目录

```text
ROS_car_V1/
├── README.md                  复刻入口
├── Core/Inc、Core/Src/         下位机头文件和实现
├── Drivers/                   CMSIS 和 STM32 HAL，包含项目使用的修复
├── cmake/                     Arm GCC 工具链、CubeMX 源文件列表
├── .vscode/                   编译/烧录任务、插件与 F8 配置样例
├── tools/                     本机工具配置、自动构建/烧录与源码打包脚本
├── docs/                      当前教程和协议说明
│   └── history/               历史优化与问题核查记录
├── tests/rdk_uart/             电脑端模拟测试
├── MDK-ARM/                   原 Keil 工程，供参考
├── ROS_car_V1.ioc              STM32CubeMX 工程
├── STM32F405xx_FLASH.ld        链接脚本
├── startup_stm32f405xx.s       GCC 启动文件
├── build/                     本机生成产物，不分发
├── archive/                   原配置和临时产物备份，不分发
└── dist/                      导出的源码压缩包
```

## 已装好工具后的快速操作

将 `tools/local.settings.example.json` 复制为 `tools/local.settings.json`，改成自己的工具路径，再在工程根目录的 PowerShell 执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Check
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Setup
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Build -Configuration Debug
```

接好 DAPLink 和目标板后，只烧录、不进入调试：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/project.ps1 -Action Flash -Configuration Debug
```

`Flash` 会先配置并编译，失败则停止，不会继续烧录旧程序。VS Code 的 `Flash DAPLink OpenOCD` 任务调用同一个入口；按教程绑定 F8 后即可一键执行。F5 是断点调试，默认停在 `main()`，与只烧录不同。

## 当前控制方式

| 条件 | 行为 |
|---|---|
| CH6 低位、CH5 高位，遥控链路正常 | 上位机接管，清空旧接收缓冲后接收新速度帧，无需零速握手 |
| CH6 高位，完成 CH5 重解锁并摇杆回中 | 手动遥控；轮速/IMU 反馈继续上传，可用于建图 |
| CH5 低位或 CH6 中位 | 停止驱动 |
| 上电一直没有收到遥控帧 | 独立上位机控制，新有效速度帧即可接管 |
| 上位机命令中断达到 200 ms | 清空目标、PID 输出并关闭 PWM，收到新有效帧可恢复 |
| 使用过遥控后，遥控数据失效或达到 300 ms | 两种模式均停车，恢复后需 CH5 重新解锁 |

“低/高位”是接收机输出的通道值，不等于拨杆一定朝下/朝上。切到上位机档表示授权执行 RDK 当前发送的目标；RDK 若仍重发旧的非零导航目标，小车也可能起步。

## 复刻范围

本目录提供 STM32 软件工程、接线依据和联调步骤；未包含整车 PCB/原理图、完整 BOM、RDK 系统镜像、雷达驱动及 ROS 2 工作空间。不能只烧入本固件就获得完整导航系统，上位机仍需配置底盘驱动、雷达、TF、里程计以及 slam_toolbox/Nav2。

当前主构建路径为 **Windows PowerShell＋CMake＋MinGW Makefiles＋Arm GNU GCC＋OpenOCD＋DAPLink**。固件编译和主机模拟测试已验证；本次资料整理未连接硬件烧录，实板电气接线和动态性能仍需按教程验证。第三方驱动的版权文件保留在原目录。
