#!/usr/bin/env bash
# 在 Docker 内运行；只读检查，不启动硬件/导航。
set -uo pipefail
[[ -f /opt/ros/humble/setup.bash ]] && source /opt/ros/humble/setup.bash
[[ -f /root/ecust_ws/install/setup.bash ]] && source /root/ecust_ws/install/setup.bash
printf 'OS: '; . /etc/os-release; echo "$PRETTY_NAME"; uname -m
printf 'ROS_DISTRO=%s DOMAIN=%s RMW=%s LOCALHOST=%s\n' "${ROS_DISTRO:-}" "${ROS_DOMAIN_ID:-}" "${RMW_IMPLEMENTATION:-}" "${ROS_LOCALHOST_ONLY:-}"
if [[ "${RMW_IMPLEMENTATION:-}" != "rmw_cyclonedds_cpp" ]]; then
  echo 'WARNING: RMW 未设为 rmw_cyclonedds_cpp；本项目 Docker/RDK 应统一 Cyclone DDS。'
fi
if [[ "${ROS_DOMAIN_ID:-}" != 1 ]]; then
  echo 'WARNING: ROS_DOMAIN_ID 不是 1。'
fi
ros2 pkg prefix rmw_cyclonedds_cpp 2>/dev/null || echo 'WARNING: 未找到 rmw_cyclonedds_cpp，请安装 ros-humble-rmw-cyclonedds-cpp'
for pkg in x5car_hardware x5car_bringup x5car_navigation x5car_slam lslidar_driver astra_camera nav2_controller nav2_costmap_2d; do
  echo "---- $pkg ----"; ros2 pkg prefix "$pkg" || true
done
echo 'TF2 packages:'; dpkg-query -W 'ros-humble-tf2*' 2>/dev/null || true
echo 'libuvc:'; ldconfig -p | grep libuvc || true
echo 'USB & video:'; ls -l /dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video* 2>/dev/null || true
echo 'udev database:'; if [[ -d /run/udev/data ]]; then echo '/run/udev/data available'; else echo 'WARNING: /run/udev/data unavailable; check /run/udev bind mount'; fi

