/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ntp.c - NTP 对时
 *
 * WHAT : NTP 对时
 * WHY  : 网络时间同步（默认阿里云，每小时一次）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/ntp.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : UDP NTP 报文，成功后写入 RTC
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/net/net.h>
#include <nuttx/net/udp.h>
#include <nuttx/sched.h>
#include <nuttx/kthread.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_NTP
#  define CONFIG_RETRO_NTP 1
#endif

#if CONFIG_RETRO_NTP

#ifndef CONFIG_RETRO_NTP_SERVER
#  define CONFIG_RETRO_NTP_SERVER "ntp.aliyun.com"
#endif

#ifndef CONFIG_RETRO_NTP_INTERVAL
#  define CONFIG_RETRO_NTP_INTERVAL 3600  /* 1 小时 */
#endif

/* NTP 后台任务栈 / NTP background task stack（原 sizeof(g_cpu1_stack) 未定义）*/
#define NTP_TASK_STACK 4096

/*==========================
 *  NTP 服务器列表
 *==========================*/

struct ntp_server {
    const char *name;
    bool        tried;
    bool        succeeded;
};

static struct ntp_server g_ntp_servers[] = {
    { "ntp.aliyun.com",        false, false },  /* 默认：阿里云 */
    { "time.cloud.tencent.com", false, false },  /* 腾讯云 */
    { "time.windows.com",       false, false },  /* 微软 */
    { "pool.ntp.org",          false, false },  /* NTP Pool */
    { "time.google.com",       false, false },  /* Google */
};

#define NTP_SERVER_COUNT (sizeof(g_ntp_servers) / sizeof(g_ntp_servers[0]))

/*==========================
 *  NTP 协议
 *==========================*/

#define NTP_PORT           123
#define NTP_MODE_CLIENT    3
#define NTP_VERSION        4
#define NTP_LI            0  /* 无闰秒 */

/* NTP 时间戳（1900-01-01 到 1970-01-01 的秒数）*/
#define NTP_EPOCH_OFFSET   2208988800UL

#pragma pack(push, 1)
struct ntp_packet {
    uint8_t  li_vn_mode;      /* 2 bits LI + 3 bits version + 3 bits mode */
    uint8_t  stratum;          /* 8 bits stratum */
    uint8_t  poll;             /* 8 bits poll interval */
    int8_t   precision;        /* 8 bits precision */
    int32_t  root_delay;       /* 32 bits root delay */
    uint32_t root_dispersion;  /* 32 bits root dispersion */
    uint32_t ref_id;           /* 32 bits reference identifier */
    uint64_t ref_timestamp;    /* 64 bits reference timestamp */
    uint64_t orig_timestamp;   /* 64 bits origin timestamp */
    uint64_t recv_timestamp;   /* 64 bits receive timestamp */
    uint64_t tx_timestamp;     /* 64 bits transmit timestamp */
};
#pragma pack(pop)

/*==========================
 *  状态
 *==========================*/

static bool g_ntp_enabled = false;
static bool g_ntp_running = false;
static bool g_initial_sync = false;  /* 首次同步完成标志 */
static uint32_t g_ntp_interval = CONFIG_RETRO_NTP_INTERVAL;
static time_t g_last_sync_time = 0;
static int g_succeeded_server = -1;  /* 成功的服务器索引 */

/*==========================
 *  NTP 同步
 *==========================*/

/**
 * 发送 NTP 请求并获取时间
 */
