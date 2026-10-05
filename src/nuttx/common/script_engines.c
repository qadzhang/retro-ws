/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * script_engines.c - 脚本引擎集成层
 *
 * WHAT : 脚本引擎集成层
 * WHY  : 五种引擎（bas/js/be/py/lgo）按需加载与统一调度
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/script_engines.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : RETRO_SCRIPT_* 可选编译 + 扩展名路由 + retro_ui 绑定注入
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/boardctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

/*==========================
 *  脚本引擎类型
 *==========================*/

typedef enum {
    SCRIPT_ENGINE_MYBASIC = 0,
    SCRIPT_ENGINE_DUKTAPE,
    SCRIPT_ENGINE_BERRY,
    SCRIPT_ENGINE_PYTHON,
    SCRIPT_ENGINE_JSLOGO,
    SCRIPT_ENGINE_COUNT
} script_engine_type_t;

struct script_engine {
    const char *name;
    const char *extension;
    bool       loaded;
    bool       available;  /* 编译是否包含 / compiled in */
    size_t     mem_size;   /* 占用内存 */
};

/* 各引擎编译可用性由 CONFIG 决定 / availability decided by CONFIG */
#ifdef CONFIG_RETRO_SCRIPT_TINYBASIC
#  define AVAIL_MYBASIC   true
#else
#  define AVAIL_MYBASIC   false
#endif
#ifdef CONFIG_RETRO_SCRIPT_DUKTAPE
#  define AVAIL_DUKTAPE   true
#else
#  define AVAIL_DUKTAPE   false
#endif
#ifdef CONFIG_RETRO_SCRIPT_BERRY
#  define AVAIL_BERRY     true
#else
#  define AVAIL_BERRY     false
#endif
#ifdef CONFIG_RETRO_SCRIPT_PYTHON
#  define AVAIL_PYTHON    true
#else
#  define AVAIL_PYTHON    false
#endif
#if defined(CONFIG_RETRO_LOGO_JSLOGO) && defined(CONFIG_RETRO_SCRIPT_DUKTAPE)
#  define AVAIL_JSLOGO    true
#else
#  define AVAIL_JSLOGO    false
#endif

static struct script_engine g_engines[SCRIPT_ENGINE_COUNT] = {
#ifndef CONFIG_RETRO_SCRIPT_STACK_TINYBASIC
#  define CONFIG_RETRO_SCRIPT_STACK_TINYBASIC 65536
#endif
#ifndef CONFIG_RETRO_SCRIPT_STACK_DUKTAPE
#  define CONFIG_RETRO_SCRIPT_STACK_DUKTAPE 262144
#endif
#ifndef CONFIG_RETRO_SCRIPT_STACK_BERRY
#  define CONFIG_RETRO_SCRIPT_STACK_BERRY 65536
#endif
#ifndef CONFIG_RETRO_SCRIPT_STACK_JSLOGO
#  define CONFIG_RETRO_SCRIPT_STACK_JSLOGO 262144
#endif

    { "my-basic", "bas", false, AVAIL_MYBASIC, CONFIG_RETRO_SCRIPT_STACK_TINYBASIC },
    { "Duktape",  "js",  false, AVAIL_DUKTAPE, CONFIG_RETRO_SCRIPT_STACK_DUKTAPE },
    { "Berry",    "be",  false, AVAIL_BERRY,   CONFIG_RETRO_SCRIPT_STACK_BERRY },
    { "CPython",  "py",  false, AVAIL_PYTHON,  1024 * 1024 },
    { "jslogo",   "lgo", false, AVAIL_JSLOGO,  CONFIG_RETRO_SCRIPT_STACK_JSLOGO },
};

static script_engine_type_t g_active_engine = SCRIPT_ENGINE_MYBASIC;

/* 前置声明：script_set_engine() 在定义之前调用它
 * forward declaration: called before its definition */
void script_unload_current(void);

/*==========================
 *  my-basic
 *==========================*/

#ifdef CONFIG_RETRO_SCRIPT_TINYBASIC

/* 头文件定位双保险：优先 -I 路径，其次仓库相对路径（从仓库根构建时生效）
 * prefer -I provided path, fall back to repo-relative deps path */
#if __has_include(<my_basic.h>)
#  include <my_basic.h>
#elif __has_include("deps/my_basic/core/my_basic.h")
#  include "deps/my_basic/core/my_basic.h"
#else
#  error "my_basic.h not found (expected via -I or deps/my_basic/core/)"
#endif

