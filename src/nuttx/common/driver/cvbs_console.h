/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * cvbs_console.h - AV 视频字符控制台 / CVBS text console
 *
 * WHAT : 把 UTF-8 文本以 12px 点阵渲染到 CVBS 帧缓冲（全系标配，
 *        含 C3/Pico CLI 档——"字符输出也必须走 AV 视频输出"）
 * WHY  : AGENTS.md 7.3：所有板必须能经 AV 输出显示中文；CLI 档
 *        无 LVGL，由本模块 + lvgl_font_compat 直接驱动字体位图
 * WHO  : retro_boot（横幅/系统消息镜像）、NSH con 命令、后续 syslog
 * WHERE: retro-ws/src/nuttx/common/driver/cvbs_console.[ch]
 * WHEN : 2026-10-04 新增；2026-10-05 ANSI/CSI+状态条；同日晚半格
 *        步进定稿（半角 6px/全角 12px，宋体 9pt 体系）
 * HOW  : UTF-8 解码 -> 半角/全角标点走 Fusion 点阵表
 *        (ascii6/fullwidth)，汉字走 retro_compat Noto 全量 -> 1bpp
 *        位图按 MSB-first 展开到 L8 亮度（0/255）；半格步进 +
 *        全角右半标记位图（\b 连退）；控制序列 \n \r \b \t +
 *        ANSI/CSI；满行滚动（帧缓冲 memmove）
 */

#ifndef __CVBS_CONSOLE_H
#define __CVBS_CONSOLE_H

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 初始化：接管 cvbs_core 帧缓冲（direct luma 模式），清屏打横幅 */
int cvbs_console_init(void);

/* 写一段 UTF-8 文本（len 字节；解码状态跨调用保持） */
int cvbs_console_write(const char *buf, size_t len);

/* 单字符（ASCII 直通；控制字符生效） */
void cvbs_console_putc(char c);

/* 帧缓冲只读访问（宿主测试导出渲染图/状态显示用；w/h 可传 NULL）
 * WHAT : 控制台接管中的 cvbs_core 帧缓冲（direct luma，8bit 灰度） */
const uint8_t *cvbs_console_fb(int *w, int *h);

/* 光标下划线可见性（全屏程序/测试扫描用；默认开） */
void cvbs_console_cursor_visible(bool on);

/*
 * CCDOS 式底部状态条（IME 常驻条，2026-10-05）：
 * on=末行划归状态条（正文区 rows-1 行照常滚动），text 更新条文本
 * （UTF-8，反色显示）；off=释放恢复整屏
 */
void cvbs_console_statusbar(bool on, const char *text);

/* 输入环注入（IME 行确认回放/测试用；同 console read 端的队列） */
void cvbs_console_input_push(char c);
int  cvbs_console_input_pop(char *c);      /* 非阻塞：1=取到 0=空 */

/* 当前列/行（测试与状态显示用） */
int cvbs_console_cols(void);
int cvbs_console_rows(void);
int cvbs_console_cursor_x(void);
int cvbs_console_cursor_y(void);

#ifdef __NuttX__
/*
 * WHAT : 注册 /dev/cvbscon 字符设备 + UART 键盘输入泵
 * WHEN : retro_boot 在 cvbs_console_init() 成功后调用（CLI 档）
 */
int cvbs_console_device_start(const char *input_dev);

/*
 * WHAT : 外部 HID 源（USB/BLE 键盘桥）向 /dev/cvbscon 输入环喂 ASCII 键流
 * WHY  : 输入优先级原则（REQUIREMENTS 2.2.3，2026-10-05）：USB > 蓝牙 >
 *        串口——HID 源与 UART 泵共用同一入环路径（含同一道 IME 门控）
 * WHO  : usb_hid.c（S3 USB 键盘）、未来 BLE HID 桥（NimBLE）
 * 返回 : 实际入环字节数 / -EINVAL
 */
int cvbs_console_feed_keys(const char *buf, size_t len);
#endif

#endif /* __CVBS_CONSOLE_H */
