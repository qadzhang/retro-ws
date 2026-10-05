/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <nuttx/kthread.h> 的宿主机桩 / host-test stub
 * WHY : memmon/cron/ntp 用 kthread_create() 起内核线程
 * HOW  : 原型复制自 deps/nuttx/include/nuttx/kthread.h:106
 *       （kthread_create(name, priority, stack_size, main_t entry, argv)）；
 *       main_t 的真实定义在 deps/nuttx/include/sys/types.h:316，宿主机
 *       无该类型，故在此按真实原型补充 typedef
 */
#ifndef __TEST_STUB_NUTTX_KTHREAD_H
#define __TEST_STUB_NUTTX_KTHREAD_H

#include <sys/types.h>

#ifndef __NUTTX_MAIN_T_DEFINED
#define __NUTTX_MAIN_T_DEFINED
typedef int (*main_t)(int argc, char *argv[]);
#endif

int kthread_create(const char *name, int priority, int stack_size,
                   main_t entry, char * const argv[]);

#endif /* __TEST_STUB_NUTTX_KTHREAD_H */
