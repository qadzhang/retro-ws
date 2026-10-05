/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_pinyin.c - 拼音输入法 GUI 组件
 *
 * WHAT : 拼音输入法 GUI 组件（LVGL 9.5 重写版）
 * WHY  : GUI 全环境中文输入（REQUIREMENTS 2.1.3）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/lvgl/app/app_pinyin.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 LVGL 9.5 API 全量重写（5W1H, AGENTS.md 4.0）
 * HOW  : 拼音串匹配 pinyin_ime.c 词库，候选框选择后注入 LVGL textarea
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "fonts/pinyin_ime.h"
#include "retro_font.h"
#include "i18n.h"

/*======================================
 *  颜色定义 / Color Definitions
 *======================================*/
#define PINYIN_BG_COLOR    lv_color_hex(0xF0F0F0)
#define PINYIN_BORDER      lv_color_hex(0x808080)
#define PINYIN_TEXT        lv_color_hex(0x000000)
#define PINYIN_CAND_BG     lv_color_hex(0xFFFFFF)
#define PINYIN_CAND_HL     lv_color_hex(0x0080FF)
#define PINYIN_STATUS_BG   lv_color_hex(0xE0E0E0)

/*======================================
 *  布局常量 / Layout Constants
 *======================================*/
#define PINYIN_PANEL_H    72          /* 输入面板总高（2026-10-05 定稿：
                                         拼音行 y6-18 / 候选行 y22-50 /
                                         状态行 y54-68——原 36+20 叠加布局
                                         超屏且行重叠，从未渲染故未暴露） */
#define CAND_BTN_W        36          /* 候选按钮宽度 / Candidate button width */
#define CAND_BTN_H        28          /* 候选按钮高度 / Candidate button height */
#define MODE_BTN_W        40          /* 模式切换按钮宽度 / Mode button width */
#define STATUS_BAR_H      20          /* 状态栏高度 / Status bar height */
#define CAND_BTN_COUNT    9           /* 候选按钮数 / Candidate button count */

/*======================================
 *  输入模式 / Input Mode
 *======================================*/
typedef enum {
    MODE_EN = 0,     /* 英文模式 / English mode */
    MODE_ZH,         /* 中文模式 / Chinese mode */
    MODE_MAX
} input_mode_t;

/*======================================
 *  IME 状态结构 / IME State Structure
 *======================================*/
typedef struct {
    lv_obj_t *panel;          /* 主面板 / Main panel */
    lv_obj_t *pinyin_label;   /* 当前拼音显示 / Current pinyin display */
    lv_obj_t *cand_container; /* 候选词容器 / Candidate container */
    lv_obj_t *cand_btns[CAND_BTN_COUNT];  /* 候选词按钮 / Candidate buttons */
    lv_obj_t *mode_btn;       /* 中英文切换按钮 / Mode switch button */
    lv_obj_t *status_bar;     /* 状态栏 / Status bar */

    input_mode_t mode;        /* 当前输入模式 / Current input mode */
    int has_candidates;       /* 是否有候选词 / Has candidates flag */
    char pinyin_buf[64];      /* 拼音缓冲区 / Pinyin buffer */

    /* 关联的 textarea / Associated textarea */
    lv_obj_t *target_ta;      /* 目标输入框 / Target textarea */
    void (*commit_cb)(uint16_t ch);  /* 字符提交回调 / Character commit callback */
} pinyin_ime_ui_t;

static pinyin_ime_ui_t g_pinyin_ui;

/*======================================
 *  内部函数声明 / Internal Function Declarations
 *======================================*/
static void pinyin_ime_btn_event_cb(lv_event_t *e);
static void pinyin_ime_mode_btn_event_cb(lv_event_t *e);
static void pinyin_ime_update_candidates(void);
static void pinyin_ime_commit_char(uint16_t ch);
static void pinyin_encode_utf8(uint16_t ch, char *buf, int bufsiz);

/*======================================
 *  UTF-8 编码 / UTF-8 Encoding
 *======================================*/

/*
 * pinyin_encode_utf8 - 把码点编码为 UTF-8 / Encode code point as UTF-8
 * WHAT: 码点 → UTF-8 字节串
 * WHY  : LVGL label/textarea 文本均为 UTF-8
 * HOW  : 按码点范围选择 1/2/3 字节编码
 * 注意: 词库存的是 GB2312 编码，此处按 Unicode 码点处理，
 *       完整正确显示需要 GB2312→Unicode 映射表（TODO）
 * Note: dictionary stores GB2312 codes treated as code points;
 *       proper glyphs need a GB2312→Unicode table (TODO)
 */
