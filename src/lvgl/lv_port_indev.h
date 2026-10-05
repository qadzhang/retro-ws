/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: lv_port_indev.h
 * 描述: LVGL 输入设备端口驱动头文件
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */

/*
 * lv_port_indev.h - LVGL 输入设备端口头文件
 *
 * WHAT : LVGL 输入设备端口头文件
 * WHY  : 输入端口对外契约
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/lv_port_indev.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 初始化原型
 */

#ifndef __LV_PORT_INDEV_H
#define __LV_PORT_INDEV_H

#include <stdint.h>
#include <lvgl/lvgl.h>

int lv_port_indev_init(void);
void lv_port_indev_deinit(void);

lv_indev_t *lv_port_keypad_indev_get(void);
lv_indev_t *lv_port_mouse_indev_get(void);
lv_indev_t *lv_port_encoder_indev_get(void);

void lv_port_indev_set_encoder_diff(int8_t diff);

/* 按键和鼠标数据注入接口 / Key and mouse data injection API */
void lv_port_indev_push_key(uint32_t key);
void lv_port_indev_set_mouse(int32_t x, int32_t y, int8_t btn);

#endif /* __LV_PORT_INDEV_H */
