/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_audio_dac.c - DAC 音频驱动
 *
 * WHAT : DAC 音频驱动
 * WHY  : 内置 DAC2(GPIO26) 输出 + ADC(GPIO34) 输入
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32/driver/audio/drv_audio_dac.c
 * WHEN : 2026-03~04 初版，2026-10-04 修复 DMA 启动/分块播放/补齐 API
 * HOW  : I2S0 DAC 模式（仅 I2S0 支持），双缓冲 DMA 环 + 描述符 owner 轮询
 */

/**
 * drv_audio_dac.c - ESP32 DAC 音频驱动
 *
 * 使用 ESP32 内置 DAC (GPIO26) 输出模拟音频
 * 与 ESP32-S3 版本不同，无需外部 I2S DAC 芯片
 *
 * 硬件连接：
 *   GPIO26 (DAC2) --> 耦合电容 --> 音频放大器 / 扬声器
 *
 * 重要限制：
 *   ESP32 只有 I2S0 支持 DAC 直连模式（I2S1 不支持）。
 *   当 I2S0 同时用于 CVBS (DAC1/GPIO25) 和音频 (DAC2/GPIO26) 时，
 *   两者共用 I2S0 的 DMA 通道，需要在 CVBS 行消隐期插入音频数据。
 *   当前实现为占位，实际需要时分复用设计。
 *
 * 2026-10-04 修复要点 / Fix notes:
 *   1. OUT_LINK 寄存器：描述符地址只取低 20 位，且必须置
 *      OUTLINK_START（位 29）。原实现写裸 32 位指针且从不置启动位，
 *      DMA 永远不会启动。
 *   2. audio_play() 原先把所有数据块覆盖写进同一缓冲区、无完成等待，
 *      只有最后一块能发声。现改为双缓冲环 + 描述符 owner 位轮询的
 *      分块阻塞写（v1 选择轮询而非中断，理由见 dma_wait_desc_cpu）。
 *   3. 补齐 common 层（drv_player.c/drv_recorder.c）调用的
 *      audio_start/pause/resume/play_pcm/get_status。
 *   4. 音量入参统一为 0-100（播放器口径），内部换算 0-255。
 */

#include <nuttx/config.h>

/* 采样率兜底（apps 模块 Kconfig 未定义时） */
#ifndef CONFIG_RETRO_AUDIO_SAMPLE_RATE
#  define CONFIG_RETRO_AUDIO_SAMPLE_RATE 44100
#endif
#include <nuttx/arch.h>
#include <syslog.h>   /* syslog()/LOG_*：NuttX libc 头，宿主机为 glibc */
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "esp32.h"
#include "board.h"
#include "drv_audio.h"

#if defined(CONFIG_RETRO_AUDIO) && defined(CONFIG_RETRO_AUDIO_DAC)

/*==========================
 *  常量
 *==========================*/

/* 注意：ESP32 只有 I2S0 支持 DAC 直连模式
 * 音频实际需要与 CVBS 共用 I2S0（时分复用），当前使用 I2S0 并标记 TODO */
#define I2S_AUDIO_BASE     ESP32_I2S0_BASE
#define APB_CLK_HZ         80000000

/* 音频参数 */
#define AUDIO_DEFAULT_SAMPLE_RATE  CONFIG_RETRO_AUDIO_SAMPLE_RATE
#define AUDIO_CHANNELS             1     /* DAC 单声道 */
#define AUDIO_BITS                 8     /* DAC 8-bit */

/* DMA 双缓冲环 / double-buffer DMA ring */
#define AUDIO_DMA_BUF_COUNT  2
#define AUDIO_DMA_BUF_SIZE   1024

/* owner 轮询超时（循环数，约几十 ms 量级，防死循环）/ poll timeout */
#define DMA_POLL_LIMIT      2000000

/*==========================
 *  全局变量
 *==========================*/

static bool g_audio_initialized = false;
static bool g_audio_playing = false;   /* DMA/TX 已启动 / DMA+TX started */
static bool g_audio_paused = false;    /* 暂停门控 / pause gate */
static uint32_t g_sample_rate = AUDIO_DEFAULT_SAMPLE_RATE;
static uint8_t g_volume_pct = 80;      /* 0-100，common 口径 / 0-100 */
static uint32_t g_bytes_played = 0;    /* 已送入 DMA 的字节数 */
static int g_dma_next = 0;             /* 下一个可写缓冲槽 / next slot */

