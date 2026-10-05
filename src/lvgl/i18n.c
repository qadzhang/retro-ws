/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * i18n.c - 多语种框架实现
 *
 * WHAT : 多语种框架实现
 * WHY  : 中英双语界面（zh_CN 默认 / en_US）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/i18n.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 键值字符串表 + 当前语言切换，语言设置持久化到存储
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"

/*======================================
 *  语言配置 / Language Configuration
 *======================================*/

/* 默认语言 / Default language */
#ifndef CONFIG_LANG
#define CONFIG_LANG "zh_CN"
#endif

/* 语言配置存储路径 / Language config storage path */
#define I18N_CONFIG_PATH "/opt/etc/lang.conf"

/* 当前语言 / Current language
 * 初始化时从存储读取，运行时可切换
 * Initialized from storage at boot, can be changed at runtime */
static const char *g_current_lang = "zh_CN";  /* 默认值，启动时会被覆盖 / Default, will be overwritten at init */

/*======================================
 *  语言配置存储 / Language Config Storage
 *======================================*/

/**
 * i18n_load_from_storage - 从持久化存储加载语言设置
 * Load language setting from persistent storage
 *
 * 读取优先级 / Read priority (first found wins):
 * 1. /opt/etc/lang.conf (片上系统配置，无 SD 卡可用，2026-10-05 定稿)
 * 2. /mnt/sd0/lang.conf (SD卡) / SD card
 * 3. /mnt/spiffs0/lang.conf (SPIFFS) / SPIFFS partition
 * 3. /mnt/data/lang.conf (数据分区) / Data partition
 * 4. /flash/lang.conf (Flash) / Flash filesystem
 * 5. /etc/lang.conf (系统配置) / System config
 * 6. CONFIG_LANG (编译时默认) / Compile-time default
 */
static void i18n_load_from_storage(void)
{
    const char *paths[] = {
        "/opt/etc/lang.conf",      /* 片上系统配置（首选，无 SD 卡可用） */
        "/mnt/sd0/lang.conf",      /* SD卡 / SD card */
        "/mnt/spiffs0/lang.conf", /* SPIFFS 分区 / SPIFFS partition */
        "/mnt/data/lang.conf",    /* 数据分区 / Data partition */
        "/flash/lang.conf",       /* Flash / Flash filesystem */
        "/etc/lang.conf",          /* 系统配置目录 / System config dir */
        NULL
    };

    static char lang_buf[32] = {0};

    /* 按优先级尝试读取 / Try each path in priority order */
    for (int i = 0; paths[i] != NULL; i++) {
        FILE *fp = fopen(paths[i], "r");
        if (fp) {
            if (fgets(lang_buf, sizeof(lang_buf), fp)) {
                /* 去掉换行符 / Remove newline */
                lang_buf[strcspn(lang_buf, "\r\n")] = 0;
                if (strcmp(lang_buf, "zh_CN") == 0 || strcmp(lang_buf, "en_US") == 0) {
                    g_current_lang = lang_buf;
                    syslog(LOG_INFO, "[i18n] loaded from %s: %s\n", paths[i], g_current_lang);
                    fclose(fp);
                    return;
                }
            }
            fclose(fp);
        }
    }

    /* 使用编译时默认 / Fall back to compile-time default */
#ifdef CONFIG_LANG
    g_current_lang = CONFIG_LANG;
#else
    g_current_lang = "zh_CN";
#endif
    syslog(LOG_INFO, "[i18n] using default: %s\n", g_current_lang);
}

/**
 * i18n_save_to_storage - 保存语言设置到持久化存储
 * Save language setting to persistent storage
 *
 * @return: 0=成功, -1=失败
 */
