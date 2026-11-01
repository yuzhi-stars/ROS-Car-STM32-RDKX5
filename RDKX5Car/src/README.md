# RDK X5 四麦轮 ROS 2 Humble 源码

本轮源码位于本地 dev_ws/src，后续同步到开发板 `/home/robotwzz/dev_ws/src`。
本轮只写代码并做静态检查：没有启动节点、打开设备或运行小车。

| 包 | 职责 | 维护入口 |
|---|---|---|
| x5car_hardware | UART、协议解析、真实 ros2_control 插件 | README.md、src/x5car_system.cpp |
| x5car_description | 模型、机械参数、安装 TF | README.md、config/robot.yaml |
| x5car_bringup | 控制器、启动、速度仲裁、传感器 | README.md、config/、launch/ |
| x5car_localization | 独立 EKF 参考实现，默认不接入 | README.md、config/ekf.yaml |
| x5car_slam | 激光建图 | README.md、config/slam_toolbox.yaml |
| x5car_navigation | Nav2 全向导航、AMCL、障碍物 | README.md、config/nav2.yaml |

原有雷达代码保持原路径；相机驱动在 drivers/ros2_astra_camera，每个实际 ROS 包均有 README。

## 当前唯一话题和 TF 链

里程计只有 `/odom`：由 mecanum_controller 输出，Nav2 所有 odom_topic 都使用它。
没有默认 `/odometry/filtered` 链路，也没有默认 EKF 节点。

| TF | 唯一发布者 |
|---|---|
| map -> odom | 建图时 SLAM Toolbox；地图定位时 AMCL，二者互斥 |
| odom -> base_link | mecanum_controller，enable_odom_tf=true；tf_odometry 已重映射 /tf |
| base_link -> body/轮/imu_link/laser/camera_link | robot_state_publisher |
| camera_link -> 相机内部传感器/光学坐标系 | Astra 驱动 |

命令：手动 `/cmd_vel_teleop` 与导航 `/cmd_vel_navigation` 经 twist_mux 输出 `/cmd_vel`，
再交给麦轮控制器。IMU 通过 `/imu/data` 提供完整数据，目前不融合，避免多条 odom 和 TF 争用。
四个速度话题（含中间 `/cmd_vel_nav_raw`）统一 `geometry_msgs/msg/Twist`；控制器显式使用
`use_stamped_vel: false` 和 `reference_unstamped`，遥控命令显式 `stamped:=false`。
话题全名、类型、发布/订阅端及 TF 类型见 [接口契约](x5car_bringup/INTERFACES.md)。

## 开发板后续准备（以下命令本轮没有执行）

Ubuntu 22.04 ARM64 + ROS 2 Humble，先 source /opt/ros/humble/setup.bash。
先安装 Humble 较新的 ros2_controllers：旧镜像可能没有 mecanum_drive_controller。

```bash
sudo apt update
sudo apt install ros-humble-ros2-control ros-humble-ros2-controllers \
  ros-humble-mecanum-drive-controller ros-humble-robot-localization \
  ros-humble-slam-toolbox ros-humble-navigation2 ros-humble-nav2-bringup \
  ros-humble-xacro ros-humble-twist-mux ros-humble-teleop-twist-keyboard \
  ros-humble-tf2-eigen libuvc-dev libgoogle-glog-dev libopencv-dev \
  libeigen3-dev libusb-1.0-0-dev libpcap-dev libpcl-dev
cd ~/dev_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install --parallel-workers 1
source install/setup.bash
```

厂家包 manifest 未完整声明的 SDK 系统依赖见各 README。先确认 MCU 串口用户权限和设备名，
不要盲目运行原目录的 wheeltec_udev.sh；规则要按实际 USB 设备 VID/PID 和序列号编写。
真实启动前修改 hardware.yaml 的设备路径和正的实测轮速上限、robot.yaml 全部示例几何、
lidar.yaml 实际型号，以及相机 ID/外参。未填写轮速上限时真实硬件启动会明确拒绝。

首次建议只联调 hardware 模式，逐轮确认 FL/FR/RL/RR 和符号，再核对 /odom 与 TF，最后建图导航。
具体启动命令在 x5car_bringup/README.md，后续 EKF 切换步骤只在 x5car_localization/README.md。
不要在Windows用本机编译器生成 RDK 的 ROS插件；Linux/ROS2 完整编译与实机验证仍待开发板执行。

官方接口参考：[Humble 麦轮控制器](https://control.ros.org/humble/doc/ros2_controllers/mecanum_drive_controller/doc/userdoc.html)、
[Humble 硬件组件](https://control.ros.org/humble/doc/ros2_control/hardware_interface/doc/writing_new_hardware_component.html)。
