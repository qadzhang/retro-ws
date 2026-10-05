/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/pio.h - Pico-PIO-USB 的 NuttX 垫片（hardware/pio.h 替身）
 *
 * WHAT : 提供 pico-sdk 的 PIO 类型/寄存器结构/函数面，使上游
 *        Pico-PIO-USB 源码零修改编译：函数调用桥接 NuttX
 *        rp2040_pio_*（索引参数形态），寄存器直访经 pio_hw_t 结构
 *        （布局 = RP2040 datasheet 3.10，与树内 hardware/rp2040_pio.h
 *        偏移宏逐项核对一致）
 * WHY  : NuttX rp2040_pio.h 无 pio_hw_t/PIO 指针语义，而上游混用
 *        "API 调用 + ->irq / ->instr_mem / ->sm[] 直访"两种视图；
 *        PIO 指针即基址（0x50200000 + n*0x100000），互转保持一致
 * WHO  : Retro WS Project Team
 * WHERE: retro-ws/src/nuttx/rp2040/driver/input/pio_usb/port/hardware/pio.h
 * WHEN : 2026-10-05 新增
 * HOW  : clkdiv float 算法照 pico-sdk（含 65535 饱和/四舍五入）；
 *        pindirs/pins with_mask 用"临时改 pinctrl + 逐 pin exec SET"
 *        （pico-sdk 同法）；sdk_compat.h 已带 jmp_pin/instance 实现
 */

#ifndef __PIOUSB_PORT_HW_PIO_H
#define __PIOUSB_PORT_HW_PIO_H

#include <stdint.h>
#include <stdbool.h>

#include "pico/platform.h"         /* __not_in_flash/io 类型/timer 等基础面 */
#include "rp2040_pio.h"            /* NuttX PIO HAL（函数/程序类型） */
#include "hardware/pio_instructions.h"

/*==========================
 *  寄存器结构（datasheet 3.10；偏移与树内 RP2040_PIO_*_OFFSET 一致）
 *==========================*/

typedef struct
{
    volatile uint32_t clkdiv;      /* +0x00 */
    volatile uint32_t execctrl;    /* +0x04 */
    volatile uint32_t shiftctrl;   /* +0x08 */
    volatile uint32_t addr;        /* +0x0c */
    volatile uint32_t instr;       /* +0x10 */
    volatile uint32_t pinctrl;     /* +0x14 */
} pio_sm_hw_t;

typedef struct pio_hw_s
{
    volatile uint32_t ctrl;        /* 0x000 */
    volatile uint32_t fstat;       /* 0x004 */
    volatile uint32_t fdebug;      /* 0x008 */
    volatile uint32_t flevel;      /* 0x00c */
    volatile uint32_t txf[4];      /* 0x010-0x01c */
    volatile uint32_t rxf[4];      /* 0x020-0x02c */
    volatile uint32_t irq;         /* 0x030 */
    volatile uint32_t irqforce;    /* 0x034 */
    volatile uint32_t input_sync_bypass; /* 0x038 */
    volatile uint32_t dbg_padout;  /* 0x03c */
    volatile uint32_t dbg_padoe;   /* 0x040 */
    volatile uint32_t dbg_cfginfo; /* 0x044 */
    volatile uint32_t instr_mem[32]; /* 0x048-0x0c7 */
    pio_sm_hw_t sm[4];             /* 0x0c8-0x127 */
    uint32_t _reserved[22];        /* 0x128-0x17f */
    volatile uint32_t intr;        /* 0x180 */
    volatile uint32_t irq0_inte;   /* 0x184 */
    volatile uint32_t irq0_intf;   /* 0x188 */
    volatile uint32_t irq0_ints;   /* 0x18c */
    volatile uint32_t irq1_inte;   /* 0x190 */
    volatile uint32_t irq1_intf;   /* 0x194 */
    volatile uint32_t irq1_ints;   /* 0x198 */
} pio_hw_t;

typedef pio_hw_t *PIO;

/*==========================
 *  基址与句柄互转（sdk_compat.h 的 PIO_NUM/PIO_INSTANCE 依赖）
 *==========================*/

#define PIO0_BASE 0x50200000u
#define PIO1_BASE 0x50300000u