static struct mb_interpreter_t *g_mb_ctx = NULL;

static int mybasic_print(struct mb_interpreter_t *s, const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s", buf);
    (void)s;
    return 0;
}

static int mybasic_init(void)
{
    int ret;

    if (g_engines[SCRIPT_ENGINE_MYBASIC].loaded)
        return OK;

    syslog(LOG_INFO, "[SCRIPT] Initializing my-basic...\n");

    ret = mb_init();
    if (ret != MB_FUNC_OK) {
        syslog(LOG_ERR, "[SCRIPT] my-basic: mb_init failed: %d\n", ret);
        return -EIO;
    }

    ret = mb_open(&g_mb_ctx);
    if (ret != MB_FUNC_OK || !g_mb_ctx) {
        syslog(LOG_ERR, "[SCRIPT] my-basic: mb_open failed: %d\n", ret);
        mb_dispose();
        return -EIO;
    }

    mb_set_printer(g_mb_ctx, mybasic_print);

    /* 注册绑定：retro_ui（LVGL 目标）+ retro_gpio（教学 GPIO） */
#ifdef CONFIG_LVGL
    {
        extern void retro_ui_bas_register(struct mb_interpreter_t *s);
        retro_ui_bas_register(g_mb_ctx);
    }
#endif
#ifdef CONFIG_RETRO_GPIO_SCRIPT
    {
        extern void retro_gpio_bas_register(struct mb_interpreter_t *s);
        retro_gpio_bas_register(g_mb_ctx);
    }
#endif
#ifdef CONFIG_RETRO_BUS
    {
        extern void retro_bus_bas_register(struct mb_interpreter_t *s);
        retro_bus_bas_register(g_mb_ctx);
    }
#endif

    g_engines[SCRIPT_ENGINE_MYBASIC].loaded = true;

    syslog(LOG_INFO, "[SCRIPT] my-basic initialized\n");
    return OK;
}

static int mybasic_exec_file(const char *path)
{
    int ret;

    if (!g_engines[SCRIPT_ENGINE_MYBASIC].loaded) {
        ret = mybasic_init();
        if (ret < 0)
            return ret;  /* 初始化失败不得继续用 g_mb_ctx / don't use ctx */
    }

    if (!g_mb_ctx)
        return -ENOSYS;

    syslog(LOG_INFO, "[SCRIPT] my-basic: executing %s\n", path);

    ret = mb_load_file(g_mb_ctx, path);
    if (ret != MB_FUNC_OK) {
        syslog(LOG_ERR, "[SCRIPT] my-basic: load failed: %d\n", ret);
        return -EIO;
    }

    ret = mb_run(g_mb_ctx, true);
    if (ret != MB_FUNC_OK) {
        syslog(LOG_ERR, "[SCRIPT] my-basic: run failed: %d\n", ret);
        return -EIO;
    }

    return OK;
}

static int mybasic_exec_line(const char *line)
{
    int ret;

    if (!g_engines[SCRIPT_ENGINE_MYBASIC].loaded) {
        ret = mybasic_init();
        if (ret < 0)
            return ret;
    }

    if (!g_mb_ctx)
        return -ENOSYS;

    syslog(LOG_INFO, "[SCRIPT] my-basic REPL: %s\n", line);

    ret = mb_load_string(g_mb_ctx, line, true);
    if (ret != MB_FUNC_OK)
        return -EIO;

    ret = mb_run(g_mb_ctx, true);
    if (ret != MB_FUNC_OK)
        return -EIO;

    return OK;
}

static void mybasic_deinit(void)
{
    if (!g_engines[SCRIPT_ENGINE_MYBASIC].loaded)
        return;

    if (g_mb_ctx) {
        mb_close(&g_mb_ctx);
        g_mb_ctx = NULL;
    }

    mb_dispose();

    g_engines[SCRIPT_ENGINE_MYBASIC].loaded = false;
    syslog(LOG_INFO, "[SCRIPT] my-basic deinitialized\n");
}

#else
static int mybasic_init(void) { return -ENOSYS; }
static int mybasic_exec_file(const char *p) { (void)p; return -ENOSYS; }
static int mybasic_exec_line(const char *l) { (void)l; return -ENOSYS; }
static void mybasic_deinit(void) {}
#endif /* CONFIG_RETRO_SCRIPT_TINYBASIC */

/*==========================
 *  Duktape JS
 *==========================*/

