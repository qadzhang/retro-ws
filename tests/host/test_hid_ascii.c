/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_hid_ascii.c - HID 键码 -> ASCII 映射测试
 *
 * WHAT : hid_ascii 纯函数的契约测试（单键换算 + 报告差分）
 * WHY  : USB/BLE 键盘桥共用本映射喂 /dev/cvbscon（输入优先级原则，
 *        REQUIREMENTS 2.2.3）——映射错一个键终端就打错一个字，
 *        长按重发不抑制会连打；性质必须在宿主端锁死
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: retro-ws/tests/host/test_hid_ascii.c
 * WHEN : 2026-10-05 新增（随 hid_ascii.c 落地）
 * HOW  : CHECK 断言：字母/数字/符号两档、控制键约定（Enter=CR、
 *        BS=0x08）、不可映射拒绝（F 键/空 usage）、按下沿差分
 *        （重发抑制/多键/释放/新按/截断/防御参数）
 */

#include <stdio.h>
#include <string.h>

#include "test_framework.h"

#include "hid_ascii.h"

/* 造一份 8 字节键盘报告到 dst：[0]=修饰键 [1]=0 [2..7]=Usage */
static uint8_t *mk_report(uint8_t *dst, uint8_t mods, int u1, int u2)
{
    memset(dst, 0, 8);
    dst[0] = mods;
    if (u1 > 0)
        dst[2] = (uint8_t)u1;
    if (u2 > 0)
        dst[3] = (uint8_t)u2;
    return dst;
}

static void test_single_usage(void)
{
    /* 字母两档 */
    CHECK(hid_ascii_from_usage(0, 0x04) == 'a');
    CHECK(hid_ascii_from_usage(HID_MOD_LSHIFT, 0x04) == 'A');
    CHECK(hid_ascii_from_usage(HID_MOD_RSHIFT, 0x1D) == 'Z');
    CHECK(hid_ascii_from_usage(0, 0x1D) == 'z');

    /* 数字行两档 */
    CHECK(hid_ascii_from_usage(0, 0x1E) == '1');
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x1E) == '!');
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x27) == ')');
    CHECK(hid_ascii_from_usage(0, 0x38) == '/');
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x38) == '?');

    /* 标点第二档 */
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x33) == ':');
    CHECK(hid_ascii_from_usage(0, 0x33) == ';');
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x30) == '}');
    CHECK(hid_ascii_from_usage(0, 0x31) == '\\');
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x31) == '|');

    /* 控制键约定（与 cvbs_console 控制序列对齐） */
    CHECK(hid_ascii_from_usage(0, 0x28) == '\r');
    CHECK(hid_ascii_from_usage(0, 0x29) == 0x1B);
    CHECK(hid_ascii_from_usage(0, 0x2A) == 0x08);
    CHECK(hid_ascii_from_usage(0, 0x2B) == '\t');
    CHECK(hid_ascii_from_usage(0, 0x2C) == ' ');

    /* shift 不改变控制键语义 */
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x28) == '\r');

    /* 不可映射：空 usage / F1(0x3A) */
    CHECK(hid_ascii_from_usage(0, 0x00) == 0);
    CHECK(hid_ascii_from_usage(0, 0x3A) == 0);
    CHECK(hid_ascii_from_usage(HID_MOD_ANY_SHIFT, 0x3A) == 0);
}

static void test_report_diff(void)
{
    uint8_t prev[8], cur[8];
    char out[6];
    int n;

    /* 首报（prev=NULL）：单键全算新按下 */
    n = hid_ascii_report(NULL, mk_report(cur, 0, 0x04, 0), out, sizeof(out));
    CHECK(n == 1);
    CHECK(n >= 1 && out[0] == 'a');

    /* 长按重发：同 usage 被抑制 */
    memcpy(prev, cur, 8);
    n = hid_ascii_report(prev, mk_report(cur, 0, 0x04, 0), out, sizeof(out));
    CHECK(n == 0);

    /* 双键同报 */
    n = hid_ascii_report(NULL, mk_report(cur, 0, 0x04, 0x1E), out, sizeof(out));
    CHECK(n == 2);
    CHECK(n >= 2 && out[0] == 'a' && out[1] == '1');

    /* 全释放不出键 */
    memcpy(prev, cur, 8);
    n = hid_ascii_report(prev, mk_report(cur, 0, 0, 0), out, sizeof(out));
    CHECK(n == 0);

    /* 'a' 保持按住、新按 '1'：只出新键 */
    memcpy(prev, mk_report(prev, 0, 0x04, 0), 8);
    n = hid_ascii_report(prev, mk_report(cur, 0, 0x04, 0x1E), out, sizeof(out));
    CHECK(n == 1);
    CHECK(n >= 1 && out[0] == '1');

    /* shift 组合在按下沿生效 */
    n = hid_ascii_report(NULL, mk_report(cur, HID_MOD_LSHIFT, 0x04, 0),
                         out, sizeof(out));
    CHECK(n == 1);
    CHECK(n >= 1 && out[0] == 'A');

    /* 不可映射键不占输出 */
    n = hid_ascii_report(NULL, mk_report(cur, 0, 0x3A, 0x05), out, sizeof(out));
    CHECK(n == 1);
    CHECK(n >= 1 && out[0] == 'b');

    /* out_sz 截断：2 键缓冲只出 1 */
    n = hid_ascii_report(NULL, mk_report(cur, 0, 0x04, 0x05), out, 1);
    CHECK(n == 1);

    /* 防御参数 */
    CHECK(hid_ascii_report(NULL, NULL, out, sizeof(out)) == 0);
    CHECK(hid_ascii_report(NULL, mk_report(cur, 0, 0x04, 0), NULL, 6) == 0);
    CHECK(hid_ascii_report(NULL, mk_report(cur, 0, 0x04, 0), out, 0) == 0);
}

int main(void)
{
    test_single_usage();
    test_report_diff();

    printf("hid_ascii: %d checks, %d failed\n", g_check_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
