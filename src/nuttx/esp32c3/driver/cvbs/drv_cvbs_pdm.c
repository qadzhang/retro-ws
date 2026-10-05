/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_cvbs_pdm.c - ESP32-C3 CVBS 硬件层（I2S0 PDM-TX raw + GDMA）
 *
 * WHAT : C3 目标的 CVBS 1-bit 流输出（覆盖 common 的 weak 符号）
 * WHY  : C3 无内置 DAC、无 LCD_CAM 并行口（HARDWARE.md 3A.2A，
 *        2026-10-04 定稿）——唯一能 DMA 直推高速流的口是 I2S0
 *        PDM TX raw 模式：DMA 供 16-bit 字，硬件按 PDM 时钟逐位
 *        直出（pcm2pdm 滤波旁路），单脚 sigma-delta DAC
 * WHO  : common/driver/drv_cvbs.c 的 cvbs_core_generate_frame()
 * WHERE: esp32-retro-ws/src/nuttx/esp32c3/driver/cvbs/drv_cvbs_pdm.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 亮度 8-bit 样本 --一阶 sigma-delta--> 1-bit 流：
 *          acc += level;  bit = (acc >= 128);  acc -= bit ? 128 : 0
 *        （误差项跨行连续，同步段全 0、白峰近全 1，RC 滤波还原）
 *        时钟链（全整数，无小数抖动）：
 *          PLL_F160M 160MHz /4 = fi2s 40MHz；Fpdm = fi2s×fp/fs
 *          = 40MHz×170/510 = 13.3333MHz 引脚速率
 *          DMA 供数 = Fpdm/16 = 833.33k 16-bit 字/秒（GDMA 环形链）
 *        场环 = 313 行 × 853 样本 = 266989 bit ≈ 32.6KB（SRAM）
 *        → 50Hz 240p 字符控制台；更新路径 = 原地重调制（tearing
 *        限于单行，字符场景不可见）
 * 注意 : 位序按 16-bit 字 MSB-first（classic ESP32 PDM 语义）；
 *        首板联调时用示波器核对同步沿宽度（4.7µs≈63 bit），
 *        若位序相反改 BIT_MSB_FIRST 宏即可
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

/* IO 原语（arch 通用；apps 包含路径下个别板缺声明，此处兜底） */
extern void putreg32(uint32_t value, uintptr_t address);
extern uint32_t getreg32(uintptr_t address);
extern void modifyreg32(uintptr_t address, uint32_t clearbits,
                        uint32_t setbits);

#include "driver/cvbs_core.h"
#include "driver/drv_cvbs.h"

#ifdef CONFIG_RETRO_AV_CONSOLE

/*==========================
 *  寄存器（esp-hal-3rdparty esp32c3 i2s_struct.h / gdma_reg.h）
 *==========================*/

#define I2S0_BASE               0x6002d000
#define I2S_INT_RAW_REG         (I2S0_BASE + 0x0c)
#define I2S_INT_CLR_REG         (I2S0_BASE + 0x18)
#define I2S_TX_CONF_REG         (I2S0_BASE + 0x24)
#define I2S_TX_CONF1_REG        (I2S0_BASE + 0x2c)
#define I2S_TX_CLKM_CONF_REG    (I2S0_BASE + 0x34)
#define I2S_TX_PCM2PDM_CONF_REG (I2S0_BASE + 0x40)
#define I2S_TX_PCM2PDM_CONF1_REG (I2S0_BASE + 0x44)

/* tx_conf 位域 */
#define I2S_TX_RESET            (1u << 0)
#define I2S_TX_FIFO_RESET       (1u << 1)
#define I2S_TX_START            (1u << 2)
#define I2S_TX_MONO             (1u << 5)
#define I2S_TX_UPDATE           (1u << 8)
#define I2S_TX_PDM_EN           (1u << 20)

/* tx_conf1 位域（字段基 S：ws_width0/bck_div7/bits_mod12/half18） */
#define I2S_TX_BCK_DIV_NUM_S    7
#define I2S_TX_BITS_MOD_S       12
#define I2S_TX_HALF_SAMPLE_S    18

