/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * wav_decoder.c - WAV 解码器
 *
 * WHAT : WAV 解码器
 * WHY  : 播放器/录音机的音频文件解析
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/audio/wav_decoder.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 解析 RIFF 头与 PCM 块，流式喂给音频驱动
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"

/*======================================
 *  WAV 格式常量 / WAV Format Constants
 *======================================*/

#define WAV_RIFF_MAGIC   0x46464952  /* "RIFF" */
#define WAV_WAVE_MAGIC   0x45564157  /* "WAVE" */
#define WAV_FMT_MAGIC    0x20746D66  /* "fmt " */
#define WAV_DATA_MAGIC   0x61746164  /* "data" */

#define WAV_FMT_PCM      0x0001      /* PCM 格式 / PCM format */
#define WAV_FMT_IEEE     0x0003      /* IEEE float / Not used */
#define WAV_FMT_ALAW     0x0006      /* A-law / Not used */
#define WAV_FMT_MULAW    0x0007      /* μ-law / Not used */

/*======================================
 *  WAV 文件信息结构 / WAV File Info Structure
 *======================================*/

typedef struct {
    /* 文件句柄 / File handle */
    FILE *file;

    /* 音频参数 / Audio parameters */
    uint16_t audio_format;     /* 音频格式 / Audio format (PCM=1) */
    uint16_t num_channels;     /* 声道数 / Number of channels (1=mono, 2=stereo) */
    uint32_t sample_rate;       /* 采样率 / Sample rate (Hz) */
    uint32_t byte_rate;         /* 字节率 / Bytes per second */
    uint16_t block_align;       /* 块对齐 / Block alignment */
    uint16_t bits_per_sample;   /* 位深 / Bits per sample (8 or 16) */

    /* 数据信息 / Data info */
    uint32_t data_size;         /* PCM 数据总大小(字节) / Total PCM data size */
    uint32_t data_offset;       /* PCM 数据在文件中的偏移 / PCM data file offset */
    uint32_t current_pos;       /* 当前读取位置 / Current read position */

    /* 状态 / Status */
    bool     is_open;           /* 是否已打开 / Is file open */
    bool     is_valid;          /* 是否为有效 WAV / Is valid WAV file */
    char     filename[128];     /* 文件名 / Filename */
} wav_info_t;

/* 全局解码器实例 / Global decoder instance */
static wav_info_t g_wav = {0};

/*
 * wav_reset - 关闭文件并复位状态 / Close file and reset state
 * WHAT: 失败路径统一清理
 * WHY  : 旧错误分支只 fclose 不清 g_wav.file，后续 wav_close 会二次 fclose
 * HOW  : 关闭句柄并整体清零状态
 */
static void wav_reset(void)
{
    if (g_wav.file) {
        fclose(g_wav.file);
        g_wav.file = NULL;
    }
    memset(&g_wav, 0, sizeof(g_wav));
    g_wav.is_open = false;
}

/*======================================
 *  字节序转换 / Byte Order Helpers
 *======================================*/

/**
 * 从文件读取 32-bit 小端整数
 * Read 32-bit little-endian integer from file
 */
static uint32_t read_u32(FILE *f)
{
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) != 4)
        return 0;
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/**
 * 从文件读取 16-bit 小端整数
 * Read 16-bit little-endian integer from file
 */
