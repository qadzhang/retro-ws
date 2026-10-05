/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * retro_bus.h - 脚本引擎总线统一接口（I2C/SPI/UART，machine 风格）
 *
 * WHAT : 面向全部脚本引擎的 I2C/SPI/UART 总线接口（教学定位）
 * WHY  : 用户要求兼容层学习 MicroPython machine——同构 API 覆盖
 *        三大总线；后端运行时探测：NuttX 硬件驱动(/dev/i2cN 等)
 *        优先，缺驱动的板回退 retro_gpio 位摆软总线
 * WHO  : retro_bus_{bas,js,berry}.c 各引擎绑定转发到本接口
 * WHERE: retro-ws/src/nuttx/common/driver/retro_bus.[ch]
 * WHEN : 2026-10-04(晚) 新增（NEXT_STEPS 49 落地）
 * HOW  : 会话表按 id 索引；I2C 硬件后端=ioctl I2CIOC_TRANSFER，
 *        SPI 硬件后端=SPIIOC_TRANSFER 序列，UART=termios 原始流；
 *        软后端=retro_gpio 半双工位摆（100kHz 级，够教学传感器）
 *
 * 用法示例（Berry）：
 *   retro_i2c_init(0, 18, 19, 100000)      # I2C(0, scl=19, sda=18)
 *   retro_i2c_write(0, 0x68, bytes)        # writeto
 *   b = retro_i2c_read(0, 0x68, 4)         # readfrom
 *   retro_spi_init(0, 11, 12, 10, 1000000) # SPI(0, baudrate=1M)
 *   retro_uart_init(1, 4, 5, 115200)       # UART(1, tx=4, rx=5)
 */

#ifndef __RETRO_BUS_H
#define __RETRO_BUS_H

#include <nuttx/config.h>
#include <stdint.h>
#include <stddef.h>

/* 总线实例上限（超出返回 -ENODEV） */
#define RETRO_BUS_I2C_MAX     2
#define RETRO_BUS_SPI_MAX     2
#define RETRO_BUS_UART_MAX    3

/*==== 初始化/释放（AGENTS.md 8.1 模式，五引擎同名映射）====*/

/*
 * I2C：id 总线号；scl/sda 引脚（软后端使用；硬件后端由板级路由，
 * 传入引脚仅做登记）；freq_hz 目标频率（软后端半周期节拍）
 */
int retro_bus_i2c_init(int id, int scl, int sda, int freq_hz);

/* SPI：mode0 主机；cs 由调用方用 retro_gpio 管理（machine 习惯） */
int retro_bus_spi_init(int id, int mosi, int miso, int sck, int freq_hz);

/* UART：8N1 原始模式（无回显、无流控） */
int retro_bus_uart_init(int id, int tx, int rx, int baud);

/*==== I2C 传输 ====*/

/* 地址探测回调（返回非 0 中止扫描）；返回命中数量 */
int retro_bus_i2c_scan(int id, int (*cb)(int addr, void *arg), void *arg);

int retro_bus_i2c_write(int id, uint16_t addr,
                        const uint8_t *buf, int len);
int retro_bus_i2c_read(int id, uint16_t addr, uint8_t *buf, int len);

/* 8 位寄存器子地址便捷接口（writeto_mem / readfrom_mem） */
int retro_bus_i2c_write_reg8(int id, uint16_t addr,
                             uint8_t reg, uint8_t val);
int retro_bus_i2c_read_regs8(int id, uint16_t addr,
                             uint8_t reg, uint8_t *buf, int len);

/*==== SPI 传输 ====*/

/* 全双工：tx/rx 任一可为 NULL（写/读单边）；len 字节数 */
int retro_bus_spi_xfer(int id, const uint8_t *tx, uint8_t *rx, int len);

/*==== UART 读写 ====*/

int retro_bus_uart_write(int id, const void *buf, int len);
/* timeout_ms<0 阻塞读满；=0 立即返回现有；>0 限时 */
int retro_bus_uart_read(int id, void *buf, int len, int timeout_ms);
int retro_bus_uart_available(int id);

/*==== 后端自检（sysinfo 展示用） ====*/

const char *retro_bus_i2c_backend(int id);
const char *retro_bus_spi_backend(int id);

#endif /* __RETRO_BUS_H */
