/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * pkg_manager.c - .rpk 包管理器实现 / retro package manager
 *
 * WHAT : Debian deb 风格的安装/卸载/列表/查询，处理 .rpk（USTAR 容器）
 * WHY  : 软件交付需要元数据+依赖+维护脚本+数据库，而非裸拷 ELF；
 *        GPL 组件经此通道以独立程序形态安装（许可证隔离不变）
 * WHO  : cmd_pkg（nsh_cmds.c）转发用户命令
 * WHERE: retro-ws/src/nuttx/common/apps/system/pkg_manager.c
 * WHEN : 2026-10-04 新增；同日修订（安全+健壮性，见 BUILD_FIXES.md）
 * HOW  : 流式解析 tar（512B 块缓冲，不整包载入内存）：
 *        control -> 架构/依赖/已装检查 -> 脚本入库(info/<pkg>.<名>) ->
 *        preinst -> data/ 逐文件落盘（实测 CRC32 与打包器 manifest
 *        逐一比对，不符即回滚）-> 写数据库 -> postinst。
 *        卸载：prerm -> 按 manifest 删文件 -> postrm -> 清库。
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>
#include <ctype.h>
#include <stddef.h>

#include "pkg_manager.h"

#define TAR_BLOCK_SIZE     512
#define RPKG_MAX_PATH      384

/* 单个维护脚本/manifest 的内存暂存上限（超限视为恶意/损坏包） */
#define RPKG_SCRIPT_MAX    8192
/* manifest 校验条目上限 */
#define RPKG_MANIFEST_MAX  128
/* 安装失败回滚时记录的文件数上限 */
#define RPKG_ROLLBACK_MAX  128

/*==========================
 *  CRC32（载荷校验，轻量查表实现）
 *==========================*/

static uint32_t g_crc_table[256];
static bool g_crc_ready = false;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    if (!g_crc_ready)
        crc32_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = g_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/*==========================
 *  通用小工具
 *==========================*/

/*
 * WHAT : 循环读满 len 字节 / read exactly len bytes or fail
 * WHY  : read() 允许短读（FATFS 缓冲、信号打断），tar 流式解析
 *        一旦短读就会把后续块整体错位——必须读满
 * HOW  : 循环 read 直到读满；EOF 或错误返回 -EIO
 */
static ssize_t read_full(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = read(fd, p + got, len - got);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -EIO;
        }
        if (n == 0)
            return -EIO;            /* 意外 EOF（包被截断） */
        got += (size_t)n;
    }
    return (ssize_t)got;
}

/*
 * WHAT : 包名校验 / validate package name
 * WHY  : 包名直接拼进数据库文件名（<pkg>.control、info/<pkg>.preinst），
 *        含 '/' 或 ".." 的恶意包名会把文件写到数据库之外（路径穿越）
 * WHO  : rpkg_install（control 解析后）、rpkg_remove（用户输入）
 * WHERE: 本文件
 * WHEN : 2026-10-04 修订新增（安全修复）
 * HOW  : 仅允许 [A-Za-z0-9.+-]，长度 1..PKG_FIELD_MAX-1，首尾不为 '.'/'-'
 */
static bool pkg_name_is_valid(const char *name)
{
    size_t len = strlen(name);

    if (len == 0 || len >= PKG_FIELD_MAX)
        return false;
    if (name[0] == '.' || name[0] == '-' ||
        name[len - 1] == '.' || name[len - 1] == '-')
        return false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) &&
            c != '.' && c != '+' && c != '-')
            return false;
    }
    return true;
}

/*==========================
 *  数据库路径辅助
 *==========================*/

int rpkg_db_path(char *buf, int buflen, const char *fmt, ...)
{
    va_list ap;
    int n = snprintf(buf, buflen, "%s/", PKG_DB_ROOT);
    if (n <= 0 || n >= buflen)
        return -ENOSPC;

    va_start(ap, fmt);
    int m = vsnprintf(buf + n, buflen - n, fmt, ap);
    va_end(ap);
    return (m < 0 || n + m >= buflen) ? -ENOSPC : OK;
}

/*==========================
 *  control 解析（deb 字段子集）
 *==========================*/

static const char *g_field_names[PKG_FLD_COUNT] = {
    "Package", "Version", "Arch", "Depends", "License",
    "Description", "Installed-Size", "Maintainer",
};

/*
 * WHAT : 解析 control 单行字段 / parse one control field line
 * HOW  : "Field: value" 前缀匹配后去空白；剥掉 Windows 换行的 '\r'；
 *        未知字段返回 -ENOENT（调用方忽略）
 */
