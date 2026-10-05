/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * port_dma.c - Pico-PIO-USB 垫片：pico-sdk DMA 通道语义实现
 *
 * WHAT : hardware/dma.h 声明的 4 个通道函数 + 认领（寄存器直写）
 * WHY  : NuttX rp2040 DMAC 是框架式（rp2040_dmac.c 面向驱动注册），
 *        无 pico-sdk 裸通道 API；上游 TX 路径只需要"配置一次 +
 *        每包写 READ_ADDR/COUNT 立即启动"的语义（位域见 dma.h）
 * WHO  : Retro WS Project Team
 * WHERE: retro-ws/src/nuttx/rp2040/driver/input/pio_usb/port/port_dma.c
 * WHEN : 2026-10-05 新增
 * HOW  : set_config 写 CTRL（不触发）；transfer_from_buffer_now 走
 *        WRITE_ADDR + READ_ADDR + AL1_TRANS_TRIG（写 COUNT_TRIG 启动，
 *        与 pico-sdk 相同的"最后一次写触发"顺序）
 */

#include <stdint.h>
#include <stdbool.h>

#include "hardware/dma.h"

void piousb_port_dma_claim(uint32_t ch)
{
    /* 清残留：中止在途传输并关通道使能（独占使用，无并发认领协商） */
    PIOUSB_DMA_ABORT(ch) = 1u;
    while ((PIOUSB_DMA_ABORT(ch) & 1u) != 0)
        ;
    PIOUSB_DMA_CH_CTRL_TRIG(ch) = 0;
}

void dma_channel_set_config(uint32_t channel, const dma_channel_config *config,
                            bool trigger)
{
    if (trigger)
        PIOUSB_DMA_CH_CTRL_TRIG(channel) = *config;
    else
        PIOUSB_DMA_CH_AL1_CTRL(channel) = *config;
}

void dma_channel_set_write_addr(uint32_t channel, volatile void *write_addr,
                                bool trigger)
{
    if (trigger)
        PIOUSB_DMA_CH_WRITE_ADDR(channel) = (uint32_t)(uintptr_t)write_addr;
    else
        /* 非触发写即 WRITE_ADDR 本体（AL1 序列寄存器 0x04 同址） */
        PIOUSB_DMA_CH_WRITE_ADDR(channel) = (uint32_t)(uintptr_t)write_addr;
}

void dma_channel_transfer_from_buffer_now(uint32_t channel,
                                          const volatile void *read_addr,
                                          uint32_t transfer_count)
{
    /* pico-sdk 顺序：WRITE_ADDR -> READ_ADDR -> TRANS_COUNT（触发启动） */
    PIOUSB_DMA_CH_READ_ADDR(channel) = (uint32_t)(uintptr_t)read_addr;
    PIOUSB_DMA_CH_TRANS_COUNT(channel) = transfer_count;
}
