/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_cvbs.c - ESP32-S3 CVBS 硬件层（LCD_CAM I80 并行口 + GDMA）
 *
 * WHAT : S3 目标的 CVBS 全帧输出硬件层（覆盖 common 的 weak 符号）
 * WHY  : S3 无内置 DAC 且 I2S(HW v2) 无并行/LCD 模式；真并行流出口
 *        是 LCD_CAM 外设 I80 模式（HARDWARE.md 6.5，2026-10-04 定稿），
 *        8-bit 总线只接高/低 4 位到 4-bit R-2R 电阻梯（16 电平）
 * WHO  : common/driver/drv_cvbs.c 的 cvbs_core_generate_frame()
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/cvbs/drv_cvbs.c
 * WHEN : 2026-03 初版；2026-10-04 接 cvbs_core；同日(晚)重写为
 *        LCD_CAM+GDMA 真硬件流（原 I2S 桩只 memcpy 不上硬件）
 * HOW  : 帧缓冲 = 625 行 × 853 样本（PAL 隔行整帧，13.3333MHz 采样），
 *        GDMA 环形描述符链永续供数（末描述符回链首，无终结），
 *        LCD_USER.lcd_always_out_en=1 让 DOUT 相持续输出；
 *        时钟 = PLL160M/12 = 13.3333MHz 整数分频（无小数抖动），
 *        行长 853 样本（12/63/68/710）→ 行周期 63.975µs（-0.04%）。
 *        更新路径：drv_cvbs_frame() 整帧原地重渲（25Hz， tearing
 *        限于单行内，字符/GUI 场景可接受）。
 * 注意 : 引脚映射 GPIO2/15/16/17 ← LCD_DATA_OUT0/1/2/3（4-bit 梯），
 *        PCLK/CS/DC 不外接引脚（外设内部照常走节拍）。
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

/* IO 原语（arch 通用；apps 包含路径下部分板缺声明，兜底 extern） */
extern void putreg32(uint32_t value, uintptr_t address);
extern uint32_t getreg32(uintptr_t address);
extern void modifyreg32(uintptr_t address, uint32_t clearbits,
                        uint32_t setbits);

#include "esp32s3.h"
#include "board.h"

#include "driver/cvbs_core.h"
#include "driver/drv_cvbs.h"

#if defined(CONFIG_RETRO_DISPLAY) || defined(CONFIG_RETRO_AV_CONSOLE)

/*
 * NuttX GDMA 驱动接口（arch/xtensa/src/esp32s3/esp32s3_dma.[ch]——
 * 头文件不在 apps 包含路径，按链接期符号 extern 声明）
 */
struct esp32s3_dmadesc_s {
    uint32_t ctrl;                    /* DMA 控制块 */
    const uint8_t *pbuf;              /* DMA TX/RX 缓冲地址 */
    struct esp32s3_dmadesc_s *next;   /* 下一描述符 */
};

enum esp32s3_dma_periph_e {
    ESP32S3_DMA_PERIPH_LCDCAM = 5,
};

enum esp32s3_dma_ext_memblk_e {
    ESP32S3_DMA_EXT_MEMBLK_64B = 2,
};

#define ESP32S3_DMA_CTRL_OWN          (1 << 31)
#define ESP32S3_DMA_CTRL_EOF          (1 << 30)
#define ESP32S3_DMA_CTRL_DATALEN_S    (12)
#define ESP32S3_DMA_CTRL_BUFLEN_S     (0)
#define ESP32S3_DMA_CTRL_BUFLEN_V     (0xfff)

extern int32_t esp32s3_dma_request(enum esp32s3_dma_periph_e periph,
                                   uint32_t tx_prio, uint32_t rx_prio,
                                   bool burst_en);
extern void esp32s3_dma_load(struct esp32s3_dmadesc_s *dmadesc, int chan,
                             bool tx);
extern void esp32s3_dma_enable(int chan, bool tx);
extern void esp32s3_dma_set_ext_memblk(int chan, bool tx,
                                       enum esp32s3_dma_ext_memblk_e type);
extern void esp32s3_gpio_matrix_out(uint32_t pin, uint32_t signal_idx,
                                    bool out_inv, bool oen_inv);

/*==========================
 *  寄存器与常数（esp-hal-3rdparty lcd_cam_reg.h 校对）
 *==========================*/

#define LCD_CAM_BASE           0x60041000
#define LCD_CLOCK_REG          (LCD_CAM_BASE + 0x00)
#define LCD_RGB_YUV_REG        (LCD_CAM_BASE + 0x10)
#define LCD_USER_REG           (LCD_CAM_BASE + 0x14)
#define LCD_MISC_REG           (LCD_CAM_BASE + 0x18)

/* LCD_CLOCK 位域 */
#define LCD_CLKCNT_N_S         0
#define LCD_CLK_EQU_SYSCLK     (1u << 6)
#define LCD_CK_IDLE_EDGE       (1u << 7)
#define LCD_CK_OUT_EDGE        (1u << 8)
#define LCD_CLKM_DIV_NUM_S     9    /* 8-bit 整数分频（div_num 段） */
#define LCD_CLKM_DIV_B_S       17
#define LCD_CLKM_DIV_A_S       23
#define LCD_CLK_SEL_S          29   /* 3=PLL_F160M */

