/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_gpio_py.c - CPython 的 GPIO 绑定
 *
 * WHAT : Python 脚本 import retro_gpio 模块（教学 GPIO/ADC/PWM）
 * WHY  : 教学设备核心能力，统一走 retro_gpio.h（占用脚报"已占用"）
 * WHO  : script_engines.c 的 python_init() 调用 retro_gpio_py_init()
 *        （PyImport_AppendInittab 须在 Py_Initialize 之前）
 * WHERE: retro-ws/src/nuttx/common/driver/retro_gpio_py.c
 * WHEN : 2026-10-04 新增
 * HOW  : PyMethodDef 表 + PyModule_Create 内建模块
 *
 * 用法：
 *   import retro_gpio
 *   retro_gpio.config(12, "out")
 *   retro_gpio.write(12, 1)
 *   if retro_gpio.read(8) == 0: ...
 *   retro_gpio.pwm(12, 50, 1000)
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_PYTHON

#include <syslog.h>
#include <string.h>

/* NuttX apps interpreters/python 端口的头文件路径，按实际版本调整 */
#include <Python.h>

#include "retro_gpio.h"

static PyObject *py_gpio_config(PyObject *self, PyObject *args)
{
    int pin;
    const char *mode = "in";

    if (!PyArg_ParseTuple(args, "i|s", &pin, &mode))
        return NULL;
    return PyLong_FromLong(retro_gpio_config(pin, mode));
}

static PyObject *py_gpio_write(PyObject *self, PyObject *args)
{
    int pin, value;

    if (!PyArg_ParseTuple(args, "ii", &pin, &value))
        return NULL;
    return PyLong_FromLong(retro_gpio_write(pin, value));
}

static PyObject *py_gpio_read(PyObject *self, PyObject *args)
{
    int pin;

    if (!PyArg_ParseTuple(args, "i", &pin))
        return NULL;
    return PyLong_FromLong(retro_gpio_read(pin));
}

static PyObject *py_gpio_adc(PyObject *self, PyObject *args)
{
    int channel;

    if (!PyArg_ParseTuple(args, "i", &channel))
        return NULL;
    return PyLong_FromLong(retro_gpio_adc_read(channel));
}

static PyObject *py_gpio_pwm(PyObject *self, PyObject *args)
{
    int pin, duty, freq = 1000;

    if (!PyArg_ParseTuple(args, "ii|i", &pin, &duty, &freq))
        return NULL;
    return PyLong_FromLong(retro_gpio_pwm_set(pin, duty, freq));
}

static PyObject *py_gpio_release(PyObject *self, PyObject *args)
{
    int pin;

    if (!PyArg_ParseTuple(args, "i", &pin))
        return NULL;
    return PyLong_FromLong(retro_gpio_release(pin));
}

static PyMethodDef retro_gpio_methods[] = {
    { "config",  py_gpio_config,  METH_VARARGS, "config(pin, mode='in')" },
    { "write",   py_gpio_write,   METH_VARARGS, "write(pin, value)" },
    { "read",    py_gpio_read,    METH_VARARGS, "read(pin) -> 0/1" },
    { "adc",     py_gpio_adc,     METH_VARARGS, "adc(channel) -> raw" },
    { "pwm",     py_gpio_pwm,     METH_VARARGS, "pwm(pin, duty, freq=1000)" },
    { "release", py_gpio_release, METH_VARARGS, "release(pin)" },
    { NULL, NULL, 0, NULL }
};

static struct PyModuleDef retro_gpio_module = {
    PyModuleDef_HEAD_INIT,
    "retro_gpio",
    "ESP32 Retro WS script GPIO glue / 脚本 GPIO 接口",
    -1,
    retro_gpio_methods
};

PyMODINIT_FUNC PyInit_retro_gpio(void)
{
    PyObject *m = PyModule_Create(&retro_gpio_module);
    if (!m)
        return NULL;
    PyModule_AddStringConstant(m, "version", "1.0.0");
    return m;
}

int retro_gpio_py_init(void)
{
    if (PyImport_AppendInittab("retro_gpio", PyInit_retro_gpio) != 0) {
        syslog(LOG_ERR, "[retro_gpio_py] AppendInittab failed\n");
        return -1;
    }
    syslog(LOG_INFO, "[retro_gpio_py] registered\n");
    return 0;
}

#endif /* {guard} */
