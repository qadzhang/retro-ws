/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * pico/platform.h - Pico-PIO-USB 的 NuttX 垫片（pico/platform.h 替身）
 *
 * WHAT : 上游 Pico-PIO-USB（MIT）依赖的 pico-sdk 基础宏/内联在此用
 *        NuttX 环境等价实现（AGENTS.md 11.5 覆盖头路线：src 优先于 deps）
 * WHY  : PIO-USB 主机移植（NEXT_STEPS 54）——上游源码零修改，pico-sdk
 *        语义经本垫片落到 NuttX RP2040 环境
 * WHO  : Retro WS Project Team
 * WHERE: retro-ws/src/nuttx/rp2040/driver/input/pio_usb/port/pico/platform.h
 * WHEN : 2026-10-05 新增
 * HOW  : 版本宏走 sdk_compat 的 SDK1.x 分支（其自带 jmp_pin/instance
 *        兼容实现）；busy_wait 以 nop 自旋近似（125MHz 核心频率）
 */

#ifndef __PIOUSB_PORT_PICO_PLATFORM_H
#define __PIOUSB_PORT_PICO_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 走 sdk_compat.h 的 SDK 1.x 兼容分支（自带 pio_sm_set_jmp_pin、
 * pio_get_instance 等；避免我们重复实现并与上游语义分叉） */
#define PICO_SDK_VERSION_MAJOR 1
#define PICO_SDK_VERSION_MINOR 5
#define PICO_SDK_VERSION_REVISION 0

/* pico-sdk 段属性宏：RP2040 无 XIP 分区概念（NuttX flat 镜像常驻
 * Flash/或整体入 RAM），展开为空；语义由链接布局等价保证 */
#ifndef __not_in_flash
#  define __not_in_flash(x)
#endif
#ifndef __not_in_flash_func
#  define __not_in_flash_func(x) x
#endif
#ifndef __no_inline_not_in_flash_func
#  define __no_inline_not_in_flash_func(x) __noinline x
#endif

#ifndef __unused
#  define __unused __attribute__((unused))
#endif

/* pico-sdk alarm/repeating timer：skip_alarm_pool 模式不实际使用，
 * 空类型 + 空实现满足上游类型面（帧节拍由本项目 1ms 任务提供） */
typedef struct
{
    int _dummy;
} alarm_pool_t;

typedef struct
{
    int _dummy;
} repeating_timer_t;

static inline bool alarm_pool_add_repeating_timer_us(
    alarm_pool_t *pool, int64_t delay_us, bool (*callback)(repeating_timer_t *),
    void *user_data, repeating_timer_t *out)
{
    (void)pool; (void)delay_us; (void)callback; (void)user_data; (void)out;
    return false;
}

static inline alarm_pool_t *alarm_pool_create(uint32_t hw_alarm_num,
                                              uint32_t max_timers)
{
    (void)hw_alarm_num; (void)max_timers;
    return NULL;   /* skip_alarm_pool 模式不实际使用 */
}

static inline void cancel_repeating_timer(repeating_timer_t *rt)
{
    (void)rt;
}

/* pico-sdk IO 类型（volatile 限定即语义等价） */
typedef volatile uint32_t io_ro_32;
typedef volatile uint32_t io_wo_32;
typedef volatile uint32_t io_rw_32;

/* RP2040 TIMER（datasheet 3.5.3）：基址 0x40054000，TIMERAWL=0x28 */
typedef struct
{
    volatile uint32_t timehw;    /* +0x00 写高 32 位（向上计数） */
    volatile uint32_t timelw;    /* +0x04 */
    volatile uint32_t timehr;    /* +0x08 */
    volatile uint32_t timelr;    /* +0x0c */
    volatile uint32_t alarm0;    /* +0x10 */
    volatile uint32_t alarm1;    /* +0x14 */
    volatile uint32_t alarm2;    /* +0x18 */
    volatile uint32_t alarm3;    /* +0x1c */
    volatile uint32_t armed;     /* +0x20 */
    volatile uint32_t timerawh;  /* +0x24 */
    volatile uint32_t timerawl;  /* +0x28 */
} timer_hw_t;

#define PICO_TIMER_BASE 0x40054000u
#define timer_hw ((timer_hw_t *)PICO_TIMER_BASE)

#ifndef __noinline
#  define __noinline __attribute__((noinline))
#endif

#ifndef __force_inline
#  define __force_inline inline __attribute__((always_inline))
#endif

#ifndef __time_critical_func
#  define __time_critical_func(x) x
#endif

#ifndef __always_inline
#  define __always_inline inline __attribute__((always_inline))
#endif

#ifndef static_assert
#  define static_assert(cond, msg) _Static_assert(cond, msg)
#endif

/* 编译/内存屏障（pico-sdk 语义） */
#ifndef __compiler_memory_barrier
#  define __compiler_memory_barrier() __asm__ volatile("" ::: "memory")
#endif

/* 核心频率：本项目 Pico 档固定 125MHz（CVBS PIO 分频同源，HARDWARE 3B） */
#ifndef PIOUSB_PORT_SYSCLK_HZ
#  define PIOUSB_PORT_SYSCLK_HZ 125000000u
#endif

/* 忙等（近似 nop 自旋；仅用于微秒级 turnaround/复位时序，无精度硬要求） */
static inline void tight_loop_contents(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline void busy_wait_at_least_cycles(uint32_t cycles)
{
    /* Cortex-M0+ 仅 Thumb16（无三操作数 subs 立即数），用 C 自旋：
     * 每迭代约 3 周期（循环开销），近似等待 */
    __asm__ volatile("" : "+r"(cycles));

    while (cycles >= 3)
    {
        __asm__ volatile("nop");
        cycles -= 3;
    }
}

static inline void busy_wait_us(uint32_t us)
{
    busy_wait_at_least_cycles((uint32_t)((uint64_t)us * PIOUSB_PORT_SYSCLK_HZ / 1000000u));
}

static inline void busy_wait_ms(uint32_t ms)
{
    while (ms-- > 0)
        busy_wait_us(1000);
}

#endif /* __PIOUSB_PORT_PICO_PLATFORM_H */
