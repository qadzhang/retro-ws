/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * retro_boot.c - Retro WS 固件入口（四板共用）
 *
 * WHAT : CONFIG_INIT_ENTRYPOINT 指向本函数：先装配各板任务
 *        （双核分工），再交棒 NSH 控制台
 * WHY  : NuttX 只认一个 init 入口；项目任务必须在 NSH 前起好，
 *        同时不能丢 NSH（CLI 是所有档位的基础交互）
 * WHO  : NuttX 启动代码（CONFIG_INIT_ENTRYPOINT="retro_boot_main"）
 * WHERE: retro-ws/firmware/retro-apps/retro_boot.c（真身，
 *        同步到 deps/nuttx-apps/retro/）
 * WHEN : 2026-10-04 新增
 * HOW  : 按架构挑 <board>_retro_start() -> nsh_main()；
 *        板级 NuttX 符号（board_late_initialize 等）仍归
 *        deps/nuttx/boards，本入口是应用层装配点
 */

#include <nuttx/config.h>

#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#if defined(CONFIG_ARCH_CHIP_ESP32S3)
extern void esp32s3_retro_start(void);
#elif defined(CONFIG_ARCH_CHIP_ESP32)
extern void esp32_retro_start(void);
#elif defined(CONFIG_ARCH_CHIP_ESP32C3)
extern void esp32c3_retro_start(void);
#elif defined(CONFIG_ARCH_CHIP_RP2040)
extern void rp2040_retro_start(void);
#endif

extern int nsh_main(int argc, char *argv[]);

int retro_boot_main(int argc, char *argv[])
{
    syslog(LOG_INFO, "[retro] boot: CPU0=programs, CPU1=media (dual-core "
           "split where applicable)\n");

#ifdef CONFIG_RETRO_SCRIPTS
    /* 脚本引擎（Berry + my-basic 默认入 ROM）随启动初始化 */
    {
        extern int script_init(void);
        script_init();
    }
#endif

#ifdef CONFIG_RETRO_AV_CONSOLE
    /* AV 视频字符控制台（AGENTS.md 7.3：字符输出必须走 AV） */
    {
        extern int drv_cvbs_init(void);
        extern int cvbs_console_init(void);
        extern int cvbs_console_write(const char *, size_t);
        extern int cvbs_console_device_start(const char *);

        /* CLI 档无 RETRO_DISPLAY：这里兜底分配 CVBS 帧缓冲（幂等） */
#ifndef CONFIG_RETRO_DISPLAY
        drv_cvbs_init();
#endif

        if (cvbs_console_init() == 0) {
            cvbs_console_write("ESP32 Retro WS\n", 16);
            cvbs_console_write("AV 视频字符控制台就绪 / AV console ready\n", 39);

#ifdef CONFIG_RETRO_AV_INPUT
            /* NSH 跑 AV 屏：/dev/cvbscon + UART 键盘泵 */
            cvbs_console_device_start(NULL);
#endif
        }
    }
#endif

#ifdef CONFIG_RETRO_SCRIPTS_ROM
    /* 板级脚本 ROM 化：/rom/scripts 挂载（script 命令可执行） */
    {
        extern int retro_scripts_rom_mount(void);

        retro_scripts_rom_mount();
    }
#endif

#ifdef CONFIG_RETRO_PIO_USB
    /* PIO-USB 主机键盘（Pico）：PIO1 + 1ms 轮询任务（钉 CPU1，驱动内） */
    {
        extern int piousb_kbd_init(void);

        piousb_kbd_init();
    }
#endif

#if defined(CONFIG_ARCH_CHIP_ESP32S3)
    esp32s3_retro_start();
#elif defined(CONFIG_ARCH_CHIP_ESP32)
    esp32_retro_start();
#elif defined(CONFIG_ARCH_CHIP_ESP32C3)
    esp32c3_retro_start();
#elif defined(CONFIG_ARCH_CHIP_RP2040)
    rp2040_retro_start();
#endif

#ifdef CONFIG_RETRO_WIFI
    /* 网络层（2026-10-05 全量编入）：按 /opt/etc/network.conf 自动连网
     * （无配置静默跳过，首配经 `wifi connect`）；NTP 周期对时启动 */
    {
        extern int wifi_auto_connect(void);
        extern int ntp_sync_start(void);

        wifi_auto_connect();
        ntp_sync_start();
    }
#endif

    /* 交棒 NSH：控制台/程序运行都在 CPU0（程序核） */
    return nsh_main(argc, argv);
}
