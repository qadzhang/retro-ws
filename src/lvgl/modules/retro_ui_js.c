/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui_js.c - retro_ui 的 Duktape JS 绑定
 *
 * WHAT : retro_ui 的 Duktape JS 绑定
 * WHY  : JS 脚本调用 retro_ui.msgbox/input/list 等
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/modules/retro_ui_js.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : duk_function_list 注册 retro_ui 全局对象
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

/* Duktape 头文件 / Duktape header files */
#include "duktape.h"
#if __has_include(<duk_console.h>)
#  include <duk_console.h>
#else
#  include "duk_console.h"
#endif
#if __has_include(<duk_print_alert.h>)
#  include <duk_print_alert.h>
#else
#  include "duk_print_alert.h"
#endif

/* retro_ui C 核心接口 / retro_ui C core interface */
#include "retro_ui.h"

/*======================================
 * 工具函数 / Utility Functions
 *======================================*/

/* 获取 JS 字符串并转换为 C 字符串 / Get JS string and convert to C string
 * HOW: duk_safe_to_string 原地转换任意值为字符串（Duktape 真实 API，
 *      原 duk_string_to_c_string 不存在——链接真实解释器时暴露） */
static const char *js_to_c_string(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_null_or_undefined(ctx, idx)) {
        return "";
    }
    const char *s = duk_safe_to_string(ctx, idx);
    return s ? s : "";
}

/* 获取 JS 整数 / Get JS integer */
static int js_get_int(duk_context *ctx, duk_idx_t idx, int default_val)
{
    if (duk_is_number(ctx, idx)) {
        return (int)duk_get_int(ctx, idx);
    }
    return default_val;
}

/* 获取 JS 数组长度 / Get JS array length */
static duk_size_t js_get_array_len(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_array(ctx, idx)) {
        return duk_get_length(ctx, idx);
    }
    return 0;
}

/*======================================
 * JS: retro_ui.msgbox(title, msg)
 *======================================*/
static duk_ret_t js_msgbox(duk_context *ctx)
{
    const char *title = js_to_c_string(ctx, 0);
    const char *msg = js_to_c_string(ctx, 1);

    syslog(LOG_INFO, "[retro_ui_js] msgbox: %s\n", title);

#ifdef CONFIG_LVGL
    int result = retro_ui_msgbox(title, msg);
    (void)result;  /* msgbox 无返回值 / msgbox has no return value */
#endif

    duk_push_number(ctx, 0);
    return 1;
}

/*======================================
 * JS: retro_ui.input(title, prompt, bufsize)
 *======================================*/
static duk_ret_t js_input(duk_context *ctx)
{
    const char *title = js_to_c_string(ctx, 0);
    const char *prompt = js_to_c_string(ctx, 1);
    int bufsize = js_get_int(ctx, 2, 64);

    if (bufsize < 1) bufsize = 64;
    if (bufsize > 1024) bufsize = 1024;  /* 限制大小 / Limit size */

    syslog(LOG_INFO, "[retro_ui_js] input: %s, bufsize=%d\n", title, bufsize);

#ifdef CONFIG_LVGL
    static char g_input_buf[1024];  /* 静态缓冲区 / Static buffer */
    if (bufsize > (int)sizeof(g_input_buf)) {
        bufsize = sizeof(g_input_buf);
    }

    int ok = retro_ui_input(title, prompt, g_input_buf, bufsize);

    if (ok == 1) {
        duk_push_string(ctx, g_input_buf);
    } else {
        duk_push_string(ctx, "");  /* 取消返回空 / Empty on cancel */
    }
#else
    duk_push_string(ctx, "");
#endif
    return 1;
}

/*======================================
 * JS: retro_ui.list(title, prompt, items)
 *======================================*/
static duk_ret_t js_list(duk_context *ctx)
{
    const char *title = js_to_c_string(ctx, 0);
    const char *prompt = js_to_c_string(ctx, 1);

    syslog(LOG_INFO, "[retro_ui_js] list: %s\n", title);

#ifdef CONFIG_LVGL
    /* 从 JS 数组获取 items / Get items from JS array */
    if (!duk_is_array(ctx, 2)) {
        duk_push_number(ctx, -1);
        return 1;
    }

    duk_size_t count = duk_get_length(ctx, 2);
    if (count == 0) {
        duk_push_number(ctx, -1);
        return 1;
    }

    /* 限制项数（ESP32 资源限制）/ Limit items (ESP32 resource constraints) */
    if (count > 20) count = 20;

    /* 将 JS 数组项复制到 C 数组 / Copy JS array items to C array */
    static const char *g_list_items[20];
    static char g_list_bufs[20][64];  /* 每项最大64字符 / Max 64 chars per item */

    for (duk_size_t i = 0; i < count; i++) {
        duk_get_prop_index(ctx, 2, (duk_uarridx_t)i);
        const char *item = js_to_c_string(ctx, -1);
        strncpy(g_list_bufs[i], item, sizeof(g_list_bufs[i]) - 1);
        g_list_bufs[i][sizeof(g_list_bufs[i]) - 1] = '\0';
        g_list_items[i] = g_list_bufs[i];
        duk_pop(ctx);
    }

    int selected = retro_ui_list(title, prompt, g_list_items, (int)count);
    duk_push_number(ctx, (double)selected);
#else
    duk_push_number(ctx, -1);
#endif
    return 1;
}

/*======================================
 * JS: retro_ui.confirm(title, msg)
 *======================================*/
