/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * board.c - ESP32-CAM 板级初始化
 *
 * WHAT : ESP32-CAM 板级初始化
 * WHY  : GPIO/SPI/外设时钟就位
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/board/board.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 按 hw_esp32cam_aithinker.h 引脚表配置
 */

/**
 * board.c - ESP32-CAM 板级初始化
 *
 * ESP32-CAM (AI-Thinker) 板级初始化：
 * - Flash 4MB (QSPI), PSRAM 4MB (QSPI)
 * - 内置 DAC: GPIO25 (CVBS), GPIO26 (Audio)
 * - SPI SD 卡, BLE HID 输入
 * - OV2640 摄像头（与 DAC 共用 GPIO25/26）
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include "esp32.h"
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

/*==========================
 *  全局变量
 *==========================*/

static uint32_t g_reset_reason = 0;

/*==========================
 *  内部函数
 *==========================*/

static uint32_t esp32_get_reset_reason(void)
{
    /* ESP32 复位原因在 RTC_CNTL_OPTIONS0_REG */
    return getreg32(ESP32_RTC_CNTL_RESET_STATE_REG) & 0x1F;
}

static void print_reset_reason(uint32_t reason)
{
    const char *str = "UNKNOWN";

    switch (reason) {
        case 1:  str = "POWERON_RESET";       break;
        case 2:  str = "SW_RESET";            break;
        case 3:  str = "OWDT_RESET";          break;
        case 4:  str = "DEEPSLEEP_RESET";     break;
        case 6:  str = "TG0WDT_SYS_RESET";    break;
        case 7:  str = "TG1WDT_SYS_RESET";    break;
        case 8:  str = "RTCWDT_SYS_RESET";    break;
        case 11: str = "SW_CPU_RESET";        break;
        case 12: str = "RTCWDT_CPU_RESET";    break;
        case 14: str = "RTCWDT_BROWN_OUT";    break;
        case 15: str = "RTCWDT_RTC_RESET";    break;
        default: str = "UNKNOWN";              break;
    }

    board_info("Reset reason: %s (0x%02lx)\n", str, (unsigned long)reason);
}

static void print_board_info(void)
{
    board_info("========================================\n");
    board_info("ESP32-CAM 复古联网图形工作站\n");
    board_info("Board: ESP32-CAM (AI-Thinker)\n");
    board_info("Chip: ESP32 (Xtensa LX6 Dual-Core)\n");
    board_info("Flash: 4 MB (QSPI)\n");
    board_info("PSRAM: 4 MB (QSPI)\n");
    board_info("DAC: CH1=GPIO25, CH2=GPIO26\n");
    board_info("========================================\n");
}

/*==========================
 *  公开函数
 *==========================*/






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
