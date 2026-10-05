#!/bin/bash
#
# setup_tools.sh - 设置 ESP-IDF 工具链环境变量
#
# 用法: source scripts/setup_tools.sh
#

# 检测脚本是否被 source
if [ -z "$BASH_SOURCE" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
else
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="$PROJECT_ROOT/deps"
TOOLS_DIR="$DEPS_DIR/esp-idf-tools"

# 设置 IDF_TOOLS_PATH
export IDF_TOOLS_PATH="$TOOLS_DIR"

# 项目自定义工具（esptool.py 包装器）添加到 PATH 前部
export PATH="$PROJECT_ROOT/bin:$PATH"

# 添工具链到 PATH（按优先级排序）
export PATH="$TOOLS_DIR/xtensa-esp-elf/bin:$TOOLS_DIR/xtensa-esp-elf-gdb/bin:$TOOLS_DIR/riscv32-esp-elf/bin:$TOOLS_DIR/riscv32-esp-elf-gdb/bin:$TOOLS_DIR/esp32ulp-elf/bin:$TOOLS_DIR/openocd-esp32/bin:$TOOLS_DIR/qemu/bin:$PATH"

# 验证工具
if command -v xtensa-esp32-elf-gcc &> /dev/null; then
    echo "[OK] xtensa-esp32-elf-gcc: $(xtensa-esp32-elf-gcc --version | head -1)"
else
    echo "[ERR] xtensa-esp32-elf-gcc 未找到"
fi

if command -v xtensa-esp32s3-elf-gcc &> /dev/null; then
    echo "[OK] xtensa-esp32s3-elf-gcc: $(xtensa-esp32s3-elf-gcc --version | head -1)"
else
    echo "[WARN] xtensa-esp32s3-elf-gcc 未找到"
fi

if command -v ninja &> /dev/null; then
    echo "[OK] ninja: $(ninja --version)"
else
    echo "[WARN] ninja 未安装，请运行: sudo apt-get install ninja-build"
fi

if command -v cmake &> /dev/null; then
    echo "[OK] cmake: $(cmake --version | head -1)"
else
    echo "[WARN] cmake 未安装，请运行: sudo apt-get install cmake"
fi

if [ -x "$TOOLS_DIR/qemu/bin/qemu-system-xtensa" ]; then
    QEMU_VER=$("$TOOLS_DIR/qemu/bin/qemu-system-xtensa" -version 2>&1 | head -1)
    echo "[OK] qemu-system-xtensa: $QEMU_VER"
elif command -v qemu-system-xtensa &> /dev/null; then
    QEMU_VER=$(qemu-system-xtensa -version 2>&1 | head -1)
    if echo "$QEMU_VER" | grep -q "esp"; then
        echo "[OK] qemu-system-xtensa: $QEMU_VER"
    else
        echo "[WARN] 系统 QEMU 不支持 ESP32 (Espressif 专用版本)"
        echo "       需要: $TOOLS_DIR/qemu/bin/qemu-system-xtensa"
        echo "       或使用 Docker: docker run --rm -it espressif/idf-qemu"
    fi
else
    echo "[WARN] qemu-system-xtensa 未安装"
    echo "       下载: https://github.com/espressif/qemu/releases"
    echo "       或使用 Docker: docker run --rm -it espressif/idf-qemu"
fi

echo ""
echo "工具链环境已设置"
echo "IDF_TOOLS_PATH: $IDF_TOOLS_PATH"
echo ""