#ifdef CONFIG_RETRO_SCRIPT_DUKTAPE

/* 同 my_basic：双路径定位 duktape.h / dual-path include for duktape.h */
#if __has_include(<duktape.h>)
#  include <duktape.h>
#elif __has_include("duktape/duktape.h")
#  include "duktape/duktape.h"
#else
#  error "duktape.h not found (expected via -I or deps/duktape)"
#endif

/* retro_ui JS 绑定（retro_ui_js.c）/ retro_ui JS bindings */
extern void retro_ui_js_init(duk_context *ctx);
extern void retro_ui_js_deinit(duk_context *ctx);

/* jslogo 加载器（logo_jslogo.c，依赖本引擎）/ jslogo loader on this engine */
#ifdef CONFIG_RETRO_LOGO_JSLOGO
extern int logo_jslogo_bootstrap(duk_context *ctx);
extern int logo_jslogo_run(duk_context *ctx, const char *path);
#endif

static duk_context *g_duktape_ctx = NULL;

/**
 * Duktape 打印函数
 */
static duk_ret_t duk_print(duk_context *ctx)
{
    printf("%s\n", duk_to_string(ctx, 0));
    return 0;
}

/**
 * Duktape 初始化
 */
static int duk_init(void)
{
    if (g_engines[SCRIPT_ENGINE_DUKTAPE].loaded)
        return OK;

    syslog(LOG_INFO, "[SCRIPT] Initializing Duktape...\n");

    g_duktape_ctx = duk_create_heap_default();
    if (!g_duktape_ctx) {
        syslog(LOG_ERR, "[SCRIPT] Duktape: heap creation failed\n");
        return -ENOMEM;
    }

    /* 注册 print 函数 */
    duk_push_c_function(g_duktape_ctx, duk_print, 1);
    duk_put_global_string(g_duktape_ctx, "print");

    /* 注册 retro_ui 模块（LVGL 目标） */
#ifdef CONFIG_LVGL
    retro_ui_js_init(g_duktape_ctx);
#endif

#ifdef CONFIG_RETRO_GPIO_SCRIPT
    /* 注册 retro_gpio 教学接口（retro_gpio_js.c） */
    {
        extern void retro_gpio_js_init(duk_context *ctx);
        retro_gpio_js_init(g_duktape_ctx);
    }
#endif
#ifdef CONFIG_RETRO_BUS
    /* 注册总线兼容层（retro_bus_js.c：retro_bus 对象） */
    {
        extern void retro_bus_js_init(duk_context *ctx);
        retro_bus_js_init(g_duktape_ctx);
    }
#endif

#ifdef CONFIG_RETRO_LOGO_JSLOGO
    /* 注册 jslogo 海龟画图后端（LVGL canvas shim） */
    logo_jslogo_bootstrap(g_duktape_ctx);
#endif

    g_engines[SCRIPT_ENGINE_DUKTAPE].loaded = true;

    syslog(LOG_INFO, "[SCRIPT] Duktape initialized\n");
    return OK;
}

/**
 * Duktape 执行脚本文件
 */
static int duk_exec_file(const char *path)
{
    if (!g_engines[SCRIPT_ENGINE_DUKTAPE].loaded) {
        int ret = duk_init();
        if (ret < 0)
            return ret;  /* 初始化失败不得继续用 g_duktape_ctx */
    }

    if (!g_duktape_ctx)
        return -ENOSYS;

    syslog(LOG_INFO, "[SCRIPT] Duktape: executing %s\n", path);

    /* 读取文件 / read file */
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -ENOENT;

    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 0) {
        close(fd);
        return -EIO;
    }

    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) {
        close(fd);
        return -ENOMEM;
    }

    ssize_t nread = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (nread < 0) {
        free(buf);
        return -EIO;
    }
    buf[nread] = '\0';

    /* 执行 */
    duk_int_t ret = duk_peval_string(g_duktape_ctx, buf);
    if (ret != 0) {
        fprintf(stderr, "[JS ERROR] %s\n", duk_safe_to_string(g_duktape_ctx, -1));
        free(buf);
        return -EIO;
    }

    /* 弹出返回值 */
    duk_pop(g_duktape_ctx);
    free(buf);
    return OK;
}

/**
 * Duktape 执行一行
 */
