/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * pkg_rom.c - ROM 包存储实现 / ROM package store
 *
 * WHAT : ROMFS 包存储（pkg_romfs.c 镜像）的树内直查、根层枚举、
 *        .rpk control 直读、/dev/rom1 挂载与 rommod XIP 寻址接线
 * WHY  : 应用/系统分离（2026-10-06，同日策略修订为构建期离线安装）：
 *        板级默认名单在编译期经 gen_pkgdb.py 直接安装到位（db/ 预装
 *        数据库随镜像分发，首启零安装动作）；.rmo 载荷 Flash 原址执行
 * WHO  : retro_boot / pkg 命令 / desktop.c / rommod / 宿主测试
 * WHERE: retro-ws/src/nuttx/common/pkg_rom.c
 * WHEN : 2026-10-06 新增
 * HOW  : ROMFS 树解析按 fs_romfsutil.c 语义（目录 rf_info=首孩子、
 *        rf_next=父层下一兄弟、根 rf_next=首孩子）；tar 容器 512B
 *        流式头解析复用 pkg_manager 同款套路（control 直读用，
 *        第三方 .rpk 免安装查询）。
 */

#include <nuttx/config.h>

#ifdef CONFIG_RETRO_PKG_STORE

#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

#include "pkg_rom.h"
#include "apps/system/pkg_manager.h"
#include "rommod.h"                 /* XIP 提供方注册（xip_lookup_default） */

/*==========================
 *  ROMFS 只读树解析（目录语义与 script_rom.c 平铺版不同：支持子目录）
 *==========================*/

#define RFNEXT_MODEMASK    7
#define RFNEXT_DIRECTORY   1
#define RFNEXT_FILE        2

static const uint8_t *g_img;
static size_t g_img_len;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static bool image_valid(void)
{
    return g_img != NULL && g_img_len >= 64 &&
           memcmp(g_img, "-rom1fs-", 8) == 0;
}

/* 卷名区结束 -> 根条目头偏移 */
static uint32_t root_offset(void)
{
    int vh = 16;

    while (g_img[vh] != 0 && vh < 16 + 256)
        vh++;
    return (uint32_t)(((vh + 1) + 15) & ~15);
}

/*
 * WHAT : 读一个条目头（偏移越界防护）
 * 返回 : false = 镜像越界/损坏
 */
static bool read_hdr(uint32_t off, uint32_t *next, uint32_t *info,
                     uint32_t *size, const char **name)
{
    if (off + 16 > g_img_len)
        return false;

    *next = be32(g_img + off);
    *info = be32(g_img + off + 4);
    *size = be32(g_img + off + 8);
    *name = (const char *)(g_img + off + 16);
    return true;
}

/* 条目名字长度（上限防越界；NUL 必在镜像内） */
static uint32_t name_len(uint32_t off)
{
    const uint8_t *nm = g_img + off + 16;
    size_t max = g_img_len - (size_t)(nm - g_img);
    size_t n = 0;

    while (n < max && nm[n])
        n++;
    return (uint32_t)n;
}

/* 条目数据区偏移（头 16B + 名字 16 对齐）；bin/ 目录下的载荷再
 * 4096 对齐（mkromfs 树模式特性：静态绑定档烘焙地址稳定性前提，
 * HARDWARE 14.1） */
static uint32_t data_offset_aligned(uint32_t off, bool bin_child)
{
    uint32_t nlen = name_len(off);
    uint32_t d = off + 16 + ((nlen + 1 + 15) / 16) * 16;

    return bin_child ? (d + 4095) & ~4095u : d;
}

static uint32_t data_offset(uint32_t off)
{
    return data_offset_aligned(off, false);
}

/* 条目头偏移处的目录名是否 "bin"（父目录判定用） */
static bool entry_named_bin(uint32_t off)
{
    if (off + 20 > g_img_len)
        return false;
    return strncmp((const char *)(g_img + off + 16), "bin", 4) == 0;
}

/*
 * WHAT : 在目录条目（头偏移 dir_off，"." 或子目录名）的孩子链上找名字
 * HOW  : 孩子链起点 = 目录 rf_info（根条目则为其 rf_next）；沿孩子
 *        rf_next 逐个比对名字；进入名字区前校验偏移合法
 * 返回 : 找到的条目头偏移；0 = 未找到
 */
static uint32_t find_child(uint32_t dir_off, const char *want,
                           bool root_level)
{
    uint32_t next;
    uint32_t info;
    uint32_t size;
    const char *nm;

    if (!read_hdr(dir_off, &next, &info, &size, &nm))
        return 0;

    uint32_t off = root_level ? (next & ~15u) : info;
    uint32_t guard = 0;

    while (off != 0 && off + 16 < g_img_len && guard++ < 4096) {
        if (!read_hdr(off, &next, &info, &size, &nm))
            return 0;

        uint32_t nlen = name_len(off);
        if (nlen == strlen(want) &&
            memcmp(g_img + off + 16, want, nlen) == 0)
            return off;

        off = next & ~15u;
    }
    return 0;
}

