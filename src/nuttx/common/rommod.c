/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * rommod.c - ROM XIP 应用模块加载器实现 / ROM XIP module loader
 *
 * WHAT : 把 ET_DYN 应用模块（.rmo）装载为可执行实体：只读段在
 *        Flash 原址执行（XIP 零拷贝），可写段入 RAM 并重定位
 * WHY  : 应用/系统分离（2026-10-06）：应用装在 ROM 包存储、代码不
 *        整体载入内存——C3/Pico 小 SRAM 板受益最大；Xtensa 档因
 *        l32r 字面量内嵌 text（flash 只读不可补丁）改走构建期
 *        静态绑定（重定位归零），ARM/RISC-V 走运行时动态重定位
 * WHO  : desktop.c（GUI 模块）、cmd_run（CLI 模块）、宿主测试
 * WHERE: retro-ws/src/nuttx/common/rommod.c
 * WHEN : 2026-10-06 新增
 * HOW  : 自包含 ELF32/64 解析（逐字段小端读取，免 libelf 依赖与
 *        对齐陷阱；alloc 节经 va2addr 映射、非 alloc 节按文件偏移）：
 *        PT_LOAD 分段（RW->RAM 拷贝，RO->原址 XIP）-> 遍历全部
 *        SHT_REL/RELA 节按 e_machine 分派 ARM(REL)/RISC-V/x86-64
 *        (RELA) 补丁 GOT 与数据指针；无重定位节即静态绑定档
 *        （数据镜像直拷 arena，区间经 rommod_set_arena 校验防
 *        布局漂移）。外部符号经 rommod_bind 符号表二分解析。
 */

#include <nuttx/config.h>

#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rommod.h"

/*==========================
 *  ELF 常量（自包含定义，不依赖系统 elf.h）
 *==========================*/

#define ELFCLASS32       1
#define ELFCLASS64       2
#define ELFDATA2LSB      1
#define ET_EXEC          2
#define ET_DYN           3

#define PT_LOAD          1

#define SHT_SYMTAB       2
#define SHT_STRTAB       3
#define SHT_RELA         4
#define SHT_REL          9
#define SHT_DYNSYM       11
#define SHN_UNDEF        0
#define SHF_ALLOC        0x2

#define PF_W             0x2

/* 机器类型（装载目标白名单） */
#define EM_ARM           3
#define EM_X86_64        62
#define EM_XTENSA        94
#define EM_RISCV         243

/* 重定位类型（按机器分组，语义见应用函数） */
#define R_ARM_ABS32          2
#define R_ARM_GLOB_DAT       21
#define R_ARM_JUMP_SLOT      22
#define R_ARM_RELATIVE       23

#define R_RISCV_32           1
#define R_RISCV_GLOB_DAT     20
#define R_RISCV_JUMP_SLOT    21
#define R_RISCV_RELATIVE     3
#define R_RISCV_64           257

#define R_X86_64_64          1
#define R_X86_64_GLOB_DAT    6
#define R_X86_64_JUMP_SLOT   7
#define R_X86_64_RELATIVE    8

/*==========================
 *  小端字段读取（免 packed 结构/对齐/别名问题）
 *==========================*/

static uint32_t ld32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t ld64(const uint8_t *p)
{
    return (uint64_t)ld32(p) | ((uint64_t)ld32(p + 4) << 32);
}

/* 规范化 ELF 头（32/64 统一） */
struct rm_ehdr_s {
    uint8_t  class;
    uint16_t type;
    uint16_t machine;
    uint64_t phoff;
    uint64_t shoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
};

struct rm_ph_s {
    uint32_t type;
    uint32_t flags;
    uint64_t off;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
};

struct rm_sh_s {
    uint32_t type;
    uint32_t link;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint64_t entsize;
};

struct rm_sym_s {
    uint32_t name;
    uint16_t shndx;
    uint64_t value;
};

/* 规范化重定位项（REL 无加数时 addend=0 且 has_addend=false） */
struct rm_rloc_s {
    uint64_t offset;
    uint64_t info;
    int64_t  addend;
    bool     has_addend;
};

/*
 * WHAT : 解析 ELF 头（class 区分 32/64 字段宽度）
 * 返回 : OK / -ENOEXEC（截断或标识非法）
 */
