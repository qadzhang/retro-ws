// SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
// SPDX-License-Identifier: Apache-2.0
// 板级演示（ROM XIP）：JS 问候
print("你好，复古工作站！/ JS from ROM");
for (var i = 0; i < 3; i++) {
    retro_gpio_write(48, 1);
    sleep(200);
    retro_gpio_write(48, 0);
    sleep(200);
}
