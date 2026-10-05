/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : retro_gpio 策略层测试（宿主机）
 * WHY  : "系统占用脚必须被拦截并提示" 是用户明确要求的硬契约
 * WHO  : tests/host/run_all.sh
 * WHERE: retro-ws/tests/host/test_retro_gpio.c
 * WHEN : 2026-10-04 新增
 * HOW  : 本文件提供模拟占用表（g_retro_gpio_occupied，模拟 S3 板：
 *        GPIO2=CVBS、GPIO26..32=flash、GPIO35..37=PSRAM），
 *        断言：占用脚全部 -EBUSY 且打印"已占用"；空闲脚在无 /dev
 *        环境下返回 -ENOENT（不会先碰设备）。
 */

#include "test_framework.h"

#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "retro_gpio.h"

/* 模拟 ESP32-S3 DevKitC 占用表（与 hw_esp32s3_devkitc.h 同源数据） */
const struct retro_gpio_occ_s g_retro_gpio_occupied[] = {
    { 2,  "CVBS video out"  },
    { 26, "flash"           },
    { 27, "flash"           },
    { 28, "flash"           },
    { 29, "flash"           },
    { 30, "flash"           },
    { 31, "flash"           },
    { 32, "flash"           },
    { 35, "PSRAM"           },
    { 36, "PSRAM"           },
    { 37, "PSRAM"           },
};
const int g_retro_gpio_occupied_count =
    sizeof(g_retro_gpio_occupied) / sizeof(g_retro_gpio_occupied[0]);

/*
 * WHAT : 捕获 stdout 到文件，便于断言"已占用"提示确实送达用户
 * HOW  : freopen 重建 stdout；返回原 fd 供恢复
 */
static int capture_stdout(char *path)
{
    fflush(stdout);
    snprintf(path, 128, "/tmp/retro_gpio_cap_%d.txt", (int)getpid());
    int saved = dup(fileno(stdout));
    freopen(path, "w", stdout);
    return saved;
}

static void restore_stdout(int saved)
{
    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    char capath[128];

    CHECK_EQ_INT(retro_gpio_init(), OK);

    /* 占用脚：全部入口一律 -EBUSY（用户要求的"直接报错返回已占用"） */
    CHECK_EQ_INT(retro_gpio_config(2, "out"), -EBUSY);
    CHECK_EQ_INT(retro_gpio_write(2, 1), -EBUSY);
    CHECK_EQ_INT(retro_gpio_read(2), -EBUSY);
    CHECK_EQ_INT(retro_gpio_pwm_set(2, 50, 1000), -EBUSY);
    CHECK_EQ_INT(retro_gpio_release(2), -EBUSY);

    for (int i = 26; i <= 32; i++)
        CHECK_EQ_INT(retro_gpio_write(i, 1), -EBUSY);
    for (int i = 35; i <= 37; i++)
        CHECK_EQ_INT(retro_gpio_config(i, "in"), -EBUSY);

    /* 提示语含双语"已占用"与占用原因 */
    int saved = capture_stdout(capath);
    retro_gpio_write(2, 1);
    restore_stdout(saved);
    CHECK(file_contains(capath, "已被系统占用"));
    CHECK(file_contains(capath, "occupied"));
    CHECK(file_contains(capath, "CVBS"));

    /* 空闲教学脚：宿主机无 /dev/gpioN -> -ENOENT（策略层放行） */
    CHECK_EQ_INT(retro_gpio_config(8, "out"), -ENOENT);
    CHECK_EQ_INT(retro_gpio_write(12, 1), -ENOENT);
    CHECK_EQ_INT(retro_gpio_read(8), -ENOENT);

    /* 参数校验 */
    CHECK_EQ_INT(retro_gpio_config(-1, "out"), -EINVAL);
    CHECK_EQ_INT(retro_gpio_config(8, NULL), -EINVAL);
    CHECK_EQ_INT(retro_gpio_config(8, "wat"), -EINVAL);
    CHECK_EQ_INT(retro_gpio_write(-5, 1), -EINVAL);
    CHECK_EQ_INT(retro_gpio_pwm_set(8, 101, 1000), -EINVAL);
    CHECK_EQ_INT(retro_gpio_pwm_set(8, 50, 0), -EINVAL);

    /* 占用判定与表规模：模拟板 11 脚 */
    CHECK_EQ_INT(g_retro_gpio_occupied_count, 11);

    TEST_REPORT("test_retro_gpio");
}
