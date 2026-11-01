# RDKx5 ROS Car：电脑运行算法，板卡运行硬件

> 公开版镜像说明：原始 Docker 镜像校验失败，未随仓库发布。电脑端请先按[容器重建说明](../../docs/RELEASE.md)准备镜像；下文直接导入原镜像的步骤暂不可用。源码可直接使用 `../src/` 或 `archives/WORKSPACE_SRC.tar.gz`。


本说明对应第二条链路：电脑 Docker 运行 SLAM 或 Nav2＋AMCL，以及 RViz；板卡运行底盘控制、雷达等传感器和基础 TF。电脑经 DDS 接收传感器、里程计及 TF，计算速度后发回板卡执行。

第一次配置按本文前半部分执行；配置完成后，直接使用第 10 节的日常命令。板卡 SSH 后保持 `robotwzz` 用户，电脑使用 Humble 容器，RViz 启动命令只需 `rviz2`。

## 1．两端分别运行什么

| 功能或节点 | 板卡 | 电脑 Docker |
| --- | --- | --- |
| 底盘串口、ros2_control、麦轮控制器 | 运行 | 不启动 |
| 雷达驱动、可选相机驱动 | 运行 | 不启动 |
| robot_state_publisher、关节状态及基础 TF | 运行 | 不重复启动 |
| twist_mux，汇总导航和遥控速度 | 运行 | 不重复启动 |
| SLAM Toolbox 建图 | 不启动 | 建图时运行 |
| Map Server＋AMCL | 不启动 | 已有地图导航时运行 |
| Nav2、代价地图、规划、控制与速度平滑 | 不启动 | 导航时运行 |
| RViz | 不启动 | 运行 |

板卡仍然负责采集传感器、计算轮式里程计和执行电机控制，不能只保留一个接收速度的空节点。雷达、底盘和相机连接板卡，无需改插到电脑。

数据流：

```text
板卡 /scan、/odom、/tf、/tf_static ──DDS──→ 电脑 SLAM / AMCL / Nav2

电脑 Nav2 → /cmd_vel_nav_raw → velocity_smoother
       → /cmd_vel_navigation ──DDS──→ 板卡 twist_mux
       → /cmd_vel → mecanum_controller → 底盘 MCU
```

TF 的发布位置：

| TF 边 | 唯一发布者 |
| --- | --- |
| `map → odom` | 电脑 SLAM 或电脑 AMCL，按模式二选一 |
| `odom → base_link` | 板卡麦轮控制器 |
| 车体到雷达、IMU 等固定 TF | 板卡 robot_state_publisher |

不要在电脑启动 `x5car_bringup robot.launch.py mode:=mapping/navigation`。该总入口会同时启动底盘和传感器；电脑应使用本文给出的独立算法 launch。

## 2．准备路径与前提

| 项目 | 本文使用的值 |
| --- | --- |
| 板卡 SSH | `ssh robotwzz@192.168.1.48` |
| 板卡工作空间 | `/home/robotwzz/dev_ws` |
| 电脑部署包 | `/home/zzw/RDKX5Car`，即 `~/RDKX5Car` |
| 电脑工作空间 | `/home/zzw/ecust_ws`，即 `~/ecust_ws` |
| 电脑容器工作空间 | `/root/ecust_ws` |
| 容器 | `x5car_dev` |
| 本文默认导航地图 | `/root/ecust_ws/maps/room.yaml` |

IP 和用户名以实际设备为准。如果板卡完全没有项目，先按[从零部署说明](RDKx5_ROS_Car_新板卡从零部署算法与首次运行说明.md)第 1～7 节完成用户、SSH、源码、依赖、串口、硬件参数、编译、自动环境和时间配置。运行位置以本文为准，板卡后续只启动 hardware 模式。

板卡安装了 SLAM/Nav2 软件包并不意味着它们正在运行，无需为了切换链路卸载这些包。

电脑沿用已配置的镜像、host 网络、共享工作空间和 X11 授权。如果 Docker 容器尚未建立，按上述从零部署说明第 8 节先建立容器。

如果从[板卡运行算法](RDKx5_ROS_Car_板卡运行算法和Rviz_电脑端RViz操作说明_非初始运行.md)切换到本链路，先停稳并退出板卡 mapping/navigation，再改为 hardware；然后启动电脑算法。切回板卡算法时，先停止电脑 SLAM/AMCL/Nav2，再停止板卡 hardware 并启动板卡 mapping/navigation。RViz 可保留，两端算法不能同时留着运行。

