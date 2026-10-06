/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* recorder 模块描述符：录音机 GUI 应用（.rpk 包交付，2026-10-06） */
#include <nuttx/config.h>
#include "lvgl/lvgl.h"
#include "retro_app_module.h"

extern lv_obj_t *recorder_create(void);

static lv_obj_t *recorder_app_create(void)
{
    return recorder_create();
}

const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "recorder",
    .title_zh    = "录音机",
    .title_en    = "Recorder",
    .icon        = "[●]",
    .create      = recorder_app_create,
};
