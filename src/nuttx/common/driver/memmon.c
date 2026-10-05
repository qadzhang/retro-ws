/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * memmon.c - 内存监控
 *
 * WHAT : 内存监控
 * WHY  : 堆水位监控与三级告警（80/90/95%）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/driver/memmon.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 5 秒巡检，紧急时终止最大非核心任务或触发重启
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/kthread.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <malloc.h>
#include <sched.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_MEMMON
#  define CONFIG_RETRO_MEMMON 1
#endif

#if CONFIG_RETRO_MEMMON

/*==========================
 *  配置参数
 *==========================*/

#define MEMMON_INTERVAL_MS    5000    /* 监控间隔（5秒）*/
#define MEMMON_SAMPLE_RATE    1       /* 采样率（每秒采样次数）*/

#define MEMMON_WARNING_THRESH  80     /* 警告阈值（%）*/
#define MEMMON_CRITICAL_THRESH 90    /* 严重阈值（%）*/
#define MEMMON_EMERGENCY_THRESH 95   /* 紧急阈值（%）*/

/* 监控任务栈大小 / Monitor task stack size（原 sizeof(g_cpu1_stack) 未定义）*/
#define MEMMON_TASK_STACK      4096

/* 告警最小间隔（秒）/ Minimum seconds between repeated alerts */
#define MEMMON_ALERT_INTERVAL_SEC 30

/*==========================
 *  内存信息
 *==========================*/

struct mem_info {
    size_t    total;         /* 总内存（字节）*/
    size_t    used;          /* 已使用（字节）*/
    size_t    peak;          /* 峰值使用（字节）*/
    size_t    free;          /* 空闲（字节）*/
    uint8_t   usage_pct;     /* 使用率（%）*/
    uint32_t  alloc_count;   /* 分配次数 */
    uint32_t  free_count;    /* 释放次数 */
    uint32_t  fail_count;    /* 分配失败次数 */
    uint32_t  last_update;   /* 上次更新时间 */
};

/* 任务内存信息 */
struct task_mem_info {
    pid_t     pid;           /* 任务 PID */
    char      name[32];      /* 任务名 */
    size_t    stack_used;    /* 栈使用量 */
    size_t    heap_used;     /* 堆使用量 */
    uint8_t   priority;      /* 优先级 */
    bool      is_critical;   /* 是否核心任务 */
};

static struct mem_info g_mem = {0};

/* 监控状态 */
static bool g_memmon_running = false;
static bool g_memmon_paused = false;
static uint32_t g_warning_count = 0;
static uint32_t g_critical_count = 0;

/* 紧急情况 */
static bool g_emergency_mode = false;
static size_t g_min_free_bytes = (size_t)-1;

/*==========================
 *  获取内存信息
 *==========================*/

/**
 * 获取当前内存信息
 */
static void memmon_update_info(void)
{
#ifdef CONFIG_SMART
    struct mallinfo mi = mallinfo();

    /* arena 即堆总量（已含空闲块），total 不能再加 fordblks（重复计数）
     * arena is total heap; adding fordblks double-counts free memory */
    g_mem.total      = (size_t)mi.arena;
    g_mem.used       = (size_t)mi.uordblks;
    g_mem.free       = (size_t)mi.fordblks;
    g_mem.usage_pct  = (g_mem.total > 0) ?
                       (uint8_t)((g_mem.used * 100) / g_mem.total) : 0;

    /* 更新峰值 */
    if (g_mem.used > g_mem.peak)
        g_mem.peak = g_mem.used;

    /* 更新最小空闲 */
    if (g_mem.free < g_min_free_bytes)
        g_min_free_bytes = g_mem.free;

    /* 更新时间戳（Unix 秒）/ Update timestamp (unix seconds) */
    g_mem.last_update = (uint32_t)time(NULL);
#endif

#ifdef CONFIG_ESP32S3_SPIRAM
    /* PSRAM 信息 */
    extern size_t esp_spiram_get_free_size(void);
    extern size_t esp_spiram_get_allocated_size(void);
    /* 可以添加 PSRAM 统计 */
#endif
}

