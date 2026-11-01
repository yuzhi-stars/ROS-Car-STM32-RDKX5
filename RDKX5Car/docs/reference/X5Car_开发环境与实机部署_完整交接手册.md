# X5Car 开发环境、Docker 镜像、实机设备及 ROS 2 部署交接手册

**版本**：v2.2 · 2026-10-29（根据原机 docker inspect 锁定真实运行参数及动态热插拔能力）　　**适用对象**：开发接手者、实机调试者　　**平台**：Ubuntu 24.04 宿主机 + Ubuntu 22.04 / ROS 2 Humble Docker + RDK X5 Ubuntu 22.04 / ROS 2 Humble

> **本项目当前通信基线**：电脑 Docker 和 RDK X5 的所有 ROS 2 Humble 进程统一采用 `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`、`ROS_DOMAIN_ID=1`、`ROS_LOCALHOST_ONLY=0`。已验证的 Docker 镜像还应保留升级后的 TF2 软件包版本；改用 Cyclone DDS 本身并不是此前 TF 停滞的修复手段。
>
> **已实测的运行参数**：2026-10-29 原电脑执行 `docker inspect x5car_dev`，证实原容器基底 `x5car_humble:dev`、`privileged=true`、`network=host`、`ipc=host`，并绑定 `/dev`、`/run/udev`、`/tmp/.X11-unix` 和 `/home/luca/ecust_ws`。`HostConfig.Devices` 为空，属于整目录挂载的正常表现，不代表不能访问设备。
>
> **交接状态的说明**：本手册根据前一版《X5Car_Docker_开发环境部署与交接手册》、2026-10-28 的 `src.zip` 源码快照、实际调试命令和最新用户反馈整理。本文附的 `compose.yaml`、脚本和后备 `Dockerfile` 是**推荐的交接/恢复模板**，**不是对当前 Docker 运行参数的自动读取结果，也尚未在接手电脑上实机验收**。交付前必须从原机执行 `docker inspect`、导出镜像并保存包版本；下面涉及硬件规格、端口、相机 SDK 的项目均需实测确认。
>
> **安全边界**：交接采用用户原先要求的 **`privileged: true` + `network_mode: host` + `ipc: host` + `/dev`、`/run/udev` 整目录挂载 + X11 授权**。这种容器有很高的宿主机访问能力，只能在受信任的专用开发电脑使用。**USB 设备实际插在哪台主机，就在那台主机安装 udev 规则。** 不要把有密码、SSH 私钥、令牌、公司受限 SDK 的镜像或源码发布到公共仓库。

---

## 0. 交接清单与关键结论

### 0.1 接手人实际需要收到什么

| 交接物 | 来源 | 必须性 | 说明 |
|---|---|---|---|
| `X5CAR_DOCKER_IMAGE.tar.gz` | 旧电脑 `x5car_dev` 容器 `docker commit` 后 `docker save` | 必须 | 保存旧容器文件系统中 apt、pip 和 `/usr/local` 增量安装；**不含 bind mount 数据** |
| `WORKSPACE_SRC.tar.gz` | 原电脑 `$HOME/ecust_ws/src/` 的**最新**源码 | 必须 | 自研包、雷达驱动、相机驱动；不包含旧的 `build/install/log` |
| `map.yaml` + `map.pgm` | 开发板实际建图结果 | 导航必需 | RDK 原图尺寸曾为 315 × 95；旧 `src.zip` 不是这对文件，必须单独核对 |
| 当前 RDK `x5car_system.cpp/.hpp` | 开发板**实机最新版** | 必须 | 原 `src.zip` 尚未叠加 `apply_x5car_watchdog_fix(2).py`，不可覆盖板端修复 |
| `docker/compose.yaml`、`.env` 配置 | 已按原机 `docker inspect` 的明确字段更新；接手人替换实际宿主路径 | 必须 | 恢复 `privileged`、host 网络/IPC、整 `/dev`、`/run/udev`、USB/ACM/视频热插拔、共享工作区、Cyclone DDS、X11 |
| `udev/` 及 Orbbec 原厂规则 | 本包雷达规则 + 项目驱动目录 | 设备直连时必须 | 安装在 RDK 或 PC 的真实宿主机，**不是 Docker 内** |
| `DPKG_ALL.txt`、`PIP_FREEZE.txt`、`RUNTIME_CONFIGURATION.txt`、SHA256 | 原机导出脚本 | 强烈建议 | 保留 TF2 等关键软件版本和容器运行条件 |

不要把过去为诊断临时拉取的 `nav2_controller`、`nav2_costmap_2d` 源码覆盖进正式工程。用户最近已反馈：**升级 TF2 后内部 TF 卡顿现象消失**。此结果是当前现场反馈；确切修复版本号尚未提供，交接时用 `dpkg-query` 保存，禁止猜测。要保留升级后的稳定容器镜像。旧源码中私自修改的 `ControllerServer::isGoalReached()` 保护补丁若已删除，不应写成已部署的正式功能。

### 0.2 环境边界

| 环境 | 系统 | 工作空间 | 职责 |
|---|---|---|---|
| PC 宿主 | Ubuntu 24.04、x86_64；可能还安装 Jazzy | 如 `/home/luca/ecust_ws` | Docker、Git、VS Code、X11、SSH、物理 USB 设备管理 |
| PC Docker `x5car_dev` | Ubuntu 22.04 Jammy、ROS Humble、x86_64 | `/root/ecust_ws` | 项目编译、RViz2、可选在 PC 侧运行 Nav2/AMCL |
| RDK X5 开发板 | Ubuntu 22.04、ROS Humble、ARM64 | `/home/robotwzz/dev_ws` | STM32、麦轮控制器、雷达、相机；可独立运行 SLAM/Nav2 |

**不同 CPU 架构只迁移 `src/` 源码及配置，不迁移 `build/`、`install/` 产物或 x86 动态库到 RDK ARM64。** Docker 镜像按 x86_64 交付，只能在同架构/兼容环境中直接恢复。

---

## 1. 项目代码与系统关系（按已上传源码核对）

`src.zip` 包含 10 个 ROS 功能包：`x5car_bringup`、`x5car_description`、`x5car_hardware`、`x5car_localization`、`x5car_navigation`、`x5car_slam`、`lslidar_driver`、`lslidar_msgs`、`astra_camera`、`astra_camera_msgs`；第三方驱动置于各自子目录。

**数据链**：

```text
  RDK: STM32 ↔ X5CarSystem ↔ controller_manager ↔ mecanum_controller
                                              ├─ /odom (约 50 Hz)
                                              ├─ odom → base_link（动态 /tf）
                                              └─ /cmd_vel ← twist_mux
  RDK: N10 ── /scan (约 10 Hz) ──────────────┐
  RDK: robot_state_publisher ── /tf_static ──┤
                                            ├─ SLAM 模式：slam_toolbox 产生 /map 与 map → odom
                                            └─ Nav2 模式：map_server 发布 /map，AMCL 发布 map → odom
  Nav2: planner_server → controller_server → /cmd_vel_nav_raw
                                  ↓ velocity_smoother → /cmd_vel_navigation
                                                ↓ [DDS 跨机，可选]
                                         RDK twist_mux → /cmd_vel
  电脑 Docker: RViz2 / rqt，必要时独立启动 AMCL + Nav2
```

