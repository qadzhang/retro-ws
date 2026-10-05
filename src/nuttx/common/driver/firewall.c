/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * firewall.c - 防火墙
 *
 * WHAT : 防火墙
 * WHY  : 默认拒入站/放出站/防 ping，规则持久化
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/driver/firewall.c
 * WHEN : 2026-03~04 初版，2026-10-04 5W1H 标准化；2026-10-05 规则
 *        持久化落地 /opt/etc/firewall.conf（文本 CSV，NEXT_STEPS 51 关单）
 * HOW  : 连接跟踪表 + 规则存片上 /opt/etc（增删改即存，开机 load，
 *        无配置文件时加载默认规则），fw_* NSH 命令管理
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/net/net.h>
#include <nuttx/net/ip.h>    /* 原 <nuttx/net/ipv4.h> 在 NuttX 中不存在 */
#include <nuttx/net/udp.h>
#include <nuttx/net/tcp.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_FIREWALL
#  define CONFIG_RETRO_FIREWALL 1
#endif

#if CONFIG_RETRO_FIREWALL

/*==========================
 *  常量
 *==========================*/

#define FIREWALL_MAX_RULES      32

/* 规则持久化路径（片上可写区；宿主测试可 -D 注入沙箱） */
#ifndef FW_CONF_PATH
#  define FW_CONF_PATH  "/opt/etc/firewall.conf"
#endif
#ifndef FW_CONF_DIR
#  define FW_CONF_DIR   "/opt/etc"
#endif
#define FIREWALL_MAX_CONNECTIONS 128
#define FIREWALL_CONN_TIMEOUT   300   /* 5 分钟连接超时 */

/* 规则动作 */
#define FW_ACTION_ALLOW  0
#define FW_ACTION_DENY   1
#define FW_ACTION_LOG    2

/* 协议类型 */
#define FW_PROTO_ANY   0
#define FW_PROTO_TCP   6
#define FW_PROTO_UDP   17
#define FW_PROTO_ICMP  1

/* 规则方向 */
#define FW_DIR_IN    0   /* 入站 */
#define FW_DIR_OUT   1   /* 出站 */
#define FW_DIR_ANY   2

/* IP 地址通配符（0 表示任意）*/
#define FW_IP_ANY     0x00000000U
#define FW_PORT_ANY   0

/*==========================
 *  规则结构
 *==========================*/

struct fw_rule {
    bool         enabled;    /* 规则是否启用 */
    uint8_t      action;     /* ALLOW / DENY / LOG */
    uint8_t      proto;      /* 协议 */
    uint8_t      dir;        /* 方向 */
    uint32_t     src_ip;     /* 源 IP（0=任意）*/
    uint32_t     dst_ip;     /* 目标 IP（0=任意）*/
    uint16_t     src_port;   /* 源端口（0=任意）*/
    uint16_t     dst_port;   /* 目标端口（0=任意）*/
    uint32_t     match_count; /* 命中次数 */
    char         desc[32];   /* 描述 */
};

/* 连接跟踪条目 */
struct fw_conn {
    bool         active;     /* 是否活跃 */
    uint8_t      proto;      /* 协议 */
    uint32_t     src_ip;     /* 源 IP */
    uint32_t     dst_ip;     /* 目标 IP */
    uint16_t     src_port;   /* 源端口 */
    uint16_t     dst_port;   /* 目标端口 */
    uint32_t     timestamp;  /* 最后活跃时间 */
    uint8_t      state;      /* 连接状态 */
};

/* 前置声明：增删改钩子调用保存（定义见持久化节） */
int fw_save_rules(void);

/*==========================
 *  全局变量
 *==========================*/

/* 默认规则（编译时常量）
 * 顺序即优先级：具体规则在前，泛化兜底规则在后
 * (allow outbound + deny inbound last)，否则泛化规则会遮蔽具体规则
 * Order is priority: specific rules first, generic fallbacks last */
