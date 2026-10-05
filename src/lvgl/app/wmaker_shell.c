/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * wmaker_shell.c - WindowMaker/NeXT 风格桌面外壳
 *                    WindowMaker/NeXT style desktop shell
 *
 * WHAT : 在现有 desktop.c 之上提供第二种桌面外壳（可选、可运行时切换）：
 *        右侧竖排 Dock、桌面应用图标、NeXT 式根菜单（点击桌面空白处弹出）。
 * WHY  : 复古情怀——NeXTSTEP/WindowMaker 观感与 Win3.2 并存可切换。
 * WHO  : 由 desktop.c 的 retro_desktop_set_shell() 创建/销毁；
 *        NSH `shell wmaker|win3` 命令经 desktop_api.h 切换。
 * WHERE: src/lvgl/app/wmaker_shell.c
 * WHEN : 2026-10-04 新增（REQUIREMENTS.md 2.1.1）。
 * HOW  : 纯 LVGL 控件组合（无窗口管理器依赖），应用启动统一走
 *        retro_desktop_app_launch() 派发到 desktop.c 的窗口工厂。
 *
 * 观感规范（NeSTEP）：
 *   - 灰色 #A8A8A8 瓷砖底、纯黑 1px 外框
 *   - 凸起：上左白高光、下右深灰阴影
 *   - 凹陷：上左深灰、下右白（Dock 槽位）
 *   - 菜单：黑底白字标题条 + 灰底黑字菜单项
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include "lvgl/lvgl.h"
#include "retro_font.h"
#include "i18n.h"
#include "retro_ui.h"
#include "desktop_api.h"

/*==========================
 *  NeXT 调色板 / NeXT palette
 *==========================*/

#define WM_COLOR_GRAY    lv_color_hex(0xA8A8A8)  /* 主灰 */
#define WM_COLOR_TILE    lv_color_hex(0x999999)  /* Dock 槽 */
#define WM_COLOR_DARK    lv_color_hex(0x555555)  /* 阴影 */
#define WM_COLOR_WHITE   lv_color_hex(0xFFFFFF)  /* 高光 */
#define WM_COLOR_BLACK   lv_color_hex(0x000000)  /* 外框/文字 */

#define WM_DOCK_W        72     /* Dock 总宽 */
#define WM_DOCK_TILE     64     /* Dock 槽位尺寸 */
#define WM_ICON_SIZE     48     /* 桌面图标尺寸 */
#define WM_ICON_LABEL_W  (WM_ICON_SIZE + 40)  /* 图标标签宽(容纳 4 字中文名) */
#define WM_LABEL_H       20     /* 图标标签高度 */
#define WM_MENU_ITEM_H   22     /* 菜单项高度 */

/*==========================
 *  应用表 / App table
 *==========================*/

struct wm_app {
    const char *id;       /* desktop.c 派发标识 */
    const char *i18n_key; /* i18n 名称键 */
    const char *symbol;   /* 图标 ASCII 符号（复古单色风） */
    bool on_dock;         /* 是否上 Dock */
};

static const struct wm_app g_wm_apps[] = {
    { "notepad",      "APP_NOTEPAD",      "N", true  },
    { "terminal",     "APP_TERMINAL",     "T", true  },
    { "browser",      "APP_BROWSER",      "W", true  },
    { "filemanager",  "APP_FILE_MANAGER", "F", true  },
    { "minesweeper",  "APP_MINESWEEPER",  "*", true  },
    { "player",       "APP_MEDIA_PLAYER", "M", false },
    { "recorder",     "APP_RECORDER",     "R", false },
    { "sqlite",       "APP_SQLITE",       "S", false },
    { "controlpanel", "APP_CONTROL_PANEL","C", false },
    { "settings",     "APP_SETTINGS",     "$", false },
};

#define WM_APP_COUNT  (sizeof(g_wm_apps) / sizeof(g_wm_apps[0]))

/*==========================
 *  状态 / State
 *==========================*/

static lv_obj_t  *g_wm_root = NULL;   /* 工作区背景（接收根菜单点击） */
static lv_obj_t  *g_wm_dock  = NULL;  /* Dock 容器 */
static lv_obj_t  *g_wm_menu  = NULL;  /* 当前打开的根菜单 */
static lv_timer_t *g_wm_timer = NULL; /* Dock 时钟定时器 */
static bool        g_wm_active = false;

