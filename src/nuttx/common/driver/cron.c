/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cron.c - Cron 定时任务
 *
 * WHAT : Cron 定时任务
 * WHY  : crontab 格式定时执行（shell/audio/tts/notify/reboot）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/cron.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 分钟粒度扫描 /sdcard/etc/crontab，日志写 /sdcard/logs
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/kthread.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/boardctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <limits.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_CRON
#  define CONFIG_RETRO_CRON 1
#endif

#if CONFIG_RETRO_CRON

/* crontab 文件路径 */
#ifndef CONFIG_CRON_CRONTAB
#  define CONFIG_CRON_CRONTAB "/sdcard/etc/crontab"
#endif

/* Cron 日志路径 */
#ifndef CONFIG_CRON_LOG
#  define CONFIG_CRON_LOG "/sdcard/logs/cron.log"
#endif

#define CRON_MAX_ENTRIES    32
#define CRON_MAX_LINE_LEN   256
#define CRON_TASK_STACK     4096  /* Cron 任务栈 / cron task stack */
#define CRON_FILE_MAX       (CRON_MAX_ENTRIES * CRON_MAX_LINE_LEN)

/*==========================
 *  Cron 任务条目
 *==========================*/

typedef enum {
    CRON_CMD_NONE = 0,
    CRON_CMD_SHELL,      /* 执行 NSH 命令 */
    CRON_CMD_SCRIPT,     /* 执行脚本 */
    CRON_CMD_AUDIO,      /* 播放音频 */
    CRON_CMD_TTS,        /* TTS 播报 */
    CRON_CMD_NOTIFY,     /* 显示通知 */
    CRON_CMD_REBOOT,     /* 重启 */
    CRON_CMD_WIFI        /* WiFi 重连 */
} cron_cmd_type_t;

struct cron_entry {
    bool         enabled;    /* 是否启用 */
    bool         active;     /* 是否正在运行 */

    /* 时间字段（crontab 格式，-1 表示任意）*/
    int8_t       minute;      /* 0-59, -1=任意 */
    int8_t       hour;        /* 0-23, -1=任意 */
    int8_t       day;         /* 1-31, -1=任意 */
    int8_t       month;       /* 1-12, -1=任意 */
    int8_t       weekday;     /* 0-6 (0=周日), -1=任意 */

    cron_cmd_type_t type;     /* 命令类型 */
    char          command[CRON_MAX_LINE_LEN];  /* 命令内容 */
    char          desc[64];   /* 描述 */

    /* 执行统计 */
    uint32_t      exec_count;  /* 执行次数 */
    uint32_t      last_run;    /* 上次执行时间 */
    time_t        last_success; /* 上次成功时间 */
};

static struct cron_entry g_entries[CRON_MAX_ENTRIES];
static int g_entry_count = 0;

/* Cron 运行状态 */
static bool g_cron_running = false;
static bool g_cron_paused = false;
static pid_t g_cron_pid = 0;

/*==========================
 *  工具函数
 *==========================*/

/**
 * 将字段字符串转换为数值（支持 *）
 * 返回值：数值或 -1（表示任意）
 */
static int parse_field(const char *str, int min, int max)
{
    if (strcmp(str, "*") == 0)
        return -1;

    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0' || val < min || val > max)
        return -2;  /* 无效 */

    return (int)val;
}

/**
 * 检查当前时间是否匹配条目
 */
static bool cron_match(const struct cron_entry *entry)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    /* 分钟 */
    if (entry->minute >= 0 && entry->minute != tm_now.tm_min)
        return false;

    /* 小时 */
    if (entry->hour >= 0 && entry->hour != tm_now.tm_hour)
        return false;

    /* 日 */
    if (entry->day >= 0 && entry->day != tm_now.tm_mday)
        return false;

    /* 月 */
    if (entry->month >= 0 && entry->month != tm_now.tm_mon + 1)
        return false;

    /* 星期 */
    if (entry->weekday >= 0 && entry->weekday != tm_now.tm_wday)
        return false;

    return true;
}

/**
 * 记录 Cron 日志
 */
