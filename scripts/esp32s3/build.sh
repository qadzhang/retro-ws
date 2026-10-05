#!/bin/bash
#
# build.sh - ESP32-S3 编译/烧录脚本
#
# 用法:
#   ./build.sh nuttx      - 编译 NuttX 固件
#   ./build.sh qemu       - QEMU 模拟
#   ./build.sh flash      - 烧录到开发板
#   ./build.sh clean      - 清理
#   ./build.sh menuconfig - 图形配置

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="$PROJECT_ROOT/deps/nuttx"
IDF_PATH="${IDF_PATH:-$PROJECT_ROOT/deps/esp-idf}"
BUILD_DIR="$NUTTX_DIR/build"

# ESP32-S3 固定参数
CHIP_NAME="esp32s3"
NUTTX_BOARD="esp32s3-devkit"
NUTTX_CONFIG="qemu_openeth"
TARGET_DIR="esp32s3"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[ESP32-S3]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err()  { echo -e "${RED}[ERR]${NC} $1"; exit 1; }

check_env() {
    if [ ! -d "$NUTTX_DIR" ]; then
        err "NuttX 未找到，先运行: cd $PROJECT_ROOT && ./scripts/download_deps.sh"
    fi
}

source_idf() {
    if [ -f "$PROJECT_ROOT/scripts/setup_tools.sh" ]; then
        source "$PROJECT_ROOT/scripts/setup_tools.sh" 2>/dev/null
    elif [ -f "$IDF_PATH/export.sh" ]; then
        source "$IDF_PATH/export.sh" 2>/dev/null
    fi
}

build_nuttx() {
    check_env
    source_idf
    cd "$NUTTX_DIR"

    if [ ! -f "$BUILD_DIR/.config" ]; then
        log "配置 NuttX（${NUTTX_BOARD}:${NUTTX_CONFIG}）..."
        ./tools/configure.sh ${NUTTX_BOARD}:${NUTTX_CONFIG}
    fi

    log "编译（$(nproc) 核心）..."
    make -j$(nproc) \
        EXTRAFLAGS="-I${PROJECT_ROOT}/src/nuttx/common -I${PROJECT_ROOT}/src/nuttx/${TARGET_DIR}" \
        2>&1 | tee "$PROJECT_ROOT/build.log"

    if [ -f "$NUTTX_DIR/nuttx.bin" ]; then
        SIZE=$(du -h "$NUTTX_DIR/nuttx.bin" | cut -f1)
        log "编译成功！固件: $NUTTX_DIR/nuttx.bin ($SIZE)"
    else
        err "编译失败，查看 build.log"
    fi
}

run_menuconfig() {
    check_env
    source_idf
    cd "$NUTTX_DIR"

    if [ ! -f "$BUILD_DIR/.config" ]; then
        ./tools/configure.sh ${NUTTX_BOARD}:${NUTTX_CONFIG}
    fi
    make menuconfig
}

flash_firmware() {
    check_env
    source_idf

    local PORT="${ESP_PORT:-/dev/ttyUSB0}"
    local BAUD="${ESP_BAUD:-921600}"

    if [ ! -f "$NUTTX_DIR/nuttx.bin" ]; then
        err "固件不存在，先运行: ./build.sh nuttx"
    fi

    log "烧录到 $PORT ($BAUD) [${CHIP_NAME}]..."

    esptool.py --chip "$CHIP_NAME" --port "$PORT" --baud "$BAUD" erase_flash

    python -m esptool --chip "$CHIP_NAME" --port "$PORT" --baud "$BAUD" \
        write_flash \
        0x1000 "$NUTTX_DIR/bootloader/bootloader.bin" \
        0x8000 "$NUTTX_DIR/partitions.bin" \
        0x10000 "$NUTTX_DIR/nuttx.bin"

    log "烧录完成！请重启开发板"
}

build_qemu() {
    check_env
    source_idf
    cd "$NUTTX_DIR"

    make distclean 2>/dev/null || true
    log "配置 QEMU（${NUTTX_BOARD}:${NUTTX_CONFIG}）..."
    ./tools/configure.sh ${NUTTX_BOARD}:${NUTTX_CONFIG}

    log "编译..."
    make -j$(nproc) \
        EXTRAFLAGS="-I${PROJECT_ROOT}/src/nuttx/common -I${PROJECT_ROOT}/src/nuttx/${TARGET_DIR}"

    if [ ! -f "$NUTTX_DIR/nuttx.bin" ]; then
        err "编译失败"; fi

    QEMU="$PROJECT_ROOT/deps/esp-idf-tools/qemu/bin/qemu-system-xtensa"
    if [ ! -x "$QEMU" ]; then
        err "未找到 QEMU: $QEMU"; fi

    log "启动 QEMU (Ctrl+C 退出)..."
    "$QEMU" -machine esp32 -nographic -kernel "$NUTTX_DIR/nuttx" \
        -netdev tap,id=tap0,ifname=tap0,script=no \
        -device openeth,netdev=tap0
}

clean_all() {
    cd "$NUTTX_DIR" 2>/dev/null || true
    make distclean 2>/dev/null || true
    rm -f "$PROJECT_ROOT/build.log"
    log "清理完成"
}

show_help() {
    cat << EOF
ESP32-S3 复古工作站 - 编译脚本

用法: ./build.sh <命令>

命令:
  nuttx      编译 NuttX 固件
  qemu       编译 + QEMU 模拟
  menuconfig 图形配置
  flash      烧录到开发板
  clean      清理

环境变量:
  ESP_PORT  串口端口（默认: /dev/ttyUSB0）
  ESP_BAUD  烧录波特率（默认: 921600）

示例:
  ./build.sh nuttx
  ./build.sh flash
  ESP_PORT=/dev/ttyUSB1 ./build.sh flash
EOF
}

case "${1:-help}" in
    nuttx)      build_nuttx ;;
    qemu)       build_qemu ;;
    menuconfig) run_menuconfig ;;
    flash)      flash_firmware ;;
    clean)      clean_all ;;
    help|--help|-h) show_help ;;
    *)          err "未知命令: $1" ;;
esac
