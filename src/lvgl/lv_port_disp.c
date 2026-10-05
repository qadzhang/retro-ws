/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 *
 * 文件: lv_port_disp.c
 * 描述: LVGL 显示端口驱动 - CVBS 显示输出
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.2.0
 * 日期: 2026-10-04
 */

/*
 * lv_port_disp.c - LVGL 显示端口
 *
 * WHAT : LVGL 显示端口（L8 亮度格式）
 * WHY  : 把 LVGL 刷新接到 CVBS 显示驱动；CVBS 只携带亮度信号，
 *        L8 (8-bit luminance) 与之天然匹配，且无需调色板
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/lv_port_disp.c
 * WHEN : 2026-03~04 初版，2026-10-04 移除 9.5 不存在的 set_px/px_cb，改行拷贝
 * HOW  : PARTIAL 绘制缓冲 + disp_flush 逐行拷入全尺寸帧缓冲，
 *        再 drv_cvbs_send 送 CVBS；cvbs_core_set_direct_luma(true) 让
 *        帧缓冲字节直映射 CVBS studio-swing 亮度 (16..235)
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <errno.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <lvgl/lvgl.h>

#include "driver/drv_cvbs.h"

/* 显示尺寸缺省（宿主机模拟器/未配 Kconfig 时兜底为控制台档） */
#ifndef CONFIG_RETRO_DISPLAY_WIDTH
#  define CONFIG_RETRO_DISPLAY_WIDTH  320
#endif
#ifndef CONFIG_RETRO_DISPLAY_HEIGHT
#  define CONFIG_RETRO_DISPLAY_HEIGHT 240
#endif
#include "driver/cvbs_core.h"

static lv_display_t *g_disp = NULL;

/* L8 帧缓冲: 每像素 1 字节亮度。
 * 大缓冲（640x480=300KB）经 malloc 进 SPIRAM 堆；静态数组会爆 SRAM */
static uint8_t *g_fb = NULL;
static uint32_t g_fb_size = 0;
static uint32_t g_fb_stride = 0;   /* 行跨度(字节) / Row stride in bytes */

/*
 * disp_flush - LVGL 刷新回调 / LVGL flush callback
 * WHAT: 把部分重绘的 px_map 拷入帧缓冲并送 CVBS
 * WHY  : PARTIAL 模式下 px_map 是按刷新区打包的，需按行展开到全屏缓冲
 * HOW  : 每行拷贝 w 字节（L8 每像素 1 字节，源行跨度 = lv_area_get_width），
 *        目的偏移 = y*屏宽 + x；完成后 drv_cvbs_send 整帧
 */
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t src_stride;
    int32_t y;

    if (disp == NULL || area == NULL || px_map == NULL)
        return;

    /* L8: 源行跨度 = 刷新区宽度(字节) / L8: source row stride = area width */
    src_stride = lv_area_get_width(area);

    for (y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= CONFIG_RETRO_DISPLAY_HEIGHT)
            continue;

        uint8_t *dst = &g_fb[(uint32_t)y * g_fb_stride + (uint32_t)area->x1];
        uint8_t *src = &px_map[(uint32_t)(y - area->y1) * (uint32_t)src_stride];
        uint32_t copy_len = (uint32_t)src_stride;

        /* 裁剪水平越界 / Clip horizontal overflow */
        if (area->x1 + (int32_t)copy_len > CONFIG_RETRO_DISPLAY_WIDTH)
            copy_len = (uint32_t)(CONFIG_RETRO_DISPLAY_WIDTH - area->x1);

        memcpy(dst, src, copy_len);
    }

    drv_cvbs_send(g_fb, g_fb_size);

    lv_display_flush_ready(disp);
}

int lv_port_disp_init(void)
{
    int ret;
    lv_display_t *disp;

    syslog(LOG_INFO, "lv_port_disp: Initializing LVGL display port (L8)\n");

    g_fb_size = (uint32_t)CONFIG_RETRO_DISPLAY_WIDTH *
                (uint32_t)CONFIG_RETRO_DISPLAY_HEIGHT;
    g_fb_stride = (uint32_t)CONFIG_RETRO_DISPLAY_WIDTH;

    /* 先初始化 CVBS 驱动（内部分配帧缓冲），再取缓冲指针
     * 2026-10-04 晚修复：原实现在此之前 memset 空指针——设备 GUI
     * 启动即崩，由 tools/sim cvbs_pipeline 模拟器捕获 */
    ret = drv_cvbs_init();
    if (ret < 0)
    {
        syslog(LOG_ERR, "lv_port_disp: CVBS driver init failed: %d\n", ret);
        return ret;
    }

    g_fb = drv_cvbs_get_fb();
    if (g_fb == NULL)
    {
        syslog(LOG_ERR, "lv_port_disp: CVBS framebuffer is NULL\n");
        return -ENOMEM;
    }

    memset(g_fb, 16, g_fb_size);  /* 初始为黑（studio 黑 16）/ init black */

    /* 帧缓冲字节直映射 CVBS 亮度（16..235，不低于消隐电平）
     * Framebuffer bytes map directly to CVBS studio-swing luma */
    cvbs_core_set_direct_luma(true);

    disp = lv_display_create(CONFIG_RETRO_DISPLAY_WIDTH, CONFIG_RETRO_DISPLAY_HEIGHT);
    if (disp == NULL)
    {
        syslog(LOG_ERR, "lv_port_disp: Display create failed\n");
        return -ENOMEM;
    }

    /* L8 亮度格式：CVBS 仅传亮度，无需调色板
     * L8 luminance format: CVBS carries luma only, no palette needed */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);

    /* PARTIAL 渲染：LVGL 绘到部分缓冲，flush 再展开进全帧
     * PARTIAL render: LVGL draws into partial buffer, flush expands */
    lv_display_set_buffers(disp, g_fb, NULL, g_fb_size, LV_DISP_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(disp, disp_flush);

    g_disp = disp;

    syslog(LOG_INFO, "lv_port_disp: LVGL display port initialized: %dx%d L8\n",
           CONFIG_RETRO_DISPLAY_WIDTH, CONFIG_RETRO_DISPLAY_HEIGHT);

    return OK;
}

void lv_port_disp_deinit(void)
{
    if (g_disp != NULL)
    {
        lv_display_delete(g_disp);
        g_disp = NULL;
    }

    drv_cvbs_deinit();

    syslog(LOG_INFO, "lv_port_disp: LVGL display port deinitialized\n");
}

lv_display_t *lv_port_disp_get(void)
{
    return g_disp;
}

int lv_port_disp_set_resolution(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    return OK;
}
