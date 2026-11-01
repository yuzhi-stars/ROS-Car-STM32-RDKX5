# RDKx5 ROS Car：板卡运行算法，电脑只运行 RViz（日常操作）

> 公开版镜像说明：原始 Docker 镜像校验失败，未随仓库发布。电脑端请先按[容器重建说明](../../docs/RELEASE.md)准备镜像；下文直接导入原镜像的步骤暂不可用。源码可直接使用 `../src/` 或 `archives/WORKSPACE_SRC.tar.gz`。


本文覆盖连接板卡、通信检查、建图、保存地图及已有地图导航。Docker 镜像、容器、SSH 和相关配置文件视为已准备完成，不重复修改。

全新板卡先阅读[从零部署说明](RDKx5_ROS_Car_新板卡从零部署算法与首次运行说明.md)。如果要把算法放在电脑运行，使用[电脑运行算法说明](RDKx5_ROS_Car_电脑运行算法和Rviz_板卡运行硬件部署说明.md)，不要混用两份文档的算法启动命令。

## 运行位置与已确认路径

| 项目 | 当前值 |
| --- | --- |
| 板卡 SSH | `ssh robotwzz@192.168.1.48` |
| 板卡工作空间 | `/home/robotwzz/dev_ws` |
| 本文默认地图 | `/home/robotwzz/dev_ws/maps/room.yaml`、`room.pgm` |
| 电脑容器 | `x5car_dev` |
| 电脑容器工作空间 | `/root/ecust_ws` |

板卡运行底盘、雷达、TF、SLAM 或 Nav2＋AMCL；电脑容器只运行 RViz 和通信检查命令。

> 板卡 SSH 登录后保持 `robotwzz` 用户，ROS、工作空间和通信环境已经自动加载，可以直接运行 `ros2` 命令。不要自动切换 root；需要管理员权限时只对对应命令使用 sudo。

如果实际地图位于 `/home/robotwzz/dev_ws/map.yaml` 和同目录的 `map.pgm`，导航时将 `map:=` 改为该实际路径。本文默认使用第 7 节保存的地图，保存与加载位置保持一致。

## 4．连接板卡，确认 ROS 通信环境

### 4.1 电脑宿主机：连接板卡

```bash
ping -c 4 192.168.1.48
ssh robotwzz@192.168.1.48
```

电脑与板卡应处于可互通的局域网。若地址变化，以板卡当前 IP 为准。

### 4.2 板卡：直接运行 ROS 命令

SSH 登录后环境已经自动就绪，直接执行后续的 `ros2 launch`、`ros2 run` 等命令即可。每次新开板卡 SSH 终端也无需重复加载环境。

板卡与电脑容器均已配置好 ROS 和通信环境，无需手动加载环境或重复设置变量。

### 4.3 检查两端时间

分别在电脑宿主机与板卡执行：

```bash
date -Ins
timedatectl status
```

确认时间同步正常。电脑 Docker 使用电脑宿主机的系统时钟。

## 5．启动板卡硬件并进入电脑容器

首次测试先架空车轮或关闭电机使能，并准备好物理急停。确认没有重复运行同一套硬件或导航节点。

### 5.1 板卡终端 A：启动底盘与雷达

SSH 登录板卡后直接执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=hardware \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false
```

保持该终端运行。这一步只启动硬件，不启动建图或导航。

### 5.2 电脑宿主机：进入容器

在电脑另开终端：

```bash
echo "$DISPLAY"
xhost +si:localuser:root
sudo docker start x5car_dev
sudo docker exec -it x5car_dev bash
```

以上命令在电脑图形桌面宿主机执行。进入容器后 ROS 环境已自动加载，可以直接执行后续的 RViz 命令。`docker start` 用于已有容器；如果容器尚未创建，先完成从零部署说明中的 Docker 配置。

若 RViz 报 `Authorization required`，在宿主机重新执行图形授权。若容器 DISPLAY 与宿主机不一致，退出容器后，在同一个电脑桌面终端改用以下方式重新进入：

```bash
sudo docker exec -e DISPLAY="$DISPLAY" -it x5car_dev bash
```

不需要为图形授权或 DISPLAY 问题重装 Qt。

## 6．切换建图，电脑启动 RViz

### 6.1 板卡终端 A：停止硬件模式，启动建图

在第 5.1 节的终端按 `Ctrl+C`，等待原 launch 退出，再执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=mapping \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false
```

该命令已包含底盘、雷达和 SLAM，不要另开第二套 hardware 模式，也不要同时运行 AMCL。

### 6.2 电脑容器：启动 RViz

在第 5.2 节打开的电脑容器终端直接执行：

```bash
rviz2
```