## 3．两端配置一致的 ROS 通信环境

两端都使用：

```text
ROS_DISTRO=humble
ROS_DOMAIN_ID=1
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ROS_LOCALHOST_ONLY=0
```

Docker Compose 保留 `network_mode: host`，无需设置 ROS 1 的 `ROS_MASTER_URI`。这里只用普通局域网 DDS，不配置 Discovery Server。

### 3.1 板卡：自动环境文件

如果已有从零部署说明中的自动加载配置，只检查，无需重复写入。在板卡执行：

```bash
whoami
echo "$ROS_DISTRO $ROS_DOMAIN_ID $RMW_IMPLEMENTATION $ROS_LOCALHOST_ONLY"
ros2 pkg prefix x5car_bringup
```

如果尚未配置，在板卡以 `robotwzz` 用户执行一次：

```bash
mkdir -p /home/robotwzz/.config/x5car

cat > /home/robotwzz/.config/x5car/ros_env.bash <<'EOF'
source /opt/ros/humble/setup.bash
if [ -f /home/robotwzz/dev_ws/install/setup.bash ]; then
    source /home/robotwzz/dev_ws/install/setup.bash
fi
export ROS_DOMAIN_ID=1
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_LOCALHOST_ONLY=0
unset CYCLONEDDS_URI
unset ROS_DISCOVERY_SERVER
EOF

grep -Fqx 'source /home/robotwzz/.config/x5car/ros_env.bash' \
  /home/robotwzz/.bashrc || \
  printf '\nsource /home/robotwzz/.config/x5car/ros_env.bash\n' \
  >> /home/robotwzz/.bashrc

source /home/robotwzz/.config/x5car/ros_env.bash
ros2 daemon stop
```

新开 SSH 终端应能直接运行项目命令。不要配置自动切换 root。

### 3.2 电脑容器：自动环境文件

电脑宿主机执行：

```bash
echo "$DISPLAY"
xhost +si:localuser:root
sudo docker start x5car_dev
sudo docker exec -it x5car_dev bash
```

以上命令在电脑图形桌面宿主机执行，用于已有容器。容器尚未创建时先执行从零部署说明第 8 节，不要直接 docker exec。

如果容器已有相同配置，保留即可。否则在容器内执行一次：

```bash
mkdir -p /root/.config/x5car

cat > /root/.config/x5car/ros_env.bash <<'EOF'
source /opt/ros/humble/setup.bash
if [ -f /root/ecust_ws/install/setup.bash ]; then
    source /root/ecust_ws/install/setup.bash
fi
export ROS_DOMAIN_ID=1
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_LOCALHOST_ONLY=0
unset CYCLONEDDS_URI
unset ROS_DISCOVERY_SERVER
EOF

grep -Fqx 'source /root/.config/x5car/ros_env.bash' /root/.bashrc || \
  printf '\nsource /root/.config/x5car/ros_env.bash\n' >> /root/.bashrc

source /root/.config/x5car/ros_env.bash
ros2 daemon stop
```

普通 `bash` 会加载 `.bashrc`，进入容器的命令不加 `--noprofile --norc`。首次配置需要执行一次 source；日常打开新容器终端不再需要手动加载。

如果修改了已运行节点的通信环境，需要停止并重新启动相关 ROS 节点；修改配置文件不会改变现有进程的环境。删除、重建容器会丢失其可写层里的环境文件和 RViz 默认配置，重建后要重新配置或从备份恢复。

### 3.3 时间同步与网络

电脑宿主机和板卡分别执行：

```bash
date -Ins
timedatectl status
```

两台设备应使用有效时间源同步；Docker 使用电脑宿主机时钟。电脑运行定位和规划时，跨机时间差会直接影响激光、里程计和 TF 的时效判断，不能通过任意扩大 TF tolerance 掩盖。

在使用 systemd-timesyncd 的主机上，可用 `timedatectl timesync-status` 和 `timedatectl show -p NTPSynchronized` 确认实际同步成功；使用 chrony 时检查 `chronyc tracking`。服务开启不等于同步成功，完整配置方法见从零部署说明第 7 节。大幅校时前先停稳并退出算法。

