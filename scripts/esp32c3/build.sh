#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : ESP32-C3（合宙核心板）编译/烧录脚本
# WHY  : 第三目标入口（CLI 工作站，RISC-V）
# WHO  : 开发者
# WHERE: esp32-retro-ws/scripts/esp32c3/build.sh
# WHEN : 2026-10-04 新增
# HOW  : 复用 nuttx_build.sh；烧录经 esptool（芯片 esp32c3）
#
# 用法:
#   ./build.sh nuttx      - 编译 NuttX 固件
#   ./build.sh flash      - 烧录（经典款经 CH343 串口 / 简约款经原生 USB）
#   ./build.sh clean      - 清理

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="$PROJECT_ROOT/deps/nuttx"

PORT="${ESPPORT:-/dev/ttyUSB0}"

cmd="${1:-help}"
case "$cmd" in
    nuttx)
        ./nuttx_build.sh build
        ;;
    flash)
        "$PROJECT_ROOT/bin/esptool.py" --chip esp32c3 --port "$PORT" \
            --baud 921600 write_flash 0x0 "$NUTTX_DIR/nuttx.bin"
        ;;
    clean)
        ./nuttx_build.sh clean
        ;;
    *)
        echo "用法: $0 <nuttx|flash|clean>"
        echo "  烧录端口: ESPPORT=$PORT (export ESPPORT=/dev/ttyACM0 可改)"
        ;;
esac
