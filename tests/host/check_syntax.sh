#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 全仓真实头文件语法检查矩阵 / whole-repo syntax matrix
# WHY  : 捕获幻觉 API/错误包含路径——所有 src/ 下的 C 文件必须
#        能在"真实 NuttX 12.12 + 真实 LVGL 9.5 头"下通过语法检查
# WHO  : CI / tests/host/run_matrix.sh 调用；开发者提交前
# WHERE: retro-ws/tests/host/check_syntax.sh
# WHEN : 2026-10-04 新增
# HOW  : 1) 组装 realinc/（arch->xtensa/include 软链、chip 链到
#           esp32s3 硬件头、最小 core-isa、空 config 兜底）
#        2) 逐文件 gcc -fsyntax-only（nuttx 树与 lvgl 树分开配置）
#        3) 输出 PASS/FAIL 清单，非零退出
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="$ROOT/deps"
RI="$ROOT/tests/host/realinc"

echo "[matrix] preparing real-include layer..."
rm -rf "$RI"
mkdir -p "$RI/nuttx" "$RI/archroot/arch" "$RI/chiproot/arch/chip"

# 空配置（真实头自带 OK/ERROR，避免与桩冲突）
cat > "$RI/nuttx/config.h" <<'EOF'
/* 真实头语法检查模式：仅提供无法从源码推断的最小配置 */
#define XTENSA_ESP32S3 1
#define CONFIG_ARCH_FAMILY_LX7 1
#define CONFIG_NFILE_DESCRIPTORS_PER_BLOCK 8
#define CONFIG_LIBC_MAX_EXITFUNS 16
#define CONFIG_MAX_TASKS 16
#define CONFIG_SMP_NCPUS 2
#define CONFIG_STREAM_OUT_BUFFER_SIZE 64
#define CONFIG_STREAM_HEXDUMP_BUFFER_SIZE 80
#define CONFIG_STREAM_BASE64_BUFFER_SIZE 64
#define CONFIG_SDCLKE_DISABLE 1
#define CONFIG_NUNGETS 8
#define CONFIG_MAX_FD_PATH 32
#define CONFIG_DEV_GPIO 1
#define CONFIG_BOARDCTL 1
#define CONFIG_BOARDCTL_RESET 1
#define CONFIG_BOARDCTL_RESET_CAUSE 1
#define CONFIG_VERSION_STRING "12.12.0-matrix"
#define CONFIG_RETRO_SCRIPT_TINYBASIC 1
#define CONFIG_RETRO_SCRIPT_DUKTAPE 1
EOF

# arch 层：arch/xxx -> xtensa/include/xxx
ln -sfn "$DEPS/nuttx/arch/xtensa/include/types.h" "$RI/archroot/arch/types.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/inttypes.h" "$RI/archroot/arch/inttypes.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/irq.h" "$RI/archroot/arch/irq.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/syscall.h" "$RI/archroot/arch/syscall.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/arch.h" "$RI/archroot/arch/arch.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/limits.h" "$RI/archroot/arch/limits.h"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/lx7" "$RI/archroot/arch/lx7"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/xtensa" "$RI/archroot_arch_xtensa"
mkdir -p "$RI/archroot/arch"
ln -sfn "$DEPS/nuttx/arch/xtensa/include/xtensa" "$RI/archroot/arch/xtensa_link"
mv "$RI/archroot/arch/xtensa_link" "$RI/archroot/arch/xtensa"
# esp-hal 的 xtensa 覆盖（项目自带 tie.h 等）优先
for f in $(find "$ROOT/src/nuttx/esp32s3/chip/esp-hal-3rdparty" -name '*.h' 2>/dev/null); do
    rel="${f#*$ROOT/src/nuttx/esp32s3/chip/esp-hal-3rdparty/components/xtensa/}"
    dest="$RI/archroot/arch/xtensa/${rel#include/}"
    mkdir -p "$(dirname "$dest")"
    ln -sfn "$f" "$dest"
done

# chip 层：esp32s3 硬件头 + 项目 tie.h 覆盖 + 最小 core-isa
for f in "$DEPS"/nuttx/arch/xtensa/src/esp32s3/hardware/esp32s3_*.h; do
    ln -sfn "$f" "$RI/chiproot/arch/chip/$(basename "$f")"
