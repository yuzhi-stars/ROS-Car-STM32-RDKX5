# Humble 话题与消息类型契约

日期：2026-10-27。适用于本工作空间的 ROS 2 Humble 配置。重映射只改话题名称，不转换消息类型。
本文是接口约定；本轮没有运行节点，因此设备实际安装包的端点类型仍需后续在开发板核对。

## 1. 速度链路统一使用 Twist

| 线上话题 | 消息类型 | 发布端 | 订阅端 |
|---|---|---|---|
| `/cmd_vel_teleop` | `geometry_msgs/msg/Twist` | teleop_twist_keyboard，stamped=false | twist_mux 的 teleop 输入 |
| `/cmd_vel_nav_raw` | `geometry_msgs/msg/Twist` | controller_server、behavior_server 的 cmd_vel | velocity_smoother 的 cmd_vel |
| `/cmd_vel_navigation` | `geometry_msgs/msg/Twist` | velocity_smoother 的 cmd_vel_smoothed | twist_mux 的 navigation 输入 |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | twist_mux 的 cmd_vel_out | mecanum_controller 的 reference_unstamped |

以上命令为 base_link 中的车体速度，linear.x/y 单位 m/s，angular.z 单位 rad/s。
硬件插件本身不订阅 cmd_vel：麦轮控制器先计算四轮目标，再通过 velocity 命令接口把轮轴 rad/s 交给插件。

