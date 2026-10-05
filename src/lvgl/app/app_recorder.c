/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_recorder.c - Win3.2 录音机
 *
 * WHAT : Win3.2 录音机
 * WHY  : 音频录制与回放（波形条形图/时长显示）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/app_recorder.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : ADC 采集经 drv_recorder.c 写 WAV，回放走播放器通道
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"
#include "i18n.h"

/*======================================
 *  Windows 3.2 颜色定义
 *======================================*/

#define WIN3_BG         lv_color_hex(0xC0C0C0)
#define WIN3_WHITE      lv_color_hex(0xFFFFFF)
#define WIN3_BLACK      lv_color_hex(0x000000)
#define WIN3_BLUE       lv_color_hex(0x000080)
#define WIN3_RED        lv_color_hex(0xFF0000)
#define WIN3_TITLE_FG   lv_color_hex(0xFFFFFF)
#define WIN3_LTGRAY     lv_color_hex(0xC0C0C0)
#define WIN3_DKGRAY     lv_color_hex(0x808080)
#define WIN3_BORDER_HI  lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_LO  lv_color_hex(0x808080)
#define WIN3_BORDER_MID lv_color_hex(0xC0C0C0)
#define CAPTION_X       18        /* 标题文字 X 偏移 / Title text X offset */
#define WIN3_WAVEFORM   lv_color_hex(0x008000)  /* 波形绿色 */

/*======================================
 *  布局常量
 *======================================*/

#define WIN_W           320
#define WIN_H           280
#define BORDER_W        2
#define TITLE_H         18
#define MENU_H          20
#define STATUS_H        20
#define WAVEFORM_H      60
#define BTN_W           60
#define BTN_H           24
#define MAX_DURATION_MS (60 * 1000)  /* 最大录音时长 / Max recording duration */

/*======================================
 *  录音状态枚举
 *======================================*/

typedef enum {
    REC_STOPPED = 0,
    REC_RECORDING,
    REC_PAUSED,
} rec_state_t;

/*======================================
 *  录音机数据结构
 *======================================*/

typedef struct {
    lv_obj_t *win;           /* 主窗口 */
    lv_obj_t *client;         /* 客户区 */
    lv_obj_t *menu_bar;       /* 菜单栏 */

    /* 时间显示 / Time display */
    lv_obj_t *time_lbl;       /* 录音时长标签 */
    uint32_t record_time_ms;  /* 当前录音时长(ms) */

    /* 波形显示 / Waveform display */
    lv_obj_t *waveform_bg;    /* 波形背景 */
    lv_obj_t *waveform_bars[16]; /* 简化条形波形(16个) */

    /* 控制按钮 / Control buttons */
    lv_obj_t *btn_record;     /* 录音按钮 */
    lv_obj_t *btn_stop;       /* 停止按钮 */
    lv_obj_t *btn_play;       /* 播放按钮 */
    lv_obj_t *btn_save;       /* 保存按钮 */

    /* 状态显示 / Status display */
    lv_obj_t *status_lbl;     /* 状态标签 */
    lv_obj_t *status_bar;     /* 状态栏 */

    /* 状态 / State */
    rec_state_t state;        /* 当前状态 */
    FILE *record_file;        /* 录音文件句柄 */
    char save_path[128];      /* 保存路径 */
    uint8_t volume;           /* 音量 */

    /* 定时器 / Timer */
    lv_timer_t *timer;        /* 定时器 */
    uint32_t max_duration_ms; /* 最大录音时长 */

} rec_context_t;

static rec_context_t g_rec = {0};

/*======================================
 *  外部函数声明 / External declarations
 *======================================*/

/* 音频驱动 / Audio driver */
extern int audio_init(void);
extern int audio_start(void);
extern int audio_stop(void);
extern int audio_set_volume(uint8_t vol);

/* WAV 解码器 (复用播放器的) / WAV decoder (reuse player's) */
extern int wav_open(const char *path);
extern void wav_close(void);
extern int wav_read(void *buf, int len);
extern void wav_get_info(uint32_t *sr, uint16_t *ch, uint16_t *bps,
                         uint32_t *size, uint32_t *dur_ms);

/* 播放器 / Player */
extern int player_play_file(const char *path);

/*======================================
 *  工具函数 / Utility Functions
 *======================================*/

/**
 * 更新时间显示 / Update time display
 */
