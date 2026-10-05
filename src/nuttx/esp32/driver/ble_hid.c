/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ble_hid.c - BLE HID 驱动（CAM）
 *
 * WHAT : BLE HID 驱动（CAM）
 * WHY  : 蓝牙键鼠（NimBLE），GPIO4 闪光灯状态指示
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/driver/ble_hid.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : GAP 扫描连接 + HID 报告上报 + Bond 自动重连
 */

/**
 * ble_hid.c - BLE HID 键盘/鼠标输入驱动
 *
 * ESP32-CAM 没有 USB OTG，使用 BLE HID 连接蓝牙键盘和鼠标
 * 基于 ESP-IDF NimBLE 协议栈
 *
 * 流程：
 * 1. BLE 作为 Central 扫描附近的 HID 设备
 * 2. 发现 HID GATT Service (UUID 0x1812)
 * 3. 连接并配对 (JustWorks 自动确认)
 * 4. 订阅 HID Report 特征值
 * 5. 解析 HID 报告转换为 LVGL 输入事件
 *
 * GPIO 使用：
 * - 状态 LED: BT_LED_GPIO (GPIO4, Flash LED)
 * - RTC I2C: GPIO21 (SDA) / GPIO22 (SCL)
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>
#include <errno.h>
#include <sched.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

#include "board.h"
#include "ble_hid.h"

/*
 * 编译开关说明（2026-10-04）/ Build-switch note:
 * 本文件主体依赖 ESP-IDF/Bluedroid 与 FreeRTOS 头（esp_bt_main.h、
 * esp_bt_hid_host_api.h、esp_bt_hid_defs.h、driver/gpio.h、hal/gpio_hal.h、
 * vTaskDelay 等），NuttX 构建下这些头不存在，无法编译；其中还引用了
 * 若干虚构类型（esp_ble_ble_id_t、param->ble_security.key.p_key_key）。
 * NuttX 路线的 BLE 移植将基于 NimBLE，见 NEXT_STEPS。
 * CONFIG_RETRO_BLE_STACK_IDF 是为未来 IDF 构建保留的符号，当前任何
 * 配置都不定义它——即本文件整体编译关闭（保持可语法检查）。
 * The body uses ESP-IDF/Bluedroid/FreeRTOS headers that do not exist
 * under NuttX (plus a few fabricated types). The NuttX-line BLE port
 * will be NimBLE-based, see NEXT_STEPS. CONFIG_RETRO_BLE_STACK_IDF is
 * reserved for a future IDF-based build and is defined nowhere today,
 * so this file compiles out entirely.
 */
#if defined(CONFIG_RETRO_INPUT_BLE_HID) && defined(CONFIG_RETRO_BLE_STACK_IDF)

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_BLE_DEVICE_NAME
#define CONFIG_RETRO_BLE_DEVICE_NAME    "ESP32-CAM"
#endif

#ifndef CONFIG_RETRO_BLE_AUTO_CONNECT
#define CONFIG_RETRO_BLE_AUTO_CONNECT    1
#endif

#ifndef CONFIG_RETRO_BLE_SCAN_TIMEOUT
#define CONFIG_RETRO_BLE_SCAN_TIMEOUT    10
#endif

/* LED GPIO 已移至 board.h 中的 BT_LED_GPIO 宏 */

/*==========================
 *  ESP-IDF NimBLE 头文件
 *==========================*/

#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_hid_host_api.h"
#include "esp_bt_hid_defs.h"
#include "esp_hidd_prf.h"
#include "esp_bt_ssp.h"

/*==========================
 *  HID UUID
 *==========================*/

#define HID_SERVICE_UUID         0x1812  /* HID Service */
#define HID_REPORT_UUID          0x2A4D  /* HID Report Characteristic */
#define HID_INFORMATION_UUID     0x2A4A  /* HID Information */
#define HID_CONTROL_POINT_UUID    0x2A4C  /* HID Control Point */

/* HID Report ID */
#define HID_REPORT_ID_KEYBOARD   1
#define HID_REPORT_ID_MOUSE      2

