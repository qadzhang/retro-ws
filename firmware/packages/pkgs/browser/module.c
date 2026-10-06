/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* browser 模块描述符：简易浏览器 GUI 应用（.rpk 包交付，2026-10-06） */
#include <nuttx/config.h>
#include "lvgl/lvgl.h"
#include "retro_app_module.h"

extern lv_obj_t *browser_create(void);

static lv_obj_t *browser_app_create(void)
{
    return browser_create();
}

const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "browser",
    .title_zh    = "浏览器",
    .title_en    = "Browser",
    .icon        = "[W]",
    .create      = browser_app_create,
};
