/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_sqlite.c - Win3.2 SQLite 图形工具
 *
 * WHAT : Win3.2 SQLite 图形工具
 * WHY  : 数据库查询/结果表格/CSV 导出
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/app_sqlite.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : drv_sqlite.c 封装 SQLite C API，结果渲染为 LVGL 表格
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"
#include "i18n.h"

/*======================================
 *  Windows 3.2 颜色定义 / Win 3.2 Colors
 *======================================*/
#define WIN3_BG         lv_color_hex(0xC0C0C0)
#define WIN3_WHITE      lv_color_hex(0xFFFFFF)
#define WIN3_BLACK      lv_color_hex(0x000000)
#define WIN3_BLUE       lv_color_hex(0x000080)
#define WIN3_GRAY       lv_color_hex(0x808080)
#define WIN3_LTGRAY     lv_color_hex(0xC0C0C0)
#define WIN3_BORDER_HI  lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_LO  lv_color_hex(0x808080)
#define WIN3_TITLE_BG   lv_color_hex(0x000080)
#define WIN3_TITLE_FG   lv_color_hex(0xFFFFFF)
#define WIN3_MENU_BG    lv_color_hex(0xC0C0C0)
#define WIN3_STATUS_BG  lv_color_hex(0xC0C0C0)
#define WIN3_TOOLBAR_BG lv_color_hex(0xC0C0C0)
#define WIN3_CONTENT_BG lv_color_hex(0xFFFFFF)
#define WIN3_GRID_LINE  lv_color_hex(0x808080)
#define WIN3_GRID_HDR  lv_color_hex(0xD0D0D0)

#define BORDER_W        2
#define TITLE_H         18
#define MENU_H          20
#define STATUS_H        22
#define TOOLBAR_H       32
#define MAX_COLS        16          /* 最大显示列数 / Max displayed columns */
#define MAX_ROWS        1000        /* 最大显示行数 / Max displayed rows */
#define MAX_COL_WIDTH   64          /* 最大列宽 / Max column width */
#define MAX_HISTORY     20          /* 最大历史记录 / Max history items */
#define MAX_ROW_DATA    4096        /* 行数据缓冲区 / Row data buffer */

/*======================================
 *  SQLite 可选支持 / Optional SQLite Support
 *======================================*/
#ifdef CONFIG_UTILS_SQLITE
#include <sqlite3.h>
#define HAVE_SQLITE 1
#else
#define HAVE_SQLITE 0
#endif

/*======================================
 *  SQLite 浏览器状态 / SQLite Browser State
 *======================================*/
typedef struct {
    lv_obj_t *win;                 /* 主窗口 / Main window */
    lv_obj_t *menu_bar;            /* 菜单栏 / Menu bar */
    lv_obj_t *toolbar;             /* 工具栏 / Toolbar */
    lv_obj_t *db_path_input;        /* 数据库路径输入 / DB path input */
    lv_obj_t *sql_input;            /* SQL 输入框 / SQL input textarea */
    lv_obj_t *result_table;         /* 结果表格 / Result table */
    lv_obj_t *status_bar;           /* 状态栏 / Status bar */
    lv_obj_t *status_lbl;           /* 状态标签 / Status label */
    lv_obj_t *history_dropdown;     /* 历史记录下拉 / History dropdown */
    lv_obj_t *msg_box;              /* 消息框 / Message box */

    /* 状态数据 / State data */
    char db_path[256];              /* 当前数据库路径 / Current DB path */
    char current_sql[1024];          /* 当前 SQL 语句 / Current SQL */
    char history[MAX_HISTORY][512];  /* 历史记录 / History */
    int history_count;              /* 历史记录数 / History count */
    int result_rows;                /* 结果行数 / Result row count */
    int result_cols;                /* 结果列数 / Result column count */
    int affected_rows;              /* 影响行数 / Affected rows */
    int has_result;                 /* 是否有结果集 / Has result set */
    int connected;                  /* 是否已连接 / Is connected */
} sqlite_gui_t;

static sqlite_gui_t g_sqlite_gui;

/*======================================
 *  内部函数声明 / Internal Function Declarations
 *======================================*/
static void create_sqlite_window(void);
static void create_menu_bar(sqlite_gui_t *sg);
static void create_toolbar(sqlite_gui_t *sg);
static void create_db_section(sqlite_gui_t *sg);
static void create_sql_section(sqlite_gui_t *sg);
static void create_result_section(sqlite_gui_t *sg);
static void create_status_bar(sqlite_gui_t *sg);

static void btn_open_db_cb(lv_event_t *e);
static void btn_execute_cb(lv_event_t *e);
static void btn_export_csv_cb(lv_event_t *e);
static void btn_export_sqlite_cb(lv_event_t *e);
static void btn_clear_cb(lv_event_t *e);
static void btn_history_sel_cb(lv_event_t *e);
static void win_close_cb(lv_event_t *e);
static void menu_file_open_cb(lv_event_t *e);
static void menu_file_close_cb(lv_event_t *e);
static void menu_export_csv_cb(lv_event_t *e);
static void menu_export_sqlite_cb(lv_event_t *e);
static void menu_help_about_cb(lv_event_t *e);

static int execute_sql(sqlite_gui_t *sg, const char *sql);
static int execute_select(sqlite_gui_t *sg, const char *sql);
static int execute_modify(sqlite_gui_t *sg, const char *sql);
static void update_result_table(sqlite_gui_t *sg, int cols, int rows);
static void add_history(sqlite_gui_t *sg, const char *sql);
static void set_status(sqlite_gui_t *sg, const char *msg);
static void show_error(sqlite_gui_t *sg, const char *msg);
static void show_info(sqlite_gui_t *sg, const char *msg);
static char *trim_string(char *s);
static void truncate_path(char *dest, const char *src, int maxlen);