static void rec_update_time(void)
{
    if (!g_rec.time_lbl) return;

    uint32_t ms = g_rec.record_time_ms;
    uint32_t min = ms / 60000;
    uint32_t sec = (ms % 60000) / 1000;
    uint32_t centisec = (ms % 1000) / 10;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu.%02lu",
             (unsigned long)min, (unsigned long)sec,
             (unsigned long)centisec);
    lv_label_set_text(g_rec.time_lbl, buf);

    /* 更新进度 / Update progress
     * 以时间标签宽度按比例反映录音进度（简单方式）
     * Reflect progress by time label; full progress bar TODO */
    if (g_rec.max_duration_ms > 0) {
        float progress = (float)ms / (float)g_rec.max_duration_ms;
        if (progress > 1.0f) progress = 1.0f;
        (void)progress;
    }
}

/**
 * 更新波形显示 / Update waveform display
 * 简化实现: 随机生成条形图
 * Simplified implementation: random bar chart
 */
static void rec_update_waveform(void)
{
    if (g_rec.state != REC_RECORDING) {
        /* 停止时清空波形 / Clear waveform when stopped */
        for (int i = 0; i < 16; i++) {
            if (g_rec.waveform_bars[i]) {
                lv_obj_set_height(g_rec.waveform_bars[i], 2);
            }
        }
        return;
    }

    /* 模拟波形数据 / Simulate waveform data */
    /* 真实实现需要从 DMA 缓冲区读取 / Real implementation needs DMA buffer read */
    for (int i = 0; i < 16; i++) {
        if (g_rec.waveform_bars[i]) {
            /* 随机高度 2-40 / Random height 2-40 */
            int h = 2 + (rand() % 40);
            lv_obj_set_height(g_rec.waveform_bars[i], h);
        }
    }
}

/**
 * 设置状态 / Set status
 */
static void rec_set_status(const char *status_zh, const char *status_en)
{
    if (!g_rec.status_lbl) return;

    if (i18n_is_chinese()) {
        lv_label_set_text(g_rec.status_lbl, status_zh);
    } else {
        lv_label_set_text(g_rec.status_lbl, status_en);
    }
}

/*======================================
 *  WAV 文件写入 / WAV File Write
 *======================================*/

/**
 * 写入 WAV 文件头
 * Write WAV file header
 *
 * @param f   文件句柄
 * @param sr  采样率
 * @param ch  声道数
 * @param bps 位深
 * @param data_size PCM 数据大小
 */
