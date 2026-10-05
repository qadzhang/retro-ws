/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_audio.c - I2S 音频驱动
 *
 * WHAT : I2S 音频驱动
 * WHY  : 外部 I2S DAC（GPIO40/41/42）输出与 ADC 输入
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/audio/drv_audio.c
 * WHEN : 2026-03~04 初版，2026-10-04 修复寄存器偏移/中断/补齐 API
 * HOW  : I2S DMA 多缓冲环，44100Hz/16bit；寄存器偏移统一取自
 *        src/nuttx/esp32s3/chip/esp32s3.h（不再硬编码）
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <syslog.h>   /* syslog()/LOG_*：NuttX libc 头，宿主机为 glibc */
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>

#include "esp32s3.h"
#include "board.h"
#include "drv_audio.h"

/*
 * 目标机芯片中断头 / target-only chip IRQ headers（按可见性兜底）：
 * 1) "esp32s3_irq.h" —— NuttX 惯例：chip src 目录在包含路径上
 *    （deps/nuttx/arch/xtensa/src/esp32s3/esp32s3_irq.h，
 *     声明 esp32s3_setup_irq()）
 * 2) <arch/chip/esp32s3_irq.h> —— 宿主机 realinc 语法检查层
 * 3) <arch/irq.h> —— 至少建立基础中断类型
 * IRQ 号宏 ESP32S3_IRQ_I2S0 / ESP32S3_PERIPH_I2S0 的权威出处：
 * deps/nuttx/arch/xtensa/include/esp32s3/irq.h
 */
#ifdef CONFIG_ARCH_XTENSA
#  if defined(__has_include)
#    if __has_include("esp32s3_irq.h")
#      include "esp32s3_irq.h"
#    elif __has_include(<arch/chip/esp32s3_irq.h>)
#      include <arch/chip/esp32s3_irq.h>
#    endif
#  endif
#  include <arch/irq.h>
#endif

/*
 * IRAM_ATTR 由 xtensa 架构头提供 / provided by xtensa arch headers；
 * 宿主机语法检查时兜底为空
 */
#ifndef IRAM_ATTR
#  define IRAM_ATTR
#endif

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_AUDIO
#  undef CONFIG_ESP32S3_I2S
#  define CONFIG_ESP32S3_I2S 0
#endif

#ifndef CONFIG_RETRO_AUDIO_SAMPLE_RATE
#  define CONFIG_RETRO_AUDIO_SAMPLE_RATE 44100
#endif

#if CONFIG_ESP32S3_I2S

/*==========================
 *  常量
 *==========================*/

/*
 * 注意：board.h 的 AUDIO_I2S_PORT 展开为 ESP-IDF 枚举 I2S_NUM_0，
 * NuttX 下不存在该符号，这里直接固定使用 I2S0
 * / board.h's AUDIO_I2S_PORT expands to the ESP-IDF enum I2S_NUM_0
 *   which does not exist under NuttX; I2S0 is used directly here.
 */
#define AUDIO_DMA_BUF_COUNT   4        /* DMA 环中缓冲区数量 / buffers in ring */
#define AUDIO_DMA_BUF_SIZE    2048     /* 每个缓冲区字节数 / bytes per buffer */
#define DMA_POLL_LIMIT        4000000  /* owner 轮询上限 / poll spin limit */

/*==========================
 *  I2S 寄存器 — 全部经 esp32s3.h 取址（原实现硬编码偏移与头冲突）
 *==========================*/

#define I2S_CONF              ESP32S3_I2S0_CONF_REG
#define I2S_SAMPLE_RATE_CONF  ESP32S3_I2S0_SAMPLE_RATE_CONF_REG
#define I2S_CLKM_CONF         ESP32S3_I2S0_CLKM_CONF_REG
#define I2S_FIFO_CONF         ESP32S3_I2S0_FIFO_CONF_REG
#define I2S_OUT_LINK          ESP32S3_I2S0_OUT_LINK_REG
#define I2S_INT_ENA           ESP32S3_I2S0_INT_ENA_REG
#define I2S_INT_ST            ESP32S3_I2S0_INT_ST_REG
#define I2S_INT_CLR           ESP32S3_I2S0_INT_CLR_REG

