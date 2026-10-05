/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ble_storage.c - BLE Bond 存储
 *
 * WHAT : BLE Bond 存储
 * WHY  : 配对信息持久化（重启自动重连）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32/driver/ble_storage.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : Bond 写入片上 Flash 分区
 */

/**
 * ble_storage.c - BLE Bond 信息 Flash 存储
 *
 * 负责配对信息的持久化存储，支持:
 * - 存储最多 N 个配对设备的 Bond 信息
 * - 读取/删除配对信息
 * - 自动加载已配对设备
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "board.h"

/*
 * 编译开关说明（2026-10-04）/ Build-switch note:
 * 本文件与 ble_hid/ble_nsh/ble_pair_ui 属同一 IDF/Bluedroid BLE 组，
 * NuttX 构建下无法编译；NuttX 路线的 BLE 移植将基于 NimBLE，见
 * NEXT_STEPS。CONFIG_RETRO_BLE_STACK_IDF 是为未来 IDF 构建保留的
 * 符号，当前任何配置都不定义它——即本文件整体编译关闭。
 * （已核实 src/nuttx/common 下没有任何文件引用 ble_storage 符号，
 * 外部仅 ble_nsh/ble_pair_ui 使用，二者一并关闭。）
 * Part of the same IDF-based BLE set; NimBLE port tracked in
 * NEXT_STEPS. CONFIG_RETRO_BLE_STACK_IDF is defined nowhere today.
 * Verified: nothing under src/nuttx/common references ble_storage;
 * the only callers (ble_nsh/ble_pair_ui) are wrapped off with it.
 */
#if defined(CONFIG_RETRO_INPUT_BLE_HID) && defined(CONFIG_RETRO_BLE_STACK_IDF)

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_BLE_MAX_BOND
#define CONFIG_RETRO_BLE_MAX_BOND  3
#endif

#define BLE_STORAGE_PATH           "/opt/var/ble_bond.dat"
#define BLE_STORAGE_MAGIC         0x424C4530  /* "BLE0" */
#define BLE_STORAGE_VERSION        1

/*==========================
 *  Bond 数据结构
 *==========================*/

/**
 * 单个设备的 Bond 信息
 */
struct ble_bond_s {
    uint8_t  addr_type;        /* 0=PUBLIC, 1=RANDOM */
    uint8_t  addr[6];          /* 设备 MAC 地址 */
    uint8_t  ltk[16];         /* Long Term Key */
    uint8_t  irk[16];         /* Identity Resolving Key */
    uint8_t  csrk[16];        /* Connection Signature Resolving Key */
    uint8_t  ediv;             /* EDIV (encrypted diversifier) */
    uint8_t  rand[8];         /* LTK 随机数 */
    char     name[32];         /* 设备名称 */
    bool     valid;            /* 该条目是否有效 */
};

/**
 * Bond 存储头
 */
struct ble_storage_header_s {
    uint32_t magic;            /* 魔数 */
    uint8_t  version;          /* 版本号 */
    uint8_t  count;            /* 已配对设备数 */
    uint8_t  reserved[2];      /* 保留 */
};

/**
 * 完整的 Bond 存储文件
 */
struct ble_storage_s {
    struct ble_storage_header_s header;
    struct ble_bond_s devices[CONFIG_RETRO_BLE_MAX_BOND];
};

/*==========================
 *  内部状态
 *==========================*/

static struct ble_storage_s g_bond_storage;
static bool g_storage_loaded = false;

/*==========================
 *  工具函数
 *==========================*/

/**
 * 将 MAC 地址转换为字符串
 */
void ble_storage_addr2str(uint8_t *addr, char *str, size_t len)
{
    snprintf(str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2],
             addr[3], addr[4], addr[5]);
}

/**
 * 比较两个 MAC 地址
 */
