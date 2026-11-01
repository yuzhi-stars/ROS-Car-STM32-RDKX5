# astra_camera（厂商源码副本）

此包来自本地 Astra Pro 资料中的 OpenNI ROS2 SDK v1.1.0，原始说明见上一级 README.MD。
SDK 和采集逻辑保留。工作空间副本有一处 TF 修正：`src/ob_camera_node.cpp` 删除冗余的
camera_link->camera_color_frame，保留 camera_link->camera_depth_frame->camera_color_frame，
避免 camera_color_frame 同时有两个父坐标系。原资料目录没有改动。
启动文件 `launch/astra_pro.launch.xml`；内部光学 TF 由驱动发布。
机器人 bringup 通过 enable_camera:=true 包含该启动文件，camera_name=camera，并开启深度点云。
base_link->camera_link 安装 TF 由 x5car_description 负责，禁止再手动发布同一条 TF。
小车 sensors.launch.py 设置 tf_publish_rate=0，内部固定外参发布到 /tf_static。
默认分辨率 640x480/30Hz，RDK 性能不够时从该 launch 降低帧率，而非改机器人 UART。
后续按实际设备确认 UVC vendor/product ID、USB 带宽、彩色/深度帧名、点云话题。
依赖包括 libuvc、glog、OpenCV、Eigen、tf2_eigen 和 ROS image/camera 组件；安装见工作空间 README。
openni2_redist 内分 arm/arm64/x64，CMake 按开发板架构选库，需确认系统为 aarch64。
保留厂商授权；该源码和 SDK 尚未在当前开发板构建或运行。
