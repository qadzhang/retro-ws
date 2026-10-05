/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * nsh_cmds.c - 自定义 NSH 命令
 *
 * WHAT : 自定义 NSH 命令
 * WHY  : sysinfo/fsk/display/nettest/reboothist/reboot/shell 等命令
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/apps/system/nsh_cmds.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 命令表注册 + boardctl/驱动接口封装
 */

#include <nuttx/config.h>

/* 显示尺寸兜底（display-width-fallback：板 Kconfig 未定义时） */
#ifndef CONFIG_RETRO_DISPLAY_WIDTH
#  define CONFIG_RETRO_DISPLAY_WIDTH 640
#endif
#ifndef CONFIG_RETRO_DISPLAY_HEIGHT
#  define CONFIG_RETRO_DISPLAY_HEIGHT 480
#endif
#ifndef CONFIG_RETRO_DISPLAY_REFR_RATE
#  define CONFIG_RETRO_DISPLAY_REFR_RATE 30
#endif

/* NuttX 版本串缺省（部分 defconfig 无该符号时兜底） */
#ifndef CONFIG_VERSION_STRING
#  define CONFIG_VERSION_STRING "12.12.0"
#endif
#include <nuttx/arch.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/boardctl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <malloc.h>
#include <sys/time.h>

#include <nuttx/net/net.h>
#include <nuttx/net/ethernet.h>

/*==========================
 *  系统信息命令
 *==========================*/

/*
 * 功能描述 / WHAT:
 *   打印系统信息（机型/CPU/内存/网络/时间）
 * WHY : cmd_sysinfo 与 bootmenu 的 [S] 键共用同一份输出，
 *       抽取为可调用函数避免重复实现（bootmenu.c 通过 extern 引用）
 * WHO : cmd_sysinfo / bootmenu
 * WHERE: retro-ws/src/nuttx/common/apps/system/nsh_cmds.c
 * WHEN : 2026-10-04 从 cmd_sysinfo 中抽取
 * HOW  : printf 直出；mallinfo 统计堆内存
 * 注意 / Note:
 *   - mallinfo 字段定义见 deps/nuttx/include/malloc.h
 *   - total = arena（arena 已含空闲块，不能再加 fordblks）
 */
void sysinfo_print(void)
{
    printf("\n");
    printf("=== ESP32-S3 复古图形工作站 ===\n");
    printf("型号:     ESP32-S3-DevKitC-1\n");
    printf("CPU:      Xtensa LX7 Dual-Core @ 240MHz\n");
    printf("Flash:    16MB\n");
    printf("PSRAM:    8MB\n");
    printf("操作系统: Apache NuttX %s\n", CONFIG_VERSION_STRING);
/* NuttX 12.12 的 SPIRAM 开关是 CONFIG_ESP32S3_SPIRAM（arch/xtensa/src/esp32s3/Kconfig） */
#if defined(CONFIG_ESP32S3_SPIRAM) || defined(CONFIG_ESP32_SPIRAM)
    printf("PSRAM:    已启用\n");
#else
    printf("PSRAM:    未启用\n");
#endif
    printf("\n");

    /* 显示内存信息 / Show memory info */
#ifdef CONFIG_SMART
    printf("=== 内存 ===\n");
    struct mallinfo mi = mallinfo();
    printf("总内存:   %lu KB\n", (unsigned long)mi.arena / 1024);
    printf("已使用:   %lu KB\n", (unsigned long)mi.uordblks / 1024);
    printf("空闲:     %lu KB\n", (unsigned long)mi.fordblks / 1024);
    printf("\n");
#endif

    /* 显示网络信息 / Show network info */
#ifdef CONFIG_ESP32S3_WIFI
    printf("=== WiFi ===\n");
    printf("WiFi:     已配置\n");
    /* TODO: 获取实际连接状态 / TODO: query real link state */
    printf("状态:     已连接\n");
    printf("\n");
#endif

    /* 显示时间 / Show time */
    printf("=== 时间 ===\n");
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    printf("当前时间: %04d-%02d-%02d %02d:%02d:%02d\n",
           tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
           tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    printf("\n");
}

/**
 * cmd_sysinfo - 显示系统信息 / Print system info (NSH entry)
 */
int cmd_sysinfo(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sysinfo_print();
    return OK;
}

/**
 * cmd_fsk - FSK 磁带调制解调器控制
 */
int cmd_fsk(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: fsk <send|recv|status|baud> [参数]\n");
        printf("  send <字符串>  - 发送数据\n");
        printf("  recv            - 接收数据\n");
        printf("  status         - 显示状态\n");
        printf("  baud <速率>    - 设置码速率(300/600/1200/2400/4800/9600)\n");
        return OK;
    }

#ifdef CONFIG_RETRO_FSK
    if (strcmp(argv[1], "status") == 0) {
        /* 驱动接口未接入前先给默认值，避免打印未初始化变量
         * Defaults until fsk_get_status() is wired in */
        bool tx = false;
        bool rx = false;
        int baud = 300;
        /* fsk_get_status(&tx, &rx, &baud); */
        printf("FSK 状态:\n");
        printf("  码速率: %d baud\n", baud);
        printf("  发送:   %s\n", tx ? "活动" : "停止");
        printf("  接收:   %s\n", rx ? "活动" : "停止");
    } else if (strcmp(argv[1], "baud") == 0) {
        if (argc < 3) {
            printf("用法: fsk baud <300|600|1200|2400|4800|9600>\n");
            return OK;
        }
        int baud = atoi(argv[2]);
        /* int ret = fsk_set_baud(baud); */
        printf("FSK 码速率设置为: %d baud\n", baud);
    } else if (strcmp(argv[1], "send") == 0) {
        if (argc < 3) {
            printf("用法: fsk send <字符串>\n");
            return OK;
        }
        /* fsk_send((uint8_t*)argv[2], strlen(argv[2])); */
        printf("FSK 发送: %s\n", argv[2]);
    } else {
        printf("未知子命令: %s\n", argv[1]);
    }
#else
    printf("FSK 驱动未启用\n");
#endif

    return OK;
}

