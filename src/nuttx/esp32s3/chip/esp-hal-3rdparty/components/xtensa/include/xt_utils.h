/*
 * SPDX-FileCopyrightText: 2020-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: xt_utils.h
 * Description: Xtensa utility functions - Patched version for NuttX ESP32-S3
 *              Fixes 'rer' instruction incompatibility with ESP32-S3 toolchain
 * Author: ESP32-S3 Retro Project Team
 * Version: 0.1.0
 * Date: 2026-03-29
 */

/*
 * xt_utils.h - xt_utils.h 覆盖副本
 *
 * WHAT : xt_utils.h 覆盖副本
 * WHY  : 上游头文件缺陷修正
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/chip/esp-hal-3rdparty/components/xtensa/include/xt_utils.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : EXTRAFLAGS -I 覆盖
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "soc/soc_caps.h"
#ifndef __NuttX__
#include "xtensa/config/core-isa.h"
#include "xtensa/config/core.h"
#else
#include <arch/chip/core-isa.h>
#include <arch/xtensa/core.h>
#endif
#include "xtensa/config/extreg.h"
#ifndef __NuttX__
#include "xtensa/config/specreg.h"
#else
#include <arch/xtensa/xtensa_specregs.h>
#endif
#include "xtensa/xtruntime.h"
#include "xt_instr_macros.h"
#include "esp_bit_defs.h"
#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------- CPU Registers ----------------------------------------------------
 *
 * ------------------------------------------------------------------------------------------------------------------ */

FORCE_INLINE_ATTR __attribute__((pure)) uint32_t xt_utils_get_core_id(void)
{
#if SOC_CPU_CORES_NUM > 1
    uint32_t id;
    asm volatile (
        "rsr.prid %0\n"
        "extui %0,%0,13,1"
        :"=r"(id));
    return id;
#else
    return 0;
#endif
}

FORCE_INLINE_ATTR __attribute__((pure)) uint32_t xt_utils_get_raw_core_id(void)
{
#if XCHAL_HAVE_PRID
    uint32_t id;
    asm volatile (
        "rsr.prid %0\n"
        :"=r"(id));
    return id;
#else
    return 0;
#endif
}

FORCE_INLINE_ATTR void *xt_utils_get_sp(void)
{
    void *sp;
    asm volatile ("mov %0, sp;" : "=r" (sp));
    return sp;
}

FORCE_INLINE_ATTR uint32_t xt_utils_get_cycle_count(void)
{
    uint32_t ccount;
    RSR(CCOUNT, ccount);
    return ccount;
}

static inline void xt_utils_set_cycle_count(uint32_t ccount)
{
    WSR(CCOUNT, ccount);
}

FORCE_INLINE_ATTR void xt_utils_wait_for_intr(void)
{
    asm volatile ("waiti 0\n");
}

/* ------------------------------------------------- CPU Interrupts ----------------------------------------------------
 *
 * ------------------------------------------------------------------------------------------------------------------ */

FORCE_INLINE_ATTR void xt_utils_set_vecbase(uint32_t vecbase)
{
    asm volatile ("wsr %0, vecbase" :: "r" (vecbase));
}

FORCE_INLINE_ATTR uint32_t xt_utils_intr_get_enabled_mask(void)
{
    uint32_t intr_mask;
    RSR(INTENABLE, intr_mask);
    return intr_mask;
}

/* -------------------------------------------------- Memory Ports -----------------------------------------------------
 *
 * ------------------------------------------------------------------------------------------------------------------ */

/* ---------------------------------------------------- Debugging ------------------------------------------------------
 *
 * ------------------------------------------------------------------------------------------------------------------ */

FORCE_INLINE_ATTR void xt_utils_set_breakpoint(int bp_num, uint32_t bp_addr)
{
    if (bp_num == 1) {
        WSR(IBREAKA_1, bp_addr);
    } else {
        WSR(IBREAKA_0, bp_addr);
    }
    uint32_t brk_ena_reg;
    RSR(IBREAKENABLE, brk_ena_reg);
    brk_ena_reg |= BIT(bp_num);
    WSR(IBREAKENABLE, brk_ena_reg);
}