/*==========================
 *  样式辅助 / Style helpers
 *==========================*/

/* 凸起/凹陷瓷砖 / raised or sunken tile */
static void wm_bevel(lv_obj_t *obj, bool raised)
{
    lv_obj_set_style_bg_color(obj, WM_COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);

    if (raised) {
        lv_obj_set_style_border_color(obj, WM_COLOR_WHITE, LV_PART_MAIN);
        lv_obj_set_style_border_side(obj,
            LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
        /* 下右阴影用第二层：简化为深色右下描边 */
        lv_obj_set_style_outline_color(obj, WM_COLOR_DARK, LV_PART_MAIN);
        lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(obj, 0, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(obj, WM_COLOR_DARK, LV_PART_MAIN);
        lv_obj_set_style_border_side(obj,
            LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
        lv_obj_set_style_outline_color(obj, WM_COLOR_WHITE, LV_PART_MAIN);
        lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(obj, 0, LV_PART_MAIN);
    }
}

/*==========================
 *  Dock / Dock 栏
 *==========================*/

static void wm_dock_tile_cb(lv_event_t *e)
{
    const struct wm_app *app =
        (const struct wm_app *)lv_event_get_user_data(e);

    if (app)
        retro_desktop_app_launch(app->id);
}

static void wm_clock_timer_cb(lv_timer_t *t)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_timer_get_user_data(t);
    if (!lbl)
        return;

    /* 复用 NuttX 时间：HH:MM */
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(lbl, buf);
}

/* 顶部 Dock 槽：时钟 / top dock tile: clock */
static void wm_dock_add_clock(lv_obj_t *dock)
{
    lv_obj_t *tile = lv_obj_create(dock);
    lv_obj_set_size(tile, WM_DOCK_TILE, WM_DOCK_TILE);
    lv_obj_set_style_bg_color(tile, WM_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_obj_set_style_text_color(lbl, WM_COLOR_WHITE, 0);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl, "--:--");

    g_wm_timer = lv_timer_create(wm_clock_timer_cb, 60000, lbl);
    lv_timer_ready(g_wm_timer);   /* 立即出时间，不等 60s 首拍 */
}

static void wm_dock_create(lv_obj_t *parent)
{
    g_wm_dock = lv_obj_create(parent);
    lv_obj_set_size(g_wm_dock, WM_DOCK_W, 480);
    lv_obj_align(g_wm_dock, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_wm_dock, WM_COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_wm_dock, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_wm_dock, WM_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_radius(g_wm_dock, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_wm_dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(g_wm_dock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_wm_dock, 2, 0);
    lv_obj_set_style_pad_row(g_wm_dock, 6, 0);

    wm_dock_add_clock(g_wm_dock);

    /* NeXTSTEP 传统：Dock 应用磁贴为彩色图标（低饱和 NeXT 系色板，
     * 工作区保持灰阶）——2026-10-05 补彩色点缀（README 彩色截图验收） */
    static const uint32_t tile_colors[] = {
        0x556B2F, 0x8B4513, 0x4169AF, 0x8B2500,
        0x2F4F6F, 0x6B4C9A, 0x1F6E43, 0x7A3B69,
    };
    int dock_idx = 0;

    for (size_t i = 0; i < WM_APP_COUNT; i++) {
        if (!g_wm_apps[i].on_dock)
            continue;

        lv_obj_t *tile = lv_obj_create(g_wm_dock);
        lv_obj_set_size(tile, WM_DOCK_TILE, WM_DOCK_TILE);
        wm_bevel(tile, false);  /* Dock 槽位凹陷 */
        lv_obj_set_style_bg_color(tile, lv_color_hex(
            tile_colors[dock_idx % 8]), 0);
        dock_idx++;

        lv_obj_t *sym = lv_label_create(tile);
        lv_label_set_text(sym, g_wm_apps[i].symbol);
        lv_obj_set_style_text_font(sym, RETRO_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(sym, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(sym, LV_ALIGN_CENTER, 0, 0);

        lv_obj_add_event_cb(tile, wm_dock_tile_cb,
                            LV_EVENT_CLICKED, (void *)&g_wm_apps[i]);
    }
}

/*==========================
 *  桌面应用图标 / Desktop app icons
 *==========================*/

static void wm_icon_dblck(lv_event_t *e)
{
    const struct wm_app *app =
        (const struct wm_app *)lv_event_get_user_data(e);

    if (app)
        retro_desktop_app_launch(app->id);
}

static void wm_icons_create(lv_obj_t *parent)
{
    int col_x = 12;
    int y = 12;

    for (size_t i = 0; i < WM_APP_COUNT; i++) {
        const struct wm_app *app = &g_wm_apps[i];
        const char *name = app->i18n_key ? i18n_get(app->i18n_key) : app->id;

        /* 放不下"图标 48+标签 20"就先换列,保证标签完整在屏内
         * Wrap column first when icon+label won't fit on screen */
        if (y + WM_ICON_SIZE + WM_LABEL_H + 4 > 480) {
            y = 12;
            col_x += WM_ICON_SIZE + 28;
        }

        lv_obj_t *tile = lv_obj_create(parent);
        lv_obj_set_size(tile, WM_ICON_SIZE, WM_ICON_SIZE);
        lv_obj_set_pos(tile, col_x, y);
        wm_bevel(tile, true);

        lv_obj_t *sym = lv_label_create(tile);
        lv_label_set_text(sym, app->symbol);
        lv_obj_set_style_text_font(sym, RETRO_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(sym, WM_COLOR_BLACK, 0);
        lv_obj_align(sym, LV_ALIGN_CENTER, 0, 0);

        /* 标签 88px 宽居中于图标,长名("SQLite 浏览器")省略号收尾
         * 88px label centered under icon, DOT ellipsis for long names */
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, name);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(lbl, WM_COLOR_BLACK, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(lbl, WM_COLOR_GRAY, 0);
        int lx = col_x + WM_ICON_SIZE / 2 - WM_ICON_LABEL_W / 2;
        if (lx < 2)
            lx = 2;
        lv_obj_set_pos(lbl, lx, y + WM_ICON_SIZE + 2);
        lv_obj_set_size(lbl, WM_ICON_LABEL_W, WM_LABEL_H);
        lv_obj_set_style_pad_all(lbl, 0, 0);

        lv_obj_add_event_cb(tile, wm_icon_dblck,
                            LV_EVENT_DOUBLE_CLICKED, (void *)app);

        y += WM_ICON_SIZE + 28;
    }
}

/*==========================
 *  NeXT 式根菜单 / NeXT style root menu
 *==========================*/

static void wm_menu_close(void)
{
    if (g_wm_menu) {
        lv_obj_delete(g_wm_menu);
        g_wm_menu = NULL;
    }
}

static void wm_menu_item_cb(lv_event_t *e)
{
    const struct wm_app *app =
        (const struct wm_app *)lv_event_get_user_data(e);

    wm_menu_close();

    if (app)
        retro_desktop_app_launch(app->id);
}

static void wm_menu_shell_cb(lv_event_t *e)
{
    (void)e;
    wm_menu_close();
    retro_desktop_set_shell(RETRO_SHELL_WIN3);
}

static void wm_menu_about_cb(lv_event_t *e)
{
    (void)e;
    wm_menu_close();
    retro_ui_msgbox(i18n_get("WM_ABOUT_TITLE"), i18n_get("WM_ABOUT_TEXT"));
}

/* 根菜单标题条 / menu title bar (black on white, NeXT style) */
static lv_obj_t *wm_menu_add_title(lv_obj_t *menu, const char *text)
{
    lv_obj_t *title = lv_obj_create(menu);
    lv_obj_set_size(title, LV_PCT(100), WM_MENU_ITEM_H);
    lv_obj_set_style_bg_color(title, WM_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(title, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(title, 0, LV_PART_MAIN);
    /* pad 归零,标题文字按 6px 缩进落位 / zero pad for 6px indent */
    lv_obj_set_style_pad_all(title, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(title);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, WM_COLOR_WHITE, 0);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
    return title;
}

/* 普通菜单项 / normal menu item */
static void wm_menu_add_item(lv_obj_t *menu, const char *text,
                             lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *item = lv_obj_create(menu);
    lv_obj_set_size(item, LV_PCT(100), WM_MENU_ITEM_H);
    lv_obj_set_style_bg_color(item, WM_COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(item, WM_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_radius(item, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, WM_COLOR_BLACK, 0);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);

    if (cb)
        lv_obj_add_event_cb(item, cb, LV_EVENT_CLICKED, user_data);
}

/* 分节标题（不可点击）/ section header (not clickable) */
static void wm_menu_add_section(lv_obj_t *menu, const char *text)
{
    lv_obj_t *item = lv_obj_create(menu);
    lv_obj_set_size(item, LV_PCT(100), WM_MENU_ITEM_H - 4);
    lv_obj_set_style_bg_color(item, WM_COLOR_TILE, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(item, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, WM_COLOR_DARK, 0);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);
}

static void wm_menu_open(lv_coord_t x, lv_coord_t y)
{
    wm_menu_close();

    g_wm_menu = lv_obj_create(g_wm_root);
    lv_obj_set_size(g_wm_menu, 168,
                    (WM_APP_COUNT + 4) * WM_MENU_ITEM_H + 8);
    lv_obj_set_pos(g_wm_menu, x, y);
    lv_obj_set_style_bg_color(g_wm_menu, WM_COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_wm_menu, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_wm_menu, WM_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_radius(g_wm_menu, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_wm_menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_wm_menu, 2, 0);
    lv_obj_set_style_pad_row(g_wm_menu, 1, 0);

    wm_menu_add_title(g_wm_menu, i18n_get("WM_MENU_TITLE"));

    wm_menu_add_section(g_wm_menu, i18n_get("WM_MENU_APPS"));
    for (size_t i = 0; i < WM_APP_COUNT; i++)
        wm_menu_add_item(g_wm_menu, i18n_get(g_wm_apps[i].i18n_key),
                         wm_menu_item_cb, (void *)&g_wm_apps[i]);

    wm_menu_add_section(g_wm_menu, i18n_get("WM_MENU_SHELL"));
    wm_menu_add_item(g_wm_menu, i18n_get("WM_MENU_SWITCH_WIN3"),
                     wm_menu_shell_cb, NULL);

    wm_menu_add_section(g_wm_menu, i18n_get("WM_MENU_INFO"));
    wm_menu_add_item(g_wm_menu, i18n_get("WM_MENU_ABOUT"),
                     wm_menu_about_cb, NULL);
}

/* 桌面空白处点击 → 根菜单 / click on workspace opens root menu */
static void wm_root_click_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = { 0, 0 };

    if (indev)
        lv_indev_get_point(indev, &p);

    /* 菜单避开 Dock 区域 */
    if (p.x > 640 - WM_DOCK_W - 170)
        p.x = 640 - WM_DOCK_W - 170;

    wm_menu_open(p.x, p.y);
}

/*==========================
 *  对外接口 / Public API（供 desktop.c 调用）
 *==========================*/

/**
 * wmaker_shell_create - 创建 WindowMaker 外壳
 */
int wmaker_shell_create(lv_obj_t *parent)
{
    if (g_wm_active)
        return OK;

    if (!parent)
        return -EINVAL;

    syslog(LOG_INFO, "[wmaker] creating WindowMaker shell\n");

    /* 工作区背景（避开右侧 Dock） */
    /* pad 归零:默认主题 16px 内边距会把图标/标签整体内缩出屏
     * Zero pad: theme padding would inset the icon grid */
    g_wm_root = lv_obj_create(parent);
    lv_obj_set_size(g_wm_root, 640 - WM_DOCK_W, 480);
    lv_obj_set_pos(g_wm_root, 0, 0);
    lv_obj_set_style_bg_color(g_wm_root, WM_COLOR_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_wm_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_wm_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_wm_root, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_wm_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_wm_root, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(g_wm_root, wm_root_click_cb, LV_EVENT_CLICKED, NULL);

    wm_icons_create(g_wm_root);
    wm_dock_create(parent);

    g_wm_active = true;
    syslog(LOG_INFO, "[wmaker] shell ready\n");
    return OK;
}

/**
 * wmaker_shell_destroy - 销毁 WindowMaker 外壳
 */
void wmaker_shell_destroy(void)
{
    if (!g_wm_active)
        return;

    if (g_wm_timer) {
        lv_timer_delete(g_wm_timer);
        g_wm_timer = NULL;
    }

    wm_menu_close();

    if (g_wm_root) {
        lv_obj_delete(g_wm_root);
        g_wm_root = NULL;
    }

    if (g_wm_dock) {
        lv_obj_delete(g_wm_dock);
        g_wm_dock = NULL;
    }

    g_wm_active = false;
    syslog(LOG_INFO, "[wmaker] shell destroyed\n");
}

/**
 * wmaker_shell_active - 是否处于激活状态
 */
bool wmaker_shell_active(void)
{
    return g_wm_active;
}
