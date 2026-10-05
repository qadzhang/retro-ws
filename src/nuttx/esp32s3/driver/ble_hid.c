/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ble_hid.c - BLE HID 驱动（S3）
 *
 * WHAT : BLE HID 驱动（S3）
 * WHY  : 蓝牙键鼠（NimBLE），WS2812 状态指示
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/ble_hid.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）；
 *        同日晚 LED 改走 ws2812_rmt（RMT 硬件外设）
 * HOW  : GAP 扫描连接 + HID 报告上报；LED 经 /dev/rmt0 RMT 硬件驱动
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <debug.h>

#include "esp32s3.h"
#include "board.h"

/*
 * 编译开关说明（2026-10-04）/ Build-switch note:
 * 本文件主体依赖 ESP-IDF 头（driver/gpio.h、hal/gpio_hal.h、
 * gpio_config_t/gpio_num_t 等），NuttX 构建下不存在，无法编译。
 * NuttX 路线的 BLE 移植将基于 NimBLE，见 NEXT_STEPS。
 * CONFIG_RETRO_BLE_STACK_IDF 是为未来 IDF 构建保留的符号，当前任何
 * 配置都不定义它——即本文件整体编译关闭（保持可语法检查）。
 * The body uses ESP-IDF headers (driver/gpio.h, hal/gpio_hal.h) that do
 * not exist under NuttX. The NuttX-line BLE port will be NimBLE-based,
 * see NEXT_STEPS. CONFIG_RETRO_BLE_STACK_IDF is reserved for a future
 * IDF-based build and is defined nowhere today.
 */
#if defined(CONFIG_RETRO_INPUT_BLE_HID) && defined(CONFIG_RETRO_BLE_STACK_IDF)

/*==========================
 *  ESP-IDF GPIO 头文件
 *==========================*/

#include "driver/gpio.h"
#include "hal/gpio_hal.h"

/* WS2812 RMT 硬件驱动（2026-10-04 晚：状态灯改真外设路径） */
#include "ws2812_rmt.h"

/*==========================
 *  LED 配置
 *==========================*/

#define LED_GPIO_PIN       BT_LED_GPIO  /* 来自 board.h: WS2812 (v1.1=GPIO38 / v1.0=GPIO48) */

/* LED 状态 */
static enum {
    BLE_LED_OFF = 0,
    BLE_LED_SLOW_FLASH,   /* 慢闪 (2s/次) */
    BLE_LED_FAST_FLASH,   /* 快闪 (0.5s/次) */
    BLE_LED_ON,           /* 常亮 */
} g_led_state = BLE_LED_OFF;

static bool g_led_on = false;
static uint32_t g_led_last_toggle = 0;

/*==========================
 *  HID 报告解析
 *==========================*/

/* 标准 USB HID 键盘报告 (8 bytes):
 * [0] Modifier keys (Ctrl/Shift/Alt/GUI)
 * [1] Reserved
 * [2-7] Key codes (max 6 simultaneous)
 */
#define HID_KBD_REPORT_SIZE   8
#define HID_KBD_MAX_KEYS      6

/* Modifier 位掩码 */
#define HID_MOD_LCTRL   (1 << 0)
#define HID_MOD_LSHIFT  (1 << 1)
#define HID_MOD_LALT    (1 << 2)
#define HID_MOD_LGUI    (1 << 3)
#define HID_MOD_RCTRL   (1 << 4)
#define HID_MOD_RSHIFT  (1 << 5)
#define HID_MOD_RALT    (1 << 6)
#define HID_MOD_RGUI    (1 << 7)

/* 标准 USB HID 鼠标报告 (4 bytes):
 * [0] Button bitmap (bit0=Left, bit1=Right, bit2=Middle)
 * [1] X displacement (signed)
 * [2] Y displacement (signed)
 * [3] Wheel displacement (signed)
 */
#define HID_MOUSE_REPORT_SIZE  4

/*==========================
 *  HID -> LVGL 键码映射
 *==========================*/

struct hid_keymap_s {
    uint8_t hid_key;
    uint32_t lvgl_key;
};

