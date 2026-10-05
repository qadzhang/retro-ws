/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_player.c - 媒体播放器驱动
 *
 * WHAT : 媒体播放器驱动
 * WHY  : 音频播放的后端通道
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/drv_player.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : WAV 流送音频驱动（DAC/I2S）
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

/*==========================
 *  配置检查
 *==========================*/

#ifndef CONFIG_RETRO_AUDIO
#  define CONFIG_RETRO_AUDIO 0
#endif

#if CONFIG_RETRO_AUDIO

/*==========================
 *  WAV 格式常量 / WAV Format Constants
 *==========================*/

#define WAV_RIFF_MAGIC   0x46464952  /* "RIFF" */
#define WAV_WAVE_MAGIC   0x45564157  /* "WAVE" */
#define WAV_FMT_MAGIC    0x20746D66  /* "fmt " */
#define WAV_DATA_MAGIC   0x61746164  /* "data" */
#define WAV_FMT_PCM      0x0001

/*==========================
 *  WAV 文件信息 / WAV File Info
 *==========================*/

typedef struct {
    int           fd;           /* 文件描述符 / File descriptor */
    uint32_t      sample_rate;   /* 采样率 / Sample rate */
    uint16_t      channels;      /* 声道数 / Channels */
    uint16_t      bits_per_sample; /* 位深 / Bits per sample */
    uint32_t      data_size;     /* PCM 数据大小 / PCM data size */
    uint32_t      byte_rate;     /* 字节率 / Bytes per second */
    uint32_t      current_pos;   /* 当前读取位置 / Current position */
    bool          is_valid;      /* 是否有效 / Is valid */
    bool          at_end;        /* 是否到达末尾 / At end */
    char          filename[64];  /* 文件名 / Filename */
} wav_context_t;

/* fd 显式初始化为 -1：{0} 会让 fd==0，wav_open 首次调用时误 close(stdin)
 * fd=-1 so the first wav_open() does not close stdin (fd 0) */
static wav_context_t g_wav = { .fd = -1 };

/*==========================
 *  音频驱动声明 / Audio Driver Declarations
 *==========================*/

extern int audio_init(void);
extern int audio_start(void);
extern int audio_stop(void);
extern int audio_pause(void);
extern int audio_resume(void);
extern int audio_set_volume(uint8_t vol);
extern uint8_t audio_get_volume(void);
extern int audio_play_pcm(const void *data, size_t len);
extern void audio_get_status(bool *playing, bool *paused,
                             uint32_t *sample_rate, uint8_t *volume,
                             uint32_t *bytes_played);

/*==========================
 *  播放器状态 / Player State
 *==========================*/

typedef enum {
    PLAY_STOPPED = 0,
    PLAY_PLAYING,
    PLAY_PAUSED,
} play_state_t;

static play_state_t g_state = PLAY_STOPPED;
static uint8_t      g_volume = 80;

/*==========================
 *  WAV 解析函数 / WAV Parse Functions
 *==========================*/

/**
 * 从文件读取 32-bit 小端整数
 * Read 32-bit little-endian integer
 */
