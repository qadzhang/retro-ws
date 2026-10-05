/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * network.c - WiFi/网络
 *
 * WHAT : WiFi/网络
 * WHY  : STA 连接/DHCP/DNS 与 ping/netstat/ifconfig
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/driver/network.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : NuttX 网络栈 + WiFi 驱动封装
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/net/net.h>
#include <nuttx/net/tcp.h>
#include <nuttx/net/udp.h>
#include <nuttx/net/igmp.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

/* 工具链自带 netdb.h 的宏门控会遮住 getaddrinfo 原型（同 ntp.c/
 * network_utils.c），按 NuttX libc 签名显式声明（flat 模式 FAR 为空） */
extern int  getaddrinfo(const char *nodename, const char *servname,
                        const struct addrinfo *hints,
                        struct addrinfo **res);
extern void freeaddrinfo(struct addrinfo *res);

/*
 * WHAT : 域名 -> IPv4（getaddrinfo 适配）
 * WHY  : gethostbyname 在目标工具链头环境不可见（2026-10-05 修）
 */
static int net_resolve_ipv4(const char *host, struct in_addr *ip)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct sockaddr_in *sin;
    int gai;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    gai = getaddrinfo(host, NULL, &hints, &res);
    if (gai != 0 || res == NULL)
        return -EHOSTUNREACH;

    sin = (struct sockaddr_in *)res->ai_addr;
    *ip = sin->sin_addr;
    freeaddrinfo(res);
    return 0;
}
#include <netinet/in.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "wifi_conf.h"   /* /opt/etc/network.conf 读写（2026-10-05） */

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NETUTILS_NETLIB)
#  include <netutils/netlib.h>   /* netlib_set_ipv4addr 三件套 */
#  include <nuttx/net/dns.h>     /* dns_add_nameserver */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_NET
#  define CONFIG_RETRO_NET 1
#endif

#if CONFIG_RETRO_NET

/*==========================
 *  WiFi 状态
 *==========================*/

typedef enum {
    WIFI_STATE_IDLE = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_FAILED
} wifi_state_t;

struct wifi_status {
    wifi_state_t  state;
    char          ssid[32];
    int8_t        rssi;           /* 信号强度 dBm */
    uint8_t       channel;
    uint32_t      ip_addr;
    uint32_t      gateway;
    uint32_t      netmask;
    uint32_t      dns1;
    uint32_t      dns2;
    uint32_t      connect_time;   /* 连接时长（秒）*/
    uint32_t      last_update;
};

static struct wifi_status g_wifi = {
    .state     = WIFI_STATE_IDLE,
    .rssi      = -100,
    .channel   = 0,
    .ip_addr   = 0,
};

/* 重连请求标志：由 wifi_reconnect() 置位，待网络监控任务消费（TODO）
 * reconnect request flag; to be consumed by a monitor task (TODO) */
static volatile bool g_wifi_reconnect_req;

/*==========================
 *  WiFi 连接
 *==========================*/

/**
 * 连接到 WiFi 网络
 *
 * @param ssid     SSID 名称
 * @param password 密码
 * @return OK 或错误码
 */
int wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        syslog(LOG_ERR, "[WIFI] SSID and password required\n");
        return -EINVAL;
    }

    syslog(LOG_INFO, "[WIFI] Connecting to SSID: %s\n", ssid);

    strncpy(g_wifi.ssid, ssid, sizeof(g_wifi.ssid) - 1);
    g_wifi.state = WIFI_STATE_CONNECTING;

#ifdef CONFIG_ESP32S3_WIFI
    /* TODO: 调用 ESP-IDF WiFi 驱动
     * esp_wifi_set_mode(WIFI_MODE_STA);
     * esp_wifi_config_80211_tx_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_54M);
     * wifi_config_t sta_config = {
     *     .sta = { .ssid = ssid, .password = password, .bssid_set = false }
     * };
     * esp_wifi_set_config(WIFI_IF_STA, &sta_config);
     * esp_wifi_start();
     * esp_wifi_connect();
     */
#endif

    syslog(LOG_INFO, "[WIFI] Connection initiated\n");
    return OK;
}

/**
 * 断开 WiFi 连接
 */
int wifi_disconnect(void)
{
    syslog(LOG_INFO, "[WIFI] Disconnecting...\n");

#ifdef CONFIG_ESP32S3_WIFI
    /* esp_wifi_disconnect(); */
#endif

    g_wifi.state = WIFI_STATE_DISCONNECTED;
    g_wifi.ip_addr = 0;
    return OK;
}

