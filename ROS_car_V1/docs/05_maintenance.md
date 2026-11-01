# 维护、测试与源码分发

## 1. 修改入口

| 要改什么 | 对应位置与联动 |
|---|---|
| 电机接线/方向/反馈符号 | `Motor.c` 的 `motor_config[]`，保留 FL/FR/RL/RR 顺序 |
| 默认 PID | `main.c` 的 `motor_pid[]`；有效 FRAM 参数会覆盖默认值 |
| 轮径/轴距/轮距 | `Odom.c`，同时核对 RDK 几何参数 |
| 每轮一圈计数 | `Odom.c`、`rdk_uart.h` 同步修改并重新标定 |
| 手动速度 | `ELRS.c` 平移/转向上限、`drive_control.h` 每轮上限 |
| CH5/CH6 和接管条件 | `drive_control.c`，保留单一来源和失联策略 |
| 串口帧/有效标志/超时 | `rdk_uart.c/.h`，改布局需上下位机同步 |
| IMU 量程/安装轴 | `IMU.c`、单位换算、校准和坐标变换一起核对 |
| Mahony 参数 | `imu_attitude.h`、`imu_calibration.c/.h`，随后跑算法测试 |

控制周期改变后，实际 dt、PID 离散系数、滤波系数和里程计都要重审。新增业务 `.c` 加入根 `CMakeLists.txt` 的 `target_sources()`；CubeMX 基础清单在 `cmake/stm32cubemx/CMakeLists.txt`。需要 Keil 时再同步其源文件清单，本次 GCC 构建不等于 Keil 已验证。

## 2. CubeMX 再生成

`.ioc` 保存主要配置，但不保证重现全部手工修复。即使选择保留 USER CODE，生成区内的改动也可能丢失。重点备份/对比：

- `main.c`：USART1 停用、USART3 初始化、启动顺序、主循环、回调。
- `usart.c`、`dma.c`、`i2c.c`、`stm32f4xx_it.c`：DMA、IRQ 和超时服务。
- `Motor.c`：运行时 TIM13 的 20 ms 配置。
- `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c` 中 `Project fix` 补丁。
- 根 CMake、工具链、自定义模块清单和 VS Code 任务。

先导出源码 ZIP，另外复制一份工程进行生成，对比合并后再编译/测试。不要直接覆盖唯一工作副本。

## 3. 独立控制模拟测试

电脑需要 MinGW 的 `gcc.exe`、`g++.exe`。脚本会将 `tools/local.settings.json` 中 `makeBin` 加入自身 PATH；该目录必须包含原生编译器，不能用 Arm 编译器来运行 Windows 测试。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_runtime_control_tests.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_runtime_control_tests.ps1 -Configuration Release
```

直接编译实际 RDK/ELRS/DMA/控制仲裁/麦轮源码，用模拟 HAL 和数据帧检查：无遥控启动、在线 HOST 档复位、CH5 高低、手动切上位机、非零首帧、RX 积压屏障、200/300 ms 超时、失联锁定、手动回中、CRC/NaN/范围/过期拒绝。

测试不连接电机或 DAPLink，不能替代真实 USB 驱动、总线波形、中断抢占、CPU 最坏占用和电机动态性能验证。

## 4. 完整历史互通测试

完整入口还测试姿态、校准、原上位机互通、OLED、菜单、里程计、ELRS 和 HAL I2C 错误处理：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Debug -ProtocolDirectory 'C:/path/to/upstream'
powershell -NoProfile -ExecutionPolicy Bypass -File tests/rdk_uart/run_tests.ps1 -Configuration Release -ProtocolDirectory 'C:/path/to/upstream'
```

指定目录必须有真实 `x5car_protocol.cpp/.hpp`。它们目前不在本目录中，未加入分发包；默认桌面路径只是旧测试默认值，不是复刻者必须把工程放桌面。

缺文件时完整入口会退出，不能把“未运行”记成“通过”。只测当前接管逻辑用上一节。姿态和校准模块也可按完整脚本中的独立 gcc/g++ 命令编译。

## 5. 缓存与本机配置

`build/Debug`、`build/Release` 中的 ELF、map、compile_commands、对象文件和 `.su` 是派生文件，含本机路径，不作为移植输入。移动源码或更换生成器/工具链后，关闭占用程序，把对应构建目录改名备份，再重新配置；不要删除业务源码来修缓存。

`Setup` 生成的 `.vscode/launch.json`、`c_cpp_properties.json` 和本机 `local.settings.json` 已忽略；公共任务、扩展推荐、模板和脚本保留。

## 6. 导出给别人

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/export_source.ps1
```

生成 `dist/ROS_car_V1_source.zip`，顶层为 `ROS_car_V1/`，包含源代码、HAL/CMSIS、CMake/CubeMX、Keil 源工程、文档、测试和公共 VS Code 配置。

不包含 build/archive/dist 自身、个人 IDE 状态、本机路径和生成的调试配置。别人解压后按 [环境教程](01_vscode_daplink.md) 创建本机配置，Check → Setup → Build。软件安装包、PCB/BOM 和 RDK 工作空间不在包中；第三方驱动的许可证保留。

不会覆盖已有 ZIP，后续发布使用新名字：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/export_source.ps1 -OutputFile 'dist/ROS_car_V1_source_v2.zip'
```

## 7. 验证记录

移植后记录硬件差异、参数、工具版本、构建配置和实际跑过的测试。软件模拟与实车验收分开；实板确认方向、有效帧间隔、遥控失联和停止输出。实时性改动补实测中断耗时/周期抖动及测量条件。
