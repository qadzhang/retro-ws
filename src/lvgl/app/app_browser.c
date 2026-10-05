/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * app_browser.c - Win3.2 简易浏览器
 *
 * WHAT : Win3.2 简易浏览器
 * WHY  : 复古网页浏览（URL 栏/前进后退/书签/纯文本 HTML 渲染）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/app/app_browser.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : network_utils.c 拉取 HTML，简易解析为文本+链接，LVGL label 渲染
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef CONFIG_LVGL

#include "lvgl/lvgl.h"
#include "retro_font.h"

/*======================================
 *  Windows 3.2 颜色
 *======================================*/
#define WIN3_BG        lv_color_hex(0xC0C0C0)
#define WIN3_WHITE     lv_color_hex(0xFFFFFF)
#define WIN3_BLACK     lv_color_hex(0x000000)
#define WIN3_BLUE      lv_color_hex(0x000080)
#define WIN3_GRAY      lv_color_hex(0x808080)
#define WIN3_LTGRAY    lv_color_hex(0xC0C0C0)
#define WIN3_BORDER_HI lv_color_hex(0xFFFFFF)
#define WIN3_BORDER_LO lv_color_hex(0x808080)
#define WIN3_TITLE_BG  lv_color_hex(0x000080)
#define WIN3_TITLE_FG  lv_color_hex(0xFFFFFF)
#define WIN3_STATUS_BG lv_color_hex(0xC0C0C0)
#define WIN3_TOOLBAR_BG lv_color_hex(0xC0C0C0)
#define WIN3_CONTENT_BG lv_color_hex(0xFFFFFF)
#define WIN3_LINK_COLOR lv_color_hex(0x0000FF)
#define WIN3_VISITED   lv_color_hex(0x800080)

#define BORDER_W       2
#define TITLE_H        18
#define TOOLBAR_H      32
#define URLBAR_H       28
#define STATUS_H       22
#define NAV_BTN_SIZE   26

/*======================================
 *  浏览器状态
 *======================================*/
typedef struct {
    lv_obj_t *win;
    lv_obj_t *toolbar;
    lv_obj_t *url_bar;
    lv_obj_t *url_input;
    lv_obj_t *content;
    lv_obj_t *content_label;
    lv_obj_t *status_bar;
    lv_obj_t *status_lbl;
    lv_obj_t *loading_bar;

    /* 导航历史 */
    char current_url[512];
    char history[10][512];
    int history_pos;
    int history_count;

    /* 书签 */
    char bookmarks[5][256];
    int bookmark_count;

    /* 状态 */
    int loading;
    int load_progress;
} browser_t;

static browser_t g_browser;

/*======================================
 *  导航按钮
 *======================================*/

typedef struct {
    const char *label;
    const char *symbol;
    void (*callback)(void);
} nav_btn_t;

static void nav_back(void);
static void nav_forward(void);
static void nav_refresh(void);
static void nav_home(void);
static void nav_bookmarks(void);
static void url_submit(void);
static void load_url(const char *url);
static void parse_html(const char *html);
static void browser_set_title(const char *title);

/*======================================
 *  创建工具栏按钮
 *======================================*/

/* URL 输入框提交回调 */
static void url_input_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        url_submit();
    }
}

/* Go 按钮点击回调 */
static void go_btn_cb(lv_event_t *e)
{
    (void)e;
    url_submit();
}

/* 书签按钮点击回调 */
static void bm_btn_cb(lv_event_t *e)
{
    (void)e;
    nav_bookmarks();
}

/* 导航按钮通用回调 - 通过 user_data 获取实际回调函数并调用 */
static void nav_btn_generic_cb(lv_event_t *e)
{
    void (*cb)(void) = (void(*)(void))lv_obj_get_user_data(lv_event_get_target(e));
    if (cb) cb();
}