/* 导出函数 / Export functions (仅 SQLite 构建存在 / only with SQLite) */
#if HAVE_SQLITE
static int export_to_csv(void *db, const char *sql, const char *out_path);
static int export_to_sqlite(void *src_db, const char *sql, const char *out_path);
#endif

/* drv_sqlite.c 公共导出接口 / Public export API from drv_sqlite.c */
extern int sqlite_export_db(void *src_db, const char *query, const char *out_path);

/*======================================
 *  公共接口 / Public API
 *======================================*/

/**
 * sqlite_gui_start - 启动 SQLite GUI / Start SQLite GUI
 * return: 0 成功 / 0 success
 */
int sqlite_gui_start(void)
{
    if (g_sqlite_gui.win != NULL) {
        /* 窗口已存在，激活并前置 / Window exists, activate and bring to front
         * move_to_index(0xFFFF) 在 9.5 为 no-op，改用最后子索引
         * move_to_index(0xFFFF) is a no-op in 9.5; use last child index */
        lv_obj_move_to_index(g_sqlite_gui.win,
            (int32_t)lv_obj_get_child_count(lv_obj_get_parent(g_sqlite_gui.win)) - 1);
        return 0;
    }
    create_sqlite_window();
    syslog(LOG_INFO, "SQLite GUI: started\n");
    return 0;
}

/**
 * sqlite_gui_stop - 停止 SQLite GUI / Stop SQLite GUI
 */
void sqlite_gui_stop(void)
{
    if (g_sqlite_gui.win != NULL) {
        lv_obj_delete(g_sqlite_gui.win);
        g_sqlite_gui.win = NULL;
    }
    syslog(LOG_INFO, "SQLite GUI: stopped\n");
}

/**
 * sqlite_gui_create - 创建并返回窗口对象 / Create and return window object
 * return: 窗口对象指针 / Window object pointer
 */
lv_obj_t *sqlite_gui_create(void)
{
    if (g_sqlite_gui.win == NULL) {
        sqlite_gui_start();
    }
    return g_sqlite_gui.win;
}

/*======================================
 *  窗口创建 / Window Creation
 *======================================*/

static void create_sqlite_window(void)
{
    sqlite_gui_t *sg = &g_sqlite_gui;
    memset(sg, 0, sizeof(*sg));

    /* 创建主窗口 (480x400) / Create main window */
    sg->win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(sg->win, 480, 420);
    lv_obj_set_pos(sg->win, 40, 30);
    lv_obj_set_style_bg_color(sg->win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(sg->win, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->win, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(sg->win, 0, LV_PART_MAIN);

    /* 标题栏 / Title bar */
    lv_obj_t *title_bar = lv_obj_create(sg->win);
    lv_obj_set_size(title_bar, 480 - BORDER_W * 2, TITLE_H);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, i18n_get("SQLITE_TITLE"));
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, TITLE_H - 4, TITLE_H - 4);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(close_btn, WIN3_BLACK, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "×");
    lv_obj_set_style_text_color(close_lbl, WIN3_BLACK, LV_PART_MAIN);
    lv_obj_set_style_text_font(close_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_add_event_cb(close_btn, win_close_cb, LV_EVENT_CLICKED, NULL);

    /* 菜单栏 / Menu bar */
    create_menu_bar(sg);

    /* 工具栏 / Toolbar */
    create_toolbar(sg);

    /* 数据库选择区 / DB selection section */
    create_db_section(sg);

    /* SQL 输入区 / SQL input section */
    create_sql_section(sg);

    /* 结果显示区 / Result section */
    create_result_section(sg);

    /* 状态栏 / Status bar */
    create_status_bar(sg);

    /* 窗口关闭事件 / Window close event */
    lv_obj_add_event_cb(sg->win, win_close_cb, LV_EVENT_DELETE, NULL);
}

/*======================================
 *  菜单栏 / Menu Bar
 *======================================*/

static void create_menu_bar(sqlite_gui_t *sg)
{
    /* 菜单栏背景 / Menu bar background */
    lv_obj_t *menu_bg = lv_obj_create(sg->win);
    lv_obj_set_size(menu_bg, 480 - BORDER_W * 2, MENU_H);
    lv_obj_set_pos(menu_bg, BORDER_W, TITLE_H);
    lv_obj_set_style_bg_color(menu_bg, WIN3_MENU_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(menu_bg, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_bg, LV_SCROLLBAR_MODE_OFF);

    /* File 菜单 / File menu */
    lv_obj_t *btn_file = lv_button_create(menu_bg);
    lv_obj_set_size(btn_file, 40, MENU_H - 4);
    lv_obj_set_pos(btn_file, 0, 2);
    lv_obj_set_style_bg_color(btn_file, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_file, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_file, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_file, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_file), i18n_get("MENU_FILE"));
    lv_obj_add_event_cb(btn_file, menu_file_open_cb, LV_EVENT_CLICKED, NULL);

    /* Export 菜单 / Export menu */
    lv_obj_t *btn_export = lv_button_create(menu_bg);
    lv_obj_set_size(btn_export, 50, MENU_H - 4);
    lv_obj_set_pos(btn_export, 44, 2);
    lv_obj_set_style_bg_color(btn_export, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_export, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_export, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_export, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_export), i18n_get("MENU_EXPORT"));
    lv_obj_add_event_cb(btn_export, menu_export_csv_cb, LV_EVENT_CLICKED, NULL);

    /* Help 菜单 / Help menu */
    lv_obj_t *btn_help = lv_button_create(menu_bg);
    lv_obj_set_size(btn_help, 40, MENU_H - 4);
    lv_obj_set_pos(btn_help, 98, 2);
    lv_obj_set_style_bg_color(btn_help, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_help, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_help, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_help, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_help), i18n_get("MENU_HELP"));
    lv_obj_add_event_cb(btn_help, menu_help_about_cb, LV_EVENT_CLICKED, NULL);
}

/*======================================
 *  工具栏 / Toolbar
 *======================================*/