/* HID Device Info */
#define HID_DEV_NAME             CONFIG_RETRO_BLE_DEVICE_NAME
#define HID_MANUFACTURER        "ESP32-CAM"

/*==========================
 *  HID 报告解析
 *==========================*/

/* 标准 USB HID 键盘报告 (8 bytes):
 * [0] Modifier keys (Ctrl/Shift/Alt/GUI)
 * [1] Reserved
 * [2-7] Key codes (max 6 simultaneous)
 */
#define HID_KBD_REPORT_SIZE      8
#define HID_KBD_MAX_KEYS         6

/* Modifier 位掩码 */
#define HID_MOD_LCTRL    (1 << 0)
#define HID_MOD_LSHIFT   (1 << 1)
#define HID_MOD_LALT     (1 << 2)
#define HID_MOD_LGUI     (1 << 3)
#define HID_MOD_RCTRL    (1 << 4)
#define HID_MOD_RSHIFT   (1 << 5)
#define HID_MOD_RALT     (1 << 6)
#define HID_MOD_RGUI     (1 << 7)

/* 标准 USB HID 鼠标报告 (4 bytes):
 * [0] Button bitmap
 * [1] X displacement (signed)
 * [2] Y displacement (signed)
 * [3] Wheel displacement (signed)
 */
#define HID_MOUSE_REPORT_SIZE    4

/*==========================
 *  HID -> LVGL 键码映射
 *==========================*/

struct hid_keymap_s {
    uint8_t hid_key;
    uint32_t lvgl_key;
};

static const struct hid_keymap_s g_hid_keymap[] = {
    /* 字母键 A-Z */
    {0x04, 'a'}, {0x05, 'b'}, {0x06, 'c'}, {0x07, 'd'},
    {0x08, 'e'}, {0x09, 'f'}, {0x0A, 'g'}, {0x0B, 'h'},
    {0x0C, 'i'}, {0x0D, 'j'}, {0x0E, 'k'}, {0x0F, 'l'},
    {0x10, 'm'}, {0x11, 'n'}, {0x12, 'o'}, {0x13, 'p'},
    {0x14, 'q'}, {0x15, 'r'}, {0x16, 's'}, {0x17, 't'},
    {0x18, 'u'}, {0x19, 'v'}, {0x1A, 'w'}, {0x1B, 'x'},
    {0x1C, 'y'}, {0x1D, 'z'},
    /* 数字键 1-9, 0 */
    {0x1E, '1'}, {0x1F, '2'}, {0x20, '3'}, {0x21, '4'},
    {0x22, '5'}, {0x23, '6'}, {0x24, '7'}, {0x25, '8'},
    {0x26, '9'}, {0x27, '0'},
    /* 特殊键 */
    {0x28, '\n'},  /* Enter */
    {0x29, 0x1B},  /* Escape */
    {0x2A, 0x08},  /* Backspace */
    {0x2B, '\t'},  /* Tab */
    {0x2C, ' '},   /* Space */
    /* 方向键（HID 0x4F-0x52 = Right/Left/Down/Up）
     * TODO(重新启用时): 原实现在此映射了 0x1B/0x1A/0x19/0x18 等
     * 伪 LVGL 键码（既非 ASCII 也非 LV_KEY_*）。重写时应包含
     * <lvgl/lvgl.h> 并映射到 LV_KEY_RIGHT/LEFT/DOWN/UP。
     * TODO(re-enable): map 0x4F..0x52 to LV_KEY_RIGHT/LEFT/DOWN/UP
     * (include lvgl.h) instead of the fabricated codes removed here. */
    /* 功能键 F1-F12 */
    {0x3A, 0xF1}, {0x3B, 0xF2}, {0x3C, 0xF3}, {0x3D, 0xF4},
    {0x3E, 0xF5}, {0x3F, 0xF6}, {0x40, 0xF7}, {0x41, 0xF8},
    {0x42, 0xF9}, {0x43, 0xFA}, {0x44, 0xFB}, {0x45, 0xFC},
};

#define HID_KEYMAP_SIZE (sizeof(g_hid_keymap) / sizeof(g_hid_keymap[0]))

