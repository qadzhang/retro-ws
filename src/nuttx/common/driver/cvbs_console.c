/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * cvbs_console.c - AV 视频字符控制台实现（含 ANSI/CSI + 字符设备）
 *
 * WHAT : 见 cvbs_console.h 头注释；2026-10-04(晚) 升级为完整
 *        终端：ANSI CSI 子集（光标定位/清屏/清行/光标可见）、
 *        可见光标（下划线覆盖层）、/dev/cvbscon 字符设备
 *        （read=键盘输入环 write=渲染）、UART 输入泵任务
 * WHY  : AGENTS.md 7.3——全系 AV 输出 + 唯一 UTF-8 字体；
 *        NSH 与 nano 等全屏程序要跑在 AV 屏上（用户要求字符
 *        输出走 AV 视频输出），必须提供与 VT100 兼容的终端语义
 * WHO  : retro_boot / NSH(CONFIG_NSH_CONDEV=/dev/cvbscon) / nano
 *        / 测试 tests/host/test_cvbs_console.c
 * WHERE: retro-ws/src/nuttx/common/driver/cvbs_console.c
 * WHEN : 2026-10-04 新增；同日(晚)加 ANSI+字符设备+输入泵
 * HOW  : 字形 1bpp 行距 = ceil(box_w/8)；全角字 adv_w=12 对齐；
 *        direct luma 前景 255/背景 0；滚动 = 整屏 memmove；
 *        CSI 状态机：ESC→'['→参数→终字节(HfABCDJKsu 等)；
 *        写路径尾部 drv_cvbs_frame() 把 L8 帧缓冲推给硬件层
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "cvbs_core.h"
#include "cvbs_console.h"
#include "cvbs_ime.h"
#include "lvgl_font_compat.h"

/* 12px 字号配套网格（全系唯一字号，2026-10-05）：
 * 全角 adv=12、半角 adv=6；行高 14 = 12 + 2 行距
 * 320x240 -> 26 列 x 17 行；640x480 -> 53 列 x 34 行 */
#define CELL_W        12
#define CELL_H        14

/* 控制台状态 */
static struct {
    int      w, h;          /* 帧缓冲像素尺寸 */
    int      cols, rows;
    int      cx, cy;        /* 单元格光标 */
    uint32_t u8_acc;        /* UTF-8 解码累积 */
    int      u8_rem;        /* 剩余续字节 */
    bool     inited;
    bool     cursor_on;     /* 可见光标（下划线覆盖层） */
    uint8_t  attr;          /* SGR 属性位：bit0=反色（mono 语义） */
    /* CSI 状态机 */
    int      esc;           /* 0=无 1=ESC 2=CSI */
    int      csi_params[4];
    int      csi_nparam;
    bool     csi_private;   /* '?' 前缀 */
    bool     status_on;     /* 底部状态条占用末行（CCDOS 式 IME 条） */
    char     status_text[48];  /* 条文本（UTF-8） */
} g_con;

/*==========================
 *  帧缓冲原语
 *==========================*/

static void px(int x, int y, uint8_t v)
{
    uint8_t *fb = cvbs_core_fb();

    if (fb == NULL || x < 0 || y < 0 || x >= g_con.w || y >= g_con.h)
        return;
    fb[(size_t)y * g_con.w + x] = v;
}

static void fill_cell(int cell_x, int cell_y, uint8_t v)
{
    uint8_t *fb = cvbs_core_fb();

    if (fb == NULL)
        return;

    int x0 = cell_x * CELL_W;
    int y0 = cell_y * CELL_H;
    int x1 = x0 + CELL_W;
    int y1 = y0 + CELL_H;

    if (x1 > g_con.w) x1 = g_con.w;
    if (y1 > g_con.h) y1 = g_con.h;

    for (int y = y0; y < y1; y++)
        memset(&fb[(size_t)y * g_con.w + x0], v, (size_t)(x1 - x0));
}

/*
 * WHAT : 属性位 -> 前景/背景墨色 / attr to fg/bg ink
 * WHY  : SGR 反色（nano 快捷栏/标题栏）在 mono 屏 = 前景背景互换
 */
static uint8_t attr_bg(void)
{
    return (g_con.attr & 0x01) ? 255 : 0;
}

static uint8_t attr_fg(void)
{
    return (g_con.attr & 0x01) ? 0 : 255;
}

