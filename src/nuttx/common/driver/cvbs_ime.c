/*
 * SPDX-FileCopyrightText: 2026 ESP32-Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cvbs_ime.c - CCDOS 式 AV 控制台输入法实现
 *
 * WHAT : ime on 后常驻输入法服务：中文/全角模式屏幕最下方显示反色
 *        输入法条（拼音串+1-9 候选，键盘接管）；英文模式条收起、
 *        键纯直通；Ctrl+Q 或 ime off 彻底退出释放
 * WHY  : DOS 时代 CCDOS/UCDOS 的交互模型（用户 2026-10-06 定稿三层
 *        语义）：默认不启动 -> ime on 启动常驻 -> Ctrl+Space 在
 *        "中文条显示"与"英文直通收起"间切换（调出=中文/全角输入，
 *        收起=英文——显隐即中英）-> Ctrl+Q/ime off 退出服务；
 *        可选 autostart 配置随系统启动（默认关）
 * WHO  : cvbs_console 键盘泵 + NSH `ime` 命令 + retro_boot（自启）
 *        + tests/host/test_ime.c
 * WHERE: retro-ws/src/nuttx/common/driver/cvbs_ime.c
 * WHEN : 2026-10-05 新增；2026-10-06 三层语义 + autostart
 * HOW  : 组合键（UART 字节流语义）：Ctrl+Space=0x00 切换中英=
 *        输入法条调出/收起（收起时普通键 feed 返回 0 直通终端，
 *        组合键仍驻留生效）、Ctrl+Q=0x11 彻底退出；引擎=drv_pinyin
 *        （整行缓冲）；屏显同步=feed 后比对 input_len 增量
 *        console_write 新段、缩量 console "\b"；Enter=整行+'\n'
 *        逐字节回放输入流；autostart=CVBS_IME_CONF 单行配置文件
 *        （retro_boot 启动期读取，存在 autostart=1 即启用）
 */

#include <nuttx/config.h>

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "cvbs_console.h"
#include "cvbs_ime.h"
#include "drv_pinyin.h"

#ifdef CONFIG_RETRO_PINYIN_CLI

#define CVBS_IME_KEY_MODE   0x00    /* Ctrl+Space：调出/收起=中英切换 */
#define CVBS_IME_KEY_OFF    0x11    /* Ctrl+Q：彻底退出 IME 释放条 */
#define STATUS_MAX          48

/* 自启动配置文件（`ime autostart on` 写入；缺省/删除 = 不自启） */
#ifndef CVBS_IME_CONF
#  define CVBS_IME_CONF     "/opt/etc/ime.conf"
#endif

static bool g_ime_on = false;
static char g_status[STATUS_MAX];

/*
 * WHAT : 重拼状态条文本并重绘
 * HOW  : 中文=「拼音[xx] 1你 2尼 …」；英文=「英文(直通)」；
 *        无拼音时=「中文 拼音就绪」
 */
static void status_refresh(void)
{
    if (!g_ime_on)
        return;

    if (cli_pinyin_get_mode() == 0) {
        /* 英文=收起态：条已释放，仅更新文本镜像（不重绘） */
        snprintf(g_status, sizeof(g_status), " 英文直通 Ctrl+Space=中文 ");
    } else {
        const char *cand[9];
        int n = cli_pinyin_candidates(cand, 9);
        int off;

        off = snprintf(g_status, sizeof(g_status), " 拼音[%s] ",
                       cli_pinyin_get_pinyin());
        for (int i = 0; i < n && off < STATUS_MAX - 8; i++)
            off += snprintf(g_status + off, STATUS_MAX - off,
                            "%d%s ", i + 1, cand[i]);
        if (cli_pinyin_get_input_len() == 0 && n == 0)
            snprintf(g_status + off - 1, STATUS_MAX - off + 1,
                     " 就绪 ");
    }

    cvbs_console_statusbar(true, g_status);
}

bool cvbs_ime_active(void)
{
    return g_ime_on;
}

