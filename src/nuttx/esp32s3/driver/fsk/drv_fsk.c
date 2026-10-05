/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_fsk.c - FSK 磁带驱动（S3）
 *
 * WHAT : FSK 磁带驱动（S3）
 * WHY  : KCS 300 波特磁带调制解调
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/driver/fsk/drv_fsk.c
 * WHEN : 2026-03~04 初版，2026-10-04 修复频率宏反逻辑/块大小/
 *        Goertzel 溢出，并如实标注 RX 现状
 * HOW  : 1200/2400Hz Goertzel 检测 + 调制输出
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/mm/mm.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>
#include <malloc.h>
#include <math.h>

#include "esp32s3.h"
#include "board.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/*==========================
 *  配置
 *==========================*/

/* 2026-10-04 修复：原逻辑仅在 RETRO_FSK 关闭时兜底定义波特率，
 * 开启时（默认 y）反而缺定义编不过——改为无条件兜底（Kconfig 同名
 * int 项存在时以 Kconfig 为准） */
#ifndef CONFIG_RETRO_FSK_BAUD
#  define CONFIG_RETRO_FSK_BAUD 300
#endif

/*==========================
 *  FSK 参数
 *==========================*/

/* 采样率 */
#define FSK_SAMPLE_RATE     44100
#define FSK_SAMPLE_PERIOD   (1000000 / FSK_SAMPLE_RATE)  /* μs */

/* 码元持续时间（微秒）*/
#define FSK_BIT_US          (1000000 / CONFIG_RETRO_FSK_BAUD)

/* Goertzel 算法参数 */
#define GOERTZEL_N          64      /* 每个码元的采样数 */
#define GOERTZEL_THRESHOLD  1000000 /* 检测阈值（预留，当前仅比较两频率能量）*/

/*
 * 默认频率（Kansas City Standard 300 baud）
 *
 * 统一方案（两块板一致）/ Unified scheme (both boards):
 *   #ifndef 只负责给 CONFIG 宏本身兜底缺省值，代码中所有初始化
 *   一律引用 CONFIG 宏。原实现用 #ifndef 定义独立的 DEFAULT_FREQ0，
 *   却在初始化中无条件使用——一旦 Kconfig 真的给出
 *   CONFIG_RETRO_FSK_LOGIC0_FREQ，DEFAULT_FREQ0 即未定义，编译失败。
 * The #ifndef provides the fallback for the CONFIG macro itself and
 * initializers always reference the CONFIG macro (the old inverted
 * #ifndef/DEFAULT_FREQ0 scheme broke when Kconfig supplied a value).
 */
#ifndef CONFIG_RETRO_FSK_LOGIC0_FREQ
#  define CONFIG_RETRO_FSK_LOGIC0_FREQ 1200
#endif
#ifndef CONFIG_RETRO_FSK_LOGIC1_FREQ
#  define CONFIG_RETRO_FSK_LOGIC1_FREQ 2400
#endif

/*==========================
 *  码速率配置表
 *==========================*/

struct fsk_baud_config {
    int baud;
    int freq0;
    int freq1;
    int samples_per_bit;  /* 每个码元的采样数 */
};

static const struct fsk_baud_config g_baud_table[] = {
    {   300,  1200,  2400, FSK_SAMPLE_RATE / 300  },
    {   600,  1200,  2400, FSK_SAMPLE_RATE / 600  },
    {  1200,  2400,  4800, FSK_SAMPLE_RATE / 1200 },
    {  2400,  4800,  9600, FSK_SAMPLE_RATE / 2400 },
    {  4800,  4800,  9600, FSK_SAMPLE_RATE / 4800 }, /* 默认 */
    {  9600,  9600, 19200, FSK_SAMPLE_RATE / 9600 },
};

#define BAUD_TABLE_SIZE (sizeof(g_baud_table) / sizeof(g_baud_table[0]))

/*==========================
 *  全局变量
 *==========================*/

static bool g_fsk_initialized = false;
static bool g_fsk_tx_active = false;
static bool g_fsk_rx_active = false;

