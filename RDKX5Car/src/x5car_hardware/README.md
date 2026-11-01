# x5car_hardware

Linux/RDK X5 的 `hardware_interface::SystemInterface` 插件，插件名 `x5car_hardware/X5CarSystem`。
本包实际实现 UART 和生命周期，不依赖此前说明文档中的未实现串口辅助函数。

| 文件 | 后续修改位置 |
|---|---|
| `src/x5car_system.cpp` | 参数校验、生命周期、read/write、故障停机、关节映射 |
| `src/serial_port.cpp` | Linux 115200/8N1 UART、独占访问、短写、有限发送队列 |
| `include/x5car_hardware/safety_checks.hpp` | 单调时钟循环监控、逐帧连续零速确认 |
| `src/x5car_protocol.cpp` | 命令打包、拆包/粘包、CRC、有效位、超时查询 |
| `include/x5car_hardware/x5car_protocol.hpp` | 固定帧协议完整逐字节注释、十六进制样例 |
| `x5car_hardware.xml` / `CMakeLists.txt` | pluginlib 导出及共享库编译 |
| `test/protocol_checks.cpp` | 协议单元检查，只有显式 colcon test 才运行 |
| `test/hardware_safety_checks.cpp` | 不依赖 ROS 的确定性保护回归检查 |

四轮顺序固定 FL、FR、RL、RR，关节名按 hardware 参数映射，不能按 URDF 遍历顺序猜测。
接口：四轮 `velocity` 命令与 `position/velocity` 状态；IMU 10 个标准状态接口。
position 由实际轮速积分，仅用于显示，不是掉电持久化编码器位置。
IMU 的有效标志在本包内检查；广播器只读取数值状态。

`on_configure` 打开串口；`on_activate` 在激活等待上限内发零速并等待轮速/IMU；`read` 调用 feed 并更新状态；
`write` 调用 encode_speed_command 并处理发送；停用等待停车，错误/关闭有界尝试停机并清理。
周期 read/write 不 sleep；生命周期等待有截止时间。必须保留 MCU 独立 200 ms 命令超时停车。
串口只由本包打开，不同时启动其他 MCU/IMU 串口节点。使用 TIOCEXCL 防止重复打开。

线协议无时间戳和序号：AA55 + TYPE + 固定载荷 + CRC16/CCITT-FALSE，CRC 覆盖 TYPE+载荷。
0x03 目标速度 21 字节；0x90 实际轮速 22 字节；0x91 IMU 46 字节。详细定义见头文件。
内部 steady_clock 只用于上位机接收超时，不在线上传。不能识别下位机持续重放的旧采样。

配置入口是 `x5car_bringup/config/hardware.yaml`，真实轮速上限默认 0，必须填写实际正值。
真实硬件只能在 Linux ARM64 上构建；本地 Windows 编译器可检查独立协议模块，不能代替 ROS 构建。
`require_imu` 默认 true；当前按联调要求只要求 IMU 角速度+加速度有效（0x06），暂不要求姿态四元数。
姿态无效时保留解析器的 NaN，不用单位四元数冒充；恢复完整约束见 feedback_ok() 的注释。
激活后所需反馈无效或超过 200 ms 触发故障、零速及清理。
四轮全部 NaN（Humble 控制器停用状态）解释为零速；部分 NaN、Inf 或越界仍报错。
没有硬件使能协议，因此 active/inactive 是本地发送门控，不能等同驱动器物理上电/断电。

构建：在开发板工作空间运行 `colcon build --packages-select x5car_hardware`。
检查：`colcon test --packages-select x5car_hardware`；本轮未执行 ROS 构建或真实串口运行。

## 2026-10-28 保护修复

read/write 分别使用 steady_clock 监控循环间隔，任一达到 100 ms 后锁定故障；
在读取积压数据前清串口和协议缓存、尝试零速。故障后继续调用 read/write 均返回 ERROR，
只有显式重新配置/激活并完成零速握手才恢复，不重放旧非零命令。

激活、停用和关闭的停车确认逐帧观察所有轮速反馈，要求有效零速样本覆盖至少 200 ms。
同一批数据中的非零/无效帧也会清除稳定状态；只有一批零帧后一直静默不能通过。
`activate_timeout_ms` 和 `stop_timeout_ms` 必须大于 200 ms，默认值分别 3000/500 ms。
串口 receive 新增可选 WheelObserver，常规 read 保持批量解析；停车时才逐帧观察。
线协议及原编解码源码没有改变。

已通过 Windows 离线安全检查和真实组件源码的模拟系统调用集成检查，包括 250 ms 循环停顿、
read/write 之间延迟、故障锁定和重新激活、拆包/短写、混合零/非零帧、静默，以及 IMU=0x06。
集成检查替代了 ROS 类型和串口系统调用，不能代替 RDK/Linux 的 ROS 构建、ABI 或实机验证。

本包此次修改：`src/x5car_system.cpp`、`include/x5car_hardware/x5car_system.hpp`、
`src/serial_port.cpp`、`include/x5car_hardware/serial_port.hpp`、`CMakeLists.txt`、`README.md`；
新增 `include/x5car_hardware/safety_checks.hpp`、`test/hardware_safety_checks.cpp`。