/*==========================
 *  内部状态
 *==========================*/

static bool g_ble_initialized = false;
static enum ble_conn_state_e g_conn_state = BLE_STATE_IDLE;
static enum ble_led_state_e g_led_state = BLE_LED_OFF;

static bool g_kbd_connected = false;
static bool g_mouse_connected = false;
static bool g_scan_active = false;
static uint32_t g_scan_start_time = 0;

/* Bond 信息 (当前连接设备) */
static struct ble_bond_s g_current_device;
static bool g_current_device_valid = false;

/* 上一次键盘报告 (用于去重) */
static uint8_t g_prev_kbd_report[HID_KBD_REPORT_SIZE];

/* 信号量 - 连接完成通知 */
static sem_t g_conn_sem;
static bool g_sem_initialized = false;

/* 发现的 HID 设备列表 */
#define MAX_DISCOVERED_DEVICES 5
static struct {
    uint8_t addr_type;
    uint8_t addr[6];
    char name[32];
    int rssi;
    int count;
} g_discovered_devices[MAX_DISCOVERED_DEVICES];
static int g_discovered_count = 0;

/* GATT 客户端 */
static uint16_t g_gattc_if = 0;
static uint16_t g_conn_id = 0;
static bool g_gatt_connected = false;
static esp_bd_addr_t g_connected_addr;

/* HID 特征值句柄 */
static uint16_t g_hid_report_handle = 0;

/* LED GPIO 状态 */
static bool g_led_on = false;
static uint32_t g_led_last_toggle = 0;

/*==========================
 *  GPIO 控制
 *==========================*/

#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#define LED_GPIO_PIN       BT_LED_GPIO  /* 来自 board.h */

/**
 * 初始化 LED GPIO
 */
static void ble_led_init(void)
{
#if LED_GPIO_PIN >= 0
    gpio_num_t gpio_num = (gpio_num_t)LED_GPIO_PIN;

    /* 配置 GPIO 为输出 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* 初始关闭 LED */
    gpio_set_level(gpio_num, 0);
    g_led_on = false;

    syslog(LOG_INFO, "[BLE] LED GPIO %d initialized\n", LED_GPIO_PIN);
#endif
}

/**
 * 设置 LED 状态
 */
void ble_hid_set_led(enum ble_led_state_e state)
{
#if LED_GPIO_PIN >= 0
    g_led_state = state;
#endif
}

/**
 * LED 任务 (在主循环中调用)
 */
static void ble_hid_led_task(void)
{
#if LED_GPIO_PIN < 0
    return;
#else
    gpio_num_t gpio_num = (gpio_num_t)LED_GPIO_PIN;
    uint32_t now = sched_clock() / 1000; /* ms */
    uint32_t interval;

    switch (g_led_state) {
    case BLE_LED_OFF:
        gpio_set_level(gpio_num, 0);
        g_led_on = false;
        break;

    case BLE_LED_ON:
        gpio_set_level(gpio_num, 1);
        g_led_on = true;
        break;

    case BLE_LED_SLOW_FLASH:
        interval = 1000; /* 1s on, 1s off */
        if (now - g_led_last_toggle > interval) {
            g_led_on = !g_led_on;
            gpio_set_level(gpio_num, g_led_on ? 1 : 0);
            g_led_last_toggle = now;
        }
        break;

    case BLE_LED_FAST_FLASH:
        interval = 250; /* 0.25s on, 0.25s off */
        if (now - g_led_last_toggle > interval) {
            g_led_on = !g_led_on;
            gpio_set_level(gpio_num, g_led_on ? 1 : 0);
            g_led_last_toggle = now;
        }
        break;
    }
#endif
}

/*==========================
 *  HID 报告处理
 *==========================*/

/**
 * 将 HID key code 转换为 LVGL key
 */
static uint32_t hid_key_to_lvgl(uint8_t hid_key)
{
    int i;
    for (i = 0; i < (int)HID_KEYMAP_SIZE; i++) {
        if (g_hid_keymap[i].hid_key == hid_key)
            return g_hid_keymap[i].lvgl_key;
    }
    return 0;
}