/**
 * i18n_save_to_storage - 保存语言设置到持久化存储
 * Save language setting to persistent storage
 *
 * 尝试保存到多个位置，确保无论是否有 SD 卡都能工作
 * Try to save to multiple locations to ensure it works with or without SD card
 *
 * 保存位置 / Save locations (按优先级):
 * 1. /opt/etc/lang.conf (片上系统配置，无 SD 卡可用，2026-10-05 定稿)
 * 2. /mnt/sd0/lang.conf (SD卡) / SD card
 * 3. /mnt/spiffs0/lang.conf (SPIFFS) / SPIFFS partition
 * 3. /flash/lang.conf (Flash) / Flash filesystem
 * 4. /etc/lang.conf (系统配置) / System config (if writable)
 *
 * @return: 0=全部成功, -1=全部失败, 正数=部分成功
 */
static int i18n_save_to_storage(void)
{
    const char *paths[] = {
        "/opt/etc/lang.conf",      /* 片上系统配置（首选，无 SD 卡可用） */
        "/mnt/sd0/lang.conf",      /* SD卡 / SD card */
        "/mnt/spiffs0/lang.conf",  /* SPIFFS 分区 / SPIFFS partition */
        "/mnt/data/lang.conf",     /* 数据分区 / Data partition */
        "/flash/lang.conf",        /* Flash / Flash filesystem */
        "/etc/lang.conf",           /* 系统配置目录 / System config dir */
        NULL
    };

    mkdir("/opt/etc", 0755);   /* 片上配置目录（可能尚不存在） */

    int success_count = 0;

    for (int i = 0; paths[i] != NULL; i++) {
        FILE *fp = fopen(paths[i], "w");
        if (fp) {
            fprintf(fp, "%s\n", g_current_lang);
            fclose(fp);
            syslog(LOG_INFO, "[i18n] saved to: %s\n", paths[i]);
            success_count++;
        } else {
            syslog(LOG_DEBUG, "[i18n] cannot write to: %s (errno=%d)\n", paths[i], errno);
        }
    }

    if (success_count == 0) {
        syslog(LOG_ERR, "[i18n] failed to save lang config to any location\n");
        return -1;
    }

    syslog(LOG_INFO, "[i18n] saved to %d location(s): %s\n", success_count, g_current_lang);
    return success_count > 0 ? 0 : -1;
}

/**
 * i18n_init - 初始化 i18n 系统
 * Initialize i18n system
 *
 * 从持久化存储加载语言设置
 * Load language setting from persistent storage
 */
void i18n_init(void)
{
    i18n_load_from_storage();
    syslog(LOG_INFO, "[i18n] initialized with lang=%s\n", g_current_lang);
}

/*======================================
 *  字符串表结构 / String Table Structure
 *======================================*/

typedef struct {
    const char *key;     /* 字符串键名 / String key */
    const char *zh_CN;   /* 中文 / Chinese */
    const char *en_US;   /* 英文 / English */
} i18n_string_t;

/*======================================
 *  字符串定义 / String Definitions
 *======================================*/