static int duk_exec_line(const char *line)
{
    if (!g_engines[SCRIPT_ENGINE_DUKTAPE].loaded) {
        int ret = duk_init();
        if (ret < 0)
            return ret;
    }

    if (!g_duktape_ctx)
        return -ENOSYS;

    duk_int_t ret = duk_peval_string(g_duktape_ctx, line);
    if (ret != 0) {
        fprintf(stderr, "[JS ERROR] %s\n", duk_safe_to_string(g_duktape_ctx, -1));
        return -EIO;
    }

    duk_pop(g_duktape_ctx);
    return OK;
}

/**
 * Duktape 释放
 */
static void duk_deinit(void)
{
    if (!g_engines[SCRIPT_ENGINE_DUKTAPE].loaded)
        return;

    if (g_duktape_ctx) {
        retro_ui_js_deinit(g_duktape_ctx);
        duk_destroy_heap(g_duktape_ctx);
        g_duktape_ctx = NULL;
    }

    g_engines[SCRIPT_ENGINE_DUKTAPE].loaded = false;
    syslog(LOG_INFO, "[SCRIPT] Duktape deinitialized\n");
}

#else
static int duk_init(void) { return -ENOSYS; }
static int duk_exec_file(const char *p) { (void)p; return -ENOSYS; }
static int duk_exec_line(const char *l) { (void)l; return -ENOSYS; }
static void duk_deinit(void) {}
#endif /* CONFIG_RETRO_SCRIPT_DUKTAPE */

/*==========================
 *  Berry（类 Python 轻量语言）
 *==========================*/

#ifdef CONFIG_RETRO_SCRIPT_BERRY

#include "berry.h"

/* retro_ui Berry 绑定（retro_ui_berry.c）/ retro_ui Berry bindings */
extern void retro_ui_berry_init(bvm *vm);
extern void retro_ui_berry_deinit(bvm *vm);

static bvm *g_berry_vm = NULL;

static int berry_init(void)
{
    if (g_engines[SCRIPT_ENGINE_BERRY].loaded)
        return OK;

    syslog(LOG_INFO, "[SCRIPT] Initializing Berry...\n");

    g_berry_vm = be_vm_new();
    if (!g_berry_vm) {
        syslog(LOG_ERR, "[SCRIPT] Berry: VM creation failed\n");
        return -ENOMEM;
    }

    /* 注册 retro_ui_* 全局函数（LVGL 目标） */
#ifdef CONFIG_LVGL
    retro_ui_berry_init(g_berry_vm);
#endif
#ifdef CONFIG_RETRO_GPIO_SCRIPT
    {
        extern void retro_gpio_berry_init(bvm *vm);
        retro_gpio_berry_init(g_berry_vm);
    }
#endif
#ifdef CONFIG_RETRO_BUS
    {
        extern void retro_bus_berry_init(bvm *vm);
        retro_bus_berry_init(g_berry_vm);
    }
#endif

    g_engines[SCRIPT_ENGINE_BERRY].loaded = true;

    syslog(LOG_INFO, "[SCRIPT] Berry initialized\n");
    return OK;
}

static int berry_exec_file(const char *path)
{
    if (!g_engines[SCRIPT_ENGINE_BERRY].loaded) {
        int ret = berry_init();
        if (ret < 0)
            return ret;  /* 初始化失败不得继续用 g_berry_vm */
    }

    if (!g_berry_vm)
        return -ENOSYS;

    syslog(LOG_INFO, "[SCRIPT] Berry: executing %s\n", path);

    if (be_loadfile(g_berry_vm, path) != 0) {
        syslog(LOG_ERR, "[SCRIPT] Berry: load failed: %s\n", path);
        be_pop(g_berry_vm, 1);
        return -EIO;
    }

    if (be_pcall(g_berry_vm, 0) != 0) {
        const char *err = be_tostring(g_berry_vm, -1);
        fprintf(stderr, "[BERRY ERROR] %s\n", err ? err : "unknown");
        be_pop(g_berry_vm, 1);
        return -EIO;
    }

    be_pop(g_berry_vm, 1);
    return OK;
}

static int berry_exec_line(const char *line)
{
    if (!g_engines[SCRIPT_ENGINE_BERRY].loaded) {
        int ret = berry_init();
        if (ret < 0)
            return ret;
    }

    if (!g_berry_vm)
        return -ENOSYS;

    if (be_loadstring(g_berry_vm, line) != 0)
        return -EIO;

    if (be_pcall(g_berry_vm, 0) != 0) {
        const char *err = be_tostring(g_berry_vm, -1);
        fprintf(stderr, "[BERRY ERROR] %s\n", err ? err : "unknown");
        be_pop(g_berry_vm, 1);
        return -EIO;
    }

    be_pop(g_berry_vm, 1);
    return OK;
}

