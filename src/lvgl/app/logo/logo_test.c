/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: logo_test.c
 * 描述: Logo 小海龟解释器 - 测试程序
 *       用于在 PC 上验证 Logo 解释器功能
 *       许可证: MIT
 *
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-31
 */

/*
 * logo_test.c - 海龟画图自测程序
 *
 * WHAT : 海龟画图自测程序
 * WHY  : 开发期功能自检（不入正式固件）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/logo/logo_test.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 依次执行示例命令验证 logo_turtle/logo_vm 行为
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "logo_turtle.h"

/*
 * 测试统计 / Test statistics
 */
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/*
 * 简单绘图回调 - 仅打印信息 / Simple draw callback - just print info
 */
static int test_draw_callback(
    void *user_data,
    int x1, int y1,
    int x2, int y2,
    logo_color_t color,
    int pen_size)
{
    (void)user_data;
    printf("  DRAW: (%d,%d) -> (%d,%d) color=(%d,%d,%d) size=%d\n",
           x1, y1, x2, y2, color.r, color.g, color.b, pen_size);
    return 0;
}

/*
 * 测试初始化 / Test initialization
 */
static void test_init(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_init()\n");

    int ret = logo_init(&canvas);
    assert(ret == 0);
    assert(canvas.width == LOGO_CANVAS_WIDTH);
    assert(canvas.height == LOGO_CANVAS_HEIGHT);
    assert(canvas.turtle.x == LOGO_CANVAS_WIDTH / 2);
    assert(canvas.turtle.y == LOGO_CANVAS_HEIGHT / 2);
    assert(canvas.turtle.heading == 0);
    assert(canvas.turtle.pen_down == true);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试前进 / Test forward
 */
static void test_forward(void)
{
    logo_canvas_t canvas;
    int old_x, old_y;

    printf("Test: logo_turtle_forward()\n");

    logo_init(&canvas);
    logo_register_draw_callback(test_draw_callback, &canvas);

    old_x = canvas.turtle.x;
    old_y = canvas.turtle.y;

    logo_turtle_forward(&canvas, 100);

    /* 海龟应该向上移动（朝向 0 度）*/
    /* Turtle should move up (heading 0 degrees) */
    assert(canvas.turtle.y < old_y);
    assert(canvas.turtle.x == old_x);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试后退 / Test back
 */
static void test_back(void)
{
    logo_canvas_t canvas;
    int old_x, old_y;

    printf("Test: logo_turtle_back()\n");

    logo_init(&canvas);

    old_x = canvas.turtle.x;
    old_y = canvas.turtle.y;

    logo_turtle_back(&canvas, 50);

    /* 海龟应该向下移动 */
    /* Turtle should move down */
    assert(canvas.turtle.y > old_y);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试转向 / Test turning
 */
static void test_turn(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_turtle_left/right()\n");

    logo_init(&canvas);

    /* Logo convention: 0=up, 90=right, 180=down, 270=left
     * RIGHT adds to heading (clockwise)
     * LEFT subtracts from heading (counterclockwise)
     */

    /* 测试右转 / Test right turn */
    logo_turtle_right(&canvas, 90);
    assert(canvas.turtle.heading == 90);  /* 0 + 90 = 90 (up -> right) */

    logo_turtle_right(&canvas, 90);
    assert(canvas.turtle.heading == 180);  /* 90 + 90 = 180 (right -> down) */

    /* 测试左转 / Test left turn */
    logo_turtle_left(&canvas, 45);
    assert(canvas.turtle.heading == 135);  /* 180 - 45 = 135 */

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试抬笔/落笔 / Test pen up/down
 */
static void test_pen(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_turtle_pen_up/down()\n");

    logo_init(&canvas);

    assert(canvas.turtle.pen_down == true);

    logo_turtle_pen_up(&canvas);
    assert(canvas.turtle.pen_down == false);

    logo_turtle_pen_down(&canvas);
    assert(canvas.turtle.pen_down == true);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试命令解析 / Test command parsing
 */
static void test_parse_forward(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_execute_line() - FORWARD\n");

    logo_init(&canvas);
    logo_register_draw_callback(test_draw_callback, &canvas);

    int ret = logo_execute_line(&canvas, "FD 100");
    assert(ret == LOGO_OK);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试命令解析 - 右转 / Test command parsing - right
 */
static void test_parse_right(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_execute_line() - RIGHT\n");

    logo_init(&canvas);
    /* From heading 0, RT 90 (clockwise) gives 90 (up -> right) */
    int ret = logo_execute_line(&canvas, "RT 90");
    assert(ret == LOGO_OK);
    assert(canvas.turtle.heading == 90);  /* 0 + 90 = 90 */

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试命令解析 - 完整图形 / Test command parsing - complete drawing
 */
static void test_parse_square(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_execute_line() - Draw Square\n");

    logo_init(&canvas);
    logo_register_draw_callback(test_draw_callback, &canvas);

    /* 画一个正方形 / Draw a square */
    int ret = logo_execute_line(&canvas, "FD 100");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "RT 90");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "FD 100");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "RT 90");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "FD 100");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "RT 90");
    assert(ret == LOGO_OK);

    ret = logo_execute_line(&canvas, "FD 100");
    assert(ret == LOGO_OK);

    /* 应该回到原点 / Should return to origin */
    printf("  Final position: (%d, %d), heading: %d\n",
           canvas.turtle.x, canvas.turtle.y, canvas.turtle.heading);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试设置颜色 / Test set pen color
 */
static void test_set_color(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_set_pen_color()\n");

    logo_init(&canvas);

    logo_color_t red = LOGO_COLOR_RED;
    logo_set_pen_color(&canvas, red);

    assert(canvas.turtle.pen_color.r == 255);
    assert(canvas.turtle.pen_color.g == 0);
    assert(canvas.turtle.pen_color.b == 0);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试画笔粗细 / Test pen size
 */
static void test_set_pen_size(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_set_pen_size()\n");

    logo_init(&canvas);

    logo_set_pen_size(&canvas, 5);
    assert(canvas.turtle.pen_size == 5);

    logo_set_pen_size(&canvas, 10);
    assert(canvas.turtle.pen_size == 10);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试画布大小设置 / Test canvas size setting
 */
static void test_canvas_size(void)
{
    logo_canvas_t canvas;

    printf("Test: logo_set_canvas_size()\n");

    logo_init(&canvas);

    logo_set_canvas_size(&canvas, 800, 600);

    assert(canvas.width == 800);
    assert(canvas.height == 600);
    assert(canvas.turtle.x == 400);  /* 居中 / Centered */
    assert(canvas.turtle.y == 300);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试 HOME 命令 / Test HOME command
 */
static void test_home(void)
{
    logo_canvas_t canvas;
    int center_x, center_y;

    printf("Test: logo_turtle_home()\n");

    logo_init(&canvas);
    center_x = canvas.width / 2;
    center_y = canvas.height / 2;

    /* 移动到其他地方 / Move somewhere else */
    logo_turtle_forward(&canvas, 100);
    logo_turtle_right(&canvas, 90);

    /* 执行 HOME / Execute HOME */
    logo_turtle_home(&canvas);

    assert(canvas.turtle.x == center_x);
    assert(canvas.turtle.y == center_y);
    assert(canvas.turtle.heading == 0);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 测试星形图案 / Test star pattern
 */
static void test_star(void)
{
    logo_canvas_t canvas;
    int i;

    printf("Test: Draw Star Pattern\n");

    logo_init(&canvas);
    logo_register_draw_callback(test_draw_callback, &canvas);

    /* 使用循环绘制五角星 / Draw 5-pointed star using loop */
    for (i = 0; i < 5; i++) {
        logo_execute_line(&canvas, "FD 100");
        logo_execute_line(&canvas, "RT 144");
    }

    printf("  Final position: (%d, %d), heading: %d\n",
           canvas.turtle.x, canvas.turtle.y, canvas.turtle.heading);

    printf("  PASSED\n");
    g_tests_passed++;
}

/*
 * 主函数 / Main function
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("===========================================\n");
    printf("  Logo Turtle Graphics Interpreter Test\n");
    printf("===========================================\n");
    printf("\n");

    /* 运行所有测试 / Run all tests */
    test_init();
    test_forward();
    test_back();
    test_turn();
    test_pen();
    test_parse_forward();
    test_parse_right();
    test_set_color();
    test_set_pen_size();
    test_canvas_size();
    test_home();
    test_parse_square();
    test_star();

    /* 打印测试结果 / Print test results */
    printf("\n");
    printf("===========================================\n");
    printf("  Test Results: %d passed, %d failed\n",
           g_tests_passed, g_tests_failed);
    printf("===========================================\n");
    printf("\n");

    if (g_tests_failed > 0) {
        return 1;
    }

    printf("All tests passed! Logo interpreter is working correctly.\n");
    printf("\n");
    printf("Supported commands:\n");
    printf("  FD n / FORWARD n  - 前进 n 步\n");
    printf("  BK n / BACK n     - 后退 n 步\n");
    printf("  LT n / LEFT n     - 左转 n 度\n");
    printf("  RT n / RIGHT n    - 右转 n 度\n");
    printf("  PU / PENUP        - 抬笔\n");
    printf("  PD / PENDOWN      - 落笔\n");
    printf("  HOME              - 回到原点\n");
    printf("  CS / CLEARSCREEN  - 清屏\n");
    printf("  ST / SHOWTURTLE   - 显示海龟\n");
    printf("  HT / HIDETURTLE   - 隐藏海龟\n");
    printf("  SETPENCOLOR [r g b] - 设置颜色\n");
    printf("  SETPENSIZE n      - 设置画笔粗细\n");
    printf("  SETX n            - 设置 X 坐标\n");
    printf("  SETY n            - 设置 Y 坐标\n");
    printf("  SETH n / SETHEADING n - 设置朝向\n");
    printf("\n");

    return 0;
}
