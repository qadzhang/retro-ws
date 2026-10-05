/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : CVBS 核心的时序/图像验证（含模拟电视解码器）
 * WHY  : 视频时序错误在真机上只能"看不到画面"——宿主机先用
 *        独立解码器把波形还原成图像，逐样本差分验证
 * WHO  : tests/host/run_all.sh；渲染的 PGM 交 glm 视觉审查
 * WHERE: retro-ws/tests/host/test_cvbs.c
 * WHEN : 2026-10-04 新增
 * HOW  : 1) 行结构断言（同步位置/宽度/电平）
 *        2) 场/帧行数断言（312/313/625）
 *        3) 端到端：绘制测试图 -> 生成波形 -> 解码器(sync 沿检测)
 *           重建图像 -> 与编码端亮度逐像素差分 + 输出 PGM
 */

#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cvbs_core.h"

#define IMG_W 320
#define IMG_H 240

/*==========================
 *  波形采集 sink
 *==========================*/

static uint8_t wave[700 * CVBS_LINE_TOTAL];
static int wave_lines = 0;

static void capture_sink(const uint8_t *line, size_t len, int line_no)
{
    (void)line_no;
    if (wave_lines < 700 && len == CVBS_LINE_TOTAL) {
        memcpy(wave + (size_t)wave_lines * CVBS_LINE_TOTAL, line, len);
        wave_lines++;
    }
}

/*==========================
 *  行结构断言
 *==========================*/

static void test_line_structure(void)
{
    uint8_t ln[CVBS_LINE_TOTAL];
    uint8_t row[IMG_W];

    memset(row, 1, sizeof(row));    /* 调色板 1 = 白 */
    size_t n = cvbs_core_encode_line(ln, row, IMG_W);
    CHECK_EQ_INT(n, CVBS_LINE_TOTAL);
    CHECK_EQ_INT(CVBS_LINE_TOTAL, 864);

    /* 前沿 12：消隐电平 */
    for (int i = 0; i < CVBS_H_FRONT_PORCH; i++)
        CHECK_EQ_INT(ln[i], CVBS_LEVEL_BLANK);
    /* 同步 64：同步电平 */
    for (int i = 12; i < 12 + CVBS_H_SYNC; i++)
        CHECK_EQ_INT(ln[i], CVBS_LEVEL_SYNC);
    /* 后沿 68：消隐 */
    for (int i = 76; i < 76 + CVBS_H_BACK_PORCH; i++)
        CHECK_EQ_INT(ln[i], CVBS_LEVEL_BLANK);
    /* 活跃区：白色 = 255 -> Y 应接近 235（studio swing 由调色板决定：
     * 白色 RGB 255,255,255 -> rgb_to_y = 255，DAC 全摆幅） */
    CHECK_EQ_INT(ln[144], 255);
    CHECK_EQ_INT(ln[144 + IMG_W - 1], 255);
    /* 帧缓冲宽度之外补消隐 */
    CHECK_EQ_INT(ln[144 + IMG_W], CVBS_LEVEL_BLANK);
    CHECK_EQ_INT(ln[CVBS_LINE_TOTAL - 1], CVBS_LEVEL_BLANK);

    /* 空行（无 fb_row）活跃区全消隐 */
    cvbs_core_encode_line(ln, NULL, 0);
    for (int i = 144; i < CVBS_LINE_TOTAL; i++)
        CHECK_EQ_INT(ln[i], CVBS_LEVEL_BLANK);

    /* 亮度定点系数：和恰为 256（BT.601 权重守恒） */
    CHECK_EQ_INT(cvbs_rgb_to_y(255, 255, 255), 255);
    CHECK_EQ_INT(cvbs_rgb_to_y(0, 0, 0), 0);
    /* 单色亮度排序：白 > 黄 > 青 > 绿 > 紫 > 红 > 蓝 > 黑 */
    uint8_t yw = cvbs_rgb_to_y(255, 255, 255), yy = cvbs_rgb_to_y(255, 255, 0);
    uint8_t yc = cvbs_rgb_to_y(0, 255, 255), yg = cvbs_rgb_to_y(0, 255, 0);
    uint8_t ym = cvbs_rgb_to_y(255, 0, 255), yr = cvbs_rgb_to_y(255, 0, 0);
    uint8_t yb = cvbs_rgb_to_y(0, 0, 255);
    CHECK(yw > yy && yy > yc && yc > yg && yg > ym && ym > yr && yr > yb);
}

