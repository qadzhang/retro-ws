/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: lvgl_app.c
 * 描述: LVGL 应用程序初始化入口
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */

/*
 * lvgl_app.c - LVGL 应用入口
 *
 * WHAT : LVGL 应用入口
 * WHY  : LVGL 初始化装配（显示/输入/桌面）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/lvgl_app.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : lv_port_disp + lv_port_indev + retro_desktop_init 顺序装配
 */

#include <nuttx/config.h>
#include <syslog.h>

#ifdef CONFIG_LVGL

#include <lvgl/lvgl.h>
#include "i18n.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

void lvgl_init(void)
{
    syslog(LOG_INFO, "[LVGL] Initializing LVGL v%d.%d.%d...\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();

    /* NuttX 系统时钟作为 LVGL tick 源（无它定时器永不触发） */
    {
        extern uint32_t retro_lv_tick_cb(void);
        lv_tick_set_cb(retro_lv_tick_cb);
    }

    i18n_init();

    lv_port_disp_init();

    lv_port_indev_init();

    syslog(LOG_INFO, "[LVGL] Initialization complete\n");
}

void lvgl_task_handler(void)
{
    lv_timer_handler();
}

#endif /* CONFIG_LVGL */

/*
 * WHAT : LVGL tick 回调（NuttX clock_gettime 毫秒）
 * WHY  : LVGL 9 需外部 tick；sim 里手工 lv_tick_inc，固件用系统时钟
 * HOW  : CLOCK_MONOTONIC 毫秒截断
 */
#include <time.h>
uint32_t retro_lv_tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
