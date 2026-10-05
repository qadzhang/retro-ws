/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_ui_py.c - retro_ui 的 CPython 绑定
 *
 * WHAT : retro_ui 的 CPython 绑定
 * WHY  : Python 脚本 import retro_ui 调 UI（仅 S3 N16R8）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/modules/retro_ui_py.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : PyImport_AppendInittab 注册内建模块（可选编译）
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_PYTHON

#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifdef CONFIG_LVGL
#include "lvgl/lvgl.h"
#endif

/* NuttX apps interpreters/python 端口的头文件路径，按实际版本调整 */
#include <Python.h>

#include "retro_ui.h"

#define PY_LIST_MAX_ITEMS   20
#define PY_ITEM_MAX_LEN     64

/*======================================
 * retro_ui.msgbox(title, msg)
 *======================================*/
static PyObject *py_msgbox(PyObject *self, PyObject *args)
{
    const char *title;
    const char *msg;

    if (!PyArg_ParseTuple(args, "ss", &title, &msg))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] msgbox: %s\n", title);

#ifdef CONFIG_LVGL
    retro_ui_msgbox(title, msg);
#endif
    Py_RETURN_NONE;
}

/*======================================
 * retro_ui.input(title, prompt, bufsize=64) -> str
 *======================================*/
static PyObject *py_input(PyObject *self, PyObject *args)
{
    const char *title;
    const char *prompt;
    int bufsize = 64;

    if (!PyArg_ParseTuple(args, "ss|i", &title, &prompt, &bufsize))
        return NULL;

    if (bufsize < 1) bufsize = 64;
    if (bufsize > 1024) bufsize = 1024;

    syslog(LOG_INFO, "[retro_ui_py] input: %s\n", title);

    static char g_py_input_buf[1024];

#ifdef CONFIG_LVGL
    int ok = retro_ui_input(title, prompt, g_py_input_buf, bufsize);
    if (ok == 1)
        return PyUnicode_FromString(g_py_input_buf);
#endif
    return PyUnicode_FromString("");
}

/*======================================
 * retro_ui.list(title, prompt, items) -> int
 *======================================*/
static PyObject *py_list(PyObject *self, PyObject *args)
{
    const char *title;
    const char *prompt;
    PyObject *obj;

    if (!PyArg_ParseTuple(args, "ssO", &title, &prompt, &obj))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] list: %s\n", title);

    int selected = -1;

#ifdef CONFIG_LVGL
    PyObject *seq = PySequence_Fast(obj, "items 必须为序列 / items must be a sequence");
    if (!seq)
        return NULL;

    Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
    if (count > PY_LIST_MAX_ITEMS)
        count = PY_LIST_MAX_ITEMS;

    static const char *g_py_items[PY_LIST_MAX_ITEMS];
    static char g_py_item_bufs[PY_LIST_MAX_ITEMS][PY_ITEM_MAX_LEN];

    for (Py_ssize_t i = 0; i < count; i++) {
        PyObject *item = PySequence_Fast_GET_ITEM(seq, i);
        const char *s = PyUnicode_AsUTF8(item);
        strncpy(g_py_item_bufs[i], s ? s : "", PY_ITEM_MAX_LEN - 1);
        g_py_item_bufs[i][PY_ITEM_MAX_LEN - 1] = '\0';
        g_py_items[i] = g_py_item_bufs[i];
    }
    Py_DECREF(seq);

    selected = retro_ui_list(title, prompt, g_py_items, (int)count);
#endif
    return PyLong_FromLong(selected);
}

/*======================================
 * retro_ui.confirm(title, msg) -> bool
 *======================================*/
static PyObject *py_confirm(PyObject *self, PyObject *args)
{
    const char *title;
    const char *msg;

    if (!PyArg_ParseTuple(args, "ss", &title, &msg))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] confirm: %s\n", title);

    int result = 0;
#ifdef CONFIG_LVGL
    result = retro_ui_confirm(title, msg);
#endif
    if (result == 1)
        Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

/*======================================
 * retro_ui.status(msg)
 *======================================*/
static PyObject *py_status(PyObject *self, PyObject *args)
{
    const char *msg;

    if (!PyArg_ParseTuple(args, "s", &msg))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] status: %s\n", msg);