/*==========================
 *  字形渲染
 *==========================*/

/*
 * WHAT : 重画一个单元格（清背景 + 字形）/ redraw one cell
 * WHY  : 光标覆盖层移动后需恢复原字形
 */
static void draw_glyph(uint32_t cp, int cell_x, int cell_y);

static void redraw_cell(int cell_x, int cell_y)
{
    /* 单元格内容（字形）不留副本——光标层用异或式恢复：
     * 下划线只占底部 2px，重画时先清整格再画字形即可 */
    fill_cell(cell_x, cell_y, attr_bg());
}

static void glyph_put(uint32_t cp, int cell_x, int cell_y)
{
    const lv_font_t *font = retro_compat_font();
    lv_font_glyph_dsc_t d;

    if (!retro_compat_glyph_dsc(font, &d, cp) || d.box_w == 0) {
        fill_cell(cell_x, cell_y, attr_fg());
        return;
    }

    const uint8_t *bm = retro_compat_glyph_bitmap(font, cp);
    if (bm == NULL)
        return;

    int px0 = cell_x * CELL_W;
    int py0 = cell_y * CELL_H;

    /*
     * 基线对齐（2026-10-05 修复标点悬浮 BUG + 双重 py0 BUG）：
     * 原实现把字形墨迹框垂直居中——逗号/句号/下划线的墨迹本来就在
     * 基线附近及以下，居中后飘到行中间。正确做法：cell 基线取在底部
     * 光标线上方 2px；ofs_y 语义 = 字形底边在基线上方 ofs_y（兼容层
     * 与 LVGL 一致）。gy0 为**绝对像素行号**（含 py0），px 直接用。
     */
    int baseline = py0 + CELL_H - 3;          /* 底部留 2px 给光标 */
    int gy0 = baseline - (int)d.ofs_y - (int)d.box_h + 1;
    if (gy0 < py0)
        gy0 = py0;
    if (gy0 + (int)d.box_h > py0 + CELL_H - 1)
        gy0 = py0 + CELL_H - (int)d.box_h;

    uint8_t fg = attr_fg();

    for (int gy = 0; gy < (int)d.box_h; gy++) {
        for (int gx = 0; gx < (int)d.box_w; gx++) {
            int bit = retro_compat_bit(bm, gy * (int)d.box_w + gx);
            px(px0 + gx, gy0 + gy, bit ? fg : attr_bg());
        }
    }
}

static void draw_glyph(uint32_t cp, int cell_x, int cell_y)
{
    redraw_cell(cell_x, cell_y);
    glyph_put(cp, cell_x, cell_y);
}

/*==========================
 *  可见光标（下划线覆盖层）
 *==========================*/

static void cursor_draw(bool on)
{
    if (!g_con.inited)
        return;

    int x0 = g_con.cx * CELL_W;
    int y1 = g_con.cy * CELL_H + CELL_H - 2;
    int x1 = x0 + CELL_W;

    if (x1 > g_con.w) x1 = g_con.w;

    uint8_t ink = on ? (attr_fg() == 255 ? 255 : 255) : attr_bg();

    for (int y = y1; y < y1 + 2 && y < g_con.h; y++)
        for (int x = x0; x < x1; x++)
            px(x, y, ink);
}

static void cursor_move(int nx, int ny)
{
    cursor_draw(false);
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= g_con.cols) nx = g_con.cols - 1;
    if (ny >= g_con.rows) ny = g_con.rows - 1;
    g_con.cx = nx;
    g_con.cy = ny;
    if (g_con.cursor_on)
        cursor_draw(true);
}

/*==========================
 *  滚动 / 底部状态条（CCDOS 式）
 *==========================*/

/* 有效文本行数：状态条占用时正文不含末行（IME 条常驻、其他命令照常） */
static int rows_eff(void)
{
    return g_con.rows - (g_con.status_on ? 1 : 0);
}

