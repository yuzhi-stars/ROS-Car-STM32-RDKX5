#!/usr/bin/env bash
# 运行地点：USB 设备物理连接的宿主机（RDK 或 PC），不要在 Docker 容器内执行。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo '注意：将安装 N10 的规则；Astra 规则仅在提供 ASTRA_RULES 文件时安装。'
echo '当前 USB 设备：'
lsusb || true
sudo install -m 0644 "$ROOT/udev/99-x5car-n10.rules" /etc/udev/rules.d/99-x5car-n10.rules
if [[ -n "${ASTRA_RULES:-}" ]]; then
  [[ -f "$ASTRA_RULES" ]] || { echo "找不到相机规则: $ASTRA_RULES" >&2; exit 1; }
  sudo install -m 0644 "$ASTRA_RULES" /etc/udev/rules.d/56-orbbec-usb.rules
  echo "Astra 原厂规则已安装；注意其可能使用 MODE=0666，请按设备管理策略审查。"
fi
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=tty || true
sudo udevadm trigger --subsystem-match=usb || true
echo '请重新插拔雷达和相机，并检查：'
echo '  ls -l /dev/wheeltec_lidar /dev/ttyACM* /dev/ttyUSB* /dev/video* 2>/dev/null'
echo '  udevadm info -a -n /dev/ttyACM0 | less  # 将设备名改为实际值'
