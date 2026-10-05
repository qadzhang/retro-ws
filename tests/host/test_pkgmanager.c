/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : pkg_manager 单元/契约测试（宿主机）
 * WHY  : .rpk 解析器与安装器是安全敏感路径（路径穿越/CRC/架构门禁），
 *        按 ai-code-testing 规范用"关系断言+工业级 oracle"绕开自证
 * WHO  : tests/host/run_all.sh 调度；变异测试复用本套（-DPKG_MGR_SRC）
 * WHERE: esp32-retro-ws/tests/host/test_pkgmanager.c
 * WHEN : 2026-10-04 新增
 * HOW  : 直接 #include 被测 .c 以触达 static 函数；
 *        oracle 来源：CRC32 标准校验值（RFC 1952 生态共识）、
 *        GNU tar 生成的真实 ustar 流、glibc 文件系统语义。
 */

#include "test_framework.h"

#ifndef PKG_MGR_SRC
#define PKG_MGR_SRC "pkg_manager.c"
#endif
#include PKG_MGR_SRC

#include <sys/wait.h>

/* 前置声明：arc_open 依赖（定义在文件后段） */
static void write_file(const char *path, const void *data, size_t len);

/*==========================
 *  测试沙箱布局（run_all.sh 保证目录干净）
 *==========================*/

#define SANDBOX      "/tmp/retro_test"
#define SD_ROOT      SANDBOX "/sd"
#define DB_ROOT      SANDBOX "/db"
#define STAGING      SANDBOX "/stage"
#define FIXTURE_RPK  SANDBOX "/fixture.rpk"

/*==========================
 *  CRC32：标准校验向量（非 AI 生成的独立 oracle）
 *==========================*/

static void test_crc32_known_vectors(void)
{
    /* "123456789" 的 CRC-32 标准校验值（IEEE 802.3，全生态共识） */
    const uint8_t v1[] = "123456789";
    CHECK_EQ_INT(crc32_update(0, v1, 9), 0xCBF43926u);

    /* 空输入 = 0（初值两次取反抵消） */
    CHECK_EQ_INT(crc32_update(0, v1, 0), 0);

    /* 分块累积 == 整体一次：蜕变关系（分块不变性） */
    uint32_t whole = crc32_update(0, v1, 9);
    uint32_t split = crc32_update(0, v1, 3);
    split = crc32_update(split, v1 + 3, 6);
    CHECK_EQ_INT(split, whole);

    /* 前缀关系：加一个字节必然改变结果（无碰撞弱断言） */
    CHECK(crc32_update(0, v1, 8) != whole);
}

/*==========================
 *  tar_parse_size
 *==========================*/

static void test_tar_parse_size(void)
{
    CHECK_EQ_INT(tar_parse_size("00000000000"), 0);
    CHECK_EQ_INT(tar_parse_size("00000001000"), 512);   /* 8 进制 1000 = 512 */
    CHECK_EQ_INT(tar_parse_size("00000001474"), 828);   /* 01474o = 828 */
    CHECK_EQ_INT(tar_parse_size("0000000000\x00 "), 0); /* NUL 提前终止 */
    CHECK_EQ_INT(tar_parse_size("0000000000 "), 0);     /* 空格终止 */
    CHECK_EQ_INT(tar_parse_size("99999999999"), 0);     /* 非 8 进制 -> 停止 */
}

/*==========================
 *  path_is_safe：表驱动
 *==========================*/

