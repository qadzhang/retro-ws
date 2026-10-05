/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui_berry.c - retro_ui 的 Berry 绑定
 *
 * WHAT : retro_ui 的 Berry 绑定
 * WHY  : Berry 脚本调用 retro_ui_* 统一接口
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/modules/retro_ui_berry.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : be_regfunc 注册全局函数，list 经 be_getindex 迭代（可选编译）
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_BERRY

#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

#include "berry.h"
#include "retro_ui.h"

#define BERRY_LIST_MAX_ITEMS   20
#define BERRY_ITEM_MAX_LEN     64

/*======================================
 * 工具函数 / Utility Functions
 *======================================*/

/* 取 Berry 参数为 C 字符串 / get Berry arg as C string */
static const char *be_arg_string(bvm *vm, int idx)
{
    if (idx > be_top(vm))
        return "";
    const char *s = be_tostring(vm, idx);
    return s ? s : "";
}

/* 取 Berry 参数为整数 / get Berry arg as int */
static int be_arg_int(bvm *vm, int idx, int default_val)
{
    if (idx > be_top(vm) || !be_isnumber(vm, idx))
        return default_val;
    return be_toint(vm, idx);
}

/*======================================
 * retro_ui_msgbox(title, msg)
 *======================================*/
static int be_retro_ui_msgbox(bvm *vm)
{
    const char *title = be_arg_string(vm, 1);
    const char *msg = be_arg_string(vm, 2);

    syslog(LOG_INFO, "[retro_ui_berry] msgbox: %s\n", title);

#ifdef CONFIG_LVGL
    retro_ui_msgbox(title, msg);
#endif
    be_return_nil(vm);
}

/*======================================
 * retro_ui_input(title, prompt, bufsize) -> string
 *======================================*/
static int be_retro_ui_input(bvm *vm)
{
    const char *title = be_arg_string(vm, 1);
    const char *prompt = be_arg_string(vm, 2);
    int bufsize = be_arg_int(vm, 3, 64);

    if (bufsize < 1) bufsize = 64;
    if (bufsize > 1024) bufsize = 1024;

    syslog(LOG_INFO, "[retro_ui_berry] input: %s\n", title);

    static char g_be_input_buf[1024];

#ifdef CONFIG_LVGL
    int ok = retro_ui_input(title, prompt, g_be_input_buf, bufsize);
    if (ok == 1) {
        be_pushstring(vm, g_be_input_buf);
        be_return(vm);
    }
#endif
    be_pushstring(vm, "");
    be_return(vm);
}

/*======================================
 * retro_ui_list(title, prompt, items) -> int
 *======================================*/
static int be_retro_ui_list(bvm *vm)
{
    const char *title = be_arg_string(vm, 1);
    const char *prompt = be_arg_string(vm, 2);

    syslog(LOG_INFO, "[retro_ui_berry] list: %s\n", title);

    int selected = -1;

#ifdef CONFIG_LVGL
    if (be_top(vm) >= 3 && be_islist(vm, 3)) {
        /* 取 list.size() / get list size via size() method */
        be_getmember(vm, 3, "size");
        be_call(vm, 0);
        int count = be_toint(vm, -1);
        be_pop(vm, 1);

        if (count < 0) count = 0;
        if (count > BERRY_LIST_MAX_ITEMS) count = BERRY_LIST_MAX_ITEMS;

        static const char *g_be_items[BERRY_LIST_MAX_ITEMS];
        static char g_be_item_bufs[BERRY_LIST_MAX_ITEMS][BERRY_ITEM_MAX_LEN];

        for (int i = 0; i < count; i++) {
            /* 以整数下标取元素：key 压栈后 be_getindex / index access */
            be_pushint(vm, i);
            be_getindex(vm, 3);
            const char *item = be_tostring(vm, -1);
            strncpy(g_be_item_bufs[i], item ? item : "",
                    BERRY_ITEM_MAX_LEN - 1);
            g_be_item_bufs[i][BERRY_ITEM_MAX_LEN - 1] = '\0';
            g_be_items[i] = g_be_item_bufs[i];
            be_pop(vm, 1);
        }

        selected = retro_ui_list(title, prompt, g_be_items, count);
    }
#endif
    be_pushint(vm, selected);
    be_return(vm);
}