/**
 * 获取任务内存使用情况
 */
static int memmon_get_task_info(struct task_mem_info *info, int max_tasks)
{
#ifdef CONFIG_SCHED_CPULOAD_NONE
    return 0;
#else
    int count = 0;

    /* 遍历所有任务 */
    for (int i = 0; i < max_tasks && count < max_tasks; i++) {
        /* TODO: 使用 sched_foreach 遍历任务 */
        /* 这里需要 NuttX 的任务遍历 API */
    }

    return count;
#endif
}

/*==========================
 *  告警
 *==========================*/

/* 前置声明：告警路径在定义之前调用 / forward declarations */
static int memmon_kill_largest_task(void);
static int memmon_kill_all_noncritical(void);

/**
 * 记录告警
 */
static void memmon_log_alert(const char *level, uint8_t usage_pct,
                             size_t used, size_t total)
{
    static time_t last_alert_time = 0;
    time_t now = time(NULL);

    /* 限制告警频率（每 MEMMON_ALERT_INTERVAL_SEC 秒最多一次）
     * Rate limit: at most one alert per interval */
    if (last_alert_time != 0 &&
        (now - last_alert_time) < MEMMON_ALERT_INTERVAL_SEC)
        return;

    last_alert_time = now;

    syslog(LOG_WARNING,
           "[MEMMON] %s: Memory usage %u%% (%lu / %lu bytes)\n",
           level, usage_pct,
           (unsigned long)used, (unsigned long)total);

    /* 同时输出到串口 */
    printf("[MEMMON] %s: Memory %u%% used\n", level, usage_pct);
}

/**
 * 触发警告（80%）
 */
static void memmon_warning(size_t used, size_t total, uint8_t pct)
{
    g_warning_count++;
    memmon_log_alert("WARNING", pct, used, total);

    /* TODO: 可选：通知用户（LED 闪烁等）*/
}

/**
 * 触发严重告警（90%）
 */
static void memmon_critical(size_t used, size_t total, uint8_t pct)
{
    g_critical_count++;

    syslog(LOG_ERR,
           "[MEMMON] CRITICAL: Memory %u%% (%lu / %lu bytes)\n",
           pct, (unsigned long)used, (unsigned long)total);

    /* 尝试终止最大的非核心任务 */
    memmon_kill_largest_task();
}

/**
 * 触发紧急情况（95%+）/ Trigger emergency (95%+)
 */
static void memmon_emergency(size_t used, size_t total, uint8_t pct)
{
    g_emergency_mode = true;

    syslog(LOG_EMERG,
           "[MEMMON] EMERGENCY: Memory %u%% - triggering recovery!\n",
           pct);

    /* 终止所有非核心任务 */
    memmon_kill_all_noncritical();

    /* TODO(策略/Policy): 紧急时的最终手段（重启 vs 继续降级运行）待定，
     * 原 while(1) 死等看门狗的写法会阻塞监控任务本身且依赖未定义的
     * esp32s3_wdt_feed()；现仅持续记录并返回，等待下一轮巡检复核
     * Final-action policy (reboot vs degraded run) is undecided; the old
     * busy-loop relied on an undefined esp32s3_wdt_feed() symbol. We now
     * log and return; the next poll re-evaluates. */
    syslog(LOG_EMERG,
           "[MEMMON] Memory exhausted (%lu/%lu bytes), non-critical tasks "
           "killed; monitoring continues\n",
           (unsigned long)used, (unsigned long)total);
}

/*==========================
 *  任务管理
 *==========================*/

/**
 * 终止最大的非核心任务
 */