/* LCD_USER 位域（lcd_cam_struct.h） */
#define LCD_DOUT_CYCLELEN_S    0    /* 13-bit */
#define LCD_ALWAYS_OUT_EN      (1u << 13)
#define LCD_UPDATE             (1u << 20)
#define LCD_2BYTE_EN           (1u << 23)
#define LCD_DOUT               (1u << 24)
#define LCD_START              (1u << 27)

/* LCD_MISC 位域 */
#define LCD_AFIFO_RESET        (1u << 15)  /* 自清零：fifo reset */

/* 系统外设（SYSTEM）开时钟/复位 */
#define SYSTEM_BASE            0x600c0000
#define SYSTEM_PERIP_CLK_EN1   (SYSTEM_BASE + 0x1c)
#define SYSTEM_PERIP_RST_EN1   (SYSTEM_BASE + 0x24)
#define SYSTEM_LCD_CAM_CLK_EN  (1u << 8)
#define SYSTEM_LCD_CAM_RST     (1u << 8)

/* 采样时钟：PLL_F160M 160MHz / 12 = 13.3333MHz（整数分频零抖动） */
#define CVBS_CLK_SRC_HZ        160000000
#define CVBS_CLK_DIV_NUM       11          /* 除数 = div_num+1 = 12 */
#define CVBS_SAMPLE_RATE_HZ    (CVBS_CLK_SRC_HZ / 12)

/* 853 样本行布局（HARDWARE.md 6.2：行 63.975µs，-0.04%） */
#define CVBS_S3_LINE_FRONT     12
#define CVBS_S3_LINE_SYNC      63
#define CVBS_S3_LINE_BACK      68
#define CVBS_S3_LINE_ACTIVE    710
#define CVBS_S3_LINE_TOTAL     853

/* 整帧 = PAL 隔行 625 行（240p 模式 generate_frame 也输出 625 行） */
#define CVBS_FRAME_LINES       625
#define CVBS_FRAME_BYTES       (CVBS_FRAME_LINES * CVBS_S3_LINE_TOTAL)

/* 4-bit R-2R 接线（HARDWARE.md 2.7：bit0..bit3） */
#define CVBS_LADDER_PIN0       2
#define CVBS_LADDER_PIN1       15
#define CVBS_LADDER_PIN2       16
#define CVBS_LADDER_PIN3       17

/* GPIO 矩阵信号索引（gpio_sig_map.h） */
#define LCD_DATA_OUT0_IDX      133
#define LCD_DATA_OUT1_IDX      134
#define LCD_DATA_OUT2_IDX      135
#define LCD_DATA_OUT3_IDX      136

/*==========================
 *  状态
 *==========================*/

static uint8_t *g_frame = NULL;          /* 信号帧缓冲（PSRAM 堆） */
static struct esp32s3_dmadesc_s *g_desc = NULL;
static int g_desc_count = 0;
static int g_dma_chan = -1;
static bool g_hw_started = false;

/*==========================
 *  GDMA 环形描述符链
 *==========================*/

/*
 * WHAT : 构建覆盖帧缓冲的环形描述符链 / build ring descriptors
 * WHY  : 永续视频流——末描述符回链首，DMA 永不到达链尾
 * HOW  : 每描述符 ≤4092B（BUFLEN 域 12-bit）；EOF 置于帧末仅作
 *        心跳标记（本驱动不开中断，链继续走）
 */
static int frame_ring_build(void)
{
    size_t chunk = 4092 & ~3;            /* 4 对齐 + ≤4095 */
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

        g_desc[i].ctrl = ESP32S3_DMA_CTRL_OWN |
                         ((uint32_t)len << ESP32S3_DMA_CTRL_DATALEN_S) |
                         ((uint32_t)len << ESP32S3_DMA_CTRL_BUFLEN_S);
        if (i == n - 1)
            g_desc[i].ctrl |= ESP32S3_DMA_CTRL_EOF;
        g_desc[i].pbuf = g_frame + off;
        g_desc[i].next = &g_desc[(i + 1) % n];   /* 环回 */
        off += len;
    }

    g_desc_count = n;
    return OK;
}

/*
 * WHAT : 硬件启动（一次）/ engage LCD_CAM + GDMA
 * HOW  : 时钟使能→LCD 寄存器→GPIO 矩阵→DMA 环→lcd_start；
 *        帧缓冲先整帧填消隐（画面出黑，随后 frame() 原地更新）
 */
