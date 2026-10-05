/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * nano_port/compat.c - NuttX libc 缺口的补齐（nano 8.4 移植）
 *
 * WHAT : mkstemps() —— GNU 扩展，NuttX libc 只有 mkstemp()
 * WHY  : deps/nano/src/files.c:1482 原生调用（安全临时文件做
 *        ^O 原子写盘），上游源码不改（AGENTS.md 11.5）
 * WHO  : GNU nano files.c
 * WHERE: esp32-retro-ws/src/nuttx/common/nano_port/compat.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 模板 "XXXXXX" 在后缀前——循环：随机填充 X → O_CREAT|O_EXCL
 *        open 成功即返回 fd（glibc mkstemps 语义）
 */

#include <nuttx/config.h>

#include <fcntl.h>
#include <locale.h>
#include <langinfo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * WHAT : locale 存根——NuttX libc 有头无实现
 * WHY  : nano 用 setlocale/nl_langinfo(CODESET) 判定 UTF-8；
 *        本机全链路 UTF-8（AGENTS.md 7.3），恒报 UTF-8 即正确
 * HOW  : setlocale 返回 "C"；CODESET 返回 "UTF-8"
 */
char *setlocale(int category, const char *locale)
{
    (void)category;
    (void)locale;
    return (char *)"C";
}

char *nl_langinfo(nl_item item)
{
    (void)item;
    return (char *)"UTF-8";
}

int mkstemps(char *template_name, int suffix_len)
{
    size_t len = strlen(template_name);
    char *xes;

    if (len < 6 + (size_t)suffix_len)
        return -1;

    xes = template_name + len - suffix_len - 6;
    if (memcmp(xes, "XXXXXX", 6) != 0)
        return -1;

    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned r = (unsigned)rand();

        for (int i = 0; i < 6; i++)
            xes[i] = "0123456789abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[((r >> (5 * i)) & 31) +
                                                     ((r >> (5 * i + 3)) & 1) * 26];

        int fd = open(template_name, O_RDWR | O_CREAT | O_EXCL, 0600);

        if (fd >= 0)
            return fd;
    }

    return -1;
}
