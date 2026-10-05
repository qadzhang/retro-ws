/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * pkg_manager.h - .rpk 包管理器接口 / retro package manager API
 *
 * WHAT : Debian deb 风格的包管理系统（.rpk 格式）对外接口与格式定义
 * WHY  : 软件安装不能是"拷一个 ELF 完事"——需要元数据、依赖检查、
 *        维护脚本、安装数据库，支持安装/卸载/查询（参考 /var/lib/dpkg）
 * WHO  : NSH `pkg` 命令（nsh_cmds.c）与后续 GUI 软件中心
 * WHERE: esp32-retro-ws/src/nuttx/common/apps/system/pkg_manager.[ch]
 * WHEN : 2026-10-04 新增
 * HOW  : .rpk = USTAR tar 容器（512 字节流式解析，适合 ESP32 小内存）：
 *
 *   包名-版本.rpk (ustar)
 *   +-- control          # deb 风格元数据（字段见 rpkg_control_field_t）
 *   +-- manifest         # 载荷清单："<crc32十六进制> <安装路径>" 逐行
 *   +-- preinst|postinst|prerm|postrm   # 维护脚本（NSH 脚本，可选）
 *   +-- data/...         # 载荷根，data/ 之后的部分原样落到安装前缀下
 *
 *   数据库（仿 /var/lib/dpkg）：/sdcard/var/lib/rpkg/
 *   +-- <包名>.control   # 安装时的 control 快照
 *   +-- manifest/<包名>  # 已装文件清单（卸载依据）
 *   +-- info/<包名>.*    # 维护脚本存档（卸载时执行 prerm/postrm）
 */

#ifndef __PKG_MANAGER_H
#define __PKG_MANAGER_H

#include <nuttx/config.h>

/* 安装前缀：.rpk 中 data/ 下内容全部落于此前缀之下
 * （宿主机测试可用 -DPKG_INSTALL_PREFIX='"..."' 覆盖，默认值不变） */
#ifndef PKG_INSTALL_PREFIX
#  define PKG_INSTALL_PREFIX     "/sdcard"
#endif

/* 包数据库根（仿 /var/lib/dpkg；同样允许测试覆盖） */
#ifndef PKG_DB_ROOT
#  define PKG_DB_ROOT            "/sdcard/var/lib/rpkg"
#endif

/* .rpk 容器内保留路径（不可作为载荷名） */
#define PKG_PATH_CONTROL       "control"
#define PKG_PATH_MANIFEST      "manifest"
#define PKG_PATH_DATA          "data"

/* 单个 control 字段最大长度 */
#define PKG_FIELD_MAX          256

/* control 支持的字段（deb 子集） */
enum rpkg_control_field_e {
    PKG_FLD_PACKAGE = 0,    /* Package: 包名（[a-z0-9+-]） */
    PKG_FLD_VERSION,        /* Version: 版本（语义化建议 x.y.z-rev） */
    PKG_FLD_ARCH,           /* Arch: esp32s3 | esp32cam | all */
    PKG_FLD_DEPENDS,        /* Depends: 逗号分隔包名（v1 不比较版本） */
    PKG_FLD_LICENSE,        /* License: SPDX 标识（GPL 包必须声明） */
    PKG_FLD_DESCRIPTION,    /* Description: 一行描述 */
    PKG_FLD_INSTALLED_SIZE, /* Installed-Size: KB 估算 */
    PKG_FLD_MAINTAINER,     /* Maintainer: 维护者 */
    PKG_FLD_COUNT
};

/* 解析后的 control 字段集 */
struct rpkg_control_s {
    char field[PKG_FLD_COUNT][PKG_FIELD_MAX];
};

/* 包管理器操作结果 */
int rpkg_install(const char *rpk_path);
int rpkg_remove(const char *pkg_name);
int rpkg_list(void);
int rpkg_info(const char *pkg_name);

/* 内部工具（cmd 层直接复用的查询） */
int rpkg_is_installed(const char *pkg_name);
int rpkg_db_path(char *buf, int buflen, const char *fmt, ...);

#endif /* __PKG_MANAGER_H */