static void test_path_is_safe(void)
{
    static const struct {
        const char *path;
        bool safe;
    } cases[] = {
        { "a.txt",            true  },
        { "bin/hello",        true  },
        { "a/b/c/d.txt",      true  },
        { "a..b",             true  },  /* "a..b" 不是 ".." 分量 */
        { "x..y/z",           true  },
        { "..",               false },
        { "../evil",          false },
        { "a/../b",           false },
        { "a/..",             false },
        { "a/../../b",        false },
        { "/abs",             false },
        { "/etc/passwd",      false },
        { "",                 false },
        { NULL,               false },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        CHECK_EQ_INT(path_is_safe(cases[i].path), cases[i].safe);
}

/*==========================
 *  arch_match（CONFIG_RETRO_ARCH 由编译命令行注入）
 *==========================*/

static void test_arch_match(void)
{
    CHECK(arch_match(NULL));
    CHECK(arch_match(""));
    CHECK(arch_match("all"));

    CHECK(arch_match(CONFIG_RETRO_ARCH_VAL));     /* 全名 */
    CHECK(arch_match(CONFIG_RETRO_FAMILY_VAL));   /* 架构族前缀 */
    CHECK(arch_match(CONFIG_RETRO_CHIP_VAL));     /* 芯片名后缀 */

    CHECK(!arch_match("mips"));
    CHECK(!arch_match("xtensa-esp32"));           /* 族内不同芯片不通用 */
    CHECK(!arch_match("riscv-esp32c3"));          /* 跨 ISA 不通用 */
    CHECK(!arch_match("all-x"));                  /* all 前缀伪装 */
}

/*==========================
 *  pkg_name_is_valid
 *==========================*/

static void test_pkg_name_valid(void)
{
    CHECK(pkg_name_is_valid("hello"));
    CHECK(pkg_name_is_valid("ucblogo"));
    CHECK(pkg_name_is_valid("my-pkg+2.1"));

    CHECK(!pkg_name_is_valid(""));
    CHECK(!pkg_name_is_valid("../evil"));
    CHECK(!pkg_name_is_valid("a/b"));
    CHECK(!pkg_name_is_valid(".hidden"));
    CHECK(!pkg_name_is_valid("-lead"));
    CHECK(!pkg_name_is_valid("trail."));
    CHECK(!pkg_name_is_valid("name with space"));
}

/*==========================
 *  control 解析
 *==========================*/

static void test_control_parse(void)
{
    struct rpkg_control_s ctl;

    memset(&ctl, 0, sizeof(ctl));
    CHECK_EQ_INT(control_parse_line(&ctl, "Package: hello"), OK);
    CHECK_STR_EQ(ctl.field[PKG_FLD_PACKAGE], "hello");

    CHECK_EQ_INT(control_parse_line(&ctl, "Version:1.2.3"), OK);
    CHECK_STR_EQ(ctl.field[PKG_FLD_VERSION], "1.2.3");

    CHECK_EQ_INT(control_parse_line(&ctl, "Description:   中文 描述"), OK);
    CHECK_STR_EQ(ctl.field[PKG_FLD_DESCRIPTION], "中文 描述");

    /* '\r' 剥除 */
    CHECK_EQ_INT(control_parse_line(&ctl, "Arch: xtensa\r"), OK);
    CHECK_STR_EQ(ctl.field[PKG_FLD_ARCH], "xtensa");

    /* 前缀混淆字段不得误命中 */
    memset(&ctl, 0, sizeof(ctl));
    CHECK_EQ_INT(control_parse_line(&ctl, "PackageX: no"), -ENOENT);
    CHECK(ctl.field[PKG_FLD_PACKAGE][0] == '\0');
    CHECK(control_get(&ctl, PKG_FLD_PACKAGE) == NULL);

    CHECK(control_get(&ctl, PKG_FLD_PACKAGE) == NULL);
    CHECK_EQ_INT(control_parse_line(&ctl, "Package: x"), OK);
    CHECK_STR_EQ(control_get(&ctl, PKG_FLD_PACKAGE), "x");
}

/*==========================
 *  manifest 解析
 *==========================*/

static void test_manifest_parse(void)
{
    struct manifest_ent_s ents[8];

    const char *buf =
        "deadbeef " PKG_INSTALL_PREFIX "/bin/tool\n"
        "00000000 " PKG_INSTALL_PREFIX "/etc/conf\n"
        "12345678 /etc/passwd\n"          /* 前缀之外 -> 丢弃 */
        "abcdef01 " PKG_INSTALL_PREFIX "/../evil\n"  /* 穿越 -> 丢弃 */
        "ffffffff " PKG_INSTALL_PREFIX "/lib/a.so\n"; /* 无换行尾行 */

    int n = manifest_parse(ents, 8, buf);

    CHECK_EQ_INT(n, 3);
    if (n >= 3) {
        CHECK_EQ_INT(ents[0].crc, 0xdeadbeefu);
        CHECK_STR_EQ(ents[0].rel, "bin/tool");
        CHECK_EQ_INT(ents[2].crc, 0xffffffffu);
        CHECK_STR_EQ(ents[2].rel, "lib/a.so");
    }

    CHECK(manifest_find(ents, n, "bin/tool") == &ents[0]);
    CHECK(manifest_find(ents, n, "no/such") == NULL);

    /* 空缓冲 -> 0 条目（无清单不算错） */
    CHECK_EQ_INT(manifest_parse(ents, 8, ""), 0);
}

/*==========================
 *  手工 ustar 构造器（含合法 checksum）
 *==========================*/

static void ustar_hdr(uint8_t blk[512], const char *name, size_t size,
                      char typeflag)
{
    memset(blk, 0, 512);
    struct tar_hdr_s *h = (struct tar_hdr_s *)blk;

    snprintf(h->name, sizeof(h->name), "%s", name);
    snprintf(h->mode, sizeof(h->mode), "%07o", 0777);
    snprintf(h->size, sizeof(h->size), "%011o", (unsigned)size);
    snprintf(h->mtime, sizeof(h->mtime), "%011o", 0);
    h->typeflag = typeflag;
    memcpy(h->magic, "ustar", 6);
    h->version[0] = '0'; h->version[1] = '0';

    uint32_t sum = 0;
    for (int i = 0; i < 512; i++)
        sum += (i >= 148 && i < 156) ? ' ' : blk[i];
    snprintf(h->chksum, sizeof(h->chksum), "%06o", sum);
    h->chksum[6] = '\0';
    h->chksum[7] = ' ';
}

/* 构造：单文件条目（数据自动补齐到 512 边界） */
static size_t ustar_append_entry(uint8_t *buf, size_t off,
                                 const char *name, const void *data,
                                 size_t dlen, char typeflag)
{
    ustar_hdr(buf + off, name, dlen, typeflag);
    off += 512;
    if (dlen) {
        memcpy(buf + off, data, dlen);
        off += (dlen + 511) & ~(size_t)511;
    }
    return off;
}

/*==========================
 *  tar 迭代器（手工构造流 + 边界情况）
 *==========================*/

/*
 * WHAT : 把内存中的 archive 写临时文件再 open（返回真实 fd）
 * WHY  : fmemopen 的流没有可用的 fileno()，而设备端 tar 解析器
 *        面向 fd 编程——用真实文件保持同一条代码路径
 */
static int arc_open(const uint8_t *buf, size_t len)
{
    char path[128];
    static int seq = 0;

    snprintf(path, sizeof(path), "%s/arc%d.tar", SANDBOX, seq++);
    write_file(path, buf, len);
    return open(path, O_RDONLY);
}

static void test_tar_iteration(void)
{
    static uint8_t arc[8192];
    size_t off = 0;

    off = ustar_append_entry(arc, off, "control",
                             "Package: t\n", 12, '0');
    off = ustar_append_entry(arc, off, "data/empty", "", 0, '0');
    off = ustar_append_entry(arc, off, "data/dir", NULL, 0, '5');
    /* 结束双零块 */
    memset(arc + off, 0, 1024);
    off += 1024;

    int fd = arc_open(arc, off);
    CHECK(fd >= 0);

    struct tar_iter_s it = { .fd = fd, .remain = 0, .eof = false };
    char name[RPKG_MAX_PATH];
    char type;
    size_t size;

    CHECK_EQ_INT(tar_next(&it, name, sizeof(name), &size, &type), 0);
    CHECK_STR_EQ(name, "control");
    CHECK_EQ_INT(size, 12);
    CHECK_EQ_INT(type, '0');

    char data[32];
    CHECK_EQ_INT(tar_read_data(&it, data, sizeof(data), size), OK);
    CHECK_STR_EQ(data, "Package: t\n");

    CHECK_EQ_INT(tar_next(&it, name, sizeof(name), &size, &type), 0);
    CHECK_STR_EQ(name, "data/empty");
    CHECK_EQ_INT(size, 0);
    CHECK_EQ_INT(tar_read_data(&it, data, sizeof(data), 0), OK);

    CHECK_EQ_INT(tar_next(&it, name, sizeof(name), &size, &type), 0);
    CHECK_STR_EQ(name, "data/dir");
    CHECK_EQ_INT(type, '5');

    CHECK_EQ_INT(tar_next(&it, name, sizeof(name), &size, &type), 1);
    CHECK_EQ_INT(tar_next(&it, name, sizeof(name), &size, &type), 1);

    close(fd);

    /* 截断的流 -> -EIO 而非崩溃（截在结尾零块中间） */
    fd = arc_open(arc, off - 700);
    struct tar_iter_s it2 = { .fd = fd, .remain = 0, .eof = false };
    CHECK_EQ_INT(tar_next(&it2, name, sizeof(name), &size, &type), 0);
    CHECK_EQ_INT(tar_next(&it2, name, sizeof(name), &size, &type), 0);
    CHECK_EQ_INT(tar_next(&it2, name, sizeof(name), &size, &type), 0);
    CHECK_EQ_INT(tar_next(&it2, name, sizeof(name), &size, &type), -EIO);
    close(fd);

    /* 损坏的 checksum -> -EIO */
    arc[3] ^= 0x40;
    fd = arc_open(arc, off);
    struct tar_iter_s it3 = { .fd = fd, .remain = 0, .eof = false };
    CHECK_EQ_INT(tar_next(&it3, name, sizeof(name), &size, &type), -EIO);
    close(fd);
    arc[3] ^= 0x40;
}

/*==========================
 *  GNU tar 工业级 oracle：真实 ustar 流的差分遍历
 *==========================*/

static void test_tar_vs_gnu_tar(void)
{
    /* 用 GNU tar 打一个真实包（与 make_package.sh 同参数） */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "rm -rf '%s/oracle' && mkdir -p '%s/oracle/data/bin' && "
             "printf 'hello-elf-bytes' > '%s/oracle/data/bin/tool' && "
             "printf 'Package: oracle\\nVersion: 9.9\\n' > '%s/oracle/control' && "
             "printf 'cfg' > '%s/oracle/data/etc.conf' && "
             "(cd '%s/oracle' && find . -mindepth 1 \\( -type f -o -type d \\) -print | "
             "sed 's|^\\./||' | sort | tar --format=ustar --no-recursion -cf '%s/gnu.rpk' -T -)",
             SANDBOX, SANDBOX, SANDBOX, SANDBOX, SANDBOX, SANDBOX, SANDBOX);
    CHECK_EQ_INT(system(cmd), 0);

    FILE *fp = fopen(SANDBOX "/gnu.rpk", "rb");
    CHECK(fp != NULL);
    if (!fp)
        return;

    struct tar_iter_s it = { .fd = fileno(fp), .remain = 0, .eof = false };
    char name[RPKG_MAX_PATH];
    char type;
    size_t size;
    int n = 0;
    int rc;

    /* 差分关系：条目集合 = GNU tar 输入文件集合（无丢失/无多余） */
    while ((rc = tar_next(&it, name, sizeof(name), &size, &type)) == 0) {
        if (strcmp(name, "./control") == 0) {   /* GNU tar -T 可能带 ./ 前缀 */
            CHECK_EQ_INT(size, 29);
            n++;
        } else if (strcmp(name, "control") == 0) {
            CHECK_EQ_INT(size, 29);
            n++;
        } else if (strcmp(name, "data/bin/tool") == 0) {
            CHECK_EQ_INT(size, 15);
            CHECK_EQ_INT(type, '0');
            char buf[16];
            CHECK_EQ_INT(tar_read_data(&it, buf, sizeof(buf), size), OK);
            CHECK_STR_EQ(buf, "hello-elf-bytes");
            n++;
        } else if (strcmp(name, "data/etc.conf") == 0) {
            CHECK_EQ_INT(size, 3);
            n++;
        } else if (strcmp(name, "data/bin/") == 0 ||
                   strcmp(name, "data/bin") == 0) {
            /* GNU tar 目录条目带尾斜杠 */
            CHECK_EQ_INT(type, '5');
            n++;
        } else if (strcmp(name, "data/") == 0 ||
                   strcmp(name, "data") == 0) {
            CHECK_EQ_INT(type, '5');
            n++;
        } else {
            CHECK(0);  /* 不应出现未知条目 */
        }
    }
    CHECK_EQ_INT(rc, 1);      /* 正常收尾 */
    CHECK_EQ_INT(n, 5);       /* 5 个条目全部对上 */
    fclose(fp);
}

