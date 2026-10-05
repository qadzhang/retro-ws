#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 一键整体构建——固件（双目标）+ 可选安装包（GPL 独立 ELF）+ SD 分发目录
# WHY  : 单一入口完成"固件不含 GPL + GPL 以独立程序交付"的完整编译流程
# WHO  : ESP32-S3 Retro Project Team
# WHERE: esp32-retro-ws/scripts/build_all.sh
# WHEN : 2026-10-04 新增
# HOW  : 串接 download_deps -> setup_tools -> scripts/<target>/build.sh ->
#        build_packages.sh -> dist/sdcard/ 打包
#
# 用法:
#   ./build_all.sh [esp32s3|esp32cam|all] [--no-packages]
#
# 产物:
#   deps/nuttx/nuttx.bin          固件（Apache-2.0，无 GPL）
#   dist/sdcard/                  SD 安装目录（含 GPL 独立 ELF）

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

TARGET="esp32s3"
WITH_PACKAGES=true
for arg in "$@"; do
    case "$arg" in
        esp32s3)        TARGET="esp32s3" ;;
        esp32cam)       TARGET="esp32cam" ;;
        esp32c3)        TARGET="esp32c3" ;;
        all)            TARGET="all" ;;
        --no-packages)  WITH_PACKAGES=false ;;
        *) echo "未知参数: $arg（可用: esp32s3|esp32cam|esp32c3|all|--no-packages）"; exit 1 ;;
    esac
done

log() { echo -e "\033[0;34m[BUILD-ALL]\033[0m $1"; }

build_firmware()
{
    local t="$1"
    log "===== 构建固件: $t ====="
    (cd "$PROJECT_ROOT/scripts/$t" && ./nuttx_build.sh defconfig && ./build.sh nuttx)
}

build_target()
{
    local t="$1"
    build_firmware "$t"

    if [ "$WITH_PACKAGES" = true ]; then
        log "===== 构建可选安装包: $t ====="
        bash "$SCRIPT_DIR/build_packages.sh" --target="$t"
    fi
}

if [ ! -d "$PROJECT_ROOT/deps/nuttx" ]; then
    log "deps 缺失，先下载依赖..."
    bash "$SCRIPT_DIR/download_deps.sh"
fi

if [ "$TARGET" = "all" ]; then
    build_target esp32s3
    build_target esp32cam
    build_target esp32c3
else
    build_target "$TARGET"
fi

log "===== 完成 ====="
log "固件:   deps/nuttx/nuttx.bin （Apache-2.0，无 GPL 代码）"
[ "$WITH_PACKAGES" = true ] && log "安装包: dist/sdcard/ （GPL 组件为独立 ELF）"
log "烧录:   cd scripts/$TARGET && ./build.sh flash"