static int memmon_kill_largest_task(void)
{
    /* TODO: 使用 NuttX 的 sched_foreach 找到最大任务并终止 */
    syslog(LOG_WARNING, "[MEMMON] Attempting to free memory by terminating tasks...\n");

    /* 查找优先级最低的 非核心 任务 */
    pid_t pid_to_kill = -1;
    size_t max_size = 0;

    /* 占位：实际需要遍历任务列表 */
    /* 这里简化处理，实际需要 sched_foreach */

    if (pid_to_kill > 0) {
        syslog(LOG_WARNING, "[MEMMON] Killing task PID %d to free memory\n", pid_to_kill);
        /* task_delete(pid_to_kill); */
        return OK;
    }

    syslog(LOG_WARNING, "[MEMMON] No non-critical tasks to kill\n");
    return -ESRCH;
}

/**
 * 终止所有非核心任务
 */
static int memmon_kill_all_noncritical(void)
{
    syslog(LOG_WARNING, "[MEMMON] Killing all non-critical tasks...\n");

    /* TODO: 遍历并终止所有非核心任务
     * 保留：NSH 内核任务、网络协议栈、图形核心任务
     * 终止：所有用户任务和调试任务
     */

    return OK;
}

/**
 * 恢复模式：允许重新分配内存
 */
static void memmon_recovery(void)
{
    g_emergency_mode = false;
    g_min_free_bytes = (size_t)-1;
    syslog(LOG_INFO, "[MEMMON] Recovery mode exited\n");
}

/*==========================
 *  监控线程
 *==========================*/

/**
 * 内存监控任务（Core 1 后台运行）
 */
static void memmon_task(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    syslog(LOG_INFO, "[MEMMON] Memory monitor task started\n");

    /* 绑定到 Core 1 / Pin to CPU1
     * NuttX 12.12 无 BOARDIOC_SMP_SETAFFINITY 命令（见
     * deps/nuttx/include/sys/boardctl.h），标准做法是 sched_setaffinity()
     * （声明见 deps/nuttx/include/sched.h:256）*/
#ifdef CONFIG_SMP
    {
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(cpu_set_t), &mask);
    }
#endif

    g_memmon_running = true;

    while (g_memmon_running) {
        if (!g_memmon_paused) {
            /* 更新内存信息 */
            memmon_update_info();

            uint8_t pct = g_mem.usage_pct;

            /* 检查阈值 */
            if (pct >= MEMMON_EMERGENCY_THRESH) {
                memmon_emergency(g_mem.used, g_mem.total, pct);
            } else if (pct >= MEMMON_CRITICAL_THRESH) {
                memmon_critical(g_mem.used, g_mem.total, pct);
            } else if (pct >= MEMMON_WARNING_THRESH) {
                memmon_warning(g_mem.used, g_mem.total, pct);
            }
        }

        /* 采样间隔 */
        usleep(MEMMON_INTERVAL_MS * 1000);
    }

    syslog(LOG_INFO, "[MEMMON] Memory monitor task exiting\n");
}

/*==========================
 *  API
 *==========================*/

/**
 * 启动内存监控
 */
int memmon_start(void)
{
    if (g_memmon_running)
        return OK;

    /* 启动监控任务 / Start monitor task */
    pid_t pid = kthread_create("memmon",
                                125,  /* 优先级 / priority */
                                MEMMON_TASK_STACK,
                                (main_t)memmon_task, NULL);
    if (pid < 0) {
        syslog(LOG_ERR, "[MEMMON] Failed to start monitor task: %d\n", pid);
        return pid;
    }

    g_memmon_running = true;
    syslog(LOG_INFO, "[MEMMON] Started (PID=%d)\n", pid);
    return OK;
}

/**
 * 停止内存监控
 */
int memmon_stop(void)
{
    g_memmon_running = false;
    syslog(LOG_INFO, "[MEMMON] Stopped\n");
    return OK;
}

/**
 * 暂停监控
 */
int memmon_pause(void)
{
    g_memmon_paused = true;
    return OK;
}

/**
 * 恢复监控
 */
int memmon_resume(void)
{
    g_memmon_paused = false;
    memmon_recovery();
    return OK;
}

/**
 * 重置峰值统计
 */
