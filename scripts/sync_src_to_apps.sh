#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 项目源同步进 NuttX apps / stage src into nuttx-apps module
# WHY  : NuttX 只编译 apps/ 内的模块；src/ 是仓库真身，
#        同步副本进 deps/nuttx-apps/retro（deps 不入库）
# WHO  : scripts/build_firmware.sh（每次构建前调用）
# WHERE: retro-ws/scripts/sync_src_to_apps.sh
# WHEN : 2026-10-04 新增
# HOW  : rsync 四棵树到 apps/retro/src/{common,esp32s3,esp32,esp32c3,rp2040}
#        + lvgl 源到 apps/retro/src/lvgl（CONFIG_RETRO_LVGL 时编译）
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/deps/nuttx-apps/retro"
mkdir -p "$DEST/src"
rsync -a --delete "$ROOT/src/nuttx/common/"    "$DEST/src/common/"
rsync -a --delete "$ROOT/src/nuttx/esp32s3/"   "$DEST/src/esp32s3/"
rsync -a --delete "$ROOT/src/nuttx/esp32/"     "$DEST/src/esp32/"
rsync -a --delete "$ROOT/src/nuttx/esp32c3/"   "$DEST/src/esp32c3/"
rsync -a --delete "$ROOT/src/nuttx/rp2040/"    "$DEST/src/rp2040/"
rsync -a --delete "$ROOT/src/lvgl/"            "$DEST/src/lvgl/"

# 模块骨架（Kconfig/Makefile/入口）从 firmware/retro-apps 取
cp "$ROOT/firmware/retro-apps/Kconfig"      "$DEST/Kconfig"
cp "$ROOT/firmware/retro-apps/Makefile"     "$DEST/Makefile"
cp "$ROOT/firmware/retro-apps/retro_boot.c" "$DEST/retro_boot.c"
cp "$ROOT/firmware/retro-apps/Make.defs"    "$DEST/Make.defs"
mkdir -p "$DEST/fontbridge/lvgl"
cp "$ROOT/firmware/retro-apps/fontbridge/lvgl/lvgl.h" "$DEST/fontbridge/lvgl/lvgl.h"
mkdir -p "$DEST/lv_conf_dir"
cp "$ROOT/configs/lv_conf.h" "$DEST/lv_conf_dir/lv_conf.h"
echo "[sync] staged $(find $DEST/src -name '*.c' | wc -l) .c files into $DEST"
