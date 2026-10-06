#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
"""
gen_romsymtab.py - 模块未定义符号 -> 固件基础符号表 C 源

WHAT : 收集全部 .rmo 模块的未定义符号（nm -u），到固件 pass1 ELF 的
       符号表（nm -n）中解析地址，生成按名升序的 rommod 符号表数组
WHY  : ROM XIP 模块（动态档 ARM/RISC-V）装载时经此表解析 lv_/i18n_/
       libc 引用（rommod_bind 二分查找）；缺失符号在这里构建期拦截
WHO  : scripts/build_romapps.sh（finalize 阶段，固件 pass1 之后调用）
WHERE: retro-ws/tools/gen_romsymtab.py
WHEN : 2026-10-06 新增
HOW  : nm 输出解析（模块: "U symbol" 行；固件: "addr T symbol" 行）->
       交集按名排序 -> C 数组 {name, addr}；任何模块符号在固件中
       缺失即非零退出（fail fast，防上板后 rommod 未定义符号）

用法: gen_romsymtab.py <固件ELF> <模块1.rmo> [模块2.rmo ...] -o <输出.c>
"""

import argparse
import subprocess
import sys


def nm_undefined(path):
    """nm -u：返回未定义符号集合"""
    out = subprocess.run(["nm", "--undefined-only", path],
                         capture_output=True, text=True, check=True)
    syms = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            syms.add(parts[1])
        elif len(parts) == 1:
            syms.add(parts[0])          # 无字母标记的 U 行（某些 nm 版本）
    return syms


def nm_defined(path):
    """nm -n：返回 {符号名: 地址}（取每个名字的第一个定义）"""
    out = subprocess.run(["nm", "--defined-only", "-n", path],
                         capture_output=True, text=True, check=True)
    syms = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            name = parts[2]
            if name not in syms:
                syms[name] = addr
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", help="固件 pass1 ELF（nuttx）")
    ap.add_argument("modules", nargs="+", help=".rmo 模块文件")
    ap.add_argument("-o", "--out", required=True, help="输出 .c 文件")
    args = ap.parse_args()

    fw = nm_defined(args.elf)

    wanted = set()
    for m in args.modules:
        wanted |= nm_undefined(m)

    # 排除弱符号类的噪音（如 __gmon_start__）；其余必须在固件里找得到
    skip = {"__gmon_start__", "_edata", "_end", "__bss_start__", "__bss_end__"}
    missing = sorted(s for s in wanted if s not in fw and s not in skip)
    if missing:
        print("[gen_romsymtab] 固件缺失符号（构建失败，防上板 rommod "
              "未定义符号）:", file=sys.stderr)
        for s in missing:
            print("  " + s, file=sys.stderr)
        sys.exit(1)

    entries = sorted(s for s in wanted if s in fw)

    lines = []
    lines.append("/*")
    lines.append(" * SPDX-FileCopyrightText: 2026 Retro WS Project")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" */")
    lines.append("/*")
    lines.append(" * rom_symtab.c - ROM 模块基础符号表（自动生成，勿手改）")
    lines.append(" * 生成: tools/gen_romsymtab.py（固件 pass1 ELF + 全部 .rmo）")
    lines.append(" * 消费: rommod_bind()（动态档模块 extern 符号解析）")
    lines.append(" */")
    lines.append("")
    lines.append("#include <nuttx/config.h>")
    lines.append('#include "rommod.h"')
    lines.append("")
    lines.append("const struct rommod_sym_s g_rommod_symtab[] =")
    lines.append("{")
    for s in entries:
        lines.append('    { "%s", (void *)0x%08x },' % (s, fw[s]))
    lines.append("};")
    lines.append("")
    lines.append("const int g_rommod_symtab_n = %d;" % len(entries))
    lines.append("")
    with open(args.out, "w") as fp:
        fp.write("\n".join(lines))
    print("[gen_romsymtab] %d 符号 -> %s" % (len(entries), args.out))


if __name__ == "__main__":
    main()
