# RDKx5 ROS Car：新板卡从零部署与首次运行说明

> 公开版镜像说明：原始 Docker 镜像校验失败，未随仓库发布。电脑端请先按[容器重建说明](../../docs/RELEASE.md)准备镜像；下文直接导入原镜像的步骤暂不可用。源码可直接使用 `../src/` 或 `archives/WORKSPACE_SRC.tar.gz`。


适用情况：一块全新的 RDKx5，只有 Ubuntu 22.04 和 ROS 2 Humble，没有项目源码、项目依赖、串口规则和工作空间编译结果。本文从登录板卡开始，完成配置文件写入、源码拷贝、依赖安装、编译和首次运行。目标是板卡运行底盘、雷达、建图和导航，电脑 Docker 只运行 RViz。

本文把一次性部署和日常启动分开。部署完成后，板卡 SSH 登录和电脑进入容器时会自动加载 ROS 环境；日常启动无需再手动 `source`，电脑启动可视化只输入 `rviz2`。

本文采用“板卡运行算法”链路。部署后可使用[日常操作说明](RDKx5_ROS_Car_板卡运行算法和Rviz_电脑端RViz操作说明_非初始运行.md)；如果算法改在电脑运行，完成板卡第 1～7 节和电脑第 8 节后，转到[电脑运行算法说明](RDKx5_ROS_Car_电脑运行算法和Rviz_板卡运行硬件部署说明.md)，不要继续启动本文的板卡建图/导航节点。

命令按执行位置分块运行。安装、传输或编译出现错误时先解决该错误，再继续下一步，不要直接跳过失败步骤启动机器人。

## 0．先认清执行位置和文件来源

| 执行位置 | 提示符示例 | 用途 |
| --- | --- | --- |
| 电脑宿主机 | `zzw@robot:~$` | 拷贝文件、SSH、Docker 和图形授权 |
| 板卡 | `robotwzz@ubuntu:~$` | 安装 ARM64 依赖、编译和运行算法 |
| 电脑 Docker | `root@x5car-dev:~#` | 配置容器环境、运行 RViz |

本文使用以下路径：

| 项目 | 路径或设置 |
| --- | --- |
| 电脑部署包根目录 | `/home/zzw/RDKX5Car`，即电脑上的 `~/RDKX5Car` |
| 电脑共享工作空间 | `/home/zzw/ecust_ws` |
| 电脑容器工作空间 | `/root/ecust_ws` |
| 板卡工作空间 | `/home/robotwzz/dev_ws` |
| 板卡接收部署文件的目录 | `/home/robotwzz/x5car_setup` |
| 板卡用户名 | `robotwzz`，普通用户，有 sudo 权限 |
| 板卡 IP 示例 | `192.168.1.48`，新板卡必须先查实际地址 |
| 通信设置 | Humble、Cyclone DDS、`ROS_DOMAIN_ID=1`、`ROS_LOCALHOST_ONLY=0` |

如果用户名或电脑部署包位置不同，统一替换对应路径。不要用 root 登录板卡开展日常操作，也不要设置 SSH 登录后自动 `sudo -i`。下文的板卡操作都以 `robotwzz` 用户执行，需要管理员权限时单独加 `sudo`。

### 项目部署包提供的文件

源码和镜像位于 `archives/`，Docker 配置、脚本和设备规则分别位于根目录的 `docker/`、`scripts/`、`udev/`。下文命令已经按整理后的目录填写。

- `archives/WORKSPACE_SRC.tar.gz`：项目 `src/`，需要在板卡重新编译。
- `scripts/`、`udev/`：依赖安装和设备规则模板。
- `archives/X5CAR_DOCKER_IMAGE.tar.gz`：电脑的 x86_64 镜像，只给电脑使用。
- `environment/DPKG_ALL.txt`：原电脑容器的软件版本参考，不是板卡 ARM64 安装脚本。

**不要向板卡复制电脑的 Docker 镜像、`build/`、`install/` 或 x86_64 动态库。**

### 全新部署时需要按实物填写的内容

- 板卡当前 IP 地址，以及底盘 MCU、雷达对应的实际串口。
- `hardware.yaml` 中的轮轴角速度上限；文件里的 `100` 不能直接当作已确认的实车安全值。
- 车轮半径、轴距、轮距、车体尺寸，以及雷达、IMU 等传感器安装位置。
- 启用相机时，确认 SDK 库为 ARM64，并在板卡实际验收图像和点云。

这些内容在后续步骤逐项填写。初次启用硬件还需检查串口反馈、控制器状态和看门狗，不要通过关闭保护或任意放大参数绕过错误。

底盘 MCU 应具有与项目 `x5car_protocol.hpp` 匹配的固件，并能反馈轮速和所需 IMU 数据。板卡上的 ROS 软件依赖 MCU 协议正确，底盘串口有设备节点并不代表协议已经匹配。

## 1．新板卡第一次开机：确认用户、系统和网络

### 1.1 板卡本地终端：检查基础环境