优先使用稳定有线网络。无线网络需要允许设备间通信，且具备足够稳定的吞吐和延迟；ping 通不等于 DDS 的 UDP 发现和数据传输正常。

## 4．电脑准备源码、几何配置并编译算法包

此前“电脑只运行 RViz”可以不编译项目；本链路必须在电脑 Docker 中编译并安装自定义算法包。

### 4.1 电脑宿主机：确认源码已共享到容器

```bash
ls ~/ecust_ws/src/x5car_slam
ls ~/ecust_ws/src/x5car_navigation
ls ~/ecust_ws/src/x5car_description

sudo docker inspect x5car_dev \
  --format '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}'
```

应有 `/home/zzw/ecust_ws → /root/ecust_ws` 绑定。若电脑尚无源码，新建工作空间后解压部署包；已有源码不要直接覆盖：

```bash
mkdir -p ~/ecust_ws
if [ -e ~/ecust_ws/src ]; then
    echo '电脑 src 已存在，未覆盖；请检查现有源码。'
else
    tar -xzf ~/RDKX5Car/archives/WORKSPACE_SRC.tar.gz -C ~/ecust_ws
fi
```

### 4.2 两端使用同一份实车几何

板卡使用 `robot.yaml` 生成车体和传感器 TF，电脑 Nav2 使用相同文件计算 footprint，尺寸必须一致。完成板卡实测配置后，在电脑宿主机同步该单个配置文件；如电脑已经做过修改，先备份再覆盖：

```bash
scp robotwzz@192.168.1.48:/home/robotwzz/dev_ws/src/x5car_description/config/robot.yaml \
  ~/ecust_ws/src/x5car_description/config/robot.yaml
```

电脑算法不需要打开底盘串口，因此无需把板卡的 `/dev/ttyUSB0` 映射到电脑。电机上限、串口和 watchdog 仍在板卡端生效。

### 4.3 电脑容器：检查依赖

```bash
ros2 pkg prefix slam_toolbox
ros2 pkg prefix nav2_bringup
ros2 pkg prefix rmw_cyclonedds_cpp
command -v colcon
```

验证镜像已包含大部分依赖，不必为了切换模式做全系统升级。仅在缺包时，在电脑容器内补装：

```bash
apt-get update
apt-get install -y build-essential cmake python3-yaml \
  python3-colcon-common-extensions python3-rosdep \
  ros-humble-slam-toolbox ros-humble-navigation2 \
  ros-humble-nav2-bringup ros-humble-nav2-rviz-plugins \
  ros-humble-rmw-cyclonedds-cpp ros-humble-rviz2 \
  ros-humble-xacro ros-humble-robot-state-publisher \
  ros-humble-joint-state-publisher ros-humble-joint-state-publisher-gui
```

如构建还提示声明依赖缺失，可在已初始化 rosdep 的环境中执行：

```bash
cd /root/ecust_ws
rosdep update
rosdep install \
  --from-paths src/x5car_slam src/x5car_navigation src/x5car_description \
  --ignore-src --rosdistro humble -r -y
```

未初始化 rosdep 时先执行一次 `rosdep init`。保留镜像中经过验证的 TF2/Nav2 版本，不将临时 Nav2 调试源码放入工作空间覆盖 apt 包。

### 4.4 电脑容器：编译

先停止电脑正在运行的算法节点，在容器内执行：

```bash
cd /root/ecust_ws
export CMAKE_BUILD_PARALLEL_LEVEL=1
export MAKEFLAGS=-j1

colcon build --executor sequential --symlink-install \
  --packages-up-to x5car_slam x5car_navigation \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

source /root/.config/x5car/ros_env.bash

ros2 pkg prefix x5car_slam
ros2 pkg prefix x5car_navigation
ros2 pkg prefix x5car_description
ros2 pkg prefix nav2_controller
```

`--packages-up-to` 会同时构建需要的项目依赖，包括 `x5car_description`。三个自定义包应指向 `/root/ecust_ws/install/...`，`nav2_controller` 应指向 `/opt/ros/humble`。

首次编译后的现有容器终端要重新加载一次环境，或退出后重新 `docker exec`。后续新终端会自动加载。不要把电脑构建结果复制到板卡，也不要反向复制板卡 ARM64 构建结果到电脑。

## 5．板卡只启动硬件，检查跨机数据

