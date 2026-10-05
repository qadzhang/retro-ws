/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/* WHAT : CAM 占用表 provider（含档案 -> 落地导出） */
#include "hw_tab_provider.h"
#include "hw_esp32cam_aithinker.h"

const struct hw_occ_s hw_cam_tab[] = { RETRO_GPIO_OCCUPIED_LIST };
const int hw_cam_tab_n = sizeof(hw_cam_tab) / sizeof(hw_cam_tab[0]);