static void cron_log(const char *level, const char *fmt, ...)
{
    static char log_buf[512];
    va_list va;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_now);

    va_start(va, fmt);
    vsnprintf(log_buf, sizeof(log_buf), fmt, va);
    va_end(va);

    /* 打印到控制台 */
    printf("[CRON] [%s] %s: %s\n", level, time_str, log_buf);

    /* 写入日志文件 */
#ifdef CONFIG_FS_FAT
    int fd = open(CONFIG_CRON_LOG, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        dprintf(fd, "[%s] %s: %s\n", level, time_str, log_buf);
        close(fd);
    }
#endif

    /* 写入 syslog */
    if (strcmp(level, "ERROR") == 0)
        syslog(LOG_ERR, "[CRON] %s\n", log_buf);
    else if (strcmp(level, "WARN") == 0)
        syslog(LOG_WARNING, "[CRON] %s\n", log_buf);
    else
        syslog(LOG_INFO, "[CRON] %s\n", log_buf);
}

/*==========================
 *  命令执行
 *==========================*/

/**
 * 执行 Cron 条目命令
 */
static int cron_exec_entry(struct cron_entry *entry)
{
    int ret = OK;

    if (entry->active)
        return 0;  /* 正在执行，跳过 */

    entry->active = true;
    entry->last_run = (uint32_t)time(NULL);
    entry->exec_count++;

    cron_log("INFO", "Executing: %s", entry->command);

    switch (entry->type) {
        case CRON_CMD_SHELL: {
            /* 执行 NSH 命令 */
            /* TODO: 使用 nsh_execute 或类似接口 */
            syslog(LOG_INFO, "[CRON] Shell: %s\n", entry->command);
            break;
        }

        case CRON_CMD_SCRIPT: {
            /* 执行脚本 */
            syslog(LOG_INFO, "[CRON] Script: %s\n", entry->command);
            /* TODO: fork + exec 执行脚本 */
            break;
        }

        case CRON_CMD_AUDIO: {
            /* 播放音频文件 */
            syslog(LOG_INFO, "[CRON] Audio: %s\n", entry->command);
            /* TODO: 调用音频播放接口 */
            break;
        }

        case CRON_CMD_TTS: {
            /* TTS 播报 */
            syslog(LOG_INFO, "[CRON] TTS: %s\n", entry->command);
            /* TODO: 调用 TTS 接口 */
            break;
        }

        case CRON_CMD_NOTIFY: {
            /* 显示桌面通知 */
            extern void desktop_notify(const char *msg);
            desktop_notify(entry->command);
            break;
        }

        case CRON_CMD_REBOOT: {
            cron_log("INFO", "Rebooting system...");
            /* 使用 boardctl 执行软件复位，而非不存在的 esp32s3_software_reset() */
            boardctl(BOARDIOC_RESET, 0);
            break;
        }

        case CRON_CMD_WIFI: {
            /* WiFi 重连 */
            extern int wifi_reconnect(void);
            wifi_reconnect();
            break;
        }

        default:
            ret = -EINVAL;
            break;
    }

    entry->active = false;

    if (ret == OK) {
        entry->last_success = time(NULL);
        cron_log("INFO", "Completed: %s", entry->command);
    } else {
        cron_log("ERROR", "Failed: %s (ret=%d)", entry->command, ret);
    }

    return ret;
}

/*==========================
 *  Cron 主循环
 *==========================*/

/**
 * Cron 任务主循环
 */
static void cron_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog(LOG_INFO, "[CRON] Cron task started\n");

    /* 绑定到 Core 1 / Pin to CPU1
     * NuttX 12.12 无 BOARDIOC_SMP_SETAFFINITY（见
     * deps/nuttx/include/sys/boardctl.h），用标准 sched_setaffinity()
     * （deps/nuttx/include/sched.h:256）*/
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(cpu_set_t), &mask);
    }
#endif

    g_cron_running = true;
    time_t last_check = 0;

    while (g_cron_running) {
        if (!g_cron_paused) {
            time_t now = time(NULL);

            /* 每分钟检查一次 */
            if (now - last_check >= 60) {
                last_check = now;

                /* 遍历所有条目 */
                for (int i = 0; i < g_entry_count; i++) {
                    struct cron_entry *entry = &g_entries[i];

                    if (!entry->enabled)
                        continue;

                    if (cron_match(entry)) {
                        /* 检查是否刚执行过（防止重复）*/
                        if (entry->last_run > 0 &&
                            (uint32_t)(now - entry->last_run) < 120) {
                            continue;  /* 2 分钟内执行过，跳过 */
                        }

                        cron_exec_entry(entry);
                    }
                }
            }
        }

        /* 休眠 10 秒 */
        sleep(10);
    }

    syslog(LOG_INFO, "[CRON] Cron task exiting\n");
}

