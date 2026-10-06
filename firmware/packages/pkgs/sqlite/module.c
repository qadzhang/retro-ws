/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* sqlite 模块描述符：SQLite 工具 GUI 应用（.rpk 包交付，2026-10-06） */
#include <nuttx/config.h>
#include "lvgl/lvgl.h"
#include "retro_app_module.h"

extern lv_obj_t *sqlite_gui_create(void);

static lv_obj_t *sqlite_app_create(void)
{
    return sqlite_gui_create();
}

const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "sqlite",
    .title_zh    = "SQLite 工具",
    .title_en    = "SQLite Browser",
    .icon        = "[D]",
    .create      = sqlite_app_create,
};