第一次需要通过板卡本地显示器、串口终端或已经可用的远程连接操作：

```bash
whoami
lsb_release -ds
uname -m
ls /opt/ros
hostname -I
```

预期系统为 Ubuntu 22.04，架构为 `aarch64`，`/opt/ros` 中有 `humble`。

如果尚无 `robotwzz` 用户，由当前有 sudo 权限的管理员创建一次；已有该用户则跳过：

```bash
sudo adduser robotwzz
sudo usermod -aG sudo robotwzz
```

之后以 `robotwzz` 登录。新建用户时设置的密码就是后续 SSH 和 sudo 使用的用户密码。

### 1.2 板卡：开启 SSH

```bash
sudo apt update
sudo apt install -y openssh-server
sudo systemctl enable --now ssh
systemctl is-active ssh
hostname -I
```

应看到 SSH 为 `active`。记录板卡连接电脑所在局域网的实际 IPv4 地址。`192.168.1.48` 仅作为本文命令示例；如果实际地址不同，后续命令全部替换为实际地址。

板卡和电脑接入同一个可互通的局域网。本文不强制写静态 IP；如需要固定地址，可以在路由器为板卡设置 DHCP 地址保留。

### 1.3 电脑宿主机：建立 SSH 连接

以下地址按上一步实际值修改：

```bash
ping -c 4 192.168.1.48
ssh robotwzz@192.168.1.48
```

首次连接确认主机指纹后，输入 `robotwzz` 用户密码。登录后的提示符应为 `robotwzz@ubuntu`，不是 root。

每个需要操作板卡的新电脑终端，都要执行一次 SSH；同一 SSH 终端内执行多条命令无需重复连接。

## 2．电脑把项目源码、脚本和规则拷到新板卡

### 2.1 电脑宿主机：确认部署包

```bash
cd ~/RDKX5Car/archives
ls -lh WORKSPACE_SRC.tar.gz X5CAR_DOCKER_IMAGE.tar.gz
sha256sum -c SHA256SUMS
```

两个归档都应显示 `OK`。后续只将小体积的源码归档及辅助文件复制到板卡，不复制电脑镜像。

### 2.2 电脑宿主机：传输文件

```bash
ssh robotwzz@192.168.1.48 'mkdir -p /home/robotwzz/x5car_setup'

scp ~/RDKX5Car/archives/WORKSPACE_SRC.tar.gz \
  robotwzz@192.168.1.48:/home/robotwzz/x5car_setup/

scp -r ~/RDKX5Car/scripts ~/RDKX5Car/udev \
  robotwzz@192.168.1.48:/home/robotwzz/x5car_setup/
```

可以在电脑和板卡分别运行 `sha256sum`，比较传输后的源码是否一致：

```bash
# 电脑宿主机
sha256sum ~/RDKX5Car/archives/WORKSPACE_SRC.tar.gz
```

```bash
# 板卡
sha256sum /home/robotwzz/x5car_setup/WORKSPACE_SRC.tar.gz
```

### 2.3 板卡：创建工作空间并解压 src

仅对新工作空间执行。如果 `/home/robotwzz/dev_ws/src` 已存在，先检查和备份，不要直接覆盖。

```bash
mkdir -p /home/robotwzz/dev_ws
tar -tzf /home/robotwzz/x5car_setup/WORKSPACE_SRC.tar.gz | head -30
```

确认归档内容以 `src/` 开头，且当前没有需要保留的 `dev_ws/src`，再执行：

```bash
if [ -e /home/robotwzz/dev_ws/src ]; then
    echo 'src 已存在，未覆盖；请先检查或备份。'
else
    tar -xzf /home/robotwzz/x5car_setup/WORKSPACE_SRC.tar.gz \
      -C /home/robotwzz/dev_ws
fi

ls /home/robotwzz/dev_ws/src
```

应能看到 `x5car_bringup`、`x5car_hardware`、`x5car_description`、`x5car_slam`、`x5car_navigation` 和雷达驱动等目录。

## 3．板卡安装项目依赖

### 3.1 板卡：安装基础工具及部署脚本中的依赖

```bash
sudo apt update
sudo apt install -y nano rsync usbutils v4l-utils file \
  build-essential cmake git pkg-config python3-yaml \
  python3-colcon-common-extensions python3-rosdep

bash /home/robotwzz/x5car_setup/scripts/install_project_deps.sh \
  /home/robotwzz/dev_ws
```

该脚本会安装控制框架、麦轮控制器、SLAM Toolbox、Nav2、Cyclone DDS、图像处理等依赖，并通过 rosdep 补齐 `package.xml` 声明的依赖。即使板卡安装了 RViz 软件包，后面也不在板卡运行 RViz。

如果 apt 报某个包不存在，检查板卡 ROS Humble 软件源和网络。不要跨发行版安装 Jazzy 包，也不要直接用电脑的 x86_64 `.deb` 替代 ARM64 包。

### 3.2 板卡：检查关键依赖

