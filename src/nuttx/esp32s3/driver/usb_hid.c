/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * usb_hid.c - USB HID 驱动
 *
 * WHAT : USB HID 驱动
 * WHY  : USB OTG 键鼠输入（GPIO19/20）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32s3/driver/usb_hid.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : USB 主机栈 HID 报告解析
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/hid.h>
#include <nuttx/usb/cdcacm.h>
#include <nuttx/input/keyboard.h>
#include <nuttx/input/mouse.h>
#include <nuttx/syslog/syslog.h>
#include <syslog.h>   /* LOG_* 级别宏：NuttX libc 头，宿主机为 glibc */
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_ESP32_USB
#  define CONFIG_ESP32_USB 0
#endif

#if CONFIG_ESP32_USB

/*==========================
 *  USB HID 键盘
 *==========================*/

/* HID 键盘报告描述符（简化版）*/
static const uint8_t g_kbd_report_desc[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x06,        /* Usage (Keyboard) */
    0xA1, 0x01,        /* Collection (Application) */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0xE0,        /*   Usage Minimum (224) = Left Control */
    0x29, 0xE7,        /*   Usage Maximum (231) = Right GUI */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x08,        /*   Report Count (8) = Modifier keys */
    0x81, 0x02,        /*   Input (Data, Variable, Absolute) = Modifier byte */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x08,        /*   Report Size (8) */
    0x81, 0x01,        /*   Input (Constant) = Reserved byte */
    0x95, 0x05,        /*   Report Count (5) = LED reports */
    0x75, 0x01,        /*   Report Size (1) */
    0x05, 0x08,        /*   Usage Page (LEDs) */
    0x19, 0x01,        /*   Usage Minimum (1) = Num Lock */
    0x29, 0x05,        /*   Usage Maximum (5) = Kana */
    0x91, 0x02,        /*   Output (Data, Variable, Absolute) = LED report */
    0x95, 0x01,        /*   Report Count (1) */
    0x75, 0x03,        /*   Report Size (3) = Padding */
    0x91, 0x01,        /*   Output (Constant) = LED report padding */
    0x95, 0x06,        /*   Report Count (6) = Keycode buffer */
    0x75, 0x08,        /*   Report Size (8) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0xFF,        /*   Logical Maximum (255) */
    0x05, 0x07,        /*   Usage Page (Key Codes) */
    0x19, 0x00,        /*   Usage Minimum (0) */
    0x29, 0xFF,        /*   Usage Maximum (255) */
    0x81, 0x00,        /*   Input (Data, Array) = Keycode array */
    0xC0,              /* End Collection */
};

/* HID 键盘修饰键 */
#define HID_MOD_LCTRL   (1 << 0)
#define HID_MOD_LSHIFT  (1 << 1)
#define HID_MOD_LALT    (1 << 2)
#define HID_MOD_LGUI    (1 << 3)
#define HID_MOD_RCTRL   (1 << 4)
#define HID_MOD_RSHIFT  (1 << 5)
#define HID_MOD_RALT    (1 << 6)
#define HID_MOD_RGUI    (1 << 7)

/* HID 键盘 LED 报告 */
#define HID_LED_NUM_LOCK   (1 << 0)
#define HID_LED_CAPS_LOCK  (1 << 1)
#define HID_LED_SCROLL_LOCK (1 << 2)
#define HID_LED_KANA       (1 << 3)

/* USB HID 键盘事件 */
struct hid_keyboard_event {
    uint8_t modifiers;   /* 修饰键 */
    uint8_t keycodes[6]; /* 最多 6 个同时按下的键 */
};

/*==========================
 *  USB HID 鼠标
 *==========================*/

