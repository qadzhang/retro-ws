/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * cvbs_core.h - CVBS 复合视频可移植核心 / portable PAL CVBS core
 *
 * WHAT : 与硬件无关的 CVBS 信号生成核心（调色板/帧缓冲/绘制/
 *        PAL 行与场时序编码）
 * WHY  : S3（I2S+电阻网络）与 CAM（内置 DAC）共用同一套信号语义，
 *        抽出核心后可在宿主机用"解码模拟器"验证时序与图像，
 *        设备层只负责把采样字节以 13.5MHz 推给 DAC/I2S
 * WHO  : drv_cvbs.c(S3)、drv_cvbs_dac.c(CAM)、tests/host CVBS 模拟器
 * WHERE: esp32-retro-ws/src/nuttx/common/driver/cvbs_core.[ch]
 * WHEN : 2026-10-04 新增（修复原两份驱动各自缺垂直同步/时序
 *        常数不一致/flush 逻辑无效的问题，见 BUILD_FIXES.md）
 * HOW  : PAL-B 625 行 50Hz：
 *   - 行周期 64µs = 864 样本（样本率 864*15625 = 13.5MHz）
 *   - 正常行: 前沿1.6µs + 同步4.7µs + 后沿5.1µs + 活跃51.9µs... 本核心
 *     用工程近似 12/64/68/720 样本（retro 设备常见做法）
 *   - 场同步: 每场前置 3 行均衡脉冲 + 3 行宽脉冲 + 3 行均衡脉冲
 *   - 复古 240p 模式: 只重复输出场 1（"288p"），电视按逐行显示
 */

#ifndef __CVBS_CORE_H
#define __CVBS_CORE_H

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*==========================
 *  PAL-B 时序常量（样本单位，13.5MHz 采样）
 *==========================*/

#define CVBS_H_FRONT_PORCH   12      /* 1.6µs 前沿 */
#define CVBS_H_SYNC          64      /* 4.7µs 行同步 */
#define CVBS_H_BACK_PORCH    68      /* 5.0µs 后沿（含色同步槽） */
#define CVBS_H_ACTIVE        720     /* 53.3µs 活跃视频 */
#define CVBS_LINE_TOTAL      (CVBS_H_FRONT_PORCH + CVBS_H_SYNC + \
                              CVBS_H_BACK_PORCH + CVBS_H_ACTIVE) /* 864 */

#define CVBS_FRAME_LINES     625     /* PAL 每帧总行数（两场） */
#define CVBS_FIELD_LINES     312     /* 场 1 行数（半行属场 2） */
#define CVBS_FIELD1_ACTIVE   288     /* 每场有效视频行 */

/* 电平（8bit DAC，BT.601 studio swing 保守近似） */
#define CVBS_LEVEL_SYNC      0
#define CVBS_LEVEL_BLANK     16
#define CVBS_LEVEL_WHITE     235

/* 同步检测门限（模拟器/解码端共用语义：须介于同步 0 与消隐 16 之间） */
#define CVBS_SYNC_THRESHOLD  8

/*==========================
 *  类型
 *==========================*/

/*
 * 行输出槽：核心每编码完一行调用一次
 * WHO : 设备层接 DMA（每行推一次），模拟器接文件/解码器
 */
typedef void (*cvbs_sink_t)(const uint8_t *line, size_t len, int line_no);

/*==========================
 *  调色板与帧缓冲
 *==========================*/

int  cvbs_core_set_palette(const uint8_t *palette, size_t count);
void cvbs_core_palette_get_rgb(uint8_t index, uint8_t *r, uint8_t *g,
                               uint8_t *b);

/*
 * WHAT : 直通亮度模式 / direct-luma mode
 * WHY  : LVGL 9.5 显示器无索引调色板，本机渲染走 L8（灰度）；
 *        帧缓冲字节即亮度值，编码时不再查调色板
 * HOW  : true 时 fb 字节 x8 映射为 CVBS studio swing
 *        Y = 16 + x8*219/255（保证永远高于同步/消隐电平）；
 *        默认 false（调色板模式，兼容既有索引色用户）
 */
void cvbs_core_set_direct_luma(bool on);

int  cvbs_core_fb_alloc(int width, int height);
void cvbs_core_fb_free(void);
uint8_t *cvbs_core_fb(void);
int  cvbs_core_fb_width(void);
int  cvbs_core_fb_height(void);

/*==========================
 *  绘制 API（越界自动裁剪；未分配帧缓冲时为安全空操作）
 *==========================*/