int retro_pkg_rom_init(const uint8_t *image, size_t len)
{
    if (image == NULL || len < 64 || memcmp(image, "-rom1fs-", 8) != 0)
        return -EINVAL;

    g_img = image;
    g_img_len = len;
    return OK;
}

int retro_pkg_rom_find(const char *relpath, const uint8_t **data,
                       size_t *len)
{
    if (!image_valid() || relpath == NULL || relpath[0] == '/' ||
        relpath[0] == '\0')
        return -ENOENT;

    uint32_t cur = root_offset();
    bool root_level = true;
    const char *p = relpath;

    while (*p) {
        const char *slash = strchr(p, '/');
        size_t clen = slash ? (size_t)(slash - p) : strlen(p);

        if (clen == 0 || clen > 255)
            return -ENOENT;

        char comp[256];
        memcpy(comp, p, clen);
        comp[clen] = '\0';

        uint32_t hit = find_child(cur, comp, root_level);
        if (hit == 0)
            return -ENOENT;

        if (slash == NULL) {
            /* 末分量：必须是文件；父目录为 bin/ 时数据 4096 对齐 */
            uint32_t next;
            uint32_t info;
            uint32_t size;
            const char *nm;

            if (!read_hdr(hit, &next, &info, &size, &nm) ||
                (next & RFNEXT_MODEMASK) != RFNEXT_FILE)
                return -ENOENT;

            uint32_t doff = data_offset_aligned(hit,
                                                entry_named_bin(cur) &&
                                                !root_level);
            if ((size_t)doff + size > g_img_len)
                return -ENOENT;

            if (data)
                *data = g_img + doff;
            if (len)
                *len = size;
            return OK;
        }

        /* 中间分量：必须是目录 */
        uint32_t next;
        uint32_t info;
        uint32_t size;
        const char *nm;

        if (!read_hdr(hit, &next, &info, &size, &nm) ||
            (next & RFNEXT_MODEMASK) != RFNEXT_DIRECTORY)
            return -ENOENT;

        cur = hit;
        root_level = false;
        p = slash + 1;
    }
    return -ENOENT;
}

int retro_pkg_rom_scan(int (*cb)(const char *name, const uint8_t *data,
                                 size_t len, void *arg), void *arg)
{
    if (!image_valid() || cb == NULL)
        return -EINVAL;

    uint32_t next;
    uint32_t info;
    uint32_t size;
    const char *nm;
    uint32_t off = root_offset();
    int count = 0;

    if (!read_hdr(off, &next, &info, &size, &nm))
        return -ENOENT;

    off = next & ~15u;              /* 根的 next = 首孩子 */
    while (off != 0 && off + 16 < g_img_len) {
        if (!read_hdr(off, &next, &info, &size, &nm))
            break;

        uint32_t doff = data_offset(off);

        if ((next & RFNEXT_MODEMASK) == RFNEXT_FILE &&
            doff + size <= g_img_len) {
            int ret = cb((const char *)(g_img + off + 16), g_img + doff,
                         size, arg);
            if (ret < 0)
                return ret;
            count++;
        }
        off = next & ~15u;
    }
    return count;
}

/*==========================
 *  .rpk 容器内 control 直读（512B 流式，无整包载入）
 *==========================*/

/*
 * WHAT : 在 tar 镜像内顺序找 control 并提取单个字段值
 * HOW  : 条目头 512B（ustar 名在偏移 0、大小八进制在偏移 124）；
 *        control 为小文件（<4KB，与 pkg_manager 同上限）——整体读入
 *        堆缓冲后逐行匹配 "Field: value"（堆分配规避 NSH 栈上限）
 * 返回 : OK / -ENOENT（无 control 或无该字段）/ -EIO（损坏）
 */
