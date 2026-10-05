/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : 核心路径微基准 / micro benchmarks for hot paths
 * WHY  : ai-code-testing 规范：性能守护（回归门禁的基线来源）；
 *        CRC32 逐位表查实现 vs zlib 字节表实现应有同数量级吞吐
 * WHO  : tests/host/run_all.sh
 * WHERE: esp32/esp32-retro-ws/tests/host/bench_core.c 之
 *        esp32-retro-ws/tests/host/bench_core.c
 * WHEN : 2026-10-04 新增
 * HOW  : CLOCK_MONOTONIC 计时；断言吞吐下限（防"功能对但慢"
 *        的回归），输出 JSON 行供 CI 记录基线
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PKG_MGR_SRC
#define PKG_MGR_SRC "pkg_manager.c"
#endif
#include PKG_MGR_SRC

#include "cvbs_core.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint32_t sink_dummy;

static void bench_sink(const uint8_t *line, size_t len, int no)
{
    (void)line; (void)len; (void)no;
    sink_dummy++;
}

int main(void)
{
    /* ---------- CRC32：1MB 缓冲 20 遍 ---------- */
    size_t n = 1 << 20;
    uint8_t *buf = malloc(n);
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(i * 131);

    crc32_update(0, buf, 1);            /* 预热建表 */

    double t0 = now_ms();
    uint32_t crc = 0;
    for (int r = 0; r < 20; r++)
        crc = crc32_update(0, buf, n);
    double t1 = now_ms();

    double mbps = 20.0 * (n / 1048576.0) / ((t1 - t0) / 1000.0);
    printf("{\"bench\":\"crc32\",\"mb_per_s\":%.1f,\"crc\":\"%08x\"}\n",
           mbps, crc);

    /* 门禁：宿主机上低于 50MB/s 说明实现退化（表驱动应 >200） */
    if (mbps < 50.0) {
        fprintf(stderr, "BENCH FAIL: crc32 %.1f MB/s < 50\n", mbps);
        return 1;
    }

    /* ---------- CVBS 成帧：50 帧端到端 ---------- */
    cvbs_core_fb_alloc(320, 240);
    cvbs_core_set_direct_luma(true);

    t0 = now_ms();
    for (int r = 0; r < 50; r++)
        cvbs_core_generate_frame(true, bench_sink);
    t1 = now_ms();

    double fps = 50.0 / ((t1 - t0) / 1000.0);
    printf("{\"bench\":\"cvbs_frame\",\"fps\":%.1f,\"lines\":%u}\n",
           fps, (unsigned)sink_dummy);

    /* 门禁：50Hz 实时目标 -> 至少 5 倍余量 */
    if (fps < 250.0) {
        fprintf(stderr, "BENCH FAIL: cvbs frame %.1f fps < 250\n", fps);
        return 1;
    }

    cvbs_core_fb_free();
    free(buf);

    printf("[bench_core] PASS\n");
    return 0;
}