static duk_ret_t js_confirm(duk_context *ctx)
{
    const char *title = js_to_c_string(ctx, 0);
    const char *msg = js_to_c_string(ctx, 1);

    syslog(LOG_INFO, "[retro_ui_js] confirm: %s\n", title);

#ifdef CONFIG_LVGL
    int result = retro_ui_confirm(title, msg);
    duk_push_boolean(ctx, result == 1);
#else
    duk_push_boolean(ctx, 0);
#endif
    return 1;
}

/*======================================
 * JS: retro_ui.status(msg)
 *======================================*/
static duk_ret_t js_status(duk_context *ctx)
{
    const char *msg = js_to_c_string(ctx, 0);

    syslog(LOG_INFO, "[retro_ui_js] status: %s\n", msg);

#ifdef CONFIG_LVGL
    retro_ui_status(msg);
#endif
    return 0;  /* 无返回值 / No return value */
}

/*======================================
 * JS: retro_ui.progress(value, max)
 *======================================*/
static duk_ret_t js_progress(duk_context *ctx)
{
    int value = js_get_int(ctx, 0, 0);
    int max = js_get_int(ctx, 1, 100);

    syslog(LOG_INFO, "[retro_ui_js] progress: %d/%d\n", value, max);

#ifdef CONFIG_LVGL
    retro_ui_progress(value, max);
#endif
    return 0;  /* 无返回值 / No return value */
}

/*======================================
 * JS: retro_ui.close_window(title)
 *======================================*/
static duk_ret_t js_close_window(duk_context *ctx)
{
    const char *title = js_to_c_string(ctx, 0);

    syslog(LOG_INFO, "[retro_ui_js] close_window: %s\n", title);

#ifdef CONFIG_LVGL
    int result = retro_ui_close_window(title);
    duk_push_number(ctx, (double)result);
#else
    duk_push_number(ctx, -1);
#endif
    return 1;
}

/*======================================
 * JS: retro_ui.get_version()
 *======================================*/
static duk_ret_t js_get_version(duk_context *ctx)
{
    duk_push_string(ctx, "1.0.0");
    return 1;
}

/*======================================
 * 模块初始化 / Module Initialization
 *======================================*/

/* 前向声明：定义在文件末尾，先于方法表使用
 * Forward declarations: defined below, used in the method table above */
static duk_ret_t js_set_lang(duk_context *ctx);
static duk_ret_t js_get_lang(duk_context *ctx);

/* retro_ui 模块的方法表 / retro_ui module method table */
static const duk_function_list_entry retro_ui_funcs[] = {
    { "msgbox",       js_msgbox,       2 },   /* title, msg */
    { "input",        js_input,        3 },   /* title, prompt, bufsize */
    { "list",         js_list,         3 },   /* title, prompt, items[] */
    { "confirm",      js_confirm,      2 },   /* title, msg */
    { "status",       js_status,       1 },   /* msg */
    { "progress",     js_progress,     2 },   /* value, max */
    { "close_window", js_close_window, 1 },   /* title */
    { "get_version",  js_get_version,  0 },   /* () */
    { "set_lang",    js_set_lang,      1 },   /* lang */
    { "get_lang",    js_get_lang,      0 },   /* () */
    { NULL, NULL, 0 }
};

/* retro_ui 模块的属性表 / retro_ui module property table */
static const duk_number_list_entry retro_ui_numbers[] = {
    { NULL, 0.0 }
};

/**
 * retro_ui_js_init - 初始化 retro_ui JS 模块 / Initialize retro_ui JS module
 * @ctx: Duktape 上下文 / Duktape context
 *
 * 在 Duktape 运行时中注册 retro_ui 全局对象
 * Registers retro_ui global object in Duktape runtime
 */
void retro_ui_js_init(duk_context *ctx)
{
    if (!ctx) return;

    /* 初始化 C 核心 / Initialize C core */
#ifdef CONFIG_LVGL
    retro_ui_init();
#endif

    /* 压入 retro_ui 对象 / Push retro_ui object */
    duk_push_object(ctx);

    /* 设置方法列表 / Set method list */
    duk_put_function_list(ctx, -1, retro_ui_funcs);

    /* 设置属性列表 / Set property list */
    duk_put_number_list(ctx, -1, retro_ui_numbers);

    /* 设置版本属性 / Set version property */
    duk_push_string(ctx, "1.0.0");
    duk_put_prop_string(ctx, -2, "version");

    /* 注册为全局对象 / Register as global object */
    duk_put_global_string(ctx, "retro_ui");

    syslog(LOG_INFO, "[retro_ui_js] module initialized\n");
}

/**
 * retro_ui_js_deinit - 清理 retro_ui JS 模块 / Cleanup retro_ui JS module
 */
void retro_ui_js_deinit(duk_context *ctx)
{
    (void)ctx;
#ifdef CONFIG_LVGL
    retro_ui_deinit();
#endif
    syslog(LOG_INFO, "[retro_ui_js] module deinitialized\n");
}

/*======================================
 * JS: retro_ui.set_lang(lang)
 * 设置 UI 语言 / Set UI language
 *======================================*/
static duk_ret_t js_set_lang(duk_context *ctx)
{
    const char *lang = js_to_c_string(ctx, 0);
    int result = retro_ui_set_lang(lang);
    duk_push_int(ctx, result);
    return 1;
}

/*======================================
 * JS: retro_ui.get_lang()
 * 获取当前语言 / Get current language
 *======================================*/
static duk_ret_t js_get_lang(duk_context *ctx)
{
    const char *lang = retro_ui_get_lang();
    duk_push_string(ctx, lang ? lang : "unknown");
    return 1;
}
