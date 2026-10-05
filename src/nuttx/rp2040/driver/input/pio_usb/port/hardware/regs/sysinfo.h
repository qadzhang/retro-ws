/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/regs/sysinfo.h - Pico-PIO-USB 的 NuttX 垫片
 *
 * WHAT : 上游读 SYSINFO CHIP_ID 版本域做芯片修订探测。RP2040：
 *        SYSINFO 基址 0x40000000，CHIP_ID 偏移 0x000，REVISION 为
 *        bits[31:28]（RP2040 datasheet 2.14.2，与 pico-sdk regs 同值）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_REGS_SYSINFO_H
#define __PIOUSB_PORT_HW_REGS_SYSINFO_H

#define SYSINFO_BASE 0x40000000u

#define SYSINFO_CHIP_ID_OFFSET 0x0000u
#define SYSINFO_CHIP_ID_REVISION_BITS 0xF0000000u
#define SYSINFO_CHIP_ID_REVISION_LSB 28u

#endif /* __PIOUSB_PORT_HW_REGS_SYSINFO_H */
