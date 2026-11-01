# x5car_slam

通过 slam_toolbox online_async 建二维激光地图；输入 `/scan` 和 TF `odom->base_link->laser`。
**当前 odom->base_link 来自麦轮控制器，不是 EKF。**本包只发布 map->odom 和 `/map`。
参数修改入口 `config/slam_toolbox.yaml`：激光距离、分辨率、更新距离和回环匹配。
地图是真实建图产物，没有内置假地图。运行时地图存工作空间 `~/dev_ws/maps`，不要写 install/share。

完整系统入口：`ros2 launch x5car_bringup robot.launch.py mode:=mapping enable_lidar:=true`。
若底盘和雷达已由其他 launch 启动，只运行 `ros2 launch x5car_slam slam.launch.py`。
不要同时运行 AMCL 或第二个 SLAM Toolbox。

未来保存导航用地图：

```bash
mkdir -p ~/dev_ws/maps
ros2 run nav2_map_server map_saver_cli -f ~/dev_ws/maps/room
```

需要继续编辑 SLAM posegraph 时另外使用 slam_toolbox 的 serialize_map 服务；PGM/YAML 仅供静态地图定位。
上述命令均为后续使用说明，本轮没有执行节点、建图或地图保存。
