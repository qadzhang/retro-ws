/*
 * SPDX-FileCopyrightText: 2026 ESP32-Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cvbs_ime.h - CCDOS 式 AV 控制台输入法（状态条 IME）接口
 *
 * WHAT : CLI 拼音输入法的屏幕形态：ime on 后屏幕最下方常驻一行
 *        反色输入法条（拼音串+候选字），键盘由 IME 先行接管，
 *        Enter 确认后整行回放给终端——DOS 时代 CCDOS/UCDOS 的
 *        显示处理方式（用户需求 2026-10-05）
 * WHY  : CLI 全环境中文输入（REQUIREMENTS 2.1.3）；不启动时完全
 *        不显示、不占用屏幕与按键（NEXT_STEPS 20 落地）
 * WHO  : cvbs_console 键盘泵（拦截）、NSH `ime` 命令（开关）
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/cvbs_ime.h
 * WHEN : 2026-10-05 新增
 * HOW  : 组合键（UART 字节流）：Ctrl+Space=0x00 中英切换、
 *        Ctrl+Q=0x11 关闭 IME 释放状态条；数字 1-9 选字、空格=?
 *        （空格保留给终端）、Enter=首选上屏+行结束
 */

#ifndef __CVBS_IME_H
#define __CVBS_IME_H

#include <stdbool.h>

/* IME 是否激活（键盘泵查询：激活时键先喂 cvbs_ime_feed） */
bool cvbs_ime_active(void);

/* 开/关：开=cli_pinyin 中文模式 + 占用底部状态条；关=释放整屏 */
void cvbs_ime_enable(bool on);

/*
 * 喂一个按键（仅激活时有效）
 * 返回 1=IME 已消费（泵不得送终端）；0=IME 未激活/组合键关掉后
 * 的首个键之后由泵直通
 */
int  cvbs_ime_feed(int ch);

/* 条文本（调试/测试断言用）：'拼音[ni] 1你 2尼...' / '英文' / '中文' */
const char *cvbs_ime_statusline(void);

#endif /* __CVBS_IME_H */