/*
 * WHAT : 启动时按 /opt/etc/network.conf 自动连接（SD 卡有同名文件作回退）
 * WHY  : WiFi 凭据属系统配置，存片上可写区（HARDWARE 12.4）——不插 SD 卡
 *        也应自动联网；无配置文件时静默跳过（首配经 `wifi connect` 命令）
 * WHEN : 2026-10-05 新增
 * 返回 : OK（含无配置跳过）/ -EINVAL（配置非法）
 */
int wifi_auto_connect(void)
{
    struct wifi_conf_s cf;

    if (wifi_conf_load(&cf) != 0)
        return OK;                       /* 无配置：等首次 wifi connect */

    if (!wifi_conf_valid(&cf)) {
        syslog(LOG_ERR, "[WIFI] network.conf 静态 IP 字段非法\n");
        return -EINVAL;
    }

    syslog(LOG_INFO, "[WIFI] auto connect: %s (%s)\n", cf.ssid,
           cf.ip_mode == WIFI_IP_STATIC ? "static" : "dhcp");
    return wifi_connect(cf.ssid, cf.password);
}

/*
 * WHAT : 静态 IP 应用（ip_mode=static 时在链路 up 后调用）
 * HOW  : netlib 三件套设置 wlan0 地址/掩码/网关 + dns_add_nameserver
 *        注册 DNS（NuttX 12.12 真实 API，NEXT_STEPS 52 关单）；
 *        DHCP 模式由 wifi_on_dhcp_done 回调填状态，不经本函数
 * WHEN : 2026-10-05 新增；同日接通 netlib/dns（原仅记录状态）
 */
int wifi_apply_static_ip(const struct wifi_conf_s *cf)
{
#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NETUTILS_NETLIB)
    struct in_addr addr;
    struct sockaddr_in sa;
    int failures = 0;

    if (cf == NULL || cf->ip_mode != WIFI_IP_STATIC)
        return -EINVAL;

    if (inet_pton(AF_INET, cf->ip, &addr) == 1) {
        g_wifi.ip_addr = addr.s_addr;
        if (netlib_set_ipv4addr("wlan0", &addr) != OK)
            failures++;
    }

    if (inet_pton(AF_INET, cf->netmask, &addr) == 1) {
        g_wifi.netmask = addr.s_addr;
        if (netlib_set_ipv4netmask("wlan0", &addr) != OK)
            failures++;
    }

    if (inet_pton(AF_INET, cf->gateway, &addr) == 1) {
        g_wifi.gateway = addr.s_addr;
        if (netlib_set_dripv4addr("wlan0", &addr) != OK)
            failures++;
    }

    if (cf->dns[0] != '\0' && inet_pton(AF_INET, cf->dns, &addr) == 1) {
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(53);
        sa.sin_addr   = addr;
        g_wifi.dns1 = addr.s_addr;
        if (dns_add_nameserver((const struct sockaddr *)&sa,
                               sizeof(sa)) != OK)
            failures++;
    }

    syslog(failures == 0 ? LOG_INFO : LOG_WARNING,
           "[WIFI] static IP %s (wlan0 ip=%s mask=%s gw=%s)\n",
           failures == 0 ? "applied" : "partially applied",
           cf->ip, cf->netmask, cf->gateway);
    return failures == 0 ? OK : -EIO;
#else
    (void)cf;
    return -ENOSYS;
#endif
}

/*
 * WHAT : NSH `wifi connect` 落库——凭据即存 /opt/etc/network.conf
 * WHEN : 2026-10-05 新增
 */
int wifi_connect_and_save(const char *ssid, const char *password,
                          int ip_mode, const char *ip, const char *netmask,
                          const char *gateway, const char *dns)
{
    struct wifi_conf_s cf;
    int ret;

    memset(&cf, 0, sizeof(cf));
    snprintf(cf.ssid, sizeof(cf.ssid), "%s", ssid ? ssid : "");
    snprintf(cf.password, sizeof(cf.password), "%s", password ? password : "");
    cf.ip_mode = ip_mode;
    if (ip)       snprintf(cf.ip, sizeof(cf.ip), "%s", ip);
    if (netmask)  snprintf(cf.netmask, sizeof(cf.netmask), "%s", netmask);
    if (gateway)  snprintf(cf.gateway, sizeof(cf.gateway), "%s", gateway);
    if (dns)      snprintf(cf.dns, sizeof(cf.dns), "%s", dns);

    if (!wifi_conf_valid(&cf)) {
        syslog(LOG_ERR, "[WIFI] 静态 IP 参数非法，拒绝保存\n");
        return -EINVAL;
    }

    ret = wifi_conf_save(&cf);
    if (ret < 0)
        syslog(LOG_WARNING, "[WIFI] 配置保存失败: %d（仅本次连接）\n", ret);

    return wifi_connect(ssid, password);
}

