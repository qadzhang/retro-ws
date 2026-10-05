/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui.h - retro_ui 胶水层头文件
 *
 * WHAT : retro_ui 胶水层头文件
 * WHY  : 统一 UI 接口契约（AGENTS.md 8.1）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/retro_ui.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : retro_ui_* 函数原型
 */

#ifndef RETRO_UI_H
#define RETRO_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化/反初始化 / Init/Deinit */
void retro_ui_init(void);
void retro_ui_deinit(void);

/* 消息框 / Message box */
int retro_ui_msgbox(const char *title, const char *msg);

/* 输入对话框 / Input dialog */
int retro_ui_input(const char *title, const char *prompt, char *buf, int bufsize);

/* 列表选择 / List selection */
int retro_ui_list(const char *title, const char *prompt,
                  const char **items, int count);

/* 确认对话框 / Confirm dialog */
int retro_ui_confirm(const char *title, const char *msg);

/* 状态栏 / Status bar */
int retro_ui_status(const char *msg);

/* 进度条 / Progress bar */
int retro_ui_progress(int value, int max);

/* 窗口操作 / Window operations */
int retro_ui_close_window(const char *win_title);

/* 语言设置 / Language settings */
int retro_ui_set_lang(const char *lang);
const char *retro_ui_get_lang(void);

#ifdef __cplusplus
}
#endif

#endif /* RETRO_UI_H */