static int control_parse_line(struct rpkg_control_s *ctl, const char *line)
{
    for (int i = 0; i < PKG_FLD_COUNT; i++) {
        size_t len = strlen(g_field_names[i]);
        if (strncmp(line, g_field_names[i], len) == 0 && line[len] == ':') {
            const char *val = line + len + 1;
            while (*val == ' ' || *val == '\t')
                val++;

            char *dst = ctl->field[i];
            snprintf(dst, PKG_FIELD_MAX, "%s", val);
            /* 剥 '\r'（Windows 打包的 control） */
            size_t vlen = strlen(dst);
            while (vlen > 0 && (dst[vlen - 1] == '\r' ||
                                dst[vlen - 1] == '\n'))
                dst[--vlen] = '\0';
            return OK;
        }
    }
    return -ENOENT;  /* 未知字段忽略 */
}

static const char *control_get(const struct rpkg_control_s *ctl, int fld)
{
    return ctl->field[fld][0] ? ctl->field[fld] : NULL;
}

/*==========================
 *  已安装查询 / 依赖检查
 *==========================*/

int rpkg_is_installed(const char *pkg_name)
{
    char path[RPKG_MAX_PATH];
    struct stat st;

    if (rpkg_db_path(path, sizeof(path), "%s.control", pkg_name) != OK)
        return 0;
    return stat(path, &st) == 0 ? 1 : 0;
}

/*
 * WHAT : 逗号分隔的 Depends 字段逐项检查数据库（v1 仅查包名存在性）
 * WHY  : 安装前拦截缺失依赖（deb 语义的简化）
 * HOW  : 就地切分字符串，逐项 trim 后调 rpkg_is_installed
 */
static int check_depends(const char *depends)
{
    char buf[PKG_FIELD_MAX];
    char *save = NULL;

    snprintf(buf, sizeof(buf), "%s", depends ? depends : "");

    for (char *tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        while (*tok == ' ')
            tok++;

        char *paren = strchr(tok, '(');
        if (paren)
            *paren = '\0';  /* v1 忽略版本约束 >= x.y */

        size_t len = strlen(tok);
        while (len > 0 && tok[len - 1] == ' ')
            tok[--len] = '\0';

        if (len == 0)
            continue;

        if (!rpkg_is_installed(tok)) {
            printf("缺少依赖 / missing dependency: %s\n", tok);
            return -ENOENT;
        }
    }
    return OK;
}

/*==========================
 *  维护脚本执行
 *==========================*/

/*
 * WHAT : 执行包维护脚本（preinst/postinst/prerm/postrm，NSH 脚本）
 * HOW  : system("sh <path>")；脚本无则跳过（返回 OK）
 */
static int run_script(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return OK;  /* 无脚本，正常 */

    printf("运行 %s\n", path);

    char cmdbuf[RPKG_MAX_PATH + 8];
    snprintf(cmdbuf, sizeof(cmdbuf), "sh %s", path);
    return system(cmdbuf);
}

/*==========================
 *  USTAR 流式解析
 *==========================*/

struct tar_hdr_s {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
};

static size_t tar_parse_size(const char *oct)
{
    size_t v = 0;
    for (int i = 0; i < 11 && oct[i] && oct[i] != ' '; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;
        v = v * 8 + (oct[i] - '0');
    }
    return v;
}

/*
 * WHAT : 校验 tar 头部 checksum / validate ustar header checksum
 * WHY  : SD 传输/存储损坏的包若不校验头，会把错位数据当条目解析；
 *        ustar 规定 checksum = 头 512B 逐字节和（chksum 字段按空格计）
 * HOW  : chksum 字段位置按 8 个空格累加，其余按原字节；结果与
 *        chksum 字段的八进制值比对
 */
static bool tar_hdr_checksum_ok(const uint8_t *block)
{
    const struct tar_hdr_s *hdr = (const struct tar_hdr_s *)block;
    size_t off = offsetof(struct tar_hdr_s, chksum);
    uint32_t sum = 0;

    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (i >= (int)off && i < (int)off + 8)
            sum += ' ';
        else
            sum += block[i];
    }

    uint32_t expect = (uint32_t)tar_parse_size(hdr->chksum);
    return sum == expect;
}

/*
 * WHAT : 路径穿越防护 / path traversal guard
 * WHY  : 恶意 .rpk 的条目名若含 ".." 分量或绝对路径，落盘时会
 *        覆盖安装前缀之外的任意文件（如 /etc、/bin）
 * WHERE: 本文件，rpkg_install 第二遍对每个 data/ 条目调用
 * WHEN : 2026-10-04 修订增强（原实现漏掉裸 ".." 结尾分量）
 * HOW  : 拒绝绝对路径；逐分量检查，任一分量为 ".." 即拒绝
 */