/* tx_clkm_conf 位域 */
#define I2S_TX_CLKM_DIV_NUM_S   0
#define I2S_TX_CLK_ACTIVE       (1u << 26)
#define I2S_TX_CLK_SEL_S        27    /* 0=XTAL 1=PLL240M 2=PLL160M 3=MCLK_in */
#define I2S_CLK_EN              (1u << 29)

/* tx_pcm2pdm_conf 位域 */
#define I2S_PDM_PRESCALE_S      9    /* 8-bit */
#define I2S_PDM_DAC_MODE_EN     (1u << 24)
#define I2S_PCM2PDM_CONV_EN     (1u << 25)

/* tx_pcm2pdm_conf1 位域 */
#define I2S_PDM_FS_S            10   /* 10-bit */
#define I2S_PDM_FP_S            0    /* 10-bit */

/* GPIO 矩阵信号（gpio_sig_map.h）：I2S PDM 数据出 */
#define I2SO_SD_OUT_IDX         15

/*==========================
 *  CVBS 常数（HARDWARE.md 3A.2A / 6.2）
 *==========================*/

#define CVBS_PDM_PIN            1     /* GPIO1：PDM 数据 → RC → 75Ω */
#define CVBS_CLK_SRC_HZ         160000000
#define CVBS_I2S_DIV_NUM        3     /* fi2s = 160/(3+1) = 40MHz */
#define CVBS_PDM_FP             170   /* Fpdm = 40MHz×170/510 */
#define CVBS_PDM_FS             510
#define CVBS_PDM_PIN_RATE_HZ    13333333

/* 853 样本行（13.3333MHz，行 63.975µs） */
#define CVBS_C3_LINE_FRONT      12
#define CVBS_C3_LINE_SYNC       63
#define CVBS_C3_LINE_BACK       68
#define CVBS_C3_LINE_ACTIVE     710
#define CVBS_C3_LINE_TOTAL      853

/* 场环：313 行（field(false) 312 + 1 补行）= 20.03ms ≈ 50Hz 240p */
#define CVBS_RING_LINES         313
#define CVBS_RING_BITS          ((size_t)CVBS_RING_LINES * CVBS_C3_LINE_TOTAL)
#define CVBS_RING_BYTES         ((CVBS_RING_BITS + 7) / 8)

/* sigma-delta 满量程（8-bit 样本调制到 1-bit） */
#define SD_THRESHOLD            128

/*==========================
 *  NuttX common/espressif 驱动接口（apps 包含路径外，extern 声明）
 *==========================*/

struct esp_dmadesc_s {
    uint32_t ctrl;                    /* bit31=OWN bit30=EOF size/len 域 */
    const uint8_t *pbuf;              /* DMA 缓冲地址 */
    struct esp_dmadesc_s *next;       /* 下一描述符 */
};

enum esp_dma_periph_e {
    ESPRESSIF_DMA_PERIPH_M2M,
    ESPRESSIF_DMA_PERIPH_UHCI,
    ESPRESSIF_DMA_PERIPH_SPI,
    ESPRESSIF_DMA_PERIPH_I2S,
};

extern int32_t esp_dma_request(enum esp_dma_periph_e periph,
                               uint32_t tx_prio, uint32_t rx_prio,
                               bool burst_en);
extern void esp_dma_load(struct esp_dmadesc_s *dmadesc, int chan, bool tx);
extern void esp_dma_enable(int chan, bool tx);
extern void esp_gpio_matrix_out(uint32_t pin, uint32_t signal_idx,
                                bool out_inv, bool oen_inv);

#define ESP_DMA_CTRL_OWN          (1u << 31)
#define ESP_DMA_CTRL_EOF          (1u << 30)
#define ESP_DMA_CTRL_DATALEN_S    (12)
#define ESP_DMA_CTRL_BUFLEN_V     (0xfff)

