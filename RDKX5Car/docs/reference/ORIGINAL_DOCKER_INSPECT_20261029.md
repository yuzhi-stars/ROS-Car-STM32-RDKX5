# 原电脑 Docker 运行参数现场记录（2026-10-29）

数据来源：用户在 Ubuntu 24.04 宿主机 `luca@luca` 实际运行 `docker inspect x5car_dev` 后提供；以下为可复核摘要。

```text
CONTAINER ID   IMAGE              COMMAND                 CREATED      STATUS        NAMES
590243484b1b   x5car_humble:dev   "/ros_entrypoint.sh …" 2 days ago   Up 2 hours    x5car_dev
image=x5car_humble:dev privileged=true network=host ipc=host
/dev -> /dev
/home/luca/ecust_ws -> /root/ecust_ws
/run/udev -> /run/udev
/tmp/.X11-unix -> /tmp/.X11-unix
HostConfig.Devices: (empty)
```

## 解释与恢复约束

- 这是整个宿主 `/dev` 的 **bind mount** 加 `privileged=true`，不是指定启动时已存在的 `ttyACM0`、`ttyUSB0`、`video0` 等单个设备。因此在 Docker 运行后插入新设备，新增节点应在容器内可见。
- `/run/udev` 挂载提供宿主 udev 数据库给 `libudev` / 某些相机、USB SDK；实际宿主 udev 规则仍应在**设备物理连接的主机**安装和运行。
- `HostConfig.Devices` 为空是预期，不代表设备不能访问；`--device` 列表不该被当作检验这个容器设备访问的唯一依据。
- 用户没有展示 `Mounts[].RW`、挂载传播标志、容器 `DISPLAY` 的具体值，因此交接 Compose 按典型 `:rw` 模式还原，但需在原机补充核对，不能宣称每个选项都已实测。
- 当前容器原始镜像为 `x5car_humble:dev`。按脚本 `docker commit` 后另行命名 `x5car-humble:verified`，给新电脑导入恢复使用；这两个标签用途不同。
- 即使能实时看到插拔，已有 ROS 驱动是否自动关闭并重新打开设备必须单独实测，不能仅由上述参数推定。

## 原机补充核对命令

```bash
docker inspect x5car_dev --format '{{range .Mounts}}{{printf "%s -> %s RW=%t propagation=%s\n" .Source .Destination .RW .Propagation}}{{end}}'
docker inspect x5car_dev --format '{{range .Config.Env}}{{println .}}{{end}}' | grep -E '^(ROS_DOMAIN_ID|RMW_IMPLEMENTATION|ROS_LOCALHOST_ONLY|DISPLAY|QT_X11_NO_MITSHM)='
```

注意：如设备实际接在 RDK X5 而不是 PC，则该设备的 udev 规则必须装在 RDK 上，PC Docker 通过 ROS DDS 话题接收数据，不能把远端 RDK 的 `/dev` 当成宿主机本地设备。