Humble 的 [Nav2 控制服务器](https://github.com/ros-navigation/navigation2/blob/humble/nav2_controller/src/controller_server.cpp)
和 [velocity_smoother](https://github.com/ros-navigation/navigation2/blob/humble/nav2_velocity_smoother/src/velocity_smoother.cpp)
在代码中创建 Twist 发布/订阅端；[twist_mux](https://github.com/ros-teleop/twist_mux/blob/humble/src/twist_mux.cpp)
也使用 Twist。本配置沿用这些原生接口，没有添加虚假的 stamped 配置开关。

## 2. 麦轮控制器两个输入不能混用

| use_stamped_vel | 控制器原生输入 | 消息类型 | 本项目是否采用 |
|---|---|---|---|
| false | `/mecanum_controller/reference_unstamped` | geometry_msgs/msg/Twist | 是，重映射成 `/cmd_vel` |
| true | `/mecanum_controller/reference` | geometry_msgs/msg/TwistStamped | 否，未接到本项目速度链路 |

依据：[Humble 麦轮控制器接口](https://control.ros.org/humble/doc/ros2_controllers/mecanum_drive_controller/doc/userdoc.html)。
官方 Humble 分支的 [参数声明文件](https://github.com/ros-controls/ros2_controllers/blob/humble/mecanum_drive_controller/src/mecanum_drive_controller.yaml)
明确声明 use_stamped_vel 为 bool、默认 true；实现是在 on_configure 中读取它并创建对应订阅端。
这是当前官方 Humble 实现的依据，不等于已经检查开发板旧镜像或厂家修改版本。
本包 `controllers.yaml` 显式指定 false，hardware.launch.py 会检查这一约定。
launch 的检查只校验本项目配置，没有替代板端参数声明核对；不要把运行中 param set 成功当成订阅类型已切换。
不要因其他控制器或其他 ROS 发行版默认使用 TwistStamped，就改接到 reference。
如果以后接入只能输出 TwistStamped 的设备，需要边界转换节点（读取 msg.twist），
或整体重做速度接口配置；不能仅把两个不同类型的话题 remap 成同名。

Twist 没有 header，但控制器的 unstamped 回调使用接收时刻管理 reference_timeout，
所以仍保留 ROS 指令 0.2 秒超时；这不替代 MCU 的独立串口命令超时停车。

手动输入命令明确选择未盖时间戳的类型：

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard --ros-args -p stamped:=false -r cmd_vel:=/cmd_vel_teleop
```

该参数由 [Humble 发布的 teleop_twist_keyboard 源码分支](https://github.com/ros2/teleop_twist_keyboard/blob/dashing/teleop_twist_keyboard.py)
声明；分支名为 dashing 是该包沿用的维护分支，不代表项目切换了 ROS 版本。

## 3. 状态与感知话题

| 话题 | 消息类型 | 发布者/主要使用者 |
|---|---|---|
| `/odom` | nav_msgs/msg/Odometry | mecanum_controller → Nav2；header.frame_id=odom，child_frame_id=base_link |
| `/imu/data` | sensor_msgs/msg/Imu | imu_sensor_broadcaster，frame=imu_link；当前默认不进入 EKF |
| `/joint_states` | sensor_msgs/msg/JointState | joint_state_broadcaster → robot_state_publisher |
| `/scan` | sensor_msgs/msg/LaserScan | LSLidar → SLAM、AMCL、Nav2，frame=laser |
| `/map` | nav_msgs/msg/OccupancyGrid | SLAM Toolbox 或地图服务器，二者按工作模式选择 |
| `/camera/color/image_raw` | sensor_msgs/msg/Image | Astra Pro 彩色图像 |
| `/camera/depth/image_raw` | sensor_msgs/msg/Image | Astra Pro 深度图；编码/单位以驱动 Image.encoding 为准 |
| `/camera/color/camera_info` | sensor_msgs/msg/CameraInfo | 相机内参 |
| `/camera/depth/camera_info` | sensor_msgs/msg/CameraInfo | 深度内参 |
| `/camera/depth/points` | sensor_msgs/msg/PointCloud2 | Astra 深度点云 → 可选 Nav2 障碍层 |
| `/robot_description` | std_msgs/msg/String | robot_state_publisher，模型文本 |
| `/initialpose` | geometry_msgs/msg/PoseWithCovarianceStamped | RViz 2D Pose Estimate → AMCL |

相机话题名称按当前复制的 Astra Pro 配置约定；换厂家驱动后要同时调整 remap 和导航输入。
ROS 话题的 Odometry 内部虽然含 twist 字段，它的顶层消息类型仍是 Odometry，不能替代速度命令话题。

## 4. TF 与 action

`/tf` 和 `/tf_static` 的类型均为 `tf2_msgs/msg/TFMessage`，不是 TransformStamped 单条消息。
`/tf_static` 的订阅需兼容 Transient Local；类型一致之外，还需要核对 QoS 是否兼容。

| TF 边 | 唯一发布者 |
|---|---|
| map → odom | SLAM Toolbox 或 AMCL，互斥 |
| odom → base_link | mecanum_controller，私有 tf_odometry 已重映射 /tf |
| base_link → 模型/轮/传感器安装坐标系 | robot_state_publisher |
| camera_link → 相机内部光学坐标系 | Astra 驱动，当前配置发布静态外参 |

导航目标用 `/navigate_to_pose`，类型为 `nav2_msgs/action/NavigateToPose`，它是 action 接口，
不是在同名普通 topic 发布一个 PoseStamped。当前 RViz 使用 Nav2 GoalTool 发送该 action。
EKF 独立参考包不由任何默认启动文件启动，不增加第二个 /odom 或 odom->base_link 发布者。

## 5. 开发板后续只读核对命令

以下命令留作联调时使用，本轮未执行、不发送任何运动指令：

```bash
ros2 topic type /cmd_vel_teleop
ros2 topic type /cmd_vel_nav_raw
ros2 topic type /cmd_vel_navigation
ros2 topic type /cmd_vel
ros2 topic info /cmd_vel --verbose
ros2 topic type /odom
ros2 topic type /imu/data
ros2 topic type /tf
ros2 pkg xml mecanum_drive_controller
ros2 param describe /mecanum_controller use_stamped_vel
ros2 param get /mecanum_controller use_stamped_vel
```

四个速度话题都应为 geometry_msgs/msg/Twist。若同名存在 Twist 和 TwistStamped 两种端点，
先查发布者与订阅者，再修正类型/入口，不要继续叠加重映射。随后核对 QoS、节点生命周期状态、
namespace、ROS_DOMAIN_ID 和 ROS 图发现，不能仅因 topic list 里出现名称就认定已连通。
参数 describe/get 要在控制器节点已加载时执行；若该版本没有此参数，不能继续依赖 false 配置，
应先更换为已核对接口的 Humble 控制器版本，或按实际入口补消息转换节点。
