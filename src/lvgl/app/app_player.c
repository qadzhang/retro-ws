/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_player.c - Win3.2 媒体播放器
 *
 * WHAT : Win3.2 媒体播放器
 * WHY  : WAV 音频 GUI 播放（进度/音量/文件选择）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/app_player.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : wav_decoder.c 解码 + drv_player.c 驱动 DAC/I2S 输出
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"
#include "i18n.h"

/*======================================
 *  Windows 3.2 颜色定义
 *======================================*/

#define WIN3_BG         lv_color_hex(0xC0C0C0)   /* 窗口灰 */
#define WIN3_WHITE      lv_color_hex(0xFFFFFF)
#define WIN3_BLACK      lv_color_hex(0x000000)
#define WIN3_BLUE       lv_color_hex(0x000080)   /* 标题栏蓝 */
#define WIN3_TITLE_FG   lv_color_hex(0xFFFFFF)
#define WIN3_LTGRAY     lv_color_hex(0xC0C0C0)
#define WIN3_DKGRAY     lv_color_hex(0x808080)
#define WIN3_BORDER_HI  lv_color_hex(0xFFFFFF)  /* 3D 高光 */
#define WIN3_BORDER_LO  lv_color_hex(0x808080)  /* 3D 阴影 */
#define WIN3_BORDER_MID lv_color_hex(0xC0C0C0)  /* 中间灰 */

/*======================================
 *  布局常量
 *======================================*/

#define WIN_W           360
#define WIN_H           300
#define BORDER_W        2
#define TITLE_H         18
#define MENU_H          20
#define CAPTION_X       18        /* 标题文字 X 偏移 / Title text X offset */
#define STATUS_H        20
#define CONTROL_H       60
#define PROGRESS_H      32
#define BTN_W           48
#define BTN_H           24
#define SLIDER_W        120

/*======================================
 *  播放器状态枚举
 *======================================*/

typedef enum {
    PLAYER_STOPPED = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
} player_state_t;

/*======================================
 *  播放器数据结构
 *======================================*/

typedef struct {
    lv_obj_t *win;           /* 主窗口 */
    lv_obj_t *client;         /* 客户区 */
    lv_obj_t *menu_bar;       /* 菜单栏 */

    /* 文件信息 / File info */
    lv_obj_t *filename_lbl;   /* 文件名标签 */
    lv_obj_t *status_lbl;     /* 状态标签 */
    lv_obj_t *time_lbl;       /* 时间标签 */

    /* 控制按钮 / Control buttons */
    lv_obj_t *btn_play;       /* 播放按钮 */
    lv_obj_t *btn_pause;      /* 暂停按钮 */
    lv_obj_t *btn_stop;       /* 停止按钮 */
    lv_obj_t *btn_open;       /* 打开按钮 */

    /* 进度条 / Progress bar */
    lv_obj_t *progress_bar;   /* 进度条 */
    lv_obj_t *progress_bg;    /* 进度条背景 */
    float     progress;        /* 进度值 0.0 - 1.0 */

    /* 音量 / Volume */
    lv_obj_t *vol_slider;     /* 音量滑块 */
    lv_obj_t *vol_lbl;        /* 音量标签 */

    /* 状态 / State */
    player_state_t state;     /* 当前状态 */
    char     current_file[128]; /* 当前文件 */
    uint8_t  volume;          /* 音量 0-100 */
    uint32_t total_ms;        /* 总时长(ms) */
    uint32_t current_ms;      /* 当前播放位置(ms) */

    /* 定时器 / Timer */
    lv_timer_t *timer;        /* UI 更新定时器 */

} player_context_t;

static player_context_t g_player = {0};

/*======================================
 *  外部函数声明 / External declarations
 *======================================*/

/* WAV 解码器 / WAV decoder */
extern int wav_open(const char *path);
extern void wav_close(void);
extern int wav_read(void *buf, int len);
extern void wav_get_info(uint32_t *sr, uint16_t *ch, uint16_t *bps,
                          uint32_t *size, uint32_t *dur_ms);
