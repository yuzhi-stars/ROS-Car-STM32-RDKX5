# X5Car 交接包 v2.2 · 使用顺序

**本压缩包不是原机 Docker 镜像本身**。它包含完整交接手册、设备及 Compose 模板，以及在**原电脑**导出实际 Docker 环境的脚本。当前对话无法访问你的本地 Docker daemon，所以无法代替你将 `x5car_dev` 的真实镜像导出；需要你在旧电脑上执行一次导出操作。

**当前 DDS 统一配置**：Docker 与 RDK X5 均使用 `export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`，`ROS_DOMAIN_ID=1`，`ROS_LOCALHOST_ONLY=0`。Docker Compose、备用 Dockerfile、依赖脚本及交接手册均已同步更正。升级 TF2 后问题已解决，因此交接时务必保留真实稳定版本。

**原机已实测的 Docker 热插拔配置（2026-10-29）**：`x5car_dev` 原镜像 `x5car_humble:dev`，`privileged=true`、`network=host`、`ipc=host`，挂载整个 `/dev` 和 `/run/udev`（以及 X11、工作空间）。`HostConfig.Devices` 为空完全正常：不是固定 `--device=/dev/ttyACM0` 的逐个映射，而是在容器启动后仍可看到新增的设备节点。接手人必须复现整目录绑定，并实际做 USB、ACM、相机插拔验收。**不要把静态设备映射冒充为等价热插拔方案。**

**共享文件夹**：宿主 `.env` 中的 `HOST_WS=/home/<接手人用户名>/ecust_ws`，映射至容器 `/root/ecust_ws`，读写双向同步；`build/`、`install/` 仅供 Docker 的 x86_64 ROS Humble 使用。具体测试见交接手册 §4.1。

## 应先做的三件事

1. 阅读 `X5Car_开发环境与实机部署_完整交接手册.md`，尤其是第 0～2 章。
2. 在**旧电脑 Ubuntu 24.04 宿主机**执行（脚本路径按你解压位置调整）：

   ```bash
   export CONTAINER=x5car_dev
   export HOST_WS="$HOME/ecust_ws"
   bash scripts/export_verified_environment.sh
   ```

   导出后把 `X5CAR_DOCKER_IMAGE.tar.gz`、`WORKSPACE_SRC.tar.gz`、包版本快照、`SHA256SUMS` 与本交接包一起交给接手人。注意导出的是**原机最新源码**，不是旧 `src.zip`。

3. 接手电脑（Ubuntu 24.04）安装 Docker 后，先恢复镜像和源码，再修改 `docker/.env` 与图形授权：

   ```bash
   export HOST_WS="$HOME/ecust_ws"
   bash scripts/restore_verified_environment.sh /实际路径/X5Car_verified_export
   cd docker
   cp .env.example .env  # 编辑 HOST_WS 为真实绝对路径
   xhost +si:localuser:root
   docker compose up -d
   docker exec -it x5car_dev bash
   ```

## 目录说明

| 文件 | 功能 |
|---|---|
| `X5Car_开发环境与实机部署_完整交接手册.md` | 含 15 章、从零安装、Docker、硬件、通信、编译、运行、故障与验收 |
| `docker/compose.yaml` | Cyclone DDS + 全特权 + host 网络 + 全 `/dev` + X11 + `HOST_WS` 双向共享，仅可信实验 PC 使用 |
| `docker/.env.example` | 改为接手人的工作空间绝对路径 |
| `docker/Dockerfile.rebuild` | 无真实镜像时的后备构建方案，不能视为等价复现 |
| `scripts/export_verified_environment.sh` | 在原电脑导出镜像增量、源码、包版本、参数摘要、校验 |
| `scripts/restore_verified_environment.sh` | 接手电脑导入镜像、恢复 `src/`，避免覆盖现有目录 |
| `scripts/install_project_deps.sh` | 容器或 RDK 的常见 apt/rosdep 依赖安装 |
| `scripts/install_udev_on_host.sh` | 在设备实际连接的 Linux 宿主机安装规则 |
| `scripts/verify_ros_environment.sh` | 容器内只读检查包路径、TF2 版本和设备 |
| `scripts/verify_device_passthrough.sh` | PC 宿主机只读核对 privileged、host、四项绑定及热插拔设备枚举 |
| `udev/99-x5car-n10.rules` | N10 雷达稳定串口别名模板，须核对设备 ID |

## 重要提醒

- 原 `src.zip` 中的硬件代码不包含后来板端应用的完整 watchdog 补丁；交接前从 RDK 复制实际最新版 `.cpp/.hpp`。
- `map.yaml` 和 `map.pgm` 必须从 RDK 复制最新建图结果；旧源码包里的地图文件名不同。
- 现场最近反馈**升级 TF2 后导航内部 TF 时间戳冻结已消失**，但尚无精确包版本清单；以原机导出的镜像及 `DPKG_ALL.txt` 为准。
- `docker commit` 不保存宿主机 bind mount 文件；所以不能省略 `WORKSPACE_SRC.tar.gz`。
- `privileged: true` 与 `/dev:/dev` 会大幅降低容器与宿主的隔离。不要把这个交接包应用在不受信任的共享电脑上，也不要把镜像/源码上传至公共仓库。