/* DMA 缓冲区 — 必须在内部 SRAM */
static uint8_t g_audio_dma_a[AUDIO_DMA_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t g_audio_dma_b[AUDIO_DMA_BUF_SIZE] __attribute__((aligned(16)));

/* DMA 描述符 */
static esp32_dma_descriptor_t g_audio_dma_desc[AUDIO_DMA_BUF_COUNT]
    __attribute__((aligned(4)));

/*==========================
 *  DMA 辅助
 *==========================*/

/*
 * WHAT : 等待指定描述符归还 CPU（owner 位变 0）
 * WHY  : 分块播放时不能覆盖 DMA 正在搬运的缓冲区
 * WHEN : audio_play/audio_play_pcm 每写入一块之前调用
 * HOW  : v1 采用忙等轮询而非 EOF 中断：音频路径暂无中断框架，
 *        1024 字节 @44.1kHz/8bit 约 23ms，忙等代价可接受；
 *        中断+信号量方案登记在 NEXT_STEPS（避免过度设计）
 * 返回 : true = 已归还可写；false = 超时（DMA 可能未启动或卡死）
 */
static bool dma_wait_desc_cpu(int idx)
{
    uint32_t spins = 0;

    while (g_audio_dma_desc[idx].owner != ESP32_I2S_DMA_DESC_OWNER_CPU) {
        if (++spins > DMA_POLL_LIMIT)
            return false;
    }

    return true;
}

/*
 * WHAT : 把一块 PCM 数据按音量缩放后填入 DMA 缓冲并移交 DMA
 * WHY  : 音量必须在填充时应用（DMA 原样搬运）
 * HOW  : data 为 unsigned 8-bit（0x80=静音中点），
 *        缩放系数 = g_volume_pct * 255 / 100（0-100 -> 0-255）
 */
static void dma_fill_chunk(int idx, const uint8_t *data, size_t chunk)
{
    uint8_t *buf = (idx == 0) ? g_audio_dma_a : g_audio_dma_b;
    int32_t scale = ((int32_t)g_volume_pct * 255) / 100;

    for (size_t i = 0; i < chunk; i++) {
        int16_t sample = (int16_t)data[i] - 128;
        sample = (int16_t)((sample * scale) >> 8);
        buf[i] = (uint8_t)(sample + 128);
    }

    g_audio_dma_desc[idx].length = (uint32_t)chunk;
    g_audio_dma_desc[idx].owner = ESP32_I2S_DMA_DESC_OWNER_DMA;
}

/*==========================
 *  I2S0 DAC 初始化
 *==========================*/

/*
 * WHAT : 配置 I2S0 为 DAC 音频输出模式
 * WHY  : ESP32 内置 DAC 只能通过 I2S0 直连输出（GPIO26 = DAC2）
 * WHEN : audio_init() 时调用一次
 * HOW  : 复位 -> 时钟分频 -> 采样位宽 -> DAC 模式位 ->
 *        挂 DMA 环：地址取低 20 位并置 OUTLINK_START（关键，
 *        不置位 DMA 永不启动，同 drv_cvbs_dac.c 的修复模式）
 * 注意 : 与 CVBS(DAC1) 时分复用尚未实现（TODO，见文件头）
 */
static void i2s_dac_audio_init(void)
{
    /* 1. 复位 I2S0 / reset I2S0 */
    putreg32(ESP32_I2S_TX_RESET | ESP32_I2S_RX_RESET |
             ESP32_I2S_TX_FIFO_RESET | ESP32_I2S_RX_FIFO_RESET,
             ESP32_I2S_CONF_REG(0));
    putreg32(0, ESP32_I2S_CONF_REG(0));

    /* 2. 配置时钟分频
     * APB_CLK / CLKM_DIV = 采样率 * 32 (I2S 帧格式)
     * 对于 44100Hz: CLKM_DIV = 80000000 / (44100 * 32) ≈ 56 */
    uint32_t clkm_div = APB_CLK_HZ / (g_sample_rate * 32);
    if (clkm_div < 2)
        clkm_div = 2;
    putreg32(clkm_div - 1, ESP32_I2S_CLKM_CONF_REG(0));

    /* 3. 采样配置：8-bit 单通道（DAC 直连 8-bit）*/
    putreg32(0x00010001, ESP32_I2S_SAMPLE_RATE_CONF_REG(0));

    /* 4. FIFO 配置：单字节出 FIFO / one byte per sample out of FIFO */
    putreg32(0x00000001, ESP32_I2S_FIFO_CONF_REG(0));

    /* 5. 使能 DAC 模式 + DAC 左声道使能（DAC2 = GPIO26）*/
    putreg32(ESP32_I2S_DAC_MODE_EN |
             ESP32_I2S_DAC_LEFT_ENA |
             ESP32_I2S_TX_MSB_SHIFT,
             ESP32_I2S_CONF_REG(0));

    /* 6. DMA 描述符环（A/B 互指，owner 初始归 CPU）*/
    for (int i = 0; i < AUDIO_DMA_BUF_COUNT; i++) {
        g_audio_dma_desc[i].eof    = 0;
        g_audio_dma_desc[i].owner  = ESP32_I2S_DMA_DESC_OWNER_CPU;
        g_audio_dma_desc[i].length = 0;
        g_audio_dma_desc[i].size   = AUDIO_DMA_BUF_SIZE;
        g_audio_dma_desc[i].buf    =
            (uint32_t)(i == 0 ? (uintptr_t)g_audio_dma_a
                              : (uintptr_t)g_audio_dma_b);
        g_audio_dma_desc[i].next   =
            (uint32_t)(uintptr_t)&g_audio_dma_desc[i ^ 1];
    }

    /* 7. 静音缓冲区（DAC 中点 0x80 = 静音）*/
    memset(g_audio_dma_a, 0x80, AUDIO_DMA_BUF_SIZE);
    memset(g_audio_dma_b, 0x80, AUDIO_DMA_BUF_SIZE);

    g_dma_next = 0;
}

/*==========================
 *  公开 API
 *==========================*/

/*
 * WHAT : 初始化音频 DAC
 * HOW  : 配置 I2S0/DMA 环；不立即启动发送（等 audio_start/play）
 * 返回 : OK；重复调用直接返回 OK
 */
int audio_init(void)
{
    if (g_audio_initialized)
        return OK;

    i2s_dac_audio_init();

    g_audio_initialized = true;
    g_audio_playing = false;
    g_audio_paused = false;

    syslog(LOG_INFO, "[AUDIO] DAC mode initialized: GPIO26, %luHz, 8-bit\n",
           (unsigned long)g_sample_rate);

    return OK;
}

/*
 * WHAT : 启动 DMA 与 I2S 发送
 * WHY  : common 播放器在喂数据前调用 audio_start()
 * HOW  : 挂描述符环（地址掩码到 [19:0] + OUTLINK_START）再置 TX_START；
 *        幂等，已启动则直接返回 OK
 */
int audio_start(void)
{
    if (!g_audio_initialized)
        return -ENODEV;

    if (g_audio_playing)
        return OK;

    putreg32((((uint32_t)(uintptr_t)&g_audio_dma_desc[0]) &
              ESP32_I2S_OUTLINK_ADDR_MASK) | ESP32_I2S_OUTLINK_START,
             ESP32_I2S_OUT_LINK_REG(0));

    uint32_t conf = getreg32(ESP32_I2S_CONF_REG(0));
    conf |= ESP32_I2S_TX_START;
    putreg32(conf, ESP32_I2S_CONF_REG(0));

    g_audio_playing = true;
    g_audio_paused = false;

    return OK;
}

/*
 * WHAT : 播放一段 PCM 数据（分块阻塞）
 * WHY  : WAV 播放器按块喂数据；只有最后一块留存在 DMA 里是缺陷
 * WHEN : 参数为 unsigned 8-bit 单声道 PCM（0x80 = 静音）
 * HOW  : 每块先轮询等描述符归还 CPU，再按音量填充并移交 DMA；
 *        暂停（g_audio_paused）时不消费数据，返回 -EBUSY；
 *        首次调用自动 audio_start()
 * 返回 : OK 成功；-ENODEV 未初始化；-EBUSY 已暂停；-EIO DMA 超时
 */
int audio_play(const uint8_t *data, size_t len)
{
    if (!g_audio_initialized)
        return -ENODEV;

    if (g_audio_paused)
        return -EBUSY;

    if (data == NULL || len == 0)
        return -EINVAL;

    if (!g_audio_playing) {
        int ret = audio_start();
        if (ret < 0)
            return ret;
    }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > AUDIO_DMA_BUF_SIZE)
            chunk = AUDIO_DMA_BUF_SIZE;

        if (!dma_wait_desc_cpu(g_dma_next)) {
            syslog(LOG_ERR, "[AUDIO] DMA descriptor timeout\n");
            return -EIO;
        }

        dma_fill_chunk(g_dma_next, &data[offset], chunk);
        g_bytes_played += chunk;
        offset += chunk;

        g_dma_next = (g_dma_next + 1) % AUDIO_DMA_BUF_COUNT;
    }

    return OK;
}