#ifdef CONFIG_LVGL
    retro_ui_status(msg);
#endif
    Py_RETURN_NONE;
}

/*======================================
 * retro_ui.progress(value, max=100)
 *======================================*/
static PyObject *py_progress(PyObject *self, PyObject *args)
{
    int value;
    int max = 100;

    if (!PyArg_ParseTuple(args, "i|i", &value, &max))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] progress: %d/%d\n", value, max);

#ifdef CONFIG_LVGL
    retro_ui_progress(value, max);
#endif
    Py_RETURN_NONE;
}

/*======================================
 * retro_ui.close_window(title) -> int
 *======================================*/
static PyObject *py_close_window(PyObject *self, PyObject *args)
{
    const char *title;

    if (!PyArg_ParseTuple(args, "s", &title))
        return NULL;

    syslog(LOG_INFO, "[retro_ui_py] close_window: %s\n", title);

    int result = -1;
#ifdef CONFIG_LVGL
    result = retro_ui_close_window(title);
#endif
    return PyLong_FromLong(result);
}

/*======================================
 * retro_ui.set_lang(lang) -> int
 *======================================*/
static PyObject *py_set_lang(PyObject *self, PyObject *args)
{
    const char *lang;

    if (!PyArg_ParseTuple(args, "s", &lang))
        return NULL;

    return PyLong_FromLong(retro_ui_set_lang(lang));
}

/*======================================
 * retro_ui.get_lang() -> str
 *======================================*/
static PyObject *py_get_lang(PyObject *self, PyObject *args)
{
    (void)self;
    (void)args;

    const char *lang = retro_ui_get_lang();
    return PyUnicode_FromString(lang ? lang : "unknown");
}

/*======================================
 * 模块定义 / Module Definition
 *======================================*/

static PyMethodDef retro_ui_methods[] = {
    { "msgbox",       py_msgbox,       METH_VARARGS, "msgbox(title, msg)" },
    { "input",        py_input,        METH_VARARGS, "input(title, prompt, bufsize=64) -> str" },
    { "list",         py_list,         METH_VARARGS, "list(title, prompt, items) -> int" },
    { "confirm",      py_confirm,      METH_VARARGS, "confirm(title, msg) -> bool" },
    { "status",       py_status,       METH_VARARGS, "status(msg)" },
    { "progress",     py_progress,     METH_VARARGS, "progress(value, max=100)" },
    { "close_window", py_close_window, METH_VARARGS, "close_window(title) -> int" },
    { "set_lang",     py_set_lang,     METH_VARARGS, "set_lang(lang) -> int" },
    { "get_lang",     py_get_lang,     METH_NOARGS,  "get_lang() -> str" },
    { NULL, NULL, 0, NULL }
};

static struct PyModuleDef retro_ui_module = {
    PyModuleDef_HEAD_INIT,
    "retro_ui",                                   /* 模块名 / module name */
    "ESP32 Retro WS UI glue layer / UI 胶水层",   /* 文档 / doc */
    -1,                                           /* 无每解释器状态 */
    retro_ui_methods
};

/**
 * PyInit_retro_ui - CPython 内建模块入口 / builtin module entry
 */
PyMODINIT_FUNC PyInit_retro_ui(void)
{
    PyObject *m = PyModule_Create(&retro_ui_module);
    if (!m)
        return NULL;

    PyModule_AddStringConstant(m, "version", "1.0.0");
    return m;
}

/**
 * retro_ui_py_init - 注册内建模块（必须在 Py_Initialize 前调用）
 * retro_ui_py_init - register builtin module (call before Py_Initialize)
 */
int retro_ui_py_init(void)
{
#ifdef CONFIG_LVGL
    retro_ui_init();
#endif

    if (PyImport_AppendInittab("retro_ui", PyInit_retro_ui) != 0) {
        syslog(LOG_ERR, "[retro_ui_py] AppendInittab failed\n");
        return -EIO;
    }

    syslog(LOG_INFO, "[retro_ui_py] builtin module registered\n");
    return 0;
}

#endif /* CONFIG_RETRO_SCRIPT_PYTHON */
