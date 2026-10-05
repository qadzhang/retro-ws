/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * nano_port/config.h - GNU nano 8.4 NuttX 移植配置
 *
 * WHAT : autoconf config.h 的手工等价物（NuttX 无 autotools）
 * WHY  : deps/nano 上游源码不改（AGENTS.md 11.5），全部适配经
 *        本目录 -I 优先注入
 * WHO  : deps/nano/src/definitions.h (#include <config.h>)
 * WHERE: esp32-retro-ws/src/nuttx/common/nano_port/config.h
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : autoconf 惯例——只定义"开启"的 HAVE_/ENABLE_ 宏，
 *        关闭的功能一律【不定义】（nano 用 #ifdef 判定，
 *        #define X 0 也会命中）；裁剪：保 UTF-8/帮助/浏览器/
 *        行号/多缓冲/软换行，去色彩（AV 单色）/鼠标/fork 系
 *        speller/libmagic/NLS
 */

#ifndef NANO_PORT_CONFIG_H
#define NANO_PORT_CONFIG_H

#define PACKAGE         "nano"
#define PACKAGE_NAME    "GNU nano"
#define PACKAGE_STRING  "GNU nano 8.4"
#define VERSION         "8.4"
#define LOCALEDIR       "/rom"
#define SYSCONFDIR      "/etc"

/* 正则旗标（上游 configure 探测产物；glibc/NuttX 均为纯 REG_EXTENDED） */
#define NANO_REG_EXTENDED  REG_EXTENDED

/* ---- 开启的功能（编辑体验核心）---- */
#define ENABLE_UTF8         1
#define ENABLE_HELP         1   /* ^G 帮助（nano 身份特征） */
#define ENABLE_BROWSER      1   /* ^R ^T 文件浏览器 */
#define ENABLE_JUSTIFY      1   /* ^J 段落重排 */
#define ENABLE_WRAPPING     1   /* 行内软换行 */
#define ENABLE_LINENUMBERS  1   /* 行号栏 */
#define ENABLE_MULTIBUFFER  1   /* 多文件缓冲 */
#define ENABLE_NANORC       1   /* 配置文件（/etc/nanorc 等） */

/* ---- 关闭的功能：不定义 ----
 * ENABLE_HISTORIES / ENABLE_OPERATINGDIR / ENABLE_SPELLER /
 * ENABLE_COLOR / ENABLE_SYNTAX / ENABLE_COMMENT /
 * ENABLE_WORDCOMPLETION / ENABLE_TABCOMP / ENABLE_FORMATTER /
 * ENABLE_LINTER / ENABLE_EXTRA / ENABLE_MOUSE / ENABLE_NLS
 */

/* ---- 系统能力（NuttX libc 实测齐备；同样只定义存在的）---- */
#define HAVE_TERMIOS_H      1
#define HAVE_PWD_H          1
#define HAVE_LIMITS_H       1
#define HAVE_STRING_H       1
#define HAVE_STRINGS_H      1
#define HAVE_STDINT_H       1
#define HAVE_UNISTD_H       1
#define HAVE_FCNTL_H        1
#define HAVE_TIME_H         1
#define HAVE_LOCALE_H       1
#define HAVE_WCHAR_H        1
#define HAVE_WCTYPE_H       1
#define HAVE_GLOB_H         1
#define HAVE_GETOPT_H       1
#define HAVE_LIBGEN_H       1
#define HAVE_DIRENT_H       1
#define HAVE_REGEX_H        1
#define HAVE_FSYNC          1

/* NuttX libc 缺口补齐（nano_port/compat.c 提供） */
int mkstemps(char *template_name, int suffix_len);

/* NuttX regex 无 REG_STARTEND 扩展：置 0 使标志位无操作，
 * 有界搜索退化为全串匹配（结果由 nano 的边界判定收口） */
#define REG_STARTEND 0

/* 垫片提供的 ncurses 差异项 */
#define HAVE_USE_DEFAULT_COLORS     1
#define HAVE_SET_ESCDELAY           1

/* 不存在：HAVE_FORK / HAVE_WAITPID / HAVE_PIPE / HAVE_LIBMAGIC /
 * HAVE_GETEUID / HAVE_FUNLOCKFILE / HAVE_NCURSES_H …（一律不定义）*/

#endif /* NANO_PORT_CONFIG_H */