**严格保证 TF 的唯一发布者**：`map → odom` 只能由当前使用的 SLAM 或 AMCL 发布；`odom → base_link` 来自 mecanum_controller；车体固定/关节 TF 来自 robot_state_publisher。两台机器上不应同时再启动第二套控制器、地图服务器或 AMCL。

**源码中的特有实现**（必须在交接时保持）：

- `hardware.launch.py` 从 `robot.yaml` 读取轮径和 `(half_wheelbase + half_track)`，注入 mecanum_controller 参数；校验 `use_stamped_vel: false`，将 `reference_unstamped` 重映射到 `/cmd_vel`，把私有 `tf_odometry` 重映射到 `/tf`；三控制器按序 spawn。
- `robot.launch.py` 支持 `mode:=hardware|mapping|navigation`，启用雷达 `enable_lidar:=true`；导航地图参数是绝对 `map.yaml` 路径。
- `sensors.launch.py` 中雷达节点位于 `/x10` 命名空间；雷达 scan 话题由 `lidar.yaml` 设置为 `/scan`；相机默认 `astra_pro.launch.xml`。
- `navigation.launch.py` 读取**系统已安装的** `nav2_bringup/params/nav2_params.yaml`，再深度合并项目 `nav2.yaml`，按机器人几何生成 footprint，并产生临时 YAML。`localization:=true` 时启动 Map Server + AMCL。
- `nav2.yaml` 在所上传快照中是 `DWBLocalPlanner`、`controller_frequency: 20`、局部 `global_frame: odom`、全局 `global_frame: map`、`OmniMotionModel`。之后现场调整的 DWB 参数**应以当前最新仓库文件为准**。
- `twist_mux.yaml`：`/cmd_vel_navigation` 优先级 10，`/cmd_vel_teleop` 优先级 100；输出 `/cmd_vel`。Humble 使用 `geometry_msgs/msg/Twist`，不要把 topic remap 当作类型转换。

### 1.1 需要在交付前人工补齐的变更

`src.zip` 中的 `hardware.yaml` 曾写 `max_wheel_speed_rad_s: 100`；这是**未经硬件规格验收的上限**，不能当安全值直接使用。板端有“轮速异常发零速且保持 active”的改动，以及“看门狗从首次真实 read() 启动”的 `.cpp/.hpp` 补丁。应先在板端提取最新版或 Git commit，再用 diff 检查源代码变化：

```bash
# RDK 端（由开发板操作员执行）
cd ~/dev_ws
sha256sum src/x5car_hardware/src/x5car_system.cpp \
  src/x5car_hardware/include/x5car_hardware/x5car_system.hpp
# 在 PC 侧和板端分别执行：git status --short; git log -1 --oneline（如果是 Git 仓库）
```

**禁止**用本交接包附带的历史 `src.zip` 直接覆盖最新版。板端串口通常是 `/dev/ttyUSB0`，雷达是 `/dev/wheeltec_lidar`，必须以真实连接结果为准。

---

## 2. 交接前：先从原电脑导出已验证 Docker

