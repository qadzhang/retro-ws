#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : 可选安装包构建器——把 GPL 组件（UCBLogo / GNU nano）构建为独立 ELF 并打包 SD 安装目录
# WHY  : GPL 组件以 mere aggregation（进程隔离）方式提供，固件 ROM 保持 Apache-2.0；
#        同时验证 binfmt ELF 动态加载/软件安装链路
# WHO  : Retro WS Project Team
# WHERE: retro-ws/scripts/build_packages.sh（由 build_all.sh 或手动调用）
# WHEN : 2026-10-04 新增；2026-10-05 增 GNU nano 包（nano 出 ROM 定稿）
# HOW  : 下载/定位上游源码 -> 同步 apps-extra/ 到 nuttx-apps/external/ ->
#        LOADABLE 构建 -> 收集 .elf 与源码副本到 dist/sdcard/
#
# 用法:
#   ./build_packages.sh [--target esp32s3|esp32cam] [--download-only]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NUTTX_DIR="$PROJECT_ROOT/deps/nuttx"
APPS_DIR="$PROJECT_ROOT/deps/nuttx-apps"
DIST_DIR="$PROJECT_ROOT/dist"
DOWNLOAD_DIR="$PROJECT_ROOT/deps/download"

TARGET="esp32s3"
DOWNLOAD_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --target=*)     TARGET="${arg#*=}" ;;
        --esp32cam)     TARGET="esp32cam" ;;
        --download-only) DOWNLOAD_ONLY=true ;;
        *) echo "未知参数: $arg"; exit 1 ;;
    esac
done

# UCBLogo 上游（GPL-2.0-or-later，SourceForge）
UCBLOGO_URL="https://sourceforge.net/projects/ucblogo/files/ucblogo/6.2.2/ucblogo-6.2.2.tar.gz"
UCBLOGO_TARBALL="ucblogo-6.2.2.tar.gz"
UCBLOGO_SRC="$PROJECT_ROOT/apps-extra/ucblogo/src"

# GNU nano 上游（GPL-3.0，deps/nano 手动放置——download_deps.sh 未含）
NANO_SRC="$PROJECT_ROOT/deps/nano"
NANO_TARBALL="nano-8.4.tar.xz"   # 手动下载副本（若 deps/download/ 有则随包分发）

log()  { echo -e "\033[0;34m[PKG]\033[0m $1"; }
ok()   { echo -e "\033[0;32m[PKG-OK]\033[0m $1"; }
warn() { echo -e "\033[1;33m[PKG-WARN]\033[0m $1"; }

