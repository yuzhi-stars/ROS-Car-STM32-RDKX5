# x5car_localization（独立参考，当前不接入）

包含 `config/ekf.yaml` 和 `launch/ekf.launch.py`，使用 robot_localization 的二维 EKF。
默认 bringup 不引用本包；当前唯一里程计是麦轮控制器 `/odom`，TF 也由控制器发布。

参考配置预留：输入轮速里程计 `/wheel/odometry`、IMU `/imu/data`，输出统一 `/odom`。
仅融合轮里程计 twist 的 vx/vy/wz 和 IMU gz；不重复融合同源积分 pose，不默认融合六轴绝对 yaw。
IMU 加速度仍上传，但此参考 EKF 默认不融合它，避免未调校时积分漂移。

以后真正启用时，必须同时完成以下改动，不能直接与默认启动并行运行：

1. `x5car_bringup/config/controllers.yaml` 的 enable_odom_tf 改 false。
2. 硬件 launch 的 odom_topic 改 `/wheel/odometry`（已有同名 launch 参数）。
3. 单独启动本包 EKF，默认 odometry/filtered 已重映射到 `/odom`。
4. 确认只有 EKF 发布 odom->base_link，Nav2 仍只读 `/odom`，不用修改导航输入。

未来启用命令示例：`ros2 launch x5car_localization ekf.launch.py`。
目前不要执行；本包只是实现和配置参考。噪声、协方差及反馈延迟需要实测后调整。
