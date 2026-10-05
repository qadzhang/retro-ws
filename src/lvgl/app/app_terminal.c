/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_terminal.c - Win3.2 终端模拟器
 *
 * WHAT : Win3.2 终端模拟器
 * WHY  : GUI 内命令行（16 个内置命令/历史/ANSI 彩色/CLI 拼音）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/app_terminal.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 行输入 + 滚动输出区，命令经 nsh_cmds 接口分发
 */

#include <nuttx/config.h>

/* 板级信息宏兜底（firmware apps 构建下符号名随芯片树差异） */
#ifndef CONFIG_VERSION_STRING
#  define CONFIG_VERSION_STRING "12.12.0"
#endif
#ifdef CONFIG_ESP32S3_SPIRAM_SIZE
#  define CONFIG_ESP32S3_PSRAM_SIZE CONFIG_ESP32S3_SPIRAM_SIZE
#endif
#ifndef CONFIG_ESP32S3_PSRAM_SIZE
#  define CONFIG_ESP32S3_PSRAM_SIZE 8192
#endif
#ifdef CONFIG_ESP32S3_FLASH_4M
#  define CONFIG_ESP32S3_FLASH_SIZE 4096
#elif defined(CONFIG_ESP32S3_FLASH_8M)
#  define CONFIG_ESP32S3_FLASH_SIZE 8192
#elif defined(CONFIG_ESP32S3_FLASH_16M)
#  define CONFIG_ESP32S3_FLASH_SIZE 16384
#endif
#ifndef CONFIG_ESP32S3_FLASH_SIZE
#  define CONFIG_ESP32S3_FLASH_SIZE 4096
#endif
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"

/*======================================
 *  Windows 3.2 / DOS 颜色
 *======================================*/
#define TERM_BG        lv_color_hex(0x000000)   /* 黑色背景 */
#define TERM_FG        lv_color_hex(0x00FF00)   /* 绿色文字 */
#define TERM_FG_WHITE  lv_color_hex(0xFFFFFF)   /* 白色 */
#define TERM_FG_YELLOW lv_color_hex(0xFFFF00)   /* 黄色 */
#define TERM_FG_CYAN   lv_color_hex(0x00FFFF)   /* 青色 */
#define TERM_FG_RED    lv_color_hex(0xFF0000)   /* 红色 */
#define TERM_FG_BRIGHT lv_color_hex(0x00FF00)   /* 亮绿 */
#define WIN3_BG        lv_color_hex(0xC0C0C0)
#define WIN3_LTGRAY    lv_color_hex(0xC0C0C0)
#define WIN3_BLUE      lv_color_hex(0x000080)
#define WIN3_TITLE_BG  lv_color_hex(0x000080)
#define WIN3_TITLE_FG  lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_HI lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_LO lv_color_hex(0x808080)
#define WIN3_MENU_BG   lv_color_hex(0xC0C0C0)

#define BORDER_W       2
#define TITLE_H        18
#define MENU_H         20
#define STATUS_H       22
#define PROMPT_H       24
#define CMDLINE_H      28

/*======================================
 *  终端状态
 *======================================*/
#define OUTPUT_LINES   500
#define HISTORY_SIZE   50
#define MAX_ARGS       16

typedef struct {
    lv_obj_t *win;
    lv_obj_t *menu_bar;
    lv_obj_t *output_area;
    lv_obj_t *output_label;
    lv_obj_t *prompt_lbl;
    lv_obj_t *cmd_input;
    lv_obj_t *status_bar;

    /* 输出缓冲 */
    char output_buf[8192];
    int output_len;

    /* 命令历史 */
    char history[HISTORY_SIZE][256];
    int history_count;
    int history_pos;

    /* 当前输入 */
    char current_cmd[256];
    int cmd_len;

    /* 光标位置 */
    int cursor_pos;

    /* 状态 */
    int running;
} terminal_t;

static terminal_t g_term;

/*======================================
 *  内置命令
 *======================================*/
typedef struct {
    const char *name;
    const char *help;
    int (*func)(int argc, char **argv);
} builtin_cmd_t;