static time_t ntp_query(const char *server)
{
    int sock;
    struct sockaddr_in dest;
    struct ntp_packet packet;
    socklen_t addr_len = sizeof(dest);
    ssize_t n;

    syslog(LOG_INFO, "[NTP] Querying %s...\n", server);

    /* 创建 UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        syslog(LOG_ERR, "[NTP] socket failed: %d\n", errno);
        return (time_t)-1;
    }

    /* 设置超时（5秒）*/
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 解析服务器地址 */
    struct hostent *he = gethostbyname(server);
    if (!he) {
        syslog(LOG_ERR, "[NTP] cannot resolve %s\n", server);
        close(sock);
        return (time_t)-1;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(NTP_PORT);
    dest.sin_addr   = *(struct in_addr *)he->h_addr;

    /* 构造 NTP 请求包 */
    memset(&packet, 0, sizeof(packet));
    packet.li_vn_mode = (NTP_LI << 6) | (NTP_VERSION << 3) | NTP_MODE_CLIENT;
    packet.stratum    = 0;
    packet.poll       = 10;
    packet.precision  = -6;  /* 2^-6 = ~15.6ms */

    /* 发送请求 */
    n = sendto(sock, &packet, sizeof(packet), 0,
               (struct sockaddr *)&dest, sizeof(dest));
    if (n < 0) {
        syslog(LOG_ERR, "[NTP] sendto failed: %d\n", errno);
        close(sock);
        return (time_t)-1;
    }

    /* 接收响应 */
    memset(&packet, 0, sizeof(packet));
    n = recvfrom(sock, &packet, sizeof(packet), 0,
                 (struct sockaddr *)&dest, &addr_len);
    close(sock);

    if (n < 0) {
        syslog(LOG_ERR, "[NTP] recv failed: %d\n", errno);
        return (time_t)-1;
    }

    if (n < (ssize_t)sizeof(packet)) {
        syslog(LOG_ERR, "[NTP] response too short: %zd\n", n);
        return (time_t)-1;
    }

    /* 解析时间戳（网络字节序 → 主机字节序）
     * NTP 秒数在 64 位时间戳的低 32 位（小端主机读出后取低 32 位再 ntohl）；
     * 原实现误用高 32 位，导致 LE 平台时间完全错误
     * NTP seconds live in the LOW 32 bits; the old code wrongly used hi */
    time_t t = (time_t)ntohl((uint32_t)(packet.tx_timestamp & 0xffffffffULL))
               - (time_t)NTP_EPOCH_OFFSET;

    syslog(LOG_INFO, "[NTP] Response from %s: %s\n",
           server, ctime(&t));

    return t;
}

/**
 * 执行一次 NTP 同步
 */
static int ntp_sync_once(void)
{
    time_t now;
    int success_server = -1;

    /* 尝试所有服务器 */
    for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
        if (g_ntp_servers[i].tried && g_ntp_servers[i].succeeded)
            continue;  /* 已成功过，优先重试 */

        time_t result = ntp_query(g_ntp_servers[i].name);
        g_ntp_servers[i].tried = true;

        if (result > 0) {
            now = result;
            g_ntp_servers[i].succeeded = true;
            success_server = i;
            break;
        }
    }

    if (success_server < 0) {
        syslog(LOG_ERR, "[NTP] All servers failed\n");
        return -1;
    }

    /* 设置系统时间 */
    struct timeval tv = {
        .tv_sec  = now,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);

    /* 写入 RTC */
#ifdef CONFIG_RTC
    extern int rtc_set_time(time_t time);
    rtc_set_time(now);
#endif

    g_last_sync_time = now;
    g_succeeded_server = success_server;
    g_initial_sync = true;

    syslog(LOG_INFO, "[NTP] Time synchronized to %s: %s",
           g_ntp_servers[success_server].name, ctime(&now));

    return OK;
}

/**
 * NTP 后台任务
 */
static void ntp_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog(LOG_INFO, "[NTP] NTP sync task started\n");

    /* 绑定到 Core 1 / Pin to CPU1
     * NuttX 12.12 无 BOARDIOC_SMP_SETAFFINITY（见
     * deps/nuttx/include/sys/boardctl.h），用标准 sched_setaffinity() */
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(cpu_set_t), &mask);
    }
#endif

    g_ntp_running = true;

    /* 立即同步一次 */
    if (ntp_sync_once() < 0) {
        syslog(LOG_WARNING, "[NTP] Initial sync failed, will retry...\n");
    }

    /* 周期同步 */
    while (g_ntp_running) {
        /* 等待间隔 */
        sleep(g_ntp_interval);

        if (!g_ntp_running)
            break;

        /* 检查网络是否连接 / Skip sync when WiFi is down
         * 实现在 common/driver/network.c / implemented in network.c */
        extern int wifi_is_connected(void);
        if (!wifi_is_connected()) {
            syslog(LOG_INFO, "[NTP] WiFi not connected, skipping sync\n");
            continue;
        }

        /* 重新同步 */
        if (ntp_sync_once() < 0) {
            syslog(LOG_WARNING, "[NTP] Sync failed, will retry next interval\n");
        }
    }

    syslog(LOG_INFO, "[NTP] NTP sync task exiting\n");
}

