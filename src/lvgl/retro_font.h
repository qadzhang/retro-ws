/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_font.h - 项目唯一字体 / the single project font
 *
 * WHAT : 全部界面代码通过 RETRO_FONT_DEFAULT 取字体（AGENTS.md 7.3：
 *        唯一字号，禁止第二套中文字库/第二档字号）
 * WHY  : 嵌入式体积优先（用户定稿 2026-10-05）——全系（CLI+GUI、
 *        320x240 控制台 + 640x480 桌面）共用 12px；12px 恰为中文
 *        Windows 3.2/95 界面宋体 9pt 点阵的历史标准字号
 * WHO  : src/lvgl 全部 UI 代码、cvbs_console、tools/sim 模拟器
 * WHERE: esp32-retro-ws/src/lvgl/retro_font.h
 * WHEN : 2026-10-04 定稿；2026-10-05 由两档收敛为单一 12px 档
 * HOW  : GUI 构建直接引用 LVGL 字体符号；CLI 构建（无 LVGL）经
 *        lvgl_font_compat.h 同一字体文件编译（见 cvbs_console）
 */

#ifndef __RETRO_FONT_H
#define __RETRO_FONT_H

#include "lvgl/lvgl.h"

extern const lv_font_t lv_font_notosans_sc_12;

/* 唯一字号：GUI 桌面 / CLI 控制台 / 模拟器 全部使用 */
#define RETRO_FONT_DEFAULT   (&lv_font_notosans_sc_12)
#define RETRO_FONT_CONSOLE   (&lv_font_notosans_sc_12)

#endif /* __RETRO_FONT_H */