/* I2S_CONF 位域（项目寄存器模型 / project register model）*/
#define I2S_CONF_TX_START     (1 << 0)
#define I2S_CONF_RX_START     (1 << 1)
#define I2S_CONF_TX_RESET     (1 << 2)
#define I2S_CONF_RX_RESET     (1 << 3)
#define I2S_CONF_TX_FIFO_RESET (1 << 4)
#define I2S_CONF_RX_FIFO_RESET (1 << 5)

/* CLKM 分频位域 */
#define I2S_CLKM_DIV_NUM_MASK 0xFF

/* FIFO 位域 */
#define I2S_TX_FIFO_MOD_SHIFT 0
#define I2S_TX_FIFO_MOD_MASK  (7 << 0)
#define I2S_TX_BITS_MOD_SHIFT 0
#define I2S_RX_BITS_MOD_SHIFT 16

/*==========================
 *  音频状态
 *==========================*/

struct audio_state {
    bool              initialized;
    bool              playing;
    bool              paused;

    uint32_t         sample_rate;      /* 采样率 */
    uint8_t          bits_per_sample;  /* 位深 */
    uint8_t          channels;         /* 声道数 */

    /* DMA 缓冲区 */
    uint8_t          *dma_buf[AUDIO_DMA_BUF_COUNT * 2];
    esp32s3_dma_descriptor_t dma_desc[AUDIO_DMA_BUF_COUNT * 2];

    /* 播放位置 */
    volatile uint32_t write_idx;       /* 下一个待填充描述符 / next to fill */
    volatile uint32_t irq_idx;         /* 中断统计游标 / ISR bookkeeping cursor */

    /* 统计 */
    uint32_t         total_played_bytes;
    uint32_t         underrun_count;
    uint32_t         overrun_count;

    /* 音量 (0-100，common 口径 / 0-100 as common layer expects) */
    uint8_t          volume;
};

/* 全局状态 */
static struct audio_state g_audio = {0};

/*==========================
 *  I2S 寄存器操作（直接操作完整地址 / full register address）
 *  参数用 uintptr_t：目标机 32 位等宽，宿主机语法检查时不产生
 *  int->pointer 截断告警 / uintptr_t keeps host builds warning-free
 *==========================*/

static inline uint32_t i2s_rd(uintptr_t reg)
{
    return getreg32(reg);
}

static inline void i2s_wr(uintptr_t reg, uint32_t value)
{
    putreg32(value, reg);
}

static inline void i2s_set_bit(uintptr_t reg, uint32_t mask)
{
    i2s_wr(reg, i2s_rd(reg) | mask);
}

static inline void i2s_clr_bit(uintptr_t reg, uint32_t mask)
{
    i2s_wr(reg, i2s_rd(reg) & ~mask);
}

/*==========================
 *  时钟配置
 *==========================*/

/*
 * WHAT : 配置 I2S 时钟分频
 * WHY  : 采样率由 APB(80MHz)/bck 分频决定
 * HOW  : div = apb/(rate*bits*ch) - 1，同时写 TX/RX 位宽到
 *        SAMPLE_RATE_CONF；偏移全部来自 esp32s3.h
 */
static int i2s_set_clock(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    uint32_t apb_freq = 80 * 1000000;  /* 80MHz APB */
    uint32_t bck = sample_rate * bits * channels;
    uint32_t div = (apb_freq / bck) - 1;
    if (div > 255)
        div = 255;
    if (div < 1)
        div = 1;

    uint32_t clkm_conf = i2s_rd(I2S_CLKM_CONF);
    clkm_conf = (clkm_conf & ~I2S_CLKM_DIV_NUM_MASK) | (div & I2S_CLKM_DIV_NUM_MASK);
    clkm_conf |= (1 << 8);  /* I2S_CLK_EN */
    clkm_conf |= (1 << 7);  /* CLK_EN */
    i2s_wr(I2S_CLKM_CONF, clkm_conf);

    uint32_t rate_conf = i2s_rd(I2S_SAMPLE_RATE_CONF);
    rate_conf &= ~(0xFF << I2S_TX_BITS_MOD_SHIFT);
    rate_conf |= ((bits - 1) << I2S_TX_BITS_MOD_SHIFT);
    rate_conf &= ~(0xFF << I2S_RX_BITS_MOD_SHIFT);
    rate_conf |= ((bits - 1) << I2S_RX_BITS_MOD_SHIFT);
    i2s_wr(I2S_SAMPLE_RATE_CONF, rate_conf);

    return OK;
}