static int read_ehdr(const uint8_t *f, size_t len, struct rm_ehdr_s *eh)
{
    if (len < 52)                    /* ELF32 头最小尺寸 */
        return -ENOEXEC;
    if (memcmp(f, "\x7f" "ELF", 4) != 0)
        return -ENOEXEC;
    if (f[4] != ELFCLASS32 && f[4] != ELFCLASS64)
        return -ENOEXEC;
    if (f[5] != ELFDATA2LSB)
        return -ENOEXEC;             /* 全系目标与宿主均为小端 */

    eh->class = f[4];
    if (eh->class == ELFCLASS64) {
        if (len < 64)
            return -ENOEXEC;
        eh->type      = (uint16_t)ld32(f + 16);
        eh->machine   = (uint16_t)ld32(f + 18);
        eh->phoff     = ld64(f + 32);
        eh->shoff     = ld64(f + 40);
        eh->phentsize = (uint16_t)ld32(f + 54);
        eh->phnum     = (uint16_t)ld32(f + 56);
        eh->shentsize = (uint16_t)ld32(f + 58);
        eh->shnum     = (uint16_t)ld32(f + 60);
    } else {
        eh->type      = (uint16_t)ld32(f + 16);
        eh->machine   = (uint16_t)ld32(f + 18);
        eh->phoff     = ld32(f + 28);
        eh->shoff     = ld32(f + 32);
        eh->phentsize = (uint16_t)ld32(f + 42);
        eh->phnum     = (uint16_t)ld32(f + 44);
        eh->shentsize = (uint16_t)ld32(f + 46);
        eh->shnum     = (uint16_t)ld32(f + 48);
    }

    /* e_type/e_machine 在小端下取低 16 位（HalfWord 位于 4 字节槽内）。
     * ET_DYN=动态/静态绑定两档通用；ET_EXEC=静态绑定档专用产物
     * （build_romapps 直链，无 PLT——Thumb-1 无 PLT 支持） */
    if (eh->type != ET_DYN && eh->type != ET_EXEC)
        return -ENOEXEC;
    if (eh->machine != EM_ARM && eh->machine != EM_RISCV &&
        eh->machine != EM_X86_64 && eh->machine != EM_XTENSA)
        return -ENOEXEC;
    return OK;
}

/* 读第 idx 个程序头（越界返回 -ENOEXEC） */
static int read_phdr(const uint8_t *f, size_t len,
                     const struct rm_ehdr_s *eh, int idx,
                     struct rm_ph_s *ph)
{
    size_t off = (size_t)eh->phoff + (size_t)idx * eh->phentsize;

    if (eh->phentsize == 0)
        return -ENOEXEC;
    if (eh->class == ELFCLASS64) {
        if (off + 56 > len)
            return -ENOEXEC;
        ph->type   = ld32(f + off);
        ph->flags  = ld32(f + off + 4);
        ph->off    = ld64(f + off + 8);
        ph->vaddr  = ld64(f + off + 16);
        ph->filesz = ld64(f + off + 32);
        ph->memsz  = ld64(f + off + 40);
    } else {
        if (off + 32 > len)
            return -ENOEXEC;
        ph->type   = ld32(f + off);
        ph->off    = ld32(f + off + 4);
        ph->vaddr  = ld32(f + off + 8);
        ph->filesz = ld32(f + off + 16);
        ph->memsz  = ld32(f + off + 20);
        ph->flags  = ld32(f + off + 24);
    }
    return OK;
}

/* 读第 idx 个节头 */
static int read_shdr(const uint8_t *f, size_t len,
                     const struct rm_ehdr_s *eh, int idx,
                     struct rm_sh_s *sh)
{
    size_t off = (size_t)eh->shoff + (size_t)idx * eh->shentsize;

    if (eh->shentsize == 0)
        return -ENOEXEC;
    if (eh->class == ELFCLASS64) {
        if (off + 64 > len)
            return -ENOEXEC;
        sh->type    = ld32(f + off + 4);
        sh->flags   = ld64(f + off + 8);
        sh->addr    = ld64(f + off + 16);
        sh->offset  = ld64(f + off + 24);
        sh->size    = ld64(f + off + 32);
        sh->link    = ld32(f + off + 40);
        sh->entsize = ld64(f + off + 56);
    } else {
        if (off + 40 > len)
            return -ENOEXEC;
        sh->type    = ld32(f + off + 4);
        sh->flags   = ld32(f + off + 8);
        sh->addr    = ld32(f + off + 12);
        sh->offset  = ld32(f + off + 16);
        sh->size    = ld32(f + off + 20);
        sh->link    = ld32(f + off + 24);
        sh->entsize = ld32(f + off + 36);
    }
    return OK;
}

/* 读一个符号项（symtab 起始指针 + 索引） */
static int read_sym(const uint8_t *tab, int is64, int idx,
                    struct rm_sym_s *sym)
{
    if (is64) {
        const uint8_t *p = tab + (size_t)idx * 24;

        sym->name  = ld32(p);
        sym->shndx = (uint16_t)ld32(p + 6);
        sym->value = ld64(p + 8);
    } else {
        const uint8_t *p = tab + (size_t)idx * 16;

        sym->name  = ld32(p);
        sym->shndx = (uint16_t)ld32(p + 14);
        sym->value = ld32(p + 4);
    }
    return OK;
}

