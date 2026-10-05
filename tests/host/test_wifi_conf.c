/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_wifi_conf.c - network.conf 解析/校验/往返测试
 *
 * WHAT : wifi_conf 纯函数的契约测试（key=value 解析、IP 校验、存取往返）
 * WHY  : WiFi 凭据/静态 IP 写错一格网络就断——配置文件是唯一事实来源，
 *        解析性质必须宿主锁死（ai-code-testing L1 契约 + 蜕变往返）
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: retro-ws/tests/host/test_wifi_conf.c
 * WHEN : 2026-10-05 新增（配置文件策略补全）
 * HOW  : CHECK 断言：合法/非法键、值截断拒绝、ip_mode 枚举、点分十进制
 *        四段校验、save->load 往返一致、静态 IP 缺字段拒绝保存
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "test_framework.h"

#include "wifi_conf.h"

static void test_parse_line(void)
{
    struct wifi_conf_s cf;

    memset(&cf, 0, sizeof(cf));
    cf.ip_mode = WIFI_IP_DHCP;

    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "ssid=HomeNet"), 0);
    CHECK_STR_EQ(cf.ssid, "HomeNet");
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "password=p@ss w0rd"), 0);
    CHECK_STR_EQ(cf.password, "p@ss w0rd");

    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "ip_mode=static"), 0);
    CHECK_EQ_INT(cf.ip_mode, WIFI_IP_STATIC);
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "ip_mode=dhcp"), 0);
    CHECK_EQ_INT(cf.ip_mode, WIFI_IP_DHCP);
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "ip_mode=whatever"), -EINVAL);

    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "ip=192.168.1.10\n"), 0);
    CHECK_STR_EQ(cf.ip, "192.168.1.10");       /* 行尾换行剥除 */
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "dns=8.8.8.8  "), 0);
    CHECK_STR_EQ(cf.dns, "8.8.8.8");           /* 尾空白剥除 */

    /* 未知键忽略 / 畸形行拒绝 / 防御参数 */
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "hostname=x"), -ENOENT);
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, "novalue"), -ENOENT);
    CHECK_EQ_INT(wifi_conf_parse_line(NULL, "ssid=a"), -ENOENT);
    CHECK_EQ_INT(wifi_conf_parse_line(&cf, NULL), -ENOENT);

    /* 超长值拒绝（不留半截密码） */
    {
        char big[128];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        CHECK_EQ_INT(wifi_conf_parse_line(&cf, big), -ENOENT); /* 无 '=' */
        {
            char kv[144];
            snprintf(kv, sizeof(kv), "password=%s", big);
            CHECK_EQ_INT(wifi_conf_parse_line(&cf, kv), -ENOSPC);
        }
    }
}

static void test_ipv4_valid(void)
{
    struct wifi_conf_s cf;

    memset(&cf, 0, sizeof(cf));

    /* dhcp 模式恒通过 */
    cf.ip_mode = WIFI_IP_DHCP;
    CHECK_EQ_INT(wifi_conf_valid(&cf), 1);

    /* static 合法组 */
    cf.ip_mode = WIFI_IP_STATIC;
    strcpy(cf.ip, "192.168.1.10");
    strcpy(cf.netmask, "255.255.255.0");
    strcpy(cf.gateway, "192.168.1.1");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 1);
    strcpy(cf.dns, "223.5.5.5");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 1);

    /* 段越界 / 三段 / 空段 / 尾 garbage / DNS 可空 */
    strcpy(cf.ip, "256.1.1.1");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 0);
    strcpy(cf.ip, "192.168.1");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 0);
    strcpy(cf.ip, "192..1.1");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 0);
    strcpy(cf.ip, "192.168.1.1x");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 0);
    strcpy(cf.ip, "192.168.1.10");
    strcpy(cf.dns, "");
    CHECK_EQ_INT(wifi_conf_valid(&cf), 1);

    CHECK_EQ_INT(wifi_conf_valid(NULL), 0);
}

static void test_save_load_roundtrip(void)
{
    struct wifi_conf_s cf, back;

    /* 路径已由 run_all 以 -D 注入沙箱（蜕变：save->load 往返一致） */
    memset(&cf, 0, sizeof(cf));
    CHECK_EQ_INT(wifi_conf_save(&cf), -EINVAL);   /* 空 ssid 拒绝 */

    strcpy(cf.ssid, "HomeNet");
    strcpy(cf.password, "p@ss");
    cf.ip_mode = WIFI_IP_STATIC;
    strcpy(cf.ip, "10.0.0.999");                  /* 非法 IP 拒绝保存 */
    strcpy(cf.netmask, "255.0.0.0");
    strcpy(cf.gateway, "10.0.0.1");
    CHECK_EQ_INT(wifi_conf_save(&cf), -EINVAL);

    strcpy(cf.ip, "10.0.0.8");
    strcpy(cf.dns, "223.5.5.5");
    CHECK_EQ_INT(wifi_conf_save(&cf), 0);

    memset(&back, 0, sizeof(back));
    CHECK_EQ_INT(wifi_conf_load(&back), 0);
    CHECK_STR_EQ(back.ssid, "HomeNet");
    CHECK_STR_EQ(back.password, "p@ss");
    CHECK_EQ_INT(back.ip_mode, WIFI_IP_STATIC);
    CHECK_STR_EQ(back.ip, "10.0.0.8");
    CHECK_STR_EQ(back.netmask, "255.0.0.0");
    CHECK_STR_EQ(back.gateway, "10.0.0.1");
    CHECK_STR_EQ(back.dns, "223.5.5.5");

    /* 片上文件删除后回退读 SD 卡副本（搬运渠道语义） */
    unlink(WIFI_CONF_PATH_PRIMARY);
    {
        FILE *fp = fopen(WIFI_CONF_PATH_SD, "w");
        CHECK(fp != NULL);
        if (fp) {
            fprintf(fp, "ssid=SDNet\npassword=sdpwd\nip_mode=dhcp\n");
            fclose(fp);
        }
    }
    memset(&back, 0, sizeof(back));
    CHECK_EQ_INT(wifi_conf_load(&back), 0);
    CHECK_STR_EQ(back.ssid, "SDNet");
    CHECK_EQ_INT(back.ip_mode, WIFI_IP_DHCP);

    /* 两处皆无 -> -ENOENT */
    unlink(WIFI_CONF_PATH_SD);
    CHECK_EQ_INT(wifi_conf_load(&back), -ENOENT);
}

int main(void)
{
    test_parse_line();
    test_ipv4_valid();
    test_save_load_roundtrip();

    printf("wifi_conf: %d checks, %d failed\n", g_check_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
