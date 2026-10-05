/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * drv_cvbs.h - CVBS 显示驱动统一接口 / unified CVBS display driver API
 *
 * WHAT : 面向 LVGL lv_port_disp 的显示驱动接口（两块板同一套符号）
 * WHY  : 原先 S3/CAM 各自实现且符号不一致（cvbs_init vs drv_cvbs_init），
 *        lv_port_disp.c 引用的接口在两份驱动里都不存在——统一后
 *        一份头文件 + 可移植实现 + 板级 weak 钩子
 * WHO  : lv_port_disp.c（LVGL flush）、esp32*_retro.c（启动）
 * WHERE: retro-ws/src/nuttx/common/driver/drv_cvbs.[ch]
 * WHEN : 2026-10-04 新增（补齐缺失代码，见 BUILD_FIXES.md）
 * HOW  : 帧缓冲/调色板/PAL 场时序全部来自 cvbs_core；
 *        每行波形经 drv_cvbs_emit_line()（weak 钩子）送往硬件——
 *        S3: I2S+GDMA 推电阻网络；CAM: I2S0 DAC(GPIO25)；
 *        宿主机模拟器提供自己的强符号接文件/解码器
 */

#ifndef __DRV_CVBS_H
#define __DRV_CVBS_H

#include <nuttx/config.h>
#include <stdint.h>
#include <stddef.h>

/* 初始化（分配帧缓冲、复位调色板、启动输出） */
int  drv_cvbs_init(void);
void drv_cvbs_deinit(void);

/* 帧缓冲访问（LVGL I8 8bit 索引色帧缓冲） */
uint8_t *drv_cvbs_get_fb(void);
int  drv_cvbs_get_width(void);
int  drv_cvbs_get_height(void);

/* LVGL flush：把渲染好的像素拷入帧缓冲（部分刷新可多次调用） */
void drv_cvbs_send(const uint8_t *px_map, size_t size);

/*
 * 输出一整帧（240p 同相双场，625 行周期）。
 * 设备上由刷新任务/垂直同步调用；模拟器直接调用采集波形
 */
void drv_cvbs_frame(void);

/*
 * 板级硬件钩子：把一行 864 样本（13.5MHz 字节流）推给 DAC/I2S。
 * common 的 drv_cvbs.c 提供 weak 空实现（未接硬件时安全），
 * S3/CAM 驱动文件提供强符号覆盖
 */
void drv_cvbs_emit_line(const uint8_t *line, size_t len, int line_no);

#endif /* __DRV_CVBS_H */