static int g_fsk_baud = CONFIG_RETRO_FSK_BAUD;
static int g_fsk_freq0 = CONFIG_RETRO_FSK_LOGIC0_FREQ;
static int g_fsk_freq1 = CONFIG_RETRO_FSK_LOGIC1_FREQ;
static int g_fsk_samples_per_bit = FSK_SAMPLE_RATE / CONFIG_RETRO_FSK_BAUD;

/* 环形缓冲区 */
#define FSK_RX_BUF_SIZE  4096
static uint8_t g_rx_buf[FSK_RX_BUF_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;

/*
 * Goertzel 状态（float 累加）
 *
 * 精度选型（2026-10-04）/ Precision choice:
 * 原实现用 int32 状态 + int64 中间量，最后截断回 int32——真实信号
 * 下 s1^2 项轻易超过 INT32_MAX 造成能量翻转。改为 float 单精度：
 *   - Xtensa LX7 带单精度 FPU，float 运算反而比 int64 乘加快；
 *   - 64 样本窗内能量动态范围远在 float 可表示范围内；
 *   - 与阈值比较只需相对大小，单精度裕度充足。
 * The old int32/int64 mix truncated to int32 and overflowed on real
 * signals; float accumulates natively on the LX7 FPU with ample
 * headroom for a 64-sample window.
 */
static struct goertzel_state {
    float   s1_0, s2_0;   /* 频率0的滤波器状态 / filter state for f0 */
    float   s1_1, s2_1;   /* 频率1的滤波器状态 / filter state for f1 */
    float   coeff_0;      /* 频率0的系数 2cos(2πk/N) / coeff for f0 */
    float   coeff_1;      /* 频率1的系数 / coeff for f1 */
    int16_t sample_count;
    bool    enabled;
} g_goertzel;

/* 正弦表（用于调制）*/
#define SINE_TABLE_SIZE 256
static int16_t g_sine_table[SINE_TABLE_SIZE];

/*==========================
 *  内部函数
 *==========================*/

/**
 * 生成正弦表
 */
static void gen_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double angle = 2.0 * M_PI * i / SINE_TABLE_SIZE;
        g_sine_table[i] = (int16_t)(32767.0 * sin(angle));
    }
}

/**
 * 获取码速率配置
 */
static const struct fsk_baud_config *fsk_get_baud_config(int baud)
{
    for (size_t i = 0; i < BAUD_TABLE_SIZE; i++) {
        if (g_baud_table[i].baud == baud)
            return &g_baud_table[i];
    }
    return &g_baud_table[0]; /* 默认 300 baud (KCS) */
}

/**
 * 设置码速率
 */
int fsk_set_baud(int baud)
{
    const struct fsk_baud_config *cfg = fsk_get_baud_config(baud);
    if (!cfg)
        return -EINVAL;

    g_fsk_baud = cfg->baud;
    g_fsk_freq0 = cfg->freq0;
    g_fsk_freq1 = cfg->freq1;
    g_fsk_samples_per_bit = cfg->samples_per_bit;

    syslog(LOG_INFO, "FSK: baud set to %d (f0=%dHz, f1=%dHz)\n",
           g_fsk_baud, g_fsk_freq0, g_fsk_freq1);
    return OK;
}

/*
 * WHAT : 计算单个频率的 Goertzel 系数
 * WHY  : 系数 = 2cos(2πk/N)，k = N*f/Fs 取整
 * HOW  : float 直存（不做 Q15 定标，见 goertzel_state 选型说明）
 */
static float goertzel_calc_coeff(int target_freq)
{
    int k = (GOERTZEL_N * target_freq) / FSK_SAMPLE_RATE;
    if (k < 1)
        k = 1;
    if (k >= GOERTZEL_N)
        k = GOERTZEL_N - 1;
    double wk = 2.0 * M_PI * k / GOERTZEL_N;
    return (float)(2.0 * cos(wk));
}

/**
 * Goertzel 算法初始化
 */