/*
 * WHAT : 复位 I2S 控制器
 * HOW  : 停 TX/RX -> 脉冲复位 FIFO -> 脉冲复位 TX/RX
 */
static void i2s_reset(void)
{
    i2s_clr_bit(I2S_CONF, I2S_CONF_TX_START | I2S_CONF_RX_START);

    i2s_set_bit(I2S_CONF, I2S_CONF_TX_FIFO_RESET | I2S_CONF_RX_FIFO_RESET);
    i2s_clr_bit(I2S_CONF, I2S_CONF_TX_FIFO_RESET | I2S_CONF_RX_FIFO_RESET);

    i2s_set_bit(I2S_CONF, I2S_CONF_TX_RESET | I2S_CONF_RX_RESET);
    i2s_clr_bit(I2S_CONF, I2S_CONF_TX_RESET | I2S_CONF_RX_RESET);
}

/*
 * WHAT : 配置 I2S 引脚（GPIO40/41/42 -> I2S0 输出信号）
 * WHY  : 外部 I2S DAC 需要 WS/SCK/SDO 三路输出
 * WHEN : audio_init() 时调用
 * HOW  : 通过 GPIO 矩阵 OUT_SEL 寄存器路由：
 *        - FUNCn_OUT_SEL_CFG_REG(n) 低 9 位写信号 ID
 *          （信号 ID 来源 / signal IDs from:
 *            deps/nuttx/arch/xtensa/src/esp32s3/hardware/
 *            esp32s3_gpio_sigmap.h — I2S0O_BCK_OUT=22,
 *            I2S0O_WS_OUT=24, I2S0O_SD_OUT=25）
 *        - GPIO<32 用 ENABLE_W1TS，GPIO>=32 用 ENABLE1_W1TS 置输出使能
 * TODO : IO_MUX pad 级配置（MCU_SEL 功能选择 / FUN_DRV 驱动强度 /
 *        FUN_WPU 上拉，位于 GPIO_PINn_REG）在本头暂无位域宏，
 *        真机联调按 HARDWARE.md 流程补齐后再写
 */
static void i2s_config_pins(void)
{
    static const struct {
        uint32_t pin;    /* 板级引脚号（hw_esp32s3_devkitc.h）*/
        uint32_t sig;    /* GPIO 矩阵输出信号 ID / matrix out signal ID */
    } pinmap[] = {
        { AUDIO_I2S_SCK, 22 },   /* I2S0O_BCK_OUT */
        { AUDIO_I2S_WS,  24 },   /* I2S0O_WS_OUT  */
        { AUDIO_I2S_SDO, 25 },   /* I2S0O_SD_OUT  */
    };
    const int nmap = sizeof(pinmap) / sizeof(pinmap[0]);

    for (int i = 0; i < nmap; i++) {
        uint32_t pin = pinmap[i].pin;

        putreg32(pinmap[i].sig,
                 (uintptr_t)ESP32S3_GPIO_FUNC_OUT_SEL_CFG_REG(pin));

        if (pin < 32)
            putreg32(1u << pin, (uintptr_t)ESP32S3_GPIO_ENABLE_W1TS_REG);
        else
            putreg32(1u << (pin - 32), (uintptr_t)ESP32S3_GPIO_ENABLE1_W1TS_REG);
    }
}

/*
 * WHAT : 配置 I2S 为 PCM 模式
 * HOW  : PCM 短帧同步 + MSB 先行 + 左对齐，FIFO 走 16-bit 双声道
 */
