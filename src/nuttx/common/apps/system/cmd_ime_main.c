/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cmd_ime_main.c - NSH `ime` 命令（CCDOS 式输入法开关）
 *
 * WHAT : ime on/off/status——CLI 拼音输入法的命令入口
 * WHY  : REQUIREMENTS 2.1.3 / NEXT_STEPS 20：纯 CLI/安全模式下
 *        显式唤起行内拼音；ime on 后屏幕底部出现常驻输入法条
 *        （CCDOS 显示方式），Ctrl+Q 或 ime off 释放
 * WHO  : NuttX builtin（retro-apps Application.mk 注册）
 * WHERE: retro-ws/src/nuttx/common/apps/system/cmd_ime_main.c
 * WHEN : 2026-10-05 新增
 * HOW  : 薄壳转 cvbs_ime_enable/active；组合键：Ctrl+Space 中英、
 *        Ctrl+Q 关闭（在 cvbs_ime_feed 内处理）
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "driver/cvbs_console.h"
#include "driver/cvbs_ime.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("用法: ime on|off|status\n");
        printf("  on      启动拼音输入法（屏幕底部常驻输入法条）\n");
        printf("  off     关闭并释放状态条\n");
        printf("  status  查看状态\n");
        printf("组合键: Ctrl+Space 中英切换 / Ctrl+Q 关闭\n");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        cvbs_ime_enable(true);
        printf("输入法已启动：底部状态条（Ctrl+Space 切换中英，Ctrl+Q 退出）\n");
    } else if (strcmp(argv[1], "off") == 0) {
        cvbs_ime_enable(false);
        printf("输入法已关闭，状态条已释放\n");
    } else if (strcmp(argv[1], "status") == 0) {
        printf("输入法: %s\n", cvbs_ime_active() ? "运行中" : "未启动");
        if (cvbs_ime_active())
            printf("状态条: %s\n", cvbs_ime_statusline());
    } else {
        fprintf(stderr, "ime: 未知参数 %s（on/off/status）\n", argv[1]);
        return 1;
    }

    return 0;
}