static void create_toolbar(sqlite_gui_t *sg)
{
    int y_pos = TITLE_H + MENU_H;

    /* 工具栏背景 / Toolbar background */
    lv_obj_t *toolbar = lv_obj_create(sg->win);
    lv_obj_set_size(toolbar, 480 - BORDER_W * 2, TOOLBAR_H);
    lv_obj_set_pos(toolbar, BORDER_W, y_pos);
    lv_obj_set_style_bg_color(toolbar, WIN3_TOOLBAR_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(toolbar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(toolbar, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(toolbar, LV_SCROLLBAR_MODE_OFF);

    /* 历史记录下拉 / History dropdown */
    lv_obj_t *hist_lbl = lv_label_create(toolbar);
    lv_label_set_text(hist_lbl, i18n_get("SQLITE_HISTORY"));
    lv_obj_set_style_text_font(hist_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(hist_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    sg->history_dropdown = lv_dropdown_create(toolbar);
    lv_obj_set_size(sg->history_dropdown, 140, 24);
    lv_obj_align(sg->history_dropdown, LV_ALIGN_LEFT_MID, 70, 0);
    lv_dropdown_set_options(sg->history_dropdown, i18n_get("SQLITE_NO_HISTORY"));
    lv_obj_set_style_bg_color(sg->history_dropdown, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->history_dropdown, WIN3_GRAY, LV_PART_MAIN);
    lv_obj_add_event_cb(sg->history_dropdown, btn_history_sel_cb, LV_EVENT_VALUE_CHANGED, sg);

    /* 执行按钮 / Execute button */
    lv_obj_t *btn_exec = lv_button_create(toolbar);
    lv_obj_set_size(btn_exec, 60, 24);
    lv_obj_align(btn_exec, LV_ALIGN_RIGHT_MID, -180, 0);
    lv_obj_set_style_bg_color(btn_exec, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_exec, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_exec, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_exec, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_exec), i18n_get("BTN_EXECUTE"));
    lv_obj_add_event_cb(btn_exec, btn_execute_cb, LV_EVENT_CLICKED, sg);

    /* 导出 CSV 按钮 / Export CSV button */
    lv_obj_t *btn_csv = lv_button_create(toolbar);
    lv_obj_set_size(btn_csv, 70, 24);
    lv_obj_align(btn_csv, LV_ALIGN_RIGHT_MID, -112, 0);
    lv_obj_set_style_bg_color(btn_csv, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_csv, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_csv, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_csv, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_csv), "CSV");
    lv_obj_add_event_cb(btn_csv, btn_export_csv_cb, LV_EVENT_CLICKED, sg);

    /* 导出 SQLite 按钮 / Export SQLite button */
    lv_obj_t *btn_sq = lv_button_create(toolbar);
    lv_obj_set_size(btn_sq, 70, 24);
    lv_obj_align(btn_sq, LV_ALIGN_RIGHT_MID, -36, 0);
    lv_obj_set_style_bg_color(btn_sq, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_sq, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_sq, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_sq, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_sq), "SQLite");
    lv_obj_add_event_cb(btn_sq, btn_export_sqlite_cb, LV_EVENT_CLICKED, sg);
}

/*======================================
 *  数据库选择区 / Database Selection Section
 *======================================*/

static void create_db_section(sqlite_gui_t *sg)
{
    int y_pos = TITLE_H + MENU_H + TOOLBAR_H;

    /* DB 标签 / DB label */
    lv_obj_t *db_lbl = lv_label_create(sg->win);
    lv_label_set_text(db_lbl, i18n_get("SQLITE_DB_PATH"));
    lv_obj_set_style_text_font(db_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_pos(db_lbl, BORDER_W + 4, y_pos + 2);

    /* DB 路径输入框 / DB path input */
    sg->db_path_input = lv_textarea_create(sg->win);
    lv_obj_set_size(sg->db_path_input, 340, 24);
    lv_obj_set_pos(sg->db_path_input, BORDER_W + 4, y_pos + 18);
    lv_textarea_set_placeholder_text(sg->db_path_input, "/mnt/sd0/data.db");
    lv_textarea_set_max_length(sg->db_path_input, sizeof(sg->db_path) - 1);
    lv_obj_set_style_bg_color(sg->db_path_input, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->db_path_input, WIN3_GRAY, LV_PART_MAIN);
    lv_textarea_set_one_line(sg->db_path_input, true);

    /* 打开按钮 / Open button */
    lv_obj_t *btn_open = lv_button_create(sg->win);
    lv_obj_set_size(btn_open, 60, 24);
    lv_obj_set_pos(btn_open, 480 - BORDER_W * 2 - 64, y_pos + 18);
    lv_obj_set_style_bg_color(btn_open, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_open, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_open, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_open, 0, LV_PART_MAIN);
    lv_label_set_text(lv_label_create(btn_open), i18n_get("BTN_OPEN"));
    lv_obj_add_event_cb(btn_open, btn_open_db_cb, LV_EVENT_CLICKED, sg);
}

/*======================================
 *  SQL 输入区 / SQL Input Section
 *======================================*/

static void create_sql_section(sqlite_gui_t *sg)
{
    int y_pos = TITLE_H + MENU_H + TOOLBAR_H + 48;

    /* SQL 标签 / SQL label */
    lv_obj_t *sql_lbl = lv_label_create(sg->win);
    lv_label_set_text(sql_lbl, "SQL:");
    lv_obj_set_style_text_font(sql_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_pos(sql_lbl, BORDER_W + 4, y_pos + 2);

    /* SQL 输入框 (多行) / SQL input (multi-line) */
    sg->sql_input = lv_textarea_create(sg->win);
    lv_obj_set_size(sg->sql_input, 456, 60);
    lv_obj_set_pos(sg->sql_input, BORDER_W + 4, y_pos + 18);
    lv_textarea_set_placeholder_text(sg->sql_input, "SELECT * FROM table_name\nWHERE id = 1;");
    lv_textarea_set_max_length(sg->sql_input, sizeof(sg->current_sql) - 1);
    lv_obj_set_style_bg_color(sg->sql_input, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->sql_input, WIN3_GRAY, LV_PART_MAIN);
    lv_textarea_set_accepted_chars(sg->sql_input, NULL);
}

/*======================================
 *  结果显示区 / Result Display Section
 *======================================*/

static void create_result_section(sqlite_gui_t *sg)
{
    int y_pos = TITLE_H + MENU_H + TOOLBAR_H + 48 + 66;

    /* 结果表格 / Result table */
    sg->result_table = lv_table_create(sg->win);
    lv_obj_set_size(sg->result_table, 456, 120);
    lv_obj_set_pos(sg->result_table, BORDER_W + 4, y_pos);
    lv_obj_set_style_bg_color(sg->result_table, WIN3_CONTENT_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->result_table, WIN3_GRAY, LV_PART_MAIN);
    lv_obj_set_style_text_font(sg->result_table, RETRO_FONT_DEFAULT, LV_PART_MAIN);

    /* 设置表格初始样式 / Set initial table styles */
    lv_table_set_column_width(sg->result_table, 0, 80);
    lv_table_set_row_count(sg->result_table, 1);
    lv_table_set_column_count(sg->result_table, 1);
    lv_table_set_cell_value(sg->result_table, 0, 0, i18n_get("SQLITE_NO_RESULT"));
}

/*======================================
 *  状态栏 / Status Bar
 *======================================*/

static void create_status_bar(sqlite_gui_t *sg)
{
    /* 状态栏背景 / Status bar background */
    sg->status_bar = lv_obj_create(sg->win);
    lv_obj_set_size(sg->status_bar, 480 - BORDER_W * 2, STATUS_H);
    lv_obj_set_pos(sg->status_bar, BORDER_W, 420 - BORDER_W - STATUS_H);
    lv_obj_set_style_bg_color(sg->status_bar, WIN3_STATUS_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(sg->status_bar, BORDER_W, LV_PART_MAIN);
    lv_obj_set_style_border_color(sg->status_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(sg->status_bar, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(sg->status_bar, LV_SCROLLBAR_MODE_OFF);

    /* 状态标签 / Status label */
    sg->status_lbl = lv_label_create(sg->status_bar);
    lv_label_set_text(sg->status_lbl, i18n_get("SQLITE_READY"));
    lv_obj_set_style_text_font(sg->status_lbl, RETRO_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_align(sg->status_lbl, LV_ALIGN_LEFT_MID, 4, 0);
}

/*======================================
 *  按钮回调 / Button Callbacks
 *======================================*/

/**
 * btn_open_db_cb - 打开数据库按钮回调 / Open DB button callback
 */
static void btn_open_db_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    const char *path = lv_textarea_get_text(sg->db_path_input);

    if (path == NULL || strlen(path) == 0) {
        show_error(sg, i18n_get("SQLITE_ERR_NO_PATH"));
        return;
    }

    strncpy(sg->db_path, path, sizeof(sg->db_path) - 1);
    sg->db_path[sizeof(sg->db_path) - 1] = '\0';
    sg->connected = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_DB_OPENED"), path);
    set_status(sg, buf);
    syslog(LOG_INFO, "SQLite: opened %s\n", path);
}

/**
 * btn_execute_cb - 执行 SQL 按钮回调 / Execute SQL button callback
 */
static void btn_execute_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    const char *sql = lv_textarea_get_text(sg->sql_input);

    if (sql == NULL || strlen(sql) == 0) {
        show_error(sg, i18n_get("SQLITE_ERR_NO_SQL"));
        return;
    }

    /* 保存到历史记录 / Save to history */
    add_history(sg, sql);

    /* 执行 SQL / Execute SQL */
    execute_sql(sg, sql);
}

/**
 * btn_export_csv_cb - 导出 CSV 按钮回调 / Export CSV button callback
 */
static void btn_export_csv_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    const char *sql = lv_textarea_get_text(sg->sql_input);

    (void)sql;  /* 无 SQLite 构建下不使用 / unused when SQLite disabled */

    if (!sg->has_result) {
        show_info(sg, i18n_get("SQLITE_ERR_NO_RESULT"));
        return;
    }

    /* 使用固定路径导出 / Export to fixed path */
    const char *out_path = "/mnt/sd0/export.csv";
    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_EXPORTING_CSV"), out_path);
    set_status(sg, buf);

#if HAVE_SQLITE
    sqlite3 *db = NULL;
    if (sg->db_path[0] && strlen(sg->db_path) > 0) {
        if (sqlite3_open(sg->db_path, &db) == SQLITE_OK) {
            int ret = export_to_csv(db, sql, out_path);
            sqlite3_close(db);
            if (ret == 0) {
                snprintf(buf, sizeof(buf), i18n_get("SQLITE_CSV_EXPORTED"), out_path);
                show_info(sg, buf);
            } else {
                show_error(sg, i18n_get("SQLITE_ERR_EXPORT"));
            }
        } else {
            show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        }
    } else {
        show_error(sg, i18n_get("SQLITE_ERR_NO_DB"));
    }
#else
    show_error(sg, "SQLite not available / SQLite 不可用");
#endif
}

/**
 * btn_export_sqlite_cb - 导出 SQLite 按钮回调 / Export SQLite button callback
 */
static void btn_export_sqlite_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    const char *sql = lv_textarea_get_text(sg->sql_input);

    (void)sql;  /* 无 SQLite 构建下不使用 / unused when SQLite disabled */

    if (!sg->has_result) {
        show_info(sg, i18n_get("SQLITE_ERR_NO_RESULT"));
        return;
    }

    const char *out_path = "/mnt/sd0/export.db";
    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_EXPORTING_DB"), out_path);
    set_status(sg, buf);

#if HAVE_SQLITE
    sqlite3 *src_db = NULL;
    if (sg->db_path[0] && strlen(sg->db_path) > 0) {
        if (sqlite3_open(sg->db_path, &src_db) == SQLITE_OK) {
            int ret = export_to_sqlite(src_db, sql, out_path);
            sqlite3_close(src_db);
            if (ret == 0) {
                snprintf(buf, sizeof(buf), i18n_get("SQLITE_DB_EXPORTED"), out_path);
                show_info(sg, buf);
            } else {
                show_error(sg, i18n_get("SQLITE_ERR_EXPORT"));
            }
        } else {
            show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        }
    } else {
        show_error(sg, i18n_get("SQLITE_ERR_NO_DB"));
    }
#else
    show_error(sg, "SQLite not available / SQLite 不可用");
#endif
}

/**
 * btn_clear_cb - 清除按钮回调 / Clear button callback
 */
static void btn_clear_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    lv_textarea_set_text(sg->sql_input, "");
    lv_table_set_row_count(sg->result_table, 1);
    lv_table_set_column_count(sg->result_table, 1);
    lv_table_set_cell_value(sg->result_table, 0, 0, i18n_get("SQLITE_NO_RESULT"));
    sg->has_result = 0;
    set_status(sg, i18n_get("SQLITE_CLEARED"));
}

/**
 * btn_history_sel_cb - 历史记录选择回调 / History selection callback
 */
static void btn_history_sel_cb(lv_event_t *e)
{
    sqlite_gui_t *sg = (sqlite_gui_t *)lv_event_get_user_data(e);
    lv_obj_t *dd = lv_event_get_target(e);

    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel > 0 && sel <= (uint32_t)sg->history_count) {
        lv_textarea_set_text(sg->sql_input, sg->history[sel - 1]);
    }
}

/**
 * win_close_cb - 窗口关闭回调 / Window close callback
 */
static void win_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_stop();
}

/*======================================
 *  菜单回调 / Menu Callbacks
 *======================================*/

static void menu_file_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_t *sg = &g_sqlite_gui;
    const char *path = lv_textarea_get_text(sg->db_path_input);

    if (path == NULL || strlen(path) == 0) {
        show_error(sg, i18n_get("SQLITE_ERR_NO_PATH"));
        return;
    }

    strncpy(sg->db_path, path, sizeof(sg->db_path) - 1);
    sg->db_path[sizeof(sg->db_path) - 1] = '\0';
    sg->connected = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_DB_OPENED"), path);
    set_status(sg, buf);
    syslog(LOG_INFO, "SQLite: opened %s\n", path);
}

