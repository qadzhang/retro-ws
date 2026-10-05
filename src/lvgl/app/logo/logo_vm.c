/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: logo_vm.c
 * 描述: Logo 小海龟解释器 - 解析器与命令执行
 *       简单嵌入式 Logo 解释器，支持基本绘图命令
 *       许可证: MIT
 *
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-31
 */

/*
 * logo_vm.c - Logo 命令解释器（自研）
 *
 * WHAT : Logo 命令解释器（自研）
 * WHY  : 解析 FD/RT/REPEAT/TO 等 Logo 命令驱动海龟
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/logo/logo_vm.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 逐行词法解析 + 命令分发表调用 logo_turtle_* API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "logo_turtle.h"

/*
 * 记号类型 / Token types
 */
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_NUMBER,      /* 数字 / Number */
    TOKEN_WORD,        /* 单词 / Word */
    TOKEN_LBRACKET,    /* 左括号 / Left bracket */
    TOKEN_RBRACKET,    /* 右括号 / Right bracket */
    TOKEN_LBRACE,     /* 左花括号 / Left brace */
    TOKEN_RBRACE,     /* 右花括号 / Right brace */
    TOKEN_STRING,      /* 字符串 / String */
    TOKEN_LIST         /* 列表 / List */
} token_type_t;

/*
 * 记号结构 / Token structure
 */
typedef struct {
    token_type_t type;
    union {
        int num;           /* 数值 / Number value */
        char word[LOGO_MAX_TOKEN_LEN];  /* 单词 / Word value */
        struct {
            logo_color_t color;
        } color_val;
    } value;
} logo_token_t;

/*
 * 解析器状态 / Parser state
 * 支持两种输入源：字符串扫描 或 记号数组重放（REPEAT 用）
 * Supports two sources: string scanning, or token-array replay (for REPEAT)
 */
typedef struct {
    const char *input;     /* 输入字符串 / Input string */
    int pos;               /* 当前位置 / Current position */
    int len;               /* 字符串长度 / String length */
    const logo_token_t *tok_arr;  /* 记号数组源 / Token array source (NULL=string) */
    int tok_idx;           /* 数组游标 / Array cursor */
    int tok_count;         /* 数组长度 / Array length */
    logo_token_t token;    /* 当前记号 / Current token */
} logo_parser_t;

/* 前向声明：两者在 logo_cmd_* 前被使用
 * Forward declarations: used before definition */
static void logo_next_token(logo_parser_t *parser);
static int logo_execute_command(logo_canvas_t *canvas, logo_parser_t *parser);

/*
 * 变量存储 / Variable storage (简单实现)
 */
#define LOGO_MAX_VARS 32
typedef struct {
    char name[LOGO_MAX_TOKEN_LEN];
    int value;
} logo_variable_t;

static logo_variable_t g_variables[LOGO_MAX_VARS];
static int g_var_count = 0;

/*
 * 前进命令 / Forward command
 */
static int logo_cmd_forward(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int dist = 0;

    /* 解析参数 / Parse argument */
    if (parser->token.type == TOKEN_NUMBER) {
        dist = parser->token.value.num;
    } else {
        /* 查找变量 / Look up variable */
        for (int i = 0; i < g_var_count; i++) {
            if (strcmp(g_variables[i].name, parser->token.value.word) == 0) {
                dist = g_variables[i].value;
                break;
            }
        }
    }

    logo_turtle_forward(canvas, dist);
    return 0;
}

/*
 * 后退命令 / Back command
 */
static int logo_cmd_back(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int dist = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        dist = parser->token.value.num;
    }

    logo_turtle_back(canvas, dist);
    return 0;
}

/*
 * 左转命令 / Left command
 */
static int logo_cmd_left(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int angle = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        angle = parser->token.value.num;
    }

    logo_turtle_left(canvas, angle);
    return 0;
}

/*
 * 右转命令 / Right command
 */
static int logo_cmd_right(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int angle = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        angle = parser->token.value.num;
    }

    logo_turtle_right(canvas, angle);
    return 0;
}

/*
 * 抬笔命令 / Pen up command
 */
static int logo_cmd_pen_up(logo_canvas_t *canvas)
{
    logo_turtle_pen_up(canvas);
    return 0;
}

/*
 * 落笔命令 / Pen down command
 */
static int logo_cmd_pen_down(logo_canvas_t *canvas)
{
    logo_turtle_pen_down(canvas);
    return 0;
}

/*
 * 显示海龟命令 / Show turtle command
 */
static int logo_cmd_show_turtle(logo_canvas_t *canvas)
{
    logo_turtle_show(canvas);
    return 0;
}

/*
 * 隐藏海龟命令 / Hide turtle command
 */
static int logo_cmd_hide_turtle(logo_canvas_t *canvas)
{
    logo_turtle_hide(canvas);
    return 0;
}

