/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio_js.c - Duktape JS 的 GPIO 绑定
 *
 * WHAT : JS 脚本 retro_gpio.* 对象（config/write/read/adc/pwm/release）
 * WHY  : 教学设备核心能力，统一走 retro_gpio.h（占用脚报"已占用"）
 * WHO  : script_engines.c 的 duk_init() 调用 retro_gpio_js_init()
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/retro_gpio_js.c
 * WHEN : 2026-10-04 新增
 * HOW  : duk_push_c_function 注册到全局对象 retro_gpio
 *
 * 用法：
 *   retro_gpio.config(12, "out");
 *   retro_gpio.write(12, 1);
 *   if (retro_gpio.read(8) === 0) { ... }
 *   var a = retro_gpio.adc(0);
 *   retro_gpio.pwm(12, 50, 1000);
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_DUKTAPE

#include <syslog.h>
#include <string.h>

#if __has_include(<duktape.h>)
#  include <duktape.h>
#else
#  include "duktape/duktape.h"
#endif
#include "retro_gpio.h"

static duk_ret_t js_gpio_config(duk_context *ctx)
{
    int pin = (int)duk_get_int(ctx, 0);
    const char *mode = duk_get_string(ctx, 1);
    duk_push_int(ctx, retro_gpio_config(pin, mode ? mode : "in"));
    return 1;
}

static duk_ret_t js_gpio_write(duk_context *ctx)
{
    duk_push_int(ctx, retro_gpio_write((int)duk_get_int(ctx, 0),
                                       (int)duk_get_int(ctx, 1)));
    return 1;
}

static duk_ret_t js_gpio_read(duk_context *ctx)
{
    duk_push_int(ctx, retro_gpio_read((int)duk_get_int(ctx, 0)));
    return 1;
}

static duk_ret_t js_gpio_adc(duk_context *ctx)
{
    duk_push_int(ctx, retro_gpio_adc_read((int)duk_get_int(ctx, 0)));
    return 1;
}

static duk_ret_t js_gpio_pwm(duk_context *ctx)
{
    duk_push_int(ctx, retro_gpio_pwm_set((int)duk_get_int(ctx, 0),
                                         (int)duk_get_int(ctx, 1),
                                         (int)duk_get_int(ctx, 2)));
    return 1;
}

static duk_ret_t js_gpio_release(duk_context *ctx)
{
    duk_push_int(ctx, retro_gpio_release((int)duk_get_int(ctx, 0)));
    return 1;
}

static const duk_function_list_entry retro_gpio_funcs[] = {
    { "config",  js_gpio_config,  2 },   /* pin, mode */
    { "write",   js_gpio_write,   2 },   /* pin, value */
    { "read",    js_gpio_read,    1 },   /* pin -> 0/1 */
    { "adc",     js_gpio_adc,     1 },   /* channel -> 原始值 */
    { "pwm",     js_gpio_pwm,     3 },   /* pin, duty%, freq */
    { "release", js_gpio_release, 1 },   /* pin */
    { NULL, NULL, 0 }
};

void retro_gpio_js_init(duk_context *ctx)
{
    if (!ctx)
        return;

    retro_gpio_init();

    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, retro_gpio_funcs);
    duk_put_global_string(ctx, "retro_gpio");

    syslog(LOG_INFO, "[retro_gpio_js] registered\n");
}

#endif /* {guard} */
