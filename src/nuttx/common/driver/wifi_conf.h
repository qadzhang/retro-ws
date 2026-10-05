/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * wifi_conf.h - WiFi/网络配置文件（/opt/etc/network.conf，纯函数件）
 *
 * WHAT : 网络配置（SSID/密码/IP 模式/静态 IP 组）的读写与 key=value 解析
 * WHY  : 系统配置一律存片上可写区（HARDWARE 12.4，无 SD 卡可用）——
 *        WiFi 凭据/固定 IP 属系统配置，不再依赖 SD 卡或编译期硬编码
 * WHO  : network.c（wifi_auto_connect 集成）、cmd_wifi（NSH）、宿主测试
 * WHERE: retro-ws/src/nuttx/common/driver/wifi_conf.[ch]
 * WHEN : 2026-10-05 新增（配置文件策略补全，用户指示）
 * HOW  : key=value 逐行文本；读序 /opt/etc/network.conf 首选 ->
 *        /mnt/sd0/network.conf 回退；写只写 /opt/etc（含建目录）；
 *        只用 stdio，宿主机可直链测试
 */

#ifndef __WIFI_CONF_H
#define __WIFI_CONF_H

#include <stdint.h>
#include <stddef.h>

#define WIFI_SSID_MAX   32
#define WIFI_PASSWD_MAX 64
#define WIFI_IPSTR_MAX  16   /* 点分十进制最长 15 + NUL */

/* IP 模式 */
enum wifi_ip_mode_e {
    WIFI_IP_DHCP = 0,
    WIFI_IP_STATIC = 1,
};

struct wifi_conf_s {
    char ssid[WIFI_SSID_MAX];
    char password[WIFI_PASSWD_MAX];
    int  ip_mode;                              /* wifi_ip_mode_e */
    char ip[WIFI_IPSTR_MAX];                   /* static 模式本机地址 */
    char netmask[WIFI_IPSTR_MAX];
    char gateway[WIFI_IPSTR_MAX];
    char dns[WIFI_IPSTR_MAX];
};

/*
 * WHAT : 从持久化存储读网络配置（首选 /opt/etc，SD 回退）
 * 返回 : OK / -ENOENT（两处均无配置文件）/ -EINVAL（文件解析为空）
 */
int wifi_conf_load(struct wifi_conf_s *cf);

/*
 * WHAT : 保存网络配置到片上 /opt/etc/network.conf（自动建目录）
 * 返回 : OK / 负错误码
 */
int wifi_conf_save(const struct wifi_conf_s *cf);

/*
 * WHAT : 校验静态 IP 字段的点分十进制格式（每段 0-255）
 * 返回 : true/false（ip_mode != STATIC 时恒 true）
 */
int wifi_conf_valid(const struct wifi_conf_s *cf);

/*
 * WHAT : 单行 key=value 解析进配置结构（宿主测试逐键用）
 * HOW  : 已知键：ssid/password/ip_mode/ip/netmask/gateway/dns；
 *        未知键忽略返回 -ENOENT，值超长返回 -ENOSPC
 */
int wifi_conf_parse_line(struct wifi_conf_s *cf, const char *line);

#endif /* __WIFI_CONF_H */
