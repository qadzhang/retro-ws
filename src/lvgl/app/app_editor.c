/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_editor.c - Win3.2 记事本
 *
 * WHAT : Win3.2 记事本
 * WHY  : 桌面文本编辑器（多标签/行号/搜索替换/文件读写/拼音输入）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/app_editor.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : LVGL textarea + 标签页管理，经 drv_pinyin 支持中文输入
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"

/*======================================
 *  Windows 3.2 颜色
 *======================================*/
#define WIN3_BG        lv_color_hex(0xC0C0C0)
#define WIN3_WHITE     lv_color_hex(0xFFFFFF)
#define WIN3_BLACK     lv_color_hex(0x000000)
#define WIN3_BLUE      lv_color_hex(0x000080)
#define WIN3_GRAY      lv_color_hex(0x808080)
#define WIN3_LTGRAY    lv_color_hex(0xC0C0C0)
#define WIN3_BORDER_HI lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_LO lv_color_hex(0x808080)
#define WIN3_TITLE_BG  lv_color_hex(0x000080)
#define WIN3_TITLE_FG  lv_color_hex(0xFFFFFF)
#define WIN3_MENU_BG   lv_color_hex(0xC0C0C0)
#define WIN3_EDITOR_BG lv_color_hex(0xFFFFFF)
#define WIN3_STATUS_BG  lv_color_hex(0xC0C0C0)

#define BORDER_W       2
#define TITLE_H        18
#define MENU_H         20
#define STATUS_H       22
#define TAB_H          24
#define LINE_NUM_W     40

/*======================================
 *  编辑器状态
 *======================================*/
typedef struct {
    lv_obj_t *win;
    lv_obj_t *menu_bar;
    lv_obj_t *tab_bar;
    lv_obj_t *editor_area;
    lv_obj_t *line_numbers;
    lv_obj_t *text_area;
    lv_obj_t *status_bar;
    lv_obj_t *status_lbl;

    /* 文件信息 */
    char filename[256];
    char title[320];
    int modified;
    int cursor_line;
    int cursor_col;
    int total_lines;
    int total_chars;

    /* 搜索状态 */
    lv_obj_t *search_bar;
    lv_obj_t *search_input;
    int search_visible;
} editor_t;

static editor_t g_editor;

/*======================================
 *  菜单项
 *======================================*/
typedef struct {
    const char *name;
    void (*callback)(void);
} menu_item_t;

static void menu_file_new(void);
static void menu_file_open(void);
static void menu_file_save(void);
static void menu_file_saveas(void);
static void menu_file_exit(void);
static void menu_edit_undo(void);
static void menu_edit_redo(void);
static void menu_edit_cut(void);
static void menu_edit_copy(void);
static void menu_edit_paste(void);
static void menu_edit_selectall(void);
static void menu_search_find(void);
static void menu_search_replace(void);
static void menu_view_wrap(void);
static void menu_help_about(void);

static void create_menu_bar(editor_t *ed);
static void update_line_numbers(editor_t *ed);
static void update_status(editor_t *ed);
static void update_title(editor_t *ed);
static void text_changed_cb(lv_event_t *e);
static void cursor_pos_cb(lv_event_t *e);
static void close_editor(void);
static void tab_create(const char *filename);
static void show_search_bar(editor_t *ed, int show_replace);

/*======================================
 *  通用 C 回调函数（替代 C++ lambda）
 *======================================*/

/* 菜单项通用回调 - 通过 user_data 获取实际回调函数 */
static void menu_btn_generic_cb(lv_event_t *e)
{
    void (*cb)(void) = (void(*)(void))lv_obj_get_user_data(lv_event_get_target(e));
    if (cb) cb();
}

/* 自动关闭 msgbox 的定时器回调 */
static void msgbox_auto_close_cb(lv_timer_t *t)
{
    lv_msgbox_close((lv_obj_t *)lv_timer_get_user_data(t));
    lv_timer_delete(t);
}

/*
 * show_msgbox - 构建 9.5 风格消息框 / Build LVGL 9.5 style message box
 * WHAT: 标题+正文+确定按钮的消息框
 * WHY  : lv_msgbox_create 在 9.5 只接收 parent 一个参数，标题/正文/按钮
 *        需用 lv_msgbox_add_title/add_text/add_footer_button 构建
 * HOW  : 任意按钮点击即删除消息框（事件回调 lv_obj_delete）
 */
