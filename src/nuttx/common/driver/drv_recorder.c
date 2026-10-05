/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_recorder.c - 录音机驱动
 *
 * WHAT : 录音机驱动
 * WHY  : ADC 采样写 WAV 的后端通道
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_recorder.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 分块采集 + RIFF 组包
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

/*==========================
 *  配置检查
 *==========================*/

#ifndef CONFIG_RETRO_AUDIO
#  define CONFIG_RETRO_AUDIO 0
#endif

#if CONFIG_RETRO_AUDIO

/*==========================
 *  常量定义
 *==========================*/

#define REC_SAMPLE_RATE   16000   /* 采样率 16kHz */
#define REC_CHANNELS      1       /* 单声道 */
#define REC_BITS          16     /* 16-bit */
#define REC_MAX_DURATION  60000   /* 最大录音时长 60 秒 (ms) */
#define REC_BUF_SIZE      2048    /* 录音缓冲区大小 */

/*==========================
 *  音频驱动声明 / Audio Driver Declarations
 *==========================*/

extern int audio_init(void);
extern int audio_start(void);
extern int audio_stop(void);
extern int audio_set_volume(uint8_t vol);

/* 播放器命令 (复用) / Player command (reuse) */
extern int cmd_player(int argc, char **argv);

/*==========================
 *  录音状态 / Recording State
 *==========================*/

typedef enum {
    REC_STATE_STOPPED = 0,
    REC_STATE_RECORDING,
    REC_STATE_PAUSED,
} rec_state_t;

typedef struct {
    rec_state_t state;          /* 当前状态 */
    FILE        *file;          /* 录音文件 */
    char        filepath[128];  /* 文件路径 */
    uint32_t    start_time;     /* 开始时间 (系统启动后 ms) */
    uint32_t    duration_ms;    /* 当前录音时长 */
    uint32_t    bytes_written;  /* 已写入字节数 */
    uint32_t    sample_rate;    /* 采样率 */
    uint16_t    channels;       /* 声道数 */
    uint16_t    bits;           /* 位深 */
} rec_context_t;

static rec_context_t g_rec = {
    .state        = REC_STATE_STOPPED,
    .file         = NULL,
    .filepath     = {0},
    .start_time   = 0,
    .duration_ms  = 0,
    .bytes_written = 0,
    .sample_rate  = REC_SAMPLE_RATE,
    .channels     = REC_CHANNELS,
    .bits         = REC_BITS,
};

/*==========================
 *  WAV 头写入 / WAV Header Write
 *==========================*/

/**
 * 写入 WAV 文件头
 * Write WAV file header
 *
 * @param f         文件句柄
 * @param sr        采样率
 * @param ch        声道数
 * @param bps       位深
 * @param data_size 数据大小
 */
