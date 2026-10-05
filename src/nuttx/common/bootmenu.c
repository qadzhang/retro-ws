/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * bootmenu.c - 启动菜单
 *
 * WHAT : 启动菜单
 * WHY  : 上电 3 秒倒计时选 GUI/CLI；连续 3 次 WDT 重启进安全模式
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/bootmenu.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : bootctl 读取重启记录，倒计时超时按默认模式引导
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/fs/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/boardctl.h>

/*==========================
 *  启动模式
 *==========================*/

typedef enum {
    BOOT_MODE_GUI = 0,   /* 图形桌面模式（默认）*/
    BOOT_MODE_CLI = 1,   /* 命令行模式（NSH）*/
    BOOT_MODE_SAFE = 2,   /* 安全模式（CLI + 禁用非核心功能）*/
    BOOT_MODE_COUNT
} boot_mode_t;

/*==========================
 *  常量
 *==========================*/

#define BOOT_MENU_TIMEOUT_MS   3000   /* 3 秒倒计时 */
#define BOOT_MENU_SERIAL_BAUD  115200

#define BOOT_CONFIG_FILE  "/var/boot.cfg"

/*==========================
 *  全局变量
 *==========================*/

static boot_mode_t g_default_boot_mode = BOOT_MODE_GUI;
static boot_mode_t g_current_boot_mode = BOOT_MODE_GUI;
static bool g_boot_menu_enabled = true;
static bool g_safe_mode = false;
static uint32_t g_consecutive_wdt_resets = 0;

/*==========================
 *  Boot Config 存储
 *==========================*/

/**
 * 从 Flash 加载启动配置
 */
static int boot_load_config(void)
{
    int fd = open(BOOT_CONFIG_FILE, O_RDONLY);
    if (fd < 0) {
        /* 没有配置文件，使用默认值 */
        g_default_boot_mode = BOOT_MODE_GUI;
        return 0;
    }

    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0)
        return -EIO;

    buf[n] = '\0';

    /* 解析配置: "mode=gui" 或 "mode=cli" */
    if (strstr(buf, "mode=gui"))
        g_default_boot_mode = BOOT_MODE_GUI;
    else if (strstr(buf, "mode=cli"))
        g_default_boot_mode = BOOT_MODE_CLI;
    else if (strstr(buf, "mode=safe"))
        g_default_boot_mode = BOOT_MODE_SAFE;

    return OK;
}

/**
 * 保存启动配置到 Flash
 */
