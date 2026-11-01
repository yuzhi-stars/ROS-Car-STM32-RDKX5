下面把这次在 Docker 中调通的依赖整理成一份 RDK X5 部署安装清单。以后把代码迁移到开发板时，可以按顺序执行。

适用环境：

| 项目        | 配置                     |
| --------- | ---------------------- |
| 开发板       | D-Robotics RDK X5      |
| 系统        | Ubuntu 22.04           |
| ROS 2     | Humble                 |
| CPU 架构    | ARM64 / aarch64        |
| 工作空间      | `~/ecust_ws`           |
| 激光雷达      | 镭神 N10                 |
| 深度相机      | Orbbec Astra Pro       |
| 底盘控制      | STM32 + `ros2_control` |
| SLAM / 导航 | SLAM Toolbox + Nav2    |

重要： 以下命令是在 RDK X5 原生 Ubuntu 系统执行，不是在电脑的 Docker 中。默认 RDK 已安装 ROS 2 Humble，并且能够通过 apt 安装 ROS 软件包。

## 一、安装系统基础开发依赖

```
sudo apt update

sudo apt install -y \
  build-essential \
  cmake \
  git \
  wget \
  curl \
  pkg-config \
  python3-pip \
  python3-colcon-common-extensions \
  python3-rosdep \
  libeigen3-dev \
  libusb-1.0-0-dev \
  libudev-dev \
  libserial-dev \
  libpcap-dev \
  libpcl-dev \
  libgflags-dev \
  libgoogle-glog-dev
```

这些库覆盖 C++ 编译、USB 通信、串口通信、雷达点云及 Astra SDK 所需的常见基础依赖。

## 二、安装 ros2_control 和底盘控制依赖

这是之前 `x5car_hardware` 编译缺少 `hardware_interface` 时对应的依赖。

```
sudo apt install -y \
  ros-humble-ros2-control \
  ros-humble-ros2-controllers \
  ros-humble-hardware-interface \
  ros-humble-controller-manager \
  ros-humble-mecanum-drive-controller \
  ros-humble-joint-state-broadcaster \
  ros-humble-imu-sensor-broadcaster
```

`ros2_control` 提供硬件抽象接口，`ros2_controllers` 提供控制器插件。Humble 的官方安装文档支持通过 apt 安装这两个组件。