/**
 * 处理键盘 HID 报告
 */
static void handle_kbd_report(const uint8_t *report, size_t len)
{
    int i, j;

    if (len < HID_KBD_REPORT_SIZE)
        return;

    uint8_t modifiers = report[0];

    /* 检查新按下的键 */
    for (i = 2; i < 2 + HID_KBD_MAX_KEYS; i++) {
        uint8_t key = report[i];
        if (key == 0)
            continue;

        /* 检查是否是新按下的 (上次没有) */
        bool is_new = true;
        for (j = 2; j < 2 + HID_KBD_MAX_KEYS; j++) {
            if (g_prev_kbd_report[j] == key) {
                is_new = false;
                break;
            }
        }

        if (is_new) {
            uint32_t lvgl_key = hid_key_to_lvgl(key);

            /* Shift 修饰键处理 */
            if (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) {
                if (lvgl_key >= 'a' && lvgl_key <= 'z')
                    lvgl_key -= 32;  /* 转大写 */
            }

            if (lvgl_key) {
                /* 发送到 LVGL 输入设备 */
                extern void lvgl_send_key(uint32_t key, bool pressed);
                lvgl_send_key(lvgl_key, true);
                lvgl_send_key(lvgl_key, false);
            }
        }
    }

    /* 保存报告 */
    memcpy(g_prev_kbd_report, report, HID_KBD_REPORT_SIZE);
}

/**
 * 处理鼠标 HID 报告
 */
static void handle_mouse_report(const uint8_t *report, size_t len)
{
    if (len < HID_MOUSE_REPORT_SIZE)
        return;

    uint8_t buttons = report[0];
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];

    /* 发送到 LVGL 输入设备 */
    extern void lvgl_send_mouse(int8_t dx, int8_t dy, uint8_t buttons);
    lvgl_send_mouse(dx, dy, buttons);
}

/*==========================
 *  ESP-IDF GAP 回调
 *==========================*/

