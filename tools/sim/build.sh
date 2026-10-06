#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : GUI/CVBS 宿主机模拟构建脚本 / host simulator build
# WHY  : 把真实固件源（desktop/wmaker/i18n/lv_port_disp/drv_cvbs）
#        与宿主 LVGL 库链接成无头模拟器
# WHO  : 开发者与 CI（视觉验证闭环）
# WHERE: retro-ws/tools/sim/build.sh
# WHEN : 2026-10-04 新增
# HOW  : 1) LVGL 9.5 源码编译为 /tmp/liblvgl_host.a
#        2) lvgl_sim（桌面外壳渲染）与 cvbs_pipeline（全链路波形）
#        两个可执行文件；输出 PPM 到 /tmp/retro_sim/
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OBJ=/tmp/lvgl_obj
LVCONF_SRC="$ROOT/configs/lv_conf.h"
FONT_DEFS="-DCONFIG_LVGL_FONT_10=1 -DCONFIG_LVGL_FONT_12=1 -DCONFIG_LVGL_FONT_14=1 \
 -DCONFIG_LVGL_FONT_16=1 -DCONFIG_LVGL_FONT_18=1 -DCONFIG_LVGL_FONT_20=1 \
 -DCONFIG_LVGL_FONT_24=1 -DCONFIG_LVGL_FONT_28=1 -DCONFIG_LVGL_FONT_32=1"
LVGL_FLAGS="-I $ROOT/tests/host/stubs -I $ROOT/deps -DLV_CONF_INCLUDE_SIMPLE $FONT_DEFS -O1 -fPIC -Wno-unused-parameter"
PROJ_FLAGS="-I $ROOT/deps -I $ROOT/tests/host/stubs -I $ROOT/src/nuttx/common \
 -I $ROOT/src/nuttx/common/driver -I $ROOT/src/lvgl -I $ROOT/src/lvgl/fonts -I $ROOT/src/lvgl -I $ROOT/src/lvgl/fonts/app \
 -DLV_CONF_INCLUDE_SIMPLE $FONT_DEFS -DCONFIG_LVGL=1 \
 -DCONFIG_RETRO_DISPLAY_WIDTH=640 -DCONFIG_RETRO_DISPLAY_HEIGHT=480 \
 -DCONFIG_RETRO_DESKTOP_SHELL_WIN3=1 -DCONFIG_RETRO_FONT_CJK_FULL=1 \
 -I $ROOT/deps/my_basic/core -I $ROOT/deps/duktape/src -I $ROOT"

mkdir -p "$OBJ" /tmp/retro_sim

# 1. lv_conf.h 放到 deps/ 一级（LVGL 默认查找路径 ../../lv_conf.h）
cp -f "$LVCONF_SRC" "$ROOT/deps/lv_conf.h"

