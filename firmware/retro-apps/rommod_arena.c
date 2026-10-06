/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * rommod_arena.c - 静态绑定档模块数据竞技场（全板）
 *
 * WHAT : ROM 模块（.rmo 静态档）可写段（.data/.bss）的固定落点区间
 * WHY  : 全板模块走构建期静态绑定（2026-10-06 宿主测试定稿：
 *        PC 相对 GOT 使 flash/内存分段放置不可行，HARDWARE 14 章）：
 *        .data/.bss 链接在本 arena 的构建期分配地址，加载器只做
 *        镜像拷贝（重定位归零）
 * WHO  : rommod_set_arena（retro_boot 注册）；build_romapps.sh（偏移规划）
 * WHERE: retro-ws/firmware/retro-apps/rommod_arena.c（同步进 retro 模块）
 * WHEN : 2026-10-06 新增
 * HOW  : 大数组常驻 .bss（有 SPIRAM 时入 .ext_ram.bss=PSRAM，避开
 *        512KB SRAM）；构建器按模块 RW memsz 顺序切分偏移并烘焙进
 *        模块链接地址，装载期 rommod 校验区间后直拷
 */

#include <nuttx/config.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_RETRO_ROMMOD_ARENA_SIZE > 0

#if defined(CONFIG_ESP32S3_SPIRAM) || defined(CONFIG_ESP32_SPIRAM)
/* PSRAM 段（.ext_ram.bss，NOLOAD）——SRAM 只留实时 */
uint8_t g_rommod_arena[CONFIG_RETRO_ROMMOD_ARENA_SIZE]
    __attribute__((section(".ext_ram.bss"), aligned(16)));
#else
uint8_t g_rommod_arena[CONFIG_RETRO_ROMMOD_ARENA_SIZE] __attribute__((aligned(16)));
#endif

const size_t g_rommod_arena_len = CONFIG_RETRO_ROMMOD_ARENA_SIZE;

#endif /* CONFIG_RETRO_ROMMOD_ARENA_SIZE > 0 */
