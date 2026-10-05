/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_bus_bas.c - my-basic 的总线（I2C/SPI/UART）绑定
 *
 * WHAT : BASIC 脚本 retro_i2c/spi/uart_* CALL/函数
 * WHY  : machine 风格总线兼容层（HARDWARE.md 13.3）
 * WHO  : script_engines.c 的 mybasic_init() 调用 retro_bus_bas_register()
 * WHERE: retro-ws/src/nuttx/common/driver/retro_bus_bas.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : mb_register_func；字节流接口用十进制字符串（BASIC 无
 *        bytes 类型：写=逗号分隔十进制串，读=返回同格式串）
 *
 * 用法：
 *   CALL retro_i2c_init(0, 19, 18, 100000)
 *   CALL retro_i2c_write(0, 0x68, "0,1,2")
 *   LET r$ = retro_i2c_read$(0, 0x68, 4)
 *   IF retro_i2c_write_reg8(0, 0x68, 0, 15) = 0 THEN ...
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPT_TINYBASIC

#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if __has_include(<my_basic.h>)
#  include <my_basic.h>
#else
#  include "deps/my_basic/core/my_basic.h"
#endif
#include "retro_bus.h"

/* 十进制串 -> 字节缓冲；返回字节数（0 失败） */
static int parse_bytes(const char *s, uint8_t *out, int max)
{
    int n = 0;

    if (s == NULL)
        return 0;

    while (*s && n < max) {
        char *end;
        long v = strtol(s, &end, 0);

        if (end == s)
            break;
        out[n++] = (uint8_t)v;
        s = end;
        while (*s == ',' || *s == ' ')
            s++;
    }
    return n;
}

/* 字节缓冲 -> "a,b,c" 串（静态循环缓冲 3 个防覆盖） */
static const char *bytes_to_str(const uint8_t *b, int n)
{
    static char buf[3][96];
    static int slot = 0;
    char *p = buf[slot];
    char *end = p + sizeof(buf[0]) - 1;

    slot = (slot + 1) % 3;
    for (int i = 0; i < n && p < end; i++)
        p += snprintf(p, (size_t)(end - p), "%s%d", i ? "," : "", b[i]);
    *p = 0;
    return buf[(slot + 2) % 3];
}

/* CALL retro_i2c_init(id, scl, sda, freq) */
static int bas_i2c_init(struct mb_interpreter_t *s, void **l)
{
    int id = -1, scl = -1, sda = -1, freq = 100000;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &scl);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &sda);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &freq);
    mb_attempt_func_end(s, l);

    return retro_bus_i2c_init(id, sda, scl, freq) < 0 ? MB_FUNC_ERR
                                                      : MB_FUNC_OK;
}

/* CALL retro_i2c_write(id, addr, bytes$) */
static int bas_i2c_write(struct mb_interpreter_t *s, void **l)
{
    int id = -1, addr = -1;
    char *bs = NULL;
    uint8_t tmp[32];

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &addr);
    if (mb_has_arg(s, l)) mb_pop_string(s, l, &bs);
    mb_attempt_func_end(s, l);

    int n = parse_bytes(bs, tmp, (int)sizeof(tmp));
    return retro_bus_i2c_write(id, (uint16_t)addr, tmp, n) < 0
           ? MB_FUNC_ERR : MB_FUNC_OK;
}

/* r$ = retro_i2c_read$(id, addr, len) */
static int bas_i2c_read(struct mb_interpreter_t *s, void **l)
{
    int id = -1, addr = -1, len = 0;
    uint8_t tmp[32];

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &addr);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &len);
    mb_attempt_func_end(s, l);

    if (len <= 0 || len > (int)sizeof(tmp))
        return MB_FUNC_ERR;

    int r = retro_bus_i2c_read(id, (uint16_t)addr, tmp, len);
    if (r < 0)
        return MB_FUNC_ERR;

    mb_push_string(s, l, (char *)bytes_to_str(tmp, r));
    return MB_FUNC_OK;
}

static int bas_i2c_write_reg8(struct mb_interpreter_t *s, void **l)
{
    int id = -1, addr = -1, reg = 0, val = 0;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &addr);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &reg);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &val);
    mb_attempt_func_end(s, l);

    return retro_bus_i2c_write_reg8(id, (uint16_t)addr,
                                    (uint8_t)reg, (uint8_t)val) < 0
           ? MB_FUNC_ERR : MB_FUNC_OK;
}

static int bas_i2c_read_regs8(struct mb_interpreter_t *s, void **l)
{
    int id = -1, addr = -1, reg = 0, len = 0;
    uint8_t tmp[32];

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &addr);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &reg);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &len);
    mb_attempt_func_end(s, l);

    if (len <= 0 || len > (int)sizeof(tmp))
        return MB_FUNC_ERR;

    int r = retro_bus_i2c_read_regs8(id, (uint16_t)addr, (uint8_t)reg,
                                     tmp, len);
    if (r < 0)
        return MB_FUNC_ERR;

    mb_push_string(s, l, (char *)bytes_to_str(tmp, r));
    return MB_FUNC_OK;
}

