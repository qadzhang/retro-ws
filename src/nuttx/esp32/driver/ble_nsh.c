/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ble_nsh.c - BLE NSH 命令
 *
 * WHAT : BLE NSH 命令
 * WHY  : ble list/scan/pair/unpair/status 管理
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32/driver/ble_nsh.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 命令行封装 ble_hid 接口
 */

/**
 * ble_nsh.c - BLE HID NSH 命令
 *
 * 提供 NSH 命令行接口来管理 BLE 配对
 *
 * 用法:
 *   ble list     - 列出已配对设备
 *   ble scan     - 扫描附近的 HID 设备
 *   ble pair     - 进入配对模式
 *   ble unpair N - 删除第 N 个配对设备
 *   ble unpair all - 删除所有配对设备
 *   ble status   - 显示连接状态
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * 编译开关说明（2026-10-04）/ Build-switch note:
 * 本文件依赖的 ble_hid/ble_storage 实现基于 ESP-IDF/Bluedroid，
 * NuttX 构建下无法编译；且直接引用了 ble_hid.c 的 static 变量
 * g_ble_initialized（跨编译单元不可见，重写时需改为查询 API）。
 * NuttX 路线的 BLE 移植将基于 NimBLE，见 NEXT_STEPS。
 * CONFIG_RETRO_BLE_STACK_IDF 是为未来 IDF 构建保留的符号，当前任何
 * 配置都不定义它——即本文件整体编译关闭。
 * Depends on the IDF-based ble_hid/ble_storage (not buildable under
 * NuttX) and references ble_hid.c's static g_ble_initialized (needs an
 * accessor API when re-enabled). NimBLE port is tracked in NEXT_STEPS;
 * CONFIG_RETRO_BLE_STACK_IDF is defined nowhere today.
 */
#if defined(CONFIG_RETRO_INPUT_BLE_HID) && defined(CONFIG_RETRO_BLE_STACK_IDF)

#include "driver/ble_hid.h"

/*==========================
 *  命令实现
 *==========================*/

/**
 * ble list - 列出已配对设备
 */
static int cmd_ble_list(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!g_ble_initialized) {
        printf("[BLE] Not initialized. Run 'ble init' first.\n");
        return 1;
    }

    printf("\n");
    printf("=== BLE Paired Devices ===\n");
    printf("Connection state: %s\n", ble_hid_get_state_str());
    printf("\n");

    ble_storage_list_all();

    return 0;
}

/**
 * ble scan - 扫描附近的 HID 设备
 */
static int cmd_ble_scan(int argc, char **argv)
{
    int timeout = CONFIG_RETRO_BLE_SCAN_TIMEOUT;

    if (!g_ble_initialized) {
        printf("[BLE] Not initialized. Run 'ble init' first.\n");
        return 1;
    }

    if (argc > 1) {
        timeout = atoi(argv[1]);
        if (timeout <= 0 || timeout > 30) {
            printf("[BLE] Invalid timeout (1-30 seconds)\n");
            return 1;
        }
    }

    printf("[BLE] Scanning for HID devices...\n");
    printf("[BLE] Put your keyboard/mouse in pairing mode\n");
    printf("[BLE] Timeout: %d seconds\n", timeout);

    ble_hid_start_scan();

    /* 注意: 扫描是异步的，结果会通过事件返回
     * 实际使用中，用户应该 'ble status' 查看结果
     */

    return 0;
}

/**
 * ble pair - 进入配对模式
 */
static int cmd_ble_pair(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!g_ble_initialized) {
        printf("[BLE] Not initialized. Run 'ble init' first.\n");
        return 1;
    }

    printf("[BLE] Entering pairing mode...\n");
    printf("\n");
    printf("Steps to pair:\n");
    printf("  1. Put your Bluetooth keyboard/mouse in pairing mode\n");
    printf("     (usually by holding the Bluetooth button for 3-5 seconds)\n");
    printf("  2. The LED on your keyboard should start blinking\n");
    printf("  3. ESP32-CAM will automatically find and connect\n");
    printf("  4. LED will change to slow flash when connected\n");
    printf("\n");

    ble_hid_start_scan();

    return 0;
}

/**
 * ble unpair - 删除配对设备
 */
