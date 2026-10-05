/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: logo_turtle.c
 * 描述: Logo 小海龟画图解释器实现
 *       简单嵌入式 Logo 解释器，支持基本绘图命令
 *       许可证: MIT
 *
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-31
 */

/*
 * logo_turtle.c - 海龟画图核心（自研 Logo）
 *
 * WHAT : 海龟画图核心（自研 Logo）
 * WHY  : 复古 Logo 小海龟绘图的核心状态机
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/logo/logo_turtle.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : logo_canvas_t 保存海龟状态，forward/left 等经注册的画线回调输出
 */

#define _GNU_SOURCE  /* 用于 M_PI / For M_PI */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "logo_turtle.h"

/*
 * 预定义颜色实现 / Predefined colors implementation
 */
const logo_color_t LOGO_COLOR_WHITE   = {255, 255, 255};
const logo_color_t LOGO_COLOR_BLACK   = {  0,   0,   0};
const logo_color_t LOGO_COLOR_RED     = {255,   0,   0};
const logo_color_t LOGO_COLOR_GREEN   = {  0, 255,   0};
const logo_color_t LOGO_COLOR_BLUE    = {  0,   0, 255};
const logo_color_t LOGO_COLOR_YELLOW  = {255, 255,   0};
const logo_color_t LOGO_COLOR_ORANGE  = {255, 128,   0};
const logo_color_t LOGO_COLOR_PURPLE  = {128,   0, 128};
const logo_color_t LOGO_COLOR_CYAN    = {  0, 255, 255};
const logo_color_t LOGO_COLOR_PINK    = {255, 192, 203};

/*
 * 内部状态 / Internal state
 */
static logo_draw_line_cb g_draw_callback = NULL;  /* 绘图回调 / Drawing callback */
static void *g_draw_user_data = NULL;            /* 用户数据 / User data */

/*
 * 颜色比较 / Color comparison
 */
static bool logo_color_equal(logo_color_t a, logo_color_t b)
{
    return (a.r == b.r && a.g == b.g && a.b == b.b);
}

/*
 * 角度转弧度 / Convert degrees to radians
 */
static double logo_deg_to_rad(int degrees)
{
    return degrees * M_PI / 180.0;
}

/*
 * 初始化 / Initialize
 */
int logo_init(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return -1;

    canvas->width = LOGO_CANVAS_WIDTH;
    canvas->height = LOGO_CANVAS_HEIGHT;
    canvas->bg_color = LOGO_COLOR_BLACK;
    canvas->turtle.x = canvas->width / 2;
    canvas->turtle.y = canvas->height / 2;
    canvas->turtle.heading = 0;  /* 朝上 / Facing up */
    canvas->turtle.pen_down = true;
    canvas->turtle.visible = true;
    canvas->turtle.pen_color = LOGO_COLOR_WHITE;
    canvas->turtle.pen_size = 1;

    return 0;
}

/*
 * 重置 / Reset
 */
void logo_reset(logo_canvas_t *canvas)
{
    logo_init(canvas);
}

/*
 * 设置画布大小 / Set canvas size
 */
void logo_set_canvas_size(logo_canvas_t *canvas, int width, int height)
{
    if (canvas == NULL || width <= 0 || height <= 0)
        return;

    canvas->width = width;
    canvas->height = height;
    logo_turtle_home(canvas);
}

/*
 * 内部绘图函数 / Internal draw function
 */
static void logo_draw(int x1, int y1, int x2, int y2, void *user_data)
{
    logo_canvas_t *canvas = (logo_canvas_t *)user_data;

    /* 如果画笔落下且有回调，调用绘图回调 */
    /* If pen is down and callback exists, call drawing callback */
    if (canvas->turtle.pen_down && g_draw_callback != NULL) {
        g_draw_callback(g_draw_user_data, x1, y1, x2, y2,
                       canvas->turtle.pen_color,
                       canvas->turtle.pen_size);
    }
}

/*
 * 海龟前进 / Turtle moves forward
 */
void logo_turtle_forward(logo_canvas_t *canvas, int distance)
{
    int old_x, old_y;
    int new_x, new_y;

    if (canvas == NULL || distance == 0)
        return;

    old_x = canvas->turtle.x;
    old_y = canvas->turtle.y;

    /* 根据当前朝向计算新位置 */
    /* Calculate new position based on current heading */
    double rad = logo_deg_to_rad(canvas->turtle.heading);
    new_x = old_x + (int)(distance * sin(rad));
    new_y = old_y - (int)(distance * cos(rad));

    /* 边界检查 / Boundary check - wrap around */
    if (new_x < 0) new_x += canvas->width;
    if (new_x >= canvas->width) new_x -= canvas->width;
    if (new_y < 0) new_y += canvas->height;
    if (new_y >= canvas->height) new_y -= canvas->height;

    canvas->turtle.x = new_x;
    canvas->turtle.y = new_y;

    /* 绘制线段 / Draw line */
    logo_draw(old_x, old_y, new_x, new_y, canvas);
}

