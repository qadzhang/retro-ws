/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_bus_berry.c - Berry 的总线（I2C/SPI/UART）绑定
 *
 * WHAT : Berry 脚本 retro_i2c/spi/uart_* 全局函数
 * WHY  : machine 风格总线兼容层（HARDWARE.md 13.3）
 * WHO  : script_engines.c 的 berry_init() 调用 retro_bus_berry_init()
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/retro_bus_berry.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : be_regfunc 注册；字节缓冲用 bytes 类型（be_pushbytes /
 *        be_isbytes / be_tobytes）
 *
 * 用法：
 *   retro_i2c_init(0, 18, 19, 100000)
 *   retro_i2c_write(0, 0x68, bytes('01'))       # writeto
 *   b = retro_i2c_read(0, 0x68, 4)              # readfrom -> bytes
 *   retro_i2c_write_reg8(0, 0x68, 0x00, 0x0f)
 *   retro_spi_init(0, 11, 12, 10, 1000000)
 *   rx = retro_spi_xfer(0, bytes('ff ff'))      # 全双工
 *   retro_uart_init(1, 4, 5, 115200)
 *   retro_uart_write(1, 'AT\r\n')
 *   retro_uart_read(1, 32, 100)                 # 100ms 限时
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_BERRY

#include <syslog.h>
#include <errno.h>
#include <string.h>

#include "berry.h"
#include "retro_bus.h"

static int arg_int(bvm *vm, int idx)
{
    if (idx > be_top(vm) || !be_isnumber(vm, idx))
        return -1;
    return be_toint(vm, idx);
}

static int be_i2c_init(bvm *vm)
{
    be_pushint(vm, retro_bus_i2c_init(arg_int(vm, 1), arg_int(vm, 3),
                                      arg_int(vm, 2), arg_int(vm, 4)));
    be_return(vm);
}

static int be_i2c_write(bvm *vm)
{
    const void *buf = NULL;
    size_t len = 0;

    if (be_top(vm) >= 3 && be_isbytes(vm, 3)) {
        buf = be_tobytes(vm, 3, &len);
    } else if (be_top(vm) >= 3 && be_isstring(vm, 3)) {
        buf = be_tostring(vm, 3);
        len = strlen((const char *)buf);
    }

    be_pushint(vm, retro_bus_i2c_write(arg_int(vm, 1),
                                       (uint16_t)arg_int(vm, 2),
                                       buf, (int)len));
    be_return(vm);
}

static int be_i2c_read(bvm *vm)
{
    uint8_t tmp[64];
    int len = arg_int(vm, 3);

    if (len <= 0 || (size_t)len > sizeof(tmp)) {
        be_pushint(vm, -EINVAL);
        be_return(vm);
    }

    int r = retro_bus_i2c_read(arg_int(vm, 1), (uint16_t)arg_int(vm, 2),
                               tmp, len);

    if (r < 0) {
        be_pushint(vm, r);
        be_return(vm);
    }

    be_pushbytes(vm, tmp, (size_t)r);
    be_return(vm);
}

static int be_i2c_write_reg8(bvm *vm)
{
    be_pushint(vm, retro_bus_i2c_write_reg8(arg_int(vm, 1),
                                            (uint16_t)arg_int(vm, 2),
                                            (uint8_t)arg_int(vm, 3),
                                            (uint8_t)arg_int(vm, 4)));
    be_return(vm);
}

static int be_i2c_read_regs8(bvm *vm)
{
    uint8_t tmp[64];
    int len = arg_int(vm, 4);

    if (len <= 0 || (size_t)len > sizeof(tmp)) {
        be_pushint(vm, -EINVAL);
        be_return(vm);
    }

    int r = retro_bus_i2c_read_regs8(arg_int(vm, 1), (uint16_t)arg_int(vm, 2),
                                     (uint8_t)arg_int(vm, 3), tmp, len);

    if (r < 0) {
        be_pushint(vm, r);
        be_return(vm);
    }

    be_pushbytes(vm, tmp, (size_t)r);
    be_return(vm);
}

static int be_i2c_scan_cb(int addr, void *arg)
{
    bvm *vm = (bvm *)arg;

    be_pushint(vm, addr);
    be_call(vm, 1);
    be_pop(vm, 1);
    return 0;
}