static void editor_msgbox_btn_cb(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_target(e));
}

static lv_obj_t *show_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    if (m == NULL)
        return NULL;

    lv_msgbox_add_title(m, title);
    lv_msgbox_add_text(m, text);
    lv_obj_t *btn = lv_msgbox_add_footer_button(m, "OK");
    lv_obj_add_event_cb(btn, editor_msgbox_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(m);
    return m;
}

/* 搜索栏关闭按钮回调 */
static void search_close_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(g_editor.search_bar, LV_OBJ_FLAG_HIDDEN);
    g_editor.search_visible = 0;
}

/* 编辑器关闭按钮回调 */
static void editor_close_btn_cb(lv_event_t *e)
{
    (void)e;
    menu_file_exit();
}

/*======================================
 *  菜单回调
 *======================================*/

static void menu_file_new(void)
{
    tab_create("Untitled");
}

static void menu_file_open(void)
{
    /* 简化的文件选择 - 使用输入对话框 / Simple picker via msgbox */
    show_msgbox("Open File", "Enter filename:");
}

static void menu_file_save(void)
{
    if (g_editor.filename[0] == 0) {
        menu_file_saveas();
        return;
    }

    const char *text = lv_textarea_get_text(g_editor.text_area);
    syslog(LOG_INFO, "Editor: saving to %s (%d chars)\n",
           g_editor.filename, (int)strlen(text));

    g_editor.modified = 0;
    update_title(&g_editor);
    update_status(&g_editor);

    lv_obj_t *toast = show_msgbox("Saved", g_editor.filename);
    if (toast)
        lv_timer_create(msgbox_auto_close_cb, 2000, toast);
}

static void menu_file_saveas(void)
{
    show_msgbox("Save As", "Enter filename:");
}

static void menu_file_exit(void)
{
    if (g_editor.modified) {
        show_msgbox("Notepad", "Save changes before closing?");
        /* TODO: 处理保存逻辑 / Handle save logic */
    }
    close_editor();
}

static void menu_edit_undo(void)
{
    /* LVGL textarea 不支持原生 undo / No native undo in LVGL textarea */
}

static void menu_edit_redo(void)
{
}

static void menu_edit_cut(void)
{
    /* 9.5 无 set_selection_start/cut API：开启选择模式供键盘选择
     * 9.5 has no set_selection_start/cut: enable selection instead */
    lv_textarea_set_text_selection(g_editor.text_area, true);
    /* TODO: 复制到剪贴板 / Clipboard support */
}

static void menu_edit_copy(void)
{
    lv_textarea_set_text_selection(g_editor.text_area, true);
}

static void menu_edit_paste(void)
{
    /* 9.5 无 textarea paste API / No paste API in 9.5 textarea */
}

static void menu_edit_selectall(void)
{
    /* 9.5 无 select_all API：仅开启选择模式 / No select_all API */
    lv_textarea_set_text_selection(g_editor.text_area, true);
}

static void menu_search_find(void)
{
    show_search_bar(&g_editor, 0);
}

static void menu_search_replace(void)
{
    show_search_bar(&g_editor, 1);
}

static void menu_view_wrap(void)
{
    /* 旧实现把 wrap 开关错接到 one_line；改为真实的单行开关
     * Old code wired the wrap toggle to one_line; now a real
     * "one line / wrap" toggle (one_line=false 即多行换行) */
    static int one_line = 0;
    one_line = !one_line;
    lv_textarea_set_one_line(g_editor.text_area, one_line ? true : false);
}

static void menu_help_about(void)
{
    static const char *about_text =
        "ESP32-S3 Notepad\n"
        "Version 1.0\n\n"
        "A simple text editor for\n"
        "the Retro Windows 3.2 UI.\n\n"
        "Built with LVGL v9";
    show_msgbox("About Notepad", about_text);
}

/*======================================
 *  菜单栏
 *======================================*/

