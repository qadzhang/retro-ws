#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
"""
mkromfs.py - 目录树 -> ROMFS 镜像 C 数组（无外部依赖）

WHAT : 把 firmware/scripts/<板>/（平铺脚本）或包存储目录树
       （bin/ + share/ + *.rpk）打包成 ROMFS 镜像，输出可编译的
       C 源（uint8_t 数组，链接进 Flash 只读段）
WHY  : 板级脚本 ROM XIP 直跑（HARDWARE.md 13.4）+ ROM 包存储
       （/rom/pkg，应用/系统分离 2026-10-06）：镜像数组常驻 Flash，
       pkg_rom.c 直查文件数据指针，rommod 让 .rmo 模块原址执行
WHO  : scripts/firmware/build_firmware.sh（每次构建调用）；
       tests/host/python/test_romfs_tree.py（镜像回读差分）
WHERE: retro-ws/tools/mkromfs.py
WHEN : 2026-10-04(晚) 新增；2026-10-06 增目录树/链接段属性/自定义符号名
HOW  : ROMFS 格式按 NuttX fs/romfs/fs_romfs.h 与 fs_romfsutil.c 实测语义：
       卷头 "-rom1fs-" + be32 全尺寸 + be32 头 512B 异或校验 + 卷名(16 对齐)；
       条目头 NEXT|INFO|SIZE|CHKSUM + 名(16 对齐) + 数据(16 对齐)；
       NEXT 低 4 位=类型(2=文件,1=目录)+8(可执行)；目录条目
       rf_info=首孩子头偏移、rf_next=父层下一兄弟；根条目
       rf_next=首孩子（内核 searchdir 从根头沿 next 链遍历）。

用法:
  平铺（板级脚本，兼容旧调用）:
    mkromfs.py <目录> <输出.c> [卷名]
  目录树（ROM 包存储）:
    mkromfs.py --tree <目录> <输出.c> [卷名] [--symname g_pkg_romfs]
               [--section .flash.text]
"""

import argparse
import os
import struct
import sys

ROMFS_MAGIC = b"-rom1fs-"
RFNEXT_FILE = 2
RFNEXT_DIR = 1
RFNEXT_EXEC = 8


def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def align16(n):
    return (n + 15) & ~15


def vol_checksum(header512):
    """头 512 字节按 4 字节字异或（不足补零）"""
    data = bytearray(header512)
    data += b"\x00" * ((-len(data)) % 512 if len(data) < 512 else 0)
    x = 0
    for i in range(0, 512, 4):
        x ^= struct.unpack(">I", data[i:i + 4])[0]
    return x


class Node:
    __slots__ = ("name", "is_dir", "data", "children",
                 "hdr_off", "data_off", "end")

    def __init__(self, name, is_dir, data=None):
        self.name = name
        self.is_dir = is_dir
        self.data = data
        self.children = []
        self.hdr_off = 0
        self.data_off = 0
        self.end = 0


def scan_dir(path):
    """递归扫描目录树 -> Node 根；子项按名排序（构建确定性）；
    自底向上剔除空目录（ROMFS 空目录 rf_info=0 会误导内核遍历）"""
    root = Node(".", True)
    for dirpath, dirnames, filenames in os.walk(path):
        dirnames.sort()
        filenames.sort()
        rel = os.path.relpath(dirpath, path)
        node = root if rel == "." else find_node(root, rel.split(os.sep))
        for d in dirnames:
            node.children.append(Node(d, True))
        for f in filenames:
            with open(os.path.join(dirpath, f), "rb") as fp:
                node.children.append(Node(f, False, fp.read()))

    def prune_empty(n):
        n.children = [c for c in n.children
                      if not c.is_dir or prune_empty(c)]
        return len(n.children) > 0

    prune_empty(root)
    return root


def find_node(root, parts):
    node = root
    for p in parts:
        for c in node.children:
            if c.name == p:
                node = c
                break
    return node


def align_to(n, a):
    return (n + a - 1) & ~(a - 1)


def layout(node, off, in_bin=False):
    """深度优先分配偏移：目录孩子紧跟目录头之后物理放置
    （rf_next 显式跳到父层下一兄弟，物理相邻仅为紧凑）。
    bin/ 目录下的载荷（.rmo 模块）数据 4096 对齐——静态绑定档两遍
    链接的烘焙地址稳定性前提（F 数组 4096 对齐 + doff 4096 对齐 +
    max-page-size=0x1000 -> 段文件内偏移两遍确定全等）"""
    node.hdr_off = off
    node.data_off = off + 16 + align16(len(node.name.encode()) + 1)
    if node.is_dir:
        cur = node.data_off
        child_bin = in_bin or node.name == "bin"
        for c in node.children:
            layout(c, cur, child_bin)
            cur = c.end
        node.end = cur if node.children else node.data_off
    else:
        if in_bin:
            node.data_off = align_to(node.data_off, 4096)
        node.end = node.data_off + align16(len(node.data))


def emit_tree(node, out, is_root=False, next_sibling=0, first_child=0):
    """递归发射条目：根 rf_next=首孩子；目录 rf_info=首孩子；
    孩子的 rf_next 指向父层下一兄弟（末孩子=0）"""
    if node.is_dir:
        nxt = first_child if is_root else next_sibling
        flags = RFNEXT_DIR | RFNEXT_EXEC
        info = node.children[0].hdr_off if node.children else 0
        size = 0
    else:
        nxt = next_sibling
        flags = RFNEXT_FILE | RFNEXT_EXEC
        info = 0
        size = len(node.data)

    off = node.hdr_off
    out[off:off + 4] = be32((nxt & ~15) | flags)
    out[off + 4:off + 8] = be32(info)
    out[off + 8:off + 12] = be32(size)
    out[off + 12:off + 16] = be32(0)   # 条目校验和（内核不校验）

    # 名字区起始于条目头后（off+16），NUL 结尾右补零——内核
    # fs_romfsutil 的 parsefilename 从 off+16 读到首个 NUL
    name = node.name.encode() + b"\x00"
    out[node.hdr_off + 16:node.hdr_off + 16 + len(name)] = name

    if not node.is_dir:
        out[node.data_off:node.data_off + len(node.data)] = node.data

    n = len(node.children)
    for i, c in enumerate(node.children):
        nxt_sib = node.children[i + 1].hdr_off if i + 1 < n else 0
        emit_tree(c, out, False, nxt_sib)


