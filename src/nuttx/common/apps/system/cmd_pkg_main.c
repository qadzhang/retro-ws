/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * cmd_pkg_main.c - NSH 内置命令 pkg 的 main 壳
 *
 * WHAT : Application.mk 把本文件 main 重命名为 pkg_main 并注册
 *        进 builtin 表（NSH 直接按命令名调用）
 * WHY  : nsh_cmds.c 的命令表原先无注册通道（死代码）——builtin
 *        是 NuttX 标准的应用注册机制
 * WHO  : NuttX builtin 框架（PROGNAME/MAINSRC 按序配对）
 * WHERE: esp32-retro-ws/src/nuttx/common/apps/system/cmd_pkg_main.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 转发到 cmd_pkg()（nsh_cmds.c / script_rom.c 实现）
 */

int cmd_pkg(int argc, char **argv);

int main(int argc, char **argv)
{
    return cmd_pkg(argc, argv);
}
