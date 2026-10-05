/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 宿主机测试用的 NuttX 内核堆桩 / host-test stub for nuttx/mm/mm.h
 * WHY  : 被测模块按 NuttX 习惯用 kmm_malloc/kmm_free，头在宿主机不存在；
 * HOW  : 镜像 deps/nuttx/include/nuttx/mm/mm.h 中的函数原型
 *        （FAR 简化为空；宿主机上仅做语法检查，不链接）
 */
#ifndef __TEST_STUB_NUTTX_MM_MM_H
#define __TEST_STUB_NUTTX_MM_MM_H

#include <stddef.h>

#ifndef FAR
#  define FAR
#endif

/* 来源: deps/nuttx/include/nuttx/mm/mm.h */
FAR void *kmm_malloc(size_t size);
FAR void  kmm_free(FAR void *mem);

#endif