static inline uint32_t read_u32(int fd)
{
    uint8_t buf[4];
    if (read(fd, buf, 4) != 4) return 0;
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/**
 * 从文件读取 16-bit 小端整数
 * Read 16-bit little-endian integer
 */
static inline uint16_t read_u16(int fd)
{
    uint8_t buf[2];
    if (read(fd, buf, 2) != 2) return 0;
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/**
 * 打开并解析 WAV 文件
 * Open and parse WAV file
 */
static int wav_open(const char *path)
{
    if (g_wav.fd >= 0) {
        close(g_wav.fd);
    }
    memset(&g_wav, 0, sizeof(g_wav));
    g_wav.fd = -1;  /* memset 后立刻复位，open 失败时不残留 fd==0 */

    g_wav.fd = open(path, O_RDONLY);
    if (g_wav.fd < 0) {
        syslog(LOG_ERR, "player: cannot open %s: %d\n", path, errno);
        return -ENOENT;
    }

    strncpy(g_wav.filename, path, sizeof(g_wav.filename) - 1);

    /* === 读取 RIFF 头 / Read RIFF header === */
    uint32_t riff_magic = read_u32(g_wav.fd);
    uint32_t riff_size  = read_u32(g_wav.fd);
    uint32_t wave_magic = read_u32(g_wav.fd);
    (void)riff_size;  /* 未使用 / unused */

    if (riff_magic != WAV_RIFF_MAGIC || wave_magic != WAV_WAVE_MAGIC) {
        syslog(LOG_ERR, "player: %s is not a valid WAV file\n", path);
        close(g_wav.fd);
        g_wav.fd = -1;
        return -EINVAL;
    }

    /* === 搜索 fmt 和 data chunk === */
    bool fmt_found = false;
    bool data_found = false;

    while (!g_wav.at_end) {
        uint32_t chunk_id   = read_u32(g_wav.fd);
        uint32_t chunk_size = read_u32(g_wav.fd);

        if (chunk_id == WAV_FMT_MAGIC) {
            g_wav.channels        = read_u16(g_wav.fd);
            g_wav.sample_rate     = read_u32(g_wav.fd);
            g_wav.byte_rate       = read_u32(g_wav.fd);
            read_u16(g_wav.fd);  /* block_align */
            g_wav.bits_per_sample = read_u16(g_wav.fd);
            fmt_found = true;

            if (chunk_size > 16) {
                lseek(g_wav.fd, chunk_size - 16, SEEK_CUR);
            }

            syslog(LOG_INFO, "player: fmt: %u Hz, %u ch, %u-bit\n",
                   g_wav.sample_rate, g_wav.channels, g_wav.bits_per_sample);

        } else if (chunk_id == WAV_DATA_MAGIC) {
            g_wav.data_size  = chunk_size;
            g_wav.at_end     = false;
            data_found       = true;

            syslog(LOG_INFO, "player: data: %lu bytes\n",
                   (unsigned long)g_wav.data_size);
            break;

        } else {
            /* 跳过未知 chunk */
            if (chunk_size & 1) chunk_size++;  /* 奇数对齐 */
            lseek(g_wav.fd, chunk_size, SEEK_CUR);
        }

        /* 防止无限循环 */
        if (!fmt_found && !data_found) {
            break;
        }
    }

    if (!fmt_found || !data_found) {
        syslog(LOG_ERR, "player: invalid WAV structure\n");
        close(g_wav.fd);
        g_wav.fd = -1;
        return -EINVAL;
    }

    g_wav.is_valid    = true;
    g_wav.current_pos = 0;

    syslog(LOG_INFO, "player: opened %s\n", path);
    return 0;
}

/**
 * 关闭 WAV 文件 / Close WAV file
 */
static void wav_close(void)
{
    if (g_wav.fd >= 0) {
        close(g_wav.fd);
    }
    memset(&g_wav, 0, sizeof(g_wav));
    g_wav.fd = -1;
}

/**
 * 读取 WAV PCM 数据
 * Read WAV PCM data
 *
 * @param buf  输出缓冲区
 * @param len  缓冲区大小(字节)
 * @return     实际读取字节数
 */
static int wav_read(uint8_t *buf, int len)
{
    if (g_wav.fd < 0 || g_wav.at_end) {
        return 0;
    }

    int remaining = g_wav.data_size - g_wav.current_pos;
    if (len > remaining) {
        len = remaining;
        g_wav.at_end = true;
    }

    int bytes_read = read(g_wav.fd, buf, len);
    if (bytes_read > 0) {
        g_wav.current_pos += bytes_read;
    }

    return bytes_read;
}

/**
 * 获取 WAV 信息 / Get WAV info
 */
static void wav_info(void)
{
    if (!g_wav.is_valid) {
        printf("No file loaded.\n");
        return;
    }

    printf("\n");
    printf("=== WAV Info ===\n");
    printf("File:      %s\n", g_wav.filename);
    printf("Sample Rate: %lu Hz\n", (unsigned long)g_wav.sample_rate);
    printf("Channels:    %u\n", g_wav.channels);
    printf("Bit Depth:  %u-bit\n", g_wav.bits_per_sample);
    printf("Data Size:  %lu bytes\n", (unsigned long)g_wav.data_size);
    printf("Duration:   %.1f seconds\n",
           (double)g_wav.data_size / (double)g_wav.byte_rate);
    printf("Position:   %lu bytes (%.1f%%)\n",
           (unsigned long)g_wav.current_pos,
           g_wav.data_size > 0 ?
           (double)g_wav.current_pos / g_wav.data_size * 100.0 : 0.0);
    printf("\n");
}

/*==========================
 *  播放循环 / Playback Loop
 *==========================*/

/**
 * 播放任务 (在后台任务中运行)
 * Playback task (runs in background task)
 *
 * 注意: 这是一个简化实现
 * 真实实现需要在独立任务/线程中运行
 */
static void playback_task(void)
{
    /* DMA 缓冲区 / DMA buffer */
    #define PLAY_BUF_SIZE 2048
    uint8_t play_buf[PLAY_BUF_SIZE];

    audio_init();
    audio_set_volume(g_volume);
    audio_start();

    syslog(LOG_INFO, "player: playback started\n");

    while (g_state == PLAY_PLAYING && !g_wav.at_end) {
        int bytes = wav_read(play_buf, sizeof(play_buf));
        if (bytes <= 0) {
            break;
        }

        /* 送音频后端（esp32s3 drv_audio.c 实现该接口）
         * feed the audio backend (implemented in esp32s3 drv_audio.c) */
        audio_play_pcm(play_buf, (size_t)bytes);

        /* 按字节率节流（byte_rate==0 时除零，直接跳出）
         * throttle by byte rate; guard divide-by-zero */
        if (g_wav.byte_rate == 0) {
            syslog(LOG_WARNING, "player: byte_rate==0, aborting playback\n");
            break;
        }
        usleep(bytes * 1000000UL / g_wav.byte_rate);
    }

    audio_stop();
    g_state = PLAY_STOPPED;

    syslog(LOG_INFO, "player: playback finished\n");
}

/*==========================
 *  命令处理 / Command Handlers
 *==========================*/

/**
 * 播放 WAV 文件 / Play WAV file
 */
static int cmd_play(const char *filename)
{
    if (!filename) {
        printf("Usage: player <filename>\n");
        return -EINVAL;
    }

    /* 停止当前播放 / Stop current playback */
    if (g_state != PLAY_STOPPED) {
        audio_stop();
        wav_close();
        g_state = PLAY_STOPPED;
    }

    /* 打开 WAV 文件 / Open WAV file */
    int ret = wav_open(filename);
    if (ret < 0) {
        return ret;
    }

    /* 打印文件信息 / Print file info */
    wav_info();

    /* 开始播放 / Start playback */
    g_state = PLAY_PLAYING;
    playback_task();

    return 0;
}

/**
 * 停止播放 / Stop playback
 */
static int cmd_stop(void)
{
    if (g_state == PLAY_STOPPED) {
        printf("Not playing.\n");
        return OK;
    }

    audio_stop();
    wav_close();
    g_state = PLAY_STOPPED;

    printf("Stopped.\n");
    return OK;
}

/**
 * 暂停播放 / Pause playback
 */
static int cmd_pause(void)
{
    if (g_state != PLAY_PLAYING) {
        printf("Not playing.\n");
        return OK;
    }

    audio_pause();
    g_state = PLAY_PAUSED;
    printf("Paused.\n");
    return OK;
}

/**
 * 继续播放 / Resume playback
 */
static int cmd_resume(void)
{
    if (g_state != PLAY_PAUSED) {
        printf("Not paused.\n");
        return OK;
    }

    audio_resume();
    g_state = PLAY_PLAYING;
    playback_task();

    return OK;
}

/**
 * 显示状态 / Show status
 */
static int cmd_status(void)
{
    bool playing, paused;
    uint32_t rate, bytes;
    uint8_t vol;

    audio_get_status(&playing, &paused, &rate, &vol, &bytes);

    printf("\n");
    printf("=== Player Status ===\n");
    printf("State:     %s\n",
           g_state == PLAY_PLAYING ? "PLAYING" :
           g_state == PLAY_PAUSED  ? "PAUSED"  : "STOPPED");
    printf("Volume:    %u%%\n", vol);

    if (g_wav.is_valid) {
        printf("File:      %s\n", g_wav.filename);
        printf("Sample Rate: %lu Hz\n", (unsigned long)g_wav.sample_rate);
        printf("Position:   %lu / %lu bytes\n",
               (unsigned long)g_wav.current_pos,
               (unsigned long)g_wav.data_size);
    }

    printf("\n");
    return OK;
}

/**
 * 设置音量 / Set volume
 */
static int cmd_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    g_volume = (uint8_t)vol;
    audio_set_volume(g_volume);
    printf("Volume set to %u%%\n", g_volume);
    return OK;
}

/*==========================
 *  主命令入口 / Main Command Entry
 *==========================*/

/**
 * player 命令入口 / player command entry
 *
 * NuttX NSH 命令注册:
 * 在 nsh_cmds.c 中添加:
 *   SHELL_COMMAND_ARG("player", "WAV player", cmd_player, 3)
 */
int cmd_player(int argc, char **argv)
{
    if (argc < 2) {
        printf("\n");
        printf("=== WAV Player ===\n");
        printf("Usage: player <command> [args]\n");
        printf("\n");
        printf("Commands:\n");
        printf("  player <file.wav>   Play WAV file\n");
        printf("  player stop         Stop playback\n");
        printf("  player pause        Pause playback\n");
        printf("  player resume       Resume playback\n");
        printf("  player status       Show status\n");
        printf("  player volume <n>   Set volume (0-100)\n");
        printf("\n");
        printf("Note: MP3 support requires minimp3 library.\n");
        printf("      WAV (PCM) is supported natively.\n");
        printf("\n");
        return OK;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "stop") == 0) {
        return cmd_stop();
    }
    else if (strcmp(cmd, "pause") == 0) {
        return cmd_pause();
    }
    else if (strcmp(cmd, "resume") == 0) {
        return cmd_resume();
    }
    else if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    }
    else if (strcmp(cmd, "volume") == 0 || strcmp(cmd, "vol") == 0) {
        int vol = 80;  /* 默认音量 */
        if (argc > 2) {
            vol = atoi(argv[2]);
        }
        return cmd_volume(vol);
    }
    else {
        /* 假设是文件名 / Assume filename */
        return cmd_play(cmd);
    }
}

#endif /* CONFIG_RETRO_AUDIO */

/**
 * 配置说明 / Configuration:
 *
 * 在 NuttX 配置中添加:
 *   CONFIG_RETRO_AUDIO=y       - 启用音频支持
 *   CONFIG_ESP32S3_I2S=y       - 启用 I2S
 *   CONFIG_FS_FAT=y            - FAT 文件系统 (用于 SD 卡)
 *
 * SD 卡挂载点:
 *   /sdcard/                   - SD 卡根目录
 *
 * 编译:
 *   make -j4
 *
 * 使用:
 *   nsh> player /sdcard/test.wav
 */
