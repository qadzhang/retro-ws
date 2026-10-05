/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * hid_ascii.h - USB/BLE HID 键码 -> ASCII 映射（纯函数，板无关）
 *
 * WHAT : 把 HID 键盘报告（Usage ID + 修饰键）换算为终端 ASCII 字节流
 * WHY  : 输入优先级原则（REQUIREMENTS 2.2.3，2026-10-05）：USB > 蓝牙 >
 *        串口——USB HID（usb_hid.c）与 BLE HID（NimBLE 移植后）都产生
 *        同格式的键盘报告，共用本映射喂 /dev/cvbscon，避免两套换算
 * WHO  : usb_hid.c（S3 USB 键盘桥）、未来 BLE HID 桥、宿主测试
 * WHERE: retro-ws/src/nuttx/common/driver/hid_ascii.[ch]
 * WHEN : 2026-10-05 新增（输入优先级原则落地的公共件）
 * HOW  : 8 字节标准键盘报告：[0]=修饰键 [1]=保留 [2..7]=Usage 数组；
 *        查 base/shift 两张 256 表 + 控制键映射，report 版做按下沿
 *        检测（cur 有 prev 无）抑制长按重复；CapsLock/LED 不跟踪（v1）
 */

#ifndef __HID_ASCII_H
#define __HID_ASCII_H

#include <stdint.h>
#include <stddef.h>

/* HID 修饰键位（报告字节 0） */
#define HID_MOD_LCTRL    (1u << 0)
#define HID_MOD_LSHIFT   (1u << 1)
#define HID_MOD_LALT     (1u << 2)
#define HID_MOD_LGUI     (1u << 3)
#define HID_MOD_RCTRL    (1u << 4)
#define HID_MOD_RSHIFT   (1u << 5)
#define HID_MOD_RALT     (1u << 6)
#define HID_MOD_RGUI     (1u << 7)
#define HID_MOD_ANY_SHIFT (HID_MOD_LSHIFT | HID_MOD_RSHIFT)

/*
 * 单键换算：usage -> ASCII；不可映射（无键/功能键/未知）返回 0
 *   modifiers - HID 修饰键位集（控制键位不影响输出，v1 忽略 Ctrl/Alt）
 *   usage      - HID Keyboard/Keypad Usage ID（0x04='a' ... 0x38='/'）
 */
int hid_ascii_from_usage(uint8_t modifiers, uint8_t usage);

/*
 * 报告差分：换算 cur 相对 prev 新按下的键为 ASCII，写入 out
 *   prev - 上一份 8 字节报告；NULL 视为全释放（首批全算新按下）
 *   cur  - 当前 8 字节报告（[0]=修饰键 [1]=保留 [2..7]=Usage）
 *   out/out_sz - 输出缓冲（最多 6 键）
 * 返回写出的键数（0..6）；长按重发同键被抑制（仅按下沿出键）
 */
int hid_ascii_report(const uint8_t *prev, const uint8_t *cur,
                     char *out, int out_sz);

#endif /* __HID_ASCII_H */
