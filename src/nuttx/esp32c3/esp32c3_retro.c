/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * esp32c3_retro.c - 合宙 ESP32-C3 核心板主入口（CLI 工作站）
 *
 * WHAT : esp32c3 目标系统装配：NSH + 网络 + 脚本引擎 + .rpk 包管理器
 * WHY  : 低资源/低价格目标（RISC-V 单核 160MHz、400KB SRAM、无 PSRAM），
 *        不运行 LVGL/CVBS——定位纯 CLI 工作站，软件经 .rpk 包安装
 * WHO  : NuttX 启动流程（board 最终初始化）
 * WHERE: retro-ws/src/nuttx/esp32c3/esp32c3_retro.c
 * WHEN : 2026-10-04 新增
 * HOW  : C3 为单核（无双核分工）：主任务顺序初始化各模块后进入
 *        NSH 交互；控制台由 Kconfig 选择（经典款 UART0 / 简约款 USB）
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <stdio.h>

extern int esp32c3_board_init(void);
extern void esp32retro_nsh_register(void);

int esp32c3_retro_main(int argc, char *argv[])
{
    syslog(LOG_INFO, "[c3] Luatos ESP32-C3 CLI workstation booting\n");

    /* 板级初始化（LED/SD/控制台）*/
    esp32c3_board_init();

    /* 注册自定义 NSH 命令（sysinfo/nettest/reboot/pkg 等）*/
    esp32retro_nsh_register();

    syslog(LOG_INFO, "[c3] ready - 'pkg list' 查看已装软件, "
           "'pkg install /sdcard/pkg/*.rpk' 安装\n");

    return 0;
}

/*
 * WHAT : 板级任务装配入口（retro_boot.c 调用）
 * WHY  : C3 单核——无媒体核分工，装配即板级初始化+命令注册
 * HOW  : 转发到 esp32c3_retro_main
 */
void esp32c3_retro_start(void)
{
    esp32c3_retro_main(0, NULL);
}