done
for f in "$DEPS"/nuttx/arch/xtensa/src/esp32s3/*.h; do
    ln -sfn "$f" "$RI/chiproot/arch/chip/$(basename "$f")"
done
ln -sfn "$RI/chiproot/arch/chip/esp32s3_irq.h" "$RI/chiproot/arch/chip/irq.h"

# 项目自带的 esp-hal tie.h 覆盖（AGENTS.md 11.5 路线：src 覆盖 deps）
hal_tie="$ROOT/src/nuttx/esp32s3/chip/esp-hal-3rdparty/components/xtensa/esp32s3/include/xtensa/config/tie.h"
[ -e "$hal_tie" ] && ln -sfn "$hal_tie" "$RI/chiproot/arch/chip/tie.h"
hal_tie_arch="$ROOT/src/nuttx/esp32s3/chip/esp-hal-3rdparty/components/xtensa/include/xt_utils.h"
mkdir -p "$RI/chiproot/arch/chip"

cat > "$RI/chiproot/arch/chip/core-isa.h" <<'EOF'
/* 宿主语法检查：最小 Xtensa LX7 core-isa（仅覆盖被引宏，不带语义验证） */
#ifndef _XTENSA_CORE_CONFIGURATION_H_
#define _XTENSA_CORE_CONFIGURATION_H_
#define XCHAL_HAVE_BE            0
#define XCHAL_HAVE_MMU           0
#define XCHAL_NUM_AREGS         64
#define XCHAL_HAVE_INTERRUPTS    1
#define XCHAL_HAVE_EXCEPTIONS    1
#define XCHAL_NUM_INTERRUPTS    32
#define XCHAL_EXCM_LEVEL         3
#define XCHAL_HAVE_DEBUG         1
#define XCHAL_NUM_IBREAK         2
#define XCHAL_NUM_DBREAK         2
#define XCHAL_DEBUGLEVEL         6
#define XCHAL_MAX_INSTRUCTION_SIZE 4
#define XCHAL_INST_FETCH_WIDTH   4
#endif
EOF

# arm/rp2040 架构层（types/inttypes 直接来自 xtensa 版布局近似——语法检查够用）
mkdir -p "$RI/armroot/arch"
cp "$RI/nuttx/config.h" /dev/null 2>/dev/null || true

# deps/nuttx/include 是源码头（保留），但其 arch/ 是 configure 生成的
# 软链（构建哪个板就指向哪个架构——c3 构建后 xtensa 文件被 riscv types
# 污染报 _int64_t 未定义，2026-10-05 事故）。realinc 放指向 xtensa 源的
# arch 软链并置于 -I 最前，优先级压制生成物：
ln -sfn "$DEPS/nuttx/arch/xtensa/include" "$RI/arch"

NUTTX_INC="-I $RI -I $DEPS/nuttx/include -I $RI/archroot -I $RI/chiproot \
 -I $ROOT/tests/host/stubs -I $ROOT \
 -I $ROOT/deps/my_basic/core -I $ROOT/deps/duktape/src \
 -I $ROOT/src/nuttx/common -I $ROOT/src/nuttx/common/driver \
 -I $ROOT/src/nuttx/common/apps/system \
 -I $ROOT/src/nuttx/esp32s3 -I $ROOT/src/nuttx/esp32s3/chip -I $ROOT/src/nuttx/esp32s3/board \
 -I $ROOT/src/nuttx/esp32 -I $ROOT/src/nuttx/esp32/chip -I $ROOT/src/nuttx/esp32/board \
 -I $ROOT/src/nuttx/esp32c3 -I $ROOT/src/nuttx/esp32c3/board \
 -I $ROOT/src/nuttx/rp2040 -I $ROOT/src/nuttx/rp2040/board \
 -I $DEPS/nuttx/arch/arm/src/rp2040 -I $DEPS/nuttx/arch/arm/src/rp2040/hardware \
 -I $DEPS/nuttx/arch/arm/src/armv6m -I $DEPS/nuttx/arch/arm/src/common \
 -I $DEPS/nuttx/arch/arm/include"

