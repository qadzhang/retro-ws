/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio.h - 脚本引擎 GPIO 统一接口 / script GPIO glue API
 *
 * WHAT : 面向全部脚本引擎的 GPIO/ADC/PWM 读写接口（教学定位）
 * WHY  : 复古教学设备的核心能力——脚本直接点灯/读按键/呼吸灯，
 *        无需写 C；与 retro_ui 同构的胶水层（AGENTS.md 8.1 模式）
 * WHO  : retro_gpio_{bas,js,berry,py}.c 各引擎绑定转发到本接口
 * WHERE: retro-ws/src/nuttx/common/driver/retro_gpio.[ch]
 * WHEN : 2026-10-04 新增
 * HOW  : 策略层 + 后端层：
 *   - 策略层：查目标板"系统占用引脚表"（各板 board.c 提供的
 *     g_retro_gpio_occupied），占用引脚一律拒绝并打印"已占用"提示，
 *     返回 -EBUSY —— 第三方脚本无法触碰系统引脚（CVBS/SD/USB/...）
 *   - 后端层：对接 NuttX 通用设备驱动（/dev/gpioN、/dev/adc0、/dev/pwmN），
 *     首次编译时核对 ioctl 头文件路径（见 NEXT_STEPS）
 *
 * 用法示例（BASIC）：
 *   CALL retro_gpio_config(8, "in")
 *   CALL retro_gpio_write(12, 1)
 *   IF retro_gpio_read(8) = 0 THEN ...
 */

#ifndef __RETRO_GPIO_H
#define __RETRO_GPIO_H

#include <nuttx/config.h>

/* GPIO 模式字符串（retro_gpio_config 的 mode 参数） */
#define RETRO_GPIO_MODE_OUT    "out"     /* 推挽输出 */
#define RETRO_GPIO_MODE_IN     "in"      /* 浮空输入 */
#define RETRO_GPIO_MODE_IN_PU  "in_pu"   /* 上拉输入 */
#define RETRO_GPIO_MODE_IN_PD  "in_pd"   /* 下拉输入 */

/* 系统占用引脚描述（各板 board.c 实例化，来源为硬件档案） */
struct retro_gpio_occ_s {
    int         pin;      /* GPIO 编号 */
    const char *reason;   /* 占用原因（错误提示展示给脚本用户） */
};

/* 各目标板提供的占用表（单一定义，链接期解析） */
extern const struct retro_gpio_occ_s g_retro_gpio_occupied[];
extern const int                    g_retro_gpio_occupied_count;

/*==== 统一接口（AGENTS.md 8.1 扩展，五种引擎同名映射）====*/

int retro_gpio_init(void);
int retro_gpio_config(int pin, const char *mode);
int retro_gpio_write(int pin, int value);
int retro_gpio_read(int pin);
int retro_gpio_adc_read(int channel);
int retro_gpio_pwm_set(int pin, int duty, int freq);
int retro_gpio_release(int pin);

#endif /* __RETRO_GPIO_H */
