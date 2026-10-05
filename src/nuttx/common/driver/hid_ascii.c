/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * hid_ascii.c - USB/BLE HID 键码 -> ASCII 映射（纯函数，板无关）
 *
 * WHAT/WHY/WHERE/HOW 见 hid_ascii.h 头注释（唯一事实来源）
 * WHEN : 2026-10-05 新增；同日宿主测试 test_hid_ascii.c 锁行为
 */

#include "hid_ascii.h"

/* 不按 shift 时的 Usage ID -> ASCII（0 = 不可映射）。
 * 覆盖 104 键主区：字母/数字/标点/控制键；F 键与导航键区不映射（v1） */
static const unsigned char g_base[256] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0',
    [0x28] = '\r',   /* Enter -> CR（终端约定） */
    [0x29] = 0x1B,   /* Escape */
    [0x2A] = 0x08,   /* Backspace（ANSI BS，cvbs_console 控制序列同值） */
    [0x2B] = '\t',   /* Tab */
    [0x2C] = ' ',
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x32] = '#', [0x33] = ';', [0x34] = '\'',
    [0x35] = '`', [0x36] = ',', [0x37] = '.', [0x38] = '/',
};

/* 按 shift 时的第二档（仅与 g_base 不同的档位；字母的大写在函数里
 * 用 toupper 逻辑处理，不占表）；未列出的沿用 g_base 语义 */
static const unsigned char g_shift[256] = {
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$',
    [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
    [0x26] = '(', [0x27] = ')',
    [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}',
    [0x31] = '|', [0x33] = ':', [0x34] = '"', [0x35] = '~',
    [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

int hid_ascii_from_usage(uint8_t modifiers, uint8_t usage)
{
    unsigned char b, s;

    if (usage == 0)
        return 0;

    b = g_base[usage];
    if (b == 0)
        return 0;                    /* 基础档不可映射（F 键/导航键/未知） */

    if (modifiers & HID_MOD_ANY_SHIFT) {
        s = g_shift[usage];
        if (s != 0)
            return s;                /* 显式第二档（!@#$ 等） */
        if (b >= 'a' && b <= 'z')
            return b - 'a' + 'A';    /* 字母档 shift = 大写 */
        return b;                    /* 控制键（Enter/Tab 等）shift 不改值 */
    }

    return b;
}

int hid_ascii_report(const uint8_t *prev, const uint8_t *cur,
                     char *out, int out_sz)
{
    int n = 0;
    int i, j;

    if (cur == NULL || out == NULL || out_sz <= 0)
        return 0;

    for (i = 2; i < 8 && n < out_sz; i++) {
        uint8_t usage = cur[i];
        int held = 0;

        if (usage == 0)
            continue;

        /* 按下沿检测：usage 已在上一份报告里则不是新按下 */
        if (prev != NULL) {
            for (j = 2; j < 8; j++) {
                if (prev[j] == usage) {
                    held = 1;
                    break;
                }
            }
        }
        if (held)
            continue;

        {
            int c = hid_ascii_from_usage(cur[0], usage);
            if (c > 0)
                out[n++] = (char)c;
        }
    }

    return n;
}
