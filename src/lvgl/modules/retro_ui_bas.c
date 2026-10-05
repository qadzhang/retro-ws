/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui_bas.c - retro_ui 的 BASIC 绑定
 *
 * WHAT : retro_ui 的 BASIC 绑定
 * WHY  : my_basic 脚本调用 LVGL UI（AGENTS.md 8.1）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/modules/retro_ui_bas.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 注册 CALL retro_ui_* 原生函数
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

#if __has_include(<my_basic.h>)
#  include <my_basic.h>
#else
#  include "deps/my_basic/core/my_basic.h"
#endif

/* retro_ui C 核心接口 / retro_ui C core interface */
#include "retro_ui.h"

/*======================================
 * my-basic 函数注册
 *======================================*/

static int bas_msgbox(struct mb_interpreter_t *s, void **l)
{
    char *title = NULL;
    char *msg = NULL;
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &title);
    }
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &msg);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] msgbox: %s\n", title ? title : "");
    
#ifdef CONFIG_LVGL
    retro_ui_msgbox(title ? title : "", msg ? msg : "");
#endif

    return MB_FUNC_OK;
}

static int bas_input(struct mb_interpreter_t *s, void **l)
{
    char *title = NULL;
    char *prompt = NULL;
    int bufsize = 64;
    static char input_buf[256];
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &title);
    }
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &prompt);
    }
    if (mb_has_arg(s, l)) {
        mb_pop_int(s, l, &bufsize);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] input: %s\n", title ? title : "");
    
#ifdef CONFIG_LVGL
    if (bufsize > (int)sizeof(input_buf)) bufsize = sizeof(input_buf);
    retro_ui_input(title ? title : "", prompt ? prompt : "", input_buf, bufsize);
#else
    input_buf[0] = '\0';
#endif

    mb_push_string(s, l, input_buf);
    return MB_FUNC_OK;
}

static int bas_confirm(struct mb_interpreter_t *s, void **l)
{
    char *title = NULL;
    char *msg = NULL;
    int result = 0;
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &title);
    }
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &msg);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] confirm: %s\n", title ? title : "");
    
#ifdef CONFIG_LVGL
    result = retro_ui_confirm(title ? title : "", msg ? msg : "");
#endif

    mb_push_int(s, l, result);
    return MB_FUNC_OK;
}

static int bas_status(struct mb_interpreter_t *s, void **l)
{
    char *msg = NULL;
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &msg);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] status: %s\n", msg ? msg : "");
    
#ifdef CONFIG_LVGL
    retro_ui_status(msg ? msg : "");
#endif

    return MB_FUNC_OK;
}

static int bas_progress(struct mb_interpreter_t *s, void **l)
{
    int value = 0;
    int max = 100;
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_int(s, l, &value);
    }
    if (mb_has_arg(s, l)) {
        mb_pop_int(s, l, &max);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] progress: %d/%d\n", value, max);
    
#ifdef CONFIG_LVGL
    retro_ui_progress(value, max);
#endif

    return MB_FUNC_OK;
}

static int bas_set_lang(struct mb_interpreter_t *s, void **l)
{
    char *lang = NULL;
    int result = 0;
    
    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) {
        mb_pop_string(s, l, &lang);
    }
    mb_attempt_func_end(s, l);

    syslog(LOG_INFO, "[retro_ui_bas] set_lang: %s\n", lang ? lang : "");
    
#ifdef CONFIG_LVGL
    result = retro_ui_set_lang(lang ? lang : "");
#endif

    mb_push_int(s, l, result);
    return MB_FUNC_OK;
}

static int bas_get_lang(struct mb_interpreter_t *s, void **l)
{
    const char *lang = "zh_CN";
    
    mb_attempt_func_begin(s, l);
    mb_attempt_func_end(s, l);

#ifdef CONFIG_LVGL
    lang = retro_ui_get_lang();
    if (!lang) lang = "zh_CN";
#endif

    mb_push_string(s, l, (char*)lang);
    return MB_FUNC_OK;
}

/*======================================
 * 模块注册函数
 *======================================*/

void retro_ui_bas_register(struct mb_interpreter_t *s)
{
    if (!s) return;

    syslog(LOG_INFO, "[retro_ui_bas] registering functions\n");

#ifdef CONFIG_LVGL
    retro_ui_init();
#endif

    mb_register_func(s, "retro_ui_msgbox", bas_msgbox);
    mb_register_func(s, "retro_ui_input", bas_input);
    mb_register_func(s, "retro_ui_confirm", bas_confirm);
    mb_register_func(s, "retro_ui_status", bas_status);
    mb_register_func(s, "retro_ui_progress", bas_progress);
    mb_register_func(s, "retro_ui_set_lang", bas_set_lang);
    mb_register_func(s, "retro_ui_get_lang", bas_get_lang);

    syslog(LOG_INFO, "[retro_ui_bas] registration complete\n");
}

void retro_ui_bas_unregister(void)
{
#ifdef CONFIG_LVGL
    retro_ui_deinit();
#endif
    syslog(LOG_INFO, "[retro_ui_bas] unregistered\n");
}