/*==========================
 *  端到端安装/卸载（真实文件系统 + system() 脚本）
 *==========================*/

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
    }
}

static bool file_equals(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t buf[4096];
    size_t got = fread(buf, 1, len > sizeof(buf) ? sizeof(buf) : len, f);
    bool ok = (got == len && memcmp(buf, data, len) == 0);
    char extra;
    if (ok && fread(&extra, 1, 1, f) != 0)
        ok = false;
    fclose(f);
    return ok;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void build_fixture_package(void)
{
    static const char elf_body[] = "\x7f" "ELF-fake-payload-0123456789abcdef";
    static const char conf_body[] = "key=value\n";

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/data/bin' '%s'",
             STAGING, STAGING, STAGING);
    system(cmd);

    write_file(STAGING "/control",
               "Package: fixture\n"
               "Version: 1.0.0\n"
               "Arch: " CONFIG_RETRO_CHIP_VAL "\n"
               "Description: unit test fixture\n", 0 + strlen(
               "Package: fixture\n"
               "Version: 1.0.0\n"
               "Arch: " CONFIG_RETRO_CHIP_VAL "\n"
               "Description: unit test fixture\n"));
    write_file(STAGING "/postinst",
               "printf ran > " SANDBOX "/postinst.ran\n", 0 + strlen(
               "printf ran > " SANDBOX "/postinst.ran\n"));
    write_file(STAGING "/prerm",
               "printf rm > " SANDBOX "/prerm.ran\n", 0 + strlen(
               "printf rm > " SANDBOX "/prerm.ran\n"));
    write_file(STAGING "/data/bin/tool", elf_body, sizeof(elf_body) - 1);
    write_file(STAGING "/data/etc.conf", conf_body, sizeof(conf_body) - 1);

    /* manifest：与设备端同格式，CRC 用被测实现算（蜕变：自洽可回验） */
    uint32_t c1 = crc32_update(0, (const uint8_t *)elf_body,
                               sizeof(elf_body) - 1);
    uint32_t c2 = crc32_update(0, (const uint8_t *)conf_body,
                               sizeof(conf_body) - 1);
    char manifest[512];
    int len = snprintf(manifest, sizeof(manifest),
                       "%08lx " PKG_INSTALL_PREFIX "/bin/tool\n"
                       "%08lx " PKG_INSTALL_PREFIX "/etc.conf\n",
                       (unsigned long)c1, (unsigned long)c2);
    write_file(STAGING "/manifest", manifest, (size_t)len);

    snprintf(cmd, sizeof(cmd),
             "(cd '%s' && find . -mindepth 1 \\( -type f -o -type d \\) -print | "
             "sed 's|^\\./||' | sort | "
             "tar --format=ustar --no-recursion -cf '%s' -T -)",
             STAGING, FIXTURE_RPK);
    system(cmd);
}

