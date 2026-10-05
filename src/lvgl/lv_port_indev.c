/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: lv_port_indev.c
 * 描述: LVGL 输入设备端口驱动 - 键盘/鼠标/编码器
 *       使用环形缓冲区缓存按键和鼠标事件
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */

/*
 * lv_port_indev.c - LVGL 输入设备端口
 *
 * WHAT : LVGL 输入设备端口
 * WHY  : 把 USB/BLE HID 键鼠接入 LVGL
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/lvgl/lv_port_indev.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : read 回调轮询 HID 驱动，映射键值与坐标
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <lvgl/lvgl.h>

/*======================================
 *  按键环形缓冲区 / Key ring buffer
 *======================================*/
#define KEY_BUF_SIZE 32

static uint32_t g_key_buf[KEY_BUF_SIZE];
static volatile int g_key_head = 0;
static volatile int g_key_tail = 0;

/*======================================
 *  鼠标状态 / Mouse state
 *======================================*/
static volatile int32_t g_mouse_x = 0;
static volatile int32_t g_mouse_y = 0;
static volatile int8_t  g_mouse_btn = 0;

/*======================================
 *  编码器状态 / Encoder state
 *======================================*/
static volatile int8_t g_encoder_diff = 0;

/* 输入设备句柄 */
static lv_indev_t *g_keypad_indev = NULL;
static lv_indev_t *g_mouse_indev = NULL;
static lv_indev_t *g_encoder_indev = NULL;

/*======================================
 *  按键缓冲区操作 / Key buffer operations
 *======================================*/

/* 外部调用: 将按键压入环形缓冲区 / External: push key into ring buffer */
void lv_port_indev_push_key(uint32_t key)
{
    int next = (g_key_head + 1) % KEY_BUF_SIZE;
    if (next != g_key_tail)
    {
        g_key_buf[g_key_head] = key;
        g_key_head = next;
    }
}

/* 从缓冲区读取一个按键 / Read one key from buffer */
static uint32_t pop_key(void)
{
    if (g_key_head == g_key_tail)
        return 0;

    uint32_t key = g_key_buf[g_key_tail];
    g_key_tail = (g_key_tail + 1) % KEY_BUF_SIZE;
    return key;
}

/* 外部调用: 更新鼠标坐标和按键状态 / External: update mouse state */
void lv_port_indev_set_mouse(int32_t x, int32_t y, int8_t btn)
{
    g_mouse_x = x;
    g_mouse_y = y;
    g_mouse_btn = btn;
}

void lv_port_indev_set_encoder_diff(int8_t diff)
{
    g_encoder_diff = diff;
}

/*======================================
 *  LVGL 输入设备回调 / LVGL indev read callbacks
 *======================================*/

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t key = pop_key();

    data->state = (key != 0) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (key != 0)
        last_key = key;

    data->key = last_key;

    /* 缓冲区中还有数据时继续读取 / Continue reading if buffer has data */
    if (g_key_head != g_key_tail)
        data->continue_reading = true;
}

static void mouse_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = (lv_coord_t)g_mouse_x;
    data->point.y = (lv_coord_t)g_mouse_y;
    data->state = (g_mouse_btn > 0) ? LV_INDEV_STATE_PRESSED
                                     : LV_INDEV_STATE_RELEASED;
}

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->enc_diff = (int16_t)g_encoder_diff;
    data->key = 0;
    g_encoder_diff = 0;
}

/*======================================
 *  初始化 / Initialization
 *======================================*/

int lv_port_indev_init(void)
{
    lv_indev_t *indev;

    syslog(LOG_INFO, "lv_port_indev: Initializing LVGL input device port\n");

    /* 键盘 / Keypad */
    indev = lv_indev_create();
    if (indev == NULL)
    {
        syslog(LOG_ERR, "lv_port_indev: Failed to create keypad indev\n");
        return -ENOMEM;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, keypad_read);
    g_keypad_indev = indev;

    /* 鼠标 / Mouse */
    indev = lv_indev_create();
    if (indev == NULL)
    {
        syslog(LOG_ERR, "lv_port_indev: Failed to create mouse indev\n");
        return -ENOMEM;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /* 9.5 无 MOUSE 类型，指针即鼠标 */
    lv_indev_set_read_cb(indev, mouse_read);
    g_mouse_indev = indev;

    /* 编码器 / Encoder */
    indev = lv_indev_create();
    if (indev == NULL)
    {
        syslog(LOG_ERR, "lv_port_indev: Failed to create encoder indev\n");
        return -ENOMEM;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, encoder_read);
    g_encoder_indev = indev;

    syslog(LOG_INFO, "lv_port_indev: LVGL input device port initialized\n");

    return OK;
}

void lv_port_indev_deinit(void)
{
    if (g_keypad_indev != NULL)
    {
        lv_indev_delete(g_keypad_indev);
        g_keypad_indev = NULL;
    }

    if (g_mouse_indev != NULL)
    {
        lv_indev_delete(g_mouse_indev);
        g_mouse_indev = NULL;
    }

    if (g_encoder_indev != NULL)
    {
        lv_indev_delete(g_encoder_indev);
        g_encoder_indev = NULL;
    }

    syslog(LOG_INFO, "lv_port_indev: LVGL input device port deinitialized\n");
}

lv_indev_t *lv_port_keypad_indev_get(void)
{
    return g_keypad_indev;
}

lv_indev_t *lv_port_mouse_indev_get(void)
{
    return g_mouse_indev;
}

lv_indev_t *lv_port_encoder_indev_get(void)
{
    return g_encoder_indev;
}