/* 读一个重定位项（rela=true 为 RELA 格式） */
static int read_rloc(const uint8_t *tab, int is64, int idx, bool rela,
                     struct rm_rloc_s *r)
{
    const uint8_t *p = tab + (size_t)idx * (is64 ? (rela ? 24 : 16)
                                                 : (rela ? 12 : 8));

    if (is64) {
        r->offset = ld64(p);
        r->info   = ld64(p + 8);
        r->addend = rela ? (int64_t)ld64(p + 16) : 0;
    } else {
        r->offset = ld32(p);
        r->info   = ld32(p + 4);
        r->addend = rela ? (int32_t)ld32(p + 8) : 0;
    }
    r->has_addend = rela;
    return OK;
}

/* 重定位项的符号索引与类型（按字宽拆 info）：
 * ELF64: 高 32 位=符号索引，低 32 位=类型；ELF32: 高 24 位=索引，低 8 位=类型 */
static uint32_t rloc_sym(const struct rm_rloc_s *r, int is64)
{
    return is64 ? (uint32_t)(r->info >> 32)
                : (uint32_t)(r->info >> 8);
}

static uint32_t rloc_type(const struct rm_rloc_s *r, int is64)
{
    return is64 ? (uint32_t)(r->info & 0xffffffffu)
                : (uint32_t)(r->info & 0xffu);
}

/*==========================
 *  模块句柄与全局状态
 *==========================*/

struct rommod_s {
    int          refs;
    int          flavor;
    char         name[ROMMOD_NAME_MAX];
    const uint8_t *file;             /* 文件 ROM 基址（XIP 视角） */
    size_t       filelen;
    struct {
        uintptr_t    vaddr;          /* 段链接期 VA */
        uint64_t     memsz;
        const uint8_t *xip;          /* 只读段：flash 原址基址 */
        uint8_t     *ram;            /* 可写段：RAM 镜像（静态档=arena 直址） */
        bool         ram_owned;      /* ram 是否为本加载器 malloc（卸载回收依据） */
    } seg[ROMMOD_MAX_SEGS];
    int          nsegs;
    size_t       ram_bytes;
    /* 导出符号表（flash 只读；动态档 .dynsym / 静态档 .symtab） */
    const uint8_t *symtab;
    int          nsyms;
    int          sym_is64;
    const char  *strtab;
};

/* 同名模块复用（desktop 重复启动同一应用共享一份 RAM 镜像） */
#define ROMMOD_MAX_MODULES 8
static struct rommod_s *g_modules[ROMMOD_MAX_MODULES];

/* 基础符号表（rommod_bind 注册，按名升序 -> 二分查找） */
static const struct rommod_sym_s *g_symtab;
static int g_symtab_n;

/* XIP 载荷寻址回调（默认 pkg_rom；宿主测试注入内存镜像） */
static rommod_xip_lookup_t g_xip_lookup;

/* 静态档 arena 合法区间 */
static uint8_t *g_arena_base;
static size_t   g_arena_size;

/* RAM 窗口内联模式：按 vaddr 布置段（dlopen 同款映射语义）——动态档
 * GOT 访问为 PC 相对，须保持链接期段间相对布局；宿主测试与未来
 * RAM 窗口板使用，flash XIP 板不走此路径 */
static bool g_inline_map;
static size_t g_inline_cap;

static const void *const *g_keep;

void rommod_set_keep(const void *const *keep)
{
    g_keep = keep;                  /* gc 根链锚点 + 运行期诊断句柄 */
}

void rommod_bind(const struct rommod_sym_s *tab, int n)
{
    g_symtab = tab;
    g_symtab_n = n;
}

void rommod_set_xip_provider(rommod_xip_lookup_t lookup)
{
    g_xip_lookup = lookup;
}

void rommod_set_arena(void *base, size_t size)
{
    g_arena_base = (uint8_t *)base;
    g_arena_size = size;
}

/*==========================
 *  地址映射与符号解析
 *==========================*/

/*
 * WHAT : 模块 VA -> 运行期地址（只读段=XIP flash 指针，可写段=RAM 镜像）
 * HOW  : 逐段线性查找；writable_only=true 时只匹配可写段（重定位
 *        补丁目标必须可写——打在 XIP 段等于写 flash，直接拒绝）
 */
static bool va2addr(const struct rommod_s *m, uintptr_t va, bool writable_only,
                    uintptr_t *out)
{
    for (int i = 0; i < m->nsegs; i++) {
        if (va >= m->seg[i].vaddr &&
            va - m->seg[i].vaddr < (uintptr_t)m->seg[i].memsz) {
            if (writable_only && m->seg[i].ram == NULL)
                continue;
            *out = (uintptr_t)(m->seg[i].ram ? m->seg[i].ram
                                             : m->seg[i].xip) +
                   (va - m->seg[i].vaddr);
            return true;
        }
    }
    return false;
}