首次部署时尚未配置自动环境加载，先临时加载一次系统 ROS：

```bash
source /opt/ros/humble/setup.bash

ros2 pkg prefix controller_manager
ros2 pkg prefix mecanum_drive_controller
ros2 pkg prefix slam_toolbox
ros2 pkg prefix nav2_bringup
ros2 pkg prefix rmw_cyclonedds_cpp
```

每条都应输出路径。记录板卡的实际软件版本：

```bash
dpkg-query -W 'ros-humble-tf2*' 'ros-humble-nav2-*' \
  > /home/robotwzz/x5car_setup/RDK_ROS_VERSIONS.txt
```

板卡软件以 ARM64 软件源提供的 Humble 兼容版本为准。保存本次实际安装版本，便于定位后续的 API、ABI 或参数兼容问题。

## 4．板卡配置串口权限与硬件参数

底盘 MCU、雷达都连接到板卡时，下面的规则和参数配置在板卡执行，不是在电脑 Docker 内执行。首次检查先断开电机使能或架空车轮。

### 4.1 查清底盘 MCU 与雷达分别是哪个设备

```bash
lsusb
ls -l /dev/serial/by-id/ 2>/dev/null
ls -l /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* 2>/dev/null
```

逐个识别设备，必要时在无运动状态下分别插拔确认。不要把底盘 MCU 的串口填成雷达串口。

底盘串口可以是 `/dev/ttyUSB0`，雷达规则模板使用 USB ID `1a86:55d4`。这两个值都必须与当前设备实际信息核对。

例如，实际雷达设备为 `/dev/ttyACM0` 时：

```bash
udevadm info --query=property --name=/dev/ttyACM0
```

其他设备名应替换成实际值，重点查看 `ID_VENDOR_ID`、`ID_MODEL_ID`、`ID_SERIAL_SHORT`。

### 4.2 写入雷达 udev 规则

先查看部署模板：

```bash
cat /home/robotwzz/x5car_setup/udev/99-x5car-n10.rules
```

如果实测雷达 USB ID 确实是 `1a86:55d4`，可以直接安装：

```bash
sudo install -m 0644 \
  /home/robotwzz/x5car_setup/udev/99-x5car-n10.rules \
  /etc/udev/rules.d/99-x5car-n10.rules
```

若 ID 不同，改用下面命令编辑规则，按实际值填写：

```bash
sudo nano /etc/udev/rules.d/99-x5car-n10.rules
```

规则结构如下；这里给出已知设备的示例，不要对不同型号原样照抄：

```udev
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d4", MODE="0660", GROUP="dialout", SYMLINK+="wheeltec_lidar"
```

多个设备具有相同 VID/PID 时，需要加入实际序列号等条件区分，避免链接指错设备。

赋予用户串口组权限并重载规则：

```bash
sudo usermod -aG dialout robotwzz
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty
```

在无设备传输和无运动状态下重新插拔雷达。退出 SSH 后重新登录，使新增用户组生效：

```bash
exit
```

在电脑宿主机重新连接：

```bash
ssh robotwzz@192.168.1.48
```

回到板卡检查：

```bash
id
ls -l /dev/wheeltec_lidar
readlink -f /dev/wheeltec_lidar
```

用户组应包含 `dialout`，链接应指向实际雷达设备。

### 4.3 编辑底盘 hardware.yaml

```bash
nano /home/robotwzz/dev_ws/src/x5car_bringup/config/hardware.yaml
```

需要确认的主要项：

| 参数 | 填写依据 |
| --- | --- |
| `serial_port` | 实际底盘 MCU 串口，优先使用存在且唯一的 `/dev/serial/by-id/...` 稳定路径；否则使用已核对的设备名 |
| `max_wheel_speed_rad_s` | 已验证的轮轴角速度上限，单位 rad/s；不是车速 m/s，不是随意填的电机转速 |
| `require_imu` | 与当前 MCU 协议和实际反馈一致，不能用关闭检查掩盖异常 |

文件中的 `max_wheel_speed_rad_s: 100` 不能视为已确认安全值。应依据当前实车的电机、减速比和 MCU 限制确定。若轮轴转速已知为 n rpm，换算为 `n × 2π / 60` rad/s；还需核对该值是否允许当前负载下持续运行。

其他超时和停车阈值先保留项目文件中的值，不为绕过错误任意修改。Nano 中按 `Ctrl+O`、回车保存，`Ctrl+X` 退出。

### 4.4 编辑雷达与几何参数

```bash
nano /home/robotwzz/dev_ws/src/x5car_bringup/config/lidar.yaml
```

该 YAML 的节点层级应保留为 `x10 → lslidar_driver_node → ros__parameters`。核对：

```yaml
serial_port: "/dev/wheeltec_lidar"
lidar_model: "N10"
frame_id: "laser"
laserscan_topic: "/scan"
```