/*
 * WHAT : 原始 PCM 阻塞写（与 audio_play 同路径）
 * WHY  : common 播放器声明的 audio_play_pcm(data, len) 落到这里；
 *        ESP32 DAC 路径本身就是裸 8-bit PCM，无需格式转换
 */
int audio_play_pcm(const void *data, size_t len)
{
    return audio_play((const uint8_t *)data, len);
}

/*
 * WHAT : 停止播放
 * HOW  : 清 TX_START；DMA 环保留，audio_start 可再次启动
 */
int audio_stop(void)
{
    if (!g_audio_initialized)
        return -ENODEV;

    uint32_t conf = getreg32(ESP32_I2S_CONF_REG(0));
    conf &= ~ESP32_I2S_TX_START;
    putreg32(conf, ESP32_I2S_CONF_REG(0));

    g_audio_playing = false;
    g_audio_paused = false;
    return OK;
}

/*
 * WHAT : 暂停播放
 * WHY  : common 播放器 pause 命令
 * HOW  : 置 g_audio_paused 门控（audio_play/play_pcm 拒绝新数据）
 *        并停 TX 保持静音；描述符环不动，resume 后继续
 * 返回 : OK；-ENODEV 未初始化；-EALREADY 本未在播
 */
int audio_pause(void)
{
    if (!g_audio_initialized)
        return -ENODEV;

    if (!g_audio_playing || g_audio_paused)
        return -EALREADY;

    uint32_t conf = getreg32(ESP32_I2S_CONF_REG(0));
    conf &= ~ESP32_I2S_TX_START;
    putreg32(conf, ESP32_I2S_CONF_REG(0));

    g_audio_paused = true;
    return OK;
}