extern void wav_get_duration_str(char *buf, int bufsiz);
extern void wav_get_current_time_str(char *buf, int bufsiz);
extern void wav_seek(float percent);
extern bool wav_at_end(void);
extern const char *wav_get_filename(void);

/* 音频驱动 / Audio driver */
extern int audio_init(void);
extern int audio_start(void);
extern int audio_stop(void);
extern int audio_pause(void);
extern int audio_resume(void);
extern int audio_set_volume(uint8_t vol);
extern uint8_t audio_get_volume(void);
extern int audio_play_pcm(const void *data, size_t len);

/*======================================
 *  工具函数 / Utility Functions
 *======================================*/

/**
 * 更新状态标签 / Update status label
 */
static void player_set_status(const char *status_zh, const char *status_en)
{
    if (!g_player.status_lbl) return;

    if (i18n_is_chinese()) {
        lv_label_set_text(g_player.status_lbl, status_zh);
    } else {
        lv_label_set_text(g_player.status_lbl, status_en);
    }
}

/**
 * 更新时间显示 / Update time display
 */
static void player_update_time(void)
{
    if (!g_player.time_lbl) return;

    char buf[32];
    uint32_t cur_ms = g_player.current_ms;
    uint32_t tot_ms = g_player.total_ms;

    uint32_t cur_min = cur_ms / 60000;
    uint32_t cur_sec = (cur_ms % 60000) / 1000;
    uint32_t tot_min = tot_ms / 60000;
    uint32_t tot_sec = (tot_ms % 60000) / 1000;

    snprintf(buf, sizeof(buf), "%02lu:%02lu / %02lu:%02lu",
             (unsigned long)cur_min, (unsigned long)cur_sec,
             (unsigned long)tot_min, (unsigned long)tot_sec);
    lv_label_set_text(g_player.time_lbl, buf);
}

/**
 * 更新进度条 / Update progress bar
 */
static void player_update_progress(float progress)
{
    if (!g_player.progress_bar) return;

    g_player.progress = progress;
    int w = (int)(progress * (WIN_W - BORDER_W * 2 - 4));
    if (w < 0) w = 0;
    lv_obj_set_width(g_player.progress_bar, w);
}

/**
 * 格式化文件大小 / Format file size
 */
