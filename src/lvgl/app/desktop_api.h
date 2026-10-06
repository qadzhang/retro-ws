/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * desktop_api.h - 桌面外壳公共接口
 *
 * WHAT : 桌面外壳公共接口
 * WHY  : 外壳切换与应用启动的跨模块契约（wmaker_shell.c / nsh_cmds.c 共用）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/desktop_api.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : RETRO_SHELL_* 模式宏 + ready/app_launch/set_shell/get_shell 四个函数声明
 */

#ifndef __DESKTOP_API_H
#define __DESKTOP_API_H

#include <nuttx/config.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

/* 外壳模式 / Shell modes */
#define RETRO_SHELL_WIN3       0   /* Windows 3.2 任务栏风格（默认） */
#define RETRO_SHELL_WMAKER     1   /* WindowMaker/NeXT 风格（Dock + 右键菜单） */

/* 桌面是否已初始化 / whether desktop is initialized */
int  retro_desktop_ready(void);

/* 按应用标识启动窗口 / launch an app window by id
 * 返回 OK / -ENOENT / -ENOSYS（桌面未运行） */
int  retro_desktop_app_launch(const char *app_id);

/* 切换桌面外壳 / switch desktop shell
 * mode: RETRO_SHELL_WIN3 或 RETRO_SHELL_WMAKER */
int  retro_desktop_set_shell(int mode);

/* 当前外壳模式 / current shell mode */
int  retro_desktop_get_shell(void);

/*
 * WHAT : 桌面窗口 API（导出给 ROM 模块应用，2026-10-06 应用/系统分离）
 * WHY  : 模块应用经固件符号表解析本入口，创建受窗口管理器托管的
 *        窗口（标题栏/关闭/层级/激活），无需链编 desktop 内部符号
 * 返回 : 窗口根对象；NULL = 窗口数满
 */
#ifdef CONFIG_LVGL
lv_obj_t *retro_desk_win_create(const char *title, int x, int y,
                                int w, int h);
#endif

#endif /* __DESKTOP_API_H */
