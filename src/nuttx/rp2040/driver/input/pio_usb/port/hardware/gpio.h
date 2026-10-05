/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/gpio.h - Pico-PIO-USB 的 NuttX 垫片（hardware/gpio.h 替身）
 *
 * WHAT : 上游用的 gpio_* 调用面映射到 NuttX rp2040_gpio_*；不足的
 *        （inover/oeover/slew/pull_down/input_enabled）以 IO_BANK0
 *        CTRL / PADS_BANK0 寄存器直写实现（RP2040 datasheet 2.19/2.20）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_GPIO_H
#define __PIOUSB_PORT_HW_GPIO_H

#include <stdint.h>
#include "rp2040_gpio.h"

/* ---- 直映射（NuttX 同形） ---- */
#define gpio_init            rp2040_gpio_init
#define gpio_get             rp2040_gpio_get
#define gpio_set_drive_strength(pin, strength) \
        rp2040_gpio_set_drive_strength(pin, strength)
#define gpio_set_outover(pin, value) \
        rp2040_gpio_set_outover(pin, value)

#define GPIO_DRIVE_STRENGTH_12MA 1
#define GPIO_OVERRIDE_NORMAL     0
#define GPIO_OVERRIDE_INVERT     1
#define GPIO_OVERRIDE_LOW        2
#define GPIO_OVERRIDE_HIGH       3
#define GPIO_SLEW_RATE_SLOW      0
#define GPIO_SLEW_RATE_FAST      1

/* ---- PADS_BANK0 / IO_BANK0 寄存器基址 ---- */
#define PIOUSB_PADS_BANK0_BASE      0x4002c000u
#define PIOUSB_PADS_BANK0_GPIO_OFF  0x04u    /* GPIO0 起，每 pin 4B */
#define PIOUSB_IO_BANK0_BASE        0x40014000u
#define PIOUSB_IO_BANK0_CTRL_OFF    0x04u    /* GPIO0 CTRL 起，每 pin 8B */

static inline void piousb_pad_wr(unsigned pin, uint32_t value)
{
    *(volatile uint32_t *)(PIOUSB_PADS_BANK0_BASE +
                           PIOUSB_PADS_BANK0_GPIO_OFF + pin * 4) = value;
}

static inline uint32_t piousb_pad_rd(unsigned pin)
{
    return *(volatile uint32_t *)(PIOUSB_PADS_BANK0_BASE +
                                  PIOUSB_PADS_BANK0_GPIO_OFF + pin * 4);
}

static inline uint32_t piousb_ioctrl_rd(unsigned pin)
{
    return *(volatile uint32_t *)(PIOUSB_IO_BANK0_BASE +
                                  PIOUSB_IO_BANK0_CTRL_OFF + pin * 8);
}

static inline void piousb_ioctrl_wr(unsigned pin, uint32_t v)
{
    *(volatile uint32_t *)(PIOUSB_IO_BANK0_BASE +
                           PIOUSB_IO_BANK0_CTRL_OFF + pin * 8) = v;
}

/* ---- 补齐的 pico-sdk 语义 ---- */

static inline void gpio_set_slew_rate(unsigned pin, unsigned slew)
{
    uint32_t pad = piousb_pad_rd(pin);
    pad = (pad & ~(1u << 9)) | ((slew & 1u) << 9);   /* SLEWFAST bit9 */
    piousb_pad_wr(pin, pad);
}

static inline void gpio_pull_down(unsigned pin)
{
    rp2040_gpio_set_pulls(pin, false, true);
}

static inline void gpio_pull_up(unsigned pin)
{
    rp2040_gpio_set_pulls(pin, true, false);
}

static inline void gpio_disable_pulls(unsigned pin)
{
    rp2040_gpio_set_pulls(pin, false, false);
}

/* INOVER/OEOVER: IO_BANK0 GPIOx_CTRL bits[3:2]/[7:6]（RP2040 表 188） */
static inline void gpio_set_inover(unsigned pin, unsigned value)
{
    uint32_t ctrl = piousb_ioctrl_rd(pin);
    ctrl = (ctrl & ~(3u << 2)) | ((value & 3u) << 2);
    piousb_ioctrl_wr(pin, ctrl);
}

static inline void gpio_set_oeover(unsigned pin, unsigned value)
{
    uint32_t ctrl = piousb_ioctrl_rd(pin);
    ctrl = (ctrl & ~(3u << 6)) | ((value & 3u) << 6);
    piousb_ioctrl_wr(pin, ctrl);
}

/* 输入使能：PADS IE bit6（复位默认开；PIO-USB 显式打开） */
static inline void gpio_set_input_enabled(unsigned pin, bool enabled)
{
    uint32_t pad = piousb_pad_rd(pin);
    if (enabled)
        pad |= (1u << 6);
    else
        pad &= ~(1u << 6);
    piousb_pad_wr(pin, pad);
}

#endif /* __PIOUSB_PORT_HW_GPIO_H */
