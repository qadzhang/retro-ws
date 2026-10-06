/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* player 模块描述符：媒体播放器 GUI 应用（含 WAV 解码器，.rpk 交付） */
#include <nuttx/config.h>
#include "lvgl/lvgl.h"
#include "retro_app_module.h"

extern lv_obj_t *player_create(void);

static lv_obj_t *player_app_create(void)
{
    return player_create();
}

const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "player",
    .title_zh    = "媒体播放器",
    .title_en    = "Media Player",
    .icon        = "[♪]",
    .create      = player_app_create,
};
