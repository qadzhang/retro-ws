/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_bus_js.c - Duktape(JS) 的总线（I2C/SPI/UART）绑定
 *
 * WHAT : JS 脚本 retro_bus.i2c/spi/uart_* 对象方法
 * WHY  : machine 风格总线兼容层（HARDWARE.md 13.3）
 * WHO  : script_engines.c 的 duk 初始化调用 retro_bus_js_init()
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/retro_bus_js.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : duk_push_c_function 注册到全局对象 retro_bus；
 *        字节缓冲用 JS 数组（读返回 number[]）
 *
 * 用法：
 *   retro_bus.i2c_init(0, 19, 18, 100000)
 *   retro_bus.i2c_write(0, 0x68, [1, 2, 3])
 *   var b = retro_bus.i2c_read(0, 0x68, 4)
 *   retro_bus.uart_init(1, 4, 5, 115200)
 *   retro_bus.uart_write(1, "AT\r\n")
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_DUKTAPE

#include <syslog.h>
#include <errno.h>
#include <string.h>

#include "duktape.h"
#include "retro_bus.h"

static int arg_int(duk_context *ctx, int idx)
{
    return (int)duk_get_int(ctx, idx);
}

/* JS number[] -> C 字节；返回元素数 */
static int arg_bytes(duk_context *ctx, int idx, uint8_t *out, int max)
{
    int n = 0;

    if (!duk_is_array(ctx, idx))
        return 0;

    int len = (int)duk_get_length(ctx, idx);
    for (int i = 0; i < len && n < max; i++) {
        duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
        out[n++] = (uint8_t)duk_get_int(ctx, -1);
        duk_pop(ctx);
    }
    return n;
}

static void push_bytes(duk_context *ctx, const uint8_t *b, int n)
{
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_int(ctx, b[i]);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
}

static duk_ret_t js_i2c_init(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_i2c_init(arg_int(ctx, 0), arg_int(ctx, 2),
                                         arg_int(ctx, 1), arg_int(ctx, 3)));
    return 1;
}

static duk_ret_t js_i2c_write(duk_context *ctx)
{
    uint8_t tmp[32];
    int n = arg_bytes(ctx, 2, tmp, (int)sizeof(tmp));

    duk_push_int(ctx, retro_bus_i2c_write(arg_int(ctx, 0),
                                          (uint16_t)arg_int(ctx, 1),
                                          tmp, n));
    return 1;
}

static duk_ret_t js_i2c_read(duk_context *ctx)
{
    uint8_t tmp[32];
    int len = arg_int(ctx, 2);

    if (len <= 0 || len > (int)sizeof(tmp)) {
        duk_push_int(ctx, -22);      /* -EINVAL */
        return 1;
    }

    int r = retro_bus_i2c_read(arg_int(ctx, 0), (uint16_t)arg_int(ctx, 1),
                               tmp, len);
    if (r < 0) {
        duk_push_int(ctx, r);
        return 1;
    }

    push_bytes(ctx, tmp, r);
    return 1;
}

static duk_ret_t js_i2c_write_reg8(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_i2c_write_reg8(arg_int(ctx, 0),
                                               (uint16_t)arg_int(ctx, 1),
                                               (uint8_t)arg_int(ctx, 2),
                                               (uint8_t)arg_int(ctx, 3)));
    return 1;
}

static duk_ret_t js_i2c_read_regs8(duk_context *ctx)
{
    uint8_t tmp[32];
    int len = arg_int(ctx, 3);

    if (len <= 0 || len > (int)sizeof(tmp)) {
        duk_push_int(ctx, -22);
        return 1;
    }

    int r = retro_bus_i2c_read_regs8(arg_int(ctx, 0),
                                     (uint16_t)arg_int(ctx, 1),
                                     (uint8_t)arg_int(ctx, 2), tmp, len);
    if (r < 0) {
        duk_push_int(ctx, r);
        return 1;
    }

    push_bytes(ctx, tmp, r);
    return 1;
}

static duk_ret_t js_i2c_scan(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_i2c_scan(arg_int(ctx, 0), NULL, NULL));
    return 1;
}

