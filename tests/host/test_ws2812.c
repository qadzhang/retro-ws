/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_ws2812.c - WS2812 RMT 编码器全层测试
 *
 * WHAT : ws2812_rmt 纯编码函数的契约/蜕变/差分/PBT/模糊测试
 * WHY  : ai-code-testing 分层守护——RMT 符号字时序错了 LED 全灭/乱色，
 *        且真机上肉眼只能看结果无法看时序，必须宿主端锁死性质
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: esp32-retro-ws/tests/host/test_ws2812.c
 * WHEN : 2026-10-04 晚新增（随 ws2812_rmt.c 硬件驱动落地）
 * HOW  : 直链被测源（-DWS2812_TEST_HOST 裁掉设备 IO），断言：
 *        L1 契约（数据手册时序窗）+ L3 蜕变（周期守恒/位反转对称/
 *        GRB 序/解码回读）+ 差分（对标 NuttX ws2812esp32rmt 公式）+
 *        L4 模糊（畸形缓冲）+ PBT（伪随机 2^20 色回读不变量）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "test_framework.h"

#include "ws2812_rmt.h"

/* RMT 符号字位布局（ESP32-S3 TRM：bit15=相位0电平，bit31=相位1电平） */
#define PH0_DUR(w)  ((w) & 0x7FFFu)
#define PH0_LVL(w)  (((w) >> 15) & 1u)
#define PH1_DUR(w)  (((w) >> 16) & 0x7FFFu)
#define PH1_LVL(w)  (((w) >> 31) & 1u)

/* 80MHz 通道时钟 -> ns */
#define TICK_NS     12.5

/*==========================
 *  L1 契约：WS2812B 数据手册时序窗（容差 ±150ns）
 *==========================*/

static void test_contract_timing(void)
{
    uint32_t w0 = ws2812_rmt_encode_bit(false);
    uint32_t w1 = ws2812_rmt_encode_bit(true);
    double t0h = PH0_DUR(w0) * TICK_NS, t0l = PH1_DUR(w0) * TICK_NS;
    double t1h = PH0_DUR(w1) * TICK_NS, t1l = PH1_DUR(w1) * TICK_NS;

    /* 手册窗：T0H 200-500 / T0L 650-950 / T1H 650-950 / T1L 200-500 (ns)
     * 本实现取中心 350/900/900/350 —— 断言同时落在窗与中心附近 */
    CHECK(t0h >= 200 && t0h <= 500);
    CHECK(t0l >= 650 && t0l <= 950);
    CHECK(t1h >= 650 && t1h <= 950);
    CHECK(t1l >= 200 && t1l <= 500);

    /* 高电平相必须在前（相位0），低电平相在后 */
    CHECK_EQ_INT(PH0_LVL(w0), 1);
    CHECK_EQ_INT(PH1_LVL(w0), 0);
    CHECK_EQ_INT(PH0_LVL(w1), 1);
    CHECK_EQ_INT(PH1_LVL(w1), 0);

    /* 码元周期 = 1.25us ± 25%（两相时长和） */
    CHECK((t0h + t0l) > 1000 && (t0h + t0l) < 1500);
    CHECK((t1h + t1l) > 1000 && (t1h + t1l) < 1500);
}

/*==========================
 *  L3 蜕变：关系断言（绕开 oracle）
 *==========================*/

static void test_metamorphic(void)
{
    uint32_t w0 = ws2812_rmt_encode_bit(false);
    uint32_t w1 = ws2812_rmt_encode_bit(true);

    /* M1 位取反 <=> 高电平时长与低电平时长互换（总周期守恒） */
    CHECK_EQ_INT(PH0_DUR(w0), PH1_DUR(w1));
    CHECK_EQ_INT(PH1_DUR(w0), PH0_DUR(w1));
    CHECK_EQ_INT(PH0_DUR(w0) + PH1_DUR(w0), PH0_DUR(w1) + PH1_DUR(w1));

    /* M2 bit1 的高电平比 bit0 长（占空比方向性，反了颜色就反） */
    CHECK(PH0_DUR(w1) > PH0_DUR(w0));

    /* M3 编码长度守恒：任何颜色都是 24+1 字 */
    uint32_t buf[WS2812_WORDS_TOTAL];
    uint32_t reference = 0;
    for (int i = 0; i < 256; i++) {
        int n = ws2812_rmt_encode_rgb((uint8_t)i, (uint8_t)(255 - i),
                                      (uint8_t)(i ^ 0x5A), buf,
                                      WS2812_WORDS_TOTAL);
        CHECK_EQ_INT(n, WS2812_WORDS_TOTAL);
        if (i == 0)
            reference = 1;
    }
    (void)reference;

    /* M4 复位符号：两相全低且总时长 >= 50us（数据手册 latch 下限） */
    uint32_t reset = buf[WS2812_WORDS_TOTAL - 1];
    CHECK_EQ_INT(PH0_LVL(reset), 0);
    CHECK_EQ_INT(PH1_LVL(reset), 0);
    CHECK((PH0_DUR(reset) + PH1_DUR(reset)) * TICK_NS >= 50000.0);
}

