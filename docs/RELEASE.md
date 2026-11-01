# 首次公开发布

## 发布内容

固件从原始 `ROS_car_V1.zip` 提取，保留业务源码、HAL/CMSIS、CMake / CubeMX / Keil 工程、测试、公共 VS Code 配置和文档；排除生成产物、历史备份和个人工具配置。ROS 2 源码从 `WORKSPACE_SRC.tar.gz` 提取到 `RDKX5Car/src/`，原始源码归档保持不变。PCB、机械结构、整车照片及部署资料一并纳入。

`docker/.env` 使用者自行从 `.env.example` 创建。输入资料中的 `/home/zzw`、`/home/robotwzz` 等是原部署路径，按自己的用户名和设备调整。

## Docker 镜像状态

输入镜像 `X5CAR_DOCKER_IMAGE.tar.gz` 大小 2,987,007,758 字节。校验结果：

```text
历史 SHA-256: 1c9354745bb0d209e7efc3dd0edf051d965235fc9a9777bfcbe30c9b0ae6eef6
实际 SHA-256: 6ac1ef2b83a5439bcd22791f4d41635a1390b1873c4dfb65be69f6afcd0220b6
gzip: CRC check failed
```

此镜像未公开发布。本地输入文件保持原样；重新从可用环境导出并通过 SHA-256、gzip 和 Docker load 检查后再发布。新镜像若超过单个附件 2 GiB，需拆分或使用镜像仓库，见 [GitHub Releases 限制](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases#storage-and-bandwidth-quotas)。

## 电脑端容器重建

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

当前操作教程中的“导入镜像 / 完整快照恢复”步骤需要新的有效镜像；本公开版使用以上重建路径。参考目录中的历史记录保留原文。
