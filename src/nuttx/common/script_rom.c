/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_scripts_rom.c - 板级脚本 ROM 化（内存盘 + XIP 直查 + script 命令）
 *
 * WHAT : (1) 把 ROMFS 镜像（scripts_romfs.c，tools/mkromfs.py 生成）
 *        注册为内存块设备 /dev/rom0 并挂载到 /rom/scripts；
 *        (2) retro_romfs_find() 直查镜像内文件数据指针（Flash 执行）；
 *        (3) NSH builtin 命令 script：按名字/路径执行 ROM 脚本
 * WHY  : 用户要求每板脚本目录编译期二进制进 ROM，运行时从 ROM
 *        直跑不加载 RAM（XIP 语义，HARDWARE.md 13.4；对标
 *        MicroPython frozen bytecode——源码常驻 Flash，引擎 parser
 *        直接消费指针，Berry 编译产物落堆、跑完即释放）
 * WHO  : retro_boot（挂载）；NSH 用户（script 命令）
 * WHERE: retro-ws/src/nuttx/common/script_rom.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 块设备 = 最小 struct block_operations（geometry/read），
 *        512B 扇区只读；镜像数组在 .rodata（Flash XIP 段）；
 *        ROMFS 解析：卷头 -> 根目录链 -> 名字匹配 -> 数据指针
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_SCRIPTS_ROM

#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <nuttx/fs/fs.h>

/* 生成的镜像（build_firmware.sh 调 tools/mkromfs.py 产出） */
extern const uint8_t g_scripts_romfs[];
extern const size_t g_scripts_romfs_len;

/*==========================
 *  ROMFS 只读解析（与 NuttX fs/romfs/fs_romfs.h 布局一致）
 *==========================*/

#define RFNEXT_MODEMASK    7
#define RFNEXT_DIRECTORY   1
#define RFNEXT_FILE        2

static uint32_t romfs_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int romfs_valid(void)
{
    return g_scripts_romfs_len >= 64 &&
           memcmp(g_scripts_romfs, "-rom1fs-", 8) == 0;
}

/*
 * WHAT : 在镜像根目录链上按名查找 / find file in root chain
 * 返回 : OK 且 *data/*len 指向 Flash 内数据；-ENOENT 未找到
 */
static int romfs_find(const char *name, const uint8_t **data,
                      size_t *len)
{
    if (!romfs_valid() || name == NULL)
        return -ENOENT;

    const uint8_t *img = g_scripts_romfs;

    /* 卷名结束位置 -> 根条目 */
    int vh = 16;
    while (img[vh] != 0 && vh < 16 + 256)
        vh++;
    uint32_t off = ((vh + 1) + 15) & ~15;

    /* 根条目的 next 指向第一个孩子 */
    off = romfs_be32(img + off) & ~15u;

    while (off != 0 && off + 16 < g_scripts_romfs_len) {
        uint32_t next = romfs_be32(img + off);
        uint32_t mode = next & RFNEXT_MODEMASK;
        uint32_t size = romfs_be32(img + off + 8);
        const uint8_t *nm = img + off + 16;
        size_t nmax = g_scripts_romfs_len - (size_t)(nm - img);
        size_t nlen = strnlen((const char *)nm, nmax);
        uint32_t data_off = off + 16 + ((uint32_t)nlen + 1 + 15) / 16 * 16;

        if (mode == RFNEXT_FILE && strcmp((const char *)nm, name) == 0 &&
            data_off + size <= g_scripts_romfs_len) {
            if (data)
                *data = img + data_off;
            if (len)
                *len = size;
            return OK;
        }

        off = next & ~15u;
    }

    return -ENOENT;
}

int retro_romfs_find(const char *name, const uint8_t **data, size_t *len)
{
    return romfs_find(name, data, len);
}

/*==========================
 *  内存盘块设备 /dev/rom0
 *==========================*/

static int romdisk_open(FAR struct inode *inode)
{
    return OK;
}

static int romdisk_geometry(FAR struct inode *inode,
                            FAR struct geometry *geometry)
{
    (void)inode;

    if (geometry == NULL)
        return -EINVAL;

    memset(geometry, 0, sizeof(*geometry));
    geometry->geo_available = true;
    geometry->geo_mediachanged = false;
    geometry->geo_writeenabled = false;       /* 只读盘 */
    geometry->geo_sectorsize = 512;
    geometry->geo_nsectors = (g_scripts_romfs_len + 511) / 512;
    return OK;
}