static bool path_is_safe(const char *path)
{
    if (path == NULL || path[0] == '\0' || path[0] == '/')
        return false;

    const char *p = path;
    while (*p) {
        const char *seg = p;
        size_t seglen = 0;
        while (p[seglen] && p[seglen] != '/')
            seglen++;

        if (seglen == 2 && seg[0] == '.' && seg[1] == '.')
            return false;

        p += seglen;
        if (*p == '/')
            p++;
    }
    return true;
}

/* tar 迭代器状态 */
struct tar_iter_s {
    int       fd;
    uint8_t   block[TAR_BLOCK_SIZE];
    size_t    remain;   /* 当前条目尚未消费的数据字节数 */
    bool      eof;
};

static int tar_next(struct tar_iter_s *it, char *name, size_t name_len,
                    size_t *size, char *type)
{
    /* 先消费完上一条目的填充块 */
    while (it->remain > 0) {
        if (read_full(it->fd, it->block, TAR_BLOCK_SIZE) < 0)
            return -EIO;
        it->remain -= it->remain > TAR_BLOCK_SIZE ?
                      TAR_BLOCK_SIZE : it->remain;
    }

    if (it->eof)
        return 1;  /* 迭代结束 */

    if (read_full(it->fd, it->block, TAR_BLOCK_SIZE) < 0)
        return -EIO;

    /* 结束标志：全零块（简化处理，单个即止） */
    bool zero = true;
    for (int i = 0; i < TAR_BLOCK_SIZE; i++)
        if (it->block[i]) { zero = false; break; }
    if (zero) {
        it->eof = true;
        return 1;
    }

    if (!tar_hdr_checksum_ok(it->block)) {
        printf("损坏的包头 / corrupt tar header\n");
        return -EIO;
    }

    struct tar_hdr_s *hdr = (struct tar_hdr_s *)it->block;
    size_t fsize = tar_parse_size(hdr->size);

    name[0] = '\0';
    if (hdr->prefix[0]) {
        snprintf(name, name_len, "%.155s/%.100s",
                 hdr->prefix, hdr->name);
    } else {
        snprintf(name, name_len, "%.100s", hdr->name);
    }

    *size = fsize;
    *type = hdr->typeflag;
    it->remain = (fsize + TAR_BLOCK_SIZE - 1) & ~(size_t)(TAR_BLOCK_SIZE - 1);
    return 0;
}

/*
 * WHAT : 读出当前条目的全部数据（小文件：control/manifest/脚本）
 * HOW  : 循环读满 fsize 后消费尾部填充块；返回 -ENOSPC 表示
 *        条目大于缓冲区（调用方决定视为损坏/恶意包）
 */
static int tar_read_data(struct tar_iter_s *it, char *buf, size_t buflen,
                         size_t fsize)
{
    if (fsize >= buflen)
        return -ENOSPC;

    if (read_full(it->fd, buf, fsize) < 0)
        return -EIO;
    buf[fsize] = '\0';

    size_t pad = (TAR_BLOCK_SIZE - (fsize % TAR_BLOCK_SIZE)) %
                 TAR_BLOCK_SIZE;
    if (pad && read_full(it->fd, it->block, pad) < 0)
        return -EIO;
    it->remain = 0;
    return OK;
}

/* 流式落盘一个文件（大载荷：ELF 等），返回 CRC32 */
static int tar_extract_file(struct tar_iter_s *it, const char *dest,
                            size_t fsize, uint32_t *crc_out)
{
    int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0)
        return -errno;

    uint32_t crc = 0;
    size_t done = 0;

    while (done < fsize) {
        size_t want = fsize - done;
        if (want > TAR_BLOCK_SIZE)
            want = TAR_BLOCK_SIZE;
        if (read_full(it->fd, it->block, want) < 0) {
            close(fd);
            return -EIO;
        }
        crc = crc32_update(crc, it->block, want);
        if (write(fd, it->block, want) != (ssize_t)want) {
            close(fd);
            return -EIO;
        }
        done += want;
    }
    close(fd);

    /* 消费尾部填充并清零迭代器余量（防止 tar_next 重复跳块） */
    size_t pad = (TAR_BLOCK_SIZE - (fsize % TAR_BLOCK_SIZE)) %
                 TAR_BLOCK_SIZE;
    if (pad && read_full(it->fd, it->block, pad) < 0)
        return -EIO;
    it->remain = 0;

    *crc_out = crc;
    return OK;
}