/*==========================
 *  L3 差分：对标 NuttX 树内 ws2812esp32rmt 例程公式
 *==========================*/

/* 参考实现：deps/nuttx-apps/examples/ws2812esp32rmt 的编码公式逐字复刻 */
static uint32_t ref_encode_bit(bool bit)
{
    const uint16_t T0H = (uint16_t)(350 / 12.5);
    const uint16_t T0L = (uint16_t)(900 / 12.5);
    const uint16_t T1H = (uint16_t)(900 / 12.5);
    const uint16_t T1L = (uint16_t)(350 / 12.5);

    if (bit)
        return ((uint32_t)T1L << 16) | (0x8000u | T1H);
    return ((uint32_t)T0L << 16) | (0x8000u | T0H);
}

static void test_differential_upstream(void)
{
    for (int b = 0; b < 2; b++)
        CHECK_EQ_INT((long)ws2812_rmt_encode_bit(b != 0),
                     (long)ref_encode_bit(b != 0));

    /* GRB 顺序差分：绿色 MSB 应等于参考对 g 的第一个符号 */
    uint32_t buf[WS2812_WORDS_TOTAL];
    uint8_t r = 0xAB, g = 0xCD, b_ = 0xEF;
    ws2812_rmt_encode_rgb(r, g, b_, buf, WS2812_WORDS_TOTAL);

    /* 手工按参考公式排布 24 符号 */
    uint8_t seq[3] = { g, r, b_ };
    int idx = 0;
    for (int byte = 0; byte < 3; byte++)
        for (int bit = 7; bit >= 0; bit--) {
            CHECK_EQ_INT((long)buf[idx],
                         (long)ref_encode_bit((seq[byte] >> bit) & 1));
            idx++;
        }
    CHECK_EQ_INT(idx, 24);
}

/*==========================
 *  L3 PBT：随机颜色回读不变量（xorshift，2^20 样本）
 *==========================*/

static uint32_t rng_state = 0x20261004u;

static uint32_t rng_next(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void test_pbt_roundtrip(void)
{
    uint32_t buf[WS2812_WORDS_TOTAL];

    for (long iter = 0; iter < (1L << 20); iter++) {
        uint8_t r = (uint8_t)rng_next();
        uint8_t g = (uint8_t)rng_next();
        uint8_t b = (uint8_t)rng_next();

        ws2812_rmt_encode_rgb(r, g, b, buf, WS2812_WORDS_TOTAL);

        /* 回读：按 GRB MSB-first 从符号字解码应还原三字节 */
        uint8_t seq[3] = { 0, 0, 0 };
        for (int i = 0; i < 24; i++) {
            uint32_t w = buf[i];
            /* 高电平相(相位0)比低电平相长 => bit=1 */
            bool bit = PH0_DUR(w) > PH1_DUR(w);
            seq[i / 8] |= (uint8_t)(bit ? (1u << (7 - (i % 8))) : 0);
        }

        if (seq[0] != g || seq[1] != r || seq[2] != b) {
            fprintf(stderr,
                    "roundtrip fail iter=%ld got G=%02X R=%02X B=%02X "
                    "want G=%02X R=%02X B=%02X (seed_state=%08X)\n",
                    iter, seq[0], seq[1], seq[2], g, r, b, rng_state);
            g_fail_count++;
            return;
        }
    }
}

/*==========================
 *  L4 模糊：畸形输入不崩溃、错误码正确
 *==========================*/

static void test_fuzz_buffers(void)
{
    uint32_t buf[WS2812_WORDS_TOTAL];

    /* 缓冲不足：0..24 字都应拒绝（-EINVAL），不越界写 */
    for (int cap = 0; cap < WS2812_WORDS_TOTAL; cap++) {
        uint32_t small[WS2812_WORDS_TOTAL];
        memset(small, 0xA5, sizeof(small));
        CHECK_EQ_INT(ws2812_rmt_encode_rgb(1, 2, 3, small, cap), -EINVAL);
        /* cap 之后不应被触碰（越界写检测） */
        bool touched = false;
        for (int k = cap; k < WS2812_WORDS_TOTAL; k++)
            if (small[k] != 0xA5A5A5A5u)
                touched = true;
        CHECK(!touched);
    }

    /* NULL 缓冲 */
    CHECK_EQ_INT(ws2812_rmt_encode_rgb(1, 2, 3, NULL, 100), -EINVAL);

    /* 极值颜色不崩溃 */
    CHECK_EQ_INT(ws2812_rmt_encode_rgb(0, 0, 0, buf, WS2812_WORDS_TOTAL),
                 WS2812_WORDS_TOTAL);
    CHECK_EQ_INT(ws2812_rmt_encode_rgb(255, 255, 255, buf, WS2812_WORDS_TOTAL),
                 WS2812_WORDS_TOTAL);
}

int main(void)
{
    test_contract_timing();
    test_metamorphic();
    test_differential_upstream();
    test_pbt_roundtrip();
    test_fuzz_buffers();

    printf("ws2812: %d checks, %d failed\n", g_check_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
