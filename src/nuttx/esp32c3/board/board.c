/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * board.c - 合宙 ESP32-C3 核心板板级初始化
 *
 * WHAT : esp32c3 目标上电外设就位（GPIO/SD/控制台）
 * WHY  : CLI 工作站最小系统初始化
 * WHO  : esp32c3_retro.c 启动时调用
 * WHERE: esp32-retro-ws/src/nuttx/esp32c3/board/board.c
 * WHEN : 2026-10-04 新增（骨架，待 deps 就绪后随 NuttX 初始化流程完善）
 * HOW  : 按 hw_esp32c3_luatos.h 引脚表配置；LED 状态指示低电平点亮
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <stdint.h>
#include <errno.h>

#include "board.h"

int esp32c3_board_init(void)
{
    syslog(LOG_INFO, "[c3-board] Luatos ESP32-C3 core board init\n");

    /* TODO: 随 NuttX 初始化流程接入（首次编译时完善）：
     *   1. 状态 LED（GPIO12，低电平点亮）GPIO 输出配置
     *   2. SD 卡 SPI（GPIO4/5/6/7）初始化 + /sdcard 挂载
     *   3. 控制台：经典款 UART0 / 简约款原生 USB-JTAG（Kconfig 选择）
     */
    return OK;
}

/*==========================
 *  脚本 GPIO 占用表实例化 / script GPIO occupied table instance
 *
 * WHAT : 把硬件档案的 RETRO_GPIO_OCCUPIED_LIST 落成 retro_gpio 查询的表
 * WHY  : 第三方脚本访问系统占用脚时直接报"已占用"返回 -EBUSY
 * WHO  : retro_gpio.c 的 gpio_is_blocked()
 * WHERE: 本文件（数据来源 hw_*.h 硬件档案）
 * WHEN : 2026-10-04 新增
 * HOW  : 表内容见硬件档案，改引脚先改 HARDWARE.md 与档案，再同步此处
 *==========================*/

#include "driver/retro_gpio.h"

const struct retro_gpio_occ_s g_retro_gpio_occupied[] = {
    RETRO_GPIO_OCCUPIED_LIST
};

const int g_retro_gpio_occupied_count =
    sizeof(g_retro_gpio_occupied) / sizeof(g_retro_gpio_occupied[0]);