static void berry_deinit(void)
{
    if (!g_engines[SCRIPT_ENGINE_BERRY].loaded)
        return;

    if (g_berry_vm) {
        retro_ui_berry_deinit(g_berry_vm);
        be_vm_delete(g_berry_vm);
        g_berry_vm = NULL;
    }

    g_engines[SCRIPT_ENGINE_BERRY].loaded = false;
    syslog(LOG_INFO, "[SCRIPT] Berry deinitialized\n");
}

#else
static int berry_init(void) { return -ENOSYS; }
static int berry_exec_file(const char *p) { (void)p; return -ENOSYS; }
static int berry_exec_line(const char *l) { (void)l; return -ENOSYS; }
static void berry_deinit(void) {}
#endif /* CONFIG_RETRO_SCRIPT_BERRY */

/*==========================
 *  CPython（完整 Python 3，仅 ESP32-S3）
 *==========================*/

#ifdef CONFIG_RETRO_SCRIPT_PYTHON

/* NuttX apps interpreters/python 端口的头文件路径，按实际版本调整 */
#include <Python.h>

/* retro_ui CPython 绑定（retro_ui_py.c）/ retro_ui CPython bindings */
extern int retro_ui_py_init(void);

static bool g_python_ready = false;

static int python_init(void)
{
    if (g_engines[SCRIPT_ENGINE_PYTHON].loaded)
        return OK;

    syslog(LOG_INFO, "[SCRIPT] Initializing CPython...\n");

    /* 注册 retro_ui 内建模块（必须在 Py_Initialize 之前） */
    retro_ui_py_init();

#ifdef CONFIG_RETRO_GPIO_SCRIPT
    /* 注册 retro_gpio 内建模块（必须在 Py_Initialize 之前） */
    {
        extern int retro_gpio_py_init(void);
        retro_gpio_py_init();
    }
#endif

    Py_Initialize();
    g_python_ready = true;

    g_engines[SCRIPT_ENGINE_PYTHON].loaded = true;

    syslog(LOG_INFO, "[SCRIPT] CPython initialized\n");
    return OK;
}

static int python_exec_file(const char *path)
{
    if (!g_engines[SCRIPT_ENGINE_PYTHON].loaded) {
        int ret = python_init();
        if (ret < 0)
            return ret;
    }

    syslog(LOG_INFO, "[SCRIPT] CPython: executing %s\n", path);

    FILE *fp = fopen(path, "r");
    if (!fp)
        return -ENOENT;

    int ret = PyRun_SimpleFile(fp, path);
    fclose(fp);

    if (ret != 0) {
        PyErr_Print();
        return -EIO;
    }

    return OK;
}

static int python_exec_line(const char *line)
{
    if (!g_engines[SCRIPT_ENGINE_PYTHON].loaded) {
        int ret = python_init();
        if (ret < 0)
            return ret;
    }

    if (PyRun_SimpleString(line) != 0) {
        PyErr_Print();
        return -EIO;
    }

    return OK;
}

static void python_deinit(void)
{
    if (!g_engines[SCRIPT_ENGINE_PYTHON].loaded)
        return;

    if (g_python_ready) {
        Py_Finalize();
        g_python_ready = false;
    }

    g_engines[SCRIPT_ENGINE_PYTHON].loaded = false;
    syslog(LOG_INFO, "[SCRIPT] CPython deinitialized\n");
}

#else
static int python_init(void) { return -ENOSYS; }
static int python_exec_file(const char *p) { (void)p; return -ENOSYS; }
static int python_exec_line(const char *l) { (void)l; return -ENOSYS; }
static void python_deinit(void) {}
#endif /* CONFIG_RETRO_SCRIPT_PYTHON */

/*==========================
 *  jslogo（UCBLogo 子集，跑在 Duktape 上）
 *==========================*/

#if defined(CONFIG_RETRO_LOGO_JSLOGO) && defined(CONFIG_RETRO_SCRIPT_DUKTAPE)

static int jslogo_exec_file(const char *path)
{
    /* 确保 Duktape 已初始化（jslogo 依赖它）/ ensure Duktape is up */
    if (!g_engines[SCRIPT_ENGINE_DUKTAPE].loaded) {
        int ret = duk_init();
        if (ret < 0)
            return ret;
    }

    if (!g_duktape_ctx)
        return -ENOSYS;

    g_engines[SCRIPT_ENGINE_JSLOGO].loaded =
        g_engines[SCRIPT_ENGINE_DUKTAPE].loaded;

    return logo_jslogo_run(g_duktape_ctx, path);
}