/* 桌面字符串 / Desktop strings */
static const i18n_string_t g_desktop_strings[] = {
    /* 桌面 / Desktop */
    {"DESKTOP_TITLE",        "ESP32-S3 复古工作站",        "ESP32-S3 Retro Workstation"},
    {"MY_COMPUTER",          "我的电脑",                   "My Computer"},
    {"NETWORK_NEIGHBOR",     "网上邻居",                  "Network Neighbor"},
    {"START",                "开始",                       "Start"},

    /* 任务栏 / Taskbar */
    {"TASKBAR_READY",        "就绪",                       "Ready"},
    {"TASKBAR_OPEN",         "已打开",                     "Open"},

    /* 开始菜单 / Start Menu */
    {"MENU_PROGRAMS",        "程序",                       "Programs"},
    {"MENU_ACCESSORIES",     "附件",                       "Accessories"},
    {"MENU_GAMES",           "游戏",                       "Games"},
    {"MENU_FILE_MANAGER",    "文件管理器",                 "File Manager"},
    {"MENU_CONTROL_PANEL",   "控制面板",                   "Control Panel"},
    {"MENU_RUN",             "运行...",                    "Run..."},
    {"MENU_SHUTDOWN",        "关机",                       "Shut Down"},

    /* 窗口 / Window */
    {"WIN_MINIMIZE",         "最小化",                     "Minimize"},
    {"WIN_MAXIMIZE",         "最大化",                     "Maximize"},
    {"WIN_RESTORE",          "还原",                       "Restore"},
    {"WIN_CLOSE",            "关闭",                       "Close"},

    /* 菜单 / Menu */
    {"MENU_FILE",            "文件",                       "File"},
    {"MENU_EDIT",            "编辑",                       "Edit"},
    {"MENU_VIEW",            "查看",                       "View"},
    {"MENU_HELP",            "帮助",                       "Help"},
    {"MENU_SEARCH",          "搜索",                       "Search"},

    /* 通用 / Common */
    {"OK",                   "确定",                       "OK"},
    {"CANCEL",               "取消",                       "Cancel"},
    {"YES",                  "是",                         "Yes"},
    {"NO",                   "否",                         "No"},
    {"INPUT",                "输入",                       "Input"},
    {"SELECT",               "选择",                       "Select"},
    {"CONFIRM",              "确认",                       "Confirm"},
    {"SAVE",                 "保存",                       "Save"},
    {"OPEN",                 "打开",                       "Open"},
    {"CLOSE",                "关闭",                       "Close"},
    {"HELP",                 "帮助",                       "Help"},
    {"ABOUT",                "关于",                       "About"},
    {"SETTINGS",             "设置",                       "Settings"},
    {"EXIT",                 "退出",                       "Exit"},

    /* 菜单项 / Menu Items */
    {"MENU_FILE",            "文件",                       "File"},
    {"MENU_EDIT",            "编辑",                       "Edit"},
    {"MENU_VIEW",            "查看",                       "View"},
    {"MENU_HELP",            "帮助",                       "Help"},
    {"MENU_EXPORT",          "导出",                       "Export"},
    {"MENU_SEARCH",          "搜索",                       "Search"},

    /* 开始菜单 / Start Menu */
    {"START_MENU_PROGRAMS",   "程序",                       "Programs"},
    {"START_MENU_ACCESSORIES","附件",                       "Accessories"},
    {"START_MENU_GAMES",     "游戏",                       "Games"},
    {"START_MENU_RUN",       "运行...",                    "Run..."},
    {"START_MENU_SHUTDOWN",   "关机",                       "Shut Down"},

    /* 程序组 / Program Groups */
    {"PROG_MAIN",            "主群组",                     "Main"},
    {"PROG_ACCESSORIES",     "附件",                       "Accessories"},
    {"PROG_GAMES",           "游戏",                       "Games"},
    {"PROG_STARTUP",         "启动",                       "Startup"},

    /* 应用名称 / Application Names */
    {"APP_NOTEPAD",          "记事本",                     "Notepad"},
    {"APP_BROWSER",          "浏览器",                     "Browser"},
    {"APP_TERMINAL",         "终端",                       "Terminal"},
    {"APP_SQLITE",           "SQLite 浏览器",               "SQLite Browser"},
    {"APP_MINESWEEPER",     "扫雷",                       "Minesweeper"},
    {"APP_MEDIA_PLAYER",     "媒体播放器",                 "Media Player"},
    {"APP_RECORDER",         "录音机",                     "Recorder"},
    {"APP_FILE_MANAGER",     "文件管理器",                 "File Manager"},
    {"APP_CONTROL_PANEL",    "控制面板",                   "Control Panel"},

    /* 通用操作 / Common Actions */
    {"ACTION_OPEN",           "打开",                       "Open"},
    {"ACTION_SAVE",          "保存",                       "Save"},
    {"ACTION_SAVE_AS",       "另存为...",                  "Save As..."},
    {"ACTION_EXPORT",         "导出",                       "Export"},
    {"ACTION_CLOSE",         "关闭",                       "Close"},
    {"ACTION_CANCEL",         "取消",                       "Cancel"},
    {"ACTION_OK",             "确定",                       "OK"},
    {"ACTION_EXECUTE",        "执行",                       "Execute"},

    /* 状态消息 / Status Messages */
    {"STATUS_READY",         "就绪",                       "Ready"},
    {"STATUS_LOADING",        "加载中...",                  "Loading..."},
    {"STATUS_DONE",          "完成",                       "Done"},
    {"STATUS_ERROR",          "错误",                       "Error"},
    {"SEARCH",               "搜索",                       "Search"},
    {"REPLACE",              "替换",                       "Replace"},
    {"FIND",                 "查找",                       "Find"},
    {"CUT",                  "剪切",                       "Cut"},
    {"COPY",                 "复制",                       "Copy"},
    {"PASTE",                "粘贴",                       "Paste"},
    {"UNDO",                 "撤销",                       "Undo"},
    {"REDO",                 "重做",                       "Redo"},
    {"SELECT_ALL",           "全选",                       "Select All"},
    {"NEW",                  "新建",                       "New"},
    {"NEW_FILE",             "新建文件",                   "New File"},
    {"SAVE_AS",              "另存为",                     "Save As"},
    {"PRINT",                "打印",                       "Print"},

    /* 状态 / Status */
    {"STATUS_LINE",          "行",                         "Line"},
    {"STATUS_COLUMN",        "列",                         "Col"},
    {"STATUS_MODIFIED",      "已修改",                     "Modified"},
    {"STATUS_READY",         "就绪",                       "Ready"},
    {"STATUS_LOADING",      "加载中...",                  "Loading..."},
    {"STATUS_DONE",          "完成",                       "Done"},
    {"STATUS_ERROR",         "错误",                       "Error"},
    {"STATUS_CONNECTING",   "连接中...",                  "Connecting..."},

    /* Program Manager */
    {"PROG_MAIN",            "主群组",                     "Main"},
    {"PROG_ACCESSORIES",    "附件",                       "Accessories"},
    {"PROG_GAMES",           "游戏",                       "Games"},
    {"PROG_STARTUP",        "启动",                       "Startup"},

    /* 应用补充 / App extras */
    {"APP_SETTINGS",         "设置",                       "Settings"},

    /* WindowMaker 外壳 / WindowMaker shell (wmaker_shell.c) */
    {"WM_MENU_TITLE",        "WindowMaker",                "WindowMaker"},
    {"WM_MENU_APPS",         "应用程序 Applications",       "Applications"},
    {"WM_MENU_SHELL",        "外壳 Shell",                  "Shell"},
    {"WM_MENU_SWITCH_WIN3",  "切换到 Win3.2 桌面",          "Switch to Win3.2"},
    {"WM_MENU_INFO",         "信息 Info",                   "Info"},
    {"WM_MENU_ABOUT",        "关于本机",                    "About"},
    {"WM_ABOUT_TITLE",       "关于",                        "About"},
    {"WM_ABOUT_TEXT",        "ESP32 复古工作站 WindowMaker 外壳 v0.1", "ESP32 Retro WS WindowMaker shell v0.1"},

    /* 空字符串 / End marker */
    {"",                    "",                            ""},
};