static void write_wav_header(FILE *f, uint32_t sr, uint16_t ch,
                              uint16_t bps, uint32_t data_size)
{
    uint32_t chunk_size = 36 + data_size;
    uint32_t byte_rate = sr * ch * bps / 8;
    uint16_t block_align = ch * bps / 8;

    /* RIFF header */
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
 * 生成默认文件名
 * Generate default filename
 */
static void generate_filename(char *buf, int bufsiz)
{
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    snprintf(buf, bufsiz,
             "/sdcard/rec_%04d%02d%02d_%02d%02d%02d.wav",
             tm_now->tm_year + 1900,
             tm_now->tm_mon + 1,
             tm_now->tm_mday,
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec);
}

/*======================================
 *  按钮回调 / Button Callbacks
 *======================================*/

/**
 * 录音按钮回调 / Record button callback
 */
static void btn_record_cb(lv_event_t *e)
{
    (void)e;

    if (g_rec.state == REC_RECORDING) {
        /* 正在录音 -> 暂停 */
        g_rec.state = REC_PAUSED;
        rec_set_status("已暂停", "Paused");
        lv_obj_set_style_bg_color(g_rec.btn_record, WIN3_LTGRAY, LV_PART_MAIN);
        return;
    }

    if (g_rec.state == REC_PAUSED) {
        /* 暂停 -> 继续录音 */
        g_rec.state = REC_RECORDING;
        rec_set_status("正在录音", "Recording");
        lv_obj_set_style_bg_color(g_rec.btn_record, WIN3_RED, LV_PART_MAIN);
        return;
    }

    /* 停止状态 -> 开始录音 */
    /* 生成文件名 / Generate filename */
    generate_filename(g_rec.save_path, sizeof(g_rec.save_path));

    /* 打开文件写入 / Open file for writing */
    g_rec.record_file = fopen(g_rec.save_path, "wb");
    if (!g_rec.record_file) {
        rec_set_status("打开失败", "Open failed");
        syslog(LOG_ERR, "Recorder: failed to open %s\n", g_rec.save_path);
        return;
    }

    /* 写入占位符 header (录音结束后更新) / Write placeholder header */
    write_wav_header(g_rec.record_file, 16000, 1, 16, 0);

    /* 重置计时 / Reset timer */
    g_rec.record_time_ms = 0;
    g_rec.state = REC_RECORDING;

    rec_set_status("正在录音", "Recording");
    lv_obj_set_style_bg_color(g_rec.btn_record, WIN3_RED, LV_PART_MAIN);

    audio_init();

    syslog(LOG_INFO, "Recorder: started recording to %s\n", g_rec.save_path);
}

/**
 * 停止按钮回调 / Stop button callback
 */
static void btn_stop_cb(lv_event_t *e)
{
    (void)e;

    if (g_rec.state == REC_STOPPED) {
        return;
    }

    /* 关闭文件 / Close file */
    if (g_rec.record_file) {
        /* 更新 WAV header 中的 data_size / Update WAV header data_size */
        uint32_t data_size = g_rec.record_time_ms * 16000 / 1000 * 2;  /* 16-bit mono */

        /* 跳回文件头更新 / Seek back to update header */
        fseek(g_rec.record_file, 0, SEEK_SET);
        write_wav_header(g_rec.record_file, 16000, 1, 16, data_size);

        fclose(g_rec.record_file);
        g_rec.record_file = NULL;
    }

    g_rec.state = REC_STOPPED;
    rec_set_status("已停止", "Stopped");
    lv_obj_set_style_bg_color(g_rec.btn_record, WIN3_LTGRAY, LV_PART_MAIN);

    syslog(LOG_INFO, "Recorder: stopped, saved to %s\n", g_rec.save_path);
}

/**
 * 播放录音按钮回调 / Play recording button callback
 */
static void btn_play_cb(lv_event_t *e)
{
    (void)e;

    if (g_rec.save_path[0] == 0) {
        rec_set_status("没有录音", "No recording");
        return;
    }

    /* 使用播放器播放 / Use player to play */
    player_play_file(g_rec.save_path);
    rec_set_status("正在播放", "Playing");
}

/**
 * 保存按钮回调 / Save button callback
 */
static void btn_save_cb(lv_event_t *e)
{
    (void)e;

    if (g_rec.state != REC_STOPPED) {
        btn_stop_cb(NULL);
    }

    if (g_rec.save_path[0] == 0) {
        rec_set_status("没有录音", "No recording");
        return;
    }

    /* 显示保存路径 / Show save path */
    rec_set_status(g_rec.save_path, g_rec.save_path);
    syslog(LOG_INFO, "Recorder: saved to %s\n", g_rec.save_path);
}

/*======================================
 *  定时器回调 / Timer Callback
 *======================================*/

/**
 * 录音定时器回调 / Recording timer callback
 */
static void rec_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (g_rec.state != REC_RECORDING) {
        return;
    }

    g_rec.record_time_ms += 100;

    /* 检查最大时长 / Check max duration */
    if (g_rec.record_time_ms >= g_rec.max_duration_ms) {
        btn_stop_cb(NULL);
        rec_set_status("达到最大时长", "Max duration reached");
        return;
    }

    /* 更新显示 / Update display */
    rec_update_time();
    rec_update_waveform();

    /* TODO: 从 I2S DMA 读取音频数据并写入文件
     * TODO: Read audio data from I2S DMA and write to file
     * 真实实现: i2s_read_samples(buf, len) 后 fwrite 到 g_rec.record_file */
}

/*======================================
 *  窗口关闭回调 / Window Close Callback
 *======================================*/

static void win_close_cb(lv_event_t *e)
{
    (void)e;

    /* 停止录音 / Stop recording */
    if (g_rec.state != REC_STOPPED) {
        btn_stop_cb(NULL);
    }

    /* 删除定时器 / Delete timer */
    if (g_rec.timer) {
        lv_timer_delete(g_rec.timer);
        g_rec.timer = NULL;
    }

    /* 删除窗口 / Delete window */
    if (g_rec.win) {
        lv_obj_delete(g_rec.win);
        g_rec.win = NULL;
    }

    syslog(LOG_INFO, "Recorder: window closed\n");
}

/*======================================
 *  创建 3D 按钮 / Create 3D Button
 *======================================*/

static lv_obj_t *create_win3_btn(lv_obj_t *parent, const char *text,
                                  lv_coord_t w, lv_coord_t h,
                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    return btn;
}

/*======================================
 *  主创建函数 / Main Create Function
 *======================================*/

/**
 * 创建录音机窗口
 * Create recorder window
 */
