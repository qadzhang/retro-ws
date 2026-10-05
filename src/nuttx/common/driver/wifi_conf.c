/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * wifi_conf.c - WiFi/网络配置文件读写（纯 stdio，宿主可测）
 *
 * WHAT/WHY/WHERE/HOW 见 wifi_conf.h 头注释（唯一事实来源）
 * WHEN : 2026-10-05 新增；宿主测试 test_wifi_conf.c 锁解析性质
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "wifi_conf.h"

/* 安装路径（宿主测试可 -D 覆盖进沙箱，学 pkg_manager 前缀注入） */
#ifndef WIFI_CONF_PATH_PRIMARY
#  define WIFI_CONF_PATH_PRIMARY "/opt/etc/network.conf"
#endif
#ifndef WIFI_CONF_PATH_SD
#  define WIFI_CONF_PATH_SD      "/mnt/sd0/network.conf"
#endif
#ifndef WIFI_CONF_DIR_PRIMARY
#  define WIFI_CONF_DIR_PRIMARY "/opt/etc"
#endif

/* 拷贝 value 进定长字段；超长截断拒绝（防半截密码连不上） */
static int field_copy(char *dst, size_t dstsz, const char *val)
{
    if (strlen(val) >= dstsz)
        return -ENOSPC;
    strcpy(dst, val);
    return 0;
}

int wifi_conf_parse_line(struct wifi_conf_s *cf, const char *line)
{
    const char *eq;
    char key[16];
    size_t klen;
    const char *val;
    char *nl;

    if (cf == NULL || line == NULL)
        return -ENOENT;

    eq = strchr(line, '=');
    if (eq == NULL)
        return -ENOENT;

    klen = (size_t)(eq - line);
    if (klen == 0 || klen >= sizeof(key))
        return -ENOENT;

    memcpy(key, line, klen);
    key[klen] = '\0';
    val = eq + 1;

    /* 剥行尾换行与空白 */
    {
        static char vbuf[WIFI_PASSWD_MAX + 32];
        size_t vlen = strlen(val);

        if (vlen >= sizeof(vbuf))
            return -ENOSPC;
        memcpy(vbuf, val, vlen + 1);
        nl = vbuf + strlen(vbuf);
        while (nl > vbuf && (nl[-1] == '\n' || nl[-1] == '\r' ||
                             nl[-1] == ' ' || nl[-1] == '\t'))
            *--nl = '\0';
        val = vbuf;
    }

    if (strcmp(key, "ssid") == 0)
        return field_copy(cf->ssid, sizeof(cf->ssid), val);
    if (strcmp(key, "password") == 0)
        return field_copy(cf->password, sizeof(cf->password), val);
    if (strcmp(key, "ip_mode") == 0) {
        if (strcmp(val, "dhcp") == 0)
            cf->ip_mode = WIFI_IP_DHCP;
        else if (strcmp(val, "static") == 0)
            cf->ip_mode = WIFI_IP_STATIC;
        else
            return -EINVAL;
        return 0;
    }
    if (strcmp(key, "ip") == 0)
        return field_copy(cf->ip, sizeof(cf->ip), val);
    if (strcmp(key, "netmask") == 0)
        return field_copy(cf->netmask, sizeof(cf->netmask), val);
    if (strcmp(key, "gateway") == 0)
        return field_copy(cf->gateway, sizeof(cf->gateway), val);
    if (strcmp(key, "dns") == 0)
        return field_copy(cf->dns, sizeof(cf->dns), val);

    return -ENOENT;   /* 未知键忽略 */
}

/* 点分十进制校验：四段、每段 0-255、无多余字符 */
static int ipv4_ok(const char *s)
{
    int parts = 0;
    uint32_t acc = 0;
    int digits = 0;

    if (s == NULL || *s == '\0')
        return 0;

    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') {
            acc = acc * 10 + (uint32_t)(*p - '0');
            if (++digits > 3 || acc > 255)
                return 0;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0 || ++parts > 4)
                return 0;
            if (*p == '\0')
                break;
            acc = 0;
            digits = 0;
        } else {
            return 0;
        }
    }

    return parts == 4;
}

int wifi_conf_valid(const struct wifi_conf_s *cf)
{
    if (cf == NULL)
        return 0;
    if (cf->ip_mode != WIFI_IP_STATIC)
        return 1;
    return ipv4_ok(cf->ip) && ipv4_ok(cf->netmask) && ipv4_ok(cf->gateway) &&
           (cf->dns[0] == '\0' || ipv4_ok(cf->dns));
}

static int conf_read_from(const char *path, struct wifi_conf_s *cf)
{
    FILE *fp = fopen(path, "r");
    char line[192];
    int got = 0;

    if (fp == NULL)
        return -ENOENT;

    memset(cf, 0, sizeof(*cf));
    cf->ip_mode = WIFI_IP_DHCP;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (wifi_conf_parse_line(cf, line) >= 0)
            got++;
    }
    fclose(fp);

    return got > 0 ? 0 : -EINVAL;
}

int wifi_conf_load(struct wifi_conf_s *cf)
{
    int ret;

    if (cf == NULL)
        return -EINVAL;

    /* 首选片上（无 SD 卡可用），SD 卡作为搬运/覆盖渠道 */
    ret = conf_read_from(WIFI_CONF_PATH_PRIMARY, cf);
    if (ret == 0 && cf->ssid[0] != '\0')
        return 0;
    if (ret == 0)
        return -EINVAL;    /* 有文件但无有效键 */

    return conf_read_from(WIFI_CONF_PATH_SD, cf);
}

/* 逐级建目录（mkdir 只建末级，/opt/etc 可能两级都不存在） */
static void mkdirs(const char *path)
{
    char tmp[128];
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0)
        return;

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int wifi_conf_save(const struct wifi_conf_s *cf)
{
    FILE *fp;

    if (cf == NULL || cf->ssid[0] == '\0')
        return -EINVAL;

    if (!wifi_conf_valid(cf))
        return -EINVAL;

    mkdirs(WIFI_CONF_DIR_PRIMARY);
    fp = fopen(WIFI_CONF_PATH_PRIMARY, "w");
    if (fp == NULL)
        return -errno;

    fprintf(fp, "# retro-ws network.conf（wifi_conf_save 生成）\n");
    fprintf(fp, "ssid=%s\n", cf->ssid);
    fprintf(fp, "password=%s\n", cf->password);
    fprintf(fp, "ip_mode=%s\n",
            cf->ip_mode == WIFI_IP_STATIC ? "static" : "dhcp");
    if (cf->ip_mode == WIFI_IP_STATIC) {
        fprintf(fp, "ip=%s\n", cf->ip);
        fprintf(fp, "netmask=%s\n", cf->netmask);
        fprintf(fp, "gateway=%s\n", cf->gateway);
        if (cf->dns[0] != '\0')
            fprintf(fp, "dns=%s\n", cf->dns);
    }
    fclose(fp);

    return 0;
}
