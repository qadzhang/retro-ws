/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <sys/boardctl.h> 的宿主机桩 / host-test stub
 * WHY : nsh_cmds/bootmenu/cron 用 boardctl(BOARDIOC_RESET) 复位板子
 * HOW  : 原型与命令宏复制自 deps/nuttx/include/sys/boardctl.h
 *       （boardctl 原型见 L527：两参数版；BOARDIOC_RESET 见 L199）。
 *       真实头没有 BOARDIOC_SMP_SETAFFINITY（业务代码已改用
 *       sched_setaffinity()），故桩中也不提供
 */
#ifndef __TEST_STUB_SYS_BOARDCTL_H
#define __TEST_STUB_SYS_BOARDCTL_H

#include <stdint.h>

#define BOARDIOC_RESET             0x0004  /* deps/nuttx/include/sys/boardctl.h:199 */

int boardctl(unsigned int cmd, uintptr_t arg);

#endif /* __TEST_STUB_SYS_BOARDCTL_H */
