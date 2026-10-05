/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * startup.c - 自定义启动代码（参考实现，默认不参与编译）
 *
 * WHAT : Xtensa 裸机启动参考：复位向量 -> C 环境 -> os_start()
 * WHY  : 教学留档；NuttX 自带 esp32s3_start.c 已完整处理
 *        Flash/PSRAM/双核引导，本文件若覆盖它会破坏启动——
 *        因此整体收进 CONFIG_RETRO_CUSTOM_STARTUP 守卫（默认未定义）
 * WHO  : 需要定制启动流程的开发者显式打开该宏后使用
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/chip/startup.c
 * WHEN : 2026-03~04 初版；2026-10-04 修订（修复非法标识符/
 *        汇编约束/重复符号，见 BUILD_FIXES.md）
 * HOW  : _start(naked) 清 BSS/设栈 -> esp32s3_start() ->
 *        __init_array 构造 -> os_start()
 */

#ifdef CONFIG_RETRO_CUSTOM_STARTUP

#include <nuttx/config.h>

#include "esp32s3.h"

/*==========================
 *  外部引用
 *==========================*/

extern uint32_t _text;          /* 代码段起始（原文件此处是西里尔乱码） */
extern uint32_t _etext;         /* 代码段结束 */
extern uint32_t _data;          /* 数据段起始 */
extern uint32_t _edata;         /* 数据段结束 */
extern uint32_t _bss;           /* BSS 段起始 */
extern uint32_t _ebss;          /* BSS 段结束 */
extern uint32_t _heap;          /* 堆起始 */
extern uint32_t _iram_text;     /* IRAM 代码起始 */
extern uint32_t _iram_text_end; /* IRAM 代码结束 */

/*==========================
 *  前向声明
 *==========================*/

void _start(void) __attribute__((naked, noreturn, section(".start")));
void esp32s3_start(void);
void cpu0_boot(void);
void cache_init(void);

/*==========================
 *  ROM 函数（从 Flash 调用）
 *==========================*/

extern void Cache_Flush(void);
extern void Cache_Read_Enable(uint32_t int_mask, uint32_t dram_size,
                              uint32_t irom_size);
extern void Software_Reset(void);
extern void ets_printf(const char *fmt, ...);
extern uint32_t ets_get_apb_freq(void);
extern void ets_set_appcpu_boot_addr(uint32_t addr);

/*==========================
 *  启动栈大小
 *==========================*/

#define STARTUP_STACKSIZE (8 * 1024)  /* 8KB 启动栈 */

static uint8_t g_startup_stack[STARTUP_STACKSIZE]
    __attribute__((aligned(16)));

/*==========================
 *  CPU0 启动
 *==========================*/

/*
 * WHAT : CPU0 复位向量（第一条指令）
 * HOW  : 汇编级：关中断 -> 清 BSS -> 设栈 -> 数据段搬运 ->
 *        cache_init -> esp32s3_start
 */
void IRAM_ATTR _start(void)
{
    /* 1. 关闭中断 */
    __asm__ volatile ("wsr.ps 0x40000" ::: "memory");

    /* 2. 初始化 CPU */
#if defined(CONFIG_XTENSA_HAVE_FPU) || defined(CONFIG_XTENSA_HAVE_FPUIR)
    __asm__ volatile ("wsr.ccount 0" ::: "memory");
#endif

    /* 3. 初始化 BSS 段 */
    __asm__ volatile (
        "movi a2, 0\n"
        "movi a3, _bss\n"
        "movi a4, _ebss\n"
        "bgeu a3, a4, 2f\n"
        "1: s32i.n a2, a3, 0\n"
        "addi a3, a3, 4\n"
        "bltu a3, a4, 1b\n"
        "2:\n"
        ::: "a2", "a3", "a4", "memory"
    );

    /* 4. 设置启动栈指针 */
    __asm__ volatile (
        "movi a1, _estack\n"
        "addi a1, a1, -16\n"
        ::: "a1"
    );

    /* 5. 初始化数据段（从 Flash 复制到 RAM）*/
    __asm__ volatile (
        "movi a2, _data\n"
        "movi a3, _edata\n"
        "movi a4, _etext\n"
        "bgeu a2, a3, 2f\n"
        "1: l32i.n a5, a4, 0\n"
        "s32i.n a5, a2, 0\n"
        "addi a2, a2, 4\n"
        "addi a4, a4, 4\n"
        "bltu a2, a3, 1b\n"
        "2:\n"
        ::: "a2", "a3", "a4", "memory"
    );

    /* 6. 初始化 Cache */
    cache_init();

    /* 7. 跳转到 C 启动代码 */
    esp32s3_start();

    /* 不应返回 */
    while (1)
        ;
}

