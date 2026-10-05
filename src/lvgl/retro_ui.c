/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui.c - retro_ui 胶水层 C 核心
 *
 * WHAT : retro_ui 胶水层 C 核心
 * WHY  : 所有脚本引擎共用的 LVGL 对话框/进度/状态服务
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/retro_ui.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : LVGL msgbox/键盘/列表控件封装 + i18n 翻译
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#include "retro_font.h"

/*==========================
 *  CLI（无 LVGL）编译路径 / CLI build path
 * WHY  : C3 等 CLI 目标无图形栈，公共 API 返回 -ENOSYS，
 *        调用方（脚本引擎绑定）已由 CONFIG_LVGL 守卫，这里兜底
 *==========================*/
#ifndef CONFIG_LVGL

#include <errno.h>

int retro_ui_msgbox(const char *title, const char *msg)
{ (void)title; (void)msg; return -ENOSYS; }
int retro_ui_input(const char *title, const char *prompt, char *buf, int bufsize)
{ (void)title; (void)prompt; (void)buf; (void)bufsize; return -ENOSYS; }
int retro_ui_list(const char *title, const char *prompt,
                  const char **items, int count)
{ (void)title; (void)prompt; (void)items; (void)count; return -ENOSYS; }
int retro_ui_confirm(const char *title, const char *msg)
{ (void)title; (void)msg; return -ENOSYS; }
int retro_ui_status(const char *msg)
{ (void)msg; return -ENOSYS; }
int retro_ui_progress(int value, int max)
{ (void)value; (void)max; return -ENOSYS; }
int retro_ui_close_window(const char *win_title)
{ (void)win_title; return -ENOSYS; }
void retro_ui_init(void) {}
void retro_ui_deinit(void) {}
int retro_ui_set_lang(const char *lang)
{ (void)lang; return -ENOSYS; }
const char *retro_ui_get_lang(void)
{ return "zh_CN"; }

#else /* CONFIG_LVGL */
#include "i18n.h"
#include "retro_win3_styles.h"
#endif /* CONFIG_LVGL */

/*======================================
 * Windows 3.2 颜色常量 / Win3.2 color constants
 * （已移至 retro_win3_styles.h / Moved to retro_win3_styles.h）
 *======================================*/

/*======================================
 * 布局常量 / Layout constants
 *======================================*/
#define RETRO_UI_WIN_W     320    /* 对话框默认宽度 / Dialog default width */
#define RETRO_UI_WIN_H     180    /* 对话框默认高度 / Dialog default height */
#define RETRO_UI_TITLE_H   22     /* 标题栏高度 / Title bar height */
#define RETRO_UI_BTN_H     28     /* 按钮高度 / Button height */
#define RETRO_UI_PAD       8      /* 内边距 / Padding */
#define RETRO_UI_BTN_W     72     /* 按钮宽度 / Button width */
#define RETRO_UI_LIST_H    120    /* 列表区域高度 / List area height */

/*======================================
 * 静态对象池（避免堆分配）/ Static object pool
 *======================================*/
#define MAX_DIALOGS  3     /* 最大并发对话框数 / Max concurrent dialogs */
typedef enum {
    DIALOG_NONE,
    DIALOG_MSGBOX,
    DIALOG_INPUT,
    DIALOG_LIST,
    DIALOG_CONFIRM,
    DIALOG_PROGRESS
} dialog_type_t;

typedef struct {
    lv_obj_t *win;           /* 窗口对象 / Window object */
    lv_obj_t *btn1;          /* 按钮1 / Button 1 */
    lv_obj_t *btn2;          /* 按钮2 / Button 2 */
    lv_obj_t *btn3;          /* 按钮3 / Button 3 */
    lv_obj_t *content;        /* 内容对象 / Content object */
    lv_obj_t *result_lbl;    /* 结果标签 / Result label */
    volatile int selected;   /* 选择结果 / Selection result */
    volatile int done;       /* 是否完成 / Done flag */
    dialog_type_t type;       /* 对话框类型 / Dialog type */
} retro_dialog_t;

