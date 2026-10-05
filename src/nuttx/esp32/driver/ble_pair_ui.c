/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ble_pair_ui.c - BLE 配对 UI
 *
 * WHAT : BLE 配对 UI
 * WHY  : LVGL 图形化配对/设备管理
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/driver/ble_pair_ui.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 扫描列表 + 确认对话框
 */

/**
 * ble_pair_ui.c - BLE 配对 LVGL 图形界面
 *
 * 提供图形界面的方式来管理 BLE 配对:
 * - 显示连接状态
 * - 列出已配对设备
 * - 配对新设备
 * - 删除配对设备
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

/*
 * 编译开关说明（2026-10-04）/ Build-switch note:
 * 本文件依赖的 ble_hid 实现基于 ESP-IDF/Bluedroid，NuttX 构建下无法
 * 编译；NuttX 路线的 BLE 移植将基于 NimBLE，见 NEXT_STEPS。
 * CONFIG_RETRO_BLE_STACK_IDF 是为未来 IDF 构建保留的符号，当前任何
 * 配置都不定义它——即本文件整体编译关闭。
 * Depends on the IDF-based ble_hid (not buildable under NuttX); the
 * NuttX-line BLE port will be NimBLE-based, see NEXT_STEPS.
 * CONFIG_RETRO_BLE_STACK_IDF is defined nowhere today.
 *
 * 额外注意（重新启用时必须重写）/ Must-rewrite note for re-enabling:
 * 文件内的 lv_obj_add_event_cb(..., [](lv_event_t*){...}) 使用了
 * C++ lambda，这在 C 编译器下非法（当前位于关闭的 #ifdef 分支内
 * 未被解析）。重新启用前须改写为静态命名函数 + user_data 传参。
 * The lambda callbacks [](lv_event_t*){...} are invalid C (they only
 * survive because this branch is compiled out); rewrite them as named
 * static functions with user_data before re-enabling.
 */
#if defined(CONFIG_RETRO_INPUT_BLE_HID) && defined(CONFIG_RETRO_BLE_STACK_IDF)

#include <lvgl/lvgl.h>
#include "ble_hid.h"

/*==========================
 *  静态变量
 *==========================*/

static lv_obj_t *g_ble_window = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_device_list = NULL;
static lv_obj_t *g_scan_btn = NULL;
static lv_obj_t *g_refresh_btn = NULL;

static bool g_ui_initialized = false;

/*==========================
 *  UI 组件
 *==========================*/

/**
 * 创建 BLE 设置窗口
 */
static lv_obj_t *ble_ui_create_window(void)
{
    lv_obj_t *win;
    lv_obj_t *title;
    lv_obj_t *header;
    lv_obj_t *close_btn;

    /* 主窗口 */
    win = lv_win_create(lv_screen_active(), 50);
    lv_win_set_title(win, "蓝牙设置");
    lv_win_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_ON);

    /* 关闭按钮 */
    close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    /* TODO(C++ lambda 非法 C，重写为静态命名函数 /
     * invalid C lambda, rewrite as a named static callback): */
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        lv_obj_delete(g_ble_window);
        g_ble_window = NULL;
        g_ui_initialized = false;
    }, LV_EVENT_CLICKED, NULL);

    /* 状态区域 */
    header = lv_win_get_content(win);

    /* 状态标签 */
    g_status_label = lv_label_create(header);
    lv_label_set_text(g_status_label, "状态: 初始化中...");
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 10, 10);

    /* 设备列表容器 */
    g_device_list = lv_list_create(header);
    lv_obj_set_size(g_device_list, LV_PCT(100), 200);
    lv_obj_align(g_device_list, LV_ALIGN_TOP_LEFT, 0, 40);

    /* 按钮容器 */
    lv_obj_t *btn_container = lv_obj_create(header);
    lv_obj_set_size(btn_container, LV_PCT(100), 60);
    lv_obj_align_to(btn_container, g_device_list, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACEAround, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 扫描按钮 */
    g_scan_btn = lv_btn_create(btn_container);
    lv_obj_t *scan_label = lv_label_create(g_scan_btn);
    lv_label_set_text(scan_label, "扫描设备");
    lv_obj_add_event_cb(g_scan_btn, [](lv_event_t *e) {
        (void)e;
        lv_label_set_text(g_status_label, "状态: 扫描中...");
        ble_hid_start_scan();
    }, LV_EVENT_CLICKED, NULL);

    /* 刷新按钮 */
    g_refresh_btn = lv_btn_create(btn_container);
    lv_obj_t *refresh_label = lv_label_create(g_refresh_btn);
    lv_label_set_text(refresh_label, "刷新");
    lv_obj_add_event_cb(g_refresh_btn, [](lv_event_t *e) {
        (void)e;
        ble_ui_refresh();
    }, LV_EVENT_CLICKED, NULL);

    return win;
}

