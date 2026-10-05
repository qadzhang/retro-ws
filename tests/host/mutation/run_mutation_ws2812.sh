#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : ws2812_rmt 编码器变异测试 / mutation testing for WS2812 encoder
# WHY  : 测试全绿不等于测试有效——主动改坏时序/顺序/电平，
#        看 test_ws2812 能否抓到（ai-code-testing 阶段 6）
# WHO  : tests/host/run_all.sh（或手工执行）
# WHERE: esp32-retro-ws/tests/host/mutation/run_mutation_ws2812.sh
# WHEN : 2026-10-04 晚新增
# HOW  : 对 ws2812_rmt.c 副本逐一注入单点变异（sed 表驱动），
#        重编 test_ws2812 后运行；退出非零/崩溃 = 变异被杀；
#        门禁：杀死率 >= 80%

set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/src/nuttx/esp32s3/driver/ws2812_rmt.c"
HDRDIR="$ROOT/src/nuttx/esp32s3/driver"
MUTDIR="$(mktemp -d /tmp/ws2812_mut.XXXXXX)"
CC=${CC:-gcc}

build_test() {  # $1 = 被测源文件路径, $2 = 输出二进制
    $CC -Wall -Wextra -g -fsanitize=address,undefined \
        -I "$ROOT/tests/host/stubs" -I "$HDRDIR" \
        "$ROOT/tests/host/test_ws2812.c" "$1" -o "$2" 2>/dev/null
}

# 变异表：描述|sed 表达式（对 ws2812_rmt.c 副本单点注入）
MUTATIONS=(
  'T0H 时长换成 T1H|s/WS2812_T0H_TICKS       28/WS2812_T0H_TICKS       72/'
  'T1H 时长换成 T0H|s/WS2812_T1H_TICKS       72/WS2812_T1H_TICKS       28/'
  'T0L 时长换成 T0H|s/WS2812_T0L_TICKS       72/WS2812_T0L_TICKS       28/'
  'T1L 时长换成 T1H|s/WS2812_T1L_TICKS       28/WS2812_T1L_TICKS       72/'
  '相位0 电平位丢失|s/(RMT_LEVEL_BIT | WS2812_T0H_TICKS)/(WS2812_T0H_TICKS)/'
  'GRB 换成 RRB|s/n += encode_byte_msb(g, words + n);/n += encode_byte_msb(r, words + n);/'
  'MSB-first 换 LSB-first|s/uint8_t mask = 0x80;/uint8_t mask = 0x01;/'
  '移位方向反|s/mask >>= 1;/mask <<= 1;/'
  '复位符号变高电平|s/words\[n++\] = (uint32_t)WS2812_RESET_TICKS << 16 | WS2812_RESET_TICKS;/words[n++] = 0x80008000u | ((uint32_t)WS2812_RESET_TICKS << 16) | WS2812_RESET_TICKS;/'
  '复位符号不入序列|s/words\[n++\] = (uint32_t)WS2812_RESET_TICKS << 16/words[n] = (uint32_t)WS2812_RESET_TICKS << 16/'
  '缓冲不足不报错|s/return -EINVAL;/return 0;/'
  '容量检查放水|s/max_words < WS2812_WORDS_TOTAL/max_words < WS2812_WORDS_TOTAL - 25/'
)

total=0
killed=0
survived=()

for entry in "${MUTATIONS[@]}"; do
    desc="${entry%%|*}"
    sedexpr="${entry#*|}"

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
echo "[ws2812] 变异总数: $total  杀死: $killed  存活: $((total - killed))"
echo "杀死率: ${rate}%  (门禁 >= 80%)"
if [ ${#survived[@]} -gt 0 ]; then
    echo "存活变异（测试漏洞，需补测试）:"
    for s in "${survived[@]}"; do echo "  - $s"; done
fi

rm -rf "$MUTDIR"
[ "$rate" -ge 80 ] && echo "WS2812-MUTATION-PASS" || echo "WS2812-MUTATION-FAIL"
[ "$rate" -ge 80 ] || exit 1
