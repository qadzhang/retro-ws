/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_cvbs_pio.c - RP2040(Pico) CVBS 硬件层（PIO + DMA 逐行）
 *
 * WHAT : Pico 目标的 CVBS 4-bit 并行输出（覆盖 common 的 weak 符号）
 * WHY  : RP2040 无视频 DAC，但 PIO 是天生的并行流外设（HARDWARE.md
 *        3B.2A，2026-10-04 定稿）：SM0 单指令 `out pins,4 [4]`，
 *        每 5 个 SM 周期输出一个 4-bit 样本到 GP12-15 → 4-bit R-2R
 * WHO  : Core1 视频生成任务（rp2040_retro 启动；双核分工规范）
 * WHERE: retro-ws/src/nuttx/rp2040/driver/cvbs/drv_cvbs_pio.c
 * WHEN : 2026-10-04(晚) 新增；同日深夜移脚 GP20-23 -> GP12-15
 *        （GP23=SMPS PS 脚：视频位翻转会把 3V3 纹波调制进
 *        R-2R 基准；且 PIO OUT PINS 只能映射连续引脚）
 * HOW  : SM 时钟 125MHz/1.8515625(int1+frac218) ≈ 67.5316MHz，
 *        每样本 5 周期 → 13.5063MHz（+0.047%，PAL 容差内）；
 *        864 样本/行（默认布局）→ 行 63.97µs。
 *        SRAM 放不下整场缓冲（264KB）→ 逐行架构：
 *          DMA(PIO TXF DREQ 节流) ← 行槽×4(108 词=864 样本) ←
 *          Core1 任务 cvbs_core_field_line() 按行拉取（240p 50Hz）
 *        PIO FIFO 深度天然吸收供数抖动——SM 时钟晶振级不中断，
 *        行时序永远精确。DRV 双缓冲 4 槽 = 3 行余量。
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
#include <unistd.h>

#include <nuttx/semaphore.h>
#include <nuttx/sched.h>
#include <nuttx/kthread.h>

#include "rp2040_pio.h"
#include "rp2040_dmac.h"
#include "board.h"

#include "driver/cvbs_core.h"
#include "driver/drv_cvbs.h"

#ifdef CONFIG_RETRO_AV_CONSOLE

/*==========================
 *  几何与引脚（HARDWARE.md 3B.2A）
 *==========================*/

#define CVBS_PIO            0
#define CVBS_SM             0
#define CVBS_PIN_BASE       12      /* GP12-15 = 4-bit R-2R（晚间移脚，见头注释） */
#define CVBS_PIN_COUNT      4

/* SM 分频：125MHz / (1 + 218/256) = 67.5316MHz；5 周期/样本 */
#define CVBS_SM_DIV_INT     1
#define CVBS_SM_DIV_FRAC    218
#define CVBS_SAMPLE_RATE_HZ 13506322

/* 行槽数量与深度：864 样本 = 108 个 32-bit 词（4-bit 打包） */
#define CVBS_LINE_WORDS     (864 / 8)
#define CVBS_SLOT_COUNT     4
#define CVBS_VIDEO_TASK_PRI  150
#define CVBS_VIDEO_STACK     4096

/* PIO 程序：out pins,4 [4] —— 5 SM 周期吐一个 4-bit 样本 */
#define CVBS_PIO_OUT_PINS4_D4   0x6403

static const rp2040_pio_program_t g_cvbs_prog =
{
    .instructions = (uint16_t *)&(uint16_t){ CVBS_PIO_OUT_PINS4_D4 },
    .length = 1,
    .origin = -1,
};

/*==========================
 *  状态
 *==========================*/

static uint32_t g_slots[CVBS_SLOT_COUNT][CVBS_LINE_WORDS];
static volatile int g_slot_line[CVBS_SLOT_COUNT];   /* 槽对应场行号 */
static volatile int g_send_idx = 0;                 /* DMA 正在发送的槽 */
static volatile int g_fill_idx = 0;                 /* 下一待填槽 */
static volatile int g_field_line = 0;               /* 下一生成场行 */
static DMA_HANDLE g_dma = NULL;
static sem_t g_fill_sem;
static bool g_video_on = false;
static uint32_t g_prog_off = 0;