static uint16_t read_u16(FILE *f)
{
    uint8_t buf[2];
    if (fread(buf, 1, 2, f) != 2)
        return 0;
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/*======================================
 *  WAV 打开与解析 / WAV Open and Parse
 *======================================*/

void wav_close(void);  /* 前向声明 / forward declaration */

/**
 * 打开并解析 WAV 文件
 * Open and parse a WAV file
 *
 * @param path WAV 文件路径 / WAV file path
 * @return 0 成功，负数错误码 / 0 success, negative errno
 *
 * 使用示例 / Usage example:
 *   wav_info_t info;
 *   if (wav_open("/sdcard/test.wav", &info) == 0) {
 *       // info.sample_rate, info.num_channels, info.bits_per_sample
 *       wav_read(&info, buffer, sizeof(buffer));
 *       wav_close(&info);
 *   }
 */
int wav_open(const char *path)
{
    if (!path) {
        syslog(LOG_ERR, "WAV: NULL path\n");
        return -EINVAL;
    }

    /* 关闭之前打开的文件 / Close previously opened file */
    if (g_wav.is_open) {
        wav_close();
    }

    /* 打开文件 / Open file */
    g_wav.file = fopen(path, "rb");
    if (!g_wav.file) {
        syslog(LOG_ERR, "WAV: failed to open %s\n", path);
        return -ENOENT;
    }

    strncpy(g_wav.filename, path, sizeof(g_wav.filename) - 1);
    g_wav.filename[sizeof(g_wav.filename) - 1] = 0;

    /* === 读取 RIFF 头 / Read RIFF header === */
    uint32_t riff_magic = read_u32(g_wav.file);
    uint32_t riff_size = read_u32(g_wav.file);  /* 文件大小-8 / file size - 8 */
    uint32_t wave_magic = read_u32(g_wav.file);
    (void)riff_size;  /* 未使用 / unused */

    if (riff_magic != WAV_RIFF_MAGIC) {
        syslog(LOG_ERR, "WAV: not a RIFF file: %s\n", path);
        wav_reset();
        return -EINVAL;
    }

    if (wave_magic != WAV_WAVE_MAGIC) {
        syslog(LOG_ERR, "WAV: not a WAVE file: %s\n", path);
        wav_reset();
        return -EINVAL;
    }

    /* === 搜索 fmt chunk / Search for fmt chunk === */
    bool fmt_found = false;
    bool data_found = false;

    while (!feof(g_wav.file)) {
        uint32_t chunk_id = read_u32(g_wav.file);
        uint32_t chunk_size = read_u32(g_wav.file);

        if (chunk_id == WAV_FMT_MAGIC) {
            /* 读取 fmt 数据 / Read fmt data */
            g_wav.audio_format    = read_u16(g_wav.file);
            g_wav.num_channels   = read_u16(g_wav.file);
            g_wav.sample_rate    = read_u32(g_wav.file);
            g_wav.byte_rate      = read_u32(g_wav.file);
            g_wav.block_align    = read_u16(g_wav.file);
            g_wav.bits_per_sample = read_u16(g_wav.file);

            /* 对齐到 word 边界 / Align to word boundary */
            if (chunk_size > 16) {
                fseek(g_wav.file, chunk_size - 16, SEEK_CUR);
            }

            fmt_found = true;
            syslog(LOG_INFO, "WAV: fmt found: %dch, %luHz, %d-bit\n",
                   g_wav.num_channels, (unsigned long)g_wav.sample_rate,
                   g_wav.bits_per_sample);

        } else if (chunk_id == WAV_DATA_MAGIC) {
            /* 找到 data chunk / Found data chunk */
            g_wav.data_size = chunk_size;
            g_wav.data_offset = (uint32_t)ftell(g_wav.file);
            data_found = true;

            syslog(LOG_INFO, "WAV: data chunk: %lu bytes\n",
                   (unsigned long)g_wav.data_size);
            break;

        } else {
            /* 跳过未知 chunk / Skip unknown chunk */
            fseek(g_wav.file, chunk_size, SEEK_CUR);
            /* 奇数字节对齐 / Odd byte alignment */
            if (chunk_size & 1)
                fseek(g_wav.file, 1, SEEK_CUR);
        }
    }

    if (!fmt_found || !data_found) {
        syslog(LOG_ERR, "WAV: invalid WAV file (missing fmt or data chunk)\n");
        wav_reset();
        return -EINVAL;
    }

    /* 检查 PCM 格式支持 / Check PCM format support */
    if (g_wav.audio_format != WAV_FMT_PCM) {
        syslog(LOG_ERR, "WAV: unsupported audio format %d (only PCM supported)\n",
               g_wav.audio_format);
        wav_reset();
        return -ENOSYS;
    }

    /* 检查采样格式 / Check sample format */
    if (g_wav.bits_per_sample != 8 && g_wav.bits_per_sample != 16) {
        syslog(LOG_ERR, "WAV: unsupported bit depth %d (only 8/16 supported)\n",
               g_wav.bits_per_sample);
        wav_reset();
        return -ENOSYS;
    }

    if (g_wav.num_channels < 1 || g_wav.num_channels > 2) {
        syslog(LOG_ERR, "WAV: unsupported channel count %d (only 1/2 supported)\n",
               g_wav.num_channels);
        wav_reset();
        return -ENOSYS;
    }

    /* block_align 为 0 会在 wav_seek 除零 / block_align==0 would div-by-zero in wav_seek */
    if (g_wav.block_align == 0) {
        syslog(LOG_ERR, "WAV: invalid block_align 0\n");
        wav_reset();
        return -EINVAL;
    }

    g_wav.is_open = true;
    g_wav.is_valid = true;
    g_wav.current_pos = 0;

    syslog(LOG_INFO, "WAV: opened %s (%lu Hz, %d ch, %d-bit)\n",
           path, (unsigned long)g_wav.sample_rate,
           g_wav.num_channels, g_wav.bits_per_sample);

    return 0;
}

/*======================================
 *  WAV 读取 / WAV Read
 *======================================*/

/**
 * 从 WAV 文件读取 PCM 数据
 * Read PCM data from WAV file
 *
 * @param buf  输出缓冲区 / Output buffer
 * @param len  缓冲区大小(字节) / Buffer size in bytes
 * @return     实际读取的字节数 / Actual bytes read
 *
 * 注意 / Note:
 *   对于 16-bit 立体声，返回的 PCM 数据格式为:
 *   L0_low, L0_high, R0_low, R0_high, L1_low, L1_high, R1_low, R1_high ...
 *   即 interleaved stereo 16-bit PCM
 */
int wav_read(void *buf, int len)
{
    if (!g_wav.is_open || !g_wav.file) {
        return -ENODEV;
    }

    /* 检查是否已到达数据末尾 / Check if at end of data */
    if (g_wav.current_pos >= g_wav.data_size) {
        return 0;  /* EOF */
    }

    /* 限制读取长度 / Limit read length */
    int remaining = g_wav.data_size - g_wav.current_pos;
    if (len > remaining) {
        len = remaining;
    }

    /* 读取数据 / Read data */
    size_t bytes_read = fread(buf, 1, len, g_wav.file);
    g_wav.current_pos += bytes_read;

    return (int)bytes_read;
}

/**
 * 将 PCM 数据重采样并转换为 16-bit 立体声
 * Resample and convert PCM data to 16-bit stereo
 *
 * @param in_buf   输入缓冲区 / Input buffer (raw WAV PCM)
 * @param out_buf  输出缓冲区 / Output buffer (16-bit stereo PCM)
 * @param in_len   输入数据长度(字节) / Input data length in bytes
 * @return         输出的样本数(16-bit 采样点) / Output sample count
 *
 * 注意 / Note:
 *   输出格式固定为: 每样本 = 2字节(L) + 2字节(R) = 4字节
 *   Output format: per sample = 2bytes(L) + 2bytes(R) = 4 bytes
 *
 * ESP32-S3 内存限制说明 / ESP32-S3 Memory Constraint Note:
 *   ESP32-S3 PSRAM 8MB，SDRAM 最大 120MHz
 *   当前版本使用简单线性插值重采样
 *   如需高质量重采样，可添加 libreSrc 等库
 */
int wav_convert_to_stereo16(const void *in_buf, void *out_buf, int in_len)
{
    if (!in_buf || !out_buf || in_len <= 0) {
        return 0;
    }

    int out_samples = 0;

    /* === 16-bit 立体声 -> 16-bit 立体声 (直接复制) === */
    if (g_wav.bits_per_sample == 16 && g_wav.num_channels == 2) {
        /* 直接复制 / Direct copy */
        memcpy(out_buf, in_buf, in_len);
        out_samples = in_len / 4;  /* 2ch * 2bytes = 4 bytes per sample */
    }
    /* === 16-bit 单声道 -> 16-bit 立体声 (复制到左右) === */
    else if (g_wav.bits_per_sample == 16 && g_wav.num_channels == 1) {
        int16_t *in = (int16_t *)in_buf;
        int16_t *out = (int16_t *)out_buf;
        int count = in_len / 2;

        for (int i = 0; i < count; i++) {
            out[i * 2]     = in[i];  /* L = R = mono sample */
            out[i * 2 + 1] = in[i];
        }
        out_samples = count;
    }
    /* === 8-bit 单声道 -> 16-bit 立体声 === */
    else if (g_wav.bits_per_sample == 8 && g_wav.num_channels == 1) {
        uint8_t *in = (uint8_t *)in_buf;
        int16_t *out = (int16_t *)out_buf;
        int count = in_len;

        for (int i = 0; i < count; i++) {
            /* 8-bit PCM: 0 = silent, 128 = center, 255 = max */
            /* 转换为有符号 16-bit / Convert to signed 16-bit */
            int16_t sample = ((int16_t)in[i] - 128) << 8;
            out[i * 2]     = sample;
            out[i * 2 + 1] = sample;
        }
        out_samples = count;
    }
    /* === 8-bit 立体声 -> 16-bit 立体声 === */
    else if (g_wav.bits_per_sample == 8 && g_wav.num_channels == 2) {
        uint8_t *in = (uint8_t *)in_buf;
        int16_t *out = (int16_t *)out_buf;
        int count = in_len / 2;  /* 2 channels */

        for (int i = 0; i < count; i++) {
            int16_t l = ((int16_t)in[i * 2] - 128) << 8;
            int16_t r = ((int16_t)in[i * 2 + 1] - 128) << 8;
            out[i * 2]     = l;
            out[i * 2 + 1] = r;
        }
        out_samples = count;
    }

    return out_samples;
}

/**
 * 跳转到指定位置(百分比)
 * Seek to position by percentage
 *
 * @param percent 0.0 - 1.0
 * @return 0 成功 / 0 success
 */
int wav_seek(float percent)
{
    if (!g_wav.is_open || !g_wav.file) {
        return -ENODEV;
    }

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    uint32_t target = (uint32_t)((float)g_wav.data_size * percent);
    /* 对齐到块边界 / Align to block boundary */
    target = (target / g_wav.block_align) * g_wav.block_align;

    fseek(g_wav.file, g_wav.data_offset + target, SEEK_SET);
    g_wav.current_pos = target;

    return 0;
}

/*======================================
 *  WAV 信息查询 / WAV Info Query
 *======================================*/

/**
 * 获取 WAV 文件信息
 * Get WAV file information
 */
void wav_get_info(uint32_t *sample_rate, uint16_t *channels,
                  uint16_t *bits_per_sample, uint32_t *data_size,
                  uint32_t *duration_ms)
{
    if (sample_rate)      *sample_rate = g_wav.sample_rate;
    if (channels)         *channels    = g_wav.num_channels;
    if (bits_per_sample)  *bits_per_sample = g_wav.bits_per_sample;
    if (data_size)        *data_size   = g_wav.data_size;
    if (duration_ms) {
        if (g_wav.byte_rate > 0) {
            *duration_ms = (g_wav.data_size * 1000) / g_wav.byte_rate;
        } else {
            *duration_ms = 0;
        }
    }
}

/**
 * 获取当前播放位置(百分比)
 * Get current play position (percentage)
 */
float wav_get_position(void)
{
    if (!g_wav.is_open || g_wav.data_size == 0) {
        return 0.0f;
    }
    return (float)g_wav.current_pos / (float)g_wav.data_size;
}

/**
 * 获取文件名
 * Get filename
 */
const char *wav_get_filename(void)
{
    return g_wav.filename;
}

/**
 * 检查 WAV 是否打开 / Check if WAV is open
 */
bool wav_is_open(void)
{
    return g_wav.is_open;
}

/**
 * 检查是否到达末尾 / Check if at end of data
 */
bool wav_at_end(void)
{
    if (!g_wav.is_open) return true;
    return g_wav.current_pos >= g_wav.data_size;
}

/*======================================
 *  WAV 关闭 / WAV Close
 *======================================*/

/**
 * 关闭 WAV 文件
 * Close WAV file
 */
void wav_close(void)
{
    if (g_wav.file) {
        fclose(g_wav.file);
        g_wav.file = NULL;
    }
    memset(&g_wav, 0, sizeof(g_wav));
    g_wav.is_open = false;
}

/**
 * 获取总时长字符串 (mm:ss 格式)
 * Get total duration string (mm:ss format)
 */
void wav_get_duration_str(char *buf, int bufsiz)
{
    if (!buf || bufsiz <= 0) return;

    uint32_t duration_ms;
    wav_get_info(NULL, NULL, NULL, NULL, &duration_ms);

    uint32_t total_sec = duration_ms / 1000;
    uint32_t min = total_sec / 60;
    uint32_t sec = total_sec % 60;

    snprintf(buf, bufsiz, "%02lu:%02lu", (unsigned long)min, (unsigned long)sec);
}

/**
 * 获取当前播放时间字符串 (mm:ss 格式)
 * Get current play time string (mm:ss format)
 */
void wav_get_current_time_str(char *buf, int bufsiz)
{
    if (!buf || bufsiz <= 0) return;

    if (!g_wav.is_open) {
        snprintf(buf, bufsiz, "00:00");
        return;
    }

    /* 计算当前时间 / Calculate current time */
    float pos = wav_get_position();
    uint32_t total_ms;
    wav_get_info(NULL, NULL, NULL, NULL, &total_ms);

    uint32_t cur_ms = (uint32_t)((float)total_ms * pos);
    uint32_t total_sec = cur_ms / 1000;
    uint32_t min = total_sec / 60;
    uint32_t sec = total_sec % 60;

    snprintf(buf, bufsiz, "%02lu:%02lu", (unsigned long)min, (unsigned long)sec);
}

#endif /* CONFIG_LVGL */
