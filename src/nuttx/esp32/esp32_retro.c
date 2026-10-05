/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * esp32_retro.c - ESP32-CAM 主入口（兼容目标）
 *
 * WHAT : ESP32-CAM 主入口（兼容目标）
 * WHY  : 双核任务装配：Core0 图形音频 / Core1 系统网络
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/esp32_retro.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 与 S3 模板同构，外设按 CAM 硬件档案
 */

/**
 * esp32_retro.c - ESP32-CAM 复古工作站主入口
 *
 * 系统启动流程：
 * 1. board_early_initialize() — NuttX 内核启动前
 * 2. NuttX 内核启动 (SMP 自动启动第二核)
 * 3. board_late_initialize() — 内核启动后
 * 4. 双核任务运行
 */

#include <nuttx/config.h>

/* NuttX 版本串兜底（部分 defconfig 无该符号） */
#ifndef CONFIG_VERSION_STRING
#  define CONFIG_VERSION_STRING "12.12.0"
#endif
#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/sched.h>
#include <nuttx/init.h>
#include <nuttx/kthread.h>
#include <sys/boardctl.h>

#include "esp32.h"
#include "board.h"

/*==========================
 *  日志
 *==========================*/

#define syslog_emerg(fmt, ...)    syslog(LOG_EMERG,   fmt, ##__VA_ARGS__)
#define syslog_err(fmt, ...)      syslog(LOG_ERR,     fmt, ##__VA_ARGS__)
#define syslog_warning(fmt, ...)  syslog(LOG_WARNING, fmt, ##__VA_ARGS__)
#define syslog_info(fmt, ...)     syslog(LOG_INFO,    fmt, ##__VA_ARGS__)
#define syslog_debug(fmt, ...)    syslog(LOG_DEBUG,   fmt, ##__VA_ARGS__)

/*==========================
 *  双核任务定义
 *==========================*/

#define CORE0_TASK_PRI   100
#define CORE0_TASK_STACK (4 * 1024)

#define CORE1_TASK_PRI   120
#define CORE1_TASK_STACK (8 * 1024)

/*==========================
 *  前向声明
 *==========================*/

static int core1_media_task(int argc, char **argv);
static int core0_system_task(int argc, char **argv);

/*==========================
 *  公开变量
 *==========================*/

volatile bool g_system_running = false;
volatile bool g_gui_running = false;

/*==========================
 *  看门狗
 *==========================*/

#ifdef CONFIG_WATCHDOG

void esp32_wdt_init(int wdt_id, uint32_t timeout_ms)
{
    uint32_t base;
    switch (wdt_id) {
        case 0:  base = ESP32_TIMERGROUP0_BASE; break;
        case 1:  base = ESP32_TIMERGROUP1_BASE; break;
        default: return;
    }

    /* 解锁 */
    putreg32(ESP32_WDT_WKEY_VALUE, base + ESP32_TIMG_WDTWPROTECT_OFFSET);

    /* 配置：阶段0=中断, 阶段1=系统复位 */
    uint32_t ticks = timeout_ms * (80000000 / 1000);
    putreg32(ticks, base + ESP32_TIMG_WDTCONFIG2_OFFSET);
    putreg32(ticks * 2, base + ESP32_TIMG_WDTCONFIG3_OFFSET);
    putreg32(ESP32_WDT_STG0_INT | ESP32_WDT_STG1_RESET_SYS | ESP32_WDT_EN,
             base + ESP32_TIMG_WDTCONFIG0_OFFSET);

    /* 锁定 */
    putreg32(0, base + ESP32_TIMG_WDTWPROTECT_OFFSET);
}

void esp32_wdt_feed(int wdt_id)
{
    uint32_t base;
    switch (wdt_id) {
        case 0:  base = ESP32_TIMERGROUP0_BASE; break;
        case 1:  base = ESP32_TIMERGROUP1_BASE; break;
        default: return;
    }

    putreg32(ESP32_WDT_WKEY_VALUE, base + ESP32_TIMG_WDTWPROTECT_OFFSET);
    putreg32(0, base + ESP32_TIMG_WDTFEED_OFFSET);
    putreg32(0, base + ESP32_TIMG_WDTWPROTECT_OFFSET);
}
#endif /* CONFIG_WATCHDOG */

/*==========================
 *  软件复位 — ESP32 RTC 方式
 *==========================*/

void Software_Reset(void)
{
    putreg32(ESP32_RTC_CNTL_SW_SYS_RESET, ESP32_RTC_CNTL_OPTIONS0_REG);
    while (1);
}

/*==========================
 *  Core 0 任务（图形+音频，DAC 模式）
 *==========================*/