### 5.1 板卡终端 A

若板卡当前运行 mapping 或 navigation，先停稳、退出遥控和原 launch，等待退出。若设置了自动启动，也需要确认自启动入口是 hardware 模式。

电脑宿主机建立连接：

```bash
ssh robotwzz@192.168.1.48
```

板卡执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=hardware \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false
```

该终端持续运行。后续由建图切到导航时，板卡仍保留这条 hardware launch，无需每次重启硬件。

### 5.2 电脑容器检查

电脑另开终端进入容器：

```bash
sudo docker exec -it x5car_dev bash
```

容器内执行：

```bash
ros2 node list --no-daemon
timeout 8s ros2 topic hz /scan
timeout 8s ros2 topic hz /odom
ros2 run tf2_ros tf2_echo odom base_link
```

确认数据持续到达、变换正常，再按 `Ctrl+C` 退出 tf2_echo。此时没有地图正常。若底盘控制器没有激活、雷达无数据或 TF 不完整，先解决，再启动电脑算法。

## 6．电脑运行 SLAM 建图

### 6.1 电脑容器终端 B：独立启动 SLAM

电脑宿主机另开终端进入容器：

```bash
sudo docker exec -it x5car_dev bash
```

容器内执行：

```bash
ros2 launch x5car_slam slam.launch.py use_sim_time:=false
```

该 launch 运行 SLAM，使用板卡的 `/scan` 和真实 TF；不要启用源码中未加入 LaunchDescription 的 fake odom、fake TF 测试节点。

### 6.2 电脑容器终端 C：RViz

再进入一个容器终端，执行：

```bash
rviz2
```

如果报图形授权错误，在电脑图形桌面宿主机执行 `xhost +si:localuser:root`，然后重试。若已有容器的 DISPLAY 与桌面不一致，在宿主机改用 `sudo docker exec -e DISPLAY="$DISPLAY" -it x5car_dev bash` 进入新终端。

RViz 设置：Fixed Frame 为 `map`；Map 使用 `/map` 和 Transient Local；LaserScan 使用 `/scan` 和 Best Effort；RobotModel 的 Description Source 为 Topic，使用 `/robot_description` 和 Transient Local；开启 TF，`use_sim_time` 保持 false。设置完成后保存为 `/root/.rviz2/default.rviz`，以后直接 `rviz2` 使用默认配置。

### 6.3 遥控与保存地图

遥控可以继续在板卡执行。另开 SSH 终端后运行：

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard \
  --ros-args -r cmd_vel:=/cmd_vel_teleop
```

让车停稳，保持 SLAM 运行，在另一个**电脑容器终端**保存地图：

```bash
mkdir -p /root/ecust_ws/maps

ros2 run nav2_map_server map_saver_cli \
  -f /root/ecust_ws/maps/room

ls -lh /root/ecust_ws/maps/room.yaml /root/ecust_ws/maps/room.pgm
```

本链路中地图保存在电脑：

```text
容器：/root/ecust_ws/maps/room.yaml、room.pgm
电脑宿主机：/home/zzw/ecust_ws/maps/room.yaml、room.pgm
```

两处是同一个绑定目录。新文件不会自动出现在板卡上。重复保存需要保留已有地图时，换一个文件名。

## 7．电脑运行已有地图导航

### 7.1 准备电脑上的地图

如果刚完成第 6 节，地图已在电脑，无需复制。

如果要使用板卡上现有的 `maps/room.yaml` 和 `maps/room.pgm`，在电脑宿主机执行。目标目录有同名地图时先备份，或改用新的目录并同步修改导航路径：

```bash
mkdir -p ~/ecust_ws/maps

scp robotwzz@192.168.1.48:/home/robotwzz/dev_ws/maps/room.yaml \
  ~/ecust_ws/maps/

scp robotwzz@192.168.1.48:/home/robotwzz/dev_ws/maps/room.pgm \
  ~/ecust_ws/maps/
```

如果板卡地图实际位于 `dev_ws/map.yaml` 和 `map.pgm`，按实际文件名和位置复制，并使用相应的容器路径。

检查电脑地图 YAML 中的 `image` 字段。典型情况是 `image: room.pgm`，相对同目录即可。如果它仍指向板卡上的绝对路径，在电脑副本中修改为当前实际图像路径，再启动导航。