static void i2s_config_pcm_mode(void)
{
    uint32_t conf = i2s_rd(I2S_CONF);

    conf |= (1 << 24);  /* PCM_SHORT_FRAME */
    conf &= ~(1 << 25); /* PCM_FIRST_BIT_SHIT = MSB */
    conf |= (1 << 27);  /* LEFT_ALIGN */
    conf |= (1 << 28);  /* ENB_24FS */

    i2s_wr(I2S_CONF, conf);

    uint32_t fifo_conf = i2s_rd(I2S_FIFO_CONF);
    fifo_conf &= ~I2S_TX_FIFO_MOD_MASK;
    fifo_conf |= (4 << I2S_TX_FIFO_MOD_SHIFT);  /* 16-bit dual channel */
    fifo_conf |= (1 << 13);  /* TX_FIFO_FORCE_DMA_EN */
    i2s_wr(I2S_FIFO_CONF, fifo_conf);
}

/*==========================
 *  DMA 初始化
 *==========================*/

/*
 * WHAT : 初始化 DMA 描述符环
 * WHY  : I2S 连续输出需要自循环链表
 * HOW  : N 个缓冲接成环，owner 初始归 CPU（软件可写）
 * 返回 : OK / -ENOMEM
 */
static int dma_init_chain(void)
{
    int i;

    for (i = 0; i < AUDIO_DMA_BUF_COUNT * 2; i++) {
        uint8_t *buf = g_audio.dma_buf[i];
        if (!buf) {
            buf = (uint8_t *)memalign(16, AUDIO_DMA_BUF_SIZE);
            if (!buf)
                return -ENOMEM;
            g_audio.dma_buf[i] = buf;
            memset(buf, 0, AUDIO_DMA_BUF_SIZE);
        }

        esp32s3_dma_descriptor_t *desc = &g_audio.dma_desc[i];
        desc->eof    = 0;
        desc->owner  = ESP32S3_DMA_DESC_OWNER_CPU;
        desc->length = 0;
        desc->size   = AUDIO_DMA_BUF_SIZE;
        desc->buf    = (uint32_t)(uintptr_t)buf;
        desc->next   = (uint32_t)(uintptr_t)
                       &g_audio.dma_desc[(i + 1) % (AUDIO_DMA_BUF_COUNT * 2)];
    }

    g_audio.write_idx = 0;
    g_audio.irq_idx = 0;

    return OK;
}

/*
 * WHAT : 启动 DMA 传输
 * WHY  : 原实现把描述符地址左移 12 位写入 OUT_LINK（错误拼位）
 * HOW  : 描述符地址掩码到 [19:0] 并置 OUTLINK_START（位 29），
 *        同 drv_cvbs_dac.c 的启动模式；随后置 TX_START
 */
static void dma_start(void)
{
    uint32_t desc_addr = (uint32_t)(uintptr_t)&g_audio.dma_desc[0];

    i2s_wr(I2S_OUT_LINK,
           (desc_addr & ESP32S3_I2S_OUTLINK_ADDR_MASK) |
           ESP32S3_I2S_OUTLINK_START);

    i2s_set_bit(I2S_CONF, I2S_CONF_TX_START);
}

/*
 * WHAT : 停止 DMA 传输
 * HOW  : 先停 TX 再对 OUT_LINK 写 OUTLINK_STOP（位 28，W1T 触发）
 */
static void dma_stop(void)
{
    i2s_clr_bit(I2S_CONF, I2S_CONF_TX_START);
    i2s_wr(I2S_OUT_LINK, ESP32S3_I2S_OUTLINK_STOP);
}

/*==========================
 *  中断处理
 *==========================*/

/*
 * WHAT : I2S DMA 发送完成中断服务程序
 * WHY  : 原实现把 INT_ENA 当状态读（使能位≠挂起位），且从未挂接到
 *        中断控制器——中断从未触发过
 * WHEN : 每个描述符搬运完成（TX_DONE）时由硬件触发
 * HOW  : 读 INT_ST 判断挂起位，对 INT_CLR 写 1 清除（WT 寄存器，
 *        不可读改写），按完成游标累计已播放字节数
 * 返回 : OK（NuttX xcpt_t 约定）
 */
