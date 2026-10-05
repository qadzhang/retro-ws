/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * lvgl_sim.c - LVGL 无头模拟器 / headless LVGL simulator
 *
 * WHAT : 在宿主机内存帧缓冲上驱动真实 desktop.c/wmaker_shell.c，
 *        渲染结果截图为 PPM 供视觉审查
 * WHY  : GUI 代码的正确性（布局/中文/交互结构）只有在渲染出
 *        图像后才能被验证——模拟器让"看画面"进入 CI 闭环
 * WHO  : tools/sim/build.sh 编译运行；glm 视觉审查读输出的 PPM
 * WHERE: retro-ws/tools/sim/lvgl_sim.c
 * WHEN : 2026-10-04 新增
 * HOW  : lv_init -> 内存 display(I8 640x480, 调色板取自 cvbs_core)
 *        -> retro_desktop_init -> 若干帧 lv_timer_handler ->
 *        截图 -> 切 wmaker 外壳 -> 若干帧 -> 截图 -> 退出码报告
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lvgl/lvgl.h"

#include "driver/cvbs_core.h"
#include "app/desktop_api.h"
#include "i18n.h"
#include "retro_font.h"

#define SIM_W 640
#define SIM_H 480

static uint8_t sim_fb[SIM_W * SIM_H];

static lv_display_t *g_sim_disp;

/*
 * WHAT : flush 回调：把 LVGL 渲染的 L8 行块拷进模拟帧缓冲
 * HOW  : L8 每像素 1 字节（亮度），行距 = 区域宽（无 padding）
 */
static void sim_flush(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    for (int32_t y = 0; y < h; y++) {
        memcpy(&sim_fb[(area->y1 + y) * SIM_W + area->x1],
               px_map + (size_t)y * w, (size_t)w);
    }

    lv_display_flush_ready(disp);
}

/*
 * WHAT : 键盘读回调桩 / stub keyboard read
 * WHY  : desktop 依赖 indev 存在以接收按键；模拟器无输入，
 *        恒报 RELEASED 让事件循环走通
 */
static void sim_keyboard_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

/*
 * WHAT : 截图到 PPM / screenshot framebuffer to PPM
 * HOW  : L8 亮度直接展开为 RGB 灰度
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
            uint8_t v = sim_fb[y * SIM_W + x];
            uint8_t px[3] = { v, v, v };
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

    printf("sim: LVGL %d.%d.%d headless\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    lv_init();

    /* 显示：L8 亮度（与固件 lv_port_disp 同格式，CVBS 链路只传亮度） */
    cvbs_core_set_direct_luma(true);
    g_sim_disp = lv_display_create(SIM_W, SIM_H);
    lv_display_set_color_format(g_sim_disp, LV_COLOR_FORMAT_L8);
    lv_display_set_flush_cb(g_sim_disp, sim_flush);
    lv_display_set_buffers(g_sim_disp, sim_fb, NULL, sizeof(sim_fb),
                           LV_DISP_RENDER_MODE_FULL);

    /* 主题默认字体 = 项目 CJK 字体（中文为默认语言） */
    lv_theme_t *th = lv_theme_default_init(g_sim_disp,
                                           lv_color_hex(0x0000ff),
                                           lv_color_hex(0x00ff00),
                                           false, RETRO_FONT_DEFAULT);
    lv_display_set_theme(g_sim_disp, th);

    /* 输入设备桩 */
    lv_indev_t *kb = lv_indev_create();
    lv_indev_set_type(kb, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb, sim_keyboard_read);

    /* 真实桌面初始化（i18n 默认 zh_CN） */
    i18n_init();
    retro_desktop_init();

    /* 渲染若干帧（让布局/动画稳定） */
    for (int i = 0; i < 30; i++) {
        lv_tick_inc(33);
        lv_timer_handler();
    }
    usleep(100000);
    for (int i = 0; i < 30; i++) {
        lv_tick_inc(33);
        lv_timer_handler();
    }

    snprintf(path, sizeof(path), "%s/desktop_win3.ppm", outdir);
    screenshot(path);

    /* 切换 WindowMaker/NeXT 外壳（先强制失效整个屏幕保证重绘） */
    lv_obj_invalidate(lv_screen_active());
    if (retro_desktop_set_shell(RETRO_SHELL_WMAKER) == 0) {
        lv_obj_invalidate(lv_screen_active());
        for (int i = 0; i < 30; i++)
            lv_timer_handler();
        usleep(100000);
        for (int i = 0; i < 30; i++)
            lv_timer_handler();

        snprintf(path, sizeof(path), "%s/desktop_wmaker.ppm", outdir);
        screenshot(path);
    } else {
        fprintf(stderr, "sim: wmaker shell switch FAILED\n");
        return 1;
    }

    printf("sim: done\n");
    return 0;
}