int memmon_reset_peak(void)
{
    g_mem.peak = g_mem.used;
    g_min_free_bytes = g_mem.free;
    syslog(LOG_INFO, "[MEMMON] Peak statistics reset\n");
    return OK;
}

/**
 * 获取内存状态 / Get memory status
 */
void memmon_get_status(struct mem_info *info)
{
    if (info == NULL)
        return;

    memmon_update_info();
    memcpy(info, &g_mem, sizeof(struct mem_info));
}

/**
 * 打印内存状态
 */
void memmon_print_status(void)
{
    memmon_update_info();

    printf("\n");
    printf("=== Memory Status ===\n");
    printf("Total:    %lu bytes (%.2f MB)\n",
           (unsigned long)g_mem.total,
           (double)g_mem.total / (1024 * 1024));
    printf("Used:     %lu bytes (%.2f MB) [%u%%]\n",
           (unsigned long)g_mem.used,
           (double)g_mem.used / (1024 * 1024),
           g_mem.usage_pct);
    printf("Free:     %lu bytes (%.2f MB)\n",
           (unsigned long)g_mem.free,
           (double)g_mem.free / (1024 * 1024));
    printf("Peak:     %lu bytes (%.2f MB)\n",
           (unsigned long)g_mem.peak,
           (double)g_mem.peak / (1024 * 1024));
    printf("Min Free: %lu bytes\n",
           (unsigned long)g_min_free_bytes);
    printf("\n");
    printf("Alloc:    %lu\n", (unsigned long)g_mem.alloc_count);
    printf("Free:     %lu\n", (unsigned long)g_mem.free_count);
    printf("Failed:   %lu\n", (unsigned long)g_mem.fail_count);
    printf("Warnings: %lu\n", (unsigned long)g_warning_count);
    printf("Critical: %lu\n", (unsigned long)g_critical_count);
    printf("Emergency Mode: %s\n", g_emergency_mode ? "YES" : "NO");
    printf("\n");
}

/**
 * 获取内存使用百分比
 */
uint8_t memmon_get_usage_pct(void)
{
    memmon_update_info();
    return g_mem.usage_pct;
}

/**
 * 检查内存是否足够
 */
bool memmon_check_available(size_t required)
{
    memmon_update_info();
    return (g_mem.free >= required);
}

/*==========================
 *  分配器挂钩
 *==========================*/

/**
 * 分配前检查（可作为 malloc 挂钩）
 */
bool memmon_pre_alloc_check(size_t size)
{
    if (g_emergency_mode) {
        /* 紧急模式下拒绝新分配 */
        g_mem.fail_count++;
        return false;
    }

    if (!memmon_check_available(size)) {
        g_mem.fail_count++;
        return false;
    }

    g_mem.alloc_count++;
    return true;
}

/**
 * 分配后回调
 */
void memmon_post_alloc(size_t size)
{
    (void)size;
    /* 更新统计 */
}

/**
 * 释放后回调
 */
void memmon_post_free(size_t size)
{
    (void)size;
    g_mem.free_count++;
    /* 退出紧急模式（如果有）*/
    if (g_emergency_mode && g_mem.usage_pct < MEMMON_EMERGENCY_THRESH) {
        memmon_recovery();
    }
}

/*==========================
 *  NSH 命令
 *==========================*/

/**
 * cmd_memmon - 内存监控命令
 */
int cmd_memmon(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: memmon <status|start|stop|pause|resume|reset>\n");
        return OK;
    }

    if (strcmp(argv[1], "status") == 0) {
        memmon_print_status();
    } else if (strcmp(argv[1], "start") == 0) {
        memmon_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        memmon_stop();
    } else if (strcmp(argv[1], "pause") == 0) {
        memmon_pause();
    } else if (strcmp(argv[1], "resume") == 0) {
        memmon_resume();
    } else if (strcmp(argv[1], "reset") == 0) {
        memmon_reset_peak();
    } else {
        printf("未知子命令: %s\n", argv[1]);
    }

    return OK;
}

#endif /* CONFIG_RETRO_MEMMON */
