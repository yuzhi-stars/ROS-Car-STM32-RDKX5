# 首次公开发布检查（2026-11-01）

| 检查 | 结果 |
| --- | --- |
| 原始固件 ZIP 完整性 | ZIP CRC 检查通过 |
| ROS 源码归档 SHA-256 | 与原始记录一致 |
| ROS 源码展开 | 与归档逐文件核对一致；相机驱动 `.gitattributes` 随后调整为普通 Git 存储，源码和 SDK 文件不变 |
| STM32 Debug 干净构建 | CMake + Arm GNU GCC 14.3.1 通过；FLASH 87,756 B，RAM 13,360 B |
| STM32 主机控制测试 | `run_runtime_control_tests.ps1 -Configuration Debug` 通过 |
| 完整主机测试 | `run_tests.ps1 -Configuration Debug` 通过，涵盖姿态、校准、协议互通、DMA、ELRS、OLED、里程计、菜单、控制仲裁及 HAL I2C 错误处理 |
| Python 语法 | 23 个文件解析通过 |
| ROS 包清单 | 10 个 `package.xml` XML 解析通过 |
| 新增首页与导航 | 本地相对链接目标检查通过 |
| Docker 原始镜像 | SHA-256 与历史记录不符，gzip CRC 失败；本次未发布 |

完整主机测试使用本仓库 `RDKX5Car/src/x5car_hardware/` 的实际协议实现。为适配固件测试脚本原先的平铺目录参数，测试时将协议 `.cpp`、`.hpp` 复制到仓库外的临时目录，并保留 `x5car_hardware/` 头文件子目录；不修改协议源码。

相机驱动原始 LFS 属性和忽略规则会遗漏随附 SDK，公开版已把 SDK 文件直接纳入 Git，克隆无需额外下载 LFS 对象。原始属性文件仍保留在源码归档中。

本次未执行 ROS 2 / ARM64 构建、Docker 重建、固件烧录或实车测试。软件检查结果不代表这些环境已经验证。