static void test_install_remove_e2e(void)
{
    build_fixture_package();

    char marker[RPKG_MAX_PATH];
    snprintf(marker, sizeof(marker), "%s/postinst.ran", SANDBOX);
    unlink(marker);
    snprintf(marker, sizeof(marker), "%s/prerm.ran", SANDBOX);
    unlink(marker);

    CHECK_EQ_INT(rpkg_install(FIXTURE_RPK), OK);

    /* 落盘内容逐字节一致（差分断言） */
    static const char elf_body[] = "\x7f" "ELF-fake-payload-0123456789abcdef";
    static const char conf_body[] = "key=value\n";
    char path[RPKG_MAX_PATH];
    snprintf(path, sizeof(path), "%s/bin/tool", PKG_INSTALL_PREFIX);
    CHECK(file_equals(path, elf_body, sizeof(elf_body) - 1));
    snprintf(path, sizeof(path), "%s/etc.conf", PKG_INSTALL_PREFIX);
    CHECK(file_equals(path, conf_body, sizeof(conf_body) - 1));

    /* 数据库落位 */
    CHECK(rpkg_is_installed("fixture") == 1);
    snprintf(path, sizeof(path), "%s/manifest/fixture", PKG_DB_ROOT);
    CHECK(file_exists(path));
    snprintf(path, sizeof(path), "%s/info/fixture.postinst", PKG_DB_ROOT);
    CHECK(file_exists(path));

    /* postinst 已执行 */
    snprintf(marker, sizeof(marker), "%s/postinst.ran", SANDBOX);
    CHECK(file_exists(marker));

    /* 重复安装被拒 */
    CHECK_EQ_INT(rpkg_install(FIXTURE_RPK), -EEXIST);

    /* rpkg_list 能看到 */
    CHECK_EQ_INT(rpkg_list(), OK);

    /* 卸载：prerm 执行、文件删除、数据库清空 */
    CHECK_EQ_INT(rpkg_remove("fixture"), OK);
    snprintf(marker, sizeof(marker), "%s/prerm.ran", SANDBOX);
    CHECK(file_exists(marker));
    snprintf(path, sizeof(path), "%s/bin/tool", PKG_INSTALL_PREFIX);
    CHECK(!file_exists(path));
    snprintf(path, sizeof(path), "%s/etc.conf", PKG_INSTALL_PREFIX);
    CHECK(!file_exists(path));
    CHECK(rpkg_is_installed("fixture") == 0);

    /* 卸载后再装（干净往返） */
    CHECK_EQ_INT(rpkg_install(FIXTURE_RPK), OK);
    CHECK_EQ_INT(rpkg_remove("fixture"), OK);
}

