#!/bin/bash
#
# nuttx_build.sh - ESP32-S3 NuttX 编译脚本
#
# 用法:
#   cd scripts/esp32s3
#   ./nuttx_build.sh defconfig  - 应用 ESP32-S3 默认配置
#   ./nuttx_build.sh build      - 编译
#   ./nuttx_build.sh clean      - 清理
#   ./nuttx_build.sh config     - menuconfig
#   ./nuttx_build.sh qemu       - QEMU 模拟

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="${PROJECT_ROOT}/deps/nuttx"
APPS_DIR="${PROJECT_ROOT}/deps/nuttx-apps"
IDF_DIR="${PROJECT_ROOT}/deps/esp-idf"
BUILD_DIR="${NUTTX_DIR}/build"

# ESP32-S3 固定参数
NUTTX_BOARD="esp32s3-devkit"
NUTTX_CONFIG="nsh"
CHIP_NAME="esp32s3"
TARGET_DIR="esp32s3"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[ESP32-S3]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERR]${NC} $1"; exit 1; }

# 激活工具链
source_idf() {
    if [ -f "${PROJECT_ROOT}/scripts/setup_tools.sh" ]; then
        source "${PROJECT_ROOT}/scripts/setup_tools.sh" 2>/dev/null
        log "使用项目工具链"
    elif [ -f "$IDF_DIR/export.sh" ]; then
        source "$IDF_DIR/export.sh" 2>/dev/null
        log "ESP-IDF 环境已激活"
    else
        warn "未找到工具链设置"
    fi
}

# ========== 配置 ==========
do_config() {
    source_idf
    cd "$NUTTX_DIR"

    if [ ! -f "$BUILD_DIR/.config" ]; then
        log "配置 NuttX（${NUTTX_BOARD}:nsh）..."
        ./tools/configure.sh ${NUTTX_BOARD}:nsh
    fi

    make menuconfig
}

# ========== 默认配置 ==========
do_defconfig() {
    source_idf
    cd "$NUTTX_DIR"

    log "配置 NuttX (${NUTTX_BOARD}:${NUTTX_CONFIG})..."
    ./tools/configure.sh ${NUTTX_BOARD}:${NUTTX_CONFIG}
    log "配置完成，请运行 ./nuttx_build.sh build"
}

# ========== 编译 ==========
do_build() {
    source_idf
    cd "$NUTTX_DIR"

    if [ ! -f "$NUTTX_DIR/.config" ]; then
        log "未检测到配置，先运行配置..."
        do_defconfig
    fi

    log "编译 NuttX（ESP32-S3，使用 $(nproc) 核心）..."

    # EXTRAFLAGS: 项目源码头文件搜索路径
    #   common/          — 共享驱动、工具、启动菜单
    #   common/browser/  — Links 浏览器适配层（config.h）
    #   esp32s3/         — ESP32-S3 专用代码
    #   deps/links/      — Links 2.30 源码
    make -j$(nproc) \
        EXTRAFLAGS="-I${PROJECT_ROOT}/src/nuttx/common \
                    -I${PROJECT_ROOT}/src/nuttx/common/browser \
                    -I${PROJECT_ROOT}/src/nuttx/${TARGET_DIR} \
                    -I${PROJECT_ROOT}/deps/links" \
        2>&1 | tee "$PROJECT_ROOT/build.log"

    if [ -f "$NUTTX_DIR/nuttx.bin" ]; then
        SIZE=$(du -h "$NUTTX_DIR/nuttx.bin" | cut -f1)
        log "编译成功！固件: $NUTTX_DIR/nuttx.bin ($SIZE)"
    else
        err "编译失败，请查看 build.log"
    fi
}

# ========== 清理 ==========
do_clean() {
    cd "$NUTTX_DIR"
    log "清理..."
    make clean 2>/dev/null || true
    rm -f "$PROJECT_ROOT/build.log"
    log "清理完成"
}

# ========== QEMU ==========
do_qemu() {
    source_idf

    TOOLS_QEMU="${PROJECT_ROOT}/deps/esp-idf-tools/qemu/bin/qemu-system-xtensa"

    if [ ! -x "$TOOLS_QEMU" ]; then
        err "未找到 QEMU: $TOOLS_QEMU"
    fi

    cd "$NUTTX_DIR"

    if [ ! -f "$NUTTX_DIR/nuttx" ]; then
        do_build
    fi

    NET_OPTS=""
    if [ -w /dev/net/tun ] || [ "$EUID" -eq 0 ]; then
        NET_OPTS="-netdev tap,id=tap0,ifname=tap0,script=no -device openeth,netdev=tap0"
        log "网络: 已启用"
    else
        warn "网络: 已禁用（需要 root 或 /dev/net/tun 权限）"
    fi

    log "启动 QEMU (Ctrl+C 退出)..."
    "$TOOLS_QEMU" \
        -machine esp32 \
        -nographic \
        -kernel "$NUTTX_DIR/nuttx" \
        $NET_OPTS
}

# ========== 帮助 ==========
show_help() {
    cat << EOF
ESP32-S3 NuttX 编译脚本

用法: ./nuttx_build.sh <命令>

命令:
  defconfig  应用 ESP32-S3 默认配置
  build      编译 NuttX
  config     menuconfig
  clean      清理
  qemu       QEMU 模拟

示例:
  ./nuttx_build.sh defconfig && ./nuttx_build.sh build

前提:
  先运行: cd ../.. && ./scripts/download_deps.sh
  然后:   source scripts/setup_tools.sh
EOF
}

case "${1:-help}" in
    config)    do_config ;;
    defconfig) do_defconfig ;;
    build)     do_build ;;
    clean)     do_clean ;;
    qemu)      do_qemu ;;
    help|--help|-h) show_help ;;
    *)         err "未知命令: $1" ;;
esac