### 7.2 停止电脑 SLAM，保留板卡 hardware

让车停稳，退出遥控，在电脑运行 SLAM 的终端按 `Ctrl+C`，等待其退出。板卡 hardware 和电脑 RViz 保持运行。

不能让 SLAM 和 AMCL 同时发布 `map → odom`。

### 7.3 电脑容器终端 B：启动 AMCL＋Nav2

```bash
ros2 launch x5car_navigation navigation.launch.py \
  localization:=true \
  map:=/root/ecust_ws/maps/room.yaml \
  use_sim_time:=false
```

这个独立入口会在电脑启动地图服务、AMCL、Nav2 和速度平滑；不会启动板卡硬件，也不会自动打开 RViz。

板卡继续运行第 5.1 节的 `mode:=hardware`，不要改成 navigation。

### 7.4 电脑容器另一终端：检查生命周期和速度链路

```bash
ros2 lifecycle get /map_server
ros2 lifecycle get /amcl
ros2 lifecycle get /controller_server
ros2 lifecycle get /planner_server

ros2 topic info /cmd_vel_navigation -v
ros2 topic info /cmd_vel -v
```

导航生命周期应为 active。`/cmd_vel_navigation` 类型应为 `geometry_msgs/msg/Twist`，可看到电脑 `velocity_smoother` 的发布端和板卡 `twist_mux` 的订阅端。`/cmd_vel` 应连接板卡 mux 输出与麦轮控制器。

端点存在只说明已经发现发布者和订阅者，不代表正在传输有效控制数据。完成定位、在安全条件下执行目标期间，可在板卡另一终端运行 `timeout 8s ros2 topic hz /cmd_vel_navigation`，确认速度数据实际到达；没有目标或目标结束时可能没有持续速度输出，不能只凭此时无频率判断故障。

速度链路中不要另加第二个 mux，不要把速度直接改接为 TwistStamped。板卡配置应保留：

```yaml
# twist_mux.yaml 中 navigation 项的关键字段
topic: /cmd_vel_navigation
timeout: 0.2
priority: 10
```

以及控制器配置中的：

```yaml
use_stamped_vel: false
```

以上是已有 YAML 内部字段，不要用这几个字段覆盖整个配置文件。现有项目默认值已符合要求，通常不需要修改。

### 7.5 RViz 发送目标

先使用 2D Pose Estimate 给 AMCL 初始位置和朝向，确认激光与地图对齐，再用 Nav2 Goal 发送附近开阔位置的目标。

必要时在电脑容器检查：

```bash
ros2 run tf2_ros tf2_echo map odom
```

AMCL active 不等于定位成功；未移动就立即报告成功、TF 过期或数据冻结时，应先取消目标并排查。

## 8．可选：电脑边建图边运行导航

先保持板卡 hardware 和电脑 SLAM 运行，再在另一个电脑容器终端执行：

```bash
ros2 launch x5car_navigation navigation.launch.py \
  localization:=false \
  use_sim_time:=false
```

此模式由 SLAM 提供地图和 `map → odom`，Nav2 只负责导航，不启动 AMCL 和静态地图服务。不要同时运行第 7 节的 `localization:=true` 导航。

切换到静态地图导航前，先保存地图、取消目标并退出上述导航和 SLAM，再执行第 7.3 节。

## 9．参数和故障应该在哪一端修改

| 内容 | 修改位置 |
| --- | --- |
| 底盘串口、轮轴速度上限、MCU 反馈和看门狗 | 板卡 `src/x5car_bringup/config/hardware.yaml` 及硬件源码 |
| 雷达型号、串口和 `/scan` 发布配置 | 板卡 `src/x5car_bringup/config/lidar.yaml` |
| 车体几何和传感器外参 | 板卡 `src/x5car_description/config/robot.yaml`，并同步电脑几何文件 |
| SLAM 参数 | 电脑 `~/ecust_ws/src/x5car_slam/config/slam_toolbox.yaml` |
| AMCL、规划、DWB、costmap、速度平滑参数 | 电脑 `~/ecust_ws/src/x5car_navigation/config/nav2.yaml` |
| 地图文件和导航 map 路径 | 电脑 `~/ecust_ws/maps` 及对应容器路径 |
| udev 和串口访问权限 | 传感器实际连接的板卡 |