/**
 * 获取 WiFi 状态 / Get WiFi status
 */
int wifi_get_status(struct wifi_status *status)
{
    memcpy(status, &g_wifi, sizeof(struct wifi_status));
    return OK;
}

/*
 * 功能描述 / WHAT: 查询 WiFi 链路是否已连接 / Query WiFi link state
 * WHY : NTP 同步（ntp.c）等模块需要在动作前确认链路可用
 * WHO : ntp.c / 其他 common 模块 / other common modules
 * WHERE: retro-ws/src/nuttx/common/driver/network.c
 * WHEN : 2026-10-04 新增（原为 ntp.c 中未定义的 extern bool）
 * HOW  : 读取本模块 g_wifi 状态机的当前值
 * 返回值 / Return: 1=已连接 / 1=connected, 0=未连接 / 0=not connected
 */
int wifi_is_connected(void)
{
    return g_wifi.state == WIFI_STATE_CONNECTED ? 1 : 0;
}

/*
 * 功能描述 / WHAT: 请求 WiFi 重连 / Request a WiFi reconnect
 * WHY : cron 的 wifi_reconnect 定时任务（cron.c）需要一个真实入口
 * WHO : cron.c / 用户
 * WHERE: retro-ws/src/nuttx/common/driver/network.c
 * WHEN : 2026-10-04 新增（原为 cron.c 中未定义的 extern）
 * HOW  : 记录重连请求标志；当前固件没有后台网络监控任务消费该标志，
 *       仅置状态为 CONNECTING 后返回 -ENOSYS 提示未完成
 * TODO(架构/Arch): 增加网络监控任务消费 g_wifi_reconnect_req，
 *       届时把返回值改为 OK / add a monitor task to consume the flag
 * 返回值 / Return: OK - 请求已受理；-ENOSYS - 尚无后台任务执行重连
 */
int wifi_reconnect(void)
{
    if (g_wifi.state == WIFI_STATE_CONNECTED)
        return OK;  /* 已连接无需重连 / already connected */

    if (g_wifi.ssid[0] == '\0')
        return -EINVAL;  /* 无保存的 SSID / no stored SSID */

    g_wifi_reconnect_req = true;
    g_wifi.state = WIFI_STATE_CONNECTING;
    syslog(LOG_INFO, "[WIFI] Reconnect requested (SSID=%s)\n", g_wifi.ssid);

    /* TODO(架构/Arch): 见函数头注释 / see header comment above */
    return -ENOSYS;
}

/**
 * WiFi 连接事件处理（由 WiFi 驱动调用）
 */
void wifi_on_connected(const char *ssid, int8_t rssi, uint8_t channel)
{
    g_wifi.state   = WIFI_STATE_CONNECTED;
    g_wifi.rssi    = rssi;
    g_wifi.channel = channel;

    syslog(LOG_INFO, "[WIFI] Connected to %s (RSSI=%ddBm, CH=%d)\n",
           ssid, rssi, channel);
}

/**
 * WiFi 连接失败处理
 */
void wifi_on_disconnect(int reason)
{
    g_wifi.state = WIFI_STATE_DISCONNECTED;
    syslog(LOG_WARNING, "[WIFI] Disconnected (reason=%d)\n", reason);
}

/**
 * DHCP 获取 IP 回调
 */
