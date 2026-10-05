#!/bin/bash
#
# setup_env.sh - 安装 ESP-IDF 工具链（需要 sudo）
#
# 用法: ./setup_env.sh
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="$PROJECT_ROOT/deps"

echo "========================================"
echo " ESP32-S3 开发环境安装"
echo "========================================"

# 检查系统
if [ "$(uname)" != "Linux" ]; then
    echo "错误: 仅支持 Linux"
    exit 1
fi

# 检查 sudo
if ! sudo -n true 2>/dev/null; then
    echo "请输入 sudo 密码（如果需要）"
fi

# 安装基础依赖
echo "[1/5] 安装系统依赖..."
sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    git \
    wget \
    curl \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-venv \
    python3-setuptools \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0 \
    2>/dev/null || true
# 注意：标准 qemu-system-xtensa 不支持 ESP32/ESP32-S3
# 需要安装 Espressif 专用 QEMU: https://github.com/espressif/qemu/releases

# 安装 ESP-IDF Python 依赖
echo "[2/5] 安装 Python 依赖..."
pip3 install --break-system-packages --upgrade pip setuptools wheel
pip3 install --break-system-packages \
    construct intelhex reedsolo pyserial bitstring rich-click click

# 安装 ESP-IDF tools
echo "[3/5] 安装 ESP-IDF 工具..."
export IDF_PATH="$DEPS_DIR/esp-idf"
python3 "$IDF_PATH/tools/idf_tools.py" install --force 2>&1 | tail -10

# 激活 ESP-IDF 环境
echo "[4/5] 激活 ESP-IDF..."
source "$DEPS_DIR/esp-idf/export.sh"

# 验证
echo "[5/5] 验证安装..."
echo "Xtensa GCC: $(which xtensa-esp32-elf-gcc 2>/dev/null || echo '未找到')"
echo "esptool.py: $(which esptool.py 2>/dev/null || echo '未找到')"
echo "cmake:      $(which cmake 2>/dev/null || echo '未找到')"
echo "ninja:      $(which ninja 2>/dev/null || echo '未找到')"

echo ""
echo "========================================"
echo " 环境安装完成！"
echo "========================================"
echo ""
echo "下一步:"
echo "  1. source $DEPS_DIR/esp-idf/export.sh"
echo "  2. cd $PROJECT_ROOT"
echo "  3. ./scripts/download_deps.sh  (如需要)"
echo "  4. ./scripts/nuttx_build.sh build"
echo ""
