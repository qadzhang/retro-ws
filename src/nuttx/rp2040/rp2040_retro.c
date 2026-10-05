/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * rp2040_retro.c - Pico 主入口（第四目标：本地 CLI 终端）
 *
 * WHAT : 双核任务装配——Core0 = NSH/程序与脚本运行（NuttX 默认），
 *        Core1 = 文件 IO/SD 服务任务
 * WHY  : 用户定的全局双核分工原则在 Pico 档的落地（本板无图形/
 *        音频，媒体核退化为文件服务核）
 * WHO  : board.c 的 board_late_initialize()（经 apps 集成层）
 * WHERE: retro-ws/src/nuttx/rp2040/rp2040_retro.c
 * WHEN : 2026-10-04 新增
 * HOW  : rp2040_retro_start() -> kthread_create(io 服务) 并以
 *        sched_setaffinity 钉到 CPU1；NSH 保持 CPU0 上下文
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/sched.h>
#include <nuttx/kthread.h>

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "board.h"

#define IO_TASK_PRI    120
#define IO_TASK_STACK  (3 * 1024)

/*==========================
 *  Core1 文件 IO 服务 / file-IO service core
 *==========================*/

/*
 * WHAT : Core1 服务任务（SD/文件 IO 协作方）
 * WHY  : 双核分工：Core0 跑用户程序，Core1 承担存储相关操作
 * HOW  : 当前为心跳+喂狗占位；具体文件服务（异步 SD 刷新/缓冲）
 *        按需求接入（NEXT_STEPS 41）
 */
static int core1_io_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog(LOG_INFO, "[Core1] file-IO service started\n");

    /* 钉到 CPU1：与 NSH(CPU0) 分离 */
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
    }
#endif

    while (1) {
        usleep(1000000);   /* 服务心跳（后续挂真实文件作业） */
    }

    return 0;
}

/*==========================
 *  任务装配
 *==========================*/

void rp2040_retro_start(void)
{
    /* 注册项目 NSH 命令（sysinfo/pkg/script 等） */
    {
        extern void esp32retro_nsh_register(void);
        esp32retro_nsh_register();
    }

    int ret = kthread_create("ioservice", IO_TASK_PRI,
                             IO_TASK_STACK,
                             core1_io_task, NULL);
    if (ret < 0)
        syslog(LOG_ERR, "[Pico] io service task failed: %d\n", ret);
    else
        syslog(LOG_INFO, "Retro WS Pico started (dual-core split)\n");
}
