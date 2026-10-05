/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * cvbs_core.c - CVBS 复合视频可移植核心实现
 *
 * WHAT : 调色板/帧缓冲/绘制/PAL 行与场时序编码的硬件无关实现
 * WHY  : 见 cvbs_core.h 头注释（两板共用 + 宿主机可验证）
 * WHO  : drv_cvbs.c(S3)、drv_cvbs_dac.c(CAM)、tests/host CVBS 模拟器
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/cvbs_core.c
 * WHEN : 2026-10-04 新增
 * HOW  : 纯 C 无硬件依赖；帧缓冲堆分配；所有绘制越界裁剪。
 *        场时序为 PAL-B 工程近似（均衡/宽脉冲宽度按 13.5MHz 取整），
 *        精度足以被标准电视解码器锁定（retro 设备通行做法）
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "cvbs_core.h"

/*==========================
 *  调色板
 *==========================*/

/* 默认调色板：0..7 基本色，8..31 VGA 16 色，32..255 灰阶 */
static uint8_t g_cvbs_palette[256 * 3];

/*
 * WHAT : 初始化默认调色板 / build default palette
 * WHO  : 首次访问调色板时惰性调用
 * HOW  : 固定前 32 项 + 线性灰阶填充 32..255
 */
static void palette_init(void)
{
    static bool inited = false;
    if (inited)
        return;

    static const uint8_t base[32 * 3] = {
        0x00, 0x00, 0x00,   /* 0: 黑 */
        0xff, 0xff, 0xff,   /* 1: 白 */
        0xff, 0x00, 0x00,   /* 2: 红 */
        0x00, 0xff, 0x00,   /* 3: 绿 */
        0x00, 0x00, 0xff,   /* 4: 蓝 */
        0xff, 0xff, 0x00,   /* 5: 黄 */
        0x00, 0xff, 0xff,   /* 6: 青 */
        0xff, 0x00, 0xff,   /* 7: 紫 */
        0x00, 0x00, 0x00,   /* 8: 黑 */
        0x80, 0x00, 0x00,   /* 9: 暗红 */
        0x00, 0x80, 0x00,   /* 10: 暗绿 */
        0x80, 0x80, 0x00,   /* 11: 暗黄 */
        0x00, 0x00, 0x80,   /* 12: 暗蓝 */
        0x80, 0x00, 0x80,   /* 13: 暗紫 */
        0x00, 0x80, 0x80,   /* 14: 暗青 */
        0xc0, 0xc0, 0xc0,   /* 15: 亮灰 */
        0x80, 0x80, 0x80,   /* 16: 暗灰 */
        0xff, 0x00, 0x00,   /* 17: 红 */
        0x00, 0xff, 0x00,   /* 18: 绿 */
        0xff, 0xff, 0x00,   /* 19: 黄 */
        0x00, 0x00, 0xff,   /* 20: 蓝 */
        0xff, 0x00, 0xff,   /* 21: 紫 */
        0x00, 0xff, 0xff,   /* 22: 青 */
        0xff, 0xff, 0xff,   /* 23: 白 */
        0x00, 0x00, 0x00,   /* 24: 黑 */
        0x00, 0x00, 0x5f,   /* 25: 深蓝 */
        0x00, 0x00, 0x9e,   /* 26 */
        0x00, 0x00, 0xbe,   /* 27 */
        0x00, 0x00, 0xdf,   /* 28 */
        0x20, 0x00, 0x20,   /* 29: 深紫 */
        0x3f, 0x00, 0x3f,   /* 30 */
        0x5f, 0x00, 0x5f,   /* 31 */
    };

    memcpy(g_cvbs_palette, base, sizeof(base));

    for (int i = 32; i < 256; i++) {
        uint8_t gray = (uint8_t)((i - 32) * 255 / 223);
        g_cvbs_palette[i * 3 + 0] = gray;
        g_cvbs_palette[i * 3 + 1] = gray;
        g_cvbs_palette[i * 3 + 2] = gray;
    }

    inited = true;
}

