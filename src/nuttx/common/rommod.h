/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * rommod.h - ROM XIP 应用模块加载器接口 / ROM XIP module loader API
 *
 * WHAT : 把常驻 Flash 的 ET_DYN 应用模块（.rmo，pkg ROMFS 内）加载为
 *        可执行实体：只读段（.text/.rodata）原址执行（XIP，零拷贝），
 *        可写段（.data/.got/.bss）按需入 RAM 并完成重定位
 * WHY  : 应用/系统分离重构（2026-10-06）——应用以 .rpk 包交付、首启
 *        自动安装、代码常驻 ROM 执行不整包入 RAM，最大限度省内存
 *        （C3 400KB SRAM / Pico 264KB SRAM 档受益最大）
 * WHO  : desktop.c（GUI 模块桌面注册表）、cmd_run（CLI 模块）、
 *        宿主测试 test_rommod.c
 * WHERE: retro-ws/src/nuttx/common/rommod.[ch]
 * WHEN : 2026-10-06 新增
 * HOW  : 两种装载形态，同一段代码按包构建档位自动选择：
 *        - 动态档（ARM/RISC-V/宿主）：模块 -fPIC -shared，运行时处理
 *          REL/RELA 重定位（GOT 落 RAM，text 零补丁）
 *        - 静态绑定档（Xtensa）：构建期把外部符号/数据段地址全部烘焙
 *          进模块（重定位数=0），加载器只做段映射 + 数据镜像拷贝
 */

#ifndef __ROMMOD_H
#define __ROMMOD_H

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

/* 模块段数量上限（ET_DYN 产物典型 2~4 个 PT_LOAD） */
#define ROMMOD_MAX_SEGS      8
/* 模块名上限（与 ROMFS 文件名一致，如 "editor"） */
#define ROMMOD_NAME_MAX      32

/* 装载形态（rommod_flavor 查询用） */
#define ROMMOD_FLAVOR_DYNAMIC 1   /* 运行时重定位（ARM/RISC-V/x86-64） */
#define ROMMOD_FLAVOR_STATIC  2   /* 构建期静态绑定，零运行时重定位 */

/* 基础符号表条目（固件导出给模块的函数/变量，构建期生成） */
struct rommod_sym_s {
    const char *name;
    void *addr;
};

/* 已装载模块句柄（内部结构，调用方按不透明指针使用） */
struct rommod_s;

/*==========================
 *  初始化与符号绑定
 *==========================*/

/*
 * WHAT : 登记符号保持器（stage 生成的模块所需符号引用数组）
 * WHY  : 应用抽离后固件自身不再引用部分符号（唯一调用方在 .rmo 模块
 *        里），--gc-sections 会回收 -> 静态绑定 defsym 无址可取。
 *        本登记形成 gc 根链并留运行期诊断句柄
 */
void rommod_set_keep(const void *const *keep);

/*
 * WHAT : 注册固件基础符号表（模块未定义符号的解析来源）
 * WHY  : 动态档模块的 extern 引用（lv_、i18n_、libc 系）在装载时经此表
 *        取地址写入 GOT；静态档不需要（构建期已烘焙），传 NULL 跳过
 * HOW  : tab 按 name 升序排序（二分查找）；n<=0 表示无表
 */
void rommod_bind(const struct rommod_sym_s *tab, int n);

/*==========================
 *  XIP 载荷寻址（宿主测试可替换）
 *==========================*/

/*
 * WHAT : 按模块名取 XIP 载荷基址（"文件在 ROM 镜像内的数据指针"）
 * RETURN: OK 且 data 与 len 给出 Flash 只读指针；-ENOENT 未找到
 */
typedef int (*rommod_xip_lookup_t)(const char *name,
                                   const uint8_t **data, size_t *len);

/*
 * WHAT : 设置 XIP 载荷查找回调（默认实现走 pkg_rom ROMFS 直查）
 * WHY  : 宿主测试注入内存镜像即可端到端验证装载链路，无需块设备
 */
void rommod_set_xip_provider(rommod_xip_lookup_t lookup);

/*==========================
 *  静态绑定档 arena 校验
 *==========================*/

/*
 * WHAT : 注册静态档模块可写段的合法落点区间（arena）
 * WHY  : 静态档模块的 .data/.bss 链接在构建期分配的 arena 固定地址，
 *        加载器拷贝前必须校验目标落在固件登记的 arena 之内（防
 *        固件布局漂移后旧包把数据镜像写到随机 RAM）
 */
void rommod_set_arena(void *base, size_t size);

/*==========================
 *  装载 / 卸载 / 符号
 *==========================*/

/*
 * WHAT : 按模块名装载（XIP provider 寻址 -> rommod_load_from_mem）
 * 返回 : OK 且 *mod 给出句柄（引用计数=1）；负数错误码
 */
int rommod_load(const char *name, struct rommod_s **mod);

/*
 * WHAT : 从内存镜像装载一个 ET_DYN 模块
 * WHY  : mem 即"ROM 视角"的文件基址——目标板上为 flash 只读指针
 *        （零拷贝），宿主测试为 malloc 内存；两种形态按重定位
 *        有无自动分流
 * 返回 : OK / -ENOEXEC（非法 ELF/不支持的机器或重定位）/
 *        -ENOMEM / -EFAULT（静态档地址越出 arena 或布局漂移）
 */
int rommod_load_from_mem(const char *name, const uint8_t *mem,
                         size_t len, struct rommod_s **mod);

/*
 * WHAT : RAM 窗口内联装载（RW 段留在镜像缓冲内，段间相对布局不变）
 * WHY  : 动态档 GOT 访问为 PC 相对（x86-64/ARM/RISC-V 实测），
 *        flash XIP 板的"RO 原址 + RW 搬 RAM"分段放置会打断寻址——
 *        运行时重定位仅在整模块连续可写窗口内成立。宿主测试与
 *        未来 RAM 窗口板使用；五板固件走静态绑定档不经过本入口
 * 返回 : 同 rommod_load_from_mem
 */
int rommod_load_from_mem_inline(const char *name, uint8_t *mem,
                                size_t len, size_t cap,
                                struct rommod_s **mod);

/*
 * WHAT : 引用 +1（同模块多次启动窗口时共用一份 RAM 镜像）
 */
void rommod_get(struct rommod_s *mod);

/*
 * WHAT : 引用 -1，归零时释放可写段 RAM（XIP 段本就不占 RAM）
 */
void rommod_put(struct rommod_s *mod);

/*
 * WHAT : 按名取模块导出符号（dynsym 线性扫描，模块符号数很小）
 * 返回 : OK 且 *addr 给出运行期地址；-ENOENT 未导出
 */
int rommod_getsym(struct rommod_s *mod, const char *name, void **addr);

/*==========================
 *  观测（测试/诊断）
 *==========================*/

/* 装载形态：ROMMOD_FLAVOR_DYNAMIC / ROMMOD_FLAVOR_STATIC */
int  rommod_flavor(const struct rommod_s *mod);
/* 可写段实际占用 RAM 字节数（XIP 段不计入——省内存观测量） */
size_t rommod_ram_used(const struct rommod_s *mod);
/* 模块总字节数（flash 占用观测量） */
size_t rommod_flash_size(const struct rommod_s *mod);
/* 当前活跃模块数（load-put 守恒的蜕变断言用） */
int  rommod_active_count(void);

#endif /* __ROMMOD_H */
