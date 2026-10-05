/*
 * SPDX-FileCopyrightText: 2026 ESP32-Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cvbs_ime.c - CCDOS 式 AV 控制台输入法实现
 *
 * WHAT : ime on 后屏幕最下方常驻反色输入法条：中文模式显示
 *        拼音串与 1-9 候选；键盘被 IME 接管，确认文本回显在光标
 *        处，Enter 后整行回放进终端输入流
 * WHY  : DOS 时代 CCDOS/UCDOS 的显示处理方式——输入法条常驻但不
 *        侵入正文滚动区（rows_eff-1），其他命令照常运行（用户
 *        需求 2026-10-05；REQUIREMENTS 2.1.3 CLI IME / NEXT_STEPS 20）
 * WHO  : cvbs_console 键盘泵 + NSH `ime` 命令 + tests/host/test_ime.c
 * WHERE: retro-ws/src/nuttx/common/driver/cvbs_ime.c
 * WHEN : 2026-10-05 新增
 * HOW  : 组合键（UART 字节流语义）：Ctrl+Space=0x00 中英切换、
 *        Ctrl+Q=0x11 关闭释放条；引擎=drv_pinyin（整行缓冲）；
 *        屏显同步=feed 后比对 input_len 增量 console_write 新段、
 *        缩量 console "\b"（\b 自带左移+擦格）；Enter=cli_getline
 *        等价：input_buf+'\n' 逐字节 cvbs_console_input_push 回放
 */

#include <nuttx/config.h>

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "cvbs_console.h"
#include "cvbs_ime.h"
#include "drv_pinyin.h"

#ifdef CONFIG_RETRO_PINYIN_CLI

#define CVBS_IME_KEY_MODE   0x00    /* Ctrl+Space：中英切换 */
#define CVBS_IME_KEY_OFF    0x11    /* Ctrl+Q：关闭 IME 释放条 */
#define STATUS_MAX          48

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
        status_refresh();
        return 1;
    }

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

#endif /* CONFIG_RETRO_PINYIN_CLI */