/**
 * GAP 扫描结果回调
 */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    /* TODO(重新启用时): 原实现此处声明了 esp_ble_ble_id_t *（Bluedroid
     * 中不存在的类型）且未使用，已删除；重写时按真实 API 补。
     * TODO(re-enable): fabricated esp_ble_ble_id_t decls removed. */
    uint8_t *adv_name;
    uint8_t adv_name_len;
    uint8_t *adv_data;
    uint8_t adv_data_len;
    int i;

    switch (event) {
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;

        if (scan_result->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT) {
            /* 获取设备名称 */
            adv_name = (uint8_t *)esp_ble_resolve_adv_data(
                scan_result->scan_rst.ble_adv,
                ESP_BLE_AD_TYPE_NAME_CMPL,
                &adv_name_len);

            /* 检查是否是 HID 设备 (检查 Service UUID) */
            adv_data = scan_result->scan_rst.ble_adv;
            adv_data_len = scan_result->scan_rst.adv_data_len;

            bool is_hid = false;
            for (i = 0; i < adv_data_len; i++) {
                if (adv_data[i] == 0x03 && i + 2 < adv_data_len) { /* Type=0x03: 16-bit UUID */
                    if (adv_data[i + 1] == (HID_SERVICE_UUID & 0xFF) &&
                        adv_data[i + 2] == ((HID_SERVICE_UUID >> 8) & 0xFF)) {
                        is_hid = true;
                        break;
                    }
                }
            }

            if (is_hid) {
                /* 添加到发现列表 */
                char *name = (adv_name != NULL) ? (char *)adv_name : "BLE HID Device";

                syslog(LOG_INFO, "[BLE] Found HID device: %s (rssi=%d)\n",
                       name, scan_result->scan_rst.rssi);

                /* 检查是否已发现 */
                bool found = false;
                for (i = 0; i < g_discovered_count; i++) {
                    if (memcmp(g_discovered_devices[i].addr,
                              scan_result->scan_rst.bda, 6) == 0) {
                        found = true;
                        g_discovered_devices[i].rssi = scan_result->scan_rst.rssi;
                        break;
                    }
                }

                /* 新设备 */
                if (!found && g_discovered_count < MAX_DISCOVERED_DEVICES) {
                    memcpy(g_discovered_devices[g_discovered_count].addr,
                           scan_result->scan_rst.bda, 6);
                    g_discovered_devices[g_discovered_count].addr_type = 0; /* PUBLIC */
                    g_discovered_devices[g_discovered_count].rssi = scan_result->scan_rst.rssi;
                    strncpy(g_discovered_devices[g_discovered_count].name,
                            name, 31);
                    g_discovered_count++;

                    /* 自动连接第一个 HID 设备 */
                    if (g_discovered_count == 1 && g_scan_active) {
                        syslog(LOG_INFO, "[BLE] Auto-connecting to first HID device: %s\n", name);
                        ble_hid_stop_scan();

                        /* 发起连接 */
                        esp_ble_gattc_open(g_gattc_if,
                                          scan_result->scan_rst.bda,
                                          scan_result->scan_rst.ble_addr_type,
                                          true);
                    }
                }
            }
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        syslog(LOG_INFO, "[BLE] Scan parameters set complete\n");
        break;

    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            syslog(LOG_ERR, "[BLE] Scan start failed\n");
        } else {
            syslog(LOG_INFO, "[BLE] Scan started\n");
        }
        break;

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            syslog(LOG_ERR, "[BLE] Scan stop failed\n");
        } else {
            syslog(LOG_INFO, "[BLE] Scan stopped\n");
        }
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param->auth_cmpl.success) {
            syslog(LOG_INFO, "[BLE] Authentication success\n");

            /* 保存 Bond 信息 */
            g_current_device.valid = true;
            memcpy(g_current_device.addr, g_connected_addr, 6);
            g_current_device.addr_type = 0;

            /* 生成随机名称 (从设备地址) */
            snprintf(g_current_device.name, 32, "BLE Device %02X:%02X",
                    g_connected_addr[0], g_connected_addr[1]);

            /* 保存到 Flash */
            ble_storage_add_bond(&g_current_device);

            g_conn_state = BLE_STATE_CONNECTED;
            ble_hid_set_led(BLE_LED_OFF);  /* 灭 = 连接成功 */
        } else {
            syslog(LOG_ERR, "[BLE] Authentication failed: %d\n",
                   param->auth_cmpl.auth_mode);
            g_conn_state = BLE_STATE_IDLE;
            ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 慢闪 = 认证失败 */
        }
        break;

    case ESP_GAP_BLE_KEY_EVT:
        /* TODO(重新启用时): 原实现访问 param->ble_security.key.p_key_key
         * （Bluedroid 中不存在的字段，已移除）。重写时按
         * esp_ble_gap_cb_param_t 真实结构取 key 类型。
         * TODO(re-enable): p_key_key is not a real Bluedroid field. */
        break;

    default:
        break;
    }
}

