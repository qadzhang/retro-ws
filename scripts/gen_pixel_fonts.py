#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# gen_pixel_fonts.py - Fusion Pixel 半角/全角标点点阵表生成器
#
# WHAT : 从 Fusion Pixel Font 12px 等宽（OFL-1.1，缝合像素字体）
#        生成 cvbs_console 专用两张 C 表：
#        1) lv_font_ascii_6.c    半角 U+0020-0x7E（6px 等宽半格）
#        2) lv_font_fullwidth.c  全角标点/符号（12px 满格）
# WHY  : 半格步进体系（2026-10-05，中文 Win3.2/95 宋体 9pt 半角/
#        全角点阵）——Noto Sans SC 是矢量比例字体，12px 光栅化后
#        拉丁墨迹 1-10px 宽窄不一（塞半格重叠/M 中拱消失）、全角
#        标点墨迹仅 1-3px（！：分不清）；Fusion 为 12px 逐像素设计
#        的等宽点阵（半角 adv=6、全角 adv=12），1:1 渲染无失真。
#        汉字主体仍用 Noto 全量表（2.1 万字形全量覆盖）
# WHO  : scripts/convert_font.sh 调用；换字体时随全量表一起再生成
# WHERE: retro-ws/scripts/gen_pixel_fonts.py
# WHEN : 2026-10-05 新增（半格网格三次定稿配套；当日由 Noto 压缩
#        方案 max-pooling+手工 M/W 切换为 Fusion 整源）
# HOW  : PIL FreeType 12px 渲染（像素字体 1:1，阈值 128 只滤渲染
#        亚像素灰阶）-> ofs_y=ascent-墨迹底（与 lv_font_conv 同
#        语义）-> MSB-first 位连续流（与 LVGL 1bpp 同格式）
#
# 用法: gen_pixel_fonts.py [latin.ttf] [zh_hans.ttf] [out_dir]

import sys
import os
from PIL import Image, ImageFont, ImageDraw

LATIN_TTF = sys.argv[1] if len(sys.argv) > 1 else \
    "deps/fonts/fusion-pixel-12px-monospaced-latin.ttf"
HANS_TTF = sys.argv[2] if len(sys.argv) > 2 else \
    "deps/fonts/fusion-pixel-12px-monospaced-zh_hans.ttf"
OUT_DIR = sys.argv[3] if len(sys.argv) > 3 else "src/lvgl/fonts"

THRESH = 128      # 像素字体本体无 AA，阈值只滤渲染器亚像素灰阶

# 全角标点/符号字符集（与全量表区段对齐：CJK 符号区 + 全角形式区
# + 弯引号；face 缺的字形自动跳过 -> console 回落 Noto 全量表）
FW_CHARS = (
    [chr(c) for c in range(0x3000, 0x3040)] +      # 。，、！？：；”’《》【】…—·￥ 等
    [chr(c) for c in range(0x2013, 0x201E)] +       # – — ― ‘ ’ “ ”
    [chr(0x2026)] +                                 # … 省略号
    [chr(c) for c in range(0xFF01, 0xFF5F)]         # ！＂＃…～ 全角形式
)


def render_glyph(font, ch):
    """返回 (rows 点阵串, ofs_y)；无墨迹返回 None"""
    x0, y0, x1, y1 = font.getbbox(ch)
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return None
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255)
    px = img.load()
    rows = ["".join("#" if px[x, y] >= THRESH else "." for x in range(w))
            for y in range(h)]
    if not any("#" in r for r in rows):
        return None
    return rows, font.getmetrics()[0] - y1   # 底边在基线上方（下伸为负）


def pack_bits(rows):
    """行点阵 -> MSB-first 位连续流（LVGL 1bpp stride==0 格式）"""
    out = bytearray()
    acc = 0
    nbits = 0
    for row in rows:
        for c in row:
            acc = (acc << 1) | (1 if c == "#" else 0)
            nbits += 1
            if nbits == 8:
                out.append(acc)
                acc = 0
                nbits = 0
    if nbits:
        out.append(acc << (8 - nbits))
    return out