/* n = retro_i2c_scan(id) —— 命中数 */
static int bas_i2c_scan(struct mb_interpreter_t *s, void **l)
{
    int id = -1;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    mb_attempt_func_end(s, l);

    mb_push_int(s, l, retro_bus_i2c_scan(id, NULL, NULL));
    return MB_FUNC_OK;
}

static int bas_spi_init(struct mb_interpreter_t *s, void **l)
{
    int id = -1, mosi = -1, miso = -1, sck = -1, freq = 1000000;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &mosi);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &miso);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &sck);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &freq);
    mb_attempt_func_end(s, l);

    return retro_bus_spi_init(id, mosi, miso, sck, freq) < 0 ? MB_FUNC_ERR
                                                            : MB_FUNC_OK;
}

/* r$ = retro_spi_xfer$(id, bytes$) */
static int bas_spi_xfer(struct mb_interpreter_t *s, void **l)
{
    int id = -1;
    char *bs = NULL;
    uint8_t tmp[32];

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_string(s, l, &bs);
    mb_attempt_func_end(s, l);

    int n = parse_bytes(bs, tmp, (int)sizeof(tmp));
    if (n <= 0)
        return MB_FUNC_ERR;

    int r = retro_bus_spi_xfer(id, tmp, tmp, n);
    if (r < 0)
        return MB_FUNC_ERR;

    mb_push_string(s, l, (char *)bytes_to_str(tmp, r));
    return MB_FUNC_OK;
}

static int bas_uart_init(struct mb_interpreter_t *s, void **l)
{
    int id = -1, tx = -1, rx = -1, baud = 115200;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &tx);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &rx);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &baud);
    mb_attempt_func_end(s, l);

    return retro_bus_uart_init(id, tx, rx, baud) < 0 ? MB_FUNC_ERR
                                                     : MB_FUNC_OK;
}

static int bas_uart_write(struct mb_interpreter_t *s, void **l)
{
    int id = -1;
    char *str = NULL;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_string(s, l, &str);
    mb_attempt_func_end(s, l);

    return retro_bus_uart_write(id, str, str ? (int)strlen(str) : 0) < 0
           ? MB_FUNC_ERR : MB_FUNC_OK;
}

/* r$ = retro_uart_read$(id, len, timeout_ms) */
static int bas_uart_read(struct mb_interpreter_t *s, void **l)
{
    int id = -1, len = 0, tmo = -1;
    char tmp[96];

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &len);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &tmo);
    mb_attempt_func_end(s, l);

    if (len <= 0 || len > (int)sizeof(tmp) - 1)
        return MB_FUNC_ERR;

    int r = retro_bus_uart_read(id, tmp, len, tmo);
    if (r < 0)
        return MB_FUNC_ERR;

    tmp[r > 0 ? r : 0] = 0;
    mb_push_string(s, l, tmp);
    return MB_FUNC_OK;
}

static int bas_uart_available(struct mb_interpreter_t *s, void **l)
{
    int id = -1;

    mb_attempt_func_begin(s, l);
    if (mb_has_arg(s, l)) mb_pop_int(s, l, &id);
    mb_attempt_func_end(s, l);

    mb_push_int(s, l, retro_bus_uart_available(id));
    return MB_FUNC_OK;
}

void retro_bus_bas_register(struct mb_interpreter_t *s)
{
    if (!s)
        return;

    mb_register_func(s, "retro_i2c_init",       bas_i2c_init);
    mb_register_func(s, "retro_i2c_write",      bas_i2c_write);
    mb_register_func(s, "retro_i2c_read",       bas_i2c_read);
    mb_register_func(s, "retro_i2c_write_reg8", bas_i2c_write_reg8);
    mb_register_func(s, "retro_i2c_read_regs8", bas_i2c_read_regs8);
    mb_register_func(s, "retro_i2c_scan",       bas_i2c_scan);
    mb_register_func(s, "retro_spi_init",       bas_spi_init);
    mb_register_func(s, "retro_spi_xfer",       bas_spi_xfer);
    mb_register_func(s, "retro_uart_init",      bas_uart_init);
    mb_register_func(s, "retro_uart_write",     bas_uart_write);
    mb_register_func(s, "retro_uart_read",      bas_uart_read);
    mb_register_func(s, "retro_uart_available", bas_uart_available);

    syslog(LOG_INFO, "[retro_bus_bas] registered\n");
}

#endif /* CONFIG_RETRO_SCRIPT_TINYBASIC */
