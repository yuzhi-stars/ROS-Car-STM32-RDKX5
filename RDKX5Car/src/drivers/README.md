# 第三方传感器驱动

雷达沿用工作空间已有 `src/lslidar_driver/lslidar_driver` 和 `src/lslidar_msgs/lslidar_msgs`，未移动或修改其源码。
相机 `ros2_astra_camera` 来自用户本地提供的 OpenNI ROS2 SDK v1.1.0（2023-01-31）。
复制时保留源码、launch、SDK、厂商说明及授权信息，去掉 build/install/log/cmake-build 缓存。
该版本含 openni2_redist/arm64，不能拿资料中的独立 OpenNI-Linux-x64 SDK代替 RDK X5 的 ARM64 库。
驱动较旧，ARM64/Humble 的依赖和 ABI 必须在开发板实际编译后确认，尚未验证。
同一工作空间不要再导入第二份同名 astra_camera/astra_camera_msgs 包。
