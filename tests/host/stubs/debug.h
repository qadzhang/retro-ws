/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 宿主机测试用的 debug.h 桩 / host-test stub for <debug.h>
 * WHY  : 被测模块包含 <debug.h>（NuttX 调试日志入口），宿主机不存在；
 * HOW  : 真实 NuttX debug.h 最终提供 syslog 宏；桩转发到已有的
 *        nuttx/syslog/syslog.h 桩（再转 glibc syslog）
 */
#ifndef __TEST_STUB_DEBUG_H
#define __TEST_STUB_DEBUG_H

#include <nuttx/syslog/syslog.h>

#endif
