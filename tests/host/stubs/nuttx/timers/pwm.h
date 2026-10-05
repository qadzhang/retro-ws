/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX PWM 头的宿主机隔离桩（与 12.12 真实头同源）
 */
#ifndef __TEST_STUB_PWM_H
#define __TEST_STUB_PWM_H

#include <stdint.h>

#define PWMIOC_SETCHARACTERISTICS      _PWMIOC(1)
#define PWMIOC_GETCHARACTERISTICS      _PWMIOC(2)
#define PWMIOC_START                   _PWMIOC(3)
#define PWMIOC_STOP                    _PWMIOC(4)
#define PWMIOC_FAULTS_FETCH_AND_CLEAR  _PWMIOC(5)

#define _PWMIOC(n) (n)

typedef uint16_t ub16_t;

struct pwm_info_s
{
  uint32_t frequency;
  ub16_t   duty;      /* 最大 0xffff */
  uint8_t  cpol;
  uint8_t  dcpol;
};

#endif /* __TEST_STUB_PWM_H */