static void format_file_size(uint32_t bytes, char *buf, int bufsiz)
{
    if (bytes < 1024) {
        snprintf(buf, bufsiz, "%lu B", (unsigned long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufsiz, "%lu KB", (unsigned long)(bytes / 1024));
    } else {
        snprintf(buf, bufsiz, "%.1f MB", (float)bytes / (1024.0f * 1024.0f));
    }
}

/*======================================
 *  按钮回调 / Button Callbacks
 *======================================*/

/**
 * 播放按钮回调 / Play button callback
 */
static void btn_play_cb(lv_event_t *e)
{
    (void)e;

    if (g_player.state == PLAYER_PLAYING) {
        /* 正在播放 -> 暂停 */
        audio_pause();
        g_player.state = PLAYER_PAUSED;
        player_set_status("已暂停", "Paused");
        lv_obj_set_style_bg_color(g_player.btn_play, WIN3_LTGRAY, LV_PART_MAIN);
        return;
    }

    if (g_player.state == PLAYER_PAUSED) {
        /* 暂停 -> 继续播放 */
        audio_resume();
        g_player.state = PLAYER_PLAYING;
        player_set_status("正在播放", "Playing");
        lv_obj_set_style_bg_color(g_player.btn_play, WIN3_LTGRAY, LV_PART_MAIN);
        return;
    }

    /* 停止状态 -> 开始播放 */
    if (g_player.current_file[0] == 0) {
        /* 没有文件 -> 弹出文件选择器 */
        player_set_status("请选择文件", "Select a file");
        return;
    }

    /* 打开 WAV 文件 / Open WAV file */
    int ret = wav_open(g_player.current_file);
    if (ret < 0) {
        player_set_status("打开失败", "Open failed");
        syslog(LOG_ERR, "Player: failed to open %s\n", g_player.current_file);
        return;
    }

    /* 获取文件信息 / Get file info */
    uint32_t sr, size;
    uint16_t ch, bps;
    wav_get_info(&sr, &ch, &bps, &size, &g_player.total_ms);

    /* 初始化音频 / Initialize audio */
    audio_init();
    audio_set_volume(g_player.volume);
    audio_start();

    g_player.state = PLAYER_PLAYING;
    g_player.current_ms = 0;

    player_set_status("正在播放", "Playing");
    lv_label_set_text(g_player.filename_lbl,
                      wav_get_filename() ? wav_get_filename() : "");
    player_update_time();
    player_update_progress(0.0f);

    syslog(LOG_INFO, "Player: playing %s\n", g_player.current_file);
}

/**
 * 停止按钮回调 / Stop button callback
 */
static void btn_stop_cb(lv_event_t *e)
{
    (void)e;

    audio_stop();
    wav_close();

    g_player.state = PLAYER_STOPPED;
    g_player.current_ms = 0;

    player_set_status("已停止", "Stopped");
    player_update_time();
    player_update_progress(0.0f);

    syslog(LOG_INFO, "Player: stopped\n");
}

/**
 * 打开文件按钮回调 / Open file button callback
 */
static void btn_open_cb(lv_event_t *e)
{
    (void)e;

    /* 停止当前播放 / Stop current playback */
    if (g_player.state != PLAYER_STOPPED) {
        btn_stop_cb(NULL);
    }

    /* 模拟文件选择器 (演示用) / Simulated file picker (demo) */
    /* TODO: 实现真实文件系统选择器 / Implement real file system picker */
    /* ESP32-S3 上可使用 FatFS 枚举 / On ESP32-S3 use FatFS enumeration */

    /* 演示文件名 / Demo filename */
    const char *demo_files[] = {
        "/sdcard/test.wav",
        "/sdcard/music.wav",
        "/sdcard/voice.wav",
    };
    static int demo_idx = 0;

    strncpy(g_player.current_file, demo_files[demo_idx],
            sizeof(g_player.current_file) - 1);
    g_player.current_file[sizeof(g_player.current_file) - 1] = 0;
    demo_idx = (demo_idx + 1) % 3;

    /* 显示文件名 / Show filename */
    lv_label_set_text(g_player.filename_lbl, g_player.current_file);

    /* 获取并显示文件信息 / Get and show file info */
    uint32_t sr, size;
    uint16_t ch, bps;
    uint32_t dur_ms;

    /* 尝试打开获取信息 / Try to open to get info */
    if (wav_open(g_player.current_file) == 0) {
        wav_get_info(&sr, &ch, &bps, &size, &dur_ms);
        wav_close();

        char info_buf[64];
        char size_buf[32];
        format_file_size(size, size_buf, sizeof(size_buf));

        if (i18n_is_chinese()) {
            snprintf(info_buf, sizeof(info_buf), "%luHz %dch %dbit %s",
                     (unsigned long)sr, ch, bps, size_buf);
        } else {
            snprintf(info_buf, sizeof(info_buf), "%luHz %dch %dbit %s",
                     (unsigned long)sr, ch, bps, size_buf);
        }
        player_set_status(info_buf, info_buf);
    } else {
        player_set_status("文件不存在", "File not found");
    }
}

/**
 * 音量滑块回调 / Volume slider callback
 */
static void vol_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int16_t val = lv_slider_get_value(slider);

    g_player.volume = (uint8_t)val;
    audio_set_volume(g_player.volume);

    /* 更新音量标签 / Update volume label */
    if (g_player.vol_lbl) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Vol:%d%%", val);
        lv_label_set_text(g_player.vol_lbl, buf);
    }
}

/**
 * 进度条点击回调 / Progress bar click callback
 */