电脑上的 `nav2.yaml` 是项目覆盖参数集，必须由 `x5car_navigation navigation.launch.py` 合并系统默认配置，不宜直接作为完整配置传给其他 Nav2 入口。

修改配置或源码后，按需在相应架构重新编译并重启有关节点。参数更改前，应让机器人停止运动。

电脑 Nav2 的速度必须符合板卡真实轮轴上限。使用相同几何时，可以用 `(abs(vx) + abs(vy) + (half_wheelbase + half_track) × abs(wz)) / wheel_radius` 估算保守轮速上界，并同时核对 DWB 与 velocity_smoother 的速度、加减速限制。不要只提高板卡上限来让电脑输出通过检查。

### 可选相机点云：两端分别启用

先按从零部署说明第 12 节完成板卡 ARM64 相机依赖和编译，并确认 `/camera/depth/points` 与 optical TF 正常。停止现有硬件 launch 后，在板卡启动：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=hardware enable_lidar:=true enable_camera:=true \
  rviz:=false use_sim_time:=false
```

已有地图导航时，在电脑容器启动：

```bash
ros2 launch x5car_navigation navigation.launch.py \
  localization:=true map:=/root/ecust_ws/maps/room.yaml \
  use_depth:=true use_sim_time:=false
```

两端分别负责“发布点云”和“在代价地图使用点云”。只启用板卡相机，不会自动改变电脑独立导航入口默认的 `use_depth:=false`。点云话题应与电脑 `nav2.yaml` 中的配置一致，并评估跨机带宽；先跑通激光链路再加入相机。

跨机速度受到网络延迟、抖动和断链影响。板卡应保留 mux 超时、控制器输入超时和硬件保护；首次验证优先架空车轮或禁用电机，实际确认指令丢失时的停止行为，不依赖电脑端退出进程来保证停车。

## 10．配置完成后的日常最短命令

### 终端 A：SSH 到板卡，只运行硬件

电脑宿主机：

```bash
ssh robotwzz@192.168.1.48
```

板卡：

```bash
ros2 launch x5car_bringup robot.launch.py mode:=hardware enable_lidar:=true rviz:=false use_sim_time:=false
```

### 终端 B：电脑进入容器，运行算法

电脑图形桌面宿主机：

```bash
xhost +si:localuser:root
sudo docker start x5car_dev
sudo docker exec -it x5car_dev bash
```

容器内按任务二选一。纯建图：

```bash
ros2 launch x5car_slam slam.launch.py use_sim_time:=false
```

已有地图导航：

```bash
ros2 launch x5car_navigation navigation.launch.py localization:=true map:=/root/ecust_ws/maps/room.yaml use_sim_time:=false
```

### 终端 C：电脑 RViz

电脑宿主机：

```bash
sudo docker exec -it x5car_dev bash
```

容器内：

```bash
rviz2
```

同一容器可打开多个终端；每个新终端都使用相同自动环境。结束时先取消目标、停稳并退出遥控，再停止电脑导航/建图，最后按需停止板卡 hardware。

## 11．快速排查

| 现象 | 优先检查 |
| --- | --- |
| 电脑找不到 x5car_slam 或 x5car_navigation | 是否在 Docker 中编译、install 是否加载、是否误在电脑宿主 ROS 中启动 |
| 电脑看不到 `/scan`、`/odom` | 板卡 hardware 是否正常、DDS 环境、host 网络、防火墙、路由和无线隔离 |
| 电脑导航有路径但车不动 | `/cmd_vel_navigation` 是否有发布与订阅、板卡 mux 是否正常、遥控是否持续占用高优先级输入、底盘控制器是否 active |
| 地图加载失败 | 地图必须在电脑容器可访问路径；检查 YAML 的 image 指向及图像是否一起复制 |
| 定位或 TF 持续过期 | 检查电脑与板卡实际时钟、网络数据延迟、TF 链和唯一发布者 |
| 两套 AMCL、SLAM 或重复控制器 | 板卡只保留 hardware；电脑使用独立算法 launch；排查自启动节点 |
| 电脑 footprint 与实车不符 | 同步实测 robot.yaml，重新构建描述包和导航包并重启 |

本文已按源码中的 launch 参数、依赖和话题重映射核对。第二条链路仍需在当前电脑、板卡及实际网络上完成运动和断链停止验收，不能仅以节点启动成功判定通过。
