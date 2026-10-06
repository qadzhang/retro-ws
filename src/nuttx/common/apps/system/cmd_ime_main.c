/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cmd_ime_main.c - NSH `ime` 命令（CCDOS 式输入法开关）
 *
 * WHAT : ime on/off/status/autostart——CLI 拼音输入法命令入口
 * WHY  : REQUIREMENTS 2.1.3（2026-10-06 三层语义定稿）：默认不启动；
 *        ime on 启动常驻服务（Ctrl+Space 调出/收起=中英切换，
 *        Ctrl+Q 或 ime off 彻底退出）；autostart 控制是否随系统
 *        启动（默认关，写 /opt/etc/ime.conf）
 * WHO  : NuttX builtin（retro-apps Application.mk 注册）
 * WHERE: retro-ws/src/nuttx/common/apps/system/cmd_ime_main.c
 * WHEN : 2026-10-05 新增；2026-10-06 autostart + 三层帮助
 * HOW  : 薄壳转 cvbs_ime_enable/active/autostart_*；组合键在
 *        cvbs_ime_feed 内处理
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "driver/cvbs_console.h"
#include "driver/cvbs_ime.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("用法: ime on|off|status|autostart on|off\n");
        printf("  on          启动输入法服务（底部常驻输入法条）\n");
        printf("  off         彻底退出并释放状态条\n");
        printf("  status      查看状态\n");
        printf("  autostart   是否随系统启动（默认: 不启动）\n");
        printf("组合键: Ctrl+Space 调出/收起输入法条（调出=中文/全角，\n");
        printf("        收起=英文直通）/ Ctrl+Q 彻底退出\n");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        cvbs_ime_enable(true);
        printf("输入法服务已启动（Ctrl+Space 调出/收起，Ctrl+Q 退出）\n");
    } else if (strcmp(argv[1], "off") == 0) {
        cvbs_ime_enable(false);
        printf("输入法已退出，状态条已释放\n");
    } else if (strcmp(argv[1], "autostart") == 0) {
        if (argc >= 3 && strcmp(argv[2], "on") == 0) {
            int ret = cvbs_ime_autostart_set(true);
            if (ret != 0) {
                fprintf(stderr, "ime: 写配置失败(%d)，片上可写区未就绪?\n", ret);
                return 1;
            }
            printf("已设置随系统启动（/opt/etc/ime.conf）\n");
        } else if (argc >= 3 && strcmp(argv[2], "off") == 0) {
            cvbs_ime_autostart_set(false);
            printf("已取消随系统启动（默认）\n");
        } else {
            printf("随系统启动: %s\n", cvbs_ime_autostart_get() ? "开" : "关");
        }
    } else if (strcmp(argv[1], "status") == 0) {
        printf("输入法服务: %s\n", cvbs_ime_active() ? "运行中" : "未启动");
        if (cvbs_ime_active())
            printf("状态条: %s\n", cvbs_ime_statusline());
        printf("随系统启动: %s\n", cvbs_ime_autostart_get() ? "开" : "关");
    } else {
        fprintf(stderr, "ime: 未知参数 %s（on/off/status/autostart）\n", argv[1]);
        return 1;
    }

    return 0;
}
