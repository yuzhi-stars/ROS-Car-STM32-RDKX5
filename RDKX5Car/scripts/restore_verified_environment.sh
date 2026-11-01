#!/usr/bin/env bash
# 运行地点：接手者 Ubuntu 24.04 宿主机；从原机导出的快照目录恢复镜像和 src。
# 不自动启动容器，不覆盖现存 src，不删除任何现存文件。
set -euo pipefail
SNAPSHOT="${1:-}"
[[ -n "$SNAPSHOT" ]] || { echo '用法: bash restore_verified_environment.sh /path/to/X5Car_verified_export'; exit 2; }
[[ -f "$SNAPSHOT/X5CAR_DOCKER_IMAGE.tar.gz" && -f "$SNAPSHOT/WORKSPACE_SRC.tar.gz" ]] || {
  echo '快照目录中缺少镜像或源码文件' >&2; exit 1;
}
HOST_WS="${HOST_WS:-$HOME/ecust_ws}"
[[ ! -e "$HOST_WS/src" ]] || { echo "拒绝覆盖现有目录: $HOST_WS/src；请先人工备份或使用新的 HOST_WS" >&2; exit 1; }
if [[ -f "$SNAPSHOT/SHA256SUMS" ]]; then (cd "$SNAPSHOT" && sha256sum -c SHA256SUMS); fi
mkdir -p "$HOST_WS"
# 防误操作：只解压交接脚本打包的以 src/ 开头的归档。
if tar -tzf "$SNAPSHOT/WORKSPACE_SRC.tar.gz" | grep -Ev '^src(/|$)' | grep -q .; then
  echo '发现 src/ 之外的归档路径，拒绝自动解压' >&2; exit 1
fi
printf '将镜像导入 Docker 并将源码恢复至 %s。\n' "$HOST_WS"
read -r -p '确认恢复？输入 YES: ' confirm
[[ "$confirm" == YES ]] || exit 1
docker load -i "$SNAPSHOT/X5CAR_DOCKER_IMAGE.tar.gz"
tar -xzf "$SNAPSHOT/WORKSPACE_SRC.tar.gz" -C "$HOST_WS"
echo '恢复完成。下一步：配置 docker/.env、X11，并 docker compose up -d。'