/* 应用名称 / Application names */
static const i18n_string_t g_app_strings[] = {
    {"APP_NOTEPAD",          "记事本",                     "Notepad"},
    {"APP_BROWSER",          "浏览器",                     "Browser"},
    {"APP_TERMINAL",         "终端",                       "Terminal"},
    {"APP_MINESWEEPER",      "扫雷",                       "Minesweeper"},
    {"APP_FILE_MANAGER",     "文件管理器",                 "File Manager"},
    {"APP_CONTROL_PANEL",    "控制面板",                   "Control Panel"},
    {"APP_CALCULATOR",       "计算器",                     "Calculator"},
    {"APP_CLOCK",            "时钟",                       "Clock"},
    {"APP_MEDIA_PLAYER",     "媒体播放器",                  "Media Player"},
    {"APP_RECORDER",         "录音机",                      "Recorder"},
    {"APP_SQLITE",           "SQLite 浏览器",                "SQLite Browser"},

    /* SQLite Browser / SQLite 浏览器 */
    {"SQLITE_TITLE",         "SQLite 数据库查询工具",          "SQLite Database Browser"},
    {"SQLITE_DB_PATH",       "数据库:",                      "Database:"},
    {"SQLITE_SQL",           "SQL:",                         "SQL:"},
    {"SQLITE_HISTORY",       "历史:",                        "History:"},
    {"SQLITE_NO_HISTORY",    "(无历史)",                      "(No history)"},
    {"SQLITE_NO_RESULT",     "(无结果)",                      "(No result)"},
    {"SQLITE_READY",         "就绪",                         "Ready"},
    {"SQLITE_CLEARED",       "已清除",                        "Cleared"},
    {"SQLITE_ERROR",         "错误",                          "Error"},
    {"SQLITE_SUCCESS",        "成功",                          "Success"},
    {"SQLITE_NO_DB",         "未选择数据库",                   "No database selected"},
    {"SQLITE_NO_SQL",        "未输入 SQL 语句",                 "No SQL statement"},
    {"SQLITE_NO_PATH",        "未输入路径",                     "No path"},
    {"SQLITE_DB_OPENED",     "已打开: %s",                    "Opened: %s"},
    {"SQLITE_MODIFY_OK",     "(语句已执行)",                  "(Query executed)"},
    {"SQLITE_ERR_NO_DB",     "错误: 未打开数据库",              "Error: No database"},
    {"SQLITE_ERR_NO_SQL",    "错误: 未输入 SQL",                "Error: No SQL"},
    {"SQLITE_ERR_NO_PATH",   "错误: 未输入路径",                "Error: No path"},
    {"SQLITE_ERR_OPEN",      "错误: 无法打开数据库",             "Error: Cannot open database"},
    {"SQLITE_ERR_EXPORT",    "错误: 导出失败",                   "Error: Export failed"},
    {"SQLITE_EXPORTING_CSV", "正在导出 CSV: %s",               "Exporting CSV: %s"},
    {"SQLITE_CSV_EXPORTED",  "CSV 已导出: %s",                 "CSV exported: %s"},
    {"SQLITE_EXPORTING_DB",  "正在导出数据库: %s",             "Exporting SQLite DB: %s"},
    {"SQLITE_DB_EXPORTED",   "数据库已导出: %s",               "SQLite DB exported: %s"},
    {"SQLITE_EXPORT_FAILED", "导出失败",                        "Export failed"},
    {"SQLITE_OPEN_FAILED",   "无法打开: %s",                   "Cannot open: %s"},
    {"SQLITE_ROWS_RETURNED", "返回 %d 行 x %d 列",             "Returned %d rows x %d cols"},
    {"SQLITE_AFFECTED_ROWS", "影响 %d 行",                    "Affected %d rows"},

    /* SQLite 缺失键补齐 / Missing SQLite keys added 2026-10-04 */
    {"SQLITE_ERR_NO_RESULT", "错误: 无结果可导出",              "Error: No result to export"},
    {"SQLITE_NO_RESULT2",    "无结果可导出",                    "No result to export"},
    {"SQLITE_ERR_OPEN_FMT",  "打开错误: %s",                   "Open error: %s"},
    {"SQLITE_ERR_SQL_FMT",   "SQL 错误: %s",                   "SQL error: %s"},

    /* 拼音输入法 / Pinyin IME (app_pinyin.c) */
    {"PINYIN_INPUT_HINT",    "输入拼音，数字键选择候选 / Input pinyin, 1-9 select",
                             "Type pinyin, press 1-9 to pick"},
    {"PINYIN_CHINESE_MODE",  "中文模式 / Chinese mode",        "Chinese mode"},
    {"PINYIN_ENGLISH_MODE",  "英文模式 / English mode",        "English mode"},

    /* Menu */
    {"MENU_FILE",            "文件",                          "File"},
    {"MENU_EXPORT",          "导出",                          "Export"},
    {"MENU_HELP",            "帮助",                          "Help"},
    {"BTN_OPEN",             "打开",                          "Open"},
    {"BTN_EXECUTE",          "执行",                          "Execute"},
    {"BTN_CLEAR",            "清除",                          "Clear"},
    {"BTN_SAVE",             "保存",                          "Save"},
    {"BTN_CANCEL",           "取消",                          "Cancel"},
    {"BTN_OK",               "确定",                          "OK"},
    {"",                    "",                            ""},
};

