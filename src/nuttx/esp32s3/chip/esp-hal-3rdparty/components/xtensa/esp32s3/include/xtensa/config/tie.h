/*
 * tie.h -- compile-time HAL definitions dependent on CORE & TIE configuration
 *
 * SPDX-FileCopyrightText: 1999-2021 Cadence Design Systems Inc.
 * SPDX-License-Identifier: MIT
 *
 * 此文件是从 esp-hal-3rdparty 同步过来的，用于解决 XCHAL_CP_NUM 定义冲突问题。
 * nuttx 原始的 tie.h 定义 XCHAL_CP_NUM=0，但 ESP32-S3 实际有 2 个协处理器（FPU + cop_ai）。
 * 本文件确保 XCHAL_CP_NUM 正确设置为 2。
 * 
 * 来源: esp-hal-3rdparty components/xtensa/esp32s3/include/xtensa/config/tie.h
 * 修改原因: ESP32-S3 需要 XCHAL_CP_NUM=2
 */

/*
 * tie.h - tie.h 覆盖副本
 *
 * WHAT : tie.h 覆盖副本
 * WHY  : 修正 XCHAL_CP_NUM（BUILD_FIXES.md）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/chip/esp-hal-3rdparty/components/xtensa/esp32s3/include/xtensa/config/tie.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : EXTRAFLAGS -I 覆盖
 */

#ifndef _XTENSA_CORE_TIE_H
#define _XTENSA_CORE_TIE_H

#define XCHAL_CP_NUM			2	/* number of coprocessors */
#define XCHAL_CP_MAX			4	/* max CP ID + 1 (0 if none) */
#define XCHAL_CP_MASK			0x09	/* bitmask of all CPs by ID */
#define XCHAL_CP_PORT_MASK		0x00	/* bitmask of only port CPs */

/*  Basic parameters of each coprocessor:  */
#define XCHAL_CP0_NAME			"FPU"
#define XCHAL_CP0_IDENT			FPU
#define XCHAL_CP0_SA_SIZE		72	/* size of state save area */
#define XCHAL_CP0_SA_ALIGN		4	/* min alignment of save area */
#define XCHAL_CP_ID_FPU             	0	/* coprocessor ID (0..7) */
#define XCHAL_CP3_NAME			"cop_ai"
#define XCHAL_CP3_IDENT			cop_ai
#define XCHAL_CP3_SA_SIZE		208	/* size of state save area */
#define XCHAL_CP3_SA_ALIGN		16	/* min alignment of save area */
#define XCHAL_CP_ID_COP_AI          	3	/* coprocessor ID (0..7) */

/*  Filler info for unassigned coprocessors:  */
#define XCHAL_CP1_SA_SIZE		0
#define XCHAL_CP1_SA_ALIGN		1
#define XCHAL_CP2_SA_SIZE		0
#define XCHAL_CP2_SA_ALIGN		1
#define XCHAL_CP4_SA_SIZE		0
#define XCHAL_CP4_SA_ALIGN		1
#define XCHAL_CP5_SA_SIZE		0
#define XCHAL_CP5_SA_ALIGN		1
#define XCHAL_CP6_SA_SIZE		0
#define XCHAL_CP6_SA_ALIGN		1
#define XCHAL_CP7_SA_SIZE		0
#define XCHAL_CP7_SA_ALIGN		1

/*  Save area for non-coprocessor optional and custom (TIE) state:  */
#define XCHAL_NCP_SA_SIZE		36
#define XCHAL_NCP_SA_ALIGN		4

/*  Total save area:  */
#define XCHAL_TOTAL_SA_SIZE		336
#define XCHAL_TOTAL_SA_ALIGN		16

#define XCHAL_NCP_SA_NUM	9
#define XCHAL_NCP_SA_LIST(s)	\
 XCHAL_SA_REG(s,1,2,1,1,      threadptr, 4, 4, 4,0x03E7,  ur,231, 32,0,0,0) \
 XCHAL_SA_REG(s,1,0,0,1,          acclo, 4, 4, 4,0x0210,  sr,16 , 32,0,0,0) \
 XCHAL_SA_REG(s,1,0,0,1,          acchi, 4, 4, 4,0x0211,  sr,17 ,  8,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             br, 4, 4, 4,0x0204,  sr,4  , 16,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,      scompare1, 4, 4, 4,0x020C,  sr,12 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m0, 4, 4, 4,0x0220,  sr,32 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m1, 4, 4, 4,0x0221,  sr,33 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m2, 4, 4, 4,0x0222,  sr,34 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,0,1,             m3, 4, 4, 4,0x0223,  sr,35 , 32,0,0,0)

