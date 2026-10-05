/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio_berry.c - Berry 的 GPIO 绑定
 *
 * WHAT : Berry 脚本 retro_gpio_* 全局函数（教学 GPIO/ADC/PWM）
 * WHY  : 教学设备核心能力，统一走 retro_gpio.h（占用脚报"已占用"）
 * WHO  : script_engines.c 的 berry_init() 调用 retro_gpio_berry_init()
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/retro_gpio_berry.c
 * WHEN : 2026-10-04 新增
 * HOW  : be_regfunc 注册全局函数（CLI 目标 C3 也可用）
 *
 * 用法：
 *   retro_gpio_config(12, "out")
 *   retro_gpio_write(12, 1)
 *   if retro_gpio_read(8) == 0 ... end
 *   retro_gpio_pwm(12, 50, 1000)
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_BERRY

#include <syslog.h>
#include <string.h>

#include "berry.h"
#include "retro_gpio.h"

static int be_arg_int(bvm *vm, int idx)
{
    if (idx > be_top(vm) || !be_isnumber(vm, idx))
        return -1;
    return be_toint(vm, idx);
}

static int be_gpio_config(bvm *vm)
{
    const char *mode = (be_top(vm) >= 2) ? be_tostring(vm, 2) : "in";
    be_pushint(vm, retro_gpio_config(be_arg_int(vm, 1), mode));
    be_return(vm);
}

static int be_gpio_write(bvm *vm)
{
    be_pushint(vm, retro_gpio_write(be_arg_int(vm, 1), be_arg_int(vm, 2)));
    be_return(vm);
}

static int be_gpio_read(bvm *vm)
{
    be_pushint(vm, retro_gpio_read(be_arg_int(vm, 1)));
    be_return(vm);
}

static int be_gpio_adc(bvm *vm)
{
    be_pushint(vm, retro_gpio_adc_read(be_arg_int(vm, 1)));
    be_return(vm);
}

static int be_gpio_pwm(bvm *vm)
{
    be_pushint(vm, retro_gpio_pwm_set(be_arg_int(vm, 1),
                                      be_arg_int(vm, 2),
                                      be_arg_int(vm, 3)));
    be_return(vm);
}

static int be_gpio_release(bvm *vm)
{
    be_pushint(vm, retro_gpio_release(be_arg_int(vm, 1)));
    be_return(vm);
}

void retro_gpio_berry_init(bvm *vm)
{
    if (!vm)
        return;

    retro_gpio_init();

    be_regfunc(vm, "retro_gpio_config",  be_gpio_config);
    be_regfunc(vm, "retro_gpio_write",   be_gpio_write);
    be_regfunc(vm, "retro_gpio_read",    be_gpio_read);
    be_regfunc(vm, "retro_gpio_adc",     be_gpio_adc);
    be_regfunc(vm, "retro_gpio_pwm",     be_gpio_pwm);
    be_regfunc(vm, "retro_gpio_release", be_gpio_release);

    syslog(LOG_INFO, "[retro_gpio_berry] registered\n");
}

#endif /* {guard} */