若已经运行 RViz，保留一个窗口即可。启动后按下表核对显示项，缺少的显示项可通过 Add 添加。

核对 RViz 显示项：

| 项目 | 设置 |
| --- | --- |
| Fixed Frame | `map` |
| Map | `/map`；Durability 为 `Transient Local` |
| LaserScan | `/scan`；Reliability 为 `Best Effort` |
| RobotModel | Description Source 为 Topic；Topic 为 `/robot_description`；Durability 为 `Transient Local` |
| TF | 启用 |

RViz 使用 `use_sim_time=false`。设置完成后通过 File → Save Config As 保存为 `/root/.rviz2/default.rviz`，以后直接 `rviz2` 即可加载默认配置。

### 6.3 板卡终端 B：低速遥控建图

可以沿用已有遥控方式。如果使用键盘遥控，在电脑新开一个终端，通过 SSH 连接板卡后直接执行：

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard \
  --ros-args -r cmd_vel:=/cmd_vel_teleop
```

按终端提示操作，从低速开始，观察地图逐步生成。速度指令经 `/cmd_vel_teleop → twist_mux → /cmd_vel` 到达底盘。

## 7．在板卡保存地图

保持建图 launch 运行。在另一个板卡 SSH 终端直接执行：

```bash
mkdir -p /home/robotwzz/dev_ws/maps

ros2 run nav2_map_server map_saver_cli \
  -f /home/robotwzz/dev_ws/maps/room
```

该示例将新地图保存到单独目录，保留工作空间根目录的已有地图。如果 `maps/room.*` 已存在且需要保留，保存前换一个文件名。

检查保存结果：

```bash
ls -lh /home/robotwzz/dev_ws/maps/room.yaml \
  /home/robotwzz/dev_ws/maps/room.pgm
```

第一条链路由板卡读取地图，电脑 RViz 通过 `/map` 接收地图，不需要将地图复制到电脑。

若直接使用板卡已有地图，可以跳过建图和保存步骤，完成第 5 节后直接进入第 8 节，并按第 6.2 节启动电脑 RViz。

## 8．停止建图，切到已有地图导航

### 8.1 停稳并退出原模式

先让小车停稳，退出键盘遥控，再在板卡终端 A 按 `Ctrl+C`，等待建图 launch 退出。如果当前运行的是 hardware 模式，也先停止该 launch。

不要同时运行两套底盘驱动，也不要让 SLAM 与 AMCL 同时发布 `map → odom`。

### 8.2 板卡终端 A：使用保存的地图导航

在板卡直接执行，默认使用第 7 节保存的地图：

```bash
ls -lh /home/robotwzz/dev_ws/maps/room.yaml \
  /home/robotwzz/dev_ws/maps/room.pgm

ros2 launch x5car_bringup robot.launch.py \
  mode:=navigation \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false \
  map:=/home/robotwzz/dev_ws/maps/room.yaml
```

如果实际地图在工作空间根目录，将导航参数改为 `map:=/home/robotwzz/dev_ws/map.yaml`，并确认对应的图片存在。地图 YAML 的 `image` 字段必须指向实际存在的地图图像；相对路径以 YAML 所在目录为基准。

### 8.3 板卡另一终端：检查导航状态

在另一个板卡 SSH 终端直接执行：

```bash
ros2 lifecycle get /map_server
ros2 lifecycle get /amcl
ros2 lifecycle get /controller_server
ros2 lifecycle get /planner_server
```

正常应为 `active`。电脑继续使用第 6 节的 RViz 窗口。

### 8.4 电脑 RViz：设置初始位置并发送目标

1. 使用 **2D Pose Estimate**，在地图上设置小车当前的位置与朝向。
2. 确认激光点与地图墙体、障碍物基本对齐。
3. 使用 **Nav2 Goal**，先发送附近开阔区域内的可达目标。
4. 观察路径、小车运动及到达情况，随时准备取消目标或急停。

若没有 Nav2 Goal 工具，可在 RViz 工具栏的 `+` 中添加 `nav2_rviz_plugins/GoalTool`。

AMCL 为 `active` 不代表定位已经成功。完成初始位姿设置后，可在板卡另一个终端检查：

```bash
ros2 run tf2_ros tf2_echo map odom
```

如果小车未移动却立刻提示目标成功，或持续出现 TF 过期错误，先取消目标并排查，不视为导航通过。

结束测试时先取消导航目标，确认小车停止，再依次退出导航及硬件节点，最后按需关闭板卡。

如果从“电脑运行算法”切回本链路，先停稳并停止电脑的 SLAM/AMCL/Nav2，再停止板卡 hardware launch，最后在板卡启动 mapping 或 navigation。RViz 可以保留，不要保留另一条链路的算法节点。