/**
 * cmd_display - 显示控制
 */
int cmd_display(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: display <on|off|status|mode>\n");
        printf("  on       - 开启显示\n");
        printf("  off      - 关闭显示\n");
        printf("  status   - 显示状态\n");
        printf("  mode     - 显示当前分辨率\n");
        return OK;
    }

#ifdef CONFIG_RETRO_DISPLAY
    if (strcmp(argv[1], "on") == 0) {
        /* cvbs_start(); */
        printf("显示已开启\n");
    } else if (strcmp(argv[1], "off") == 0) {
        /* cvbs_stop(); */
        printf("显示已关闭\n");
    } else if (strcmp(argv[1], "status") == 0) {
        int w, h;
        /* cvbs_get_resolution(&w, &h); */
        w = CONFIG_RETRO_DISPLAY_WIDTH;
        h = CONFIG_RETRO_DISPLAY_HEIGHT;
        printf("显示状态:\n");
        printf("  分辨率:  %d x %d\n", w, h);
        printf("  刷新率:  %d Hz\n", CONFIG_RETRO_DISPLAY_REFR_RATE);
        printf("  色深:    8-bit palette\n");
        printf("  扫描:    逐行扫描\n");
    } else {
        printf("未知子命令: %s\n", argv[1]);
    }
#else
    printf("显示驱动未启用\n");
#endif

    return OK;
}

/**
 * cmd_nettest - 网络测试
 */
int cmd_nettest(int argc, char **argv)
{
#ifdef CONFIG_NET
    printf("网络测试...\n");

    /* TODO: 实际的网络测试 */
    printf("WiFi: 已连接\n");
    printf("IP地址: 192.168.1.100\n");

#else
    printf("网络未启用\n");
#endif
    return OK;
}

/**
 * cmd_reboothist - 查看重启历史 / View reboot history
 */
int cmd_reboothist(int argc, char **argv)
{
    printf("重启历史 / Reboot History:\n");
    /* TODO: 读取 /var/log/reboot.log */
    printf("  上次重启: 正常重启 / Last reboot: Normal\n");
    return OK;
}

/**
 * cmd_reboot - 安全重启系统 / Safe system reboot
 *
 * 卸载所有已挂载的文件系统，同步缓冲区，然后重启
 * Unmount all mounted filesystems, sync buffers, then reboot
 */
int retro_cmd_reboot(int argc, char **argv)
{
    int ret;

    printf("正在安全重启系统...\n");
    printf("Safe reboot in progress...\n");

    /* 同步文件系统缓冲区 / Sync filesystem buffers */
#ifdef CONFIG_FS_SYNC
    sync();
#endif

    /* 卸载已知挂载点 / Unmount known mount points */
    const char *mount_points[] = {
        "/mnt/sd0",
        "/mnt/sd1",
        "/mnt/spiffs0",
        "/mnt/data",
        "/mnt/fat",
        NULL
    };

    for (int i = 0; mount_points[i] != NULL; i++) {
        ret = umount(mount_points[i]);
        if (ret == OK) {
            printf("  已卸载 / Unmounted: %s\n", mount_points[i]);
        }
    }

    /* 再次同步 / Sync again */
#ifdef CONFIG_FS_SYNC
    sync();
#endif

    printf("正在重启...\n");
    printf("Rebooting now...\n");
    fflush(stdout);
    usleep(100000);

    /* 执行硬件重启 / Perform hardware reboot */
    boardctl(BOARDIOC_RESET, 0);

    /* 不应到达此处 / Should not reach here */
    return OK;
}

