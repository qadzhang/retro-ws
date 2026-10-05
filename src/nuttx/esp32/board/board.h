/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * board.h - ESP32-CAM 板级配置头
 *
 * WHAT : ESP32-CAM 板级配置头
 * WHY  : 外设参数/内存映射/中断定义（引脚在硬件档案）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32/board/board.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : #include hw_esp32cam_aithinker.h + 非引脚配置
 */

#ifndef __BOARD_H
#define __BOARD_H

/* 板卡硬件档案（引脚唯一事实来源 / single source of pin truth） */
#include "hw_esp32cam_aithinker.h"

/*==========================
 *  外设配置
 *==========================*/

/* I2S 配置 — I2S0 用于 DAC 模式（CVBS + 音频） */
#define AUDIO_I2S_PORT          0     /* I2S 端口 0 */
#define AUDIO_SAMPLE_RATE       44100
#define AUDIO_I2S_BITS          8     /* DAC 模式 8-bit */

/* SD 卡 SPI 配置 */
#define SD_SPI_HOST             2     /* HSPI (SPI2) */
#define SD_SPI_FREQ             25000000  /* 25MHz */

/* 软件模拟 I2C (RTC) */
#define I2C_FREQ                400000  /* 400kHz Fast Mode */

/* SPI Flash */
#define SPI_FLASH_FREQ          40*1000*1000  /* 40MHz（QSPI） */

/*==========================
 *  显示配置
 *==========================*/

/* 默认分辨率（档位: 320x240 控制台 / 640x480 常规与最高，DAC 带宽所限） */
#define DISPLAY_WIDTH           CONFIG_RETRO_DISPLAY_WIDTH
#define DISPLAY_HEIGHT          CONFIG_RETRO_DISPLAY_HEIGHT

/* 像素时钟 — I2S DAC 模式下受限于 APB/8 = 10MHz */
#define DISPLAY_PIXEL_CLK       ((DISPLAY_WIDTH + 144) * DISPLAY_HEIGHT * 50)

/*==========================
 *  内存映射
 *==========================*/

/* Flash */
#define ESP32_FLASH_BASE        0x3F400000

/* PSRAM — QSPI 模式 */
#define ESP32_PSRAM_BASE        0x3F800000

/* SRAM */
#define ESP32_SRAM_BASE         0x3FFAE000
#define ESP32_SRAM_SIZE         (520 * 1024)           /* 520KB */

/*==========================
 *  中断定义
 *==========================*/

/* 外部中断 */
#define ESP32_GPIO_IRQ_0        0     /* GPIO 0-31 中断 */
#define ESP32_GPIO_IRQ_1        1     /* GPIO 32-39 中断 */

/* DMA 中断 */
#define ESP32_DMA_INTR          9     /* SPI/DMA 中断 */

/*==========================
 *  时钟定义
 *==========================*/

#define ESP32_CPU_FREQ          240000000      /* 240 MHz */
#define ESP32_APB_FREQ          80000000       /* 80 MHz */
#define ESP32_XTAL_FREQ         40000000       /* 40 MHz */

/*==========================
 *  启动配置
 *==========================*/

/* Bootloader 地址 */
#define BOOTLOADER_OFFSET       0x1000
#define BOOTLOADER_MAXSIZE      (16*1024)

/* 分区表地址 */
#define PARTITION_TABLE_ADDR    0x8000

/* NVS 地址 */
#define NVS_OFFSET              0x9000
#define NVS_SIZE                (24*1024)

/* 应用分区 — 4MB Flash 紧凑布局 */
#define APP_OFFSET              0x10000
#define APP_MAXSIZE             (0x400000 - 0x10000)   /* ~3.9MB */

#endif /* __BOARD_H */