static void progress_click_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_target(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_point_t point;

    /* 9.5 无 lv_event_get_point，改从输入设备取点
     * LVGL 9.5 has no lv_event_get_point; read from indev instead */
    if (indev == NULL)
        return;
    lv_indev_get_point(indev, &point);

    int bar_w = (int)lv_obj_get_width(bar);
    if (bar_w <= 0) return;

    float percent = (float)point.x / (float)bar_w;
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 1.0f) percent = 1.0f;

    wav_seek(percent);

    /* 更新显示 / Update display */
    if (g_player.total_ms > 0) {
        g_player.current_ms = (uint32_t)((float)g_player.total_ms * percent);
    }
    player_update_progress(percent);
    player_update_time();
}

/*======================================
 *  UI 更新定时器 / UI Update Timer
 *======================================*/

/**
 * 播放更新定时器回调 / Playback update timer callback
 */
static void player_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (g_player.state != PLAYER_PLAYING) {
        return;
    }

    /* 模拟播放进度 / Simulate playback progress */
    /* 真实实现需要 DMA 中断更新 / Real implementation needs DMA interrupt update */
    if (g_player.total_ms > 0) {
        g_player.current_ms += 100;  /* 每 100ms 更新一次 */
        if (g_player.current_ms > g_player.total_ms) {
            g_player.current_ms = g_player.total_ms;
        }

        float progress = (float)g_player.current_ms / (float)g_player.total_ms;
        player_update_progress(progress);
        player_update_time();

        /* 检查是否播放完毕 / Check if playback finished */
        if (g_player.current_ms >= g_player.total_ms) {
            btn_stop_cb(NULL);
        }
    }
}

/*======================================
 *  窗口关闭回调 / Window Close Callback
 *======================================*/

static void win_close_cb(lv_event_t *e)
{
    (void)e;

    /* 停止播放 / Stop playback */
    if (g_player.state != PLAYER_STOPPED) {
        audio_stop();
        wav_close();
    }

    /* 删除定时器 / Delete timer */
    if (g_player.timer) {
        lv_timer_delete(g_player.timer);
        g_player.timer = NULL;
    }

    /* 删除窗口 / Delete window */
    if (g_player.win) {
        lv_obj_delete(g_player.win);
        g_player.win = NULL;
    }

    syslog(LOG_INFO, "Player: window closed\n");
}

/*======================================
 *  创建 Windows 3.2 风格按钮
 *======================================*/

/**
 * 创建 3D 风格按钮 / Create 3D style button
 */
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
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
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
 * 创建媒体播放器窗口
 * Create media player window
 */
