#!/usr/bin/env bash
# 运行地点：已有 ROS Humble 的 Ubuntu 22.04 Docker root shell 或 RDK (sudo)
# 注意第三方 Astra OpenNI SDK、libuvc 的平台库不一定能用 rosdep 自动解决。
set -euo pipefail
WS="${1:-/root/ecust_ws}"
[[ -d "$WS/src" ]] || { echo "不存在源码目录 $WS/src" >&2; exit 1; }
if [[ -f /opt/ros/humble/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
else
  echo '未找到 /opt/ros/humble；请先安装 ROS 2 Humble' >&2; exit 1
fi
APT=(apt-get)
if [[ $EUID -ne 0 ]]; then APT=(sudo apt-get); fi
"${APT[@]}" update
"${APT[@]}" install -y \
  build-essential cmake git pkg-config python3-colcon-common-extensions python3-rosdep \
  libusb-1.0-0-dev libudev-dev libserial-dev libpcap-dev libpcl-dev libjpeg-dev \
  ros-humble-ros2-control ros-humble-ros2-controllers \
  ros-humble-mecanum-drive-controller ros-humble-joint-state-broadcaster \
  ros-humble-imu-sensor-broadcaster ros-humble-teleop-twist-keyboard \
  ros-humble-navigation2 ros-humble-nav2-bringup ros-humble-nav2-rviz-plugins \
  ros-humble-rmw-cyclonedds-cpp \
  ros-humble-slam-toolbox ros-humble-robot-localization ros-humble-twist-mux \
  ros-humble-pcl-ros ros-humble-pcl-conversions ros-humble-cv-bridge \
  ros-humble-image-transport ros-humble-camera-info-manager \
  ros-humble-image-geometry ros-humble-rviz2
if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
  if [[ $EUID -eq 0 ]]; then rosdep init; else sudo rosdep init; fi
fi
rosdep update
cd "$WS"
rosdep install --from-paths src --ignore-src --rosdistro humble -r -y
printf '依赖安装完成。下一步：检查 Astra SDK/libuvc、源码版本，然后 colcon build。\n'