static duk_ret_t js_spi_init(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_spi_init(arg_int(ctx, 0), arg_int(ctx, 1),
                                         arg_int(ctx, 2), arg_int(ctx, 3),
                                         arg_int(ctx, 4)));
    return 1;
}

static duk_ret_t js_spi_xfer(duk_context *ctx)
{
    uint8_t tmp[32];
    int n = arg_bytes(ctx, 1, tmp, (int)sizeof(tmp));

    if (n <= 0) {
        duk_push_int(ctx, -22);
        return 1;
    }

    int r = retro_bus_spi_xfer(arg_int(ctx, 0), tmp, tmp, n);
    if (r < 0) {
        duk_push_int(ctx, r);
        return 1;
    }

    push_bytes(ctx, tmp, r);
    return 1;
}

static duk_ret_t js_uart_init(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_uart_init(arg_int(ctx, 0), arg_int(ctx, 1),
                                          arg_int(ctx, 2), arg_int(ctx, 3)));
    return 1;
}

static duk_ret_t js_uart_write(duk_context *ctx)
{
    const char *s = duk_is_string(ctx, 1) ? duk_get_string(ctx, 1) : NULL;

    duk_push_int(ctx, retro_bus_uart_write(arg_int(ctx, 0), s,
                                           s ? (int)strlen(s) : 0));
    return 1;
}

static duk_ret_t js_uart_read(duk_context *ctx)
{
    char tmp[96];
    int len = arg_int(ctx, 1);

    if (len <= 0 || len > (int)sizeof(tmp)) {
        duk_push_int(ctx, -22);
        return 1;
    }

    int r = retro_bus_uart_read(arg_int(ctx, 0), tmp, len, arg_int(ctx, 2));
    if (r < 0) {
        duk_push_int(ctx, r);
        return 1;
    }

    duk_push_lstring(ctx, tmp, (duk_size_t)(r > 0 ? r : 0));
    return 1;
}

static duk_ret_t js_uart_available(duk_context *ctx)
{
    duk_push_int(ctx, retro_bus_uart_available(arg_int(ctx, 0)));
    return 1;
}

void retro_bus_js_init(duk_context *ctx)
{
    if (!ctx)
        return;

    duk_idx_t obj = duk_push_object(ctx);

    duk_push_c_function(ctx, js_i2c_init, 4);
    duk_put_prop_string(ctx, obj, "i2c_init");
    duk_push_c_function(ctx, js_i2c_write, 3);
    duk_put_prop_string(ctx, obj, "i2c_write");
    duk_push_c_function(ctx, js_i2c_read, 3);
    duk_put_prop_string(ctx, obj, "i2c_read");
    duk_push_c_function(ctx, js_i2c_write_reg8, 4);
    duk_put_prop_string(ctx, obj, "i2c_write_reg8");
    duk_push_c_function(ctx, js_i2c_read_regs8, 4);
    duk_put_prop_string(ctx, obj, "i2c_read_regs8");
    duk_push_c_function(ctx, js_i2c_scan, 1);
    duk_put_prop_string(ctx, obj, "i2c_scan");
    duk_push_c_function(ctx, js_spi_init, 5);
    duk_put_prop_string(ctx, obj, "spi_init");
    duk_push_c_function(ctx, js_spi_xfer, 2);
    duk_put_prop_string(ctx, obj, "spi_xfer");
    duk_push_c_function(ctx, js_uart_init, 4);
    duk_put_prop_string(ctx, obj, "uart_init");
    duk_push_c_function(ctx, js_uart_write, 2);
    duk_put_prop_string(ctx, obj, "uart_write");
    duk_push_c_function(ctx, js_uart_read, 3);
    duk_put_prop_string(ctx, obj, "uart_read");
    duk_push_c_function(ctx, js_uart_available, 1);
    duk_put_prop_string(ctx, obj, "uart_available");

    duk_put_global_string(ctx, "retro_bus");

    syslog(LOG_INFO, "[retro_bus_js] registered\n");
}

#endif /* CONFIG_RETRO_SCRIPT_DUKTAPE */
