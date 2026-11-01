# 环境快照

这里保存部署归档随附的原始文本记录，内容未改写。

| 文件 | 记录内容 |
| --- | --- |
| `DPKG_ALL.txt` | 电脑 x86_64 容器的 dpkg 软件包版本 |
| `ROS_ENV_INFO.txt` | 导出时的架构、系统和 ROS 环境变量 |
| `RUNTIME_CONFIGURATION.txt` | 容器镜像、挂载、网络及部分环境变量 |

快照中记录的 RMW 为 `rmw_fastrtps_cpp`；当前 Compose 和操作说明统一为 `rmw_cyclonedds_cpp`。快照用于追溯导出时的状态。当前运行应核对实际节点环境，不直接照搬快照中的 RMW 或 DISPLAY。

x86_64 版本清单用于参考，不能作为板卡 ARM64 安装脚本。板卡实际版本在部署时另行记录。容器的完整历史 inspect 摘要见 [参考资料](../docs/reference/ORIGINAL_DOCKER_INSPECT_20261029.md)。