static int cmd_ble_unpair(int argc, char **argv)
{
    int index;
    char *endptr;

    if (!g_ble_initialized) {
        printf("[BLE] Not initialized. Run 'ble init' first.\n");
        return 1;
    }

    if (argc < 2) {
        printf("Usage: ble unpair <index|'all'>\n");
        printf("\n");
        printf("Examples:\n");
        printf("  ble unpair 0     - Remove first paired device\n");
        printf("  ble unpair all   - Remove all paired devices\n");
        printf("\n");
        printf("Use 'ble list' to see device indices\n");
        return 1;
    }

    if (strcmp(argv[1], "all") == 0) {
        printf("[BLE] Removing all paired devices...\n");
        ble_hid_clear_all_bonds();
        printf("[BLE] All bonds cleared\n");
        return 0;
    }

    index = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || index < 0 || index >= CONFIG_RETRO_BLE_MAX_BOND) {
        printf("[BLE] Invalid device index\n");
        return 1;
    }

    printf("[BLE] Removing device at index %d...\n", index);
    int ret = ble_hid_delete_bond(index);
    if (ret == 0) {
        printf("[BLE] Device removed\n");
    } else {
        printf("[BLE] Failed to remove device (error %d)\n", ret);
    }

    return 0;
}

/**
 * ble status - 显示连接状态
 */
static int cmd_ble_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!g_ble_initialized) {
        printf("[BLE] Not initialized. Run 'ble init' first.\n");
        return 1;
    }

    printf("\n");
    printf("=== BLE HID Status ===\n");
    printf("State: %s\n", ble_hid_get_state_str());
    printf("\n");

    /* 显示已配对设备 */
    int count = ble_hid_get_paired_count();
    printf("Paired devices: %d\n", count);

    if (count > 0) {
        char addr_str[32];
        struct ble_bond_s *bond;
        int i;

        printf("\n");
        for (i = 0; i < CONFIG_RETRO_BLE_MAX_BOND; i++) {
            bond = ble_hid_get_paired_device(i);
            if (bond != NULL) {
                ble_storage_addr2str(bond->addr, addr_str, sizeof(addr_str));
                printf("  [%d] %s\n", i, bond->name);
                printf("      MAC: %s\n", addr_str);
                printf("\n");
            }
        }
    }

    return 0;
}

/**
 * ble init - 初始化 BLE
 */
static int cmd_ble_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int ret;

    if (g_ble_initialized) {
        printf("[BLE] Already initialized\n");
        return 0;
    }

    printf("[BLE] Initializing BLE HID...\n");
    ret = ble_hid_init();
    if (ret != 0) {
        printf("[BLE] Initialization failed: %d\n", ret);
        return 1;
    }

    printf("[BLE] Initialization complete\n");
    return 0;
}

/*==========================
 *  命令表
 *==========================*/

typedef int (*ble_cmd_func_t)(int argc, char **argv);

struct ble_command_s {
    const char *name;
    ble_cmd_func_t func;
    const char *desc;
};

static const struct ble_command_s g_ble_commands[] = {
    { "init",    cmd_ble_init,    "Initialize BLE HID"    },
    { "list",    cmd_ble_list,    "List paired devices"   },
    { "scan",    cmd_ble_scan,    "Scan for HID devices"  },
    { "pair",    cmd_ble_pair,    "Enter pairing mode"    },
    { "unpair",  cmd_ble_unpair,  "Remove paired device"  },
    { "status",  cmd_ble_status,  "Show connection status"},
};

#define NUM_BLE_CMDS (sizeof(g_ble_commands) / sizeof(g_ble_commands[0]))

/**
 * ble 命令主入口
 */
int cmd_ble(int argc, char **argv)
{
    int i;

    if (argc < 2) {
        printf("BLE HID commands:\n");
        for (i = 0; i < (int)NUM_BLE_CMDS; i++) {
            printf("  %-10s - %s\n",
                   g_ble_commands[i].name,
                   g_ble_commands[i].desc);
        }
        printf("\n");
        printf("Usage: ble <command> [args]\n");
        printf("Example: ble pair\n");
        return 0;
    }

    const char *subcmd = argv[1];

    for (i = 0; i < (int)NUM_BLE_CMDS; i++) {
        if (strcmp(subcmd, g_ble_commands[i].name) == 0) {
            return g_ble_commands[i].func(argc - 1, &argv[1]);
        }
    }

    printf("[BLE] Unknown command: %s\n", subcmd);
    printf("Use 'ble' without arguments to see available commands\n");
    return 1;
}

#endif /* CONFIG_RETRO_INPUT_BLE_HID && CONFIG_RETRO_BLE_STACK_IDF */