bool ble_storage_addr_match(uint8_t *a, uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

/**
 * 获取存储路径
 */
const char *ble_storage_get_path(void)
{
    return BLE_STORAGE_PATH;
}

/*==========================
 *  存储操作
 *==========================*/

/**
 * 初始化存储，加载已存在的 Bond 信息
 */
int ble_storage_init(void)
{
    int fd;
    ssize_t nbytes;

    /* 检查是否已加载 */
    if (g_storage_loaded) {
        return 0;
    }

    /* 初始化默认值 */
    memset(&g_bond_storage, 0, sizeof(g_bond_storage));
    g_bond_storage.header.magic = BLE_STORAGE_MAGIC;
    g_bond_storage.header.version = BLE_STORAGE_VERSION;

    /* 尝试从 Flash 加载 */
    fd = open(BLE_STORAGE_PATH, O_RDONLY);
    if (fd < 0) {
        /* 文件不存在，使用默认空存储 */
        syslog(LOG_INFO, "[BLE] No bond storage found, starting fresh\n");
        g_storage_loaded = true;
        return 0;
    }

    nbytes = read(fd, &g_bond_storage, sizeof(g_bond_storage));
    close(fd);

    if (nbytes != sizeof(g_bond_storage)) {
        syslog(LOG_ERR, "[BLE] Bond storage read error: %d bytes\n", (int)nbytes);
        /* 重新初始化 */
        memset(&g_bond_storage, 0, sizeof(g_bond_storage));
        g_bond_storage.header.magic = BLE_STORAGE_MAGIC;
        g_bond_storage.header.version = BLE_STORAGE_VERSION;
        g_storage_loaded = true;
        return 0;
    }

    /* 验证魔数 */
    if (g_bond_storage.header.magic != BLE_STORAGE_MAGIC) {
        syslog(LOG_ERR, "[BLE] Invalid bond storage magic\n");
        memset(&g_bond_storage, 0, sizeof(g_bond_storage));
        g_bond_storage.header.magic = BLE_STORAGE_MAGIC;
        g_bond_storage.header.version = BLE_STORAGE_VERSION;
        g_storage_loaded = true;
        return 0;
    }

    /* 验证版本 */
    if (g_bond_storage.header.version != BLE_STORAGE_VERSION) {
        syslog(LOG_WARNING, "[BLE] Bond storage version mismatch: %d vs %d\n",
               g_bond_storage.header.version, BLE_STORAGE_VERSION);
        /* 暂时忽略版本不匹配 */
    }

    syslog(LOG_INFO, "[BLE] Loaded %d bonded devices\n", g_bond_storage.header.count);
    g_storage_loaded = true;

    return 0;
}

/**
 * 保存 Bond 信息到 Flash
 */
int ble_storage_save(void)
{
    int fd;
    ssize_t nbytes;

    /* 确保目录存在 */
    /* TODO: 检查 /mnt/sd0 是否挂载 */

    /* 写入存储 */
    mkdir("/opt/var", 0755);   /* 片上可写区（无 SD 卡可用，2026-10-05） */
    fd = open(BLE_STORAGE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "[BLE] Failed to open bond storage for write: %d\n", errno);
        return -ENOENT;
    }

    nbytes = write(fd, &g_bond_storage, sizeof(g_bond_storage));
    close(fd);

    if (nbytes != sizeof(g_bond_storage)) {
        syslog(LOG_ERR, "[BLE] Failed to write bond storage: %d bytes\n", (int)nbytes);
        return -EIO;
    }

    syslog(LOG_INFO, "[BLE] Bond storage saved (%d devices)\n", g_bond_storage.header.count);
    return 0;
}

/**
 * 添加一个配对设备
 */
int ble_storage_add_bond(struct ble_bond_s *bond)
{
    int i;

    if (!g_storage_loaded) {
        ble_storage_init();
    }

    /* 检查是否已存在 */
    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        if (g_bond_storage.devices[i].valid &&
            ble_storage_addr_match(g_bond_storage.devices[i].addr, bond->addr)) {
            /* 已存在，更新 */
            syslog(LOG_INFO, "[BLE] Updating existing bond for device\n");
            memcpy(&g_bond_storage.devices[i], bond, sizeof(struct ble_bond_s));
            return ble_storage_save();
        }
    }

    /* 找一个空槽位 */
    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        if (!g_bond_storage.devices[i].valid) {
            memcpy(&g_bond_storage.devices[i], bond, sizeof(struct ble_bond_s));
            g_bond_storage.header.count++;
            syslog(LOG_INFO, "[BLE] Added new bond at slot %d\n", i);
            return ble_storage_save();
        }
    }

    /* 没有空槽位 */
    syslog(LOG_ERR, "[BLE] No space for new bond (max %d)\n", CONFIG_RETRO_BLE_MAX_BOND);
    return -ENOSPC;
}

