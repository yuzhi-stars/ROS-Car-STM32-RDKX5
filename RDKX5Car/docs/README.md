# RDKx5 文档索引

| 阅读顺序 | 文档 | 使用场景 |
| --- | --- | --- |
| 1 | [新板卡从零部署](RDKx5_ROS_Car_新板卡从零部署算法与首次运行说明.md) | 系统已有 Ubuntu 22.04 和 ROS 2 Humble，首次安装项目 |
| 2A | [板卡运行算法](RDKx5_ROS_Car_板卡运行算法和Rviz_电脑端RViz操作说明_非初始运行.md) | 完成配置后，在板卡运行建图或导航 |
| 2B | [电脑运行算法](RDKx5_ROS_Car_电脑运行算法和Rviz_板卡运行硬件部署说明.md) | 在电脑 Docker 运行建图或导航，板卡只运行硬件 |

2A 与 2B 是不同运行链路，按说明切换。首次配置、编译需要执行相应的环境加载；自动加载验收通过后，日常终端可以直接运行 ROS 命令，RViz 只输入 `rviz2`。

部署文件位置以[根目录入口](../README.md)为准。源码归档在 `../archives/`；电脑 Docker 镜像需从 GitHub Release 下载三个分片，按[发布说明](../../docs/RELEASE.md)合并校验后放入该目录。配置和脚本在根目录对应子目录。原始材料保存在 [reference](reference/README.md)。
