/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: lv_port_disp.h
 * 描述: LVGL 显示端口驱动头文件
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */

/*
 * lv_port_disp.h - LVGL 显示端口头文件
 *
 * WHAT : LVGL 显示端口头文件
 * WHY  : 显示端口对外契约
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/lv_port_disp.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 初始化原型
 */

#ifndef __LV_PORT_DISP_H
#define __LV_PORT_DISP_H

#include <stdint.h>
#include <lvgl/lvgl.h>

int lv_port_disp_init(void);
void lv_port_disp_deinit(void);
lv_display_t *lv_port_disp_get(void);
int lv_port_disp_set_resolution(uint32_t width, uint32_t height);

#endif /* __LV_PORT_DISP_H */
