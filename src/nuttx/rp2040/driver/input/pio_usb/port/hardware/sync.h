/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/sync.h - Pico-PIO-USB 的 NuttX 垫片（hardware/sync.h 替身）
 *
 * WHAT : 上游用到 save_and_disable_interrupts/restore_interrupts
 *        （PIO 时序临界区）。Cortex-M0+ 上以 PRIMASK 实现，语义与
 *        pico-sdk 等价（全局中断关/开并保存现场）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_SYNC_H
#define __PIOUSB_PORT_HW_SYNC_H

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void)
{
    uint32_t primask;

    __asm__ volatile("mrs %0, PRIMASK\n"
                     "cpsid i"
                     : "=r"(primask)::"memory");
    return primask;
}

static inline void restore_interrupts(uint32_t primask)
{
    __asm__ volatile("msr PRIMASK, %0"
                     ::"r"(primask)
                     : "memory");
}

#endif /* __PIOUSB_PORT_HW_SYNC_H */
