#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
"""
prep_pkgstore_fixture.py - test_pkgstore 的 ROM 包存储 fixture 生成

WHAT : 构造微型 /rom/pkg 树（bin/hello.rmo + db/ 预装数据库）并产出
       二进制 ROMFS 镜像；另备一个第三方 .rpk（片上后装通道回归）
WHY  : 2026-10-06 策略修订（构建期离线安装）：fixture 与生产工具
       同源——db/ 由 tools/gen_pkgdb.py 生成（格式契约锁定），镜像由
       tools/mkromfs.py 树模式生成；C 侧断言"预装即已安装、首启零动作"
WHO  : tests/host/run_all.sh（test_pkgstore 前置步骤）
WHERE: retro-ws/tests/host/prep_pkgstore_fixture.py
WHEN : 2026-10-06 新增；同日随离线安装策略重写
HOW  : 树 = {bin/hello.rmo, pkgs 源 control} -> gen_pkgdb.py -> db/
       -> mkromfs.build_image -> rom.img；world 包单独打 .rpk 放
       sdcard/（测 rpkg_install 片上常规通道不回归）
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkromfs  # noqa: E402

OUT = "/tmp/retro_test/pkgstore"
LIST = os.path.join(OUT, "board.list")


def main():
    rom_root = sys.argv[1] if len(sys.argv) > 1 else "/rom/pkg"
    rom = os.path.join(OUT, "rom")
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(rom, "bin"))
    os.makedirs(os.path.join(OUT, "opt", "db"), exist_ok=True)

    # bin/hello.rmo：借 rommod 测试的宿主模块（同轮次已生成；否则现场编）
    src = "/tmp/retro_test/rommod/mod_cli.so"
    if not os.path.exists(src):
        os.makedirs("/tmp/retro_test/rommod", exist_ok=True)
        csrc = os.path.join(OUT, "mod_cli.c")
        with open(csrc, "w") as f:
            f.write("int main(void){return 7;}\n")
        subprocess.run(["gcc", "-fPIC", "-shared", "-nostdlib",
                        "-fno-builtin", "-o", src, csrc], check=True)
    shutil.copy(src, os.path.join(rom, "bin", "hello.rmo"))

    # 名单（hello 预装；world 走第三方通道）
    with open(LIST, "w") as f:
        f.write("hello\n")

    # hello 包源（离线安装输入）
    p1 = os.path.join(OUT, "pkgs", "hello")
    os.makedirs(os.path.join(p1, "data"))
    with open(os.path.join(p1, "control"), "w") as f:
        f.write("Package: hello\nVersion: 1.0.0-1\nArch: all\n"
                "License: Apache-2.0\nRoot: system\nType: cli\n"
                "Xip: bin/hello.rmo\nDescription: preinstalled xip app\n")

    # 生产工具同源：gen_pkgdb 离线安装 -> db/
    subprocess.run([sys.executable,
                    os.path.join(ROOT, "tools", "gen_pkgdb.py"),
                    "--rom", rom, "--pkgs", os.path.join(OUT, "pkgs"),
                    "--list", LIST, "--rom-root", rom_root], check=True)

    # world 包：第三方 .rpk（片上后装通道回归用）
    p2 = os.path.join(OUT, "third_party", "world")
    os.makedirs(os.path.join(p2, "data", "share"))
    with open(os.path.join(p2, "control"), "w") as f:
        f.write("Package: world\nVersion: 2.0.0-1\nArch: all\n"
                "License: Apache-2.0\nRoot: system\nType: cli\n"
                "Description: plain payload pkg\n")
    with open(os.path.join(p2, "data", "share", "note.txt"), "w") as f:
        f.write("world share data\n")
    subprocess.run(["bash",
                    os.path.join(ROOT, "scripts", "make_package.sh"),
                    p2, os.path.join(OUT, "sdcard")], check=True)

    img = mkromfs.build_image(mkromfs.scan_dir(rom), "pkg")
    with open(os.path.join(OUT, "rom.img"), "wb") as f:
        f.write(img)

    # C 侧安装根/DB（-D 覆盖点）
    for d in ("opt", "db", "sd"):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)

    print("[pkgstore-fixture] rom.img %d B（bin/hello.rmo + db/ 预装层；"
          "world.rpk 走第三方通道）" % len(img))


if __name__ == "__main__":
    main()
