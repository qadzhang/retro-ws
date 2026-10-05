/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * esp32s3_retro.c - ESP32-S3 主入口（开发模板）
 *
 * WHAT : ESP32-S3 双核任务装配（Core0 图形音频 / Core1 系统网络）
 * WHY  : 复古工作站的显示实时性与网络后台业务分核隔离
 * WHO  : board.c 的 board_late_initialize() 启动本模块任务
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/esp32s3_retro.c
 * WHEN : 2026-03~04 初版；2026-10-04 修订（去除与 board.c 的重复
 *        符号、WDT 溢出、未定义外部函数，见 BUILD_FIXES.md）
 * HOW  : 双核分工（全局规范，2026-10-04 用户确定）：
 *        CPU0 = 程序运行（NSH/脚本引擎，天然 PRO CPU 上下文）
 *        CPU1 = 媒体核（图形/视频/音频/文件 IO 任务，亲和性钉核）
 *        board_late_initialize() -> esp32s3_retro_start() ->
 *        kthread_create(media) + sched_setaffinity(CPU1)；
 *        板级符号（reset reason/version）唯一归属于 board.c
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
#include <nuttx/irq.h>
#include <sys/boardctl.h>

#include <stdbool.h>
#include <stdint.h>

#include "esp32s3.h"
#include "board.h"

/*==========================
 *  日志
 *==========================*/

#define syslog_err(fmt, ...)      syslog(LOG_ERR,     fmt, ##__VA_ARGS__)
#define syslog_info(fmt, ...)     syslog(LOG_INFO,    fmt, ##__VA_ARGS__)

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

/*==========================
 *  公开变量
 *==========================*/

volatile bool g_system_running = false;
volatile bool g_gui_running = false;

/*==========================
 *  看门狗（Timer Group MWDT，寄存器级）
 *==========================*/

#ifdef CONFIG_WATCHDOG

/* ESP32-S3 Timer Group WDT 基地址 */
#define WDT0_BASE  (ESP32S3_TIMERGROUP0_BASE + ESP32S3_TIMG_WDTCONFIG0_OFFSET)
#define WDT1_BASE  (ESP32S3_TIMERGROUP1_BASE + ESP32S3_TIMG_WDTCONFIG0_OFFSET)

/* WDTCONFIG2 计数器 32bit @APB 80MHz：最长约 53.7s */
#define WDT_MAX_TIMEOUT_MS  53000

/*
 * WHAT : 初始化某组 MWDT / init one timer-group watchdog
 * WHY  : 任务级喂狗防死循环；阶段0中断、阶段1复位
 * HOW  : 解锁(WPROTECT) -> 写阶段超时 -> 使能 -> 上锁；
 *        超时换算用 64 位乘法防 uint32 溢出，超限钳到上限并告警
 */
void esp32s3_wdt_init(int wdt_id, uint32_t timeout_ms)
{
    uint32_t base;
    switch (wdt_id) {
        case 0:  base = WDT0_BASE; break;
        case 1:  base = WDT1_BASE; break;
        default: return;
    }

    if (timeout_ms > WDT_MAX_TIMEOUT_MS) {
        syslog(LOG_WARNING,
               "[WDT%d] timeout %lu > max, clamped to %d ms\n",
               wdt_id, (unsigned long)timeout_ms, WDT_MAX_TIMEOUT_MS);
        timeout_ms = WDT_MAX_TIMEOUT_MS;
    }

    /* 解锁 WDT */
    putreg32(ESP32S3_WDT_WKEY_VALUE,
             base + ESP32S3_TIMG_WDTWPROTECT_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);

    /* 设置阶段0超时（APB 时钟周期，64 位乘法防溢出） */
    uint64_t ticks64 = (uint64_t)timeout_ms * (80000000 / 1000);
    uint32_t ticks = (uint32_t)ticks64;
    putreg32(ticks, base + ESP32S3_TIMG_WDTCONFIG2_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);
    /* 设置阶段1超时（2x，系统复位） */
    putreg32(ticks * 2, base + ESP32S3_TIMG_WDTCONFIG3_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);

    /* 使能 WDT: 阶段0=中断, 阶段1=系统复位 */
    putreg32(0x03 | (0x03 << 2) | ESP32S3_WDT_EN, base);

    /* 锁定 WDT */
    putreg32(0, base + ESP32S3_TIMG_WDTWPROTECT_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);

    syslog_info("WDT%d initialized, timeout=%ums\n", wdt_id, timeout_ms);
}

/*
 * WHAT : 喂狗 / feed one timer-group watchdog
 * HOW  : 解锁 -> 写 FEED -> 上锁
 */
void esp32s3_wdt_feed(int wdt_id)
{
    uint32_t base;
    switch (wdt_id) {
        case 0:  base = WDT0_BASE; break;
        case 1:  base = WDT1_BASE; break;
        default: return;
    }

    putreg32(ESP32S3_WDT_WKEY_VALUE,
             base + ESP32S3_TIMG_WDTWPROTECT_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);
    putreg32(0, base + ESP32S3_TIMG_WDTFEED_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);
    putreg32(0, base + ESP32S3_TIMG_WDTWPROTECT_OFFSET - ESP32S3_TIMG_WDTCONFIG0_OFFSET);
}
#endif /* CONFIG_WATCHDOG */

/*==========================
 *  软件复位
 *==========================*/

void board_software_reset(void)
{
    /* ESP32-S3 通过 RTC_CNTL_SW_SYS_RESET 触发软件复位 */
    putreg32(ESP32S3_RTC_CNTL_SW_SYS_RESET, ESP32S3_RTC_CNTL_OPTIONS0_REG);
    while (1)
        ;
}

/*==========================
 *  Core 1 媒体核（图形+视频+音频+文件IO）
 *==========================*/