/*
 * WHAT : 包 Arch 字段与目标架构匹配（all / 架构族 / 芯片名 三级规则）
 * WHY  : ESP32-S3/ESP32 为 Xtensa、ESP32-C3 为 RISC-V，二进制互不通用，
 *        必须在安装前拦截跨架构包（如 riscv 包装到 xtensa 目标）
 * WHO  : rpkg_install 在 Arch 检查处调用
 * WHERE: src/nuttx/common/apps/system/pkg_manager.c
 * WHEN : 2026-10-04 新增（随 esp32c3 第三目标引入）
 * HOW  : CONFIG_RETRO_ARCH 形如 "xtensa-esp32s3" / "riscv-esp32c3"；
 *        control 的 Arch 允许: all / xtensa / riscv / esp32 / esp32s3 /
 *        esp32c3（族匹配前缀、芯片匹配后缀、all 恒过）
 */
static bool arch_match(const char *pkg_arch)
{
    if (!pkg_arch || !pkg_arch[0] || strcmp(pkg_arch, "all") == 0)
        return true;

#ifdef CONFIG_RETRO_ARCH
    {
        const char *me = CONFIG_RETRO_ARCH;
        const char *dash = strchr(me, '-');

        if (strcmp(pkg_arch, me) == 0)
            return true;                 /* 全名匹配 */

        if (dash) {
            size_t family_len = (size_t)(dash - me);

            if (strlen(pkg_arch) == family_len &&
                strncmp(pkg_arch, me, family_len) == 0)
                return true;             /* 架构族匹配：xtensa / riscv */

            if (strcmp(pkg_arch, dash + 1) == 0)
                return true;             /* 芯片名匹配：esp32c3 等 */
        }
    }
#else
    (void)pkg_arch;                      /* 未定义架构标识时放行（开发期） */
#endif
    return false;
}

/*==========================
 *  manifest（打包器 CRC 清单）辅助
 *==========================*/

/* 打包器 manifest 的内存镜像："<crc8hex> /sdcard/<rel>" 逐行 */
struct manifest_ent_s {
    uint32_t crc;
    char     rel[RPKG_MAX_PATH];   /* 相对安装前缀的路径 */
};

/*
 * WHAT : 解析打包器 manifest 到条目数组
 * WHY  : 安装落盘时逐文件比对 CRC，损坏/篡改的包必须被拒绝
 * HOW  : 逐行 sscanf "%x %s"；路径须以安装前缀开头（剥离之），
 *        且剥离后必须通过 path_is_safe——否则该行直接丢弃
 *        （防止恶意 manifest 借卸载之名删除任意文件）
 * 返回 : 解析出的有效条目数（0 = 无可校验清单，不视为错误）
 */
static int manifest_parse(struct manifest_ent_s *ents, int max_ents,
                          const char *buf)
{
    int n = 0;
    const char *p = buf;

    while (p && *p && n < max_ents) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);

        char line[RPKG_MAX_PATH + 32];
        if (linelen >= sizeof(line))
            linelen = sizeof(line) - 1;
        memcpy(line, p, linelen);
        line[linelen] = '\0';

        unsigned crcval = 0;
        char path[RPKG_MAX_PATH];
        if (sscanf(line, "%x %s", &crcval, path) == 2) {
            size_t plen = strlen(PKG_INSTALL_PREFIX);
            const char *rel = path;

            /* 只接受安装前缀之下的绝对路径 */
            if (strncmp(path, PKG_INSTALL_PREFIX, plen) == 0 &&
                path[plen] == '/')
                rel = path + plen + 1;

            if (path_is_safe(rel)) {
                ents[n].crc = (uint32_t)crcval;
                snprintf(ents[n].rel, sizeof(ents[n].rel), "%s", rel);
                n++;
            }
        }

        p = eol ? eol + 1 : NULL;
    }
    return n;
}

static const struct manifest_ent_s *manifest_find(
    const struct manifest_ent_s *ents, int count, const char *rel)
{
    for (int i = 0; i < count; i++)
        if (strcmp(ents[i].rel, rel) == 0)
            return &ents[i];
    return NULL;
}

/*==========================
 *  安装 / 卸载 / 列表 / 查询
 *==========================*/