static void statusbar_redraw(void)
{
    uint8_t *fb = cvbs_core_fb();
    int row = g_con.rows - 1;                 /* 末行像素区 */
    int y0 = row * CELL_H;

    if (fb == NULL || !g_con.status_on)
        return;

    /* 反色条：白底黑字（与 SGR 反色同视觉） */
    memset(fb + (size_t)y0 * g_con.w, 255,
           (size_t)CELL_H * g_con.w);

    /* 逐字符画到条内（临时反色属性；不推光标） */
    uint8_t saved_attr = g_con.attr;
    g_con.attr |= 0x01;
    int cell = 0;
    const unsigned char *p = (const unsigned char *)g_con.status_text;
    while (*p && cell < g_con.cols) {
        uint32_t cp;
        int n;

        if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = (*p & 0x1F) << 6 | (p[1] & 0x3F);
            n = 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F);
            n = 3;
        } else {
            cp = *p;
            n = 1;
        }

        glyph_put(cp, cell, row);
        cell += cp >= 0x1100 ? 2 : 1;         /* 与正文同宽进原则 */
        p += n;
    }
    g_con.attr = saved_attr;
}

static void scroll_one(void)
{
    uint8_t *fb = cvbs_core_fb();

    if (fb == NULL)
        return;

    /* 只滚正文区：末行是状态条（或无条时的最后正文行照常滚） */
    int text_rows = rows_eff();
    if (text_rows <= 0)
        return;

    size_t block = (size_t)text_rows * CELL_H * g_con.w;

    memmove(fb, fb + (size_t)CELL_H * g_con.w,
            block - (size_t)CELL_H * g_con.w);
    memset(fb + block - (size_t)CELL_H * g_con.w, 0,
           (size_t)CELL_H * g_con.w);
}

static void newline(void)
{
    g_con.cx = 0;
    if (g_con.cy + 1 >= rows_eff()) {
        scroll_one();
        fill_cell(0, rows_eff() - 1, 0);
        g_con.cy = rows_eff() - 1;
    } else {
        g_con.cy++;
    }
}

static void erase_screen(int mode)
{
    uint8_t *fb = cvbs_core_fb();

    if (fb == NULL)
        return;

    /* 0=光标到尾 1=头到光标 2=全清 */
    if (mode == 2) {
        memset(fb, 0, (size_t)g_con.w * g_con.h);
        cursor_move(0, 0);
        return;
    }

    int row_bytes = g_con.w * CELL_H;
    if (mode == 0) {
        for (int x = g_con.cx; x < g_con.cols; x++)
            fill_cell(x, g_con.cy, 0);
        if (g_con.cy + 1 < g_con.rows)
            memset(fb + (size_t)(g_con.cy + 1) * row_bytes, 0,
                   (size_t)(g_con.rows - g_con.cy - 1) * row_bytes);
    } else {
        if (g_con.cy > 0)
            memset(fb, 0, (size_t)g_con.cy * row_bytes);
        memset(fb + (size_t)g_con.cy * row_bytes, 0,
               (size_t)g_con.w * CELL_H);
    }
}

static void erase_line(int mode)
{
    /* 0=光标到行尾 1=行头到光标 2=整行 */
    if (mode == 2) {
        fill_cell(0, g_con.cy, 0);
        return;
    }

    uint8_t *fb = cvbs_core_fb();
    if (fb == NULL)
        return;

    int y0 = g_con.cy * CELL_H;
    if (mode == 0) {
        for (int x = g_con.cx; x < g_con.cols; x++)
            fill_cell(x, g_con.cy, 0);
    } else {
        for (int x = 0; x <= g_con.cx && x < g_con.cols; x++)
            fill_cell(x, g_con.cy, 0);
    }
    (void)y0;
}

/*==========================
 *  ANSI/CSI 解析
 *==========================*/