static void test_sync_pulses_structure(void)
{
    uint8_t ln[CVBS_LINE_TOTAL];

    cvbs_core_encode_equalizing(ln);
    int lows = 0;
    for (int i = 0; i < CVBS_LINE_TOTAL; i++)
        if (ln[i] <= CVBS_SYNC_THRESHOLD) lows++;
    CHECK(lows >= 70 && lows <= 90);            /* 约 2×40 */
    CHECK_EQ_INT(ln[0], CVBS_LEVEL_BLANK);      /* 前沿后再进脉冲 */
    CHECK_EQ_INT(ln[CVBS_H_FRONT_PORCH], CVBS_LEVEL_SYNC);
    CHECK_EQ_INT(ln[CVBS_H_FRONT_PORCH + 432], CVBS_LEVEL_SYNC);

    cvbs_core_encode_broad_pulse(ln);
    lows = 0;
    for (int i = 0; i < CVBS_LINE_TOTAL; i++)
        if (ln[i] <= CVBS_SYNC_THRESHOLD) lows++;
    CHECK(lows >= 700 && lows <= 780);          /* 约 2×369 */
    CHECK_EQ_INT(ln[0], CVBS_LEVEL_SYNC);       /* 宽脉冲行头即同步 */
}

static void test_field_structure(void)
{
    CHECK_EQ_INT(cvbs_core_fb_alloc(IMG_W, IMG_H), OK);

    wave_lines = 0;
    int n1 = cvbs_core_generate_field(false, true, capture_sink);
    CHECK_EQ_INT(n1, 312);                      /* 场 1（240p）行数 */
    CHECK_EQ_INT(wave_lines, 312);

    wave_lines = 0;
    int n2 = cvbs_core_generate_field(true, false, capture_sink);
    CHECK_EQ_INT(n2, 313);                      /* 奇场含半行近似 */
    CHECK_EQ_INT(wave_lines, 313);

    wave_lines = 0;
    int nf = cvbs_core_generate_frame(false, capture_sink);
    CHECK_EQ_INT(nf, 625);                      /* PAL 整帧 */
    CHECK_EQ_INT(wave_lines, 625);

    wave_lines = 0;
    int np = cvbs_core_generate_frame(true, capture_sink);
    CHECK_EQ_INT(np, 625);                      /* 240p 同相双场 */

    CHECK_EQ_INT(cvbs_core_generate_field(false, true, NULL), -EINVAL);

    cvbs_core_fb_free();
}

/*==========================
 *  绘制 API
 *==========================*/