static int IRAM_ATTR i2s_dma_isr(int irq, FAR void *context, FAR void *arg)
{
    (void)arg;
    uint32_t status = i2s_rd(I2S_INT_ST);

    if (status & ESP32S3_I2S_TX_DONE_INT) {
        /* 写 1 清除 / write-1-to-clear */
        i2s_wr(I2S_INT_CLR, ESP32S3_I2S_TX_DONE_INT);

        g_audio.total_played_bytes += g_audio.dma_desc[g_audio.irq_idx].length;
        g_audio.irq_idx = (g_audio.irq_idx + 1) % (AUDIO_DMA_BUF_COUNT * 2);

        if (g_audio.paused)
            g_audio.underrun_count++;
    }

    return OK;
}

/*==========================
 *  音量控制
 *==========================*/

/*
 * WHAT : 应用音量到 PCM 样本（0-100）
 * WHY  : common 播放器传 0-100 口径
 */
static void apply_volume(int16_t *sample, uint8_t volume)
{
    if (volume >= 100)
        return;  /* 100% 无需处理 */

    int32_t s = *sample;
    s = (s * volume) / 100;
    if (s > 32767)  s = 32767;
    if (s < -32768) s = -32768;
    *sample = (int16_t)s;
}

/*
 * WHAT : 等待描述符归还 CPU（owner 位变 0）
 * WHY  : 分块写时不能覆盖 DMA 正在搬运的缓冲区
 * HOW  : v1 忙等轮询 owner 位（上限 DMA_POLL_LIMIT 防死等）；
 *        信号量方案登记在 NEXT_STEPS（避免过度设计）
 * 返回 : true 可写 / false 超时
 */
static bool dma_wait_desc_cpu(uint32_t idx)
{
    uint32_t spins = 0;

    while (g_audio.dma_desc[idx].owner != ESP32S3_DMA_DESC_OWNER_CPU) {
        if (++spins > DMA_POLL_LIMIT)
            return false;
    }

    return true;
}

/*
 * WHAT : 填充一个 DMA 缓冲并移交硬件
 * HOW  : 16-bit 样本逐个应用音量后写入，末尾奇数字节补 0 对齐；
 *        填完置 length 并把 owner 交给 DMA
 */
static void dma_fill_chunk(uint32_t idx, const uint8_t *data, size_t chunk)
{
    int16_t *dst = (int16_t *)g_audio.dma_buf[idx];
    size_t samples = chunk / 2;
    size_t i;

    for (i = 0; i < samples; i++) {
        int16_t s = (int16_t)((uint16_t)data[i * 2] |
                              ((uint16_t)data[i * 2 + 1] << 8));
        apply_volume(&s, g_audio.volume);
        dst[i] = s;
    }

    /* 奇数长度补一个静音样本凑整 / pad to even length */
    if (chunk & 1) {
        dst[samples] = 0;
        samples++;
        chunk = samples * 2;
    }

    g_audio.dma_desc[idx].length = (uint32_t)chunk;
    g_audio.dma_desc[idx].owner = ESP32S3_DMA_DESC_OWNER_DMA;
}

#ifdef CONFIG_RETRO_AUDIO_TEST_TONE

/*
 * WHAT : 生成 440Hz 正弦测试音（调试用）
 * WHY  : 仅在定义 CONFIG_RETRO_AUDIO_TEST_TONE 时参与编译，
 *        原实现无任何调用者（悬空函数）
 * WHEN : audio_start() 预填充缓冲时调用（替代静音填充）
 * HOW  : 32.32 相位累加 + sin 查值，双声道同值
 */
static void fill_buffer(int16_t *buf, int samples)
{
    static uint32_t phase = 0;
    uint32_t step = (uint32_t)((440.0 * 4294967296.0) /
                               (double)g_audio.sample_rate);
    int i;

    for (i = 0; i < samples; i++) {
        phase += step;

        int16_t sample = (int16_t)(sin((double)phase / 4294967296.0 *
                                       2.0 * M_PI) * 16000.0);
        apply_volume(&sample, g_audio.volume);

        buf[i * 2]     = sample;  /* L */
        buf[i * 2 + 1] = sample;  /* R */
    }
}

#endif /* CONFIG_RETRO_AUDIO_TEST_TONE */

/*
 * WHAT : 填充静音
 * HOW  : 全 0（有符号 16-bit 中点）
 */
static void fill_silence(int16_t *buf, int samples)
{
    for (int i = 0; i < samples * 2; i++)
        buf[i] = 0;
}

/*==========================
 *  公开 API
 *==========================*/