static void write_wav_header(FILE *f, uint32_t sr, uint16_t ch,
                              uint16_t bps, uint32_t data_size)
{
    uint32_t chunk_size = 36 + data_size;
    uint32_t byte_rate  = sr * ch * bps / 8;
    uint16_t block_align = ch * bps / 8;

    /* RIFF chunk */
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t audio_fmt = 1;  /* PCM */
    fwrite(&audio_fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps, 2, 1, f);

    /* data chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

/**
 * 更新 WAV 文件头 (录音结束后)
 * Update WAV file header (after recording)
 *
 * @param f 文件句柄
 * @param data_size 实际数据大小
 */
static void update_wav_header(FILE *f, uint32_t data_size)
{
    if (!f) return;

    /* 跳回文件头 / Seek to beginning */
    fseek(f, 0, SEEK_SET);

    /* 重新写入正确的数据大小 / Rewrite with correct data size */
    write_wav_header(f, g_rec.sample_rate, g_rec.channels,
                      g_rec.bits, data_size);
}

/*==========================
 *  时间管理 / Time Management
 *==========================*/

/**
 * 获取系统运行时间 (ms)
 * Get system uptime (ms)
 *
 * 使用 NuttX 的 system_time 或 uptime API
 */
static uint32_t get_system_ms(void)
{
    /* TODO: 使用 nuttx/time.h 中的 uptime 接口 */
    /* NuttX 提供 clock_systime_ticks() 或 similar */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/*==========================
 *  命令处理 / Command Handlers
 *==========================*/

/**
 * 开始录音 / Start recording
 */
static int cmd_start(const char *filepath)
{
    if (g_rec.state != REC_STATE_STOPPED) {
        printf("Already recording. Use 'rec stop' first.\n");
        return -EBUSY;
    }

    /* 如果没有指定路径，生成默认路径 */
    const char *path = filepath;
    if (!path || path[0] == 0) {
        /* 生成默认文件名 / Generate default filename */
        time_t now = time(NULL);
        struct tm *tm_now = localtime(&now);
        static char auto_path[64];
        snprintf(auto_path, sizeof(auto_path),
                 "/sdcard/rec_%04d%02d%02d_%02d%02d%02d.wav",
                 tm_now->tm_year + 1900,
                 tm_now->tm_mon + 1,
                 tm_now->tm_mday,
                 tm_now->tm_hour,
                 tm_now->tm_min,
                 tm_now->tm_sec);
        path = auto_path;
    }

    /* 打开文件写入 / Open file for writing */
    g_rec.file = fopen(path, "wb");
    if (!g_rec.file) {
        syslog(LOG_ERR, "rec: cannot create %s: %d\n", path, errno);
        printf("Failed to create file: %s\n", path);
        return -ENOENT;
    }

    /* 写入占位符 header / Write placeholder header */
    write_wav_header(g_rec.file, g_rec.sample_rate, g_rec.channels,
                     g_rec.bits, 0);

    /* 重置状态 / Reset state */
    g_rec.state        = REC_STATE_RECORDING;
    g_rec.start_time   = get_system_ms();
    g_rec.duration_ms  = 0;
    g_rec.bytes_written = 0;
    strncpy(g_rec.filepath, path, sizeof(g_rec.filepath) - 1);

    /* 初始化 I2S DMA 录音 / Initialize I2S DMA recording */
    /* TODO: audio_rec_init() - 初始化 I2S1 DMA RX */
    audio_init();

    syslog(LOG_INFO, "rec: started recording to %s\n", path);
    printf("Recording to: %s\n", path);
    printf("Press 'rec stop' to stop.\n");

    return OK;
}

/**
 * 停止录音 / Stop recording
 */
static int cmd_stop(void)
{
    if (g_rec.state == REC_STATE_STOPPED) {
        printf("Not recording.\n");
        return OK;
    }

    /* 停止 I2S DMA 录音 / Stop I2S DMA recording */
    audio_stop();

    /* 关闭文件 / Close file */
    if (g_rec.file) {
        /* 计算实际数据大小 / Calculate actual data size */
        uint32_t data_size = g_rec.bytes_written;

        /* 更新 WAV header / Update WAV header */
        fseek(g_rec.file, 0, SEEK_SET);
        write_wav_header(g_rec.file, g_rec.sample_rate, g_rec.channels,
                          g_rec.bits, data_size);

        fclose(g_rec.file);
        g_rec.file = NULL;
    }

    g_rec.state = REC_STATE_STOPPED;

    syslog(LOG_INFO, "rec: stopped, %lu bytes written\n",
           (unsigned long)g_rec.bytes_written);
    printf("Recording saved: %s\n", g_rec.filepath);
    printf("Duration: %.1f seconds\n",
           (double)g_rec.duration_ms / 1000.0);

    return OK;
}

/**
 * 播放录音 / Play recording
 */
static int cmd_play(const char *filepath)
{
    if (!filepath) {
        if (g_rec.filepath[0] == 0) {
            printf("No recording to play.\n");
            return -ENOENT;
        }
        filepath = g_rec.filepath;
    }

    printf("Playing: %s\n", filepath);

    /* 调用播放器 / Call player */
    char *player_argv[] = { "player", (char *)filepath };
    return cmd_player(2, player_argv);
}

/**
 * 显示状态 / Show status
 */
static int cmd_status(void)
{
    printf("\n");
    printf("=== Recorder Status ===\n");
    printf("State:     %s\n",
           g_rec.state == REC_STATE_RECORDING ? "RECORDING" :
           g_rec.state == REC_STATE_PAUSED    ? "PAUSED"    : "STOPPED");

    if (g_rec.state != REC_STATE_STOPPED) {
        printf("File:      %s\n", g_rec.filepath);
        printf("Duration:  %.1f seconds\n", (double)g_rec.duration_ms / 1000.0);
        printf("Written:   %lu bytes\n", (unsigned long)g_rec.bytes_written);
        printf("Sample Rate: %u Hz\n", g_rec.sample_rate);
        printf("Channels:    %u\n", g_rec.channels);
        printf("Bit Depth:   %u-bit\n", g_rec.bits);
    }

    printf("\n");
    return OK;
}

/*==========================
 *  录音任务 / Recording Task
 *==========================*/

/**
 * 录音任务 (在后台任务中运行)
 * Recording task (runs in background task)
 *
 * TODO: 实现真正的 DMA 缓冲区读取
 *
 * 真实实现需要:
 * 1. 在独立 NuttX 任务中运行
 * 2. 等待 DMA RX 完成中断
 * 3. 从 DMA 缓冲区读取 PCM 数据
 * 4. 写入文件
 *
 * 示例代码:
 *   void rec_task(int argc, char **argv)
 *   {
 *       uint8_t buf[REC_BUF_SIZE];
 *       while (g_rec.state == REC_STATE_RECORDING) {
 *           // 等待 DMA 中断信号量
 *           sem_wait(&g_dma_rx_sem);
 *
 *           // 读取 DMA 缓冲区
 *           int bytes = i2s_read_samples(buf, sizeof(buf));
 *           if (bytes > 0) {
 *               fwrite(buf, 1, bytes, g_rec.file);
 *               g_rec.bytes_written += bytes;
 *               g_rec.duration_ms = get_system_ms() - g_rec.start_time;
 *           }
 *
 *           // 检查最大时长
 *           if (g_rec.duration_ms >= REC_MAX_DURATION) {
 *               cmd_stop();
 *               break;
 *           }
 *       }
 *   }
 */

/**
 * 更新录音时长 (定时调用)
 * Update recording duration (called by timer)
 */
void rec_update_duration(void)
{
    if (g_rec.state != REC_STATE_RECORDING) {
        return;
    }

    g_rec.duration_ms = get_system_ms() - g_rec.start_time;

    /* 每秒打印一次 / Print every second */
    static uint32_t last_print = 0;
    if (g_rec.duration_ms - last_print >= 1000) {
        printf("\rRecording: %.1fs", (double)g_rec.duration_ms / 1000.0);
        fflush(stdout);
        last_print = g_rec.duration_ms;
    }

    /* 检查最大时长 / Check max duration */
    if (g_rec.duration_ms >= REC_MAX_DURATION) {
        printf("\nMax duration reached (60s).\n");
        cmd_stop();
    }
}

/*==========================
 *  主命令入口 / Main Command Entry
 *==========================*/

/**
 * rec 命令入口 / rec command entry
 *
 * NuttX NSH 命令注册:
 * 在 nsh_cmds.c 中添加:
 *   SHELL_COMMAND_ARG("rec", "Audio recorder", cmd_recorder, 3)
 *
 * 注意: rec 是 NuttX 已有的命令名
 * 可能需要重命名为 retro_rec
 */
int cmd_recorder(int argc, char **argv)
{
    if (argc < 2) {
        printf("\n");
        printf("=== Audio Recorder ===\n");
        printf("Usage: rec <command> [args]\n");
        printf("\n");
        printf("Commands:\n");
        printf("  rec start [file.wav]  Start recording to WAV file\n");
        printf("                        Default: /sdcard/rec_YYYYMMDD_HHMMSS.wav\n");
        printf("  rec stop              Stop recording\n");
        printf("  rec play [file.wav]  Play recorded file\n");
        printf("  rec status            Show status\n");
        printf("\n");
        printf("Recording format:\n");
        printf("  - 16kHz, 16-bit, mono (PCM)\n");
        printf("  - Max duration: 60 seconds\n");
        printf("  - Format: WAV (RIFF)\n");
        printf("\n");
        printf("Note: I2S MIC required (INMP441 or similar).\n");
        printf("      I2S1 configured for recording.\n");
        printf("\n");
        return OK;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "rec") == 0) {
        const char *path = (argc > 2) ? argv[2] : NULL;
        return cmd_start(path);
    }
    else if (strcmp(cmd, "stop") == 0) {
        return cmd_stop();
    }
    else if (strcmp(cmd, "play") == 0) {
        const char *path = (argc > 2) ? argv[2] : NULL;
        return cmd_play(path);
    }
    else if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    }
    else {
        printf("Unknown command: %s\n", cmd);
        printf("Usage: rec start|stop|play|status\n");
        return -EINVAL;
    }
}

