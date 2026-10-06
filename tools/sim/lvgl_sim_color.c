/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * lvgl_sim_color.c - LVGL 彩色无头模拟器（README 截图专用）
 *
 * WHAT : 以 RGB565 真彩驱动真实 desktop.c/wmaker_shell.c，出 640x480
 *        彩色截图（再经 PIL 量化为 256 色调色板 PNG = "8bit 彩色"，
 *        对齐 Win3.2 时代 VGA 256 色观感）
 * WHY  : 固件 CVBS 链路只传亮度（L8），但需求文档定义 GUI 为
 *        8-bit 调色板 256 色（REQUIREMENTS 2.2.1）——彩色截图展示
 *        目标视觉规格；README 文档配图
 * WHO  : tools/sim/build.sh 编译；docs/screenshots/ 消费
 * WHERE: retro-ws/tools/sim/lvgl_sim_color.c
 * WHEN : 2026-10-05 新增（README 截图需求）
 * HOW  : 与 lvgl_sim.c 同流程，仅显示格式 L8 -> RGB565；
 *        截图 PPM 为 RGB888（P6），转 PNG 时 quantize(256)
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl/lvgl.h"

#include "app/desktop_api.h"
#include "i18n.h"
#include "retro_font.h"
#include "app/app_pinyin.h"

#define SIM_W 640
#define SIM_H 480

static uint16_t sim_fb[SIM_W * SIM_H];

/*
 * WHAT : flush 回调：LVGL RGB565 渲染块拷进模拟帧缓冲
 */
static void sim_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    for (int32_t y = 0; y < h; y++) {
        memcpy(&sim_fb[(area->y1 + y) * SIM_W + area->x1],
               px_map + (size_t)y * w * 2, (size_t)w * 2);
    }

    lv_display_flush_ready(disp);
}

static void sim_keyboard_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

/*
 * WHAT : 截图到 PPM（RGB888 展开）
 * HOW  : RGB565 -> R8G8B8（高位补零展开）
 */
static void screenshot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sim: cannot write %s\n", path);
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", SIM_W, SIM_H);

    for (int y = 0; y < SIM_H; y++) {
        for (int x = 0; x < SIM_W; x++) {
            uint16_t c = sim_fb[y * SIM_W + x];
            uint8_t r = (uint8_t)((c >> 11) & 0x1F) * 255 / 31;
            uint8_t g = (uint8_t)((c >> 5) & 0x3F) * 255 / 63;
            uint8_t b = (uint8_t)(c & 0x1F) * 255 / 31;
            uint8_t px[3] = { r, g, b };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
    printf("sim: screenshot %s\n", path);
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    char path[512];

    printf("sim(color): LVGL %d.%d.%d headless RGB565\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();

    lv_display_t *disp = lv_display_create(SIM_W, SIM_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, sim_flush);
    lv_display_set_buffers(disp, sim_fb, NULL, sizeof(sim_fb),
                           LV_DISP_RENDER_MODE_FULL);

    lv_theme_t *th = lv_theme_default_init(disp,
                                           lv_color_hex(0x0000ff),
                                           lv_color_hex(0x00ff00),
                                           false, RETRO_FONT_DEFAULT);
    lv_display_set_theme(disp, th);

    lv_indev_t *kb = lv_indev_create();
    lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb, sim_keyboard_read);

    i18n_init();
    retro_desktop_init();

    for (int i = 0; i < 30; i++) {
        lv_tick_inc(33);
        lv_timer_handler();
    }
    usleep(100000);
    for (int i = 0; i < 30; i++) {
        lv_tick_inc(33);
        lv_timer_handler();
    }

    snprintf(path, sizeof(path), "%s/desktop_win3_color.ppm", outdir);
    screenshot(path);

    /* 第三景：Win95 式拼音输入（文本框 + IME 候选面板，喂 nihao） */
    {
        lv_obj_t *win = lv_obj_create(lv_screen_active());
        lv_obj_set_size(win, 340, 150);
        lv_obj_set_pos(win, 140, 120);
        lv_obj_set_style_bg_color(win, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_border_color(win, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(win, 0, 0);

        lv_obj_t *title = lv_label_create(win);
        lv_label_set_text(title, "记事本 - 无标题");
        lv_obj_set_style_text_font(title, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 4);

        lv_obj_t *ta = lv_textarea_create(win);
        lv_obj_set_size(ta, 320, 60);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 24);
        lv_textarea_set_one_line(ta, false);
        lv_obj_set_style_text_font(ta, RETRO_FONT_DEFAULT, 0);

        /* IME 面板挂顶层 layer（Win95 输入条=悬浮于所有窗口之上；
         * screen 被桌面布局管理，set_pos 会被 layout 覆盖） */
        app_pinyin_init(lv_layer_top());
        {
            lv_obj_t *pnl = NULL;
            uint32_t cnt = lv_obj_get_child_count(lv_layer_top());

            for (uint32_t i = cnt; i > 0; i--) {
                lv_obj_t *c = lv_obj_get_child(lv_layer_top(), i - 1);

                if (lv_obj_get_width(c) > 200) {   /* 找到全宽面板 */
                    pnl = c;
                    break;
                }
            }
            if (pnl != NULL)
                lv_obj_set_pos(pnl, 0, 480 - 74);
        }
        app_pinyin_set_target(ta);
        app_pinyin_input('n');
        app_pinyin_input('i');
        app_pinyin_input('h');
        app_pinyin_input('a');
        app_pinyin_input('o');

        for (int i = 0; i < 30; i++) {
            lv_tick_inc(33);
            lv_timer_handler();
        }
        usleep(100000);
        for (int i = 0; i < 30; i++) {
            lv_tick_inc(33);
            lv_timer_handler();
        }
        snprintf(path, sizeof(path), "%s/ime_win95.ppm", outdir);
        screenshot(path);

        /* 场景清理：删演示窗口与 IME 顶层面板，残留会盖住 wmaker 景
         * 的 Dock 时钟与工作区（旧 wmaker 截图早于 IME 景加入，为保
         * 文档截图同等干净，切壳前先清理）
         * Scene cleanup: drop demo window + IME top-layer panel so the
         * leftovers do not cover the wmaker dock clock / workspace in
         * the next screenshot (added 2026-10-06, app-split refactor) */
        lv_obj_delete(win);
        for (uint32_t i = lv_obj_get_child_count(lv_layer_top()); i > 0; i--) {
            lv_obj_t *c = lv_obj_get_child(lv_layer_top(), i - 1);
            if (lv_obj_get_width(c) > 200)   /* 全宽 IME 面板 / IME panel */
                lv_obj_delete(c);
        }
    }

    lv_obj_invalidate(lv_screen_active());
    if (retro_desktop_set_shell(RETRO_SHELL_WMAKER) == 0) {
        lv_obj_invalidate(lv_screen_active());
        for (int i = 0; i < 30; i++)
            lv_timer_handler();
        usleep(100000);
        for (int i = 0; i < 30; i++)
            lv_timer_handler();

        snprintf(path, sizeof(path), "%s/desktop_wmaker_color.ppm", outdir);
        screenshot(path);
    } else {
        fprintf(stderr, "sim: wmaker shell switch FAILED\n");
        return 1;
    }

    printf("sim(color): done\n");
    return 0;
}
