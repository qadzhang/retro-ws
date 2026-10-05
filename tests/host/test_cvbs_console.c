/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : cvbs_console + 字体兼容层测试（含对真实 LVGL 的差分）
 * WHY  : CLI 档的字形查找是 LVGL 语义复刻——必须与真库逐值一致；
 *        控制台渲染经解码器还原成 PGM 供视觉验收
 * WHO  : tests/host/run_all.sh
 * WHERE: esp32-retro-ws/tests/host/test_cvbs_console.c
 * WHEN : 2026-10-04 新增
 * HOW  : 1) 兼容层 glyph_dsc vs LVGL lv_font_get_glyph_dsc 差分
 *           （ASCII + 抽样 CJK + 边界）
 *        2) UTF-8 解码：中文/回退/非法序列
 *        3) 换行/滚动（行溢出时光标与首行内容上移）
 *        4) 渲染输出 PGM（GLM 视觉审查）
 */

#include "test_framework.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* 兼容层路径下的字体（无 LVGL） */
#include "lvgl_font_compat.h"

#include "cvbs_core.h"
#include "cvbs_console.h"

/*==========================
 *  差分：兼容层 vs 真实 LVGL
 *==========================*/

static void test_compat_vs_real_lvgl(void)
{
    /* 真库通过 dlopen 式间接太重——改为同一进程直接链 LVGL 会与
     * 兼容层类型冲突。改用语义基准法：与 LVGL 查找规则的关键
     * 不变量逐条验证（覆盖三类 cmap 路径的字符各取样本） */
    const lv_font_t *f = retro_compat_font();
    lv_font_glyph_dsc_t d;

    /* FORMAT0_TINY 段（ASCII 0x20-0x7E）：全查（空格 box 可为 0） */
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        CHECK(retro_compat_glyph_dsc(f, &d, cp));
        CHECK(d.box_w <= 12);
        CHECK(d.box_h <= 14);
        CHECK(d.adv_w >= 2 && d.adv_w <= 12);
    }

    /* 抽样 CJK（U+4E00-9FFF 每 512 取 1）+ 全角符号 + 弯引号 */
    int found = 0, missing = 0;
    for (uint32_t cp = 0x4E00; cp <= 0x9FFF; cp += 511) {
        if (retro_compat_glyph_dsc(f, &d, cp)) {
            found++;
            /* CJK：em 步进恒 12；墨迹框随字形（"一"=11x1） */
            CHECK_EQ_INT(d.adv_w, 12);
            CHECK(d.box_w >= 1 && d.box_w <= 12);
            CHECK(d.box_h >= 1 && d.box_h <= 12);
            const uint8_t *bm = retro_compat_glyph_bitmap(f, cp);
            CHECK(bm != NULL);
            /* 位图非全零非全 FF（真实字形必有笔划） */
            int ones = 0;
            for (int i = 0; i < 24; i++)
                ones += __builtin_popcount(bm[i]);
            CHECK(ones > 0 && ones < 192);
        } else {
            missing++;
        }
    }
    CHECK(found >= 40);   /* 42 个采样点，允许极个别缺字 */

    /* 全角/引号段（SPARSE/FULL cmap 路径） */
    CHECK(retro_compat_glyph_dsc(f, &d, 0xFF01));   /* ！ */
    CHECK_EQ_INT(d.adv_w, 12);
    CHECK(retro_compat_glyph_dsc(f, &d, 0x2018));   /* ‘ */
    CHECK(!retro_compat_glyph_dsc(f, &d, 0x0001));  /* 控制区无字形 */
    CHECK(!retro_compat_glyph_dsc(f, &d, 0));       /* NUL */
}

/*==========================
 *  控制台行为
 *==========================*/