以上是文件层级内部的关键字段，不是可单独替换整个文件的 YAML。雷达型号需与实物一致；本项目串口 N10 使用 230400 波特率，应核对设备与驱动实际实现，不要随意添加未经驱动支持的参数名。

然后检查车体尺寸和传感器外参：

```bash
nano /home/robotwzz/dev_ws/src/x5car_description/config/robot.yaml
```

部署包中的几何值为轮半径 `0.040 m`、半轴距 `0.0835 m`、半轮距 `0.094 m`。这些值及其他车体尺寸、雷达、IMU、相机安装外参都应与当前实物核对，不一致时按实测值修改。不要把半径写成直径，也不要把半轴距写成完整轴距。

`controllers.yaml` 中保留 `use_stamped_vel: false`，项目速度链路使用 `geometry_msgs/msg/Twist`。

导航速度还需要与真实轮轴上限匹配。在 `src/x5car_navigation/config/nav2.yaml` 中核对 DWB 与 velocity_smoother 的线速度、角速度及加减速限制。麦轮的保守轮速上界可按 `(abs(vx) + abs(vy) + (half_wheelbase + half_track) × abs(wz)) / wheel_radius` 估算，不能超过配置的轮轴上限。这个估算用于检查参数一致性，实际还需结合当前负载验证。

## 5．板卡首次编译项目

### 5.1 先编译底盘、雷达、建图和导航

第一次先不接入相机，避免相机 SDK 阻塞激光建图和导航。当前源码相机默认关闭，可以在本次构建中跳过两个相机包，其余包正常编译：

```bash
cd /home/robotwzz/dev_ws
source /opt/ros/humble/setup.bash

colcon list --names-only

export CMAKE_BUILD_PARALLEL_LEVEL=1
export MAKEFLAGS=-j1

colcon build --executor sequential --symlink-install \
  --packages-skip astra_camera astra_camera_msgs \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

新板卡应使用新的构建目录。不要把电脑的 `build/`、`install/` 和 `log/` 复制进来，也不要把临时调试用的 Nav2 源码放进正式工作空间覆盖 apt 安装版本。

构建完成后应没有 failed 或 aborted 包。再检查：

```bash
source /home/robotwzz/dev_ws/install/setup.bash

ros2 pkg prefix x5car_bringup
ros2 pkg prefix x5car_hardware
ros2 pkg prefix x5car_navigation
ros2 pkg prefix lslidar_driver
ros2 pkg prefix nav2_controller
```

自研包和雷达驱动应指向 `/home/robotwzz/dev_ws/install/...`；`nav2_controller` 应指向 `/opt/ros/humble`。

### 5.2 修改配置后如何生效

使用 `--symlink-install` 后，部分资源文件会链接回源码，但不要假设每个文件都会自动更新。修改 C++ 必须重新编译；修改 YAML、launch 或 URDF 后，若不确定安装目录是否已同步，重新执行上述构建，再重启对应 launch。

## 6．板卡写入自动 ROS 环境配置

这一步是新板卡首次部署时执行一次。以后 SSH 登录 `robotwzz` 就可以直接运行项目 ROS 命令。

### 6.1 创建环境文件

在板卡以 `robotwzz` 用户执行：

```bash
mkdir -p /home/robotwzz/.config/x5car

cat > /home/robotwzz/.config/x5car/ros_env.bash <<'EOF'
# RDKx5 ROS 2 Humble environment
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
```

### 6.2 在用户 .bashrc 末尾加载

```bash
cp -a /home/robotwzz/.bashrc \
  "/home/robotwzz/.bashrc.backup.$(date +%Y%m%d_%H%M%S)"

grep -Fqx 'source /home/robotwzz/.config/x5car/ros_env.bash' \
  /home/robotwzz/.bashrc || \
  printf '\nsource /home/robotwzz/.config/x5car/ros_env.bash\n' \
  >> /home/robotwzz/.bashrc
