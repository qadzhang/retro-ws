/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * pico/bootrom.h - Pico-PIO-USB 的 NuttX 垫片（pico/bootrom.h 替身）
 *
 * WHAT : 上游 pio_usb.c include 此头仅为其设备模式 reset_usb_boot；
 *        本项目只用主机模式，提供空声明保证编译（不进入任何路径）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_PICO_BOOTROM_H
#define __PIOUSB_PORT_PICO_BOOTROM_H

static inline void reset_usb_boot(unsigned int activity_pin_mask,
                                  unsigned int disable_interface_mask)
{
    (void)activity_pin_mask;
    (void)disable_interface_mask;
}

#endif /* __PIOUSB_PORT_PICO_BOOTROM_H */