static void menu_file_close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_stop();
}

static void menu_export_csv_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_t *sg = &g_sqlite_gui;
    if (!sg->has_result) {
        show_info(sg, i18n_get("SQLITE_NO_RESULT2"));
        return;
    }

    const char *out_path = "/mnt/sd0/export.csv";
    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_EXPORTING_CSV"), out_path);
    set_status(sg, buf);

#if HAVE_SQLITE
    sqlite3 *db = NULL;
    if (sg->db_path[0] && strlen(sg->db_path) > 0) {
        if (sqlite3_open(sg->db_path, &db) == SQLITE_OK) {
            const char *sql = lv_textarea_get_text(sg->sql_input);
            int ret = export_to_csv(db, sql ? sql : "SELECT * FROM sqlite_master", out_path);
            sqlite3_close(db);
            if (ret == 0) {
                snprintf(buf, sizeof(buf), i18n_get("SQLITE_CSV_EXPORTED"), out_path);
                show_info(sg, buf);
            } else {
                show_error(sg, i18n_get("SQLITE_ERR_EXPORT"));
            }
        } else {
            show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        }
    } else {
        show_error(sg, i18n_get("SQLITE_ERR_NO_DB"));
    }
#else
    show_error(sg, "SQLite not available / SQLite 不可用");
