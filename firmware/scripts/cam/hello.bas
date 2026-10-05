' SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
' SPDX-License-Identifier: Apache-2.0
' 板级演示（ROM XIP）：BASIC 问候 + GPIO
PRINT "你好，复古工作站！/ BASIC from ROM"
CALL retro_gpio_config(12, "out")
FOR i = 0 TO 3
    CALL retro_gpio_write(12, 1)
    SLEEP 250
    CALL retro_gpio_write(12, 0)
    SLEEP 250
NEXT i
PRINT "完成 / done"