static const struct hid_keymap_s g_hid_keymap[] = {
    /* 字母键 A-Z (HID 0x04-0x1D) */
    {0x04, 'a'}, {0x05, 'b'}, {0x06, 'c'}, {0x07, 'd'},
    {0x08, 'e'}, {0x09, 'f'}, {0x0A, 'g'}, {0x0B, 'h'},
    {0x0C, 'i'}, {0x0D, 'j'}, {0x0E, 'k'}, {0x0F, 'l'},
    {0x10, 'm'}, {0x11, 'n'}, {0x12, 'o'}, {0x13, 'p'},
    {0x14, 'q'}, {0x15, 'r'}, {0x16, 's'}, {0x17, 't'},
    {0x18, 'u'}, {0x19, 'v'}, {0x1A, 'w'}, {0x1B, 'x'},
    {0x1C, 'y'}, {0x1D, 'z'},
    /* 数字键 1-9, 0 (HID 0x1E-0x27) */
    {0x1E, '1'}, {0x1F, '2'}, {0x20, '3'}, {0x21, '4'},
    {0x22, '5'}, {0x23, '6'}, {0x24, '7'}, {0x25, '8'},
    {0x26, '9'}, {0x27, '0'},
    /* 特殊键 */
    {0x28, '\n'},   /* Enter */
    {0x29, 0x1B},   /* Escape */
    {0x2A, 0x08},   /* Backspace */
    {0x2B, '\t'},   /* Tab */
    {0x2C, ' '},    /* Space */
    /* 功能键 F1-F12 (HID 0x3A-0x45) */
    {0x3A, 0x01},   /* F1 */
    {0x3B, 0x02},   /* F2 */
    {0x3C, 0x03},   /* F3 */
    {0x3D, 0x04},   /* F4 */
    {0x3E, 0x05},   /* F5 */
    {0x3F, 0x06},   /* F6 */
    {0x40, 0x07},   /* F7 */
    {0x41, 0x08},   /* F8 */
    {0x42, 0x09},   /* F9 */
    {0x43, 0x0A},   /* F10 */
    {0x44, 0x0B},   /* F11 */
    {0x45, 0x0C},   /* F12 */
    /* 方向键（HID 0x4F-0x52 = Right/Left/Down/Up）
     * TODO(重新启用时): 原实现在此映射了 0x1B/0x1A/0x19/0x18 等
     * 伪键码（既非 ASCII 也非 LV_KEY_*）。重写时应包含 <lvgl/lvgl.h>
     * 并映射到 LV_KEY_RIGHT/LEFT/DOWN/UP。
     * TODO(re-enable): map 0x4F..0x52 to LV_KEY_RIGHT/LEFT/DOWN/UP
     * (include lvgl.h) instead of the fabricated codes removed here. */
};

#define HID_KEYMAP_SIZE (sizeof(g_hid_keymap) / sizeof(g_hid_keymap[0]))

/**
 * 将 HID key code 转换为 LVGL key
 */
static uint32_t hid_key_to_lvgl(uint8_t hid_key)
{
    for (int i = 0; i < HID_KEYMAP_SIZE; i++) {
        if (g_hid_keymap[i].hid_key == hid_key)
            return g_hid_keymap[i].lvgl_key;
    }
    return 0;
}

/*==========================
 *  BLE 状态
 *==========================*/

static bool g_ble_initialized = false;
static bool g_ble_connected = false;
static bool g_ble_scanning = false;
static bool g_kbd_connected = false;
static bool g_mouse_connected = false;

/* 上一次键盘报告（用于去重和释放检测） */
static uint8_t g_prev_kbd_report[HID_KBD_REPORT_SIZE];

/* 设备地址 */
static uint8_t g_kbd_addr[6];
static uint8_t g_mouse_addr[6];

/*==========================
 *  LED 控制
 *==========================*/

/* WS2812 蓝牙状态用蓝色通道（灭=已连接/慢闪=配对/快闪=扫描） */
#define BLE_LED_BRIGHT          64

/*
 * WHAT : 初始化状态 LED（WS2812 走 RMT 硬件外设）
 * WHY  : WS2812 单线协议 gpio 直驱点不亮（HARDWARE.md 2.8），
 *        2026-10-04 晚由 gpio 桩改为 ws2812_rmt 硬件路径
 * HOW  : 打开 /dev/rmt0（板级 bringup 已绑 GPIO38/48）并灭灯
 */
