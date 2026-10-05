/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX GPIO ioctl 头的宿主机隔离桩
 * WHY  : 直接 -I 真实 nuttx/include 会接管全部系统头（stdio 等），
 *        与宿主 libc 冲突；桩从 deps/nuttx 12.12 真实头逐字提取
 *        必要定义，语义与目标机一致
 * HOW  : 与 include/nuttx/ioexpander/gpio.h 同源（CONFIG_DEV_GPIO）
 */
#ifndef __TEST_STUB_GPIO_H
#define __TEST_STUB_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#define GPIOC_WRITE      _GPIOC(1)
#define GPIOC_READ       _GPIOC(2)
#define GPIOC_PINTYPE    _GPIOC(3)
#define GPIOC_REGISTER   _GPIOC(4)
#define GPIOC_UNREGISTER _GPIOC(5)
#define GPIOC_SETPINTYPE _GPIOC(6)

#define _GPIOC(n) (n)

/* Identifies the type of the GPIO pin（与 NuttX 12.12 逐字一致） */
enum gpio_pintype_e
{
  GPIO_INPUT_PIN = 0, /* float */
  GPIO_INPUT_PIN_PULLUP,
  GPIO_INPUT_PIN_PULLDOWN,
  GPIO_OUTPUT_PIN, /* push-pull */
  GPIO_OUTPUT_PIN_OPENDRAIN,
  GPIO_INTERRUPT_PIN,
  GPIO_INTERRUPT_HIGH_PIN,
  GPIO_INTERRUPT_LOW_PIN,
  GPIO_INTERRUPT_RISING_PIN,
  GPIO_INTERRUPT_FALLING_PIN,
  GPIO_INTERRUPT_BOTH_PIN,
  GPIO_INTERRUPT_PIN_WAKEUP,
  GPIO_INTERRUPT_HIGH_PIN_WAKEUP,
  GPIO_INTERRUPT_LOW_PIN_WAKEUP,
  GPIO_INTERRUPT_RISING_PIN_WAKEUP,
  GPIO_INTERRUPT_FALLING_PIN_WAKEUP,
  GPIO_INTERRUPT_BOTH_PIN_WAKEUP,
  GPIO_OUTPUT_PIN_OPENDRAIN_PULLUP,
  GPIO_INPUT_PIN_PUSHPULL,
};

#endif /* __TEST_STUB_GPIO_H */