#else
static int jslogo_exec_file(const char *p) { (void)p; return -ENOSYS; }
#endif

/*==========================
 *  统一脚本引擎接口
 *==========================*/

/**
 * 根据文件扩展名选择引擎
 */
static script_engine_type_t select_engine_by_ext(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return g_active_engine;
    ext++;

    if (strcmp(ext, "bas") == 0)  return SCRIPT_ENGINE_MYBASIC;
    if (strcmp(ext, "js") == 0)   return SCRIPT_ENGINE_DUKTAPE;
    if (strcmp(ext, "be") == 0)   return SCRIPT_ENGINE_BERRY;
    if (strcmp(ext, "py") == 0)   return SCRIPT_ENGINE_PYTHON;
    if (strcmp(ext, "lgo") == 0)  return SCRIPT_ENGINE_JSLOGO;

    return g_active_engine;
}

/**
 * 初始化所有脚本引擎（实际按需加载，此处只登记默认引擎）
 */
int script_init(void)
{
    syslog(LOG_INFO, "[SCRIPT] Initializing script engines...\n");

    /* 默认选择第一个编译可用的引擎 */
    for (int i = 0; i < SCRIPT_ENGINE_COUNT; i++) {
        if (g_engines[i].available) {
            g_active_engine = (script_engine_type_t)i;
            break;
        }
    }

    /* 启动即装载（用户要求 Berry + my-basic 默认编入 ROM 并默认可用；
     * 惰性装载会被 --gc-sections 裁掉引擎代码） */
#if defined(CONFIG_RETRO_SCRIPT_TINYBASIC)
    mybasic_init();
#endif
#if defined(CONFIG_RETRO_SCRIPT_BERRY)
    berry_init();
#endif

    syslog(LOG_INFO, "[SCRIPT] Script engines ready (eager)\n");
    return OK;
}

/**
 * 切换脚本引擎
 */
int script_set_engine(script_engine_type_t engine)
{
    if (engine >= SCRIPT_ENGINE_COUNT)
        return -EINVAL;

    if (!g_engines[engine].available) {
        syslog(LOG_ERR, "[SCRIPT] Engine '%s' not compiled in\n",
               g_engines[engine].name);
        return -ENOSYS;
    }

    /* 释放当前引擎 */
    script_unload_current();

    g_active_engine = engine;
    syslog(LOG_INFO, "[SCRIPT] Active engine: %s\n",
           g_engines[engine].name);

    return OK;
}

/**
 * 卸载当前引擎
 */
void script_unload_current(void)
{
    switch (g_active_engine) {
        case SCRIPT_ENGINE_MYBASIC:   mybasic_deinit(); break;
        case SCRIPT_ENGINE_DUKTAPE:   duk_deinit(); break;
        case SCRIPT_ENGINE_BERRY:     berry_deinit(); break;
        case SCRIPT_ENGINE_PYTHON:    python_deinit(); break;
        /* jslogo 无独立运行时，随 Duktape 一起卸载 / rides on Duktape */
        case SCRIPT_ENGINE_JSLOGO:    duk_deinit(); break;
        default: break;
    }
}

/**
 * 确保指定引擎已加载
 */
static int ensure_engine_loaded(script_engine_type_t engine)
{
    if (!g_engines[engine].available)
        return -ENOSYS;

    if (g_engines[engine].loaded)
        return OK;

    switch (engine) {
        case SCRIPT_ENGINE_MYBASIC:  return mybasic_init();
        case SCRIPT_ENGINE_DUKTAPE:  return duk_init();
        case SCRIPT_ENGINE_BERRY:    return berry_init();
        case SCRIPT_ENGINE_PYTHON:   return python_init();
        /* .lgo 跑在 Duktape 上（jslogo 无独立运行时）
         * jslogo rides on the Duktape engine */
        case SCRIPT_ENGINE_JSLOGO:   return duk_init();
        default: return -EINVAL;
    }
}

/**
 * 执行脚本文件
 */