/*
 * WHAT : 恢复播放
 * HOW  : 清暂停门控并重新置 TX_START（DMA 环仍在运行）
 * 返回 : OK；-ENODEV 未初始化；-EALREADY 本未暂停
 */
int audio_resume(void)
{
    if (!g_audio_initialized)
        return -ENODEV;

    if (!g_audio_paused)
        return -EALREADY;

    uint32_t conf = getreg32(ESP32_I2S_CONF_REG(0));
    conf |= ESP32_I2S_TX_START;
    putreg32(conf, ESP32_I2S_CONF_REG(0));

    g_audio_paused = false;
    return OK;
}

/*
 * WHAT : 查询播放状态
 * WHY  : common 播放器 status 命令需要五元组
 * HOW  : 出参均可为 NULL（逐项判空）；volume 返回 0-100 口径
 */
void audio_get_status(bool *playing, bool *paused,
                      uint32_t *sample_rate, uint8_t *volume,
                      uint32_t *bytes_played)
{
    if (playing)
        *playing = g_audio_playing && !g_audio_paused;
    if (paused)
        *paused = g_audio_paused;
    if (sample_rate)
        *sample_rate = g_sample_rate;
    if (volume)
        *volume = g_volume_pct;
    if (bytes_played)
        *bytes_played = g_bytes_played;
}

/*
 * WHAT : 设置音量
 * WHY  : 播放器传 0-100，驱动采样缩放需要 0-255
 * HOW  : 入参按 0-100 截断保存，填充时换算 ×255/100（见 dma_fill_chunk）
 */
int audio_set_volume(uint8_t volume)
{
    if (volume > 100)
        volume = 100;

    g_volume_pct = volume;
    return OK;
}

/*
 * WHAT : 获取当前音量（0-100，与 set 口径一致）
 */
uint8_t audio_get_volume(void)
{
    return g_volume_pct;
}

/*
 * WHAT : 设置采样率
 * HOW  : 更新分频寄存器（CLKM_DIV）；仅在已初始化时写硬件
 * 返回 : OK；-EINVAL 超出 8k-48k
 */
int audio_set_sample_rate(uint32_t rate)
{
    if (rate < 8000 || rate > 48000)
        return -EINVAL;

    g_sample_rate = rate;

    if (g_audio_initialized) {
        uint32_t clkm_div = APB_CLK_HZ / (g_sample_rate * 32);
        if (clkm_div < 2)
            clkm_div = 2;
        putreg32(clkm_div - 1, ESP32_I2S_CLKM_CONF_REG(0));
    }

    return OK;
}

/*
 * WHAT : 关闭音频
 * HOW  : 先停播放再清初始化标志；DMA 缓冲保留（静态内存）
 */
void audio_deinit(void)
{
    audio_stop();
    g_audio_initialized = false;
}

#endif /* CONFIG_RETRO_AUDIO_DAC */