static void mkdirs(const char *path)
{
    char tmp[RPKG_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

/* 维护脚本内存暂存（第一遍读入，检查通过后落库） */
struct script_ent_s {
    const char *name;   /* 指向静态字面量：preinst/postinst/prerm/postrm */
    char *data;         /* malloc，NUL 结尾 */
    size_t len;
};

static void scripts_free(struct script_ent_s *scripts, int count)
{
    for (int i = 0; i < count; i++)
        free(scripts[i].data);
}

/*
 * WHAT : 把暂存的维护脚本写入数据库 info/<包名>.<脚本名>
 * WHY  : 脚本随包隔离存档——多包共存时互不覆盖，卸载时各取各的
 * WHERE: rpkg_install 检查全部通过之后调用
 * WHEN : 2026-10-04 修订（原先所有包共享 info/<脚本名>，互相覆盖）
 */
static int scripts_store(const struct script_ent_s *scripts, int count,
                         const char *pkg)
{
    mkdirs(PKG_DB_ROOT "/info");

    for (int i = 0; i < count; i++) {
        char path[RPKG_MAX_PATH];
        if (rpkg_db_path(path, sizeof(path), "info/%s.%s",
                         pkg, scripts[i].name) != OK)
            return -ENOSPC;

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd < 0)
            return -errno;
        ssize_t w = write(fd, scripts[i].data, scripts[i].len);
        close(fd);
        if (w != (ssize_t)scripts[i].len)
            return -EIO;
    }
    return OK;
}

/*
 * WHAT : 安装失败回滚 / rollback partially installed files
 * WHY  : CRC 不符/写盘失败时不能留半截文件冒充已安装
 * HOW  : 逆序 unlink 本次已落盘的文件（目录留着，无害）
 */
static void rollback_files(char (*paths)[RPKG_MAX_PATH], int count)
{
    for (int i = count - 1; i >= 0; i--)
        unlink(paths[i]);
}

/*
 * WHAT : rpkg_install - 安装 .rpk 包
 * HOW  : 第一遍 tar 扫描收 control/manifest/脚本（内存暂存），
 *        通过架构/依赖/重装检查后才落库脚本并进入第二遍
 *        data/ 载荷落盘（实测 CRC 与打包器 manifest 比对，不符回滚）
 */
int rpkg_install(const char *rpk_path)
{
    struct rpkg_control_s ctl;
    char name[RPKG_MAX_PATH];
    char dest[RPKG_MAX_PATH];
    char script_path[RPKG_MAX_PATH];
    char type;
    size_t fsize;
    int ret;

    memset(&ctl, 0, sizeof(ctl));

    int fd = open(rpk_path, O_RDONLY);
    if (fd < 0) {
        printf("无法打开包 / cannot open: %s\n", rpk_path);
        return -ENOENT;
    }

    struct tar_iter_s it = { .fd = fd, .remain = 0, .eof = false };

    /* 大缓冲一律堆分配（NSH 命令栈有限，勿放大数组） */
    char *manifest_buf = calloc(1, RPKG_SCRIPT_MAX);
    char *manifest_out = calloc(1, RPKG_SCRIPT_MAX);
    struct manifest_ent_s *ments = calloc(RPKG_MANIFEST_MAX,
                                          sizeof(*ments));
    char (*rollback)[RPKG_MAX_PATH] = calloc(RPKG_ROLLBACK_MAX,
                                             sizeof(*rollback));
    struct script_ent_s scripts[4];
    int nscripts = 0;

    if (!manifest_buf || !manifest_out || !ments || !rollback) {
        ret = -ENOMEM;
        goto out_free;
    }

    bool have_control = false;

    /* --- 第一遍：元数据与脚本（暂存内存，检查通过前不落盘）--- */
    while ((ret = tar_next(&it, name, sizeof(name), &fsize, &type)) == 0) {
        if (type == '5')
            continue;

        if (strcmp(name, PKG_PATH_CONTROL) == 0) {
            char *buf = malloc(2048);
            if (!buf) {
                ret = -ENOMEM;
                goto out_free;
            }
            ret = tar_read_data(&it, buf, 2048, fsize);
            if (ret == -ENOSPC) {
                printf("control 过大 / control too large\n");
                free(buf);
                goto out_free;
            }
            if (ret < 0) {
                free(buf);
                goto out_free;
            }

            for (char *line = strtok(buf, "\n"); line;
                 line = strtok(NULL, "\n"))
                control_parse_line(&ctl, line);
            free(buf);
            have_control = true;
        }
        else if (strcmp(name, PKG_PATH_MANIFEST) == 0) {
            ret = tar_read_data(&it, manifest_buf, RPKG_SCRIPT_MAX, fsize);
            if (ret == -ENOSPC) {
                printf("manifest 过大 / manifest too large\n");
                goto out_free;
            }
            if (ret < 0)
                goto out_free;
        }
        else {
            static const char *const snames[4] = {
                "preinst", "postinst", "prerm", "postrm"
            };
            int si;
            for (si = 0; si < 4; si++)
                if (strcmp(name, snames[si]) == 0)
                    break;
            if (si < 4) {
                if (nscripts >= 4) {
                    ret = -EINVAL;
                    goto out_free;
                }
                char *buf = malloc(RPKG_SCRIPT_MAX);
                if (!buf) {
                    ret = -ENOMEM;
                    goto out_free;
                }
                ret = tar_read_data(&it, buf, RPKG_SCRIPT_MAX, fsize);
                if (ret == -ENOSPC) {
                    printf("脚本过大 / script too large: %s\n", name);
                    free(buf);
                    goto out_free;
                }
                if (ret < 0) {
                    free(buf);
                    goto out_free;
                }
                scripts[nscripts].name = snames[si];
                scripts[nscripts].data = buf;
                scripts[nscripts].len = fsize;
                nscripts++;
            }
        }
        /* data/ 载荷留到第二遍（seek 回起点重扫） */
    }
    if (ret < 0)
        goto out_free;

    if (!have_control || !control_get(&ctl, PKG_FLD_PACKAGE)) {
        printf("包缺少 control/Package 字段\n");
        ret = -EINVAL;
        goto out_free;
    }

    const char *pkg = control_get(&ctl, PKG_FLD_PACKAGE);
    const char *arch = control_get(&ctl, PKG_FLD_ARCH);

    if (!pkg_name_is_valid(pkg)) {
        printf("非法包名 / invalid package name: %s\n", pkg);
        ret = -EINVAL;
        goto out_free;
    }

    if (!arch_match(arch)) {
        printf("架构不符 / arch mismatch: 包=%s 本机=%s\n",
               arch ? arch : "(none)",
#ifdef CONFIG_RETRO_ARCH
               CONFIG_RETRO_ARCH
#else
               "unknown"
#endif
               );
        ret = -EINVAL;
        goto out_free;
    }

    if (rpkg_is_installed(pkg)) {
        printf("已安装 / already installed: %s（先 pkg remove）\n", pkg);
        ret = -EEXIST;
        goto out_free;
    }

    const char *deps = control_get(&ctl, PKG_FLD_DEPENDS);
    if (deps) {
        ret = check_depends(deps);
        if (ret < 0)
            goto out_free;
    }

    int nments = manifest_parse(ments, RPKG_MANIFEST_MAX, manifest_buf);

    /* 检查全部通过：脚本落库，执行 preinst */
    ret = scripts_store(scripts, nscripts, pkg);
    if (ret < 0) {
        printf("脚本入库失败 / cannot store scripts\n");
        goto out_free;
    }

    rpkg_db_path(script_path, sizeof(script_path), "info/%s.preinst", pkg);
    if (run_script(script_path) != OK) {
        printf("preinst 失败，中止安装\n");
        /* 清掉刚入库的脚本，保持数据库干净 */
        const char *snames[] = { "preinst", "postinst", "prerm", "postrm" };
        for (int i = 0; i < 4; i++) {
            rpkg_db_path(script_path, sizeof(script_path), "info/%s.%s",
                         pkg, snames[i]);
            unlink(script_path);
        }
        ret = -EIO;
        goto out_free;
    }

    /* --- 第二遍：data/ 载荷落盘（回到文件起点重扫）--- */
    lseek(fd, 0, SEEK_SET);
    it.remain = 0;
    it.eof = false;

    size_t mlen = 0;
    int nrollback = 0;
    int nchecked = 0;

    while ((ret = tar_next(&it, name, sizeof(name), &fsize, &type)) == 0) {
        if (strncmp(name, PKG_PATH_DATA "/", 5) != 0)
            continue;

        const char *rel = name + 5;  /* data/ 之后 */
        if (rel[0] == '\0')
            continue;                /* "data/" 目录条目本身 */

        if (!path_is_safe(rel)) {
            printf("拒绝不安全路径 / unsafe path: %s\n", rel);
            ret = -EINVAL;
            goto out_rollback;
        }

        /* 长度前置检查：杜绝 snprintf 静默截断出半截路径 */
        if (strlen(rel) + strlen(PKG_INSTALL_PREFIX) + 1 >= sizeof(dest)) {
            printf("路径过长 / path too long: %s\n", rel);
            ret = -ENAMETOOLONG;
            goto out_rollback;
        }
        snprintf(dest, sizeof(dest), "%s/%s", PKG_INSTALL_PREFIX, rel);

        if (type == '5' || rel[strlen(rel) - 1] == '/') {
            mkdirs(dest);
            continue;
        }

        /* 建父目录 */
        snprintf(script_path, sizeof(script_path), "%s", dest);
        char *slash = strrchr(script_path, '/');
        if (slash) {
            *slash = '\0';
            mkdirs(script_path);
        }

        uint32_t crc;
        ret = tar_extract_file(&it, dest, fsize, &crc);
        if (ret < 0) {
            printf("写入失败 / extract failed: %s\n", dest);
            goto out_rollback;
        }

        /* CRC 与打包器 manifest 比对（有清单才比对） */
        const struct manifest_ent_s *me = manifest_find(ments, nments, rel);
        if (me) {
            if (me->crc != crc) {
                printf("校验失败 / CRC mismatch: %s "
                       "(包=%08lx 实测=%08lx)\n", dest,
                       (unsigned long)me->crc, (unsigned long)crc);
                unlink(dest);
                ret = -EILSEQ;
                goto out_rollback;
            }
            nchecked++;
        }

        if (nrollback < RPKG_ROLLBACK_MAX) {
            snprintf(rollback[nrollback], RPKG_MAX_PATH, "%s", dest);
            nrollback++;
        }

        int n = snprintf(manifest_out + mlen, RPKG_SCRIPT_MAX - mlen,
                         "%08lx %s\n", (unsigned long)crc, dest);
        if (n < 0 || (size_t)n >= RPKG_SCRIPT_MAX - mlen) {
            printf("manifest 溢出 / manifest overflow\n");
            ret = -ENOSPC;
            goto out_rollback;
        }
        mlen += (size_t)n;
        printf("  安装 %s (%u bytes)\n", dest, (unsigned)fsize);
    }
    if (ret < 0)
        goto out_rollback;

    if (nments > 0 && nchecked != nments) {
        printf("manifest 与载荷不一致 / manifest mismatch: "
               "清单 %d 项，校验 %d 项\n", nments, nchecked);
        ret = -EILSEQ;
        goto out_rollback;
    }

    /* --- 写数据库（control 快照 + manifest）--- */
    mkdirs(PKG_DB_ROOT "/manifest");

    rpkg_db_path(dest, sizeof(dest), "%s.control", pkg);
    int dfd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dfd >= 0) {
        for (int i = 0; i < PKG_FLD_COUNT; i++) {
            if (ctl.field[i][0])
                dprintf(dfd, "%s: %s\n", g_field_names[i], ctl.field[i]);
        }
        close(dfd);
    }

    rpkg_db_path(dest, sizeof(dest), "manifest/%s", pkg);
    dfd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dfd >= 0) {
        /* 打包器清单含 CRC 视为权威（已逐项验证过）；否则用实测清单 */
        const char *m = (nments > 0) ? manifest_buf : manifest_out;
        if (write(dfd, m, strlen(m)) < 0)
            syslog(LOG_WARNING, "[rpkg] manifest write failed\n");
        close(dfd);
    }

    close(fd);
    fd = -1;

    /* postinst */
    rpkg_db_path(script_path, sizeof(script_path), "info/%s.postinst", pkg);
    run_script(script_path);

    printf("安装完成 / installed: %s %s\n", pkg,
           control_get(&ctl, PKG_FLD_VERSION) ?: "");
    syslog(LOG_INFO, "[rpkg] installed %s %s\n", pkg,
           control_get(&ctl, PKG_FLD_VERSION) ?: "");

    scripts_free(scripts, nscripts);
    free(manifest_buf);
    free(manifest_out);
    free(ments);
    free(rollback);
    return OK;

