#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : Espressif HAL（esp-hal-3rdparty）离线装配（无 git-clone）
# WHY  : NuttX 构建默认 git clone+submodule；本脚本用 GitHub API
#        tarball 装配同 commit 快照并把补丁步骤幂等化
# WHO  : scripts/firmware/build_firmware.sh（首次/重建 deps 时调用）
# WHERE: retro-ws/scripts/firmware/prepare_esp_hal.sh
# WHEN : 2026-10-04 新增
# HOW  : 对 esp32 / esp32s3 / esp32c3 三处：
#        1) tarball 拉 HAL @ NuttX Make.defs 锁定的 commit
#        2) tarball 拉 pinned mbedtls / esp-phy-lib 到子模块位
#        3) 各目录 git 快照（满足 Make.defs 的 git 操作）
#        4) mbedtls 预打 NuttX 补丁并提交
#        5) Make.defs 的 git apply 行幂等化（重跑即复现）
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NUTTX="$ROOT/deps/nuttx"
API="https://api.github.com/repos"

pin_of() {
    curl -s --max-time 30 "$API/espressif/esp-hal-3rdparty/contents/$2?ref=$1" \
        | python3 -c "import json,sys; print(json.load(sys.stdin).get('sha',''))"
}

fetch_tar() {  # fetch_tar <owner/repo> <ref> <dest>
    rm -rf "$3"; mkdir -p "$3"
    curl -sL --max-time 900 "$API/$1/tarball/$2" \
        | tar -xz -C "$3" --strip-components=1
}

snap() {
    git init -q && git add -A >/dev/null 2>&1 \
        && git -c user.email=retro@local -c user.name=retro commit -qm "$1" >/dev/null 2>&1
}

stage_one() {  # stage_one <base> <halref>
    local base=$1 ref=$2

    echo "[hal] staging $base @ ${ref:0:8}"
    fetch_tar espressif/esp-hal-3rdparty "$ref" "$base/esp-hal-3rdparty"

    local psha
    # mbedtls 固定用上游 v3.6.2 单体版：espressif 分支 pin(98ae8db) 已拆
    # tf-psa-crypto 子模块（S3 HAL 2025 版编译时缺 include 编不过）
    fetch_tar Mbed-TLS/mbedtls "refs/tags/v3.6.2" \
        "$base/esp-hal-3rdparty/components/mbedtls/mbedtls" || \
    fetch_tar Mbed-TLS/mbedtls "v3.6.2" \
        "$base/esp-hal-3rdparty/components/mbedtls/mbedtls"
    psha=$(pin_of "$ref" components/esp_phy/lib)
    fetch_tar espressif/esp-phy-lib "$psha" "$base/esp-hal-3rdparty/components/esp_phy/lib"

    ( cd "$base/esp-hal-3rdparty" && snap snap )

    ( cd "$base/esp-hal-3rdparty/components/mbedtls/mbedtls" && snap snap \
        && git apply ../../../nuttx/patches/components/mbedtls/mbedtls/0001-*.patch 2>/dev/null \
        && git apply --recount ../../../nuttx/patches/components/mbedtls/mbedtls/0002-*.patch 2>/dev/null \
        && git add -A >/dev/null 2>&1 \
        && git -c user.email=retro@local -c user.name=retro commit -qm patched >/dev/null 2>&1 \
        || echo "[hal] mbedtls patches pre-applied or n/a" )

    # 无线组件目录：部分芯片快照里不存在（esp32 tarball 无空目录），
    # 补建 + git 快照，Make.defs 的 submodule update 才有落脚点
    for d in esp_phy/lib esp_wifi/lib bt/controller/lib_esp32 bt/controller/lib_esp32c3 esp_coex/lib; do
        mkdir -p "$base/esp-hal-3rdparty/components/$d"
        ( cd "$base/esp-hal-3rdparty/components/$d" && snap snap ) || true
    done

    local mk="$base/Make.defs"
    if [ -f "$mk" ] && { ! grep -q "retro.*already applied" "$mk" || grep -q "components/esp_wifi/lib components/bt" "$mk"; }; then
        python3 - "$mk" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "git apply ../../../nuttx/patches/components/mbedtls/mbedtls/*.patch"
if old in s:
    s = s.replace(old, "(git apply ../../../nuttx/patches/components/mbedtls/mbedtls/*.patch || echo \"[retro] mbedtls patches already applied\")")

# 离线快照无 gitlink：submodule update 只留 mbedtls（phy 无线库已由
# prepare 脚本 tarball 预置/建仓，esp32 老版 Make.defs 的完整清单会
# 因 index 无 gitlink 报"路径规格未匹配"而中断 context）
for old_sub in (
    "components/mbedtls/mbedtls components/esp_phy/lib components/esp_wifi/lib components/bt/controller/lib_esp32 components/esp_coex/lib",
    "components/esp_phy/lib components/esp_wifi/lib components/bt/controller/lib_esp32 components/esp_coex/lib",
):
    if old_sub in s:
        s = s.replace(old_sub, "")
        print("[hal] Make.defs submodule list trimmed (offline):", p)

if "retro.*already applied" not in s and old in s:
    open(p, "w").write(s)
    print("[hal] Make.defs idempotent:", p)
elif old_sub in s:
    open(p, "w").write(s)
PY
    fi
}

halref_of() {
    grep -A2 "ifndef ESP_HAL_3RDPARTY_VERSION" "$1" | grep -oE "[0-9a-f]{40}" | head -1
}

for chip in esp32 esp32s3; do
    stage_one "$NUTTX/arch/xtensa/src/$chip" \
        "$(halref_of "$NUTTX/arch/xtensa/src/$chip/Make.defs")"
done

# risc-v：HAL 必须放 esp32c3 真目录（chip 是 configure 建的符号链接，
# 被真目录占用会导致 clean_dirlinks 失败——踩过的坑）
rm -rf "$NUTTX/arch/risc-v/src/chip"
stage_one "$NUTTX/arch/risc-v/src/esp32c3" \
    "$(halref_of "$NUTTX/arch/risc-v/src/common/espressif/Make.defs")"

echo "[hal] done."
