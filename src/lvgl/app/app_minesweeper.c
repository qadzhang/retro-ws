/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * app_minesweeper.c - 扫雷游戏（ROM 模块应用）
 *
 * WHAT : 经典扫雷游戏窗口（9x9 棋盘静态展示版），ROM .rmo 模块形态
 * WHY  : 应用/系统分离（2026-10-06）——游戏属应用软件，从 desktop.c
 *        内嵌件抽离为 .rpk 包交付（名单默认安装、XIP 原址执行）；
 *        桌面经 retro_desk_win_create 托管窗口
 * WHO  : desktop.c 模块注册表（描述符消费方）；minesweeper 包构建
 * WHERE: retro-ws/src/lvgl/app/app_minesweeper.c
 * WHEN : 2026-10-06 从 desktop.c 提取（原 create_minesweeper）
 * HOW  : 模块入口 = retro_gui_app_info 描述符（magic/ABI/标题/图标/
 *        create）；窗口经固件导出的 retro_desk_win_create 创建，
 *        关窗由桌面窗口管理器回调（LVGL DELETE 事件触发模块卸载）
 */

#include <nuttx/config.h>

#include <string.h>

#include "lvgl/lvgl.h"
#include "retro_win3_styles.h"
#include "retro_font.h"
#include "desktop_api.h"
#include "retro_app_module.h"

/*
 * WHAT : 创建扫雷主窗口（模块描述符的 create 入口）
 * HOW  : retro_desk_win_create 建托管窗（标题栏/关闭按钮/层级），
 *        客户区内摆 9x9 棋盘与雷数/计时标签（静态演示棋盘）
 * 返回 : 窗口根对象（桌面挂 DELETE 回调据此卸载模块）
 */
static lv_obj_t *minesweeper_create(void)
{
    lv_obj_t *win = retro_desk_win_create("Minesweeper", 120, 80, 240, 280);

    if (win == NULL)
        return NULL;

    /* 客户区：retro_desk_win_create 返回窗口根，子件挂根上（标题栏
     * 之外的整个区域即客户区——与 desktop 内嵌版布局一致） */
    lv_obj_t *game_area = lv_obj_create(win);
    lv_obj_set_size(game_area, 200, 200);
    lv_obj_set_pos(game_area, 20, 40);
    lv_obj_set_style_bg_color(game_area, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(game_area, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(game_area, WIN3_BORDER_HI, LV_PART_MAIN);

    lv_obj_t *info_lbl = lv_label_create(win);
    lv_label_set_text(info_lbl, "Mines: 10\nTimer: 000");
    lv_obj_set_pos(info_lbl, 20, 22);
    lv_obj_set_style_text_font(info_lbl, RETRO_FONT_DEFAULT, 0);

    return win;
}

/* 模块应用描述符（.rodata 常驻 Flash；桌面 rommod_getsym 读取） */
const struct retro_gui_app_info_s retro_gui_app_info =
{
    .magic       = RETRO_APP_MAGIC,
    .abi_version = RETRO_APP_ABI_VERSION,
    .app_id      = "minesweeper",
    .title_zh    = "扫雷",
    .title_en    = "Minesweeper",
    .icon        = "[#]",
    .create      = minesweeper_create,
};