[image](https://www.google.com/s2/favicons?domain=https://control.ros.org\&sz=32)

ROS2_Control: Humble Oct 2026 documentation



安装后验证：

```
source /opt/ros/humble/setup.bash

ros2 pkg prefix hardware_interface
ros2 pkg prefix controller_manager
ros2 pkg prefix mecanum_drive_controller
```

三个都能找到，说明核心控制依赖已经安装。

## 三、安装定位、SLAM、导航及 TF 依赖

```
sudo apt install -y \
  ros-humble-robot-localization \
  ros-humble-slam-toolbox \
  ros-humble-navigation2 \
  ros-humble-nav2-bringup \
  ros-humble-tf2-ros \
  ros-humble-tf2-tools \
  ros-humble-robot-state-publisher \
  ros-humble-joint-state-publisher \
  ros-humble-twist-mux \
  ros-humble-xacro
```

对应你的几个包：

| 自定义功能包               | 主要依赖                        |
| -------------------- | --------------------------- |
| `x5car_description`  | Xacro、Robot State Publisher |
| `x5car_hardware`     | ros2_control                |
| `x5car_localization` | robot_localization、TF       |
| `x5car_slam`         | slam_toolbox                |
| `x5car_navigation`   | Nav2                        |
| `x5car_bringup`      | 上述各组件                       |

## 四、安装激光雷达依赖

```
sudo apt install -y \
  ros-humble-pcl-conversions \
  ros-humble-pcl-ros \
  ros-humble-sensor-msgs \
  libpcl-dev \
  libpcap-dev
```

你的 `lslidar_driver` 和 `lslidar_msgs` 都已经在电脑 Docker 中成功编译过。迁移到 RDK 后需要重新编译 ARM64 版本。

雷达串口仍使用：

```
/dev/wheeltec_lidar
```

波特率仍为：

```
230400
```

RDK 上也需要安装对应的 udev 规则，因为电脑宿主机的规则不会随 ROS 工作空间源码一起迁移。

## 五、安装 Astra Pro 深度摄像头依赖

这是你这次花时间调通的部分，建议单独保存。

### 5.1 安装 ROS 图像处理依赖

```
sudo apt install -y \
  ros-humble-image-geometry \
  ros-humble-camera-info-manager \
  ros-humble-image-transport \
  ros-humble-image-publisher \
  ros-humble-cv-bridge \
  ros-humble-image-pipeline
```

### 5.2 安装 libuvc

你提供的 Orbbec 驱动 README 明确要求安装 `libuvc`。

README.MD



```
cd ~
git clone https://github.com/libuvc/libuvc.git
cd libuvc

mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
sudo make install
sudo ldconfig
```

RDK X5 是 ARM64，因此应当在 RDK 上从源码编译 libuvc，不要直接复制电脑 Docker 中的 x86-64 库文件。

### 5.3 安装 Orbbec USB 权限规则

将源码迁移到 RDK 后，找到相机驱动目录：

```
cd ~/ecust_ws/src/drivers/ros2_astra_camera/astra_camera/scripts
```

先确认脚本内容：

```
cat install.sh
```

然后安装 USB 规则：

```
sudo bash install.sh

sudo udevadm control --reload-rules
sudo udevadm trigger
```

这是你提供的 Astra 驱动文档中的安装流程。

README.MD



注意： 这里的路径按你最新的 VS Code 工作空间结构填写。如果实际迁移路径不同，以 `find ~/ecust_ws/src -name install.sh` 查找到的 Astra 脚本为准。

### 5.4 Astra Pro 使用专用启动文件

在电脑上，你已经验证普通 `astra.launch.xml` 不能正常输出 RGB 图像，而切换到 Astra Pro 的专用启动文件后，RGB 图像恢复。

迁移后建议直接使用：

```
ros2 launch astra_camera astra_pro.launch.xml
```

这是你工作空间中已经存在的专用启动文件。

设备检查：

```
lsusb
```

此前 Astra Pro 对应：

```
2bc5:0403  ORBBEC ASTRA Pro
2bc5:0501  Astra Pro HD Camera
```

### 5.5 特别注意 ARM64 SDK

你这份 Astra 驱动 README 还要求提供相应的 OpenNI SDK 文件。

README.MD



这是迁移时最需要单独确认的兼容性问题。

如果当前 `ros2_astra_camera` 源码自带的 OpenNI SDK 只有 x86-64 二进制库，即使 ROS 依赖全部安装，RDK X5 上仍然可能编译或运行失败。

迁移前可以在电脑 Docker 中检查：

```
find /root/ecust_ws/src/drivers/ros2_astra_camera \
  -type f \( -name '*.so' -o -name '*.a' \) \
  -exec file {} \;
```

如果输出 `x86-64`，对应的预编译库不能直接用于 RDK X5；需要找兼容 ARM64 的 SDK。这一步不能仅靠 `apt install` 解决。

## 六、安装其他运行工具

如果 RDK 需要本地调试：

```
sudo apt install -y \
  ros-humble-rqt \
  ros-humble-rqt-image-view \
  ros-humble-rviz2 \
  usbutils \
  v4l-utils
```

其中 RViz2、rqt 是可选的。RDK X5 主要承担底盘和传感器节点运行时，可以在电脑上运行 RViz2，减少开发板的图形负载。

## 七、迁移源码并编译

在电脑 Ubuntu 宿主机执行：

```
rsync -av \
  /home/luca/ecust_ws/src/ \
  robotwzz@192.168.1.48:~/ecust_ws/src/
```

这里仅复制 `src/`。不要复制电脑 Docker 生成的 `build/`、`install/` 和 `log/`。

在 RDK X5 上执行：

```
cd ~/ecust_ws

source /opt/ros/humble/setup.bash

colcon build --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

source install/setup.bash
```

如果 RDK 原先没有创建工作空间目录，先执行：

```
mkdir -p ~/ecust_ws/src
```

### 使用 rosdep 补齐遗漏依赖

由于你的自定义功能包还在持续开发，最终依赖可能发生变化。推荐在 RDK 上用 rosdep 根据 `package.xml` 自动安装：

```
sudo rosdep init
rosdep update

cd ~/ecust_ws

rosdep install \
  --from-paths src \
  --ignore-src \
  --rosdistro humble \
  -r -y
```

如果 `rosdep init` 提示已经初始化，可直接跳过该步。如果报告某个包缺少 rosdep 定义，应单独核对其依赖。

## 八、迁移后检查

建议按以下顺序验证，不必一开始就启动所有节点。

```
# 1. 控制框架
ros2 pkg prefix hardware_interface

# 2. 激光雷达
ros2 launch lslidar_driver lsn10_launch.py

# 3. Astra Pro 摄像头
ros2 launch astra_camera astra_pro.launch.xml

# 4. SLAM Toolbox
ros2 pkg prefix slam_toolbox

# 5. Nav2
ros2 pkg prefix nav2_bringup
```

雷达和摄像头需要分别在不同终端运行，并加载工作空间环境。

## 九、迁移时最需要注意的三个问题

1. ARM64 架构兼容性。 普通 ROS 2 Humble 软件包优先通过 RDK 的 apt 软件源安装；Astra 的 OpenNI 预编译 SDK 必须另行确认 ARM64 支持。
2. 硬件设备权限。 Docker 的 `--privileged` 不会迁移到 RDK。RDK 原生系统需要通过 udev、用户组以及实际串口路径管理硬件。
3. 系统与软件版本。 `ros2_control` 等软件在迁移时最好记录已验证的版本，避免电脑与 RDK 安装不同版本而出现控制器参数或 ABI 差异。

为了以后精确复现电脑 Docker 的环境，建议现在就在电脑容器里导出一份实际安装包清单：

```
dpkg-query -W -f='${Package}=${Version}\n' \
  | grep -E '^(ros-humble-|libuvc|libusb|libpcl|libgflags|libgoogle-glog|libeigen3)' \
  > /root/ecust_ws/DEPENDENCIES_DOCKER.txt
```

这份文件会自动同步到电脑宿主机的：

`/home/luca/ecust_ws/DEPENDENCIES_DOCKER.txt`

它比手工整理的推荐包列表更准确地记录你现在实际安装的版本。不过不能把这份 x86-64 包版本清单直接当作 ARM64 安装脚本，仍需检查 RDK 软件源是否提供对应版本。

建议最终保存两份资料：这份 RDK 安装步骤，以及 Docker 实际依赖清单。 以后迁移或重装 RDK X5 时就有完整依据。