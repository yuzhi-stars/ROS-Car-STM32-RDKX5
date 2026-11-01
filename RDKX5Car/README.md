# RDKx5 小车部署资料

> 公开版：ROS 2 源码已展开至 [src/](src/README.md)。原始 Docker 镜像校验失败，未发布；电脑端按[容器重建说明](../docs/RELEASE.md)部署，原文镜像导入步骤需待重新导出有效镜像后使用。

本目录集中保存 RDKx5 小车的部署归档、配置、脚本和操作文档。板卡为 Ubuntu 22.04 / ROS 2 Humble / ARM64；电脑使用 x86_64 Humble Docker。

## 从哪里开始

| 当前任务 | 阅读文档 |
| --- | --- |
| 一块只有 Ubuntu 和 ROS 2 的全新板卡 | [从零部署与首次运行](docs/RDKx5_ROS_Car_新板卡从零部署算法与首次运行说明.md) |
| 板卡已配置好，在板卡运行建图和导航，电脑只运行 RViz | [板卡算法日常操作](docs/RDKx5_ROS_Car_板卡运行算法和Rviz_电脑端RViz操作说明_非初始运行.md) |
| 板卡运行硬件，电脑运行建图或导航算法及 RViz | [电脑算法部署与操作](docs/RDKx5_ROS_Car_电脑运行算法和Rviz_板卡运行硬件部署说明.md) |

三份文档区分板卡、电脑宿主机和电脑容器的命令。两条运行链路按文档切换，保持同一功能的节点只有一套。

## 目录结构

```text
RDKX5Car/
├── README.md                 # 本入口
├── src/                      # ROS 2 源码、驱动和各包说明
├── archives/                 # 原始源码归档及 SHA256SUMS
├── docs/                     # 三份当前操作说明和文档索引
│   └── reference/            # 原始交接手册、说明和运行参数记录
├── environment/              # 软件包版本和环境快照
├── docker/                   # compose.yaml、.env.example 和重建 Dockerfile
├── scripts/                  # 安装、检查、导出和恢复脚本
└── udev/                     # 雷达串口规则模板
```

## 部署归档校验

源码已包含在 `archives/WORKSPACE_SRC.tar.gz`，可运行 `sha256sum -c SHA256SUMS` 校验。`src/` 是相同源码的展开副本。原始 Docker 镜像未通过校验，公开版使用[重建说明](../docs/RELEASE.md)。

## 配置与脚本

- [Docker Compose](docker/compose.yaml)：host 网络、共享工作空间、图形和设备绑定；从 `docker/.env.example` 创建自己的 `.env`。
- [脚本用途索引](scripts/README.md)：依赖、设备、ROS 环境检查及备份恢复。
- [环境快照说明](environment/README.md)：快照中的软件版本和通信环境含义。
- [参考资料索引](docs/reference/README.md)：原始资料的适用范围。

部署包根目录仍为 `/home/zzw/RDKX5Car`。实际项目工作空间仍为电脑 `~/ecust_ws`、容器 `/root/ecust_ws` 和板卡 `/home/robotwzz/dev_ws`；本次整理不改变这些工作空间。

当前运行基线为 Cyclone DDS、`ROS_DOMAIN_ID=1`、`ROS_LOCALHOST_ONLY=0`。IP、串口、轮速上限和几何参数按实际设备确认。`environment/` 是导出时的记录；运行配置以当前操作说明和配置文件为准。
