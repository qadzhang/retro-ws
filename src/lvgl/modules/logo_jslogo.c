/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * logo_jslogo.c - jslogo (UCBLogo 子集) Duktape 加载器
 *                  jslogo loader on Duktape
 *
 * WHAT : 在 Duktape JS 引擎上运行 jslogo（Apache-2.0，UCBLogo 子集），
 *        海龟绘图经现有 logo_draw.c LVGL 后端显示。
 * WHY  : 获得"真 Logo"兼容度（TO/REPEAT/递归），复用已验证的 LVGL 绘图层。
 * WHO  : 由 script_engines.c 在 Duktape 初始化时调用（CONFIG_RETRO_LOGO_JSLOGO）。
 * WHERE: src/lvgl/modules/logo_jslogo.c
 * WHEN : duk_init() 时 bootstrap；script run *.lgo 时执行。
 * HOW  :
 *   1. C 侧注册 JS 全局对象 retro_turtle（海龟原语 -> logo_canvas_t API）
 *   2. 求值 boot JS（本文件内置），生成 RetroCanvas（2d 上下文子集风格）
 *   3. 依次求值 /sdcard/scripts/logo/lib/*.js（jslogo 源码，download_deps.sh
 *      下载到 deps/jslogo，由用户拷入 SD 卡）
 *   4. 求值 .lgo Logo 源文件
 *
 * 集成注意 / Integration note:
 *   jslogo 原生前端从浏览器获取 <canvas>；集成时在 jslogo 源码中把
 *   canvas 获取处改为全局 RetroCanvas（预计一处改动，见 NEXT_STEPS.md
 *   验证任务）。jslogo 本体未随仓库分发（Apache-2.0，deps/jslogo）。
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#if __has_include(<duktape.h>)
#  include <duktape.h>
#else
#  include "duktape/duktape.h"
#endif

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

#include "logo/logo_turtle.h"

/* jslogo 库目录 / jslogo bundle directory on SD card */
#define JSLOGO_LIB_DIR   "/sdcard/scripts/logo/lib"

/*======================================
 * 海龟后端 / Turtle backend
 *======================================*/

/* logo_draw.c 提供的 LVGL 绘图上下文 / LVGL draw context from logo_draw.c */
extern void *logo_draw_create(int width, int height);
extern void logo_draw_destroy(void *ctx);
extern logo_canvas_t *logo_draw_get_canvas(void *ctx);
extern void logo_draw_clear(void *ctx);

static void *g_jslogo_draw_ctx = NULL;
static logo_canvas_t *g_jslogo_canvas = NULL;

/* 惰性初始化绘图后端 / lazily init draw backend */
static int jslogo_backend_init(void)
{
    if (g_jslogo_canvas)
        return OK;

    g_jslogo_draw_ctx = logo_draw_create(640, 480);
    if (!g_jslogo_draw_ctx)
        return -ENOMEM;

    g_jslogo_canvas = logo_draw_get_canvas(g_jslogo_draw_ctx);
    if (!g_jslogo_canvas) {
        logo_draw_destroy(g_jslogo_draw_ctx);
        g_jslogo_draw_ctx = NULL;
        return -ENOMEM;
    }

    return OK;
}

/*======================================
 * Duktape C 函数：retro_turtle 原语
 *======================================*/

#define TURTLE_FN(name, body)                            \
    static duk_ret_t name(duk_context *ctx)              \
    {                                                    \
        if (jslogo_backend_init() < OK) {                \
            duk_push_number(ctx, -1);                    \
            return 1;                                    \
        }                                                \
        body                                             \
        duk_push_number(ctx, 0);                         \
        return 1;                                        \
    }

