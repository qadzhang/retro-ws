/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * board.c - ESP32-S3 板级初始化
 *
 * WHAT : ESP32-S3 板级初始化
 * WHY  : GPIO/SPI/外设时钟就位
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/board/board.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 按 hw_esp32s3_devkitc.h 引脚表配置
 */

/**
 * board.c - ESP32-S3 开发板初始化
 *
 * 主要任务：
 * 1. 初始化 Flash 和 PSRAM
 * 2. 配置 GPIO
 * 3. 初始化外设（SDIO、I2C、I2S）
 * 4. 设置中断
 * 5. 打印板信息
 *
 * 注意：NuttX 的板级初始化分为三个阶段：
 *   - board_early_initialize(): 内核启动前，基础硬件
 *   - board_late_initialize():  内核启动后，NSH 之前
 *   - 应用初始化: NSH 启动后
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/boardctl.h>

#include "esp32s3.h"
#include "board.h"

/*==========================
 *  调试
 *==========================*/

#define BOARD_DEBUG 1

#if BOARD_DEBUG
#  define board_debug(fmt, ...) syslog(LOG_DEBUG, "[BOARD] " fmt, ##__VA_ARGS__)
#else
#  define board_debug(fmt, ...) ((void)0)
#endif

#define board_info(fmt, ...)  syslog(LOG_INFO,  "[BOARD] " fmt, ##__VA_ARGS__)
#define board_err(fmt, ...)   syslog(LOG_ERR,    "[BOARD] " fmt, ##__VA_ARGS__)

/*==========================
 *  全局变量
 *==========================*/

/*==========================
 *  内部函数
 *==========================*/

/**
 * 获取重启原因
 * ESP32-S3 的复位原因在 RTC 域（SIMPLE_CHIP_RESET_REASON）
 */
/**
 * 打印重启原因
 */
static void print_reset_reason(uint32_t reason)
{
    const char *str = "UNKNOWN";

    switch (reason) {
        case 0:  str = "POWERON";         break;
        case 1:  str = "SW_RESET";        break;
        case 3:  str = "GLITCH";          break;
        case 4:  str = "MWDT0_RST";       break;
        case 5:  str = "MWDT1_RST";       break;
        case 6:  str = "RTC_WDT_RST";     break;
        case 7:  str = "RTC_SW_RST";      break;
        case 8:  str = "BROWN_OUT";       break;
        case 9:  str = "CPU_UNLOCK";      break;
        case 10: str = "APPCPU_RESET";    break;
        case 11: str = "DEEPSLEEP";       break;
        case 12: str = "SW_CPU_RST";      break;
        case 14: str = "RTC_WDT_CPU";     break;
        default: str = "UNKNOWN";          break;
    }

    board_info("Reset reason: %s (0x%02lx)\n", str, (unsigned long)reason);
}

/**
 * 打印板信息
 */
/*==========================
 *  公开函数
 *==========================*/

/**
 * 板级早期初始化 — NuttX 内核启动前调用
 *
 * 基础硬件初始化：时钟、Flash、PSRAM
 * NuttX 的 esp32s3_start.c 已处理大部分基础初始化，
 * 此函数仅做项目特定的补充。
 */

/* board_late_initialize / reset reason 归 NuttX 板级（esp32s3_boot.c、
 * esp32s3_reset.c）；本文件只提供占用表与项目侧装配数据 */

/**
 * 判断是否为看门狗重启
 */

/**
 * 获取固件版本
 */

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