这是本项目最重要的一步：**不要从头新装替代已跑通的环境**。因为旧容器可能在创建后手动安装过 TF2 修复版、libuvc、SDK 等；直接 `docker save` 原始基础镜像可能丢掉这些变化。Docker 官方明确指出 `commit` 保存容器可写层但**不保存挂载卷内容**，因此还必须导出源码。详见 [Docker commit](https://docs.docker.com/reference/cli/docker/container/commit/) / [Docker save](https://docs.docker.com/reference/cli/docker/image/save/)。

### 2.1 原机确认真实配置（宿主机，而非容器）

```bash
docker ps -a --filter name=x5car_dev
docker inspect x5car_dev --format \
  'image={{.Config.Image}} privileged={{.HostConfig.Privileged}} network={{.HostConfig.NetworkMode}} ipc={{.HostConfig.IpcMode}}'
docker inspect x5car_dev --format \
  '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}'
docker inspect x5car_dev --format \
  '{{range .HostConfig.Devices}}{{println .PathOnHost "->" .PathInContainer}}{{end}}'
```

**原机已提供的实测输出**（2026-10-29，原电脑 Ubuntu 24.04）：

```text
CONTAINER ID   IMAGE              STATUS         NAMES
590243484b1b   x5car_humble:dev   Up 2 hours     x5car_dev
image=x5car_humble:dev privileged=true network=host ipc=host
/dev -> /dev
/home/luca/ecust_ws -> /root/ecust_ws
/run/udev -> /run/udev
/tmp/.X11-unix -> /tmp/.X11-unix
HostConfig.Devices: (empty)
```

这里 **`HostConfig.Devices` 为空正是预期**：当前不是用 `--device=/dev/ttyACM0` 固定单个节点，而是 `privileged: true` + 整个 `/dev` **bind mount**。宿主机新增的设备节点可随插入/拔出在容器内变化。`/run/udev` 也独立绑定，供相机和传感器 SDK 查询宿主机的 udev 设备数据库。该配置允许在**容器已启动后**插入 USB、CH9102/ttyACM、UVC 相机并枚举新节点，不需要为每个新设备重建容器；但设备断开后，已有驱动程序是否能自动重新连接，需要分别测试（见 §6.5）。

交接应复现这四个 bind mount，而非改成某个固定 `/dev/ttyACM0` 的 `devices:` 配置。上面仅证实已显示的 inspect 字段；仍应在现场核对 `Mounts[].RW`、Docker 环境变量和真实插拔能力。

确认原容器内确已升级 TF2：

```bash
docker exec -it x5car_dev bash -lc \
  "dpkg-query -W 'ros-humble-tf2*' 'ros-humble-nav2-*' 2>/dev/null | head -100"
```

执行前审查容器内是否存在私人 SSH 密钥、云账户、API Key、浏览器缓存和带敏感信息的 shell 历史。**镜像提交无法确保清除曾被写进旧镜像层的秘密**；如果发现敏感信息应重建净化镜像，而不是直接分享。

### 2.2 一键导出（推荐）

把本包 `scripts/export_verified_environment.sh` 放到旧电脑上，**在宿主 Ubuntu 的普通用户终端**运行：

```bash
export CONTAINER=x5car_dev
export HOST_WS="$HOME/ecust_ws"
export OUT="$HOME/X5Car_verified_export"
bash scripts/export_verified_environment.sh
```

脚本要求明确输入 `YES`，随后：保存运行参数白名单、全部 dpkg 包版本、pip freeze、源码压缩包，`docker commit` 当前容器为 `x5car-humble:verified`，再 `docker save | gzip`，生成 SHA256 校验文件。它**不修改/删除原来的工作空间或容器**。建议在停止 Nav2、RViz2 进程后运行，减少进行中写入。

如果手工导出，等价核心步骤是：

```bash
# 原 PC 宿主机
mkdir -p "$HOME/X5Car_verified_export"
docker commit x5car_dev x5car-humble:verified
docker save x5car-humble:verified | gzip -1 > \
  "$HOME/X5Car_verified_export/X5CAR_DOCKER_IMAGE.tar.gz"
tar -czf "$HOME/X5Car_verified_export/WORKSPACE_SRC.tar.gz" \
  -C "$HOME/ecust_ws" src
cd "$HOME/X5Car_verified_export"
sha256sum X5CAR_DOCKER_IMAGE.tar.gz WORKSPACE_SRC.tar.gz > SHA256SUMS
```

**特别重要**：如果原 `x5car_dev` 启动时把 `/root/ecust_ws` 绑定到宿主文件夹，`commit` **不会**将这个目录的内容写进镜像。相反，容器里自行 `apt install` 的库通常进入可写层，被 `commit` 保留；`docker save` 再保存其镜像层。不要用 `docker export/import` 替代本流程，因为它不等同保存可恢复的原始镜像元数据。

### 2.3 源码 + 地图 + RDK 代码完整性

在 RDK 上保存的 `map.yaml`、`map.pgm` 应另行同步到电脑工作区，例：

```bash
# 原电脑宿主机；替换 RDK_IP
mkdir -p "$HOME/ecust_ws/src/x5car_slam/maps"
scp robotwzz@<RDK_IP>:/home/robotwzz/dev_ws/src/x5car_slam/maps/map.{yaml,pgm} \
  "$HOME/ecust_ws/src/x5car_slam/maps/"
```

注意 Shell 中 `<RDK_IP>` 必须**替换为实际地址，不要照抄尖括号**。旧资料出现过 `192.168.127.10`，后续调试又曾使用 `192.168.1.48`，因此文档不将任何旧地址写成现行固定值。

`map.yaml` 的 `image:` 必须指向真实文件（相对路径相对于 `map.yaml` 所在目录解析）。部署到 Docker 后路径可变，地图和图片务必一起迁移。

---

## 3. 接手电脑：Ubuntu 24.04 安装 Docker

本节**只在接手人的 Ubuntu 24.04 宿主机执行**，不是容器内。已有正常 Docker 则跳过安装：

```bash
sudo apt update
sudo apt install -y ca-certificates curl git openssh-client rsync x11-xserver-utils
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
sudo tee /etc/apt/sources.list.d/docker.sources >/dev/null <<EOF_DOCKER
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF_DOCKER
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker
sudo docker run --rm hello-world
```

可选：`sudo usermod -aG docker "$USER"`，重新登录后无须 `sudo docker`；注意 `docker` 组近似宿主 root 权限。官方说明见 [Docker Engine on Ubuntu](https://docs.docker.com/engine/install/ubuntu/)。

---

## 4. 接手电脑：加载镜像并恢复完整容器

在宿主机创建工作区，放入交接文件，并校验：

```bash
mkdir -p "$HOME/ecust_ws"
cd /path/to/X5Car_verified_export  # 改为交接压缩包所在目录
sha256sum -c SHA256SUMS
# 解压源码：检查 tar 内容后再恢复
tar -tzf WORKSPACE_SRC.tar.gz | head -40
tar -xzf WORKSPACE_SRC.tar.gz -C "$HOME/ecust_ws"
# 恢复包含已装依赖的镜像
docker load -i X5CAR_DOCKER_IMAGE.tar.gz
docker image ls | grep x5car-humble
```

交接包中的 `docker/compose.yaml` 是针对**全权限硬件直通**的恢复模板：

```yaml
services:
  x5car:
    image: x5car-humble:verified
    container_name: x5car_dev
    privileged: true
    network_mode: host
    ipc: host
    volumes:
      - ${HOST_WS:?Set HOST_WS}:/root/ecust_ws:rw
      - /tmp/.X11-unix:/tmp/.X11-unix:rw
      - /dev:/dev:rw
      - /run/udev:/run/udev:rw
    environment:
      - DISPLAY=${DISPLAY:?Set DISPLAY}
      - ROS_DOMAIN_ID=1
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
      - ROS_LOCALHOST_ONLY=0
```

实际上交付的 `compose.yaml` 还包含 `QT_X11_NO_MITSHM=1`、工作目录、后台常驻命令，**以附件文件为准**。这里的 `/dev:/dev` 意味着容器可以随宿主设备枚举看到新增的 `ttyUSB*`、`ttyACM*`、`video*`、`/dev/bus/usb/*` 等设备节点；`/run/udev:/run/udev` 给 libudev 与部分相机 SDK 提供设备属性和枚举信息。**这两个挂载都不能只放在 Dockerfile 里**，必须写入 Compose 或 `docker run` 启动参数。`privileged` 会进一步放宽容器安全限制。**仅当接手人接受这一高风险架构且开发电脑专用时使用**。一般开发场景应考虑收紧至 `/dev/bus/usb` 和指定串口；此次为了还原原用户的全设备调试能力，专门保留全量方案。

创建 `.env`，**不要直接提交到 Git**：

```bash
cd /path/to/X5Car_完整交接包_20261029/docker
cp .env.example .env
# 用编辑器将 HOST_WS 改为接手人自己的真实绝对路径
nano .env
```

### 4.1 宿主机 ↔ Docker 共享文件夹（双向读写，必须验证）

当前恢复模板使用 **bind mount**，不是 Docker 自己管理的匿名卷。`docker/.env` 决定宿主机目录，`compose.yaml` 的 `volumes` 将该目录直接映射到容器 `/root/ecust_ws`：

```bash
# 例子：宿主机真实用户名为 luca；按接手人实际用户名更改
# docker/.env 文件中写入：
HOST_WS=/home/luca/ecust_ws
```

```yaml
volumes:
  - ${HOST_WS:?Set HOST_WS in .env}:/root/ecust_ws:rw
```

| 宿主机路径（示例） | Docker 内路径 | 功能 |
|---|---|---|
| `/home/luca/ecust_ws/src/` | `/root/ecust_ws/src/` | 共享同一份项目源码、配置和地图 |
| `/home/luca/ecust_ws/build/` | `/root/ecust_ws/build/` | Docker 生成的编译中间文件，**不供宿主机 Jazzy 直接使用** |
| `/home/luca/ecust_ws/install/` | `/root/ecust_ws/install/` | Docker 编译的 Humble 安装产物，仅同架构、同环境使用 |
| `/home/luca/ecust_ws/log/` | `/root/ecust_ws/log/` | Colcon 构建日志 |

启动前，先在宿主机执行 `mkdir -p "$HOME/ecust_ws/src"`，并核对 `.env` 中的 `HOST_WS` 指向**已经恢复最新源码**的目录。特别注意：**如果 `HOST_WS` 配错，可能挂载一个空目录并遮蔽容器原有的 `/root/ecust_ws` 内容**；镜像里的文件不会自动搬到宿主机。容器导出 `docker commit` 也不会包含这个挂载目录，必须交付 `WORKSPACE_SRC.tar.gz`。

```bash
# 宿主机：确认 Docker 最终解析的绑定配置
cd /path/to/X5Car_完整交接包_20261029/docker
docker compose config | grep -A 10 'volumes:'
docker inspect x5car_dev --format '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}'

# 从容器实际挂载信息中读取宿主机目录，避免接手人使用非默认 HOST_WS 时误测
WS_DIR="$(docker inspect x5car_dev --format '{{range .Mounts}}{{if eq .Destination "/root/ecust_ws"}}{{.Source}}{{end}}{{end}}')"
test -d "$WS_DIR/src" || { echo "工作空间未挂载或源码缺失：$WS_DIR"; exit 1; }

# 宿主机 → Docker
printf 'shared-ok\n' > "$WS_DIR/SHARE_TEST.txt"
docker exec x5car_dev cat /root/ecust_ws/SHARE_TEST.txt

# Docker → 宿主机
docker exec x5car_dev sh -c 'printf "container-ok\n" > /root/ecust_ws/SHARE_BACK.txt'
cat "$WS_DIR/SHARE_BACK.txt"
rm -f "$WS_DIR/SHARE_TEST.txt" "$WS_DIR/SHARE_BACK.txt"
```

`/root/ecust_ws` 是 **PC 容器工作空间**；RDK 的 `/home/robotwzz/dev_ws` 是另一台机器上的目录，**不会自动共享**。RDK 与 PC 之间要用 Git、`rsync` 或 SCP 同步源码/地图，不要把双向绑定误认为跨机器共享。

X11 直通授权（宿主机、已登录图形桌面）：

```bash
printf 'DISPLAY=%s\n' "$DISPLAY"
xhost +si:localuser:root
# 确保 Docker Compose 在同一个有 DISPLAY 的宿主会话中执行
docker compose config >/dev/null
docker compose up -d
docker exec -it x5car_dev bash
```

在 Docker 内执行 `echo "$DISPLAY"; rviz2` 验证图形窗口。需要撤销授权时在宿主机执行 `xhost -si:localuser:root`，但容器可能需要重新授权才能运行 RViz2。**不要使用 `xhost +`（开放全部客户端访问 X Server）**。宿主若使用 Wayland，要保证 XWayland 正常；SSH 无桌面终端不能照搬本地 X11。

**如果重建后 `docker exec -it` 因 `/dev/pts`/TTY 异常失败**：先停止新容器，检查是否全 `/dev` 映射引发了宿主和容器的 PTY 冲突；临时移除 `/dev:/dev` 后重建，并用仅映射 `/dev/bus/usb` 和指定 tty/video 的方式调试。不要对有实机电机连接的环境盲目尝试。

---

## 5. 无镜像时的备用重建路线（不保证与原环境逐字一致）

优先使用第 2～4 章的“已验证镜像”。只有镜像丢失才使用 `docker/Dockerfile.rebuild`：

```bash
cd /path/to/X5Car_完整交接包_20261029/docker
docker build -f Dockerfile.rebuild -t x5car-humble:verified .
# 然后按第 4 章 .env 和 compose 启动
```

该后备 Dockerfile 使用 ROS 官方 Humble 基础镜像和 apt 安装常用库，并编译 libuvc；它不自动下载 X5Car 私有源码，也不能自动补齐厂商 ARM64 OpenNI 二进制 SDK。要严格复现，必须参考 `DPKG_ALL.txt` 和 SDK SHA 版本，优先恢复原镜像。

### 5.1 Docker 内安装依赖（手动/缺包时）

下列命令**只对已安装 ROS Humble 的 Ubuntu 22.04 容器或 RDK 执行**；如果镜像中已存在则不必重复安装：

```bash
apt update
apt install -y \
  build-essential cmake git curl wget pkg-config python3-pip \
  python3-colcon-common-extensions python3-rosdep python3-yaml \
  libeigen3-dev libusb-1.0-0-dev libudev-dev libserial-dev \
  libpcap-dev libpcl-dev libgflags-dev libgoogle-glog-dev libjpeg-dev \
  usbutils v4l-utils \
  ros-humble-ros2-control ros-humble-ros2-controllers \
  ros-humble-hardware-interface ros-humble-controller-manager \
  ros-humble-mecanum-drive-controller ros-humble-joint-state-broadcaster \
  ros-humble-imu-sensor-broadcaster ros-humble-teleop-twist-keyboard \
  ros-humble-robot-localization ros-humble-slam-toolbox \
  ros-humble-navigation2 ros-humble-nav2-bringup ros-humble-nav2-rviz-plugins \
  ros-humble-tf2-ros ros-humble-tf2-tools ros-humble-twist-mux \
  ros-humble-robot-state-publisher ros-humble-joint-state-publisher \
  ros-humble-joint-state-publisher-gui ros-humble-xacro \
  ros-humble-pcl-conversions ros-humble-pcl-ros \
  ros-humble-image-geometry ros-humble-camera-info-manager \
  ros-humble-image-transport ros-humble-image-publisher \
  ros-humble-cv-bridge ros-humble-image-pipeline \
  ros-humble-rviz2 ros-humble-rqt ros-humble-rqt-image-view \
  ros-humble-rmw-cyclonedds-cpp
```

**当前交接默认使用 Cyclone DDS**，所以 `ros-humble-rmw-cyclonedds-cpp` 是必需依赖，而非可选测试项。Fast DDS 仅可留作备用，正式运行时 Docker 与 RDK 都使用 `rmw_cyclonedds_cpp`。历史上单独切换 DDS 未解决 Footprint 冻结，现场反馈是在升级 TF2 后恢复，因此**不可把更换 DDS 说成 TF2 问题的修复原因**。

然后通过 `package.xml` 声明依赖补齐：

```bash
source /opt/ros/humble/setup.bash
[ -f /etc/ros/rosdep/sources.list.d/20-default.list ] || rosdep init
rosdep update
cd /root/ecust_ws
rosdep install --from-paths src --ignore-src --rosdistro humble -r -y
```

也可使用附件 `scripts/install_project_deps.sh /root/ecust_ws`。rosdep 不能保证解决厂商 SDK、手工 libuvc 和需要自定义 Git 源码的问题；错误应单独核查。参考 [ROS 2 rosdep 指南](https://docs.ros.org/en/humble/Tutorials/Intermediate/Rosdep.html)。

### 5.2 libuvc 与 Astra Pro

若 `ldconfig -p | grep libuvc` 无结果，可以在**目标架构机器上**安装：

```bash
apt update
apt install -y cmake build-essential git pkg-config libusb-1.0-0-dev libjpeg-dev
cd /tmp
git clone https://github.com/libuvc/libuvc.git
cmake -S /tmp/libuvc -B /tmp/libuvc/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/libuvc/build --parallel 2
cmake --install /tmp/libuvc/build
ldconfig
ldconfig -p | grep libuvc
```

**正式交付要记录 libuvc 的 commit SHA**，而不是永远跟随 Git 最新提交。Astra 相机还涉及 OpenNI SDK、架构对应 `.so` 和 udev 权限规则。用 `file <SDK路径>/*.so` 核对是 x86-64 还是 aarch64；跨架构库不能直接混用。摄像头的正确独立启动文件是：

```bash
ros2 launch astra_camera astra_pro.launch.xml
# 常见话题（以实际运行结果为准）：
ros2 topic hz /camera/color/image_raw
ros2 topic hz /camera/depth/image_raw
ros2 topic hz /camera/depth/points
```

旧测试中使用普通 `astra.launch.xml` 曾出现 RGB 异常，切换 `astra_pro.launch.xml` 才恢复 RGB。

---

## 6. USB、ACM、串口、相机、udev：配置在实际连接设备的 Linux 主机

### 6.1 先确认设备插在哪里

| 设备 | 常见节点 | 实际安装规则位置 |
|---|---|---|
| STM32 底盘控制串口 | `/dev/ttyUSB0`（示例） | RDK 物理连接时装在 RDK；电脑直连时装在电脑 Ubuntu 宿主 |
| N10（CH9102）雷达 | `/dev/ttyACM0` / `ttyCH343USB*` 或其他 | 同上；推荐固定软链接 `/dev/wheeltec_lidar` |
| Astra Pro 深度相机 | USB `2bc5:0403` | USB 接入所在宿主机 |
| Astra Pro RGB UVC | USB `2bc5:0501`、`/dev/video*` | USB 接入所在宿主机 |

先执行：

```bash
lsusb
ls -l /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video* 2>/dev/null
# 选择真实串口设备（示例 /dev/ttyACM0）：
udevadm info -a -n /dev/ttyACM0 | less
v4l2-ctl --list-devices
```

不要认为 Docker 内给 root 或 `--privileged` 就能改变 RDK 宿主机 USB 权限；udev 是**宿主机**上的设备管理机制。

### 6.2 N10 雷达稳定别名

历史数据：雷达 CH9102 USB ID `1a86:55d4`，N10 波特率 `230400`，接口可出现 ttyACM 或 ttyCH343USB。已提供模板 `udev/99-x5car-n10.rules`：

```udev
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d4", MODE="0660", GROUP="dialout", SYMLINK+="wheeltec_lidar"
```

在雷达物理连接的宿主机安装：

```bash
sudo install -m 0644 udev/99-x5car-n10.rules /etc/udev/rules.d/99-x5car-n10.rules
sudo usermod -aG dialout "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger
# 重新插拔雷达，重新登录令用户组生效
ls -l /dev/wheeltec_lidar
readlink -f /dev/wheeltec_lidar
```

旧 `wheeltec_udev.sh` 中还曾配置 `10c4:ea60` 的 CP2102 和 `ATTRS{serial}=="0001"`。**新规则应按当前实机 lsusb / udevadm 的真实枚举调整**。如果有多个相同 VID/PID 的串口，必须附加 serial 或物理路径等条件，不能让设备争抢 `/dev/wheeltec_lidar`。不推荐旧脚本一律 `0777` 权限。

雷达配置应检查 `x5car_bringup/config/lidar.yaml` 中：`lidar_model: N10`、`serial_port: /dev/wheeltec_lidar`、`laserscan_topic: /scan`。波特率由雷达驱动的实际启动配置/设备要求确认（历史 N10 实测为 230400），不要误套 M10P 500000 的旧值。

### 6.3 Astra Pro 相机权限

**优先使用项目自带的 Orbbec 厂商规则**，路径为：

```text
src/drivers/ros2_astra_camera/astra_camera/scripts/56-orbbec-usb.rules
```

在相机真实连接的 RDK 或电脑宿主机（在工作空间 `src/` 已放好后）：

```bash
cd <宿主机工作空间>/src/drivers/ros2_astra_camera/astra_camera
sudo bash scripts/install.sh
sudo udevadm control --reload-rules
sudo udevadm trigger
# 重新插拔相机后
lsusb -d 2bc5:
v4l2-ctl --list-devices
```

上面 `<宿主机工作空间>` 必须换成实际路径。另可用 `ASTRA_RULES=/实际路径/56-orbbec-usb.rules bash scripts/install_udev_on_host.sh` 同时安装雷达和相机规则。注意：提供的 Orbbec 厂商文件中含 `MODE:=0666`、`GROUP:=video` 一类宽松规则，生产环境应按组织设备安全策略审查后才部署；不应把整份厂商规则盲目简化成一个 USB ID，因为 Astra Pro 深度和 RGB 可能分别枚举。

### 6.4 Docker 全设备直通：完全按照原机已确认配置

已确认的实际配置为：`privileged=true`、`network=host`、`ipc=host`，绑定 `/dev:/dev`、`/run/udev:/run/udev`、`/tmp/.X11-unix:/tmp/.X11-unix`、宿主工作区 `/home/luca/ecust_ws:/root/ecust_ws`。其目的是让 Docker 在创建后也能识别新增 USB/ACM 串口、相机 `/dev/video*` 设备。**这不是只授权当前插着的设备**。

| 运行参数 | 是否已由用户 inspect 确认 | 用途 |
|---|---|---|
| `privileged: true` | 已确认 | 开放主机设备访问能力（安全风险很高） |
| `network_mode: host` | 已确认 | ROS 2 DDS 网络发现与通信 |
| `ipc: host` | 已确认 | 与主机共享 IPC 命名空间 |
| `/dev:/dev:rw` | 已确认两端路径；读写状态待核查 | 让新出现或重新编号的 USB/串口/视频设备节点可见 |
| `/run/udev:/run/udev:rw` | 已确认两端路径；读写状态待核查 | 供 libudev/相机 SDK 枚举宿主 udev 设备属性，实际是否必需依 SDK |
| `/tmp/.X11-unix:/tmp/.X11-unix` | 已确认 | X11 / RViz2 |
| `${HOST_WS}:/root/ecust_ws:rw` | 已确认原机具体宿主路径 | 源码、地图与编译结果双向共享 |
| `devices:` 无逐一设备条目 | 已确认 | 正常；无需提前知道 `ttyACM0`、`ttyUSB0`、`video0` 等节点名 |

如果未来有更严格的安全隔离需求，可以重新设计最小权限容器；**但不能把静态 `--device=/dev/ttyACM0` 当作原机的等价替代**。官方 Docker 文档也区分 `--device` 指定设备与 `--privileged` 全部设备访问能力。

### 6.5 USB / ACM / Astra Pro 热插拔验收（必测）

在**电脑宿主机**执行：

```bash
# 启动容器后，再插入新设备（避免仅验证容器创建时已存在的设备）
docker ps --filter name=x5car_dev
bash scripts/verify_device_passthrough.sh x5car_dev

# 可选：宿主机单独监听 udev 插拔事件（另开终端）
sudo udevadm monitor --kernel --udev
```

随后，**保持容器运行，不要重建或重启容器**，实际插入雷达或相机，分别比较宿主机与 Docker 中的枚举情况：

```bash
# 电脑宿主机
lsusb
ls -l /dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video* 2>/dev/null

# 电脑宿主机中查看容器
# 在容器内展开通配符，避免宿主机 shell 提前匹配：
docker exec x5car_dev sh -lc 'ls -l /dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video* 2>/dev/null || true'
docker exec x5car_dev ls -ld /run/udev /run/udev/data
```

**验收标准**：容器启动后才插入的设备，宿主和容器都能看到同一个新设备节点；`/dev/wheeltec_lidar` 符号链接存在且目标设备正确；相机的 USB 和视频节点可见，`/run/udev/data` 在容器中可读取。再进行实际驱动启动、数据发布测试。**看到节点 ≠ 驱动已经能正常打开或从 USB 断线后自动恢复**：在无机器人运行、无设备传输和电机安全隔离的情况下，另做拔插复测；如果 driver 没有实现 USB 热重连，需要重启相应 ROS 节点（不必重建 Docker）。

**udev 安装位置**：PC USB 直插则装在 Ubuntu 24.04 **PC 宿主机**；雷达/相机插 RDK 时则装在 **RDK Ubuntu**。在 Docker 内写 `/etc/udev/rules.d` 不会取代宿主机的规则管理。

---

## 7. ROS 2 DDS 通信及时间配置

当前生产/调试统一基线：**Cyclone DDS**。先在 Docker 和 RDK **分别**安装其 RMW 插件（已包含在验证镜像中的可跳过）：

```bash
sudo apt update
sudo apt install -y ros-humble-rmw-cyclonedds-cpp
# Docker 内通常是 root：去掉 sudo 即可
```

两边启动 ROS 节点的终端使用相同环境：

```bash
source /opt/ros/humble/setup.bash
export ROS_DOMAIN_ID=1
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_LOCALHOST_ONLY=0
unset CYCLONEDDS_URI
unset ROS_DISCOVERY_SERVER   # 当前项目不使用 Fast DDS Discovery Server
ros2 daemon stop
printf 'RMW=%s, DOMAIN=%s, LOCALHOST=%s\n' "$RMW_IMPLEMENTATION" "$ROS_DOMAIN_ID" "$ROS_LOCALHOST_ONLY"
ros2 pkg prefix rmw_cyclonedds_cpp
```

这组 `export` 要在 **RDK 的 `robot.launch.py` 启动终端**、**Docker 的 Nav2/AMCL 启动终端**、**Docker 的 RViz2 启动终端**都生效。Compose 会为容器进程注入相同的三项环境变量；也可在 RDK 的 ROS 专用启动脚本中写入，避免每次新开终端漏配。不要让宿主 Ubuntu 24.04 的 Jazzy 环境覆盖 Humble。Docker 和 RDK 的 RMW 名称虽然相同，但仍需保证两台机器网络可达、系统时间一致。

在宿主机检查 Docker 启动环境：

```bash
docker inspect x5car_dev --format '{{range .Config.Env}}{{println .}}{{end}}' | grep -E '^(RMW_IMPLEMENTATION|ROS_DOMAIN_ID|ROS_LOCALHOST_ONLY)='
```

**如果容器是改配置前创建的**，只修改 Compose 文件不会更新已有进程的环境，须在停止 ROS 进程后运行 `docker compose up -d --force-recreate`，并重新打开 Docker 内各终端。

- 电脑 Ubuntu 24.04 宿主若有 ROS Jazzy，不要在 Docker Humble shell 中同时 `source` 两套发行版。
- Docker 的 `--network host` 共享宿主网络栈，方便 DDS 跨机发现；仍要核实路由、UDP 和防火墙。仅 `ping` 通不代表 DDS 数据都能通。
- Docker 与 RDK 时钟应同步（`date -Ins`、`timedatectl status`）；若 AMCL/Nav2 都在 RDK，电脑时钟偏差主要影响 RViz2 可视化和带时间戳输入；若 AMCL/Nav2 在 PC，跨机时钟一致性直接影响定位与 TF。
- 切换 RMW 必须停止并重新启动相应 ROS 进程；不要在旧节点运行时仅修改 shell 变量。此前单独尝试 Cyclone DDS 未解决 Footprint 冻结，但用户现在已统一采用 Cyclone DDS，并在升级 TF2 后恢复正常。交付时应保留该组合，不要因为历史排障记录擅自切回 Fast DDS。

### 7.1 从 Docker 实际接收数据（RDK 硬件运行时）

```bash
ros2 node list --no-daemon
ros2 topic list -t --no-daemon
timeout 8s ros2 topic hz /scan
timeout 8s ros2 topic hz /odom
ros2 run tf2_ros tf2_echo odom base_link
```

地图需要订阅 `Transient Local`：

```bash
ros2 topic echo /map nav_msgs/msg/OccupancyGrid \
  --once --field info --qos-durability transient_local
```

---

## 8. 编译与验证：必须在两个架构分别编译

### 8.1 Docker x86_64 工作空间

```bash
docker exec -it x5car_dev bash
cd /root/ecust_ws
source /opt/ros/humble/setup.bash
colcon list --names-only
# 首次恢复：build/install/log 应是新的；执行完整构建
colcon build --executor sequential --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

注意：在 `source /root/ecust_ws/install/setup.bash` 的旧终端中**删除 `install/` 后继续编译**，容易残留 `AMENT_PREFIX_PATH` 指向已删除目录；建议新开干净 shell，只 source `/opt/ros/humble` 后构建。出现 `--allow-overriding` 参数不被 Colcon 识别时不要强加该选项：对于本项目自研包正常重新编译无需该参数。**不要编译下载的临时 `nav2_controller` / `nav2_costmap_2d` 源码；正式交付使用 apt 版本。**

内存紧张时：

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=1
export MAKEFLAGS=-j1
colcon build --executor sequential --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

核查来源：

```bash
ros2 pkg prefix x5car_navigation
ros2 pkg prefix x5car_bringup
ros2 pkg prefix lslidar_driver
ros2 pkg prefix astra_camera
ros2 pkg prefix nav2_controller
ros2 pkg prefix nav2_costmap_2d
dpkg-query -W 'ros-humble-tf2*'
```

预期 `x5car_*` 指向 `/root/ecust_ws/install/...`；`nav2_controller`、`nav2_costmap_2d` 指向 `/opt/ros/humble`。升级过的 TF2 **确切版本需要保存到交接快照**，在新的环境中不要随意降级。

### 8.2 RDK ARM64 实机编译

RDK 必须使用当前经过 watchdog 修复的本机源码：

```bash
cd /home/robotwzz/dev_ws
source /opt/ros/humble/setup.bash
export CMAKE_BUILD_PARALLEL_LEVEL=1
export MAKEFLAGS=-j1
colcon build --executor sequential --symlink-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

如果要从电脑向 RDK 同步，先做差异核查：

```bash
# PC 宿主机示例：必须把 IP 换成真实地址
rsync -av --dry-run "$HOME/ecust_ws/src/" \
  robotwzz@<RDK_IP>:/home/robotwzz/dev_ws/src/
# 确认不会覆盖板端 watchdog、安全命令、地图后，才能进行正式同步
```

不要在确认前添加 `--delete`，以免删除板端独有的地图和补丁。RDK 相机 SDK 必须用 ARM64 库。

---

## 9. 启动方式：单板运行 vs PC 分布式运行

**方案 A（已验证的基本架构）**：RDK 完整运行硬件/SLAM 或 Nav2，PC Docker 只启动 RViz2。两套方案互斥，不能同时启动两个 SLAM/AMCL/地图服务。

```bash
# RDK：仅硬件 + N10 雷达
ros2 launch x5car_bringup robot.launch.py mode:=hardware enable_lidar:=true
# RDK：SLAM 建图
ros2 launch x5car_bringup robot.launch.py mode:=mapping enable_lidar:=true
# RDK：加载已有地图导航
ros2 launch x5car_bringup robot.launch.py mode:=navigation enable_lidar:=true \
  map:=/home/robotwzz/dev_ws/src/x5car_slam/maps/map.yaml
```

**方案 B（用于减轻 RDK 计算压力的测试架构）**：RDK 只运行硬件，PC Docker 运行 Nav2 + AMCL + RViz2：

```bash
# RDK
ros2 launch x5car_bringup robot.launch.py mode:=hardware enable_lidar:=true

# PC Docker，另一个终端
ros2 launch x5car_navigation navigation.launch.py localization:=true \
  map:=/root/ecust_ws/src/x5car_slam/maps/map.yaml use_sim_time:=false

# PC Docker，再开终端，只运行一个 Humble RViz2
rviz2 -d /opt/ros/humble/share/nav2_bringup/rviz/nav2_default_view.rviz
```

方案 B 必须确认速度指令从 Docker 的 `/cmd_vel_navigation` 能通过 DDS 到达 RDK 的 `twist_mux`，以及 RDK 的 `/scan`、`/odom`、`/tf` 正常被 PC 接收；本模式下**不能在 RDK 再启动导航**。

RViz2 设置：`Fixed Frame = map`（定位后）；Map `/map` 的 Durability 选 `Transient Local`；LaserScan `/scan`；RobotModel 从 `/robot_description` 读取；使用 `2D Pose Estimate` 设置 AMCL 初始位姿，再发送导航目标。Humble 官方 RViz2 Nav2 面板可以使用 `Navigation 2`，不要加载其他 ROS 版本的 `Selector` / `Docking` 面板（历史环境曾报插件缺失）。

---

## 10. 实机验收与安全停机

测试前把机器人放在开阔、安全、可立即断电的区域；首次检查优先架空车轮或关闭电机使能；完成生命周期及 TF 验证后再测试低速移动。**不要绕开 `twist_mux` 或取消硬件看门狗、轮速安全限制。**

### 10.1 RDK 硬件验收

```bash
ros2 control list_hardware_components
ros2 control list_controllers
ros2 topic hz /odom
ros2 topic hz /joint_states
ros2 topic hz /scan
ros2 run tf2_ros tf2_echo odom base_link
```

要求 `mecanum_controller`、`joint_state_broadcaster`、`imu_sensor_broadcaster` 均 `active`；/odom 约 50 Hz、/scan 约 10 Hz 是先前测到的参考值，不是绝对性能指标。

### 10.2 定位和导航验收

```bash
ros2 lifecycle get /map_server
ros2 lifecycle get /amcl
ros2 lifecycle get /controller_server
ros2 lifecycle get /planner_server
ros2 run tf2_ros tf2_echo map odom
ros2 topic info /local_costmap/published_footprint -v
```

AMCL `active` 不等于已经定位成功；先完成 2D Pose Estimate、确认 scan 与地图障碍物对齐。向远离障碍物和膨胀区的可达目标发送导航指令，观察全局路径、局部路径和控制速度。**出现“瞬间 Goal succeeded”时不得视为通过**；历史 Humble 的 `ControllerServer::isGoalReached()` 曾在 TF 转换失败后仍检查目标，用户此前临时源码修补过，但正式代码是否包含该修复需要以镜像与 apt 版本为准。

**特别验收 TF2 升级后的稳定性**：之前出现过“外部 `/tf`、`tf2_echo` 正常，Costmap 的 `published_footprint` 以 5 Hz 发布但时间戳保持数分钟不变”问题。用户近期反馈升级 TF2 后恢复。至少运行 10～20 分钟，比较：

```bash
for i in 1 2 3; do
  date +%s.%N
  timeout 5s ros2 topic echo /local_costmap/published_footprint \
    --qos-durability volatile --once --field header
  sleep 10
done
```

轮廓消息时间戳应持续变化且与当前系统时间大致接近（以实际更新策略核验），不能长期冻结。不要用大幅扩大 TF tolerance 或手动伪造时间戳掩盖故障。

### 10.3 操作中止与清理

导航失败或出现过期 TF 时取消目标、停止运动控制，检查机器人是否已静止；必须保留物理急停。关闭顺序通常先取消导航与关闭导航启动进程，再关闭硬件控制节点，最后断开硬件供电。关闭 Docker 容器前保留必要的运行日志。

---

## 11. 已知历史问题、影响与交接处理

| 现象 | 已有经验与应对 |
|---|---|
| `No map received`，而 `/map` echo 正常 | RViz2 `Map` 的 Durability 使用 `Transient Local`；`Fixed Frame` 在完成定位后用 `map` |
| SLAM 正常，导航时无 `map → odom` | SLAM Toolbox 和 AMCL 的变换来源不同；检查 AMCL active、初始位姿、Map Server、激光及 TF |
| 目标点击后立即 `Goal succeeded`，车没动 | 历史 TF 过期时触发 Humble 目标检查逻辑异常；升级 TF2 后必须回归测试；不要视为正常到达 |
| Nav2 局部/全局 Footprint 时间戳冻结 | 历史表现为外部 TF 正常、内部缓存陈旧。更换 Fast/Cyclone DDS 未解决，用户反馈升级 TF2 后恢复。保存准确软件版本 |
| `change_state` 服务超时，Controller inactive，规划节点 unconfigured | 生命周期管理器未完成启动；不要仅依据 `Creating Costmap` 判断已激活；检查 `ros2 lifecycle get` |
| DWB 控制周期超时 | RDK 负载可能偏高，现场曾测试 10 Hz / 降采样。PC 运行 Nav2 可做性能对照；不是所有超时都是 CPU 不足 |
| N10 无 `/scan` / 设备名变化 | 查看 USB ID、雷达型号、udev 稳定链接及 baud、`lidar.yaml` |
| Astra 深度正常但 RGB 异常 | 使用 `astra_pro.launch.xml`，核对 RGB UVC 的 USB ID 与 libuvc |
| 控制循环启动即报 100ms watchdog | 板端需有“首次 `read()` 才开始计时”的修复，运行中的 100ms 保护继续有效 |
| 非法轮速导致控制器故障 | 使用板端“发送零速且保持 active”的安全处理版本，并校验 wheel speed 真正上限 |
| `colcon build` 同名 underlay 覆盖警告 | 不要提前 source 已被删除或旧 install；使用干净终端。旧 Colcon 不一定支持 `--allow-overriding` |
| 多个 RViz2 窗口造成资源占用 | `ps -C rviz2 -o pid,stat,cmd`，只保留一个；`Z` 进程不能用 kill 消除，需要父进程回收 |

---

## 12. 故障定位命令速查

```bash
# ROS 进程与节点
ps -C rviz2 -o pid,ppid,stat,%cpu,%mem,cmd
ros2 node list --no-daemon
ros2 topic info /tf -v
ros2 topic info /initialpose -v

# 时间与生命周期
date +%s.%N
timedatectl status
ros2 lifecycle get /amcl
ros2 lifecycle get /controller_server
ros2 lifecycle get /planner_server

# 动态 TF 与轮廓
ros2 run tf2_ros tf2_echo odom base_link
ros2 run tf2_ros tf2_echo map odom
ros2 topic echo /local_costmap/published_footprint \
  --qos-durability volatile --once --field header

# 设备、依赖
lsusb
v4l2-ctl --list-devices
ls -l /dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
ldconfig -p | grep libuvc
dpkg-query -W 'ros-humble-tf2*'
```

---

## 13. 接手验收表（每项提供截图或命令输出）

- [ ] 宿主 Ubuntu 24.04、Docker 和 Compose 正常，镜像与源码 SHA256 校验通过。
- [ ] `docker inspect` 中 `privileged=true`、`network=host`、`ipc=host`、四个 bind mount（`/dev`、`/run/udev`、X11、工作区）与原机一致，`Devices` 为空是正常状态。
- [ ] 在 Docker 启动之后再插 USB/ACM/相机，容器无需重建就能看到新增节点；udev 稳定别名存在，驱动热重连能力单独验收。
- [ ] Docker 内 Ubuntu 22.04、ROS Humble、已升级 TF2 的版本与原机快照一致。
- [ ] `src/` 自研包及雷达/相机驱动齐全；无临时 TF 诊断包覆盖；完整 colcon 编译成功。
- [ ] RDK ARM64 原生编译成功；已核对 watchdog `.cpp/.hpp`、轮速安全处理；实际串口正确。
- [ ] N10 物理连接主机已装 udev 规则，`/dev/wheeltec_lidar` 指向正确的设备。
- [ ] Astra 深度与 RGB 的设备权限和 SDK 架构正确，使用 `astra_pro.launch.xml` 完成测试。
- [ ] X11 / `DISPLAY` / `xhost +si:localuser:root` 正常；RViz2 只运行一个实例。
- [ ] PC Docker ↔ RDK DDS 话题发现、`/scan`、`/odom`、动态 `/tf` 实际可接收。
- [ ] SLAM 可建图并保存最新 `map.pgm/map.yaml`；独立导航可加载该地图。
- [ ] Map Server、AMCL、Controller、Planner 等生命周期处于 `active`；机器人完成定位，`map → odom` 正常。
- [ ] Footprint 时间戳不再停滞，连续稳定性测试通过；导航目标执行和失败处理均合理。
- [ ] 完成风险交底：高权限容器仅可信机器使用；急停、速度限制和现场操作人已明确。

---

## 14. 正式交付时填写的元信息（待交接人现场填写）

| 字段 | 交付填写 |
|---|---|
| 原机 Docker 实测基础镜像 | `x5car_humble:dev`（2026-10-29 inspect） |
| 原机 Docker ID | `590243484b1b`（临时 ID，仅用于对应原机记录） |
| 原机 Docker Image ID / 保存归档 SHA256 | ______ |
| 原机 Docker 容器启动参数 `RUNTIME_CONFIGURATION.txt` | `privileged=true`、host network/IPC、`/dev`、`/run/udev`、X11、宿主 `/home/luca/ecust_ws` 绑定（原机已确认）；其他细节待导出 |
| TF2 / Nav2 / ros2_control apt 精确版本 | ______ |
| Astra SDK、libuvc 版本/SHA 与 ARM64 兼容情况 | ______ |
| X5Car 完整源码 Git commit 或源码 SHA256 | ______ |
| RDK 最新 watchdog + 轮速安全代码 SHA256 | ______ |
| 宿主工作空间目录 / DISPLAY / 主机 IP / RDK IP | ______ |
| STM32 串口、N10 串口、VID/PID、设备序列号 | ______ |
| 地图文件、地图原点/分辨率/图像大小 | ______ |
| 实际 DWB 参数、最高安全轮速、电机急停方式 | ______ |
| 验收日期、接手人及现场负责人 | ______ |

---

## 15. 参考资料与交接来源

**本项目资料**：旧版《X5Car_Docker_开发环境部署与交接手册》(2026-10-28)；用户上传的 `src.zip`（2026-10-28 的源码快照，非最终板端补丁）；`apply_x5car_watchdog_fix(2).py`；本次调试中的 TF2、AMCL、Nav2、Costmap 运行日志。未核对到的原主机镜像 ID、RDK 最终源码 Git SHA、最新 TF2 确切版本，均如实标记“待交接填写”。

**官方文档**：

- [Docker Engine Ubuntu 安装](https://docs.docker.com/engine/install/ubuntu/)
- [Docker commit（不包含已挂载卷）](https://docs.docker.com/reference/cli/docker/container/commit/)
- [Docker save / Docker load](https://docs.docker.com/reference/cli/docker/image/save/)
- [Docker run：privileged 与 --device](https://docs.docker.com/reference/cli/docker/container/run)
- [Docker bind mounts：读写与路径映射](https://docs.docker.com/engine/storage/bind-mounts/)
- [Docker Compose services 与 privileged](https://docs.docker.com/reference/compose-file/services/)
- [ROS 2 Humble rosdep](https://docs.ros.org/en/humble/Tutorials/Intermediate/Rosdep.html)
- [ROS 2 Humble 文档](https://docs.ros.org/en/humble/)
- [systemd udev 规则](https://www.freedesktop.org/software/systemd/man/latest/udev.html)
- [libuvc 源码与安装](https://github.com/libuvc/libuvc)

> **最终原则**：**优先使用原容器镜像保真恢复；同时单独交付当前源码与地图；所有 udev 规则在物理宿主机安装；RDK 与 Docker 架构分别编译；正式运行前完整验收 TF、生命周期、轮速与物理安全保护。**