def write_table(out_path, what, guard, cell_w, rows_map):
    """生成 dsc（cp 索引）+ 位图池 + 查询函数的 C 文件"""
    dsc = []
    pool = bytearray()

    for cp in sorted(rows_map):
        rows, ofs_y = rows_map[cp]
        w, h = len(rows[0]), len(rows)
        bits = pack_bits(rows)
        off = len(pool)
        pool.extend(bits)
        ox = 0 if w >= cell_w else (cell_w - w) // 2   # 窄字形格内居中
        dsc.append((cp, w, h, ox, ofs_y, off))

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("""/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * %s - %s
 *
 * WHAT : %s，%s_dsc/%s_bitmap 按码点查询
 * WHY  : cvbs_console 半格步进体系（2026-10-05）——Noto 矢量比例
 *        字形 12px 光栅化后宽窄不一（半角溢出重叠/全角标点墨迹
 *        过细分不清）；Fusion Pixel 为 12px 逐像素设计点阵
 *        （半角 adv=6 / 全角 adv=12），1:1 渲染无失真
 * WHO  : scripts/gen_pixel_fonts.py 生成（勿手改）；源字体
 *        Fusion Pixel Font 12px monospaced（OFL-1.1，
 *        https://github.com/TakWolf/fusion-pixel-font）
 * WHERE: retro-ws/%s
 * WHEN : 2026-10-05 新增（半格网格三次定稿配套）
 * HOW  : 1bpp MSB-first 位连续流（与 LVGL 1bpp 同格式）；
 *        ofs_y 与 lv_font_conv 同语义（基线-墨迹底）
 */

#include <stddef.h>

#include "%s.h"

""" % (os.path.basename(out_path)[:-2], what, what, guard, guard,
            out_path, os.path.basename(out_path)[:-2]))
        f.write("static const uint8_t g_%s_bitmap_pool[] = {\n" % guard)
        for i in range(0, len(pool), 16):
            f.write("    " + ",".join("0x%02x" % b for b in pool[i:i + 16]) + ",\n")
        f.write("};\n\n")

        f.write("static const struct %s_glyph_s g_%s[] = {\n" % (guard, guard))
        for cp, w, h, ox, oy, off in dsc:
            f.write("    {0x%04x, %d, %d, %d, %d, %d},\n" % (cp, w, h, ox, oy, off))
        f.write("    {0, 0, 0, 0, 0, 0},   /* 哨兵（cp=0 终止） */\n")
        f.write("};\n\n")

        f.write("""const struct %s_glyph_s *%s_dsc(uint32_t cp)
{
    for (int i = 0; g_%s[i].cp != 0; i++)
        if (g_%s[i].cp == cp)
            return &g_%s[i];
    return NULL;
}

const uint8_t *%s_bitmap(uint32_t cp)
{
    const struct %s_glyph_s *d = %s_dsc(cp);

    return d != NULL ? &g_%s_bitmap_pool[d->offset] : NULL;
}
""" % ((guard,) * 9))

    return len(dsc), len(pool)


# ---- 1. 半角表（latin face） ----
latin = ImageFont.truetype(LATIN_TTF, 12)
rows_map = {}
for cp in range(0x20, 0x7F):
    r = render_glyph(latin, chr(cp))
    if r is not None:
        rows_map[cp] = r
n1, b1 = write_table(os.path.join(OUT_DIR, "lv_font_ascii_6.c"),
                     "半角拉丁 6px 等宽点阵表（cvbs_console 专用）",
                     "ascii6", 6, rows_map)
print("ascii6    : %d glyphs, %d bytes" % (n1, b1))

# ---- 2. 全角标点/符号表（zh_hans face） ----
hans = ImageFont.truetype(HANS_TTF, 12)
rows_map = {}
for ch in FW_CHARS:
    cp = ord(ch)
    if cp in rows_map:
        continue
    r = render_glyph(hans, ch)
    if r is not None:
        rows_map[cp] = r
n2, b2 = write_table(os.path.join(OUT_DIR, "lv_font_fullwidth.c"),
                     "全角标点/符号 12px 点阵表（cvbs_console 专用）",
                     "fullwidth", 12, rows_map)
print("fullwidth : %d glyphs, %d bytes" % (n2, b2))
