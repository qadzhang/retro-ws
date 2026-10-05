#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 四板固件构建入口 / firmware build for all four boards
# WHY  : 真机交付需要交叉编译产物 + ROM 尺寸核查
# WHO  : 开发者/CI
# WHERE: retro-ws/scripts/firmware/build_firmware.sh
# WHEN : 2026-10-04 新增；同日改为树内构建（NuttX 12 configure.sh 形式）
# HOW  : 用法: build_firmware.sh <pico|c3|cam|s3|all>
#        1) sync src -> deps/nuttx-apps/retro
#        2) (cd deps/nuttx && ./tools/configure.sh <board>:nsh)
#        3) 追加 firmware/<名>.appconfig -> olddefconfig
#        4) make APPDIR=... -> 拷 nuttx.bin 到 dist/<名>/
#        工具链: deps/esp-idf-tools/{xtensa,riscv,arm}
set -e

export PATH="$HOME/.local/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NUTTX="$ROOT/deps/nuttx"
APPS="$ROOT/deps/nuttx-apps"
TOOLS="$ROOT/deps/esp-idf-tools"
FW="$ROOT/firmware"
DIST="$ROOT/dist/firmware"

XT="$TOOLS/xtensa/bin"
RV="$TOOLS/riscv/bin"
ARM="$TOOLS/arm/bin"

bash "$ROOT/scripts/sync_src_to_apps.sh" >/dev/null

# lv_conf.h 同步到 deps/（LVGL 经 -I deps 找 ../lv_conf.h；tools/sim 也用
# 同一副本——两处入口共用，防止 configs/ 改后 deps/ 留旧档）
cp -f "$ROOT/configs/lv_conf.h" "$ROOT/deps/lv_conf.h"

# 板级脚本 ROMFS 镜像生成（HARDWARE.md 13.4：每板 scripts/ 目录入 ROM）
for b in s3 s3n8 cam c3 pico; do
    if [ -d "$FW/scripts/$b" ]; then
        python3 "$ROOT/tools/mkromfs.py" "$FW/scripts/$b" \
            "$APPS/retro/scripts_romfs.c" retro || true
    fi
done

# Berry codegen（宿主 gcc 生成 be_const_strtab；产物入 deps/berry/generate）
if [ -d "$ROOT/deps/berry" ] && [ ! -f "$ROOT/deps/berry/generate/be_const_strtab.h" ]; then
    ( cd "$ROOT/deps/berry" && make prebuild CC=gcc >/dev/null 2>&1 ) || true
fi
mkdir -p "$DIST"

build_one() {  # build_one <名称> <board> <toolchainbin>
    local name=$1 board=$2 tcb=$3
    echo
    echo "=============================================="
    echo "[fw] building $name ($board:nsh)"
    echo "=============================================="

    # 换板必须 distclean；distclean 会删 esp-hal-3rdparty 装配，
    # 故每次构建前重跑 prepare_esp_hal.sh（幂等，约 1-2 分钟）
    ( cd "$NUTTX" && make distclean >/dev/null 2>&1 || true \
        && ./tools/configure.sh "$board:nsh" ) > /tmp/fw_cfg.log 2>&1 \
        || { tail -10 /tmp/fw_cfg.log; return 1; }
    bash "$ROOT/scripts/firmware/prepare_esp_hal.sh" >> /tmp/fw_cfg.log 2>&1 \
        || { tail -10 /tmp/fw_cfg.log; return 1; }

    if [ -f "$FW/$name.appconfig" ]; then
        # "unset:" 前缀行 = 先 kconfig-tweak --disable（choice 切换必需）
        grep '^# unset:' "$FW/$name.appconfig" | sed 's/^# unset:[[:space:]]*//' | while read -r sym; do
            kconfig-tweak --file "$NUTTX/.config" --disable "$sym"
        done
        grep -v '^# unset:' "$FW/$name.appconfig" >> "$NUTTX/.config"
    fi

    ( cd "$NUTTX" && PATH="$tcb:$PATH" make olddefconfig ) > /tmp/fw_old.log 2>&1 \
        || { tail -10 /tmp/fw_old.log; return 1; }

    # 全量日志落盘再抽错（管道 grep|head 会 SIGPIPE 误杀 make）
    ( cd "$NUTTX" && PATH="$tcb:$PATH" \
        make -j"$(nproc)" APPDIR="$APPS" ) > "/tmp/fw_$name.log" 2>&1 || true
    ( cd "$NUTTX" && ls -la nuttx.bin nuttx >/dev/null 2>&1 || true; echo staged ) > "/tmp/fw_$name.ls"

    # RP2040 关闭 UF2 后无 .bin 目标——从 ELF 现场生成
    if [ ! -f "$NUTTX/nuttx.bin" ] && [ -f "$NUTTX/nuttx" ]; then
        local oc="$(echo "$tcb" | sed 's|/bin$||')/bin/$(basename "$(ls "$tcb" | grep objcopy | head -1)")"
        [ -x "$oc" ] || oc="$(find "$tcb" -name '*objcopy' | head -1)"
        "$oc" -O binary "$NUTTX/nuttx" "$NUTTX/nuttx.bin"
        [ "$name" = "pico" ] && python3 "$ROOT/scripts/make_uf2.py" \
            "$NUTTX/nuttx.bin" "$NUTTX/nuttx.uf2" >/dev/null
    fi

    if [ -f "$NUTTX/nuttx.bin" ]; then
        mkdir -p "$DIST/$name"
        cp "$NUTTX/nuttx.bin" "$DIST/$name/"
        [ -f "$NUTTX/nuttx.hex" ] && cp "$NUTTX/nuttx.hex" "$DIST/$name/"
        [ -f "$NUTTX/nuttx.uf2" ] && cp "$NUTTX/nuttx.uf2" "$DIST/$name/"
        cp "$NUTTX/.config" "$DIST/$name/defconfig"
        local size=$(stat -c%s "$NUTTX/nuttx.bin")
        printf "[fw] %-5s OK  nuttx.bin=%d B (%.1f KB)\n" "$name" "$size" \
               "$(python3 -c "print($size/1024)")"
        return 0
    else
        echo "[fw] $name FAILED（前 30 错误）:"
        tr '\r' '\n' < "/tmp/fw_$name.log" | grep -E ": error|undefined reference|错误" | head -30
        return 1
    fi
}

case "${1:-all}" in
    s3)   build_one s3   esp32s3-devkit  "$XT"  ;;
    s3n8) build_one s3n8 esp32s3-devkit  "$XT"  ;;
    cam)  build_one cam  esp32-wrover-kit "$XT" ;;
    c3)   build_one c3   esp32c3-devkit  "$RV:$TOOLS/shim-riscv64/bin"  ;;
    pico) build_one pico raspberrypi-pico "$ARM" ;;
    all)
        rc=0
        build_one pico raspberrypi-pico "$ARM" || rc=1
        build_one c3   esp32c3-devkit  "$RV:$TOOLS/shim-riscv64/bin"  || rc=1
        build_one cam  esp32-wrover-kit "$XT" || rc=1
        build_one s3   esp32s3-devkit  "$XT"  || rc=1
        build_one s3n8 esp32s3-devkit  "$XT"  || rc=1
        echo
        echo "[fw] 全部产物: $DIST"
        ls -la "$DIST"/*/nuttx.bin 2>/dev/null | awk '{printf "  %-46s %8.1f KB\n", $NF, $5/1024}'
        exit $rc
        ;;
    *) echo "用法: $0 [s3|cam|c3|pico|all]"; exit 1 ;;
esac