static const struct fw_rule g_default_rules[] = {
    /* 允许 ICMP (ping) 出站 */
    {
        .enabled  = true,
        .action   = FW_ACTION_ALLOW,
        .proto    = FW_PROTO_ICMP,
        .dir      = FW_DIR_OUT,
        .src_ip   = FW_IP_ANY,
        .dst_ip   = FW_IP_ANY,
        .src_port = FW_PORT_ANY,
        .dst_port = FW_PORT_ANY,
        .desc     = "Allow outbound ping"
    },
    /* 拒绝外部 ping */
    {
        .enabled  = true,
        .action   = FW_ACTION_DENY,
        .proto    = FW_PROTO_ICMP,
        .dir      = FW_DIR_IN,
        .src_ip   = FW_IP_ANY,
        .dst_ip   = FW_IP_ANY,
        .src_port = FW_PORT_ANY,
        .dst_port = FW_PORT_ANY,
        .desc     = "Block inbound ping"
    },
    /* 出站规则：默认允许所有 */
    {
        .enabled  = true,
        .action   = FW_ACTION_ALLOW,
        .proto    = FW_PROTO_ANY,
        .dir      = FW_DIR_OUT,
        .src_ip   = FW_IP_ANY,
        .dst_ip   = FW_IP_ANY,
        .src_port = FW_PORT_ANY,
        .dst_port = FW_PORT_ANY,
        .desc     = "Default: allow all outbound"
    },
    /* 入站规则：默认拒绝所有 */
    {
        .enabled  = true,
        .action   = FW_ACTION_DENY,
        .proto    = FW_PROTO_ANY,
        .dir      = FW_DIR_IN,
        .src_ip   = FW_IP_ANY,
        .dst_ip   = FW_IP_ANY,
        .src_port = FW_PORT_ANY,
        .dst_port = FW_PORT_ANY,
        .desc     = "Default: deny all inbound"
    },
};

/* 运行时规则表 */
static struct fw_rule g_rules[FIREWALL_MAX_RULES];
static int g_rule_count = 0;

/* 连接跟踪表 */
static struct fw_conn g_conns[FIREWALL_MAX_CONNECTIONS];
static uint32_t g_conn_count = 0;
static uint32_t g_blocked_count = 0;
static uint32_t g_allowed_count = 0;

/* 防火墙状态 */
static bool g_firewall_enabled = true;
static bool g_log_dropped = true;  /* 记录丢弃的包 */

/*==========================
 *  工具函数
 *==========================*/

/*
 * 功能描述 / WHAT: IP 转点分字符串（写入调用方缓冲）
 * WHY : 原实现返回共享 static 缓冲，同一条 syslog 里 src/dst 两个 IP
 *       互相覆盖，打印结果两个都是 dst / shared buffer made both IPs
 *       print as dst
 * WHO : fw_check_packet / fw_print_status
 * WHERE: retro-ws/src/nuttx/common/driver/firewall.c
 * WHEN : 2026-10-04 重构签名
 * HOW  : snprintf 进调用方提供的缓冲
 * 参数 / Params: ip - 主机序 32 位 IP；buf/len - 输出缓冲
 * 返回值 / Return: buf 本身，便于直接内嵌在 printf 参数里
 */
static const char *ip_to_str(uint32_t ip, char *buf, size_t len)
{
    snprintf(buf, len, "%u.%u.%u.%u",
             (ip >> 0) & 0xff,
             (ip >> 8) & 0xff,
             (ip >> 16) & 0xff,
             (ip >> 24) & 0xff);
    return buf;
}

/*
 * 功能描述 / WHAT: 端口转字符串（写入调用方缓冲）
 * WHY : 同 ip_to_str，消除共享 static 缓冲
 * HOW  : 0 (FW_PORT_ANY) 显示为 "*"
 */
static const char *port_to_str(uint16_t port, char *buf, size_t len)
{
    if (port == FW_PORT_ANY) {
        snprintf(buf, len, "*");
    } else {
        snprintf(buf, len, "%u", port);
    }
    return buf;
}