/*==========================
 *  状态
 *==========================*/

static uint8_t *g_ring = NULL;          /* 1-bit 场环（SRAM） */
static struct esp_dmadesc_s *g_desc = NULL;
static int g_dma_chan = -1;
static bool g_hw_started = false;
static int g_sd_acc = 0;                /* sigma-delta 误差累积 */

/*==========================
 *  位流写入
 *==========================*/

/*
 * WHAT : 把一个样本调制成的位写入场环指定位偏移
 * HOW  : MSB-first 位序（按 16-bit 字边界消费时与 classic PDM 一致）；
 *        位偏移 = 行号×853 + 样本号（行不对齐字节，流连续）
 */
static inline void ring_write_bit(size_t bit_off, int bit)
{
    if (bit_off >= CVBS_RING_BITS)
        return;

    uint8_t *byte = &g_ring[bit_off >> 3];
    uint8_t mask = (uint8_t)(1u << (7 - (bit_off & 7)));

    if (bit)
        *byte |= mask;
    else
        *byte &= (uint8_t)~mask;
}

/*
 * WHAT : 一行 8-bit 样本 → 1-bit 调制进场环
 * HOW  : 一阶 sigma-delta：acc += level; bit = acc>=128; acc&=127
 */
static void ring_modulate_line(int line_no, const uint8_t *line, size_t len)
{
    size_t base = (size_t)(line_no % CVBS_RING_LINES) * CVBS_C3_LINE_TOTAL;

    for (size_t i = 0; i < len && i < CVBS_C3_LINE_TOTAL; i++) {
        g_sd_acc += line[i];
        int bit = g_sd_acc >= SD_THRESHOLD;
        if (bit)
            g_sd_acc -= SD_THRESHOLD;
        ring_write_bit(base + i, bit);
    }
}

/*
 * WHAT : 构建覆盖场环的 GDMA 环形描述符链
 * HOW  : ≤4092B/描述符，末描述符回链首（永续流），EOF 置帧末作心跳
 */
static int ring_desc_build(void)
{
    size_t chunk = 4092 & ~3;
    size_t total = CVBS_RING_BYTES;
    int n = (int)((total + chunk - 1) / chunk);

    g_desc = malloc((size_t)n * sizeof(*g_desc));
    if (g_desc == NULL)
        return -ENOMEM;

    size_t off = 0;
    for (int i = 0; i < n; i++) {
        size_t len = total - off;
        if (len > chunk)
            len = chunk;

        g_desc[i].ctrl = ESP_DMA_CTRL_OWN |
                         ((uint32_t)len << ESP_DMA_CTRL_DATALEN_S) |
                         ((uint32_t)len & ESP_DMA_CTRL_BUFLEN_V);
        if (i == n - 1)
            g_desc[i].ctrl |= ESP_DMA_CTRL_EOF;
        g_desc[i].pbuf = g_ring + off;
        g_desc[i].next = &g_desc[(i + 1) % n];
        off += len;
    }
    return OK;
}

/*
 * WHAT : 硬件启动（一次）/ engage I2S0 PDM TX + GDMA
 * HOW  : 场环先填"全消隐流"（level16 → 1/16 占空比位型由调制器
 *        生成），再配 I2S PDM raw、路由 GPIO、起 DMA 环
 */