static void test_install_corrupt_payload(void)
{
    build_fixture_package();

    /* 复制并翻转【第二个】载荷文件（etc.conf）的一个字节——
     * 第一文件已落盘，可同时验证 CRC 拦截与失败回滚 */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "cp '%s' '%s/bad.rpk' && "
             "python3 -c \"d=bytearray(open('%s/bad.rpk','rb').read());"
             "i=d.find(b'key=value');assert i>0;"
             "d[i+2]^=0xff;open('%s/bad.rpk','wb').write(d)\"",
             FIXTURE_RPK, SANDBOX, SANDBOX, SANDBOX);
    CHECK_EQ_INT(system(cmd), 0);

    char path[RPKG_MAX_PATH];
    snprintf(path, sizeof(path), "%s/bin/tool", PKG_INSTALL_PREFIX);
    unlink(path);

    CHECK_EQ_INT(rpkg_install(SANDBOX "/bad.rpk"), -EILSEQ);

    /* 回滚：损坏包不留半截文件（含先前已落盘的 tool） */
    CHECK(!file_exists(path));
    snprintf(path, sizeof(path), "%s/etc.conf", PKG_INSTALL_PREFIX);
    CHECK(!file_exists(path));
    CHECK(rpkg_is_installed("fixture") == 0);
}