/*
 * 清屏命令 / Clear screen command
 */
static int logo_cmd_clear_screen(logo_canvas_t *canvas)
{
    logo_clear_screen(canvas);
    return 0;
}

/*
 * 回到原点命令 / Home command
 */
static int logo_cmd_home(logo_canvas_t *canvas)
{
    logo_turtle_home(canvas);
    return 0;
}

/*
 * 设置X坐标命令 / Set X command
 */
static int logo_cmd_setx(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int x = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        x = parser->token.value.num;
    }

    logo_turtle_set_pos(canvas, x, canvas->turtle.y);
    return 0;
}

/*
 * 设置Y坐标命令 / Set Y command
 */
static int logo_cmd_sety(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int y = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        y = parser->token.value.num;
    }

    logo_turtle_set_pos(canvas, canvas->turtle.x, y);
    return 0;
}

/*
 * 设置位置命令 / Set position command
 */
static int logo_cmd_setpos(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int x = canvas->turtle.x;
    int y = canvas->turtle.y;

    /* 跳过左括号 / Skip left bracket */
    if (parser->token.type == TOKEN_LBRACKET) {
        logo_next_token(parser);
    }

    /* 解析 X */
    if (parser->token.type == TOKEN_NUMBER) {
        x = parser->token.value.num;
        logo_next_token(parser);
    }

    /* 解析 Y */
    if (parser->token.type == TOKEN_NUMBER) {
        y = parser->token.value.num;
        logo_next_token(parser);
    }

    /* 跳过右括号 / Skip right bracket */
    if (parser->token.type == TOKEN_RBRACKET) {
        logo_next_token(parser);
    }

    logo_turtle_set_pos(canvas, x, y);
    return 0;
}

/*
 * 设置朝向命令 / Set heading command
 */
static int logo_cmd_setheading(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int angle = 0;

    if (parser->token.type == TOKEN_NUMBER) {
        angle = parser->token.value.num;
    }

    logo_turtle_set_heading(canvas, angle);
    return 0;
}

/*
 * 设置画笔颜色命令 / Set pen color command
 */
static int logo_cmd_setpencolor(logo_canvas_t *canvas, logo_parser_t *parser)
{
    logo_color_t color = LOGO_COLOR_WHITE;

    /* 解析颜色值 [r g b] / Parse color value [r g b] */
    if (parser->token.type == TOKEN_LBRACKET) {
        int r = 0, g = 0, b = 0;

        logo_next_token(parser);
        /* 读取 r */
        if (parser->token.type == TOKEN_NUMBER) {
            r = parser->token.value.num;
            logo_next_token(parser);
        }

        /* 读取 g */
        if (parser->token.type == TOKEN_NUMBER) {
            g = parser->token.value.num;
            logo_next_token(parser);
        }

        /* 读取 b */
        if (parser->token.type == TOKEN_NUMBER) {
            b = parser->token.value.num;
            logo_next_token(parser);
        }

        /* 跳过右括号 / Skip right bracket */
        if (parser->token.type == TOKEN_RBRACKET) {
            logo_next_token(parser);
        }

        color.r = (uint8_t)(r & 0xFF);
        color.g = (uint8_t)(g & 0xFF);
        color.b = (uint8_t)(b & 0xFF);
    }

    logo_set_pen_color(canvas, color);
    return 0;
}

/*
 * 设置画笔粗细命令 / Set pen size command
 */
static int logo_cmd_setpensize(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int size = 1;

    if (parser->token.type == TOKEN_NUMBER) {
        size = parser->token.value.num;
    }

    logo_set_pen_size(canvas, size);
    return 0;
}

/*
 * 重复命令 / Repeat command
 */
#define LOGO_MAX_BLOCK_TOKENS 256