/*======================================
 *  API 实现 / API Implementation
 *======================================*/

/**
 * 设置当前语言 / Set current language
 * @param lang 语言代码 zh_CN / en_US / NULL (toggle)
 */
void i18n_set_lang(const char *lang)
{
    if (lang == NULL) {
        /* 切换语言 / Toggle language */
        if (strcmp(g_current_lang, "zh_CN") == 0) {
            g_current_lang = "en_US";
        } else {
            g_current_lang = "zh_CN";
        }
    } else {
        if (strcmp(lang, "zh_CN") == 0 || strcmp(lang, "en_US") == 0) {
            g_current_lang = lang;
        }
    }
    syslog(LOG_INFO, "i18n: language set to %s\n", g_current_lang);
    /* 保存到持久化存储 / Save to persistent storage */
    i18n_save_to_storage();
}

/**
 * 获取当前语言 / Get current language
 */
const char *i18n_get_lang(void)
{
    return g_current_lang;
}

/**
 * 获取字符串 / Get string by key
 * @param key 字符串键名
 * @return 对应语言的字符串，或 key 本身（未找到时）
 */
const char *i18n_get(const char *key)
{
    if (!key) return "";

    /* 查找桌面字符串 / Search desktop strings */
    for (int i = 0; g_desktop_strings[i].key[0] != 0; i++) {
        if (strcmp(g_desktop_strings[i].key, key) == 0) {
            if (strcmp(g_current_lang, "zh_CN") == 0) {
                return g_desktop_strings[i].zh_CN;
            } else {
                return g_desktop_strings[i].en_US;
            }
        }
    }

    /* 查找应用字符串 / Search app strings */
    for (int i = 0; g_app_strings[i].key[0] != 0; i++) {
        if (strcmp(g_app_strings[i].key, key) == 0) {
            if (strcmp(g_current_lang, "zh_CN") == 0) {
                return g_app_strings[i].zh_CN;
            } else {
                return g_app_strings[i].en_US;
            }
        }
    }

    /* 未找到 / Not found */
    return key;
}

