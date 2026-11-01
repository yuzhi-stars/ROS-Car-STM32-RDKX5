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
| Docker 镜像（补充校验） | 本机原归档与历史 SHA-256 一致；此前 U 盘副本损坏，未发布该副本 |
| 镜像压缩包及内容 | gzip CRC/长度、tar 解析、全部 16 个 blob SHA-256、OCI 描述符引用及大小通过；包含 14 层 |
| Docker 导入及基础 ROS 运行 | `docker load` 成功；无网络、只读根文件系统、无宿主目录挂载的临时容器中，7 个关键 ROS 包和 rclpy 节点创建/销毁通过 |
| 镜像分片 | 三个分片逐份计算 SHA-256；按序合并后的 SHA-256 与完整原归档及历史记录一致 |

完整主机测试使用本仓库 `RDKX5Car/src/x5car_hardware/` 的实际协议实现。为适配固件测试脚本原先的平铺目录参数，测试时将协议 `.cpp`、`.hpp` 复制到仓库外的临时目录，并保留 `x5car_hardware/` 头文件子目录；不修改协议源码。

相机驱动原始 LFS 属性和忽略规则会遗漏随附 SDK，公开版已把 SDK 文件直接纳入 Git，克隆无需额外下载 LFS 对象。原始属性文件仍保留在源码归档中。

本次未执行 ROS 2 / ARM64 构建、Docker 重建、固件烧录或实车测试。软件检查结果不代表这些环境已经验证。

镜像补充检查详情见 [IMAGE_VALIDATION.json](IMAGE_VALIDATION.json)，分片下载及校验见[发布说明](RELEASE.md)。基础容器检查不代表图形显示、跨主机 DDS 通信或实车功能已通过验证。
