/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * hardware/pio_instructions.h - Pico-PIO-USB 的 NuttX 垫片
 *
 * WHAT : 上游用到的 PIO 指令编码原语（jmp/set/sideset）与主操作码
 *        判定位——按 RP2040 datasheet 3.4 指令编码实现（与 pico-sdk
 *        pio_instructions.h 同值）
 * WHEN : 2026-10-05 新增
 */

#ifndef __PIOUSB_PORT_HW_PIO_INSTRUCTIONS_H
#define __PIOUSB_PORT_HW_PIO_INSTRUCTIONS_H

#include <stdint.h>

/* 指令 [14:13] = 主操作码判定域 */
enum pio_instr_bits
{
    pio_instr_bits_jmp = 0x0000u,
    pio_instr_bits_wait = 0x2000u,
    pio_instr_bits_in = 0x4000u,
    pio_instr_bits_out = 0x6000u,
    pio_instr_bits_push = 0x8000u,
    pio_instr_bits_pull = 0x8000u,
    pio_instr_bits_mov = 0xA000u,
    pio_instr_bits_irq = 0xC000u,
    pio_instr_bits_set = 0xE000u,
};

/* SET 目的编码（datasheet 3.4.7） */
enum pio_src_dest
{
    pio_pins = 0,
    pio_x = 1,
    pio_y = 2,
    pio_null = 3,
    pio_pindirs = 5,
    pio_exec_mov = 6,
    pio_status = 7,
};

static inline uint32_t _pio_major_instr_bits(uint32_t instr)
{
    return instr & 0xE000u;
}

/* MOV 源/目的编码（datasheet 3.4.6） */
enum pio_mov_dest
{
    pio_mov_pins = 0,
    pio_mov_x = 1,
    pio_mov_y = 2,
    pio_mov_exec = 4,     /* 别名 pio_exec_mov 已列于 src_dest */
    pio_mov_pindirs = 6,  /* RP2350，RP2040 保留 */
    pio_mov_status = 7,
};

/* MOV 的源域编码（含取反/位反修饰：bits[7:5] op） */
enum pio_mov_src
{
    pio_mov_src_pins = 0,
    pio_mov_src_x = 1,
    pio_mov_src_y = 2,
    pio_mov_src_null = 3,
    pio_mov_src_isr = 4,   /* pio_osr 的 mov 别名见下 */
    pio_mov_src_exec = 5,
    pio_mov_src_status = 6,
    pio_mov_src_isr_inv = 7,
};

/* pico-sdk 常用源/目的别名 */
#define pio_isr 4
#define pio_osr 5
#define pio_mov_op_none  0u
#define pio_mov_op_invert (1u << 5)   /* 取反，作用在源索引上 */
#define pio_mov_op_bitrev (1u << 6)

/* MOV：101 dest(3) op(2) src(3) delay(5)——pico-sdk mov_not = op 取反 */
static inline uint32_t pio_encode_mov_not(uint32_t dest, uint32_t src)
{
    return pio_instr_bits_mov | ((dest & 7u) << 5) | (1u << 3) |
           ((src & 7u) << 0);
}

/* JMP：000 cond(3) delay(5) addr(5) —— 上游只用无条件跳转 */
static inline uint32_t pio_encode_jmp(uint32_t addr)
{
    return pio_instr_bits_jmp | ((addr & 0x1fu));
}

/* SET：111 dest(3) delay(5) data(5) */
static inline uint32_t pio_encode_set(enum pio_src_dest dest, uint32_t value)
{
    return pio_instr_bits_set | ((dest & 7u) << 5) | (value & 0x1fu);
}

/* SIDSET 位域（非指令）：n 位 sideset 占 delay/sideset 域 [12:8] 高位，
 * pico-sdk 约定 pio_encode_sideset(bits, value) = value << (13 - bits) */
static inline uint32_t pio_encode_sideset(uint32_t bits, uint32_t value)
{
    return (value & ((1u << bits) - 1u)) << (13u - bits);
}

#endif /* __PIOUSB_PORT_HW_PIO_INSTRUCTIONS_H */
