/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ble_hid.h - BLE HID 头文件（S3）
 *
 * WHAT : BLE HID 头文件（S3）
 * WHY  : 驱动对上层契约
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/driver/ble_hid.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 初始化/回调原型
 */

#ifndef __BLE_HID_H__
#define __BLE_HID_H__

#include <stdint.h>
#include <stdbool.h>

/*==========================
 *  连接状态
 *==========================*/

/**
 * BLE 连接状态
 */
enum ble_conn_state_e {
    BLE_STATE_IDLE = 0,        /* 空闲/未连接 */
    BLE_STATE_SCANNING,        /* 扫描中 */
    BLE_STATE_CONNECTING,      /* 连接中 */
    BLE_STATE_PAIRING,         /* 配对中 */
    BLE_STATE_CONNECTED,       /* 已连接 */
    BLE_STATE_DISCONNECTED,    /* 连接断开 */
};

/*==========================
 *  LED 状态
 *==========================*/

/**
 * BLE LED 状态
 */
enum ble_led_state_e {
    BLE_LED_OFF = 0,           /* 关闭 */
    BLE_LED_SLOW_FLASH,        /* 慢闪 (2s 周期的) */
    BLE_LED_FAST_FLASH,        /* 快闪 (0.5s 周期) */
    BLE_LED_ON,                /* 常亮 */
};

/*==========================
 *  HID 报告类型
 *==========================*/

#define HID_REPORT_TYPE_KEYBOARD   1
#define HID_REPORT_TYPE_MOUSE      2

/*==========================
 *  函数声明
 *==========================*/

/* 初始化 */
int ble_hid_init(void);
void ble_hid_deinit(void);

/* 主循环调用 */
void ble_hid_task(void);

/* 连接状态 */
enum ble_conn_state_e ble_hid_get_state(void);
const char *ble_hid_get_state_str(void);

/* 手动控制 */
int ble_hid_start_scan(void);
int ble_hid_stop_scan(void);
int ble_hid_disconnect(void);

/* 连接状态查询 */
void ble_hid_get_status(bool *ble_init, bool *kbd, bool *mouse);

/* LED 控制 */
void ble_hid_set_led(int state);

#endif /* __BLE_HID_H__ */
