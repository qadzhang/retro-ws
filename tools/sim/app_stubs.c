/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * app_stubs.c - 模拟器的应用窗口桩 / app window stubs for the simulator
 *
 * WHAT : 提供 desktop.c 引用的六个应用 create 函数的最小实现
 * WHY  : 模拟器聚焦桌面外壳本身；真实应用窗口依赖
 *        sqlite/wav/网络等设备，无头环境不可用
 * WHO  : tools/sim/build.sh 链接
 * WHERE: retro-ws/tools/sim/app_stubs.c
 * WHEN : 2026-10-04 新增
 * HOW  : 每个桩创建带标题标签的窗口对象，验证桌面装配不空转
 */

#include <nuttx/config.h>

#include "lvgl/lvgl.h"

static lv_obj_t *stub_window(const char *title, lv_color32_t tint)
{
    lv_obj_t *win = lv_obj_create(lv_screen_active());
    lv_obj_set_size(win, 320, 200);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_make(tint.red, tint.green,
                                                 tint.blue), 0);

    lv_obj_t *lbl = lv_label_create(win);
    lv_label_set_text(lbl, title);
    lv_obj_center(lbl);
    return win;
}

void browser_create(void)
{
    stub_window("Browser (stub)", lv_color32_make(0xC0, 0xD8, 0xF0, 0xFF));
}

void editor_create(void)
{
    stub_window("Editor (stub)", lv_color32_make(0xF0, 0xF0, 0xD0, 0xFF));
}

void player_create(void)
{
    stub_window("Player (stub)", lv_color32_make(0xD0, 0xF0, 0xD0, 0xFF));
}

void recorder_create(void)
{
    stub_window("Recorder (stub)", lv_color32_make(0xF0, 0xD8, 0xE0, 0xFF));
}

void sqlite_gui_create(void)
{
    stub_window("SQLite (stub)", lv_color32_make(0xD8, 0xE8, 0xF0, 0xFF));
}

void terminal_create(void)
{
    stub_window("Terminal (stub)", lv_color32_make(0x10, 0x10, 0x10, 0xFF));
}