/**
 * GATT 客户端回调
 */
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                         esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    int i;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        syslog(LOG_INFO, "[BLE] GATT client registered, app_id = %d\n", gattc_if);
        g_gattc_if = gattc_if;
        break;

    case ESP_GATTC_CONNECT_EVT:
        g_conn_id = p_data->connect.conn_id;
        memcpy(g_connected_addr, p_data->connect.remote_bda, 6);
        g_gatt_connected = true;
        syslog(LOG_INFO, "[BLE] GATT connected to: %02X:%02X:%02X:%02X:%02X:%02X\n",
               g_connected_addr[0], g_connected_addr[1], g_connected_addr[2],
               g_connected_addr[3], g_connected_addr[4], g_connected_addr[5]);

        g_conn_state = BLE_STATE_CONNECTING;

        /* 搜索 HID Service */
        esp_ble_gattc_search_service(g_gattc_if, p_data->connect.conn_id, NULL);
        break;

    case ESP_GATTC_DISCONNECT_EVT:
        syslog(LOG_INFO, "[BLE] GATT disconnected\n");
        g_gatt_connected = false;
        g_kbd_connected = false;
        g_mouse_connected = false;
        g_conn_state = BLE_STATE_DISCONNECTED;
        ble_hid_set_led(BLE_LED_FAST_FLASH);

        /* 断线重连 */
        if (CONFIG_RETRO_BLE_AUTO_CONNECT) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            syslog(LOG_INFO, "[BLE] Attempting to reconnect...\n");
            esp_ble_gattc_open(g_gattc_if, g_connected_addr, 0, true);
        }
        break;

    case ESP_GATTC_SEARCH_RES_EVT: {
        uint16_t handle = p_data->search_res.handle;
        uint16_t uuid = p_data->search_res.uuid.uuid;

        if (uuid == HID_SERVICE_UUID) {
            syslog(LOG_INFO, "[BLE] Found HID Service, handle: %d\n", handle);

            /* 获取 HID Report 特征值 */
            /* 遍历特征值列表 */
            for (i = 0; i < (int)p_data->search_res.num_results; i++) {
                /* 需要在 ESP_GATTC_SEARCH_INCL_RES_EVT 中处理 */
            }
        }
        break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT:
        syslog(LOG_INFO, "[BLE] Service search complete\n");

        /* 获取特征值列表 */
        if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
            syslog(LOG_INFO, "[BLE] Service found in remote device\n");
        } else {
            syslog(LOG_INFO, "[BLE] Service found in local database\n");
        }
        break;

    case ESP_GATTC_GET_CHAR_EVT:
        if (p_data->get_char.status == ESP_GATT_OK) {
            uint16_t uuid = p_data->get_char.char_uuid.uuid;

            if (uuid == HID_REPORT_UUID) {
                g_hid_report_handle = p_data->get_char.char_handle;
                syslog(LOG_INFO, "[BLE] Found HID Report handle: %d\n", g_hid_report_handle);

                /* 订阅 HID Report 通知 */
                if (g_hid_report_handle > 0) {
                    esp_ble_gattc_register_for_notify(g_gattc_if,
                                                      g_connected_addr,
                                                      g_hid_report_handle);
                }
            }
        }
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        syslog(LOG_INFO, "[BLE] Registered for notify, status: %d\n",
               p_data->reg_for_notify.status);
        break;

    case ESP_GATTC_NOTIFY_EVT:
        if (p_data->notify.is_notify) {
            /* HID 数据 */
            uint8_t *data = (uint8_t *)p_data->notify.value;
            uint16_t len = p_data->notify.value_len;

            /* 判断是键盘还是鼠标报告 */
            if (len >= 1) {
                uint8_t report_id = data[0];

                if (report_id == HID_REPORT_ID_KEYBOARD && len == HID_KBD_REPORT_SIZE) {
                    handle_kbd_report(data, len);
                } else if (report_id == HID_REPORT_ID_MOUSE && len == HID_MOUSE_REPORT_SIZE) {
                    handle_mouse_report(data, len);
                } else if (len == HID_KBD_REPORT_SIZE) {
                    /* 某些设备不发送 Report ID */
                    handle_kbd_report(data, len);
                } else if (len == HID_MOUSE_REPORT_SIZE) {
                    handle_mouse_report(data, len);
                }
            }
        }
        break;

    default:
        break;
    }
}

/*==========================
 *  BLE 操作函数
 *==========================*/

/**
 * 初始化 BLE HID
 */