/*
 * WHAT : 从内存缓冲执行脚本（ROM XIP 路径：buf 直指 Flash）
 * WHY  : 板级脚本打包进 ROMFS 镜像（HARDWARE.md 13.4），
 *        引擎 parser 直接消费 Flash 指针，零 RAM 拷贝
 * HOW  : 按文件名扩展名挑引擎（.be=Berry .bas=my-basic .js=Duktape）；
 *        my-basic 的 mb_load_string 需要 NUL 结尾——若 buf 尾部
 *        无 NUL 则栈上补一个 0 字节判定（镜像数据 16 对齐后有填充，
 *        生成器保证尾部至少一个 0）
 * 返回 : OK / 负错误码
 */
int script_exec_buffer(const char *name, const char *buf, size_t len)
{
    const char *dot = strrchr(name, '.');
    const char *ext = dot ? dot + 1 : "";
    char *zbuf = NULL;

    if (name == NULL || buf == NULL || len == 0)
        return -EINVAL;

    /* Berry/Duktape 走长度型接口；my-basic 要求 NUL 终止（只读 Flash 不可原地补零） */
    if (strcmp(ext, "bas") == 0 && buf[len - 1] != '\0') {
        zbuf = malloc(len + 1);
        if (zbuf == NULL)
            return -ENOMEM;
        memcpy(zbuf, buf, len);
        zbuf[len] = '\0';
        buf = zbuf;
    }

    int ret;

    if (strcmp(ext, "be") == 0) {
#ifdef CONFIG_RETRO_SCRIPT_BERRY
        if (!g_engines[SCRIPT_ENGINE_BERRY].loaded) {
            ret = berry_init();
            if (ret < 0) { free(zbuf); return ret; }
        }
        if (!g_berry_vm) { free(zbuf); return -ENOSYS; }
        ret = be_loadbuffer(g_berry_vm, name, buf, len) == 0 ? OK : -EIO;
        if (ret == OK) {
            if (be_pcall(g_berry_vm, 0) != 0) {
                const char *err = be_tostring(g_berry_vm, -1);
                fprintf(stderr, "[BERRY ERROR] %s\n", err ? err : "unknown");
                be_pop(g_berry_vm, 1);
                ret = -EIO;
            }
            be_pop(g_berry_vm, 0);
        }
#else
        ret = -ENOSYS;
#endif
    } else if (strcmp(ext, "bas") == 0) {
#ifdef CONFIG_RETRO_SCRIPT_TINYBASIC
        if (!g_engines[SCRIPT_ENGINE_MYBASIC].loaded) {
            ret = mybasic_init();
            if (ret < 0) { free(zbuf); return ret; }
        }
        if (!g_mb_ctx) { free(zbuf); return -ENOSYS; }
        ret = mb_load_string(g_mb_ctx, buf, true) == MB_FUNC_OK ? OK : -EIO;
        if (ret == OK) {
            ret = mb_run(g_mb_ctx, true) == MB_FUNC_OK ? OK : -EIO;
        }
#else
        ret = -ENOSYS;
#endif
    } else if (strcmp(ext, "js") == 0) {
#ifdef CONFIG_RETRO_SCRIPT_DUKTAPE
        if (!g_engines[SCRIPT_ENGINE_DUKTAPE].loaded) {
            ret = duk_init();
            if (ret < 0) { free(zbuf); return ret; }
        }
        if (!g_duktape_ctx) { free(zbuf); return -ENOSYS; }
        ret = duk_peval_lstring(g_duktape_ctx, buf, (duk_size_t)len) == 0 ? OK : -EIO;
#else
        ret = -ENOSYS;
#endif
    } else {
        ret = -ENOEXEC;
    }

    free(zbuf);
    return ret;
}

int script_exec_file(const char *path)
{
    script_engine_type_t engine = select_engine_by_ext(path);

    int ret = ensure_engine_loaded(engine);
    if (ret < 0)
        return ret;

    switch (engine) {
        case SCRIPT_ENGINE_MYBASIC:  ret = mybasic_exec_file(path); break;
        case SCRIPT_ENGINE_DUKTAPE:  ret = duk_exec_file(path); break;
        case SCRIPT_ENGINE_BERRY:    ret = berry_exec_file(path); break;
        case SCRIPT_ENGINE_PYTHON:   ret = python_exec_file(path); break;
        case SCRIPT_ENGINE_JSLOGO:   ret = jslogo_exec_file(path); break;
        default: ret = -EINVAL; break;
    }

    return ret;
}

/**
 * 执行一行脚本（REPL 模式）
 */
