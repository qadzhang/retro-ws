/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * pkg_rom.h - ROM 包存储接口 / ROM package store API
 *
 * WHAT : ROMFS 包存储（pkg_romfs.c 镜像 -> /rom/pkg）的挂载、树内
 *        直查（零拷贝指针）、包枚举与 rommod XIP 寻址接线
 * WHY  : 应用/系统分离（2026-10-06）：各板默认应用名单在构建期打成
 *        .rpk 入固件 ROM；系统第一次运行时自动安装到包安装路径，
 *        .rmo 载荷常驻 Flash 原址执行（rommod XIP）
 * WHO  : retro_boot（挂载+XIP 接线）、pkg_manager（ROM 预装层读取）、
 *        desktop.c（GUI 应用注册表直读）、rommod（XIP 载荷寻址）
 * WHERE: retro-ws/src/nuttx/common/pkg_rom.[ch]
 * WHEN : 2026-10-06 新增；同日策略修订（构建期离线安装替代首启 seed）
 * HOW  : 镜像 = tools/mkromfs.py --tree 产物（目录条目 rf_info=首孩子、
 *        rf_next=父层下一兄弟）；直查按路径分量逐层下降（返回镜像内
 *        Flash 指针）；db/ 预装数据库由 gen_pkgdb.py 生成
 */

#ifndef __PKG_ROM_H
#define __PKG_ROM_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

/*==========================
 *  初始化与挂载
 *==========================*/

/*
 * WHAT : 绑定 ROMFS 镜像（固件传 g_pkg_romfs；宿主测试传自制镜像）
 * 返回 : OK / -EINVAL（非法镜像）
 */
int retro_pkg_rom_init(const uint8_t *image, size_t len);

/*
 * WHAT : 注册 /dev/rom1 块设备并挂载到 /rom/pkg（幂等； NuttX 侧）
 * WHY  : 让 .rpk 经普通 open()/opendir() 亦可访问（pkg install 用路径）
 */
int retro_pkg_rom_mount(void);

/*
 * WHAT : 把 pkg 存储直查注册为 rommod 的 XIP 载荷提供方
 * WHY  : rommod_load("editor") 经此取 /rom/pkg/bin/editor 的 Flash
 *        只读指针（宿主测试可另行 rommod_set_xip_provider 覆盖）
 */
int retro_pkg_rom_xip_register(void);

/*==========================
 *  树内直查（零拷贝）
 *==========================*/

/*
 * WHAT : 按相对路径取文件数据指针（如 "bin/editor"、"a.rpk"）
 * 返回 : OK 且 data/len 指向镜像内 Flash；-ENOENT 未找到
 */
int retro_pkg_rom_find(const char *relpath, const uint8_t **data,
                       size_t *len);

/*
 * WHAT : 枚举根层文件（callback 形式；seed 与桌面注册表共用）
 * HOW  : 每个根层文件调用 cb(name, data, len, arg)；cb 返回负数即中止
 * 返回 : 枚举文件数或中止时的负数错误码
 */
int retro_pkg_rom_scan(int (*cb)(const char *name, const uint8_t *data,
                                 size_t len, void *arg), void *arg);

/*
 * WHAT : 从 ROM 内 .rpk 容器直读 control 字段（免安装查询）
 * WHY  : 桌面注册表在包 DB 不可用（片上 /opt 未挂）时的回退数据源
 * 返回 : OK 且 buf 填入字段值（NUL 结尾）；-ENOENT 字段/文件缺失
 */
int retro_pkg_rom_control(const char *rpk_name, const char *field,
                          char *buf, int buflen);

/*==========================
 *  构建期离线安装（2026-10-06 策略修订）
 *==========================*/

/*
 * WHAT : 预装数据库位于镜像 db/（PKG_ROM_DB_ROOT = /rom/pkg/db）
 * WHY  : 名单包在编译期经 tools/gen_pkgdb.py 直接安装到位——control
 *        快照 + manifest（Xip 载荷最终 CRC）随固件只读分发；运行期
 *        pkg_manager 以"ROM 预装层 + 片上覆盖层（墓碑/后装）"两级
 *        读取（详见 pkg_manager.h），首启零安装动作
 */

#endif /* __PKG_ROM_H */