/*==========================
 *  行打包：864 字节样本 → 108 词 4-bit
 *==========================*/

/*
 * WHAT : 一行 8-bit 样本量化打包进 32-bit 词槽
 * HOW  : 4-bit 四舍五入 (v+8)>>4；LSB-first（OSR shift-right），
 *        每词 8 样本，第 i 样本占 bit[4i+3:4i]
 */
static void pack_line(uint32_t *dst, const uint8_t *src, int n)
{
    int words = n / 8;

    for (int w = 0; w < words; w++) {
        uint32_t v = 0;

        for (int i = 0; i < 8; i++) {
            uint32_t nib = ((uint32_t)src[w * 8 + i] + 8) >> 4;

            v |= (nib & 0xf) << (4 * i);
        }
        dst[w] = v;
    }

    /* 尾部不满一词的样本（864%8=0，安全余量） */
    if (n % 8 != 0 && words < CVBS_LINE_WORDS)
        dst[words] = 0;
}

/*
 * WHAT : 生成场内第 line_no 行并填入空闲槽
 * HOW  : cvbs_core_field_line（240p：偶场重复，progressive）
 */
static void fill_next_line(void)
{
    uint8_t lbuf[CVBS_LINE_TOTAL];
    int slot = g_fill_idx;
    size_t len;

    len = cvbs_core_field_line(g_field_line, false, true, lbuf);
    if (len == 0)
        g_field_line = 0;                       /* 场结束回卷 */

    pack_line(g_slots[slot], lbuf, (int)len);
    g_slot_line[slot] = g_field_line;
    g_field_line++;
    if (g_field_line >= cvbs_core_field_line_count(false, true))
        g_field_line = 0;

    g_fill_idx = (g_fill_idx + 1) % CVBS_SLOT_COUNT;
}

/*==========================
 *  DMA 回调（中断上下文）：逐行续排
 *==========================*/

static void cvbs_dma_cb(DMA_HANDLE handle, uint8_t status, void *arg)
{
    (void)handle;
    (void)status;
    (void)arg;

    if (!g_video_on)
        return;

    nxsem_post(&g_fill_sem);
}

/*
 * WHAT : Core1 视频任务——保持槽满 + 触发下一行 DMA
 * WHY  : 双核分工规范：Core0=程序(NSH/nano/脚本)，Core1=视频
 * HOW  : 信号量醒（每行一次）→ 填一槽 → 对"已填好且尚未发送"的
 *        下一槽续排 DMA；PIO 是时基主，DMA 晚一点只吃 FIFO 余量
 */
static int cvbs_video_task(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

#ifdef CONFIG_SMP
    {
        cpu_set_t mask;

        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
    }
#endif

    syslog(LOG_INFO, "[cvbs-pico] Core1 video task started\n");

    while (g_video_on) {
        int sret = nxsem_wait(&g_fill_sem);

        if (sret < 0 || !g_video_on)
            continue;

        fill_next_line();

        /* 续排下一行（刚填的槽在 g_send 序列上） */
        int next = (g_send_idx + 1) % CVBS_SLOT_COUNT;

        if (g_slot_line[next] >= 0) {
            dma_config_t cfg =
            {
                .dreq = RP2040_DMA_DREQ_PIO0_TX0 + CVBS_SM,
                .size = RP2040_DMA_SIZE_WORD,
                .noincr = false,
            };

            g_send_idx = next;
            rp2040_txdmasetup(g_dma,
                              (uintptr_t)RP2040_PIO_TXF(CVBS_PIO, CVBS_SM),
                              (uintptr_t)g_slots[next],
                              CVBS_LINE_WORDS * 4, cfg);
            rp2040_dmastart(g_dma, cvbs_dma_cb, NULL);
        }
    }

    return 0;
}