static void hw_start(void)
{
    g_hw_started = true;

    cvbs_core_set_line_layout(CVBS_C3_LINE_FRONT, CVBS_C3_LINE_SYNC,
                              CVBS_C3_LINE_BACK, CVBS_C3_LINE_ACTIVE);

    g_ring = memalign(32, CVBS_RING_BYTES);
    if (g_ring == NULL) {
        syslog(LOG_ERR, "[cvbs-c3] ring alloc failed\n");
        return;
    }

    /* 预调一帧消隐（同步/消隐时序立即可锁，内容随后原地更新） */
    {
        uint8_t blank_line[CVBS_LINE_TOTAL];
        cvbs_core_encode_line(blank_line, NULL, 0);
        g_sd_acc = 0;
        for (int n = 0; n < CVBS_RING_LINES; n++) {
            if (cvbs_core_line_kind(n, false, true) == CVBS_LINE_EQ)
                cvbs_core_encode_equalizing(blank_line);
            else if (cvbs_core_line_kind(n, false, true) == CVBS_LINE_BROAD)
                cvbs_core_encode_broad_pulse(blank_line);
            ring_modulate_line(n, blank_line, cvbs_core_line_total());
        }
    }

    g_dma_chan = esp_dma_request(ESPRESSIF_DMA_PERIPH_I2S, 5, 5, false);
    if (g_dma_chan < 0) {
        syslog(LOG_ERR, "[cvbs-c3] GDMA request failed\n");
        return;
    }

    if (ring_desc_build() != OK)
        return;

    /* I2S0：复位发送通道 */
    modifyreg32(I2S_TX_CONF_REG, 0, I2S_TX_RESET | I2S_TX_FIFO_RESET);
    modifyreg32(I2S_TX_CONF_REG, I2S_TX_RESET | I2S_TX_FIFO_RESET, 0);

    /* 时钟：PLL160M ÷4 = 40MHz，PDM 分频 fp/fs=170/510 → 13.3333MHz */
    modifyreg32(I2S_TX_CLKM_CONF_REG, 0,
                (CVBS_I2S_DIV_NUM << I2S_TX_CLKM_DIV_NUM_S) |
                (2u << I2S_TX_CLK_SEL_S) |
                I2S_TX_CLK_ACTIVE | I2S_CLK_EN);

    /* PDM raw：旁路 PCM→PDM 滤波，DAC 单声道模式关 */
    modifyreg32(I2S_TX_PCM2PDM_CONF_REG, I2S_PCM2PDM_CONV_EN,
                I2S_PDM_DAC_MODE_EN);      /* 清 conv_en 与 dac_mode */

    /* fp/fs */
    putreg32((CVBS_PDM_FS << I2S_PDM_FS_S) | (CVBS_PDM_FP << I2S_PDM_FP_S),
             I2S_TX_PCM2PDM_CONF1_REG);

    /* 16-bit 单声道（FIFO 每 16-bit 字 = 16 个 PDM 时钟） */
    modifyreg32(I2S_TX_CONF1_REG, 0,
                (5u << I2S_TX_BCK_DIV_NUM_S) |     /* bck ÷6 */
                (15u << I2S_TX_BITS_MOD_S) |       /* 16-bit */
                (15u << I2S_TX_HALF_SAMPLE_S));    /* half=16bit */

    /* GPIO 路由：I2S0 PDM 数据出 → CVBS_PIN */
    esp_gpio_matrix_out(CVBS_PDM_PIN, I2SO_SD_OUT_IDX, false, false);

    /* 起 DMA（供 FIFO），再放行 I2S TX */
    esp_dma_load(&g_desc[0], g_dma_chan, true);
    esp_dma_enable(g_dma_chan, true);

    modifyreg32(I2S_TX_CONF_REG, 0, I2S_TX_UPDATE);
    modifyreg32(I2S_TX_CONF_REG, 0, I2S_TX_START | I2S_TX_PDM_EN | I2S_TX_MONO);

    syslog(LOG_INFO, "[cvbs-c3] I2S0 PDM engaged: %dHz pin rate, ring %d B, "
           "GDMA ch%d\n", CVBS_PDM_PIN_RATE_HZ, (int)CVBS_RING_BYTES,
           g_dma_chan);
}

/*
 * WHAT : 硬件行输出钩子（覆盖 common 层 weak 符号）
 * HOW  : 收到编码行 → sigma-delta 调制进 313 行场环（行号取模）
 */
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    if (line == NULL)
        return;

    if (!g_hw_started)
        hw_start();

    if (g_ring == NULL)
        return;

    ring_modulate_line(line_no, line, len);
}

#endif /* CONFIG_RETRO_AV_CONSOLE */
