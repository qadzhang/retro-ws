/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : rpk 命令行驱动壳 / CLI driver around rpkg_* for host tests
 * WHY  : Python 差分/PBT 套件需要一个进程级入口驱动真实安装器
 * WHO  : tests/host/python/test_rpkg_diff.py 以 subprocess 调用
 * WHERE: esp32-retro-ws/tests/host/rpk_tool.c
 * WHEN : 2026-10-04 新增
 * HOW  : argv[1] = install|remove|list|info|isinstalled；退出码即
 *        rpkg_* 返回值（负值转 byte 为可见差异）
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pkg_manager.h"

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;

    if (strcmp(argv[1], "install") == 0 && argc == 3)
        return rpkg_install(argv[2]) == 0 ? 0 : 1;
    if (strcmp(argv[1], "remove") == 0 && argc == 3)
        return rpkg_remove(argv[2]) == 0 ? 0 : 1;
    if (strcmp(argv[1], "list") == 0)
        return rpkg_list() == 0 ? 0 : 1;
    if (strcmp(argv[1], "info") == 0 && argc == 3)
        return rpkg_info(argv[2]) == 0 ? 0 : 1;
    if (strcmp(argv[1], "isinstalled") == 0 && argc == 3)
        return rpkg_is_installed(argv[2]);

    return 2;
}
