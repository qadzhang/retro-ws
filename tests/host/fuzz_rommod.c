/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * fuzz_rommod.c - ROM XIP 模块加载器模糊测试（ai-code-testing Layer 4）
 *
 * WHAT : 对 rommod_load_from_mem 喂畸形/随机变异输入，断言"永不崩
 *        溃、只返回错误码"（ASAN/UBSAN 守护）
 * WHY  : 装载器解析不可信二进制（SD 侧 .rmo 或损坏 ROM）；任何越界
 *        读/写都是任意代码执行面——模糊是蜕变/差分覆盖不到的崩溃类
 * WHO  : tests/host/run_all.sh（fuzz 步骤，PR 短跑 3 秒）
 * WHERE: retro-ws/tests/host/fuzz_rommod.c
 * WHEN : 2026-10-06 新增
 * HOW  : 种子语料=合法 ET_DYN 模块（gcc 现场编译）+ 结构化变异
 *        （截断/头部位翻转/phoff·shoff 指野/随机字节翻）×N 轮，
 *        全部返回值仅可为错误码或成功（成功者立即卸载保持守恒）
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rommod.h"

#define FIXDIR "/tmp/retro_test/rommod"
#define ITERS 20000

static uint32_t xrng(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

int main(void)
{
    FILE *f = fopen(FIXDIR "/mod_cli.so", "rb");
    uint8_t *seed;
    size_t slen;
    uint32_t rng = 0xC0FFEE;
    int crashes = 0;
    int accepted = 0;

    if (!f)
    {
        printf("[fuzz_rommod] 种子缺失（先跑 test_rommod 生成 fixture）\n");
        return 2;
    }
    fseek(f, 0, SEEK_END);
    slen = ftell(f);
    fseek(f, 0, SEEK_SET);
    seed = malloc(slen);
    if (fread(seed, 1, slen, f) != slen)
        return 2;
    fclose(f);

    uint8_t *buf = malloc(slen + 64);

    for (int i = 0; i < ITERS; i++)
    {
        struct rommod_s *mod = NULL;
        size_t len = slen;
        int ret;

        memcpy(buf, seed, slen);
        xrng(&rng);

        switch (i & 7)
        {
        case 0:                         /* 随机截断 */
            len = xrng(&rng) % (slen + 1);
            break;
        case 1:                         /* 头部区随机位翻转 */
            for (int k = 0; k < 4; k++)
                buf[xrng(&rng) % 64] ^= 1u << (xrng(&rng) & 7);
            break;
        case 2:                         /* e_phoff 指野 */
            memset(buf + 28, 0xFF, 4);
            buf[32] = xrng(&rng) & 0x7F;
            break;
        case 3:                         /* e_shoff 指野 */
            memset(buf + 32, 0xFF, 4);
            buf[36] = xrng(&rng) & 0x7F;
            break;
        case 4:                         /* 任意两处字节翻转 */
            buf[xrng(&rng) % slen] ^= 0xFF;
            buf[xrng(&rng) % slen] ^= 0x80;
            break;
        case 5:                         /* e_machine/e_type 打野 */
            buf[18] = (uint8_t)xrng(&rng);
            buf[16] = (uint8_t)xrng(&rng);
            break;
        case 6:                         /* 全随机短块 */
            len = 32 + xrng(&rng) % 512;
            for (size_t k = 0; k < len; k++)
                buf[k] = (uint8_t)xrng(&rng);
            break;
        default:                        /* 完整随机长块 */
            len = 128 + xrng(&rng) % 2048;
            for (size_t k = 0; k < len; k++)
                buf[k] = (uint8_t)xrng(&rng);
            break;
        }

        ret = rommod_load_from_mem("fuzz", buf, len, &mod);
        if (ret == 0)
        {
            /* 结构意外成立（如仅头部外围翻转）：卸载保持守恒 */
            accepted++;
            rommod_put(mod);
        }
    }

    printf("[fuzz_rommod] %d 轮完成：0 崩溃，%d 次结构成立（已卸载）\n",
           ITERS, accepted);
    free(seed);
    free(buf);
    return crashes;
}