void cvbs_clear(void);
void cvbs_draw_pixel(int x, int y, uint8_t color_index);
void cvbs_draw_hline(int x1, int y, int x2, uint8_t color_index);
void cvbs_draw_vline(int x, int y1, int y2, uint8_t color_index);
void cvbs_draw_rect(int x, int y, int w, int h, uint8_t color_index);
void cvbs_fill_rect(int x, int y, int w, int h, uint8_t color_index);

/*==========================
 *  亮度
 *==========================*/

uint8_t cvbs_rgb_to_y(uint8_t r, uint8_t g, uint8_t b);

/*==========================
 *  行/场编码
 *==========================*/

/*
 * WHAT : 运行期行长布局（每板采样时钟不同，HARDWARE.md 6.2）
 * WHY  : 真外设只能整数分频（S3/CAM/C3=13.3333MHz→853 样本行，
 *        Pico=13.5063MHz→864 样本行）；行时间压进 PAL 容差 ±0.05%
 * HOW  : fb_alloc 之前调用；四段和不得超过编译期上限 CVBS_LINE_TOTAL
 * 返回 : OK / -EINVAL
 */
int cvbs_core_set_line_layout(int front, int sync, int back, int active);

int cvbs_core_line_total(void);

/*
 * WHAT : 编码一行 CVBS（正常行时序）
 * WHAT2: line_out 需至少 CVBS_LINE_TOTAL 字节
 * HOW  : 前沿(消隐) + 行同步(同步电平) + 后沿(消隐) + 活跃视频；
 *        is_active 且 fb_row 非空时活跃区取帧缓冲亮度，否则全消隐
 * 返回 : 编码字节数（恒等于 cvbs_core_line_total()）
 */
size_t cvbs_core_encode_line(uint8_t *out, const uint8_t *fb_row,
                             int row_width);

/* 单行种类（均衡/宽脉冲/消隐/活跃）——逐行 DMA 架构（Pico PIO）用 */
enum cvbs_line_kind {
    CVBS_LINE_EQ,       /* 均衡脉冲行 */
    CVBS_LINE_BROAD,    /* 宽脉冲行（垂直同步） */
    CVBS_LINE_BLANK,    /* 消隐行 */
    CVBS_LINE_ACTIVE,   /* 活跃视频行 */
};

enum cvbs_line_kind cvbs_core_line_kind(int n, bool odd_field,
                                        bool progressive);

/*
 * WHAT : 一场的总行数（偶场 312，奇场 313）
 */
int cvbs_core_field_line_count(bool odd_field, bool progressive);

/*
 * WHAT : 编码场内第 n 行到 out（≥CVBS_LINE_TOTAL 字节）
 * 返回 : 编码字节数；n 超出场行数返回 0
 */
size_t cvbs_core_field_line(int n, bool odd_field, bool progressive,
                            uint8_t *out);

/*
 * WHAT : PAL 场同步区（均衡脉冲 + 宽脉冲 + 均衡脉冲）
 * HOW  : pre_eq_lines/width 均衡脉冲行（同步 2.3µs≈39 样本，双脉冲），
 *        broad_lines 宽脉冲行（同步 27.3µs≈369 样本，双脉冲）
 */
size_t cvbs_core_encode_equalizing(uint8_t *out);
size_t cvbs_core_encode_broad_pulse(uint8_t *out);

/*
 * WHAT : 生成一个完整的 PAL 场（312/313 行）逐行喂给 sink
 * WHY  : 设备层 DMA 与宿主机模拟器共用同一场结构，
 *        保证"模拟器里验证过的时序"就是设备发出的时序
 * HOW  : 场结构（0-based 行号）：
 *        0..2   均衡脉冲（前均衡）
 *        3..5   宽脉冲（垂直同步）
 *        6..8   均衡脉冲（后均衡）
 *        9..23  消隐行
 *        24..311 活跃视频（fb 第 0..287 行，超出显示高度用消隐）
 *        odd_field=true 时加末尾半行（隔行相位，简化为整行消隐）
 *        progressive=true 时跳过（240p 复古模式恒用场 1）
 * 返回 : 总行数
 */
int cvbs_core_generate_field(bool odd_field, bool progressive,
                             cvbs_sink_t sink);

/*
 * WHAT : 生成完整帧（两场，隔行）或单场重复（240p）
 * HOW  : progressive: 同一场 1 连发两次（50Hz 288p）；
 *        interlace: 场 1 + 场 2
 */
int cvbs_core_generate_frame(bool progressive, cvbs_sink_t sink);

#endif /* __CVBS_CORE_H */