static inline uint32_t PIO_IDX(PIO pio)
{
    return (uint32_t)(((uintptr_t)pio - PIO0_BASE) >> 20);
}

static inline PIO PIO_PTR(uint32_t idx)
{
    return (PIO)(PIO0_BASE + (uintptr_t)idx * 0x100000u);
}

/* pio_sm_set_jmp_pin / pio_get_instance / PIO_NUM 由上游 sdk_compat.h
 * 提供（SDK1.x 分支），此处补其依赖的 EXECCTRL JMP_PIN 位域宏 */
#define PIO_SM0_EXECCTRL_JMP_PIN_BITS 0x00F80000u
#define PIO_SM0_EXECCTRL_JMP_PIN_LSB  19u

/*==========================
 *  类型映射（NuttX 同构）
 *==========================*/

typedef rp2040_pio_program_t pio_program_t;
typedef rp2040_pio_sm_config pio_sm_config;
#define pio_program rp2040_pio_program   /* 生成头用 struct tag 形式 */

/* 生成头（usb_tx/rx.pio.h）的默认配置构造与 wrap/sideset 设置 */
static inline pio_sm_config pio_get_default_sm_config(void)
{
    return rp2040_pio_get_default_sm_config();
}

static inline void sm_config_set_wrap(pio_sm_config *c, uint32_t target,
                                      uint32_t wrap)
{
    rp2040_sm_config_set_wrap(c, target, wrap);
}

static inline void sm_config_set_sideset(pio_sm_config *c, uint32_t bits,
                                         bool enable, bool value)
{
    rp2040_sm_config_set_sideset(c, bits, enable, value);
}

static inline void sm_config_set_out_pins(pio_sm_config *c, uint32_t base,
                                          uint32_t count)
{
    rp2040_sm_config_set_out_pins(c, base, count);
}

static inline void sm_config_set_set_pins(pio_sm_config *c, uint32_t base,
                                          uint32_t count)
{
    rp2040_sm_config_set_set_pins(c, base, count);
}

static inline void sm_config_set_in_pins(pio_sm_config *c, uint32_t base)
{
    rp2040_sm_config_set_in_pins(c, base);
}

static inline void sm_config_set_in_shift(pio_sm_config *c, bool right,
                                          bool autopush, uint32_t bits)
{
    rp2040_sm_config_set_in_shift(c, right, autopush, bits);
}

static inline void sm_config_set_out_shift(pio_sm_config *c, bool right,
                                           bool autopull, uint32_t bits)
{
    rp2040_sm_config_set_out_shift(c, right, autopull, bits);
}

static inline void sm_config_set_clkdiv(pio_sm_config *c, float div)
{
    rp2040_sm_config_set_clkdiv(c, div);
}

static inline void pio_sm_init(PIO pio, uint32_t sm, uint32_t initial_pc,
                               const pio_sm_config *config)
{
    rp2040_pio_sm_init(PIO_IDX(pio), sm, initial_pc, config);
}

/*==========================
 *  函数桥（NuttX 同形直映射）
 *==========================*/

static inline uint32_t pio_add_program(PIO pio, const pio_program_t *prog)
{
    return rp2040_pio_add_program(PIO_IDX(pio), prog);
}

/* NuttX 版返回 void（上游不用其返回值） */
static inline void pio_add_program_at_offset(PIO pio,
                                             const pio_program_t *prog,
                                             uint32_t offset)
{
    rp2040_pio_add_program_at_offset(PIO_IDX(pio), prog, offset);
}

static inline void pio_gpio_init(PIO pio, uint32_t pin)
{
    rp2040_pio_gpio_init(PIO_IDX(pio), pin);
}

static inline void pio_sm_claim(PIO pio, uint32_t sm)
{
    rp2040_pio_sm_claim(PIO_IDX(pio), sm);
}

static inline void pio_sm_set_enabled(PIO pio, uint32_t sm, bool enabled)
{
    rp2040_pio_sm_set_enabled(PIO_IDX(pio), sm, enabled);
}

static inline void pio_sm_restart(PIO pio, uint32_t sm)
{
    rp2040_pio_sm_restart(PIO_IDX(pio), sm);
}

