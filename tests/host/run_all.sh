#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 宿主机全量测试调度 / host test orchestrator
# WHY  : ai-code-testing 规范要求机器化收敛判定——一键跑齐
#        单元/差分/PBT/模糊/变异/基准六类守护，任一失败即非零退出
# WHO  : 开发者提交前、CI
# WHERE: retro-ws/tests/host/run_all.sh
# WHEN : 2026-10-04 新增
# HOW  : 依次构建并执行 test_pkgmanager / test_retro_gpio / test_cvbs /
#        fuzz_tar(短跑) / python 差分+PBT / mutation(>=80%) / bench；
#        汇总 PASS/FAIL
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=/tmp/retro_test
FAIL=0

step() { echo; echo "===== [run_all] $1 ====="; }

CC=${CC:-gcc}
STUBS="$ROOT/tests/host/stubs"
APPINC="$ROOT/src/nuttx/common/apps/system"
DRVINC="$ROOT/src/nuttx/common/driver"
SAN="-fsanitize=address,undefined -g"

rm -rf "$OUT"
mkdir -p "$OUT/sd" "$OUT/db"

step "build+unit pkg_manager"
if $CC -Wall -Wextra -Wno-unused-parameter $SAN -I "$STUBS" -I "$APPINC" \
      -DPKG_INSTALL_PREFIX="\"$OUT/sd\"" -DPKG_DB_ROOT="\"$OUT/db\"" \
      -DCONFIG_RETRO_ARCH='"xtensa-esp32s3"' \
      -DCONFIG_RETRO_ARCH_VAL='"xtensa-esp32s3"' \
      -DCONFIG_RETRO_FAMILY_VAL='"xtensa"' \
      -DCONFIG_RETRO_CHIP_VAL='"esp32s3"' \
      "$ROOT/tests/host/test_pkgmanager.c" -o "$OUT/test_pkg" 2>/tmp/ra1.log \
   && "$OUT/test_pkg" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra1.log)"; FAIL=1
fi

step "build+unit retro_gpio"
if $CC -Wall -Wextra $SAN -I "$STUBS" -I "$DRVINC" \
      "$ROOT/tests/host/test_retro_gpio.c" "$DRVINC/retro_gpio.c" \
      -o "$OUT/test_gpio" 2>/tmp/ra2.log \
   && "$OUT/test_gpio" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra2.log)"; FAIL=1
fi

step "build+unit cvbs_core"
if $CC -Wall -Wextra $SAN -I "$STUBS" -I "$DRVINC" \
      "$ROOT/tests/host/test_cvbs.c" "$DRVINC/cvbs_core.c" \
      -o "$OUT/test_cvbs" 2>/tmp/ra3.log \
   && "$OUT/test_cvbs" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3.log)"; FAIL=1
fi

step "build+unit cvbs_console(UTF-8 点阵控制台)"
# 字体桥：lv_font_notosans_sc_12.c include "lvgl/lvgl.h"，CLI 档用
# 兼容层桥接（firmware/retro-apps/fontbridge 为唯一真身）
rm -rf /tmp/fontbridge
mkdir -p /tmp/fontbridge/lvgl
cp "$ROOT/firmware/retro-apps/fontbridge/lvgl/lvgl.h" /tmp/fontbridge/lvgl/
if $CC -Wall -Wextra -g -fsanitize=address,undefined -I "$STUBS" \
      -I "$DRVINC" -I /tmp/fontbridge \
      "$ROOT/tests/host/test_cvbs_console.c" \
      "$DRVINC/cvbs_console.c" "$DRVINC/cvbs_core.c" \
      "$DRVINC/lvgl_font_compat.c" \
      "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
      -o "$OUT/test_con" 2>/tmp/ra3b.log \
   && "$OUT/test_con" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3b.log)"; FAIL=1
fi

step "build+unit hid_ascii（USB/BLE 键盘桥映射，输入优先级原则）"
if $CC -Wall -Wextra $SAN -I "$STUBS" -I "$DRVINC" \
      "$ROOT/tests/host/test_hid_ascii.c" "$DRVINC/hid_ascii.c" \
      -o "$OUT/test_hidascii" 2>/tmp/ra3d.log \
   && "$OUT/test_hidascii" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3d.log)"; FAIL=1
fi

step "build+unit ws2812_rmt 编码（契约+蜕变+差分+PBT+模糊）"
S3DRV="$ROOT/src/nuttx/esp32s3/driver"
if $CC -Wall -Wextra $SAN -I "$STUBS" -I "$S3DRV" \
      "$ROOT/tests/host/test_ws2812.c" "$S3DRV/ws2812_rmt.c" \
      -o "$OUT/test_ws2812" 2>/tmp/ra3c.log \
   && "$OUT/test_ws2812" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3c.log)"; FAIL=1
fi

