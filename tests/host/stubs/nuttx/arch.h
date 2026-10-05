/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <nuttx/arch.h> 的宿主机桩 / host-test stub
 * WHY : 被测源文件按 NuttX 习惯包含它，符号均来自系统头或未使用
 * HOW  : 镜像 deps/nuttx/include/nuttx/arch.h 中被用到的最小声明集
 */
#ifndef __TEST_STUB_NUTTX_ARCH_H
#define __TEST_STUB_NUTTX_ARCH_H

#include <sys/types.h>

/* glibc 仅在 __USE_XOPEN 下暴露 useconds_t（sys/types.h），桩补齐 */
#if !defined(__USE_XOPEN) && !defined(__useconds_t_defined)
typedef unsigned int useconds_t;
#define __useconds_t_defined 1
#endif

/* 来源: deps/nuttx/include/nuttx/arch.h -> arch/<chip>/up_internal.h */
void up_udelay(useconds_t us);
void up_mdelay(unsigned int milliseconds);

#endif