static void csi_dispatch(int final)
{
    int p0 = g_con.csi_nparam > 0 ? g_con.csi_params[0] : 0;

    switch (final) {
    case 'H': case 'f': {                    /* CUP 光标定位 */
        int row = g_con.csi_nparam > 0 ? g_con.csi_params[0] : 1;
        int col = g_con.csi_nparam > 1 ? g_con.csi_params[1] : 1;
        cursor_move(col - 1, row - 1);
        return;
    }
    case 'A':                                /* CUU 上 */
        cursor_move(g_con.cx, g_con.cy - (p0 ? p0 : 1));
        return;
    case 'B':                                /* CUD 下 */
        cursor_move(g_con.cx, g_con.cy + (p0 ? p0 : 1));
        return;
    case 'C':                                /* CUF 右 */
        cursor_move(g_con.cx + (p0 ? p0 : 1), g_con.cy);
        return;
    case 'D':                                /* CUB 左 */
        cursor_move(g_con.cx - (p0 ? p0 : 1), g_con.cy);
        return;
    case 'G':                                /* CHA 列定位 */
        cursor_move((p0 ? p0 : 1) - 1, g_con.cy);
        return;
    case 'd':                                /* VPA 行定位 */
        cursor_move(g_con.cx, (p0 ? p0 : 1) - 1);
        return;
    case 'J':                                /* ED 清屏 */
        erase_screen(p0);
        return;
    case 'K':                                /* EL 清行 */
        erase_line(p0);
        return;
    case 'm': {                              /* SGR：0 复位 / 7 反色 / 27 退反色 */
        for (int i = 0; i < g_con.csi_nparam || i == 0; i++) {
            int p = (i < g_con.csi_nparam) ? g_con.csi_params[i] : 0;

            switch (p) {
            case 0:
                g_con.attr = 0;
                break;
            case 7:
                g_con.attr |= 0x01;
                break;
            case 27:
                g_con.attr &= (uint8_t)~0x01;
                break;
            default:
                break;          /* bold/颜色等 mono 屏无视觉差异 */
            }
            if (g_con.csi_nparam == 0)
                break;
        }
        return;
    }
    case 'h': case 'l':                      /* 私有模式（含 ?25 光标） */
        if (g_con.csi_private && p0 == 25) {
            g_con.cursor_on = (final == 'h');
            if (g_con.cursor_on)
                cursor_draw(true);
        }
        return;                              /* bracketed paste 等忽略 */
    default:
        return;                              /* 未知序列安全忽略 */
    }
}

/*
 * WHAT : 单字节入 CSI 状态机；返回 true 表示已消费
 */
static bool csi_feed(unsigned char c)
{
    switch (g_con.esc) {
    case 0:
        if (c == 0x1b) {
            g_con.esc = 1;
            return true;
        }
        return false;

    case 1:
        if (c == '[') {
            g_con.esc = 2;
            g_con.csi_nparam = 0;
            g_con.csi_params[0] = 0;
            g_con.csi_private = false;
            return true;
        }
        if (c == ']') {                      /* OSC：吞到 BEL/ESC\ */
            g_con.esc = 3;
            return true;
        }
        g_con.esc = 0;                       /* 独立 ESC：丢弃 */
        return true;

    case 2:
        if (c == '?') {
            g_con.csi_private = true;
            return true;
        }
        if (c >= '0' && c <= '9') {
            if (g_con.csi_nparam == 0)
                g_con.csi_nparam = 1;        /* 隐式第一参数 */
            if (g_con.csi_nparam <= 4)
                g_con.csi_params[g_con.csi_nparam - 1] =
                    g_con.csi_params[g_con.csi_nparam - 1] * 10 + (c - '0');
            return true;
        }
        if (c == ';') {
            if (g_con.csi_nparam < 4) {
                g_con.csi_nparam++;
                g_con.csi_params[g_con.csi_nparam - 1] = 0;
            }
            return true;
        }
        if (c >= 0x40 && c <= 0x7e) {
            g_con.esc = 0;
            csi_dispatch(c);
            return true;
        }
        g_con.esc = 0;                       /* 非法 CSI */
        return true;

    case 3:                                  /* OSC 内容 */
        if (c == 0x07 || c == '\\')
            g_con.esc = 0;
        return true;

    default:
        g_con.esc = 0;
        return true;
    }
}

/*==========================
 *  公开接口
 *==========================*/

int cvbs_console_init(void)
{
    int w = cvbs_core_fb_width();
    int h = cvbs_core_fb_height();

    if (w <= 0 || h <= 0)
        return -ENODEV;

    memset(&g_con, 0, sizeof(g_con));
    g_con.w = w;
    g_con.h = h;
    g_con.cols = w / CELL_W;
    g_con.rows = h / CELL_H;
    g_con.inited = true;
    g_con.cursor_on = true;

    cvbs_core_set_direct_luma(true);
    memset(cvbs_core_fb(), 0, (size_t)w * h);

    return 0;
}

