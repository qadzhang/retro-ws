/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * board.h - ESP32-S3 板级配置头
 *
 * WHAT : ESP32-S3 板级配置头
 * WHY  : 外设参数/内存映射/中断定义（引脚在硬件档案）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32s3/board/board.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : #include hw_esp32s3_devkitc.h + 非引脚配置
 */

#ifndef __BOARD_H
#define __BOARD_H

/* 板卡硬件档案（引脚唯一事实来源 / single source of pin truth） */
#include "hw_esp32s3_devkitc.h"

/*==========================
 *  外设配置
 *==========================*/

/* I2S 配置 */
#define AUDIO_I2S_PORT       I2S_NUM_0
#define AUDIO_SAMPLE_RATE    44100
#define AUDIO_I2S_BITS        16

/* SD 卡 SPI 配置 */
#define SD_SPI_HOST           VSPI_HOST
#define SD_SPI_FREQ           25000000  /* 25MHz */

/* 软件模拟 I2C (RTC) */
#define I2C_FREQ             400000  /* 400kHz Fast Mode */

/* SPI Flash */
#define SPI_FLASH_FREQ       80*1000*1000  /* 80MHz */

/*==========================
 *  显示配置
 *==========================*/

/* 默认分辨率（三档: 320x240 控制台 / 640x480 常规 / 1024x768 最高） */
#define DISPLAY_WIDTH        CONFIG_RETRO_DISPLAY_WIDTH
#define DISPLAY_HEIGHT       CONFIG_RETRO_DISPLAY_HEIGHT

/* 像素时钟 */
#define DISPLAY_PIXEL_CLK    ((DISPLAY_WIDTH + 144) * DISPLAY_HEIGHT * 60)

/*==========================
 *  内存映射
 *==========================*/

/* Flash（容量来自硬件档案：N16R8=16MB / N8R8=8MB） */
#define ESP32S3_FLASH_BASE    0x4000_0000

/* PSRAM（R8 = 8MB Octal） */
#define ESP32S3_PSRAM_BASE    0x3C00_0000

/* SRAM */
#define ESP32S3_SRAM_BASE     0x3FF9_0000
#define ESP32S3_SRAM_SIZE     (512 * 1024)          /* 512KB */

/*==========================
 *  中断定义
 *==========================*/

/* 外部中断 */
#define ESP32S3_GPIO_IRQ_0    0    /* GPIO 0-4 中断 */
#define ESP32S3_GPIO_IRQ_1    1    /* GPIO 5-31 中断 */
#define ESP32S3_GPIO_IRQ_2    2    /* GPIO 32+ 中断 */

/* DMA 中断 */
#define ESP32S3_DMA_INTR      9    /* GPS DMA 中断 */

/*==========================
 *  时钟定义
 *==========================*/

#define ESP32S3_CPU_FREQ      240*1000*1000   /* 240 MHz */
#define ESP32S3_APB_FREQ      80*1000*1000    /* 80 MHz */
#define ESP32S3_SLOW_FREQ    40*1000*1000    /* 40 MHz */

/*==========================
 *  启动配置
 *==========================*/

/* Bootloader 地址 */
#define BOOTLOADER_OFFSET     0x1000
#define BOOTLOADER_MAXSIZE   (48*1024)

/* 分区表地址 */
#define PARTITION_TABLE_ADDR  0x8000

/* NVS 地址 */
#define NVS_OFFSET            0x10000
#define NVS_SIZE              (32*1024)

#endif /* __BOARD_H */