/**
 * 协议号转字符串
 */
static const char *proto_to_str(uint8_t proto)
{
    switch (proto) {
        case FW_PROTO_TCP:  return "TCP";
        case FW_PROTO_UDP:  return "UDP";
        case FW_PROTO_ICMP: return "ICMP";
        default:            return "ANY";
    }
}

/*==========================
 *  规则匹配
 *==========================*/

/**
 * 检查 IP 是否匹配规则
 */
static bool ip_match(uint32_t rule_ip, uint32_t packet_ip)
{
    return (rule_ip == FW_IP_ANY) || (rule_ip == packet_ip);
}

/**
 * 检查端口是否匹配规则
 */
static bool port_match(uint16_t rule_port, uint16_t packet_port)
{
    return (rule_port == FW_PORT_ANY) || (rule_port == packet_port);
}

/**
 * 检查数据包是否匹配规则
 */
static bool packet_match(const struct fw_rule *rule,
                         uint8_t proto,
                         uint8_t dir,
                         uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port)
{
    if (!rule->enabled)
        return false;

    if (rule->proto != FW_PROTO_ANY && rule->proto != proto)
        return false;

    if (rule->dir != FW_DIR_ANY && rule->dir != dir)
        return false;

    if (!ip_match(rule->src_ip, src_ip))
        return false;

    if (!ip_match(rule->dst_ip, dst_ip))
        return false;

    if (!port_match(rule->src_port, src_port))
        return false;

    if (!port_match(rule->dst_port, dst_port))
        return false;

    return true;
}

/*==========================
 *  连接跟踪
 *==========================*/

/**
 * 查找或创建连接跟踪条目
 */
static struct fw_conn *conn_track_find_or_create(
    uint8_t proto, uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port)
{
    /* 查找现有条目 */
    for (int i = 0; i < FIREWALL_MAX_CONNECTIONS; i++) {
        if (g_conns[i].active &&
            g_conns[i].proto == proto &&
            g_conns[i].src_ip == src_ip &&
            g_conns[i].dst_ip == dst_ip &&
            g_conns[i].src_port == src_port &&
            g_conns[i].dst_port == dst_port) {
            g_conns[i].timestamp = 0;  /* TODO: 时间戳 */
            return &g_conns[i];
        }
    }

    /* 创建新条目 */
    for (int i = 0; i < FIREWALL_MAX_CONNECTIONS; i++) {
        if (!g_conns[i].active) {
            memset(&g_conns[i], 0, sizeof(struct fw_conn));
            g_conns[i].active   = true;
            g_conns[i].proto   = proto;
            g_conns[i].src_ip  = src_ip;
            g_conns[i].dst_ip  = dst_ip;
            g_conns[i].src_port = src_port;
            g_conns[i].dst_port = dst_port;
            g_conns[i].timestamp = 0;
            g_conn_count++;
            return &g_conns[i];
        }
    }

    /* 表满，返回 NULL */
    return NULL;
}

/**
 * 清理超时连接
 */
static void conn_track_cleanup(void)
{
    /* TODO: 基于时间戳清理超时连接 */
}

/*==========================
 *  防火墙核心
 *==========================*/

/**
 * 检查数据包是否允许通过
 *
 * @return true=允许, false=拒绝
 */