void cvbs_console_putc(char c)
{
    if (!g_con.inited)
        return;

    /* CSI/OSC 序列优先（全屏程序驱动） */
    if (g_con.esc != 0 || (uint8_t)c == 0x1b) {
        if (csi_feed((unsigned char)c))
            return;
    }

    if (g_con.u8_rem > 0) {
        if (((uint8_t)c & 0xC0) == 0x80) {
            g_con.u8_acc = (g_con.u8_acc << 6) | ((uint8_t)c & 0x3F);
            if (--g_con.u8_rem == 0) {
                cursor_draw(false);
                draw_glyph(g_con.u8_acc, g_con.cx, g_con.cy);

                /* 按字形步进宽度推进（2026-10-05 修复：全角恒 +1 致
                 * 排版错位——CJK/全角 adv=12 > CELL_W/2 应占 2 格） */
                int step = 1;
                lv_font_glyph_dsc_t d;
                if (retro_compat_glyph_dsc(retro_compat_font(), &d,
                                           g_con.u8_acc) &&
                    d.adv_w > CELL_W / 2)
                    step = 2;

                g_con.cx += step;
                if (g_con.cx >= g_con.cols)
                    newline();
                if (g_con.cursor_on)
                    cursor_draw(true);
            }
            return;
        }
        g_con.u8_rem = 0;
    }

    if ((uint8_t)c < 0x80) {
        switch (c) {
        case '\n':
            cursor_draw(false);
            newline();
            if (g_con.cursor_on)
                cursor_draw(true);
            return;
        case '\r':
            cursor_draw(false);
            g_con.cx = 0;
            if (g_con.cursor_on)
                cursor_draw(true);
            return;
        case '\b':
            cursor_draw(false);
            if (g_con.cx > 0)
                g_con.cx--;
            fill_cell(g_con.cx, g_con.cy, 0);
            if (g_con.cursor_on)
                cursor_draw(true);
            return;
        case '\t':
            cursor_draw(false);
            g_con.cx = (g_con.cx + 4) & ~3;
            if (g_con.cx >= g_con.cols)
                newline();
            if (g_con.cursor_on)
                cursor_draw(true);
            return;
        default:
            break;
        }
        if (c < ' ')
            return;

        cursor_draw(false);
        draw_glyph((uint32_t)(uint8_t)c, g_con.cx, g_con.cy);
        if (++g_con.cx >= g_con.cols)
            newline();
        if (g_con.cursor_on)
            cursor_draw(true);
        return;
    }

    /* UTF-8 首字节 */
    cursor_draw(false);
    if (((uint8_t)c & 0xE0) == 0xC0) {
        g_con.u8_acc = (uint8_t)c & 0x1F;
        g_con.u8_rem = 1;
    } else if (((uint8_t)c & 0xF0) == 0xE0) {
        g_con.u8_acc = (uint8_t)c & 0x0F;
        g_con.u8_rem = 2;
    } else if (((uint8_t)c & 0xF8) == 0xF0) {
        g_con.u8_acc = (uint8_t)c & 0x07;
        g_con.u8_rem = 3;
    } else {
        draw_glyph(0xFFFD, g_con.cx, g_con.cy);
        if (++g_con.cx >= g_con.cols)
            newline();
    }
    if (g_con.cursor_on)
        cursor_draw(true);
}

int cvbs_console_write(const char *buf, size_t len)
{
    if (!g_con.inited || buf == NULL)
        return -ENODEV;

    for (size_t i = 0; i < len; i++)
        cvbs_console_putc(buf[i]);

    /* 推一帧给硬件层（S3=LCD_CAM 环重写 / C3=PDM 重调制 /
     * Pico=生成器直读 fb 无需操作）。weak：宿主测试不链硬件层 */
    {
        extern void drv_cvbs_frame(void) __attribute__((weak));

        if (drv_cvbs_frame != NULL)
            drv_cvbs_frame();
    }

    return (int)len;
}

/*
 * WHAT : CCDOS 式底部状态条开关（IME 常驻条；2026-10-05）
 * WHY  : CLI 拼音输入法（ime on）在屏幕最下方占一行常驻条显示
 *        拼音/候选，正文区其余行照常滚动——DOS 时代 CCDOS 的
 *        显示处理方式（用户需求）
 * HOW  : on=末行划归状态条（正文 rows_eff-1，重画反色条+文本）；
 *        off=释放末行恢复整屏；text 非 NULL 时同时更新条文本
 */
