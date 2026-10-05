/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_cvbs_dac.c - ESP32(CAM) CVBS 硬件层（内置 DAC + 整帧 DMA 环）
 *
 * WHAT : ESP32 目标的 CVBS 全帧输出（覆盖 common 的 weak 符号）
 * WHY  : ESP32 内置 8bit DAC（DAC1=GPIO25），I2S0 LCD/DAC 模式 DMA
 *        直出复合视频（HARDWARE.md 5.3/6.2）；2026-10-04(晚) 从
 *        两行乒乓升级为整帧环形链（消除乒乓撕裂），时钟改整除
 * WHO  : common/driver/drv_cvbs.c 的 cvbs_core_generate_frame()
 * WHERE: esp32-retro-ws/src/nuttx/esp32/driver/cvbs/drv_cvbs_dac.c
 * WHEN : 2026-03 初版；2026-10-04 重写；同日(晚) 整帧环 + 853 行
 * HOW  : APB 80MHz 整数 ÷6 = 13.3333MHz（无小数抖动），
 *        行长 853 样本（12/63/68/710）→ 行 63.975µs（-0.04%）；
 *        帧缓冲 = 625 行 × 853 样本 × 2 字节（16-bit 槽位，DAC 取
 *        高 8 位）≈ 1.07MB 放 PSRAM；DMA 描述符环永续输出，
 *        emit_line 原地更新（25Hz，单行内撕裂不可见）
 * 注意 : 描述符 size 字：[11:0]=长度 bit30=suc_eof bit31=owner(DMA=1)，
 *        旧实现漏 owner 位（DMA 链无法交接）
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <nuttx/arch.h>
#include "esp32.h"
#include "board.h"

#include "driver/cvbs_core.h"
#include "driver/drv_cvbs.h"

#ifdef CONFIG_RETRO_CVBS_DAC

/* 853 样本行（13.3333MHz，HARDWARE.md 6.2） */
#define CVBS_CAM_LINE_FRONT    12
#define CVBS_CAM_LINE_SYNC     63
#define CVBS_CAM_LINE_BACK     68
#define CVBS_CAM_LINE_ACTIVE   710
#define CVBS_CAM_LINE_TOTAL    853

#define CVBS_FRAME_LINES       625
#define CVBS_FRAME_BYTES       (CVBS_FRAME_LINES * CVBS_CAM_LINE_TOTAL * 2)

/* APB 80MHz ÷ 6 = 13.3333MHz 整数分频 */
#define APB_CLK_HZ             80000000
#define PIXEL_CLK_HZ           13333333
#define CLKM_DIV               6

/* DMA 描述符（经典 ESP32 I2S 链表格式） */
struct esp32_dma_desc_s {
    uint32_t size;      /* [11:0]长度 | bit30 EOF | bit31 owner */
    uint32_t length;    /* 有效字节数 */
    uint32_t buf;       /* 缓冲地址 */
    uint32_t next;      /* 下一个描述符地址 */
};

#define DESC_OWNER_DMA         (1u << 31)
#define DESC_EOF               (1u << 30)

#define I2S_CONF2_LCD_EN       (1u << 5)

static uint16_t *g_frame = NULL;        /* 16-bit 样本帧缓冲（PSRAM） */
static struct esp32_dma_desc_s *g_desc = NULL;
static int g_desc_count = 0;
static bool g_hw_started = false;

/*
 * WHAT : 构建覆盖帧缓冲的环形描述符链
 * HOW  : ≤4078B/描述符（size 域 12-bit），末描述符回链首
 */
static int frame_ring_build(void)
{
    size_t chunk = 4078 & ~3;
    size_t total = CVBS_FRAME_BYTES;
    int n = (int)((total + chunk - 1) / chunk);

    g_desc = malloc((size_t)n * sizeof(*g_desc));
    if (g_desc == NULL)
        return -ENOMEM;

    size_t off = 0;
    for (int i = 0; i < n; i++) {
        size_t len = total - off;
        if (len > chunk)
            len = chunk;

        g_desc[i].size = (uint32_t)len | DESC_OWNER_DMA;
        if (i == n - 1)
            g_desc[i].size |= DESC_EOF;
        g_desc[i].length = (uint32_t)len;
        g_desc[i].buf = (uint32_t)(uintptr_t)((uint8_t *)g_frame + off);
        g_desc[i].next = (uint32_t)(uintptr_t)&g_desc[(i + 1) % n];
        off += len;
    }

    g_desc_count = n;
    return OK;
}