int script_exec_line(const char *line)
{
    int ret = ensure_engine_loaded(g_active_engine);
    if (ret < 0)
        return ret;

    switch (g_active_engine) {
        case SCRIPT_ENGINE_MYBASIC:  ret = mybasic_exec_line(line); break;
        case SCRIPT_ENGINE_DUKTAPE:  ret = duk_exec_line(line); break;
        case SCRIPT_ENGINE_BERRY:    ret = berry_exec_line(line); break;
        case SCRIPT_ENGINE_PYTHON:   ret = python_exec_line(line); break;
        case SCRIPT_ENGINE_JSLOGO:   ret = -ENOSYS; break;  /* 无 REPL */
        default: ret = -EINVAL; break;
    }

    return ret;
}

/**
 * 列出已加载的引擎状态
 */
void script_print_status(void)
{
    printf("\n");
    printf("=== Script Engine Status ===\n");
    printf("Active engine: %s\n", g_engines[g_active_engine].name);
    printf("\n");
    printf("%-12s %-8s %-10s %-10s %s\n", "Engine", "Compiled", "Loaded", "Memory", "Extension");
    printf("%-12s %-8s %-10s %-10s %s\n", "------", "--------", "------", "------", "---------");

    for (int i = 0; i < SCRIPT_ENGINE_COUNT; i++) {
        printf("%-12s %-8s %-10s %-10zu .%s\n",
               g_engines[i].name,
               g_engines[i].available ? "YES" : "no",
               g_engines[i].loaded ? "YES" : "NO",
               g_engines[i].loaded ? g_engines[i].mem_size : 0,
               g_engines[i].extension);
    }
    printf("\n");
}

/*==========================
 *  NSH 命令
 *==========================*/

/**
 * 按名称解析引擎 / resolve engine by name
 */
static script_engine_type_t engine_from_name(const char *name)
{
    if (strcmp(name, "basic") == 0 || strcmp(name, "bas") == 0)
        return SCRIPT_ENGINE_MYBASIC;
    if (strcmp(name, "js") == 0 || strcmp(name, "duk") == 0)
        return SCRIPT_ENGINE_DUKTAPE;
    if (strcmp(name, "berry") == 0 || strcmp(name, "be") == 0)
        return SCRIPT_ENGINE_BERRY;
    if (strcmp(name, "py") == 0 || strcmp(name, "python") == 0)
        return SCRIPT_ENGINE_PYTHON;
    if (strcmp(name, "logo") == 0 || strcmp(name, "jslogo") == 0)
        return SCRIPT_ENGINE_JSLOGO;
    return SCRIPT_ENGINE_COUNT;
}

int cmd_script(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: script <engine|run|list|status>\n");
        printf("  script engine <basic|js|berry|py|logo>  - 切换引擎\n");
        printf("  script run <filepath>                    - 执行脚本文件\n");
        printf("  script list                              - 列出脚本目录\n");
        printf("  script status                            - 显示引擎状态\n");
        return OK;
    }

    if (strcmp(argv[1], "engine") == 0 || strcmp(argv[1], "use") == 0) {
        if (argc < 3) {
            printf("Current engine: %s\n", g_engines[g_active_engine].name);
            return OK;
        }

        script_engine_type_t new_engine = engine_from_name(argv[2]);
        if (new_engine == SCRIPT_ENGINE_COUNT) {
            printf("Unknown engine: %s\n", argv[2]);
            printf("Available: basic, js, berry, py, logo\n");
            return OK;
        }

        if (script_set_engine(new_engine) < 0)
            printf("Engine not compiled in: %s\n", argv[2]);
        else
            printf("Engine switched to: %s\n", g_engines[new_engine].name);
    }
    else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            printf("Usage: script run <filepath>\n");
            return OK;
        }
        int ret = script_exec_file(argv[2]);
        if (ret < 0)
            printf("Script error: %d\n", ret);
    }
    else if (strcmp(argv[1], "list") == 0) {
        printf("\nUser scripts:\n");
        printf("  BASIC:  /sdcard/scripts/basic/*.bas\n");
        printf("  JS:     /sdcard/scripts/js/*.js\n");
        printf("  Berry:  /sdcard/scripts/berry/*.be\n");
        printf("  Python: /sdcard/scripts/python/*.py\n");
        printf("  Logo:   /sdcard/scripts/logo/*.lgo\n");
        printf("\n");
    }
    else if (strcmp(argv[1], "status") == 0) {
        script_print_status();
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return OK;
}
