/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * lv_font_fullwidth.h - 全角标点/符号 12px 点阵表查询接口
 *
 * WHAT : CJK 符号区/全角形式区/弯引号的全角字形查询（12px 满格、
 *        1bpp MSB-first 位连续流，与 LVGL 1bpp 同格式）
 * WHY  : Noto 全角标点 12px 光栅化墨迹仅 1-3px（！：分不清，
 *        2026-10-05 用户目视确认）；Fusion Pixel 全角标点为
 *        12px 逐像素设计（！满高、，。沉底），命中则优先生效，
 *        未命中码点回落 Noto 全量表
 * WHO  : 表由 scripts/gen_pixel_fonts.py 生成（lv_font_fullwidth.c）
 * WHERE: retro-ws/src/lvgl/fonts/lv_font_fullwidth.h；消费者
 *        src/nuttx/common/driver/cvbs_console.c
 * WHEN : 2026-10-05 新增（半格网格三次定稿配套）
 * HOW  : fullwidth_dsc() 返回 NULL 表示无该码点（回落 Noto）；
 *        ofs_y 与 lv_font_conv 同语义（基线-墨迹底）
 */

#ifndef __LV_FONT_FULLWIDTH_H
#define __LV_FONT_FULLWIDTH_H

#include <stdint.h>
#include <stddef.h>

struct fullwidth_glyph_s {
    uint32_t cp;     /* 码点（键，0=哨兵） */
    uint8_t w;       /* 墨迹宽（<=12px） */
    uint8_t h;       /* 墨迹高（12px 体系） */
    uint8_t ofs_x;   /* 格内水平偏移（满宽为 0） */
    int8_t  ofs_y;   /* 墨迹底在基线上方距离（下伸为负） */
    uint16_t offset; /* 位图池字节偏移 */
};

const struct fullwidth_glyph_s *fullwidth_dsc(uint32_t cp);
const uint8_t *fullwidth_bitmap(uint32_t cp);

#endif /* __LV_FONT_FULLWIDTH_H */