lv_obj_t *recorder_create(void)
{
    /* 重复创建时先停止录音并删除旧窗口/定时器（镜像 win_close_cb，防泄漏）
     * Stop recording and delete existing window+timer first (avoid leaks) */
    if (g_rec.timer) {
        lv_timer_delete(g_rec.timer);
        g_rec.timer = NULL;
    }
    if (g_rec.win) {
        if (g_rec.state != REC_STOPPED) {
            btn_stop_cb(NULL);
        }
        lv_obj_delete(g_rec.win);
        g_rec.win = NULL;
    }

    memset(&g_rec, 0, sizeof(g_rec));

    /* 获取桌面 / Get desktop */
    lv_obj_t *desktop = lv_screen_active();

    /* === 创建窗口 / Create window === */
    g_rec.win = lv_obj_create(desktop);
    lv_obj_set_size(g_rec.win, WIN_W, WIN_H);
    lv_obj_set_pos(g_rec.win, 80, 60);
    lv_obj_set_style_bg_color(g_rec.win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rec.win, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_rec.win, 0, LV_PART_MAIN);

    /* === 标题栏 / Title bar === */
    lv_obj_t *title_bar = lv_obj_create(g_rec.win);
    lv_obj_set_pos(title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(title_bar, WIN_W - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(title_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    /* 标题文字 / Title text */
    const char *title_text = i18n_is_chinese() ? "录音机" : "Recorder";
    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, title_text);
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, CAPTION_X, 0);

    /* 关闭按钮 / Close button */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 16, 14);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(close_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(close_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, win_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, "x");
    lv_obj_set_style_text_font(x_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(x_lbl, LV_ALIGN_CENTER, 0, 0);

    /* === 菜单栏 / Menu bar === */
    g_rec.menu_bar = lv_obj_create(g_rec.win);
    lv_obj_set_pos(g_rec.menu_bar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(g_rec.menu_bar, WIN_W - BORDER_W * 2, MENU_H);
    lv_obj_set_style_bg_color(g_rec.menu_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rec.menu_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rec.menu_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(g_rec.menu_bar, 0, LV_PART_MAIN);

    /* 菜单项 / Menu items */
    const char *menu_items[] = {"File", "Edit", "Help"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *mi = lv_obj_create(g_rec.menu_bar);
        lv_obj_set_size(mi, 32, MENU_H - 2);
        lv_obj_set_pos(mi, BORDER_W + 2 + i * 36, 1);
        lv_obj_set_style_bg_color(mi, WIN3_LTGRAY, LV_PART_MAIN);
        lv_obj_set_style_border_width(mi, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(mi, 0, LV_PART_MAIN);

        lv_obj_t *ml = lv_label_create(mi);
        lv_label_set_text(ml, menu_items[i]);
        lv_obj_set_style_text_font(ml, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(ml, LV_ALIGN_CENTER, 0, 0);
    }

    /* === 客户区 / Client area === */
    int client_y = BORDER_W + TITLE_H + MENU_H;
    int client_h = WIN_H - BORDER_W * 2 - TITLE_H - MENU_H - STATUS_H - BORDER_W;
    g_rec.client = lv_obj_create(g_rec.win);
    lv_obj_set_pos(g_rec.client, BORDER_W, client_y);
    lv_obj_set_size(g_rec.client, WIN_W - BORDER_W * 2, client_h);
    lv_obj_set_style_bg_color(g_rec.client, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rec.client, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rec.client, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(g_rec.client, 0, LV_PART_MAIN);

    /* === 时间显示 / Time display === */
    g_rec.time_lbl = lv_label_create(g_rec.client);
    lv_label_set_text(g_rec.time_lbl, "00:00.00");
    lv_obj_set_pos(g_rec.time_lbl, 8, 8);
    lv_obj_set_style_text_font(g_rec.time_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_rec.time_lbl, WIN3_BLACK, LV_PART_MAIN);

    /* 最大时长提示 / Max duration hint */
    lv_obj_t *max_lbl = lv_label_create(g_rec.client);
    char max_buf[32];
    snprintf(max_buf, sizeof(max_buf), "/ %ds", MAX_DURATION_MS / 1000);
    lv_label_set_text(max_lbl, max_buf);
    lv_obj_set_pos(max_lbl, 120, 14);
    lv_obj_set_style_text_font(max_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(max_lbl, WIN3_DKGRAY, LV_PART_MAIN);

    /* === 波形显示 / Waveform display === */
    int waveform_y = 44;

    g_rec.waveform_bg = lv_obj_create(g_rec.client);
    lv_obj_set_pos(g_rec.waveform_bg, 8, waveform_y);
    lv_obj_set_size(g_rec.waveform_bg, WIN_W - BORDER_W * 2 - 16, WAVEFORM_H);
    lv_obj_set_style_bg_color(g_rec.waveform_bg, WIN3_BLACK, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rec.waveform_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rec.waveform_bg, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(g_rec.waveform_bg, 0, LV_PART_MAIN);

    /* 创建 16 个波形条 / Create 16 waveform bars */
    int bar_w = (WIN_W - BORDER_W * 2 - 16 - 2 * 16) / 16;  /* 间距 2px */
    for (int i = 0; i < 16; i++) {
        g_rec.waveform_bars[i] = lv_obj_create(g_rec.waveform_bg);
        lv_obj_set_pos(g_rec.waveform_bars[i],
                       8 + i * (bar_w + 2),
                       WAVEFORM_H / 2 - 1);
        lv_obj_set_size(g_rec.waveform_bars[i], bar_w, 2);
        lv_obj_set_style_bg_color(g_rec.waveform_bars[i], WIN3_WAVEFORM, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_rec.waveform_bars[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(g_rec.waveform_bars[i], 0, LV_PART_MAIN);
    }

    /* === 控制按钮 / Control buttons === */
    int btn_y = waveform_y + WAVEFORM_H + 12;

    /* 录音按钮 / Record button */
    g_rec.btn_record = create_win3_btn(g_rec.client,
                                        i18n_is_chinese() ? "录音" : "Rec",
                                        BTN_W, BTN_H, btn_record_cb);
    lv_obj_set_pos(g_rec.btn_record, 20, btn_y);

    /* 停止按钮 / Stop button */
    g_rec.btn_stop = create_win3_btn(g_rec.client,
                                      i18n_is_chinese() ? "停止" : "Stop",
                                      BTN_W, BTN_H, btn_stop_cb);
    lv_obj_set_pos(g_rec.btn_stop, 20 + BTN_W + 8, btn_y);

    /* 播放按钮 / Play button */
    g_rec.btn_play = create_win3_btn(g_rec.client,
                                      i18n_is_chinese() ? "播放" : "Play",
                                      BTN_W, BTN_H, btn_play_cb);
    lv_obj_set_pos(g_rec.btn_play, WIN_W - BORDER_W * 2 - BTN_W * 3 - 16, btn_y);

    /* 保存按钮 / Save button */
    g_rec.btn_save = create_win3_btn(g_rec.client,
                                      i18n_is_chinese() ? "保存" : "Save",
                                      BTN_W, BTN_H, btn_save_cb);
    lv_obj_set_pos(g_rec.btn_save, WIN_W - BORDER_W * 2 - BTN_W * 2 - 8, btn_y);

    /* === 状态栏 / Status bar === */
    int status_y = client_y + client_h;
    g_rec.status_bar = lv_obj_create(g_rec.win);
    lv_obj_set_pos(g_rec.status_bar, BORDER_W, status_y);
    lv_obj_set_size(g_rec.status_bar, WIN_W - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(g_rec.status_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_rec.status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_rec.status_bar, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(g_rec.status_bar, 0, LV_PART_MAIN);

    g_rec.status_lbl = lv_label_create(g_rec.status_bar);
    lv_label_set_text(g_rec.status_lbl, i18n_is_chinese() ? "就绪" : "Ready");
    lv_obj_set_style_text_font(g_rec.status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(g_rec.status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* === 初始化状态 / Initialize state === */
    g_rec.state = REC_STOPPED;
    g_rec.record_time_ms = 0;
    g_rec.max_duration_ms = MAX_DURATION_MS;
    g_rec.save_path[0] = 0;

    /* === 启动定时器 / Start timer === */
    g_rec.timer = lv_timer_create(rec_timer_cb, 100, NULL);  /* 100ms 间隔 */
    lv_timer_set_repeat_count(g_rec.timer, -1);

    /* 移到最前 / Bring to front
     * move_to_index(0xFFFF) 在 9.5 为 no-op，用最后子索引
     * move_to_index(0xFFFF) is a no-op in 9.5; use last child index */
    lv_obj_move_to_index(g_rec.win,
        (int32_t)lv_obj_get_child_count(lv_obj_get_parent(g_rec.win)) - 1);

    syslog(LOG_INFO, "Recorder: window created\n");

    return g_rec.win;
}

#endif /* CONFIG_LVGL */
