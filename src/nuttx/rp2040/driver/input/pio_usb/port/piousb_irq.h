/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * piousb_irq.h - PIO1 中断号垫片（piousb_kbd 专用）
 *
 * WHAT : 定义 PIO1_IRQ_0 的 NuttX 内部中断号（25 = EXTI 9）
 * WHY  : 树内真值在 arch/arm/include/rp2040/irq.h（RP2040_IRQ_
 *        EXTINT+9），但 retro-apps 的 CFLAGS 不含 arch/arm/include
 *        路径（全局加会污染 berry 等引擎的 nuttx 头解析），宿主
 *        语法矩阵的 include 链又固定 xtensa（防构建产物污染）——
 *        两种环境都无法直达该头，按垫片层惯例在此定值
 * WHO  : piousb_kbd.c 消费；改动需与树内 irq.h 保持一致
 * WHERE: retro-ws/src/nuttx/rp2040/driver/input/pio_usb/port/piousb_irq.h
 * WHEN : 2026-10-05 新增（半格字体重构当日发现矩阵 flaky 顺带修正）
 * HOW  : RP2040 NVIC：EXTI0=16 起，PIO1_IRQ_0 向量 25（datasheet
 *        2.3.2 中断向量表 IRQ 9）；与树内宏等值，若树内改号需同步
 */

#ifndef __PIOUSB_IRQ_H
#define __PIOUSB_IRQ_H

#define PIOUSB_PIO1_IRQ_0   (25)    /* = RP2040_IRQ_EXTINT(16)+9 */

#endif /* __PIOUSB_IRQ_H */
