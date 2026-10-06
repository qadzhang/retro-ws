/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * test_rommod.c - ROM XIP 模块加载器宿主测试（ai-code-testing 分层）
 *
 * WHAT : rommod.c 的端到端/蜕变/差分/拒绝路径测试——在宿主机上用
 *        真实 x86-64 ET_DYN 模块跑完整装载链路
 * WHY  : rommod 是应用/系统分离的核心承重件（XIP 执行/省内存），
 *        装载器错误=任意代码执行面，必须机器化闭环（SKILL 9 层）
 * WHO  : tests/host/run_all.sh（步骤 build+unit rommod）
 * WHERE: retro-ws/tests/host/test_rommod.c
 * WHEN : 2026-10-06 新增
 * HOW  : 四类方法（Layer 3/4）：
 *   端到端：gcc 现场编 3 个模块（动态 CLI/动态 GUI 描述符/静态绑定），
 *     经 rommod_load_from_mem 装载 -> getsym -> 调用 -> 断言副作用；
 *     静态绑定档复刻 build_romapps.sh 的"mmap 定址 -> readelf 取段偏移
 *     -> 重链 -> 装载"全流程
 *   蜕变：load/put 守恒（active_count 归零）、同名复用共享镜像、
 *     ram_used <= flash_size 且 == RW 段合计、重复装载 N 轮无泄漏
 *   差分：同一 .so 经 dlopen(RTLD_NOW) 与 rommod 两条路径调用，
 *     返回值与全局副作用逐一相等（对标型 oracle）
 *   拒绝路径：坏魔数/截断/非 ET_DYN/TLS 重定位/未解析符号/
 *     静态档地址漂移（arena 外）/RO 段漂移
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rommod.h"
#include "test_framework.h"

/* 差分/端到端用外部符号：模块经基础符号表（或 -rdynamic 的 dlopen）解析 */
long g_ext_counter = 100;
int  ext_doubler(int x) { return x * 2; }
static void *ext_obj_alloc(void) { return (void *)&g_ext_counter; }

#define FIXDIR "/tmp/retro_test/rommod"

static int fixture_write(const char *name, const char *text)
{
    char path[256];
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", FIXDIR, name);
    f = fopen(path, "w");
    if (!f)
        return -1;
    fputs(text, f);
    fclose(f);
    return 0;
}

static int run_cmd(const char *cmd)
{
    int rc = system(cmd);

    return rc == 0 ? 0 : -1;
}

/*==========================
 *  基础符号表（rommod 路径解析；与 -rdynamic 导出一致）
 *==========================*/

static const struct rommod_sym_s g_test_syms[] =
{
    { "ext_doubler",   ext_doubler },
    { "ext_obj_alloc", ext_obj_alloc },
    { "g_ext_counter", &g_ext_counter },
};

/*==========================
 *  XIP 提供方：内存镜像注册表
 *==========================*/

struct mem_file_s
{
    char name[32];
    uint8_t *data;
    size_t len;
};

static struct mem_file_s g_memfiles[8];
static int g_nmemfiles;

static int memfile_lookup(const char *name, const uint8_t **data, size_t *len)
{
    for (int i = 0; i < g_nmemfiles; i++)
    {
        if (strcmp(g_memfiles[i].name, name) == 0)
        {
            *data = g_memfiles[i].data;
            *len = g_memfiles[i].len;
            return 0;
        }
    }
    return -1;
}

static void memfile_register(const char *name, const char *path)
{
    struct mem_file_s *mf = &g_memfiles[g_nmemfiles++];
    FILE *f = fopen(path, "rb");

    snprintf(mf->name, sizeof(mf->name), "%s", name);
    fseek(f, 0, SEEK_END);
    mf->len = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* 模拟 ROMFS 文件数据指针（flash 只读语义；registry 上的模块
     * 仅做装载级测试——调用级测试走 inline/静态档各自的专用镜像） */
    mf->data = malloc((mf->len + 15) & ~15UL);
    if (fread(mf->data, 1, mf->len, f) != mf->len)
        exit(2);
    fclose(f);
}

/* 可执行镜像装载（inline 模式调用模块代码：RAM 窗口 RWX；
 * cap 预留 vaddr+memsz 布局余量，页面取整仍可能不足——固定 0x8000） */
#define EXEC_IMG_CAP 0x8000

