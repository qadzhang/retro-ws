/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * board.h - Pico 板级公共定义
 *
 * WHAT : 板级符号导出（占用表实例化 + 复位原因桩）
 * WHY  : 与其他三块板同构（board.c 唯一符号归属）
 * WHERE: esp32-retro-ws/src/nuttx/rp2040/board/board.h
 * WHEN : 2026-10-04 新增
 * HOW  : 包含硬件档案；声明 board_* 接口
 */

#ifndef __RP2040_RETRO_BOARD_H
#define __RP2040_RETRO_BOARD_H

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw_rp2040_pico.h"

uint32_t board_get_reset_reason(void);
bool board_is_watchdog_reset(void);
const char *board_get_version(void);

#endif /* __RP2040_RETRO_BOARD_H */
