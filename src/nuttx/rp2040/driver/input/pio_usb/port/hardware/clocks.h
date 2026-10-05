/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/clocks.h - Pico-PIO-USB 的 NuttX 垫片（hardware/clocks.h 替身）
 *
 * WHAT : 上游只调 clock_get_hz(clk_sys) 取系统时钟（算 PIO 分频）。
 *        本项目 Pico 档系统时钟固定 125MHz（与 CVBS PIO 时钟推导同源，
 *        HARDWARE 3B）；经 PIOUSB_PORT_SYSCLK_HZ 可整体覆盖
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_CLOCKS_H
#define __PIOUSB_PORT_HW_CLOCKS_H

#include <stdint.h>
#include "pico/platform.h"      /* PIOUSB_PORT_SYSCLK_HZ */

enum clock_index
{
    clk_sys = 5,                /* pico-sdk 枚举值（仅作标识用） */
    clk_usb,
};

static inline uint32_t clock_get_hz(enum clock_index idx)
{
    (void)idx;
    return PIOUSB_PORT_SYSCLK_HZ;
}

#endif /* __PIOUSB_PORT_HW_CLOCKS_H */