static const struct rommod_sym_s *base_sym_lookup(const char *name)
{
    int lo = 0;
    int hi = g_symtab_n - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(g_symtab[mid].name, name);

        if (c == 0)
            return &g_symtab[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/*
 * WHAT : 解析模块内符号引用 -> 运行期地址
 * HOW  : 本模块定义 -> va2addr；未定义（SHN_UNDEF）-> 基础符号表
 * 返回 : true 且 *val 给出地址；false（含未解析符号名打印）
 */
static bool resolve_symbol(const struct rommod_s *m, uint32_t idx,
                           uintptr_t *val)
{
    struct rm_sym_s sym;

    if (idx >= (uint32_t)m->nsyms)
        return false;
    read_sym(m->symtab, m->sym_is64, (int)idx, &sym);

    if (sym.shndx != SHN_UNDEF)
        return va2addr(m, (uintptr_t)sym.value, false, val);

    const char *name = m->strtab + sym.name;
    const struct rommod_sym_s *base = base_sym_lookup(name);

    if (base == NULL) {
        printf("rommod: 未定义符号 / unresolved symbol: %s\n", name);
        return false;
    }
    *val = (uintptr_t)base->addr;
    return true;
}

/*==========================
 *  段装载（XIP 映射 / RAM 镜像 / arena 直拷）
 *==========================*/

/*
 * WHAT : RAM 窗口整段布置：每个 PT_LOAD 拷到 buf+p_vaddr（dlopen
 *        同款语义，保持 PC 相对段间布局）
 * HOW  : 降序拷贝防前段数据覆盖未读的源区；要求缓冲容量覆盖最大
 *        vaddr+memsz，且节头表不被段拷贝覆盖（否则后续解析失效）
 * 返回 : OK / -ENOEXEC（容量不足或头部会被覆盖）
 */
static int inline_remap(struct rommod_s *m, const struct rm_ehdr_s *eh)
{
    size_t need = 0;
    size_t hdr_end = (size_t)eh->shoff + (size_t)eh->shnum * eh->shentsize;

    for (int i = 0; i < eh->phnum; i++)
    {
        struct rm_ph_s ph;

        if (read_phdr(m->file, m->filelen, eh, i, &ph) != OK)
            return -ENOEXEC;
        if (ph.type != PT_LOAD)
            continue;
        if ((size_t)ph.vaddr + (size_t)ph.memsz > need)
            need = (size_t)ph.vaddr + (size_t)ph.memsz;
    }

    if (need > g_inline_cap)
        return -ENOEXEC;            /* 容量不足（调用方缓冲太小） */

    /* 降序拷贝（高 vaddr 先），bss 尾清零；节头表与任一段目标区
     * 相交则拒绝（拷贝会破坏后续节表解析） */
    for (int i = eh->phnum - 1; i >= 0; i--)
    {
        struct rm_ph_s ph;

        if (read_phdr(m->file, m->filelen, eh, i, &ph) != OK)
            return -ENOEXEC;
        if (ph.type != PT_LOAD)
            continue;

        uint8_t *dst = (uint8_t *)m->file + (size_t)ph.vaddr;

        /* 节头区 [shoff, hdr_end) 与段拷贝区 [vaddr, vaddr+filesz)
         * 相交则拒绝（拷贝会破坏后续节表解析） */
        if ((size_t)ph.vaddr < hdr_end &&
            (size_t)eh->shoff < (size_t)ph.vaddr + (size_t)ph.filesz)
            return -ENOEXEC;

        memmove(dst, m->file + (size_t)ph.off, (size_t)ph.filesz);
        memset(dst + (size_t)ph.filesz, 0,
               (size_t)ph.memsz - (size_t)ph.filesz);
    }
    return OK;
}

static int load_segments(struct rommod_s *m, const struct rm_ehdr_s *eh,
                         int flavor)
{
    if (g_inline_map)
    {
        int ret = inline_remap(m, eh);

        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < eh->phnum; i++) {
        struct rm_ph_s ph;

        if (read_phdr(m->file, m->filelen, eh, i, &ph) != OK)
            return -ENOEXEC;
        if (ph.type != PT_LOAD)
            continue;

        if (m->nsegs >= ROMMOD_MAX_SEGS)
            return -ENOEXEC;
        if ((size_t)ph.off > m->filelen ||
            (size_t)ph.filesz > m->filelen - (size_t)ph.off)
            return -ENOEXEC;

        if (ph.flags & PF_W) {
            uint8_t *dst;

            if (flavor == ROMMOD_FLAVOR_STATIC) {
                /* 静态档：p_vaddr 为构建期 arena 内烘焙地址，直拷 */
                uintptr_t target = (uintptr_t)ph.vaddr;

                if (g_arena_base == NULL ||
                    target < (uintptr_t)g_arena_base ||
                    (size_t)(target - (uintptr_t)g_arena_base) +
                        (size_t)ph.memsz > g_arena_size) {
                    printf("rommod: 静态档 RW 段越出 arena / arena "
                           "drift: %p\n", (void *)target);
                    return -EFAULT;
                }
                dst = (uint8_t *)target;

                memcpy(dst, m->file + (size_t)ph.off, (size_t)ph.filesz);
                memset(dst + (size_t)ph.filesz, 0,
                       (size_t)ph.memsz - (size_t)ph.filesz);
            } else if (g_inline_map) {
                /* RAM 窗口：inline_remap 已按 vaddr 布置就位，此处
                 * 只登记段（bss 已清零） */
                dst = (uint8_t *)m->file + (size_t)ph.vaddr;
            } else {
                /* 动态档（flash XIP 板）：按需 RAM 镜像——仅当模块
                 * 无 PC 相对 GOT 依赖时可用（本固件走静态绑定档） */
                dst = malloc((size_t)ph.memsz);
                if (dst == NULL)
                    return -ENOMEM;

                memcpy(dst, m->file + (size_t)ph.off, (size_t)ph.filesz);
                memset(dst + (size_t)ph.filesz, 0,
                       (size_t)ph.memsz - (size_t)ph.filesz);
            }

            m->seg[m->nsegs].vaddr = (uintptr_t)ph.vaddr;
            m->seg[m->nsegs].memsz = ph.memsz;
            m->seg[m->nsegs].ram = dst;
            m->seg[m->nsegs].ram_owned =
                (flavor == ROMMOD_FLAVOR_DYNAMIC) && !g_inline_map;
            m->seg[m->nsegs].xip = NULL;
            m->ram_bytes += (size_t)ph.memsz;
        } else {
            /* 只读段：flash 原址 XIP（零 RAM、零拷贝）；
             * inline 模式下内容已被 inline_remap 拷到 buf+vaddr */
            uintptr_t xip = (uintptr_t)(m->file +
                             (g_inline_map ? (size_t)ph.vaddr
                                           : (size_t)ph.off));

            if (ph.memsz != ph.filesz)
                return -ENOEXEC;     /* 只读段出现 NOBITS，非法 */

            if (flavor == ROMMOD_FLAVOR_STATIC &&
                (uintptr_t)ph.vaddr != xip) {
                /* 静态档地址已烘焙：运行期必须与链接期一致 */
                printf("rommod: 静态档 RO 段布局漂移 / layout drift: "
                       "link=%p run=%p\n",
                       (void *)(uintptr_t)ph.vaddr, (void *)xip);
                return -EFAULT;
            }

            m->seg[m->nsegs].vaddr = (uintptr_t)ph.vaddr;
            m->seg[m->nsegs].memsz = ph.memsz;
            m->seg[m->nsegs].xip = (const uint8_t *)xip;
            m->seg[m->nsegs].ram = NULL;
            m->seg[m->nsegs].ram_owned = false;
        }

        m->nsegs++;
    }

    return m->nsegs > 0 ? OK : -ENOEXEC;
}

/*==========================
 *  符号表与重定位节（节头表驱动）
 *==========================*/

/*
 * WHAT : 节数据指针：alloc 节经 va2addr（在装载段内），非 alloc 节
 *        按文件偏移（.symtab/.strtab 等不在 PT_LOAD 内）
 */
static bool section_ptr(const struct rommod_s *m, const struct rm_sh_s *sh,
                        const uint8_t **out)
{
    if (sh->flags & SHF_ALLOC) {
        uintptr_t a;

        if (!va2addr(m, (uintptr_t)sh->addr, false, &a))
            return false;
        *out = (const uint8_t *)a;
        return true;
    }

    if ((size_t)sh->offset + (size_t)sh->size > m->filelen)
        return false;
    *out = m->file + (size_t)sh->offset;
    return true;
}

/*
 * WHAT : 定位导出符号表（.dynsym 优先；静态档退 .symtab）
 * HOW  : 两遍扫描（先 DYNSYM 后 SYMTAB），配套 strtab 按链接索引取
 */
static int find_symtab(struct rommod_s *m, const struct rm_ehdr_s *eh)
{
    static const uint32_t want[2] = { SHT_DYNSYM, SHT_SYMTAB };

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < eh->shnum; i++) {
            struct rm_sh_s sh;
            struct rm_sh_s stsh;
            const uint8_t *symp;
            const uint8_t *strp;

            if (read_shdr(m->file, m->filelen, eh, i, &sh) != OK)
                return -ENOEXEC;
            if (sh.type != want[pass] || sh.entsize == 0 ||
                sh.link >= eh->shnum)
                continue;
            if (read_shdr(m->file, m->filelen, eh, (int)sh.link, &stsh) != OK)
                return -ENOEXEC;
            if (stsh.type != SHT_STRTAB)
                continue;

            if (!section_ptr(m, &sh, &symp) || !section_ptr(m, &stsh, &strp))
                continue;

            m->symtab = symp;
            m->nsyms = (int)(sh.size / sh.entsize);
            m->sym_is64 = (eh->class == ELFCLASS64);
            m->strtab = (const char *)strp;
            return OK;
        }
    }
    return -ENOEXEC;
}