static void hw_start(void)
{
    int ret;

    g_hw_started = true;

    /* 行长布局（必须在 fb 消费之前） */
    cvbs_core_set_line_layout(CVBS_S3_LINE_FRONT, CVBS_S3_LINE_SYNC,
                              CVBS_S3_LINE_BACK, CVBS_S3_LINE_ACTIVE);

    /* 信号帧缓冲（533KB → PSRAM 堆） */
    g_frame = memalign(32, CVBS_FRAME_BYTES);
    if (g_frame == NULL) {
        syslog(LOG_ERR, "[cvbs-s3] frame buffer alloc failed\n");
        return;
    }
    memset(g_frame, CVBS_LEVEL_BLANK, CVBS_FRAME_BYTES);

    /* GDMA 通道（外设选择 LCDCAM=5，由驱动写 OUT_PERI_SEL） */
    g_dma_chan = esp32s3_dma_request(ESP32S3_DMA_PERIPH_LCDCAM, 5, 5, false);
    if (g_dma_chan < 0) {
        syslog(LOG_ERR, "[cvbs-s3] GDMA channel request failed\n");
        return;
    }

    /* 帧缓冲在 PSRAM 时 GDMA 走 64B 块访问 */
    esp32s3_dma_set_ext_memblk(g_dma_chan, true, ESP32S3_DMA_EXT_MEMBLK_64B);

    ret = frame_ring_build();
    if (ret != OK)
        return;

    /* LCD_CAM 外设时钟 + 复位 */
    modifyreg32(SYSTEM_PERIP_RST_EN1, 0, SYSTEM_LCD_CAM_RST);
    modifyreg32(SYSTEM_PERIP_RST_EN1, SYSTEM_LCD_CAM_RST, 0);
    modifyreg32(SYSTEM_PERIP_CLK_EN1, 0, SYSTEM_LCD_CAM_CLK_EN);

    /* LCD_CLOCK：PLL160M 源，整数 ÷12，PCLK=等系统时钟（无预分频） */
    putreg32(0, LCD_CLOCK_REG);
    modifyreg32(LCD_CLOCK_REG, 0,
                (CVBS_CLK_DIV_NUM << LCD_CLKM_DIV_NUM_S) |
                (3u << LCD_CLK_SEL_S) |        /* PLL_F160M */
                LCD_CLK_EQU_SYSCLK |
                LCD_CK_IDLE_EDGE);             /* WR 空闲高 */

    /* LCD_MISC：复位发送 FIFO */
    modifyreg32(LCD_MISC_REG, 0, LCD_AFIFO_RESET);

    /* LCD_USER：8-bit 数据、无命令/_dummy 相、DOUT 常开 */
    putreg32(0, LCD_USER_REG);
    modifyreg32(LCD_USER_REG, 0,
                LCD_DOUT | LCD_ALWAYS_OUT_EN | (0 << LCD_DOUT_CYCLELEN_S));

    /* 4-bit R-2R：矩阵输出 DATA_OUT0-3 到 ladder 引脚 */
    esp32s3_gpio_matrix_out(CVBS_LADDER_PIN0, LCD_DATA_OUT0_IDX, false, false);
    esp32s3_gpio_matrix_out(CVBS_LADDER_PIN1, LCD_DATA_OUT1_IDX, false, false);
    esp32s3_gpio_matrix_out(CVBS_LADDER_PIN2, LCD_DATA_OUT2_IDX, false, false);
    esp32s3_gpio_matrix_out(CVBS_LADDER_PIN3, LCD_DATA_OUT3_IDX, false, false);

    /* 先起 DMA（生产者填 FIFO），再放行 LCD（消费者按 PCLK 取） */
    esp32s3_dma_load(&g_desc[0], g_dma_chan, true);
    esp32s3_dma_enable(g_dma_chan, true);

    modifyreg32(LCD_USER_REG, 0, LCD_UPDATE);
    modifyreg32(LCD_USER_REG, 0, LCD_START);

    syslog(LOG_INFO, "[cvbs-s3] LCD_CAM engaged: %dHz PCLK, %d-sample lines, "
           "GDMA ch%d ring %d desc\n",
           CVBS_SAMPLE_RATE_HZ, CVBS_S3_LINE_TOTAL, g_dma_chan, g_desc_count);
}

/*
 * WHAT : 硬件行输出钩子（覆盖 common 层 weak 符号）
 * WHY  : cvbs_core_generate_frame 逐行回调；本层量化 4-bit 写入
 *        帧缓冲对应行（DMA 环永续并发读出）
 * HOW  : 8-bit 样本 -> (v+8)>>4 四舍五入到 16 电平，置于字节低 4 位
 *        （LCD_DATA_OUT0-3 接线）；行号 0..624 直映射帧缓冲行
 */
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    if (line_no < 0 || line_no >= CVBS_FRAME_LINES || g_frame == NULL)
        return;

    if (!g_hw_started)
        hw_start();

    if (g_frame == NULL)
        return;

    if (len > CVBS_S3_LINE_TOTAL)
        len = CVBS_S3_LINE_TOTAL;

    uint8_t *dst = g_frame + (size_t)line_no * CVBS_S3_LINE_TOTAL;
    for (size_t i = 0; i < len; i++)
        dst[i] = (uint8_t)((line[i] + 8) >> 4);
}

/*
 * WHAT : 兼容别名：原启动代码调用 cvbs_init()
 * HOW  : 转发统一驱动入口
 */
int cvbs_init(void)
{
    return drv_cvbs_init();
}

#endif /* CONFIG_RETRO_DISPLAY || CONFIG_RETRO_AV_CONSOLE */