TURTLE_FN(turtle_forward, {
    logo_turtle_forward(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_back, {
    logo_turtle_back(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_left, {
    logo_turtle_left(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_right, {
    logo_turtle_right(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_pen_up, {
    logo_turtle_pen_up(g_jslogo_canvas);
})
TURTLE_FN(turtle_pen_down, {
    logo_turtle_pen_down(g_jslogo_canvas);
})
TURTLE_FN(turtle_home, {
    logo_turtle_home(g_jslogo_canvas);
})
TURTLE_FN(turtle_set_xy, {
    logo_turtle_set_pos(g_jslogo_canvas,
                        (int)duk_get_int(ctx, 0),
                        (int)duk_get_int(ctx, 1));
})
TURTLE_FN(turtle_set_heading, {
    logo_turtle_set_heading(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_set_color, {
    /* 读取 3 个参数 r,g,b 构造 logo_color_t
     * Read 3 args r,g,b and build logo_color_t struct
     * (旧代码 int→struct 强制转换不合法 / old int->struct cast was invalid) */
    logo_color_t color;
    color.r = (uint8_t)(duk_get_int(ctx, 0) & 0xFF);
    color.g = (uint8_t)(duk_get_int(ctx, 1) & 0xFF);
    color.b = (uint8_t)(duk_get_int(ctx, 2) & 0xFF);
    logo_set_pen_color(g_jslogo_canvas, color);
})
TURTLE_FN(turtle_set_width, {
    logo_set_pen_size(g_jslogo_canvas, (int)duk_get_int(ctx, 0));
})
TURTLE_FN(turtle_show, {
    logo_turtle_show(g_jslogo_canvas);
})
TURTLE_FN(turtle_hide, {
    logo_turtle_hide(g_jslogo_canvas);
})

TURTLE_FN(turtle_clear, {
    logo_clear_screen(g_jslogo_canvas);
})

/*======================================
 * boot JS：把 retro_turtle 包装成 2d 上下文子集风格 RetroCanvas
 *======================================*/

static const char *jslogo_boot_js =
    "var RetroCanvas = {"
    "  _pen: 0, _x: 0, _y: 0, _sx: 0, _sy: 0,"
    "  beginPath: function () { this._pen = 0; },"
    "  moveTo: function (x, y) { this._x = x; this._y = y; },"
    "  lineTo: function (x, y) {"
    "    retro_turtle.pen_up();"
    "    retro_turtle.set_xy(this._x, this._y);"
    "    retro_turtle.pen_down();"
    "    retro_turtle.set_xy(x, y);"
    "    this._x = x; this._y = y;"
    "  },"
    "  stroke: function () {},"
    "  clearRect: function () { retro_turtle.clear(); },"
    "  fillRect: function (x, y, w, h) {"
    "    retro_turtle.pen_up();"
    "    retro_turtle.set_xy(x, y);"
    "    retro_turtle.pen_down();"
    "    retro_turtle.set_xy(x + w, y);"
    "    retro_turtle.set_xy(x + w, y + h);"
    "    retro_turtle.set_xy(x, y + h);"
    "    retro_turtle.set_xy(x, y);"
    "  },"
    "  measureText: function (s) {"
    "    return { width: 8 * (s ? s.length : 0) };"
    "  },"
    "  fillText: function () {}"
    "};";

/*======================================
 * 对外接口 / Public interface
 *======================================*/

/**
 * logo_jslogo_bootstrap - 注册海龟后端与 RetroCanvas
 */
int logo_jslogo_bootstrap(duk_context *ctx)
{
    if (!ctx)
        return -EINVAL;

    /* retro_turtle 原语对象 */
    duk_push_object(ctx);

    duk_push_c_function(ctx, turtle_forward, 1);
    duk_put_prop_string(ctx, -2, "forward");
    duk_push_c_function(ctx, turtle_back, 1);
    duk_put_prop_string(ctx, -2, "back");
    duk_push_c_function(ctx, turtle_left, 1);
    duk_put_prop_string(ctx, -2, "left");
    duk_push_c_function(ctx, turtle_right, 1);
    duk_put_prop_string(ctx, -2, "right");
    duk_push_c_function(ctx, turtle_pen_up, 0);
    duk_put_prop_string(ctx, -2, "pen_up");
    duk_push_c_function(ctx, turtle_pen_down, 0);
    duk_put_prop_string(ctx, -2, "pen_down");
    duk_push_c_function(ctx, turtle_home, 0);
    duk_put_prop_string(ctx, -2, "home");
    duk_push_c_function(ctx, turtle_set_xy, 2);
    duk_put_prop_string(ctx, -2, "set_xy");
    duk_push_c_function(ctx, turtle_set_heading, 1);
    duk_put_prop_string(ctx, -2, "set_heading");
    duk_push_c_function(ctx, turtle_set_color, 3);
    duk_put_prop_string(ctx, -2, "set_color");
    duk_push_c_function(ctx, turtle_set_width, 1);
    duk_put_prop_string(ctx, -2, "set_width");
    duk_push_c_function(ctx, turtle_show, 0);
    duk_put_prop_string(ctx, -2, "show");
    duk_push_c_function(ctx, turtle_hide, 0);
    duk_put_prop_string(ctx, -2, "hide");
    duk_push_c_function(ctx, turtle_clear, 0);
    duk_put_prop_string(ctx, -2, "clear");

    duk_put_global_string(ctx, "retro_turtle");

    /* RetroCanvas（2d 上下文子集） */
    duk_int_t ret = duk_peval_string(ctx, jslogo_boot_js);
    if (ret != 0) {
        syslog(LOG_ERR, "[jslogo] boot script failed: %s\n",
               duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return -EIO;
    }
    duk_pop(ctx);

    syslog(LOG_INFO, "[jslogo] retro_turtle + RetroCanvas ready\n");
    return OK;
}

/**
 * 读取并求值一个 JS/Logo 文件 / read and evaluate one file
 */
static int jslogo_eval_file(duk_context *ctx, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -ENOENT;

    struct stat st;
    fstat(fd, &st);
    char *buf = malloc(st.st_size + 1);
    if (!buf) {
        close(fd);
        return -ENOMEM;
    }

    read(fd, buf, st.st_size);
    buf[st.st_size] = '\0';
    close(fd);

    duk_int_t ret = duk_peval_string(ctx, buf);
    free(buf);

    if (ret != 0) {
        fprintf(stderr, "[JSLOGO ERROR] %s: %s\n", path,
                duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return -EIO;
    }

    duk_pop(ctx);
    return OK;
}

/**
 * logo_jslogo_run - 执行 .lgo Logo 程序
 * logo_jslogo_run - run a .lgo Logo program
 *
 * 顺序：jslogo 库（SD 卡 lib 目录） -> Logo 源文件
 */
int logo_jslogo_run(duk_context *ctx, const char *path)
{
    int ret;

    if (!ctx || !path)
        return -EINVAL;

    syslog(LOG_INFO, "[jslogo] running %s\n", path);

    /* 1. 加载 jslogo 库（每次运行前重载，确保解释器状态干净） */
    DIR *dir = opendir(JSLOGO_LIB_DIR);
    if (dir) {
        struct dirent *ent;
        char fullpath[256];

        while ((ent = readdir(dir)) != NULL) {
            const char *ext = strrchr(ent->d_name, '.');
            if (!ext || strcmp(ext, ".js") != 0)
                continue;

            snprintf(fullpath, sizeof(fullpath), "%s/%s",
                     JSLOGO_LIB_DIR, ent->d_name);

            ret = jslogo_eval_file(ctx, fullpath);
            if (ret < 0) {
                syslog(LOG_ERR, "[jslogo] lib load failed: %s\n", fullpath);
                closedir(dir);
                return ret;
            }
        }
        closedir(dir);
    } else {
        syslog(LOG_ERR, "[jslogo] lib dir missing: %s (copy deps/jslogo "
               "bundle here, see DEPENDENCIES.md)\n", JSLOGO_LIB_DIR);
        return -ENOENT;
    }

    /* 2. 执行 Logo 源文件 */
    ret = jslogo_eval_file(ctx, path);
    if (ret < 0)
        return ret;

    return OK;
}