/**
 * 删除一个配对设备
 */
int ble_storage_remove_bond_by_addr(uint8_t *addr)
{
    int i;

    if (!g_storage_loaded) {
        ble_storage_init();
    }

    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        if (g_bond_storage.devices[i].valid &&
            ble_storage_addr_match(g_bond_storage.devices[i].addr, addr)) {
            /* 找到，删除 */
            memset(&g_bond_storage.devices[i], 0, sizeof(struct ble_bond_s));
            g_bond_storage.header.count--;
            syslog(LOG_INFO, "[BLE] Removed bond at slot %d\n", i);
            return ble_storage_save();
        }
    }

    return -ENOENT;
}

/**
 * 删除第 N 个配对设备 (0-based)
 */
int ble_storage_remove_bond_by_index(int index)
{
    if (!g_storage_loaded) {
        ble_storage_init();
    }

    if (index < 0 || index >= CONFIG_RETRO_BLE_MAX_BOND) {
        return -EINVAL;
    }

    if (!g_bond_storage.devices[index].valid) {
        return -ENOENT;
    }

    memset(&g_bond_storage.devices[index], 0, sizeof(struct ble_bond_s));
    g_bond_storage.header.count--;
    return ble_storage_save();
}

/**
 * 删除所有配对设备
 */
int ble_storage_clear_all(void)
{
    if (!g_storage_loaded) {
        ble_storage_init();
    }

    memset(&g_bond_storage.devices, 0, sizeof(g_bond_storage.devices));
    g_bond_storage.header.count = 0;
    return ble_storage_save();
}

/**
 * 获取已配对设备数量
 */
int ble_storage_get_count(void)
{
    if (!g_storage_loaded) {
        ble_storage_init();
    }
    return g_bond_storage.header.count;
}

/**
 * 获取第 N 个已配对设备
 */
struct ble_bond_s *ble_storage_get_bond(int index)
{
    if (!g_storage_loaded) {
        ble_storage_init();
    }

    if (index < 0 || index >= CONFIG_RETRO_BLE_MAX_BOND) {
        return NULL;
    }

    if (!g_bond_storage.devices[index].valid) {
        return NULL;
    }

    return &g_bond_storage.devices[index];
}

/**
 * 获取第一个有效设备（用于自动连接）
 */
struct ble_bond_s *ble_storage_get_first_valid(void)
{
    int i;

    if (!g_storage_loaded) {
        ble_storage_init();
    }

    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        if (g_bond_storage.devices[i].valid) {
            return &g_bond_storage.devices[i];
        }
    }

    return NULL;
}

/**
 * 打印所有已配对设备
 */
void ble_storage_list_all(void)
{
    int i;
    char addr_str[32];

    if (!g_storage_loaded) {
        ble_storage_init();
    }

    printf("\n=== Bonded Devices (%d/%d) ===\n",
           g_bond_storage.header.count, CONFIG_RETRO_BLE_MAX_BOND);

    for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
        if (g_bond_storage.devices[i].valid) {
            ble_storage_addr2str(g_bond_storage.devices[i].addr, addr_str, sizeof(addr_str));
            printf("  [%d] %s\n", i, g_bond_storage.devices[i].name);
            printf("      MAC: %s\n", addr_str);
            printf("      Type: %s\n",
                   g_bond_storage.devices[i].addr_type == 0 ? "PUBLIC" : "RANDOM");
            printf("\n");
        }
    }

    if (g_bond_storage.header.count == 0) {
        printf("  No bonded devices\n\n");
    }
}

#endif /* CONFIG_RETRO_INPUT_BLE_HID && CONFIG_RETRO_BLE_STACK_IDF */