int cvbs_core_set_palette(const uint8_t *palette, size_t count)
{
    if (palette == NULL)
        return -EINVAL;
    if (count > 256)
        count = 256;

    palette_init();
    memcpy(g_cvbs_palette, palette, count * 3);
    if (count < 256)
        memset(&g_cvbs_palette[count * 3], 0, (256 - count) * 3);
    return OK;
}

void cvbs_core_palette_get_rgb(uint8_t index, uint8_t *r, uint8_t *g,
                               uint8_t *b)
{
    palette_init();
    if (r) *r = g_cvbs_palette[index * 3 + 0];
    if (g) *g = g_cvbs_palette[index * 3 + 1];
    if (b) *b = g_cvbs_palette[index * 3 + 2];
}

/*==========================
 *  行结构（每板采样时钟不同 -> 运行期行长布局）
 *==========================*/

/*
 * 行结构运行时可配（2026-10-04 晚，HARDWARE.md 6.2）：
 * 各板真外设只能整数分频出特定采样率（S3/CAM/C3=13.3333MHz、
 * Pico=13.5063MHz），行长按 64µs 换算成整数样本（853 或 864），
 * 四段比例保持 PAL 语义。默认 12/64/68/720=864（13.5MHz 基准）。
 */
static int g_h_front = CVBS_H_FRONT_PORCH;
static int g_h_sync  = CVBS_H_SYNC;
static int g_h_back  = CVBS_H_BACK_PORCH;
static int g_h_active = CVBS_H_ACTIVE;

int cvbs_core_set_line_layout(int front, int sync, int back, int active)
{
    int total = front + sync + back + active;

    /* 缓冲按编译期上限分配，运行期布局不得超过 */
    if (front <= 0 || sync <= 0 || back <= 0 || active <= 0 ||
        total > CVBS_LINE_TOTAL)
        return -EINVAL;

    g_h_front = front;
    g_h_sync = sync;
    g_h_back = back;
    g_h_active = active;
    return OK;
}

int cvbs_core_line_total(void)
{
    return g_h_front + g_h_sync + g_h_back + g_h_active;
}

/*==========================
 *  帧缓冲
 *==========================*/

static uint8_t *g_fb = NULL;
static int g_fb_w = 0;
static int g_fb_h = 0;

int cvbs_core_fb_alloc(int width, int height)
{
    /* 高度上限 576 = 两场之和：<=288 走 240p 单场，更大走隔行 */
    if (width <= 0 || height <= 0 ||
        width > g_h_active || height > CVBS_FIELD1_ACTIVE * 2)
        return -EINVAL;

    uint8_t *fb = malloc((size_t)width * (size_t)height);
    if (fb == NULL)
        return -ENOMEM;

    memset(fb, 0, (size_t)width * (size_t)height);

    free(g_fb);
    g_fb = fb;
    g_fb_w = width;
    g_fb_h = height;
    return OK;
}

void cvbs_core_fb_free(void)
{
    free(g_fb);
    g_fb = NULL;
    g_fb_w = 0;
    g_fb_h = 0;
}

uint8_t *cvbs_core_fb(void)
{
    return g_fb;
}

int cvbs_core_fb_width(void)
{
    return g_fb_w;
}

int cvbs_core_fb_height(void)
{
    return g_fb_h;
}

/*==========================
 *  绘制 API（越界裁剪，未分配时安全空操作）
 *==========================*/

void cvbs_clear(void)
{
    if (g_fb)
        memset(g_fb, 0, (size_t)g_fb_w * (size_t)g_fb_h);
}

void cvbs_draw_pixel(int x, int y, uint8_t color_index)
{
    if (g_fb == NULL || x < 0 || x >= g_fb_w || y < 0 || y >= g_fb_h)
        return;
    g_fb[(size_t)y * g_fb_w + x] = color_index;
}

void cvbs_draw_hline(int x1, int y, int x2, uint8_t color_index)
{
    if (g_fb == NULL || y < 0 || y >= g_fb_h)
        return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= g_fb_w) x2 = g_fb_w - 1;
    if (x1 > x2)
        return;
    memset(&g_fb[(size_t)y * g_fb_w + x1], color_index,
           (size_t)(x2 - x1 + 1));
}

