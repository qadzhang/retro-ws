/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * lv_font_ascii_6.h - 半角拉丁 6px 等宽点阵表查询接口
 *
 * WHAT : U+0020-0x7E 半角字形 dsc/位图查询（半格 6px、12px 高、
 *        1bpp MSB-first 位连续流，与 LVGL 1bpp 同格式）
 * WHY  : cvbs_console 半格步进网格的 ASCII 专用点阵（宋体 9pt
 *        半角形态；Fusion Pixel 12px 等宽逐像素设计）
 * WHO  : 表由 scripts/gen_pixel_fonts.py 生成（lv_font_ascii_6.c）
 * WHERE: retro-ws/src/lvgl/fonts/lv_font_ascii_6.h；消费者
 *        src/nuttx/common/driver/cvbs_console.c
 * WHEN : 2026-10-05 新增（半格网格三次定稿配套）
 * HOW  : ascii6_dsc() 返回 NULL 表示无该码点字形；ofs_y 与
 *        lv_font_conv 同语义（基线-墨迹底），ofs_x 半格内居中
 */

#ifndef __LV_FONT_ASCII_6_H
#define __LV_FONT_ASCII_6_H

#include <stdint.h>
#include <stddef.h>

struct ascii6_glyph_s {
    uint32_t cp;     /* 码点（键，0=哨兵） */
    uint8_t w;       /* 墨迹宽（<=6px） */
    uint8_t h;       /* 墨迹高（12px 体系，与全量表同） */
    uint8_t ofs_x;   /* 半格内水平偏移（居中） */
    int8_t  ofs_y;   /* 墨迹底在基线上方距离（下伸为负） */
    uint16_t offset; /* 位图池字节偏移 */
};

const struct ascii6_glyph_s *ascii6_dsc(uint32_t cp);
const uint8_t *ascii6_bitmap(uint32_t cp);

#endif /* __LV_FONT_ASCII_6_H */
