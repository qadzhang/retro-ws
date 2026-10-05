/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX ADC 头的宿主机隔离桩（与 12.12 真实头同源）
 */
#ifndef __TEST_STUB_ADC_H
#define __TEST_STUB_ADC_H

#include <stdint.h>

struct adc_msg_s
{
  uint8_t  am_channel;
  int32_t  am_data;
};

#endif /* __TEST_STUB_ADC_H */
