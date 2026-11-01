# 部署归档

本次公开发布包含 `WORKSPACE_SRC.tar.gz`，并在 `../src/` 提供完全相同的展开源码。校验：

```bash
cd RDKX5Car/archives
sha256sum -c SHA256SUMS
```

`ORIGINAL_SHA256SUMS.txt` 保留输入资料的历史校验记录，不能用于校验本目录当前发布内容。

原始 `X5CAR_DOCKER_IMAGE.tar.gz` 的实际 SHA-256 与历史记录不一致，gzip CRC 校验失败，因此没有上传。`restore_verified_environment.sh` 需要完整有效镜像，当前不能直接执行该恢复路径。请使用[公开版部署说明](../../docs/RELEASE.md)中的重建方式，或重新导出并校验镜像后再恢复。