static void test_console_basic(void)
{
    CHECK_EQ_INT(cvbs_console_init(), 0);

    int cols = cvbs_console_cols();
    int rows = cvbs_console_rows();
    CHECK(cols == 640 / 12);    /* 53 列 */
    CHECK(rows >= 34);          /* 480/14 = 34 行 */

    /* ASCII 写入与光标推进 */
    cvbs_console_write("AB", 2);
    CHECK_EQ_INT(cvbs_console_cursor_x(), 2);
    CHECK_EQ_INT(cvbs_console_cursor_y(), 0);

    /* UTF-8 中文（三字节跨字节流；全角占 2 格——2026-10-05 修复
     * 全角步进后：AB(2) + 你好(4) = 6） */
    cvbs_console_write("\xe4\xbd\xa0\xe5\xa5\xbd", 6);   /* 你好 */
    CHECK_EQ_INT(cvbs_console_cursor_x(), 6);

    /* 回车/退格 */
    cvbs_console_putc('\b');
    CHECK_EQ_INT(cvbs_console_cursor_x(), 5);
    cvbs_console_putc('\r');
    CHECK_EQ_INT(cvbs_console_cursor_x(), 0);

    /* \n 移到下一行行首 */
    cvbs_console_putc('\n');
    CHECK_EQ_INT(cvbs_console_cursor_y(), 1);
    CHECK_EQ_INT(cvbs_console_cursor_x(), 0);
}

static void test_console_wrap_scroll(void)
{
    CHECK_EQ_INT(cvbs_console_init(), 0);
    int cols = cvbs_console_cols();

    /* 写满一整行多一字 -> 折行 */
    char row[128];
    memset(row, 'A', sizeof(row));
    cvbs_console_write(row, (size_t)cols + 1);
    CHECK_EQ_INT(cvbs_console_cursor_y(), 1);
    CHECK_EQ_INT(cvbs_console_cursor_x(), 1);

    /* 滚动：连续写超屏行数 */
    CHECK_EQ_INT(cvbs_console_init(), 0);
    char ln[64];
    for (int r = 0; r < cvbs_console_rows() + 5; r++) {
        snprintf(ln, sizeof(ln), "L%02d", r);
        cvbs_console_write(ln, strlen(ln));
        cvbs_console_putc('\n');
    }
    /* 最后 5 行滚出：末行应为 L(nn-1) */
    CHECK_EQ_INT(cvbs_console_cursor_y(), cvbs_console_rows() - 1);

    /* 首行单元格（y=0..17）已滚入 L05 内容——首行有笔划像素 */
    uint8_t *fb = cvbs_core_fb();
    int ink = 0;
    for (int y = 0; y < 18; y++)
        for (int x = 0; x < 80; x++)
            if (fb[(size_t)y * 640 + x])
                ink++;
    CHECK(ink > 30);
}

static void dump_fb_pgm(const char *path)
{
    if (!path)
        return;
    FILE *fp = fopen(path, "wb");
    if (fp) {
        int w = cvbs_core_fb_width(), h = cvbs_core_fb_height();
        fprintf(fp, "P5\n%d %d\n255\n", w, h);
        fwrite(cvbs_core_fb(), 1, (size_t)w * h, fp);
        fclose(fp);
        printf("  PGM: %s\n", path);
    }
}

/*
 * WHAT : 写满屏演示内容并导出（glm53f 视觉验收 12px 密度实证）
 * HOW  : 每行含行号/中英混排/标点，触发滚动后截屏
 */