/*======================================
 * retro_ui_confirm(title, msg) -> bool
 *======================================*/
static int be_retro_ui_confirm(bvm *vm)
{
    const char *title = be_arg_string(vm, 1);
    const char *msg = be_arg_string(vm, 2);

    syslog(LOG_INFO, "[retro_ui_berry] confirm: %s\n", title);

    int result = 0;
#ifdef CONFIG_LVGL
    result = retro_ui_confirm(title, msg);
#endif
    be_pushbool(vm, result == 1);
    be_return(vm);
}

/*======================================
 * retro_ui_status(msg)
 *======================================*/
static int be_retro_ui_status(bvm *vm)
{
    const char *msg = be_arg_string(vm, 1);

    syslog(LOG_INFO, "[retro_ui_berry] status: %s\n", msg);

#ifdef CONFIG_LVGL
    retro_ui_status(msg);
#endif
    be_return_nil(vm);
}

/*======================================
 * retro_ui_progress(value, max)
 *======================================*/
static int be_retro_ui_progress(bvm *vm)
{
    int value = be_arg_int(vm, 1, 0);
    int max = be_arg_int(vm, 2, 100);

    syslog(LOG_INFO, "[retro_ui_berry] progress: %d/%d\n", value, max);

#ifdef CONFIG_LVGL
    retro_ui_progress(value, max);
#endif
    be_return_nil(vm);
}

/*======================================
 * retro_ui_close_window(title) -> int
 *======================================*/
static int be_retro_ui_close_window(bvm *vm)
{
    const char *title = be_arg_string(vm, 1);

    syslog(LOG_INFO, "[retro_ui_berry] close_window: %s\n", title);

    int result = -1;
#ifdef CONFIG_LVGL
    result = retro_ui_close_window(title);
#endif
    be_pushint(vm, result);
    be_return(vm);
}

/*======================================
 * retro_ui_set_lang(lang) -> int
 *======================================*/
static int be_retro_ui_set_lang(bvm *vm)
{
    const char *lang = be_arg_string(vm, 1);
    be_pushint(vm, retro_ui_set_lang(lang));
    be_return(vm);
}

/*======================================
 * retro_ui_get_lang() -> string
 *======================================*/
static int be_retro_ui_get_lang(bvm *vm)
{
    const char *lang = retro_ui_get_lang();
    be_pushstring(vm, lang ? lang : "unknown");
    be_return(vm);
}

/*======================================
 * 模块初始化 / Module Initialization
 *======================================*/

/**
 * retro_ui_berry_init - 注册 retro_ui_* 全局函数
 * retro_ui_berry_init - register retro_ui_* global functions
 * @vm: Berry 虚拟机 / Berry VM
 */
void retro_ui_berry_init(bvm *vm)
{
    if (!vm)
        return;

#ifdef CONFIG_LVGL
    retro_ui_init();
#endif

    /* 全局函数形式，与 BASIC 的 CALL retro_ui_* 对齐 */
    be_regfunc(vm, "retro_ui_msgbox",      be_retro_ui_msgbox);
    be_regfunc(vm, "retro_ui_input",       be_retro_ui_input);
    be_regfunc(vm, "retro_ui_list",        be_retro_ui_list);
    be_regfunc(vm, "retro_ui_confirm",     be_retro_ui_confirm);
    be_regfunc(vm, "retro_ui_status",      be_retro_ui_status);
    be_regfunc(vm, "retro_ui_progress",    be_retro_ui_progress);
    be_regfunc(vm, "retro_ui_close_window", be_retro_ui_close_window);
    be_regfunc(vm, "retro_ui_set_lang",    be_retro_ui_set_lang);
    be_regfunc(vm, "retro_ui_get_lang",    be_retro_ui_get_lang);

    syslog(LOG_INFO, "[retro_ui_berry] module initialized\n");
}

/**
 * retro_ui_berry_deinit - 清理 / cleanup
 */
void retro_ui_berry_deinit(bvm *vm)
{
    (void)vm;
#ifdef CONFIG_LVGL
    retro_ui_deinit();
#endif
    syslog(LOG_INFO, "[retro_ui_berry] module deinitialized\n");
}

#endif /* CONFIG_RETRO_SCRIPT_BERRY */