static void goertzel_init(void)
{
    g_goertzel.s1_0 = g_goertzel.s2_0 = 0.0f;
    g_goertzel.s1_1 = g_goertzel.s2_1 = 0.0f;
    g_goertzel.coeff_0 = goertzel_calc_coeff(g_fsk_freq0);
    g_goertzel.coeff_1 = goertzel_calc_coeff(g_fsk_freq1);
    g_goertzel.sample_count = 0;
    g_goertzel.enabled = true;
}

/*
 * WHAT : Goertzel 滤波器同时更新两个频率
 * HOW  : s = x[n] + coeff * s1 - s2；s2 = s1; s1 = s（float 单精度）
 */
static void goertzel_step_dual(int16_t sample)
{
    /* 频率0 / Frequency 0 */
    float s0 = (float)sample +
               g_goertzel.coeff_0 * g_goertzel.s1_0 - g_goertzel.s2_0;
    g_goertzel.s2_0 = g_goertzel.s1_0;
    g_goertzel.s1_0 = s0;

    /* 频率1 / Frequency 1 */
    float s1 = (float)sample +
               g_goertzel.coeff_1 * g_goertzel.s1_1 - g_goertzel.s2_1;
    g_goertzel.s2_1 = g_goertzel.s1_1;
    g_goertzel.s1_1 = s1;
}

/*
 * WHAT : 计算 Goertzel 能量（指定频率状态）
 * WHY  : 原实现 int64 累加后截断 int32，真实信号溢出（见选型说明）
 * HOW  : 全 float 端到端：E = s1² + s2² − coeff·s1·s2
 * 返回 : 非负能量（float，无穷大不可能——64 样本窗动态范围内）
 */
static float goertzel_compute_power_for(float s1, float s2, float coeff)
{
    float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return (p > 0.0f) ? p : 0.0f;
}

/**
 * 解调一个采样
 * 返回: 0=逻辑0, 1=逻辑1, -1=等待更多采样
 */
static int goertzel_demod_sample(int16_t sample)
{
    /* 同时更新两个频率的 Goertzel 状态 */
    goertzel_step_dual(sample);

    g_goertzel.sample_count++;

    if (g_goertzel.sample_count >= GOERTZEL_N) {
        float power0 = goertzel_compute_power_for(
            g_goertzel.s1_0, g_goertzel.s2_0, g_goertzel.coeff_0);
        float power1 = goertzel_compute_power_for(
            g_goertzel.s1_1, g_goertzel.s2_1, g_goertzel.coeff_1);

        goertzel_init();

        /* 返回能量更大的频率对应的逻辑值 */
        return (power1 > power0) ? 1 : 0;
    }

    return -1; /* 采样不足 */
}

/*==========================
 *  调制（TX）
 *==========================*/

/**
 * 生成 FSK 音频样本
 * phase: 相位累加器（32.32 定点）
 * freq: 频率
 * 返回: 下一个相位
 */
static uint32_t fsk_gen_tone(uint32_t phase, int freq)
{
    phase += (uint32_t)((uint64_t)freq * 0x100000000ULL / FSK_SAMPLE_RATE);
    return phase;
}

/**
 * 调制一个字节（8N1 格式: 1起始位 + 8数据位 + 1停止位）
 */