/*
 * WHAT : 统计重定位节（存在性判定 -> 装载形态分流）
 */
static int scan_relocs(const struct rommod_s *m, const struct rm_ehdr_s *eh,
                       int *has_rel, int *has_rela)
{
    *has_rel = 0;
    *has_rela = 0;

    for (int i = 0; i < eh->shnum; i++) {
        struct rm_sh_s sh;

        if (read_shdr(m->file, m->filelen, eh, i, &sh) != OK)
            return -ENOEXEC;
        if (sh.entsize == 0)
            continue;
        if (sh.type == SHT_REL)
            *has_rel = 1;
        else if (sh.type == SHT_RELA)
            *has_rela = 1;
    }
    return OK;
}

/*==========================
 *  重定位应用（按机器分派）
 *==========================*/

/*
 * WHAT : 应用一条 ARM(REL) 重定位
 * HOW  : ABS32 = S + 原字（REL 就地加数）；GLOB_DAT/JUMP_SLOT = S；
 *        RELATIVE = 原字为模块内链接 VA，经 va2addr 双基映射
 * 返回 : OK / -ENOEXEC（目标不可写）/ -ENOENT / -ENOTSUP
 */
static int apply_arm_rel(struct rommod_s *m, const struct rm_rloc_s *r)
{
    uintptr_t t;
    uint32_t type = rloc_type(r, 0);
    uint32_t sym = rloc_sym(r, 0);

    if (!va2addr(m, (uintptr_t)r->offset, true, &t)) {
        printf("rommod: ARM 重定位目标不可写 / RO target: type=%u\n", type);
        return -ENOEXEC;
    }

    switch (type) {
    case R_ARM_RELATIVE: {
        uintptr_t mapped;

        if (!va2addr(m, ld32((const uint8_t *)t), false, &mapped))
            return -ENOEXEC;
        *(uint32_t *)t = (uint32_t)mapped;
        return OK;
    }
    case R_ARM_ABS32: {
        uintptr_t val;

        if (!resolve_symbol(m, sym, &val))
            return -ENOENT;
        *(uint32_t *)t = (uint32_t)val + ld32((const uint8_t *)t);
        return OK;
    }
    case R_ARM_GLOB_DAT:
    case R_ARM_JUMP_SLOT: {
        uintptr_t val;

        if (!resolve_symbol(m, sym, &val))
            return -ENOENT;
        *(uint32_t *)t = (uint32_t)val;
        return OK;
    }
    default:
        printf("rommod: 不支持的 ARM 重定位 / unsupported: %u\n", type);
        return -ENOTSUP;
    }
}