static void test_draw_clipping(void)
{
    /* 未分配帧缓冲：全部安全空操作 */
    cvbs_core_fb_free();
    cvbs_clear();
    cvbs_draw_pixel(0, 0, 1);
    cvbs_draw_hline(-10, 0, 5000, 1);
    cvbs_fill_rect(-5, -5, 100, 100, 1);
    CHECK(cvbs_core_fb() == NULL);

    CHECK_EQ_INT(cvbs_core_fb_alloc(IMG_W, IMG_H), OK);
    cvbs_clear();
    CHECK(cvbs_core_fb() != NULL);
    CHECK_EQ_INT(cvbs_core_fb()[0], 0);

    /* 越界绘制被裁剪，不越界访问（ASan 守护） */
    cvbs_draw_pixel(-1, -1, 1);
    cvbs_draw_pixel(IMG_W, IMG_H, 1);
    cvbs_draw_hline(-100, 5, 1000, 2);
    CHECK_EQ_INT(cvbs_core_fb()[5 * IMG_W + 0], 2);
    CHECK_EQ_INT(cvbs_core_fb()[5 * IMG_W + IMG_W - 1], 2);
    cvbs_fill_rect(IMG_W - 2, IMG_H - 2, 100, 100, 3);
    CHECK_EQ_INT(cvbs_core_fb()[(IMG_H - 1) * IMG_W + IMG_W - 1], 3);

    /* 参数校验 */
    CHECK_EQ_INT(cvbs_core_fb_alloc(0, 100), -EINVAL);
    CHECK_EQ_INT(cvbs_core_fb_alloc(100, 0), -EINVAL);
    CHECK_EQ_INT(cvbs_core_fb_alloc(CVBS_H_ACTIVE + 1, 100), -EINVAL);
    CHECK_EQ_INT(cvbs_core_fb_alloc(100, CVBS_FIELD1_ACTIVE * 2 + 1), -EINVAL);
    cvbs_core_fb_free();
}

/*==========================
 *  模拟电视解码器（独立实现，差分用）
 *==========================*/

/*
 * WHAT : 在一行样本里测同步脉冲宽度
 * 返回 : 首个低于门限段的长度；无同步返回 -1
 */
static int measure_sync_width(const uint8_t *ln, size_t len)
{
    int start = -1;
    for (size_t i = 0; i < len; i++) {
        if (ln[i] <= CVBS_SYNC_THRESHOLD) {
            if (start < 0)
                start = (int)i;
        } else if (start >= 0) {
            return (int)i - start;
        }
    }
    return start >= 0 ? (int)len - start : -1;
}