/*==========================
 *  API
 *==========================*/

/**
 * 启动 NTP 同步
 */
int ntp_sync_start(void)
{
    if (g_ntp_running)
        return OK;

    /* 重置服务器状态 */
    for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
        g_ntp_servers[i].tried     = false;
        g_ntp_servers[i].succeeded = false;
    }
    g_succeeded_server = -1;

    /* 启动后台任务 */
    g_ntp_enabled = true;

    pid_t pid = kthread_create("ntp",
                                130,  /* 低优先级 */
                                NTP_TASK_STACK,
                                (main_t)ntp_task, NULL);
    if (pid < 0) {
        syslog(LOG_ERR, "[NTP] Failed to start task: %d\n", pid);
        return pid;
    }

    syslog(LOG_INFO, "[NTP] Started (PID=%d)\n", pid);
    return OK;
}

/**
 * 停止 NTP 同步
 */
int ntp_sync_stop(void)
{
    g_ntp_running = false;
    g_ntp_enabled = false;
    syslog(LOG_INFO, "[NTP] Stopped\n");
    return OK;
}

/**
 * 立即同步一次
 */
int ntp_sync_now(void)
{
    if (!g_ntp_enabled) {
        g_ntp_enabled = true;
        int ret = ntp_sync_once();
        g_ntp_enabled = false;
        return ret;
    }
    return ntp_sync_once();
}

/**
 * 设置同步间隔
 */
int ntp_set_interval(uint32_t seconds)
{
    if (seconds < 60)  /* 最短 1 分钟 */
        seconds = 60;
    if (seconds > 86400)  /* 最长 1 天 */
        seconds = 86400;

    g_ntp_interval = seconds;
    syslog(LOG_INFO, "[NTP] Interval set to %lu seconds\n",
           (unsigned long)seconds);
    return OK;
}

/**
 * 获取 NTP 状态
 */
void ntp_get_status(char *status, size_t len)
{
    const char *state = g_ntp_running ? "running" : "stopped";
    const char *server = (g_succeeded_server >= 0) ?
                         g_ntp_servers[g_succeeded_server].name : "none";
    const char *last = g_last_sync_time ? ctime(&g_last_sync_time) : "never";

    snprintf(status, len,
             "NTP: %s, server=%s, interval=%lus, last=%s",
             state, server, (unsigned long)g_ntp_interval, last);
}

/**
 * 打印 NTP 状态
 */
void ntp_print_status(void)
{
    char status[256];
    ntp_get_status(status, sizeof(status));

    printf("\n");
    printf("=== NTP Time Sync ===\n");
    printf("%s\n", status);
    printf("\n");

    printf("Servers:\n");
    for (size_t i = 0; i < NTP_SERVER_COUNT; i++) {
        printf("  %-25s %s %s\n",
               g_ntp_servers[i].name,
               g_ntp_servers[i].tried ? "[tried]" : "",
               g_ntp_servers[i].succeeded ? "[OK]" : "");
    }
    printf("\n");
}

/**
 * NSH 命令
 */
int cmd_ntp(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: ntp <status|sync|start|stop|interval>\n");
        return OK;
    }

    if (strcmp(argv[1], "status") == 0) {
        ntp_print_status();
    } else if (strcmp(argv[1], "sync") == 0) {
        printf("Syncing NTP time now...\n");
        int ret = ntp_sync_now();
        if (ret < 0)
            printf("NTP sync failed\n");
        else
            printf("NTP sync successful\n");
    } else if (strcmp(argv[1], "start") == 0) {
        ntp_sync_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        ntp_sync_stop();
    } else if (strcmp(argv[1], "interval") == 0) {
        if (argc > 2)
            ntp_set_interval(atoi(argv[2]));
        else
            printf("Current interval: %lu seconds\n",
                   (unsigned long)g_ntp_interval);
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return OK;
}

#endif /* CONFIG_RETRO_NTP */