#endif
}

static void menu_export_sqlite_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_t *sg = &g_sqlite_gui;
    if (!sg->has_result) {
        show_info(sg, i18n_get("SQLITE_NO_RESULT2"));
        return;
    }

    const char *out_path = "/mnt/sd0/export.db";
    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_EXPORTING_DB"), out_path);
    set_status(sg, buf);

#if HAVE_SQLITE
    sqlite3 *src_db = NULL;
    if (sg->db_path[0] && strlen(sg->db_path) > 0) {
        if (sqlite3_open(sg->db_path, &src_db) == SQLITE_OK) {
            const char *sql = lv_textarea_get_text(sg->sql_input);
            int ret = sqlite_export_db(src_db, sql ? sql : "SELECT * FROM sqlite_master", out_path);
            sqlite3_close(src_db);
            if (ret == 0) {
                snprintf(buf, sizeof(buf), i18n_get("SQLITE_DB_EXPORTED"), out_path);
                show_info(sg, buf);
            } else {
                show_error(sg, i18n_get("SQLITE_ERR_EXPORT"));
            }
        } else {
            show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        }
    } else {
        show_error(sg, i18n_get("SQLITE_ERR_NO_DB"));
    }
#else
    show_error(sg, "SQLite not available / SQLite 不可用");
#endif
}

static void menu_help_about_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sqlite_gui_t *sg = &g_sqlite_gui;
    show_info(sg, "SQLite Browser v1.0\nWindows 3.2 Style\nESP32-S3 Retro WS");
}

/*======================================
 *  SQL 执行函数 / SQL Execution Functions
 *======================================*/

static int execute_sql(sqlite_gui_t *sg, const char *sql)
{
#if HAVE_SQLITE
    sqlite3 *db = NULL;
    int rc;

    if (sg->db_path[0] == '\0') {
        show_error(sg, i18n_get("SQLITE_ERR_NO_DB"));
        return -1;
    }

    rc = sqlite3_open(sg->db_path, &db);
    if (rc != SQLITE_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), i18n_get("SQLITE_ERR_OPEN_FMT"),
                 sqlite3_errmsg(db));
        show_error(sg, buf);
        return -1;
    }

    /* 去除首尾空白 / Trim whitespace */
    char *trimmed = trim_string((char *)sql);
    if (strncasecmp(trimmed, "SELECT", 6) == 0 ||
        strncasecmp(trimmed, "PRAGMA", 6) == 0 ||
        strncasecmp(trimmed, "EXPLAIN", 7) == 0) {
        rc = execute_select(sg, trimmed);
    } else {
        rc = execute_modify(sg, trimmed);
    }

    sqlite3_close(db);
    return rc;
