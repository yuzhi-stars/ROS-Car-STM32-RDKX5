# 脚本用途

脚本内容及其相对目录关系保持原样。

| 脚本 | 执行位置和用途 |
| --- | --- |
| `install_project_deps.sh` | 有 Humble 的板卡或电脑容器；参数为目标工作空间路径 |
| `install_udev_on_host.sh` | USB 设备实际连接的 Linux 主机；读取根目录 `udev/` 模板 |
| `verify_ros_environment.sh` | ROS 环境所在终端；检查工作空间、包版本和设备 |
| `verify_device_passthrough.sh` | 电脑宿主机；检查已有 Docker 容器的设备绑定 |
| `restore_verified_environment.sh` | 电脑宿主机；参数为同时包含源码、镜像和校验文件的快照目录，当前可指定 `~/RDKX5Car/archives` |
| `export_verified_environment.sh` | 有可用 Docker 容器和已核对共享工作空间的电脑；导出新的快照 |

安装与恢复脚本会修改相应目标环境。日常运行、首次部署、sudo 和 Docker 权限的具体用法见 [当前操作说明](../docs/README.md)。导出脚本使用其 `OUT` 指定的目录，默认生成独立快照目录，不会自动改写本部署包的 `archives/`。