step "build+unit 四板占用表契约（LED 避让 + 修正回归）"
B3="$ROOT/src/nuttx/esp32s3/board" B1="$ROOT/src/nuttx/esp32/board"
BC="$ROOT/src/nuttx/esp32c3/board" BP="$ROOT/src/nuttx/rp2040/board"
if $CC -Wall -Wextra -Wcomment $SAN -I "$STUBS" -I "$ROOT/tests/host" \
      -I "$B3" -I "$B1" -I "$BC" -I "$BP" \
      "$ROOT/tests/host/test_hw_profiles.c" \
      "$ROOT/tests/host/hw_tab_s3.c" "$ROOT/tests/host/hw_tab_cam.c" \
      "$ROOT/tests/host/hw_tab_c3.c" "$ROOT/tests/host/hw_tab_pico.c" \
      -o "$OUT/test_hw" 2>/tmp/ra3d.log \
   && "$OUT/test_hw" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3d.log)"; FAIL=1
fi

step "build+unit CCDOS 输入法（状态条/选字/回放/组合键）"
if $CC -Wall -Wextra $SAN -DCONFIG_RETRO_PINYIN_CLI=1 \
      -I "$STUBS" -I "$DRVINC" -I /tmp/fontbridge \
      "$ROOT/tests/host/test_ime.c" \
      "$DRVINC/cvbs_ime.c" "$DRVINC/cvbs_console.c" "$DRVINC/cvbs_core.c" \
      "$DRVINC/lvgl_font_compat.c" "$DRVINC/drv_pinyin.c" \
      "$ROOT/src/lvgl/fonts/lv_font_notosans_sc_12.c" \
      -o "$OUT/test_ime" 2>/tmp/ra3e.log \
   && "$OUT/test_ime" 2>&1 | grep -q '0 failed'; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra3e.log)"; FAIL=1
fi

step "fuzz tar (short)"
if $CC -Wall -Wextra -Wno-unused-parameter $SAN -I "$STUBS" -I "$APPINC" \
      -DPKG_INSTALL_PREFIX="\"$OUT/sd\"" -DPKG_DB_ROOT="\"$OUT/db\"" \
      "$ROOT/tests/host/fuzz_tar.c" -o "$OUT/fuzz_tar" 2>/tmp/ra4.log \
   && timeout 300 "$OUT/fuzz_tar" 2>&1 | grep -q PASS; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra4.log)"; FAIL=1
fi

step "python differential + PBT"
if $CC -Wall -Wextra -g -I "$STUBS" -I "$APPINC" \
      -DPKG_INSTALL_PREFIX='"/tmp/retro_pbt/sd"' \
      -DPKG_DB_ROOT='"/tmp/retro_pbt/db"' \
      "$ROOT/tests/host/rpk_tool.c" "$APPINC/pkg_manager.c" \
      -o "$OUT/rpk_tool" 2>/tmp/ra5.log \
   && $CC -Wall -Wextra -g -I "$STUBS" -I "$APPINC" \
      -DPKG_INSTALL_PREFIX='"/tmp/retro_pbt/sd"' \
      -DPKG_DB_ROOT='"/tmp/retro_pbt/db"' \
      -DCONFIG_RETRO_ARCH='"xtensa-esp32s3"' \
      "$ROOT/tests/host/rpk_tool.c" "$APPINC/pkg_manager.c" \
      -o "$OUT/rpk_tool_s3" 2>>/tmp/ra5.log \
   && RPK_TOOL="$OUT/rpk_tool" RPK_TOOL_S3="$OUT/rpk_tool_s3" \
      python3 "$ROOT/tests/host/python/test_rpkg_diff.py" 2>&1 | grep -q PASS; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra5.log)"; FAIL=1
fi

step "mutation gate (>=80%)"
if bash "$ROOT/tests/host/mutation/run_mutation.sh" 2>&1 | grep -q MUTATION-PASS; then
    echo ">>> PASS (pkg_manager)"
else
    echo ">>> FAIL"; FAIL=1
fi

step "mutation gate ws2812 (>=80%)"
if bash "$ROOT/tests/host/mutation/run_mutation_ws2812.sh" 2>&1 | grep -q WS2812-MUTATION-PASS; then
    echo ">>> PASS (ws2812)"
else
    echo ">>> FAIL"; FAIL=1
fi

step "bench gate"
if $CC -O2 -Wall -Wextra -Wno-unused-parameter -I "$STUBS" -I "$APPINC" -I "$DRVINC" \
      -DPKG_INSTALL_PREFIX="\"$OUT/sd\"" -DPKG_DB_ROOT="\"$OUT/db\"" \
      "$ROOT/tests/host/bench_core.c" "$DRVINC/cvbs_core.c" \
      -o "$OUT/bench_core" 2>/tmp/ra6.log \
   && "$OUT/bench_core" 2>&1 | grep -q PASS; then
    echo ">>> PASS"
else
    echo ">>> FAIL (see /tmp/ra6.log)"; FAIL=1
fi

step "CLI UTF-8 gate"
if python3 "$ROOT/tests/host/python/check_cli_utf8.py" 2>&1 | grep -q PASS; then
    echo ">>> PASS"
else
    echo ">>> FAIL"; FAIL=1
fi

echo
echo "======================================"
if [ "$FAIL" -eq 0 ]; then
    echo "[run_all] ALL PASS"
else
    echo "[run_all] FAILURES PRESENT"
fi
exit $FAIL
