# 许可范围与第三方组件

根目录 MIT 许可证适用于 Zizheng Wang 的原创固件、原创文档及原创硬件设计。已有独立许可声明的文件和组件保留原许可证，根目录 MIT 不替换它们。

| 组件 | 本仓库位置 | 许可依据 |
| --- | --- | --- |
| STM32 HAL、CMSIS 与设备支持 | `ROS_car_V1/Drivers/` | 保留各目录 `LICENSE.txt` 及源文件版权头 |
| ROS 2 x5car 包 | `RDKX5Car/src/x5car_*` | 各 `package.xml` 中的 Apache-2.0 声明 |
| LSLidar 驱动及消息 | `RDKX5Car/src/lslidar_driver`、`lslidar_msgs` | 各 `package.xml` 中的 Apache-2.0 声明及原版权头 |
| Orbbec Astra ROS 2 驱动 | `RDKX5Car/src/drivers/ros2_astra_camera` | 随附 `README.MD` 的 Apache-2.0 许可节；[上游](https://github.com/orbbec/ros2_astra_camera) |
| OpenNI2 与 Orbbec 运行库 | Astra 驱动中的 `openni2*` 目录 | 保留原版权头、厂商文件及运行库，不重新授权为 MIT |
| nlohmann/json、magic_enum 等内嵌库 | Astra 驱动 `include/` | 遵循各文件内嵌的版权和许可声明 |

相机驱动部分 `package.xml` 仍有历史占位或不同的许可文字，本次保留源码，未把这些声明改写为 MIT；该组件的项目级许可见其原始 README。原始 `WORKSPACE_SRC.tar.gz` 原样保留，归档内的许可声明同样适用。

底盘文件 `hardware/structure/R3mini麦轮车底盘（无悬挂）.stp` 的原作者和许可未在输入资料中标注；该文件不在原创 MIT 授权范围内，使用时需向原作者确认授权。