```

保留 Ubuntu 用户默认的 `.profile` 对 `.bashrc` 的加载，确保交互式 Bash 登录时会读取 `.bashrc`。不添加自动 `sudo -i` 命令，SSH 登录后保持普通用户身份。

让当前终端立即生效一次：

```bash
source /home/robotwzz/.config/x5car/ros_env.bash
ros2 daemon stop
```

### 6.3 用新 SSH 终端验收自动加载

电脑另开终端连接：

```bash
ssh robotwzz@192.168.1.48
```

在新板卡终端直接执行，不手动 `source`：

```bash
whoami
echo "$ROS_DISTRO $ROS_DOMAIN_ID $RMW_IMPLEMENTATION $ROS_LOCALHOST_ONLY"
ros2 pkg prefix x5car_bringup
```

预期用户名为 `robotwzz`，环境值为 `humble 1 rmw_cyclonedds_cpp 0`，项目路径指向板卡工作空间。

## 7．两端时间和网络检查

在电脑宿主机和板卡分别执行：

```bash
date -Ins
timedatectl status
```

有可用校时服务和时间源时，确认它处于同步状态。若系统使用 systemd-timesyncd，可以执行 `sudo timedatectl set-ntp true`；若使用 chrony，应通过现有 chrony 配置同步，不要叠加启用多个校时服务。

使用 systemd-timesyncd 时，在每台需要校时的主机执行：

```bash
sudo timedatectl set-ntp true
timedatectl timesync-status
timedatectl show -p NTPSynchronized
```

应看到 `NTPSynchronized=yes`。若使用 chrony，用 `chronyc tracking` 检查同步状态。若主机没有校时服务，在确认未运行 chrony 等其他服务后，可安装并启用：

```bash
sudo apt install -y systemd-timesyncd
sudo systemctl enable --now systemd-timesyncd
sudo timedatectl set-ntp true
```

随后重新检查同步结果。上述公网校时需要能访问有效的 NTP 时间源；隔离局域网应配置可达的局域网时间源。大幅调整时钟应在机器人停稳、算法退出后进行。

电脑与板卡时间应一致，DDS 需要跨机 UDP 和发现通信。Wi-Fi 客户端隔离或防火墙会造成 ping 通但 ROS 话题不通；排查时检查实际网络规则，不要只靠 ping 判断成功。

## 8．电脑首次配置 Docker 和 RViz 环境

本章假设电脑已经安装 Docker 和 Compose。首次部署完成第 8.4 节后，继续第 9 节检查硬件；第 8.5 节是日常启动参考。已有容器、源码和自动加载配置都正常时，可直接使用第 8.5 节。这里不会在电脑运行底盘、SLAM、AMCL 或 Nav2 节点。

### 8.1 电脑宿主机：导入镜像，解压电脑源码

```bash
cd ~/RDKX5Car/archives
sudo docker load -i X5CAR_DOCKER_IMAGE.tar.gz
sudo docker image ls
```

预期存在镜像 `x5car-humble:verified`。

```bash
mkdir -p ~/ecust_ws
```

仅当 `~/ecust_ws/src` 不存在时执行以下解压；已有源码则保留并核对，避免覆盖：

```bash
if [ -e ~/ecust_ws/src ]; then
    echo '电脑 src 已存在，未覆盖；请检查现有源码。'
else
    tar -xzf ~/RDKX5Car/archives/WORKSPACE_SRC.tar.gz -C ~/ecust_ws
fi
```

电脑运行 RViz 不要求先编译整个项目。若使用的 RobotModel 依赖本地 `package://` 网格资源，则需另外安装或构建对应描述包；当前项目的可视化应以实际模型资源为准。

### 8.2 电脑宿主机：写入 Compose 的 .env

```bash
cd ~/RDKX5Car/docker
printf 'HOST_WS=%s\n' "$HOME/ecust_ws" > .env
cat .env
```

当前电脑应显示：

```dotenv
HOST_WS=/home/zzw/ecust_ws
```

使用部署包的 `compose.yaml`，它已声明 host 网络、host IPC、X11、工作空间以及设备目录绑定。保持电脑与板卡的三项 ROS 通信变量一致。该模板带全设备和特权访问，用于当前可信开发电脑。

### 8.3 电脑图形桌面终端：授权并启动容器

如果 `xhost` 命令不存在，在电脑宿主机安装 `x11-xserver-utils`。图形命令在电脑已登录的桌面终端执行，不在板卡 SSH 会话中执行。

```bash
echo "$DISPLAY"
xhost +si:localuser:root

cd ~/RDKX5Car/docker
sudo env DISPLAY="$DISPLAY" docker compose up -d --pull never
sudo docker ps
```

保留实际 `DISPLAY`，不要固定照搬 `:0` 或 `:1`。如果同名 `x5car_dev` 已由其他方式创建，先核对原容器再处理名称冲突，不要盲目删除。

### 8.4 电脑容器：首次写入自动环境配置

在电脑宿主机执行：

```bash
sudo docker exec -it x5car_dev bash
```

然后在**容器内**执行一次：

```bash
mkdir -p /root/.config/x5car

cat > /root/.config/x5car/ros_env.bash <<'EOF'
# RDKx5 desktop ROS environment
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

cp -a /root/.bashrc "/root/.bashrc.backup.$(date +%Y%m%d_%H%M%S)"

grep -Fqx 'source /root/.config/x5car/ros_env.bash' /root/.bashrc || \
  printf '\nsource /root/.config/x5car/ros_env.bash\n' >> /root/.bashrc

source /root/.config/x5car/ros_env.bash
ros2 daemon stop
command -v rviz2
```

最后应输出 RViz 可执行文件路径。如果打开容器时提示某个 setup 文件不存在，检查 `/root/.bashrc`，删除无效路径对应的加载行，保留上面新加的环境加载行。

再打开一个新的容器终端，直接运行 `command -v rviz2` 和 `echo "$ROS_DOMAIN_ID $RMW_IMPLEMENTATION $ROS_LOCALHOST_ONLY"`，验证自动加载确实生效，再进入后续运行步骤。