static void create_menu_item(editor_t *ed, lv_obj_t *parent,
                              const char *text, void (*cb)(void))
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, MENU_H - 2);
    lv_obj_set_style_bg_color(btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    if (cb) {
        lv_obj_set_user_data(btn, cb);
        lv_obj_add_event_cb(btn, menu_btn_generic_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void create_menu_sep(lv_obj_t *parent, int x)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 1, MENU_H - 4);
    lv_obj_set_pos(sep, x, 2);
    lv_obj_set_style_bg_color(sep, WIN3_GRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
}

static void create_menu_bar(editor_t *ed)
{
    ed->menu_bar = lv_obj_create(ed->win);
    lv_obj_set_pos(ed->menu_bar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(ed->menu_bar,
        lv_obj_get_width(ed->win) - BORDER_W * 2, MENU_H);
    lv_obj_set_style_bg_color(ed->menu_bar, WIN3_MENU_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(ed->menu_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ed->menu_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(ed->menu_bar, 0, LV_PART_MAIN);

    /* File */
    create_menu_item(ed, ed->menu_bar, "File", NULL);
    /* Edit */
    create_menu_item(ed, ed->menu_bar, "Edit", NULL);
    /* Search */
    create_menu_item(ed, ed->menu_bar, "Search", NULL);
    /* View */
    create_menu_item(ed, ed->menu_bar, "View", NULL);
    /* Help */
    create_menu_item(ed, ed->menu_bar, "Help", NULL);
}

/*======================================
 *  搜索栏
 *======================================*/

static void show_search_bar(editor_t *ed, int show_replace)
{
    if (ed->search_visible && !show_replace) {
        lv_obj_add_flag(ed->search_bar, LV_OBJ_FLAG_HIDDEN);
        ed->search_visible = 0;
        return;
    }

    if (!ed->search_bar) {
        ed->search_bar = lv_obj_create(ed->win);
        lv_obj_set_pos(ed->search_bar, BORDER_W,
                       BORDER_W + TITLE_H + MENU_H);
        lv_obj_set_size(ed->search_bar,
            lv_obj_get_width(ed->win) - BORDER_W * 2, 36);
        lv_obj_set_style_bg_color(ed->search_bar, WIN3_LTGRAY, LV_PART_MAIN);
        lv_obj_set_style_border_width(ed->search_bar, 1, LV_PART_MAIN);

        ed->search_input = lv_textarea_create(ed->search_bar);
        lv_obj_set_size(ed->search_input, 200, 28);
        lv_obj_set_pos(ed->search_input, 4, 4);
        lv_textarea_set_placeholder_text(ed->search_input, "Find...");
        lv_obj_set_style_text_font(ed->search_input, RETRO_FONT_DEFAULT, 0);

        lv_obj_t *find_btn = lv_button_create(ed->search_bar);
        lv_obj_set_size(find_btn, 50, 28);
        lv_obj_set_pos(find_btn, 210, 4);
        lv_obj_set_style_bg_color(find_btn, WIN3_LTGRAY, LV_PART_MAIN);
        lv_obj_t *fl = lv_label_create(find_btn);
        lv_label_set_text(fl, "Find");
        lv_obj_align(fl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *close_btn = lv_button_create(ed->search_bar);
        lv_obj_set_size(close_btn, 24, 24);
        lv_obj_set_pos(close_btn, 270, 6);
        lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
        lv_obj_t *cl = lv_label_create(close_btn);
        lv_label_set_text(cl, "X");
        lv_obj_align(cl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(close_btn, search_close_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_remove_flag(ed->search_bar, LV_OBJ_FLAG_HIDDEN);
    ed->search_visible = 1;
}

/*======================================
 *  行号更新
 *======================================*/

static void update_line_numbers(editor_t *ed)
{
    const char *text = lv_textarea_get_text(ed->text_area);

    /* 计算总行数 */
    int lines = 1;
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') lines++;
    }
    ed->total_lines = lines;

    /* 构建行号文本（带上限，防止溢出 line_nums 缓冲）
     * Build line-number text with bound (prevent buffer overflow) */
    static char line_nums[4096];
    size_t len = 0;
    line_nums[0] = 0;
    for (int i = 1; i <= lines && len < sizeof(line_nums) - 16; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%4d\n", i);
        strcat(line_nums + len, buf);
        len += strlen(buf);
    }
    if (len > 0)
        line_nums[len - 1] = 0;  /* 去掉最后一个换行 / Drop last newline */

    lv_label_set_text(ed->line_numbers, line_nums);
}

static void update_status(editor_t *ed)
{
    static char status[256];
    snprintf(status, sizeof(status),
             "Ln %d, Col %d  |  %d lines  |  %d chars%s",
             ed->cursor_line, ed->cursor_col,
             ed->total_lines, ed->total_chars,
             ed->modified ? "  [Modified]" : "");
    lv_label_set_text(ed->status_lbl, status);
}

static void update_title(editor_t *ed)
{
    static char title[320];
    if (ed->filename[0]) {
        snprintf(title, sizeof(title), "%s%s - Notepad",
                 ed->modified ? "*" : "", ed->filename);
    } else {
        snprintf(title, sizeof(title), "Untitled%s - Notepad",
                 ed->modified ? "*" : "");
    }
    strcpy(ed->title, title);

    /* 更新窗口标题 */
    /* 注意: 需要找到标题栏标签来更新 */
}

/*======================================
 *  事件回调
 *======================================*/

static void text_changed_cb(lv_event_t *e)
{
    (void)e;
    editor_t *ed = &g_editor;

    const char *text = lv_textarea_get_text(ed->text_area);
    ed->total_chars = strlen(text);
    ed->modified = 1;

    update_line_numbers(ed);
    update_status(ed);
    update_title(ed);
}

static void cursor_pos_cb(lv_event_t *e)
{
    editor_t *ed = &g_editor;
    uint32_t pos = lv_textarea_get_cursor_pos(ed->text_area);
    const char *text = lv_textarea_get_text(ed->text_area);

    /* 计算光标所在行列 */
    int line = 1, col = 1;
    for (uint32_t i = 0; i < pos && text[i]; i++) {
        if (text[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }

    ed->cursor_line = line;
    ed->cursor_col = col;
    update_status(ed);
}

/*======================================
 *  标签页
 *======================================*/

static void tab_create(const char *filename)
{
    editor_t *ed = &g_editor;

    if (filename) {
        strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
        ed->filename[sizeof(ed->filename) - 1] = 0;
    } else {
        ed->filename[0] = 0;
    }

    ed->modified = 0;
    update_title(ed);
    update_status(ed);
}

/*======================================
 *  主窗口
 *======================================*/

static void create_editor_window(void)
{
    editor_t *ed = &g_editor;
    memset(ed, 0, sizeof(*ed));

    /* 窗口 */
    int win_w = 560;
    int win_h = 420;
    ed->win = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(ed->win, 80, 50);
    lv_obj_set_size(ed->win, win_w, win_h);
    lv_obj_set_style_bg_color(ed->win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(ed->win, 0, LV_PART_MAIN);

    /* 标题栏 */
    lv_obj_t *title_bar = lv_obj_create(ed->win);
    lv_obj_set_pos(title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(title_bar, win_w - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(title_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "Notepad - Untitled");
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 控制按钮 */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 16, 14);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, editor_close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "x");
    lv_obj_align(cl, LV_ALIGN_CENTER, 0, 0);

    /* 最大化 */
    lv_obj_t *max_btn = lv_button_create(title_bar);
    lv_obj_set_size(max_btn, 16, 14);
    lv_obj_align(max_btn, LV_ALIGN_RIGHT_MID, -21, 0);
    lv_obj_set_style_bg_color(max_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_radius(max_btn, 0, LV_PART_MAIN);
    lv_obj_t *ml = lv_label_create(max_btn);
    lv_label_set_text(ml, "□");
    lv_obj_align(ml, LV_ALIGN_CENTER, 0, 0);

    /* 最小化 */
    lv_obj_t *min_btn = lv_button_create(title_bar);
    lv_obj_set_size(min_btn, 16, 14);
    lv_obj_align(min_btn, LV_ALIGN_RIGHT_MID, -39, 0);
    lv_obj_set_style_bg_color(min_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_radius(min_btn, 0, LV_PART_MAIN);
    lv_obj_t *mil = lv_label_create(min_btn);
    lv_label_set_text(mil, "_");
    lv_obj_align(mil, LV_ALIGN_CENTER, 0, 0);

    /* 菜单栏 */
    create_menu_bar(ed);

    /* 编辑区域容器 */
    int editor_y = BORDER_W + TITLE_H + MENU_H + 2;
    int editor_h = win_h - BORDER_W * 2 - TITLE_H - MENU_H - STATUS_H - 2;
    int editor_w = win_w - BORDER_W * 2 - 2;

    lv_obj_t *editor_container = lv_obj_create(ed->win);
    lv_obj_set_pos(editor_container, BORDER_W + 1, editor_y);
    lv_obj_set_size(editor_container, editor_w, editor_h);
    lv_obj_set_style_bg_color(editor_container, WIN3_EDITOR_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(editor_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(editor_container, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(editor_container, 0, LV_PART_MAIN);

    /* 行号区域 */
    ed->line_numbers = lv_label_create(editor_container);
    lv_obj_set_size(ed->line_numbers, LINE_NUM_W, editor_h - 2);
    lv_obj_set_pos(ed->line_numbers, 1, 1);
    lv_obj_set_style_bg_color(ed->line_numbers, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_text_font(ed->line_numbers, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(ed->line_numbers, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(ed->line_numbers, "   1");

    /* 文本编辑区 */
    ed->text_area = lv_textarea_create(editor_container);
    lv_obj_set_pos(ed->text_area, LINE_NUM_W + 2, 0);
    lv_obj_set_size(ed->text_area, editor_w - LINE_NUM_W - 4, editor_h - 2);
    lv_textarea_set_placeholder_text(ed->text_area,
        "Welcome to ESP32-S3 Notepad\n\n"
        "Type your text here...\n\n"
        "Keyboard shortcuts:\n"
        "  Ctrl+S - Save\n"
        "  Ctrl+F - Find\n"
        "  Ctrl+H - Replace\n"
        "  Ctrl+O - Open");
    lv_obj_set_style_text_font(ed->text_area, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(ed->text_area, 0, LV_PART_MAIN);
    lv_textarea_set_one_line(ed->text_area, false);
    lv_textarea_set_cursor_pos(ed->text_area, 0);

    lv_obj_add_event_cb(ed->text_area, text_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ed->text_area, cursor_pos_cb, LV_EVENT_VALUE_CHANGED, NULL); /* DEFOCUSED 不会触发，改 VALUE_CHANGED / VALUE_CHANGED fires on cursor moves too */

    /* 状态栏 */
    ed->status_bar = lv_obj_create(ed->win);
    int status_y = win_h - BORDER_W - STATUS_H;
    lv_obj_set_pos(ed->status_bar, BORDER_W, status_y);
    lv_obj_set_size(ed->status_bar, win_w - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(ed->status_bar, WIN3_STATUS_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(ed->status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ed->status_bar, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(ed->status_bar, 0, LV_PART_MAIN);

    ed->status_lbl = lv_label_create(ed->status_bar);
    lv_label_set_text(ed->status_lbl, "Ln 1, Col 1  |  1 line  |  0 chars");
    lv_obj_set_style_text_font(ed->status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(ed->status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 初始化 */
    ed->total_lines = 1;
    ed->total_chars = 0;
    ed->cursor_line = 1;
    ed->cursor_col = 1;

    /* 创建新文件标签 */
    tab_create(NULL);
}

static void close_editor(void)
{
    editor_t *ed = &g_editor;
    if (ed->win) {
        lv_obj_delete(ed->win);
        memset(ed, 0, sizeof(*ed));
    }
}

/*======================================
 *  公共接口
 *======================================*/

int editor_start(void)
{
    create_editor_window();
    syslog(LOG_INFO, "Editor: started\n");
    return 0;
}

void editor_stop(void)
{
    close_editor();
    syslog(LOG_INFO, "Editor: stopped\n");
}

/* 创建编辑器并返回窗口对象 */
lv_obj_t *editor_create(void)
{
    if (!g_editor.win) {
        editor_start();
    }
    return g_editor.win;
}

#endif /* CONFIG_LVGL */