void cvbs_console_statusbar(bool on, const char *text)
{
    if (!g_con.inited)
        return;

    if (text != NULL) {
        strncpy(g_con.status_text, text, sizeof(g_con.status_text) - 1);
        g_con.status_text[sizeof(g_con.status_text) - 1] = '\0';
    }

    if (on == g_con.status_on) {
        if (on)
            statusbar_redraw();
        return;
    }

    if (on) {
        if (g_con.cy >= g_con.rows - 1)     /* 光标在末行则上移入正文 */
            g_con.cy = g_con.rows - 2;
        g_con.status_on = true;
        statusbar_redraw();
    } else {
        uint8_t *fb = cvbs_core_fb();
        g_con.status_on = false;
        if (fb != NULL)                     /* 清整个末行像素区 */
            memset(fb + (size_t)(g_con.rows - 1) * CELL_H * g_con.w,
                   0, (size_t)CELL_H * g_con.w);
        memset(g_con.status_text, 0, sizeof(g_con.status_text));
    }
}

/*==========================
 *  输入环（通用层：IME 行回放/测试与设备 read 共用）
 *==========================*/

#define CVBS_CON_IN_SIZE   64

static unsigned char g_in_ring[CVBS_CON_IN_SIZE];
static volatile int g_in_head = 0;
static volatile int g_in_tail = 0;

void cvbs_console_input_push(char c)
{
    int next = (g_in_head + 1) % CVBS_CON_IN_SIZE;

    if (next == g_in_tail)
        return;                              /* 满丢弃（键盘速率低） */

    g_in_ring[g_in_head] = (unsigned char)c;
    g_in_head = next;
}

int cvbs_console_input_pop(char *c)
{
    if (g_in_tail == g_in_head)
        return 0;

    *c = (char)g_in_ring[g_in_tail];
    g_in_tail = (g_in_tail + 1) % CVBS_CON_IN_SIZE;
    return 1;
}

/* WHAT : 光标下划线可见性开关（隐藏时写入不再覆盖底部 2px 带）
 * HOW  : 隐藏前先擦当前线；开启后即时重画 */
void cvbs_console_cursor_visible(bool on)
{
    if (!g_con.inited || on == g_con.cursor_on)
        return;
    if (!on)
        cursor_draw(false);
    g_con.cursor_on = on;
    if (on)
        cursor_draw(true);
}

int cvbs_console_cols(void)  { return g_con.cols; }
int cvbs_console_rows(void)  { return g_con.rows; }

/* WHAT : 帧缓冲只读访问（cvbs_core direct luma；测试导出渲染图用）
 * HOW  : 未初始化返回 NULL */
const uint8_t *cvbs_console_fb(int *w, int *h)
{
    if (!g_con.inited)
        return NULL;
    if (w) *w = g_con.w;
    if (h) *h = g_con.h;
    return cvbs_core_fb();
}
int cvbs_console_cursor_x(void) { return g_con.cx; }
int cvbs_console_cursor_y(void) { return g_con.cy; }

/*==========================
 *  /dev/cvbscon 字符设备 + UART 输入泵（仅 NuttX 目标编入）
 *==========================*/

#ifdef __NuttX__

#include <nuttx/fs/fs.h>
#include <nuttx/semaphore.h>
#include <nuttx/kthread.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

/* 环形缓冲本体已在通用层（cvbs_console_input_push/pop）；
 * 设备侧 push 附带唤醒阻塞读端 */
static sem_t g_in_sem;
static bool g_inited_dev = false;

static void in_ring_push(unsigned char c)
{
    cvbs_console_input_push((char)c);
    nxsem_post(&g_in_sem);
}

/*---- file_operations ----*/

static int cvbscon_open(FAR struct file *filep)
{
    (void)filep;
    return OK;
}

static int cvbscon_close(FAR struct file *filep)
{
    (void)filep;
    return OK;
}

static ssize_t cvbscon_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
    size_t got = 0;

    (void)filep;

    if (buffer == NULL)
        return -EINVAL;

    while (got < buflen) {
        unsigned char c;

        if (got == 0) {
            int ret = nxsem_wait(&g_in_sem);
            if (ret < 0)
                return ret;
        }

        if (!cvbs_console_input_pop(&c))
            break;                           /* 信号量对应数据已取走 */
        buffer[got++] = (char)c;
    }

    return (ssize_t)got;
}

static ssize_t cvbscon_write(FAR struct file *filep, FAR const char *buffer,
                             size_t buflen)
{
    (void)filep;
    return cvbs_console_write(buffer, buflen);
}