int retro_pkg_rom_control(const char *rpk_name, const char *field,
                          char *buf, int buflen)
{
    const uint8_t *data = NULL;
    size_t len = 0;
    char path[128];

    if (snprintf(path, sizeof(path), "%s", rpk_name) >= (int)sizeof(path))
        return -ENAMETOOLONG;
    if (retro_pkg_rom_find(path, &data, &len) != OK)
        return -ENOENT;

    size_t off = 0;
    uint8_t hdr[512];

    while (off + 512 <= len) {
        memcpy(hdr, data + off, 512);

        /* 全零块 = 归档结束 */
        bool zero = true;
        for (int i = 0; i < 512; i++) {
            if (hdr[i]) {
                zero = false;
                break;
            }
        }
        if (zero)
            break;

        /* ustar 尺寸字段：12 字节八进制 */
        size_t fsize = 0;
        for (int i = 0; i < 11 && hdr[124 + i] >= '0' && hdr[124 + i] <= '7';
             i++)
            fsize = fsize * 8 + (size_t)(hdr[124 + i] - '0');

        if (strcmp((const char *)hdr, "control") == 0) {
            if (fsize >= 4096 || off + 512 + fsize > len)
                return -EIO;

            char *cbuf = malloc(fsize + 1);
            if (cbuf == NULL)
                return -ENOMEM;

            memcpy(cbuf, data + off + 512, fsize);
            cbuf[fsize] = '\0';

            int ret = -ENOENT;
            size_t flen = strlen(field);
            char *p = cbuf;

            while (p && *p) {
                char *eol = strchr(p, '\n');
                if (eol)
                    *eol = '\0';
                if (strncmp(p, field, flen) == 0 && p[flen] == ':') {
                    char *val = p + flen + 1;
                    while (*val == ' ' || *val == '\t')
                        val++;
                    snprintf(buf, buflen, "%s", val);
                    ret = OK;
                    break;
                }
                p = eol ? eol + 1 : NULL;
            }

            free(cbuf);
            return ret;
        }

        off += 512 + ((fsize + 511) & ~(size_t)511);
    }
    return -ENOENT;
}

/*==========================
 *  rommod XIP 载荷寻址（bin/<模块名> -> Flash 指针）
 *==========================*/

/*
 * WHAT : rommod 的默认 XIP 提供方：模块名 -> /rom/pkg/bin/<名> 数据指针
 * WHY  : rommod_load(name) 需要零拷贝取到模块文件在 ROMFS 镜像内的
 *        Flash 地址；宿主测试可用 rommod_set_xip_provider 覆盖
 */
static int xip_lookup_default(const char *name, const uint8_t **data,
                              size_t *len)
{
    char path[64];

    if (snprintf(path, sizeof(path), "bin/%s", name) >= (int)sizeof(path))
        return -ENAMETOOLONG;
    return retro_pkg_rom_find(path, data, len);
}

int retro_pkg_rom_xip_register(void)
{
    extern void rommod_set_xip_provider(rommod_xip_lookup_t lookup);

    rommod_set_xip_provider(xip_lookup_default);
    return OK;
}

/*==========================
 *  /dev/rom1 块设备 + /rom/pkg 挂载（NuttX 侧）
 *==========================*/

#if !defined(CONFIG_RETRO_PKG_ROM_HOST_TEST)

#include <nuttx/fs/fs.h>

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
    geometry->geo_writeenabled = false;
    geometry->geo_sectorsize = 512;
    geometry->geo_nsectors = (blkcnt_t)((g_img_len + 511) / 512);
    return OK;
}

static ssize_t romdisk_read(FAR struct inode *inode,
                            FAR unsigned char *buffer,
                            blkcnt_t start_sector, unsigned int nsectors)
{
    (void)inode;

    size_t off = (size_t)start_sector * 512;
    size_t nbytes = (size_t)nsectors * 512;

    if (!image_valid())
        return 0;
    if (off >= g_img_len)
        return 0;
    if (off + nbytes > g_img_len)
        nbytes = g_img_len - off;

    memcpy(buffer, g_img + off, nbytes);
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

static bool g_mounted;

int retro_pkg_rom_mount(void)
{
    int ret;

    if (!image_valid() || g_mounted)
        return image_valid() ? OK : -ENOENT;

    ret = register_blockdriver("/dev/rom1", &g_romdisk_bops, 0444, NULL);
    if (ret < 0)
        return ret;

    ret = mount("/dev/rom1", "/rom/pkg", "romfs", MS_RDONLY, NULL);
    if (ret < 0) {
        syslog(LOG_WARNING, "[pkg_rom] mount /rom/pkg failed: %d\n", ret);
        unregister_blockdriver("/dev/rom1");
        return ret;
    }

    g_mounted = true;
    syslog(LOG_INFO, "[pkg_rom] /rom/pkg mounted (%u B store)\n",
           (unsigned)g_img_len);
    return OK;
}

#else  /* CONFIG_RETRO_PKG_ROM_HOST_TEST */

int retro_pkg_rom_mount(void)
{
    return -ENOSYS;                 /* 宿主测试无块设备层 */
}

#endif /* !CONFIG_RETRO_PKG_ROM_HOST_TEST */

#endif /* CONFIG_RETRO_PKG_STORE */