#else
    show_error(sg, "SQLite not available / SQLite 不可用");
    return -1;
#endif
}

/**
 * execute_select - 执行 SELECT 查询 / Execute SELECT query
 */
static int execute_select(sqlite_gui_t *sg, const char *sql)
{
#if HAVE_SQLITE
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cols, rows;
    char col_names[MAX_COLS][64];
    char cell_buf[MAX_ROW_DATA];

    rc = sqlite3_open(sg->db_path, &db);
    if (rc != SQLITE_OK) {
        show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        return -1;
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), i18n_get("SQLITE_ERR_SQL_FMT"),
                 sqlite3_errmsg(db));
        show_error(sg, buf);
        sqlite3_close(db);
        return -1;
    }

    cols = sqlite3_column_count(stmt);

    /* PRAGMA 等语句无结果列，避免后续除零 / No columns (PRAGMA etc.): avoid div-by-zero */
    if (cols <= 0) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        sg->has_result = 0;
        set_status(sg, i18n_get("SQLITE_NO_RESULT"));
        return 0;
    }

    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 设置表格列数 / Set table column count */
    lv_table_set_column_count(sg->result_table, cols);

    /* 设置列宽 / Set column widths */
    int table_w = 456;
    int col_w = table_w / cols;
    if (col_w > MAX_COL_WIDTH * 8) col_w = MAX_COL_WIDTH * 8;
    for (int i = 0; i < cols; i++) {
        const char *name = (const char *)sqlite3_column_name(stmt, i);
        if (name) {
            strncpy(col_names[i], name, sizeof(col_names[i]) - 1);
            col_names[i][sizeof(col_names[i]) - 1] = '\0';
        } else {
            col_names[i][0] = '\0';
        }
        lv_table_set_column_width(sg->result_table, i, col_w);

        /* 表头 / Header row */
        lv_table_set_cell_value(sg->result_table, 0, i, col_names[i]);
    }

    /* 执行查询 / Execute query */
    rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        rows++;

        /* 确保有足够的行 / Ensure enough rows */
        if ((int)lv_table_get_row_count(sg->result_table) < rows + 1) {
            lv_table_set_row_count(sg->result_table, rows + 1);
        }

        /* 填充单元格 / Fill cells */
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            const char *val = NULL;

            if (type == SQLITE_INTEGER) {
                snprintf(cell_buf, sizeof(cell_buf), "%lld",
                        (long long)sqlite3_column_int64(stmt, i));
                val = cell_buf;
            } else if (type == SQLITE_FLOAT) {
                snprintf(cell_buf, sizeof(cell_buf), "%.6g",
                        sqlite3_column_double(stmt, i));
                val = cell_buf;
            } else if (type == SQLITE_NULL) {
                val = "NULL";
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                if (text) {
                    /* 截断过长的字符串 / Truncate too long strings */
                    strncpy(cell_buf, (const char *)text, MAX_COL_WIDTH);
                    cell_buf[MAX_COL_WIDTH] = '\0';
                    val = cell_buf;
                } else {
                    val = "";
                }
            }

            lv_table_set_cell_value(sg->result_table, rows, i, val ? val : "");
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* 更新状态 / Update status */
    sg->result_rows = rows;
    sg->result_cols = cols;
    sg->has_result = 1;

    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_ROWS_RETURNED"), rows, cols);
    set_status(sg, buf);
    syslog(LOG_INFO, "SQLite: SELECT returned %d rows x %d cols\n", rows, cols);

    return 0;
#else
    show_error(sg, "SQLite not available");
    return -1;
#endif
}

/**
 * execute_modify - 执行 INSERT/UPDATE/DELETE / Execute INSERT/UPDATE/DELETE
 */
static int execute_modify(sqlite_gui_t *sg, const char *sql)
{
#if HAVE_SQLITE
    sqlite3 *db = NULL;
    char *err_msg = NULL;
    int rc;

    rc = sqlite3_open(sg->db_path, &db);
    if (rc != SQLITE_OK) {
        show_error(sg, i18n_get("SQLITE_ERR_OPEN"));
        return -1;
    }

    rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), i18n_get("SQLITE_ERR_SQL_FMT"),
                 err_msg ? err_msg : "unknown error");
        show_error(sg, buf);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return -1;
    }

    sg->affected_rows = sqlite3_changes(db);
    sqlite3_close(db);

    /* 清空结果表 / Clear result table */
    lv_table_set_row_count(sg->result_table, 1);
    lv_table_set_column_count(sg->result_table, 1);
    lv_table_set_cell_value(sg->result_table, 0, 0,
                             i18n_get("SQLITE_MODIFY_OK"));

    char buf[128];
    snprintf(buf, sizeof(buf), i18n_get("SQLITE_AFFECTED_ROWS"),
             sg->affected_rows);
    set_status(sg, buf);
    syslog(LOG_INFO, "SQLite: %d rows affected\n", sg->affected_rows);

    return 0;
#else
    show_error(sg, "SQLite not available");
    return -1;
#endif
}

/*======================================
 *  导出功能 / Export Functions
 *======================================*/

#if HAVE_SQLITE
/**
 * export_to_csv - 导出查询结果到 CSV 文件 / Export query results to CSV file
 * @db: SQLite 数据库句柄 / SQLite database handle
 * @sql: SQL 查询语句 / SQL query
 * @out_path: 输出文件路径 / Output file path
 * return: 0 成功 / 0 success
 */