环境文件写在容器可写层中，普通停止和启动容器会保留；删除、重建容器后需要重新执行本节，或使用另行维护的镜像/配置挂载保存它。

### 8.5 电脑日常启动 RViz

电脑图形桌面终端执行；重启或重新登录桌面后可能需要重新授权：

```bash
xhost +si:localuser:root
sudo docker start x5car_dev
sudo docker exec -it x5car_dev bash
```

进入容器后直接执行：

```bash
rviz2
```

不加 `--noprofile --norc`，不加 RViz 启动参数。第一次打开时按第 10 节添加显示项，完成后可通过 RViz 的保存配置功能保存为默认配置，供后续直接启动使用。

如果电脑重新登录后 DISPLAY 改变，已有容器的环境不会自动跟随。先在电脑桌面终端执行 `echo "$DISPLAY"`；如果容器显示变量不同，可在宿主机改用以下命令进入容器，不必为了这个问题重建容器：

```bash
sudo docker exec -e DISPLAY="$DISPLAY" -it x5car_dev bash
```

## 9．第一次启动板卡硬件，检查真实数据

先确认串口、真实轮速上限和几何参数已经按第 4 节配置。首次启用底盘应架空车轮或关闭电机使能，准备好急停；软件不能代替 MCU 独立超时停车和物理急停。

### 9.1 板卡终端 A：启动硬件与雷达

在电脑宿主机建立 SSH：

```bash
ssh robotwzz@192.168.1.48
```

进入板卡后直接执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=hardware \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false
```

### 9.2 板卡终端 B：检查控制器和雷达

电脑再开一个终端，重新 SSH 到板卡后执行：

```bash
ros2 control list_hardware_components
ros2 control list_controllers
timeout 8s ros2 topic hz /scan
timeout 8s ros2 topic hz /odom
ros2 run tf2_ros tf2_echo odom base_link
```

硬件组件及 `joint_state_broadcaster`、`imu_sensor_broadcaster`、`mecanum_controller` 应处于 active。`/scan` 约 10 Hz、`/odom` 约 50 Hz 可作为参考，实际以设备和控制器配置为准。最后一条按 `Ctrl+C` 结束，`timeout` 在 8 秒后结束属于预期行为。

如果启动立即报 `control loop interrupted ... >=100ms`，检查控制循环是否及时运行、系统负载及看门狗启动时机。当前归档源码在 `on_activate()` 中启动计时；如果激活到首次 `read()` 的间隔达到超时阈值，需处理初始化计时问题并重新编译，不要直接关闭保护。如果串口反馈、IMU 或控制器激活失败，应先修复，暂不进入移动测试。

### 9.3 电脑容器：验证跨机数据

另开一个电脑终端进入容器：

```bash
sudo docker exec -it x5car_dev bash
```

容器内直接执行：

```bash
ros2 node list --no-daemon
timeout 8s ros2 topic hz /scan
timeout 8s ros2 topic hz /odom
ros2 run tf2_ros tf2_echo odom base_link
```

确认实际能收到数据。`tf2_echo` 持续运行，查看后按 `Ctrl+C` 结束，再输入下一条命令；`timeout` 在 8 秒后结束属于预期行为。硬件模式没有 `/map` 是正常的。

## 10．第一次建图与电脑 RViz 设置

### 10.1 板卡终端 A：由硬件模式切到建图

在运行硬件的终端按 `Ctrl+C`，等原 launch 完全退出，再执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=mapping \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false
```

mapping 已包含底盘、雷达和 SLAM，不要重复启动 hardware，也不要同时启动 AMCL。

### 10.2 电脑容器：运行 RViz

在电脑容器的新终端或已结束数据检查的终端执行。如果 RViz 已经打开，复用现有窗口，不重复启动。

```bash
rviz2
```

第一次启动后，在 RViz 的 Displays 中设置 Global Options，并通过 Add 添加以下显示项：

| 显示项 | 设置 |
| --- | --- |
| Global Options | Fixed Frame 为 `map` |
| Map | Topic 为 `/map`；Durability 为 `Transient Local` |
| LaserScan | Topic 为 `/scan`；Reliability 为 `Best Effort` |
| RobotModel | Description Source 为 Topic；Topic 为 `/robot_description`；Durability 为 `Transient Local` |
| TF | Enabled |

真实机器人使用墙钟，RViz 的 `use_sim_time` 保持 false。检查地图、激光和模型是否正常，再通过 File → Save Config As 保存到 `/root/.rviz2/default.rviz`，后续 `rviz2` 可直接恢复默认显示设置。该文件位于容器可写层，删除、重建容器前应备份；普通停止和启动不会删除它。

### 10.3 板卡终端 B：键盘遥控建图

另开 SSH 连接，在板卡执行：

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard \
  --ros-args -r cmd_vel:=/cmd_vel_teleop
