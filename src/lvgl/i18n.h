/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * i18n.h - 多语种框架头文件
 *
 * WHAT : 多语种框架头文件
 * WHY  : i18n 对外契约（i18n_get/i18n_getf/语言查询）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/i18n.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : I18N() 宏 + 函数原型
 */

#ifndef LVGL_I18N_H
#define LVGL_I18N_H

#ifdef CONFIG_LVGL

#include <stddef.h>

/*======================================
 *  公共接口 / Public API
 *======================================*/

/* 初始化 i18n 系统 / Initialize i18n system */
void i18n_init(void);

/* 设置/获取当前语言 / Set/get current language */
void i18n_set_lang(const char *lang);
const char *i18n_get_lang(void);

/* 获取字符串 / Get string by key */
const char *i18n_get(const char *key);

/* 获取字符串（格式化）/ Get string with format */
void i18n_getf(const char *key, char *buf, size_t bufsiz, ...);

/* 检查是否中文模式 / Check if Chinese mode */
int i18n_is_chinese(void);

/* 快捷宏 / Quick macros */
#define I18N(key) i18n_get(key)

/*======================================
 *  字符串键名 / String Keys
 *======================================*/

/* 桌面 / Desktop */
#define I18N_DESKTOP_TITLE       "DESKTOP_TITLE"
#define I18N_MY_COMPUTER         "MY_COMPUTER"
#define I18N_NETWORK_NEIGHBOR    "NETWORK_NEIGHBOR"
#define I18N_START               "START"

/* 窗口 / Window */
#define I18N_WIN_MINIMIZE        "WIN_MINIMIZE"
#define I18N_WIN_MAXIMIZE        "WIN_MAXIMIZE"
#define I18N_WIN_RESTORE         "WIN_RESTORE"
#define I18N_WIN_CLOSE           "WIN_CLOSE"

/* 通用 / Common */
#define I18N_OK                  "OK"
#define I18N_CANCEL              "CANCEL"
#define I18N_YES                 "YES"
#define I18N_NO                  "NO"
#define I18N_INPUT               "INPUT"
#define I18N_SELECT              "SELECT"
#define I18N_CONFIRM             "CONFIRM"
#define I18N_SAVE                "SAVE"
#define I18N_OPEN                "OPEN"
#define I18N_CLOSE               "CLOSE"
#define I18N_HELP                "HELP"
#define I18N_ABOUT               "ABOUT"
#define I18N_EXIT                "EXIT"
#define I18N_SETTINGS            "SETTINGS"

/* 菜单项 / Menu Items */
#define I18N_MENU_FILE           "MENU_FILE"
#define I18N_MENU_EDIT           "MENU_EDIT"
#define I18N_MENU_VIEW           "MENU_VIEW"
#define I18N_MENU_HELP           "MENU_HELP"
#define I18N_MENU_EXPORT         "MENU_EXPORT"
#define I18N_MENU_SEARCH         "MENU_SEARCH"

/* 开始菜单 / Start Menu */
#define I18N_START_MENU_PROGRAMS  "START_MENU_PROGRAMS"
#define I18N_START_MENU_ACCESSORIES "START_MENU_ACCESSORIES"
#define I18N_START_MENU_GAMES    "START_MENU_GAMES"
#define I18N_START_MENU_RUN      "START_MENU_RUN"
#define I18N_START_MENU_SHUTDOWN  "START_MENU_SHUTDOWN"

/* 程序组 / Program Groups */
#define I18N_PROG_MAIN           "PROG_MAIN"
#define I18N_PROG_ACCESSORIES   "PROG_ACCESSORIES"
#define I18N_PROG_GAMES         "PROG_GAMES"
#define I18N_PROG_STARTUP        "PROG_STARTUP"

/* 通用操作 / Common Actions */
#define I18N_ACTION_OPEN         "ACTION_OPEN"
#define I18N_ACTION_SAVE        "ACTION_SAVE"
#define I18N_ACTION_SAVE_AS      "ACTION_SAVE_AS"
#define I18N_ACTION_EXPORT       "ACTION_EXPORT"
#define I18N_ACTION_CLOSE        "ACTION_CLOSE"
#define I18N_ACTION_CANCEL       "ACTION_CANCEL"
#define I18N_ACTION_OK           "ACTION_OK"
#define I18N_ACTION_EXECUTE      "ACTION_EXECUTE"

/* 状态消息 / Status Messages */
#define I18N_STATUS_READY        "STATUS_READY"
#define I18N_STATUS_LOADING      "STATUS_LOADING"
#define I18N_STATUS_DONE         "STATUS_DONE"
#define I18N_STATUS_ERROR        "STATUS_ERROR"
#define I18N_EXIT                "EXIT"

