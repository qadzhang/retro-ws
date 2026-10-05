/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * nano_port/term.h - terminfo 接口占位（nano 仅用 tigetstr 查键名）
 */

#ifndef NANO_PORT_TERM_H
#define NANO_PORT_TERM_H

/* 无 terminfo 数据库：全部返回"不存在"，nano 自动回退默认键码 */
const char *tigetstr(const char *capname);
const char *tgetstr(const char *id, char **area);
int tigetnum(const char *capname);
int tigetflag(const char *capname);

#endif /* NANO_PORT_TERM_H */
