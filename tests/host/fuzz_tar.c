/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : tar 解析器模糊测试器 / structure-aware fuzz harness
 * WHY  : 蜕变/差分/PBT 验合法输入，fuzz 验畸形输入——SD 损坏包、
 *        恶意构造包都不能让安装器崩溃或死循环
 * WHO  : tests/host/run_all.sh（短跑）/ 夜间长跑（加 --long）
 * WHERE: esp32-retro-ws/tests/host/fuzz_tar.c
 * WHEN : 2026-10-04 新增
 * HOW  : 三路轰炸（确定性 xorshift PRNG，崩溃可复现 seed）：
 *   1) 纯随机字节流
 *   2) 合法 ustar 种子的随机位翻转/字节注入/截断（结构感知）
 *   3) 梯度畸形：超长名字/前缀、巨型 size 字段、非法 typeflag
 * 不变量：不崩溃（ASan 下即内存安全）、不死循环（条目数上限）、
 *        任何返回值 ∈ {0,1,-EIO}
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef PKG_MGR_SRC
#define PKG_MGR_SRC "pkg_manager.c"
#endif
#include PKG_MGR_SRC

static uint64_t rng_state = 0x243F6A8885A308D3ull;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 32);
}

static uint8_t seed_arc[64 * 1024];
static size_t seed_len;

static void build_seed(void)
{
    uint8_t *p = seed_arc;
    size_t off = 0;

    const char *files[] = { "control", "manifest", "preinst",
                            "data/bin/tool", "data/etc.conf" };
    const char bodies[][64] = {
        "Package: fuzz\nVersion: 1\nArch: all\n",
        "00000000 /sdcard/x\n",
        "echo hi\n",
        "\x7f" "ELF-fuzz-body",
        "k=v\n",
    };

    for (int i = 0; i < 5; i++) {
        size_t dlen = strlen(bodies[i]);
        struct tar_hdr_s *h = (struct tar_hdr_s *)(p + off);
        memset(h, 0, 512);
        snprintf(h->name, sizeof(h->name), "%s", files[i]);
        snprintf(h->mode, sizeof(h->mode), "%07o", 0777);
        snprintf(h->size, sizeof(h->size), "%011o", (unsigned)dlen);
        snprintf(h->mtime, sizeof(h->mtime), "%011o", 12345);
        h->typeflag = '0';
        memcpy(h->magic, "ustar", 6);
        h->version[0] = '0';
        h->version[1] = '0';

        uint32_t sum = 0;
        for (int k = 0; k < 512; k++)
            sum += (k >= 148 && k < 156) ? ' ' : p[off + k];
        snprintf(h->chksum, sizeof(h->chksum), "%06o", sum);
        h->chksum[7] = ' ';

        off += 512;
        memcpy(p + off, bodies[i], dlen);
        off += (dlen + 511) & ~(size_t)511;
    }
    memset(p + off, 0, 1024);
    seed_len = off + 1024;
}

/* 把 buf 当包跑一遍迭代器 + 数据读取 */
static int run_parser(const uint8_t *buf, size_t len)
{
    char tmp[] = "/tmp/fuzz_tar_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0)
        return 0;
    unlink(tmp);

    if (write(fd, buf, len) != (ssize_t)len) {
        close(fd);
        return 0;
    }
    lseek(fd, 0, SEEK_SET);

    struct tar_iter_s it = { .fd = fd, .remain = 0, .eof = false };
    char name[RPKG_MAX_PATH];
    static char databuf[2048];
    char type;
    size_t size;
    int rc;
    int entries = 0;

    while ((rc = tar_next(&it, name, sizeof(name), &size, &type)) == 0) {
        if (++entries > 4096) {
            close(fd);
            return -1;              /* 死循环保护 */
        }
        if (rc == 0 && size < sizeof(databuf))
            tar_read_data(&it, databuf, sizeof(databuf), size);

        if (rc != 0 && rc != 1 && rc != -EIO)
            return -2;              /* 返回值域不变量 */
    }
    if (rc != 1 && rc != -EIO)
        return -2;

    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    long iters = (argc > 1 && strcmp(argv[1], "--long") == 0) ?
                 2000000 : 200000;

    build_seed();

    static uint8_t mut[128 * 1024];

    for (long i = 0; i < iters; i++) {
        int mode = i % 3;

        if (mode == 0) {
            /* 纯随机字节（长度也随机，偏向小包） */
            size_t len = rnd() % 4096;
            for (size_t k = 0; k < len; k++)
                mut[k] = (uint8_t)rnd();
            if (run_parser(mut, len) != 0) {
                fprintf(stderr, "FUZZ FAIL mode0 seed=%ld iter=%ld\n",
                        (long)0x243F6A88, i);
                return 1;
            }
        } else if (mode == 1) {
            /* 合法种子 + 随机破坏（位翻转/截断/注入） */
            size_t len = seed_len;
            memcpy(mut, seed_arc, len);
            int hits = 1 + (int)(rnd() % 8);
            for (int h = 0; h < hits; h++) {
                int op = (int)(rnd() % 3);
                if (op == 0) {
                    size_t pos = rnd() % len;
                    mut[pos] ^= (uint8_t)(1u << (rnd() % 8));
                } else if (op == 1 && len > 1024) {
                    len -= rnd() % 1024;      /* 截断 */
                } else {
                    size_t pos = rnd() % len;
                    if (len < sizeof(mut) - 4) {
                        memmove(mut + pos + 4, mut + pos, len - pos);
                        memset(mut + pos, rnd(), 4);  /* 注入 */
                        len += 4;
                    }
                }
            }
            if (run_parser(mut, len) != 0) {
                fprintf(stderr, "FUZZ FAIL mode1 iter=%ld\n", i);
                return 1;
            }
        } else {
            /* 梯度畸形头：随机 name/prefix/size/typeflag 填充 */
            size_t len = 512 + 1024;
            memset(mut, 0, len);
            struct tar_hdr_s *h = (struct tar_hdr_s *)mut;
            for (int k = 0; k < 100; k++)
                h->name[k] = (rnd() % 32) ? (char)('a' + rnd() % 26) : '.';
            for (int k = 0; k < 11; k++)
                h->size[k] = (char)('0' + rnd() % 10);
            h->typeflag = (char)(rnd() % 256);
            memcpy(h->magic, "ustar", 6);
            snprintf(h->chksum, sizeof(h->chksum), "%06o",
                     (unsigned)(rnd() & 0xFFF));
            if (run_parser(mut, len) != 0) {
                fprintf(stderr, "FUZZ FAIL mode2 iter=%ld\n", i);
                return 1;
            }
        }
    }

    printf("[fuzz_tar] %ld iterations, 0 crashes -> PASS\n", iters);
    return 0;
}