void cvbs_draw_vline(int x, int y1, int y2, uint8_t color_index)
{
    if (g_fb == NULL || x < 0 || x >= g_fb_w)
        return;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= g_fb_h) y2 = g_fb_h - 1;
    for (int y = y1; y <= y2; y++)
        g_fb[(size_t)y * g_fb_w + x] = color_index;
}

void cvbs_draw_rect(int x, int y, int w, int h, uint8_t color_index)
{
    if (w <= 0 || h <= 0)
        return;
    cvbs_draw_hline(x, y, x + w - 1, color_index);
    cvbs_draw_hline(x, y + h - 1, x + w - 1, color_index);
    cvbs_draw_vline(x, y, y + h - 1, color_index);
    cvbs_draw_vline(x + w - 1, y, y + h - 1, color_index);
}

void cvbs_fill_rect(int x, int y, int w, int h, uint8_t color_index)
{
    if (g_fb == NULL)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g_fb_w)  w = g_fb_w - x;
    if (y + h > g_fb_h) h = g_fb_h - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = y; row < y + h; row++)
        memset(&g_fb[(size_t)row * g_fb_w + x], color_index, (size_t)w);
}

/*==========================
 *  亮度
 *==========================*/

/* 直通亮度模式开关（见头文件 5W1H） */
static bool g_direct_luma = false;

void cvbs_core_set_direct_luma(bool on)
{
    g_direct_luma = on;
}

uint8_t cvbs_rgb_to_y(uint8_t r, uint8_t g, uint8_t b)
{
    /* BT.601: Y = 0.299R + 0.587G + 0.114B，定点系数和 = 256 */
    return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
}

/*==========================
 *  行/场编码
 *==========================*/

size_t cvbs_core_encode_line(uint8_t *out, const uint8_t *fb_row,
                             int row_width)
{
    size_t idx = 0;
    int total = cvbs_core_line_total();

    memset(out + idx, CVBS_LEVEL_BLANK, g_h_front);
    idx += g_h_front;

    memset(out + idx, CVBS_LEVEL_SYNC, g_h_sync);
    idx += g_h_sync;

    memset(out + idx, CVBS_LEVEL_BLANK, g_h_back);
    idx += g_h_back;

    if (fb_row != NULL) {
        int x = 0;
        if (g_direct_luma) {
            /* L8 直通：x8 -> studio swing 16..235（永不低于消隐电平） */
            for (; x < row_width && x < g_h_active; x++)
                out[idx++] = (uint8_t)(16 + ((unsigned)fb_row[x] * 219) / 255);
        } else {
            for (; x < row_width && x < g_h_active; x++) {
                uint8_t r, g, b;
                cvbs_core_palette_get_rgb(fb_row[x], &r, &g, &b);
                out[idx++] = cvbs_rgb_to_y(r, g, b);
            }
        }
    }
    while (idx < (size_t)total)
        out[idx++] = CVBS_LEVEL_BLANK;

    return idx;
}

/*
 * 均衡脉冲行：2.35µs 同步 ×2（半行周期重复）
 * 脉宽按行长等比缩放（864 基准 40 样本）
 */
size_t cvbs_core_encode_equalizing(uint8_t *out)
{
    int total = cvbs_core_line_total();
    int eq = total * 40 / 864;
    int half = total / 2;

    memset(out, CVBS_LEVEL_BLANK, total);
    memset(out + g_h_front, CVBS_LEVEL_SYNC, eq);
    memset(out + g_h_front + half, CVBS_LEVEL_SYNC, eq);
    return total;
}

/*
 * 宽脉冲行（垂直同步）：27.3µs 同步 ×2
 * 脉宽按行长等比缩放（864 基准 369 样本）
 */
size_t cvbs_core_encode_broad_pulse(uint8_t *out)
{
    int total = cvbs_core_line_total();
    int bp = total * 369 / 864;
    int half = total / 2;

    memset(out, CVBS_LEVEL_BLANK, total);
    memset(out, CVBS_LEVEL_SYNC, bp);
    memset(out + half, CVBS_LEVEL_SYNC, bp);
    return total;
}