void wifi_on_dhcp_done(uint32_t ip, uint32_t gateway, uint32_t netmask,
                      uint32_t dns1, uint32_t dns2)
{
    g_wifi.ip_addr  = ip;
    g_wifi.gateway  = gateway;
    g_wifi.netmask  = netmask;
    g_wifi.dns1     = dns1;
    g_wifi.dns2     = dns2;

    /* inet_ntoa 返回共享静态缓冲，同一条日志里两次调用会互相覆盖，
     * 改用 inet_ntop + 独立缓冲 / inet_ntoa shares one static buffer */
    {
        char ip_str[INET_ADDRSTRLEN];
        char gw_str[INET_ADDRSTRLEN];
        char dns1_str[INET_ADDRSTRLEN];
        char dns2_str[INET_ADDRSTRLEN];

        syslog(LOG_INFO, "[WIFI] IP: %s\n",
               inet_ntop(AF_INET, &ip, ip_str, sizeof(ip_str)));
        syslog(LOG_INFO, "[WIFI] Gateway: %s\n",
               inet_ntop(AF_INET, &gateway, gw_str, sizeof(gw_str)));
        syslog(LOG_INFO, "[WIFI] DNS: %s, %s\n",
               inet_ntop(AF_INET, &dns1, dns1_str, sizeof(dns1_str)),
               inet_ntop(AF_INET, &dns2, dns2_str, sizeof(dns2_str)));
    }
}

/*==========================
 *  诊断工具
 *==========================*/

/*
 * ping ICMP 校验和 / ICMP header checksum (RFC 1071)
 */
static uint16_t icmp_checksum(const uint16_t *data, int len_bytes)
{
    uint32_t sum = 0;

    while (len_bytes > 1) {
        sum += *data++;
        len_bytes -= 2;
    }
    if (len_bytes == 1)
        sum += (uint32_t)(*(const uint8_t *)data) << 8;

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (uint16_t)(~sum);
}

/*
 * 功能描述 / WHAT: ICMP ping 命令 / ICMP ping command
 * WHY : 网络连通性诊断 / connectivity diagnostics
 * WHO : 用户在 NSH 调用 / NSH user
 * WHERE: retro-ws/src/nuttx/common/driver/network.c
 * WHEN : 2026-10-04 重写（原实现不发包却累加 sent，统计全是假数据）
 * HOW  : SOCK_RAW + IPPROTO_ICMP，手工组 Echo Request（RFC 792），
 *       sendto/recvfrom 收发并用 gettimeofday 计 RTT；
 *       依赖 CONFIG_NET_ICMP 与内核允许 RAW socket
 */
/* 原名 cmd_ping：与 NSH 内置命令撞名（NET 开启后 nshlib 自动编入），
 * 2026-10-05 改名 retro_ping 保留为库 API；NSH `ping` 走内置实现 */
int retro_ping(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: ping <host> [count]\n");
        return OK;
    }

    const char *host = argv[1];
    int count = (argc > 2) ? atoi(argv[2]) : 4;
    if (count <= 0)
        count = 4;

    int sent = 0, received = 0;
    double min_rtt = 0.0, max_rtt = 0.0, total_rtt = 0.0;

    printf("PING %s: %d data bytes\n", host, 64);

#ifdef CONFIG_NET
    struct in_addr resolved;

    if (net_resolve_ipv4(host, &resolved) != 0) {
        printf("ping: cannot resolve %s\n", host);
        return -EHOSTUNREACH;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr = resolved;

    /* ICMP 需要使用 SOCK_RAW 而非 SOCK_DGRAM / raw socket for ICMP */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf("ping: raw ICMP socket unavailable (%d); "
               "check CONFIG_NET_ICMP\n", errno);
        return -errno == 0 ? -EIO : -errno;
    }

    /* 接收超时 1 秒 / 1s receive timeout */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    for (int i = 0; i < count; i++) {
        /* RFC 792 Echo Request: type(1) code(1) cksum(2) id(2) seq(2) */
        uint8_t pkt[64];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 8;                              /* Echo Request */
        pkt[4] = (uint8_t)(getpid() & 0xff);     /* id low byte */
        pkt[5] = (uint8_t)((getpid() >> 8) & 0xff);
        pkt[6] = (uint8_t)(i & 0xff);            /* seq low byte */
        pkt[7] = (uint8_t)((i >> 8) & 0xff);
        uint16_t ck = icmp_checksum((const uint16_t *)pkt, sizeof(pkt));
        pkt[2] = (uint8_t)(ck & 0xff);
        pkt[3] = (uint8_t)(ck >> 8);

        struct timeval t1, t2;
        gettimeofday(&t1, NULL);

        ssize_t n = sendto(sock, pkt, sizeof(pkt), 0,
                           (struct sockaddr *)&dest, sizeof(dest));
        if (n < 0) {
            printf("ping: sendto seq=%d failed (%d)\n", i, errno);
            continue;
        }
        sent++;

        uint8_t reply[128];
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);

        n = recvfrom(sock, reply, sizeof(reply), 0,
                     (struct sockaddr *)&from, &fromlen);
        gettimeofday(&t2, NULL);

        /* 回包 type 0 = Echo Reply / reply type 0 = Echo Reply */
        if (n >= 8 && reply[0] == 0) {
            double rtt = (double)(t2.tv_sec - t1.tv_sec) * 1000.0 +
                         (double)(t2.tv_usec - t1.tv_usec) / 1000.0;
            if (received == 0 || rtt < min_rtt) min_rtt = rtt;
            if (rtt > max_rtt) max_rtt = rtt;
            total_rtt += rtt;
            received++;

            printf("%zd bytes from %s: seq=%d ttl=64 time=%.3f ms\n",
                   n, host, i, rtt);
        } else {
            printf("Request timeout for seq %d\n", i);
        }

        usleep(500000);  /* 间隔 0.5s / 0.5s gap */
    }

    close(sock);

    printf("\n--- %s ping statistics ---\n", host);
    if (sent > 0) {
        printf("%d packets transmitted, %d received, %.1f%% packet loss\n",
               sent, received, (sent - received) * 100.0 / sent);
    }
    if (received > 0) {
        printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
               min_rtt, total_rtt / received, max_rtt);
    }
