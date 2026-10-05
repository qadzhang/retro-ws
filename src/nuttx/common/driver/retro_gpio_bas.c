/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio_bas.c - my-basic 的 GPIO 绑定
 *
 * WHAT : BASIC 脚本 retro_gpio_* 函数（教学点灯/读按键/呼吸灯）
 * WHY  : 教学设备核心能力，统一走 retro_gpio.h（占用脚报"已占用"）
 * WHO  : script_engines.c 的 mybasic_init() 调用 retro_gpio_bas_register()
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/retro_gpio_bas.c
 * WHEN : 2026-10-04 新增
 * HOW  : mb_register_func 注册；读函数经 mb_push_int 返回值
 *
 * 用法：
 *   CALL retro_gpio_config(12, "out")
 *   CALL retro_gpio_write(12, 1)
 *   IF retro_gpio_read(8) = 0 THEN ...
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_TINYBASIC

#include <syslog.h>
#include <string.h>

#if __has_include(<my_basic.h>)
#  include <my_basic.h>
#else
#  include "deps/my_basic/core/my_basic.h"
#endif
#include "retro_gpio.h"

/* CALL retro_gpio_config(pin, mode$) */
static int bas_gpio_config(struct mb_interpreter_t *s, void **l)
{
    int pin = -1;
    char *mode = NULL;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &pin);
    if (mb_has_arg(s, l)) mb_pop_string(s, l, &mode);
    mb_attempt_func_end(s, l);

    return retro_gpio_config(pin, mode ? mode : "") < 0 ? MB_FUNC_ERR
                                                         : MB_FUNC_OK;
}

/* CALL retro_gpio_write(pin, value) */
static int bas_gpio_write(struct mb_interpreter_t *s, void **l)
{
    int pin = -1, value = 0;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &pin);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &value);
    mb_attempt_func_end(s, l);

    return retro_gpio_write(pin, value) < 0 ? MB_FUNC_ERR : MB_FUNC_OK;
}

/* v = retro_gpio_read(pin) */
static int bas_gpio_read(struct mb_interpreter_t *s, void **l)
{
    int pin = -1, value = -1;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &pin);
    mb_attempt_func_end(s, l);

    value = retro_gpio_read(pin);
    mb_push_int(s, l, value);
    return value < 0 ? MB_FUNC_ERR : MB_FUNC_OK;
}

/* v = retro_gpio_adc(channel) */
static int bas_gpio_adc(struct mb_interpreter_t *s, void **l)
{
    int ch = -1, value = -1;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &ch);
    mb_attempt_func_end(s, l);

    value = retro_gpio_adc_read(ch);
    mb_push_int(s, l, value);
    return value < 0 ? MB_FUNC_ERR : MB_FUNC_OK;
}

/* CALL retro_gpio_pwm(pin, duty, freq) */
static int bas_gpio_pwm(struct mb_interpreter_t *s, void **l)
{
    int pin = -1, duty = 0, freq = 1000;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &pin);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &duty);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &freq);
    mb_attempt_func_end(s, l);

    return retro_gpio_pwm_set(pin, duty, freq) < 0 ? MB_FUNC_ERR
                                                    : MB_FUNC_OK;
}

/* CALL retro_gpio_release(pin) */
static int bas_gpio_release(struct mb_interpreter_t *s, void **l)
{
    int pin = -1;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &pin);
    mb_attempt_func_end(s, l);

    return retro_gpio_release(pin) < 0 ? MB_FUNC_ERR : MB_FUNC_OK;
}

void retro_gpio_bas_register(struct mb_interpreter_t *s)
{
    if (!s)
        return;

    mb_register_func(s, "retro_gpio_config",  bas_gpio_config);
    mb_register_func(s, "retro_gpio_write",   bas_gpio_write);
    mb_register_func(s, "retro_gpio_read",    bas_gpio_read);
    mb_register_func(s, "retro_gpio_adc",     bas_gpio_adc);
    mb_register_func(s, "retro_gpio_pwm",     bas_gpio_pwm);
    mb_register_func(s, "retro_gpio_release", bas_gpio_release);

    syslog(LOG_INFO, "[retro_gpio_bas] registered\n");
}

#endif /* {guard} */
