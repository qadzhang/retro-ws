#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : ROM 应用模块构建编排——按板名单把应用编成 .rmo XIP 模块、
#        打 .rpk 包、生成 /rom/pkg ROMFS 镜像与（动态档）符号表
# WHY  : 应用/系统分离（2026-10-06）：所有应用包化、构建期离线
#        直接安装入 ROM（db/ 预装层）；模块代码 Flash 原址执行不整包入 RAM
# WHO  : scripts/firmware/build_firmware.sh（stage/finalize 两阶段调用）
# WHERE: retro-ws/scripts/build_romapps.sh
# WHEN : 2026-10-06 新增
# HOW  : 全板静态绑定档（2026-10-06 宿主端到端测试定稿的修订设计）：
#   动态重定位（GOT 补丁）要求 RW 段与 text 保持链接期相对距离——
#   x86-64/ARM/RISC-V 的 GOT 访问均为 PC 相对（宿主实测 RIP+disp 证据），
#   RO 在 flash / RW 搬 RAM 的分段放置会打断寻址；NuttX NXFLAT 靠
#   寄存器基址 PIC + GPL 专用 ldnxflat 解决（许可证红线不入）。
#   故五板统一：pass0 测尺寸 -> 固件 pass1 定镜像地址 F/arena 基址
#   -> defsym 烘焙全部外部符号与段地址重链（重定位归零、尺寸不变）
#   -> 重镜像。ro 段原址 XIP 零拷贝，RW 段直拷 arena 固定槽。
#
# 用法: build_romapps.sh <s3|s3n8|cam|c3|pico> stage|finalize
#   stage    ：固件 configure+olddefconfig 后、make 前调用
#   finalize ：固件 pass1 make 后调用（触发 pass2 增量链接）

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NUTTX="$ROOT/deps/nuttx"
APPS="$ROOT/deps/nuttx-apps"
TOOLS="$ROOT/deps/esp-idf-tools"
PKGS="$ROOT/firmware/packages"
STAGE="$ROOT/firmware/packages/.stage"

BOARD="$1"
MODE="$2"

XT="$TOOLS/xtensa/bin"
RV="$TOOLS/riscv/bin"
ARM="$TOOLS/arm/bin"

case "$BOARD" in
    s3|s3n8|cam)
        CC="$XT/xtensa-esp-elf-gcc"; NM="$XT/xtensa-esp-elf-nm"
        READELF="$XT/xtensa-esp-elf-readelf"
        FLAVOR=static; SECTION=".flash.text"
        ARCHFLAGS="-mlongcalls" ;;
    c3)
        CC="$RV/riscv32-esp-elf-gcc"; NM="$RV/riscv32-esp-elf-nm"
        READELF="$RV/riscv32-esp-elf-readelf"
        FLAVOR=static; SECTION=".flash.text"
        ARCHFLAGS="-msmall-data-limit=0 -mno-relax" ;;
    pico)
        CC="$ARM/arm-none-eabi-gcc"; NM="$ARM/arm-none-eabi-nm"
        READELF="$ARM/arm-none-eabi-readelf"
        FLAVOR=static; SECTION=""
        ARCHFLAGS="-mcpu=cortex-m0plus -mthumb" ;;
    *) echo "用法: build_romapps.sh <s3|s3n8|cam|c3|pico> stage|finalize" >&2; exit 1 ;;
esac
case "$MODE" in
    stage|finalize) ;;
    *) echo "模式须为 stage 或 finalize" >&2; exit 1 ;;
esac

log() { echo -e "\033[0;34m[romapps]\033[0m $1"; }
die() { echo -e "\033[0;31m[romapps-ERR]\033[0m $1" >&2; exit 1; }

LIST="$PKGS/$BOARD.list"
[ -f "$LIST" ] || die "名单缺失: $LIST"
[ -x "$CC" ]   || die "工具链缺失: $CC"
[ -f "$NUTTX/.config" ] || die "固件未配置（先 configure+olddefconfig）"

read_list() { grep -v '^#' "$LIST" | grep -v '^[[:space:]]*$'; }

