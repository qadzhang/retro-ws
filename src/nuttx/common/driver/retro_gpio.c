/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio.c - 脚本引擎 GPIO 统一接口实现
 *
 * WHAT : 策略层（占用拦截）+ 后端层（NuttX 设备驱动）实现
 * WHY  : 教学场景要"能操作"，系统稳定性要"不越界"——两层各司其职
 * WHO  : retro_gpio_{bas,js,berry,py}.c 经 retro_gpio.h 调用
 * WHERE: retro-ws/src/nuttx/common/driver/retro_gpio.c
 * WHEN : 2026-10-04 新增；2026-10-04 按 NuttX 12.12 真实头文件
 *        （include/nuttx/ioexpancer/gpio.h 等）修正 ioctl 用法
 * HOW  : 每个入口先查 g_retro_gpio_occupied（含 reason 提示），
 *        命中即 printf 双语"已占用"并返回 -EBUSY；
 *        通过后经 /dev/gpioN（GPIOC_* ioctl，NuttX 12.12 语义：
 *        WRITE 传 0/1、READ 传 bool*、SETPINTYPE 传枚举值）、
 *        /dev/adc0（read 返回 adc_msg_s）、/dev/pwmN（PWMIOC_*）。
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/ioexpander/gpio.h>   /* GPIOC_* / enum gpio_pintype_e */
#include <nuttx/timers/pwm.h>        /* PWMIOC_* / struct pwm_info_s */
#include <nuttx/analog/adc.h>        /* struct adc_msg_s */

#include "retro_gpio.h"

/*==========================
 *  策略层 / Policy: 占用拦截
 *==========================*/

/*
 * WHAT : 查占用表并拦截 / check occupied table and reject
 * WHY  : 第三方脚本触碰 CVBS/SD/Flash 等系统引脚会破坏显示与存储
 * WHO  : 所有 retro_gpio_* 入口（config/write/read/pwm/release）
 * WHERE: 本文件
 * WHEN : 2026-10-04 新增
 * HOW  : 命中打印双语提示（含占用原因），返回 true 表示被拦截
 */
static bool gpio_is_blocked(int pin)
{
    for (int i = 0; i < g_retro_gpio_occupied_count; i++) {
        if (g_retro_gpio_occupied[i].pin == pin) {
            printf("GPIO%d 已被系统占用 / occupied: %s\n",
                   pin, g_retro_gpio_occupied[i].reason);
            syslog(LOG_WARNING,
                   "[retro_gpio] blocked pin %d (%s)\n",
                   pin, g_retro_gpio_occupied[i].reason);
            return true;
        }
    }
    return false;
}

/*==========================
 *  后端层 / Backend: 设备驱动访问
 *==========================*/

/*
 * WHAT : 打开目标引脚的通用 GPIO 字符设备
 * WHY  : NuttX CONFIG_DEV_GPIO 把每个引脚注册为 /dev/gpioN（N=引脚号）
 * HOW  : snprintf 拼路径后 open(O_RDWR)；失败返回负 errno 由调用者转译
 */
static int gpio_dev_open(int pin)
{
    char path[24];

    if (pin < 0 || pin > 99)
        return -EINVAL;

    snprintf(path, sizeof(path), "/dev/gpio%d", pin);
    int fd = open(path, O_RDWR);
    return fd < 0 ? -errno : fd;
}

/*
 * WHAT : 把脚本文本模式映射为 NuttX 引脚类型枚举
 * WHY  : 脚本侧用可读字符串（in/out/in_pu/in_pd），驱动侧用枚举
 * HOW  : 逐一 strcmp；NuttX 12.12 枚举值四种输入模式恒存在，无需 ifdef
 */
static int gpio_pintype_from_mode(const char *mode)
{
    if (!mode)
        return -1;

    if (strcmp(mode, RETRO_GPIO_MODE_OUT) == 0)
        return GPIO_OUTPUT_PIN;
    if (strcmp(mode, RETRO_GPIO_MODE_IN) == 0)
        return GPIO_INPUT_PIN;
    if (strcmp(mode, RETRO_GPIO_MODE_IN_PU) == 0)
        return GPIO_INPUT_PIN_PULLUP;
    if (strcmp(mode, RETRO_GPIO_MODE_IN_PD) == 0)
        return GPIO_INPUT_PIN_PULLDOWN;
    return -1;
}

