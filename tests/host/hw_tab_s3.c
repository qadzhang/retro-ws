/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* WHAT : S3 占用表 provider（含档案 -> 落地导出） */
#include "hw_tab_provider.h"
#include "hw_esp32s3_devkitc.h"

const struct hw_occ_s hw_s3_tab[] = { RETRO_GPIO_OCCUPIED_LIST };
const int hw_s3_tab_n = sizeof(hw_s3_tab) / sizeof(hw_s3_tab[0]);
