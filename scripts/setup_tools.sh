#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# setup_tools.sh - 激活三套交叉工具链环境（Xtensa / RISC-V / ARM）
#
# WHAT : 把本项目三套工具链与配套工具加入 PATH 并逐项自检
# WHY  : 五板三架构（S3/CAM=Xtensa、合宙 C3=RISC-V、Pico=ARM），
#        旧三板入口（scripts/esp32xx/）与交互式终端都依赖本脚本；
#        五板统一入口 scripts/firmware/build_firmware.sh 自带按板
#        PATH，不依赖本脚本
# WHO  : Retro WS Project Team
# WHERE: retro-ws/scripts/setup_tools.sh（工具链实体在 deps/esp-idf-tools/）
# WHEN : 2026-03 初版；2026-10-05 重写为三工具链（修旧目录名、补 RISC-V/ARM）
# HOW  : source 本脚本 -> PATH 前置 {xtensa,riscv,arm,shim-riscv64}/bin
#        + openocd/qemu + 项目 bin/ -> 逐个 --version 自检并给按板提示
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

# 项目自定义工具（esptool 包装器）添加到 PATH 前部
export PATH="$PROJECT_ROOT/bin:$PATH"

# 工具链加入 PATH：三套交叉编译器 + riscv64 shim + 调试/模拟（存在才加）
for d in xtensa/bin riscv/bin arm/bin shim-riscv64/bin \
         xtensa-esp-elf/bin riscv32-esp-elf/bin esp32ulp-elf/bin \
         openocd-esp32/bin qemu/bin; do
    [ -d "$TOOLS_DIR/$d" ] && export PATH="$TOOLS_DIR/$d:$PATH"
done

echo "== 工具链自检（三套，按板对应） =="

# 1) Xtensa：ESP32-S3 / ESP32-CAM
XT_OK=1
for cc in xtensa-esp-elf-gcc xtensa-esp32-elf-gcc xtensa-esp32s3-elf-gcc; do
    if command -v "$cc" &>/dev/null; then
        echo "[OK]   $cc: $("$cc" --version | head -1)"
    else
        echo "[WARN] $cc 未找到"
        XT_OK=0
    fi
done
[ "$XT_OK" = 1 ] && echo "       -> 服务板：s3 / s3n8 / cam（Xtensa LX7/LX6）"

# 2) RISC-V：合宙 ESP32-C3
if command -v riscv32-esp-elf-gcc &>/dev/null; then
    echo "[OK]   riscv32-esp-elf-gcc: $(riscv32-esp-elf-gcc --version | head -1)"
    echo "       -> 服务板：c3（RISC-V RV32IMC）"
    if [ -d "$TOOLS_DIR/shim-riscv64/bin" ]; then
        echo "[OK]   shim-riscv64（NuttX riscv64 构建工具垫片）"
    fi
else
    echo "[WARN] riscv32-esp-elf-gcc 未找到（c3 板需要；随 --tools-only 一并下载）"
fi

# 3) ARM：Raspberry Pi Pico（RP2040）
if command -v arm-none-eabi-gcc &>/dev/null; then
    echo "[OK]   arm-none-eabi-gcc: $(arm-none-eabi-gcc --version | head -1)"
    echo "       -> 服务板：pico（Cortex-M0+）"
else
    echo "[WARN] arm-none-eabi-gcc 未找到（pico 板需要）"
    echo "       安装: sudo apt-get install gcc-arm-none-eabi"
    echo "       或自备工具链放入 $TOOLS_DIR/arm/（下载脚本暂未集成 ARM）"
fi

# 4) 通用构建工具
if command -v ninja &>/dev/null; then
    echo "[OK]   ninja: $(ninja --version)"
else
    echo "[WARN] ninja 未安装，请运行: sudo apt-get install ninja-build"
fi

if command -v cmake &>/dev/null; then
    echo "[OK]   cmake: $(cmake --version | head -1)"
else
    echo "[WARN] cmake 未安装，请运行: sudo apt-get install cmake"
fi

# 5) QEMU（可选，仅 ESP32-S3 模拟）
if [ -x "$TOOLS_DIR/qemu/bin/qemu-system-xtensa" ]; then
    echo "[OK]   qemu-system-xtensa: $("$TOOLS_DIR/qemu/bin/qemu-system-xtensa" -version 2>&1 | head -1)"
elif command -v qemu-system-xtensa &>/dev/null; then
    QEMU_VER=$(qemu-system-xtensa -version 2>&1 | head -1)
    if echo "$QEMU_VER" | grep -q "esp"; then
        echo "[OK]   qemu-system-xtensa: $QEMU_VER"
    else
        echo "[WARN] 系统 QEMU 不支持 ESP32 (Espressif 专用版本)"
        echo "       需要: $TOOLS_DIR/qemu/bin/qemu-system-xtensa"
        echo "       或使用 Docker: docker run --rm -it espressif/idf-qemu"
    fi
else
    echo "[WARN] qemu-system-xtensa 未安装（可选，仅 S3 模拟用）"
    echo "       下载: https://github.com/espressif/qemu/releases"
fi

echo ""
echo "工具链环境已设置（三套：Xtensa / RISC-V / ARM）"
echo "IDF_TOOLS_PATH: $IDF_TOOLS_PATH"
echo "提示: 五板统一构建 scripts/firmware/build_firmware.sh 自带按板 PATH，可不 source 本脚本"
echo ""