#define XCHAL_CP0_SA_NUM	18
#define XCHAL_CP0_SA_LIST(s)	\
 XCHAL_SA_REG(s,0,0,1,0,            fcr, 4, 4, 4,0x03E8,  ur,232, 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,            fsr, 4, 4, 4,0x03E9,  ur,233, 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f0, 4, 4, 4,0x0030,   f,0  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f1, 4, 4, 4,0x0031,   f,1  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f2, 4, 4, 4,0x0032,   f,2  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f3, 4, 4, 4,0x0033,   f,3  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f4, 4, 4, 4,0x0034,   f,4  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f5, 4, 4, 4,0x0035,   f,5  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f6, 4, 4, 4,0x0036,   f,6  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f7, 4, 4, 4,0x0037,   f,7  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f8, 4, 4, 4,0x0038,   f,8  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             f9, 4, 4, 4,0x0039,   f,9  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f10, 4, 4, 4,0x003A,   f,10 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f11, 4, 4, 4,0x003B,   f,11 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f12, 4, 4, 4,0x003C,   f,12 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f13, 4, 4, 4,0x003D,   f,13 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f14, 4, 4, 4,0x003E,   f,14 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,            f15, 4, 4, 4,0x003F,   f,15 , 32,0,0,0)

#define XCHAL_CP1_SA_NUM	0
#define XCHAL_CP1_SA_LIST(s)
#define XCHAL_CP2_SA_NUM	0
#define XCHAL_CP2_SA_LIST(s)

#define XCHAL_CP3_SA_NUM	26
#define XCHAL_CP3_SA_LIST(s)	\
 XCHAL_SA_REG(s,0,0,1,0,         accx_0,16, 4, 4,0x0300,  ur,0  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,         accx_1, 4, 4, 4,0x0301,  ur,1  ,  8,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_h_0, 4, 4, 4,0x0302,  ur,2  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_h_1, 4, 4, 4,0x0303,  ur,3  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_h_2, 4, 4, 4,0x0304,  ur,4  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_h_3, 4, 4, 4,0x0305,  ur,5  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_h_4, 4, 4, 4,0x0306,  ur,6  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_l_0, 4, 4, 4,0x0307,  ur,7  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_l_1, 4, 4, 4,0x0308,  ur,8  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_l_2, 4, 4, 4,0x0309,  ur,9  , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_l_3, 4, 4, 4,0x030A,  ur,10 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       qacc_l_4, 4, 4, 4,0x030B,  ur,11 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,       sar_byte, 4, 4, 4,0x030D,  ur,13 ,  4,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,  fft_bit_width, 4, 4, 4,0x030E,  ur,14 ,  4,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,     ua_state_0, 4, 4, 4,0x030F,  ur,15 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,     ua_state_1, 4, 4, 4,0x0310,  ur,16 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,     ua_state_2, 4, 4, 4,0x0311,  ur,17 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,1,0,     ua_state_3, 4, 4, 4,0x0312,  ur,18 , 32,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q0,16,16,16,0x1008,   q,0  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q1,16,16,16,0x1009,   q,1  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q2,16,16,16,0x100A,   q,2  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q3,16,16,16,0x100B,   q,3  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q4,16,16,16,0x100C,   q,4  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q5,16,16,16,0x100D,   q,5  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q6,16,16,16,0x100E,   q,6  ,128,0,0,0) \
 XCHAL_SA_REG(s,0,0,2,0,             q7,16,16,16,0x100F,   q,7  ,128,0,0,0)

#define XCHAL_CP4_SA_NUM	0
#define XCHAL_CP4_SA_LIST(s)
#define XCHAL_CP5_SA_NUM	0
#define XCHAL_CP5_SA_LIST(s)
#define XCHAL_CP6_SA_NUM	0
#define XCHAL_CP6_SA_LIST(s)
#define XCHAL_CP7_SA_NUM	0
#define XCHAL_CP7_SA_LIST(s)

#define XCHAL_OP0_FORMAT_LENGTHS	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4
#define XCHAL_BYTE0_FORMAT_LENGTHS	\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4,\
	3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4, 3,3,3,3,3,3,3,3,2,2,2,2,2,2,4,4

#endif /*_XTENSA_CORE_TIE_H*/