static inline void pio_sm_clear_fifos(PIO pio, uint32_t sm)
{
    rp2040_pio_sm_clear_fifos(PIO_IDX(pio), sm);
}

static inline void pio_sm_exec(PIO pio, uint32_t sm, uint32_t instr)
{
    rp2040_pio_sm_exec(PIO_IDX(pio), sm, instr);
}

static inline uint32_t pio_sm_get(PIO pio, uint32_t sm)
{
    return rp2040_pio_sm_get(PIO_IDX(pio), sm);
}

static inline uint8_t pio_sm_get_rx_fifo_level(PIO pio, uint32_t sm)
{
    return rp2040_pio_sm_get_rx_fifo_level(PIO_IDX(pio), sm);
}

static inline uint32_t pio_get_dreq(PIO pio, uint32_t sm, bool is_tx)
{
    return rp2040_pio_get_dreq(PIO_IDX(pio), sm, is_tx);
}

/*==========================
 *  自实现（NuttX 无对应/形态不同）
 *==========================*/

/* RP2040 无 gpio base（RP2350 特性）——空实现满足链接 */
static inline void pio_set_gpio_base(PIO pio, uint32_t base)
{
    (void)pio;
    (void)base;
}

/* pico-sdk pio_calculate_clkdiv_from_float：四舍五入 + 65535/255 饱和 */
static inline void pio_calculate_clkdiv_from_float(float div,
                                                   uint16_t *div_int,
                                                   uint8_t *div_frac)
{
    if (div < 1.0f)
        div = 1.0f;
    else if (div > 65535.0f)
        div = 65535.0f;

    uint32_t div_fixed = (uint32_t)(div * 256.0f + 0.5f);

    *div_int = (uint16_t)(div_fixed >> 8);
    *div_frac = (uint8_t)(div_fixed & 0xffu);
}

static inline void pio_sm_set_clkdiv_int_frac(PIO pio, uint32_t sm,
                                              uint16_t div_int,
                                              uint8_t div_frac)
{
    rp2040_pio_sm_set_clkdiv_int_frac(PIO_IDX(pio), sm, div_int, div_frac);
}

/* SMx_PINCTRL 直访版 SET 域：SET_COUNT [15:11] / SET_BASE [10:5] */
#define PIOUSB_PINCTRL_SET_COUNT_SHIFT 11
#define PIOUSB_PINCTRL_SET_BASE_SHIFT  5
#define PIOUSB_PINCTRL_SET_COUNT_BITS  (0x1fu << PIOUSB_PINCTRL_SET_COUNT_SHIFT)
#define PIOUSB_PINCTRL_SET_BASE_BITS   (0x1fu << PIOUSB_PINCTRL_SET_BASE_SHIFT)

/* with_mask：临时把 SET 域指向单个 pin 并 exec SET（pico-sdk 同法） */
static inline void pio_sm_set_pins_with_mask(PIO pio, uint32_t sm,
                                             uint32_t pin_values,
                                             uint32_t pin_mask)
{
    uint32_t saved = pio->sm[sm].pinctrl;

    for (uint32_t i = 0; i < 32 && pin_mask; i++, pin_mask >>= 1, pin_values >>= 1)
    {
        if (pin_mask & 1u)
        {
            pio->sm[sm].pinctrl =
                (saved & ~(PIOUSB_PINCTRL_SET_COUNT_BITS |
                           PIOUSB_PINCTRL_SET_BASE_BITS)) |
                (1u << PIOUSB_PINCTRL_SET_COUNT_SHIFT) |
                (i << PIOUSB_PINCTRL_SET_BASE_SHIFT);
            pio_sm_exec(pio, sm, pio_encode_set(pio_pins, pin_values & 1u));
        }
    }

    pio->sm[sm].pinctrl = saved;
}

static inline void pio_sm_set_pindirs_with_mask(PIO pio, uint32_t sm,
                                                uint32_t pindir,
                                                uint32_t pin_mask)
{
    uint32_t saved = pio->sm[sm].pinctrl;

    for (uint32_t i = 0; i < 32 && pin_mask; i++, pin_mask >>= 1)
    {
        if (pin_mask & 1u)
        {
            pio->sm[sm].pinctrl =
                (saved & ~(PIOUSB_PINCTRL_SET_COUNT_BITS |
                           PIOUSB_PINCTRL_SET_BASE_BITS)) |
                (1u << PIOUSB_PINCTRL_SET_COUNT_SHIFT) |
                (i << PIOUSB_PINCTRL_SET_BASE_SHIFT);
            pio_sm_exec(pio, sm, pio_encode_set(pio_pindirs, pindir & 1u));
        }
    }

    pio->sm[sm].pinctrl = saved;
}