/*
 * WHAT : 初始化 I/D Cache
 * HOW  : ROM 函数刷新后启用全部 bank
 */
void cache_init(void)
{
#ifdef CONFIG_ESP32S3_HAS_CACHE
    Cache_Flush();
    Cache_Read_Enable(0x1F, 0x0F, 0x0F);
#endif
}

/*==========================
 *  C 启动函数
 *==========================*/

/*
 * WHAT : C 代码入口（构造自旋 + os_start）
 * HOW  : __init_array 区间按函数指针逐个调用；
 *        注意：正式产品路径中构造函数由 NuttX 调度，此处仅参考
 */
void IRAM_ATTR esp32s3_start(void)
{
    /* 调用全局构造函数（区间是函数指针数组） */
    extern const void __init_array_start[];
    extern const void __init_array_end[];
    const void **fn;

    for (fn = __init_array_start; fn < __init_array_end; fn++)
        ((void (*)(void))(*fn))();

    /* 打印启动信息 */
    ets_printf("\n");
    ets_printf("=== ESP32-S3 Retro Workstation ===\n");
    ets_printf("NuttX Kernel Starting...\n");
    ets_printf("CPU: Xtensa LX7 @ 240MHz\n");
    ets_printf("SPI Flash: 16MB\n");
    ets_printf("Boot: %s\n", "NuttX");

    /* 启动 NuttX 内核 */
    {
        extern int os_start(void);
        os_start();
    }

    /* 不应返回 */
    while (1) {
        __asm__ volatile ("waiti 0");
    }
}

/*==========================
 *  CPU1 启动
 *==========================*/

/*
 * WHAT : 启动 CPU1（APP CPU）
 * HOW  : 设 APPCPU 启动地址后等待其置位
 */
void IRAM_ATTR cpu1_start(void)
{
    extern uint32_t _cpu1_reset_vector;

    ets_set_appcpu_boot_addr((uint32_t)&_cpu1_reset_vector);

    while ((getreg32(ESP32S3_SYSTEM_CORE_BOOT_NUM_REG) & 0x02) == 0)
        ;

    ets_printf("CPU1 started\n");
}

/*
 * WHAT : CPU1 主循环
 * HOW  : CONFIG_SMP 下交 NuttX 接管调度
 */
void IRAM_ATTR cpu1_main(void)
{
    ets_printf("CPU1: initializing...\n");

#ifdef CONFIG_SMP
    {
        extern void up_cpu1Initialize(void);
        up_cpu1Initialize();
    }
#endif

    while (1) {
        __asm__ volatile ("waiti 0");
    }
}

/*==========================
 *  中断和异常向量（参考实现）
 *==========================*/

void _DefaultHandler(void);
void _KernelExceptionHandler(void);
void _NMIHandler(void);

/* 中断向量表 */
void IRAM_ATTR *g_interrupt_vector[256] = {
    [0 ... 255] = _DefaultHandler,
};

/*
 * WHAT : 挂接中断（参考实现，命名避开 NuttX 的 up_attach_interrupt）
 * HOW  : 索引检查后写向量表
 */
void retro_attach_interrupt(int irq, void (*handler)(void))
{
    if (irq >= 0 && irq < 256) {
        g_interrupt_vector[irq] = handler;
    }
}

void IRAM_ATTR _DefaultHandler(void)
{
    uint32_t epc;

    /* 约束用 %0：由编译器选寄存器（原写死 a0 与约束不符，读出乱值） */
    __asm__ volatile ("rsr.epc %0" : "=r"(epc));

    ets_printf("\n!!! Unexpected interrupt/exception !!!\n");
    ets_printf("EPC: 0x%08x\n", epc);

    while (1)
        ;
}

void IRAM_ATTR _KernelExceptionHandler(void)
{
    ets_printf("\n!!! Kernel Exception !!!\n");
    while (1)
        ;
}

void IRAM_ATTR _NMIHandler(void)
{
    ets_printf("\n!!! NMI !!!\n");
    while (1)
    ;
}

#endif /* CONFIG_RETRO_CUSTOM_STARTUP */