#else
    (void)sent;
    (void)received;
    (void)min_rtt;
    (void)max_rtt;
    (void)total_rtt;
    printf("ping: network stack not compiled in\n");
#endif

    return OK;
}

/**
 * netstat 命令
 */
/* 原名 cmd_netstat：同上撞名改名，NSH `netstat` 走内置 */
int retro_netstat(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* ws 声明移出 CONFIG_NET 块：下方接口状态段也使用 / ws used below
     * the #ifdef too (was declared inside, used outside) */
    struct wifi_status ws;

    printf("\n");
    printf("=== Active Internet Connections (TCP) ===\n");
    printf("%-6s %-20s %-20s %-10s\n",
           "Proto", "Local Address", "Foreign Address", "State");
    printf("%-6s %-20s %-20s %-10s\n",
           "-----", "--------------", "----------------", "-----");

#ifdef CONFIG_NET
    /* TODO: 遍历所有活动的 TCP 连接 / TODO: enumerate TCP connections */

    /* 打印 WiFi 状态 / Show WiFi state */
    wifi_get_status(&ws);

    if (ws.state == WIFI_STATE_CONNECTED) {
        char ip_str[INET_ADDRSTRLEN];
        printf("%-6s %-20s %-20s %-10s\n",
               "wifi", inet_ntop(AF_INET, &ws.ip_addr, ip_str, sizeof(ip_str)),
               "connected", "up");
    }
#endif

    printf("\n");
    printf("=== Interface Status ===\n");
    printf("%-8s %-15s %-15s %s\n",
           "Iface", "IP Address", "Gateway", "Flags");
    printf("%-8s %-15s %-15s %s\n",
           "------", "-------------", "----------------", "-----");

    /* WiFi 接口 / WiFi interface */
    wifi_get_status(&ws);
    if (ws.ip_addr) {
        /* 两个 inet_ntoa 同行会共享缓冲，改独立缓冲 / separate buffers */
        char ip_str[INET_ADDRSTRLEN];
        char gw_str[INET_ADDRSTRLEN];
        printf("%-8s %-15s %-15s %s\n",
               "wlan0",
               inet_ntop(AF_INET, &ws.ip_addr, ip_str, sizeof(ip_str)),
               inet_ntop(AF_INET, &ws.gateway, gw_str, sizeof(gw_str)),
               "[UP, RUNNING]");
    } else {
        printf("%-8s %-15s %-15s %s\n",
               "wlan0", "0.0.0.0", "0.0.0.0", "[DOWN]");
    }

    printf("\n");
    return OK;
}

/**
 * ifconfig 命令
 */