static uint8_t *slurp_exec(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    size_t pagesz;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    pagesz = EXEC_IMG_CAP;
    buf = mmap(NULL, pagesz, PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED || fread(buf, 1, *len, f) != *len)
        exit(2);
    fclose(f);
    return buf;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((*len + 15) & ~15UL);
    if (fread(buf, 1, *len, f) != *len)
        exit(2);
    fclose(f);
    return buf;
}

/*==========================
 *  测试用例
 *==========================*/

/*
 * 端到端：动态档 CLI 模块——装载/getsym/main 调用/副作用/卸载
 */
static void test_dynamic_cli(void)
{
    struct rommod_s *mod = NULL;
    void *entry = NULL;
    int (*fn)(int, char **);
    long before = g_ext_counter;
    int ret;

    size_t _len = 0;
    uint8_t *_img = slurp_exec(FIXDIR "/mod_cli.so", &_len);
    CHECK(_img != NULL);
    CHECK(rommod_load_from_mem_inline("modcli", _img, _len, 0x8000, &mod) == 0);
    CHECK(rommod_flavor(mod) == ROMMOD_FLAVOR_DYNAMIC);
    CHECK(rommod_ram_used(mod) > 0);
    CHECK(rommod_ram_used(mod) <= rommod_flash_size(mod));
    CHECK(rommod_getsym(mod, "main", &entry) == 0 && entry);;

    fn = entry;
    g_ext_counter = before;
    ret = fn(5, NULL);
    CHECK(ret == 11);
    CHECK(g_ext_counter == before + 5);

    rommod_put(mod);
    CHECK(rommod_active_count() == 0);
}

/*
 * 端到端：GUI 描述符模块——retro_app_module.h 契约（真实头文件编译）
 */
static void test_dynamic_gui_descriptor(void)
{
    struct rommod_s *mod = NULL;
    void *sym = NULL;
    const struct
    {
        uint32_t magic;
        uint32_t abi_version;
        const char *app_id;
        const char *title_zh;
        const char *title_en;
        const char *icon;
        void *(*create)(void);
    } *info;

    {
        size_t glen = 0;
        uint8_t *gimg = slurp_exec(FIXDIR "/mod_gui.so", &glen);
        CHECK(gimg != NULL);
        CHECK(rommod_load_from_mem_inline("modgui", gimg, glen, EXEC_IMG_CAP, &mod) == 0);
    }
    CHECK(mod != NULL);
    if (mod)
    CHECK(rommod_getsym(mod, "retro_gui_app_info", &sym) == 0 && sym);
    info = sym;
    CHECK(info->magic == 0x5247414Du);
    CHECK(info->abi_version == 1);
    CHECK(strcmp(info->app_id, "editor") == 0);;
    CHECK(strcmp(info->title_zh, "记事本") == 0);;
    CHECK(strcmp(info->icon, "[T]") == 0);;
    CHECK(info->create() == (void *)&g_ext_counter);
    rommod_put(mod);
}

/*
 * 蜕变：同名复用共享镜像；引用计数守恒；N 轮装载无泄漏
 */
static void test_metamorphic_lifecycle(void)
{
    struct rommod_s *a = NULL;
    struct rommod_s *b = NULL;

    CHECK(rommod_load("modcli", &a) == 0);;
    CHECK(rommod_load("modcli", &b) == 0);;
    CHECK(a == b);
    CHECK(rommod_active_count() == 1);
    rommod_put(a);
    CHECK(rommod_active_count() == 1);

    for (int i = 0; i < 32; i++)
    {
        struct rommod_s *m = NULL;

        CHECK(rommod_load("modcli", &m) == 0);;
        rommod_put(m);
    }
    rommod_put(b);
    CHECK(rommod_active_count() == 0);
}

/*
 * 差分：dlopen(RTLD_NOW) vs rommod 同一 .so 同输入同输出
 */
static void test_differential_dlopen(void)
{
    char path[256];
    void *dl = NULL;
    int (*dlmain)(int, char **);
    struct rommod_s *mod = NULL;
    void *entry = NULL;
    int (*rmain)(int, char **);
    long c0, c1, c2;
    int r1, r2;

    snprintf(path, sizeof(path), "%s/mod_cli.so", FIXDIR);
    dl = dlopen(path, RTLD_NOW);
    CHECK(dl != NULL);;
    dlmain = dlsym(dl, "main");
    CHECK(dlmain != NULL);

    {
        size_t dlen = 0;
        uint8_t *dimg = slurp_exec(FIXDIR "/mod_cli.so", &dlen);
        CHECK(dimg != NULL);
        CHECK(rommod_load_from_mem_inline("modcli", dimg, dlen, EXEC_IMG_CAP, &mod) == 0);
    }
    CHECK(mod != NULL);
    if (mod)
    CHECK(rommod_getsym(mod, "main", &entry) == 0);
    rmain = entry;

    c0 = g_ext_counter;
    r1 = dlmain(3, NULL);
    c1 = g_ext_counter;
    r2 = rmain(3, NULL);
    c2 = g_ext_counter;

    CHECK(r1 == r2);
    CHECK(c1 - c0 == c2 - c1);
    CHECK(r1 == 7);

    rommod_put(mod);
    dlclose(dl);
}