# ---- 模块编译公共旗标（GUI 应用需 LVGL 头 + 固件 config.h） ----
LVGL_INC="-I $ROOT/deps -I $ROOT/deps/nuttx/include -DLV_CONF_INCLUDE_SIMPLE \
 -DCONFIG_LVGL_FONT_12=1 -DCONFIG_LVGL=1 \
 -I $ROOT/src/lvgl -I $ROOT/src/lvgl/fonts -I $ROOT/src/lvgl/app \
 -I $ROOT/src/nuttx/common -I $ROOT/src/nuttx/common/apps/system -I $ROOT"

COMMON_C="-fno-builtin -fno-stack-protector -ffunction-sections -fdata-sections"

# 静态绑定档统一旗标：-fno-pic（绝对地址烘焙）+ 板级 ABI 旗标；
# 直链 ET_EXEC（-shared 会引入 PLT——Thumb-1 不支持；本档零重定位，
# 程序头/符号表由链接脚本与默认布局提供）
MODCFLAGS="-fno-pic $COMMON_C $ARCHFLAGS"
# --no-relax：Xtensa ld 的 relax（长调用收缩/蹦床，含窗口调用跨
# 1GB 检查）依赖最终地址——pass0 占位与 pass1 真实地址松弛机会不同
# -> 文件尺寸漂移（实测 editor 24196 -> 20824/24920）；禁用换两遍
# 确定性（模块略大的一次性代价）。窗口调用占位值须与真实符号同在
# 0x40000000-0x4FFFFFFF 象限（跨 1GB 为硬错误）
MODLDFLAGS="-nostdlib -Wl,--build-id=none -Wl,--no-undefined -Wl,-e,main -Wl,-z,max-page-size=0x1000 -Wl,--no-relax"
STATIC_DEFS_MODE=1

gen_ldscript() {
    # 全收纳布局：孤儿节（.dynsym/.dynstr/.rela.* 等）若不显式归位，
    # 会被 ld 插到 ARENA_BASE 之后挤占数据区（宿主测试实测踩坑）
    cat > "$1" <<'LDEOF'
SECTIONS {
  . = TEXT_BASE;
  .text : { *(.text .text.* .rodata .rodata.* .literal .literal.* .init .fini) }
  .dynro : { *(.dynsym .dynstr .hash .gnu.hash .rela.dyn .rela.plt .rel.dyn .rel.plt .init_array .fini_array) }
  . = ARENA_BASE;
  .rw : { *(.dynamic) *(.data.rel.ro .data.rel.ro.* .got .got.* .got.plt .data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) }
}
LDEOF
}

# 编译一个包的全部源到 obj 目录，输出 obj 清单到 $build/objs.txt，
# 未定义符号到 $build/undef.txt
compile_pkg() {  # $1=包名
    local pkg="$1"
    local dir="$PKGS/pkgs/$pkg"
    local build="$STAGE/$BOARD/obj/$pkg"
    local srcs

    [ -f "$dir/recipe.conf" ] || die "包配方缺失: $dir/recipe.conf"
    # 配方变量先清零再 source：缺失行不得残留上一包的值
    # （实测：minesweeper 无 MODSRC 行，残留 recorder 的 module.c -> 描述符重复定义）
    SRCS=""; MODSRC=""; MODTYPE=""
    . "$dir/recipe.conf"                       # SRCS/MODSRC/MODTYPE
    mkdir -p "$build"

    srcs="$SRCS"
    [ "$MODTYPE" = gui ] && [ -n "$MODSRC" ] && srcs="$MODSRC $SRCS"

    : > "$build/objs.txt"
    for s in $srcs; do
        local o="$build/$(echo "$s" | tr '/' '_').o"
        $CC $MODCFLAGS $MODINC -c "$ROOT/$s" -o "$o"
        echo "$o" >> "$build/objs.txt"
    done

    # 未定义符号集（--defsym 解析源）：nm -u 逐行两列、长符号名不换行
    # （readelf -s 会折行丢列——lv_* 长名实测漏收集）；须扣除同模块内
    # 其他目标文件已定义者（如 module.c 引用 app_editor.c 的 *_create），
    # 否则 defsym 拿着模块内符号去固件里找必然缺
    cat "$build"/objs.txt | xargs $NM --undefined-only 2>/dev/null | \
        awk 'NF==2 && $1=="U"{print $2}' | sort -u > "$build/undef_all.txt"
    cat "$build"/objs.txt | xargs $NM --defined-only 2>/dev/null | \
        awk 'NF>=3{print $NF}' | sort -u > "$build/mod_defs.txt"
    comm -23 "$build/undef_all.txt" "$build/mod_defs.txt" | \
        sed 's/\[.*$//' | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | \
        sort -u > "$build/undef.txt"
}