/*
 * 单行种类（Pico PIO 逐行生成用）
 */
enum cvbs_line_kind cvbs_core_line_kind(int n, bool odd_field,
                                        bool progressive)
{
    /* odd_field/progressive 只影响场行数（field_line_count），
     * 行类型序列对两场一致 */
    (void)odd_field;
    (void)progressive;

    if (n < 0)
        return CVBS_LINE_BLANK;

    /* 场同步区 0..8：3 均衡 + 3 宽脉冲 + 3 均衡 */
    if (n < 9)
        return (n >= 3 && n < 6) ? CVBS_LINE_BROAD : CVBS_LINE_EQ;

    /* 消隐行 9..23（15 行） */
    if (n < 24)
        return CVBS_LINE_BLANK;

    /* 活跃 24..311（288 行） */
    if (n < 24 + CVBS_FIELD1_ACTIVE)
        return CVBS_LINE_ACTIVE;

    /* 隔行奇场补 1 行消隐 */
    return CVBS_LINE_BLANK;
}

int cvbs_core_field_line_count(bool odd_field, bool progressive)
{
    int lines = 24 + CVBS_FIELD1_ACTIVE;
    if (odd_field && !progressive)
        lines += 1;
    return lines;
}

/*
 * WHAT : 编码场内第 n 行 / encode field line n in-place
 * WHY  : Pico 逐行 DMA 架构无法整场渲染（SRAM 放不下场缓冲），
 *        需要按行拉取；generate_field 也走本函数保证语义一致
 * 返回 : 编码字节数（=cvbs_core_line_total()），n 越界返回 0
 */
size_t cvbs_core_field_line(int n, bool odd_field, bool progressive,
                            uint8_t *out)
{
    switch (cvbs_core_line_kind(n, odd_field, progressive)) {
    case CVBS_LINE_EQ:
        return cvbs_core_encode_equalizing(out);
    case CVBS_LINE_BROAD:
        return cvbs_core_encode_broad_pulse(out);
    case CVBS_LINE_ACTIVE: {
        int fb_w = cvbs_core_fb_width();
        int fb_h = cvbs_core_fb_height();
        uint8_t *fb = cvbs_core_fb();
        int row = n - 24;
        int row0 = odd_field && !progressive ? CVBS_FIELD1_ACTIVE : 0;
        int fr = row0 + row;
        const uint8_t *fb_row =
            (fb && fr < fb_h) ? &fb[(size_t)fr * fb_w] : NULL;
        return cvbs_core_encode_line(out, fb_row, fb_w);
    }
    default:
        return cvbs_core_encode_line(out, NULL, 0);
    }
}

int cvbs_core_generate_field(bool odd_field, bool progressive,
                             cvbs_sink_t sink)
{
    uint8_t line[CVBS_LINE_TOTAL];
    int emitted = 0;

    if (sink == NULL)
        return -EINVAL;

    int lines = cvbs_core_field_line_count(odd_field, progressive);
    for (int n = 0; n < lines; n++) {
        size_t len = cvbs_core_field_line(n, odd_field, progressive, line);
        sink(line, len, emitted++);
    }

    return emitted;
}

int cvbs_core_generate_frame(bool progressive, cvbs_sink_t sink)
{
    if (sink == NULL)
        return -EINVAL;

    int n = cvbs_core_generate_field(false, false, sink);
    if (n < 0)
        return n;

    if (progressive) {
        /* 240p 复古模式：同相场 1 连发 + 1 行补足 = 625 行周期，
         * 保证 50Hz 帧率且每帧活跃行相位严格对齐 */
        int m = cvbs_core_generate_field(false, false, sink);
        if (m < 0)
            return m;

        uint8_t line[CVBS_LINE_TOTAL];
        cvbs_core_encode_line(line, NULL, 0);
        sink(line, cvbs_core_line_total(), n + m);
        return n + m + 1;
    }

    int m = cvbs_core_generate_field(true, false, sink);
    if (m < 0)
        return m;
    return n + m;                   /* 625 行隔行 */
}