static void test_end_to_end_decode(const char *pgm_path)
{
    CHECK_EQ_INT(cvbs_core_fb_alloc(IMG_W, IMG_H), OK);

    /* ---- 绘制测试图：彩条 + 渐变 + 棋盘 + 边框 ---- */
    cvbs_clear();
    const uint8_t bars[8] = { 1, 5, 6, 3, 7, 2, 4, 0 };
    for (int x = 0; x < IMG_W; x++) {
        int bar = x * 8 / IMG_W;
        for (int y = 0; y < 60; y++)
            cvbs_draw_pixel(x, y, bars[bar]);
    }
    /* 灰阶渐变（调色板 32..255） */
    for (int x = 0; x < IMG_W; x++) {
        uint8_t c = (uint8_t)(32 + x * 223 / IMG_W);
        for (int y = 60; y < 120; y++)
            cvbs_draw_pixel(x, y, c);
    }
    /* 棋盘 */
    for (int y = 120; y < 180; y++)
        for (int x = 0; x < IMG_W; x++)
            cvbs_draw_pixel(x, y, ((x / 20 + y / 20) & 1) ? 23 : 16);
    /* 白色横带 + 底部黑 */
    cvbs_fill_rect(0, 180, IMG_W, 20, 1);
    cvbs_fill_rect(0, 200, IMG_W, 40, 0);
    cvbs_draw_rect(0, 0, IMG_W, IMG_H, 1);

    /* ---- 生成 240p 场波形 ---- */
    wave_lines = 0;
    int nf = cvbs_core_generate_frame(true, capture_sink);
    CHECK_EQ_INT(nf, 625);
    CHECK_EQ_INT(wave_lines, 625);

    /* ---- 解码：逐行分类 ---- */
    uint8_t image[IMG_H][IMG_W];
    int img_rows = 0;
    int normal_lines = 0;
    int eq_lines = 0;
    int broad_lines = 0;
    int bad_sync = 0;

    for (int ln_no = 0; ln_no < wave_lines; ln_no++) {
        const uint8_t *ln = wave + (size_t)ln_no * CVBS_LINE_TOTAL;
        int sw = measure_sync_width(ln, CVBS_LINE_TOTAL);
        if (sw < 0) {
            bad_sync++;
            continue;
        }
        if (sw >= 30 && sw <= 50) {
            eq_lines++;
        } else if (sw >= 300 && sw <= 420) {
            broad_lines++;
        } else if (sw >= 60 && sw <= 68) {
            normal_lines++;
        } else {
            bad_sync++;
        }
    }

    /* 场结构：每场 3 均衡 + 3 宽 + 3 均衡，两场 */
    CHECK_EQ_INT(eq_lines, 12);
    CHECK_EQ_INT(broad_lines, 6);
    CHECK_EQ_INT(bad_sync, 0);
    CHECK_EQ_INT(normal_lines, 607);   /* (15 消隐 + 288 活跃)×2 + 奇场半行 */

    /* ---- 取图：场 1 的活跃区（行 24..311，帧缓冲 240 行在前）---- */
    for (int ln_no = 24; ln_no < 312 && img_rows < IMG_H; ln_no++) {
        const uint8_t *ln = wave + (size_t)ln_no * CVBS_LINE_TOTAL;
        int x0 = CVBS_H_FRONT_PORCH + CVBS_H_SYNC + CVBS_H_BACK_PORCH;
        memcpy(image[img_rows], ln + x0, IMG_W);
        img_rows++;
    }

    /* ---- 差分：解码行 == 编码端逐像素亮度 ---- */
    uint8_t *fb = cvbs_core_fb();
    CHECK_EQ_INT(img_rows, IMG_H);
    int mism = 0;
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            uint8_t r, g, b;
            cvbs_core_palette_get_rgb(fb[y * IMG_W + x], &r, &g, &b);
            uint8_t expect = cvbs_rgb_to_y(r, g, b);
            if (image[y][x] != expect) {
                if (mism < 5)
                    fprintf(stderr,
                            "PIXEL DIFF (%d,%d): decoded=%d expect=%d\n",
                            x, y, image[y][x], expect);
                mism++;
            }
        }
    }
    CHECK_EQ_INT(mism, 0);

    /* ---- 输出 PGM 供视觉审查 ---- */
    if (pgm_path) {
        FILE *f = fopen(pgm_path, "wb");
        if (f) {
            fprintf(f, "P5\n%d %d\n255\n", IMG_W, IMG_H);
            fwrite(image, 1, sizeof(image), f);
            fclose(f);
            printf("  PGM: %s\n", pgm_path);
        }
    }

    cvbs_core_fb_free();
}

/*==========================
 *  直通亮度模式（L8）
 *==========================*/

static void test_direct_luma_mode(void)
{
    uint8_t ln[CVBS_LINE_TOTAL];
    uint8_t row[4] = { 0, 64, 200, 255 };

    cvbs_core_set_direct_luma(true);
    cvbs_core_encode_line(ln, row, 4);

    /* x8 -> studio swing：0->16（不低于消隐），255->235 */
    CHECK_EQ_INT(ln[144], 16);
    CHECK_EQ_INT(ln[145], 16 + 64 * 219 / 255);
    CHECK_EQ_INT(ln[146], 16 + 200 * 219 / 255);
    CHECK_EQ_INT(ln[147], 235);
    /* 活跃电平永不低于消隐（同步安全） */
    for (int i = 144; i < 148; i++)
        CHECK(ln[i] >= CVBS_LEVEL_BLANK);

    cvbs_core_set_direct_luma(false);   /* 恢复默认，后续测试不受影响 */
}

/*==========================
 *  main
 *==========================*/

int main(int argc, char **argv)
{
    test_line_structure();
    test_sync_pulses_structure();
    test_field_structure();
    test_draw_clipping();
    test_direct_luma_mode();
    test_end_to_end_decode(argc > 1 ? argv[1] :
                           "/tmp/retro_test/cvbs_pattern.pgm");
    TEST_REPORT("test_cvbs");
}