/**
 * 刷新 UI 内容
 */
void ble_ui_refresh(void)
{
    char status_text[128];
    char device_name[64];
    int i;

    if (g_ble_window == NULL) {
        return;
    }

    /* 更新状态 */
    const char *state_str = ble_hid_get_state_str();
    snprintf(status_text, sizeof(status_text), "状态: %s", state_str);
    lv_label_set_text(g_status_label, status_text);

    /* 清空设备列表 */
    lv_obj_clean(g_device_list);

    /* 添加已配对设备 */
    int count = ble_hid_get_paired_count();
    struct ble_bond_s *bond;

    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        bond = ble_hid_get_paired_device(i);
        if (bond != NULL) {
            lv_obj_t *btn = lv_list_add_btn(g_device_list, LV_SYMBOL_BLUETOOTH, bond->name);
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);

            /* 添加点击事件 */
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                int index = (int)(intptr_t)lv_obj_get_user_data(e->target);
                char buf[64];
                snprintf(buf, sizeof(buf), "确认删除设备 %d?", index);

                /* 创建确认对话框 */
                lv_obj_t *mbox = lv_msgbox_create(NULL);
                lv_msgbox_set_text(mbox, buf);
                lv_msgbox_add_footer_button(mbox, "取消");
                lv_msgbox_add_footer_button(mbox, "确定");

                /* 设置确定按钮回调 */
                lv_obj_t **btns = (lv_obj_t **)lv_msgbox_get_footer(mbox);
                lv_obj_add_event_cb(btns[1], [](lv_event_t *ev) {
                    (void)ev;
                    lv_msgbox_close(lv_obj_get_parent(lv_obj_get_parent(ev->target)));
                }, LV_EVENT_CLICKED, (void *)(intptr_t)index);

                lv_obj_center(mbox);
            }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }

    if (count == 0) {
        lv_obj_t *empty = lv_list_add_btn(g_device_list, LV_SYMBOL_WARNING, "无已配对设备");
        lv_obj_set_style_text_color(empty, lv_color_gray(), 0);
        lv_obj_add_flag(empty, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_CLICKABLE);
    }
}

/**
 * 更新扫描状态
 */
void ble_ui_update_scan_status(const char *device_name, int rssi)
{
    char buf[128];

    if (g_ble_window == NULL) {
        return;
    }

    snprintf(buf, sizeof(buf), "发现: %s (RSSI: %d)", device_name, rssi);
    lv_label_set_text(g_status_label, buf);

    /* 添加到列表作为"发现"而非"已配对" */
    lv_obj_clean(g_device_list);

    lv_obj_t *btn = lv_list_add_btn(g_device_list, LV_SYMBOL_BLUETOOTH, device_name);
    lv_obj_set_style_text_color(btn, lv_color_make(0, 100, 255), 0);

    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_label_set_text(g_status_label, "状态: 正在连接...");
        /* TODO: 连接选中的设备 */
    }, LV_EVENT_CLICKED, NULL);
}

/**
 * 显示 BLE 设置窗口
 */
void ble_ui_show(void)
{
    if (g_ble_window != NULL) {
        /* 窗口已存在，聚焦 */
        lv_obj_move_foreground(g_ble_window);
        ble_ui_refresh();
        return;
    }

    g_ble_window = ble_ui_create_window();
    g_ui_initialized = true;
    ble_ui_refresh();
}

/**
 * 关闭 BLE 设置窗口
 */
void ble_ui_close(void)
{
    if (g_ble_window != NULL) {
        lv_obj_delete(g_ble_window);
        g_ble_window = NULL;
        g_ui_initialized = false;
    }
}

/**
 * 检查窗口是否已打开
 */
bool ble_ui_is_open(void)
{
    return g_ble_window != NULL;
}

/*==========================
 *  初始化
 *==========================*/

/**
 * 初始化 BLE UI 模块
 */
int ble_ui_init(void)
{
    if (g_ui_initialized) {
        return 0;
    }

    syslog(LOG_INFO, "[BLE-UI] Initializing BLE pair UI\n");
    g_ble_window = NULL;
    g_ui_initialized = true;

    return 0;
}

#endif /* CONFIG_RETRO_INPUT_BLE_HID && CONFIG_RETRO_BLE_STACK_IDF */