#---------------- stage：编译 + 打包 + 镜像（pass0） ----------------
do_stage() {
    rm -rf "$STAGE/$BOARD"
    mkdir -p "$STAGE/$BOARD/rom/bin" "$STAGE/$BOARD/pkgs"
    : > "$STAGE/$BOARD/slots.txt"

    # 模块用 config.h 等价物：固件 make 前尚未生成 include/nuttx/config.h
    # （首次干净构建的鸡蛋问题），从 .config 现场转换（y->1 / =值 / 注释跳过）
    local geninc="$STAGE/$BOARD/nuttx_inc/nuttx"
    mkdir -p "$geninc"
    {
        echo "/* 自动生成：模块编译用 .config 等价物（build_romapps.sh） */"
        echo "#ifndef __ROMMOD_CONFIG_H"
        echo "#define __ROMMOD_CONFIG_H"
        # CONFIG_INIT_ENTRYPOINT 例外：sys/types.h 顶部有同名函数声明
        # （int CONFIG_INIT_ENTRYPOINT(int, ...)），字符串宏会炸掉它——
        # 模块用不到入口符号，直接跳过
        awk -F= '/^CONFIG_[A-Za-z0-9_]+=/ && $1!="CONFIG_INIT_ENTRYPOINT"{
            v=$2; if (v=="y") v=1;
            print "#define " $1 " " v
        }' "$NUTTX/.config"
        echo "#endif"
    } > "$geninc/config.h"
    MODINC="-I $STAGE/$BOARD/nuttx_inc $LVGL_INC"
    # 清上一板的生成物（换板构建防串台：pkg_romfs.c 即将重生成，
    # rom_symtab.c 为动态档遗留物，本流程已不用）
    rm -f "$APPS/retro/rom_symtab.c"

    local arch
    arch=$(grep 'CONFIG_RETRO_ARCH=' "$NUTTX/.config" | cut -d'"' -f2)

    while read -r pkg; do
        log "编译模块: $pkg（$FLAVOR 档 pass0）"
        compile_pkg "$pkg"

        local build="$STAGE/$BOARD/obj/$pkg"
        local objs
        objs=$(tr '\n' ' ' < "$build/objs.txt")

        # pass0：defsym 占位（只为定尺寸/定布局，值不入产物）——地址
        # 按符号名哈希互异（Xtensa l32r 字面量池按精确值去重：全同值
        # 会最大化合并，pass1 真实地址各异导致池结构漂移 -> 尺寸漂移；
        # 互异占位使两遍"相等关系"一致，池行为一致）
        gen_ldscript "$build/mod.ld"
        local defs=""
        while read -r u; do
            [ -z "$u" ] && continue
            local ph
            ph=$(python3 -c "print(hex(0x40000000 | (zlib.crc32(b'$u') & 0x3FFFFFFF)))" 2>/dev/null || \
                 python3 -c "import zlib;print(hex(0x40000000 | (zlib.crc32('$u'.encode()) & 0x3FFFFFFF)))")
            defs="$defs -Wl,--defsym=$u=$ph"
        done < "$build/undef.txt"
        $CC $MODLDFLAGS -Wl,-T,"$build/mod.ld" $defs \
            -Wl,--defsym=TEXT_BASE=0x42400000 \
            -Wl,--defsym=ARENA_BASE=0x42500000 \
            -o "$STAGE/$BOARD/rom/bin/$pkg" $objs

        # 固定槽：pad 到 align(size+4096, 4096)。Xtensa ld 存在残余
        # 地址敏感布局（--no-relax 后仍实测漂移），与其强求两遍字节
        # 全等，不如把镜像内占位钉死——finalize 重链只需落槽（尾零
        # 填充对 rommod 无害：ELF 解析按节表偏移走），镜像布局/F/
        # 烘焙地址一次收敛，无需迭代
        local raw slot
        raw=$(stat -c%s "$STAGE/$BOARD/rom/bin/$pkg")
        slot=$(( (raw + 4096 + 4095) & ~4095 ))
        truncate -s "$slot" "$STAGE/$BOARD/rom/bin/$pkg"
        echo "$pkg $slot" >> "$STAGE/$BOARD/slots.txt"
    done < <(read_list)

    # 符号保持器：模块净未定义符号中"固件应提供者"的引用数组——
    # 应用抽离后固件自身可能不再引用这些 LVGL/libc 符号（唯一调用方
    # 在模块里），--gc-sections 会回收 -> 静态绑定 defsym 找不到。
    # 排除跨模块符号（定义在其他 .rmo 里，固件中本就不存在，extern
    # 进来反而把固件链接搞挂）；retro_boot 对数组持强引用形成 gc 根链
    cat "$STAGE/$BOARD"/obj/*/mod_defs.txt | sort -u \
        > "$STAGE/$BOARD/all_mod_defs.txt"
    cat "$STAGE/$BOARD"/obj/*/undef.txt | sort -u \
        > "$STAGE/$BOARD/all_undef.txt"
    comm -23 "$STAGE/$BOARD/all_undef.txt" \
             "$STAGE/$BOARD/all_mod_defs.txt" \
        > "$STAGE/$BOARD/fw_need.txt"
    {
        echo "/*"
        echo " * SPDX-FileCopyrightText: 2026 Retro WS Project"
        echo " * SPDX-License-Identifier: Apache-2.0"
        echo " */"
        echo "/* rom_keep.c - ROM 模块符号保持器（自动生成，勿手改） */"
        echo "/* 生成: build_romapps.sh stage（.rmo 所需固件符号；跨模块符号除外） */"
        echo "#include <nuttx/config.h>"
        awk '{printf("extern const unsigned char %s;\n", $0)}' \
            "$STAGE/$BOARD/fw_need.txt"
        echo "const void *const g_rom_keep[] ="
        echo "{"
        awk '{printf("    &%s,\n", $0)}' "$STAGE/$BOARD/fw_need.txt"
        echo "    0"
        echo "};"
    } > "$APPS/retro/rom_keep.c"
    log "符号保持器：$(grep -c '^    &' "$APPS/retro/rom_keep.c") 个固件符号"

    # 包源 control 按板改写 Arch（离线安装 DB 的快照来源；
    # .rpk 容器中间态取消——2026-10-06 策略修订：构建期直接安装，
    # 镜像只含 bin/ 载荷 + db/ 数据库，不再有 seed/安装环节）
    mkdir -p "$STAGE/$BOARD/pkgs"
    while read -r pkg; do
        local psrc="$STAGE/$BOARD/pkgs/$pkg"
        mkdir -p "$psrc/data"
        sed "s/^Arch: .*/Arch: ${arch:-all}/" \
            "$PKGS/pkgs/$pkg/control" > "$psrc/control"
        [ -d "$PKGS/pkgs/$pkg/data" ] && \
            cp -r "$PKGS/pkgs/$pkg/data/." "$psrc/data/"
    done < <(read_list)

    gen_romfs

    log "stage 完成（$FLAVOR 档；镜像暂只含 bin/——db/ 由 finalize 离线安装生成）"
    [ "$FLAVOR" = static ] && \
        log "finalize 将以固件 pass1 地址（镜像 F + arena）重链全部模块"
}

