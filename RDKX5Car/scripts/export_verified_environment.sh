#!/usr/bin/env bash
# 在原 Ubuntu 24.04 宿主机运行；需有可用 Docker CLI。不会删除原容器。
set -euo pipefail
CONTAINER="${CONTAINER:-x5car_dev}"
HOST_WS="${HOST_WS:-$HOME/ecust_ws}"
OUT="${OUT:-$HOME/X5Car_verified_export_$(date +%Y%m%d_%H%M%S)}"
IMAGE="${IMAGE:-x5car-humble:verified}"
command -v docker >/dev/null
command -v sha256sum >/dev/null
[[ -d "$HOST_WS/src" ]] || { echo "未找到 $HOST_WS/src，先指定 HOST_WS" >&2; exit 1; }
docker container inspect "$CONTAINER" >/dev/null
# 避免导出错工作空间：必须与容器实际 bind mount 的 /root/ecust_ws 完全匹配。
MOUNTED_WS="$(docker inspect "$CONTAINER" --format '{{range .Mounts}}{{if eq .Destination "/root/ecust_ws"}}{{.Source}}{{end}}{{end}}')"
if [[ -z "$MOUNTED_WS" || "$(realpath "$MOUNTED_WS")" != "$(realpath "$HOST_WS")" ]]; then
  echo "错误：HOST_WS=$HOST_WS 与容器 /root/ecust_ws 的实际绑定目录 [$MOUNTED_WS] 不一致" >&2
  echo '请先 export HOST_WS=实际宿主路径，切勿在错误目录打包旧版源码。' >&2
  exit 1
fi
mkdir -p "$OUT"
printf 'Export location: %s\n' "$OUT"
echo '重要：镜像可能包含 Docker 层中的私钥/口令；源码和 SDK 可能包含受限文件。'
echo '请先停止 Nav2/硬件进程、审查敏感数据，再继续。'
echo '验证过的动态设备直通应含：privileged=true、/dev -> /dev、/run/udev -> /run/udev。'
read -r -p '确认允许打包此容器及工作空间？请输入 YES: ' confirm
[[ "$confirm" == YES ]] || { echo '已取消'; exit 1; }
{
 echo "container=$CONTAINER"; echo "image=$IMAGE"; echo "workspace=$HOST_WS";
 docker inspect "$CONTAINER" --format 'base_image={{.Config.Image}} privileged={{.HostConfig.Privileged}} network={{.HostConfig.NetworkMode}} ipc={{.HostConfig.IpcMode}}'
 docker inspect "$CONTAINER" --format '{{range .Mounts}}{{println "mount:" .Type .Source "->" .Destination "rw=" .RW "propagation=" .Propagation}}{{end}}'
 docker inspect "$CONTAINER" --format '{{range .HostConfig.Devices}}{{println "device:" .PathOnHost "->" .PathInContainer}}{{end}}'
 docker inspect "$CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | grep -E '^(ROS_DOMAIN_ID|RMW_IMPLEMENTATION|ROS_LOCALHOST_ONLY|DISPLAY|QT_X11_NO_MITSHM)=' || true
 echo 'note=未保存全部环境变量以防泄露凭据；需单独移交 xhost 授权命令。' 
} > "$OUT/RUNTIME_CONFIGURATION.txt"
docker exec "$CONTAINER" dpkg-query -W > "$OUT/DPKG_ALL.txt"
docker exec "$CONTAINER" bash -lc 'python3 -m pip freeze 2>/dev/null || true' > "$OUT/PIP_FREEZE.txt" || true
docker exec "$CONTAINER" bash -lc 'uname -m; cat /etc/os-release | head -6; env | grep -E "^(ROS_DOMAIN_ID|RMW_IMPLEMENTATION|ROS_LOCALHOST_ONLY)=" || true' > "$OUT/ROS_ENV_INFO.txt" || true
# 保存包清单与工作空间源码，明确排除 build/install/log。源代码中可能含 SDK、许可证、隐私数据，需自行审查。
tar --exclude='*/.git/objects' -czf "$OUT/WORKSPACE_SRC.tar.gz" -C "$HOST_WS" src
# 把当前容器可写层中的增量 apt/源码安装打包成镜像，然后压缩导出。
docker commit "$CONTAINER" "$IMAGE" >/dev/null
docker image save "$IMAGE" | gzip -1 > "$OUT/X5CAR_DOCKER_IMAGE.tar.gz"
(
 cd "$OUT"
 sha256sum X5CAR_DOCKER_IMAGE.tar.gz WORKSPACE_SRC.tar.gz > SHA256SUMS
)
cat <<MSG
交接镜像与源码已导出到 $OUT
注意：尚需从交接模板带上 compose.yaml、udev 规则、交接手册；也要单独核对 RDK 板端补丁、map.yaml 与 map.pgm。
不要把未经敏感信息审查的整个 OUT 上传公共网盘。
MSG
