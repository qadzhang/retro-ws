/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * lvgl_font_compat.h - LVGL 字体文件的无 LVGL 编译兼容层
 *
 * WHAT : 让 lv_font_notosans_sc_12.c（LVGL fmt_txt 格式）在无 LVGL 的
 *        CLI 固件（C3/Pico 等档）里也能编译并查询字形
 * WHY  : AGENTS.md 7.3 铁律——唯一字体文件，GUI 与 CLI 共用；
 *        为 CLI 再生成一套裸字库会双份 911KB 且易失同步
 * WHO  : cvbs_console.c（字形渲染）；构建系统把本目录放在 -I 最前
 *        使 #include "lvgl.h" 命中本文件
 * WHERE: retro-ws/src/nuttx/common/driver/lvgl_font_compat.h
 * WHEN : 2026-10-04 新增
 * HOW  : 复刻 LVGL 9.5 lv_font_fmt_txt 的类型布局与查找语义
 *        （get_glyph_dsc_id 三种 cmap 类型查找逐行对照
 *        deps/lvgl/src/font/fmt_txt/lv_font_fmt_txt.c:283）；
 *        不含 kern/压缩（生成参数 --no-compress，kern 忽略不影响
 *        点阵渲染位置——adv_w 已含必要步进）
 */

#ifndef __LVGL_FONT_COMPAT_H
#define __LVGL_FONT_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*--------------------
 * 版本宏（让字体文件走 v9 分支）
 *--------------------*/

#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 5
#define LV_VERSION_CHECK(a, b, c) 0

/* 字体开关（默认开；构建可关） */
#ifndef LV_FONT_NOTOSANS_SC_16
#  define LV_FONT_NOTOSANS_SC_16 1
#endif

#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_SUBPX_NONE 0
typedef int lv_font_subpx_t;

/*--------------------
 * 类型布局（与 LVGL 9.5 逐字段一致）
 *--------------------*/

typedef struct {
    uint32_t bitmap_index;  /* 位图下标（全量字库 >64KB，必须 32 位） */
    uint16_t adv_w;      /* advance 宽（1/16px） */
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;        /* 基线以上为正 */
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint32_t bitmap_index;   /* deprecated 字段，fmt_txt 兼容位 */
} lv_font_fmt_txt_cmap_t_idx_;

enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY = 0,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL = 1,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY  = 2,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL  = 3,
};

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t *unicode_list;
    const uint8_t *glyph_id_ofs_list;
    uint16_t list_length;
    uint8_t type;
} lv_font_fmt_txt_cmap_t;

/* kern 对（本层不消费，仅为字体文件可编译） */
typedef struct {
    const void *glyph_ids;
    const int8_t *values;
    uint32_t pair_cnt      : 30;
    uint32_t glyph_ids_size : 2;
} lv_font_fmt_txt_kern_pair_t;

typedef struct {
    const uint8_t *glyph_bitmap;      /* LVGL 实为 void* */
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;             /* 本兼容层不使用 */
    uint16_t kern_scale;
    uint16_t cmap_num;
    uint8_t bpp;
    uint8_t kern_classes;
    uint16_t bitmap_format;
} lv_font_fmt_txt_dsc_t;

typedef struct {
    uint16_t adv_w;
    uint16_t box_h;
    uint16_t box_w;
    int16_t ofs_x;
    int16_t ofs_y;
    uint16_t stride;                  /* 兼容字段（1bpp 恒 0） */
} lv_font_glyph_dsc_t;

typedef struct _lv_font_t {
    bool (*get_glyph_dsc)(const struct _lv_font_t *, lv_font_glyph_dsc_t *,
                          uint32_t, uint32_t);
    const void *(*get_glyph_bitmap)(const struct _lv_font_t *, uint32_t);
    int32_t line_height;
    int32_t base_line;
    uint8_t subpx;
    int8_t underline_position;
    int8_t underline_thickness;
    const void *dsc;
    const void *fallback;
    void *user_data;
} lv_font_t;

/*--------------------
 * 字体文件引用的 LVGL 符号（实现见 lvgl_font_compat.c）
 *--------------------*/

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font,
                                   lv_font_glyph_dsc_t *dsc_out,
                                   uint32_t unicode_letter,
                                   uint32_t unicode_letter_next);
const void *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t cp);

/*--------------------
 * 字形查找（CLI 侧实现，retro_console 调用）
 *--------------------*/

/*
 * WHAT : 连续位流取位 / continuous-bitstream bit fetch
 * WHY  : LVGL 9.5 fmt_txt 在 stride==0 时 1bpp 位图行与行之间
 *        不按字节对齐（bit 连续排布，MSB 先行）——按行取整字节
 *        解码会得到乱码（实测 ASCII 6~10px 宽字形全乱）
 * HOW  : 全局 bit 偏移 = 行号 * box_w + 列号，再折算字节/位
 */
static inline int retro_compat_bit(const uint8_t *bm, int bit_off)
{
    return (bm[bit_off >> 3] >> (7 - (bit_off & 7))) & 1;
}

const lv_font_t *retro_compat_font(void);
bool retro_compat_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                            uint32_t cp);
const uint8_t *retro_compat_glyph_bitmap(const lv_font_t *font, uint32_t cp);

#endif /* __LVGL_FONT_COMPAT_H */
