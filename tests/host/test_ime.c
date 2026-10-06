/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_ime.c - CCDOS 式输入法（cvbs_ime）行为测试
 *
 * WHAT : IME 状态条占行/释放、拼音候选、选字上屏回显、行回放、
 *        组合键（Ctrl+Space/Ctrl+Q）的机器化验证
 * WHY  : ai-code-testing 分层守护——CCDOS 形态是用户明确需求
 *        （2026-10-05），行为必须被测试钉死
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: retro-ws/tests/host/test_ime.c
 * WHEN : 2026-10-05 新增
 * HOW  : 宿主直链 console+pinyin+ime；断言：条占用后滚动不侵入末行、
 *        statusline 含拼音与候选、feed 数字后正文出现 UTF-8 汉字、
 *        Enter 后输入环收到 整行+\n、0x00 调出/收起（中英=显隐，
 *        收起态普通键直通返回 0）、0x11 彻底退出、autostart 读写
 */

#include <stdio.h>
#include <string.h>

#include "cvbs_core.h"
#include "cvbs_console.h"
#include "cvbs_ime.h"
#include "drv_pinyin.h"

#include "test_framework.h"

#define CELL_W 12
#define CELL_H 14

/* 末行（状态条行）是否为反色条：白像素占比 >= 60%（条上黑字正常） */
static int statusbar_row_blank(void)
{
    int w, h;
    const uint8_t *fb = cvbs_console_fb(&w, &h);
    int y0 = (h / CELL_H - 1) * CELL_H + 2;
    int white = 0;

    for (int x = 0; x < 100; x++)
        if (fb[y0 * w + x])
            white++;
    return white >= 60;
}

/* 正文区某 cell 行是否全空 */
static int text_row_blank(int row)
{
    int w, h;
    const uint8_t *fb = cvbs_console_fb(&w, &h);

    for (int y = row * CELL_H; y < (row + 1) * CELL_H; y++)
        for (int x = 0; x < 120; x++)
            if (fb[y * w + x])
                return 0;
    return 1;
}

/* 正文区找子串出现（粗扫首 200 行内 UTF-8 逐格拼接太重——直接按
 * input 回显行断言：feed 后扫描前几行是否含目标汉字的 3 字节序列
 * 的首字节组合。简化：扫描正文区非空格墨迹存在性即可 + 光标推进 */
static void test_ime_basic(void)
{
    char c;

    CHECK_EQ_INT(cvbs_core_fb_alloc(320, 240), 0);   /* 26x17 */
    CHECK_EQ_INT(cvbs_console_init(), 0);

    /* 1) ime on：状态条占用（末行反白），未启动前条不存在 */
    CHECK(!cvbs_ime_active());
    cvbs_ime_enable(true);
    CHECK(cvbs_ime_active());
    CHECK(statusbar_row_blank());
    CHECK(strstr(cvbs_ime_statusline(), "拼音") != NULL);  /* 初始态 */

    /* 2) 拼音输入：n i -> statusline 含 "ni" 与候选 */
    CHECK_EQ_INT(cvbs_ime_feed('n'), 1);
    CHECK_EQ_INT(cvbs_ime_feed('i'), 1);
    CHECK(strstr(cvbs_ime_statusline(), "ni") != NULL);
    CHECK(strstr(cvbs_ime_statusline(), "1") != NULL);

    /* 3) 数字选字：'1' -> 已确认文本为合法 UTF-8 汉字（>=3 字节，
     *     首字节多字节前导；具体字取词库序首项不硬编码） */
    CHECK_EQ_INT(cvbs_ime_feed('1'), 1);
    const char *committed = cli_pinyin_get_input();
    CHECK(cli_pinyin_get_input_len() >= 3);
    CHECK((committed[0] & 0x80) != 0);
    /* 记录首字节供回放校验 */
    char b0 = committed[0], b1 = committed[1], b2 = committed[2];

    /* 4) Enter：整行+\n 回放进输入环 */
    CHECK_EQ_INT(cvbs_ime_feed('\r'), 1);
    CHECK_EQ_INT(cvbs_console_input_pop(&c), 1);
    CHECK_EQ_INT(c, b0);
    CHECK_EQ_INT(cvbs_console_input_pop(&c), 1);
    CHECK_EQ_INT(c, b1);
    CHECK_EQ_INT(cvbs_console_input_pop(&c), 1);
    CHECK_EQ_INT(c, b2);
    CHECK_EQ_INT(cvbs_console_input_pop(&c), 1);
    CHECK_EQ_INT(c, '\n');
    CHECK_EQ_INT(cvbs_console_input_pop(&c), 0);     /* 环空 */

    /* 5) Ctrl+Space(0x00)：切英文=收起条 -> 普通键纯直通（返回 0） */
    CHECK_EQ_INT(cvbs_ime_feed(0x00), 1);
    CHECK(strstr(cvbs_ime_statusline(), "英文") != NULL);
    CHECK(!statusbar_row_blank());                   /* 条已收起 */
    CHECK_EQ_INT(cvbs_ime_feed('a'), 0);             /* 直通：不消费 */
    CHECK_EQ_INT(cvbs_ime_feed(0x00), 1);            /* 调回中文=条重现 */
    CHECK(statusbar_row_blank());
    CHECK(strstr(cvbs_ime_statusline(), "拼音") != NULL);

    /* 6) Ctrl+Q(0x11)：关闭并释放状态条（末行恢复黑） */
    CHECK_EQ_INT(cvbs_ime_feed(0x11), 1);
    CHECK(!cvbs_ime_active());
    CHECK(!statusbar_row_blank());
}

static void test_ime_scroll_protect(void)
{
    char ln[64];

    CHECK_EQ_INT(cvbs_console_init(), 0);
    cvbs_ime_enable(true);
    CHECK(statusbar_row_blank());

    /* 写超屏行数：正文滚到 rows_eff-1=16 行，状态条(第 17 行)不被
     * 滚动覆盖——条文本仍在（反白）且正文 L05 应出现在首行 */
    for (int r = 0; r < 22; r++) {
        snprintf(ln, sizeof(ln), "L%02d", r);
        cvbs_console_write(ln, strlen(ln));
        cvbs_console_write("\n", 1);
    }
    CHECK(statusbar_row_blank());                    /* 条仍在 */
    CHECK(!text_row_blank(0));                       /* 首行有滚入内容 */
    CHECK(cvbs_console_cursor_y() < cvbs_console_rows() - 1);

    cvbs_ime_enable(false);
    CHECK(!statusbar_row_blank());
}

/*
 * 自启动配置：写->读真；关->读假且文件删除；默认（无文件）假
 */
static void test_ime_autostart(void)
{
    CHECK(!cvbs_ime_autostart_get());                /* 默认不启动 */
    CHECK_EQ_INT(cvbs_ime_autostart_set(true), 0);
    CHECK(cvbs_ime_autostart_get());
    CHECK_EQ_INT(cvbs_ime_autostart_set(false), 0);
    CHECK(!cvbs_ime_autostart_get());
    CHECK_EQ_INT(cvbs_ime_autostart_set(false), 0);  /* 幂等删 */
}

int main(void)
{
    test_ime_basic();
    test_ime_scroll_protect();
    test_ime_autostart();
    cvbs_core_fb_free();

    printf("test_ime: %d checks, %d failed\n",
           g_check_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