/*==========================
 *  硬件启动
 *==========================*/

static void hw_start(void)
{
    rp2040_pio_sm_config c = rp2040_pio_get_default_sm_config();
    int i;

    /* 4-bit R-2R 引脚交 PIO */
    for (i = 0; i < CVBS_PIN_COUNT; i++)
        rp2040_pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE + i);
    rp2040_pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM,
                                          CVBS_PIN_BASE, CVBS_PIN_COUNT,
                                          true);

    /* 程序装载 */
    g_prog_off = rp2040_pio_add_program(CVBS_PIO, &g_cvbs_prog);

    /* SM 配置：out 4 位到 GP12-15，autopull 32-bit 右移，分频 1.8516 */
    rp2040_sm_config_set_out_pins(&c, CVBS_PIN_BASE, CVBS_PIN_COUNT);
    rp2040_sm_config_set_out_shift(&c, true, true, 32);
    rp2040_sm_config_set_fifo_join(&c, RP2040_PIO_FIFO_JOIN_TX);
    rp2040_sm_config_set_clkdiv_int_frac(&c, CVBS_SM_DIV_INT,
                                         CVBS_SM_DIV_FRAC);
    rp2040_pio_sm_init(CVBS_PIO, CVBS_SM, g_prog_off, &c);

    /* DMA 通道 */
    g_dma = rp2040_dmachannel();

    /* 预填 4 槽再放行 SM（FIFO 有底，行时序立即锁定） */
    for (i = 0; i < CVBS_SLOT_COUNT; i++)
        g_slot_line[i] = -1;
    g_field_line = 0;
    g_fill_idx = 0;
    g_send_idx = 0;
    nxsem_init(&g_fill_sem, 0, 0);
    g_video_on = true;

    for (i = 0; i < CVBS_SLOT_COUNT; i++)
        fill_next_line();
    g_send_idx = 0;

    {
        dma_config_t cfg =
        {
            .dreq = RP2040_DMA_DREQ_PIO0_TX0 + CVBS_SM,
            .size = RP2040_DMA_SIZE_WORD,
            .noincr = false,
        };

        rp2040_txdmasetup(g_dma,
                          (uintptr_t)RP2040_PIO_TXF(CVBS_PIO, CVBS_SM),
                          (uintptr_t)g_slots[0],
                          CVBS_LINE_WORDS * 4, cfg);
        rp2040_dmastart(g_dma, cvbs_dma_cb, NULL);
    }

    rp2040_pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);

    /* Core1 视频任务 */
    {
        int pid = kernel_thread("cvbsvid", CVBS_VIDEO_TASK_PRI,
                                CVBS_VIDEO_STACK, cvbs_video_task, NULL);

        if (pid < 0)
            syslog(LOG_ERR, "[cvbs-pico] video task failed: %d\n", pid);
    }

    syslog(LOG_INFO, "[cvbs-pico] PIO engaged: %dHz samples, "
           "GP%d-%d 4-bit ladder\n",
           CVBS_SAMPLE_RATE_HZ, CVBS_PIN_BASE,
           CVBS_PIN_BASE + CVBS_PIN_COUNT - 1);
}

/*
 * WHAT : 硬件行输出钩子——Pico 逐行架构不用整场推送
 * HOW  : 首帧触发 hw_start（含 Core1 任务），随后为空操作：
 *        生成任务直读 cvbs L8 帧缓冲，每场自动反映最新内容
 */
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no)
{
    (void)line;
    (void)len;
    (void)line_no;

    if (!g_video_on)
        hw_start();
}

/*
 * WHAT : 帧推送覆盖——Pico 无需整帧重渲（生成器场频直读 fb）
 */
void drv_cvbs_frame(void)
{
    if (!g_video_on)
        hw_start();
}

#endif /* CONFIG_RETRO_AV_CONSOLE */