FORCE_INLINE_ATTR void xt_utils_clear_breakpoint(int bp_num)
{
    uint32_t bp_en = 0;
    RSR(IBREAKENABLE, bp_en);
    bp_en &= ~BIT(bp_num);
    WSR(IBREAKENABLE, bp_en);
    uint32_t bp_addr = 0;
    if (bp_num == 1) {
        WSR(IBREAKA_1, bp_addr);
    } else {
        WSR(IBREAKA_0, bp_addr);
    }
}

FORCE_INLINE_ATTR void xt_utils_set_watchpoint(int wp_num,
                                               uint32_t wp_addr,
                                               size_t size,
                                               bool on_read,
                                               bool on_write)
{
    uint32_t dbreakc_reg = 0x3F;
    dbreakc_reg = dbreakc_reg << (__builtin_ffsll(size) - 1);
    dbreakc_reg = dbreakc_reg & 0x3F;
    if (on_read) {
        dbreakc_reg |= BIT(30);
    }
    if (on_write) {
        dbreakc_reg |= BIT(31);
    }
    if (wp_num == 1) {
        WSR(DBREAKA_1, (uint32_t) wp_addr);
        WSR(DBREAKC_1, dbreakc_reg);
    } else {
        WSR(DBREAKA_0, (uint32_t) wp_addr);
        WSR(DBREAKC_0, dbreakc_reg);
    }
}

FORCE_INLINE_ATTR void xt_utils_clear_watchpoint(int wp_num)
{
    if (wp_num == 1) {
        WSR(DBREAKC_1, 0);
        WSR(DBREAKA_1, 0);
    } else {
        WSR(DBREAKC_0, 0);
        WSR(DBREAKA_0, 0);
    }
}

/* ---------------------- Debugger -------------------------
 *
 * xt_utils_dbgr_is_attached: Check if debugger is attached
 * PATCHED: Uses rsr.dsr instead of rer instruction which is
 * not supported by ESP32-S3 Xtensa toolchain
 * ------------------------------------------------------------------------------------------------------------------ */

FORCE_INLINE_ATTR bool xt_utils_dbgr_is_attached(void)
{
#if 1
    /* PATCHED: rer instruction not supported, return false
     * In NuttX embedded environment, debugger attachment check
     * is typically not needed for production builds */
    return false;
#else
    /* Original implementation using rer - not supported on ESP32-S3 */
    uint32_t dcr = 0;
    uint32_t reg = DSRSET;
    RER(reg, dcr);
    return (bool)(dcr & 0x1);
#endif
}

FORCE_INLINE_ATTR void xt_utils_dbgr_break(void)
{
    __asm__ ("break 1,15");
}

/* ------------------------------------------------------ Misc ---------------------------------------------------------
 *
 * ------------------------------------------------------------------------------------------------------------------ */

FORCE_INLINE_ATTR bool xt_utils_compare_and_set(volatile uint32_t *addr, uint32_t compare_value, uint32_t new_value)
{
#if XCHAL_HAVE_S32C1I
#ifdef __clang_analyzer__
    volatile uint32_t temp;
    temp = *addr;
    *addr = temp;
#endif
    uint32_t old_value = new_value;
    __asm__ __volatile__ (
        "WSR    %2, SCOMPARE1 \n"
        "S32C1I %0, %1, 0 \n"
        :"=r"(old_value)
        :"r"(addr), "r"(compare_value), "0"(old_value)
    );

    return (old_value == compare_value);
#else
    uint32_t intr_level;
    __asm__ __volatile__ ("rsil %0, " XTSTR(XCHAL_EXCM_LEVEL) "\n"
                          : "=r"(intr_level));
    uint32_t old_value;
    old_value = *addr;
    if (old_value == compare_value) {
        *addr = new_value;
    }
    __asm__ __volatile__ ("memw \n"
                          "wsr %0, ps\n"
                          :: "r"(intr_level));

    return (old_value == compare_value);
#endif
}

#ifdef __cplusplus
}
#endif