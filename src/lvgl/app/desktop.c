/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * desktop.c - Win3.2 桌面外壳 + 外壳切换/应用派发
 *
 * WHAT : Win3.2 桌面外壳 + 外壳切换/应用派发
 * WHY  : 复古图形工作站的核心桌面（任务栏/开始菜单/程序管理器/窗口管理）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/desktop.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : LVGL 控件组合，set_shell() 切换外壳，app_launch() 派发到各 create_* 窗口工厂
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"
#include "i18n.h"
#include "retro_win3_styles.h"
#include "desktop_api.h"

/* WindowMaker 外壳（wmaker_shell.c）/ WindowMaker shell */
extern int  wmaker_shell_create(lv_obj_t *parent);
extern void wmaker_shell_destroy(void);
extern bool wmaker_shell_active(void);

/* 当前外壳模式 / current shell mode */
static int g_shell_mode = RETRO_SHELL_WIN3;
#define CAPTION_X        18        /* 标题文字 X 偏移(留位置给图标按钮) */

/*======================================
 *  全局变量
 *======================================*/
static lv_display_t   *g_disp     = NULL;
static lv_indev_t     *g_keypad   = NULL;
static lv_indev_t     *g_mouse    = NULL;

static lv_obj_t       *g_desktop   = NULL;  /* 桌面背景 */
static lv_obj_t       *g_taskbar   = NULL;  /* 任务栏 */
static lv_obj_t       *g_start_btn = NULL;  /* 开始按钮 */
static lv_obj_t       *g_start_lbl = NULL;  /* 开始标签 */
static lv_obj_t       *g_clock_lbl = NULL; /* 时钟标签 */
static lv_obj_t       *g_start_menu = NULL; /* 开始菜单 */
static lv_timer_t     *g_timer     = NULL;
static lv_timer_t     *g_clock_timer = NULL;

/* 窗口管理 */
#define MAX_WINDOWS 16
typedef struct {
    lv_obj_t *win;        /* 窗口对象 */
    lv_obj_t *title_bar;  /* 标题栏 */
    lv_obj_t *menu_bar;   /* 菜单栏 */
    lv_obj_t *status_bar; /* 状态栏 */
    lv_obj_t *client;     /* 客户区 */
    lv_obj_t *min_btn;    /* 最小化按钮 */
    lv_obj_t *max_btn;    /* 最大化按钮 */
    lv_obj_t *close_btn;  /* 关闭按钮 */
    char     title[64];   /* 窗口标题 */
    int      minimized;   /* 是否最小化 */
    int      maximized;   /* 是否最大化 */
    int      being_dragged; /* 是否正在拖动 */
    int      orig_x, orig_y, orig_w, orig_h; /* 最大化前位置 */
} win_info_t;

static win_info_t g_windows[MAX_WINDOWS];
static int        g_win_count  = 0;
static win_info_t *g_active_win = NULL;  /* 当前激活窗口 */
static win_info_t *g_progman   = NULL;  /* 程序管理器主窗口 / Program Manager window */

/* 图标定义 */
typedef struct {
    const char *name;     /* 显示名称 */
    const char *icon;     /* 图标符号(ASCII art 用字符表示) */
    void (*action)(void); /* 点击动作 */
} icon_entry_t;

/* 内部函数声明 */
static void   taskbar_clock_update(void);
static void   clock_timer_cb(lv_timer_t *t);
static void   start_menu_hide(void);
static win_info_t *find_window(lv_obj_t *win);
static void   win_bring_to_front(win_info_t *wi);
static void   win_set_active(win_info_t *wi);
static void   titlebar_event_cb(lv_event_t *e);
static void   menu_item_cb(lv_event_t *e);
static void   create_progman(void);
static void   create_minesweeper(void);
static void   create_notepad(void);
static void   create_browser(void);
static void   create_terminal(void);
static void   create_control_panel(void);
static void   create_settings(void);
static void   create_file_manager(void);
static void   create_player(void);
static void   create_recorder(void);
static void   create_sqlite(void);
static void   desktop_icon_dblck(lv_event_t *e);
static void   win_close_cb(lv_event_t *e);
static void   win_min_cb(lv_event_t *e);
static void   win_max_cb(lv_event_t *e);
static void   lang_zh_cb(lv_event_t *e);
static void   lang_en_cb(lv_event_t *e);

/* External app launchers */
extern lv_obj_t *editor_create(void);       /* Notepad */
extern lv_obj_t *browser_create(void);      /* Browser */
extern lv_obj_t *terminal_create(void);     /* Terminal */
extern lv_obj_t *player_create(void);       /* Media Player */
extern lv_obj_t *recorder_create(void);      /* Recorder */
extern lv_obj_t *sqlite_gui_create(void);     /* SQLite Browser */

/* (3D drawing helpers - use LVGL native borders instead) */

/*======================================
 *  任务栏
 *======================================*/

static void draw_taskbar_3d(lv_obj_t *obj)
{
    /* 使用 Canvas 绘制 3D 效果的任务栏 */
    /* 顶部: 白/灰 3D 线 */
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
}

static lv_obj_t *create_taskbar_button(const char *text, int idx)
{
    lv_obj_t *btn = lv_button_create(g_taskbar);
    lv_obj_set_size(btn, 80, 20);
    lv_obj_set_pos(btn, START_BTN_W + 8 + idx * 84, 4);
    lv_obj_set_style_bg_color(btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return btn;
}

/*======================================
 *  开始菜单
 *======================================*/

static lv_obj_t *g_start_menu_items[24];
static int       g_start_menu_count = 0;

/*
 * win_move_to_front - 把对象移到父容器最前（最后子节点）
 * WHAT: 移到最前 / move to foreground
 * WHY : lv_obj_move_to_index(obj, 0xFFFF) 在 9.5 中索引越界会直接返回（no-op），
 *       旧代码全部无效；LVGL 9.5 无 lv_obj_move_foreground 别名，这里等价实现
 * HOW : 移动到 parent 的最后一个子索引 / move to last child index of parent
 */
static void win_move_to_front(lv_obj_t *obj)
{
    lv_obj_t *parent = lv_obj_get_parent(obj);

    if (obj == NULL || parent == NULL)
        return;

    lv_obj_move_to_index(obj, (int32_t)lv_obj_get_child_count(parent) - 1);
}

static void hide_start_menu_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
}

