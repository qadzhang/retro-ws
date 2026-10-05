/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/dma.h - Pico-PIO-USB 的 NuttX 垫片（hardware/dma.h 替身）
 *
 * WHAT : 上游只用 4 个 pico-sdk DMA 调用（默认配置/设配置/写地址/
 *        从缓冲立即传输），PIO-USB TX 通道用它把预编码 NRZI 数据
 *        以 PIO TXF DREQ 节流推出。NuttX rp2040 是框架式 DMAC 无
 *        裸通道 API——本垫片以 DMA 控制器寄存器直写实现同语义
 *        （布局 = RP2040 datasheet 2.5，通道寄存器步进 0x40）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_DMA_H
#define __PIOUSB_PORT_HW_DMA_H

#include <stdint.h>
#include <stdbool.h>

/* DMA 基址与通道寄存器（datasheet 2.5.2；通道块步进 0x40） */
#define PIOUSB_DMA_BASE 0x50000000u

#define PIOUSB_DMA_CH_REG(ch, off) \
    (*(volatile uint32_t *)(PIOUSB_DMA_BASE + (off) + (ch) * 0x40u))

#define PIOUSB_DMA_CH_READ_ADDR(ch)       PIOUSB_DMA_CH_REG(ch, 0x00)
#define PIOUSB_DMA_CH_WRITE_ADDR(ch)      PIOUSB_DMA_CH_REG(ch, 0x04)
#define PIOUSB_DMA_CH_TRANS_COUNT(ch)     PIOUSB_DMA_CH_REG(ch, 0x08)
#define PIOUSB_DMA_CH_CTRL_TRIG(ch)       PIOUSB_DMA_CH_REG(ch, 0x0c)
#define PIOUSB_DMA_CH_AL1_CTRL(ch)        PIOUSB_DMA_CH_REG(ch, 0x10)
#define PIOUSB_DMA_CH_AL1_TRANS_TRIG(ch)  PIOUSB_DMA_CH_REG(ch, 0x28)
#define PIOUSB_DMA_ABORT(ch)              PIOUSB_DMA_CH_REG(ch, 0x34)

/* CTRL_TRIG 位域（datasheet 表 110） */
#define PIOUSB_DMA_EN                     (1u << 0)
#define PIOUSB_DMA_HIGH_PRIORITY          (1u << 1)
#define PIOUSB_DMA_DATA_SIZE_SHIFT        2
#define PIOUSB_DMA_DATA_SIZE_32           (2u << PIOUSB_DMA_DATA_SIZE_SHIFT)
#define PIOUSB_DMA_INCR_READ              (1u << 4)
#define PIOUSB_DMA_INCR_WRITE             (1u << 5)
#define PIOUSB_DMA_TREQ_SEL_SHIFT         15
#define PIOUSB_DMA_CHAIN_TO_SHIFT         11

/* pico-sdk 传输宽度枚举（CTRL DATA_SIZE 域值） */
#define DMA_SIZE_8  0
#define DMA_SIZE_16 1
#define DMA_SIZE_32 2

typedef uint32_t dma_channel_config;

/* pico-sdk channel_config 系列 setter：直接拼 CTRL 位域 */
static inline void channel_config_set_read_increment(dma_channel_config *c,
                                                     bool incr)
{
    *c = (*c & ~PIOUSB_DMA_INCR_READ) | (incr ? PIOUSB_DMA_INCR_READ : 0);
}

static inline void channel_config_set_write_increment(dma_channel_config *c,
                                                      bool incr)
{
    *c = (*c & ~PIOUSB_DMA_INCR_WRITE) | (incr ? PIOUSB_DMA_INCR_WRITE : 0);
}

static inline void channel_config_set_transfer_data_size(
    dma_channel_config *c, uint32_t size)
{
    *c = (*c & ~(3u << PIOUSB_DMA_DATA_SIZE_SHIFT)) |
         ((size & 3u) << PIOUSB_DMA_DATA_SIZE_SHIFT);
}

static inline void channel_config_set_dreq(dma_channel_config *c, uint32_t dreq)
{
    *c = (*c & ~(0x3fu << PIOUSB_DMA_TREQ_SEL_SHIFT)) |
         ((dreq & 0x3fu) << PIOUSB_DMA_TREQ_SEL_SHIFT);
}

/* 通道批量认领：掩码对应通道（本集成独占，只清使能位） */
static inline void dma_claim_mask(uint32_t ch_mask)
{
    for (uint32_t ch = 0; ch < 12; ch++)
    {
        if (ch_mask & (1u << ch))
        {
            PIOUSB_DMA_ABORT(ch) = 1u;
            while ((PIOUSB_DMA_ABORT(ch) & 1u) != 0)
                ;
            PIOUSB_DMA_CH_CTRL_TRIG(ch) = 0;
        }
    }
}

/* pico-sdk dma_channel_config 已前移定义 */

/* 通道认领：驱动独占固定通道，认领仅做软件标志 + 清残留使能 */
void piousb_port_dma_claim(uint32_t ch);

static inline dma_channel_config dma_channel_get_default_config(uint32_t ch)
{
    (void)ch;
    return 0;
}

void dma_channel_set_config(uint32_t channel, const dma_channel_config *config,
                            bool trigger);

void dma_channel_set_write_addr(uint32_t channel, volatile void *write_addr,
                                bool trigger);

void dma_channel_transfer_from_buffer_now(uint32_t channel,
                                          const volatile void *read_addr,
                                          uint32_t transfer_count);

#endif /* __PIOUSB_PORT_HW_DMA_H */
