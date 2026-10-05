/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * test_hw_profiles.c - 四板硬件档案占用表契约测试
 *
 * WHAT : 校验每板 RETRO_GPIO_OCCUPIED_LIST 与全局硬件规范一致
 * WHY  : 占用表是 retro_gpio 拦截的单一事实来源；表错了教学脚本会
 *        点状态灯或碰 PSRAM/Flash 脚搞挂板子。2026-10-04 晚修正的
 *        事实（CAM GPIO17=PSRAM CLK、C3 12/13=LED 且 GPIO10 释放、
 *        Pico CVBS 移脚 GP12-15、S3 WS2812=38/48）必须被测试钉死
 * WHO  : tests/host/run_all.sh 调度
 * WHERE: esp32-retro-ws/tests/host/test_hw_profiles.c
 * WHEN : 2026-10-04 晚新增
 * HOW  : 四板 provider（hw_tab_*.c）各含档案落表 -> 本文件断言：
 *        L1 契约（板上 LED 脚全部被占 = 指示灯避让原则）+
 *        L1 契约（教学推荐脚全部不被占）+
 *        回归（本次修正的具体条目存在/不存在）
 */

#include <stdio.h>
#include "test_framework.h"
#include "hw_tab_provider.h"

int hw_tab_has(const struct hw_occ_s *tab, int n, int pin)
{
    for (int i = 0; i < n; i++)
        if (tab[i].pin == pin)
            return 1;
    return 0;
}

static void test_s3(void)
{
    /* 板上指示灯避让：WS2812 两代板脚都封 */
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 38));
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 48));

    /* 教学推荐脚（档案注释承诺）不占：4/7/8/9/12/18/21/33/34/39/47 */
    int teach[] = { 4, 7, 8, 9, 12, 18, 21, 33, 34, 39, 47 };
    for (unsigned i = 0; i < sizeof(teach) / sizeof(teach[0]); i++)
        if (hw_tab_has(hw_s3_tab, hw_s3_tab_n, teach[i])) {
            fprintf(stderr, "S3 teaching pin %d wrongly blocked\n", teach[i]);
            g_fail_count++;
        }

    /* CVBS 梯四脚全占（LCD_CAM 真硬件流） */
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 2));
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 15));
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 16));
    CHECK(hw_tab_has(hw_s3_tab, hw_s3_tab_n, 17));
}

static void test_cam(void)
{
    /* 指示灯脚：红 LED 33 + 闪光灯 4 */
    CHECK(hw_tab_has(hw_cam_tab, hw_cam_tab_n, 33));
    CHECK(hw_tab_has(hw_cam_tab, hw_cam_tab_n, 4));

    /* 2026-10-04 修正回归：GPIO17=PSRAM CLK 必须在表 */
    CHECK(hw_tab_has(hw_cam_tab, hw_cam_tab_n, 17));
    CHECK(hw_tab_has(hw_cam_tab, hw_cam_tab_n, 16));
}

static void test_c3(void)
{
    /* 板上 LED D4/D5（GPIO12/13，高电平点亮）——避让原则必须封 */
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 12));
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 13));

    /* 2026-10-04 修正回归：GPIO10 已释放为教学脚（PDM 单脚方案） */
    CHECK(!hw_tab_has(hw_c3_tab, hw_c3_tab_n, 10));

    /* CVBS PDM 脚占用；VDD_SPI 封 */
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 1));
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 11));

    /* Flash 总线四脚封（DIO：SPICS0/CLK/D0/D1） */
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 14));
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 15));
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 16));
    CHECK(hw_tab_has(hw_c3_tab, hw_c3_tab_n, 17));
}

static void test_pico(void)
{
    /* 板上 LED 避让 */
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 25));

    /* 2026-10-04 晚移脚回归：CVBS=GP12-15（GP23=SMPS 不用于视频） */
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 12));
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 13));
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 14));
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 15));

    /* SMPS/VBUS 特殊脚仍封；旧 CVBS 脚 GP20/21 释放为教学脚 */
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 23));
    CHECK(hw_tab_has(hw_pico_tab, hw_pico_tab_n, 24));
    CHECK(!hw_tab_has(hw_pico_tab, hw_pico_tab_n, 20));
    CHECK(!hw_tab_has(hw_pico_tab, hw_pico_tab_n, 21));
    CHECK(!hw_tab_has(hw_pico_tab, hw_pico_tab_n, 22));
}

/* 蜕变：任何表的引脚号必须非负且唯一（数据完整性） */
static void test_table_integrity(void)
{
    const struct hw_occ_s *tabs[] =
        { hw_s3_tab, hw_cam_tab, hw_c3_tab, hw_pico_tab };
    const int *counts[] =
        { &hw_s3_tab_n, &hw_cam_tab_n, &hw_c3_tab_n, &hw_pico_tab_n };

    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < *counts[t]; i++) {
            CHECK(tabs[t][i].pin >= 0);
            CHECK(tabs[t][i].why != NULL && tabs[t][i].why[0] != '\0');
            for (int j = i + 1; j < *counts[t]; j++)
                if (tabs[t][i].pin == tabs[t][j].pin) {
                    fprintf(stderr, "board %d duplicate pin %d\n",
                            t, tabs[t][i].pin);
                    g_fail_count++;
                }
        }
    }
}

int main(void)
{
    test_s3();
    test_cam();
    test_c3();
    test_pico();
    test_table_integrity();

    printf("hw_profiles: %d checks, %d failed\n",
           g_check_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
