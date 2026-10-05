/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * hw_tab_provider.h - 占用表提供者公共头
 *
 * WHAT : 供 test_hw_profiles.c 链接的四板占用表导出声明
 * WHY  : 四份硬件档案的 RETRO_GPIO_OCCUPIED_LIST 宏同名，无法在同
 *        一翻译单元重复展开——每板一个 provider .c 各自包含档案落地
 * WHERE: esp32-retro-ws/tests/host/hw_tab_provider.h
 * WHEN : 2026-10-04 晚新增
 */

#ifndef __HW_TAB_PROVIDER_H
#define __HW_TAB_PROVIDER_H

struct hw_occ_s
{
    int pin;
    const char *why;
};

extern const struct hw_occ_s hw_s3_tab[];
extern const int            hw_s3_tab_n;
extern const struct hw_occ_s hw_cam_tab[];
extern const int            hw_cam_tab_n;
extern const struct hw_occ_s hw_c3_tab[];
extern const int            hw_c3_tab_n;
extern const struct hw_occ_s hw_pico_tab[];
extern const int            hw_pico_tab_n;

/* 在 provider .c 里线性查表（表很小，遍历足够） */
int hw_tab_has(const struct hw_occ_s *tab, int n, int pin);

#endif /* __HW_TAB_PROVIDER_H */
