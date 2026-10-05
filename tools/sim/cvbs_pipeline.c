/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * cvbs_pipeline.c - 全链路 CVBS 管线模拟 / full pipeline CVBS simulation
 *
 * WHAT : 用真实固件链路渲染一帧并解码回图像：
 *        LVGL L8 渲染 -> lv_port_disp flush -> drv_cvbs_send ->
 *        cvbs_core 帧时序 -> 波形 -> 独立解码器 -> PPM
 * WHY  : 分层单测各自为绿不足以证明链路接对——端到端"画什么
 *        显什么"才是视频输出的验收标准
 * WHO  : tools/sim/build.sh（CI 视觉闭环），glm 视觉审查读 PPM
 * WHERE: esp32-retro-ws/tools/sim/cvbs_pipeline.c
 * WHEN : 2026-10-04 新增
 * HOW  : 复用项目 lv_port_disp.c（而非模拟器自有端口），
 *        emit_line 强符号替换为波形采集器；解码同 test_cvbs
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl/lvgl.h"

#include "driver/drv_cvbs.h"
#include "retro_font.h"
#include "driver/cvbs_core.h"

/*==========================
 *  波形采集（覆盖 drv_cvbs 的 weak emit）
 *==========================*/

#define MAX_LINES 700

static unsigned char wave[MAX_LINES * CVBS_LINE_TOTAL];
static int wave_lines = 0;

void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    (void)line_no;
    if (wave_lines < MAX_LINES && len == CVBS_LINE_TOTAL) {
        memcpy(wave + (size_t)wave_lines * CVBS_LINE_TOTAL, line, len);
        wave_lines++;
    }
}

/*==========================
 *  解码器（与 test_cvbs 相同算法的独立实现）
 *==========================*/

static int sync_width(const uint8_t *ln)
{
    int start = -1;
    for (int i = 0; i < CVBS_LINE_TOTAL; i++) {
        if (ln[i] <= 8) {
            if (start < 0) start = i;
        } else if (start >= 0) {
            return i - start;
        }
    }
    return -1;
}

/*
 * WHAT : 把采集波形解码成 PPM / decode captured waveform to PPM
 * HOW  : 行分类（均衡/宽/普通）后取场1活跃区亮度行；
 *        L8 studio-swing 16..235 反映射回 0..255 灰度
 */
static int decode_to_ppm(const char *path)
{
    int x0 = CVBS_H_FRONT_PORCH + CVBS_H_SYNC + CVBS_H_BACK_PORCH;
    int fb_w = drv_cvbs_get_width();
    int fb_h = drv_cvbs_get_height();

    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", fb_w, fb_h);

    int rows = 0;
    int progressive = fb_h <= CVBS_FIELD1_ACTIVE;
    int field2_start = progressive ? -1 : 312;
    for (int ln = 24; ln < wave_lines && rows < fb_h; ln++) {
        if (ln >= CVBS_FIELD_LINES && ln < field2_start + 24)
            continue;               /* 跳到下一场活跃区起点 */
        const uint8_t *l = wave + (size_t)ln * CVBS_LINE_TOTAL;
        int sw = sync_width(l);
        if (sw < 55 || sw > 75)
            continue;               /* 只要正常行 */

        for (int x = 0; x < fb_w; x++) {
            int y = l[x0 + x];
            int v = (y - 16) * 255 / 219;   /* studio swing 反映射 */
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            unsigned char px[3] = { (unsigned char)v,
                                    (unsigned char)v,
                                    (unsigned char)v };
            fwrite(px, 1, 3, f);
        }
        rows++;
    }
    fclose(f);
    printf("pipeline: decoded %d rows -> %s (wave lines %d)\n",
           rows, path, wave_lines);
    return rows == fb_h ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "/tmp/retro_sim/cvbs_pipeline.ppm";

    lv_init();

    /* 真实项目显示端口（内部调 drv_cvbs_init + L8 格式） */
    {
        extern int lv_port_disp_init(void);
        if (lv_port_disp_init() != 0) {
            fprintf(stderr, "pipeline: lv_port_disp_init failed\n");
            return 1;
        }
    }

    /* 主题默认字体 = 全量中文字库（须在显示创建之后设置） */
    {
        lv_theme_t *th = lv_theme_default_init(lv_display_get_default(),
                                               lv_color_hex(0x0000ff),
                                               lv_color_hex(0x00ff00),
                                               false, RETRO_FONT_DEFAULT);
        lv_display_set_theme(lv_display_get_default(), th);
    }

    /* 画一屏内容：标题 + 三块灰阶 + 边框（中文含混排验证） */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32 复古图形工作站 - CVBS 全链路测试");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    static const int grays[] = { 40, 100, 200 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_obj_create(scr);
        lv_obj_set_size(b, 160, 120);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 30 + i * 190, -60);
        lv_obj_set_style_bg_color(b,
            lv_color_make((uint8_t)grays[i], (uint8_t)grays[i],
                          (uint8_t)grays[i]), 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_width(b, 2, 0);

        lv_obj_t *lbl = lv_label_create(b);
        static const char *names[] = { "深灰块", "中灰块", "浅灰块" };
        lv_label_set_text(lbl, names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
    }

    /* 渲染若干帧 */
    for (int i = 0; i < 20; i++) {
        lv_tick_inc(33);
        lv_timer_handler();
    }

    /* 输出一整帧到波形 */
    wave_lines = 0;
    drv_cvbs_frame();

    if (decode_to_ppm(out) != 0) {
        fprintf(stderr, "pipeline: decode incomplete\n");
        return 1;
    }

    printf("pipeline: PASS\n");
    return 0;
}