static lv_obj_t *create_nav_btn(lv_obj_t *parent, const char *symbol,
                                void (*cb)(void), int x)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, NAV_BTN_SIZE, NAV_BTN_SIZE);
    lv_obj_set_pos(btn, x, 3);
    lv_obj_set_style_bg_color(btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    if (cb) {
        lv_obj_set_user_data(btn, cb);
        lv_obj_add_event_cb(btn, nav_btn_generic_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

/*======================================
 *  导航函数
 *======================================*/

static void nav_back(void)
{
    browser_t *br = &g_browser;
    if (br->history_pos > 0) {
        br->history_pos--;
        load_url(br->history[br->history_pos]);
    }
}

static void nav_forward(void)
{
    browser_t *br = &g_browser;
    if (br->history_pos < br->history_count - 1) {
        br->history_pos++;
        load_url(br->history[br->history_pos]);
    }
}

static void nav_refresh(void)
{
    browser_t *br = &g_browser;
    if (br->current_url[0]) {
        load_url(br->current_url);
    }
}

static void nav_home(void)
{
    load_url("https://example.com/");
}

static void nav_bookmarks(void)
{
    browser_t *br = &g_browser;

    /* 显示书签菜单 */
    lv_obj_t *menu = lv_obj_create(br->win);
    lv_obj_set_size(menu, 200, 200);
    lv_obj_center(menu);
    lv_obj_set_style_bg_color(menu, WIN3_LTGRAY, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(menu);
    lv_label_set_text(title, "Bookmarks");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    for (int i = 0; i < br->bookmark_count; i++) {
        lv_obj_t *item = lv_button_create(menu);
        lv_obj_set_size(item, 180, 28);
        lv_obj_set_pos(item, 10, 30 + i * 32);
        lv_obj_set_style_bg_color(item, WIN3_LTGRAY, LV_PART_MAIN);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, br->bookmarks[i]);
        lv_obj_set_style_text_font(lbl, RETRO_FONT_DEFAULT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
    }
}

/*======================================
 *  URL 加载
 *======================================*/

static void url_submit(void)
{
    browser_t *br = &g_browser;
    const char *url = lv_textarea_get_text(br->url_input);

    if (strlen(url) == 0) return;

    /* 自动添加 http:// 如果没有协议 */
    static char full_url[512];
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        snprintf(full_url, sizeof(full_url), "http://%s", url);
    } else {
        strncpy(full_url, url, sizeof(full_url) - 1);
    }

    load_url(full_url);
}

static void add_to_history(const char *url)
{
    browser_t *br = &g_browser;

    /* 如果不是在最新位置，清除后面的历史 */
    if (br->history_pos < br->history_count - 1) {
        br->history_count = br->history_pos + 1;
    }

    /* 添加到历史 */
    if (br->history_count < 10) {
        br->history_count++;
    }

    strncpy(br->history[br->history_pos], url, 511);
    br->history[br->history_pos][511] = 0;
}

static void load_url(const char *url)
{
    browser_t *br = &g_browser;

    strncpy(br->current_url, url, sizeof(br->current_url) - 1);
    br->current_url[sizeof(br->current_url) - 1] = 0;

    /* 更新 URL 显示 */
    lv_textarea_set_text(br->url_input, url);

    /* 添加到历史 */
    add_to_history(url);

    /* 显示加载状态 */
    br->loading = 1;
    br->load_progress = 0;
    lv_label_set_text(br->status_lbl, "Connecting...");
    lv_obj_remove_flag(br->loading_bar, LV_OBJ_FLAG_HIDDEN);

    syslog(LOG_INFO, "Browser: loading %s\n", url);

    /* 模拟加载过程 - 实际会调用 curl/wget */
    /* TODO: 使用 netutils_curl 实现真实 HTTP 请求 */

    /* 演示内容 */
    static const char *demo_content =
        "====================================\n"
        "  ESP32-S3 Mini Browser\n"
        "====================================\n\n"
        "URL: https://example.com/\n\n"
        "Server: Apache\n"
        "Content-Type: text/html\n\n"
        "---------- Content ----------\n\n"
        "<html>\n"
        "<head><title>Example Domain</title></head>\n"
        "<body>\n"
        "<h1>Example Domain</h1>\n"
        "<p>This domain is for use in illustrative examples in documents.\n"
        "You may use this domain in literature without prior coordination\n"
        "or asking for formality.</p>\n"
        "<p><a href=\"https://www.iana.org/domains/example\">\n"
        "More information...</a></p>\n"
        "</body>\n"
        "</html>\n\n"
        "---------- End ----------\n\n"
        "Note: Full HTTP browsing requires network\n"
        "configuration and a web server.";

    /* 模拟加载完成 */
    lv_label_set_text(br->content_label, demo_content);
    lv_label_set_text(br->status_lbl, "Done");
    lv_obj_add_flag(br->loading_bar, LV_OBJ_FLAG_HIDDEN);
    br->loading = 0;

    browser_set_title("Example Domain - ESP32 Browser");
}

static void parse_html(const char *html)
{
    /* 简化的 HTML 解析 - 提取纯文本和链接 */
    browser_t *br = &g_browser;

    static char parsed[8192];
    parsed[0] = 0;

    const char *p = html;
    int in_tag = 0;
    int in_link = 0;
    char link_buf[256];

    while (*p) {
        if (*p == '<') {
            in_tag = 1;
            /* 检查是否是链接标签 */
            if (strncmp(p, "<a ", 3) == 0) {
                in_link = 1;
                link_buf[0] = 0;
            }
            p++;
            continue;
        }

        if (*p == '>') {
            in_tag = 0;
            if (in_link) {
                strcat(parsed, " [");
                strcat(parsed, link_buf);
                strcat(parsed, "]");
                in_link = 0;
            }
            p++;
            continue;
        }

        if (!in_tag) {
            char c[2] = {*p, 0};
            if (*p == '\n' || *p == '\r') {
                strcat(parsed, "\n");
            } else if (*p != ' ' || strlen(parsed) == 0 ||
                      parsed[strlen(parsed)-1] != ' ') {
                strcat(parsed, c);
            }
        } else if (in_link) {
            /* 在链接标签内，提取 href */
            if (strncmp(p, "href=\"", 6) == 0) {
                p += 6;
                char *lp = link_buf;
                while (*p && *p != '"' && lp < link_buf + 250) {
                    *lp++ = *p++;
                }
                *lp = 0;
            }
        }

        p++;
    }

    lv_label_set_text(br->content_label, parsed);
}

/*======================================
 *  标题更新
 *======================================*/

static void browser_set_title(const char *title)
{
    browser_t *br = &g_browser;
    /* TODO: 更新窗口标题栏 */
    (void)br;
}

/*======================================
 *  创建浏览器窗口
 *======================================*/

static void create_browser_window(void)
{
    browser_t *br = &g_browser;
    memset(br, 0, sizeof(*br));

    int win_w = 600;
    int win_h = 480;

    /* 窗口 */
    br->win = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(br->win, 50, 30);
    lv_obj_set_size(br->win, win_w, win_h);
    lv_obj_set_style_bg_color(br->win, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_set_style_border_width(br->win, 0, LV_PART_MAIN);

    /* 标题栏 */
    lv_obj_t *title_bar = lv_obj_create(br->win);
    lv_obj_set_pos(title_bar, BORDER_W, BORDER_W);
    lv_obj_set_size(title_bar, win_w - BORDER_W * 2, TITLE_H);
    lv_obj_set_style_bg_color(title_bar, WIN3_TITLE_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "ESP32 Browser");
    lv_obj_set_style_text_color(title_lbl, WIN3_TITLE_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 控制按钮 */
    lv_obj_t *close_btn = lv_button_create(title_bar);
    lv_obj_set_size(close_btn, 16, 14);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(close_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "x");
    lv_obj_align(cl, LV_ALIGN_CENTER, 0, 0);

    /* 工具栏 */
    br->toolbar = lv_obj_create(br->win);
    lv_obj_set_pos(br->toolbar, BORDER_W, BORDER_W + TITLE_H);
    lv_obj_set_size(br->toolbar, win_w - BORDER_W * 2, TOOLBAR_H);
    lv_obj_set_style_bg_color(br->toolbar, WIN3_TOOLBAR_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(br->toolbar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(br->toolbar, WIN3_BORDER_HI, LV_PART_MAIN);

    /* 后退 */
    create_nav_btn(br->toolbar, "◄", nav_back, 4);
    /* 前进 */
    create_nav_btn(br->toolbar, "►", nav_forward, 34);
    /* 刷新 */
    create_nav_btn(br->toolbar, "⟳", nav_refresh, 64);
    /* 主页 */
    create_nav_btn(br->toolbar, "⌂", nav_home, 94);

    /* 分隔线 */
    lv_obj_t *sep1 = lv_obj_create(br->toolbar);
    lv_obj_set_size(sep1, 1, TOOLBAR_H - 6);
    lv_obj_set_pos(sep1, 128, 3);
    lv_obj_set_style_bg_color(sep1, WIN3_GRAY, LV_PART_MAIN);

    /* URL 栏 */
    br->url_input = lv_textarea_create(br->toolbar);
    lv_obj_set_size(br->url_input, win_w - BORDER_W * 2 - 200, URLBAR_H - 2);
    lv_obj_set_pos(br->url_input, 136, 3);
    lv_textarea_set_placeholder_text(br->url_input, "Enter URL...");
    lv_textarea_set_one_line(br->url_input, true);
    lv_obj_set_style_text_font(br->url_input, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_style_border_width(br->url_input, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(br->url_input, WIN3_BORDER_LO, LV_PART_MAIN);

    /* URL 输入框回调 / URL input callback
     * 9.5 无 LV_EVENT_SUBMITTED，回车以 LV_EVENT_READY 上报
     * LVGL 9.5 has no LV_EVENT_SUBMITTED; Enter reports LV_EVENT_READY */
    lv_obj_add_event_cb(br->url_input, url_input_cb, LV_EVENT_READY, NULL);

    /* GO 按钮 */
    lv_obj_t *go_btn = lv_button_create(br->toolbar);
    lv_obj_set_size(go_btn, 44, URLBAR_H - 2);
    lv_obj_set_pos(go_btn, win_w - BORDER_W * 2 - 58, 3);
    lv_obj_set_style_bg_color(go_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_t *go_lbl = lv_label_create(go_btn);
    lv_label_set_text(go_lbl, "Go");
    lv_obj_align(go_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(go_btn, go_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 书签按钮 */
    lv_obj_t *bm_btn = lv_button_create(br->toolbar);
    lv_obj_set_size(bm_btn, NAV_BTN_SIZE, NAV_BTN_SIZE);
    lv_obj_set_pos(bm_btn, win_w - BORDER_W * 2 - 100, 3);
    lv_obj_set_style_bg_color(bm_btn, WIN3_LTGRAY, LV_PART_MAIN);
    lv_obj_t *bm_lbl = lv_label_create(bm_btn);
    lv_label_set_text(bm_lbl, "★");
    lv_obj_align(bm_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(bm_btn, bm_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 内容区域 */
    int content_y = BORDER_W + TITLE_H + TOOLBAR_H + 4;
    int content_h = win_h - BORDER_W * 2 - TITLE_H - TOOLBAR_H - STATUS_H - 6;

    br->content = lv_obj_create(br->win);
    lv_obj_set_pos(br->content, BORDER_W, content_y);
    lv_obj_set_size(br->content, win_w - BORDER_W * 2, content_h);
    lv_obj_set_style_bg_color(br->content, WIN3_CONTENT_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(br->content, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(br->content, WIN3_BORDER_LO, LV_PART_MAIN);

    br->content_label = lv_label_create(br->content);
    lv_label_set_text(br->content_label,
        "====================================\n"
        "  ESP32-S3 Mini Browser\n"
        "====================================\n\n"
        "Welcome! Enter a URL above and click Go.\n\n"
        "Or try one of these:\n"
        "  - example.com\n"
        "  - google.com\n"
        "  - github.com\n\n"
        "Note: Full HTTP browsing requires\n"
        "network configuration.\n\n"
        "Press Go to continue...");
    lv_obj_set_style_text_font(br->content_label, RETRO_FONT_DEFAULT, 0);
    lv_obj_set_pos(br->content_label, 8, 8);
    lv_obj_set_size(br->content_label, win_w - BORDER_W * 2 - 16, content_h - 16);

    /* 加载条 */
    br->loading_bar = lv_bar_create(br->content);
    lv_obj_set_size(br->loading_bar, win_w - BORDER_W * 2 - 20, 8);
    lv_obj_set_pos(br->loading_bar, 8, content_h / 2 - 4);
    lv_obj_add_flag(br->loading_bar, LV_OBJ_FLAG_HIDDEN);

    /* 状态栏 */
    br->status_bar = lv_obj_create(br->win);
    int status_y = win_h - BORDER_W - STATUS_H;
    lv_obj_set_pos(br->status_bar, BORDER_W, status_y);
    lv_obj_set_size(br->status_bar, win_w - BORDER_W * 2, STATUS_H);
    lv_obj_set_style_bg_color(br->status_bar, WIN3_STATUS_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(br->status_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(br->status_bar, WIN3_BORDER_LO, LV_PART_MAIN);

    br->status_lbl = lv_label_create(br->status_bar);
    lv_label_set_text(br->status_lbl, "Ready");
    lv_obj_set_style_text_font(br->status_lbl, RETRO_FONT_DEFAULT, 0);
    lv_obj_align(br->status_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    /* 初始化书签 */
    strcpy(br->bookmarks[0], "example.com");
    strcpy(br->bookmarks[1], "google.com");
    strcpy(br->bookmarks[2], "github.com");
    br->bookmark_count = 3;

    syslog(LOG_INFO, "Browser: window created\n");
}

/*======================================
 *  公共接口
 *======================================*/

int browser_start(void)
{
    create_browser_window();
    syslog(LOG_INFO, "Browser: started\n");
    return 0;
}

void browser_stop(void)
{
    browser_t *br = &g_browser;
    if (br->win) {
        lv_obj_delete(br->win);
        memset(br, 0, sizeof(*br));
    }
    syslog(LOG_INFO, "Browser: stopped\n");
}

lv_obj_t *browser_create(void)
{
    if (!g_browser.win) {
        browser_start();
    }
    return g_browser.win;
}

#endif /* CONFIG_LVGL */