/*==========================
 *  统一接口实现 / Public API
 *==========================*/

int retro_gpio_init(void)
{
    syslog(LOG_INFO, "[retro_gpio] ready, %d pins occupied by system\n",
           g_retro_gpio_occupied_count);
    return OK;
}

int retro_gpio_config(int pin, const char *mode)
{
    if (pin < 0 || !mode)
        return -EINVAL;

    if (gpio_is_blocked(pin))
        return -EBUSY;              /* 系统占用：直接报错返回已占用 */

    int ptype = gpio_pintype_from_mode(mode);
    if (ptype < 0) {
        printf("未知模式 / unknown mode: %s (in/out/in_pu/in_pd)\n", mode);
        return -EINVAL;
    }

    int fd = gpio_dev_open(pin);
    if (fd < 0)
        return -ENOENT;             /* GPIO 驱动未使能或该脚未注册 */

    /* NuttX 12.12：SETPINTYPE 的 ioctl 参数是枚举值本身 */
    int ret = ioctl(fd, GPIOC_SETPINTYPE, (unsigned long)ptype);
    close(fd);
    return ret < 0 ? -EIO : OK;
}

int retro_gpio_write(int pin, int value)
{
    if (pin < 0)
        return -EINVAL;

    if (gpio_is_blocked(pin))
        return -EBUSY;

    int fd = gpio_dev_open(pin);
    if (fd < 0)
        return -ENOENT;

    /* NuttX 12.12：GPIOC_WRITE 的参数就是 0/1 电平值 */
    int ret = ioctl(fd, GPIOC_WRITE, value ? 1UL : 0UL);
    close(fd);
    return ret < 0 ? -EIO : OK;
}

int retro_gpio_read(int pin)
{
    if (pin < 0)
        return -EINVAL;

    if (gpio_is_blocked(pin))
        return -EBUSY;

    int fd = gpio_dev_open(pin);
    if (fd < 0)
        return -ENOENT;

    /* NuttX 12.12：GPIOC_READ 的参数是 bool* 出参 */
    bool val = false;
    int ret = ioctl(fd, GPIOC_READ, (unsigned long)&val);
    close(fd);

    if (ret < 0)
        return -EIO;
    return val ? 1 : 0;
}

int retro_gpio_adc_read(int channel)
{
    if (channel < 0)
        return -EINVAL;

    /* ADC 不与 GPIO 表冲突（专用引脚复用），直接转发 /dev/adc0 */
    int fd = open("/dev/adc0", O_RDONLY);
    if (fd < 0)
        return -ENOENT;

    struct adc_msg_s msg;
    ssize_t ret = read(fd, &msg, sizeof(msg));
    close(fd);

    if (ret != (ssize_t)sizeof(msg))
        return -EIO;
    return (int)msg.am_data;
}

int retro_gpio_pwm_set(int pin, int duty, int freq)
{
    if (pin < 0)
        return -EINVAL;

    if (gpio_is_blocked(pin))
        return -EBUSY;

    if (duty < 0 || duty > 100 || freq <= 0)
        return -EINVAL;

    char path[24];
    snprintf(path, sizeof(path), "/dev/pwm%d", pin);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -ENOENT;             /* 该脚无 PWM 通道 */

    struct pwm_info_s info;
    memset(&info, 0, sizeof(info));
    info.frequency = (uint32_t)freq;
    info.duty = (uint16_t)((duty * 65535) / 100);

    int ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
                    (unsigned long)&info);
    if (ret >= 0)
        ret = ioctl(fd, PWMIOC_START, 0);
    close(fd);
    return ret < 0 ? -EIO : OK;
}

int retro_gpio_release(int pin)
{
    if (pin < 0)
        return -EINVAL;

    /* 脚本释放引脚（恢复输入态）；系统占用脚同样禁止操作 */
    if (gpio_is_blocked(pin))
        return -EBUSY;

    int fd = gpio_dev_open(pin);
    if (fd < 0)
        return -ENOENT;

    int ret = ioctl(fd, GPIOC_SETPINTYPE,
                    (unsigned long)GPIO_INPUT_PIN);
    close(fd);
    return ret < 0 ? -EIO : OK;
}
