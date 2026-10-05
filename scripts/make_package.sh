#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : .rpk 打包器（主机侧）——把"包源目录"打成 deb 风格安装包
# WHY  : 设备端 pkg_manager 只做安装/卸载，打包在主机完成，保持固件精简
# WHO  : build_packages.sh 调用；用户也可手工打包自有软件
# WHERE: esp32-retro-ws/scripts/make_package.sh
# WHEN : 2026-10-04 新增
# HOW  : 包源目录 = control + manifest(可选) + 维护脚本(可选) + data/ 载荷树
#        -> 排序拼装 -> tar --format=ustar -> <name>-<ver>.rpk
#
# 用法: make_package.sh <包源目录> <输出目录>
#   包源目录/
#   +-- control        # 必需：Package/Version/Arch/Depends/License/...
#   +-- manifest       # 可选：打包器 CRC 清单（缺省由设备端实测生成）
#   +-- preinst|postinst|prerm|postrm   # 可选：NSH 维护脚本
#   +-- data/...       # 载荷：安装时落到设备 /sdcard/ 之下

set -e

PKG_DIR="$1"
OUT_DIR="$2"

if [ -z "$PKG_DIR" ] || [ -z "$OUT_DIR" ] || [ ! -f "$PKG_DIR/control" ]; then
    echo "用法: make_package.sh <包源目录(含 control 和 data/)> <输出目录>" >&2
    exit 1
fi

# 从 control 提取字段
get_field() { awk -F': ' -v k="$1" '$1==k{print $2; exit}' "$PKG_DIR/control"; }

PKG_NAME="$(get_field Package)"
PKG_VER="$(get_field Version)"
if [ -z "$PKG_NAME" ] || [ -z "$PKG_VER" ]; then
    echo "错误: control 缺少 Package 或 Version 字段" >&2
    exit 1
fi

# 生成 manifest（CRC32 + 安装路径），与设备端格式一致: "<crc8位hex> /sdcard/..."
GEN_MANIFEST="$(mktemp)"
find "$PKG_DIR/data" -type f | sort | while read -r f; do
    rel="${f#$PKG_DIR/data/}"
    crc=$(crc32 "$f" 2>/dev/null || \
          python3 -c "import sys,zlib;print(format(zlib.crc32(open(sys.argv[1],'rb').read())&0xffffffff,'08x'))" "$f")
    printf '%s /sdcard/%s\n' "$crc" "$rel"
done > "$GEN_MANIFEST"

# 拼装 tar 根（control/manifest/脚本在根，载荷在 data/）
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$GEN_MANIFEST"' EXIT

cp "$PKG_DIR/control" "$STAGE/control"
cp "$GEN_MANIFEST" "$STAGE/manifest"
for s in preinst postinst prerm postrm; do
    [ -f "$PKG_DIR/$s" ] && cp "$PKG_DIR/$s" "$STAGE/$s"
done
cp -r "$PKG_DIR/data" "$STAGE/data"

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$PKG_NAME-$PKG_VER.rpk"

# 关键：ustar 格式（设备端 pkg_manager.c 的解析器只认 ustar）
#       -mindepth 去掉 "." 根、谓词分组避免 find 优先级陷阱、
#       --no-recursion 防止目录参数被 tar 二次展开、排序保证
#       control/manifest 先于载荷出现
(cd "$STAGE" && find . -mindepth 1 \( -type f -o -type d \) -print | \
    sed 's|^\./||' | sort | \
    tar --format=ustar --no-recursion -cf - -T -) > "$OUT"

echo "[make_package] $OUT"
echo "[make_package]   载荷文件数: $(find "$PKG_DIR/data" -type f | wc -l)"