```

按提示从低速操作，逐步覆盖环境。手动速度经 `/cmd_vel_teleop → twist_mux → /cmd_vel` 到达底盘，不直接绕过 mux。

### 10.4 板卡另一终端：保存地图

让车停稳，保持建图 launch 运行，在另一 SSH 终端执行：

```bash
mkdir -p /home/robotwzz/dev_ws/maps

ros2 run nav2_map_server map_saver_cli \
  -f /home/robotwzz/dev_ws/maps/room

ls -lh /home/robotwzz/dev_ws/maps/room.yaml \
  /home/robotwzz/dev_ws/maps/room.pgm
```

日志出现 `Map saved successfully` 表示保存成功。以后重复保存需要保留已有地图时，更换文件名。该流程使用本次建图生成的 `maps/room.yaml`。

## 11．从建图切换到地图导航

先停稳、退出遥控，再在板卡终端 A 按 `Ctrl+C` 退出建图。等待完全退出后，在板卡执行：

```bash
ros2 launch x5car_bringup robot.launch.py \
  mode:=navigation \
  enable_lidar:=true \
  rviz:=false \
  use_sim_time:=false \
  map:=/home/robotwzz/dev_ws/maps/room.yaml
```

导航使用上一章在新板卡生成的地图。地图 YAML 的 `image` 字段必须能找到实际图像；如果自行更换保存目录或文件名，导航命令中的 `map` 路径也要相应修改。

在板卡另一终端检查：

```bash
ros2 lifecycle get /map_server
ros2 lifecycle get /amcl
ros2 lifecycle get /controller_server
ros2 lifecycle get /planner_server
```

应为 active。电脑继续运行同一个 RViz 窗口：

1. 添加或选中 `2D Pose Estimate`，在地图上给出小车的大致位置和车头方向。
2. 确认雷达扫描与地图中的墙体、障碍物对齐。
3. 工具栏通过 `+` 添加 `nav2_rviz_plugins/GoalTool`（若尚未添加），使用 Nav2 Goal 发送附近开阔区域的目标。
4. 观察路径和实际运动，先通过短距离低速测试，再扩大范围。

AMCL active 不等于定位成功。需要检查变换时，在板卡执行：

```bash
ros2 run tf2_ros tf2_echo map odom
```

如果没移动却瞬间提示成功、TF 持续过期，或局部代价地图不随车更新，先取消目标并排查。首次导航建议连续观察 10～20 分钟，确认 TF 和代价地图轮廓时间戳持续更新。

## 12．可选：首次接入 Astra Pro 相机

只使用雷达建图和导航时可以跳过本章。前面的构建跳过了相机包，启用相机前必须先完成本章并重新编译。

### 12.1 板卡：核对 ARM64 SDK

```bash
file /home/robotwzz/dev_ws/src/drivers/ros2_astra_camera/astra_camera/openni2_redist/arm64/libOpenNI2.so

