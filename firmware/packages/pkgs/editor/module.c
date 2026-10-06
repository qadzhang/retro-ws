/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* editor 模块描述符：记事本 GUI 应用（.rpk 包交付，2026-10-06 抽离固件） */
#include <nuttx/config.h>
#include "lvgl/lvgl.h"
#include "retro_app_module.h"

extern lv_obj_t *editor_create(void);

static lv_obj_t *editor_app_create(void)
{
    return editor_create();
}

const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "editor",
    .title_zh    = "记事本",
    .title_en    = "Notepad",
    .icon        = "[T]",
    .create      = editor_app_create,
};