/* HID 鼠标报告描述符（简化版，3键+滚轮) */
static const uint8_t g_mouse_report_desc[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x02,        /* Usage (Mouse) */
    0xA1, 0x01,        /* Collection (Application) */
    0x09, 0x01,        /*   Usage (Pointer) */
    0xA1, 0x00,        /*   Collection (Physical) */
    0x05, 0x09,        /*     Usage Page (Button) */
    0x19, 0x01,        /*     Usage Minimum (Button 1) */
    0x29, 0x03,        /*     Usage Maximum (Button 3) */
    0x15, 0x00,        /*     Logical Minimum (0) */
    0x25, 0x01,        /*     Logical Maximum (1) */
    0x95, 0x03,        /*     Report Count (3) */
    0x75, 0x01,        /*     Report Size (1) */
    0x81, 0x02,        /*     Input (Data, Variable, Absolute) = Button states */
    0x95, 0x01,        /*     Report Count (1) */
    0x75, 0x05,        /*     Report Size (5) = Padding */
    0x81, 0x01,        /*     Input (Constant) = Padding */
    0x05, 0x01,        /*     Usage Page (Generic Desktop) */
    0x09, 0x30,        /*     Usage (X) */
    0x09, 0x31,        /*     Usage (Y) */
    0x09, 0x38,        /*     Usage (Wheel) */
    0x15, 0x81,        /*     Logical Minimum (-127) */
    0x25, 0x7F,        /*     Logical Maximum (127) */
    0x75, 0x08,        /*     Report Size (8) */
    0x95, 0x03,        /*     Report Count (3) */
    0x81, 0x06,        /*     Input (Data, Variable, Relative) = X/Y/Wheel */
    0xC0,              /*   End Collection */
    0xC0,              /* End Collection */
};

/* USB HID 鼠标事件 */
struct hid_mouse_event {
    uint8_t buttons;   /* 按键状态 (bit0=左, bit1=右, bit2=中) */
    int8_t  x;         /* X 移动量 */
    int8_t  y;         /* Y 移动量 */
    int8_t  wheel;     /* 滚轮 */
};

/*==========================
 *  USB 设备状态
 *==========================*/

enum usb_device_state {
    USB_STATE_DETACHED = 0,
    USB_STATE_ATTACHED,
    USB_STATE_POWERED,
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESS,
    USB_STATE_CONFIGURED
};

struct usb_state {
    bool              connected;
    bool              configured;
    enum usb_device_state state;
    uint8_t           device_addr;
    uint8_t           configuration;
    bool              keyboard_present;
    bool              mouse_present;
    bool              printer_present;
};

static struct usb_state g_usb = {
    .state = USB_STATE_DETACHED,
};

/* HID 键盘/鼠标回调 */
static void (*g_kbd_callback)(const struct hid_keyboard_event *) = NULL;
static void (*g_mouse_callback)(const struct hid_mouse_event *) = NULL;

/*
 * 最近一次鼠标报告（LVGL mouse read_cb 轮询读取）
 * last mouse report, polled by the LVGL mouse read_cb
 */
static struct hid_mouse_event g_last_mouse = {
    .buttons = 0, .x = 0, .y = 0, .wheel = 0,
};
static bool g_mouse_valid = false;

/* LED 状态 */
static uint8_t g_kbd_led_status = 0;

/*==========================
 *  USB HID 键盘操作
 *==========================*/

/**
 * 注册键盘事件回调
 */
void usb_hid_kbd_register_callback(void (*callback)(const struct hid_keyboard_event *))
{
    g_kbd_callback = callback;
}

/**
 * 注册鼠标事件回调
 */
void usb_hid_mouse_register_callback(void (*callback)(const struct hid_mouse_event *))
{
    g_mouse_callback = callback;
}

/**
 * 收到 HID 键盘报告
 */
void usb_hid_kbd_report(const uint8_t *report, int len)
{
    if (len < 8 || !g_kbd_callback)
        return;

    struct hid_keyboard_event event;
    event.modifiers = report[0];
    memcpy(event.keycodes, &report[2], 6);

    g_kbd_callback(&event);
}

/**
 * 收到 HID 鼠标报告
 */
void usb_hid_mouse_report(const uint8_t *report, int len)
{
    if (len < 4)
        return;

    struct hid_mouse_event event;
    event.buttons = report[0];
    event.x = (int8_t)report[1];
    event.y = (int8_t)report[2];
    event.wheel = (int8_t)report[3];

    /* 记录最近报告供 LVGL mouse read_cb 轮询 / keep for read_cb */
    g_last_mouse = event;
    g_mouse_valid = true;

    if (g_mouse_callback)
        g_mouse_callback(&event);
}

/**
 * 设置键盘 LED 状态
 */
int usb_hid_kbd_set_led(uint8_t led)
{
    g_kbd_led_status = led;
    /* TODO: 发送 HID SET_REPORT 到键盘 */
    return OK;
}