/*
 * WHAT : 应用一条 RELA 重定位（RISC-V / x86-64）
 * HOW  : RELATIVE 加数为模块内链接 VA -> va2addr；GLOB_DAT/JUMP_SLOT/
 *        32/64 = S + A；写宽按机器与字宽
 */
static int apply_rela(struct rommod_s *m, const struct rm_rloc_s *r,
                      uint16_t machine, int is64)
{
    uintptr_t t;
    uint32_t type = rloc_type(r, is64);
    uint32_t sym = rloc_sym(r, is64);
    uintptr_t addend = (uintptr_t)r->addend;

    if (!va2addr(m, (uintptr_t)r->offset, true, &t)) {
        printf("rommod: RELA 目标不可写 / RO target: type=%u\n", type);
        return -ENOEXEC;
    }

    if (machine == EM_RISCV) {
        uintptr_t val = 0;

        switch (type) {
        case R_RISCV_RELATIVE: {
            uintptr_t mapped;

            if (!va2addr(m, addend, false, &mapped))
                return -ENOEXEC;
            *(uint32_t *)t = (uint32_t)mapped;
            return OK;
        }
        case R_RISCV_32:
        case R_RISCV_GLOB_DAT:
        case R_RISCV_JUMP_SLOT:
            if (!resolve_symbol(m, sym, &val))
                return -ENOENT;
            *(uint32_t *)t = (uint32_t)(val + addend);
            return OK;
        case R_RISCV_64:
            if (!is64)
                return -ENOTSUP;
            if (!resolve_symbol(m, sym, &val))
                return -ENOENT;
            *(uint64_t *)t = (uint64_t)(val + addend);
            return OK;
        default:
            printf("rommod: 不支持的 RISCV 重定位 / unsupported: %u\n",
                   type);
            return -ENOTSUP;
        }
    }

    if (machine == EM_X86_64) {
        uintptr_t val = 0;

        switch (type) {
        case R_X86_64_RELATIVE: {
            uintptr_t mapped;

            if (!va2addr(m, addend, false, &mapped))
                return -ENOEXEC;
            *(uint64_t *)t = (uint64_t)mapped;
            return OK;
        }
        case R_X86_64_64:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            if (!resolve_symbol(m, sym, &val))
                return -ENOENT;
            *(uint64_t *)t = (uint64_t)(val + addend);
            return OK;
        default:
            printf("rommod: 不支持的 x86-64 重定位 / unsupported: %u\n",
                   type);
            return -ENOTSUP;
        }
    }

    return -ENOTSUP;
}