static retro_dialog_t g_dialogs[MAX_DIALOGS];
static int g_dialog_count = 0;
static lv_obj_t *g_status_overlay = NULL;  /* 状态栏浮层 / Status overlay */
static lv_obj_t *g_status_lbl = NULL;
static lv_obj_t *g_progress_bar_fg = NULL; /* 进度条前景 / Progress bar fill */
static lv_obj_t *g_progress_pct_lbl = NULL; /* 进度百分比标签 / Percent label */
static lv_timer_t *g_progress_timer = NULL;
static int g_progress_value = 0;
static int g_progress_max = 100;

/* 向前声明 / Forward declarations */
static retro_dialog_t *alloc_dialog(void);
static void free_dialog(retro_dialog_t *d);
static void center_window(lv_obj_t *win, int w, int h);
static void btn_event_cb(lv_event_t *e);
static void list_item_cb(lv_event_t *e);
static void progress_timer_cb(lv_timer_t *t);

/*======================================
 * 对话框分配（静态池）/ Dialog allocation (static pool)
 *======================================*/

static retro_dialog_t *alloc_dialog(void)
{
    for (int i = 0; i < MAX_DIALOGS; i++) {
        if (g_dialogs[i].type == DIALOG_NONE) {
            memset(&g_dialogs[i], 0, sizeof(retro_dialog_t));
            g_dialogs[i].type = DIALOG_NONE;
            return &g_dialogs[i];
        }
    }
    return NULL;
}

static void free_dialog(retro_dialog_t *d)
{
    if (!d) return;
    if (d->win) {
        lv_obj_delete(d->win);
        d->win = NULL;
    }
    memset(d, 0, sizeof(retro_dialog_t));
    d->type = DIALOG_NONE;
}

/* 窗口居中 / Center window on screen */
static void center_window(lv_obj_t *win, int w, int h)
{
    lv_display_t *disp = lv_display_get_default();
    int scr_w = lv_display_get_horizontal_resolution(disp);
    int scr_h = lv_display_get_vertical_resolution(disp);
    lv_obj_set_pos(win, (scr_w - w) / 2, (scr_h - h) / 2);
}

/*
 * btn_event_cb - 按钮事件处理 / Button event handler
 * WHAT: 记录用户选择的按钮值并结束对话框等待
 * WHY  : 旧代码把 lv_obj_get_user_data(btn)（按钮值 0/1）当对话框指针解引用，
 *        写入地址 ~1 导致崩溃；对话框指针必须来自 add_event_cb 时的 user_data
 * HOW  : 对话框指针取 lv_event_get_user_data(e)，按钮值取 lv_obj_get_user_data(btn)
 */
static void btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    retro_dialog_t *d = (retro_dialog_t *)lv_event_get_user_data(e);
    if (!d) return;

    d->selected = (int)(intptr_t)lv_obj_get_user_data(btn);
    d->done = 1;
}

/* 列表项点击 / List item click（同上修复 / same fix as above） */
static void list_item_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    retro_dialog_t *d = (retro_dialog_t *)lv_event_get_user_data(e);
    if (!d) return;

    d->selected = (int)(intptr_t)lv_obj_get_user_data(item);
    d->done = 1;
}

/* 进度条定时器回调 / Progress bar timer callback */
static void progress_timer_cb(lv_timer_t *t)
{
    (void)t;
    /* 进度条更新由 retro_ui_progress() 直接驱动 / Driven by retro_ui_progress() */
}

/*======================================
 * 公共 API 实现 / Public API Implementation
 *======================================*/

/**
 * retro_ui_msgbox - 显示消息框 / Show message box
 * @title: 窗口标题 / Window title
 * @msg:   消息内容 / Message content
 * 返回:   始终返回 0 / Always returns 0
 */