/**
 * 获取字符串（带格式化）/ Get string with format
 * @param key 字符串键名
 * @param buf 输出缓冲区
 * @param bufsiz 缓冲区大小
 * @param ... 格式化参数
 */
void i18n_getf(const char *key, char *buf, size_t bufsiz, ...)
{
    const char *fmt = i18n_get(key);
    va_list args;
    va_start(args, bufsiz);
    vsnprintf(buf, bufsiz, fmt, args);
    va_end(args);
}

/**
 * 检查是否中文模式 / Check if Chinese mode
 */
int i18n_is_chinese(void)
{
    return strcmp(g_current_lang, "zh_CN") == 0;
}

/**
 * 获取汉字数组成员 / Get array of Chinese strings
 * 用于下拉列表等 / For dropdown lists etc.
 */
const char * const *i18n_get_array(const char *prefix)
{
    /* TODO: 实现动态数组 / TODO: implement dynamic array */
    return NULL;
}

/*======================================
 *  LVGL 标签快速宏 / LVGL Label Quick Macros
 *======================================*/

/**
 * 设置标签文本（自动多语种）/ Set label text (auto i18n)
 * @param label LVGL 标签对象
 * @param key   字符串键名
 */
static void lv_label_set_text_i18n(lv_obj_t *label, const char *key)
{
    lv_label_set_text(label, i18n_get(key));
}

#endif /* CONFIG_LVGL */