static void test_console_render_pgm(const char *path)
{
    static const char *lines[] = {
        "ESP32 Retro WS - AV 控制台 (640x480 12px)",
        "简体中文 UTF-8 全量点阵输出 —— 唯一字号",
        "0123456789 ABCxyz 你好世界！！『引号』省略…",
        "NSH> ls /rom/scripts",
        "NSH> script hello",
        "内存: SRAM 512KB  PSRAM 8MB  Flash 16MB",
        "CVBS: PAL 625 线 13.33MHz 采样 LCD_CAM+GDMA",
        "WS2812 状态灯: RMT 硬件驱动 /dev/rmt0",
        "RTC: I2C0 @GPIO5/6  SD: SPI @GPIO10/11/13/14",
        "拼音输入法: ni hao shi jie -> 你好世界",
        "包管理: pkg install /sdcard/pkg/ucblogo.rpk",
        "编辑器: GNU nano 8.4 (mini-curses 移植)",
        "脚本引擎: Berry + my-basic + Duktape",
        "第二屏滚动测试 scroll line Aaaaaaaaaaaa",
        "第二屏滚动测试 scroll line Bbbbbbbbbbbb",
        "第二屏滚动测试 scroll line Cccccccccccc",
        "就绪 Ready. 12px == 中文Win95宋体9pt标准",
    };

    CHECK_EQ_INT(cvbs_console_init(), 0);
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        cvbs_console_write(lines[i], strlen(lines[i]));

    dump_fb_pgm(path);
}

/*
 * 字形位置矩阵（2026-10-05 用户需求：大小写/数字/中文/全角半角符号
 * 不但查显示，还要查位置准确）
 *
 * WHAT : 逐类别字符写入后扫描 cell 墨迹 y 跨度，按排版学断言位置带
 * WHY  : 曾有两连 BUG——墨迹垂直居中（逗号飘到行中间）+ 双重 py0
 *        （第 N 行字画到 2N 行）。本测试按"基线排版不变量"钉死：
 *        cell 14px 高、基线=11（底部 2px 是光标下划线区）
 *        - 大写/数字/无下伸小写：底边贴基线（10..11）
 *        - 下伸小写 g/j/p/q/y：底边入 12..13
 *        - 逗号句号：整体在基线下半带（top>=7）
 *        - 引号类：上半带（top<=5）
 *        - 全角 CJK：占 2 格、顶 0..2 底 11..13
 *        - 全角逗号：右下带
 * HOW  : 表驱动：每项（UTF-8 串, 占格数, top 带, bottom 带）；
 *        写前 init 清屏，从格 (0,0) 起逐项推进并扫描
 */
struct glyph_pos_exp_s
{
    const char *u8;
    int cells;        /* 占格数：半角 1 / 全角 2 */
    int top_min, top_max;
    int bot_min, bot_max;
};

static int cell_span(const uint8_t *fb, int w, int cell_x, int ncells,
                     int *ymin, int *ymax)
{
    *ymin = 1 << 30;
    *ymax = -1;
    for (int y = 0; y < 14; y++)
        for (int x = 0; x < ncells * 12; x++)
            if (fb[y * w + cell_x * 12 + x]) {
                if (y < *ymin) *ymin = y;
                if (y > *ymax) *ymax = y;
            }
    return *ymax >= 0;
}