/* FDEBUG 位域 LSB（上游以 LSB+掩码组值；树内有 SHIFT 同值） */
#define PIO_FDEBUG_TXSTALL_LSB 24
#define PIO_FDEBUG_TXOVER_LSB  16
#define PIO_FDEBUG_RXUNDER_LSB 0
#define PIO_FDEBUG_RXOVER_LSB  8

/* FIFO JOIN 枚举（生成头用） */
#define PIO_FIFO_JOIN_NONE 0
#define PIO_FIFO_JOIN_TX   1
#define PIO_FIFO_JOIN_RX   2

static inline void sm_config_set_fifo_join(pio_sm_config *c, uint32_t join)
{
    rp2040_sm_config_set_fifo_join(c, join);
}

/* 直设版 pinctrl 域写（pico-sdk hw_write_masked 语义：RMW） */
#define PIOUSB_PINCTRL_OUT_BASE_SHIFT  0
#define PIOUSB_PINCTRL_OUT_COUNT_SHIFT 5 /* SET_BASE 同域见前 */
#define PIOUSB_PINCTRL_OUT_BASE_BITS   (0x1fu << 0)
#define PIOUSB_PINCTRL_OUT_COUNT_BITS  (0x1fu << 5)
#define PIOUSB_PINCTRL_SET_COUNT_BITS_ (0x1fu << 11)
#define PIOUSB_PINCTRL_IN_BASE_SHIFT   15
#define PIOUSB_PINCTRL_IN_BASE_BITS    (0x1fu << 15)

static inline void pio_sm_set_out_pins(PIO pio, uint32_t sm, uint32_t base,
                                       uint32_t count)
{
    volatile uint32_t *pc = &pio->sm[sm].pinctrl;
    *pc = (*pc & ~(PIOUSB_PINCTRL_OUT_BASE_BITS |
                   PIOUSB_PINCTRL_OUT_COUNT_BITS)) |
          (base << 0) | (count << 5);
}

static inline void pio_sm_set_set_pins(PIO pio, uint32_t sm, uint32_t base,
                                       uint32_t count)
{
    volatile uint32_t *pc = &pio->sm[sm].pinctrl;
    *pc = (*pc & ~(0x1fu << 10 | 0x1fu << 11)) |
          (base << 10) | (count << 11);
}

static inline void pio_sm_set_sideset_pins(PIO pio, uint32_t sm, uint32_t base)
{
    volatile uint32_t *pc = &pio->sm[sm].pinctrl;
    *pc = (*pc & ~(0x1fu << 15)) | (base << 15);
}


/* sm_config 版 jmp_pin/sideset_pins（生成头用）：CONFIG 域 RMW */
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint32_t pin)
{
    c->execctrl = (c->execctrl & ~0xF8000u) | ((pin & 0x1fu) << 19);
}

static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint32_t base)
{
    c->pinctrl = (c->pinctrl & ~(0x1fu << 15)) | ((base & 0x1fu) << 15);
}

/* 直设版 in_pins：SMx_PINCTRL IN_BASE 域 RMW */
static inline void pio_sm_set_in_pins(PIO pio, uint32_t sm, uint32_t base)
{
    volatile uint32_t *pc = &pio->sm[sm].pinctrl;
    *pc = (*pc & ~(0x1fu << 15)) | ((base & 0x1fu) << 15);
}

static inline void pio_sm_set_consecutive_pindirs(PIO pio, uint32_t sm,
                                                  uint32_t pin, uint32_t count,
                                                  uint32_t is_out)
{
    for (uint32_t i = 0; i < count; i++)
        pio_sm_set_pindirs_with_mask(pio, sm, is_out,
                                     (uint32_t)1u << (pin + i));
}

#endif /* __PIOUSB_PORT_HW_PIO_H */
#include "hardware/gpio.h"
