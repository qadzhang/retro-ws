# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
# 板级演示脚本（ROM XIP）：Hello + 点灯 + 总线扫描
import gpio  # 内建 retro_gpio 别名（若工程别名不同请改回 retro_gpio_*）

print("你好，复古工作站！/ Hello from ROM (XIP)")

# 板载 LED 呼吸（2 秒）
for i in range(0, 100, 5)
    retro_gpio_pwm(48, i, 1000)
    sleepms(20)
end

# I2C 总线扫描（machine 风格兼容层）
if retro_i2c_init(0, 18, 19, 100000) == 0
    n = retro_i2c_scan(0)
    print("I2C 设备数 / devices:", n)
end