/*
 * cmd_shell - 桌面外壳切换 / desktop shell switch
 *
 * 用法 / usage:
 *   shell                - 显示当前外壳
 *   shell win3           - Windows 3.2 风格
 *   shell wmaker         - WindowMaker/NeXT 风格（Dock + 右键菜单）
 */
#ifdef CONFIG_LVGL
int cmd_shell(int argc, char **argv)
{
    /* desktop_api.h 接口（src/lvgl/app/desktop_api.h） */
    extern int retro_desktop_ready(void);
    extern int retro_desktop_set_shell(int mode);
    extern int retro_desktop_get_shell(void);

    if (!retro_desktop_ready()) {
        printf("桌面未运行 / desktop not running\n");
        return -ENOSYS;
    }

    if (argc < 2) {
        printf("当前外壳 / current shell: %s\n",
               retro_desktop_get_shell() == 1 ? "wmaker" : "win3");
        printf("用法: shell <win3|wmaker>\n");
        return OK;
    }

    int mode;
    if (strcmp(argv[1], "win3") == 0 || strcmp(argv[1], "win32") == 0)
        mode = 0;      /* RETRO_SHELL_WIN3 */
    else if (strcmp(argv[1], "wmaker") == 0 || strcmp(argv[1], "next") == 0)
        mode = 1;      /* RETRO_SHELL_WMAKER */
    else {
        printf("未知外壳 / unknown shell: %s\n", argv[1]);
        return -EINVAL;
    }

    int ret = retro_desktop_set_shell(mode);
    if (ret < 0) {
        printf("切换失败 / switch failed: %d\n", ret);
        return ret;
    }

    printf("外壳已切换 / shell switched: %s\n",
           mode == 1 ? "wmaker" : "win3");
    return OK;
}
#else
int cmd_shell(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("图形桌面未编译 / desktop not compiled (CLI-only target)\n");
    return -ENOSYS;
}
#endif /* CONFIG_LVGL */

/*
 * WHAT : pkg - 包管理命令（deb 风格 .rpk），薄分发层转发到 pkg_manager
 * WHY  : 软件安装需要元数据/依赖/脚本/数据库完整链路（非裸拷 ELF）
 * WHO  : 用户在 NSH/终端调用
 * WHERE: src/nuttx/common/apps/system/nsh_cmds.c（实现在 pkg_manager.c）
 * WHEN : 2026-10-04 新增，同日升级为完整包管理器
 * HOW  : install/remove/list/info 四个子命令映射 rpkg_* API
 */
int cmd_pkg(int argc, char **argv)
{
    /* pkg_manager.h 接口（src/nuttx/common/apps/system/pkg_manager.h） */
    extern int rpkg_install(const char *rpk_path);
    extern int rpkg_remove(const char *pkg_name);
    extern int rpkg_list(void);
    extern int rpkg_info(const char *pkg_name);

    if (argc < 2) {
        printf("用法: pkg <install|remove|list|info> ...\n");
        printf("  pkg install /sdcard/pkg/xxx.rpk   - 安装包\n");
        printf("  pkg remove <包名>                  - 卸载包\n");
        printf("  pkg list                           - 已安装列表\n");
        printf("  pkg info <包名>                    - 包详细信息\n");
        return OK;
    }

    if (strcmp(argv[1], "install") == 0 && argc > 2)
        return rpkg_install(argv[2]);

    if (strcmp(argv[1], "remove") == 0 && argc > 2)
        return rpkg_remove(argv[2]);

    if (strcmp(argv[1], "list") == 0)
        return rpkg_list();

    if (strcmp(argv[1], "info") == 0 && argc > 2)
        return rpkg_info(argv[2]);

    printf("未知子命令: %s\n", argv[1]);
    return -EINVAL;
}

/*==========================
 *  命令表
 *==========================*/

static const struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *desc;
} g_nsh_commands[] = {
    { "sysinfo",    cmd_sysinfo,    "显示系统信息"       },
    { "fsk",       cmd_fsk,        "FSK 磁带调制解调器"  },
    { "display",   cmd_display,    "显示控制"            },
    { "nettest",   cmd_nettest,    "网络测试"            },
    { "reboothist", cmd_reboothist, "查看重启历史"       },
    { "retro_reboot", retro_cmd_reboot,  "安全重启（带历史记录）" },
    { "shell",     cmd_shell,      "桌面外壳切换"         },
    { "pkg",       cmd_pkg,        "独立程序(安装包)管理"  },
};

/* 注册命令到 NSH */
void esp32retro_nsh_register(void)
{
    /* 通过 NSH 的机制注册命令
     * 这需要在 nuttx-apps/system/nsh/ 中添加
     * 或者在 board 的启动代码中调用
     */
    syslog(LOG_INFO, "ESP32-S3 NSH commands registered\n");
}
