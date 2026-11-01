# 本轮静态检查记录

日期：2026-10-27；目标平台：RDK X5 / Linux ARM64 / ROS 2 Humble。

本轮未启动 ROS 节点、未打开实际设备、未运行测试可执行程序，也没有执行小车运动。

| 检查 | 结果 |
|---|---|
| 六个自定义包的 package.xml、CMakeLists.txt、README.md | 齐全 |
| 全工作空间 ROS 包名称 | 10 个，未发现同名包重复 |
| 自定义 Python 启动文件 | AST 语法检查通过 |
| 自定义 YAML、RViz、XML | 解析检查通过 |
| Xacro 静态展开 | 真实/mock 两种分支均通过，轮序、10 项 IMU 接口、TF 子节点唯一性已核对 |
| 独立协议源码及检查源码 | Clang C++17 `-fsyntax-only -Wall -Wextra -Wpedantic -Werror` 通过，仅静态检查，未执行 |
| 相机 ARM64 SDK | 已核对 libOpenNI2.so 为 ELF64/AArch64；未验证运行时 ABI |
| 里程计/TF | 默认只有麦轮控制器 `/odom` 和 odom->base_link，EKF 未接入 |
| Nav2 | 输入统一 /odom，DWB 配置包含 vy，AMCL 采用 OmniMotionModel |
| 速度消息类型 | 四个 cmd_vel 话题约定 Twist，控制器 use_stamped_vel=false、入口 reference_unstamped，遥控显式 stamped=false；核对依据是官方 Humble 源码，未读取板端安装版本 |
| 相机内部 TF | 副本移除 color_frame 多父节点冗余边，默认采用静态广播 |

仍需在开发板完成：Linux UART 和完整 ROS 2 插件编译/加载、第三方驱动依赖与 SDK ABI 验证、
轮序和方向核对、实测机械参数及轮速上限、停机性能、激光/视觉外参、Nav2 实车调参。
当前 hardware.yaml 的真实电机速度上限为 0，真实设备启动会拒绝，填写实测值后再联调。

Windows 独立协议检查不能代替 Linux ROS 2 插件编译。Xacro 静态展开使用隔离的 xacro 2.1.1
工具，只验证文本展开；开发板需使用其 Humble 环境中的 xacro 和 middleware 做完整构建。