void cvbs_ime_enable(bool on)
{
    if (on == g_ime_on)
        return;

    if (on) {
        cli_pinyin_init();
        cli_pinyin_set_mode(1);            /* 默认中文 */
        g_ime_on = true;
        status_refresh();
    } else {
        g_ime_on = false;
        cli_pinyin_reset();
        cvbs_console_statusbar(false, NULL);
    }
}

int cvbs_ime_feed(int ch)
{
    int prev_len;

    if (!g_ime_on)
        return 0;

    /* 组合键 */
    if (ch == CVBS_IME_KEY_OFF) {
        cvbs_ime_enable(false);
        return 1;                          /* 消费 */
    }
    if (ch == CVBS_IME_KEY_MODE) {
        cli_pinyin_toggle_mode();
        cli_pinyin_reset();                /* 清残留拼音 */
        if (cli_pinyin_get_mode() == 0) {
            snprintf(g_status, sizeof(g_status),
                     " 英文直通 Ctrl+Space=中文 ");   /* 文本镜像 */
            cvbs_console_statusbar(false, NULL);   /* 英文=收起条 */
        } else {
            status_refresh();              /* 中文=调出条 */
        }
        return 1;
    }

    /* 英文/收起态：普通键纯直通（不消费，泵送终端）；组合键驻留 */
    if (cli_pinyin_get_mode() == 0)
        return 0;

    prev_len = cli_pinyin_get_input_len();
    (void)cli_pinyin_input(ch);   /* 引擎返回值语义混叠（字母=需刷候选），行完成按 ch 判 */

    /* 屏显同步：已确认文本增量上屏 / 减量退格 */
    const char *buf = cli_pinyin_get_input();
    int now_len = cli_pinyin_get_input_len();

    if (now_len > prev_len)
        cvbs_console_write(buf + prev_len, (size_t)(now_len - prev_len));
    else if (now_len < prev_len)
        for (int i = 0; i < prev_len - now_len; i++)
            cvbs_console_write("\b", 1);

    if (ch == '\r' || ch == '\n') {
        /* 行完成（Enter）：整行 + '\n' 回放给终端输入流 */
        for (int i = 0; i < now_len; i++)
            cvbs_console_input_push(buf[i]);
        cvbs_console_input_push('\n');
        cvbs_console_write("\n", 1);
        cli_pinyin_reset();
    }

    status_refresh();
    return 1;                              /* IME 激活期消费一切键 */
}

const char *cvbs_ime_statusline(void)
{
    return g_status;
}

/*
 * WHAT : 读自启动配置（CVBS_IME_CONF 存在且含 autostart=1）
 * WHY  : "默认不启动、可配置随系统启动"（用户 2026-10-06）——
 *        retro_boot 启动期查询；文件缺失/内容不符均为不自启
 */
bool cvbs_ime_autostart_get(void)
{
    FILE *f = fopen(CVBS_IME_CONF, "r");
    char line[32];

    if (f == NULL)
        return false;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "autostart=", 10) == 0)
        {
            fclose(f);
            return line[10] == '1';
        }
    }
    fclose(f);
    return false;
}

/*
 * WHAT : 写自启动配置（on=写 autostart=1；off=删文件）
 * HOW  : 逐级建父目录（片上 /opt/etc 可能首次使用）；写失败返回
 *        -errno 供 `ime autostart` 命令透传
 */
int cvbs_ime_autostart_set(bool on)
{
    if (!on) {
        unlink(CVBS_IME_CONF);
        return 0;                           /* 文件不存在也算成功 */
    }

    /* 父目录逐级建（"a/b/c.cfg" -> a, a/b） */
    char path[sizeof(CVBS_IME_CONF) + 1];
    snprintf(path, sizeof(path), "%s", CVBS_IME_CONF);

    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, 0777);
            *p = '/';
        }
    }

    FILE *f = fopen(CVBS_IME_CONF, "w");

    if (f == NULL)
        return -ENOENT;
    fputs("autostart=1\n", f);
    fclose(f);
    return 0;
}

#endif /* CONFIG_RETRO_PINYIN_CLI */
