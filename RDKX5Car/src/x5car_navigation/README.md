# x5car_navigation

Nav2 全向导航：AMCL OmniMotionModel + NavFn + DWB + velocity_smoother。
唯一里程计输入 `/odom`，当前发布者为麦轮控制器。本包不启动 EKF。

| 文件 | 维护内容 |
|---|---|
| `config/nav2.yaml` | 用户覆盖配置：横向速度、加减速、DWB、AMCL、代价地图与深度点云 |
| `launch/navigation.launch.py` | 深度合并已安装 Humble Nav2 默认参数、生成 footprint、启动服务器及生命周期管理 |
| `rviz/navigation.rviz` | map 固定坐标、2D Pose Estimate、Nav2 Goal 工具 |

```bash
ros2 launch x5car_navigation navigation.launch.py   localization:=true   map:=/root/ecust_ws/src/x5car_slam/maps/map.yaml   use_sim_time:=false
```

配置文件是覆盖集，不是可单独直接传给所有 Nav2 原生 launch 的完整 YAML。
自定义启动文件读取本机 nav2_bringup 默认 YAML，再应用覆盖，保留 Humble 对应 BT 插件列表。
临时合并文件在 launch 关闭时清理；不要在 install 目录编辑临时产物。
footprint 根据 x5car_description/config/robot.yaml 中车体和轮子最大外廓生成；额外凸出物要更新尺寸。

输出链：controller_server/behavior_server -> `/cmd_vel_nav_raw` -> velocity_smoother
-> `/cmd_vel_navigation` -> bringup 中的 twist_mux -> `/cmd_vel` -> 麦轮控制器。
不启用第二个导航速度平滑器；不要绕过 mux 将手动指令直接发到 `/cmd_vel`。
这条速度链路全部是 `geometry_msgs/msg/Twist`，不是 TwistStamped。
麦轮控制器使用 `use_stamped_vel: false` 和 `reference_unstamped` 输入；remap 不转换消息类型。
完整消息类型表见 [bringup/INTERFACES.md](../x5car_bringup/INTERFACES.md)。

定位导航由 `localization:=true` 启动 map_server 和 AMCL，必须提供真实 `map` 文件。
首次定位需用 RViz 的 2D Pose Estimate 设置 `/initialpose`，定位成功后再用 Nav2 Goal 发目标。
建图导航用 `localization:=false`，依赖外部 SLAM 提供 map->odom，绝不并行启动 AMCL。
一键入口见 bringup README；独立入口只负责导航，不会重复启动底盘和传感器。
导航必须已经存在 `/scan`、`/odom` 以及完整 TF 链。

`use_depth:=true` 将 `/camera/depth/points` 加入局部和全局 obstacle_layer；默认 false。
启用前核对 Astra 驱动实际点云话题和光学 TF，必要时修改 nav2.yaml 的 depth_points.topic。
当前“视觉感知”是 RGB/深度/点云及障碍物处理，没有预设目标检测模型。
DWB 参数支持正负 vy；最大 vx/vy/wz 的组合必须符合实际轮轴角速度限制，否则硬件会拒绝越界命令。
所有速度、足迹和噪声是待实车调校初值，本轮未运行或验证自主导航表现。
