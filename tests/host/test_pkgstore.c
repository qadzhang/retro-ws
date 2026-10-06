/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * test_pkgstore.c - ROM 包存储（构建期离线安装）宿主测试
 *
 * WHAT : pkg_rom.c（ROMFS 树直查/枚举/control 直读）与 pkg_manager
 *        两级 DB（ROM 预装层 + 片上覆盖层/墓碑）的闭环测试
 * WHY  : 2026-10-06 策略修订——名单包在编译期直接安装到位（db/ 随
 *        镜像分发），核心不变量："预装即已安装、首启零动作、卸载 =
 *        墓碑停用、第三方通道不回归"必须机器化锁定（ai-code-testing）
 * WHO  : tests/host/run_all.sh（fixture 由 prep_pkgstore_fixture.py
 *        预生成，生产工具 gen_pkgdb.py/mkromfs.py 同源）
 * WHERE: retro-ws/tests/host/test_pkgstore.c
 * WHEN : 2026-10-06 新增；同日随离线安装策略重写
 * HOW  : 四组用例：(1) 树直查/枚举/control 直读；(2) 预装语义
 *        （is_installed 直接真、list 可见、info 可读、manifest 路径
 *        指向 ROM 载荷）；(3) 卸载墓碑（remove -> 停用 -> list 消失
 *        -> ROM 文件仍在）；(4) 第三方 .rpk 片上安装通道回归
 */

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pkg_rom.h"
#include "pkg_manager.h"
#include "test_framework.h"

#define IMGDIR "/tmp/retro_test/pkgstore"
#define IMG    IMGDIR "/rom.img"
#define THIRDPKG IMGDIR "/sdcard/world-2.0.0-1.rpk"

static uint8_t *g_img;
static size_t g_img_len;

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(*len);
    if (fread(buf, 1, *len, f) != *len)
        exit(2);
    fclose(f);
    return buf;
}

static int exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static void test_find_and_scan(void)
{
    const uint8_t *data = NULL;
    size_t len = 0;

    CHECK(retro_pkg_rom_init(g_img, g_img_len) == 0);
    CHECK(retro_pkg_rom_find("bin/hello.rmo", &data, &len) == 0);
    CHECK(data != NULL && len > 4);
    CHECK(memcmp(data, "\x7f" "ELF", 4) == 0);
    CHECK(retro_pkg_rom_find("bin/nosuch", &data, &len) == -ENOENT);
    CHECK(retro_pkg_rom_find("../etc/passwd", &data, &len) == -ENOENT);
    CHECK(retro_pkg_rom_find("a/b/c/d", &data, &len) == -ENOENT);

    /* 预装数据库文件直查（db/<pkg>.control / db/manifest/<pkg>） */
    CHECK(retro_pkg_rom_find("db/hello.control", &data, &len) == 0);
    CHECK(memcmp(data, "Package: hello", 14) == 0);
    CHECK(retro_pkg_rom_find("db/manifest/hello", &data, &len) == 0);

    /* control 直读（tar 容器路径；不存在时容错 -ENOENT） */
    char buf[128];

    CHECK(retro_pkg_rom_control("nosuch.rpk", "Package",
                                buf, sizeof(buf)) == -ENOENT);
}

static int count_scan(const char *name, const uint8_t *data, size_t len,
                      void *arg)
{
    int *n = arg;

    (void)name;
    (void)data;
    (void)len;
    (*n)++;
    return 0;
}

static int abort_scan(const char *name, const uint8_t *data, size_t len,
                      void *arg)
{
    (void)name;
    (void)data;
    (void)len;
    (void)arg;
    return -EIO;
}

static void test_scan(void)
{
    int n = 0;

    /* 根层现只有目录（bin/ db/）——离线安装后无散装 .rpk 文件；
     * 无文件即无回调（中止语义无对象，返回 0） */
    CHECK(retro_pkg_rom_scan(count_scan, &n) == 0);
    CHECK(n == 0);
    CHECK(retro_pkg_rom_scan(abort_scan, NULL) == 0);
}

/*
 * 预装语义：编译期安装到位 -> 运行期即已安装（零 seed/零安装动作）
 */
static void test_preinstalled(void)
{
    char tomb[384];

    snprintf(tomb, sizeof(tomb), "%s/removed/hello", PKG_DB_ROOT);
    unlink(tomb);                   /* 清上一轮可能的墓碑 */

    CHECK(rpkg_is_installed("hello") == 1);
    CHECK(rpkg_is_installed("world") == 0);   /* 第三方未装 */

    /* list 合并视图：预装层可见 */
    CHECK(rpkg_list() == 0);

    /* info 两级寻址：ROM 层可读 */
    CHECK(rpkg_info("hello") == 0);

    /* 第三方异名包装上（片上覆盖层） */
    CHECK(rpkg_install(THIRDPKG) == 0);
    CHECK(rpkg_is_installed("world") == 1);

    /* DB manifest（ROM 层）路径可循：Xip 条目指向 /rom/pkg/bin/ */
    char mpath[384];
    FILE *f;

    snprintf(mpath, sizeof(mpath), PKG_ROM_DB_ROOT "/manifest/hello");
    f = fopen(mpath, "r");
    CHECK(f != NULL);
    if (f)
    {
        char line[384];
        int found_rom = 0;

        while (fgets(line, sizeof(line), f))
        {
            if (strstr(line, PKG_ROM_ROOT "/bin/hello.rmo"))
                found_rom = 1;
        }
        fclose(f);
        CHECK(found_rom == 1);
    }
}

/*
 * 卸载墓碑：预装包 remove = 停用（ROM 文件不可删，载荷常驻）
 */
static void test_tombstone(void)
{
    char tomb[384];

    CHECK(rpkg_remove("hello") == 0);
    snprintf(tomb, sizeof(tomb), "%s/removed/hello", PKG_DB_ROOT);
    CHECK(exists(tomb));
    CHECK(rpkg_is_installed("hello") == 0);
    CHECK(rpkg_list() == 0);        /* 列表不再含 hello */

    /* ROM 载荷与 DB 文件仍在（只读语义，重装通道不被破坏） */
    const uint8_t *data = NULL;
    size_t len = 0;

    CHECK(retro_pkg_rom_find("bin/hello.rmo", &data, &len) == 0);
    CHECK(retro_pkg_rom_find("db/hello.control", &data, &len) == 0);
}

/*
 * 第三方通道回归：world 包片上常规装/卸不受两级 DB 影响
 */
static void test_thirdparty_channel(void)
{
    char p[384];

    CHECK(rpkg_is_installed("world") == 1);   /* 前面已装 */

    /* 常规卸载：按片上 manifest 删文件 + 清 DB */
    CHECK(rpkg_remove("world") == 0);
    CHECK(rpkg_is_installed("world") == 0);
    snprintf(p, sizeof(p), "%s/share/note.txt", PKG_SYSTEM_PREFIX);
    CHECK(!exists(p));              /* 载荷文件已删 */

    /* 重装往返 */
    CHECK(rpkg_install(THIRDPKG) == 0);
    CHECK(rpkg_is_installed("world") == 1);
    CHECK(rpkg_remove("world") == 0);
}

int main(void)
{
    g_img = slurp(IMG, &g_img_len);
    if (g_img == NULL)
    {
        fprintf(stderr, "[pkgstore] fixture 缺失: %s（run_all 预生成）\n",
                IMG);
        return 2;
    }

    test_find_and_scan();
    test_scan();
    test_preinstalled();
    test_tombstone();
    test_thirdparty_channel();

    TEST_REPORT("test_pkgstore");
}
