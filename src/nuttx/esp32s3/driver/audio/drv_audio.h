/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * drv_audio.h - ESP32-S3 I2S 音频驱动对上契约
 *
 * WHAT : audio_* 公开 API 声明（ESP32-S3 外部 I2S DAC 版）
 * WHY  : common 层 drv_player.c / drv_recorder.c 通过这些符号访问音频
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/audio/drv_audio.h
 * WHEN : 2026-10-04 新增（对齐 common 播放器/录音机调用面）
 * HOW  : 声明与 drv_player.c 中的 extern 声明逐字一致
 */

#ifndef __DRV_AUDIO_H
#define __DRV_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*==========================
 *  公开 API / Public API
 *==========================*/

int  audio_init(void);
void audio_deinit(void);

int  audio_start(void);                       /* 启动 DMA+TX / start DMA+TX */
int  audio_stop(void);                        /* 停止播放 / stop playback */
int  audio_pause(void);                       /* 暂停（门控写路径）/ pause */
int  audio_resume(void);                      /* 恢复 / resume */

int  audio_play_pcm(const void *data, size_t len);   /* 原始 PCM 阻塞写 */

void audio_get_status(bool *playing, bool *paused,
                      uint32_t *sample_rate, uint8_t *volume,
                      uint32_t *bytes_played);

/*
 * 音量约定 / Volume convention:
 *   入参/出参均为 0-100（与 common 播放器一致）
 */
int  audio_set_volume(uint8_t volume);
uint8_t audio_get_volume(void);

int  audio_set_sample_rate(uint32_t rate);

int  audio_play_wav(const char *filename);    /* TODO，当前返回 -ENOSYS */

#endif /* __DRV_AUDIO_H */