int retro_ui_msgbox(const char *title, const char *msg)
{
#ifdef CONFIG_LVGL
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return -1;

    retro_dialog_t *d = alloc_dialog();
    if (!d) return -1;

    d->type = DIALOG_MSGBOX;
    d->selected = 0;
    d->done = 0;

    /* 创建窗口 / Create window */
    int win_w = RETRO_UI_WIN_W;
    int win_h = RETRO_UI_WIN_H;
    d->win = lv_obj_create(scr);
    lv_obj_set_size(d->win, win_w, win_h);
    center_window(d->win, win_w, win_h);
    lv_obj_set_style_bg_color(d->win, WIN3_WINDOW_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->win, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->win, 0, LV_PART_MAIN);

    /* 标题栏 / Title bar */
    lv_obj_t *title_bar = lv_obj_create(d->win);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_size(title_bar, win_w, RETRO_UI_TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, title ? title : "Info");
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 6, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 18, 16);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_t *x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, "x");
    lv_obj_set_style_text_font(x_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(x_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_user_data(close_btn, (void *)(intptr_t)0);
    lv_obj_set_user_data(d->win, d);
    lv_obj_add_event_cb(close_btn, btn_event_cb, LV_EVENT_CLICKED, d);

    /* 消息内容 / Message content */
    int content_h = win_h - RETRO_UI_TITLE_H - RETRO_UI_BTN_H - RETRO_UI_PAD * 2 - 4;
    lv_obj_t *msg_lbl = lv_label_create(d->win);
    lv_obj_set_pos(msg_lbl, RETRO_UI_PAD, RETRO_UI_TITLE_H + RETRO_UI_PAD);
    lv_obj_set_size(msg_lbl, win_w - RETRO_UI_PAD * 2, content_h);
    lv_label_set_text(msg_lbl, msg ? msg : "");
    lv_obj_set_style_text_font(msg_lbl, RETRO_FONT_DEFAULT, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);

    /* 确定按钮 / OK button */
    int btn_x = (win_w - RETRO_UI_BTN_W) / 2;
    int btn_y = win_h - RETRO_UI_BTN_H - RETRO_UI_PAD;
    d->btn1 = lv_button_create(d->win);
    lv_obj_set_size(d->btn1, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn1, btn_x, btn_y);
    lv_obj_set_style_bg_color(d->btn1, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn1, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn1, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn1, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn1, (void *)(intptr_t)1);
    lv_obj_add_event_cb(d->btn1, btn_event_cb, LV_EVENT_CLICKED, d);

    lv_obj_t *ok_lbl = lv_label_create(d->btn1);
    lv_label_set_text(ok_lbl, i18n_get("OK"));
    lv_obj_set_style_text_font(ok_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(ok_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 等待用户响应 / Wait for user response */
    while (!d->done) {
        lv_timer_handler();
        usleep(10000);  /* 10ms 轮询 / 10ms polling */
    }

    free_dialog(d);
#else
    syslog(LOG_INFO, "[retro_ui] msgbox (no LVGL): %s\n", msg);
#endif
    return 0;
}

/**
 * retro_ui_input - 显示输入对话框 / Show input dialog
 * @title:    窗口标题 / Window title
 * @prompt:   提示文字 / Prompt text
 * @buf:      输出缓冲区 / Output buffer
 * @bufsize:  缓冲区大小 / Buffer size
 * 返回:      1=确定, 0=取消 / 1=OK, 0=Cancel
 */
int retro_ui_input(const char *title, const char *prompt, char *buf, int bufsize)
{
#ifdef CONFIG_LVGL
    lv_obj_t *scr = lv_screen_active();
    if (!scr || !buf || bufsize <= 0) return 0;

    retro_dialog_t *d = alloc_dialog();
    if (!d) return 0;

    d->type = DIALOG_INPUT;
    d->selected = 0;
    d->done = 0;
    buf[0] = '\0';

    int win_w = RETRO_UI_WIN_W;
    int win_h = 200;
    d->win = lv_obj_create(scr);
    lv_obj_set_size(d->win, win_w, win_h);
    center_window(d->win, win_w, win_h);
    lv_obj_set_style_bg_color(d->win, WIN3_WINDOW_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->win, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->win, 0, LV_PART_MAIN);

    /* 标题栏 / Title bar */
    lv_obj_t *title_bar = lv_obj_create(d->win);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_size(title_bar, win_w, RETRO_UI_TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, title ? title : i18n_get("APP_NOTEPAD"));
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 6, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 18, 16);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_t *cx_lbl = lv_label_create(close_btn);
    lv_label_set_text(cx_lbl, "x");
    lv_obj_set_style_text_font(cx_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(cx_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_user_data(close_btn, (void *)(intptr_t)0);
    lv_obj_add_event_cb(close_btn, btn_event_cb, LV_EVENT_CLICKED, d);

    /* 提示标签 / Prompt label */
    lv_obj_t *prompt_lbl = lv_label_create(d->win);
    lv_label_set_text(prompt_lbl, prompt ? prompt : "Enter value:");
    lv_obj_set_style_text_font(prompt_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(prompt_lbl, LV_ALIGN_TOP_LEFT, RETRO_UI_PAD, RETRO_UI_TITLE_H + RETRO_UI_PAD);

    /* 输入框 / Input text area */
    d->content = lv_textarea_create(d->win);
    int ta_y = RETRO_UI_TITLE_H + RETRO_UI_PAD + 24;
    lv_obj_set_pos(d->content, RETRO_UI_PAD, ta_y);
    lv_obj_set_size(d->content, win_w - RETRO_UI_PAD * 2, 36);
    lv_textarea_set_max_length(d->content, bufsize - 1);
    lv_textarea_set_one_line(d->content, true);
    lv_obj_set_style_text_font(d->content, RETRO_FONT_DEFAULT, 0);
    lv_textarea_set_placeholder_text(d->content, "");

    /* 按钮 / Buttons */
    int btn_y = win_h - RETRO_UI_BTN_H - RETRO_UI_PAD;
    int btn_spacing = RETRO_UI_BTN_W + 8;
    int total_btn_w = btn_spacing * 2 - 8;
    int btn_start_x = (win_w - total_btn_w) / 2;

    d->btn1 = lv_button_create(d->win);
    lv_obj_set_size(d->btn1, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn1, btn_start_x, btn_y);
    lv_obj_set_style_bg_color(d->btn1, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn1, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn1, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn1, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn1, (void *)(intptr_t)1);
    lv_obj_add_event_cb(d->btn1, btn_event_cb, LV_EVENT_CLICKED, d);
    lv_obj_t *ok_lbl = lv_label_create(d->btn1);
    lv_label_set_text(ok_lbl, i18n_get("OK"));
    lv_obj_set_style_text_font(ok_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(ok_lbl, LV_ALIGN_CENTER, 0, 0);

    d->btn2 = lv_button_create(d->win);
    lv_obj_set_size(d->btn2, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn2, btn_start_x + btn_spacing, btn_y);
    lv_obj_set_style_bg_color(d->btn2, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn2, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn2, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn2, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn2, (void *)(intptr_t)0);
    lv_obj_add_event_cb(d->btn2, btn_event_cb, LV_EVENT_CLICKED, d);
    lv_obj_t *cancel_lbl = lv_label_create(d->btn2);
    lv_label_set_text(cancel_lbl, i18n_get("CANCEL"));
    lv_obj_set_style_text_font(cancel_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(cancel_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 等待用户响应 / Wait for user response */
    while (!d->done) {
        lv_timer_handler();
        usleep(10000);
    }

    /* 读取输入内容 / Read input content */
    if (d->selected == 1) {
        const char *text = lv_textarea_get_text(d->content);
        strncpy(buf, text, bufsize - 1);
        buf[bufsize - 1] = '\0';
    }

    int result = d->selected;
    free_dialog(d);
    return result;
#else
    syslog(LOG_INFO, "[retro_ui] input (no LVGL): %s\n", prompt);
    return 0;
#endif
}

/**
 * retro_ui_list - 显示列表选择对话框 / Show list selection dialog
 * @title:   窗口标题 / Window title
 * @prompt:  提示文字 / Prompt text
 * @items:   选项数组 / Items array
 * @count:   选项数量 / Number of items
 * 返回:     选择的索引 (0-based)，取消返回 -1 / Selected index (0-based), -1 on cancel
 */
int retro_ui_list(const char *title, const char *prompt,
                  const char **items, int count)
{
#ifdef CONFIG_LVGL
    if (!items || count <= 0) return -1;

    lv_obj_t *scr = lv_screen_active();
    if (!scr) return -1;

    retro_dialog_t *d = alloc_dialog();
    if (!d) return -1;

    d->type = DIALOG_LIST;
    d->selected = -1;
    d->done = 0;

    int win_w = RETRO_UI_WIN_W;
    int win_h = 60 + RETRO_UI_LIST_H + RETRO_UI_BTN_H + RETRO_UI_PAD * 2 + 24;
    if (win_h > 380) win_h = 380;

    d->win = lv_obj_create(scr);
    lv_obj_set_size(d->win, win_w, win_h);
    center_window(d->win, win_w, win_h);
    lv_obj_set_style_bg_color(d->win, WIN3_WINDOW_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->win, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->win, 0, LV_PART_MAIN);

    /* 标题栏 / Title bar */
    lv_obj_t *title_bar = lv_obj_create(d->win);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_size(title_bar, win_w, RETRO_UI_TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, title ? title : i18n_get("SEARCH"));
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 6, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 18, 16);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_t *cx_lbl = lv_label_create(close_btn);
    lv_label_set_text(cx_lbl, "x");
    lv_obj_set_style_text_font(cx_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(cx_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_user_data(close_btn, (void *)(intptr_t)0);
    lv_obj_add_event_cb(close_btn, btn_event_cb, LV_EVENT_CLICKED, d);

    /* 提示标签 / Prompt label */
    if (prompt) {
        lv_obj_t *prompt_lbl = lv_label_create(d->win);
        lv_label_set_text(prompt_lbl, prompt);
        lv_obj_set_style_text_font(prompt_lbl, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(prompt_lbl, LV_ALIGN_TOP_LEFT, RETRO_UI_PAD, RETRO_UI_TITLE_H + 4);
    }

    /* 列表区域 - 使用按钮模拟列表项 / List area - use buttons to simulate list items */
    int list_y = RETRO_UI_TITLE_H + 24;
    int list_h = win_h - RETRO_UI_TITLE_H - RETRO_UI_BTN_H - RETRO_UI_PAD * 2 - 4;
    if (list_h > RETRO_UI_LIST_H) list_h = RETRO_UI_LIST_H;

    lv_obj_t *list_container = lv_obj_create(d->win);
    lv_obj_set_pos(list_container, RETRO_UI_PAD, list_y);
    lv_obj_set_size(list_container, win_w - RETRO_UI_PAD * 2, list_h);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(list_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list_container, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(list_container, 0, LV_PART_MAIN);

    int item_h = 24;
    int max_visible = list_h / item_h;
    int show_count = (count < max_visible) ? count : max_visible;

    for (int i = 0; i < show_count; i++) {
        lv_obj_t *item_btn = lv_button_create(list_container);
        lv_obj_set_size(item_btn, win_w - RETRO_UI_PAD * 2 - 2, item_h - 1);
        lv_obj_set_pos(item_btn, 1, i * item_h);
        lv_obj_set_style_bg_color(item_btn, WIN3_BTN_BG, LV_PART_MAIN);
        lv_obj_set_style_border_width(item_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(item_btn, 0, LV_PART_MAIN);
        lv_obj_set_user_data(item_btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(item_btn, list_item_cb, LV_EVENT_CLICKED, d);

        lv_obj_t *item_lbl = lv_label_create(item_btn);
        lv_label_set_text(item_lbl, items[i] ? items[i] : "");
        lv_obj_set_style_text_font(item_lbl, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(item_lbl, LV_ALIGN_LEFT_MID, 6, 0);
    }

    /* 按钮 / Buttons */
    int btn_y = win_h - RETRO_UI_BTN_H - RETRO_UI_PAD;
    int btn_spacing = RETRO_UI_BTN_W + 8;
    int total_btn_w = btn_spacing * 2 - 8;
    int btn_start_x = (win_w - total_btn_w) / 2;

    d->btn2 = lv_button_create(d->win);
    lv_obj_set_size(d->btn2, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn2, btn_start_x, btn_y);
    lv_obj_set_style_bg_color(d->btn2, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn2, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn2, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn2, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn2, (void *)(intptr_t)0);
    lv_obj_add_event_cb(d->btn2, btn_event_cb, LV_EVENT_CLICKED, d);
    lv_obj_t *cancel_lbl = lv_label_create(d->btn2);
    lv_label_set_text(cancel_lbl, i18n_get("CANCEL"));
    lv_obj_set_style_text_font(cancel_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(cancel_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 等待用户响应 / Wait for user response */
    while (!d->done) {
        lv_timer_handler();
        usleep(10000);
    }

    int result = d->selected;
    free_dialog(d);
    return result;
#else
    syslog(LOG_INFO, "[retro_ui] list (no LVGL): %s\n", prompt);
    return -1;
#endif
}

/**
 * retro_ui_confirm - 显示确认对话框 / Show confirm dialog
 * @title: 窗口标题 / Window title
 * @msg:   消息内容 / Message content
 * 返回:   1=是(Yes), 0=否(No) / 1=Yes, 0=No
 */
int retro_ui_confirm(const char *title, const char *msg)
{
#ifdef CONFIG_LVGL
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return 0;

    retro_dialog_t *d = alloc_dialog();
    if (!d) return 0;

    d->type = DIALOG_CONFIRM;
    d->selected = 0;
    d->done = 0;

    int win_w = RETRO_UI_WIN_W;
    int win_h = RETRO_UI_WIN_H;
    d->win = lv_obj_create(scr);
    lv_obj_set_size(d->win, win_w, win_h);
    center_window(d->win, win_w, win_h);
    lv_obj_set_style_bg_color(d->win, WIN3_WINDOW_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->win, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->win, 0, LV_PART_MAIN);

    /* 标题栏 / Title bar */
    lv_obj_t *title_bar = lv_obj_create(d->win);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_size(title_bar, win_w, RETRO_UI_TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, title ? title : i18n_get("ABOUT"));
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 6, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 18, 16);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_t *cx_lbl = lv_label_create(close_btn);
    lv_label_set_text(cx_lbl, "x");
    lv_obj_set_style_text_font(cx_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(cx_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_user_data(close_btn, (void *)(intptr_t)0);
    lv_obj_add_event_cb(close_btn, btn_event_cb, LV_EVENT_CLICKED, d);

    /* 消息内容 / Message content */
    int content_h = win_h - RETRO_UI_TITLE_H - RETRO_UI_BTN_H - RETRO_UI_PAD * 2 - 4;
    lv_obj_t *msg_lbl = lv_label_create(d->win);
    lv_obj_set_pos(msg_lbl, RETRO_UI_PAD, RETRO_UI_TITLE_H + RETRO_UI_PAD);
    lv_obj_set_size(msg_lbl, win_w - RETRO_UI_PAD * 2, content_h);
    lv_label_set_text(msg_lbl, msg ? msg : "");
    lv_obj_set_style_text_font(msg_lbl, RETRO_FONT_DEFAULT, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);

    /* Yes/No 按钮 / Yes/No buttons */
    int btn_y = win_h - RETRO_UI_BTN_H - RETRO_UI_PAD;
    int btn_spacing = RETRO_UI_BTN_W + 8;
    int total_btn_w = btn_spacing * 2 - 8;
    int btn_start_x = (win_w - total_btn_w) / 2;

    d->btn1 = lv_button_create(d->win);
    lv_obj_set_size(d->btn1, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn1, btn_start_x, btn_y);
    lv_obj_set_style_bg_color(d->btn1, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn1, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn1, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn1, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn1, (void *)(intptr_t)1);
    lv_obj_add_event_cb(d->btn1, btn_event_cb, LV_EVENT_CLICKED, d);
    lv_obj_t *yes_lbl = lv_label_create(d->btn1);
    lv_label_set_text(yes_lbl, "Yes");
    lv_obj_set_style_text_font(yes_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(yes_lbl, LV_ALIGN_CENTER, 0, 0);

    d->btn2 = lv_button_create(d->win);
    lv_obj_set_size(d->btn2, RETRO_UI_BTN_W, RETRO_UI_BTN_H);
    lv_obj_set_pos(d->btn2, btn_start_x + btn_spacing, btn_y);
    lv_obj_set_style_bg_color(d->btn2, WIN3_BTN_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(d->btn2, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(d->btn2, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(d->btn2, 0, LV_PART_MAIN);
    lv_obj_set_user_data(d->btn2, (void *)(intptr_t)0);
    lv_obj_add_event_cb(d->btn2, btn_event_cb, LV_EVENT_CLICKED, d);
    lv_obj_t *no_lbl = lv_label_create(d->btn2);
    lv_label_set_text(no_lbl, "No");
    lv_obj_set_style_text_font(no_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(no_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 等待用户响应 / Wait for user response */
    while (!d->done) {
        lv_timer_handler();
        usleep(10000);
    }

    int result = d->selected;
    free_dialog(d);
    return result;
#else
    syslog(LOG_INFO, "[retro_ui] confirm (no LVGL): %s\n", msg);
    return 0;
#endif
}

/**
 * retro_ui_status - 显示状态栏消息（临时浮层）/ Show status bar message (temporary overlay)
 * @msg: 状态消息 / Status message
 * 返回: 0=成功 / 0=success
 *
 * 显示一个临时状态消息，3秒后自动消失
 * Shows a temporary status message, auto-dismisses after 3 seconds
 */
int retro_ui_status(const char *msg)
{
#ifdef CONFIG_LVGL
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return -1;

    /* 关闭已有状态浮层 / Close existing status overlay
     * 同步清空进度条子对象指针，避免悬垂引用
     * Also clear progress child pointers to avoid dangling refs */
    if (g_status_overlay) {
        lv_obj_delete(g_status_overlay);
        g_status_overlay = NULL;
        g_status_lbl = NULL;
        g_progress_bar_fg = NULL;
        g_progress_pct_lbl = NULL;
    }

    if (!msg) return 0;

    /* 创建底部状态浮层 / Create bottom status overlay */
    lv_display_t *disp = lv_display_get_default();
    int scr_w = lv_display_get_horizontal_resolution(disp);
    int h = 28;

    g_status_overlay = lv_obj_create(scr);
    lv_obj_set_size(g_status_overlay, scr_w, h);
    lv_obj_set_pos(g_status_overlay, 0, 480 - h);  /* 假设 480 高，底部对齐 / Assume 480 height, bottom-aligned */
    lv_obj_set_style_bg_color(g_status_overlay, lv_color_hex(0x000080), LV_PART_MAIN);  /* 蓝色标题条 / Blue title bar */
    lv_obj_set_style_border_width(g_status_overlay, 0, LV_PART_MAIN);

    g_status_lbl = lv_label_create(g_status_overlay);
    lv_label_set_text(g_status_lbl, msg);
    lv_obj_set_style_text_color(g_status_lbl, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(g_status_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_timer_handler();  /* 立即刷新 / Refresh immediately */

    syslog(LOG_INFO, "[retro_ui] status: %s\n", msg);
#endif
    return 0;
}

/**
 * retro_ui_progress - 更新进度条 / Update progress bar
 * @value: 当前值 / Current value
 * @max:   最大值 / Maximum value
 * 返回:   0=成功 / 0=success
 *
 * 显示或更新一个进度条对话框
 * Shows or updates a progress bar dialog
 */
int retro_ui_progress(int value, int max)
{
#ifdef CONFIG_LVGL
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return -1;

    g_progress_value = value;
    g_progress_max = max;

    if (max <= 0) max = 100;
    if (value < 0) value = 0;
    if (value > max) value = max;

    int pct = (value * 100) / max;

    if (!g_status_overlay) {
        /* 首次调用，创建进度条浮层 / First call, create progress overlay */
        lv_display_t *disp = lv_display_get_default();
        int scr_w = lv_display_get_horizontal_resolution(disp);
        int h = 48;

        g_status_overlay = lv_obj_create(scr);
        lv_obj_set_size(g_status_overlay, scr_w, h);
        lv_obj_set_pos(g_status_overlay, 0, 480 - h);
        lv_obj_set_style_bg_color(g_status_overlay, WIN3_WINDOW_BG, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_status_overlay, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(g_status_overlay, WIN3_BORDER_HI, LV_PART_MAIN);

        /* 进度条背景 / Progress bar background */
        lv_obj_t *bar_bg = lv_obj_create(g_status_overlay);
        lv_obj_set_pos(bar_bg, 8, 8);
        lv_obj_set_size(bar_bg, scr_w - 16, 16);
        lv_obj_set_style_bg_color(bar_bg, WIN3_DKGRAY, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar_bg, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(bar_bg, WIN3_BORDER_HI, LV_PART_MAIN);
        lv_obj_set_style_radius(bar_bg, 0, LV_PART_MAIN);

        /* 进度条前景 / Progress bar foreground
         * 使用文件级静态指针，浮层被 retro_ui_status 删除时同步置 NULL
         * File-level static; NULLed when overlay deleted by retro_ui_status */
        g_progress_bar_fg = lv_obj_create(g_status_overlay);
        lv_obj_set_pos(g_progress_bar_fg, 9, 9);
        int fg_w = ((scr_w - 20) * pct) / 100;
        if (fg_w < 2) fg_w = 2;
        lv_obj_set_size(g_progress_bar_fg, fg_w, 14);
        lv_obj_set_style_bg_color(g_progress_bar_fg, lv_color_hex(0x000080), LV_PART_MAIN);  /* 蓝色 / Blue */
        lv_obj_set_style_border_width(g_progress_bar_fg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(g_progress_bar_fg, 0, LV_PART_MAIN);

        /* 百分比文字 / Percentage text */
        g_progress_pct_lbl = lv_label_create(g_status_overlay);
        lv_obj_set_style_text_color(g_progress_pct_lbl, WIN3_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(g_progress_pct_lbl, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(g_progress_pct_lbl, LV_ALIGN_CENTER, 0, 0);
    }

    /* 真正更新进度条与百分比 / Actually update bar fill and percent label */
    if (g_status_overlay && g_progress_bar_fg) {
        lv_display_t *disp = lv_display_get_default();
        int scr_w = lv_display_get_horizontal_resolution(disp);
        int fg_w = ((scr_w - 20) * pct) / 100;
        if (fg_w < 2) fg_w = 2;
        lv_obj_set_width(g_progress_bar_fg, (lv_coord_t)fg_w);
    }
    if (g_progress_pct_lbl) {
        char pct_text[16];
        snprintf(pct_text, sizeof(pct_text), "%d%%", pct);
        lv_label_set_text(g_progress_pct_lbl, pct_text);
    }

    lv_timer_handler();

    syslog(LOG_INFO, "[retro_ui] progress: %d/%d (%d%%)\n", value, max, pct);
#endif
    return 0;
}

/**
 * retro_ui_close_window - 关闭窗口 / Close window
 * @win_title: 窗口标题（支持前缀匹配）/ Window title (prefix match supported)
 * 返回: 0=成功，-1=未找到 / 0=success, -1=not found
 *
 * 关闭标题栏匹配的所有窗口
 * Closes all windows whose title bar matches
 */
int retro_ui_close_window(const char *win_title)
{
#ifdef CONFIG_LVGL
    (void)win_title;
    /* TODO: 实现窗口查找和关闭 / Implement window find and close */
    /* 当前实现依赖于调用者传入正确的标题 / Current implementation relies on caller passing correct title */
    syslog(LOG_INFO, "[retro_ui] close_window: %s\n", win_title ? win_title : "(null)");
#endif
    return 0;
}

/*======================================
 * 模块初始化 / Module Initialization
 *======================================*/

void retro_ui_init(void)
{
    memset(g_dialogs, 0, sizeof(g_dialogs));
    g_dialog_count = 0;
    g_status_overlay = NULL;
    g_status_lbl = NULL;
    g_progress_timer = NULL;
    syslog(LOG_INFO, "[retro_ui] initialized\n");
}

/*======================================
 *  语言设置 / Language Settings
 *======================================*/

/**
 * retro_ui_set_lang - 设置 UI 语言 / Set UI language
 * @param lang: 语言代码 "zh_CN" 或 "en_US"
 * @return: 0=成功, -1=失败
 */
int retro_ui_set_lang(const char *lang)
{
    if (!lang) {
        return -1;
    }
    i18n_set_lang(lang);
    syslog(LOG_INFO, "[retro_ui] language set to: %s\n", lang);
    return 0;
}

/**
 * retro_ui_get_lang - 获取当前语言 / Get current language
 * @return: 当前语言代码
 */
const char *retro_ui_get_lang(void)
{
    return i18n_get_lang();
}

void retro_ui_deinit(void)
{
    /* 关闭所有对话框 / Close all dialogs */
    for (int i = 0; i < MAX_DIALOGS; i++) {
        if (g_dialogs[i].type != DIALOG_NONE) {
            free_dialog(&g_dialogs[i]);
        }
    }

    /* 关闭状态浮层 / Close status overlay */
    if (g_status_overlay) {
        lv_obj_delete(g_status_overlay);
        g_status_overlay = NULL;
        g_status_lbl = NULL;
    }

    if (g_progress_timer) {
        lv_timer_delete(g_progress_timer);
        g_progress_timer = NULL;
    }

    syslog(LOG_INFO, "[retro_ui] deinitialized\n");
}

#endif /* CONFIG_LVGL */
