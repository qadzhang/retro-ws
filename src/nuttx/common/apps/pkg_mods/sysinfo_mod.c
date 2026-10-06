/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * sysinfo_mod.c - 系统信息应用（ROM 模块形态，自包含实现）
 *
 * WHAT : 打印机型/内存/时间的 CLI 应用，.rmo 模块交付
 * WHY  : 应用/系统分离（2026-10-06）——sysinfo 从固件 builtin 抽离为
 *        .rpk 包（板级名单默认安装）；模块免 libc 头文件（extern
 *        直decl）：nuttx <stdio.h> 链条会拉进 arch/chip/irq.h 等
 *        板级头（RISC-V 档），模块构建拿不到固件的 arch include 集
 * WHO  : `run sysinfo`（NSH）
 * WHERE: retro-ws/src/nuttx/common/apps/pkg_mods/sysinfo_mod.c
 * WHEN : 2026-10-06 从 cmd_sysinfo_main.c 迁移并自包含化
 * HOW  : printf/snprintf/time 经 extern 声明（符号表/静态绑定解析）；
 *        板级条件编译（SPIRAM/机型）来自构建期生成的 config 等价物
 */

#include <nuttx/config.h>
#include <stddef.h>
#include <stdint.h>

extern int printf(const char *fmt, ...);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("=== Retro WS 复古工作站 ===\n");

#if defined(CONFIG_ARCH_CHIP_ESP32S3)
    printf("型号:     ESP32-S3-DevKitC-1 (Xtensa LX7 双核)\n");
#elif defined(CONFIG_ARCH_CHIP_ESP32)
    printf("型号:     ESP32-CAM (Xtensa LX6 双核)\n");
#elif defined(CONFIG_ARCH_CHIP_ESP32C3)
    printf("型号:     合宙 ESP32-C3 核心板 (RISC-V 单核)\n");
#elif defined(CONFIG_ARCH_CHIP_RP2040)
    printf("型号:     Raspberry Pi Pico (RP2040 双核 Cortex-M0+)\n");
#else
    printf("型号:     未知（模块构建 config 快照）\n");
#endif

#if defined(CONFIG_ESP32S3_SPIRAM) || defined(CONFIG_ESP32_SPIRAM)
    printf("PSRAM:    已启用\n");
#else
    printf("PSRAM:    未启用\n");
#endif

    printf("运行形态: ROM 模块应用（XIP 原址执行，run sysinfo）\n\n");
    return 0;
}