#===== 1. 下载 GPL 源码（含源码副本以满足 GPL 分发义务）=====
download_ucblogo()
{
    if [ -d "$UCBLOGO_SRC" ] && [ -n "$(ls -A "$UCBLOGO_SRC" 2>/dev/null)" ]; then
        log "UCBLogo 源码已存在，跳过下载"
        return 0
    fi

    mkdir -p "$DOWNLOAD_DIR" "$UCBLOGO_SRC"
    if [ ! -f "$DOWNLOAD_DIR/$UCBLOGO_TARBALL" ]; then
        log "下载 $UCBLOGO_URL ..."
        wget -q "$UCBLOGO_URL" -O "$DOWNLOAD_DIR/$UCBLOGO_TARBALL" || {
            warn "下载失败（SourceForge 需网络）；请手动下载放入 $DOWNLOAD_DIR/"
            return 1
        }
    fi

    # tarball 内层目录形如 ucblogo-6.2.2/，展开后取 *.c/*.h 进 src/
    tar -xzf "$DOWNLOAD_DIR/$UCBLOGO_TARBALL" -C /tmp
    local inner="$(tar -tzf "$DOWNLOAD_DIR/$UCBLOGO_TARBALL" | head -1 | cut -d/ -f1)"
    cp /tmp/"$inner"/*.c /tmp/"$inner"/*.h "$UCBLOGO_SRC"/ 2>/dev/null || true
    ok "UCBLogo 源码就绪（GPL-2.0+，tarball 副本保留在 deps/download/ 以满足分发义务）"
}

#===== 2. 同步到 nuttx-apps/external（NuttX 外部应用机制）=====
sync_to_apps()
{
    [ -d "$APPS_DIR" ] || { warn "deps/nuttx-apps 不存在（先运行 download_deps.sh）"; exit 1; }
    mkdir -p "$APPS_DIR/external"
    rsync -a --delete --exclude 'src' --exclude 'package' \
        "$PROJECT_ROOT/apps-extra/ucblogo/" "$APPS_DIR/external/ucblogo/"
    mkdir -p "$APPS_DIR/external/ucblogo/src"
    cp "$UCBLOGO_SRC"/* "$APPS_DIR/external/ucblogo/src/" 2>/dev/null || true
    ok "已同步到 deps/nuttx-apps/external/ucblogo/"

    # GNU nano（GPL-3.0，.rpk 交付）：上游 src/ + nano_port 垫片 -> external/nano
    if [ -d "$NANO_SRC/src" ]; then
        rsync -a --delete --exclude 'src' --exclude 'package' \
            "$PROJECT_ROOT/apps-extra/nano/" "$APPS_DIR/external/nano/"
        mkdir -p "$APPS_DIR/external/nano/src" "$APPS_DIR/external/nano/port"
        cp "$NANO_SRC"/src/*.c "$NANO_SRC"/src/*.h \
           "$APPS_DIR/external/nano/src/" 2>/dev/null || true
        cp "$PROJECT_ROOT"/src/nuttx/common/nano_port/*.c \
           "$PROJECT_ROOT"/src/nuttx/common/nano_port/*.h \
           "$APPS_DIR/external/nano/port/" 2>/dev/null || true
        ok "已同步 nano 上游 + nano_port 到 deps/nuttx-apps/external/nano/"
    else
        warn "deps/nano 缺失（手动放置 nano-8.4 tar.xz 解压），跳过 nano 包"
    fi
}

#===== 3. LOADABLE 构建并打包为 .rpk =====
build_and_collect()
{
    [ -d "$NUTTX_DIR" ] || { warn "deps/nuttx 不存在"; exit 1; }

    if [ "$DOWNLOAD_ONLY" = true ]; then
        log "--download-only：跳过构建"
        return 0
    fi

    log "提示：需在目标 defconfig 启用 CONFIG_EXTERNAL_UCBLOGO=y（nano 为"
    log "CONFIG_EXTERNAL_NANO=y）后执行 NuttX 构建"
    log "（cd scripts/$TARGET && ./nuttx_build.sh defconfig && ./build.sh nuttx）"

    # LOADABLE 产物位于 NuttX 构建树，收集进包源目录 data/apps/
    local pkgdata="$PROJECT_ROOT/apps-extra/ucblogo/package"
    mkdir -p "$pkgdata/data/bin"
    rm -f "$pkgdata/data/bin/ucblogo"
    found=false
    for f in $(find "$NUTTX_DIR" -name 'ucblogo*' -type f 2>/dev/null); do
        case "$f" in
            *.elf|*/bin/ucblogo)
                cp "$f" "$pkgdata/data/bin/ucblogo"
                found=true ;;
        esac
    done
    $found && ok "已收集 ucblogo ELF 到包源目录" \
              || warn "未找到构建产物（首次构建流程见 apps-extra/ucblogo/README.md）"

    # 打包 .rpk（make_package.sh 生成 manifest 并打 ustar 容器）
    if [ -f "$pkgdata/data/bin/ucblogo" ]; then
        bash "$SCRIPT_DIR/make_package.sh" \
            "$pkgdata" "$DIST_DIR/sdcard/pkg"
        # GPL 分发义务：源码副本随包提供
        mkdir -p "$DIST_DIR/sdcard/pkg-src"
        cp "$DOWNLOAD_DIR/$UCBLOGO_TARBALL" "$DIST_DIR/sdcard/pkg-src/" 2>/dev/null || true
    fi

    # ---- GNU nano（GPL-3.0，2026-10-05 出 ROM 转 .rpk）----
    local npkg="$PROJECT_ROOT/apps-extra/nano/package"
    if [ -d "$APPS_DIR/external/nano" ]; then
        mkdir -p "$npkg/data/bin"
        rm -f "$npkg/data/bin/nano"
        local nfound=false
        for f in $(find "$NUTTX_DIR" -name 'nano*' -type f 2>/dev/null); do
            case "$f" in
                *.elf|*/bin/nano)
                    cp "$f" "$npkg/data/bin/nano"
                    nfound=true ;;
            esac
        done
        $nfound && ok "已收集 nano ELF 到包源目录" \
                  || warn "未找到 nano 构建产物（流程见 apps-extra/nano/README.md，binfmt 链路验证见 NEXT_STEPS）"
        if [ -f "$npkg/data/bin/nano" ]; then
            bash "$SCRIPT_DIR/make_package.sh" \
                "$npkg" "$DIST_DIR/sdcard/pkg"
            # GPL-3.0 分发义务：tarball 副本随包（deps/download/ 有则拷）
            mkdir -p "$DIST_DIR/sdcard/pkg-src"
            cp "$DOWNLOAD_DIR/$NANO_TARBALL" "$DIST_DIR/sdcard/pkg-src/" 2>/dev/null \
                || warn "deps/download/$NANO_TARBALL 不存在——分发 .rpk 前请补源码副本"
        fi
    fi
}

#===== 4. 打包 SD 安装目录 =====
package_dist()
{
    mkdir -p "$DIST_DIR/sdcard/pkg" "$DIST_DIR/sdcard/scripts/logo/lib"
    cat > "$DIST_DIR/sdcard/INSTALL.txt" <<'EOF'
ESP32 Retro WS - 软件包安装说明（deb 风格 .rpk）
==================================================
1. 将本目录全部内容拷入 SD 卡根目录（对应 /sdcard/）
2. 固件侧安装:
   nsh> pkg install /sdcard/pkg/ucblogo-6.2.2-1.rpk
   nsh> pkg install /sdcard/pkg/nano-8.4-1.rpk   # GNU nano（GPL-3.0，可选）
   nsh> pkg list                          # 查看已安装
   nsh> pkg info ucblogo                  # 包详情
   nsh> pkg remove ucblogo                # 卸载
3. 运行已安装程序: 输入完整路径（官方系统包在 /opt/bin/，如 /opt/bin/ucblogo；
   第三方包装 SD 卡，如 /sdcard/apps/<名>）
4. GPL 组件源码副本: /sdcard/pkg-src/（分发 .rpk 时须一并提供）
5. 系统默认 CLI 编辑器为 vi（nano 属可选安装的 GPL 独立程序）
EOF
    ok "SD 安装目录就绪: $DIST_DIR/sdcard/"
}

download_ucblogo
sync_to_apps
build_and_collect
package_dist
ok "完成。许可证边界: 固件 ROM 无 GPL 代码 (Apache-2.0，默认编辑器 vi)；UCBLogo (GPL-2.0+) 与 nano (GPL-3.0) 均为独立 ELF 包"