static int export_to_csv(void *db, const char *sql, const char *out_path)
{
    sqlite3 *sqldb = (sqlite3 *)db;
    sqlite3_stmt *stmt = NULL;
    FILE *fp;
    int rc;
    int cols;
    char buf[MAX_ROW_DATA];

    fp = fopen(out_path, "w");
    if (!fp) {
        syslog(LOG_ERR, "SQLite: failed to open %s for CSV export\n", out_path);
        return -1;
    }

    rc = sqlite3_prepare_v2(sqldb, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fclose(fp);
        return -1;
    }

    cols = sqlite3_column_count(stmt);
    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 写入表头 / Write header */
    for (int i = 0; i < cols; i++) {
        const char *name = (const char *)sqlite3_column_name(stmt, i);
        if (i > 0) fprintf(fp, ",");
        if (name) {
            fprintf(fp, "\"%s\"", name);
        }
    }
    fprintf(fp, "\n");

    /* 写入数据行 / Write data rows */
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        rows++;
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            if (i > 0) fprintf(fp, ",");

            if (type == SQLITE_INTEGER) {
                fprintf(fp, "%lld", (long long)sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                fprintf(fp, "%.6g", sqlite3_column_double(stmt, i));
            } else if (type == SQLITE_NULL) {
                fprintf(fp, "");
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                if (text) {
                    /* CSV 转义 / CSV escape */
                    const char *s = (const char *)text;
                    int need_quote = 0;
                    while (*s) {
                        if (*s == '"' || *s == ',' || *s == '\n') {
                            need_quote = 1;
                            break;
                        }
                        s++;
                    }
                    if (need_quote) fprintf(fp, "\"");
                    s = (const char *)text;
                    while (*s) {
                        if (*s == '"') fprintf(fp, "\"\"");
                        else fprintf(fp, "%c", *s);
                        s++;
                    }
                    if (need_quote) fprintf(fp, "\"");
                }
            }
        }
        fprintf(fp, "\n");
    }

    sqlite3_finalize(stmt);
    fclose(fp);
    syslog(LOG_INFO, "SQLite: exported %d rows to CSV: %s\n", rows, out_path);
    return 0;
}

/**
 * export_to_sqlite - 导出查询结果到新 SQLite 数据库 / Export to new SQLite database
 * @src_db: 源数据库句柄 / Source database handle
 * @sql: SQL 查询语句 / SQL query
 * @out_path: 输出文件路径 / Output file path
 * return: 0 成功 / 0 success
 */
static int export_to_sqlite(void *src_db, const char *sql, const char *out_path)
{
    sqlite3 *src = (sqlite3 *)src_db;
    sqlite3 *dst = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cols;
    char create_sql[MAX_ROW_DATA];
    char buf[MAX_ROW_DATA];

    /* 创建新数据库 / Create new database */
    rc = sqlite3_open(out_path, &dst);
    if (rc != SQLITE_OK) {
        syslog(LOG_ERR, "SQLite: failed to create export DB: %s\n", out_path);
        return -1;
    }

    /* 获取列信息 / Get column info */
    rc = sqlite3_prepare_v2(src, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_close(dst);
        return -1;
    }

    cols = sqlite3_column_count(stmt);
    if (cols > MAX_COLS) cols = MAX_COLS;

    /* 生成建表 SQL / Generate CREATE TABLE SQL */
    snprintf(create_sql, sizeof(create_sql), "CREATE TABLE results (");
    for (int i = 0; i < cols; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        if (i > 0) strncat(create_sql, ", ", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, "\"", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, name ? name : "col", sizeof(create_sql) - strlen(create_sql) - 1);
        strncat(create_sql, "\" TEXT", sizeof(create_sql) - strlen(create_sql) - 1);
    }
    strncat(create_sql, ")", sizeof(create_sql) - strlen(create_sql) - 1);

    rc = sqlite3_exec(dst, create_sql, NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        syslog(LOG_ERR, "SQLite: failed to create table: %s\n", sqlite3_errmsg(dst));
        sqlite3_finalize(stmt);
        sqlite3_close(dst);
        return -1;
    }

    /* 准备插入语句 / Prepare INSERT statement */
    char insert_sql[MAX_ROW_DATA];
    snprintf(insert_sql, sizeof(insert_sql), "INSERT INTO results VALUES (");
    for (int i = 0; i < cols; i++) {
        if (i > 0) strncat(insert_sql, ",?", sizeof(insert_sql) - strlen(insert_sql) - 1);
        else strncat(insert_sql, "?", sizeof(insert_sql) - strlen(insert_sql) - 1);
    }
    strncat(insert_sql, ")", sizeof(insert_sql) - strlen(insert_sql) - 1);

    sqlite3_stmt *ins_stmt = NULL;
    rc = sqlite3_prepare_v2(dst, insert_sql, -1, &ins_stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(dst);
        return -1;
    }

    /* 执行查询并插入 / Execute query and insert */
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && rows < MAX_ROWS) {
        sqlite3_reset(ins_stmt);
        for (int i = 0; i < cols; i++) {
            int type = sqlite3_column_type(stmt, i);
            if (type == SQLITE_NULL) {
                sqlite3_bind_null(ins_stmt, i + 1);
            } else if (type == SQLITE_INTEGER) {
                sqlite3_bind_int64(ins_stmt, i + 1, sqlite3_column_int64(stmt, i));
            } else if (type == SQLITE_FLOAT) {
                sqlite3_bind_double(ins_stmt, i + 1, sqlite3_column_double(stmt, i));
            } else {
                const unsigned char *text = sqlite3_column_text(stmt, i);
                sqlite3_bind_text(ins_stmt, i + 1, (const char *)text, -1, SQLITE_TRANSIENT);
            }
        }
        sqlite3_step(ins_stmt);
        rows++;
    }

    sqlite3_finalize(stmt);
    sqlite3_finalize(ins_stmt);
    sqlite3_close(dst);

    syslog(LOG_INFO, "SQLite: exported %d rows to SQLite DB: %s\n", rows, out_path);
    return 0;
}
#endif /* HAVE_SQLITE */