# 2. LVGL 主库（增量）
if [ ! -f /tmp/liblvgl_host.a ] || [ "$ROOT/configs/lv_conf.h" -nt /tmp/liblvgl_host.a ]; then
    echo "[sim] building LVGL host library..."
    for f in $(find "$ROOT/deps/lvgl/src" -name '*.c'); do
        o="$OBJ/$(echo "$f" | tr '/' '_').o"
        [ "$f" -nt "$o" ] && gcc -c $LVGL_FLAGS "$f" -o "$o"
    done
    ar rcs /tmp/liblvgl_host.a "$OBJ"/*.o
fi

# 3. 桌面外壳模拟器
echo "[sim] building lvgl_sim..."
gcc -Wall -Wextra -Wno-unused-parameter $PROJ_FLAGS -g \
    "$ROOT/tools/sim/lvgl_sim.c" \
    "$ROOT/src/lvgl/app/desktop.c" \
    "$ROOT/src/lvgl/app/wmaker_shell.c" \
    "$ROOT/src/lvgl/i18n.c" \
    "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
    "$ROOT/src/lvgl/fonts/lv_font_ascii_6.c" \
    "$ROOT/src/lvgl/fonts/lv_font_fullwidth.c" \
    "$ROOT/src/lvgl/retro_ui.c" \
    "$ROOT/src/lvgl/modules/retro_ui_js.c" \
    "$ROOT/src/lvgl/modules/retro_ui_bas.c" \
    "$ROOT/tools/sim/app_stubs.c" \
    "$ROOT/deps/my_basic/core/my_basic.c" \
    "$ROOT/deps/duktape/src/duktape.c" \
    "$ROOT/deps/duktape/src/duk_console.c" \
    "$ROOT/src/nuttx/common/driver/cvbs_core.c" \
    /tmp/liblvgl_host.a -lm -o /tmp/retro_sim/lvgl_sim

# 4. 全链路管线（真实 lv_port_disp + drv_cvbs + 波形解码）
echo "[sim] building cvbs_pipeline..."
gcc -Wall -Wextra -Wno-unused-parameter $PROJ_FLAGS -g \
    "$ROOT/tools/sim/cvbs_pipeline.c" \
    "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
    "$ROOT/src/lvgl/fonts/lv_font_ascii_6.c" \
    "$ROOT/src/lvgl/fonts/lv_font_fullwidth.c" \
    "$ROOT/src/lvgl/lv_port_disp.c" \
    "$ROOT/src/nuttx/common/driver/drv_cvbs.c" \
    "$ROOT/src/nuttx/common/driver/cvbs_core.c" \
    /tmp/liblvgl_host.a -lm -o /tmp/retro_sim/cvbs_pipeline

# 5. 彩色 GUI 截图（RGB565 -> PPM，PIL 转 256 色 PNG；README 配图）
echo "[sim] building lvgl_sim_color..."
gcc -Wall -Wextra -Wno-unused-parameter $PROJ_FLAGS -g \
    "$ROOT/tools/sim/lvgl_sim_color.c" \
    "$ROOT/src/lvgl/app/app_pinyin.c" \
    "$ROOT/src/lvgl/fonts/pinyin_ime.c" \
    "$ROOT/src/lvgl/app/desktop.c" \
    "$ROOT/src/lvgl/app/wmaker_shell.c" \
    "$ROOT/src/lvgl/i18n.c" \
    "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
    "$ROOT/src/lvgl/fonts/lv_font_ascii_6.c" \
    "$ROOT/src/lvgl/fonts/lv_font_fullwidth.c" \
    "$ROOT/src/lvgl/retro_ui.c" \
    "$ROOT/src/lvgl/modules/retro_ui_js.c" \
    "$ROOT/src/lvgl/modules/retro_ui_bas.c" \
    "$ROOT/tools/sim/app_stubs.c" \
    "$ROOT/deps/my_basic/core/my_basic.c" \
    "$ROOT/deps/duktape/src/duktape.c" \
    "$ROOT/deps/duktape/src/duk_console.c" \
    /tmp/liblvgl_host.a -lm -o /tmp/retro_sim/lvgl_sim_color

# 6. CLI 控制台截图（真实 cvbs_console 320x240；README 配图）
echo "[sim] building console_sim..."
gcc -Wall -Wextra -g -DCONFIG_RETRO_PINYIN_CLI=1 \
    -I "$ROOT/tests/host/stubs" \
    -I "$ROOT/src/nuttx/common" -I "$ROOT/src/nuttx/common/driver" \
    -I "$ROOT/src/lvgl/fonts" -I /tmp/fontbridge \
    "$ROOT/tools/sim/console_sim.c" \
    "$ROOT/src/nuttx/common/driver/cvbs_console.c" \
    "$ROOT/src/nuttx/common/driver/cvbs_ime.c" \
    "$ROOT/src/nuttx/common/driver/drv_pinyin.c" \
    "$ROOT/src/nuttx/common/driver/cvbs_core.c" \
    "$ROOT/src/nuttx/common/driver/lvgl_font_compat.c" \
    "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
    "$ROOT/src/lvgl/fonts/lv_font_ascii_6.c" \
    "$ROOT/src/lvgl/fonts/lv_font_fullwidth.c" \
    -o /tmp/retro_sim/console_sim

echo "[sim] OK: /tmp/retro_sim/{lvgl_sim,cvbs_pipeline,lvgl_sim_color,console_sim}"

# 7. 五板 CLI 通用模拟器（真实 cvbs_console/retro_gpio/my_basic/duktape；
#    板间差异由 -DRETRO_SIM_BOARD_* 编译期 profile 注入，引脚事实来自
#    各板 hw_*.h 占用表——与各板 board.c 同源）
BOARDS="pico s3 s3n8 cam c3"
for b in $BOARDS; do
    case "$b" in
        pico) BDEF=RETRO_SIM_BOARD_PICO;  BINC="$ROOT/src/nuttx/rp2040/board" ;;
        s3)   BDEF=RETRO_SIM_BOARD_S3;    BINC="$ROOT/src/nuttx/esp32s3/board" ;;
        s3n8) BDEF=RETRO_SIM_BOARD_S3N8;  BINC="$ROOT/src/nuttx/esp32s3/board" ;;
        cam)  BDEF=RETRO_SIM_BOARD_CAM;   BINC="$ROOT/src/nuttx/esp32/board" ;;
        c3)   BDEF=RETRO_SIM_BOARD_C3;    BINC="$ROOT/src/nuttx/esp32c3/board" ;;
    esac
    echo "[sim] building board_cli_$b..."
    gcc -Wall -Wextra -g -DCONFIG_RETRO_PINYIN_CLI=1 "-D$BDEF=1" \
        -I "$ROOT/tests/host/stubs" \
        -I "$ROOT/src/nuttx/common" -I "$ROOT/src/nuttx/common/driver" \
        -I "$BINC" \
        -I "$ROOT/src/lvgl/fonts" -I /tmp/fontbridge \
        -I "$ROOT/deps/my_basic/core" -I "$ROOT/deps/duktape/src" \
        "$ROOT/tools/sim/board_cli_sim.c" \
        "$ROOT/src/nuttx/common/driver/cvbs_console.c" \
        "$ROOT/src/nuttx/common/driver/cvbs_ime.c" \
        "$ROOT/src/nuttx/common/driver/drv_pinyin.c" \
        "$ROOT/src/nuttx/common/driver/cvbs_core.c" \
        "$ROOT/src/nuttx/common/driver/lvgl_font_compat.c" \
        "$ROOT/src/nuttx/common/driver/retro_gpio.c" \
        "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
        "$ROOT/src/lvgl/fonts/lv_font_ascii_6.c" \
        "$ROOT/src/lvgl/fonts/lv_font_fullwidth.c" \
        "$ROOT/deps/my_basic/core/my_basic.c" \
        "$ROOT/deps/duktape/src/duktape.c" \
        -lm -o "/tmp/retro_sim/board_cli_$b"
done

echo "[sim] OK: /tmp/retro_sim/{lvgl_sim,cvbs_pipeline,lvgl_sim_color,console_sim,board_cli_{pico,s3,s3n8,cam,c3}}"