/*
 * WHAT : 遍历全部 SHT_REL/SHT_RELA 节并应用重定位
 * WHY  : 以节头表驱动统一覆盖 .rel.dyn/.rela.plt 等全部节，不依赖
 *        .dynamic 的 JMPREL/PLTRELSZ 配对解析（节表在文件内只读直查）
 */
static int process_relocs(struct rommod_s *m, const struct rm_ehdr_s *eh)
{
    for (int i = 0; i < eh->shnum; i++) {
        struct rm_sh_s sh;

        if (read_shdr(m->file, m->filelen, eh, i, &sh) != OK)
            return -ENOEXEC;
        if ((sh.type != SHT_REL && sh.type != SHT_RELA) || sh.entsize == 0)
            continue;

        const uint8_t *ents;

        if (!section_ptr(m, &sh, &ents))
            continue;                /* 不在装载段内的重定位节忽略 */

        int n = (int)(sh.size / sh.entsize);
        bool rela = (sh.type == SHT_RELA);

        for (int k = 0; k < n; k++) {
            struct rm_rloc_s r;
            int ret;

            read_rloc(ents, eh->class == ELFCLASS64, k, rela, &r);
            if (rela)
                ret = apply_rela(m, &r, (uint16_t)eh->machine,
                                 eh->class == ELFCLASS64);
            else
                ret = apply_arm_rel(m, &r);   /* REL 仅 ARM32 使用 */

            if (ret < 0)
                return ret;
        }
    }
    return OK;
}

/*==========================
 *  对外 API
 *==========================*/

int rommod_load_from_mem(const char *name, const uint8_t *mem,
                         size_t len, struct rommod_s **mod)
{
    struct rm_ehdr_s eh;
    struct rommod_s *m;
    int has_rel;
    int has_rela;
    int flavor;
    int ret;

    if (name == NULL || mem == NULL || mod == NULL || len < 52)
        return -EINVAL;
    if (((uintptr_t)mem & 3u) != 0)
        return -EINVAL;              /* 指令/字访问至少 4 字节对齐 */

    ret = read_ehdr(mem, len, &eh);
    if (ret < 0)
        return ret;
    if (eh.class == ELFCLASS64 && sizeof(void *) < 8)
        return -ENOEXEC;             /* 32 位目标拒收 64 位模块 */

    m = calloc(1, sizeof(*m));
    if (m == NULL)
        return -ENOMEM;
    snprintf(m->name, sizeof(m->name), "%s", name);
    m->file = mem;
    m->filelen = len;

    /* 形态判定：有重定位节=动态档；无=静态绑定档 */
    ret = scan_relocs(m, &eh, &has_rel, &has_rela);
    if (ret < 0)
        goto err_out;

    flavor = (has_rel || has_rela) ? ROMMOD_FLAVOR_DYNAMIC
                                   : ROMMOD_FLAVOR_STATIC;

    if (flavor == ROMMOD_FLAVOR_DYNAMIC && eh.type != ET_DYN)
        {
            return -ENOEXEC;         /* 动态重定位仅接受共享对象布局 */
        }

    if (flavor == ROMMOD_FLAVOR_DYNAMIC && eh.machine == EM_XTENSA) {
        /* Xtensa 字面量内嵌 text（flash 只读），运行时补丁不可行 */
        printf("rommod: Xtensa 模块须静态绑定档构建 / use static-bind "
               "build\n");
        ret = -ENOSYS;
        goto err_out;
    }
    if (has_rel && has_rela) {
        ret = -ENOEXEC;              /* 混合 REL/RELA 非预期产物 */
        goto err_out;
    }

    ret = load_segments(m, &eh, flavor);
    if (ret < 0)
        goto err_out;

    ret = find_symtab(m, &eh);
    if (ret < 0)
        goto err_out;

    if (flavor == ROMMOD_FLAVOR_DYNAMIC) {
        ret = process_relocs(m, &eh);
        if (ret < 0)
            goto err_out;
    }

    m->refs = 1;
    m->flavor = flavor;
    *mod = m;

    syslog(LOG_INFO, "[rommod] %s loaded: flavor=%s flash=%uB ram=%uB\n",
           m->name, flavor == ROMMOD_FLAVOR_STATIC ? "static" : "dynamic",
           (unsigned)m->filelen, (unsigned)m->ram_bytes);
    return OK;

err_out:
    for (int i = 0; i < m->nsegs; i++) {
        if (m->seg[i].ram_owned)
            free(m->seg[i].ram);     /* 静态档 arena 直址非 malloc，不回收 */
    }
    free(m);
    return ret;
}