static int logo_cmd_repeat(logo_canvas_t *canvas, logo_parser_t *parser)
{
    int count = 0;
    int i;

    /* 解析重复次数 / Parse repeat count */
    if (parser->token.type == TOKEN_NUMBER) {
        count = parser->token.value.num;
        logo_next_token(parser);
    }

    /* 跳过左花括号 / Skip left brace */
    if (parser->token.type == TOKEN_LBRACE) {
        logo_next_token(parser);
    }

    /* 收集花括号内的记号 / Collect tokens inside braces */
    logo_token_t block[LOGO_MAX_BLOCK_TOKENS];
    int block_len = 0;
    int depth = 1;

    while (parser->token.type != TOKEN_EOF && depth > 0 &&
           block_len < LOGO_MAX_BLOCK_TOKENS - 1) {
        if (parser->token.type == TOKEN_LBRACE) {
            depth++;
        } else if (parser->token.type == TOKEN_RBRACE) {
            depth--;
            if (depth == 0) break;
        }
        block[block_len++] = parser->token;
        logo_next_token(parser);
    }

    /* 跳过右花括号 / Skip right brace */
    if (parser->token.type == TOKEN_RBRACE) {
        logo_next_token(parser);
    }

    /* 重复执行 / Execute repeatedly */
    if (count > 1000) count = 1000;
    if (count < 0) count = 0;

    for (i = 0; i < count; i++) {
        /* 构造子解析器重放记号块：游标独立于外层 parser，
         * 修复旧实现反复改写外层 parser->token 导致的错乱
         * Build a sub-parser over the collected tokens with its own
         * cursor; fixes the old code that mutated the outer parser */
        logo_parser_t sub;

        memset(&sub, 0, sizeof(sub));
        sub.tok_arr = block;
        sub.tok_idx = 0;
        sub.tok_count = block_len;

        logo_next_token(&sub);

        while (sub.token.type != TOKEN_EOF) {
            if (logo_execute_command(canvas, &sub) != LOGO_OK)
                break;
            logo_next_token(&sub);
        }
    }

    return 0;
}

/*
 * 设置背景色命令 / Set background color command
 */
static int logo_cmd_setbgcolor(logo_canvas_t *canvas, logo_parser_t *parser)
{
    logo_color_t color = LOGO_COLOR_BLACK;

    /* 解析颜色值 [r g b] / Parse color value [r g b] */
    if (parser->token.type == TOKEN_LBRACKET) {
        int r = 0, g = 0, b = 0;

        logo_next_token(parser);
        if (parser->token.type == TOKEN_NUMBER) {
            r = parser->token.value.num;
            logo_next_token(parser);
        }
        if (parser->token.type == TOKEN_NUMBER) {
            g = parser->token.value.num;
            logo_next_token(parser);
        }
        if (parser->token.type == TOKEN_NUMBER) {
            b = parser->token.value.num;
            logo_next_token(parser);
        }

        /* 跳过右括号 / Skip right bracket */
        if (parser->token.type == TOKEN_RBRACKET) {
            logo_next_token(parser);
        }

        color.r = (uint8_t)(r & 0xFF);
        color.g = (uint8_t)(g & 0xFF);
        color.b = (uint8_t)(b & 0xFF);
    }

    logo_set_bg_color(canvas, color);
    return 0;
}

/*
 * 获取下一个记号 / Get next token
 * 记号数组源（REPEAT 重放）优先于字符串扫描
 * Token-array source (REPEAT replay) takes precedence over string scan
 */
static void logo_next_token(logo_parser_t *parser)
{
    memset(&parser->token, 0, sizeof(parser->token));

    /* 数组重放模式 / Array replay mode */
    if (parser->tok_arr != NULL) {
        if (parser->tok_idx >= parser->tok_count) {
            parser->token.type = TOKEN_EOF;
            return;
        }
        parser->token = parser->tok_arr[parser->tok_idx++];
        return;
    }

    /* 跳过空白 / Skip whitespace */
    while (parser->pos < parser->len && isspace(parser->input[parser->pos])) {
        parser->pos++;
    }

    if (parser->pos >= parser->len) {
        parser->token.type = TOKEN_EOF;
        return;
    }

    char c = parser->input[parser->pos];

    /* 数字 / Number */
    if (isdigit(c) || (c == '-' && isdigit(parser->input[parser->pos + 1]))) {
        char *end;
        parser->token.value.num = strtol(&parser->input[parser->pos], &end, 10);
        parser->token.type = TOKEN_NUMBER;
        parser->pos = end - parser->input;
        return;
    }

    /* 单词 / Word */
    if (isalpha(c) || c == '_') {
        int i = 0;
        while (i < LOGO_MAX_TOKEN_LEN - 1 && parser->pos < parser->len &&
               (isalnum(parser->input[parser->pos]) || parser->input[parser->pos] == '_')) {
            parser->token.value.word[i++] = parser->input[parser->pos++];
        }
        parser->token.value.word[i] = '\0';
        parser->token.type = TOKEN_WORD;

        /* 转换为大写（Logo 命令不区分大小写）*/
        /* Convert to uppercase (Logo commands are case-insensitive) */
        for (i = 0; parser->token.value.word[i]; i++) {
            parser->token.value.word[i] = toupper(parser->token.value.word[i]);
        }
        return;
    }

    /* 括号 / Brackets */
    switch (c) {
        case '[':
            parser->token.type = TOKEN_LBRACKET;
            parser->pos++;
            break;
        case ']':
            parser->token.type = TOKEN_RBRACKET;
            parser->pos++;
            break;
        case '{':
            parser->token.type = TOKEN_LBRACE;
            parser->pos++;
            break;
        case '}':
            parser->token.type = TOKEN_RBRACE;
            parser->pos++;
            break;
        default:
            /* 未知字符，跳过 / Unknown char, skip */
            parser->pos++;
            parser->token.type = TOKEN_EOF;
            break;
    }
}

