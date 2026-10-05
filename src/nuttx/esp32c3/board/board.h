/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * board.h - 合宙 ESP32-C3 核心板板级配置（CLI 工作站，第三目标）
 *
 * WHAT : esp32c3 目标的非引脚配置（引脚全部在硬件档案中）
 * WHY  : 低资源/低价格目标，RISC-V 架构，纯 CLI + 包管理器
 * WHO  : src/nuttx/esp32c3/ 目标代码
 * WHERE: retro-ws/src/nuttx/esp32c3/board/board.h
 * WHEN : 2026-10-04 新增
 * HOW  : #include 硬件档案 + 外设参数；无显示/音频配置（CLI-only）
 */

#ifndef __BOARD_H
#define __BOARD_H

/* 板卡硬件档案（引脚唯一事实来源 / single source of pin truth） */
#include "hw_esp32c3_luatos.h"

/*==========================
 *  外设配置
 *==========================*/

/* SD 卡 SPI 配置 */
#define SD_SPI_HOST             2      /* SPI2 */
#define SD_SPI_FREQ             25000000  /* 25MHz */

/* 软件模拟 I2C (RTC) */
#define I2C_FREQ                400000  /* 400kHz Fast Mode */

/*==========================
 *  时钟定义
 *==========================*/

#define ESP32C3_CPU_FREQ        160000000      /* 160 MHz（C3 上限） */
#define ESP32C3_APB_FREQ        80000000       /* 80 MHz */

/*==========================
 *  启动配置
 *==========================*/

#define BOOTLOADER_OFFSET       0x0            /* C3 从 0x0 引导 */
#define PARTITION_TABLE_ADDR    0x8000
#define NVS_OFFSET              0x9000
#define NVS_SIZE                (16*1024)
#define APP_OFFSET              0x10000
#define APP_MAXSIZE             (0x400000 - 0x10000)  /* ~3.9MB */

#endif /* __BOARD_H */
