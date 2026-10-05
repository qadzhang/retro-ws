/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 极简 C 测试框架 / minimal host test framework
 * WHY  : 嵌入式项目无平台测试设施；变异测试需要"失败即非零退出码"
 * WHO  : tests/host/ 下全部 C 测试（test_pkgmanager.c 等）
 * WHERE: retro-ws/tests/host/test_framework.h
 * WHEN : 2026-10-04 新增
 * HOW  : CHECK* 宏：失败打印 文件:行 与表达式，累计失败数；
 *        main 返回 g_fail_count（0 = 全过，非零供 CI/变异脚本判定）
 */
#ifndef __TEST_FRAMEWORK_H
#define __TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail_count = 0;
static int g_check_count = 0;

#define CHECK(cond) do {                                     \
    g_check_count++;                                          \
    if (!(cond)) {                                            \
        g_fail_count++;                                       \
        fprintf(stderr, "FAIL %s:%d: %s\n",                   \
                __FILE__, __LINE__, #cond);                   \
    }                                                         \
} while (0)

#define CHECK_EQ_INT(actual, expect) do {                     \
    g_check_count++;                                          \
    long _a = (long)(actual), _e = (long)(expect);            \
    if (_a != _e) {                                           \
        g_fail_count++;                                       \
        fprintf(stderr, "FAIL %s:%d: %s == %ld, expect %ld\n",\
                __FILE__, __LINE__, #actual, _a, _e);         \
    }                                                         \
} while (0)

#define CHECK_STR_EQ(actual, expect) do {                     \
    g_check_count++;                                          \
    const char *_a = (actual), *_e = (expect);                \
    if (_a == NULL || _e == NULL || strcmp(_a, _e) != 0) {    \
        g_fail_count++;                                       \
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n",     \
                __FILE__, __LINE__,                           \
                _a ? _a : "(null)", _e ? _e : "(null)");      \
    }                                                         \
} while (0)

#define TEST_REPORT(name) do {                                \
    printf("[%s] %d checks, %d failed -> %s\n",               \
           (name), g_check_count, g_fail_count,               \
           g_fail_count == 0 ? "PASS" : "FAIL");              \
    return g_fail_count == 0 ? 0 : 1;                         \
} while (0)

#endif /* __TEST_FRAMEWORK_H */