out_rollback:
    rollback_files(rollback, nrollback);

out_free:
    if (fd >= 0)
        close(fd);
    scripts_free(scripts, nscripts);
    free(manifest_buf);
    free(manifest_out);
    free(ments);
    free(rollback);
    return ret;
}

/*
 * WHAT : rpkg_remove - 卸载包（按 manifest 删文件，执行 prerm/postrm）
 */
int rpkg_remove(const char *pkg_name)
{
    char path[RPKG_MAX_PATH];
    char line[RPKG_MAX_PATH + 32];

    if (!pkg_name_is_valid(pkg_name)) {
        printf("非法包名 / invalid package name: %s\n", pkg_name);
        return -EINVAL;
    }

    if (!rpkg_is_installed(pkg_name)) {
        printf("未安装 / not installed: %s\n", pkg_name);
        return -ENOENT;
    }

    /* prerm */
    rpkg_db_path(path, sizeof(path), "info/%s.prerm", pkg_name);
    if (run_script(path) != OK) {
        printf("prerm 失败，中止卸载\n");
        return -EIO;
    }

    /* 按 manifest 删除（行格式 "<crc> /sdcard/..."） */
    rpkg_db_path(path, sizeof(path), "manifest/%s", pkg_name);
    FILE *mf = fopen(path, "r");
    if (mf) {
        while (fgets(line, sizeof(line), mf)) {
            char *p = strchr(line, ' ');
            if (!p)
                continue;
            p++;
            char *nl = strchr(p, '\n');
            if (nl)
                *nl = '\0';

            /* 只删安装前缀之下的路径（防篡改 manifest 任意删除） */
            if (strncmp(p, PKG_INSTALL_PREFIX "/",
                        strlen(PKG_INSTALL_PREFIX) + 1) != 0)
                continue;

            if (unlink(p) == 0)
                printf("  移除 %s\n", p);
        }
        fclose(mf);
    }

    /* postrm（先执行再清理——脚本本体还没被删） */
    rpkg_db_path(path, sizeof(path), "info/%s.postrm", pkg_name);
    run_script(path);

    /* 清理数据库与脚本（按包名隔离存档） */
    rpkg_db_path(path, sizeof(path), "manifest/%s", pkg_name);
    unlink(path);
    rpkg_db_path(path, sizeof(path), "%s.control", pkg_name);
    unlink(path);
    const char *scripts[] = { "preinst", "postinst", "prerm", "postrm" };
    for (int i = 0; i < 4; i++) {
        rpkg_db_path(path, sizeof(path), "info/%s.%s", pkg_name,
                     scripts[i]);
        unlink(path);
    }

    printf("卸载完成 / removed: %s\n", pkg_name);
    syslog(LOG_INFO, "[rpkg] removed %s\n", pkg_name);
    return OK;
}