int rommod_load_from_mem_inline(const char *name, uint8_t *mem,
                                size_t len, size_t cap,
                                struct rommod_s **mod)
{
    int ret;

    if (((uintptr_t)mem & 3u) != 0 || cap < len)
        return -EINVAL;

    g_inline_map = true;
    g_inline_cap = cap;
    ret = rommod_load_from_mem(name, mem, len, mod);
    g_inline_map = false;
    g_inline_cap = 0;
    return ret;
}

int rommod_load(const char *name, struct rommod_s **mod)
{
    /* 同名模块复用：引用 +1（共享同一份 RAM 镜像） */
    for (int i = 0; i < ROMMOD_MAX_MODULES; i++) {
        if (g_modules[i] != NULL && strcmp(g_modules[i]->name, name) == 0) {
            rommod_get(g_modules[i]);
            *mod = g_modules[i];
            return OK;
        }
    }

    if (g_xip_lookup == NULL) {
        printf("rommod: XIP 提供方未注册 / no xip provider\n");
        return -ENOSYS;
    }

    const uint8_t *data = NULL;
    size_t len = 0;

    if (g_xip_lookup(name, &data, &len) != OK)
        return -ENOENT;

    struct rommod_s *m = NULL;
    int ret = rommod_load_from_mem(name, data, len, &m);

    if (ret < 0)
        return ret;

    for (int i = 0; i < ROMMOD_MAX_MODULES; i++) {
        if (g_modules[i] == NULL) {
            g_modules[i] = m;
            *mod = m;
            return OK;
        }
    }

    rommod_put(m);                   /* 注册表满：回滚 */
    printf("rommod: 模块注册表已满 / registry full\n");
    return -ENOMEM;
}

void rommod_get(struct rommod_s *mod)
{
    if (mod != NULL)
        mod->refs++;
}

void rommod_put(struct rommod_s *mod)
{
    if (mod == NULL || mod->refs <= 0)
        return;

    mod->refs--;
    if (mod->refs > 0)
        return;

    for (int i = 0; i < ROMMOD_MAX_MODULES; i++) {
        if (g_modules[i] == mod)
            g_modules[i] = NULL;
    }

    syslog(LOG_INFO, "[rommod] %s unloaded (%uB ram freed)\n",
           mod->name, (unsigned)mod->ram_bytes);

    for (int i = 0; i < mod->nsegs; i++) {
        if (mod->seg[i].ram_owned)
            free(mod->seg[i].ram);   /* 静态档 arena 直址常驻固件，不回收 */
    }
    free(mod);
}

int rommod_getsym(struct rommod_s *mod, const char *name, void **addr)
{
    if (mod == NULL || name == NULL || addr == NULL)
        return -EINVAL;

    for (int i = 0; i < mod->nsyms; i++) {
        struct rm_sym_s sym;

        read_sym(mod->symtab, mod->sym_is64, i, &sym);
        if (sym.shndx == SHN_UNDEF)
            continue;
        if (strcmp(mod->strtab + sym.name, name) == 0) {
            uintptr_t a;

            if (!va2addr(mod, (uintptr_t)sym.value, false, &a))
                return -ENOEXEC;
            *addr = (void *)a;
            return OK;
        }
    }
    return -ENOENT;
}

/*==========================
 *  观测接口
 *==========================*/

int rommod_flavor(const struct rommod_s *mod)
{
    return mod ? mod->flavor : 0;
}

size_t rommod_ram_used(const struct rommod_s *mod)
{
    return mod ? mod->ram_bytes : 0;
}

size_t rommod_flash_size(const struct rommod_s *mod)
{
    return mod ? mod->filelen : 0;
}

int rommod_active_count(void)
{
    int n = 0;

    for (int i = 0; i < ROMMOD_MAX_MODULES; i++)
        if (g_modules[i] != NULL)
            n++;
    return n;
}