int fsk_modulate_byte(uint8_t byte, int16_t *audio_buf, int max_samples)
{
    if (!g_fsk_initialized || max_samples < g_fsk_samples_per_bit * 10)
        return -ENOBUFS;

    int sample_idx = 0;
    uint32_t phase = 0;
    int freq;

    /* 起始位 = 0 */
    freq = g_fsk_freq0;
    for (int i = 0; i < g_fsk_samples_per_bit; i++) {
        phase = fsk_gen_tone(phase, freq);
        int idx = (phase >> 24) & (SINE_TABLE_SIZE - 1);
        audio_buf[sample_idx++] = g_sine_table[idx];
    }

    /* 数据位（LSB 在前）*/
    for (int bit = 0; bit < 8; bit++) {
        freq = (byte & (1 << bit)) ? g_fsk_freq1 : g_fsk_freq0;
        for (int i = 0; i < g_fsk_samples_per_bit; i++) {
            phase = fsk_gen_tone(phase, freq);
            int idx = (phase >> 24) & (SINE_TABLE_SIZE - 1);
            audio_buf[sample_idx++] = g_sine_table[idx];
        }
    }

    /* 停止位 = 1 */
    freq = g_fsk_freq1;
    for (int i = 0; i < g_fsk_samples_per_bit; i++) {
        phase = fsk_gen_tone(phase, freq);
        int idx = (phase >> 24) & (SINE_TABLE_SIZE - 1);
        audio_buf[sample_idx++] = g_sine_table[idx];
    }

    return sample_idx;
}

/*
 * WHAT : 发送原始数据（分块调制 -> I2S/DAC 硬件 DMA 播放）
 * WHY  : 磁带接口按字节流写入；2026-10-04 晚接通硬件路径
 *        （HARDWARE.md 13.1：FSK TX = audio_play_pcm -> I2S/GDMA，
 *        替代原 TODO 占位——禁止 CPU 位摆出音频）
 * HOW  : 块大小用运行时 g_fsk_samples_per_bit 计算；每字节调制
 *        出 10 个码元的采样后经音频驱动阻塞写入 DMA 环
 * 返回 : 发送字节数 / -ENODEV / -ENOMEM / -ENOSYS（无音频驱动）
 */
int fsk_send(const uint8_t *data, size_t len)
{
#ifndef CONFIG_RETRO_AUDIO
    return -ENOSYS;
#else
    extern int audio_play_pcm(const void *pcm, size_t len_bytes);

    if (!g_fsk_initialized || !g_fsk_tx_active)
        return -ENODEV;

    /* 每字节 10 个码元（起始+8 数据+停止），按当前波特率取采样数 */
    size_t chunk_samples = (size_t)g_fsk_samples_per_bit * 10;
    size_t total_samples = 0;
    int sent_bytes = 0;

    int16_t *audio_buf = kmm_malloc(chunk_samples * sizeof(int16_t));
    if (audio_buf == NULL)
        return -ENOMEM;

    for (size_t i = 0; i < len; i++) {
        int n = fsk_modulate_byte(data[i], audio_buf, (int)chunk_samples);
        if (n < 0)
            break;

        /* 硬件出声：I2S + GDMA 环形链（S3 外部 DAC / CAM 内置 DAC） */
        int ret = audio_play_pcm(audio_buf, (size_t)n * sizeof(int16_t));
        if (ret < 0) {
            syslog(LOG_ERR, "FSK: audio_play_pcm failed: %d\n", ret);
            break;
        }

        total_samples += n;
        sent_bytes++;
    }

    kmm_free(audio_buf);

    syslog(LOG_INFO, "FSK: sent %d bytes (%d samples)\n",
           sent_bytes, (int)total_samples);
    return sent_bytes;
#endif
}

/*==========================
 *  解调（RX）
 *==========================*/

/*
 * ================== RX 现状说明（5W1H）==================
 * WHAT : 当前 RX 路径仅实现"逐 64 采样窗的双频能量比较"
 * WHY  : 完整 KCS/UART 成帧需要起始位检测、码元中心采样、
 *        逐位组装与停止位校验，当前均未实现
 * WHO  : 使用者需知——fsk_recv_nonblock 读到的是"每窗一个比特"，
 *        不是解帧后的字节
 * WHERE: 本文件 fsk_rx_isr / fsk_recv_nonblock / fsk_recv_byte
 * WHEN : 2026-10-04 如实登记（不做重设计）
 * HOW  : 即 仅支持连续载波监测/演示；
 *        完整 UART 成帧（起始位对齐 + 位中心采样 + 8N1 组包）
 *        已在 NEXT_STEPS 登记，待音频 DMA 通道打通后实现。
 * =======================================================
 * Honest status: RX currently detects one bit per fixed
 * 64-sample window with no start-bit detection, no bit-center
 * sampling and no UART framing — carrier-monitoring/demo only.
 * Full UART framing is tracked in NEXT_STEPS.
 */