/*
 * WHAT : 初始化音频子系统
 * HOW  : 复位 I2S -> 配引脚 -> PCM 模式 -> 时钟 -> DMA 环 ->
 *        挂接中断（目标机）
 * 返回 : OK / -ENOMEM / -EIO（中断挂接失败）
 */
int audio_init(void)
{
    int ret;

    if (g_audio.initialized) {
        syslog(LOG_INFO, "Audio: already initialized\n");
        return OK;
    }

    syslog(LOG_INFO, "Audio: initializing I2S0\n");

    memset(g_audio.dma_buf, 0, sizeof(g_audio.dma_buf));

    g_audio.sample_rate     = CONFIG_RETRO_AUDIO_SAMPLE_RATE;
    g_audio.bits_per_sample = 16;
    g_audio.channels        = 2;
    g_audio.volume          = 80;
    g_audio.initialized     = true;
    g_audio.playing         = false;
    g_audio.paused          = false;

    i2s_reset();
    i2s_config_pins();
    i2s_config_pcm_mode();
    i2s_set_clock(g_audio.sample_rate,
                  g_audio.bits_per_sample, g_audio.channels);

    ret = dma_init_chain();
    if (ret < 0) {
        syslog(LOG_ERR, "Audio: DMA init failed: %d\n", ret);
        g_audio.initialized = false;
        return ret;
    }

    /* 填充初始静音缓冲（测试音模式除外）*/
    for (int i = 0; i < AUDIO_DMA_BUF_COUNT * 2; i++) {
#ifdef CONFIG_RETRO_AUDIO_TEST_TONE
        fill_buffer((int16_t *)g_audio.dma_buf[i],
                    AUDIO_DMA_BUF_SIZE / (g_audio.bits_per_sample / 8) / 2);
#else
        fill_silence((int16_t *)g_audio.dma_buf[i],
                     AUDIO_DMA_BUF_SIZE / (g_audio.bits_per_sample / 8) / 2);
#endif
    }

#ifdef ESP32S3_IRQ_I2S0
    /* 挂接 I2S0 中断。
     * esp32s3_setup_irq 原型在 kernel 内部头（xtensa/src/esp32s3/
     * esp32s3_irq.h）——apps 模块不可达，这里按公开签名直接声明 */
    extern int esp32s3_setup_irq(int cpu, int periphid, int priority, int flags);
#ifndef ESP32S3_CPUINT_LEVEL
#  define ESP32S3_CPUINT_LEVEL 1
#endif
    ret = irq_attach(ESP32S3_IRQ_I2S0, i2s_dma_isr, NULL);
    if (ret < 0) {
        syslog(LOG_ERR, "Audio: irq_attach failed: %d\n", ret);
        return ret;
    }

    ret = esp32s3_setup_irq(0, ESP32S3_PERIPH_I2S0, 1, ESP32S3_CPUINT_LEVEL);
    if (ret < 0) {
        syslog(LOG_ERR, "Audio: esp32s3_setup_irq failed: %d\n", ret);
        irq_detach(ESP32S3_IRQ_I2S0);
        return ret;
    }

    up_enable_irq(ESP32S3_IRQ_I2S0);
    i2s_wr(I2S_INT_ENA, ESP32S3_I2S_TX_DONE_INT);
#else
    /*
     * TODO(中断挂接): 中断号宏不可用（宿主机/无芯片头环境）。
     * 已核实的目标机头文件路径：
     *   - ESP32S3_IRQ_I2S0 / ESP32S3_PERIPH_I2S0:
     *     deps/nuttx/arch/xtensa/include/esp32s3/irq.h
     *   - esp32s3_setup_irq():
     *     deps/nuttx/arch/xtensa/src/esp32s3/esp32s3_irq.h
     * 无中断时播放依赖 play_pcm 的 owner 轮询，功能可用但状态计数
     * 不含 ISR 路径。
     */
    syslog(LOG_WARNING, "Audio: ISR not attached (no IRQ macro)\n");
#endif

    syslog(LOG_INFO, "Audio: I2S0 initialized (%lu Hz, %d-bit, %d ch)\n",
           (unsigned long)g_audio.sample_rate,
           g_audio.bits_per_sample, g_audio.channels);

    return OK;
}