# esp32(CAM) 的 chip 层用 esp32 硬件头（单独 root）
mkdir -p "$RI/chiproot_esp32/arch/chip"
for f in "$DEPS"/nuttx/arch/xtensa/src/esp32/hardware/esp32_*.h; do
    [ -e "$f" ] && ln -sfn "$f" "$RI/chiproot_esp32/arch/chip/$(basename "$f")"
done
for f in "$DEPS"/nuttx/arch/xtensa/src/esp32/*.h; do
    [ -e "$f" ] && ln -sfn "$f" "$RI/chiproot_esp32/arch/chip/$(basename "$f")"
done
[ -e "$RI/chiproot_esp32/arch/chip/esp32_irq.h" ] && \
    ln -sfn "$RI/chiproot_esp32/arch/chip/esp32_irq.h" "$RI/chiproot_esp32/arch/chip/irq.h"
cp "$RI/chiproot/arch/chip/core-isa.h" "$RI/chiproot_esp32/arch/chip/core-isa.h"
hal_tie32="$ROOT/src/nuttx/esp32/driver/../chip/esp32.h"
mkdir -p "$RI/chiproot_esp32/arch/chip"
ln -sfn "$RI/chiproot/arch/chip/tie.h" "$RI/chiproot_esp32/arch/chip/tie.h"

NUTTX_INC_ESP32="-I $RI -I $DEPS/nuttx/include -I $RI/archroot -I $RI/chiproot_esp32 \
 -I $ROOT/tests/host/stubs -I $ROOT \
 -I $ROOT/deps/my_basic/core -I $ROOT/deps/duktape/src \
 -I $ROOT/src/nuttx/common -I $ROOT/src/nuttx/common/driver \
 -I $ROOT/src/nuttx/esp32 -I $ROOT/src/nuttx/esp32/chip -I $ROOT/src/nuttx/esp32/board"

# LVGL 侧
cp -f "$ROOT/configs/lv_conf.h" "$DEPS/lv_conf.h"
FONT_DEFS="-DCONFIG_LVGL_FONT_10=1 -DCONFIG_LVGL_FONT_12=1 -DCONFIG_LVGL_FONT_14=1 \
 -DCONFIG_LVGL_FONT_16=1 -DCONFIG_LVGL_FONT_18=1 -DCONFIG_LVGL_FONT_20=1 \
 -DCONFIG_LVGL_FONT_24=1 -DCONFIG_LVGL_FONT_28=1 -DCONFIG_LVGL_FONT_32=1"
LVGL_INC="-I $ROOT/tests/host/stubs -I $DEPS -I $ROOT -I $ROOT/src/nuttx/common \
 -I $ROOT/src/lvgl -I $ROOT/src/lvgl/app -I $ROOT/deps/my_basic/core \
 -I $ROOT/deps/duktape/src -DLV_CONF_INCLUDE_SIMPLE $FONT_DEFS \
 -DCONFIG_LVGL=1 -DCONFIG_RETRO_SCRIPT_TINYBASIC=1 -DCONFIG_RETRO_SCRIPT_DUKTAPE=1"

pass=0; fail=0; failed_files=""

check_one() {  # check_one <flags> <file>
    if gcc -fsyntax-only -Wall -Wno-unused-parameter -Wno-sign-compare \
           $1 "$2" 2>/tmp/synchk.err; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failed_files="$failed_files\n  FAIL $2"
        echo "---- $2"; head -6 /tmp/synchk.err
    fi
}

echo "[matrix] src/nuttx/** ..."
for f in $(find "$ROOT/src/nuttx" -name '*.c' | sort); do
    case "$f" in
        *esp32/driver/*|*esp32/board/*|*esp32/esp32_retro*)
            check_one "$NUTTX_INC_ESP32" "$f" ;;
        *)
            check_one "$NUTTX_INC" "$f" ;;
    esac
done

echo "[matrix] src/lvgl/** ..."
for f in $(find "$ROOT/src/lvgl" -name '*.c' | sort); do
    check_one "$LVGL_INC" "$f"
done

echo
echo "======================================"
echo -e "syntax matrix: PASS=$pass FAIL=$fail$failed_files"
[ "$fail" -eq 0 ]