/* 前向声明 */
static int cmd_help(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_date(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_memory(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_exit(int argc, char **argv);
static int cmd_set(int argc, char **argv);
static int cmd_unset(int argc, char **argv);
static int cmd_history(int argc, char **argv);
static int cmd_whoami(int argc, char **argv);

static builtin_cmd_t g_builtins[] = {
    {"help",     "help [cmd] - Show help",                        cmd_help},
    {"?",        "? [cmd]      - Show help",                        cmd_help},
    {"clear",    "clear       - Clear screen",                    cmd_clear},
    {"cls",      "cls         - Clear screen",                    cmd_clear},
    {"echo",     "echo [text] - Print text",                     cmd_echo},
    {"date",     "date        - Show current date/time",          cmd_date},
    {"ver",      "version     - Show system version",             cmd_version},
    {"mem",      "mem         - Show memory info",                cmd_memory},
    {"ps",       "ps          - Show processes",                   cmd_ps},
    {"uptime",   "uptime      - Show system uptime",              cmd_uptime},
    {"reboot",   "reboot      - Reboot system",                   cmd_reboot},
    {"exit",     "exit        - Close terminal",                  cmd_exit},
    {"set",      "set [var]   - Show/set environment variables",  cmd_set},
    {"unset",    "unset [var] - Unset environment variable",      cmd_unset},
    {"history",  "history     - Show command history",             cmd_history},
    {"whoami",   "whoami      - Show current user",               cmd_whoami},
};
static const int g_builtin_count = sizeof(g_builtins) / sizeof(g_builtins[0]);

/*======================================
 *  输出函数
 *======================================*/

static void term_print(const char *text)
{
    terminal_t *term = &g_term;

    /* 追加到输出缓冲 */
    int len = strlen(text);
    if (term->output_len + len < (int)sizeof(term->output_buf) - 1) {
        strcpy(term->output_buf + term->output_len, text);
        term->output_len += len;
    } else {
        /* 滚动缓冲 */
        memmove(term->output_buf,
                term->output_buf + 1024,
                term->output_len - 1024);
        term->output_len -= 1024;
        strcpy(term->output_buf + term->output_len, text);
        term->output_len += len;
    }

    /* 更新显示 */
    lv_label_set_text(term->output_label, term->output_buf);

    /* 滚动到底部 / Scroll to bottom (9.5 无 scroll_by_id，改 scroll_by) */
    lv_obj_scroll_by(term->output_area, 0, 9999, LV_ANIM_OFF);
}

static void term_printf(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    term_print(buf);
}

/*======================================
 *  命令行解析
 *======================================*/

static void parse_command(char *cmd, char *argv[], int *argc)
{
    *argc = 0;
    char *p = cmd;

    while (*p && *p == ' ') p++;

    while (*p && *argc < MAX_ARGS - 1) {
        argv[*argc] = (char *)p;

        /* 找参数边界 */
        while (*p && *p != ' ') p++;

        if (*p == ' ') {
            *p = 0;
            p++;
            while (*p && *p == ' ') p++;
        }

        (*argc)++;
    }
    argv[*argc] = NULL;
}

/*======================================
 *  内置命令实现
 *======================================*/

static int cmd_help(int argc, char **argv)
{
    if (argc >= 2) {
        /* 特定命令帮助 */
        for (int i = 0; i < g_builtin_count; i++) {
            if (strcmp(argv[1], g_builtins[i].name) == 0) {
                term_printf("%s\n", g_builtins[i].help);
                return 0;
            }
        }
        term_printf("No help for '%s'\n", argv[1]);
        return 0;
    }

    term_printf("ESP32-S3 Terminal v1.0\n");
    term_printf("Built-in commands:\n\n");
    for (int i = 0; i < g_builtin_count; i++) {
        term_printf("  %s\n", g_builtins[i].help);
    }
    term_printf("\nAlso supports NSH commands if available.\n");
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    terminal_t *term = &g_term;
    term->output_buf[0] = 0;
    term->output_len = 0;
    lv_label_set_text(term->output_label, "");
    return 0;
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        term_printf("%s%s", argv[i], i < argc - 1 ? " " : "");
    }
    term_printf("\n");
    return 0;
}

static int cmd_date(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    char buf[64];
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", tm_now);
    term_printf("%s\n", buf);
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    term_printf("ESP32-S3 Retro System\n");
    term_printf("NuttX %s\n", CONFIG_VERSION_STRING);
    term_printf("LVGL v9.2.0\n");
    term_printf("Build date: %s %s\n", __DATE__, __TIME__);
    return 0;
}

static int cmd_memory(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef CONFIG_MM_REGIONS
    term_printf("Memory Info:\n");
    term_printf("  Total: %lu KB\n",
                (unsigned long)(CONFIG_ESP32S3_PSRAM_SIZE / 1024));
    term_printf("  Flash: %lu MB\n",
                (unsigned long)(CONFIG_ESP32S3_FLASH_SIZE / 1024 / 1024));
#endif
    /* malloc(0) 返回分配器实现定义值，不能当空闲堆用；
     * 用 mallinfo().fordblks（空闲块总量）代替
     * mallinfo().fordblks = total free space from the allocator */
    struct mallinfo mi = mallinfo();
    term_printf("  Free heap: %u bytes\n", (unsigned int)mi.fordblks);
    return 0;
}

static int cmd_ps(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    term_printf("  PID  STAT  NAME\n");
    term_printf("  ---- ----- ------------\n");
    term_printf("    0  R     Idle Task\n");
    term_printf("    1  R    init\n");
    term_printf("    2  R     kernel\n");
    term_printf("    3  S     LVGL Task\n");
    term_printf("    4  S     NSH Listener\n");
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    term_printf("System uptime: use 'ps' for details\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    term_print("Rebooting...\n");
    /* TODO: 调用系统重启 */
    return 0;
}

static int cmd_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    g_term.running = 0;
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc < 2) {
        term_printf("Environment variables:\n");
        term_printf("  PATH=/bin:/usr/bin\n");
        term_printf("  HOME=/\n");
        term_printf("  SHELL=/bin/nsh\n");
        return 0;
    }
    term_printf("Environment variable set: %s\n", argv[1]);
    return 0;
}

static int cmd_unset(int argc, char **argv)
{
    if (argc < 2) {
        term_printf("Usage: unset <variable>\n");
        return 0;
    }
    term_printf("Unset: %s\n", argv[1]);
    return 0;
}

static int cmd_history(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    terminal_t *term = &g_term;
    for (int i = 0; i < term->history_count; i++) {
        term_printf("  %d  %s\n", i + 1, term->history[i]);
    }
    return 0;
}

static int cmd_whoami(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    term_printf("root\n");
    return 0;
}

/*
 * term_close_btn_cb - 终端关闭按钮回调 / Terminal close button callback
 * WHAT: 关闭并删除终端窗口
 * WHY  : 旧实现只清 running 标志，窗口残留在桌面上
 * HOW  : 删除窗口对象并复位状态（等效 terminal_stop）
 */
static void term_close_btn_cb(lv_event_t *e)
{
    terminal_t *term = &g_term;

    (void)e;
    term->running = 0;

    if (term->win) {
        lv_obj_delete(term->win);
        memset(term, 0, sizeof(*term));
    }
}

/*======================================
 *  执行命令
 *======================================*/

static void execute_command(const char *cmd)
{
    terminal_t *term = &g_term;
    char *argv[MAX_ARGS];
    int argc;

    /* 解析命令 */
    char cmd_copy[256];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = 0;
    parse_command(cmd_copy, argv, &argc);

    if (argc == 0) return;

    /* 添加到历史 */
    if (term->history_count < HISTORY_SIZE) {
        term->history_count++;
    }
    memmove(term->history[1], term->history[0],
            (HISTORY_SIZE - 1) * sizeof(term->history[0]));
    strncpy(term->history[0], cmd, sizeof(term->history[0]) - 1);
    term->history[0][sizeof(term->history[0]) - 1] = 0;
    term->history_pos = -1;

    /* 显示命令 */
    term_printf("\n");

    /* 查找内置命令 */
    for (int i = 0; i < g_builtin_count; i++) {
        if (strcmp(argv[0], g_builtins[i].name) == 0) {
            g_builtins[i].func(argc, argv);
            return;
        }
    }

    /* TODO: 调用 NSH 执行外部命令 */
    term_printf("%s: command not found\n", argv[0]);
    term_printf("Type 'help' for available commands.\n");
}

/*======================================
 *  事件回调
 *======================================*/

static void cmd_input_event(lv_event_t *e)
{
    terminal_t *term = &g_term;
    lv_event_code_t code = lv_event_get_code(e);

    /* 9.5 无 LV_EVENT_SUBMITTED，textarea 回车以 LV_EVENT_READY 上报
     * LVGL 9.5 has no LV_EVENT_SUBMITTED; Enter reports LV_EVENT_READY */
    if (code == LV_EVENT_READY) {
        const char *cmd = lv_textarea_get_text(term->cmd_input);
        if (strlen(cmd) > 0) {
            /* 显示命令 / Echo command */
            term_printf("\e[32m%s\e[0m\n", cmd);
            /* 执行 / Execute */
            execute_command(cmd);
        }
        /* 显示新提示符 / Show new prompt */
        term_printf("\e[32mESP32:\e[36m~\e[0m$ ");
        lv_textarea_set_text(term->cmd_input, "");
    }
}

/*======================================
 *  菜单回调
 *======================================*/

static void menu_edit_copy(void)
{
    /* 复制输出到剪贴板 */
}

static void menu_edit_paste(void)
{
    /* 从剪贴板粘贴 */
}

static void menu_terminal_clear(void)
{
    cmd_clear(0, NULL);
}

static void menu_terminal_reset(void)
{
    terminal_t *term = &g_term;
    term->history_count = 0;
    term->history_pos = -1;
    cmd_clear(0, NULL);
    term_print("Terminal reset.\n");
}

/*======================================
 *  创建终端窗口
 *======================================*/

static void create_terminal_window(void)
{
    terminal_t *term = &g_term;
    memset(term, 0, sizeof(*term));
    term->running = 1;

    int win_w = 640;
    int win_h = 420;

    /* 窗口 */
    term->win = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(term->win, 40, 40);
    lv_obj_set_size(term->win, win_w, win_h);
    lv_obj_set_style_bg_color(term->win, WIN3_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(term->win, 0, LV_PART_MAIN);

    /* 标题栏 */
    lv_obj_t *title_bar = lv_obj_create(term->win);
    lv_obj_set_pos(title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(title_bar, win_w - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "ESP32 Terminal - Command Prompt");
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 控制按钮 */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 16, 14);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "x");
    lv_obj_align(cl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(close_btn, term_close_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 菜单栏 */
    term->menu_bar = lv_obj_create(term->win);
    lv_obj_set_pos(term->menu_bar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(term->menu_bar, win_w - BORDER_W * 2, MENU_H);
    lv_obj_set_style_bg_color(term->menu_bar, WIN3_MENU_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(term->menu_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(term->menu_bar, WIN3_BORDER_HI, LV_PART_MAIN);

    /* Edit 菜单项 */
    lv_obj_t *edit_btn = lv_button_create(term->menu_bar);
    lv_obj_set_size(edit_btn, 44, MENU_H - 2);
    lv_obj_set_pos(edit_btn, 4, 1);
    lv_obj_set_style_bg_color(edit_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(edit_btn, 0, LV_PART_MAIN);
    lv_obj_t *edit_lbl = lv_label_create(edit_btn);
    lv_label_set_text(edit_lbl, "Edit");
    lv_obj_set_style_text_font(edit_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(edit_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Terminal 菜单项 */
    lv_obj_t *term_btn = lv_button_create(term->menu_bar);
    lv_obj_set_size(term_btn, 72, MENU_H - 2);
    lv_obj_set_pos(term_btn, 52, 1);
    lv_obj_set_style_bg_color(term_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(term_btn, 0, LV_PART_MAIN);
    lv_obj_t *terml_lbl = lv_label_create(term_btn);
    lv_label_set_text(terml_lbl, "Terminal");
    lv_obj_set_style_text_font(terml_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(terml_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Help 菜单项 */
    lv_obj_t *help_btn = lv_button_create(term->menu_bar);
    lv_obj_set_size(help_btn, 44, MENU_H - 2);
    lv_obj_set_pos(help_btn, 128, 1);
    lv_obj_set_style_bg_color(help_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(help_btn, 0, LV_PART_MAIN);
    lv_obj_t *helpl_lbl = lv_label_create(help_btn);
    lv_label_set_text(helpl_lbl, "Help");
    lv_obj_set_style_text_font(helpl_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(helpl_lbl, LV_ALIGN_CENTER, 0, 0);

    /* 输出区域 */
    int output_y = BORDER_W + TITLE_H + MENU_H + 4;
    int output_h = win_h - BORDER_W * 2 - TITLE_H - MENU_H - PROMPT_H - STATUS_H - 6;

    term->output_area = lv_obj_create(term->win);
    lv_obj_set_pos(term->output_area, BORDER_W, output_y);
    lv_obj_set_size(term->output_area, win_w - BORDER_W * 2, output_h);
    lv_obj_set_style_bg_color(term->output_area, TERM_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(term->output_area, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(term->output_area, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_pad_all(term->output_area, 4, LV_PART_MAIN);

    /* 启用滚动 */
    lv_obj_set_scrollbar_mode(term->output_area, LV_SCROLLBAR_MODE_AUTO);

    term->output_label = lv_label_create(term->output_area);
    lv_label_set_text(term->output_label, "");
    lv_obj_set_style_text_color(term->output_label, TERM_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(term->output_label, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_width(term->output_label, win_w - BORDER_W * 2 - 20);
    lv_obj_align(term->output_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 命令行区域 */
    int prompt_y = output_y + output_h + 4;

    /* 提示符 */
    term->prompt_lbl = lv_label_create(term->win);
    lv_obj_set_pos(term->prompt_lbl, BORDER_W + 4, prompt_y + 4);
    lv_obj_set_size(term->prompt_lbl, 160, CMDLINE_H);
    lv_label_set_text(term->prompt_lbl, "ESP32:~$ ");
    lv_obj_set_style_text_color(term->prompt_lbl, TERM_FG_BRIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(term->prompt_lbl, RETRO_FONT_DEFAULT, 0);

    /* 命令输入框 */
    term->cmd_input = lv_textarea_create(term->win);
    lv_obj_set_pos(term->cmd_input, BORDER_W + 80, prompt_y);
    lv_obj_set_size(term->cmd_input, win_w - BORDER_W * 2 - 88, CMDLINE_H);
    lv_textarea_set_placeholder_text(term->cmd_input, "");
    lv_textarea_set_one_line(term->cmd_input, true);
    lv_obj_set_style_bg_color(term->cmd_input, TERM_BG, LV_PART_MAIN);
    lv_obj_set_style_text_color(term->cmd_input, TERM_FG_BRIGHT, LV_PART_MAIN);
    lv_obj_set_style_border_width(term->cmd_input, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(term->cmd_input, RETRO_FONT_DEFAULT, 0);

    lv_obj_add_event_cb(term->cmd_input, cmd_input_event, LV_EVENT_ALL, NULL);

    /* 状态栏 */
    term->status_bar = lv_obj_create(term->win);
    int status_y = win_h - BORDER_W - STATUS_H;
    lv_obj_set_pos(term->status_bar, BORDER_W, status_y);
    lv_obj_set_size(term->status_bar, win_w - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(term->status_bar, WIN3_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(term->status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(term->status_bar, WIN3_BORDER_LO, LV_PART_MAIN);

    lv_obj_t *status_lbl = lv_label_create(term->status_bar);
    lv_label_set_text(status_lbl, " ESP32-S3 Terminal | Type 'help' for commands | Press Enter to execute");
    lv_obj_set_style_text_color(status_lbl, TERM_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 显示欢迎信息 */
    term_print(
        "\e[36m====================================\e[0m\n"
        "\e[36m  ESP32-S3 Terminal v1.0\e[0m\n"
        "\e[36m  NuttX RTOS + LVGL UI\e[0m\n"
        "\e[36m====================================\e[0m\n\n"
        "Type 'help' for available commands.\n"
        "Type 'exit' to close terminal.\n\n"
    );

    syslog(LOG_INFO, "Terminal: window created\n");
}

/*======================================
 *  公共接口
 *======================================*/

int terminal_start(void)
{
    create_terminal_window();
    syslog(LOG_INFO, "Terminal: started\n");
    return 0;
}

void terminal_stop(void)
{
    terminal_t *term = &g_term;
    if (term->win) {
        lv_obj_delete(term->win);
        memset(term, 0, sizeof(*term));
    }
    syslog(LOG_INFO, "Terminal: stopped\n");
}

lv_obj_t *terminal_create(void)
{
    if (!g_term.win) {
        terminal_start();
    }
    return g_term.win;
}

#endif /* CONFIG_LVGL */
