/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 宿主机语法检查桩 / host syntax-check stub for nuttx/usb/cdcacm.h
 * WHY  : 被测模块按 NuttX 习惯包含它；真实头在
 *        deps/nuttx/include/nuttx/usb/cdcacm.h，宿主机不可用
 * HOW  : 被测文件未使用其中符号，空桩即可（仅让 include 可解析）
 */
#ifndef __TEST_STUB_NUTTX_USB_CDCACM_H
#define __TEST_STUB_NUTTX_USB_CDCACM_H
#endif
