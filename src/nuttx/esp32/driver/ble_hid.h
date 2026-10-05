/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ble_hid.h - BLE HID 头文件（CAM）
 *
 * WHAT : BLE HID 头文件（CAM）
 * WHY  : 驱动对上层契约
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/driver/ble_hid.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 初始化/回调原型
 */

/**
 * ble_hid.h - BLE HID 键盘/鼠标驱动头文件
 */

#ifndef __BLE_HID_H__
#define __BLE_HID_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* size_t（ble_storage_addr2str 形参）*/

/*==========================
 *  Bond 信息结构
 *==========================*/

/**
 * BLE 配对设备信息
 */
struct ble_bond_s {
    uint8_t  addr_type;        /* 0=PUBLIC, 1=RANDOM */
    uint8_t  addr[6];          /* 设备 MAC 地址 */
    uint8_t  ltk[16];         /* Long Term Key */
    uint8_t  irk[16];         /* Identity Resolving Key */
    uint8_t  csrk[16];        /* Connection Signature Resolving Key */
    uint8_t  ediv;             /* EDIV */
    uint8_t  rand[8];         /* LTK 随机数 */
    char     name[32];         /* 设备名称 */
    bool     valid;            /* 是否有效 */
};

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
int ble_hid_delete_bond(int index);

/* 配对设备管理 */
int ble_hid_get_paired_count(void);
struct ble_bond_s *ble_hid_get_paired_device(int index);
int ble_hid_clear_all_bonds(void);

/* Bond 存储 */
int ble_storage_init(void);
int ble_storage_save(void);
int ble_storage_add_bond(struct ble_bond_s *bond);
int ble_storage_remove_bond_by_addr(uint8_t *addr);
int ble_storage_remove_bond_by_index(int index);
int ble_storage_clear_all(void);
int ble_storage_get_count(void);
struct ble_bond_s *ble_storage_get_bond(int index);
struct ble_bond_s *ble_storage_get_first_valid(void);
void ble_storage_list_all(void);
void ble_storage_addr2str(uint8_t *addr, char *str, size_t len);

/* LED 控制 */
void ble_hid_set_led(enum ble_led_state_e state);

/* LVGL UI */
void ble_ui_show(void);
void ble_ui_close(void);
void ble_ui_refresh(void);
void ble_ui_update_scan_status(const char *device_name, int rssi);
bool ble_ui_is_open(void);
int ble_ui_init(void);

#endif /* __BLE_HID_H__ */
