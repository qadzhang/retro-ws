/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * cmd_run_main.c - NSH 内置命令 run：执行 ROM 模块应用
 *
 * WHAT : `run <模块名> [参数...]` 装载 /rom/pkg/bin/<名> 的 .rmo XIP
 *        模块并执行其 main(argc, argv)
 * WHY  : 应用/系统分离（2026-10-06）——CLI 应用（sysinfo 等）以 .rpk
 *        包交付、代码常驻 Flash 原址执行；run 是它们的统一入口
 *        （与 `script <名>` 直跑 ROM 脚本同款交互形态）
 * WHO  : NuttX builtin 框架（PROGNAME=run）；用户经 NSH 调用
 * HOME: Application.mk 将 main 重命名为 run_main（builtin）
 * WHERE: retro-ws/src/nuttx/common/apps/system/cmd_run_main.c
 * WHEN : 2026-10-06 新增
 * HOW  : rommod_load（XIP 零拷贝 + 按需 RAM 镜像）-> rommod_getsym
 *        取 main -> 在本命令任务内联调用（builtin 即独立任务，栈
 *        尺寸 STACKSIZE=16384 足够 CLI 应用）-> 返回即 rommod_put
 *        归还引用（末引用归零卸载模块 RAM 镜像）
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <errno.h>

#include "rommod.h"

int main(int argc, char *argv[])
{
    struct rommod_s *mod = NULL;
    void *entry = NULL;
    int ret;

    if (argc < 2)
    {
        printf("用法: run <模块名> [参数...]    # 执行 /rom/pkg/bin/<名> 模块\n");
        return 0;
    }

    if (rommod_load(argv[1], &mod) != OK)
    {
        printf("run: 模块未找到或装载失败: %s（/rom/pkg/bin/%s）\n",
               argv[1], argv[1]);
        return -ENOENT;
    }

    if (rommod_getsym(mod, "main", &entry) != OK || entry == NULL)
    {
        printf("run: %s 不导出 main（GUI 模块请从桌面启动）\n", argv[1]);
        rommod_put(mod);
        return -ENOENT;
    }

    /* 内联执行：本 builtin 任务即模块宿主任务（模块栈=本命令栈） */
    ret = ((int (*)(int, char **))entry)(argc - 1, argv + 1);
    rommod_put(mod);

    return ret;
}
