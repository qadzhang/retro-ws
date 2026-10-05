/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * console_sim.c - AV 控制台无头模拟器（README 截图专用）
 *
 * WHAT : 以真实 cvbs_console（12px 点阵、12x14 网格）在 320x240
 *        帧缓冲上跑一段 NSH 会话并导出截图
 * WHY  : CLI 档全系走 AV 视频输出（AGENTS.md 7.3）；README 配图
 *        需要真实渲染路径的 240p 控制台样张（非示意图）
 * WHO  : tools/sim/build.sh 编译；docs/screenshots/ 消费
 * WHERE: esp32-retro-ws/tools/sim/console_sim.c
 * WHEN : 2026-10-05 新增（README 截图需求）
 * HOW  : cvbs_core_fb_alloc(320,240) -> cvbs_console_init -> 写
 *        NSH 风格会话（中文+命令+输出）-> PGM 导出 -> PNG
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include "driver/cvbs_core.h"
#include "driver/cvbs_console.h"
#include "driver/cvbs_ime.h"
#include "driver/drv_pinyin.h"

#define CON_W 320
#define CON_H 240

static void put(const char *s)
{
    cvbs_console_write(s, strlen(s));
}

static void dump_pgm(const char *path)
{
    int w, h;
    const uint8_t *fb = cvbs_console_fb(&w, &h);
    FILE *f = fopen(path, "wb");
    if (!f || !fb) {
        fprintf(stderr, "console_sim: cannot write %s\n", path);
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(fb, 1, (size_t)w * h, f);
    fclose(f);
    printf("console_sim: %s (%dx%d, %d cols x %d rows)\n",
           path, w, h, cvbs_console_cols(), cvbs_console_rows());
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "console_320.pgm";

    if (cvbs_core_fb_alloc(CON_W, CON_H) != 0) {
        fprintf(stderr, "console_sim: fb alloc failed\n");
        return 1;
    }
    if (cvbs_console_init() != 0) {
        fprintf(stderr, "console_sim: console init failed\n");
        return 1;
    }

    /* NSH 风格会话（26 列 x 17 行，行宽控制在列内避免折行） */
    put("nsh> uname -a\n");
    put("NuttX 12.12.0 ESP32-C3\n");
    put("RISC-V RV32IMC 160MHz\n");
    put("nsh> free\n");
    put("  SRAM 总计 400KB\n");
    put("  空闲 218KB\n");
    put("nsh> ps\n");
    put("  nsh     就绪\n");
    put("  video   运行\n");
    put("nsh> script hello\n");
    put("你好，世界！AV 控制台\n");
    put("nsh> pkg list\n");
    put("  ucblogo 6.2.2-1\n");
    put("标点基线: AaBb19 ,.;:!? \"'_\n");
    put("全角对比: 你好, 世界; 界: \"引\"\n");

    /* CCDOS 式输入法演示：ime on -> 底部常驻条 -> nihao 选字 */
    put("nsh> ime on\n");
    put("输入法已启动(底部状态条)\n");
    cvbs_ime_enable(true);
    const char *imu = "nihao";
    for (const char *p = imu; *p; p++)
        cvbs_ime_feed(*p);            /* 拼音累积 -> 条显示候选 */
    cvbs_ime_feed('1');               /* 数字选首候选 */
    imu = "shijie";
    for (const char *p = imu; *p; p++)
        cvbs_ime_feed(*p);
    cvbs_ime_feed('1');

    dump_pgm(out);
    cvbs_core_fb_free();
    return 0;
}
