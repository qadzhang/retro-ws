#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
"""
gen_pkgdb.py - 构建期离线安装：.rmo 模块树 -> 包安装数据库（ROM 预装层）

WHAT : 按板名单把 ROM 应用包"直接安装到位"——在构建期生成 pkg_manager
       兼容的安装数据库（db/<pkg>.control + db/manifest/<pkg>），连同
       bin/ XIP 载荷一起编入固件 ROMFS 镜像；设备首启零安装动作
WHY  : 2026-10-06 用户定稿策略修订——应用编译 ROM 时即完成安装（预装
       数据库随镜像分发），不再有"首启 seeder 扫 .rpk 打包安装"环节：
       启动更快、不依赖片上可写区先于包管理可用、无 seed 失败重试面
WHO  : scripts/build_romapps.sh（finalize 末尾，模块重链定 CRC 后调用）
WHERE: retro-ws/tools/gen_pkgdb.py
WHY/HOW: DB 格式与 pkg_manager.c 写出物逐字节同构（test_pkgstore 契约
       锁定）：control 快照=包源 control；manifest 行 "<crc8hex> <绝对
       路径>"，Xip 载荷记 PKG_ROM_ROOT/bin/<名>（卸载据此定位只删登记
       不删 ROM 文件）；info/ 维护脚本按包源归档。运行期 pkg_manager
       以"ROM 预装层 + 片上可写覆盖层（tombstone/后装包）"两级读取。

用法: gen_pkgdb.py --rom <rom树> --pkgs <包源根> --list <板名单> [--xip-suffix .rmo]
       rom 树须已含 bin/<模块>（最终重链产物）；db/ 写入 rom 树内
"""

import argparse
import os
import sys
import zlib


def crc32_file(path):
    with open(path, "rb") as f:
        return zlib.crc32(f.read()) & 0xFFFFFFFF


def read_fields(control_path):
    fields = {}
    for line in open(control_path, encoding="utf-8"):
        line = line.rstrip("\n").rstrip("\r")
        if ":" in line:
            k, v = line.split(":", 1)
            fields[k.strip()] = v.strip()
    return fields


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True, help="ROM 构建树（含 bin/）")
    ap.add_argument("--pkgs", required=True, help="包源根（pkgs/<名>/control）")
    ap.add_argument("--list", required=True, help="板名单文件")
    ap.add_argument("--rom-root", default="/rom/pkg",
                    help="设备侧 ROM 存储挂载路径（manifest 绝对行用）")
    args = ap.parse_args()

    names = []
    for ln in open(args.list, encoding="utf-8"):
        ln = ln.strip()
        if ln and not ln.startswith("#"):
            names.append(ln)

    db = os.path.join(args.rom, "db")
    os.makedirs(os.path.join(db, "manifest"), exist_ok=True)
    os.makedirs(os.path.join(db, "info"), exist_ok=True)

    for pkg in names:
        src = os.path.join(args.pkgs, pkg)
        ctl = os.path.join(src, "control")
        if not os.path.isfile(ctl):
            print("[gen_pkgdb] 缺 control: %s" % ctl, file=sys.stderr)
            sys.exit(1)

        fields = read_fields(ctl)

        # control 快照
        with open(os.path.join(db, pkg + ".control"), "w",
                  encoding="utf-8") as f:
            order = ["Package", "Version", "Arch", "Depends", "License",
                     "Root", "Type", "Xip", "Title-Zh", "Title-En", "Icon",
                     "Description", "Installed-Size", "Maintainer"]
            done = set()
            for k in order:
                if k in fields:
                    f.write("%s: %s\n" % (k, fields[k]))
                    done.add(k)
            for k, v in fields.items():        # 未知字段按原序补尾
                if k not in done:
                    f.write("%s: %s\n" % (k, v))

        # manifest：Xip 载荷逐条（最终模块 CRC）+ 包源 data/ 载荷
        # （名单包 data/ 通常为空；有则按 Root 根前缀记绝对路径）
        root = fields.get("Root", "sdcard")
        prefix = "/opt" if root == "system" else "/sdcard"
        mlines = []
        xip = fields.get("Xip", "")
        for ent in filter(None, (e.strip() for e in xip.split(","))):
            mod = os.path.join(args.rom, ent)
            if not os.path.isfile(mod):
                print("[gen_pkgdb] Xip 载荷缺失: %s" % mod, file=sys.stderr)
                sys.exit(1)
            mlines.append("%08x %s/%s" % (crc32_file(mod), args.rom_root,
                                          ent))
        datadir = os.path.join(src, "data")
        if os.path.isdir(datadir):
            for dirpath, _dirs, files in os.walk(datadir):
                for fn in sorted(files):
                    full = os.path.join(dirpath, fn)
                    rel = os.path.relpath(full, datadir)
                    mlines.append("%08x %s/%s" % (crc32_file(full), prefix,
                                                  rel.replace(os.sep, "/")))
        with open(os.path.join(db, "manifest", pkg), "w",
                  encoding="utf-8") as f:
            for ln in mlines:
                f.write(ln + "\n")

        # 维护脚本归档（名单包通常无）
        for script in ("preinst", "postinst", "prerm", "postrm"):
            sp = os.path.join(src, script)
            if os.path.isfile(sp):
                with open(sp, "rb") as fi, \
                     open(os.path.join(db, "info", "%s.%s" % (pkg, script)),
                          "wb") as fo:
                    fo.write(fi.read())

    print("[gen_pkgdb] %d 包预装数据库 -> %s" % (len(names), db))


if __name__ == "__main__":
    main()