/*
 * 执行命令 / Execute command
 */
static int logo_execute_command(logo_canvas_t *canvas, logo_parser_t *parser)
{
    char cmd[LOGO_MAX_TOKEN_LEN];
    int ret = 0;

    if (parser->token.type != TOKEN_WORD) {
        return 0;
    }

    strncpy(cmd, parser->token.value.word, LOGO_MAX_TOKEN_LEN - 1);
    cmd[LOGO_MAX_TOKEN_LEN - 1] = '\0';

    /* 获取下一个记号作为参数 */
    /* Get next token as argument */
    logo_next_token(parser);

    /* 比较命令并执行 / Compare and execute command */
    if (strcmp(cmd, "FD") == 0 || strcmp(cmd, "FORWARD") == 0) {
        ret = logo_cmd_forward(canvas, parser);
    }
    else if (strcmp(cmd, "BK") == 0 || strcmp(cmd, "BACK") == 0) {
        ret = logo_cmd_back(canvas, parser);
    }
    else if (strcmp(cmd, "LT") == 0 || strcmp(cmd, "LEFT") == 0) {
        ret = logo_cmd_left(canvas, parser);
    }
    else if (strcmp(cmd, "RT") == 0 || strcmp(cmd, "RIGHT") == 0) {
        ret = logo_cmd_right(canvas, parser);
    }
    else if (strcmp(cmd, "PU") == 0 || strcmp(cmd, "PENUP") == 0) {
        ret = logo_cmd_pen_up(canvas);
    }
    else if (strcmp(cmd, "PD") == 0 || strcmp(cmd, "PENDOWN") == 0) {
        ret = logo_cmd_pen_down(canvas);
    }
    else if (strcmp(cmd, "ST") == 0 || strcmp(cmd, "SHOWTURTLE") == 0) {
        ret = logo_cmd_show_turtle(canvas);
    }
    else if (strcmp(cmd, "HT") == 0 || strcmp(cmd, "HIDETURTLE") == 0) {
        ret = logo_cmd_hide_turtle(canvas);
    }
    else if (strcmp(cmd, "CS") == 0 || strcmp(cmd, "CLEARSCREEN") == 0) {
        ret = logo_cmd_clear_screen(canvas);
    }
    else if (strcmp(cmd, "HOME") == 0) {
        ret = logo_cmd_home(canvas);
    }
    else if (strcmp(cmd, "SETX") == 0) {
        ret = logo_cmd_setx(canvas, parser);
    }
    else if (strcmp(cmd, "SETY") == 0) {
        ret = logo_cmd_sety(canvas, parser);
    }
    else if (strcmp(cmd, "SETPOS") == 0) {
        ret = logo_cmd_setpos(canvas, parser);
    }
    else if (strcmp(cmd, "SETH") == 0 || strcmp(cmd, "SETHEADING") == 0) {
        ret = logo_cmd_setheading(canvas, parser);
    }
    else if (strcmp(cmd, "SETPENCOLOR") == 0 || strcmp(cmd, "SETPC") == 0) {
        ret = logo_cmd_setpencolor(canvas, parser);
    }
    else if (strcmp(cmd, "SETPENSIZE") == 0 || strcmp(cmd, "SETPS") == 0) {
        ret = logo_cmd_setpensize(canvas, parser);
    }
    else if (strcmp(cmd, "SETBGCOLOR") == 0 || strcmp(cmd, "SETBG") == 0) {
        ret = logo_cmd_setbgcolor(canvas, parser);
    }
    else if (strcmp(cmd, "REPEAT") == 0) {
        ret = logo_cmd_repeat(canvas, parser);
    }
    else {
        /* 未知命令 / Unknown command */
        ret = LOGO_ERR_UNKNOWN_CMD;
    }

    return ret;
}

/*
 * 解析并执行一行代码 / Parse and execute one line
 */
int logo_execute_line(logo_canvas_t *canvas, const char *line)
{
    logo_parser_t parser;
    int result = LOGO_OK;

    if (canvas == NULL || line == NULL)
        return -1;

    /* 初始化解析器（清零确保 tok_arr 为 NULL，走字符串扫描）
     * Initialize parser (zeroed so tok_arr==NULL, string scan mode) */
    memset(&parser, 0, sizeof(parser));
    parser.input = line;
    parser.pos = 0;
    parser.len = (int)strlen(line);

    /* 逐个处理记号 / Process tokens one by one */
    logo_next_token(&parser);

    while (parser.token.type != TOKEN_EOF && result == LOGO_OK) {
        result = logo_execute_command(canvas, &parser);
        if (result == LOGO_OK) {
            logo_next_token(&parser);
        }
    }

    return result;
}
