#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 中文字库生成管线 / CJK bitmap font generation pipeline
# WHY  : 简体中文是默认语言（AGENTS.md 7.1）；复古低分辨率用 1bpp
#        纯点阵（无抗锯齿，体积仅 4bpp 的 1/4）
# WHO  : 需要重生成/换字体时手工执行
# WHERE: retro-ws/scripts/convert_font.sh
# WHEN : 2026-10-04 定稿（实测可用管线）；2026-10-05 可移植性修订（npx 查找与 -o 相对路径）
# HOW  : 系统字体 NotoSansCJK-Regular.ttc --fonttools--> 单面 ttf
#        --lv_font_conv--> LVGL C 字库；两档：
#        唯一档   : Unicode 区段全量 UTF-8（AGENTS.md 7.3——不再有子集档）
#
# 用法: ./convert_font.sh [full|gb2312|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
FONTS_OUT="$PROJECT_ROOT/src/lvgl/fonts"
TTC="${TTC:-/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

command -v python3 >/dev/null || { echo "需要 python3"; exit 1; }
python3 -c "import fontTools" 2>/dev/null || pip3 install --break-system-packages fonttools
# npx 查找：优先 PATH；没有再扫常见安装位（不写死具体用户路径）
if ! command -v npx >/dev/null 2>&1; then
    NODE_BIN="$(dirname "$(find "$HOME/.nvm" /usr/local /opt -maxdepth 6 -name npx -type f 2>/dev/null | head -1)")"
    [ -n "$NODE_BIN" ] && export PATH="$NODE_BIN:$PATH"
fi
command -v npx >/dev/null || { echo "需要 node/npx（lv_font_conv）"; exit 1; }

# 1. ttc -> SC 单面 ttf（opentype.js 不认集合字体）
python3 - "$TTC" "$WORK/NotoSansSC-Regular.ttf" <<'EOF'
import sys
from fontTools.ttLib import TTCollection
ttc = TTCollection(sys.argv[1])
for f in ttc.fonts:
    if 'SC' in (f['name'].getDebugName(4) or ''):
        f.save(sys.argv[2]); sys.exit(0)
sys.exit('SC 面未找到')
EOF
SRC_TTF="$WORK/NotoSansSC-Regular.ttf"
echo "[font] SC face: $SRC_TTF"

# 2. GB2312 全字符集（gb2312 档用）
python3 - "$WORK/gb2312.txt" <<'EOF'
import sys
chars = []
for hi in range(0xA1, 0xF8):
    for lo in range(0xA1, 0xFF):
        try:
            chars.append(bytes([hi, lo]).decode('gb2312'))
        except UnicodeDecodeError:
            pass
open(sys.argv[1], 'w').write(''.join(chars))
EOF

gen_full() {
    # 唯一字号 12px（2026-10-05 用户定稿：嵌入式体积优先，CLI/GUI 共用
    # 一个字号档；历史考证——中文 Win3.2/95 界面宋体 9pt=12px 点阵，
    # 见 HARDWARE.md 6.4）。1bpp 无 AA 纯点阵、Unicode 全量区段
    # 注意：-o 用相对路径执行（cd 到仓库根），避免生成文件头注释里
    # 嵌入开发机绝对路径
    echo "[font] generating 12px (Unicode CJK ranges, 1bpp)..."
    (cd "$PROJECT_ROOT" && npx lv_font_conv --no-prefilter --bpp 1 --size 12 \
        --font "$SRC_TTF" \
        -r 0x20-0x7F,0x3000-0x303F,0x4E00-0x9FFF,0xFF00-0xFFEF,0x2018-0x201D,0x2026 \
        --format lvgl -o "src/lvgl/fonts/lv_font_notosans_sc_12.c")
}

case "${1:-full}" in
    full|all) gen_full ;;
    *) echo "用法: $0 [full]（唯一 UTF-8 全量档）"; exit 1 ;;
esac

echo "[font] done. 生成后请按各文件 5W1H 头补注释（头会随再生成丢失）。"
ls -la "$FONTS_OUT"/lv_font_notosans_*.c 2>/dev/null