static int be_i2c_scan(bvm *vm)
{
    /* 有回调（函数）则逐地址回调，否则返回命中数 */
    if (be_top(vm) >= 2 && be_isfunction(vm, 2)) {
        be_pushint(vm, retro_bus_i2c_scan(arg_int(vm, 1), be_i2c_scan_cb, vm));
    } else {
        be_pushint(vm, retro_bus_i2c_scan(arg_int(vm, 1), NULL, NULL));
    }
    be_return(vm);
}

static int be_spi_init(bvm *vm)
{
    be_pushint(vm, retro_bus_spi_init(arg_int(vm, 1), arg_int(vm, 2),
                                      arg_int(vm, 3), arg_int(vm, 4),
                                      arg_int(vm, 5)));
    be_return(vm);
}

static int be_spi_xfer(bvm *vm)
{
    uint8_t tmp[64];
    const void *tx = NULL;
    size_t len = 0;

    if (be_top(vm) >= 2 && be_isbytes(vm, 2)) {
        tx = be_tobytes(vm, 2, &len);
    } else if (be_top(vm) >= 2 && be_isstring(vm, 2)) {
        tx = be_tostring(vm, 2);
        len = strlen((const char *)tx);
    }

    if (len == 0 || len > sizeof(tmp)) {
        be_pushint(vm, -EINVAL);
        be_return(vm);
    }

    int r = retro_bus_spi_xfer(arg_int(vm, 1), tx, tmp, (int)len);

    if (r < 0) {
        be_pushint(vm, r);
        be_return(vm);
    }

    be_pushbytes(vm, tmp, (size_t)r);
    be_return(vm);
}

static int be_uart_init(bvm *vm)
{
    be_pushint(vm, retro_bus_uart_init(arg_int(vm, 1), arg_int(vm, 2),
                                       arg_int(vm, 3), arg_int(vm, 4)));
    be_return(vm);
}

static int be_uart_write(bvm *vm)
{
    const char *s = (be_top(vm) >= 2) ? be_tostring(vm, 2) : NULL;

    be_pushint(vm, retro_bus_uart_write(arg_int(vm, 1), s,
                                        s ? (int)strlen(s) : 0));
    be_return(vm);
}

static int be_uart_read(bvm *vm)
{
    char tmp[128];
    int len = arg_int(vm, 2);

    if (len <= 0 || (size_t)len > sizeof(tmp)) {
        be_pushint(vm, -EINVAL);
        be_return(vm);
    }

    int r = retro_bus_uart_read(arg_int(vm, 1), tmp, len, arg_int(vm, 3));

    if (r <= 0) {
        be_pushint(vm, r);
        be_return(vm);
    }

    be_pushnstring(vm, tmp, (size_t)r);
    be_return(vm);
}

static int be_uart_available(bvm *vm)
{
    be_pushint(vm, retro_bus_uart_available(arg_int(vm, 1)));
    be_return(vm);
}

void retro_bus_berry_init(bvm *vm)
{
    if (!vm)
        return;

    be_regfunc(vm, "retro_i2c_init",       be_i2c_init);
    be_regfunc(vm, "retro_i2c_write",      be_i2c_write);
    be_regfunc(vm, "retro_i2c_read",       be_i2c_read);
    be_regfunc(vm, "retro_i2c_write_reg8", be_i2c_write_reg8);
    be_regfunc(vm, "retro_i2c_read_regs8", be_i2c_read_regs8);
    be_regfunc(vm, "retro_i2c_scan",       be_i2c_scan);
    be_regfunc(vm, "retro_spi_init",       be_spi_init);
    be_regfunc(vm, "retro_spi_xfer",       be_spi_xfer);
    be_regfunc(vm, "retro_uart_init",      be_uart_init);
    be_regfunc(vm, "retro_uart_write",     be_uart_write);
    be_regfunc(vm, "retro_uart_read",      be_uart_read);
    be_regfunc(vm, "retro_uart_available", be_uart_available);

    syslog(LOG_INFO, "[retro_bus_berry] registered\n");
}

#endif /* CONFIG_RETRO_SCRIPT_BERRY */