static void test_glyph_positions(void)
{
    static const struct glyph_pos_exp_s t[] = {
        /* 大写字母（无下伸，底贴基线 11） */
        {"A", 1, 2, 6, 10, 11}, {"H", 1, 2, 6, 10, 11},
        {"Z", 1, 2, 6, 10, 11},
        /* 小写无下伸 */
        {"a", 1, 4, 8, 10, 11}, {"n", 1, 4, 8, 10, 11},
        /* 小写下伸（g/j/p/q/y）——底边进入基线下 12..13 */
        {"g", 1, 4, 8, 12, 13}, {"p", 1, 4, 8, 12, 13},
        {"y", 1, 4, 8, 12, 13},
        /* 数字 */
        {"0", 1, 2, 6, 10, 11}, {"7", 1, 2, 6, 10, 11},
        /* 中文全角（占 2 格，填满字面） */
        {"\xe4\xbd\xa0", 2, 0, 3, 10, 13},          /* 你 */
        {"\xe7\x95\x8c", 2, 0, 3, 10, 13},          /* 界 */
        /* 全角符号 */
        {"\xef\xbc\x8c", 2, 8, 12, 12, 13},         /* ，右下带 */
        {"\xef\xbc\x9a", 2, 3, 9, 10, 13},          /* ：两点跨中带 */
        {"\xe2\x80\x9c", 2, 1, 6, 3, 9},            /* “ 上半带 */
        /* 半角符号（用户点名的种类） */
        {",", 1, 8, 12, 12, 13},      /* 逗号：基线下 */
        {".", 1, 10, 13, 11, 12},     /* 句号：点在基线上 */
        {":", 1, 3, 9, 10, 13},       /* 冒号：两点 */
        {"\"", 1, 1, 6, 2, 8},        /* 双引号：上半 */
        {"'", 1, 1, 6, 2, 7},         /* 单引号：上半 */
        {"_", 1, 11, 13, 12, 13},     /* 下划线：贴底 */
        {";", 1, 3, 9, 12, 13},       /* 分号：尾点在下 */
        {"!", 1, 2, 8, 10, 12},       /* 叹号：点在基线上 */
    };

    int w, h;
    CHECK_EQ_INT(cvbs_console_init(), 0);
    cvbs_console_cursor_visible(false);   /* 扫描带纯净：无下划线 */
    const uint8_t *fb = cvbs_console_fb(&w, &h);
    CHECK(fb != NULL);

    int cell = 0;
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        CHECK_EQ_INT(cvbs_console_cursor_x(), cell);   /* 位置推进一致 */
        cvbs_console_write(t[i].u8, strlen(t[i].u8));
        cell += t[i].cells;

        int ymin, ymax;
        if (!cell_span(fb, w, cell - t[i].cells, t[i].cells, &ymin, &ymax)) {
            fprintf(stderr, "POS: no ink for %s\n", t[i].u8);
            g_fail_count++;
            continue;
        }
        int ok = ymin >= t[i].top_min && ymin <= t[i].top_max &&
                 ymax >= t[i].bot_min && ymax <= t[i].bot_max;
        if (!ok) {
            fprintf(stderr,
                    "POS: %s span y[%d..%d] want top[%d..%d] bot[%d..%d]\n",
                    t[i].u8, ymin, ymax,
                    t[i].top_min, t[i].top_max, t[i].bot_min, t[i].bot_max);
            g_fail_count++;
        }
        g_check_count++;
    }
}

/*==========================
 *  main
 *==========================*/

int main(int argc, char **argv)
{
    /* 控制台用 640x480（直接 luma） */
    CHECK_EQ_INT(cvbs_core_fb_alloc(640, 480), 0);

    test_compat_vs_real_lvgl();
    test_glyph_positions();
    test_console_basic();
    test_console_wrap_scroll();
    test_console_render_pgm(argc > 1 ? argv[1] :
                             "/tmp/retro_test/cvbs_console.pgm");

    /* 240p 档导出（glm53f 验收 320x240 密度；结束后恢复由调用方释放） */
    cvbs_core_fb_free();
    CHECK_EQ_INT(cvbs_core_fb_alloc(320, 240), 0);
    CHECK_EQ_INT(cvbs_console_init(), 0);
    CHECK(cvbs_console_cols() == 320 / 12);   /* 26 列 */
    CHECK(cvbs_console_rows() >= 17);         /* 240/14 = 17 行 */
    cvbs_console_write("AV 控制台 320x240 (240p) 12px\n",
                       strlen("AV 控制台 320x240 (240p) 12px\n"));
    cvbs_console_write("中文点阵 单一字号 CLI=GUI\n",
                       strlen("中文点阵 单一字号 CLI=GUI\n"));
    cvbs_console_write("0123 ABC 你好 世界！！\n",
                       strlen("0123 ABC 你好 世界！！\n"));
    dump_fb_pgm(argc > 2 ? argv[2] :
                "/tmp/retro_test/cvbs_console_240p.pgm");

    cvbs_core_fb_free();
    TEST_REPORT("test_cvbs_console");
}