bool fw_check_packet(uint8_t proto, uint8_t dir,
                     uint32_t src_ip, uint32_t dst_ip,
                     uint16_t src_port, uint16_t dst_port)
{
    if (!g_firewall_enabled)
        return true;

    /* 依次检查规则（按优先级）*/
    for (int i = 0; i < g_rule_count; i++) {
        struct fw_rule *rule = &g_rules[i];

        if (packet_match(rule, proto, dir, src_ip, dst_ip, src_port, dst_port)) {
            rule->match_count++;

            if (rule->action == FW_ACTION_ALLOW) {
                g_allowed_count++;
                /* 创建连接跟踪 */
                if (proto == FW_PROTO_TCP || proto == FW_PROTO_UDP) {
                    conn_track_find_or_create(proto, src_ip, dst_ip, src_port, dst_port);
                }
                return true;
            }

            if (rule->action == FW_ACTION_DENY) {
                g_blocked_count++;
                if (g_log_dropped) {
                    /* 独立缓冲避免 src/dst 互相覆盖
                     * separate buffers so src/dst don't alias */
                    char sbuf[16], sport[8], dbuf[16], dport[8];
                    syslog(LOG_INFO,
                           "[FIREWALL] Blocked: %s %s %s:%s -> %s:%s\n",
                           proto_to_str(proto),
                           (dir == FW_DIR_IN) ? "IN" : "OUT",
                           ip_to_str(src_ip, sbuf, sizeof(sbuf)),
                           port_to_str(src_port, sport, sizeof(sport)),
                           ip_to_str(dst_ip, dbuf, sizeof(dbuf)),
                           port_to_str(dst_port, dport, sizeof(dport)));
                }
                return false;
            }
        }
    }

    /* 无匹配规则：默认拒绝 */
    g_blocked_count++;
    return false;
}

/**
 * TCP 入站检查（由 TCP 协议栈调用）
 */
bool fw_tcp_inbound_check(uint32_t src_ip, uint16_t src_port,
                          uint32_t dst_ip, uint16_t dst_port)
{
    return fw_check_packet(FW_PROTO_TCP, FW_DIR_IN,
                          src_ip, dst_ip, src_port, dst_port);
}

/**
 * UDP 入站检查
 */
bool fw_udp_inbound_check(uint32_t src_ip, uint16_t src_port,
                          uint32_t dst_ip, uint16_t dst_port)
{
    return fw_check_packet(FW_PROTO_UDP, FW_DIR_IN,
                          src_ip, dst_ip, src_port, dst_port);
}

/**
 * ICMP 检查
 */
bool fw_icmp_check(uint8_t type, uint8_t dir,
                   uint32_t src_ip, uint32_t dst_ip)
{
    /* 0 = Echo Reply, 8 = Echo Request (ping) */
    if (type == 8 || type == 0) {
        /* ping 相关，检查规则 */
        return fw_check_packet(FW_PROTO_ICMP, dir,
                               src_ip, dst_ip, 0, 0);
    }
    return true;  /* 其他 ICMP 类型默认允许 */
}

/*==========================
 *  规则管理
 *==========================*/

/**
 * 添加规则
 */
int fw_add_rule(uint8_t action, uint8_t proto, uint8_t dir,
                uint32_t src_ip, uint32_t dst_ip,
                uint16_t src_port, uint16_t dst_port,
                const char *desc)
{
    if (g_rule_count >= FIREWALL_MAX_RULES)
        return -ENOMEM;

    struct fw_rule *rule = &g_rules[g_rule_count];
    rule->enabled   = true;
    rule->action    = action;
    rule->proto     = proto;
    rule->dir       = dir;
    rule->src_ip    = src_ip;
    rule->dst_ip    = dst_ip;
    rule->src_port  = src_port;
    rule->dst_port  = dst_port;
    rule->match_count = 0;

    if (desc)
        strncpy(rule->desc, desc, sizeof(rule->desc) - 1);

    g_rule_count++;

    if (fw_save_rules() < 0)
        syslog(LOG_WARNING, "[FIREWALL] rule save failed (RAM only)\n");
    syslog(LOG_INFO, "[FIREWALL] Rule added: %s\n", desc ? desc : "unnamed");
    return OK;
}

/**
 * 删除规则
 */
int fw_del_rule(int index)
{
    if (index < 0 || index >= g_rule_count)
        return -EINVAL;

    /* 移动后续规则 */
    for (int i = index; i < g_rule_count - 1; i++) {
        g_rules[i] = g_rules[i + 1];
    }

    g_rule_count--;
    if (fw_save_rules() < 0)
        syslog(LOG_WARNING, "[FIREWALL] rule save failed (RAM only)\n");
    syslog(LOG_INFO, "[FIREWALL] Rule %d deleted\n", index);
    return OK;
}

