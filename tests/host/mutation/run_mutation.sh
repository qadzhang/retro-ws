#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : pkg_manager 变异测试 / mutation testing for the .rpk core
# WHY  : 测试全绿不等于测试有效——主动改坏实现，看测试能否抓到
# WHO  : tests/host/run_all.sh（或手工执行）
# WHERE: esp32-retro-ws/tests/host/mutation/run_mutation.sh
# WHEN : 2026-10-04 新增
# HOW  : 对 pkg_manager.c 的副本逐一注入单点变异（sed 表驱动），
#        用 -DPKG_MGR_SRC 重编 test_pkgmanager 后运行；
#        退出非零/崩溃 = 变异被杀；全绿 = 存活（测试有漏洞）
#        门禁：杀死率 >= 80%

set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/src/nuttx/common/apps/system/pkg_manager.c"
MUTDIR="$(mktemp -d /tmp/rpkg_mut.XXXXXX)"
CC=${CC:-gcc}

build_test() {  # $1 = 被测源文件路径, $2 = 输出二进制
    $CC -Wall -Wno-unused-parameter -g -fsanitize=address,undefined \
        -I "$ROOT/tests/host/stubs" \
        -I "$ROOT/src/nuttx/common/apps/system" \
        -DPKG_INSTALL_PREFIX='"/tmp/retro_test/sd"' \
        -DPKG_DB_ROOT='"/tmp/retro_test/db"' \
        -DCONFIG_RETRO_ARCH='"xtensa-esp32s3"' \
        -DCONFIG_RETRO_ARCH_VAL='"xtensa-esp32s3"' \
        -DCONFIG_RETRO_FAMILY_VAL='"xtensa"' \
        -DCONFIG_RETRO_CHIP_VAL='"esp32s3"' \
        -DPKG_MGR_SRC="\"$1\"" \
        "$ROOT/tests/host/test_pkgmanager.c" -o "$2" 2>/dev/null
}

# 变异表：描述|sed 表达式（对 pkg_manager.c 副本单点注入）
MUTATIONS=(
  'crc 多项式常量|s/0xEDB88320u/0x00000001u/'
  'crc 终值取反删除|s/return crc \^ 0xFFFFFFFFu;/return crc;/'
  'tar size 十进制|s/v = v \* 8 + (oct\[i\] - .0.);/v = v * 10 + (oct[i] - '"'"'0'"'"');/'
  '穿越分量检查失效|s/seglen == 2/seglen == 3/'
  '绝对路径放行|s/path\[0\] == .\/./path[0] == 0x01/'
  'all 恒匹配反转|s/strcmp(pkg_arch, .all.) == 0/strcmp(pkg_arch, "all") != 0/'
  '芯片后缀匹配失效|s/strcmp(pkg_arch, dash + 1) == 0/0/'
  '包名长度检查删除|s/len >= PKG_FIELD_MAX/len > 99999/'
  'checksum 校验恒过|s/return sum == expect;/return true;/'
  'tar 填充对齐失效|s/it->remain = (fsize + TAR_BLOCK_SIZE - 1) \& ~(size_t)(TAR_BLOCK_SIZE - 1);/it->remain = fsize;/'
  'CRC 比对永不触发|s/if (me->crc != crc)/if (0)/'
  'manifest 查找恒空|s/return \&ents\[i\];/return NULL;/'
  '回滚失效|s/unlink(paths\[i\]);/;/'
  'manifest 恶意行不丢弃|s/if (path_is_safe(rel))/if (1)/'
  '脚本入库路径|s@info/%s.%s@info/x-%s@'
  '已装查询恒否|s/return stat(path, \&st) == 0 ? 1 : 0;/return 0;/'
)

total=0
killed=0
survived=()

for entry in "${MUTATIONS[@]}"; do
    desc="${entry%%|*}"
    sedexpr="${entry#*|}"

    # 跳过占位变异
    [ "$sedexpr" = "XX/||" ] && continue

    mut="$MUTDIR/mut_$total.c"
    cp "$SRC" "$mut"
    sed -i "$sedexpr" "$mut"

    if cmp -s "$SRC" "$mut"; then
        echo "SKIP(未命中): $desc"
        continue
    fi

    total=$((total + 1))
    bin="$MUTDIR/mut_$total.bin"

    if ! build_test "$mut" "$bin"; then
        echo "KILL(编译失败): $desc"
        killed=$((killed + 1))
        continue
    fi

    rm -rf /tmp/retro_test
    mkdir -p /tmp/retro_test/sd /tmp/retro_test/db
    if timeout 60 "$bin" > "$MUTDIR/out_$total.log" 2>&1; then
        echo "SURVIVE: $desc"
        survived+=("$desc")
    else
        echo "KILL:    $desc"
        killed=$((killed + 1))
    fi
done

rate=$(( total > 0 ? killed * 100 / total : 100 ))
echo "======================================"
echo "变异总数: $total  杀死: $killed  存活: $((total - killed))"
echo "杀死率: ${rate}%  (门禁 >= 80%)"
if [ ${#survived[@]} -gt 0 ]; then
    echo "存活变异（测试漏洞，需补测试）:"
    for s in "${survived[@]}"; do echo "  - $s"; done
fi

rm -rf "$MUTDIR"
[ "$rate" -ge 80 ] && echo "MUTATION-PASS" || echo "MUTATION-FAIL"
[ "$rate" -ge 80 ] || exit 1
