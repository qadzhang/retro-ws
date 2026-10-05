/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : CLI 固件构建的字体编译桥（lvgl/lvgl.h -> 兼容层）
 * WHY  : 字体文件 include "lvgl/lvgl.h"；无 LVGL 的 CLI 档经
 *        lvgl_font_compat.h 编译同一字体文件（AGENTS.md 7.3 唯一字体）
 * HOW  : 构建把本目录放在 -I 最前（真 LVGL 构建时 deps 更优先）
 */
#include "lvgl_font_compat.h"
