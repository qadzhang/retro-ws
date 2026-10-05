/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* WHAT : Pico 占用表 provider（含档案 -> 落地导出） */
#include "hw_tab_provider.h"
#include "hw_rp2040_pico.h"

const struct hw_occ_s hw_pico_tab[] = { RETRO_GPIO_OCCUPIED_LIST };
const int hw_pico_tab_n = sizeof(hw_pico_tab) / sizeof(hw_pico_tab[0]);