/*======================================
 *  历史记录 / History Management
 *======================================*/

static void add_history(sqlite_gui_t *sg, const char *sql)
{
    /* 跳过重复 / Skip duplicates */
    for (int i = 0; i < sg->history_count; i++) {
        if (strcmp(sg->history[i], sql) == 0) return;
    }

    /* 添加到历史 / Add to history */
    if (sg->history_count < MAX_HISTORY - 1) {
        strncpy(sg->history[sg->history_count], sql, sizeof(sg->history[0]) - 1);
        sg->history[sg->history_count][sizeof(sg->history[0]) - 1] = '\0';
        sg->history_count++;
    } else {
        /* 滚动历史 / Roll history */
        memmove(sg->history[0], sg->history[1], (MAX_HISTORY - 2) * sizeof(sg->history[0]));
        strncpy(sg->history[MAX_HISTORY - 2], sql, sizeof(sg->history[0]) - 1);
        sg->history[MAX_HISTORY - 2][sizeof(sg->history[0]) - 1] = '\0';
    }

    /* 更新下拉列表 / Update dropdown */
    if (sg->history_dropdown) {
        char options[MAX_HISTORY * 64];
        options[0] = '\0';
        for (int i = 0; i < sg->history_count; i++) {
            if (i > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
            /* 截断显示 / Truncate for display */
            const char *src = sg->history[i];
            char short_sql[64];
            truncate_path(short_sql, src, 60);
            strncat(options, short_sql, sizeof(options) - strlen(options) - 1);
        }
        if (options[0] == '\0') {
            strcpy(options, i18n_get("SQLITE_NO_HISTORY"));
        }
        lv_dropdown_set_options(sg->history_dropdown, options);
    }
}

/*======================================
 *  状态显示 / Status Display
 *======================================*/

static void set_status(sqlite_gui_t *sg, const char *msg)
{
    if (sg->status_lbl) {
        lv_label_set_text(sg->status_lbl, msg);
    }
}

/*
 * sqlite_msgbox_btn_cb - 消息框按钮回调 / Msgbox button callback
 * WHAT: 点击任意按钮关闭消息框
 * WHY  : 9.5 msgbox 用 builder API，关闭需自行处理
 */
static void sqlite_msgbox_btn_cb(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_target(e));
}

/*
 * show_msgbox - 构建 9.5 风格消息框 / Build LVGL 9.5 style msgbox
 * WHAT: 标题+正文+确定按钮
 * WHY  : lv_msgbox_create 在 9.5 只有 parent 参数
 * HOW  : add_title/add_text/add_footer_button，按钮点击即删除
 */
static lv_obj_t *show_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    if (m == NULL)
        return NULL;

    lv_msgbox_add_title(m, title);
    lv_msgbox_add_text(m, text);
    lv_obj_t *btn = lv_msgbox_add_footer_button(m, i18n_get("OK"));
    lv_obj_add_event_cb(btn, sqlite_msgbox_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(m, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_color(m, WIN3_GRAY, LV_PART_MAIN);
    lv_obj_center(m);
    return m;
}

static void show_error(sqlite_gui_t *sg, const char *msg)
{
    if (sg->msg_box) {
        lv_obj_delete(sg->msg_box);
        sg->msg_box = NULL;
    }

    syslog(LOG_ERR, "SQLite error: %s\n", msg);

    sg->msg_box = show_msgbox(i18n_get("SQLITE_ERROR"), msg);

    set_status(sg, msg);
}

static void show_info(sqlite_gui_t *sg, const char *msg)
{
    if (sg->msg_box) {
        lv_obj_delete(sg->msg_box);
        sg->msg_box = NULL;
    }

    sg->msg_box = show_msgbox("SQLite", msg);

    set_status(sg, msg);
}

/*======================================
 *  工具函数 / Utility Functions
 *======================================*/

/**
 * trim_string - 去除字符串首尾空白 / Trim leading/trailing whitespace
 * @s: 输入字符串 / Input string
 * return: 修剪后的字符串 / Trimmed string
 */
static char *trim_string(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';

    return s;
}

/**
 * truncate_path - 截断路径字符串用于显示 / Truncate path string for display
 * @dest: 目标缓冲区 / Destination buffer
 * @src: 源字符串 / Source string
 * @maxlen: 最大长度 / Maximum length
 */
static void truncate_path(char *dest, const char *src, int maxlen)
{
    int len = strlen(src);
    if (len <= maxlen) {
        strcpy(dest, src);
        return;
    }

    /* 保留前 maxlen-3 字符 + "..." / Keep first maxlen-3 chars + "..." */
    strncpy(dest, src, maxlen - 3);
    dest[maxlen - 3] = '\0';
    strcat(dest, "...");
}

/*======================================
 *  更新结果表格 / Update Result Table
 *======================================*/

static void update_result_table(sqlite_gui_t *sg, int cols, int rows)
{
    if (cols <= 0 || rows <= 0) {
        lv_table_set_row_count(sg->result_table, 1);
        lv_table_set_column_count(sg->result_table, 1);
        lv_table_set_cell_value(sg->result_table, 0, 0,
                                 i18n_get("SQLITE_NO_RESULT"));
        return;
    }

    if (cols > MAX_COLS) cols = MAX_COLS;
    lv_table_set_column_count(sg->result_table, cols);
    lv_table_set_row_count(sg->result_table, rows + 1);

    /* 设置列宽 / Set column widths */
    int table_w = 456;
    int col_w = table_w / cols;
    if (col_w > MAX_COL_WIDTH * 8) col_w = MAX_COL_WIDTH * 8;
    for (int i = 0; i < cols; i++) {
        lv_table_set_column_width(sg->result_table, i, col_w);
    }
}

#endif /* CONFIG_LVGL */