int ble_hid_init(void)
{
    int ret;
    esp_err_t err;

    if (g_ble_initialized)
        return 0;

    syslog(LOG_INFO, "[BLE] Initializing BLE HID...\n");

    /* 初始化存储 */
    ret = ble_storage_init();
    if (ret != 0) {
        syslog(LOG_ERR, "[BLE] Failed to init storage: %d\n", ret);
        return ret;
    }

    /* 初始化信号量 */
    if (!g_sem_initialized) {
        sem_init(&g_conn_sem, 0, 0);
        g_sem_initialized = true;
    }

    /* 清空上次报告缓存 */
    memset(g_prev_kbd_report, 0, sizeof(g_prev_kbd_report));
    memset(&g_current_device, 0, sizeof(g_current_device));
    memset(g_discovered_devices, 0, sizeof(g_discovered_devices));
    g_discovered_count = 0;
    g_current_device_valid = false;

    /* 初始化 LED */
    ble_led_init();

    /* 初始化 BT Controller */
    err = esp_bt_controller_init(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] BT controller init failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] BT controller enable failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    /* 初始化 Bluedroid */
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Bluedroid init failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Bluedroid enable failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    /* 注册 GAP 回调 */
    err = esp_ble_gap_register_callback(esp_gap_cb);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] GAP callback register failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    /* 注册 GATT 客户端回调 */
    err = esp_ble_gattc_register_callback(esp_gattc_cb);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] GATT callback register failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    /* 注册 GATT 客户端应用 */
    err = esp_ble_gattc_app_register(0);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] GATT app register failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    /* 配置 BLE 设备名称 */
    err = esp_ble_gap_set_device_name(HID_DEV_NAME);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Set device name failed: %s\n", esp_err_to_name(err));
    }

    /* 配置安全参数 - JustWorks (无 MITM) */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_NO_BOND; /* 不需要绑定 */
    esp_ble_io_cap_t io_cap = ESP_IO_CAP_NO_INPUT_NO_OUTPUT; /* JustWorks */
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(esp_ble_auth_req_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_cap, sizeof(esp_ble_io_cap_t));

    /* 设置扫描参数 */
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,    /* 50 * 0.625ms = 31.25ms */
        .scan_window = 0x30,      /* 30 * 0.625ms = 18.75ms */
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    };

    err = esp_ble_gap_set_scan_params(&scan_params);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Set scan params failed: %s\n", esp_err_to_name(err));
        return -1;
    }

    g_ble_initialized = true;
    g_conn_state = BLE_STATE_IDLE;

    syslog(LOG_INFO, "[BLE] BLE HID initialized\n");
    syslog(LOG_INFO, "[BLE] Device name: %s\n", HID_DEV_NAME);

    /* 如果有已配对设备，尝试自动连接 */
    if (CONFIG_RETRO_BLE_AUTO_CONNECT) {
        struct ble_bond_s *bond = ble_storage_get_first_valid();
        if (bond != NULL) {
            char addr_str[32];
            ble_storage_addr2str(bond->addr, addr_str, sizeof(addr_str));
            syslog(LOG_INFO, "[BLE] Auto-connecting to %s (%s)\n", bond->name, addr_str);

            /* 发起连接 */
            g_conn_state = BLE_STATE_CONNECTING;
            ble_hid_set_led(BLE_LED_FAST_FLASH);  /* 快闪 = 正在连接 */

            err = esp_ble_gattc_open(g_gattc_if, bond->addr, bond->addr_type, true);
            if (err != ESP_OK) {
                syslog(LOG_ERR, "[BLE] Auto-connect failed: %s\n", esp_err_to_name(err));
                g_conn_state = BLE_STATE_IDLE;
                ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 慢闪 = 连接失败 */
            }
        } else {
            syslog(LOG_INFO, "[BLE] No bonded devices, waiting for pairing\n");
            /* 无配对设备，等待用户触发配对 */
            ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 慢闪 = 等待配对 */
        }
    }

    return 0;
}

/**
 * 关闭 BLE HID
 */
void ble_hid_deinit(void)
{
    if (!g_ble_initialized)
        return;

    ble_hid_stop_scan();

    if (g_gatt_connected) {
        esp_ble_gattc_close(g_gattc_if, g_conn_id);
    }

    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();

    g_ble_initialized = false;
    g_conn_state = BLE_STATE_IDLE;
    ble_hid_set_led(BLE_LED_OFF);

    if (g_sem_initialized) {
        sem_destroy(&g_conn_sem);
        g_sem_initialized = false;
    }
}

/**
 * 开始 BLE 扫描
 */
