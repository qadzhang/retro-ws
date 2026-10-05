#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
"""
WHAT : nuttx.bin -> nuttx.uf2（RP2040 drag-flash 格式）
WHY  : NuttX 的 UF2 生成依赖 picotool/pico-sdk；UF2 是极简容器，
      宿主直接打包免依赖
WHO  : scripts/firmware/build_firmware.sh
WHERE: retro-ws/scripts/make_uf2.py
WHEN : 2026-10-04 新增
HOW  : 512B 块：32B 头 + 476B 数据（RP2040 family 0xE48BFF56，
      flag 0x00002000），基址 0x10000000（XIP flash）
"""
import sys

UF2_MAGIC_START = 0x0A324655
UF2_MAGIC_END   = 0x016528E3
RP2040_FAMILY   = 0xE48BFF56
FLASH_BASE      = 0x10000000

def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    out = b''
    addr = FLASH_BASE
    for off in range(0, len(data), 476):
        chunk = data[off:off + 476]
        flags = 0x00002000          # family id present
        hdr = UF2_MAGIC_START.to_bytes(4, 'little')
        hdr += (507 | flags).to_bytes(4, 'little')
        hdr += FLASH_BASE.to_bytes(4, 'little') if False else addr.to_bytes(4, 'little')
        hdr += len(chunk).to_bytes(4, 'little')
        hdr += (off // 476).to_bytes(4, 'little')   # block no
        hdr += ((len(data) + 475) // 476).to_bytes(4, 'little')  # num blocks
        hdr += RP2040_FAMILY.to_bytes(4, 'little')
        out += hdr + chunk.ljust(476, b'\xff') + UF2_MAGIC_END.to_bytes(4, 'little')
        addr += 476
    open(dst, 'wb').write(out)
    print(f"[uf2] {dst}: {len(out)} bytes from {len(data)} bin")

if __name__ == '__main__':
    main()