/**
 * 接收一个字节（阻塞）
 */
int fsk_recv_byte(void)
{
    if (!g_fsk_initialized || !g_fsk_rx_active)
        return -ENODEV;

    /* 等待起始位（逻辑0）*/
    while (1) {
        /* TODO: 从 I2S DMA 缓冲区读取采样
         * int16_t sample = i2s_read_sample();
         */
        (void)0;

        /* 等待接收到起始位
         * 这是一个简化的占位符，实际需要 DMA 中断驱动
         * See RX 现状说明：完整成帧未实现 / see status note above */
        up_udelay(1000);
        return 0; /* 占位 */
    }
}

/**
 * 非阻塞接收
 */
int fsk_recv_nonblock(uint8_t *buf, size_t len)
{
    if (!g_fsk_rx_active)
        return -ENODEV;

    size_t received = 0;
    while (received < len && g_rx_head != g_rx_tail) {
        buf[received++] = g_rx_buf[g_rx_tail++];
        if (g_rx_tail >= FSK_RX_BUF_SIZE)
            g_rx_tail = 0;
    }

    return received;
}

/**
 * 接收中断处理（由 DMA 中断调用）
 */
void fsk_rx_isr(int16_t sample)
{
    if (!g_fsk_rx_active)
        return;

    int bit = goertzel_demod_sample(sample);
    if (bit >= 0) {
        /* 一个码元解调完成，存入缓冲区
         *（注意：这是"每窗一比特"，非成帧字节，见 RX 现状说明）*/
        uint16_t next_head = (g_rx_head + 1) % FSK_RX_BUF_SIZE;
        if (next_head != g_rx_tail) {
            g_rx_buf[g_rx_head] = (uint8_t)bit;
            g_rx_head = next_head;
        }
    }
}

/*==========================
 *  初始化和控制
 *==========================*/

/**
 * 初始化 FSK 调制解调器
 */
int fsk_init(void)
{
    if (g_fsk_initialized) {
        syslog(LOG_INFO, "FSK: already initialized\n");
        return OK;
    }

    syslog(LOG_INFO, "FSK: initializing (baud=%d, f0=%dHz, f1=%dHz)\n",
           g_fsk_baud, g_fsk_freq0, g_fsk_freq1);

    /* 生成正弦表 */
    gen_sine_table();

    /* 初始化 Goertzel */
    goertzel_init();

    /* 清空接收缓冲区 */
    g_rx_head = g_rx_tail = 0;

    /* TODO: 初始化 I2S 用于音频输入/输出
     * 1. 配置 I2S 输入（ADC 模式）
     * 2. 配置 DMA 接收
     * 3. 注册中断处理
     */

    g_fsk_initialized = true;
    g_fsk_tx_active = true;
    g_fsk_rx_active = true;

    syslog(LOG_INFO, "FSK: initialized successfully\n");
    return OK;
}

/**
 * 启动发送
 */
int fsk_tx_start(void)
{
    if (!g_fsk_initialized)
        return -ENODEV;
    g_fsk_tx_active = true;
    return OK;
}

/**
 * 停止发送
 */
int fsk_tx_stop(void)
{
    g_fsk_tx_active = false;
    return OK;
}

/**
 * 启动接收
 */
int fsk_rx_start(void)
{
    if (!g_fsk_initialized)
        return -ENODEV;
    g_fsk_rx_active = true;
    goertzel_init();
    return OK;
}

/**
 * 停止接收
 */
int fsk_rx_stop(void)
{
    g_fsk_rx_active = false;
    return OK;
}

/**
 * 获取状态信息
 */
void fsk_get_status(bool *tx_active, bool *rx_active, int *baud)
{
    *tx_active = g_fsk_tx_active;
    *rx_active = g_fsk_rx_active;
    *baud = g_fsk_baud;
}
