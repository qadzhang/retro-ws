/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * cmd_wifi_main.c - NSH `wifi` 命令（WiFi 连接与配置管理）
 *
 * WHAT : wifi status/connect/save——查看状态、连接并保存凭据
 * WHY  : WiFi 凭据/静态 IP 属系统配置，存片上 /opt/etc/network.conf
 *        （HARDWARE 12.4，无 SD 卡可用）；开机由 wifi_auto_connect
 *        自动读取连接（REQUIREMENTS 2.2.4）
 * WHO  : NuttX builtin（retro-apps Application.mk 注册）
 * WHERE: retro-ws/src/nuttx/common/apps/system/cmd_wifi_main.c
 * WHEN : 2026-10-05 新增（配置文件策略补全）
 * HOW  : 薄壳转 network.c 的 wifi_* 接口与 wifi_conf；connect 即存
 *        （dhcp 缺省；static 需 ip/netmask/gateway [dns] 四五参）
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

#include "driver/wifi_conf.h"

/* network.h 若未提供原型则在此声明（薄壳不引实现头也可） */
int wifi_connect_and_save(const char *ssid, const char *password,
                          int ip_mode, const char *ip, const char *netmask,
                          const char *gateway, const char *dns);

int main(int argc, char *argv[])
{
    struct wifi_conf_s cf;

    if (argc < 2) {
        printf("用法: wifi status | wifi connect <ssid> <password> [static <ip> <netmask> <gateway> [dns]]\n");
        printf("  status            当前连接与配置（/opt/etc/network.conf）\n");
        printf("  connect ...       连接并把凭据存到片上（缺省 dhcp；static 需 IP 组）\n");
        printf("  配置文件: /opt/etc/network.conf（开机自动连接）\n");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        if (wifi_conf_load(&cf) != 0) {
            printf("配置: 未配置（wifi connect 首配后自动保存）\n");
        } else {
            printf("配置: ssid=%s  ip_mode=%s\n", cf.ssid,
                   cf.ip_mode == WIFI_IP_STATIC ? "static" : "dhcp");
            if (cf.ip_mode == WIFI_IP_STATIC)
                printf("      ip=%s netmask=%s gateway=%s dns=%s\n",
                       cf.ip, cf.netmask, cf.gateway,
                       cf.dns[0] ? cf.dns : "(none)");
        }
        return 0;
    }

    if (strcmp(argv[1], "connect") == 0) {
        int ret;

        if (argc < 4) {
            fprintf(stderr, "wifi: 缺 ssid/password 参数\n");
            return 1;
        }

        if (argc >= 8 && strcmp(argv[4], "static") == 0) {
            ret = wifi_connect_and_save(argv[2], argv[3], WIFI_IP_STATIC,
                                        argv[5], argv[6], argv[7],
                                        argc >= 9 ? argv[8] : NULL);
        } else {
            ret = wifi_connect_and_save(argv[2], argv[3], WIFI_IP_DHCP,
                                        NULL, NULL, NULL, NULL);
        }

        if (ret < 0) {
            fprintf(stderr, "wifi: 连接/保存失败 (%d)\n", ret);
            return 1;
        }
        printf("已连接并保存到 /opt/etc/network.conf（重启自动连接）\n");
        return 0;
    }

    fprintf(stderr, "wifi: 未知参数 %s（status/connect）\n", argv[1]);
    return 1;
}