/*==========================
 *  Crontab 解析
 *==========================*/

/**
 * 解析 crontab 行
 *
 * 格式: minute hour day month weekday command [#desc]
 */
static int cron_parse_line(const char *line, struct cron_entry *entry)
{
    if (!line || *line == '#' || *line == '\0')
        return -1;  /* 注释或空行 */

    char buf[CRON_MAX_LINE_LEN];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 跳过注释 */
    char *comment = strchr(buf, '#');
    if (comment) {
        *comment = '\0';
        comment++;
        while (*comment == ' ')
            comment++;
        if (*comment)
            strncpy(entry->desc, comment, sizeof(entry->desc) - 1);
    }

    /* 解析时间字段 */
    char *fields[5];
    char *saveptr;
    char *token = strtok_r(buf, " \t", &saveptr);

    int idx = 0;
    while (token && idx < 5) {
        fields[idx++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (idx < 5) {
        syslog(LOG_ERR, "[CRON] Invalid crontab line: %s\n", line);
        return -1;
    }

    /* 解析每个字段 */
    entry->minute  = parse_field(fields[0], 0, 59);
    entry->hour    = parse_field(fields[1], 0, 23);
    entry->day     = parse_field(fields[2], 1, 31);
    entry->month   = parse_field(fields[3], 1, 12);
    entry->weekday = parse_field(fields[4], 0, 6);

    if (entry->minute == -2 || entry->hour == -2 ||
        entry->day == -2 || entry->month == -2 || entry->weekday == -2) {
        syslog(LOG_ERR, "[CRON] Invalid time field in: %s\n", line);
        return -1;
    }

    /* 剩余部分为命令（saveptr 指向第 5 个字段之后的剩余字符串，包含空格）*/
    char *cmd = saveptr;
    if (!cmd || *cmd == '\0') {
        syslog(LOG_ERR, "[CRON] No command in: %s\n", line);
        return -1;
    }
    /* 跳过前导空白 */
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if (*cmd == '\0') {
        syslog(LOG_ERR, "[CRON] No command in: %s\n", line);
        return -1;
    }

    /* 解析命令类型 */
    if (strncmp(cmd, "audio:", 6) == 0) {
        entry->type = CRON_CMD_AUDIO;
        strncpy(entry->command, cmd + 6, sizeof(entry->command) - 1);
    } else if (strncmp(cmd, "tts:", 4) == 0) {
        entry->type = CRON_CMD_TTS;
        strncpy(entry->command, cmd + 4, sizeof(entry->command) - 1);
    } else if (strncmp(cmd, "notify:", 7) == 0) {
        entry->type = CRON_CMD_NOTIFY;
        strncpy(entry->command, cmd + 7, sizeof(entry->command) - 1);
    } else if (strcmp(cmd, "reboot") == 0) {
        entry->type = CRON_CMD_REBOOT;
        strcpy(entry->command, "reboot");
    } else if (strcmp(cmd, "wifi_reconnect") == 0) {
        entry->type = CRON_CMD_WIFI;
        strcpy(entry->command, "wifi_reconnect");
    } else {
        entry->type = CRON_CMD_SHELL;
        strncpy(entry->command, cmd, sizeof(entry->command) - 1);
    }

    entry->enabled = true;
    return OK;
}

/*
 * 功能描述 / WHAT:
 *   加载 crontab 文件到条目表 / Load crontab file into the entry table
 * WHY : 定时任务需要持久化恢复 / Cron entries must survive reboot
 * WHO : cron_init / `cron load` 命令 / cron_init / `cron load` command
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/cron.c
 * WHEN : 2026-10-04 重写（原来按固定 255 字节分块 read，跨块截断长行）
 * HOW  : 一次性读入整个文件（上限 CRON_FILE_MAX），再按 '\n' 逐行切分解析
 * 返回值 / Return:
 *   >=0 - 加载的条目数 / number of entries loaded
 */
int cron_load_crontab(void)
{
    int fd = open(CONFIG_CRON_CRONTAB, O_RDONLY);
    if (fd < 0) {
        syslog(LOG_INFO, "[CRON] No crontab file at %s\n",
               CONFIG_CRON_CRONTAB);
        return 0;  /* 不是错误，只是没有 crontab / no crontab is not an error */
    }

    char *buf = malloc(CRON_FILE_MAX);
    if (!buf) {
        close(fd);
        syslog(LOG_ERR, "[CRON] Out of memory loading crontab\n");
        return -ENOMEM;
    }

    size_t total = 0;
    while (total < CRON_FILE_MAX - 1) {
        ssize_t n = read(fd, buf + total, CRON_FILE_MAX - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';

    syslog(LOG_INFO, "[CRON] Loading crontab from %s (%lu bytes)\n",
           CONFIG_CRON_CRONTAB, (unsigned long)total);

    int loaded = 0;
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);

    while (line != NULL && g_entry_count < CRON_MAX_ENTRIES) {
        struct cron_entry entry = {0};
        if (cron_parse_line(line, &entry) == OK) {
            g_entries[g_entry_count++] = entry;
            loaded++;
            syslog(LOG_INFO, "[CRON] Loaded: %s", line);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    syslog(LOG_INFO, "[CRON] Loaded %d entries\n", loaded);
    return loaded;
}

/**
 * 保存 crontab 文件
 */
int cron_save_crontab(void)
{
#ifdef CONFIG_FS_FAT
    /* 确保目录存在 */
    mkdir("/sdcard/etc", 0755);
    mkdir("/sdcard/logs", 0755);

    int fd = open(CONFIG_CRON_CRONTAB, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "[CRON] Cannot write crontab: %d\n", errno);
        return fd;
    }

    dprintf(fd, "# ESP32-S3 Retro Workstation Crontab\n");
    dprintf(fd, "# Format: minute hour day month weekday command [#description]\n");
    dprintf(fd, "# Fields: * = any, 0-59 = specific value\n");
    dprintf(fd, "# Commands: audio:/path/file.mp3, tts:text, notify:text, reboot, wifi_reconnect\n");
    dprintf(fd, "#\n\n");

    for (int i = 0; i < g_entry_count; i++) {
        struct cron_entry *e = &g_entries[i];
        const char *cmd_type = "";
        const char *cmd_arg = e->command;

        switch (e->type) {
            case CRON_CMD_AUDIO:   cmd_type = "audio:";   break;
            case CRON_CMD_TTS:     cmd_type = "tts:";     break;
            case CRON_CMD_NOTIFY:  cmd_type = "notify:";  break;
            case CRON_CMD_REBOOT:  cmd_type = "reboot";   cmd_arg = ""; break;
            case CRON_CMD_WIFI:    cmd_type = "wifi_reconnect"; cmd_arg = ""; break;
            default:               cmd_type = "";         break;
        }

        /* '*'-1 表示任意时间，写出时还原为 '*'（原实现打印成 "-1" 无法重载）
         * -1 means "any"; write back as '*' (old code printed "-1") */
        char mbuf[8], hbuf[8], dbuf[8], monbuf[8], wbuf[8];
        snprintf(mbuf,   sizeof(mbuf),   "%s", e->minute  < 0 ? "*" : "");
        snprintf(hbuf,   sizeof(hbuf),   "%s", e->hour    < 0 ? "*" : "");
        snprintf(dbuf,   sizeof(dbuf),   "%s", e->day     < 0 ? "*" : "");
        snprintf(monbuf, sizeof(monbuf), "%s", e->month   < 0 ? "*" : "");
        snprintf(wbuf,   sizeof(wbuf),   "%s", e->weekday < 0 ? "*" : "");
        if (e->minute  >= 0) snprintf(mbuf,   sizeof(mbuf),   "%d", e->minute);
        if (e->hour    >= 0) snprintf(hbuf,   sizeof(hbuf),   "%d", e->hour);
        if (e->day     >= 0) snprintf(dbuf,   sizeof(dbuf),   "%d", e->day);
        if (e->month   >= 0) snprintf(monbuf, sizeof(monbuf), "%d", e->month);
        if (e->weekday >= 0) snprintf(wbuf,   sizeof(wbuf),   "%d", e->weekday);

        dprintf(fd, "%-2s %-2s %-2s %-2s %-2s  %s%s  # %s\n",
                mbuf, hbuf, dbuf, monbuf, wbuf,
                cmd_type, cmd_arg, e->desc);
    }

    close(fd);
    syslog(LOG_INFO, "[CRON] Crontab saved\n");
#endif
    return OK;
}

/*==========================
 *  API
 *==========================*/

/**
 * 启动 Cron 服务
 */
int cron_init(void)
{
    if (g_cron_running)
        return OK;

    /* 加载 crontab */
    cron_load_crontab();

    /* 如果没有条目，创建一些默认任务 */
    if (g_entry_count == 0) {
        syslog(LOG_INFO, "[CRON] No cron entries, using defaults\n");

        /* 每天早上 8 点 NTP 对时 */
        struct cron_entry ntp_entry = {
            .enabled  = true,
            .minute   = 0,
            .hour     = 8,
            .day      = -1,
            .month    = -1,
            .weekday  = -1,
            .type     = CRON_CMD_SHELL,
        };
        strcpy(ntp_entry.command, "ntp sync");
        g_entries[g_entry_count++] = ntp_entry;
    }

    /* 启动 Cron 任务 */
    g_cron_pid = kthread_create("cron",
                                 100,  /* 优先级 */
                                 CRON_TASK_STACK,
                                 (main_t)cron_task, NULL);
    if (g_cron_pid < 0) {
        syslog(LOG_ERR, "[CRON] Failed to start: %d\n", g_cron_pid);
        return g_cron_pid;
    }

    g_cron_running = true;
    syslog(LOG_INFO, "[CRON] Started (PID=%d)\n", g_cron_pid);

    /* 保存 crontab（如果有新增默认任务）*/
    cron_save_crontab();

    return OK;
}

/**
 * 停止 Cron 服务
 */
int cron_stop(void)
{
    g_cron_running = false;
    syslog(LOG_INFO, "[CRON] Stopped\n");
    return OK;
}

/**
 * 暂停 Cron
 */
int cron_pause(void)
{
    g_cron_paused = true;
    syslog(LOG_INFO, "[CRON] Paused\n");
    return OK;
}

/**
 * 恢复 Cron
 */
int cron_resume(void)
{
    g_cron_paused = false;
    syslog(LOG_INFO, "[CRON] Resumed\n");
    return OK;
}

/**
 * 添加 Cron 条目
 */
int cron_add(const char *minute, const char *hour,
             const char *day, const char *month, const char *weekday,
             const char *command)
{
    if (g_entry_count >= CRON_MAX_ENTRIES)
        return -ENOMEM;

    struct cron_entry entry = {0};
    entry.minute  = parse_field(minute, 0, 59);
    entry.hour    = parse_field(hour, 0, 23);
    entry.day     = parse_field(day, 1, 31);
    entry.month   = parse_field(month, 1, 12);
    entry.weekday = parse_field(weekday, 0, 6);

    if (entry.minute == -2 || entry.hour == -2 ||
        entry.day == -2 || entry.month == -2 || entry.weekday == -2) {
        return -EINVAL;
    }

    strncpy(entry.command, command, sizeof(entry.command) - 1);
    entry.type    = CRON_CMD_SHELL;
    entry.enabled = true;

    g_entries[g_entry_count++] = entry;
    cron_save_crontab();

    syslog(LOG_INFO, "[CRON] Added: %s %s %s %s %s %s\n",
           minute, hour, day, month, weekday, command);

    return OK;
}

/**
 * 删除 Cron 条目
 */
int cron_del(int index)
{
    if (index < 0 || index >= g_entry_count)
        return -EINVAL;

    for (int i = index; i < g_entry_count - 1; i++)
        g_entries[i] = g_entries[i + 1];

    g_entry_count--;
    cron_save_crontab();

    syslog(LOG_INFO, "[CRON] Deleted entry %d\n", index);
    return OK;
}

/**
 * 打印 Cron 状态
 */
void cron_print_status(void)
{
    printf("\n");
    printf("=== Cron Status ===\n");
    printf("Running:  %s\n", g_cron_running ? "YES" : "NO");
    printf("Paused:  %s\n", g_cron_paused ? "YES" : "NO");
    printf("Entries: %d\n", g_entry_count);
    printf("PID:     %d\n", g_cron_pid);
    printf("\n");

    printf("%-4s %-2s %-2s %-2s %-2s %-2s  %-30s  %s\n",
           "#", "Min", "Hr", "Day", "Mon", "Dow", "Command", "Description");
    printf("%-4s %-2s %-2s %-2s %-2s %-2s  %-30s  %s\n",
           "---", "---", "---", "---", "---", "---",
           "------------------------------", "-----------");

    for (int i = 0; i < g_entry_count; i++) {
        struct cron_entry *e = &g_entries[i];
        /* -1 = 任意时间，显示为 '*' / -1 = any, display as '*' */
        char mbuf[8], hbuf[8], dbuf[8], monbuf[8], wbuf[8];
        snprintf(mbuf,   sizeof(mbuf),   "%s", e->minute  < 0 ? "*" : "");
        snprintf(hbuf,   sizeof(hbuf),   "%s", e->hour    < 0 ? "*" : "");
        snprintf(dbuf,   sizeof(dbuf),   "%s", e->day     < 0 ? "*" : "");
        snprintf(monbuf, sizeof(monbuf), "%s", e->month   < 0 ? "*" : "");
        snprintf(wbuf,   sizeof(wbuf),   "%s", e->weekday < 0 ? "*" : "");
        if (e->minute  >= 0) snprintf(mbuf,   sizeof(mbuf),   "%d", e->minute);
        if (e->hour    >= 0) snprintf(hbuf,   sizeof(hbuf),   "%d", e->hour);
        if (e->day     >= 0) snprintf(dbuf,   sizeof(dbuf),   "%d", e->day);
        if (e->month   >= 0) snprintf(monbuf, sizeof(monbuf), "%d", e->month);
        if (e->weekday >= 0) snprintf(wbuf,   sizeof(wbuf),   "%d", e->weekday);
        printf("%-4d %-2s %-2s %-2s %-2s %-2s  %-30s  %s\n",
               i, mbuf, hbuf, dbuf, monbuf, wbuf,
               e->command, e->desc);
    }
    printf("\n");
}

/**
 * NSH 命令
 */
int cmd_cron(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: cron <status|list|add|del|start|stop|pause|resume|load|save>\n");
        printf("\n");
        printf("  status   显示状态\n");
        printf("  list     列出所有任务\n");
        printf("  add      添加任务: cron add * * * * * \"command\" #desc\n");
        printf("  del <n>  删除第 n 个任务\n");
        printf("  start    启动 Cron\n");
        printf("  stop     停止 Cron\n");
        printf("  pause    暂停 Cron\n");
        printf("  resume   恢复 Cron\n");
        printf("  load     重新加载 crontab\n");
        printf("  save     保存 crontab\n");
        return OK;
    }

    if (strcmp(argv[1], "status") == 0) {
        cron_print_status();
    } else if (strcmp(argv[1], "list") == 0) {
        cron_print_status();
    } else if (strcmp(argv[1], "start") == 0) {
        cron_init();
    } else if (strcmp(argv[1], "stop") == 0) {
        cron_stop();
    } else if (strcmp(argv[1], "pause") == 0) {
        cron_pause();
    } else if (strcmp(argv[1], "resume") == 0) {
        cron_resume();
    } else if (strcmp(argv[1], "load") == 0) {
        cron_load_crontab();
    } else if (strcmp(argv[1], "save") == 0) {
        cron_save_crontab();
    } else if (strcmp(argv[1], "add") == 0) {
        if (argc < 8) {
            printf("用法: cron add <min> <hr> <day> <mon> <dow> <cmd> [#desc]\n");
        } else {
            cron_add(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
        }
    } else if (strcmp(argv[1], "del") == 0) {
        if (argc > 2)
            cron_del(atoi(argv[2]));
        else
            printf("用法: cron del <index>\n");
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return OK;
}

#endif /* CONFIG_RETRO_CRON */
