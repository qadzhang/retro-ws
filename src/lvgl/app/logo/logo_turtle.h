/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: logo_turtle.h
 * 描述: Logo 小海龟画图解释器接口
 *       简单嵌入式 Logo 解释器，支持基本绘图命令
 *       许可证: MIT
 *
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-31
 */

/*
 * logo_turtle.h - 海龟画图核心头文件
 *
 * WHAT : 海龟画图核心头文件
 * WHY  : logo_turtle/logo_vm/logo_draw/jslogo 共享的类型与 API 契约
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/logo/logo_turtle.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : logo_canvas_t / logo_draw_line_cb / logo_turtle_* 原型
 */

#ifndef __LOGO_TURTLE_H
#define __LOGO_TURTLE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * 海龟图形状态 / Turtle Graphics State
 */
#define LOGO_CANVAS_WIDTH   640
#define LOGO_CANVAS_HEIGHT  480
#define LOGO_MAX_TOKEN_LEN  64
#define LOGO_MAX_LINE_LEN   256

/* 颜色定义 / Color definitions (8-bit palette) */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} logo_color_t;

/* 海龟状态 / Turtle state */
typedef struct {
    int x;              /* X 坐标 / X position */
    int y;              /* Y 坐标 / Y position */
    int heading;        /* 朝向角度 (0-359) / Heading angle */
    bool pen_down;      /* 画笔是否落下 / Pen down state */
    bool visible;       /* 海龟是否可见 / Turtle visibility */
    logo_color_t pen_color;  /* 画笔颜色 / Pen color */
    int pen_size;       /* 画笔粗细 / Pen size */
} logo_turtle_t;

/* 画布状态 / Canvas state */
typedef struct {
    int width;          /* 画布宽度 / Canvas width */
    int height;         /* 画布高度 / Canvas height */
    logo_color_t bg_color;  /* 背景颜色 / Background color */
    logo_turtle_t turtle;   /* 海龟状态 / Turtle state */
} logo_canvas_t;

/* 解释器状态 / Interpreter state */
typedef enum {
    LOGO_OK = 0,
    LOGO_ERR_PARSE,        /* 解析错误 / Parse error */
    LOGO_ERR_UNKNOWN_CMD,   /* 未知命令 / Unknown command */
    LOGO_ERR_SYNTAX,        /* 语法错误 / Syntax error */
    LOGO_ERR_STACK_OVERFLOW,/* 栈溢出 / Stack overflow */
    LOGO_ERR_DIVISION_ZERO  /* 除零错误 / Division by zero */
} logo_error_t;

/* 命令处理函数类型 / Command handler function type */
typedef int (*logo_cmd_handler_t)(void *user_data);

/* 图形输出回调 / Graphics output callback
 * 当需要绘制线段时调用 / Called when a line needs to be drawn
 */
typedef int (*logo_draw_line_cb)(
    void *user_data,
    int x1, int y1,      /* 起点 / Start point */
    int x2, int y2,      /* 终点 / End point */
    logo_color_t color,  /* 颜色 / Color */
    int pen_size         /* 画笔粗细 / Pen size */
);

/* 初始化 / Initialize */
int logo_init(logo_canvas_t *canvas);

/* 重置 / Reset */
void logo_reset(logo_canvas_t *canvas);

/* 设置画布大小 / Set canvas size */
void logo_set_canvas_size(logo_canvas_t *canvas, int width, int height);

/* 海龟控制 / Turtle control */
void logo_turtle_forward(logo_canvas_t *canvas, int distance);
void logo_turtle_back(logo_canvas_t *canvas, int distance);
void logo_turtle_left(logo_canvas_t *canvas, int angle);
void logo_turtle_right(logo_canvas_t *canvas, int angle);
void logo_turtle_pen_up(logo_canvas_t *canvas);
void logo_turtle_pen_down(logo_canvas_t *canvas);
void logo_turtle_set_pos(logo_canvas_t *canvas, int x, int y);
void logo_turtle_set_heading(logo_canvas_t *canvas, int angle);
void logo_turtle_home(logo_canvas_t *canvas);
void logo_turtle_show(logo_canvas_t *canvas);
void logo_turtle_hide(logo_canvas_t *canvas);

/* 画笔控制 / Pen control */
void logo_set_pen_color(logo_canvas_t *canvas, logo_color_t color);
void logo_set_pen_size(logo_canvas_t *canvas, int size);

/* 画布控制 / Canvas control */
void logo_clear_screen(logo_canvas_t *canvas);
void logo_set_bg_color(logo_canvas_t *canvas, logo_color_t color);

/* 解析并执行一行代码 / Parse and execute one line */
int logo_execute_line(logo_canvas_t *canvas, const char *line);

/* 注册绘图回调 / Register drawing callback */
void logo_register_draw_callback(logo_draw_line_cb callback, void *user_data);

/* 预定义颜色 / Predefined colors */
extern const logo_color_t LOGO_COLOR_WHITE;
extern const logo_color_t LOGO_COLOR_BLACK;
extern const logo_color_t LOGO_COLOR_RED;
extern const logo_color_t LOGO_COLOR_GREEN;
extern const logo_color_t LOGO_COLOR_BLUE;
extern const logo_color_t LOGO_COLOR_YELLOW;
extern const logo_color_t LOGO_COLOR_ORANGE;
extern const logo_color_t LOGO_COLOR_PURPLE;
extern const logo_color_t LOGO_COLOR_CYAN;
extern const logo_color_t LOGO_COLOR_PINK;

/*
 * Logo 命令列表 / Supported Logo commands
 *
 * 运动命令 / Movement commands:
 *   FORWARD n (FD n)     - 前进 n 步
 *   BACK n (BK n)        - 后退 n 步
 *   LEFT n (LT n)        - 左转 n 度
 *   RIGHT n (RT n)       - 右转 n 度
 *   SETPOS [x y]         - 设置坐标
 *   SETX x               - 设置 X 坐标
 *   SETY y               - 设置 Y 坐标
 *   SETHEADING n (SETH n) - 设置朝向
 *   HOME                 - 回到原点
 *
 * 画笔命令 / Pen commands:
 *   PENUP (PU)           - 抬起画笔
 *   PENDOWN (PD)         - 落下画笔
 *   SETPENCOLOR [r g b]  - 设置颜色
 *   SETPENSIZE n         - 设置粗细
 *
 * 海龟命令 / Turtle commands:
 *   SHOWTURTLE (ST)       - 显示海龟
 *   HIDETURTLE (HT)      - 隐藏海龟
 *
 * 画布命令 / Canvas commands:
 *   CLEARSCREEN (CS)      - 清屏
 *   SETBGCOLOR [r g b]    - 设置背景色
 *
 * 控制结构 / Control structures:
 *   REPEAT n [commands]   - 重复 n 次
 *   TO name ... END      - 定义过程 (future)
 */

#endif /* __LOGO_TURTLE_H */
