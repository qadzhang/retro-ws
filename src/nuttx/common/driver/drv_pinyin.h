/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_pinyin.h - CLI 行内拼音输入法引擎接口
 *
 * WHAT : 行内拼音 IME 引擎（词库匹配/整行编辑）公开接口
 * WHY  : cvbs_ime（CCDOS 式状态条 IME）与 NSH `ime` 命令复用本引擎；
 *        原实现散落在 .c 内无头文件（2026-10-05 补）
 * WHO  : cvbs_ime.c / nsh ime 命令
 * WHERE: retro-ws/src/nuttx/common/driver/drv_pinyin.h
 * WHEN : 2026-10-05 新增（随 CCDOS 式 IME 落地）
 * HOW  : 引擎自持整行编辑缓冲（input_buf）——确认文本经
 *        cli_pinyin_input 累积、Enter（返回 1）后 cli_getline 取整行
 */

#ifndef __DRV_PINYIN_H
#define __DRV_PINYIN_H

#include <stdbool.h>

void cli_pinyin_init(void);
void cli_pinyin_reset(void);
void cli_pinyin_toggle_mode(void);
void cli_pinyin_set_mode(int mode);      /* 0=英文 1=中文 */
int  cli_pinyin_get_mode(void);

/* 喂一个按键：字母/数字/退格/Esc 进入引擎；返回 1=行完成(Enter) */
int  cli_pinyin_input(int ch);

/* 当前拼音串（状态条显示用；无输入为空串） */
const char *cli_pinyin_get_pinyin(void);

/* 已确认文本（UTF-8 整行缓冲）与长度 */
const char *cli_pinyin_get_input(void);
int  cli_pinyin_get_input_len(void);

/*
 * WHAT : 当前拼音的候选列表（词组在前、单字在后，合并编号）
 * HOW  : out 每项为 UTF-8 字符串指针（词组指向词库、单字写入静态
 *        缓冲——调用方立即消费，勿跨调用保存）；返回总数
 */
int  cli_pinyin_candidates(const char *out[], int max);

#endif /* __DRV_PINYIN_H */
