/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * drv_cvbs.c - CVBS 显示驱动可移植实现 / portable CVBS driver
 *
 * WHAT : drv_cvbs.h 接口的硬件无关实现（两块板共用）
 * WHY  : 见 drv_cvbs.h 头注释；信号语义集中在 cvbs_core，
 *        本层只做帧缓冲生命周期与场调度
 * WHO  : lv_port_disp.c / esp32*_retro.c / 宿主机模拟器
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_cvbs.c
 * WHEN : 2026-10-04 新增
 * HOW  : init -> cvbs_core_fb_alloc(显示分辨率)；
 *        send -> 拷贝进帧缓冲；frame -> cvbs_core_generate_frame(
 *        progressive, drv_cvbs_emit_line)。
 *        硬件未接时 emit_line 为 weak 空实现（仅日志一次）
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "cvbs_core.h"
#include "drv_cvbs.h"

#ifndef CONFIG_RETRO_DISPLAY_WIDTH
#  define CONFIG_RETRO_DISPLAY_WIDTH  320
#endif
#ifndef CONFIG_RETRO_DISPLAY_HEIGHT
#  define CONFIG_RETRO_DISPLAY_HEIGHT 240
#endif

static bool g_drv_initialized = false;
static bool g_hw_warned = false;

/*
 * WHAT : weak 硬件钩子默认实现 / default weak hook
 * WHY  : 未接板级 I2S/DAC（如宿主机测试未注入 sink）时保持安全
 * HOW  : 空实现 + 一次告警日志
 */
__attribute__((weak))
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    (void)line;
    (void)len;
    (void)line_no;
    if (!g_hw_warned) {
        g_hw_warned = true;
        syslog(LOG_WARNING,
               "[drv_cvbs] no hardware sink, frame not emitted\n");
    }
}

int drv_cvbs_init(void)
{
    if (g_drv_initialized)
        return OK;

    int ret = cvbs_core_fb_alloc(CONFIG_RETRO_DISPLAY_WIDTH,
                                 CONFIG_RETRO_DISPLAY_HEIGHT);
    if (ret != OK) {
        syslog(LOG_ERR, "[drv_cvbs] fb alloc failed: %d\n", ret);
        return ret;
    }

    g_drv_initialized = true;
    syslog(LOG_INFO, "[drv_cvbs] ready %dx%d (PAL, 13.5MHz samples)\n",
           CONFIG_RETRO_DISPLAY_WIDTH, CONFIG_RETRO_DISPLAY_HEIGHT);
    return OK;
}

void drv_cvbs_deinit(void)
{
    cvbs_core_fb_free();
    g_drv_initialized = false;
}

uint8_t *drv_cvbs_get_fb(void)
{
    return cvbs_core_fb();
}

int drv_cvbs_get_width(void)
{
    return cvbs_core_fb_width();
}

int drv_cvbs_get_height(void)
{
    return cvbs_core_fb_height();
}

void drv_cvbs_send(const uint8_t *px_map, size_t size)
{
    uint8_t *fb = cvbs_core_fb();
    size_t cap = (size_t)cvbs_core_fb_width() * cvbs_core_fb_height();

    if (fb == NULL || px_map == NULL)
        return;
    if (size > cap)
        size = cap;

    memcpy(fb, px_map, size);
}

__attribute__((weak))
void drv_cvbs_frame(void)
{
    if (!g_drv_initialized)
        return;

    /* 帧缓冲高 <=288（240p 控制台档）发同相双场；更高（480 档）走隔行 */
    bool progressive = cvbs_core_fb_height() <= CVBS_FIELD1_ACTIVE;
    cvbs_core_generate_frame(progressive, drv_cvbs_emit_line);
}