#endif /* CONFIG_RETRO_AUDIO */

/**
 * 配置说明 / Configuration:
 *
 * 在 NuttX 配置中添加:
 *   CONFIG_RETRO_AUDIO=y       - 启用音频支持
 *   CONFIG_ESP32S3_I2S=y       - 启用 I2S
 *   CONFIG_FS_FAT=y            - FAT 文件系统
 *   CONFIG_I2S=y               - I2S 驱动
 *   CONFIG_ESP32S3_I2S1=y     - I2S1 (用于录音)
 *
 * I2S1 MIC 引脚 (GPIO):
 *   - GPIO 34: I2S1_DATA_IN (MIC DATA)
 *   - GPIO 35: I2S1_WS (Word Select / L/R)
 *   - GPIO 36: I2S1_CLK (Bit Clock)
 *
 * 使用 INMP441 模块连接:
 *   INMP441    ESP32-S3
 *   -----      --------
 *   VCC        3.3V
 *   GND        GND
 *   DIN        GPIO 34
 *   WS         GPIO 35
 *   BCLK       GPIO 36
 *   LRCONT     (not connected, default L/R)
 *
 * TODO 列表:
 *   [ ] 实现 I2S1 DMA RX 驱动
 *   [ ] 实现 DMA ping-pong 缓冲区
 *   [ ] 创建独立录音任务
 *   [ ] 添加音量检测 (VU meter)
 *   [ ] 添加录音格式选项 (8kHz, 44.1kHz 等)
 */