/* 原名 cmd_ifconfig：同上撞名改名，NSH `ifconfig` 走内置 */
int retro_ifconfig(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct wifi_status ws;
    wifi_get_status(&ws);

    printf("\n");
    printf("wlan0    Link encap:UNSPEC  HWaddr 00:00:00:00:00:00\n");

    if (ws.ip_addr) {
        /* inet_ntop + 独立缓冲（NuttX 的 inet_ntoa 收 struct in_addr
         * 且共享静态缓冲）/ use inet_ntop with local buffers */
        char ip_str[INET_ADDRSTRLEN], mask_str[INET_ADDRSTRLEN];
        char gw_str[INET_ADDRSTRLEN], dns_str[INET_ADDRSTRLEN];
        printf("          inet addr:%s  ",
               inet_ntop(AF_INET, &ws.ip_addr, ip_str, sizeof(ip_str)));
        printf("Mask:%s\n",
               inet_ntop(AF_INET, &ws.netmask, mask_str, sizeof(mask_str)));
        printf("          Gateway:%s  ",
               inet_ntop(AF_INET, &ws.gateway, gw_str, sizeof(gw_str)));
        printf("DNS:%s\n",
               inet_ntop(AF_INET, &ws.dns1, dns_str, sizeof(dns_str)));
        printf("          TX bytes:%lu  RX bytes:%lu\n",
               (unsigned long)0, (unsigned long)0);  /* TODO: 统计 */
    } else {
        printf("          inet addr:0.0.0.0  Mask:0.0.0.0\n");
    }

    printf("          Status: ");
    switch (ws.state) {
        case WIFI_STATE_CONNECTED:
            printf("Connected (RSSI=%ddBm, CH=%d)\n", ws.rssi, ws.channel);
            break;
        case WIFI_STATE_CONNECTING:
            printf("Connecting...\n");
            break;
        case WIFI_STATE_DISCONNECTED:
            printf("Disconnected\n");
            break;
        case WIFI_STATE_FAILED:
            printf("Connection Failed\n");
            break;
        default:
            printf("Idle\n");
    }

    printf("\n");
    return OK;
}

/*==========================
 *  网络初始化
 *==========================*/

/**
 * 网络子系统初始化
 */
int net_init(void)
{
    syslog(LOG_INFO, "[NET] Initializing network subsystem...\n");

    /* 初始化 WiFi 驱动 */
#ifdef CONFIG_ESP32S3_WIFI
    /* esp_wifi_init(); */
#endif

    /* 初始化 TCP/IP 协议栈 */
#ifdef CONFIG_NET
    /* NuttX 网络初始化在 board_app_initialize 中完成 */
#endif

    /* 初始化防火墙 */
#ifdef CONFIG_RETRO_FIREWALL
    extern int fw_init(void);
    fw_init();
#endif

    syslog(LOG_INFO, "[NET] Network subsystem initialized\n");
    return OK;
}

/**
 * 连接到配置的 WiFi 网络
 */
int net_connect_wifi(void)
{
#ifdef CONFIG_RETRO_WIFI_SSID
    if (strlen(CONFIG_RETRO_WIFI_SSID) > 0) {
        syslog(LOG_INFO, "[NET] Connecting to WiFi: %s\n",
               CONFIG_RETRO_WIFI_SSID);
        return wifi_connect(CONFIG_RETRO_WIFI_SSID,
                           CONFIG_RETRO_WIFI_PASSWORD);
    }
#endif
    return OK;
}

/**
 * 打印网络状态
 */
void net_print_status(void)
{
    struct wifi_status ws;
    wifi_get_status(&ws);

    printf("\n");
    printf("=== Network Status ===\n");
    printf("WiFi:     %s\n",
           (ws.state == WIFI_STATE_CONNECTED) ? "Connected" :
           (ws.state == WIFI_STATE_CONNECTING) ? "Connecting" : "Disconnected");

    if (ws.state == WIFI_STATE_CONNECTED) {
        char ip_str[INET_ADDRSTRLEN], gw_str[INET_ADDRSTRLEN];
        char dns_str[INET_ADDRSTRLEN];
        printf("SSID:     %s\n", ws.ssid);
        printf("RSSI:     %d dBm\n", ws.rssi);
        printf("Channel:  %d\n", ws.channel);
        printf("IP:       %s\n",
               inet_ntop(AF_INET, &ws.ip_addr, ip_str, sizeof(ip_str)));
        printf("Gateway:  %s\n",
               inet_ntop(AF_INET, &ws.gateway, gw_str, sizeof(gw_str)));
        printf("DNS:      %s\n",
               inet_ntop(AF_INET, &ws.dns1, dns_str, sizeof(dns_str)));
    }
    printf("\n");
}

#endif /* CONFIG_RETRO_NET */
