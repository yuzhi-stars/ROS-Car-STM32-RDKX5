# 首次公开发布

## 发布内容

固件从原始 `ROS_car_V1.zip` 提取，保留业务源码、HAL/CMSIS、CMake / CubeMX / Keil 工程、测试、公共 VS Code 配置和文档；排除生成产物、历史备份和个人工具配置。ROS 2 源码从 `WORKSPACE_SRC.tar.gz` 提取到 `RDKX5Car/src/`，原始源码归档保持不变。PCB、机械结构、整车照片及部署资料一并纳入。

`docker/.env` 使用者自行从 `.env.example` 创建。输入资料中的 `/home/zzw`、`/home/robotwzz` 等是原部署路径，按自己的用户名和设备调整。

## Docker 镜像下载与校验

电脑端 `linux/amd64` 镜像已补充发布到 [v1.0.0 Release](https://github.com/goatwzz/ROS-Car-STM32-RDKX5/releases/tag/v1.0.0)。镜像标签为 `x5car-humble:verified`，归档大小 2,987,007,758 字节。由于 GitHub 单个附件限制为 2 GiB，原文件按字节拆成三个分片，未重新压缩。

从 Release 下载以下文件到同一目录（如 `RDKX5Car/archives/`）：

- `X5CAR_DOCKER_IMAGE.tar.gz.part00`
- `X5CAR_DOCKER_IMAGE.tar.gz.part01`
- `X5CAR_DOCKER_IMAGE.tar.gz.part02`
- `IMAGE_PARTS_SHA256SUMS`
- `IMAGE_SHA256SUMS`

在 Linux 中执行，只有校验通过后才导入：

```bash
set -e
sha256sum -c IMAGE_PARTS_SHA256SUMS
cat X5CAR_DOCKER_IMAGE.tar.gz.part00 X5CAR_DOCKER_IMAGE.tar.gz.part01 X5CAR_DOCKER_IMAGE.tar.gz.part02 > X5CAR_DOCKER_IMAGE.tar.gz
sha256sum -c IMAGE_SHA256SUMS
gzip -t X5CAR_DOCKER_IMAGE.tar.gz
docker load -i X5CAR_DOCKER_IMAGE.tar.gz
docker image inspect x5car-humble:verified --format '{{.Id}} {{.Os}}/{{.Architecture}}'
```

预期完整文件 SHA-256：

```text
1c9354745bb0d209e7efc3dd0edf051d965235fc9a9777bfcbe30c9b0ae6eef6
```

预期镜像 ID：`sha256:a5f4f2f6df2a1a65f71d59e04e6a68a9ac33700963de39325bc207e4476a73a7`，平台 `linux/amd64`。

2026-11-01 重新核对时，本机保存的原始归档与历史 SHA-256 一致，并通过 gzip CRC/长度、tar 解析、全部 16 个 blob 的 SHA-256、OCI 清单引用与大小、Docker load、独立容器基础 ROS 检查和分片合并校验。此前检查的 U 盘副本 SHA-256 为 `6ac1ef2b83a5439bcd22791f4d41635a1390b1873c4dfb65be69f6afcd0220b6`，且 gzip CRC 失败，未用于本次发布。

详细结果见[镜像验证记录](IMAGE_VALIDATION.json)；Release 同时附带 `IMAGE_DOWNLOAD.md` 和 `IMAGE_VALIDATION.json`。RDK X5 使用 ARM64 原生 ROS 工作空间，不导入电脑 x86_64 镜像。

## 可选：电脑端容器重建

在 Ubuntu x86_64 电脑上，从仓库根目录执行：

```bash
mkdir -p ~/ecust_ws
cp -a RDKX5Car/src ~/ecust_ws/
docker build --platform linux/amd64 -t x5car-humble:verified   -f RDKX5Car/docker/Dockerfile.rebuild RDKX5Car/docker
cp RDKX5Car/docker/.env.example RDKX5Car/docker/.env
# 编辑 .env，把 HOST_WS 改成自己工作空间的绝对路径
cd RDKX5Car/docker
# 在图形桌面终端中，确认 DISPLAY 和 X11 权限后执行
docker compose up -d
```

现有 Compose 引用 `x5car-humble:verified`，上面的标签仅用于兼容配置；重建镜像不等于原始已验证镜像，也未在本次发布中执行 Docker 构建或实机验证。随后按[电脑端部署教程](../RDKX5Car/docs/README.md)在容器中安装依赖、编译并配置传感器。RDK X5 使用 ARM64 原生 ROS 工作空间，不导入电脑 x86_64 镜像。

推荐优先使用上面发布的有效镜像。当前操作教程中的“导入镜像 / 完整快照恢复”步骤可在下载、合并并完成校验后执行；如需恢复脚本，将完整镜像与 `WORKSPACE_SRC.tar.gz` 放在同一快照目录，并先校验源码及镜像。参考目录中的历史记录保留原文。
