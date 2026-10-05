/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 宿主机测试用的 NuttX config 桩 / host-test stub for nuttx/config.h
 * WHY  : 被测模块按 NuttX 习惯包含 <nuttx/config.h>，宿主机上不存在；
 *        桩保持"全部 CONFIG 关闭"以覆盖 CLI 侧（无 LVGL）编译路径
 * HOW  : 编译加 -I tests/host/stubs 即命中本文件
 */
#ifndef __TEST_STUB_NUTTX_CONFIG_H
#define __TEST_STUB_NUTTX_CONFIG_H
/* 故意留空：测试按需在命令行 -D 需要的开关 */

/* NuttX 全局返回值习惯（真实 nuttx/config.h 生成时同样提供） */
#ifndef OK
#  define OK 0
#endif

/* 版本串（真实构建由 NuttX Kconfig 注入，宿主机给占位值）
 * version string normally injected by Kconfig */
#ifndef CONFIG_VERSION_STRING
#  define CONFIG_VERSION_STRING "12.6.0-host-test"
#endif

/* FAR 修饰（真实构建由 nuttx/config.h -> nuttx/compiler.h 提供；
 * 宿主机上为空即可）/ FAR qualifier, empty on host */
#ifndef FAR
#  define FAR
#endif

#endif
