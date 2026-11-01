# x5car_bringup

统一启动模型、UART 硬件、控制器、twist_mux、第三方传感器及可选 SLAM/Nav2。
**当前不启动 EKF**。不要把 x5car_localization 的独立参考启动文件加入默认流程。

| 文件 | 后续修改内容 |
|---|---|
| `config/hardware.yaml` | MCU 串口、实际轮速上限、反馈超时、激活/停车等待 |
| `config/controllers.yaml` | 控制器类型、命令超时、IMU 协方差、TF 开关 |
| `config/lidar.yaml` | 本地雷达型号、串口/网口、扫描范围，当前为 M10P 示例 |
| `config/twist_mux.yaml` | 手动和导航输入、超时、优先级 |
| `launch/hardware.launch.py` | Xacro、动态几何参数、controller_manager、广播器和控制器顺序加载 |
| `launch/sensors.launch.py` | LSLidar 与 Astra Pro 驱动对接 |
| `launch/robot.launch.py` | hardware / mapping / navigation 三种互斥工作模式 |

硬件 launch 将控制器原始里程计重映射为 `/odom`，将私有 `tf_odometry` 重映射到 `/tf`。
`enable_odom_tf: true`，因此 odom->base_link 只有麦轮控制器发布。没有 EKF 的第二条里程计。

| 话题 | 消息类型 | 生产/消费 |
|---|---|---|
| `/cmd_vel_teleop` | geometry_msgs/msg/Twist | 手动输入，高优先级 |
| `/cmd_vel_nav_raw` | geometry_msgs/msg/Twist | Nav2 控制/行为服务器 → 平滑器 |
| `/cmd_vel_navigation` | geometry_msgs/msg/Twist | 平滑器 → mux，低优先级 |
| `/cmd_vel` | geometry_msgs/msg/Twist | mux 唯一输出 → 麦轮控制器 |
| `/odom` | nav_msgs/msg/Odometry | 麦轮控制器输出，Nav2 唯一里程计输入 |
| `/imu/data` | sensor_msgs/msg/Imu | IMU 广播器输出，当前不融合 |
| `/joint_states` | sensor_msgs/msg/JointState | 关节状态广播器输出 |
| `/scan` | sensor_msgs/msg/LaserScan | 雷达输出，frame=laser |

完整类型契约和端点映射见 [INTERFACES.md](INTERFACES.md)。当前整条速度链路使用 Twist。
硬件 launch 检查 use_stamped_vel 必须为 false，并重映射 reference_unstamped；
如果改成 true 会明确报错，避免同名话题因 Twist/TwistStamped 不一致而无法通信。

真实硬件参数未填写时启动会明确失败；不要把示例机械尺寸和最大轮速当成实测值。
无串口的接口联调可用 `use_mock_hardware:=true`，这不是物理仿真或真实传感器验证。

后续手工执行的命令（本次没有运行）：

```bash
ros2 launch x5car_bringup robot.launch.py use_mock_hardware:=true
ros2 launch x5car_bringup robot.launch.py mode:=hardware
ros2 launch x5car_bringup robot.launch.py mode:=mapping enable_lidar:=true
ros2 launch x5car_bringup robot.launch.py mode:=navigation enable_lidar:=true map:=$HOME/dev_ws/maps/room.yaml
ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -p stamped:=false -r cmd_vel:=/cmd_vel_teleop
```

相机启用加 `enable_camera:=true`，默认 astra_camera/astra_pro.launch.xml。
相机内部固定外参用 tf_publish_rate=0 发布到 `/tf_static`，避免当作动态 TF 重复刷屏。
建图同时导航加 `with_navigation:=true`；这时 Nav2 不启动 AMCL，map->odom 仍由 SLAM 唯一发布。
`rviz:=true` 才开 GUI，RDK 默认不打开 RViz。两路手动/自动仲裁只在 mux 中完成。
`use_sim_time` 默认 false；没有 /clock 时不要设 true，mock 也不代表有仿真时钟。
控制器发布的 wheel odometry 包含 vx/vy/wz，Nav2/DWB 和 AMCL 均按全向配置。
