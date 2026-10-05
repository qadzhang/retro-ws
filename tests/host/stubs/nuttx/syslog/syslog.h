/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX syslog 路径的宿主机桩 / host-test stub
 * WHY  : NuttX 的 syslog 头在 <nuttx/syslog/syslog.h>，宿主机直接转发
 * HOW  : 转发 glibc <syslog.h>，LOG_* 级别宏两者一致
 */
#ifndef __TEST_STUB_NUTTX_SYSLOG_H
#define __TEST_STUB_NUTTX_SYSLOG_H
#include <syslog.h>
#endif
