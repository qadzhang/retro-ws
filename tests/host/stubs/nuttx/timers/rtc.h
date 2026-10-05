/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <nuttx/timers/rtc.h> 的宿主机桩 / host-test stub
 * WHY : drv_rtc.c 按真实路径包含它（原 <nuttx/rtc.h> 不存在），
 *       宿主机上只需要包含成功即可（被测代码未使用其中的符号）
 * HOW  : 定义与真实头一致的 include guard；镜像
 *        deps/nuttx/include/nuttx/timers/rtc.h
 */
#ifndef __TEST_STUB_NUTTX_TIMERS_RTC_H
#define __TEST_STUB_NUTTX_TIMERS_RTC_H
/* 真实头在此声明 rtc_ops_s / rtc_gettime 等，宿主机测试不需要 */
#endif /* __TEST_STUB_NUTTX_TIMERS_RTC_H */