static void pinyin_encode_utf8(uint16_t ch, char *buf, int bufsiz)
{
    if (bufsiz < 5)
        return;

    if (ch < 0x80) {
        buf[0] = (char)ch;
        buf[1] = '\0';
    } else if (ch < 0x800) {
        buf[0] = (char)(0xC0 | (ch >> 6));
        buf[1] = (char)(0x80 | (ch & 0x3F));
        buf[2] = '\0';
    } else {
        buf[0] = (char)(0xE0 | (ch >> 12));
        buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (ch & 0x3F));
        buf[3] = '\0';
    }
}

/*======================================
 *  初始化 / Initialization
 *======================================*/

/**
 * app_pinyin_init - 初始化拼音输入法 UI / Initialize pinyin IME UI
 * @parent: 父对象 / Parent object
 * return: 成功返回 0，失败返回负数 / Returns 0 on success, negative on failure
 */
int app_pinyin_init(lv_obj_t *parent)
{
    int i;

    if (parent == NULL) {
        syslog(LOG_ERR, "[PINYIN] Parent is NULL\n");
        return -EINVAL;
    }

    /* 初始化底层引擎 / Initialize underlying engine */
    pinyin_ime_init();

    /* 创建主面板 / Create main panel（宽度=容器内容宽——Win95 形态
     * 挂屏幕底部为全宽输入条；嵌窗口时适配窗口宽） */
    g_pinyin_ui.panel = lv_obj_create(parent);
    lv_coord_t panel_w = (lv_coord_t)LV_HOR_RES;
    lv_coord_t pw = lv_obj_get_width(parent);
    if (pw > 60 && pw < panel_w)
        panel_w = pw - 8;
    lv_obj_set_size(g_pinyin_ui.panel, panel_w, PINYIN_PANEL_H);
    lv_obj_set_style_bg_color(g_pinyin_ui.panel, PINYIN_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_pinyin_ui.panel, PINYIN_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_pinyin_ui.panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_pinyin_ui.panel, 4, LV_PART_MAIN);

    /* 拼音显示标签 / Pinyin display label */
    g_pinyin_ui.pinyin_label = lv_label_create(g_pinyin_ui.panel);
    lv_label_set_text(g_pinyin_ui.pinyin_label, "");
    lv_obj_set_pos(g_pinyin_ui.pinyin_label, 8, 6);
    lv_obj_set_style_text_color(g_pinyin_ui.pinyin_label, PINYIN_TEXT, LV_PART_MAIN);

    /* 候选词容器（水平排布）/ Candidate container */
    g_pinyin_ui.cand_container = lv_obj_create(g_pinyin_ui.panel);
    /* 2026-10-05 修正：容器高=按钮高时 LVGL 默认 padding 把按钮挤出
     * 可视区（label 被裁成空框）——容器加高 + 归零 padding */
    lv_obj_set_size(g_pinyin_ui.cand_container,
                    panel_w - MODE_BTN_W - 30, CAND_BTN_H + 6);
    lv_obj_set_pos(g_pinyin_ui.cand_container, 8, 20);
    lv_obj_set_style_pad_all(g_pinyin_ui.cand_container, 2, 0);
    lv_obj_set_style_bg_color(g_pinyin_ui.cand_container, PINYIN_CAND_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_pinyin_ui.cand_container, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_pinyin_ui.cand_container, 4, LV_PART_MAIN);
    lv_obj_remove_flag(g_pinyin_ui.cand_container, LV_OBJ_FLAG_CLICKABLE);

    /* 候选词按钮 / Candidate buttons */
    for (i = 0; i < CAND_BTN_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(g_pinyin_ui.cand_container);
        lv_obj_set_size(btn, CAND_BTN_W, CAND_BTN_H);
        lv_obj_set_pos(btn, i * (CAND_BTN_W + 2), 1);
        lv_obj_set_style_bg_color(btn, PINYIN_CAND_BG, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, pinyin_ime_btn_event_cb, LV_EVENT_CLICKED, NULL);
        g_pinyin_ui.cand_btns[i] = btn;

        /* 按钮上的标签 / Label on button（12px 中文点阵——数字+汉字） */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, PINYIN_TEXT, LV_PART_MAIN);
        lv_obj_center(lbl);

        /* 初始不可点击 / Initially not clickable */
        lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }

    /* 中英文切换按钮 / Mode switch button */
    g_pinyin_ui.mode_btn = lv_button_create(g_pinyin_ui.panel);
    lv_obj_set_size(g_pinyin_ui.mode_btn, MODE_BTN_W, CAND_BTN_H);
    lv_obj_set_pos(g_pinyin_ui.mode_btn,
                   panel_w - MODE_BTN_W - 8, 22);
    lv_obj_set_style_bg_color(g_pinyin_ui.mode_btn, PINYIN_CAND_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_pinyin_ui.mode_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(g_pinyin_ui.mode_btn, 2, LV_PART_MAIN);
    lv_obj_add_event_cb(g_pinyin_ui.mode_btn, pinyin_ime_mode_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *mode_lbl = lv_label_create(g_pinyin_ui.mode_btn);
    lv_label_set_text(mode_lbl, "中");
    lv_obj_set_style_text_font(mode_lbl, RETRO_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(mode_lbl, lv_color_hex(0x0000FF), LV_PART_MAIN);
    lv_obj_center(mode_lbl);

    /* 状态栏 / Status bar */
    g_pinyin_ui.status_bar = lv_label_create(g_pinyin_ui.panel);
    lv_label_set_text(g_pinyin_ui.status_bar, i18n_get("PINYIN_INPUT_HINT"));
    lv_obj_set_pos(g_pinyin_ui.status_bar, 8, 54);
    lv_obj_set_style_text_font(g_pinyin_ui.status_bar, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(g_pinyin_ui.status_bar,
                                lv_color_hex(0x808080), LV_PART_MAIN);

    /* 初始化状态 / Initialize state */
    g_pinyin_ui.mode = MODE_ZH;
    g_pinyin_ui.has_candidates = 0;
    g_pinyin_ui.pinyin_buf[0] = '\0';
    g_pinyin_ui.target_ta = NULL;
    g_pinyin_ui.commit_cb = NULL;

    syslog(LOG_INFO, "[PINYIN] IME UI initialized\n");
    return 0;
}

/**
 * app_pinyin_set_target - 设置目标输入框 / Set target textarea
 * @ta: 目标 LVGL textarea 对象 / Target LVGL textarea object
 */
void app_pinyin_set_target(lv_obj_t *ta)
{
    g_pinyin_ui.target_ta = ta;
}

/**
 * app_pinyin_set_commit_callback - 设置字符提交回调 / Set character commit callback
 * @cb: 回调函数 / Callback function
 */
void app_pinyin_set_commit_callback(void (*cb)(uint16_t ch))
{
    g_pinyin_ui.commit_cb = cb;
}

/*======================================
 *  事件处理 / Event Handling
 *======================================*/

/**
 * pinyin_ime_btn_event_cb - 候选词按钮事件回调 / Candidate button event callback
 * @e: 事件 / Event
 */
static void pinyin_ime_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint16_t hanzi;
    int i;

    /* 找到是哪个按钮 / Find which button was clicked */
    for (i = 0; i < CAND_BTN_COUNT; i++) {
        if (btn == g_pinyin_ui.cand_btns[i]) {
            /* 选择候选词 / Select candidate */
            if (pinyin_ime_select(i, &hanzi) == 0) {
                pinyin_ime_commit_char(hanzi);
            }

            /* 清空显示 / Clear display */
            lv_label_set_text(g_pinyin_ui.pinyin_label, "");
            g_pinyin_ui.pinyin_buf[0] = '\0';
            pinyin_ime_update_candidates();
            break;
        }
    }
}

/**
 * pinyin_ime_mode_btn_event_cb - 模式切换按钮事件回调 / Mode button event callback
 * @e: 事件 / Event
 */
static void pinyin_ime_mode_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);

    /* 切换模式 / Toggle mode */
    g_pinyin_ui.mode = (g_pinyin_ui.mode == MODE_ZH) ? MODE_EN : MODE_ZH;
    if (lbl != NULL) {
        lv_label_set_text(lbl, g_pinyin_ui.mode == MODE_ZH ? "中" : "英");
    }

    /* 清空拼音缓冲区 / Clear pinyin buffer */
    pinyin_ime_clear();
    lv_label_set_text(g_pinyin_ui.pinyin_label, "");
    g_pinyin_ui.pinyin_buf[0] = '\0';
    pinyin_ime_update_candidates();

    /* 更新状态 / Update status */
    lv_label_set_text(g_pinyin_ui.status_bar,
        g_pinyin_ui.mode == MODE_ZH
            ? i18n_get("PINYIN_CHINESE_MODE")
            : i18n_get("PINYIN_ENGLISH_MODE"));
}

