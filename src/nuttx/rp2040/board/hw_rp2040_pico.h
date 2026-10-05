/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * hw_rp2040_pico.h - Raspberry Pi Pico（RP2040）硬件档案
 *
 * WHAT : Pico 板全部引脚/外设分配的唯一事实来源（HARDWARE.md 3B 章）
 * WHY  : AGENTS.md 11.1-7：使用新硬件前先落档案，代码严格照抄本表
 * WHO  : src/nuttx/rp2040/ 全部板级代码与 defconfig
 * WHERE: retro-ws/src/nuttx/rp2040/board/hw_rp2040_pico.h
 * WHEN : 2026-10-04 新增（第四目标）；同日晚 CVBS 移脚 GP20-23 ->
 *        GP12-15（GP23=SMPS PS 会耦合电源纹波 + PIO 需连续引脚，
 *        HARDWARE.md 3B.2A 晚间修订）
 * HOW  : 引脚来源 RP2040 datasheet + Pico pinout（验证链接见
 *        HARDWARE.md 文末）
 */

#ifndef __HW_RP2040_PICO_H
#define __HW_RP2040_PICO_H

/*==========================
 *  核心 / 存储
 *==========================*/

#define RP2040_CPU_FREQ_HZ     133000000   /* 双核 Cortex-M0+ */
#define RP2040_SRAM_SIZE       (264 * 1024)
#define RP2040_FLASH_SIZE      (2 * 1024 * 1024)   /* 板载 QSPI 2MB */

/*==========================
 *  控制台 / 存储 / 指示
 *==========================*/

#define RP2040_CONSOLE_UART    0
#define RP2040_CONSOLE_TX_PIN  0           /* GP0 */
#define RP2040_CONSOLE_RX_PIN  1           /* GP1 */
#define RP2040_CONSOLE_BAUD    115200

/* SD 卡走 SPI0（HARDWARE.md 3B.2） */
#define RP2040_SD_SPI          0
#define RP2040_SD_MOSI_PIN     19
#define RP2040_SD_MISO_PIN     16
#define RP2040_SD_SCK_PIN      18
#define RP2040_SD_CS_PIN       17

/* 板载 LED */
#define RP2040_LED_PIN         25

/* AV 视频输出（PIO 4-bit 并行，HARDWARE.md 3B.2A 晚间修订版）
 *
 * 2026-10-04 晚移脚 GP20-23 -> GP12-15，原因：
 * 1) GP23 是 Pico 板 SMPS(RT6150) 省电模式控制脚——视频位 13.5MHz 翻转
 *    会把 3V3 纹波直接调制进 R-2R 输出（R-2R 以 3V3 为基准）；
 * 2) PIO `OUT PINS,4` 只能驱动连续编号引脚，GP20/21/22+GP26 不可行；
 * 3) GP12-15 连续、无复用、非 ADC、非 strapping，且 GP20-22 释放为教学脚。
 */
#define RP2040_CVBS_PIN_BASE   12           /* GP12-15 = 4-bit R-2R */
#define RP2040_CVBS_PIN_COUNT   4

/* ADC 通道（GP26/27/28 = ADC0/1/2） */
#define RP2040_ADC_CHANNELS    3
#define RP2040_ADC_FIRST_PIN   26

/*==========================
 *  脚本 GPIO 占用表 / occupied-pin table
 *
 * WHAT : RETRO_GPIO_OCCUPIED_LIST 宏供 board.c 实例化
 * WHY  : 第三方脚本触碰系统脚直接报"已占用"（-EBUSY）
 * HOW  : 引脚 = 硬件档案分配；改引脚先改 HARDWARE.md 再改这里。
 *        教学脚（GP25 板载 LED 遵循指示灯脚避让不入表）：
 *        GP2-GP11（数字）、GP20-GP22（数字）、GP26-GP28（ADC0/1/2）
 *==========================*/

#define RETRO_GPIO_OCCUPIED_LIST                                         \
    { 0,  "UART0 console TX" },                                         \
    { 1,  "UART0 console RX" },                                         \
    { 12, "CVBS R-2R bit0 (PIO)" },                                     \
    { 13, "CVBS R-2R bit1 (PIO)" },                                     \
    { 14, "CVBS R-2R bit2 (PIO)" },                                     \
    { 15, "CVBS R-2R bit3 (PIO)" },                                     \
    { 16, "SD card MISO (SPI0)" },                                      \
    { 17, "SD card CS (SPI0)" },                                        \
    { 18, "SD card SCK (SPI0)" },                                       \
    { 19, "SD card MOSI (SPI0)" },                                      \
    { 25, "onboard LED (avoid)" },                                      \
    { 23, "SMPS PS (do not use)" },                                     \
    { 24, "VBUS detect (do not use)" },                                 \
    { 29, "ADC reference / WiFi-ish special (do not use)" }

#endif /* __HW_RP2040_PICO_H */