/* 应用 / Applications */
#define I18N_APP_NOTEPAD         "APP_NOTEPAD"
#define I18N_APP_BROWSER         "APP_BROWSER"
#define I18N_APP_TERMINAL        "APP_TERMINAL"
#define I18N_APP_MINESWEEPER     "APP_MINESWEEPER"
#define I18N_APP_FILE_MANAGER    "APP_FILE_MANAGER"
#define I18N_APP_CONTROL_PANEL    "APP_CONTROL_PANEL"
#define I18N_APP_MEDIA_PLAYER     "APP_MEDIA_PLAYER"
#define I18N_APP_RECORDER         "APP_RECORDER"
#define I18N_APP_SQLITE           "APP_SQLITE"

/* SQLite Database Tool / SQLite 数据库工具 */
#define I18N_SQLITE_TITLE         "SQLite Browser"
#define I18N_SQLITE_DB_PATH       "Database:"
#define I18N_SQLITE_SQL           "SQL:"
#define I18N_SQLITE_HISTORY       "History:"
#define I18N_SQLITE_NO_HISTORY    "(No history)"
#define I18N_SQLITE_NO_RESULT     "(No result)"
#define I18N_SQLITE_READY         "Ready / 就绪"
#define I18N_SQLITE_CLEARED       "Cleared / 已清除"
#define I18N_SQLITE_ERROR         "Error / 错误"
#define I18N_SQLITE_SUCCESS        "Success / 成功"
#define I18N_SQLITE_ROWS_RETURNED "Returned %d rows x %d cols / 返回 %d 行 x %d 列"
#define I18N_SQLITE_AFFECTED_ROWS "Affected %d rows / 影响 %d 行"
#define I18N_SQLITE_DB_OPENED     "Opened: %s / 已打开: %s"
#define I18N_SQLITE_NO_DB         "No database selected / 未选择数据库"
#define I18N_SQLITE_NO_SQL        "No SQL statement / 未输入 SQL"
#define I18N_SQLITE_NO_PATH       "No database path / 未输入路径"
#define I18N_SQLITE_NO_RESULT2    "No result to export / 无结果可导出"
#define I18N_SQLITE_ERR_NO_DB     "Error: No database / 错误: 未打开数据库"
#define I18N_SQLITE_ERR_NO_SQL    "Error: No SQL / 错误: 未输入 SQL"
#define I18N_SQLITE_ERR_NO_PATH   "Error: No path / 错误: 未输入路径"
#define I18N_SQLITE_ERR_OPEN      "Error: Cannot open database / 错误: 无法打开数据库"
#define I18N_SQLITE_ERR_SQL       "SQL Error: %s"
#define I18N_SQLITE_ERR_SQL_FMT   "SQL Error: %s / SQL 错误: %s"
#define I18N_SQLITE_ERR_OPEN_FMT  "Open Error: %s / 打开错误: %s"
#define I18N_SQLITE_ERR_EXPORT    "Export failed / 导出失败"
#define I18N_SQLITE_MODIFY_OK     "(Query executed / 语句已执行)"
#define I18N_SQLITE_EXPORTING_CSV "Exporting to CSV: %s / 正在导出 CSV: %s"
#define I18N_SQLITE_CSV_EXPORTED  "CSV exported: %s / CSV 已导出: %s"
#define I18N_SQLITE_EXPORTING_DB  "Exporting to SQLite DB: %s / 正在导出数据库: %s"
#define I18N_SQLITE_DB_EXPORTED   "SQLite DB exported: %s / 数据库已导出: %s"
#define I18N_SQLITE_EXPORT_FAILED "Export failed / 导出失败"
#define I18N_SQLITE_OPEN_FAILED   "Cannot open: %s / 无法打开: %s"

/* Menu / 菜单 */
#define I18N_MENU_FILE            "MENU_FILE"
#define I18N_MENU_EDIT          "MENU_EDIT"
#define I18N_MENU_VIEW          "MENU_VIEW"
#define I18N_MENU_HELP          "MENU_HELP"
#define I18N_MENU_EXPORT          "MENU_EXPORT"
#define I18N_BTN_OPEN             "Open"
#define I18N_BTN_EXECUTE          "Execute"
#define I18N_BTN_CLEAR            "Clear"
#define I18N_BTN_SAVE             "Save"
#define I18N_BTN_CANCEL           "Cancel"
#define I18N_BTN_OK               "OK"

#endif /* CONFIG_LVGL */

#endif /* LVGL_I18N_H */
