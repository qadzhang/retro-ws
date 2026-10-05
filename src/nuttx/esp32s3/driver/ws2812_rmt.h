/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ws2812_rmt.h - WS2812 RGB LED 硬件 RMT 驱动接口
 *
 * WHAT : DevKitC-1 板载 WS2812（单线可寻址协议）的驱动接口
 * WHY  : WS2812 时序（ns 级）无法用 gpio_set_level 直驱，必须走
 *        RMT 硬件外设编码（HARDWARE.md 2.8 节，2026-10-04 晚定稿）；
 *        本头同时导出纯编码函数供宿主测试直链（tests/host）
 * WHO  : ble_hid.c（蓝牙状态指示）、esp32s3_retro.c（开机自检灯）
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/ws2812_rmt.h
 * WHEN : 2026-10-04 晚新增（替代 ble_hid.c 里点不亮的 gpio 直驱桩）
 * HOW  : open("/dev/rmt0") -> 24-bit GRB 编码成 RMT 符号字 -> write；
 *        /dev/rmt0 由 NuttX 树内 RMT 驱动链注册
 *        （CONFIG_RMT + CONFIG_RMTCHAR + CONFIG_ESP_RMT ->
 *         board_rmt_txinitialize(0, GPIO38/48) -> rmtchar）
 */

#ifndef __WS2812_RMT_H
#define __WS2812_RMT_H

#include <stdint.h>
#include <stdbool.h>

/* 一颗 LED = 24 bit（G8 + R8 + B8），每 bit 一个 RMT 符号字 */
#define WS2812_WORDS_PER_LED     24

/* 编码缓冲总长：LED 符号 + 复位符号（复位期电平恒低） */
#define WS2812_WORDS_TOTAL       (WS2812_WORDS_PER_LED + 1)

/*
 * WHAT : 初始化 WS2812（打开 RMT 发送通道字符设备）
 * WHY  : DevKitC-1 板级 bringup 已把 RMT TX 通道 0 绑到 WS2812 引脚
 * HOW  : devpath 传 NULL 时默认 "/dev/rmt0"
 *   devpath - RMT 字符设备路径（NULL = "/dev/rmt0"）
 *   返回    - OK / -ENOENT（设备不存在，检查 CONFIG_ESP_RMT）
 */
int ws2812_rmt_init(const char *devpath);

/*
 * WHAT : 设置 LED 颜色并立即发送（阻塞到 DMA 完成）
 * WHY  : 蓝牙状态指示复用蓝色通道（灭=已连接/慢闪=配对/快闪=扫描）
 * HOW  : GRB 顺序编码 + 复位符号，一次 write 完成
 *   r/g/b - 0-255
 *   返回  - OK / -ENODEV（未 init）/ 负错误码
 */
int ws2812_rmt_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/*
 * WHAT : 关闭设备（LED 熄灭）
 */
void ws2812_rmt_deinit(void);

/*==========================
 *  纯编码函数（无 NuttX 依赖，宿主测试直链）
 *==========================*/

/*
 * WHAT : 单 bit -> RMT 符号字（32-bit：低 16 相位在前）
 * HOW  : bit=1 -> 高电平 900ns(72 tick) + 低 350ns(28)
 *        bit=0 -> 高电平 350ns(28) + 低 900ns(72)
 *        通道时钟 80MHz（1 tick = 12.5ns，同上游 ws2812esp32rmt 例程）
 */
uint32_t ws2812_rmt_encode_bit(bool bit);

/*
 * WHAT : RGB 三字节 -> RMT 符号字序列（GRB 顺序 + 复位符号）
 * HOW  : words 缓冲至少 WS2812_WORDS_TOTAL 字
 *   返回 - 写入的字数（含复位符号）/ -EINVAL（缓冲不足）
 */
int ws2812_rmt_encode_rgb(uint8_t r, uint8_t g, uint8_t b,
                          uint32_t *words, int max_words);

#endif /* __WS2812_RMT_H */
