# 部署归档

Git 仓库包含 `WORKSPACE_SRC.tar.gz`，并在 `../src/` 提供完全相同的展开源码。源码校验：

```bash
cd RDKX5Car/archives
sha256sum -c SHA256SUMS
```

电脑 x86_64 Docker 镜像以三个分片发布在 [v1.0.0 Release](https://github.com/goatwzz/ROS-Car-STM32-RDKX5/releases/tag/v1.0.0)，需要单独下载，不包含在 Git 仓库或 Source code 下载包中。完整下载、合并、校验和导入步骤见[发布说明](../../docs/RELEASE.md)。本目录的 `IMAGE_PARTS_SHA256SUMS` 用于分片校验，`IMAGE_SHA256SUMS` 用于合并后的完整镜像校验。

`ORIGINAL_SHA256SUMS.txt` 保留历史记录。本次找到的本机镜像副本与其中的历史镜像 SHA-256 一致，已通过 gzip、镜像内容、Docker 导入和基础 ROS 运行检查；此前 U 盘损坏副本未发布。

若使用 `restore_verified_environment.sh`，先完成镜像合并与两级 SHA-256 校验，再将完整 `X5CAR_DOCKER_IMAGE.tar.gz` 与 `WORKSPACE_SRC.tar.gz` 放在同一快照目录；源码仍用 `SHA256SUMS` 校验。恢复脚本不会替代上述镜像分片及完整文件校验。