/*
 * 海龟后退 / Turtle moves backward
 */
void logo_turtle_back(logo_canvas_t *canvas, int distance)
{
    if (canvas == NULL || distance == 0)
        return;

    logo_turtle_forward(canvas, -distance);
}

/*
 * 海龟左转 / Turtle turns left (counterclockwise)
 * Logo convention: 0=up, 90=right, 180=down, 270=left
 * LEFT subtracts from heading (counterclockwise from up goes to left=270)
 */
void logo_turtle_left(logo_canvas_t *canvas, int angle)
{
    if (canvas == NULL)
        return;

    canvas->turtle.heading = (canvas->turtle.heading - angle) % 360;
    if (canvas->turtle.heading < 0)
        canvas->turtle.heading += 360;
}

/*
 * 海龟右转 / Turtle turns right (clockwise)
 * RIGHT adds to heading (clockwise from up goes to right=90)
 */
void logo_turtle_right(logo_canvas_t *canvas, int angle)
{
    if (canvas == NULL)
        return;

    canvas->turtle.heading = (canvas->turtle.heading + angle) % 360;
}

/*
 * 抬笔 / Pen up
 */
void logo_turtle_pen_up(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;
    canvas->turtle.pen_down = false;
}

/*
 * 落笔 / Pen down
 */
void logo_turtle_pen_down(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;
    canvas->turtle.pen_down = true;
}

/*
 * 设置位置 / Set position
 */
void logo_turtle_set_pos(logo_canvas_t *canvas, int x, int y)
{
    int old_x, old_y;

    if (canvas == NULL)
        return;

    old_x = canvas->turtle.x;
    old_y = canvas->turtle.y;

    canvas->turtle.x = x % canvas->width;
    canvas->turtle.y = y % canvas->height;

    /* 如果画笔落下，绘制线段 */
    /* If pen is down, draw line */
    if (canvas->turtle.pen_down) {
        logo_draw(old_x, old_y, canvas->turtle.x, canvas->turtle.y, canvas);
    }
}

/*
 * 设置朝向 / Set heading
 */
void logo_turtle_set_heading(logo_canvas_t *canvas, int angle)
{
    if (canvas == NULL)
        return;

    canvas->turtle.heading = angle % 360;
    if (canvas->turtle.heading < 0)
        canvas->turtle.heading += 360;
}

/*
 * 回到原点 / Return home
 */
void logo_turtle_home(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;

    canvas->turtle.x = canvas->width / 2;
    canvas->turtle.y = canvas->height / 2;
    canvas->turtle.heading = 0;
}

/*
 * 显示海龟 / Show turtle
 */
void logo_turtle_show(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;
    canvas->turtle.visible = true;
}

/*
 * 隐藏海龟 / Hide turtle
 */
void logo_turtle_hide(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;
    canvas->turtle.visible = false;
}

/*
 * 设置画笔颜色 / Set pen color
 */
void logo_set_pen_color(logo_canvas_t *canvas, logo_color_t color)
{
    if (canvas == NULL)
        return;
    canvas->turtle.pen_color = color;
}

/*
 * 设置画笔粗细 / Set pen size
 */
void logo_set_pen_size(logo_canvas_t *canvas, int size)
{
    if (canvas == NULL || size <= 0)
        return;
    canvas->turtle.pen_size = size;
}

/*
 * 清屏 / Clear screen
 */
void logo_clear_screen(logo_canvas_t *canvas)
{
    if (canvas == NULL)
        return;

    logo_turtle_home(canvas);
    /* 注意：实际清屏由调用者通过回调完成 */
    /* Note: Actual clearing is done by caller through callback */
}

/*
 * 设置背景色 / Set background color
 */
void logo_set_bg_color(logo_canvas_t *canvas, logo_color_t color)
{
    if (canvas == NULL)
        return;
    canvas->bg_color = color;
}

/*
 * 注册绘图回调 / Register drawing callback
 */
void logo_register_draw_callback(logo_draw_line_cb callback, void *user_data)
{
    g_draw_callback = callback;
    g_draw_user_data = user_data;
}