gen_romfs() {
    # ROMFS 镜像 C 源（xtensa/c3 放可执行 flash 段 .flash.text）
    local sect_args=()
    [ -n "$SECTION" ] && sect_args=(--section "$SECTION")
    python3 "$ROOT/tools/mkromfs.py" --tree "$STAGE/$BOARD/rom" \
        "$APPS/retro/pkg_romfs.c" pkg --symname g_pkg_romfs "${sect_args[@]}"
}

#---------------- finalize：pass1 后收尾 ----------------
do_finalize() {
    local elf="$NUTTX/nuttx"
    [ -f "$elf" ] || die "固件 pass1 ELF 缺失: $elf"

    # ---- 静态绑定档（全板统一）：以 pass1 真实地址重链 ----
    local F AB
    F=$(nm "$elf" | awk '$3=="g_pkg_romfs"{print "0x"$1; exit}')
    [ -n "$F" ] || die "pass1 ELF 无 g_pkg_romfs（pkg_romfs.c 未编入？）"
    AB=$(nm "$elf" | awk '$3=="g_rommod_arena"{print "0x"$1; exit}')
    [ -n "$AB" ] || die "pass1 ELF 无 g_rommod_arena（RETRO_ROMMOD_ARENA_SIZE=0？）"

    # 各模块在镜像内的数据偏移（与 --tree 布局同源）
    python3 "$ROOT/tools/mkromfs.py" --tree "$STAGE/$BOARD/rom" /dev/null \
        pkg --print-offsets > "$STAGE/$BOARD/offsets.txt"

    # arena 偏移规划（名单顺序切 16 对齐 RW 段）
    local off=0 plan="$STAGE/$BOARD/arena_plan.txt"
    : > "$plan"
    while read -r pkg; do
        local rwmem
        rwmem=$($READELF -lW "$STAGE/$BOARD/rom/bin/$pkg" | \
                grep -w 'RW' | awk '{print $6}' | head -1)
        # 无全局可写数据的模块没有 RW 段（合法：arena 占 0 字节）
        rwmem=${rwmem:-0}
        rwmem=$((rwmem))                       # 0x 十六进制转十进制
        # arena 槽 4096 对齐 + 一页余量（RW 段重链膨胀缓冲）
        rwmem=$(( (rwmem + 4095) & ~4095 ))
        rwmem=$(( rwmem + 4096 ))
        local aligned=$(( (off + 4095) & ~4095 ))
        echo "$pkg $aligned $rwmem" >> "$plan"
        off=$(( aligned + rwmem ))
    done < <(read_list)

    local arena_sz
    arena_sz=$(grep 'CONFIG_RETRO_ROMMOD_ARENA_SIZE=' "$NUTTX/.config" | cut -d= -f2)
    arena_sz=${arena_sz:-0}
    [ "$off" -le "$arena_sz" ] || \
        die "arena 需求 $off > 配置 $arena_sz（调大 RETRO_ROMMOD_ARENA_SIZE）"

    # 固件符号表（defsym 解析源；缺符号即失败——防上板运行期未定义）
    nm --defined-only "$elf" | awk 'NF==3{print $3, $1}' | sort -u \
        > "$STAGE/$BOARD/fwsyms.txt"

    # 跨模块符号表：模块 A 调模块 B 的导出函数（如 recorder ->
    # player_play_file）。pass0 链接基址 TEXT0=0x42400000，TEXT 区符号
    # 最终地址 = 该模块 text_base + (sym - TEXT0)；布局在 pass0 已定死，
    # 无循环依赖（互相引用不支持，直接报错）
    # 注：符号过滤用 python（mawk 无 strtonum，gawk 不可依赖）
    TEXT0=$((0x42400000))
    : > "$STAGE/$BOARD/modsyms.txt"
    while read -r pkg; do
        local mod_bin="$STAGE/$BOARD/rom/bin/$pkg"
        local doff0 p_off0 tbase0 rxlen0
        doff0=$(awk -v p="bin/$pkg" '$1==p{print $2}' "$STAGE/$BOARD/offsets.txt")
        p_off0=$($READELF -lW "$mod_bin" | grep ' R E ' | awk '{print $2}' | head -1)
        p_off0=$((p_off0))
        rxlen0=$($READELF -lW "$mod_bin" | grep ' R E ' | awk '{print $5}' | head -1)
        rxlen0=$((rxlen0))
        tbase0=$((F + doff0 + p_off0))
        $NM --defined-only "$mod_bin" | python3 -c "
import sys
base, t0, lo, hi = $tbase0, $TEXT0, $TEXT0, $TEXT0 + $rxlen0
for ln in sys.stdin:
    p = ln.split()
    if len(p) == 3:
        a = int(p[0], 16)
        if lo <= a < hi:
            print('%s 0x%x' % (p[2], base + a - t0))
" >> "$STAGE/$BOARD/modsyms.txt"
    done < <(read_list)
    sort -u "$STAGE/$BOARD/modsyms.txt" -o "$STAGE/$BOARD/modsyms.txt"
    log "跨模块符号：$(grep -c . "$STAGE/$BOARD/modsyms.txt") 个（TEXT 区导出）"

    while read -r pkg; do
        local doff p_off text_base arena_off build objs defs u addr
        build="$STAGE/$BOARD/obj/$pkg"

        doff=$(awk -v p="bin/$pkg" '$1==p{print $2}' "$STAGE/$BOARD/offsets.txt")
        [ -n "$doff" ] || die "偏移表缺 bin/$pkg"
        p_off=$($READELF -lW "$STAGE/$BOARD/rom/bin/$pkg" | \
                grep ' R E ' | awk '{print $2}' | head -1)
        [ -n "$p_off" ] || die "$pkg 无 R E 段"
        p_off=$((p_off))
        arena_off=$(awk -v p="$pkg" '$1==p{print $2}' "$plan")
        text_base=$(python3 -c "print(hex($F + $doff + $p_off))")

        objs=$(tr '\n' ' ' < "$build/objs.txt")
        defs=""
        while read -r u; do
            [ -z "$u" ] && continue
            addr=$(awk -v s="$u" '$1==s{print "0x"$2; exit}' "$STAGE/$BOARD/fwsyms.txt")
            if [ -z "$addr" ]; then
                # 回落：跨模块符号（其他 .rmo 的 TEXT 区导出；
                # modsyms 行已是 0x 前缀——勿重复加）
                addr=$(awk -v s="$u" '$1==s{print $2; exit}' \
                       "$STAGE/$BOARD/modsyms.txt")
                addr=${addr:-}
            fi
            [ -n "$addr" ] || die "无处解析符号: $u（固件与模块导出均无；$pkg）"
            defs="$defs -Wl,--defsym=$u=$addr"
        done < "$build/undef.txt"

        $CC $MODLDFLAGS -Wl,-T,"$build/mod.ld" $defs \
            -Wl,--defsym=TEXT_BASE="$text_base" \
            -Wl,--defsym=ARENA_BASE=$(python3 -c "print(hex($AB + $arena_off))") \
            -o "$STAGE/$BOARD/rom/bin/$pkg.new" $objs

        # 落槽断言：重链产物须在 pass0 定的槽内（尾部零填充无害——
        # rommod 按 ELF 节表解析；镜像占位/偏移/F 恒定）
        local s1 slot
        s1=$(stat -c%s "$STAGE/$BOARD/rom/bin/$pkg.new")
        slot=$(awk -v p="$pkg" '$1==p{print $2; exit}' "$STAGE/$BOARD/slots.txt")
        [ -n "$slot" ] || die "$pkg 无槽记录（slots.txt）"
        [ "$s1" -le "$slot" ] || \
            die "$pkg 重链溢出槽：$s1 > $slot（膨涨超一页余量）"
        truncate -s "$slot" "$STAGE/$BOARD/rom/bin/$pkg.new"
        mv "$STAGE/$BOARD/rom/bin/$pkg.new" "$STAGE/$BOARD/rom/bin/$pkg"
        log "静态重链: $pkg text@$text_base arena+$arena_off（$s1/${slot}B 槽）"
    done < <(read_list)

    # 离线安装（2026-10-06 策略修订）：最终模块 CRC 已定 ->
    # gen_pkgdb 生成预装数据库 db/（pkg_manager 兼容格式）并入镜像；
    # 设备首启零安装动作（ROM 层 DB 直读，卸载走片上 tombstone）
    python3 "$ROOT/tools/gen_pkgdb.py" \
        --rom "$STAGE/$BOARD/rom" \
        --pkgs "$STAGE/$BOARD/pkgs" \
        --list "$LIST" \
        --rom-root /rom/pkg

    # 重生成镜像（bin/ 字典序最前，偏移与 pass0 一致；db/ 只增尾部）
    gen_romfs
    log "finalize 完成（静态档：F=$F arena=$AB 用量 ${off}B/${arena_sz}B；离线安装 $(read_list | wc -l) 包）"
}

case "$MODE" in
    stage)    do_stage ;;
    finalize) do_finalize ;;
esac