/*
 * WHAT : 启动音频播放
 * HOW  : 复位 DMA -> 重配模式/时钟 -> 启动 DMA 环；幂等
 */
int audio_start(void)
{
    if (!g_audio.initialized)
        return -ENODEV;

    if (g_audio.playing)
        return OK;

    dma_stop();
    i2s_reset();
    i2s_config_pcm_mode();
    i2s_set_clock(g_audio.sample_rate,
                  g_audio.bits_per_sample, g_audio.channels);

    g_audio.write_idx = 0;
    g_audio.irq_idx = 0;

    dma_start();

    g_audio.playing = true;
    g_audio.paused = false;

    syslog(LOG_INFO, "Audio: started\n");
    return OK;
}

/*
 * WHAT : 停止音频播放
 */
int audio_stop(void)
{
    if (!g_audio.playing)
        return OK;

    dma_stop();

    g_audio.playing = false;
    g_audio.paused = false;

    syslog(LOG_INFO, "Audio: stopped\n");
    return OK;
}

/*
 * WHAT : 暂停音频播放
 * HOW  : 置 g_audio.paused 门控（play_pcm 拒绝新数据）并停 TX 静音
 * 返回 : OK / -ENODEV / -EALREADY
 */
int audio_pause(void)
{
    if (!g_audio.initialized)
        return -ENODEV;

    if (!g_audio.playing || g_audio.paused)
        return -EALREADY;

    i2s_clr_bit(I2S_CONF, I2S_CONF_TX_START);
    g_audio.paused = true;

    syslog(LOG_INFO, "Audio: paused\n");
    return OK;
}

/*
 * WHAT : 恢复音频播放
 * HOW  : 清暂停门控并重置 TX_START（DMA 环保持运行）
 * 返回 : OK / -ENODEV / -EALREADY
 */
int audio_resume(void)
{
    if (!g_audio.initialized)
        return -ENODEV;

    if (!g_audio.paused)
        return -EALREADY;

    i2s_set_bit(I2S_CONF, I2S_CONF_TX_START);
    g_audio.paused = false;

    syslog(LOG_INFO, "Audio: resumed\n");
    return OK;
}

/*
 * WHAT : 设置采样率
 * HOW  : 播放中先停再改时钟后重启
 * 返回 : OK / -ENODEV / -EINVAL
 */
int audio_set_sample_rate(uint32_t rate)
{
    if (!g_audio.initialized)
        return -ENODEV;

    if (rate < 8000 || rate > 48000)
        return -EINVAL;

    bool was_playing = g_audio.playing;
    if (was_playing)
        audio_stop();

    g_audio.sample_rate = rate;
    i2s_set_clock(rate, g_audio.bits_per_sample, g_audio.channels);

    syslog(LOG_INFO, "Audio: sample rate set to %lu Hz\n",
           (unsigned long)rate);

    if (was_playing)
        audio_start();

    return OK;
}

/*
 * WHAT : 设置音量（0-100）
 * WHY  : common 播放器口径为 0-100，超出截断
 */
int audio_set_volume(uint8_t volume)
{
    if (volume > 100)
        volume = 100;

    g_audio.volume = volume;
    syslog(LOG_INFO, "Audio: volume set to %u%%\n", volume);
    return OK;
}

/*
 * WHAT : 获取当前音量（0-100）
 */
uint8_t audio_get_volume(void)
{
    return g_audio.volume;
}

/*
 * WHAT : 获取音频状态
 * HOW  : 出参逐项回填（调用方 drv_player.c 五元组）
 */
void audio_get_status(bool *playing, bool *paused,
                      uint32_t *sample_rate, uint8_t *volume,
                      uint32_t *bytes_played)
{
    if (playing)
        *playing = g_audio.playing && !g_audio.paused;
    if (paused)
        *paused = g_audio.paused;
    if (sample_rate)
        *sample_rate = g_audio.sample_rate;
    if (volume)
        *volume = g_audio.volume;
    if (bytes_played)
        *bytes_played = g_audio.total_played_bytes;
}

/*
 * WHAT : 打印音频状态（audio status 命令用）
 */