lv_obj_t *player_create(void)
{
    /* 重复创建时先删除旧窗口与定时器（镜像 win_close_cb，防泄漏）
     * Delete existing window+timer first (mirror win_close_cb, avoid leaks) */
    if (g_player.timer) {
        lv_timer_delete(g_player.timer);
        g_player.timer = NULL;
    }
    if (g_player.win) {
        if (g_player.state != PLAYER_STOPPED) {
            audio_stop();
            wav_close();
        }
        lv_obj_delete(g_player.win);
        g_player.win = NULL;
    }

    memset(&g_player, 0, sizeof(g_player));

    /* 获取桌面 / Get desktop */
    lv_obj_t *desktop = lv_screen_active();

    /* === 创建窗口 / Create window === */
    g_player.win = lv_obj_create(desktop);
    lv_obj_set_size(g_player.win, WIN_W, WIN_H);
    lv_obj_set_pos(g_player.win, 60, 40);
    lv_obj_set_style_bg_color(g_player.win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_player.win, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_player.win, 0, LV_PART_MAIN);

    /* === 标题栏 / Title bar === */
    lv_obj_t *title_bar = lv_obj_create(g_player.win);
    lv_obj_set_pos(title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(title_bar, WIN_W - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(title_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    /* 标题文字 / Title text */
    const char *title_text = i18n_is_chinese() ? "媒体播放器" : "Media Player";
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
    g_player.menu_bar = lv_obj_create(g_player.win);
    lv_obj_set_pos(g_player.menu_bar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(g_player.menu_bar, WIN_W - BORDER_W * 2, MENU_H);
    lv_obj_set_style_bg_color(g_player.menu_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_player.menu_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_player.menu_bar, WIN3_BORDER_HI, LV_PART_MAIN);
    lv_obj_set_style_radius(g_player.menu_bar, 0, LV_PART_MAIN);

    /* 菜单项 / Menu items */
    const char *menu_items[] = {"File", "Play", "Help"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *mi = lv_obj_create(g_player.menu_bar);
        lv_obj_set_size(mi, 36, MENU_H - 2);
        lv_obj_set_pos(mi, BORDER_W + 2 + i * 40, 1);
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
    g_player.client = lv_obj_create(g_player.win);
    lv_obj_set_pos(g_player.client, BORDER_W, client_y);
    lv_obj_set_size(g_player.client, WIN_W - BORDER_W * 2, client_h);
    lv_obj_set_style_bg_color(g_player.client, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_player.client, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_player.client, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(g_player.client, 0, LV_PART_MAIN);

    /* === 文件名显示 / Filename display === */
    lv_obj_t *fn_bg = lv_obj_create(g_player.client);
    lv_obj_set_pos(fn_bg, 8, 8);
    lv_obj_set_size(fn_bg, WIN_W - BORDER_W * 2 - 16, 20);
    lv_obj_set_style_bg_color(fn_bg, WIN3_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_width(fn_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(fn_bg, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(fn_bg, 0, LV_PART_MAIN);

    g_player.filename_lbl = lv_label_create(fn_bg);
    lv_label_set_text(g_player.filename_lbl, i18n_is_chinese() ? "(未选择文件)" : "(No file selected)");
    lv_obj_set_style_text_font(g_player.filename_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(g_player.filename_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* === 进度条 / Progress bar === */
    int progress_y = 36;

    /* 进度条背景 / Progress bar background */
    g_player.progress_bg = lv_obj_create(g_player.client);
    lv_obj_set_pos(g_player.progress_bg, 8, progress_y);
    lv_obj_set_size(g_player.progress_bg, WIN_W - BORDER_W * 2 - 16, 16);
    lv_obj_set_style_bg_color(g_player.progress_bg, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_player.progress_bg, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_player.progress_bg, WIN3_BORDER_MID, LV_PART_MAIN);
    lv_obj_set_style_radius(g_player.progress_bg, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(g_player.progress_bg, progress_click_cb, LV_EVENT_CLICKED, NULL);

    /* 进度条填充 / Progress bar fill */
    g_player.progress_bar = lv_obj_create(g_player.progress_bg);
    lv_obj_set_pos(g_player.progress_bar, 0, 0);
    lv_obj_set_size(g_player.progress_bar, 0, 14);
    lv_obj_set_style_bg_color(g_player.progress_bar, WIN3_BLUE, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_player.progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_player.progress_bar, 0, LV_PART_MAIN);

    /* === 时间显示 / Time display === */
    g_player.time_lbl = lv_label_create(g_player.client);
    lv_label_set_text(g_player.time_lbl, "00:00 / 00:00");
    lv_obj_set_pos(g_player.time_lbl, 8, progress_y + 18);
    lv_obj_set_style_text_font(g_player.time_lbl, RETRO_FONT_DEFAULT, 0);

    /* === 控制按钮 / Control buttons === */
    int btn_y = progress_y + 40;

    /* Open 按钮 */
    g_player.btn_open = create_win3_btn(g_player.client,
                                        i18n_is_chinese() ? "打开" : "Open",
                                        BTN_W, BTN_H, btn_open_cb);
    lv_obj_set_pos(g_player.btn_open, 8, btn_y);

    /* Play/Pause 按钮 */
    g_player.btn_play = create_win3_btn(g_player.client,
                                        i18n_is_chinese() ? "播放" : "Play",
                                        BTN_W, BTN_H, btn_play_cb);
    lv_obj_set_pos(g_player.btn_play, 8 + BTN_W + 4, btn_y);

    /* Stop 按钮 */
    g_player.btn_stop = create_win3_btn(g_player.client,
                                        i18n_is_chinese() ? "停止" : "Stop",
                                        BTN_W, BTN_H, btn_stop_cb);
    lv_obj_set_pos(g_player.btn_stop, 8 + BTN_W * 2 + 8, btn_y);

    /* === 音量控制 / Volume control === */
    /* 音量标签 / Volume label */
    g_player.vol_lbl = lv_label_create(g_player.client);
    char vol_buf[16];
    snprintf(vol_buf, sizeof(vol_buf), "Vol:%d%%", g_player.volume);
    lv_label_set_text(g_player.vol_lbl, vol_buf);
    lv_obj_set_pos(g_player.vol_lbl, WIN_W - BORDER_W * 2 - SLIDER_W - 20, btn_y + 3);
    lv_obj_set_style_text_font(g_player.vol_lbl, RETRO_FONT_DEFAULT, 0);

    /* 音量滑块 / Volume slider */
    g_player.vol_slider = lv_slider_create(g_player.client);
    lv_obj_set_size(g_player.vol_slider, SLIDER_W, 12);
    lv_obj_set_pos(g_player.vol_slider, WIN_W - BORDER_W * 2 - SLIDER_W - 8, btn_y + 6);
    lv_slider_set_range(g_player.vol_slider, 0, 100);
    lv_slider_set_value(g_player.vol_slider, g_player.volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_player.vol_slider, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_player.vol_slider, WIN3_BLUE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_player.vol_slider, vol_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* === 状态栏 / Status bar === */
    int status_y = client_y + client_h;
    lv_obj_t *status_bar = lv_obj_create(g_player.win);
    lv_obj_set_pos(status_bar, BORDER_W, status_y);
    lv_obj_set_size(status_bar, WIN_W - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(status_bar, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(status_bar, WIN3_BORDER_LO, LV_PART_MAIN);
    lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN);

    g_player.status_lbl = lv_label_create(status_bar);
    lv_label_set_text(g_player.status_lbl, i18n_is_chinese() ? "就绪" : "Ready");
    lv_obj_set_style_text_font(g_player.status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(g_player.status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* === 启动更新定时器 / Start update timer === */
    g_player.volume = 80;  /* 默认音量 80% */
    g_player.state = PLAYER_STOPPED;
    g_player.timer = lv_timer_create(player_timer_cb, 100, NULL);  /* 100ms 间隔 */
    lv_timer_set_repeat_count(g_player.timer, -1);

    /* 初始化音频子系统 / Initialize audio subsystem */
    audio_init();

    /* 移到最前 / Bring to front
     * move_to_index(0xFFFF) 在 9.5 为 no-op，用最后子索引
     * move_to_index(0xFFFF) is a no-op in 9.5; use last child index */
    lv_obj_move_to_index(g_player.win,
        (int32_t)lv_obj_get_child_count(lv_obj_get_parent(g_player.win)) - 1);

    syslog(LOG_INFO, "Player: window created\n");

    return g_player.win;
}

/*======================================
 *  外部接口 / External Interface
 *======================================*/

/**
 * 获取播放器窗口
 * Get player window object
 */
lv_obj_t *get_player_window(void)
{
    return g_player.win;
}

/**
 * 播放指定文件
 * Play specified file
 */
int player_play_file(const char *path)
{
    if (!path) return -EINVAL;

    /* 外部入口（如录音机）调用时窗口可能不存在，先创建
     * Window may not exist when invoked externally (e.g. recorder) */
    if (g_player.win == NULL) {
        player_create();
        if (g_player.win == NULL)
            return -ENODEV;
    }

    strncpy(g_player.current_file, path, sizeof(g_player.current_file) - 1);
    g_player.current_file[sizeof(g_player.current_file) - 1] = 0;

    lv_label_set_text(g_player.filename_lbl, path);
    btn_play_cb(NULL);

    return 0;
}

#endif /* CONFIG_LVGL */