int ble_hid_start_scan(void)
{
    esp_err_t err;

    if (g_scan_active) {
        return 0;
    }

    syslog(LOG_INFO, "[BLE] Starting scan for HID devices...\n");

    g_discovered_count = 0;
    memset(g_discovered_devices, 0, sizeof(g_discovered_devices));
    g_scan_active = true;
    g_scan_start_time = sched_clock() / 1000;
    g_conn_state = BLE_STATE_SCANNING;
    ble_hid_set_led(BLE_LED_FAST_FLASH);

    err = esp_ble_gap_start_scanning(CONFIG_RETRO_BLE_SCAN_TIMEOUT);
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Start scan failed: %s\n", esp_err_to_name(err));
        g_scan_active = false;
        g_conn_state = BLE_STATE_IDLE;
        ble_hid_set_led(BLE_LED_OFF);
        return -1;
    }

    return 0;
}

/**
 * 停止 BLE 扫描
 */
int ble_hid_stop_scan(void)
{
    esp_err_t err;

    if (!g_scan_active)
        return 0;

    err = esp_ble_gap_stop_scanning();
    if (err != ESP_OK) {
        syslog(LOG_ERR, "[BLE] Stop scan failed: %s\n", esp_err_to_name(err));
    }

    g_scan_active = false;

    if (g_conn_state == BLE_STATE_SCANNING) {
        g_conn_state = BLE_STATE_IDLE;
        ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 慢闪 = 未找到设备 */
    }

    return 0;
}

/**
 * 断开当前连接
 */
int ble_hid_disconnect(void)
{
    if (!g_gatt_connected)
        return 0;

    esp_ble_gattc_close(g_gattc_if, g_conn_id);

    g_gatt_connected = false;
    g_kbd_connected = false;
    g_mouse_connected = false;
    g_conn_state = BLE_STATE_IDLE;
    ble_hid_set_led(BLE_LED_OFF);

    return 0;
}

/**
 * 删除指定索引的配对设备
 */
int ble_hid_delete_bond(int index)
{
    int ret;

    ret = ble_storage_remove_bond_by_index(index);
    if (ret == 0) {
        syslog(LOG_INFO, "[BLE] Bond %d deleted\n", index);
    }

    return ret;
}

/**
 * 获取连接状态
 */
enum ble_conn_state_e ble_hid_get_state(void)
{
    return g_conn_state;
}

/**
 * 获取连接状态字符串
 */
const char *ble_hid_get_state_str(void)
{
    switch (g_conn_state) {
    case BLE_STATE_IDLE:        return "IDLE";
    case BLE_STATE_SCANNING:    return "SCANNING";
    case BLE_STATE_CONNECTING:  return "CONNECTING";
    case BLE_STATE_PAIRING:     return "PAIRING";
    case BLE_STATE_CONNECTED:   return "CONNECTED";
    case BLE_STATE_DISCONNECTED:return "DISCONNECTED";
    default:                     return "UNKNOWN";
    }
}

/**
 * 获取已配对设备数量
 */
int ble_hid_get_paired_count(void)
{
    return ble_storage_get_count();
}

/**
 * 获取已配对设备
 */
struct ble_bond_s *ble_hid_get_paired_device(int index)
{
    return ble_storage_get_bond(index);
}

/**
 * 清除所有配对
 */
int ble_hid_clear_all_bonds(void)
{
    ble_hid_disconnect();
    return ble_storage_clear_all();
}

/**
 * BLE HID 主循环处理
 *
 * 在 Core 1 系统任务中定期调用
 */
void ble_hid_task(void)
{
    uint32_t now;

    if (!g_ble_initialized)
        return;

    /* LED 状态更新 */
    ble_hid_led_task();

    /* 扫描超时检查 */
    if (g_scan_active && g_conn_state == BLE_STATE_SCANNING) {
        now = sched_clock() / 1000;
        if (now - g_scan_start_time > CONFIG_RETRO_BLE_SCAN_TIMEOUT * 1000) {
            syslog(LOG_INFO, "[BLE] Scan timeout, stopping scan\n");
            ble_hid_stop_scan();

            if (g_discovered_count == 0) {
                syslog(LOG_INFO, "[BLE] No HID devices found\n");
                ble_hid_set_led(BLE_LED_SLOW_FLASH);
            }
        }
    }
}

#endif /* CONFIG_RETRO_INPUT_BLE_HID && CONFIG_RETRO_BLE_STACK_IDF */