void audio_print_status(void)
{
    bool playing, paused;
    uint32_t rate, bytes;
    uint8_t vol;

    audio_get_status(&playing, &paused, &rate, &vol, &bytes);

    printf("\n");
    printf("=== Audio Status ===\n");
    printf("State:    %s\n", playing ? (paused ? "PAUSED" : "PLAYING") : "STOPPED");
    printf("Sample Rate: %lu Hz\n", (unsigned long)rate);
    printf("Bit Depth:   %d-bit\n", g_audio.bits_per_sample);
    printf("Channels:    %d\n", g_audio.channels);
    printf("Volume:     %u%%\n", vol);
    printf("Played:     %lu bytes\n", (unsigned long)bytes);
    printf("Underruns:  %lu\n", (unsigned long)g_audio.underrun_count);
    printf("Overruns:   %lu\n", (unsigned long)g_audio.overrun_count);
    printf("\n");
}

/*
 * WHAT : 播放 PCM 数据（阻塞写入 DMA 环）
 * WHY  : 原实现是空壳（TODO 占位），common 播放器喂数据会全部丢弃
 * WHEN : 参数为 16-bit 小端 PCM（声道数按初始化配置）
 * HOW  : 每块先轮询等描述符归还 CPU，音量缩放后填充并移交 DMA；
 *        暂停时返回 -EBUSY；首次调用自动 audio_start()
 * 返回 : OK / -ENODEV / -EBUSY / -EINVAL / -EIO（轮询超时）
 */
int audio_play_pcm(const void *data, size_t len)
{
    const uint8_t *src;
    size_t offset = 0;

    if (!g_audio.initialized)
        return -ENODEV;

    if (g_audio.paused)
        return -EBUSY;

    if (data == NULL || len == 0)
        return -EINVAL;

    if (!g_audio.playing) {
        int ret = audio_start();
        if (ret < 0)
            return ret;
    }

    src = (const uint8_t *)data;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > AUDIO_DMA_BUF_SIZE)
            chunk = AUDIO_DMA_BUF_SIZE;

        if (!dma_wait_desc_cpu(g_audio.write_idx)) {
            syslog(LOG_ERR, "Audio: DMA descriptor timeout\n");
            return -EIO;
        }

        dma_fill_chunk(g_audio.write_idx, &src[offset], chunk);
        offset += chunk;

        g_audio.write_idx = (g_audio.write_idx + 1) % (AUDIO_DMA_BUF_COUNT * 2);
    }

    return OK;
}

/*
 * WHAT : 播放 WAV 文件（解析+播放）
 * HOW  : TODO：WAV 解析在 common/driver/drv_player.c 已有，驱动层
 *        不再重复实现，保留入口仅返回 -ENOSYS
 */
int audio_play_wav(const char *filename)
{
    syslog(LOG_INFO, "Audio: playing WAV: %s (not implemented)\n", filename);
    return -ENOSYS;
}

/*
 * WHAT : 关闭音频
 * HOW  : 停播清标志；DMA 缓冲为动态分配，保留复用
 */
void audio_deinit(void)
{
    audio_stop();
    g_audio.initialized = false;
}

/*==========================
 *  NSH 命令
 *==========================*/

int cmd_audio(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: audio <play|stop|pause|resume|rate|volume|status>\n");
        return OK;
    }

    if (strcmp(argv[1], "play") == 0) {
        audio_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        audio_stop();
    } else if (strcmp(argv[1], "pause") == 0) {
        audio_pause();
    } else if (strcmp(argv[1], "resume") == 0) {
        audio_resume();
    } else if (strcmp(argv[1], "rate") == 0) {
        if (argc > 2)
            audio_set_sample_rate(atoi(argv[2]));
        else
            printf("Current rate: %lu Hz\n",
                   (unsigned long)g_audio.sample_rate);
    } else if (strcmp(argv[1], "volume") == 0 || strcmp(argv[1], "vol") == 0) {
        if (argc > 2)
            audio_set_volume(atoi(argv[2]));
        else
            printf("Current volume: %u%%\n", audio_get_volume());
    } else if (strcmp(argv[1], "status") == 0) {
        audio_print_status();
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return OK;
}

#endif /* CONFIG_ESP32S3_I2S */