static void test_install_traversal_rejected(void)
{
    /* 手工构造恶意包：data/../evil 条目 */
    static uint8_t arc[4096];
    size_t off = 0;

    static const char ctl1[] = "Package: evil\nVersion: 1\n";
    off = ustar_append_entry(arc, off, "control",
                             ctl1, strlen(ctl1), '0');
    off = ustar_append_entry(arc, off, "data/../evil", "boom", 4, '0');
    memset(arc + off, 0, 1024);
    off += 1024;

    write_file(SANDBOX "/evil.rpk", arc, off);

    CHECK_EQ_INT(rpkg_install(SANDBOX "/evil.rpk"), -EINVAL);
    CHECK(!file_exists(SANDBOX "/evil"));
    CHECK(rpkg_is_installed("evil") == 0);
}

static void test_install_bad_package_name(void)
{
    static uint8_t arc[2048];
    size_t off = 0;

    static const char ctl2[] = "Package: ../../etc/pwn\nVersion: 1\n";
    off = ustar_append_entry(arc, off, "control",
                             ctl2, strlen(ctl2), '0');
    memset(arc + off, 0, 1024);
    off += 1024;

    write_file(SANDBOX "/pwn.rpk", arc, off);
    CHECK_EQ_INT(rpkg_install(SANDBOX "/pwn.rpk"), -EINVAL);
}