def build_image(root, volname):
    vol_off = 16 + align16(len(volname.encode()) + 1)
    layout(root, vol_off)
    total = align16(max(root.end, 512))

    out = bytearray(total)
    out[0:8] = ROMFS_MAGIC
    out[8:12] = be32(total)
    out[12:16] = b"\x00\x00\x00\x00"
    out[16:16 + len(volname.encode()) + 1] = volname.encode() + b"\x00"

    first_child = root.children[0].hdr_off if root.children else 0
    emit_tree(root, out, is_root=True, first_child=first_child)

    out[12:16] = be32(vol_checksum(bytes(out[:512])))
    return bytes(out)


def collect_offsets(node, prefix, out):
    """递归收集 文件相对路径 -> 数据区偏移（--print-offsets 用，
    与镜像布局同源——静态绑定档模块链接地址的计算依据）"""
    for c in node.children:
        rel = c.name if not prefix else prefix + "/" + c.name
        if c.is_dir:
            collect_offsets(c, rel, out)
        else:
            out[rel] = c.data_off


def build(files, volname="retro"):
    """files: [(name, bytes)] 平铺（旧接口兼容：脚本 ROMFS）"""
    root = Node(".", True)
    for name, data in files:
        root.children.append(Node(name, False, data))
    root.children.sort(key=lambda n: n.name)
    return build_image(root, volname)


def gen_c(image, symname, what, section="", tree=False):
    """生成 C 数组源；section 非空时放入指定链接段（XIP 可执行段）"""
    lines = []
    lines.append("/*")
    lines.append(" * SPDX-FileCopyrightText: 2026 Retro WS Project")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" */")
    lines.append("/*")
    lines.append(" * %s - ROMFS 镜像（自动生成，勿手改）" % symname)
    lines.append(" * 生成: tools/mkromfs.py %s" % what)
    lines.append(" * XIP: 镜像常驻 Flash；直查 API 返回镜像内指针，零拷贝")
    lines.append(" */")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    if section:
        # 可执行 flash 段（ESP32 IROM）+ 4096 对齐（静态绑定档烘焙
        # 地址的稳定前提：F≡0 mod 4096 + bin 载荷 4096 对齐）
        attr = ("__attribute__((aligned(4096), used, "
                "section(\"%s\")))" % section)
    elif tree:
        # 树模式（ROM 包存储，.rmo XIP 载荷）：即便落普通 .rodata
        # （RP2040 全 flash 可执行）也须 4096 对齐保证烘焙布局稳定
        attr = "__attribute__((aligned(4096), used))"
    else:
        attr = "__attribute__((aligned(16)))"
    lines.append("const uint8_t %s[%d] %s = {" % (symname, len(image), attr))
    for i in range(0, len(image), 16):
        chunk = image[i:i + 16]
        lines.append("    " + ",".join("0x%02X" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("const size_t %s_len = sizeof(%s);" % (symname, symname))
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="ROMFS 镜像生成器")
    ap.add_argument("--tree", action="store_true",
                    help="目录树模式（ROM 包存储：bin/ share/ *.rpk）")
    ap.add_argument("src", help="源目录")
    ap.add_argument("out", help="输出 .c 文件")
    ap.add_argument("volname", nargs="?", default=None, help="卷名")
    ap.add_argument("--symname", default="g_scripts_romfs",
                    help="C 数组符号名")
    ap.add_argument("--section", default="",
                    help="链接段属性（如 .flash.text，XIP 可执行段）")
    ap.add_argument("--print-offsets", action="store_true",
                    help="只打印布局（文件 -> 数据偏移），不生成镜像")
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        print("mkromfs: 源目录不存在: %s" % args.src, file=sys.stderr)
        sys.exit(1)

    if args.tree:
        volname = args.volname or "pkg"
        symname = args.symname if args.symname != "g_scripts_romfs" \
            else "g_pkg_romfs"
        tree = scan_dir(args.src)
        if args.print_offsets:
            offs = {}
            collect_offsets(tree, "", offs)
            for rel in sorted(offs):
                # 挂载视角路径：<卷根>/bin/x -> bin/x
                print("%s %d" % (rel, offs[rel]))
            return
        image = build_image(tree, volname)
        src = gen_c(image, symname, "--tree %s" % args.src,
                     args.section, tree=True)
    else:
        volname = args.volname or "retro"
        files = []
        for name in sorted(os.listdir(args.src)):
            p = os.path.join(args.src, name)
            if os.path.isfile(p):
                with open(p, "rb") as fp:
                    files.append((name, fp.read()))
        if not files:
            print("mkromfs: no files in %s" % args.src, file=sys.stderr)
            sys.exit(1)
        image = build(files, volname)
        src = gen_c(image, args.symname, args.src, args.section)

    with open(args.out, "w") as fp:
        fp.write(src)
    print("[mkromfs] %s: %d B (%s)" %
          (args.out, len(image), "tree" if args.tree else "flat"))


if __name__ == "__main__":
    main()
