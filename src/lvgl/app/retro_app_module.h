/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_app_module.h - GUI 应用模块 ABI（桌面 <-> .rmo 模块契约）
 *
 * WHAT : ROM 应用模块（.rmo）对桌面外壳导出的应用描述符约定
 * WHY  : 应用/系统分离（2026-10-06）——GUI 应用以 .rpk 包交付、
 *        代码 XIP 常驻 Flash，桌面经 rommod 装载后按本描述符
 *        注册图标/启动窗口，模块与固件仅经此 ABI + LVGL 符号表耦合
 * WHO  : 各 GUI 应用模块（descriptor 定义方）与 desktop.c（消费方）；
 *        构建器 scripts/build_romapps.sh 校验 descriptor 存在
 * WHERE: retro-ws/src/lvgl/app/retro_app_module.h（固件与模块共用）
 * WHEN : 2026-10-06 新增
 * HOW  : 模块内定义全局只读符号 retro_gui_app_info（const 结构体，
 *        常驻模块 RO 段=Flash）；桌面 rommod_getsym 取地址后读出
 *        函数指针与标题串（指针已在装载期重定位到运行期地址）
 */

#ifndef __RETRO_APP_MODULE_H
#define __RETRO_APP_MODULE_H

#include <nuttx/config.h>

#include <stdint.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

/* 描述符魔数（小端 'RGAM'）与 ABI 版本（不兼容变更时递增） */
#define RETRO_APP_MAGIC       0x5247414Du
#define RETRO_APP_ABI_VERSION 1u

/*
 * GUI 应用描述符（模块 .rodata 内的只读全局符号）
 *
 * 字段说明：
 *   magic/abi_version - 桌面装载后校验，防误把非应用模块当 GUI 应用
 *   app_id            - 桌面派发标识（desktop app_launch 的 id，小写）
 *   title_zh/en       - 中英文标题（模块自带，不依赖固件 i18n 表）
 *   icon              - 图标文本（LVGL symbol 字符或 ASCII 简笔）
 *   create            - 创建主窗口；返回窗口根 lv_obj_t（桌面接管
 *                       窗口关闭事件并在末窗关闭后卸载模块）
 */
struct retro_gui_app_info_s {
    uint32_t magic;
    uint32_t abi_version;
    const char *app_id;
    const char *title_zh;
    const char *title_en;
    const char *icon;
#ifdef CONFIG_LVGL
    lv_obj_t *(*create)(void);
#else
    void *(*create)(void);           /* CLI 构建（fontbridge）下的占位类型 */
#endif
};

/* 模块必须导出的符号名（rommod_getsym 按此查找） */
#define RETRO_APP_INFO_SYMBOL "retro_gui_app_info"

#endif /* __RETRO_APP_MODULE_H */
