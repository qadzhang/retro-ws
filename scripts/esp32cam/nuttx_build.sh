#!/bin/bash
#
# nuttx_build.sh - ESP32-CAM NuttX 编译脚本
#
# 用法:
#   cd scripts/esp32cam
#   ./nuttx_build.sh defconfig  - 应用 ESP32-CAM 默认配置
#   ./nuttx_build.sh build      - 编译
#   ./nuttx_build.sh clean      - 清理
#   ./nuttx_build.sh config     - menuconfig

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NUTTX_DIR="${PROJECT_ROOT}/deps/nuttx"
APPS_DIR="${PROJECT_ROOT}/deps/nuttx-apps"
IDF_DIR="${PROJECT_ROOT}/deps/esp-idf"
BUILD_DIR="${NUTTX_DIR}/build"

# ESP32-CAM 固定参数
NUTTX_BOARD="esp32-devkitc"
NUTTX_CONFIG="nsh"
CHIP_NAME="esp32"
TARGET_DIR="esp32"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[ESP32-CAM]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERR]${NC} $1"; exit 1; }

source_idf() {
    if [ -f "${PROJECT_ROOT}/scripts/setup_tools.sh" ]; then
        source "${PROJECT_ROOT}/scripts/setup_tools.sh" 2>/dev/null
        log "工具链已激活"
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

    # 应用自定义 defconfig（完整配置，包含所有组件）
    local DEFCFG="${PROJECT_ROOT}/configs/nuttx-defconfig-esp32cam-full"
    if [ -f "$DEFCFG" ]; then
        log "应用 ESP32-CAM 完整配置（LVGL+WiFi+BLE+脚本引擎）..."
        cat "$DEFCFG" >> "$NUTTX_DIR/.config"
        make olddefconfig 2>/dev/null || true
    else
        log_warn "完整配置不存在，使用: $DEFCFG"
    fi

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

    log "编译 NuttX（ESP32-CAM，使用 $(nproc) 核心）..."

    # 清理上次编译残留的 mbedtls 补丁 marker
    find "${NUTTX_DIR}/arch/xtensa/src" -name ".nuttx-build-marker" -delete 2>/dev/null
    # .nuttx-patch-applied 由补丁创建（untracked 文件），git reset --hard 不会删除它
    # 需要用 git clean -f 删除未跟踪文件，否则重复编译时 git apply 会失败
    # 注意：有两处可能存在 marker：
    #   1. nuttx/chip/... (bundled 版本)
    #   2. deps/esp-hal-3rdparty/... (通过 arch/xtensa/src/.../esp-hal-3rdparty 符号链接)
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
    # 两个补丁文件 MD5 完全相同，都尝试创建 .nuttx-patch-applied，导致第二个补丁失败
    # 这是 nuttx 本身的问题，我们删除重复文件来修复
    # 注意：补丁位置可能是 nuttx 捆绑版或 deps/esp-hal-3rdparty（通过符号链接）
    local PATCH_DIR="${NUTTX_DIR}/chip/esp-hal-3rdparty/nuttx/patches/components/mbedtls/mbedtls"
    if [ -f "${PATCH_DIR}/0002-mbedtls_add_prefix_to_macro.patch" ]; then
        rm -f "${PATCH_DIR}/0002-mbedtls_add_prefix_to_macro.patch"
        log "已删除重复的补丁文件 (nuttx 捆绑版)"
    fi
    # 同时清理 deps 版本（如果符号链接指向那里）
    local HAL_LINK="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}/esp-hal-3rdparty"
    if [ -L "$HAL_LINK" ]; then
        local HAL_REAL="$(readlink -f "$HAL_LINK")"
        local PATCH_DIR_DEPS="${HAL_REAL}/nuttx/patches/components/mbedtls/mbedtls"
        if [ -f "${PATCH_DIR_DEPS}/0002-mbedtls_add_prefix_to_macro.patch" ]; then
            rm -f "${PATCH_DIR_DEPS}/0002-mbedtls_add_prefix_to_macro.patch"
            log "已删除重复的补丁文件 (deps 版本)"
        fi
    fi

    # 设置 esp-hal-3rdparty 符号链接
    # 优先使用 deps/esp-hal-3rdparty（独立下载），若无则用 nuttx 内部版本
    local ESP_HAL_LINK="${NUTTX_DIR}/arch/xtensa/src/${TARGET_DIR}/esp-hal-3rdparty"
    local ESP_HAL_SRC="${PROJECT_ROOT}/deps/esp-hal-3rdparty"
    if [ ! -L "$ESP_HAL_LINK" ] && [ ! -d "$ESP_HAL_LINK" ]; then
        if [ -d "$ESP_HAL_SRC" ]; then
            ln -sf "$ESP_HAL_SRC" "$ESP_HAL_LINK"
            log "esp-hal-3rdparty 链接到独立版本"
        else
            # 使用 nuttx 内部版本（chip/ 目录下已有完整子模块）
            ln -sf "${NUTTX_DIR}/chip/esp-hal-3rdparty" "$ESP_HAL_LINK"
            log "esp-hal-3rdparty 链接到 nuttx 内部版本"
        fi
    fi

    # EXTRAFLAGS: 项目源码头文件搜索路径

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
    else
        err "编译失败，查看 build-esp32cam.log"
    fi
}

# ========== 清理 ==========
do_clean() {
    cd "$NUTTX_DIR" 2>/dev/null || true
    log "清理..."
    make clean 2>/dev/null || true
    rm -f "$PROJECT_ROOT/build-esp32cam.log"
    log "清理完成"
}

# ========== 帮助 ==========
show_help() {
    cat << EOF
ESP32-CAM NuttX 编译脚本

用法: ./nuttx_build.sh <命令>

命令:
  defconfig  应用 ESP32-CAM 默认配置
  build      编译 NuttX
  config     menuconfig
  clean      清理

示例:
  ./nuttx_build.sh defconfig && ./nuttx_build.sh build

前提:
  先运行: cd ../.. && ./scripts/download_deps.sh
  然后:   source scripts/setup_tools.sh

注意:
  ESP32-CAM 使用 SPI PSRAM (4MB)，Flash 4MB
  内置 DAC: GPIO25 (CVBS), GPIO26 (Audio)
  摄像头和 DAC 共用引脚，不能同时使用
EOF
}

case "${1:-help}" in
    config)    do_config ;;
    defconfig) do_defconfig ;;
    build)     do_build ;;
    clean)     do_clean ;;
    help|--help|-h) show_help ;;
    *)         err "未知命令: $1" ;;
esac