/**
 * pinyin_ime_commit_char - 提交字符 / Commit character
 * @ch: 汉字字符 / Chinese character
 */
static void pinyin_ime_commit_char(uint16_t ch)
{
    char utf8_buf[8];

    pinyin_encode_utf8(ch, utf8_buf, sizeof(utf8_buf));

    /* 提交到目标 textarea / Commit to target textarea */
    if (g_pinyin_ui.target_ta != NULL) {
        lv_textarea_add_text(g_pinyin_ui.target_ta, utf8_buf);
    }

    /* 调用回调 / Call callback */
    if (g_pinyin_ui.commit_cb != NULL) {
        g_pinyin_ui.commit_cb(ch);
    }
}

/**
 * pinyin_ime_update_candidates - 更新候选词显示 / Update candidate display
 */
static void pinyin_ime_update_candidates(void)
{
    int i;
    int count;
    uint16_t candidates[CAND_BTN_COUNT];

    count = pinyin_ime_get_candidates(candidates);
    g_pinyin_ui.has_candidates = count > 0;

    for (i = 0; i < CAND_BTN_COUNT; i++) {
        lv_obj_t *btn = g_pinyin_ui.cand_btns[i];
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);

        if (i < count) {
            /* 显示候选词: 先编码到临时缓冲，再合成序号前缀
             * Show candidate: encode into temp buffer, then compose
             * (旧代码 snprintf(buf+2, ..., "%s", buf) 源目的大量重叠，已修复)
             * (old overlapping snprintf removed) */
            char utf8_buf[8];
            char buf[16];

            pinyin_encode_utf8(candidates[i], utf8_buf, sizeof(utf8_buf));
            snprintf(buf, sizeof(buf), "%d.%s", i + 1, utf8_buf);

            lv_label_set_text(lbl, buf);
            lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        } else {
            /* 隐藏未使用的按钮 / Hide unused buttons */
            lv_label_set_text(lbl, "");
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

/*======================================
 *  外部输入接口 / External Input Interface
 *======================================*/

/**
 * app_pinyin_input - 处理键盘输入 / Handle keyboard input
 * @ch: 输入的字符 / Input character
 * return: 0表示已处理，-1表示未处理 / 0 means handled, -1 means not handled
 *
 * 说明 / Description:
 * - 字母键 (a-z) 用于输入拼音 / Letter keys (a-z) for pinyin input
 * - 数字键 (1-9) 用于选择候选词 / Number keys (1-9) for candidate selection
 * - Backspace 删除最后一个拼音字符 / Backspace deletes last pinyin character
 * - Enter/Esc 取消当前输入 / Enter/Esc cancels current input
 */
int app_pinyin_input(uint32_t ch)
{
    uint16_t hanzi;

    /* 英文模式下，直接传递普通字符 / In English mode, pass through chars */
    if (g_pinyin_ui.mode == MODE_EN) {
        if (g_pinyin_ui.target_ta != NULL && ch >= 0x20 && ch < 0x7F) {
            char ascii[2] = {(char)ch, '\0'};
            lv_textarea_add_text(g_pinyin_ui.target_ta, ascii);
        }
        return 0;
    }

    /* 中文模式 / Chinese mode */
    if (ch >= 'a' && ch <= 'z') {
        /* 拼音输入 / Pinyin input */
        if (pinyin_ime_input((char)ch) > 0) {
            const char *pinyin = pinyin_ime_get_pinyin();
            strncpy(g_pinyin_ui.pinyin_buf, pinyin,
                    sizeof(g_pinyin_ui.pinyin_buf) - 1);
            g_pinyin_ui.pinyin_buf[sizeof(g_pinyin_ui.pinyin_buf) - 1] = '\0';
            lv_label_set_text(g_pinyin_ui.pinyin_label, g_pinyin_ui.pinyin_buf);
            pinyin_ime_update_candidates();
        }
        return 0;

    } else if (ch >= '1' && ch <= '9') {
        /* 候选词选择 / Candidate selection */
        int idx = (int)ch - '1';
        if (idx < pinyin_ime_get_cand_count()) {
            if (pinyin_ime_select(idx, &hanzi) == 0) {
                pinyin_ime_commit_char(hanzi);
            }
            lv_label_set_text(g_pinyin_ui.pinyin_label, "");
            g_pinyin_ui.pinyin_buf[0] = '\0';
            pinyin_ime_update_candidates();
        }
        return 0;

    } else if (ch == '\b') {
        /* Backspace: 有拼音先删拼音 / Delete last pinyin letter first */
        if (g_pinyin_ui.pinyin_buf[0] != '\0') {
            pinyin_ime_backspace();
            const char *pinyin = pinyin_ime_get_pinyin();
            strncpy(g_pinyin_ui.pinyin_buf, pinyin,
                    sizeof(g_pinyin_ui.pinyin_buf) - 1);
            g_pinyin_ui.pinyin_buf[sizeof(g_pinyin_ui.pinyin_buf) - 1] = '\0';
            lv_label_set_text(g_pinyin_ui.pinyin_label, g_pinyin_ui.pinyin_buf);
            pinyin_ime_update_candidates();
        }
        return 0;

    } else if (ch == '\r' || ch == '\n' || ch == 0x1B) {
        /* Enter/Esc - 取消输入 / Cancel input */
        pinyin_ime_clear();
        lv_label_set_text(g_pinyin_ui.pinyin_label, "");
        g_pinyin_ui.pinyin_buf[0] = '\0';
        pinyin_ime_update_candidates();
        return 0;
    }

    return -1;  /* 未处理的按键 / Unhandled key */
}

/**
 * app_pinyin_delete_char - 删除最后一个字符 / Delete last character
 * @return: 0成功 / 0 success
 */
int app_pinyin_delete_char(void)
{
    if (g_pinyin_ui.target_ta != NULL) {
        lv_textarea_delete_char(g_pinyin_ui.target_ta);
    }
    return 0;
}

/**
 * app_pinyin_clear - 清空输入 / Clear input
 */
void app_pinyin_clear(void)
{
    pinyin_ime_clear();
    lv_label_set_text(g_pinyin_ui.pinyin_label, "");
    g_pinyin_ui.pinyin_buf[0] = '\0';
    pinyin_ime_update_candidates();
}

/**
 * app_pinyin_set_mode - 设置输入模式 / Set input mode
 * @mode: 输入模式 / Input mode (0=EN, 1=ZH)
 */
void app_pinyin_set_mode(int mode)
{
    lv_obj_t *lbl;

    g_pinyin_ui.mode = (mode == MODE_EN) ? MODE_EN : MODE_ZH;
    lbl = lv_obj_get_child(g_pinyin_ui.mode_btn, 0);
    if (lbl != NULL) {
        lv_label_set_text(lbl, g_pinyin_ui.mode == MODE_ZH ? "中" : "英");
    }

    /* 清空拼音 / Clear pinyin */
    app_pinyin_clear();
}

/**
 * app_pinyin_get_mode - 获取当前输入模式 / Get current input mode
 * return: 当前模式 / Current mode
 */
int app_pinyin_get_mode(void)
{
    return g_pinyin_ui.mode;
}

/**
 * app_pinyin_toggle_mode - 切换中英文模式 / Toggle between Chinese and English mode
 */
void app_pinyin_toggle_mode(void)
{
    app_pinyin_set_mode(g_pinyin_ui.mode == MODE_EN ? MODE_ZH : MODE_EN);
}

/**
 * app_pinyin_show - 显示输入法面板 / Show IME panel
 */
void app_pinyin_show(void)
{
    if (g_pinyin_ui.panel != NULL) {
        lv_obj_remove_flag(g_pinyin_ui.panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * app_pinyin_hide - 隐藏输入法面板 / Hide IME panel
 */
void app_pinyin_hide(void)
{
    if (g_pinyin_ui.panel != NULL) {
        lv_obj_add_flag(g_pinyin_ui.panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * app_pinyin_set_pos - 设置位置 / Set position
 * @x: X 坐标 / X coordinate
 * @y: Y 坐标 / Y coordinate
 */
void app_pinyin_set_pos(int x, int y)
{
    if (g_pinyin_ui.panel != NULL) {
        lv_obj_set_pos(g_pinyin_ui.panel, (lv_coord_t)x, (lv_coord_t)y);
    }
}

/**
 * app_pinyin_get_panel - 获取主面板对象 / Get main panel object
 * return: LVGL 面板对象 / LVGL panel object
 */
lv_obj_t *app_pinyin_get_panel(void)
{
    return g_pinyin_ui.panel;
}

#endif /* CONFIG_LVGL */