static void start_menu_cb(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
        win_move_to_front(g_start_menu);
    } else {
        lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_start_menu_item(const char *text, const char *sub,
                                  void (*cb)(void))
{
    if (g_start_menu_count >= 24) return;

    lv_obj_t *item = lv_obj_create(g_start_menu);
    lv_obj_set_size(item, 180, 22);
    lv_obj_set_pos(item, 0, 24 + g_start_menu_count * 24);
    lv_obj_set_style_bg_color(item, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(item, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(item, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    /* 子菜单箭头 */
    if (sub) {
        lv_obj_t *arrow = lv_label_create(item);
        lv_label_set_text(arrow, "▶");
        lv_obj_set_style_text_font(arrow, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    /* 分隔线效果 */
    if (text[0] == 0) {
        lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(item, WIN3_BORDER_MID, LV_PART_MAIN);
        lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    }

    if (cb) {
        lv_obj_set_user_data(item, (void(*)(void))cb);
        lv_obj_add_event_cb(item, menu_item_cb, LV_EVENT_CLICKED, NULL);
    }

    g_start_menu_items[g_start_menu_count++] = item;
}

static void build_start_menu(void)
{
    /* 开始菜单容器 */
    g_start_menu = lv_obj_create(g_taskbar);
    lv_obj_set_size(g_start_menu, 200, 320);
    lv_obj_set_pos(g_start_menu, 0, -320 - 24);
    lv_obj_set_style_bg_color(g_start_menu, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_start_menu, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_start_menu, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(g_start_menu, 0, LV_PART_MAIN);
    /* pad 归零+禁滚动:菜单项按绝对坐标排布,超出部分裁剪即可
     * Zero pad + no scroll: items use absolute coords, overflow clips */
    lv_obj_set_style_pad_all(g_start_menu, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_start_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_start_menu, LV_SCROLLBAR_MODE_OFF);

    /* 左上角 Windows 3.2 Logo 区 */
    lv_obj_t *logo_bg = lv_obj_create(g_start_menu);
    lv_obj_set_size(logo_bg, 30, 320);
    lv_obj_set_pos(logo_bg, 0, 0);
    lv_obj_set_style_bg_color(logo_bg, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(logo_bg, 0, LV_PART_MAIN);

    lv_obj_t *win_logo = lv_label_create(logo_bg);
    lv_label_set_text(win_logo, "WIN\n\n\n\n\n\n\nd");
    lv_obj_set_style_text_color(win_logo, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(win_logo, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(win_logo, LV_ALIGN_TOP_LEFT, 2, 2);

    /* 菜单分隔线 */
    lv_obj_t *sep = lv_obj_create(g_start_menu);
    lv_obj_set_size(sep, 170, 2);
    lv_obj_set_pos(sep, 30, 22);
    lv_obj_set_style_bg_color(sep, WIN3_BORDER_HI, LV_PART_MAIN);

    /* 菜单项 - 使用 i18n / Menu items - use i18n */
    add_start_menu_item(i18n_get("START_MENU_PROGRAMS"), "▶", NULL);
    add_start_menu_item(i18n_get("START_MENU_ACCESSORIES"), "▶", NULL);
    add_start_menu_item(i18n_get("START_MENU_GAMES"), "▶", NULL);
    add_start_menu_item("", "", NULL); /* 分隔线 */
    add_start_menu_item(i18n_get("APP_NOTEPAD"), NULL, create_notepad);
    add_start_menu_item(i18n_get("APP_BROWSER"), NULL, create_browser);
    add_start_menu_item(i18n_get("APP_TERMINAL"), NULL, create_terminal);
    add_start_menu_item(i18n_get("APP_SQLITE"), NULL, create_sqlite);
    add_start_menu_item(i18n_get("APP_MINESWEEPER"), NULL, create_minesweeper);
    add_start_menu_item(i18n_get("APP_MEDIA_PLAYER"), NULL, create_player);
    add_start_menu_item(i18n_get("APP_RECORDER"), NULL, create_recorder);
    add_start_menu_item("", "", NULL);
    add_start_menu_item(i18n_get("APP_FILE_MANAGER"), NULL, create_file_manager);
    add_start_menu_item(i18n_get("APP_CONTROL_PANEL"), NULL, create_control_panel);
    add_start_menu_item("", "", NULL);
    add_start_menu_item(i18n_get("START_MENU_RUN"), NULL, NULL);
    add_start_menu_item(i18n_get("START_MENU_SHUTDOWN"), NULL, NULL);

    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);

    /* 点击其他区域关闭开始菜单 */
    lv_obj_add_event_cb(g_taskbar, hide_start_menu_cb, LV_EVENT_CLICKED, NULL);
}

/*======================================
 *  窗口系统
 *======================================*/

/**
 * 创建 Windows 3.2 风格窗口
 * 返回窗口信息结构指针
 */
static win_info_t *win_create(const char *title, int x, int y, int w, int h)
{
    if (g_win_count >= MAX_WINDOWS) return NULL;

    win_info_t *wi = &g_windows[g_win_count++];
    memset(wi, 0, sizeof(*wi));
    strncpy(wi->title, title, sizeof(wi->title) - 1);

    /* 窗口本体 */
    /* 默认主题自带 16px 内边距,会把 set_pos 子对象整体内缩并撑出
     * 滚动条;固定布局容器一律 pad 归零并关闭滚动
     * Default theme pads containers by 16px which insets absolutely
     * positioned children; zero pad and disable scrolling everywhere */
    wi->win = lv_obj_create(g_desktop);
    lv_obj_set_pos(wi->win, x, y);
    lv_obj_set_size(wi->win, w, h);
    lv_obj_set_style_bg_color(wi->win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->win, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->win, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wi->win, 0, LV_PART_MAIN);
    lv_obj_remove_flag(wi->win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(wi->win, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(wi->win, LV_OBJ_FLAG_CLICKABLE);

    /* === 标题栏 (3D 凸起效果) === */
    wi->title_bar = lv_obj_create(wi->win);
    lv_obj_set_pos(wi->title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(wi->title_bar, w - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(wi->title_bar, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wi->title_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wi->title_bar, 0, LV_PART_MAIN);
    lv_obj_set_user_data(wi->title_bar, wi);
    lv_obj_add_event_cb(wi->title_bar, titlebar_event_cb, LV_EVENT_ALL, wi);

    /* 标题文字 */
    lv_obj_t *title_lbl = lv_label_create(wi->title_bar);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, CAPTION_X, 0);

    /* === 窗口控制按钮 (右边) === */
    /* 关闭按钮 */
    wi->close_btn = lv_button_create(wi->title_bar);
    lv_obj_set_size(wi->close_btn, 16, 14);
    lv_obj_align(wi->close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(wi->close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->close_btn, 0, LV_PART_MAIN);
    lv_obj_set_user_data(wi->close_btn, wi);
    lv_obj_add_event_cb(wi->close_btn, win_close_cb, LV_EVENT_CLICKED, wi);

    lv_obj_t *x_lbl = lv_label_create(wi->close_btn);
    lv_label_set_text(x_lbl, "x");
    lv_obj_set_style_text_font(x_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(x_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 最大化按钮 */
    wi->max_btn = lv_button_create(wi->title_bar);
    lv_obj_set_size(wi->max_btn, 16, 14);
    lv_obj_align(wi->max_btn, LV_ALIGN_RIGHT_MID, -21, 0);
    lv_obj_set_style_bg_color(wi->max_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->max_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->max_btn, 0, LV_PART_MAIN);
    lv_obj_set_user_data(wi->max_btn, wi);
    lv_obj_add_event_cb(wi->max_btn, win_max_cb, LV_EVENT_CLICKED, wi);

    lv_obj_t *max_lbl = lv_label_create(wi->max_btn);
    lv_label_set_text(max_lbl, "□");
    lv_obj_set_style_text_font(max_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(max_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 最小化按钮 */
    wi->min_btn = lv_button_create(wi->title_bar);
    lv_obj_set_size(wi->min_btn, 16, 14);
    lv_obj_align(wi->min_btn, LV_ALIGN_RIGHT_MID, -39, 0);
    lv_obj_set_style_bg_color(wi->min_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->min_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->min_btn, 0, LV_PART_MAIN);
    lv_obj_set_user_data(wi->min_btn, wi);
    lv_obj_add_event_cb(wi->min_btn, win_min_cb, LV_EVENT_CLICKED, wi);

    lv_obj_t *min_lbl = lv_label_create(wi->min_btn);
    lv_label_set_text(min_lbl, "_");
    lv_obj_set_style_text_font(min_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(min_lbl, LV_ALIGN_CENTER, 0, 0);

    /* === 菜单栏 === */
    wi->menu_bar = lv_obj_create(wi->win);
    lv_obj_set_pos(wi->menu_bar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(wi->menu_bar, w - BORDER_W * 2, MENU_H);
    lv_obj_set_style_bg_color(wi->menu_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->menu_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wi->menu_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->menu_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wi->menu_bar, 0, LV_PART_MAIN);

    /* 菜单项示例: File Edit Help */
    /* 菜单项 / Menu items - 使用 i18n / Use i18n */
    static const char *menu_items[] = {
        NULL,  /* 占位 / Placeholder - will be set below */
        NULL,
        NULL,
        NULL
    };
    /* 初始化菜单项文本 / Initialize menu item texts */
    menu_items[0] = i18n_get("MENU_FILE");
    menu_items[1] = i18n_get("MENU_EDIT");
    menu_items[2] = i18n_get("MENU_VIEW");
    menu_items[3] = i18n_get("MENU_HELP");
    for (int i = 0; i < 4; i++) {
        lv_obj_t *mi = lv_obj_create(wi->menu_bar);
        lv_obj_set_size(mi, 40, MENU_H - 2);
        lv_obj_set_pos(mi, BORDER_W + 2 + i * 44, 1);
        lv_obj_set_style_bg_color(mi, WIN3_LTGRAY, LV_PART_MAIN);
        lv_obj_set_style_border_width(mi, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(mi, 0, LV_PART_MAIN);
        lv_obj_t *ml = lv_label_create(mi);
        lv_label_set_text(ml, menu_items[i]);
        lv_obj_set_style_text_font(ml, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(ml, LV_ALIGN_CENTER, 0, 0);
    }

    /* === 客户区 (凹陷效果) === */
    /* 高度必须扣除状态栏(旧式只扣一条边框,状态栏溢出窗口底 18px)
     * Client height must reserve STATUS_H (old math spilled the status
     * bar 18px past the window bottom edge) */
    int client_y = BORDER_W + TITLE_H + MENU_H;
    int client_h = h - BORDER_W * 2 - TITLE_H - MENU_H - STATUS_H;
    wi->client = lv_obj_create(wi->win);
    lv_obj_set_pos(wi->client, BORDER_W, client_y);
    lv_obj_set_size(wi->client, w - BORDER_W * 2, client_h);
    lv_obj_set_style_bg_color(wi->client, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->client, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wi->client, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->client, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wi->client, 0, LV_PART_MAIN);
    lv_obj_remove_flag(wi->client, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(wi->client, LV_SCROLLBAR_MODE_OFF);

    /* === 状态栏 (底部凹陷) === */
    int status_y = client_y + client_h;
    wi->status_bar = lv_obj_create(wi->win);
    lv_obj_set_pos(wi->status_bar, BORDER_W, status_y);
    lv_obj_set_size(wi->status_bar, w - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(wi->status_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(wi->status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wi->status_bar, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(wi->status_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wi->status_bar, 0, LV_PART_MAIN);

    /* 状态栏文字 */
    lv_obj_t *status_lbl = lv_label_create(wi->status_bar);
    lv_label_set_text(status_lbl, i18n_get("STATUS_READY"));
    lv_obj_set_style_text_font(status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 设置激活状态 */
    win_set_active(wi);

    return wi;
}

static void win_set_active(win_info_t *wi)
{
    if (g_active_win == wi) return;

    /* 取消之前窗口的激活状态 */
    if (g_active_win && g_active_win->title_bar) {
        lv_obj_set_style_bg_color(g_active_win->title_bar, WIN3_BLUE, LV_PART_MAIN);
    }

    g_active_win = wi;

    if (wi && wi->title_bar) {
        /* 激活窗口标题栏更亮 */
        lv_obj_set_style_bg_color(wi->title_bar, WIN3_BLUE, LV_PART_MAIN);
    }
}

/* 标题栏拖动 */
static lv_point_t g_drag_last;  /* 上次指针绝对坐标 */

static void titlebar_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    win_info_t *wi = (win_info_t *)lv_obj_get_user_data(obj);
    if (!wi) return;

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        /* 点击标题栏: 激活窗口 + 记录起始位置 */
        win_set_active(wi);
        win_bring_to_front(wi);
        wi->being_dragged = 1;
        /* 记录按下时的绝对坐标，用于后续计算增量 */
        lv_indev_t *indev = lv_event_get_indev(e);
        if (indev) {
            lv_indev_get_point(indev, &g_drag_last);
        }
    }
    else if (code == LV_EVENT_PRESSING) {
        if (wi->being_dragged && !wi->maximized) {
            lv_indev_t *indev = lv_event_get_indev(e);
            if (indev) {
                lv_point_t cur;
                lv_indev_get_point(indev, &cur);
                /* 计算指针移动的增量 */
                lv_coord_t dx = cur.x - g_drag_last.x;
                lv_coord_t dy = cur.y - g_drag_last.y;
                g_drag_last = cur;
                int cur_x = (int)lv_obj_get_x(wi->win);
                int cur_y = (int)lv_obj_get_y(wi->win);
                lv_obj_set_pos(wi->win, cur_x + dx, cur_y + dy);
            }
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        wi->being_dragged = 0;
    }
}

static void win_bring_to_front(win_info_t *wi)
{
    win_move_to_front(wi->win); /* 移到最前 */
}

static win_info_t *find_window(lv_obj_t *win)
{
    for (int i = 0; i < g_win_count; i++) {
        if (g_windows[i].win == win) return &g_windows[i];
    }
    return NULL;
}

static void win_close_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    win_info_t *wi = (win_info_t *)lv_obj_get_user_data(btn);
    if (!wi) return;

    /* 记录窗口对象，便于压缩后按对象重查 / Remember obj for re-lookup */
    lv_obj_t *progman_win = (g_progman != NULL) ? g_progman->win : NULL;
    lv_obj_t *active_win = (g_active_win != NULL) ? g_active_win->win : NULL;

    lv_obj_delete(wi->win);

    /* 从数组中移除，将后面的窗口前移填补空洞 */
    int idx = (int)(wi - g_windows);
    for (int i = idx; i < g_win_count - 1; i++) {
        g_windows[i] = g_windows[i + 1];
    }
    g_win_count--;
    memset(&g_windows[g_win_count], 0, sizeof(win_info_t));

    /* 前移后所有 user_data 指向旧地址，逐个重挂到新位置
     * Re-attach user_data of surviving windows to their new slots */
    for (int i = 0; i < g_win_count; i++) {
        win_info_t *w = &g_windows[i];
        if (w->title_bar)
            lv_obj_set_user_data(w->title_bar, w);
        if (w->close_btn)
            lv_obj_set_user_data(w->close_btn, w);
        if (w->max_btn)
            lv_obj_set_user_data(w->max_btn, w);
        if (w->min_btn)
            lv_obj_set_user_data(w->min_btn, w);
    }

    /* 修正全局指针（按窗口对象重查）/ Fix globals by object lookup */
    g_progman = find_window(progman_win);
    g_active_win = find_window(active_win);

    /* 激活最后一个窗口 */
    if (g_win_count > 0) {
        if (g_active_win == NULL)
            win_set_active(&g_windows[g_win_count - 1]);
    } else {
        g_active_win = NULL;
    }
}

static void win_min_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    win_info_t *wi = (win_info_t *)lv_obj_get_user_data(btn);
    if (!wi) return;

    if (wi->minimized) {
        lv_obj_remove_flag(wi->win, LV_OBJ_FLAG_HIDDEN);
        wi->minimized = 0;
    } else {
        lv_obj_add_flag(wi->win, LV_OBJ_FLAG_HIDDEN);
        wi->minimized = 1;
    }
}

static void win_max_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    win_info_t *wi = (win_info_t *)lv_obj_get_user_data(btn);
    if (!wi) return;

    if (wi->maximized) {
        /* 还原 */
        lv_obj_set_pos(wi->win, wi->orig_x, wi->orig_y);
        lv_obj_set_size(wi->win, wi->orig_w, wi->orig_h);
        wi->maximized = 0;
    } else {
        /* 保存当前位置 */
        wi->orig_x = (int)lv_obj_get_x(wi->win);
        wi->orig_y = (int)lv_obj_get_y(wi->win);
        wi->orig_w = (int)lv_obj_get_width(wi->win);
        wi->orig_h = (int)lv_obj_get_height(wi->win);
        /* 最大化到桌面区域 */
        lv_obj_set_pos(wi->win, 0, 0);
        lv_obj_set_size(wi->win, 640, 480 - TASKBAR_H);
        wi->maximized = 1;
    }
}

/*======================================
 *  菜单项回调
 *======================================*/
static void menu_item_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    void (*cb)(void) = (void(*)(void))lv_obj_get_user_data(item);
    if (cb) {
        start_menu_hide();
        cb();
    }
}

static void start_menu_hide(void)
{
    if (g_start_menu) {
        lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    }
}

/*======================================
 *  程序项图标
 *======================================*/

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *label;
    const char *name;
    void (*action)(void);
    int grid_x, grid_y;
} prog_icon_t;

#define MAX_PROG_ICONS 24
static prog_icon_t g_prog_icons[MAX_PROG_ICONS];
static int         g_prog_icon_count = 0;

static void add_prog_icon(win_info_t *parent, const char *name,
                          const char *ascii_sym,
                          void (*action)(void),
                          int gx, int gy)
{
    if (g_prog_icon_count >= MAX_PROG_ICONS) return;
    prog_icon_t *pi = &g_prog_icons[g_prog_icon_count++];

    /* 网格原点(GRID_X0,GRID_TOP):首列/首行上方留给组标题与分隔线
     * Grid origin: header + separator live above the first row */
    int ox = GRID_X0 + gx * GRID_X;
    int oy = GRID_TOP + gy * GRID_Y;

    /* 图标背景(凹陷效果) */
    pi->icon = lv_obj_create(parent->client);
    lv_obj_set_size(pi->icon, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(pi->icon, ox, oy);
    lv_obj_set_style_bg_color(pi->icon, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(pi->icon, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pi->icon, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(pi->icon, 0, LV_PART_MAIN);

    /* 图标符号 */
    lv_obj_t *sym = lv_label_create(pi->icon);
    lv_label_set_text(sym, ascii_sym);
    lv_obj_set_style_text_font(sym, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(sym, LV_ALIGN_CENTER, 0, 0);

    /* 图标标签:定宽 GRID_X-8 居中于图标,禁止换行防相邻行叠压;
     * 超长名(如 "SQLite 浏览器")以省略号收尾
     * Fixed-width label centered under the icon, no wrap (DOT
     * ellipsis for long names) so rows never overlap */
    pi->label = lv_label_create(parent->client);
    lv_label_set_text(pi->label, name);
    lv_label_set_long_mode(pi->label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(pi->label, ox + ICON_SIZE / 2 - (GRID_X - 8) / 2,
                   oy + ICON_SIZE + 2);
    lv_obj_set_size(pi->label, GRID_X - 8, ICON_TEXT_H);
    lv_obj_set_style_text_font(pi->label, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(pi->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_all(pi->label, 0, 0);

    pi->name = name;
    pi->action = action;
    pi->grid_x = gx;
    pi->grid_y = gy;

    if (action) {
        lv_obj_set_user_data(pi->icon, pi);
        lv_obj_add_event_cb(pi->icon, desktop_icon_dblck, LV_EVENT_DOUBLE_CLICKED, pi);
    }
}

static void desktop_icon_dblck(lv_event_t *e)
{
    prog_icon_t *pi = (prog_icon_t *)lv_obj_get_user_data(lv_event_get_target(e));
    if (pi && pi->action) {
        pi->action();
    }
}

/*======================================
 *  应用程序窗口
 *======================================*/

/*----- Notepad -----*/
static void create_notepad(void)
{
    /* 使用完整的 Notepad 应用 */
    lv_obj_t *editor_win = editor_create();
    if (editor_win) {
        win_move_to_front(editor_win);
    }
}

/*----- Browser -----*/
static void create_browser(void)
{
    lv_obj_t *browser_win = browser_create();
    if (browser_win) {
        win_move_to_front(browser_win);
    }
}

/*----- Terminal -----*/
static void create_terminal(void)
{
    lv_obj_t *term_win = terminal_create();
    if (term_win) {
        win_move_to_front(term_win);
    }
}

/*----- Minesweeper -----*/
static void create_minesweeper(void)
{
    win_info_t *wi = win_create("Minesweeper", 120, 80, 240, 280);
    if (!wi) return;

    /* 游戏区 - 9x9 网格 */
    lv_obj_t *game_area = lv_obj_create(wi->client);
    lv_obj_set_size(game_area, 200, 200);
    lv_obj_set_pos(game_area, 20, 20);
    lv_obj_set_style_bg_color(game_area, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(game_area, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(game_area, WIN3_BORDER_HI, LV_PART_MAIN);

    lv_obj_t *info_lbl = lv_label_create(wi->client);
    lv_label_set_text(info_lbl, "Mines: 10\nTimer: 000");
    lv_obj_set_pos(info_lbl, 20, 4);
    lv_obj_set_style_text_font(info_lbl, RETRO_FONT_DEFAULT, 0);
}

/*----- Media Player -----*/
static void create_player(void)
{
    lv_obj_t *player_win = player_create();
    if (player_win) {
        win_move_to_front(player_win);
    }
}

/*----- Recorder -----*/
static void create_recorder(void)
{
    lv_obj_t *recorder_win = recorder_create();
    if (recorder_win) {
        win_move_to_front(recorder_win);
    }
}

/*----- SQLite Browser -----*/
static void create_sqlite(void)
{
    lv_obj_t *sqlite_win = sqlite_gui_create();
    if (sqlite_win) {
        win_move_to_front(sqlite_win);
    }
}

/*----- Control Panel -----*/
/* 语言切换回调 / Language switch callbacks */
static void lang_zh_cb(lv_event_t *e)
{
    (void)e;
    i18n_set_lang("zh_CN");
    syslog(LOG_INFO, "Language changed to: zh_CN\n");
    /* TODO: 通知所有窗口刷新文本 / Notify all windows to refresh */
}

static void lang_en_cb(lv_event_t *e)
{
    (void)e;
    i18n_set_lang("en_US");
    syslog(LOG_INFO, "Language changed to: en_US\n");
}

static void create_control_panel(void)
{
    win_info_t *wi = win_create(i18n_get("APP_CONTROL_PANEL"), 100, 60, 360, 320);
    if (!wi) return;

    /* 图标视图 */
    add_prog_icon(wi, "Display",     "[■]", NULL, 0, 0);
    add_prog_icon(wi, "Mouse",       "[+]", NULL, 1, 0);
    add_prog_icon(wi, "Keyboard",    "[K]", NULL, 2, 0);
    add_prog_icon(wi, "Printers",    "[P]", NULL, 0, 1);
    add_prog_icon(wi, "Fonts",       "[F]", NULL, 1, 1);
    add_prog_icon(wi, "Sound",       "[♪]", NULL, 2, 1);
    add_prog_icon(wi, "Date/Time",   "[T]", NULL, 0, 2);
    add_prog_icon(wi, "Network",     "[@]", NULL, 1, 2);
    add_prog_icon(wi, "Language",    "[中]", create_settings, 2, 2);

    /* 描述区 */
    lv_obj_t *desc = lv_label_create(wi->status_bar);
    lv_label_set_text(desc, i18n_get("ACTION_OPEN"));
    lv_obj_set_style_text_font(desc, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(desc, LV_ALIGN_LEFT_MID, 4, 0);
}

/*----- System Settings (语言设置) -----*/
static void create_settings(void)
{
    win_info_t *wi = win_create("System Settings / 系统设置", 120, 80, 320, 260);
    if (!wi) return;

    /* 语言选择区域 */
    lv_obj_t *lang_area = lv_obj_create(wi->client);
    lv_obj_set_size(lang_area, lv_obj_get_width(wi->client) - 8,
                        lv_obj_get_height(wi->client) - 8);
    lv_obj_set_pos(lang_area, 4, 4);
    lv_obj_set_style_bg_color(lang_area, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(lang_area, 0, LV_PART_MAIN);

    /* 语言选择标题 / Language title */
    lv_obj_t *title = lv_label_create(lang_area);
    lv_label_set_text(title, "Language / 语言");
    lv_obj_set_style_text_font(title, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    /* 当前语言 / Current language */
    lv_obj_t *current = lv_label_create(lang_area);
    char cur_lang[64];
    snprintf(cur_lang, sizeof(cur_lang), "Current / 当前: %s",
             strcmp(i18n_get_lang(), "zh_CN") == 0 ? "简体中文" : "English");
    lv_label_set_text(current, cur_lang);
    lv_obj_set_style_text_font(current, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(current, LV_ALIGN_TOP_LEFT, 8, 32);

    /* 中文按钮 / Chinese button */
    lv_obj_t *btn_zh = lv_button_create(lang_area);
    lv_obj_set_size(btn_zh, 120, 36);
    lv_obj_set_pos(btn_zh, 20, 70);
    lv_obj_set_style_bg_color(btn_zh, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_zh, 1, LV_PART_MAIN);

    lv_obj_t *lbl_zh = lv_label_create(btn_zh);
    lv_label_set_text(lbl_zh, "简体中文");
    lv_obj_set_style_text_color(lbl_zh, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_zh, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl_zh, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(btn_zh, lang_zh_cb, LV_EVENT_CLICKED, NULL);

    /* English 按钮 / English button */
    lv_obj_t *btn_en = lv_button_create(lang_area);
    lv_obj_set_size(btn_en, 120, 36);
    lv_obj_set_pos(btn_en, 160, 70);
    lv_obj_set_style_bg_color(btn_en, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_en, 1, LV_PART_MAIN);

    lv_obj_t *lbl_en = lv_label_create(btn_en);
    lv_label_set_text(lbl_en, "English");
    lv_obj_set_style_text_font(lbl_en, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl_en, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(btn_en, lang_en_cb, LV_EVENT_CLICKED, NULL);

    /* 说明 / Note */
    lv_obj_t *note = lv_label_create(lang_area);
    lv_label_set_text(note, "Language change takes effect immediately.\n语言切换立即生效。");
    lv_obj_set_style_text_font(note, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(note, WIN3_DKGRAY, LV_PART_MAIN);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 8, 120);
}

/*----- File Manager -----*/
static void create_file_manager(void)
{
    win_info_t *wi = win_create("File Manager", 60, 40, 480, 320);
    if (!wi) return;

    /* 工具栏 */
    lv_obj_t *toolbar = lv_obj_create(wi->client);
    lv_obj_set_size(toolbar, lv_obj_get_width(wi->client) - 4, 24);
    lv_obj_set_pos(toolbar, 2, 2);
    lv_obj_set_style_bg_color(toolbar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(toolbar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(toolbar, WIN3_BORDER_HI, LV_PART_MAIN);

    /* 驱动器选择 */
    lv_obj_t *drive_lbl = lv_label_create(toolbar);
    lv_label_set_text(drive_lbl, "[C:] [D:] [E:]");
    lv_obj_set_style_text_font(drive_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(drive_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 文件列表区 */
    lv_obj_t *file_list = lv_obj_create(wi->client);
    lv_obj_set_size(file_list, lv_obj_get_width(wi->client) - 4,
                        lv_obj_get_height(wi->client) - 30);
    lv_obj_set_pos(file_list, 2, 28);
    lv_obj_set_style_bg_color(file_list, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(file_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(file_list, WIN3_BORDER_MID, LV_PART_MAIN);

    lv_obj_t *files_lbl = lv_label_create(file_list);
    lv_label_set_text(files_lbl,
        "C:\\> dir\n"
        " CONFIG   SYS     1024  System\n"
        " APPS     DIR          Applications\n"
        " DOCS     DIR          Documents\n"
        " README   TXT     4096  Readme file\n"
        " NUTTX    BIN    65536  NuttX Firmware\n"
        " LVGL     DIR          LVGL Library");
    lv_obj_set_style_text_font(files_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(files_lbl, LV_ALIGN_TOP_LEFT, 4, 4);

    /* 状态栏 */
    lv_obj_t *sl = lv_label_create(wi->status_bar);
    lv_label_set_text(sl, "5 file(s)  68 KB free");
    lv_obj_set_style_text_font(sl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 4, 0);
}

/*======================================
 *  Program Manager (程序管理器)
 *======================================*/

static void create_progman(void)
{
    /* 窗口放在 (76,12):左侧留出 0..75 桌面快捷方式列,标签不再压窗
     * Window at (76,12): columns 0..75 stay free for the two desktop
     * shortcuts so their labels never overlap the window */
    g_progman = win_create(i18n_get("DESKTOP_TITLE"), 76, 12, 552, 430);
    if (!g_progman) return;

    int sep_w = (int)lv_obj_get_width(g_progman->client) - 8;

    /* === 程序组标题 - 使用 i18n / Program group titles - use i18n ===
     * 分组纵向节奏: 标题行 +16 分隔线 +14 图标行(86 一档)
     * Vertical rhythm: header +16 separator +14 icon row (86 pitch) */
    /* Main 组 / Main group */
    lv_obj_t *group1_hdr = lv_label_create(g_progman->client);
    lv_label_set_text(group1_hdr, i18n_get("PROG_MAIN"));
    lv_obj_set_pos(group1_hdr, 4, 2);
    lv_obj_set_style_text_font(group1_hdr, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(group1_hdr, WIN3_BLACK, LV_PART_MAIN);

    /* 分隔线 */
    lv_obj_t *sep1 = lv_obj_create(g_progman->client);
    lv_obj_set_size(sep1, sep_w, 1);
    lv_obj_set_pos(sep1, 4, 18);
    lv_obj_set_style_bg_color(sep1, WIN3_BORDER_MID, LV_PART_MAIN);

    /* Main 组图标 */
    add_prog_icon(g_progman, i18n_get("APP_CONTROL_PANEL"), "[■]", create_control_panel, 0, 0);
    add_prog_icon(g_progman, i18n_get("APP_FILE_MANAGER"),   "[D]", create_file_manager,  1, 0);

    /* Accessories 组(两行图标) / Accessories group (two rows) */
    lv_obj_t *group2_hdr = lv_label_create(g_progman->client);
    lv_label_set_text(group2_hdr, i18n_get("PROG_ACCESSORIES"));
    lv_obj_set_pos(group2_hdr, 4, 88);
    lv_obj_set_style_text_font(group2_hdr, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(group2_hdr, WIN3_BLACK, LV_PART_MAIN);

    lv_obj_t *sep2 = lv_obj_create(g_progman->client);
    lv_obj_set_size(sep2, sep_w, 1);
    lv_obj_set_pos(sep2, 4, 104);
    lv_obj_set_style_bg_color(sep2, WIN3_BORDER_MID, LV_PART_MAIN);

    add_prog_icon(g_progman, i18n_get("APP_NOTEPAD"),     "[T]", create_notepad,      0, 1);
    add_prog_icon(g_progman, i18n_get("APP_BROWSER"),    "[W]", create_browser,      1, 1);
    add_prog_icon(g_progman, i18n_get("APP_TERMINAL"),   "[>]", create_terminal,     2, 1);
    add_prog_icon(g_progman, i18n_get("APP_SQLITE"),     "[D]", create_sqlite,      3, 1);
    add_prog_icon(g_progman, i18n_get("APP_MEDIA_PLAYER"),"[♪]", create_player,      0, 2);
    add_prog_icon(g_progman, i18n_get("APP_RECORDER"),   "[●]", create_recorder,    1, 2);

    /* Games 组(独立行,不再与媒体播放器同格叠压)
     * Games group on its own row (no more cell collision) */
    lv_obj_t *group3_hdr = lv_label_create(g_progman->client);
    lv_label_set_text(group3_hdr, i18n_get("PROG_GAMES"));
    lv_obj_set_pos(group3_hdr, 4, 264);
    lv_obj_set_style_text_font(group3_hdr, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(group3_hdr, WIN3_BLACK, LV_PART_MAIN);

    lv_obj_t *sep3 = lv_obj_create(g_progman->client);
    lv_obj_set_size(sep3, sep_w, 1);
    lv_obj_set_pos(sep3, 4, 280);
    lv_obj_set_style_bg_color(sep3, WIN3_BORDER_MID, LV_PART_MAIN);

    add_prog_icon(g_progman, i18n_get("APP_MINESWEEPER"), "[#]", create_minesweeper, 0, 3);

    /* Startup 组 / Startup group */
    lv_obj_t *group4_hdr = lv_label_create(g_progman->client);
    lv_label_set_text(group4_hdr, i18n_get("PROG_STARTUP"));
    lv_obj_set_pos(group4_hdr, 4, 330);
    lv_obj_set_style_text_font(group4_hdr, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(group4_hdr, WIN3_BLACK, LV_PART_MAIN);

    lv_obj_t *sep4 = lv_obj_create(g_progman->client);
    lv_obj_set_size(sep4, sep_w, 1);
    lv_obj_set_pos(sep4, 4, 366);
    lv_obj_set_style_bg_color(sep4, WIN3_BORDER_MID, LV_PART_MAIN);

    /* === 桌面快捷方式:纵向排在 x=8 列,标签限宽 72 禁止换行,
     *  全部落在 0..75 区间,不会被 progman 窗口压住文字
     *  Desktop shortcuts: stacked in the x=8 column, labels capped
     *  at 72px wide (no wrap) inside 0..75 clear of the window === */
    lv_obj_t *my_pc = lv_obj_create(g_desktop);
    lv_obj_set_size(my_pc, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(my_pc, 8, 8);
    lv_obj_set_style_bg_color(my_pc, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(my_pc, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(my_pc, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(my_pc, 0, LV_PART_MAIN);

    lv_obj_t *my_pc_icon = lv_label_create(my_pc);
    lv_label_set_text(my_pc_icon, "[C]");
    lv_obj_set_style_text_font(my_pc_icon, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(my_pc_icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *my_pc_lbl = lv_label_create(g_desktop);
    lv_label_set_text(my_pc_lbl, i18n_get("MY_COMPUTER"));
    lv_label_set_long_mode(my_pc_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(my_pc_lbl, 2, ICON_SIZE + 10);
    lv_obj_set_size(my_pc_lbl, 72, ICON_TEXT_H);
    lv_obj_set_style_text_align(my_pc_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(my_pc_lbl, RETRO_FONT_DEFAULT, 0);
    /* 白字:DBG 黑底/青色桌面均清晰 / white text stays legible */
    lv_obj_set_style_text_color(my_pc_lbl, WIN3_WHITE, 0);
    lv_obj_set_style_pad_all(my_pc_lbl, 0, 0);

    /* 网上邻居(纵向第二个) / Network Neighbor (stacked below) */
    lv_obj_t *net_neigh = lv_obj_create(g_desktop);
    lv_obj_set_size(net_neigh, ICON_SIZE, ICON_SIZE);
    lv_obj_set_pos(net_neigh, 8, 66);
    lv_obj_set_style_bg_color(net_neigh, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(net_neigh, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(net_neigh, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(net_neigh, 0, LV_PART_MAIN);

    lv_obj_t *net_icon = lv_label_create(net_neigh);
    lv_label_set_text(net_icon, "[@]");
    lv_obj_set_style_text_font(net_icon, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(net_icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *net_lbl = lv_label_create(g_desktop);
    lv_label_set_text(net_lbl, i18n_get("NETWORK_NEIGHBOR"));
    lv_label_set_long_mode(net_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(net_lbl, 2, 100);
    lv_obj_set_size(net_lbl, 72, ICON_TEXT_H);
    lv_obj_set_style_text_align(net_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(net_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(net_lbl, WIN3_WHITE, 0);
    lv_obj_set_style_pad_all(net_lbl, 0, 0);
}

/*======================================
 *  时钟
 *======================================*/

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    taskbar_clock_update();
}

static void taskbar_clock_update(void)
{
    time_t now;
    struct tm tm_now;
    time(&now);
    localtime_r(&now, &tm_now);

    static char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d",
             tm_now.tm_hour, tm_now.tm_min);

    if (g_clock_lbl) {
        lv_label_set_text(g_clock_lbl, time_str);
    }
}

/*======================================
 *  外部接口
 *======================================*/

void retro_desktop_init(void)
{
    syslog(LOG_INFO, "Desktop: initializing Windows 3.2 style desktop\n");

    g_disp = lv_display_get_default();

    /* === 创建桌面背景 === */
    /* pad 归零:否则子对象整体内缩 16px,任务栏被推出屏幕底
     * Zero pad: theme padding would inset children and push the
     * taskbar off the bottom edge */
    g_desktop = lv_obj_create(lv_screen_active());
    lv_obj_set_size(g_desktop, 640, 480);
    lv_obj_set_style_bg_color(g_desktop, WIN3_CYAN, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_desktop, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_desktop, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_desktop, 0, LV_PART_MAIN);
    lv_obj_remove_flag(g_desktop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_desktop, LV_SCROLLBAR_MODE_OFF);

    /* === 创建任务栏 === */
    g_taskbar = lv_obj_create(g_desktop);
    lv_obj_set_size(g_taskbar, 640, TASKBAR_H);
    lv_obj_set_pos(g_taskbar, 0, 480 - TASKBAR_H);
    lv_obj_set_style_bg_color(g_taskbar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_taskbar, 0, LV_PART_MAIN);
    /* 任务栏顶部 3D 线 */
    lv_obj_set_style_border_width(g_taskbar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_taskbar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_border_side(g_taskbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    /* 禁止滚动:子对象曾比栏高撑出滚动导致整栏错位
     * No scrolling: oversized children used to misalign the bar */
    lv_obj_remove_flag(g_taskbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_taskbar, LV_SCROLLBAR_MODE_OFF);
    /* 圆角归零:主题默认圆角会让任务栏四角透出桌面底色
     * Zero radius: theme rounding let the desktop bleed through
     * the taskbar corners */
    lv_obj_set_style_radius(g_taskbar, 0, LV_PART_MAIN);

    /* 开始按钮 */
    g_start_btn = lv_button_create(g_taskbar);
    lv_obj_set_size(g_start_btn, START_BTN_W, 20);
    lv_obj_set_pos(g_start_btn, 4, 4);
    lv_obj_set_style_bg_color(g_start_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_start_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_start_btn, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(g_start_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(g_start_btn, start_menu_cb, LV_EVENT_CLICKED, NULL);

    g_start_lbl = lv_label_create(g_start_btn);
    lv_label_set_text(g_start_lbl, i18n_get("START"));
    lv_obj_set_style_text_font(g_start_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(g_start_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 任务栏分隔线 */
    lv_obj_t *task_sep = lv_obj_create(g_taskbar);
    lv_obj_set_size(task_sep, 2, 20);
    lv_obj_set_pos(task_sep, START_BTN_W + 4, 4);
    lv_obj_set_style_bg_color(task_sep, WIN3_BORDER_MID, LV_PART_MAIN);

    /* 时钟:显式 48x24 + 不换行,行高 30 的字库不再撑爆 28px 栏高
     * Clock: fixed 48x24 + no wrap so the 30px line height can't
     * overflow the 28px taskbar */
    g_clock_lbl = lv_label_create(g_taskbar);
    lv_label_set_text(g_clock_lbl, "00:00");
    lv_label_set_long_mode(g_clock_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(g_clock_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_size(g_clock_lbl, 48, 24);
    lv_obj_align(g_clock_lbl, LV_ALIGN_RIGHT_MID, -8, 0);
    taskbar_clock_update();

    lv_obj_set_style_pad_all(g_taskbar, 0, 0);

    /* === 构建开始菜单 === */
    build_start_menu();

    /* === 创建 Program Manager === */
    create_progman();

    syslog(LOG_INFO, "Desktop: Windows 3.2 desktop initialized, win_count=%d\n",
           g_win_count);

    /* 编译期默认外壳：可选 WindowMaker 风格 / compile-time default shell */
#ifdef CONFIG_RETRO_DESKTOP_SHELL_WMAKER
    retro_desktop_set_shell(RETRO_SHELL_WMAKER);
#endif
}

/*======================================
 *  外壳切换与应用派发 / Shell switch & app dispatch
 *======================================*/

/**
 * retro_desktop_ready - 桌面是否已初始化
 */
int retro_desktop_ready(void)
{
    return (g_disp && g_desktop) ? 1 : 0;
}

/**
 * retro_desktop_app_launch - 按应用标识启动窗口
 * retro_desktop_app_launch - launch an app window by id
 */
int retro_desktop_app_launch(const char *app_id)
{
    if (!app_id)
        return -EINVAL;

    if (!g_disp || !g_desktop)
        return -ENOSYS;

    if (strcmp(app_id, "notepad") == 0)           create_notepad();
    else if (strcmp(app_id, "browser") == 0)      create_browser();
    else if (strcmp(app_id, "terminal") == 0)     create_terminal();
    else if (strcmp(app_id, "minesweeper") == 0)  create_minesweeper();
    else if (strcmp(app_id, "player") == 0)       create_player();
    else if (strcmp(app_id, "recorder") == 0)     create_recorder();
    else if (strcmp(app_id, "sqlite") == 0)       create_sqlite();
    else if (strcmp(app_id, "controlpanel") == 0) create_control_panel();
    else if (strcmp(app_id, "settings") == 0)     create_settings();
    else if (strcmp(app_id, "filemanager") == 0)  create_file_manager();
    else
        return -ENOENT;

    return OK;
}

/**
 * retro_desktop_set_shell - 切换桌面外壳（Win3.2 <-> WindowMaker）
 * retro_desktop_set_shell - switch desktop shell
 */
int retro_desktop_set_shell(int mode)
{
    if (mode != RETRO_SHELL_WIN3 && mode != RETRO_SHELL_WMAKER)
        return -EINVAL;

    if (!g_disp || !g_desktop)
        return -ENOSYS;

    if (mode == g_shell_mode)
        return OK;

    if (mode == RETRO_SHELL_WMAKER) {
        /* 隐藏 Win3.2 外壳件 / hide Win3.2 chrome */
        if (g_taskbar)
            lv_obj_add_flag(g_taskbar, LV_OBJ_FLAG_HIDDEN);
        if (g_start_menu)
        printf("[dbg] startmenu %d,%d %dx%d hidden=%d\n",
               (int)lv_obj_get_x(g_start_menu), (int)lv_obj_get_y(g_start_menu),
               (int)lv_obj_get_width(g_start_menu), (int)lv_obj_get_height(g_start_menu),
               lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    if (g_progman && g_progman->win)
            lv_obj_add_flag(g_progman->win, LV_OBJ_FLAG_HIDDEN);

        wmaker_shell_create(g_desktop);
    } else {
        wmaker_shell_destroy();

        /* 恢复 Win3.2 外壳件 / restore Win3.2 chrome */
        if (g_taskbar)
            lv_obj_remove_flag(g_taskbar, LV_OBJ_FLAG_HIDDEN);
        if (g_start_menu)
        printf("[dbg] startmenu %d,%d %dx%d hidden=%d\n",
               (int)lv_obj_get_x(g_start_menu), (int)lv_obj_get_y(g_start_menu),
               (int)lv_obj_get_width(g_start_menu), (int)lv_obj_get_height(g_start_menu),
               lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    if (g_progman && g_progman->win)
            lv_obj_remove_flag(g_progman->win, LV_OBJ_FLAG_HIDDEN);
    }

    g_shell_mode = mode;
    syslog(LOG_INFO, "Desktop: shell switched to %s\n",
           mode == RETRO_SHELL_WMAKER ? "WindowMaker" : "Win3.2");
    return OK;
}

/**
 * retro_desktop_get_shell - 当前外壳模式
 */
int retro_desktop_get_shell(void)
{
    return g_shell_mode;
}

int retro_desktop_start(void)
{
    if (!g_disp) {
        syslog(LOG_ERR, "Desktop: no display available\n");
        return -ENODEV;
    }

    /* 注意: LVGL 刷新由外部主循环调用 lv_timer_handler() 驱动，
     * 此处不再创建额外定时器，避免无限递归 */

    /* 时钟更新定时器 */
    g_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_set_repeat_count(g_clock_timer, -1);

    syslog(LOG_INFO, "Desktop: started\n");
    return OK;
}

int retro_desktop_stop(void)
{
    if (g_timer) {
        lv_timer_delete(g_timer);
        g_timer = NULL;
    }
    if (g_clock_timer) {
        lv_timer_delete(g_clock_timer);
        g_clock_timer = NULL;
    }

    /* 删除所有窗口 */
    for (int i = 0; i < g_win_count; i++) {
        if (g_windows[i].win) {
            lv_obj_delete(g_windows[i].win);
            memset(&g_windows[i], 0, sizeof(win_info_t));
        }
    }
    g_win_count = 0;
    g_active_win = NULL;

    syslog(LOG_INFO, "Desktop: stopped\n");
    return OK;
}

#endif /* CONFIG_LVGL */