static int boot_save_config(void)
{
    /* 确保目录存在 */
    mkdir("/var", 0755);

    int fd = open(BOOT_CONFIG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -EIO;

    const char *mode_str;
    switch (g_default_boot_mode) {
        case BOOT_MODE_GUI:  mode_str = "mode=gui"; break;
        case BOOT_MODE_CLI: mode_str = "mode=cli"; break;
        case BOOT_MODE_SAFE: mode_str = "mode=safe"; break;
        default:             mode_str = "mode=gui"; break;
    }

    dprintf(fd, "# ESP32-S3 Retro Boot Configuration\n");
    dprintf(fd, "# Last updated: %lu\n", (unsigned long)time(NULL));
    dprintf(fd, "%s\n", mode_str);
    dprintf(fd, "countdown=%d\n", BOOT_MENU_TIMEOUT_MS / 1000);

    close(fd);
    return OK;
}

/*==========================
 *  启动模式切换
 *==========================*/

/**
 * 设置默认启动模式
 */
int boot_set_default_mode(boot_mode_t mode)
{
    if (mode >= BOOT_MODE_COUNT)
        return -EINVAL;

    g_default_boot_mode = mode;
    g_current_boot_mode = mode;

    int ret = boot_save_config();
    if (ret < 0) {
        syslog(LOG_ERR, "[BOOT] Failed to save config: %d\n", ret);
        return ret;
    }

    const char *names[] = { "GUI", "CLI", "SAFE" };
    syslog(LOG_INFO, "[BOOT] Default boot mode set to: %s\n", names[mode]);
    return OK;
}

/**
 * 获取当前启动模式
 */
boot_mode_t boot_get_mode(void)
{
    return g_current_boot_mode;
}

/**
 * 检查是否为安全模式
 */
bool boot_in_safe_mode(void)
{
    return g_safe_mode;
}

/*==========================
 *  看门狗重启计数
 *==========================*/

/**
 * 记录看门狗重启
 */
void boot_record_wdt_reset(void)
{
    g_consecutive_wdt_resets++;

    syslog(LOG_WARNING, "[BOOT] WDT reset #%lu\n",
           (unsigned long)g_consecutive_wdt_resets);

    /* 连续 3 次看门狗重启，自动进入安全模式 */
    if (g_consecutive_wdt_resets >= 3) {
        syslog(LOG_ERR, "[BOOT] Entering SAFE mode (consecutive WDT resets)\n");
        g_safe_mode = true;
        g_current_boot_mode = BOOT_MODE_SAFE;
        boot_set_default_mode(BOOT_MODE_SAFE);
    }
}

/**
 * 清除看门狗重启计数（正常重启时调用）
 */
void boot_clear_wdt_count(void)
{
    g_consecutive_wdt_resets = 0;
}

/*==========================
 *  启动菜单显示
 *==========================*/

/**
 * 打印启动菜单
 */
static void boot_menu_print(int countdown)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     ESP32-S3 复古联网图形工作站 启动菜单          ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║                                                      ║\n");
    printf("║   [1] 启动图形桌面 (GUI)   - Windows 3.2 风格     ║\n");
    printf("║   [2] 启动命令行界面 (CLI) - NSH NuttShell       ║\n");
    printf("║   [3] 安全模式 (SAFE)       - 仅 CLI，禁用图形     ║\n");
    printf("║                                                      ║\n");
    printf("║   [G] 设置默认启动模式                          ║\n");
    printf("║   [S] 系统信息                                    ║\n");
    printf("║   [R] 恢复出厂设置                                ║\n");
    printf("║                                                      ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("默认模式将在 %d 秒后启动: ", countdown);

    switch (g_default_boot_mode) {
        case BOOT_MODE_GUI:  printf("[GUI]\n"); break;
        case BOOT_MODE_CLI:  printf("[CLI]\n"); break;
        case BOOT_MODE_SAFE: printf("[SAFE]\n"); break;
        default:             printf("[GUI]\n"); break;
    }

    printf("按对应数字键选择，或按 Enter 使用默认模式...\n\n");
}

/**
 * 显示启动菜单并等待用户选择
 *
 * @return 用户选择的启动模式
 */
static boot_mode_t boot_menu_wait(void)
{
    /* 如果禁用了启动菜单，直接返回默认模式 */
    if (!g_boot_menu_enabled)
        return g_default_boot_mode;

    /* 如果是安全模式，直接进入 */
    if (g_safe_mode) {
        printf("[SAFE MODE] Starting in safe mode (CLI only)...\n");
        return BOOT_MODE_SAFE;
    }

    /* 打印启动菜单 */
    int countdown = BOOT_MENU_TIMEOUT_MS / 1000;

    for (int i = countdown; i > 0; i--) {
        boot_menu_print(i);

        /* 使用 poll 实现带超时的输入检测，替代 fgetc(stdin)
         * fgetc 在 NuttX 非阻塞环境下可能不工作 */
        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        /* 等待 1 秒或直到有输入 */
        int pret = poll(&pfd, 1, 1000);
        if (pret <= 0) {
            /* 超时或出错，继续倒计时 */
            continue;
        }

        /* 有输入，读取字符 */
        char ch;
        ssize_t rn = read(STDIN_FILENO, &ch, 1);
        if (rn <= 0)
            continue;

        int c = ch;
        if (c == '1') {
            printf("\n[USER] Selected: GUI mode\n");
            return BOOT_MODE_GUI;
        } else if (c == '2') {
            printf("\n[USER] Selected: CLI mode\n");
            return BOOT_MODE_CLI;
        } else if (c == '3') {
            printf("\n[USER] Selected: SAFE mode\n");
            return BOOT_MODE_SAFE;
        } else if (c == 'g' || c == 'G') {
            printf("\n[USER] Setting default mode...\n");
            /* 打印模式选择 */
            printf("  1 = GUI, 2 = CLI, 3 = SAFE\n");
            /* 等待模式选择输入（5秒超时）*/
            struct pollfd pfd2;
            pfd2.fd = STDIN_FILENO;
            pfd2.events = POLLIN;
            if (poll(&pfd2, 1, 5000) > 0) {
                char m_ch;
                if (read(STDIN_FILENO, &m_ch, 1) > 0 && m_ch >= '1' && m_ch <= '3') {
                    boot_set_default_mode((boot_mode_t)(m_ch - '1'));
                }
            }
            return g_default_boot_mode;
        } else if (c == 's' || c == 'S') {
            printf("\n[SYSTEM INFO]\n");
            extern void sysinfo_print(void);
            sysinfo_print();
            i++;  /* 不减少倒计时 */
            continue;
        } else if (c == 'r' || c == 'R') {
            printf("\n[RESET] Restoring factory settings...\n");
            unlink(BOOT_CONFIG_FILE);
            g_default_boot_mode = BOOT_MODE_GUI;
            g_consecutive_wdt_resets = 0;
            g_safe_mode = false;
            printf("Factory settings restored. Rebooting...\n");
            /* 使用 boardctl 执行软件复位，而非不存在的 esp32s3_software_reset() */
            boardctl(BOARDIOC_RESET, 0);
            while (1);  /* 等待复位 */
        } else if (c == '\n' || c == '\r') {
            /* 回车，使用默认模式 */
            break;
        }
        /* 其他字符忽略，继续倒计时 */
    }

    printf("\n[TIMEOUT] Starting with default mode: ");
    switch (g_default_boot_mode) {
        case BOOT_MODE_GUI:  printf("GUI\n"); break;
        case BOOT_MODE_CLI:  printf("CLI\n"); break;
        case BOOT_MODE_SAFE: printf("SAFE\n"); break;
        default:             printf("GUI\n"); break;
    }

    return g_default_boot_mode;
}

/*==========================
 *  启动序列
 *==========================*/

/**
 * 执行启动序列
 *
 * 在 NuttX 内核启动后立即调用
 */
int boot_sequence(void)
{
    syslog(LOG_INFO, "[BOOT] Boot sequence starting...\n");

    /* 加载启动配置 */
    int ret = boot_load_config();
    if (ret < 0) {
        syslog(LOG_WARNING, "[BOOT] No boot config, using defaults\n");
    }

    /* 检查看门狗重启原因 */
    extern uint32_t board_get_reset_reason(void);
    extern bool board_is_watchdog_reset(void);
    if (board_is_watchdog_reset()) {
        boot_record_wdt_reset();
    } else {
        /* 正常重启，清除计数 */
        boot_clear_wdt_count();
    }

    /* 打印启动 Banner */
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║     ESP32-S3 复古联网图形工作站                      ║\n");
    printf("║     NuttX %-15s  |  LVGL %s     ║\n",
           CONFIG_VERSION_STRING, "9.x");
    printf("║     Build: %s    %s           ║\n", __DATE__, __TIME__);
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* 如果是安全模式，直接进入 CLI */
    if (g_safe_mode) {
        printf("[SAFE MODE] System started in safe mode.\n");
        printf("[SAFE MODE] Only CLI is available. Other features are disabled.\n");
        printf("[SAFE MODE] To exit safe mode, run 'bootmode gui' or 'recovery'\n");
        printf("\n");
        g_current_boot_mode = BOOT_MODE_SAFE;
        return BOOT_MODE_SAFE;
    }

    /* 显示启动菜单 */
    g_current_boot_mode = boot_menu_wait();

    /* 根据启动模式启动对应服务 */
    switch (g_current_boot_mode) {
        case BOOT_MODE_GUI:
            syslog(LOG_INFO, "[BOOT] Starting GUI mode...\n");
            /* 启动 LVGL 图形桌面 */
            extern int retro_desktop_start(void);
            retro_desktop_start();
            break;

        case BOOT_MODE_CLI:
            /* TODO(设计/Design): 本函数运行在 NSH 会话自身的上下文中，
             * nshlib 不导出可重入的 nsh_main_loop()（链接未定义符号）；
             * CLI/SAFE 模式即当前 NSH 会话本身，此处仅记录模式后返回
             * This runs inside the NSH session itself; nshlib exports no
             * re-enterable nsh_main_loop(). CLI/SAFE mode IS this session. */
            syslog(LOG_INFO, "[BOOT] CLI mode: staying in current NSH session\n");
            break;

        case BOOT_MODE_SAFE:
            /* 同上 / Same as above: safe mode keeps the NSH session */
            syslog(LOG_INFO, "[BOOT] SAFE mode: staying in current NSH session\n");
            break;

        default:
            syslog(LOG_ERR, "[BOOT] Unknown boot mode %d, falling back to GUI\n",
                   g_current_boot_mode);
            g_current_boot_mode = BOOT_MODE_GUI;
            break;
    }

    return g_current_boot_mode;
}

/*==========================
 *  启动模式 NSH 命令
 *==========================*/

/**
 * bootmode - 设置/显示启动模式
 */
int cmd_bootmode(int argc, char **argv)
{
    if (argc < 2) {
        /* 显示当前模式 */
        const char *mode_names[] = { "gui", "cli", "safe" };
        const char *mode_desc[] = {
            "GUI (图形桌面)",
            "CLI (命令行 NSH)",
            "SAFE (安全模式)"
        };

        printf("Current boot mode: %s\n", mode_names[g_current_boot_mode]);
        printf("Description: %s\n", mode_desc[g_current_boot_mode]);
        printf("Default mode: %s\n", mode_names[g_default_boot_mode]);
        printf("Safe mode: %s\n", g_safe_mode ? "ENABLED" : "disabled");
        printf("Consecutive WDT resets: %lu\n",
               (unsigned long)g_consecutive_wdt_resets);
        printf("\n");
        printf("Usage: bootmode <gui|cli|safe|status>\n");
        return OK;
    }

    if (strcmp(argv[1], "gui") == 0) {
        boot_set_default_mode(BOOT_MODE_GUI);
        printf("Default boot mode set to GUI.\n");
        printf("Reboot to apply, or run 'bootmode now gui' to switch immediately.\n");
    } else if (strcmp(argv[1], "cli") == 0) {
        boot_set_default_mode(BOOT_MODE_CLI);
        printf("Default boot mode set to CLI.\n");
    } else if (strcmp(argv[1], "safe") == 0) {
        boot_set_default_mode(BOOT_MODE_SAFE);
        printf("Default boot mode set to SAFE.\n");
    } else if (strcmp(argv[1], "now") == 0) {
        if (argc < 3) {
            printf("Usage: bootmode now <gui|cli>\n");
            return OK;
        }
        if (strcmp(argv[2], "gui") == 0) {
            printf("Switching to GUI mode (requires restart for full effect)...\n");
            g_current_boot_mode = BOOT_MODE_GUI;
        } else if (strcmp(argv[2], "cli") == 0) {
            /* TODO(设计/Design): 本命令在 NSH 会话内执行，无法重入
             * nshlib 的控制台循环（nsh_main_loop 未导出，链接失败）
             * Cannot re-enter the NSH console loop from inside it */
            printf("Already in CLI (NSH) session; mode flag updated.\n");
            g_current_boot_mode = BOOT_MODE_CLI;
            syslog(LOG_WARNING, "[BOOT] nsh console loop cannot be re-entered\n");
            return -ENOSYS;
        }
    } else if (strcmp(argv[1], "status") == 0) {
        cmd_bootmode(1, argv);
    } else {
        printf("Unknown option: %s\n", argv[1]);
        printf("Usage: bootmode <gui|cli|safe|status>\n");
    }

    return OK;
}

/**
 * recovery - 恢复出厂设置并退出安全模式
 */
int cmd_recovery(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Recovery: Restoring factory settings...\n");

    /* 删除所有配置文件 */
    unlink(BOOT_CONFIG_FILE);
    unlink("/var/log/reboot.log");

    /* 重置看门狗计数 */
    g_consecutive_wdt_resets = 0;
    g_safe_mode = false;

    /* 恢复默认启动模式 */
    g_default_boot_mode = BOOT_MODE_GUI;
    g_current_boot_mode = BOOT_MODE_GUI;

    printf("Factory settings restored.\n");
    printf("Please reboot the system.\n");

    return OK;
}

/**
 * 重启系统
 */
int cmd_reboot_boot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Rebooting...\n");
    usleep(100000);
    boardctl(BOARDIOC_RESET, 0);
    while (1);
}
