/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: logo_draw.c
 * 描述: Logo 绘图输出 - LVGL 绑定层
 *       将 Logo 绘图命令输出到 LVGL 显示
 *       许可证: MIT
 *
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-31
 */

/*
 * logo_draw.c - 海龟画图 LVGL 后端
 *
 * WHAT : 海龟画图 LVGL 后端
 * WHY  : 把海龟线段渲染到屏幕，并对外提供绘图上下文
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/logo/logo_draw.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : logo_draw_line_cb 实现 LVGL 画线，logo_draw_create/get_canvas 供前端复用
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logo_turtle.h"

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

/*
 * Logo 绘图上下文 / Logo draw context
 */
typedef struct {
    logo_canvas_t canvas;
#ifdef CONFIG_LVGL
    lv_obj_t *canvas_obj;       /* LVGL 画布对象 / LVGL canvas object */
    lv_color_t *buf;           /* 画布缓冲区 / Canvas buffer */
#endif
} logo_draw_context_t;

/*
 * LVGL 绘图回调实现 / LVGL drawing callback implementation
 */
#ifdef CONFIG_LVGL
/*
 * 线段端点静态池 / Static pool of segment endpoints
 * WHAT: 预分配的 lv_point_precise_t[2] 块，供 lv_line_set_points 引用
 * WHY  : lv_line_set_points 只保存指针，栈上数组在回调返回后失效；
 *        静态池让端点在线对象生命周期内有效（环形复用，覆盖最老的线）
 * HOW  : 256 块循环分配；超过后最老线段的端点被复用（视觉可接受）
 */
#define LOGO_POINT_POOL_SIZE 256
static lv_point_precise_t g_line_points_pool[LOGO_POINT_POOL_SIZE][2];
static uint32_t g_line_points_next = 0;

static int logo_lv_draw_line(
    void *user_data,
    int x1, int y1,
    int x2, int y2,
    logo_color_t color,
    int pen_size)
{
    logo_draw_context_t *ctx = (logo_draw_context_t *)user_data;

    if (ctx == NULL || ctx->canvas_obj == NULL)
        return -1;

    /* 从静态池取一块端点存储（环形复用 / wrap-around reuse） */
    lv_point_precise_t *points =
        g_line_points_pool[g_line_points_next % LOGO_POINT_POOL_SIZE];
    g_line_points_next++;

    points[0].x = (lv_value_precise_t)x1;
    points[0].y = (lv_value_precise_t)y1;
    points[1].x = (lv_value_precise_t)x2;
    points[1].y = (lv_value_precise_t)y2;

    lv_obj_t *line = lv_line_create(ctx->canvas_obj);
    lv_line_set_points(line, points, 2);

    /* 设置线条颜色 / Set line color */
    lv_obj_set_style_line_color(line, lv_color_make(color.r, color.g, color.b), 0);

    /* 设置线条宽度 / Set line width */
    lv_obj_set_style_line_width(line, (int32_t)pen_size, 0);

    return 0;
}
#endif /* CONFIG_LVGL */

/*
 * 创建 Logo 绘图上下文 / Create Logo draw context
 */
logo_draw_context_t *logo_draw_create(int width, int height)
{
    logo_draw_context_t *ctx;

    ctx = (logo_draw_context_t *)malloc(sizeof(logo_draw_context_t));
    if (ctx == NULL)
        return NULL;

    memset(ctx, 0, sizeof(logo_draw_context_t));

    /* 初始化画布 / Initialize canvas */
    logo_init(&ctx->canvas);
    logo_set_canvas_size(&ctx->canvas, width, height);

#ifdef CONFIG_LVGL
    /* 创建 LVGL 画布 / Create LVGL canvas */
    ctx->canvas_obj = lv_canvas_create(lv_screen_active());
    if (ctx->canvas_obj == NULL) {
        free(ctx);
        return NULL;
    }

    /* 分配画布缓冲区 / Allocate canvas buffer */
    ctx->buf = (lv_color_t *)malloc(width * height * sizeof(lv_color_t));
    if (ctx->buf == NULL) {
        lv_obj_delete(ctx->canvas_obj);
        free(ctx);
        return NULL;
    }

    lv_canvas_set_buffer(ctx->canvas_obj, ctx->buf, width, height, LV_COLOR_FORMAT_NATIVE);
    lv_obj_center(ctx->canvas_obj);

    /* 设置背景色 / Set background color */
    lv_canvas_fill_bg(ctx->canvas_obj, lv_color_black(), LV_OPA_COVER);
#endif

    /* 注册绘图回调 / Register drawing callback */
#ifdef CONFIG_LVGL
    logo_register_draw_callback(logo_lv_draw_line, ctx);
#else
    /* 无 LVGL 时使用默认回调 / Use default callback without LVGL */
    logo_register_draw_callback(NULL, ctx);
#endif

    return ctx;
}

/*
 * 销毁 Logo 绘图上下文 / Destroy Logo draw context
 */
void logo_draw_destroy(logo_draw_context_t *ctx)
{
    if (ctx == NULL)
        return;

#ifdef CONFIG_LVGL
    if (ctx->canvas_obj)
        lv_obj_delete(ctx->canvas_obj);
    if (ctx->buf)
        free(ctx->buf);
#endif

    free(ctx);
}

/*
 * 获取画布 / Get canvas
 */
logo_canvas_t *logo_draw_get_canvas(logo_draw_context_t *ctx)
{
    if (ctx == NULL)
        return NULL;
    return &ctx->canvas;
}

/*
 * 执行 Logo 代码 / Execute Logo code
 */
int logo_draw_execute(logo_draw_context_t *ctx, const char *code)
{
    if (ctx == NULL || code == NULL)
        return -1;

    return logo_execute_line(&ctx->canvas, code);
}

/*
 * 清空画布 / Clear canvas
 */
void logo_draw_clear(logo_draw_context_t *ctx)
{
    if (ctx == NULL)
        return;

#ifdef CONFIG_LVGL
    if (ctx->canvas_obj) {
        lv_canvas_fill_bg(ctx->canvas_obj, lv_color_black(), LV_OPA_COVER);
    }
#endif

    logo_clear_screen(&ctx->canvas);
}