static void test_install_arch_mismatch(void)
{
    static uint8_t arc[2048];
    size_t off = 0;

    static const char ctl3[] = "Package: wrongarch\nVersion: 1\nArch: mips\n";
    off = ustar_append_entry(arc, off, "control",
                             ctl3, strlen(ctl3), '0');
    memset(arc + off, 0, 1024);
    off += 1024;

    write_file(SANDBOX "/mips.rpk", arc, off);
    CHECK_EQ_INT(rpkg_install(SANDBOX "/mips.rpk"), -EINVAL);
    CHECK(rpkg_is_installed("wrongarch") == 0);
}

static void test_missing_control(void)
{
    static uint8_t arc[2048];
    size_t off = 0;

    off = ustar_append_entry(arc, off, "data/x", "x", 1, '0');
    memset(arc + off, 0, 1024);
    off += 1024;

    write_file(SANDBOX "/noctl.rpk", arc, off);
    CHECK_EQ_INT(rpkg_install(SANDBOX "/noctl.rpk"), -EINVAL);
}

static void test_not_found(void)
{
    CHECK_EQ_INT(rpkg_install(SANDBOX "/nonexistent.rpk"), -ENOENT);
    CHECK_EQ_INT(rpkg_remove("never-installed"), -ENOENT);
    CHECK_EQ_INT(rpkg_remove("../evil"), -EINVAL);
    CHECK_EQ_INT(rpkg_info("nosuchpkg"), -ENOENT);
}

/*==========================
 *  main
 *==========================*/

int main(void)
{
    test_crc32_known_vectors();
    test_tar_parse_size();
    test_path_is_safe();
    test_arch_match();
    test_pkg_name_valid();
    test_control_parse();
    test_manifest_parse();
    test_tar_iteration();
    test_tar_vs_gnu_tar();
    test_install_remove_e2e();
    test_install_corrupt_payload();
    test_install_traversal_rejected();
    test_install_bad_package_name();
    test_install_arch_mismatch();
    test_missing_control();
    test_not_found();

    TEST_REPORT("test_pkgmanager");
}