/*
 * WHAT : 媒体核任务 / media core task
 * WHY  : 双核分工规范：CPU0 留给程序（NSH/脚本），本核独占
 *        LVGL/CVBS/音频/SD 文件服务的实时性
 * HOW  : sched_setaffinity 钉 CPU1；WDT1 看护；图形->音频->文件
 *        依次初始化后进入 lv_timer_handler 主循环
 */
static int core1_media_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog_info("[Core1] media task (graphics/video/audio/file-IO) started\n");

    /* 钉到 CPU1：与程序核（CPU0 上的 NSH/脚本）分离 */
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
    }
#endif

    /* 初始化看门狗（本核用 WDT1） */
#ifdef CONFIG_WATCHDOG
    esp32s3_wdt_init(1, 10000);
#endif

    /* 初始化显示（统一 CVBS 驱动：帧缓冲 + PAL 场时序核心） */
#ifdef CONFIG_RETRO_DISPLAY
    syslog_info("[Core0] Initializing CVBS display...\n");
    {
        extern int drv_cvbs_init(void);
        drv_cvbs_init();
    }
#endif

    /* 初始化 LVGL */
#ifdef CONFIG_LVGL
    syslog_info("[Core0] Initializing LVGL...\n");
    {
        extern void lvgl_init(void);
        extern void retro_desktop_init(void);

        lvgl_init();
        retro_desktop_init();
    }
#endif

    /* 初始化音频 */
#ifdef CONFIG_RETRO_AUDIO
    syslog_info("[Core0] Initializing audio...\n");
    {
        extern int audio_init(void);
        audio_init();
    }
#endif

    /* WS2812 状态灯自检（RMT 硬件外设，蓝色=系统存活，HARDWARE.md 2.8） */
#ifdef CONFIG_ESP_RMT
    {
        extern int ws2812_rmt_init(const char *devpath);
        int ret = ws2812_rmt_init(NULL);
        if (ret == 0)
            syslog_info("[Core0] WS2812 status LED ready (RMT)\n");
        else
            syslog_info("[Core0] WS2812 not available: %d\n", ret);
    }
#endif

    g_gui_running = true;

    /* 主循环 */
    while (g_system_running) {
#ifdef CONFIG_WATCHDOG
        esp32s3_wdt_feed(1);
#endif

#ifdef CONFIG_LVGL
        {
            extern void lvgl_task_handler(void);
            lvgl_task_handler();
        }
#endif

        usleep(30000);
    }

    syslog_info("[Core0] Graphics task exiting\n");
    return 0;
}

/*==========================
 *  CPU0 系统服务（网络/对时/定时/输入，程序核的常驻服务）
 *==========================*/

static int core0_system_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog_info("[Core0] system services task started\n");

    /* 初始化看门狗（程序核用 WDT0） */
#ifdef CONFIG_WATCHDOG
    esp32s3_wdt_init(0, 10000);
#endif

    /* 连接 WiFi（SSID 宏依赖 RETRO_WIFI 打开，双重条件防未定义标识符） */
#if defined(CONFIG_ESP32S3_WIFI) && defined(CONFIG_RETRO_WIFI_SSID)
    syslog_info("[Core1] Connecting to WiFi...\n");
    {
        extern int wifi_connect(const char *ssid, const char *pass);
        wifi_connect(CONFIG_RETRO_WIFI_SSID, CONFIG_RETRO_WIFI_PASSWORD);
    }
#endif

    /* NTP 对时 */
#ifdef CONFIG_RETRO_NTP
    syslog_info("[Core1] Starting NTP sync...\n");
    {
        extern int ntp_sync_start(void);
        ntp_sync_start();
    }
#endif

    /* 启动 Cron */
#ifdef CONFIG_RETRO_CRON
    syslog_info("[Core1] Starting cron...\n");
    {
        extern int cron_init(void);
        cron_init();
    }
#endif

    /* BLE HID 键鼠（ESP32-S3 支持 USB HID + BLE HID 双模） */
#ifdef CONFIG_RETRO_INPUT_BLE_HID
    syslog_info("[Core1] Initializing BLE HID...\n");
    {
        extern int ble_hid_init(void);
        ble_hid_init();
    }
#endif

    syslog_info("[Core0] system services running\n");

    /* 主循环：喂狗 + 等待 */
    while (g_system_running) {
#ifdef CONFIG_WATCHDOG
        esp32s3_wdt_feed(0);
#endif
        usleep(100000);
    }

    syslog_info("[Core0] system services exiting\n");
    return 0;
}

/*==========================
 *  任务装配 / called from board.c board_late_initialize()
 *==========================*/

/*
 * WHAT : 启动双核任务 / spawn core0 + core1 tasks
 * WHY  : NuttX 板级后期初始化是统一的装配点（符号归属 board.c，
 *        本函数只做任务创建，消除原先两处 board_late_initialize 冲突）
 * HOW  : kthread_create 两任务，失败仅告警（NSH 仍可用）
 */
void esp32s3_retro_start(void)
{
    int ret;

    g_system_running = true;

    /* 媒体核（CPU1）：图形/视频/音频/文件IO */
    ret = kthread_create("media", CORE1_TASK_PRI,
                         CORE1_TASK_STACK,
                         core1_media_task, NULL);
    if (ret < 0)
        syslog_err("Failed to start media task: %d\n", ret);

    /* 程序核常驻服务（CPU0）：网络/对时/定时/输入 */
    ret = kthread_create("syssvc", CORE0_TASK_PRI,
                         CORE0_TASK_STACK,
                         core0_system_task, NULL);
    if (ret < 0)
        syslog_err("Failed to start system services: %d\n", ret);

    syslog_info("ESP32-S3 Retro started (CPU0=programs, CPU1=media)\n");
}