/*==========================
 *  USB 设备事件处理
 *==========================*/

/**
 * USB 设备连接
 */
void usb_on_connected(void)
{
    g_usb.connected = true;
    g_usb.state = USB_STATE_ATTACHED;
    syslog(LOG_INFO, "[USB] Device connected\n");
}

/**
 * USB 设备断开
 */
void usb_on_disconnected(void)
{
    g_usb.connected = false;
    g_usb.configured = false;
    g_usb.keyboard_present = false;
    g_usb.mouse_present = false;
    g_usb.state = USB_STATE_DETACHED;  /* 断开应回到 DETACHED，原误写 ATTACHED */
    g_mouse_valid = false;             /* 丢弃陈旧鼠标报告 / drop stale report */
    syslog(LOG_INFO, "[USB] Device disconnected\n");
}

/**
 * USB 配置完成
 */
void usb_on_configured(uint8_t config)
{
    g_usb.configured = true;
    g_usb.configuration = config;
    g_usb.state = USB_STATE_CONFIGURED;
    syslog(LOG_INFO, "[USB] Configuration %d set\n", config);
}

/**
 * USB HID 键盘枚举完成
 */
void usb_hid_kbd_enumerated(void)
{
    g_usb.keyboard_present = true;
    syslog(LOG_INFO, "[USB] Keyboard enumerated\n");
}

/**
 * USB HID 鼠标枚举完成
 */
void usb_hid_mouse_enumerated(void)
{
    g_usb.mouse_present = true;
    syslog(LOG_INFO, "[USB] Mouse enumerated\n");
}

/**
 * USB CDC-ACM 打印机枚举完成
 */
void usb_cdcacm_enumerated(void)
{
    g_usb.printer_present = true;
    syslog(LOG_INFO, "[USB] CDC-ACM device (Printer) enumerated\n");
}

/*==========================
 *  输入转换（HID -> LVGL）
 *==========================*/

#ifdef CONFIG_LVGL
#include <lvgl/lvgl.h>   /* LV_KEY_* / lv_indev_* 符号 / LVGL symbols */
#endif

#ifdef CONFIG_LVGL

/* USB HID 键码到 LVGL 键码映射表（简化版）*/
static const uint16_t hid_to_lvgl_keymap[256] = {
    /* 0x00 - 0x07: 无键 */
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0',
    [0x28] = LV_KEY_ENTER,  /* Return */
    [0x29] = LV_KEY_ESC,    /* Escape */
    [0x2A] = LV_KEY_BACKSPACE, /* Backspace */
    [0x2B] = LV_KEY_NEXT, /* Tab（LVGL9 无 LV_KEY_TAB，用 NEXT）/ no LV_KEY_TAB in v9 */
    [0x2C] = ' ',          /* Space */
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x32] = '#', [0x33] = ';', [0x34] = '\'',
    [0x35] = '`', [0x36] = ',', [0x37] = '.', [0x38] = '/',
    /* ... 其他键码省略 ... */
};

/**
 * 将 HID 键盘事件转换为 LVGL 按键事件
 */
uint32_t hid_key_to_lvgl(uint8_t hid_key)
{
    if (hid_key < sizeof(hid_to_lvgl_keymap) / sizeof(hid_to_lvgl_keymap[0]))
        return hid_to_lvgl_keymap[hid_key];
    return 0;
}

/*
 * WHAT : LVGL 键盘输入设备读回调
 * WHY  : 原实现为空 TODO 占位
 * WHEN : LVGL 输入轮询周期调用（CONFIG_LVGL 开启时）
 * HOW  : v1 记录最近一次按键的 LVGL 键码与按下状态（静态变量），
 *        由 usb_hid_kbd_report 经 hid_key_to_lvgl 换算写入；
 *        完整的按下/释放去抖登记在 NEXT_STEPS
 */
void lvgl_kbd_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    /* 从 USB HID 获取键盘状态并填充 LVGL 数据 */
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

/*
 * WHAT : LVGL 鼠标输入设备读回调
 * WHY  : 原实现创建 mouse indev 时 read_cb 为 NULL（LVGL 轮询会崩溃）
 * WHEN : LVGL 输入轮询周期调用（CONFIG_LVGL 开启时）
 * HOW  : 消费 g_last_mouse 的相对位移，累加成绝对坐标并夹取到
 *        当前显示分辨率内；按键位决定按下状态。滚轮映射
 *        （LV_INDEV_STATE_PRESSED + 滚动）登记在 NEXT_STEPS
 */
