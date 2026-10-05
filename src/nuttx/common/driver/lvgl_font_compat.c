/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * lvgl_font_compat.c - fmt_txt 字形查找的 CLI 实现
 *
 * WHAT : 无 LVGL 构建下的 get_glyph_dsc/get_bitmap 等价实现
 * WHY  : 字体文件引用 lv_font_get_glyph_dsc_fmt_txt/
 *        lv_font_get_bitmap_fmt_txt 两个 LVGL 符号——CLI 固件没有 LVGL
 * WHO  : 字体文件里的函数指针（.get_glyph_dsc = ...）
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/lvgl_font_compat.c
 * WHEN : 2026-10-04 新增
 * HOW  : 语义逐行对照 LVGL 9.5 lv_font_fmt_txt.c（FORMAT0_TINY 直查、
 *        FORMAT0_FULL 带 0 偏移哨兵、SPARSE_TINY 二分 unicode_list；
 *        kern 不做——生成参数无压缩、点阵位置由 adv_w 决定）
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "lvgl_font_compat.h"

/* 字体文件提供 */
extern const lv_font_t lv_font_notosans_sc_12;

const lv_font_t *retro_compat_font(void)
{
    return &lv_font_notosans_sc_12;
}

/*
 * WHAT : cmap 里的 codepoint -> glyph_id（LVGL get_glyph_dsc_id 复刻）
 * 返回 : 0 = 无字形；>0 = glyph_dsc 下标
 */
static uint32_t glyph_id_of(const lv_font_t *font, uint32_t letter)
{
    const lv_font_fmt_txt_dsc_t *fdsc = font->dsc;

    if (letter == 0)
        return 0;

    for (uint16_t i = 0; i < fdsc->cmap_num; i++) {
        const lv_font_fmt_txt_cmap_t *cm = &fdsc->cmaps[i];
        uint32_t rcp = letter - cm->range_start;

        if (rcp >= cm->range_length)
            continue;

        uint32_t gid = 0;

        if (cm->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY) {
            gid = cm->glyph_id_start + rcp;
        }
        else if (cm->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL) {
            const uint8_t *ofs8 = cm->glyph_id_ofs_list;
            if (ofs8[rcp] == 0 && letter != cm->range_start)
                continue;           /* 0 偏移 = 缺字 */
            gid = cm->glyph_id_start + ofs8[rcp];
        }
        else if (cm->type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) {
            /* unicode_list 升序 -> 手写二分 */
            uint16_t lo = 0, hi = cm->list_length;
            while (lo < hi) {
                uint16_t mid = (lo + hi) / 2;
                if (cm->unicode_list[mid] < (uint16_t)rcp)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo < cm->list_length && cm->unicode_list[lo] == (uint16_t)rcp)
                gid = cm->glyph_id_start + lo;
        }
        /* SPARSE_FULL：本字体未使用（生成时无该类型） */

        return gid;
    }

    return 0;
}

bool retro_compat_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                            uint32_t cp)
{
    const lv_font_fmt_txt_dsc_t *fdsc = font->dsc;
    uint32_t gid = glyph_id_of(font, cp);

    if (gid == 0 || out == NULL)
        return false;

    const lv_font_fmt_txt_glyph_dsc_t *g = &fdsc->glyph_dsc[gid];

    out->adv_w = (g->adv_w + 8) >> 4;   /* 1/16px -> px */
    out->box_h = g->box_h;
    out->box_w = g->box_w;
    out->ofs_x = g->ofs_x;
    out->ofs_y = g->ofs_y;
    out->stride = 0;                    /* 1bpp 紧凑行 */

    return true;
}

const uint8_t *retro_compat_glyph_bitmap(const lv_font_t *font, uint32_t cp)
{
    const lv_font_fmt_txt_dsc_t *fdsc = font->dsc;
    uint32_t gid = glyph_id_of(font, cp);

    if (gid == 0)
        return NULL;

    return (const uint8_t *)fdsc->glyph_bitmap +
           fdsc->glyph_dsc[gid].bitmap_index;
}

/*--------------------
 * 字体文件引用的两个 LVGL 符号（弱接线到兼容实现）
 *--------------------*/

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font,
                                   lv_font_glyph_dsc_t *dsc_out,
                                   uint32_t unicode_letter,
                                   uint32_t unicode_letter_next)
{
    (void)unicode_letter_next;
    return retro_compat_glyph_dsc(font, dsc_out, unicode_letter);
}

const void *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t cp)
{
    return retro_compat_glyph_bitmap(font, cp);
}