/**
 * 启用/禁用规则
 */
int fw_rule_enable(int index, bool enable)
{
    if (index < 0 || index >= g_rule_count)
        return -EINVAL;

    g_rules[index].enabled = enable;
    if (fw_save_rules() < 0)
        syslog(LOG_WARNING, "[FIREWALL] rule save failed (RAM only)\n");
    syslog(LOG_INFO, "[FIREWALL] Rule %d %s\n", index, enable ? "enabled" : "disabled");
    return OK;
}

/**
 * 加载默认规则
 */
void fw_load_default_rules(void)
{
    /* 复制默认规则到规则表 */
    size_t n = sizeof(g_default_rules) / sizeof(g_default_rules[0]);
    for (size_t i = 0; i < n && g_rule_count < FIREWALL_MAX_RULES; i++) {
        g_rules[g_rule_count++] = g_default_rules[i];
    }

    syslog(LOG_INFO, "[FIREWALL] Loaded %zu default rules\n", n);
}


/*==========================
 *  规则持久化 /opt/etc/firewall.conf（2026-10-05，NEXT_STEPS 51）
 *==========================*/

/* 逐级建目录（mkdir 只建末级） */
static void fw_mkdirs(const char *path)
{
    char tmp[64];
    char *p;

    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* 点分十进制 <-> u32（高位在前；与自身编码对称，roundtrip 无损） */
static int fw_ip_parse(const char *s, uint32_t *out)
{
    unsigned a, b, c, d;

    if (s == NULL || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return -EINVAL;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return -EINVAL;
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return OK;
}

static void fw_ip_str(uint32_t ip, char *buf, size_t bufsz)
{
    snprintf(buf, bufsz, "%u.%u.%u.%u",
             (ip >> 24) & 0xff, (ip >> 16) & 0xff,
             (ip >> 8) & 0xff, ip & 0xff);
}

/*
 * WHAT : 解析一行规则 CSV（action,proto,dir,src,dst,sport,dport,enabled,desc）
 * HOW  : 逐字段逗号切分；desc 取余下整段（可含空格不含逗号）；非法行
 *        返回 -EINVAL 由调用方跳过
 */
static int fw_rule_parse_line(struct fw_rule *rule, const char *line)
{
    char act[8], proto[8], dir[8];
    char src[20], dst[20];
    int sport, dport, enabled;
    int consumed = 0;

    memset(rule, 0, sizeof(*rule));

    if (sscanf(line, "%7[^,],%7[^,],%7[^,],%19[^,],%19[^,],%d,%d,%d,%n",
               act, proto, dir, src, dst, &sport, &dport, &enabled,
               &consumed) < 8)
        return -EINVAL;

    if (strcmp(act, "allow") == 0)
        rule->action = FW_ACTION_ALLOW;
    else if (strcmp(act, "deny") == 0)
        rule->action = FW_ACTION_DENY;
    else if (strcmp(act, "log") == 0)
        rule->action = FW_ACTION_LOG;
    else
        return -EINVAL;

    if (strcmp(proto, "any") == 0)
        rule->proto = FW_PROTO_ANY;
    else if (strcmp(proto, "tcp") == 0)
        rule->proto = FW_PROTO_TCP;
    else if (strcmp(proto, "udp") == 0)
        rule->proto = FW_PROTO_UDP;
    else if (strcmp(proto, "icmp") == 0)
        rule->proto = FW_PROTO_ICMP;
    else
        return -EINVAL;

    if (strcmp(dir, "in") == 0)
        rule->dir = FW_DIR_IN;
    else if (strcmp(dir, "out") == 0)
        rule->dir = FW_DIR_OUT;
    else if (strcmp(dir, "any") == 0)
        rule->dir = FW_DIR_ANY;
    else
        return -EINVAL;

    if (fw_ip_parse(src, &rule->src_ip) != OK ||
        fw_ip_parse(dst, &rule->dst_ip) != OK)
        return -EINVAL;
    if (sport < 0 || sport > 65535 || dport < 0 || dport > 65535)
        return -EINVAL;
    rule->src_port = (uint16_t)sport;
    rule->dst_port = (uint16_t)dport;
    rule->enabled = enabled != 0;

    {
        const char *desc = line + consumed;
        size_t len = strlen(desc);

        while (len > 0 && (desc[len - 1] == '\n' || desc[len - 1] == '\r'))
            len--;
        if (len >= sizeof(rule->desc))
            len = sizeof(rule->desc) - 1;
        memcpy(rule->desc, desc, len);
        rule->desc[len] = '\0';
    }

    return OK;
}

static const char *fw_action_str(int a)
{
    return a == FW_ACTION_ALLOW ? "allow" :
           a == FW_ACTION_DENY ? "deny" : "log";
}

static const char *fw_proto_str(int p)
{
    return p == FW_PROTO_TCP ? "tcp" : p == FW_PROTO_UDP ? "udp" :
           p == FW_PROTO_ICMP ? "icmp" : "any";
}

static const char *fw_dir_str(int d)
{
    return d == FW_DIR_IN ? "in" : d == FW_DIR_OUT ? "out" : "any";
}

/*
 * WHAT : 从 /opt/etc/firewall.conf 加载规则（无文件/空文件 -> 默认规则）
 * WHY  : 规则持久化片上可写区（HARDWARE 12.4）——重启不丢、无 SD 卡可用
 * 返回 : OK / 负错误码（文件存在但全部行非法 = -EINVAL 且回退默认）
 */
int fw_load_rules(void)
{
    FILE *fp = fopen(FW_CONF_PATH, "r");
    char line[192];
    int loaded = 0;

    if (fp == NULL) {
        fw_load_default_rules();          /* 首次开机：默认规则 */
        return OK;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (g_rule_count >= FIREWALL_MAX_RULES)
            break;
        if (fw_rule_parse_line(&g_rules[g_rule_count], line) == OK) {
            g_rule_count++;
            loaded++;
        }
    }
    fclose(fp);

    if (loaded == 0) {
        g_rule_count = 0;
        fw_load_default_rules();
        return -EINVAL;
    }

    syslog(LOG_INFO, "[FIREWALL] Loaded %d rules from %s\n", loaded,
           FW_CONF_PATH);
    return OK;
}

/*
 * WHAT : 保存全部规则到 /opt/etc/firewall.conf（CSV 文本）
 * 返回 : OK / 负错误码
 */
int fw_save_rules(void)
{
    FILE *fp;

    fw_mkdirs(FW_CONF_DIR);
    fp = fopen(FW_CONF_PATH, "w");
    if (fp == NULL)
        return -errno;

    fprintf(fp, "# retro-ws firewall.conf（fw_save_rules 生成）\n");
    fprintf(fp, "# action,proto,dir,src,dst,sport,dport,enabled,desc\n");

    for (int i = 0; i < g_rule_count; i++) {
        struct fw_rule *r = &g_rules[i];
        char src[20], dst[20];

        fw_ip_str(r->src_ip, src, sizeof(src));
        fw_ip_str(r->dst_ip, dst, sizeof(dst));
        fprintf(fp, "%s,%s,%s,%s,%s,%u,%u,%d,%s\n",
                fw_action_str(r->action), fw_proto_str(r->proto),
                fw_dir_str(r->dir), src, dst,
                r->src_port, r->dst_port, r->enabled ? 1 : 0,
                r->desc[0] ? r->desc : "-");
    }
    fclose(fp);

    return OK;
}

/*==========================
 *  防火墙控制
 *==========================*/

/**
 * 启用防火墙
 */
int fw_enable(void)
{
    g_firewall_enabled = true;
    syslog(LOG_INFO, "[FIREWALL] Enabled\n");
    return OK;
}

/**
 * 禁用防火墙
 */
int fw_disable(void)
{
    g_firewall_enabled = false;
    syslog(LOG_INFO, "[FIREWALL] Disabled (WARNING: all traffic allowed)\n");
    return OK;
}

/**
 * 获取防火墙状态
 */
void fw_get_status(bool *enabled, uint32_t *rules, uint32_t *blocked, uint32_t *allowed)
{
    *enabled = g_firewall_enabled;
    *rules   = g_rule_count;
    *blocked = g_blocked_count;
    *allowed = g_allowed_count;
}

/**
 * 打印防火墙状态
 */
void fw_print_status(void)
{
    bool enabled;
    uint32_t rules, blocked, allowed;
    fw_get_status(&enabled, &rules, &blocked, &allowed);

    printf("\n");
    printf("=== Firewall Status ===\n");
    printf("Enabled:  %s\n", enabled ? "YES" : "NO");
    printf("Rules:    %lu active\n", (unsigned long)rules);
    printf("Blocked:  %lu packets\n", (unsigned long)blocked);
    printf("Allowed:  %lu packets\n", (unsigned long)allowed);
    printf("Conn:     %lu tracked\n", (unsigned long)g_conn_count);
    printf("\n");

    printf("Rules:\n");
    printf("%-4s %-5s %-5s %-4s %-15s %-15s %-8s %s\n",
           "#", "Action", "Proto", "Dir", "Source", "Dest", "Port", "Description");
    printf("%-4s %-5s %-5s %-4s %-15s %-15s %-8s %s\n",
           "---", "------", "-----", "---", "-------", "----", "----", "-----------");

    for (int i = 0; i < g_rule_count; i++) {
        struct fw_rule *r = &g_rules[i];
        char sbuf[16], dbuf[16], pbuf[8];
        printf("%-4d %-5s %-5s %-4s %-15s %-15s %-8s %s\n",
               i,
               r->action == FW_ACTION_ALLOW ? "ALLOW" : "DENY",
               proto_to_str(r->proto),
               r->dir == FW_DIR_IN ? "IN" : (r->dir == FW_DIR_OUT ? "OUT" : "*"),
               ip_to_str(r->src_ip, sbuf, sizeof(sbuf)),
               ip_to_str(r->dst_ip, dbuf, sizeof(dbuf)),
               port_to_str(r->dst_port, pbuf, sizeof(pbuf)),
               r->desc);
    }
    printf("\n");
}

/**
 * 清除所有规则并重装默认规则 / Flush all rules and re-install defaults
 */
int fw_flush(void)
{
    memset(g_rules, 0, sizeof(g_rules));
    memset(g_conns, 0, sizeof(g_conns));
    g_rule_count = 0;
    g_conn_count = 0;
    g_blocked_count = 0;
    g_allowed_count = 0;

    /* 原实现清空后不恢复默认规则，防火墙退化为"无规则=全拒"
     * reload defaults, otherwise flushed == deny-everything-by-default */
    fw_load_default_rules();

    syslog(LOG_INFO, "[FIREWALL] Rules flushed, defaults reloaded\n");
    return OK;
}

/*==========================
 *  初始化
 *==========================*/

/**
 * 防火墙初始化
 */
int fw_init(void)
{
    syslog(LOG_INFO, "[FIREWALL] Initializing...\n");

    /* 清空规则和连接表 */
    memset(g_rules, 0, sizeof(g_rules));
    memset(g_conns, 0, sizeof(g_conns));
    g_rule_count   = 0;
    g_conn_count   = 0;
    g_blocked_count = 0;
    g_allowed_count = 0;

    /* 加载规则 */
    fw_load_rules();

    /* 默认启用 */
    g_firewall_enabled = true;

    syslog(LOG_INFO, "[FIREWALL] Initialized successfully\n");
    fw_print_status();

    return OK;
}

#endif /* CONFIG_RETRO_FIREWALL */