static int cvbscon_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
    (void)filep;

    switch (cmd) {
    case TIOCGWINSZ: {
        struct winsize *ws = (struct winsize *)(uintptr_t)arg;

        if (ws == NULL)
            return -EINVAL;
        ws->ws_row = (unsigned short)g_con.rows;
        ws->ws_col = (unsigned short)g_con.cols;
        ws->ws_xpixel = (unsigned short)g_con.w;
        ws->ws_ypixel = (unsigned short)g_con.h;
        return OK;
    }
    default:
        return -ENOTTY;
    }
}

#define CVBS_CON_NPOLLWAITERS   2

static FAR struct pollfd *g_poll_fds[CVBS_CON_NPOLLWAITERS];

static int cvbscon_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup)
{
    (void)filep;

    if (setup) {
        for (int i = 0; i < CVBS_CON_NPOLLWAITERS; i++) {
            if (g_poll_fds[i] == NULL) {
                g_poll_fds[i] = fds;
                fds->priv = &g_poll_fds[i];
                poll_notify(&fds, 1,
                            (g_in_tail != g_in_head) ? POLLIN : 0);
                return OK;
            }
        }
        return -EBUSY;
    }

    /* 拆除 */
    {
        FAR struct pollfd **slot = (FAR struct pollfd **)fds->priv;

        if (slot != NULL) {
            *slot = NULL;
            fds->priv = NULL;
        }
    }
    return OK;
}

static const struct file_operations g_cvbscon_fops =
{
    .open   = cvbscon_open,
    .close  = cvbscon_close,
    .read   = cvbscon_read,
    .write  = cvbscon_write,
    .ioctl  = cvbscon_ioctl,
    .poll   = cvbscon_poll,
};

/*---- UART 输入泵：读控制台 UART 喂 /dev/cvbscon ----*/

static int cvbs_input_task(int argc, char *argv[])
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/console";
    char buf[32];
    int fd;

    (void)argc;
    (void)argv;

    fd = open(dev, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "[cvbscon] input open %s failed: %d\n", dev, fd);
        return fd;
    }

    syslog(LOG_INFO, "[cvbscon] keyboard pump: %s -> /dev/cvbscon\n", dev);

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                unsigned char ch = (unsigned char)buf[i];

                /* CCDOS 式 IME：激活时键先喂输入法（Ctrl+Space=0x00
                 * 中英切换 / Ctrl+Q=0x11 关闭——IME 内处理），IME
                 * 消费的键不再进入终端输入流 */
                if (cvbs_ime_active() && cvbs_ime_feed(ch))
                    continue;

                in_ring_push(ch);
            }
        } else if (n < 0) {
            usleep(50000);
        }
    }

    return 0;
}

/*
 * WHAT : 外部 HID 源（USB/BLE 键盘桥）喂 ASCII 键流入环
 * WHY  : 输入优先级原则（REQUIREMENTS 2.2.3）：HID 与 UART 泵同路径，
 *        保证 IME 门控/唤醒语义完全一致（声明见 cvbs_console.h）
 * WHEN : 2026-10-05 新增
 * 返回 : 实际入环字节数 / -EINVAL
 */
int cvbs_console_feed_keys(const char *buf, size_t len)
{
    size_t fed = 0;

    if (buf == NULL)
        return -EINVAL;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)buf[i];

        /* 与 UART 泵同一道 IME 门：激活时键先喂输入法 */
        if (cvbs_ime_active() && cvbs_ime_feed(ch))
            continue;

        in_ring_push(ch);
        fed++;
    }

    return (int)fed;
}

/*
 * WHAT : 注册 /dev/cvbscon 并启动键盘输入泵
 * WHEN : retro_boot 在 cvbs_console_init 成功后调用
 * 返回 : OK / 负错误码
 */
int cvbs_console_device_start(const char *input_dev)
{
    int ret;

    if (!g_con.inited)
        return -ENODEV;

    if (!g_inited_dev) {
        nxsem_init(&g_in_sem, 0, 0);
        ret = register_driver("/dev/cvbscon", &g_cvbscon_fops, 0666, NULL);
        if (ret < 0)
            return ret;
        g_inited_dev = true;
    }

    {
        int pid = kthread_create("avkbin", 100,
                                 CONFIG_RETRO_AV_INPUT_STACK,
                                 cvbs_input_task, NULL);
        if (pid < 0)
            syslog(LOG_WARNING, "[cvbscon] input pump failed: %d\n", pid);
    }

    return OK;
}

#endif /* __NuttX__ */