/*
 * WHAT : rpkg_list - 列出已安装包（读数据库 *.control 首行）
 */
int rpkg_list(void)
{
    char path[RPKG_MAX_PATH];
    char line[PKG_FIELD_MAX];

    mkdirs(PKG_DB_ROOT);

    DIR *dir = opendir(PKG_DB_ROOT);
    if (!dir) {
        printf("包数据库为空（%s）\n", PKG_DB_ROOT);
        return -ENOENT;
    }

    printf("%-16s %-12s %s\n", "Package", "Version", "Description");
    printf("%-16s %-12s %s\n", "-------", "-------", "-----------");

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 8 || strcmp(ent->d_name + len - 8, ".control") != 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", PKG_DB_ROOT, ent->d_name);

        char pkg[PKG_FIELD_MAX] = "", ver[PKG_FIELD_MAX] = "",
             desc[PKG_FIELD_MAX] = "";

        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Package:", 8) == 0)
                sscanf(line + 8, "%255s", pkg);
            else if (strncmp(line, "Version:", 8) == 0)
                sscanf(line + 8, "%255s", ver);
            else if (strncmp(line, "Description:", 12) == 0) {
                snprintf(desc, sizeof(desc), "%s", line + 12);
                char *d = desc;
                while (*d == ' ')
                    d++;
                char *nl = strchr(d, '\n');
                if (nl) *nl = '\0';
            }
        }
        fclose(f);

        printf("%-16s %-12s %s\n", pkg, ver, desc);
        count++;
    }
    closedir(dir);

    if (count == 0)
        printf("  (无已安装包，安装: pkg install /sdcard/pkg/*.rpk)\n");

    return OK;
}

/*
 * WHAT : rpkg_info - 显示一个已安装包的完整 control 信息
 */
int rpkg_info(const char *pkg_name)
{
    char path[RPKG_MAX_PATH];
    char line[PKG_FIELD_MAX + 32];

    if (!pkg_name_is_valid(pkg_name)) {
        printf("非法包名 / invalid package name: %s\n", pkg_name);
        return -EINVAL;
    }

    if (!rpkg_is_installed(pkg_name)) {
        printf("未安装 / not installed: %s\n", pkg_name);
        return -ENOENT;
    }

    rpkg_db_path(path, sizeof(path), "%s.control", pkg_name);

    FILE *f = fopen(path, "r");
    if (!f)
        return -ENOENT;

    printf("=== %s ===\n", pkg_name);
    while (fgets(line, sizeof(line), f))
        printf("  %s", line);
    fclose(f);

    return OK;
}
