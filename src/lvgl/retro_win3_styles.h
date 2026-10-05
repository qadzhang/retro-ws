/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: retro_win3_styles.h
 * 描述: Windows 3.2 风格共享颜色与样式常量
 *       Shared Windows 3.2 style color and layout constants
 *
 * 此头文件供 desktop.c 和 retro_ui.c 共同使用，避免 .c 文件之间的直接包含。
 * This header is shared between desktop.c and retro_ui.c
 * to avoid direct .c file inclusion.
 */

/*
 * retro_win3_styles.h - Win3.2 样式常量
 *
 * WHAT : Win3.2 样式常量
 * WHY  : 桌面与各应用共用的配色/尺寸规范
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/retro_win3_styles.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 16 色 + 窗口/边框/菜单尺寸宏
 */

#ifndef __RETRO_WIN3_STYLES_H
#define __RETRO_WIN3_STYLES_H

#include "lvgl/lvgl.h"

/*======================================
 * Windows 3.2 经典调色板 (16色)
 * Classic Windows 3.2 palette (16 colors)
 *======================================*/
#define WIN3_BLACK       lv_color_hex(0x000000)
#define WIN3_BLUE        lv_color_hex(0x000080)
#define WIN3_GREEN       lv_color_hex(0x008000)
#define WIN3_CYAN        lv_color_hex(0x008080)
#define WIN3_RED         lv_color_hex(0x800000)
#define WIN3_MAGENTA     lv_color_hex(0x800080)
#define WIN3_BROWN       lv_color_hex(0x808000)
#define WIN3_LTGRAY      lv_color_hex(0xC0C0C0)
#define WIN3_DKGRAY      lv_color_hex(0x808080)
#define WIN3_WHITE       lv_color_hex(0xFFFFFF)
#define WIN3_YELLOW      lv_color_hex(0xFFFF00)
#define WIN3_LTGREEN     lv_color_hex(0x00FF00)
#define WIN3_LTBLUE      lv_color_hex(0x0000FF)
#define WIN3_LTCYAN      lv_color_hex(0x00FFFF)
#define WIN3_LTRED       lv_color_hex(0xFF0000)
#define WIN3_LTMAGENTA   lv_color_hex(0xFF00FF)

/* Windows 3.2 专用颜色 / Win3.2 specific colors */
#define WIN3_TITLE_FG    WIN3_WHITE
#define WIN3_TITLE_BG    WIN3_BLUE
#define WIN3_WINDOW_BG   WIN3_LTGRAY
#define WIN3_BTN_BG      WIN3_LTGRAY
#define WIN3_BORDER_HI   WIN3_WHITE
#define WIN3_BORDER_MID  WIN3_LTGRAY
#define WIN3_BORDER_LO   WIN3_DKGRAY
#define WIN3_DESKTOP_BG  WIN3_CYAN
#define WIN3_BTN_OK      WIN3_LTGRAY

/* 通用布局常量 / Common layout constants
 * 网格按 4 字中文名(64px)+5 字名(80px)设计:
 * 列距 96 保证 88px 标签框放下 5 字标签不换行,
 * 行距 86 为"组标题(16)+分隔线+图标(32)+标签(20)"留足空档
 * Grid sized for 4~5 CJK char labels: col 96 fits an 88px label
 * without wrap, row 86 reserves header/separator/icon/label band */
#define TITLE_H          18
#define BORDER_W         2
#define MENU_H           20
#define STATUS_H         20
#define TASKBAR_H        28
#define ICON_SIZE        32
#define ICON_TEXT_H      20
#define GRID_X           96
#define GRID_Y           86
#define GRID_X0          40   /* 图标首列 X(标签框比图标宽,需右移防出界) */
#define GRID_TOP         32   /* 首行图标 Y,上方留给组标题+分隔线 */
#define MIN_WIN_W        200
#define MIN_WIN_H        120
#define START_BTN_W      50

#endif /* __RETRO_WIN3_STYLES_H */
