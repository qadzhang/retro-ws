/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * pinyin_ime.h - 拼音输入法引擎接口
 *
 * WHAT : 拼音输入法引擎接口（pinyin_ime.c 对外契约）
 * WHY  : app_pinyin.c / drv_pinyin.c 共享词库查询 API
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/fonts/pinyin_ime.h
 * WHEN : 2026-03~04 初版，2026-10-04 补齐缺失头文件（5W1H, AGENTS.md 4.0）
 * HOW  : 导出 pinyin_ime.c 的真实函数原型与词组长度常量
 */

#ifndef __PINYIN_IME_H
#define __PINYIN_IME_H

#include <stdint.h>

/*======================================
 *  常量 / Constants
 *======================================*/
#define PINYIN_IME_MAX_PHRASE_LEN  4   /* 词组最大字数 / Max phrase length */
#define PINYIN_IME_MAX_CANDIDATES  9   /* 最大候选数 / Max candidates */

#ifdef __cplusplus
extern "C" {
#endif

/*======================================
 *  引擎生命周期 / Engine lifecycle
 *======================================*/

/* 初始化输入法状态 / Initialize IME state */
void pinyin_ime_init(void);

/* 清空当前输入与候选 / Clear current input and candidates */
void pinyin_ime_clear(void);

/*======================================
 *  输入 / Input
 *======================================*/

/* 输入一个拼音字母(a-z) / Feed one pinyin letter; 返回 1=有候选 / 1=candidates ready */
int pinyin_ime_input(char ch);

/* 删除最后一个拼音字母 / Delete last pinyin letter */
int pinyin_ime_backspace(void);

/* 按当前拼音重新搜索候选 / Re-search candidates for current pinyin */
int pinyin_ime_search(void);

/*======================================
 *  查询 / Query
 *======================================*/

/* 当前拼音串 / Current pinyin string */
const char *pinyin_ime_get_pinyin(void);

/* 拷贝候选列表到 callers 缓冲 / Copy candidate list out */
int pinyin_ime_get_candidates(uint16_t *candidates);

/* 候选数量 / Candidate count */
int pinyin_ime_get_cand_count(void);

/* 是否有候选 / Has candidates (1/0) */
int pinyin_ime_has_candidates(void);

/*======================================
 *  选择与词库 / Selection & dictionary
 *======================================*/

/* 选择候选(0-8 对应键 1-9)，输出 GB2312 码 / Select candidate, output GB2312 code */
int pinyin_ime_select(int index, uint16_t *out_hanzi);

/* 拼音字符串转索引表下标，-1 未找到 / Pinyin string to index, -1 if not found */
int pinyin_to_index(const char *pinyin);

/* 按拼音索引查单字 / Find single chars by pinyin index */
int find_hanzi_by_pinyin(int pinyin_idx, uint16_t *results, int max_results);

/* 按拼音串查词组 / Find phrases by pinyin string */
int find_phrases_by_pinyin(const char *pinyin,
                           uint16_t (*results)[PINYIN_IME_MAX_PHRASE_LEN],
                           int max_results);

#ifdef __cplusplus
}
#endif

#endif /* __PINYIN_IME_H */
