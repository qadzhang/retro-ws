#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
"""
mkromfs.py - 板级脚本目录 -> ROMFS 镜像 C 数组（无外部依赖）

WHAT : 把 firmware/scripts/<板>/ 下的 .bas/.be/.js 打包成 ROMFS 镜像，
       输出可编译的 C 源（uint8_t 数组，链接进 .rodata=Flash）
WHY  : 用户要求每板有脚本目录，编译期二进制化进 ROM，运行时从
       Flash 直接执行（XIP 语义，HARDWARE.md 13.4）
WHO  : scripts/firmware/build_firmware.sh（同步 deps 后调用）
WHERE: esp32-retro-ws/tools/mkromfs.py
WHEN : 2026-10-04(晚) 新增
HOW  : ROMFS 格式按 NuttX fs/romfs/fs_romfs.h：
       卷头 "-rom1fs-" + be32 全尺寸 + be32 头 512B 异或校验 +
       卷名(16 对齐)；文件头 NEXT|INFO|SIZE|CHKSUM + 名(16 对齐) +
       数据(16 对齐)；NEXT 高 28 位=下条目偏移 低 4 位=类型(2=文件)+8(可执行)；
       目录(根)类型 1；链尾 NEXT 偏移=0

用法: mkromfs.py <脚本目录> <输出.c> [卷名]
"""

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


def build(files, volname="retro"):
    """files: [(name, bytes)] 平铺；返回镜像 bytes"""
    out = bytearray()
    out += ROMFS_MAGIC

    # 布局计算：卷头(16+对齐卷名) + 根目录头(16+"."名区) + 每文件(头+名+数据)
    vol_off = 16 + align16(len(volname) + 1)
    root_off = vol_off

    entries = []
    off = root_off + 16 + 16        # 根头 16B + 名 "." 补齐 16
    for name, data in files:
        hdr_off = off
        data_off = hdr_off + 16 + align16(len(name) + 1)
        next_off = data_off + align16(len(data))
        entries.append((hdr_off, data_off, next_off, name, data))
        off = next_off
    total = align16(off)
    total = max(total, 512)          # 校验和区域至少 512

    # 卷头
    out += be32(total)
    out += b"\x00\x00\x00\x00"       # 校验和占位（后回填）
    out += volname.encode() + b"\x00"
    out += b"\x00" * (vol_off - len(out))

    # 根目录条目
    first_child = entries[0][0] if entries else 0
    out += be32(first_child | RFNEXT_DIR | RFNEXT_EXEC)
    out += be32(root_off)             # 父目录 = 自身
    out += be32(0)
    out += be32(0)
    out += b".\x00"
    out += b"\x00" * (root_off + 32 - len(out))

    # 文件条目
    for i, (hdr_off, data_off, next_off, name, data) in enumerate(entries):
        last = i == len(entries) - 1
        nxt = 0 if last else entries[i + 1][0]
        out += be32(nxt | RFNEXT_FILE | RFNEXT_EXEC)
        out += be32(0)                # info: 普通文件未用
        out += be32(len(data))
        out += be32(0)                # 文件头校验和（NuttX 解析器不校验）
        out += name.encode() + b"\x00"
        out += b"\x00" * (data_off - len(out))
        out += data
        out += b"\x00" * (next_off - len(out))

    assert len(out) == off, (len(out), off)

    # 尾部补到 512 倍数 + 回填卷校验和
    out += b"\x00" * (align16(max(len(out), 512)) - len(out))
    chk = vol_checksum(bytes(out[:512]))
    out[12:16] = be32(chk)

    # 全尺寸字段：可访问字节数（含头）
    out[8:12] = be32(len(out))

    return bytes(out)


def gen_c(image, board):
    lines = []
    lines.append("/*")
    lines.append(" * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project")
    lines.append(" * SPDX-License-Identifier: Apache-2.0")
    lines.append(" */")
    lines.append("/*")
    lines.append(" * scripts_romfs.c - 板级脚本 ROMFS 镜像（自动生成，勿手改）")
    lines.append(" * 生成: tools/mkromfs.py firmware/scripts/%s -> 本文件" % board)
    lines.append(" * 挂载: /dev/rom0 -> /rom/scripts（retro_romdisk.c 块设备）")
    lines.append(" * XIP: retro_romfs_find() 直查镜像内文件指针，引擎从 Flash 执行")
    lines.append(" */")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("const uint8_t g_scripts_romfs[%d] __attribute__((aligned(16))) = {" % len(image))
    for i in range(0, len(image), 16):
        chunk = image[i:i + 16]
        lines.append("    " + ",".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("const size_t g_scripts_romfs_len = sizeof(g_scripts_romfs);")
    lines.append("")
    return "\n".join(lines)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    srcdir = sys.argv[1]
    outc = sys.argv[2]
    volname = sys.argv[3] if len(sys.argv) > 3 else "retro"

    files = []
    for name in sorted(os.listdir(srcdir)):
        path = os.path.join(srcdir, name)
        if os.path.isfile(path):
            with open(path, "rb") as f:
                files.append((name, f.read()))

    if not files:
        print("mkromfs: no files in %s" % srcdir, file=sys.stderr)
        sys.exit(1)

    board = os.path.basename(os.path.normpath(srcdir))
    image = build(files, volname)
    with open(outc, "w", encoding="utf-8") as f:
        f.write(gen_c(image, board))
    print("mkromfs: %d files, image %d bytes -> %s" %
          (len(files), len(image), outc))


if __name__ == "__main__":
    main()