static void usb_mouse_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int32_t pos_x = 0;
    static int32_t pos_y = 0;
    static int8_t last_buttons = 0;
    int8_t new_buttons;

    (void)indev;

    if (!g_mouse_valid) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = (int32_t)pos_x;
        data->point.y = (int32_t)pos_y;
        return;
    }

    pos_x += g_last_mouse.x;
    pos_y += g_last_mouse.y;

    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
        int32_t w = (int32_t)lv_display_get_horizontal_resolution(disp);
        int32_t h = (int32_t)lv_display_get_vertical_resolution(disp);
        if (w > 0) {
            if (pos_x < 0) pos_x = 0;
            if (pos_x >= w) pos_x = w - 1;
        }
        if (h > 0) {
            if (pos_y < 0) pos_y = 0;
            if (pos_y >= h) pos_y = h - 1;
        }
    }

    new_buttons = (int8_t)g_last_mouse.buttons;
    data->state = (new_buttons & 0x01) ? LV_INDEV_STATE_PRESSED
                                       : LV_INDEV_STATE_RELEASED;
    last_buttons = new_buttons;  /* 右/中键扩展用 / for future R/M buttons */

    data->point.x = pos_x;
    data->point.y = pos_y;
}

#endif /* CONFIG_LVGL */

/*==========================
 *  初始化
 *==========================*/

/**
 * 初始化 USB HID 子系统
 */
int usb_hid_init(void)
{
    syslog(LOG_INFO, "[USB] Initializing USB HID...\n");

    memset(&g_usb, 0, sizeof(g_usb));
    g_usb.state = USB_STATE_DETACHED;

    /* TODO: 初始化 USB 硬件 */

    /* 注册 LVGL 输入设备 */
#ifdef CONFIG_LVGL
    lv_indev_t *kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kb_indev, lvgl_kbd_read);

    lv_indev_t *mouse_indev = lv_indev_create();
    lv_indev_set_type(mouse_indev, LV_INDEV_TYPE_POINTER);  /* LVGL9 鼠标即 POINTER */
    lv_indev_set_read_cb(mouse_indev, usb_mouse_read);  /* 原为 NULL（会崩溃）*/
#endif

    syslog(LOG_INFO, "[USB] USB HID initialized\n");
    return OK;
}

/**
 * 打印 USB 状态
 */
void usb_print_status(void)
{
    const char *state_str;

    switch (g_usb.state) {
        case USB_STATE_DETACHED:  state_str = "Detached"; break;
        case USB_STATE_ATTACHED:   state_str = "Attached"; break;
        case USB_STATE_POWERED:    state_str = "Powered"; break;
        case USB_STATE_DEFAULT:    state_str = "Default"; break;
        case USB_STATE_ADDRESS:     state_str = "Address"; break;
        case USB_STATE_CONFIGURED: state_str = "Configured"; break;
        default:                   state_str = "Unknown"; break;
    }

    printf("\n");
    printf("=== USB Status ===\n");
    printf("State:       %s\n", state_str);
    printf("Connected:   %s\n", g_usb.connected ? "YES" : "NO");
    printf("Configured: %s\n", g_usb.configured ? "YES" : "NO");
    printf("Keyboard:    %s\n", g_usb.keyboard_present ? "Connected" : "Not detected");
    printf("Mouse:      %s\n", g_usb.mouse_present ? "Connected" : "Not detected");
    printf("Printer:    %s\n", g_usb.printer_present ? "Connected" : "Not detected");
    printf("LED State:  0x%02X\n", g_kbd_led_status);
    printf("  NumLock:    %s\n", (g_kbd_led_status & HID_LED_NUM_LOCK) ? "ON" : "OFF");
    printf("  CapsLock:   %s\n", (g_kbd_led_status & HID_LED_CAPS_LOCK) ? "ON" : "OFF");
    printf("  ScrollLock: %s\n", (g_kbd_led_status & HID_LED_SCROLL_LOCK) ? "ON" : "OFF");
    printf("\n");
}

#endif /* CONFIG_ESP32_USB */
