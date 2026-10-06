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
 * WHERE: retro-ws/src/nuttx/common/apps/system/pkg_manager.[ch]
 * WHEN : 2026-10-04 新增
 * HOW  : .rpk = USTAR tar 容器（512 字节流式解析，适合 ESP32 小内存）：
 *
 *   包名-版本.rpk (ustar)
 *   +-- control          # deb 风格元数据（字段见 rpkg_control_field_t）
 *   +-- manifest         # 载荷清单："<crc32十六进制> <安装路径>" 逐行
 *   +-- preinst|postinst|prerm|postrm   # 维护脚本（NSH 脚本，可选）
 *   +-- data/...         # 载荷根，data/ 之后的部分原样落到安装前缀下
 *
 *   数据库（仿 /var/lib/dpkg）：/opt/var/lib/rpkg/（片上可写分区）
 *   +-- <包名>.control   # 安装时的 control 快照
 *   +-- manifest/<包名>  # 已装文件清单（卸载依据）
 *   +-- info/<包名>.*    # 维护脚本存档（卸载时执行 prerm/postrm）
 */

#ifndef __PKG_MANAGER_H
#define __PKG_MANAGER_H

#include <nuttx/config.h>

/* 安装前缀（默认根）：.rpk 中 data/ 下内容全部落于所选根之下
 * （宿主机测试可用 -DPKG_INSTALL_PREFIX='"..."' 覆盖，默认值不变） */
#ifndef PKG_INSTALL_PREFIX
#  define PKG_INSTALL_PREFIX     "/sdcard"
#endif

/* system 根前缀：control 声明 Root: system 的包（官方系统包）装到片上
 * 可写分区（littlefs，挂载点 /opt——Unix 惯例"附加软件"位，避开语义
 * 为厂商只读系统的 /usr；用户数据同区写 /opt/home，见 HARDWARE 12.4）；
 * 未声明或 Root: sdcard 一律装 PKG_INSTALL_PREFIX（第三方包，默认） */
#ifndef PKG_SYSTEM_PREFIX
#  define PKG_SYSTEM_PREFIX      "/opt"
#endif

/* 包数据库根（仿 /var/lib/dpkg）——放片上可写分区：无 SD 卡时官方
 * 系统包的安装/卸载/列表仍完整可用（SD 卡是可选大容量扩展）；
 * 同样允许测试覆盖 */
#ifndef PKG_DB_ROOT
#  define PKG_DB_ROOT            "/opt/var/lib/rpkg"
#endif

/* ROM 包存储根（XIP 载荷 + 预装数据库，pkg_rom.c 提供；2026-10-06） */
#ifndef PKG_ROM_ROOT
#  define PKG_ROM_ROOT           "/rom/pkg"
#endif

/* ROM 预装层数据库（构建期 gen_pkgdb.py 离线安装产物，随固件镜像
 * 只读分发——2026-10-06 策略修订：编译 ROM 时直接安装到位，首启零
 * 安装动作；片上 PKG_DB_ROOT 为可写覆盖层：后装包 + 预装包卸载墓碑
 * （removed/<包名>）+ 预装包升级覆盖快照） */
#ifndef PKG_ROM_DB_ROOT
#  define PKG_ROM_DB_ROOT        PKG_ROM_ROOT "/db"
#endif

/*
 * WHAT : 枚举"有效已安装"包（两级合并：ROM 预装层 + 片上覆盖层；
 *        墓碑过滤；同名片上优先），桌面注册表与 pkg list 共用
 * 返回 : 枚举包数；cb 返回负数中止并透传
 */
int rpkg_iter_installed(int (*cb)(const char *pkg,
                                  const char *control_path, void *arg),
                        void *arg);

/* .rpk 容器内保留路径（不可作为载荷名） */
#define PKG_PATH_CONTROL       "control"
#define PKG_PATH_MANIFEST      "manifest"
#define PKG_PATH_DATA          "data"

/* 单个 control 字段最大长度 */
#define PKG_FIELD_MAX          256

/* control 支持的字段（deb 子集 + ROM 包存储扩展，2026-10-06） */
enum rpkg_control_field_e {
    PKG_FLD_PACKAGE = 0,    /* Package: 包名（[a-z0-9+-]） */
    PKG_FLD_VERSION,        /* Version: 版本（语义化建议 x.y.z-rev） */
    PKG_FLD_ARCH,           /* Arch: esp32s3 | esp32cam | all */
    PKG_FLD_DEPENDS,        /* Depends: 逗号分隔包名（v1 不比较版本） */
    PKG_FLD_LICENSE,        /* License: SPDX 标识（GPL 包必须声明） */
    PKG_FLD_ROOT,           /* Root: system | sdcard（安装根，缺省 sdcard） */
    PKG_FLD_DESCRIPTION,    /* Description: 一行描述 */
    PKG_FLD_INSTALLED_SIZE, /* Installed-Size: KB 估算 */
    PKG_FLD_MAINTAINER,     /* Maintainer: 维护者 */
    PKG_FLD_XIP,            /* Xip: ROM 直跑载荷清单（逗号分隔，如 bin/editor）*/
    PKG_FLD_TYPE,           /* Type: cli | gui（桌面注册表分类） */
    PKG_FLD_TITLE_ZH,       /* Title-Zh: 中文标题（GUI 应用名） */
    PKG_FLD_TITLE_EN,       /* Title-En: 英文标题 */
    PKG_FLD_ICON,           /* Icon: 图标文本（LVGL symbol / ASCII） */
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
