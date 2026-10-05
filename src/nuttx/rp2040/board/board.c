/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * board.c - Pico 板级实现
 *
 * WHAT : 占用表实例化 + 板信息接口（NuttX raspberrypi-pico 板已
 *        处理时钟/UART/flash 初始化，本文件只补项目层）
 * WHY  : 符号唯一归属；脚本 GPIO 拦截数据源
 * WHERE: esp32-retro-ws/src/nuttx/rp2040/board/board.c
 * WHEN : 2026-10-04 新增
 * HOW  : RETRO_GPIO_OCCUPIED_LIST 落表；复位原因走 NuttX 接口
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <stdio.h>

#include "board.h"

/*==========================
 *  板信息 / reset reason
 *==========================*/




/*==========================
 *  脚本 GPIO 占用表实例化 / occupied table instance
 *==========================*/

#include "driver/retro_gpio.h"

const struct retro_gpio_occ_s g_retro_gpio_occupied[] = {
    RETRO_GPIO_OCCUPIED_LIST
};

const int g_retro_gpio_occupied_count =
    sizeof(g_retro_gpio_occupied) / sizeof(g_retro_gpio_occupied[0]);