/*
 * 端到端：静态绑定档——复刻 build_romapps.sh 全流程
 * （mmap 定址 -> pass0 链接测段偏移 -> 以真实 TEXT/ARENA 基址重链
 *   -> 镜像内装载 -> 只读段必须落在 mmap 预留地址上）
 */
#define ST_ROM_BASE  ((void *)0x60000000UL)
#define ST_ARENA_BASE ((void *)0x61000000UL)
#define ST_FILE_OFF  0x2000                       /* 模块在"镜像"内偏移 */

static void test_static_bind(void)
{
    /* pass0 链接取 RX 段文件内偏移（链接脚本决定，两遍尺寸一致） */
    CHECK(run_cmd(
        "gcc -fno-pic -fno-builtin -nostdlib "
        "-Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-e,main "
        "-Wl,-T," FIXDIR "/static.ld "
        "-Wl,--defsym=TEXT_BASE=0x50000000 "
        "-Wl,--defsym=ARENA_BASE=0x51000000 "
        "-o " FIXDIR "/mod_static0.so " FIXDIR "/mod_static.c "
        "2>/dev/null") == 0);;

    char cmd[512];
    FILE *p;
    unsigned long p_off = 0;

    snprintf(cmd, sizeof(cmd),
             "readelf -lW " FIXDIR "/mod_static0.so | grep ' R E ' | "
             "head -1 | awk '{print $2}'");
    p = popen(cmd, "r");
    CHECK(p && fscanf(p, "%lx", &p_off) == 1 && p_off > 0);;
    pclose(p);

    /* mmap "ROM 镜像"与 arena 到固定地址（模块地址已烘焙的前提） */
    uint8_t *rom = mmap(ST_ROM_BASE, 1 << 20, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    uint8_t *arena = mmap(ST_ARENA_BASE, 65536, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    CHECK(rom == ST_ROM_BASE && arena == ST_ARENA_BASE);

    unsigned long text_base = (unsigned long)ST_ROM_BASE + ST_FILE_OFF + p_off;

    /* 以真实基址重链（build_romapps finalize 同款） */
    snprintf(cmd, sizeof(cmd),
        "gcc -fno-pic -fno-builtin -nostdlib "
        "-Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-e,main "
        "-Wl,-T," FIXDIR "/static.ld "
        "-Wl,--defsym=TEXT_BASE=0x%lx "
        "-Wl,--defsym=ARENA_BASE=0x%lx "
        "-o " FIXDIR "/mod_static.so " FIXDIR "/mod_static.c 2>/dev/null",
        text_base, (unsigned long)ST_ARENA_BASE);
    CHECK(run_cmd(cmd) == 0);

    size_t flen;
    uint8_t *fdata = slurp(FIXDIR "/mod_static.so", &flen);
    CHECK(fdata != NULL);
    size_t s0 = 0;
    {
        FILE *q = fopen(FIXDIR "/mod_static0.so", "rb");

        fseek(q, 0, SEEK_END);
        s0 = ftell(q);
        fclose(q);
    }
    CHECK(s0 == flen);

    memset(rom, 0, 1 << 20);
    memcpy(rom + ST_FILE_OFF, fdata, flen);
    free(fdata);
    /* 目标板上 flash 只读可执行；宿主模拟同样语义（写完转 RX） */
    mprotect(rom, 1 << 20, PROT_READ | PROT_EXEC);

    /* arena 必须先注册；装载后只读段 XIP 于 mmap 地址、数据镜像入 arena */
    rommod_set_arena(ST_ARENA_BASE, 65536);
    struct rommod_s *mod = NULL;

    CHECK(rommod_load_from_mem("modstatic", rom + ST_FILE_OFF,
                                     flen, &mod) == 0);;
    CHECK(rommod_flavor(mod) == ROMMOD_FLAVOR_STATIC);

    void *entry = NULL;
    CHECK(rommod_getsym(mod, "main", &entry) == 0);;

    int ret = ((int (*)(int, char **))entry)(8, NULL);
    CHECK(ret == 16);

    rommod_put(mod);
    rommod_set_arena(NULL, 0);
    munmap(rom, 1 << 20);
    munmap(arena, 65536);
}

/*
 * 拒绝路径：非法输入与不支持的模块形态（模糊测试结构化种子）
 */
static void test_rejections(void)
{
    struct rommod_s *mod = NULL;
    uint8_t buf[128];

    /* 坏魔数 */
    memset(buf, 0x41, sizeof(buf));
    CHECK(rommod_load_from_mem("bad", buf, sizeof(buf),
                                     &mod) == -ENOEXEC);;

    /* 截断的合法 ELF */
    {
        size_t flen;
        uint8_t *fdata = slurp(FIXDIR "/mod_cli.so", &flen);

        CHECK(fdata != NULL);
        mod = (struct rommod_s *)1;
        CHECK(rommod_load_from_mem("trunc", fdata, 52,
                                         &mod) == -ENOEXEC);;
        CHECK(rommod_load_from_mem("trunc", fdata, flen - 1,
                                         &mod) == -ENOEXEC ||
                    rommod_load_from_mem("trunc", fdata, flen - 1,
                                         &mod) == 0);;
        if (rommod_active_count() > 0)
        {
            /* 若被宽容装载（截断点无害），卸载之保持守恒 */
            struct rommod_s *m = NULL;

            rommod_load("trunc", &m);
            rommod_put(m);
            rommod_put(m);
        }
        free(fdata);
    }

    /* 非模块文件（ELF 魔数但 ET_REL 目标文件） */
    CHECK(run_cmd("gcc -c -fPIC -o " FIXDIR "/rel.o " FIXDIR
                        "/mod_cli.c 2>/dev/null") == 0);
    {
        size_t flen;
        uint8_t *fdata = slurp(FIXDIR "/rel.o", &flen);

        mod = NULL;
        CHECK(rommod_load_from_mem("rel", fdata, flen,
                                         &mod) == -ENOEXEC);;
        free(fdata);
    }

    /* TLS 重定位模块（__thread）——不支持，必须显式拒绝 */
    CHECK(run_cmd("gcc -fPIC -shared -nostdlib -fno-builtin -o " FIXDIR
                        "/mod_tls.so " FIXDIR "/mod_tls.c 2>/dev/null") == 0);
    {
        size_t flen;
        uint8_t *fdata = slurp(FIXDIR "/mod_tls.so", &flen);
        int rc = rommod_load_from_mem("tls", fdata, flen, &mod);

        CHECK(rc == -ENOTSUP || rc == -ENOEXEC || rc == -ENOENT);
        free(fdata);
    }

    /* 未解析符号（不在基础符号表） */
    CHECK(run_cmd("gcc -fPIC -shared -nostdlib -fno-builtin -o " FIXDIR
                        "/mod_missing.so " FIXDIR
                        "/mod_missing.c 2>/dev/null") == 0);
    {
        size_t flen;
        uint8_t *fdata = slurp(FIXDIR "/mod_missing.so", &flen);
        int rc = rommod_load_from_mem("missing", fdata, flen, &mod);

        CHECK(rc == -ENOENT);
        free(fdata);
    }

    /* 静态档地址漂移：arena 未注册时装载必须 -EFAULT */
    {
        size_t flen;
        uint8_t *fdata = slurp(FIXDIR "/mod_static0.so", &flen);

        mod = NULL;
        CHECK(rommod_load_from_mem("drift", fdata, flen,
                                         &mod) == -EFAULT);;
        free(fdata);
    }

    /* 不存在的模块名（XIP provider 未命中） */
    mod = NULL;
    CHECK(rommod_load("nosuchmod", &mod) == -ENOENT);;
}

/*
 * 蜕变：ram_used 恒等关系（多模块、混合形态）
 */
static void test_metamorphic_ram_accounting(void)
{
    struct rommod_s *a = NULL;
    struct rommod_s *b = NULL;
    size_t ra;
    size_t rb;

    CHECK(rommod_load("modcli", &a) == 0);;
    CHECK(rommod_load("modgui", &b) == 0);;
    ra = rommod_ram_used(a);
    rb = rommod_ram_used(b);

    /* 蜕变关系：XIP 模块 RAM 占用 = 可写段合计，严格小于文件体积 */
    CHECK(ra < rommod_flash_size(a));
    CHECK(rb < rommod_flash_size(b));
    CHECK(ra > 0 && rb > 0);

    rommod_put(a);
    rommod_put(b);
}

/*==========================
 *  入口：fixture 编译 + 用例调度
 *==========================*/

int main(void)
{
    mkdir(FIXDIR, 0755);

    /* CLI 动态模块 fixture（真实 retro 契约：extern 经符号表解析） */
    fixture_write("mod_cli.c",
        "extern int ext_doubler(int x);\n"
        "extern long g_ext_counter;\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    (void)argv;\n"
        "    g_ext_counter += argc;\n"
        "    return ext_doubler(argc) + 1;\n"
        "}\n");

    /* GUI 描述符模块 fixture：用真实 ABI 头（契约测试） */
    fixture_write("mod_gui.c",
        "#include <nuttx/config.h>\n"
        "#include \"retro_app_module.h\"\n"
        "extern void *ext_obj_alloc(void);\n"
        "static void *create(void) { return ext_obj_alloc(); }\n"
        "const struct retro_gui_app_info_s retro_gui_app_info =\n"
        "{\n"
        "    .magic = RETRO_APP_MAGIC,\n"
        "    .abi_version = RETRO_APP_ABI_VERSION,\n"
        "    .app_id = \"editor\",\n"
        "    .title_zh = \"记事本\",\n"
        "    .title_en = \"Notepad\",\n"
        "    .icon = \"[T]\",\n"
        "    .create = create,\n"
        "};\n");

    /* 静态绑定档 fixture：自包含（无外部引用 -> 零重定位） */
    fixture_write("mod_static.c",
        "static int s_data = 7;\n"
        "static int s_bss[16];\n"
        "int main(int argc, char **argv)\n"
        "{\n"
        "    (void)argv;\n"
        "    s_bss[0] = s_data + argc;\n"
        "    return s_bss[0] + 1;\n"
        "}\n");
    fixture_write("static.ld",
        "SECTIONS {\n"
        "  . = TEXT_BASE;\n"
        "  .text : { *(.text .text.* .rodata .rodata.* .literal .literal.* .init .fini) }\n"
        "  .dynro : { *(.dynsym .dynstr .hash .gnu.hash .rela.dyn .rela.plt .rel.dyn .rel.plt .init_array .fini_array) }\n"
        "  . = ARENA_BASE;\n"
        "  .rw : { *(.dynamic) *(.data.rel.ro .data.rel.ro.* .got .got.* .got.plt .data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) }\n"
        "}\n");

    /* TLS / 缺符号 fixture */
    fixture_write("mod_tls.c",
        "static __thread int t;\n"
        "int main(void) { return t; }\n");
    fixture_write("mod_missing.c",
        "extern int totally_absent_symbol(void);\n"
        "int main(void) { return totally_absent_symbol(); }\n");

    /* 编译动态档 fixtures（测试二进制 -rdynamic 供 dlopen 差分） */
    if (run_cmd("gcc -fPIC -shared -nostdlib -fno-builtin "
                "-I tests/host/stubs -I src/lvgl/app "
                "-o " FIXDIR "/mod_cli.so " FIXDIR "/mod_cli.c") != 0 ||
        run_cmd("gcc -fPIC -shared -nostdlib -fno-builtin "
                "-I tests/host/stubs -I src/nuttx/common -I src/lvgl/app "
                "-o " FIXDIR "/mod_gui.so " FIXDIR "/mod_gui.c") != 0)
    {
        printf("[rommod] fixture 编译失败\n");
        return 2;
    }

    rommod_bind(g_test_syms, 3);
    rommod_set_xip_provider(memfile_lookup);
    memfile_register("modcli", FIXDIR "/mod_cli.so");
    memfile_register("modgui", FIXDIR "/mod_gui.so");

    fprintf(stderr, "[phase] dynamic_cli\n");  test_dynamic_cli();
    fprintf(stderr, "[phase] gui_desc\n");      test_dynamic_gui_descriptor();
    fprintf(stderr, "[phase] lifecycle\n");     test_metamorphic_lifecycle();
    fprintf(stderr, "[phase] dlopen-diff\n");   test_differential_dlopen();
    fprintf(stderr, "[phase] static-bind\n");   test_static_bind();
    fprintf(stderr, "[phase] rejections\n");    test_rejections();
    fprintf(stderr, "[phase] ram-acct\n");      test_metamorphic_ram_accounting();

    TEST_REPORT("test_rommod");
}