static int core1_media_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* 钉到 CPU1：双核分工（CPU0=程序，CPU1=图形/视频/音频/文件IO） */
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
    }
#endif

    syslog_info("[Core0] Graphics task started (DAC mode)\n");

#ifdef CONFIG_WATCHDOG
    esp32_wdt_init(0, 10000);
#endif

    /* 初始化 CVBS 显示 — DAC 模式 */
#ifdef CONFIG_RETRO_DISPLAY
    syslog_info("[Core0] Initializing CVBS display (DAC)...\n");
    {
        extern int drv_cvbs_init(void);
        drv_cvbs_init();
    }
#endif

    /* 初始化 LVGL */
#ifdef CONFIG_LVGL
    syslog_info("[Core0] Initializing LVGL...\n");
    extern void lvgl_init(void);
    lvgl_init();

    extern void retro_desktop_init(void);
    retro_desktop_init();
#endif

    /* 初始化音频 — DAC 模式 */
#ifdef CONFIG_RETRO_AUDIO
    syslog_info("[Core0] Initializing audio (DAC)...\n");
    extern int audio_init(void);
    audio_init();
#endif

    g_gui_running = true;

    /* 主循环 */
    while (g_system_running) {
#ifdef CONFIG_WATCHDOG
        esp32_wdt_feed(1);
#endif

#ifdef CONFIG_LVGL
        extern void lvgl_task_handler(void);
        lvgl_task_handler();
#endif

        usleep(30000);
    }

    syslog_info("[Core0] Graphics task exiting\n");
    return 0;
}

/*==========================
 *  Core 1 任务（系统+网络）
 *==========================*/

static int core0_system_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog_info("[Core1] System task started\n");

#ifdef CONFIG_WATCHDOG
    esp32_wdt_init(1, 10000);
#endif

    /* NTP */
#ifdef CONFIG_RETRO_NTP
    syslog_info("[Core1] Starting NTP sync...\n");
    {
        extern int ntp_sync_start(void);
        ntp_sync_start();
    }
#endif

    /* WiFi（SSID 宏依赖 RETRO_WIFI 打开，双重条件防未定义标识符） */
#if defined(CONFIG_ESP32_WIFI) && defined(CONFIG_RETRO_WIFI_SSID)
    syslog_info("[Core1] Connecting to WiFi...\n");
    {
        extern int wifi_connect(const char *ssid, const char *pass);
        wifi_connect(CONFIG_RETRO_WIFI_SSID, CONFIG_RETRO_WIFI_PASSWORD);
    }
#endif

    /* Cron */
#ifdef CONFIG_RETRO_CRON
    syslog_info("[Core1] Starting cron...\n");
    {
        extern int cron_init(void);
        cron_init();
    }
#endif

    /* BLE HID */
#ifdef CONFIG_RETRO_INPUT_BLE_HID
    syslog_info("[Core1] Initializing BLE HID...\n");
    extern int ble_hid_init(void);
    ble_hid_init();
#endif

    syslog_info("[Core1] System task running\n");

    while (g_system_running) {
#ifdef CONFIG_WATCHDOG
        esp32_wdt_feed(0);
#endif
        usleep(100000);
    }

    syslog_info("[Core1] System task exiting\n");
    return 0;
}

/*==========================
 *  板级后期初始化
 *==========================*/

/*==========================
 *  任务装配 / called from board.c board_late_initialize()
 *==========================*/

/*
 * WHAT : 启动双核任务（符号唯一归属 board.c，本函数只做装配）
 * WHY  : 消除原先与 board.c 的 board_late_initialize 等符号重复
 * HOW  : kthread_create 两任务，失败仅告警（NSH 仍可用）
 */
void esp32_retro_start(void)
{
    int ret;

    syslog_info("========================================\n");
    syslog_info("ESP32-CAM 复古联网图形工作站\n");
    syslog_info("NuttX %s\n", CONFIG_VERSION_STRING);
    syslog_info("========================================\n");

    g_system_running = true;

    /* 媒体核（CPU1）：图形/视频/音频/文件IO */
    ret = kthread_create("media", CORE1_TASK_PRI,
                         CORE1_TASK_STACK,
                         core1_media_task, NULL);
    if (ret < 0) {
        syslog_err("Failed to start media task: %d\n", ret);
    }

    /* 程序核常驻服务（CPU0）：网络/对时/定时/输入 */
    ret = kthread_create("syssvc", CORE0_TASK_PRI,
                         CORE0_TASK_STACK,
                         core0_system_task, NULL);
    if (ret < 0) {
        syslog_err("Failed to start system services: %d\n", ret);
    }

    syslog_info("ESP32-CAM Retro initialized\n");
}
