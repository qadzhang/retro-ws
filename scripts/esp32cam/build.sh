#!/bin/bash
#
# build.sh - ESP32-CAM 编译/烧录脚本
#
# 用法:
#   ./build.sh nuttx      - 编译 NuttX 固件
#   ./build.sh flash      - 烧录到 ESP32-CAM
#   ./build.sh clean      - 清理
#   ./build.sh menuconfig - 图形配置

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="$PROJECT_ROOT/deps/nuttx"
IDF_PATH="${IDF_PATH:-$PROJECT_ROOT/deps/esp-idf}"
BUILD_DIR="$NUTTX_DIR/build"

# ESP32-CAM 固定参数
CHIP_NAME="esp32"
NUTTX_BOARD="esp32-devkitc"
NUTTX_CONFIG="nsh"
TARGET_DIR="esp32"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log()  { echo -e "${CYAN}[ESP32-CAM]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err()  { echo -e "${RED}[ERR]${NC} $1"; exit 1; }

check_env() {
    if [ ! -d "$NUTTX_DIR" ]; then
        err "NuttX 未找到，先运行: cd $PROJECT_ROOT && ./scripts/download_deps.sh"
    fi
}

source_idf() {
    # 使用项目自己的工具链设置脚本（不依赖 ESP-IDF Python venv）
    if [ -f "$PROJECT_ROOT/scripts/setup_tools.sh" ]; then
        source "$PROJECT_ROOT/scripts/setup_tools.sh"
        log "ESP-IDF 工具链环境已激活"
    fi
}

# 添加本地编译工具到 PATH
add_local_tools() {
    if [ -d "$PROJECT_ROOT/tools" ]; then
        export PATH="$PROJECT_ROOT/tools:$PATH"
    fi
    if [ -x "$HOME/bin/genromfs" ]; then
        export PATH="$HOME/bin:$PATH"
        log "genromfs 已添加到 PATH"
    fi
}

build_nuttx() {
    check_env
    source_idf
    add_local_tools
    cd "$NUTTX_DIR"

    if [ ! -f "$BUILD_DIR/.config" ]; then
        log "配置 NuttX（${NUTTX_BOARD}:${NUTTX_CONFIG}）..."
        ./tools/configure.sh ${NUTTX_BOARD}:${NUTTX_CONFIG}
    fi

    log "编译 NuttX (ESP32-CAM, $(nproc) cores)..."
    # 清理上次编译残留的 mbedtls 补丁 marker
    find "${NUTTX_DIR}/arch/xtensa/src" -name ".nuttx-build-marker" -delete 2>/dev/null
    # .nuttx-patch-applied 由补丁创建，需要清理
    local HAL_MBEDTLS_CHIP="${NUTTX_DIR}/chip/esp-hal-3rdparty/components/mbedtls/mbedtls"
    if [ -d "$HAL_MBEDTLS_CHIP/.git" ]; then
        git -C "$HAL_MBEDTLS_CHIP" clean -f .nuttx-patch-applied 2>/dev/null || true
    fi
    # 清理 arch/xtensa/src/... 路径下的 marker（可能是符号链接指向 deps 版本）
    local HAL_MBEDTLS_ARCH="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}/esp-hal-3rdparty/components/mbedtls/mbedtls"
    if [ -d "$HAL_MBEDTLS_ARCH/.git" ]; then
        git -C "$HAL_MBEDTLS_ARCH" clean -f .nuttx-patch-applied 2>/dev/null || true
    fi

    # 修复 nuttx 补丁重复 bug：删除重复的 mbedtls 补丁文件
    local PATCH_DIR="${NUTTX_DIR}/chip/esp-hal-3rdparty/nuttx/patches/components/mbedtls/mbedtls"
    if [ -f "${PATCH_DIR}/0002-mbedtls_add_prefix_to_macro.patch" ]; then
        rm -f "${PATCH_DIR}/0002-mbedtls_add_prefix_to_macro.patch"
    fi
    # 同时清理 deps 版本
    local HAL_LINK="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}/esp-hal-3rdparty"
    if [ -L "$HAL_LINK" ]; then
        local HAL_REAL="$(readlink -f "$HAL_LINK")"
        local PATCH_DIR_DEPS="${HAL_REAL}/nuttx/patches/components/mbedtls/mbedtls"
        if [ -f "${PATCH_DIR_DEPS}/0002-mbedtls_add_prefix_to_macro.patch" ]; then
            rm -f "${PATCH_DIR_DEPS}/0002-mbedtls_add_prefix_to_macro.patch"
        fi
    fi

    # 设置 esp-hal-3rdparty 符号链接
    local ESP_HAL_LINK="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}/esp-hal-3rdparty"
    local ESP_HAL_SRC="${PROJECT_ROOT}/deps/esp-hal-3rdparty"
    if [ ! -L "$ESP_HAL_LINK" ] && [ ! -d "$ESP_HAL_LINK" ]; then
        if [ -d "$ESP_HAL_SRC" ]; then
            ln -sf "$ESP_HAL_SRC" "$ESP_HAL_LINK"
            log "esp-hal-3rdparty 链接到独立版本"
        else
            ln -sf "${NUTTX_DIR}/chip/esp-hal-3rdparty" "$ESP_HAL_LINK"
            log "esp-hal-3rdparty 链接到 nuttx 内部版本"
        fi
    fi

    make -j$(nproc) \
        STORAGETMP=y \
        USE_NXTMPDIR_ESP_REPO_DIRECTLY=y \
        NXTMPDIR="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}" \
        EXTRAFLAGS="-I${PROJECT_ROOT}/src/nuttx/common \
                    -I${PROJECT_ROOT}/src/nuttx/common/browser \
                    -I${PROJECT_ROOT}/src/nuttx/${TARGET_DIR} \
                    -I${PROJECT_ROOT}/deps/links" \
        2>&1 | tee "$PROJECT_ROOT/build-esp32cam.log"

    if [ -f "$NUTTX_DIR/nuttx.bin" ]; then
        SIZE=$(du -h "$NUTTX_DIR/nuttx.bin" | cut -f1)
        log "编译成功！固件: $NUTTX_DIR/nuttx.bin ($SIZE)"
        log "Flash: 4MB, PSRAM: 4MB (QSPI), DAC: GPIO25+GPIO26"
    else
        err "编译失败"
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
    local BAUD="${ESP_BAUD:-460800}"

    if [ ! -f "$NUTTX_DIR/nuttx.bin" ]; then
        err "固件不存在，先运行: ./build.sh nuttx"
    fi

    log "擦除 Flash..."
    esptool.py --chip "$CHIP_NAME" --port "$PORT" --baud "$BAUD" erase_flash

    log "烧录到 ESP32-CAM ($PORT, $BAUD)..."
    python -m esptool --chip "$CHIP_NAME" --port "$PORT" --baud "$BAUD" \
        write_flash 0x1000 "$NUTTX_DIR/nuttx.bin"

    log "烧录完成！请重启开发板"
    log "提示: GPIO0 接地后上电进入烧录模式"
}

clean_all() {
    cd "$NUTTX_DIR" 2>/dev/null || true
    make distclean 2>/dev/null || true
    rm -f "$PROJECT_ROOT/build-esp32cam.log"
    log "清理完成"
}

show_help() {
    cat << EOF
ESP32-CAM 复古工作站 - 编译脚本

用法: ./build.sh <命令>

命令:
  nuttx      编译 NuttX 固件
  menuconfig 图形配置
  flash      烧录到 ESP32-CAM
  clean      清理

环境变量:
  ESP_PORT  串口端口（默认: /dev/ttyUSB0）
  ESP_BAUD  烧录波特率（默认: 460800）

ESP32-CAM 硬件:
  Flash: 4MB (QSPI)
  PSRAM: 4MB (QSPI)
  DAC:   GPIO25 (CVBS视频), GPIO26 (音频)
  SD卡:  SPI模式 (GPIO2/12/13/14/15)
  输入:  BLE HID（无 USB OTG）
  注意:  摄像头与 DAC 共用引脚，不可同时使用

烧录模式:
  GPIO0 接地 → 上电 → 运行 ./build.sh flash
EOF
}

case "${1:-help}" in
    nuttx)      build_nuttx ;;
    menuconfig) run_menuconfig ;;
    flash)      flash_firmware ;;
    clean)      clean_all ;;
    help|--help|-h) show_help ;;
    *)          err "未知命令: $1" ;;
esac