static ssize_t romdisk_read(FAR struct inode *inode, FAR unsigned char *buffer,
                            blkcnt_t start_sector, unsigned int nsectors)
{
    (void)inode;

    size_t off = (size_t)start_sector * 512;
    size_t nbytes = (size_t)nsectors * 512;

    if (off >= g_scripts_romfs_len)
        return 0;
    if (off + nbytes > g_scripts_romfs_len)
        nbytes = g_scripts_romfs_len - off;

    memcpy(buffer, g_scripts_romfs + off, nbytes);
    return (ssize_t)(nbytes / 512);
}

static const struct block_operations g_romdisk_bops =
{
    romdisk_open,       /* open */
    NULL,               /* close */
    romdisk_read,       /* read */
    NULL,               /* write */
    romdisk_geometry,   /* geometry */
    NULL,               /* ioctl */
    NULL                /* unlink */
};

static bool g_rom_mounted = false;

/*
 * WHAT : 注册 /dev/rom0 + 挂载 /rom/scripts
 * WHEN : retro_boot 启动期调用；幂等
 */
int retro_scripts_rom_mount(void)
{
    int ret;

    if (!romfs_valid()) {
        syslog(LOG_WARNING, "[scripts_rom] image absent\n");
        return -ENOENT;
    }

    if (g_rom_mounted)
        return OK;

    ret = register_blockdriver("/dev/rom0", &g_romdisk_bops, 0444, NULL);
    if (ret < 0)
        return ret;

    ret = mount("/dev/rom0", "/rom/scripts", "romfs", MS_RDONLY, NULL);
    if (ret < 0) {
        syslog(LOG_WARNING, "[scripts_rom] mount failed: %d\n", ret);
        unregister_blockdriver("/dev/rom0");
        return ret;
    }

    g_rom_mounted = true;
    syslog(LOG_INFO, "[scripts_rom] /rom/scripts mounted (%u B image)\n",
           (unsigned)g_scripts_romfs_len);
    return OK;
}

/*==========================
 *  script 命令（NSH builtin）
 *==========================*/

/* 引擎内存执行接口（script_engines.c） */
extern int script_exec_buffer(const char *name, const char *buf,
                              size_t len);

static const char *script_ext(const char *name)
{
    const char *dot = strrchr(name, '.');

    return dot != NULL ? dot + 1 : "";
}

int retro_script_main(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: script <名字|路径> | script -l\n");
        printf("  script hello      - 执行 /rom/scripts/hello.{be|bas|js}\n");
        printf("  script -l         - 列出 ROM 脚本\n");
        return OK;
    }

    if (strcmp(argv[1], "-l") == 0) {
        if (!romfs_valid())
            return -ENOENT;

        const uint8_t *img = g_scripts_romfs;
        int vh = 16;

        while (img[vh] != 0 && vh < 16 + 256)
            vh++;
        uint32_t off = ((vh + 1) + 15) & ~15;

        off = romfs_be32(img + off) & ~15u;
        while (off != 0 && off + 16 < g_scripts_romfs_len) {
            uint32_t next = romfs_be32(img + off);
            uint32_t size = romfs_be32(img + off + 8);
            const char *nm = (const char *)(img + off + 16);

            if ((next & RFNEXT_MODEMASK) == RFNEXT_FILE)
                printf("  %-20s %5u B  /rom/scripts/%s\n", nm,
                       (unsigned)size, nm);
            off = next & ~15u;
        }
        return OK;
    }

    /* 绝对路径：交给引擎的文件执行路径 */
    if (argv[1][0] == '/') {
        extern int script_exec_file(const char *path);

        return script_exec_file(argv[1]);
    }

    /* 名字：按扩展名优先级在镜像内直查（XIP：Flash 指针直跑） */
    static const char *exts[] = { "be", "bas", "js", NULL };

    for (int i = 0; exts[i] != NULL; i++) {
        char full[64];
        const uint8_t *data = NULL;
        size_t len = 0;

        snprintf(full, sizeof(full), "%s.%s", argv[1], exts[i]);
        if (romfs_find(full, &data, &len) == OK) {
            printf("[script] %s（ROM 直跑 %u B）\n", full, (unsigned)len);
            return script_exec_buffer(full, (const char *)data, len);
        }
    }

    printf("script: 未找到 %s（script -l 查看列表）\n", argv[1]);
    return -ENOENT;
}

#endif /* CONFIG_RETRO_SCRIPTS_ROM */
