#!/usr/bin/env bash
# 在 Ubuntu 电脑宿主机上执行；不会修改容器配置或操作设备。
# 用法：bash scripts/verify_device_passthrough.sh [容器名，默认 x5car_dev]
set -euo pipefail
CONTAINER="${1:-x5car_dev}"
command -v docker >/dev/null

echo '== Docker 真实运行配置 =='
docker inspect "$CONTAINER" --format 'image={{.Config.Image}} privileged={{.HostConfig.Privileged}} network={{.HostConfig.NetworkMode}} ipc={{.HostConfig.IpcMode}}'
docker inspect "$CONTAINER" --format '{{range .Mounts}}{{println .Source "->" .Destination "rw=" .RW}}{{end}}'
echo '== 逐一 --device 项（可为空，整目录 /dev 绑定时属正常）=='
docker inspect "$CONTAINER" --format '{{range .HostConfig.Devices}}{{println .PathOnHost "->" .PathInContainer}}{{end}}'

privileged="$(docker inspect "$CONTAINER" --format '{{.HostConfig.Privileged}}')"
network="$(docker inspect "$CONTAINER" --format '{{.HostConfig.NetworkMode}}')"
ipc="$(docker inspect "$CONTAINER" --format '{{.HostConfig.IpcMode}}')"
[[ "$privileged" == true ]] || echo 'WARNING: privileged 不为 true'
[[ "$network" == host ]] || echo 'WARNING: 网络模式不是 host'
[[ "$ipc" == host ]] || echo 'WARNING: IPC 模式不是 host'

for path in /dev /run/udev /tmp/.X11-unix /root/ecust_ws; do
  source_path="$(docker inspect "$CONTAINER" --format "{{range .Mounts}}{{if eq .Destination \"$path\"}}{{.Source}}{{end}}{{end}}")"
  if [[ -n "$source_path" ]]; then
    printf 'PASS %-18s <- %s\n' "$path" "$source_path"
  else
    printf 'FAIL %-18s 未绑定到宿主机\n' "$path"
  fi
done

echo '== PC 宿主机设备当前快照 =='
lsusb 2>/dev/null || true
shopt -s nullglob
nodes=(/dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video*)
for node in "${nodes[@]}"; do [[ -e "$node" || -L "$node" ]] && ls -l "$node" || true; done

echo '== Docker 内设备当前快照 =='
docker exec "$CONTAINER" sh -lc '
  if command -v lsusb >/dev/null 2>&1; then lsusb || true; else echo "(lsusb 未安装)"; fi
  ls -l /dev/wheeltec_lidar /dev/ttyUSB* /dev/ttyACM* /dev/ttyCH343USB* /dev/video* 2>/dev/null || true
  if test -d /run/udev/data; then echo "PASS /run/udev/data available"; else echo "WARNING /run/udev/data unavailable"; fi
'
cat <<'TEXT'
== 动态插拔实测步骤（需人工执行） ==
1. 确认容器保持运行；停止使用该 USB 设备的驱动/电机，保证实机安全。
2. 在容器运行之后再插入 N10 或 Astra Pro。
3. 重跑本脚本：比较宿主和容器中的 lsusb、ttyACM/ttyUSB、video 节点。
4. 可在宿主机用 sudo udevadm monitor --kernel --udev 观察 add/remove 事件。
5. 验证 udev 生成的 /dev/wheeltec_lidar 别名，以及真正的 ROS 驱动是否输出消息。
6. 重新拔插可能要求重启驱动节点；可枚举新设备不代表原进程自动热重连。
TEXT
