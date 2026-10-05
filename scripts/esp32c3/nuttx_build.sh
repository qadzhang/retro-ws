#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : ESP32-C3（合宙核心板，RISC-V）NuttX 编译脚本
# WHY  : 第三目标需要独立配置与 RISC-V 工具链
# WHO  : 开发者手动或 build_all.sh 调用
# WHERE: retro-ws/scripts/esp32c3/nuttx_build.sh
# WHEN : 2026-10-04 新增
# HOW  : 仿 esp32cam 流程，工具链换 riscv32-esp-elf
#
# 用法: ./nuttx_build.sh <defconfig|build|config|clean>

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="$PROJECT_ROOT/deps/nuttx"

NUTTX_BOARD="esp32c3-devkit"
DEFCONFIG="$PROJECT_ROOT/configs/nuttx-defconfig-esp32c3"

# RISC-V 工具链（download_deps.sh 安装）
export PATH="$PROJECT_ROOT/deps/esp-idf-tools/riscv32-esp-elf/bin:$PATH"

cmd="${1:-help}"
case "$cmd" in
    defconfig)
        [ -d "$NUTTX_DIR" ] || { echo "deps/nuttx 不存在，先运行 download_deps.sh"; exit 1; }
        cd "$NUTTX_DIR"
        cp "$DEFCONFIG" .config
        ;;
    build)
        cd "$NUTTX_DIR"
        make -j"$(nproc)"
        ;;
    config)
        cd "$NUTTX_DIR"
        make menuconfig
        ;;
    clean)
        cd "$NUTTX_DIR" && make clean
        ;;
    *)
        echo "用法: $0 <defconfig|build|config|clean>"
        ;;
esac
