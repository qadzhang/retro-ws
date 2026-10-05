/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_pinyin.h - 拼音输入法 GUI 组件接口
 *
 * WHAT : Win95 式拼音 IME 面板（拼音串 + 1-9 候选按钮 + 中英切换）
 * WHY  : GUI 全环境中文输入（REQUIREMENTS 2.1.3）；组件此前无头文件
 *        （2026-10-05 补，随模拟器截图与键盘路径接线）
 * WHO  : desktop 文本框焦点接线、tools/sim 模拟器
 * WHERE: esp32-retro-ws/src/lvgl/app/app_pinyin.h
 * WHEN : 2026-10-05 新增
 * HOW  : init(parent) 建面板；set_target 绑定 textarea（选字注入）；
 *        input(ch) 键盘喂入（字母=拼音、数字=选字、退格、Esc 清）
 */

#ifndef __APP_PINYIN_H
#define __APP_PINYIN_H

#include "lvgl/lvgl.h"

int  app_pinyin_init(lv_obj_t *parent);
void app_pinyin_set_target(lv_obj_t *ta);
void app_pinyin_set_commit_callback(void (*cb)(uint16_t ch));
int  app_pinyin_input(uint32_t ch);   /* 0=已处理 -1=未处理 */

#endif /* __APP_PINYIN_H */