/*
 * WHAT : 配置 I2S0 为 DAC 直出模式并起 DMA 环
 * HOW  : 复位 -> 整除时钟 -> 16-bit 单通道 -> DAC 模式位 ->
 *        挂描述符环并置 OUTLINK_START（关键：不置位 DMA 永不启动）
 */
static void hw_start(void)
{
    uint32_t base = ESP32_I2S0_BASE;

    g_hw_started = true;

    cvbs_core_set_line_layout(CVBS_CAM_LINE_FRONT, CVBS_CAM_LINE_SYNC,
                              CVBS_CAM_LINE_BACK, CVBS_CAM_LINE_ACTIVE);

    /* 帧缓冲（1.07MB → PSRAM 堆） */
    g_frame = memalign(32, CVBS_FRAME_BYTES);
    if (g_frame == NULL) {
        syslog(LOG_ERR, "[cvbs-cam] frame alloc failed\n");
        return;
    }

    /* 全帧预填消隐（16-bit 槽：低高字节同值，DAC 取高 8 位） */
    for (int i = 0; i < CVBS_FRAME_LINES * CVBS_CAM_LINE_TOTAL; i++)
        g_frame[i] = (uint16_t)((CVBS_LEVEL_BLANK << 8) | CVBS_LEVEL_BLANK);

    if (frame_ring_build() != OK)
        return;

    /* 1. 复位 I2S */
    putreg32(ESP32_I2S_TX_RESET | ESP32_I2S_RX_RESET |
             ESP32_I2S_TX_FIFO_RESET | ESP32_I2S_RX_FIFO_RESET,
             base + ESP32_I2S_CONF_REG(0));
    putreg32(0, base + ESP32_I2S_CONF_REG(0));

    /* 2. 时钟：APB 80MHz ÷ 6 = 13.3333MHz（整除零抖动） */
    putreg32(CLKM_DIV - 1, base + ESP32_I2S_CLKM_CONF_REG(0));

    /* 3. 采样：16bit 单通道（DAC 取每 16-bit 槽高 8 位） */
    putreg32(0x00010010, base + ESP32_I2S_SAMPLE_RATE_CONF_REG(0));
    putreg32(0x00000001, base + ESP32_I2S_FIFO_CONF_REG(0));

    /* 4. DAC 模式（ESP32 独有）：DAC1 = GPIO25（右声道） */
    putreg32(ESP32_I2S_DAC_MODE_EN | ESP32_I2S_DAC_RIGHT_ENA |
             ESP32_I2S_LCD_EN | ESP32_I2S_TX_RIGHT_FIRST,
             base + ESP32_I2S_CONF_REG(0));

    /* 5. LCD/DAC 输出时钟使能 */
    putreg32(I2S_CONF2_LCD_EN, base + ESP32_I2S_CONF2_REG(0));

    /* 6. DMA 环 + 启动 */
    putreg32((((uint32_t)(uintptr_t)&g_desc[0]) &
              ESP32_I2S_OUTLINK_ADDR_MASK) | ESP32_I2S_OUTLINK_START,
             base + ESP32_I2S_OUT_LINK_REG(0));

    /* 7. 启动发送 */
    putreg32(ESP32_I2S_TX_START, base + ESP32_I2S_CONF_REG(0));

    syslog(LOG_INFO, "[cvbs-cam] DAC1 GPIO25 engaged: %dHz, %d-sample "
           "lines, ring %d desc\n",
           PIXEL_CLK_HZ, CVBS_CAM_LINE_TOTAL, g_desc_count);
}

/*
 * WHAT : 硬件行输出钩子 / line sink -> 帧缓冲槽位
 * HOW  : 8-bit 样本展开 16-bit（高低字节同值）；行号 0..624 直映射
 */
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    if (line_no < 0 || line_no >= CVBS_FRAME_LINES || g_frame == NULL)
        return;

    if (!g_hw_started)
        hw_start();

    if (g_frame == NULL)
        return;

    if (len > CVBS_CAM_LINE_TOTAL)
        len = CVBS_CAM_LINE_TOTAL;

    uint16_t *dst = g_frame + (size_t)line_no * CVBS_CAM_LINE_TOTAL;
    for (size_t i = 0; i < len; i++)
        dst[i] = (uint16_t)((line[i] << 8) | line[i]);
}

/*
 * WHAT : 兼容别名：原启动代码调用 cvbs_init()
 * HOW  : 转发统一驱动入口
 */
int cvbs_init(void)
{
    return drv_cvbs_init();
}

#endif /* CONFIG_RETRO_CVBS_DAC */
