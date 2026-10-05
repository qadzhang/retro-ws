/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ws2812_rmt.c - WS2812 RGB LED 硬件 RMT 驱动
 *
 * WHAT : 经 NuttX RMT 字符设备（/dev/rmt0）驱动 DevKitC-1 板载 WS2812
 * WHY  : WS2812 单线协议时序 ns 级，gpio 直驱点不亮（HARDWARE.md 2.8），
 *        RMT 外设硬件生成波形，CPU 零参与——"真外设不软模拟"规范 13.1
 * WHO  : ble_hid.c（蓝牙状态）、esp32s3_retro.c（开机自检）、宿主测试
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/driver/ws2812_rmt.c
 * WHEN : 2026-10-04 晚新增
 * HOW  : 编码 24-bit GRB 为 RMT 符号字（每 bit 两相：高电平相在前，
 *        bit15=第一相电平；第二相电平位=0），尾附复位低电平符号；
 *        open 一次常驻，set 时 write 整帧（RMT DMA 硬件发出）
 *        时序基准：RMT 通道时钟 80MHz，1 tick = 12.5ns
 */

#include <nuttx/config.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include "ws2812_rmt.h"

/*==========================
 *  时序常量（tick @80MHz，12.5ns/tick）
 *==========================*/

/* WS2812B 数据手册窗口中心值（ns -> tick 四舍五入） */
#define WS2812_T0H_TICKS       28    /* 350ns  高电平，bit=0 */
#define WS2812_T0L_TICKS       72    /* 900ns  低电平，bit=0 */
#define WS2812_T1H_TICKS       72    /* 900ns  高电平，bit=1 */
#define WS2812_T1L_TICKS       28    /* 350ns  低电平，bit=1 */

/* 复位（latch）>50us 低电平：两相各 4000 tick = 100us，留裕量 */
#define WS2812_RESET_TICKS     4000

/* 符号字 bit15/bit31 = 电平（高 16 位是第二相） */
#define RMT_LEVEL_BIT          0x8000u

#define WS2812_RMT_DEFAULT_DEV "/dev/rmt0"

/*==========================
 *  私有数据
 *==========================*/

static int g_ws2812_fd = -1;

/*==========================
 *  纯编码（宿主可测）
 *==========================*/

uint32_t ws2812_rmt_encode_bit(bool bit)
{
    /* 第一相（低 16 位）= 高电平相，第二相（高 16 位）= 低电平相 */
    if (bit)
        return ((uint32_t)WS2812_T1L_TICKS << 16) |
               (RMT_LEVEL_BIT | WS2812_T1H_TICKS);

    return ((uint32_t)WS2812_T0L_TICKS << 16) |
           (RMT_LEVEL_BIT | WS2812_T0H_TICKS);
}

static int encode_byte_msb(uint8_t byte, uint32_t *dst)
{
    int n = 0;
    uint8_t mask = 0x80;

    while (mask != 0) {
        dst[n++] = ws2812_rmt_encode_bit((byte & mask) != 0);
        mask >>= 1;
    }

    return n;
}

int ws2812_rmt_encode_rgb(uint8_t r, uint8_t g, uint8_t b,
                          uint32_t *words, int max_words)
{
    int n = 0;

    if (words == NULL || max_words < WS2812_WORDS_TOTAL)
        return -EINVAL;

    /* WS2812 帧序：G -> R -> B（高位在前） */
    n += encode_byte_msb(g, words + n);
    n += encode_byte_msb(r, words + n);
    n += encode_byte_msb(b, words + n);

    /* 复位符号：两相全低电平 */
    words[n++] = (uint32_t)WS2812_RESET_TICKS << 16 | WS2812_RESET_TICKS;

    return n;
}

/*==========================
 *  设备操作
 *==========================*/

int ws2812_rmt_init(const char *devpath)
{
    if (g_ws2812_fd >= 0)
        return OK;

    g_ws2812_fd = open(devpath != NULL ? devpath : WS2812_RMT_DEFAULT_DEV,
                       O_WRONLY);
    if (g_ws2812_fd < 0) {
        syslog(LOG_ERR, "[WS2812] open %s failed: %d\n",
               devpath != NULL ? devpath : WS2812_RMT_DEFAULT_DEV,
               errno);
        return -errno;
    }

    /* 上电先灭灯（全 0 帧也会被 RMT 发出，形成确定初始态） */
    ws2812_rmt_set_rgb(0, 0, 0);

    syslog(LOG_INFO, "[WS2812] RMT channel ready\n");
    return OK;
}

int ws2812_rmt_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t words[WS2812_WORDS_TOTAL];
    int n;
    ssize_t ret;

    if (g_ws2812_fd < 0)
        return -ENODEV;

    n = ws2812_rmt_encode_rgb(r, g, b, words, WS2812_WORDS_TOTAL);
    if (n < 0)
        return n;

    ret = write(g_ws2812_fd, words, (size_t)n * sizeof(uint32_t));
    if (ret < 0)
        return -errno;

    return OK;
}

void ws2812_rmt_deinit(void)
{
    if (g_ws2812_fd >= 0) {
        close(g_ws2812_fd);
        g_ws2812_fd = -1;
    }
}