file /home/robotwzz/dev_ws/src/drivers/ros2_astra_camera/astra_camera/openni2_redist/arm64/OpenNI2/Drivers/*.so
```

应为 AArch64/ARM64 库。该源码的 CMake 在 `aarch64` 上选择 `openni2_redist/arm64`。不要用同目录下的 x64 库替代。

### 12.2 板卡：安装相机补充依赖及 libuvc

```bash
sudo apt install -y libeigen3-dev libgflags-dev libgoogle-glog-dev \
  libusb-1.0-0-dev libjpeg-dev \
  ros-humble-image-publisher ros-humble-image-pipeline \
  ros-humble-tf2-eigen ros-humble-tf2-sensor-msgs
```

先检查：

```bash
pkg-config --modversion libuvc
```

若系统已安装可用的 libuvc，先确认其版本。若不存在，在板卡本机从源码编译：

```bash
mkdir -p /home/robotwzz/vendor_src
git clone https://github.com/libuvc/libuvc.git \
  /home/robotwzz/vendor_src/libuvc

git -C /home/robotwzz/vendor_src/libuvc rev-parse HEAD \
  > /home/robotwzz/x5car_setup/LIBUVC_COMMIT.txt

cmake -S /home/robotwzz/vendor_src/libuvc \
  -B /home/robotwzz/vendor_src/libuvc/build \
  -DCMAKE_BUILD_TYPE=Release

cmake --build /home/robotwzz/vendor_src/libuvc/build --parallel 2
sudo cmake --install /home/robotwzz/vendor_src/libuvc/build
sudo ldconfig
pkg-config --modversion libuvc
```

上述首次下载取得当时仓库版本。保存的 commit SHA 用于准确记录本次构建来源；相机编译和运行验收通过后，以该 SHA 固定部署版本。网络不可达时可离线传输对应源码，不要直接拷贝电脑 x86_64 库。

### 12.3 板卡：安装相机规则并编译

```bash
sudo bash /home/robotwzz/dev_ws/src/drivers/ros2_astra_camera/astra_camera/scripts/install.sh
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb
```

在无运动和无数据传输状态下重新插拔相机。随后重新连接一个板卡 SSH 终端，停止已有项目节点后编译：

```bash
cd /home/robotwzz/dev_ws
export CMAKE_BUILD_PARALLEL_LEVEL=1
export MAKEFLAGS=-j1

colcon build --executor sequential --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

编译完成后重新 SSH，使自动环境加载覆盖新构建结果，单独测试相机：

```bash
ros2 launch astra_camera astra_pro.launch.xml
```

另一板卡终端检查实际图像和点云话题。测试后停止独立相机 launch；以后使用整体 `robot.launch.py` 的 `enable_camera:=true` 参数启用，避免重复打开设备。相机 RGB、深度和点云需要分别验收，不能仅以编译成功判断通过。

## 13．部署完成后的日常最短操作

### 电脑终端 A：连接板卡并启动算法

```bash
ssh robotwzz@192.168.1.48
```

登录后，按任务二选一。

建图：

```bash
ros2 launch x5car_bringup robot.launch.py mode:=mapping enable_lidar:=true rviz:=false use_sim_time:=false
```

已有地图导航：

```bash
ros2 launch x5car_bringup robot.launch.py mode:=navigation enable_lidar:=true rviz:=false use_sim_time:=false map:=/home/robotwzz/dev_ws/maps/room.yaml
```

### 电脑终端 B：打开 RViz

在电脑图形桌面终端执行：

```bash
xhost +si:localuser:root
sudo docker start x5car_dev
sudo docker exec -it x5car_dev bash
```

进入容器后：

```bash
rviz2
```

需要键盘遥控或保存地图时，另开电脑终端，SSH 到板卡后执行对应命令。日常操作不需要再手动 source，也不需要登录 root。

## 14．常见问题和最终备份

| 现象 | 检查方式 |
| --- | --- |
| `Package 'x5car_bringup' not found`，只搜索 `/opt/ros/humble` | 确认当前是 `robotwzz`，检查工作空间是否编译成功、用户 `.bashrc` 是否加载环境文件 |
| 临时修复工作空间环境 | 板卡执行 `source /home/robotwzz/.config/x5car/ros_env.bash`，然后用新 SSH 验收永久配置 |
| 容器内 `rviz2: command not found` | 核对 `/root/.bashrc` 和 `/root/.config/x5car/ros_env.bash`；使用普通 `bash` 进入容器 |
| RViz 报 `Authorization required`、不能连接 display | 在电脑桌面宿主机重新执行 `xhost +si:localuser:root`；核对两处 DISPLAY |
| 雷达打不开、无 `/scan` | 核对真实设备、udev 链接、dialout 组是否在重新登录后生效、雷达型号 |
| 无地图但 `/map` 有数据 | RViz Map 使用 `Transient Local`；建图或定位时 Fixed Frame 为 `map` |
| 导航无 `map → odom` | 检查地图路径、AMCL active、初始位姿、激光和 TF |
| 修改用户配置后 root 仍找不到包 | 不自动切换 root，使用普通 `robotwzz` SSH 会话 |

实机稳定后，在板卡备份当前源码、地图、自动环境配置和版本记录。先停止运动与项目节点，再执行：

```bash
mkdir -p /home/robotwzz/dev_ws/maps
mkdir -p /home/robotwzz/x5car_setup/udev_snapshot

dpkg-query -W > /home/robotwzz/x5car_setup/RDK_DPKG_ALL.txt

for rule in /etc/udev/rules.d/99-x5car-n10.rules \
            /etc/udev/rules.d/56-orbbec-usb.rules; do
    if [ -f "$rule" ]; then
        cp "$rule" /home/robotwzz/x5car_setup/udev_snapshot/
    fi
done

extra_files=()
for item in x5car_setup/LIBUVC_COMMIT.txt dev_ws/map.yaml dev_ws/map.pgm; do
    if [ -f "/home/robotwzz/$item" ]; then
        extra_files+=("$item")
    fi
done

backup_file="/home/robotwzz/X5CAR_RDK_VERIFIED_$(date +%Y%m%d_%H%M%S).tar.gz"
tar -czf "$backup_file" \
  -C /home/robotwzz \
  dev_ws/src dev_ws/maps .config/x5car .bashrc .profile \
  x5car_setup/RDK_DPKG_ALL.txt x5car_setup/udev_snapshot \
  "${extra_files[@]}"

sha256sum "$backup_file"
```

该归档同时保存 `.bashrc`、`.profile` 加载入口、环境文件、项目和硬件参数、地图、当前完整包版本及已安装的项目 udev 规则。存在相机 libuvc commit 记录或根目录地图时也会加入。它是源码和配置备份，不含 ARM64 安装包、系统镜像或全部手工安装库；重新部署仍需要本文的依赖安装与编译步骤。

本文依据本地项目手册、部署脚本及源码归档编写，按一块全新板卡的安装顺序组织。文档中的部署步骤尚未在一块全新板卡上逐项实机验收；最终依据是板卡 ARM64 编译结果、设备数据、控制器状态以及建图和导航测试。