static void ble_led_init(void)
{
#if LED_GPIO_PIN >= 0
    int ret = ws2812_rmt_init(NULL);

    if (ret < 0)
        syslog(LOG_WARNING, "[BLE] WS2812 RMT init failed: %d\n", ret);

    g_led_on = false;
    g_led_state = BLE_LED_SLOW_FLASH;  /* 初始为慢闪（等待连接） */

    syslog(LOG_INFO, "[BLE] WS2812 via RMT (pin %d) initialized\n", LED_GPIO_PIN);
#endif
}

/**
 * 设置 LED 状态
 */
void ble_hid_set_led(int state)
{
#if LED_GPIO_PIN >= 0
    g_led_state = state;
#endif
}

/**
 * LED 闪烁任务 (在主循环中调用)
 */
static void ble_hid_led_task(void)
{
#if LED_GPIO_PIN < 0
    return;
#else
    uint32_t now = sched_clock() / 1000; /* ms */
    uint32_t interval;

    switch (g_led_state) {
    case BLE_LED_OFF:
        if (g_led_on) {
            ws2812_rmt_set_rgb(0, 0, 0);
            g_led_on = false;
        }
        break;

    case BLE_LED_ON:
        if (!g_led_on) {
            ws2812_rmt_set_rgb(0, 0, BLE_LED_BRIGHT);
            g_led_on = true;
        }
        break;

    case BLE_LED_SLOW_FLASH:
        interval = 1000; /* 1s on, 1s off */
        if (now - g_led_last_toggle > interval) {
            g_led_on = !g_led_on;
            ws2812_rmt_set_rgb(0, 0, g_led_on ? BLE_LED_BRIGHT : 0);
            g_led_last_toggle = now;
        }
        break;

    case BLE_LED_FAST_FLASH:
        interval = 250; /* 0.25s on, 0.25s off */
        if (now - g_led_last_toggle > interval) {
            g_led_on = !g_led_on;
            ws2812_rmt_set_rgb(0, 0, g_led_on ? BLE_LED_BRIGHT : 0);
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
 * 处理键盘 HID 报告
 */
static void handle_kbd_report(const uint8_t *report, size_t len)
{
    if (len < HID_KBD_REPORT_SIZE)
        return;

    uint8_t modifiers = report[0];

    /* 检查新按下的键 */
    for (int i = 2; i < 2 + HID_KBD_MAX_KEYS; i++) {
        uint8_t key = report[i];
        if (key == 0) continue;

        /* 是否是新按下的 */
        bool is_new = true;
        for (int j = 2; j < 2 + HID_KBD_MAX_KEYS; j++) {
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
                    lvgl_key -= 32;
            }

            if (lvgl_key) {
                /* 发送到 LVGL 输入设备 */
                extern void lvgl_send_key(uint32_t key, bool pressed);
                lvgl_send_key(lvgl_key, true);
            }
        }
    }

    /* 检查释放的键 */
    for (int i = 2; i < 2 + HID_KBD_MAX_KEYS; i++) {
        uint8_t key = g_prev_kbd_report[i];
        if (key == 0) continue;

        bool still_pressed = false;
        for (int j = 2; j < 2 + HID_KBD_MAX_KEYS; j++) {
            if (report[j] == key) {
                still_pressed = true;
                break;
            }
        }

        if (!still_pressed) {
            uint32_t lvgl_key = hid_key_to_lvgl(key);
            if (lvgl_key) {
                extern void lvgl_send_key(uint32_t key, bool pressed);
                lvgl_send_key(lvgl_key, false);
            }
        }
    }

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

    extern void lvgl_send_mouse(int8_t dx, int8_t dy, uint8_t buttons);
    lvgl_send_mouse(dx, dy, buttons);
}

/**
 * BLE GATT HID 报告通知回调
 *
 * 当 BLE HID 设备发送报告时触发
 * 需要根据 Report Map 判断是键盘还是鼠标
 */
void ble_hid_report_callback(const uint8_t *report, size_t len,
                              uint16_t conn_handle, uint16_t attr_handle)
{
    if (len == HID_KBD_REPORT_SIZE) {
        handle_kbd_report(report, len);
    } else if (len == HID_MOUSE_REPORT_SIZE) {
        handle_mouse_report(report, len);
    }
}

/*==========================
 *  BLE GAP 事件处理
 *==========================*/

/**
 * BLE GAP 事件回调
 *
 * 处理扫描结果、连接、断开等事件
 * 同时更新 LED 状态
 */
void ble_hid_gap_event(int event, void *arg)
{
    switch (event) {
        case 0: /* BLE_GAP_EVENT_DISC */
            /* 发现设备，检查是否为 HID 设备 */
            break;

        case 1: /* BLE_GAP_EVENT_CONNECT */
            g_ble_connected = true;
            ble_hid_set_led(BLE_LED_OFF);  /* 连接成功，LED 灭 */
            syslog(LOG_INFO, "[BLE] Device connected\n");
            break;

        case 2: /* BLE_GAP_EVENT_DISCONNECT */
            g_ble_connected = false;
            g_kbd_connected = false;
            g_mouse_connected = false;
            ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 断开，慢闪 */
            syslog(LOG_INFO, "[BLE] Device disconnected, restarting scan\n");
            /* 自动重新扫描 */
            g_ble_scanning = false;
            break;

        case 3: /* BLE_GAP_EVENT_DISC_COMPLETE */
            if (!g_ble_connected) {
                ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 扫描完成但未连接，慢闪 */
                syslog(LOG_INFO, "[BLE] Scan complete, no HID device found\n");
            }
            g_ble_scanning = false;
            break;

        default:
            break;
    }
}

/*==========================
 *  公开 API
 *==========================*/

/**
 * 初始化 BLE HID
 *
 * ESP32-S3 的 BLE 5.0 栈初始化流程:
 * 1. 初始化 NimBLE 控制器和主机
 * 2. 配置 GAP（通用访问配置文件）
 * 3. 配置 SMP（安全管理协议，使能配对）
 * 4. 开始扫描 HID 设备
 */
int ble_hid_init(void)
{
    if (g_ble_initialized)
        return 0;

    syslog(LOG_INFO, "[BLE] Initializing BLE HID (ESP32-S3)...\n");

    /* 初始化 LED */
    ble_led_init();

    /* TODO: 初始化 NimBLE 协议栈
     * 1. nimble_port_init()
     * 2. ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO  (Just Works)
     * 3. ble_hs_cfg.sm_bonding = 1
     * 4. ble_hs_cfg.sm_mitm = 0
     * 5. 注册 GAP 回调: ble_gap_event_callback = ble_hid_gap_event
     * 6. 注册 GATT 发现回调
     */

    memset(g_prev_kbd_report, 0, sizeof(g_prev_kbd_report));
    memset(g_kbd_addr, 0, sizeof(g_kbd_addr));
    memset(g_mouse_addr, 0, sizeof(g_mouse_addr));

    g_ble_initialized = true;
    ble_hid_set_led(BLE_LED_SLOW_FLASH);  /* 初始为慢闪 */

    syslog(LOG_INFO, "[BLE] BLE HID initialized (ESP32-S3 BLE 5.0)\n");
    syslog(LOG_INFO, "[BLE] LED: GPIO%d (blue), scanning for HID devices...\n", LED_GPIO_PIN);

    return 0;
}

/**
 * BLE HID 主循环处理
 *
 * 在 Core 1 系统任务中定期调用，处理：
 * 1. 重连逻辑
 * 2. LED 闪烁任务
 */
void ble_hid_task(void)
{
    if (!g_ble_initialized)
        return;

    /* LED 闪烁处理 */
    ble_hid_led_task();

    /* 断开后自动重新扫描 */
    if (!g_ble_connected && !g_ble_scanning) {
        g_ble_scanning = true;
        ble_hid_set_led(BLE_LED_FAST_FLASH);  /* 扫描中，快闪 */
        /* TODO: ble_gap_scan_start() */
    }
}

/**
 * 关闭 BLE HID
 */
void ble_hid_deinit(void)
{
    g_ble_initialized = false;
    g_ble_connected = false;
    g_ble_scanning = false;
    g_kbd_connected = false;
    g_mouse_connected = false;
    ble_hid_set_led(BLE_LED_OFF);
}

/**
 * 获取 BLE HID 连接状态
 */
void ble_hid_get_status(bool *ble_init, bool *kbd, bool *mouse)
{
    *ble_init = g_ble_initialized;
    *kbd = g_kbd_connected;
    *mouse = g_mouse_connected;
}

#endif /* CONFIG_RETRO_INPUT_BLE_HID && CONFIG_RETRO_BLE_STACK_IDF */
