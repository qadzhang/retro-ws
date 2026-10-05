#!/bin/bash
#
# verify.sh - 项目完整性验证脚本
#
# 检查项目文件是否完整、依赖是否齐全
#

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS_DIR="$PROJECT_ROOT/deps"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0

log_pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

echo "========================================"
echo " ESP32-S3 Retro 项目完整性检查"
echo "========================================"
echo ""

# ===== 1. 检查项目结构 =====
log_info "检查项目结构..."

ITEMS=(
    "README.md:项目说明"
    "SYSTEM.md:系统文档"
    "CODING_STANDARD.md:编码规范"
    "scripts/download_deps.sh:下载脚本"
    "scripts/esp32s3/build.sh:ESP32-S3编译脚本"
    "scripts/esp32s3/nuttx_build.sh:ESP32-S3 NuttX编译脚本"
    "scripts/esp32cam/build.sh:ESP32-CAM编译脚本"
    "scripts/esp32cam/nuttx_build.sh:ESP32-CAM NuttX编译脚本"
    "scripts/esp32c3/build.sh:ESP32-C3编译脚本"
    "scripts/esp32c3/nuttx_build.sh:ESP32-C3 NuttX编译脚本"
    "configs/nuttx-defconfig-esp32c3:ESP32-C3内核配置"
    "scripts/setup_env.sh:环境安装脚本"
    "configs/nuttx-defconfig:内核配置"
    "configs/lv_conf.h:LVGL配置"
    "src/README.md:源代码说明"
    "src/nuttx/esp32s3/esp32s3_retro.c:主入口"
    "src/nuttx/esp32s3/Kconfig:配置选项"
    "src/nuttx/esp32s3/board/board.c:板级初始化"
    "src/nuttx/esp32s3/board/board.h:板级定义"
    "src/nuttx/esp32s3/chip/esp32s3.h:芯片定义"
    "src/nuttx/esp32s3/chip/startup.c:启动代码"
    "src/nuttx/esp32s3/driver/cvbs/drv_cvbs.c:显示驱动"
    "src/nuttx/esp32s3/driver/fsk/drv_fsk.c:磁带驱动"
    "src/nuttx/common/driver/cvbs_core.c:CVBS 时序核心"
    "src/nuttx/common/driver/drv_cvbs.c:CVBS 统一驱动"
    "src/nuttx/common/driver/drv_cvbs.h:CVBS 统一接口"
    "src/lvgl/app/desktop.c:LVGL桌面"
)

for item in "${ITEMS[@]}"; do
    file="${item%%:*}"
    desc="${item##*:}"
    if [ -f "$PROJECT_ROOT/$file" ]; then
        log_pass "$desc ($file)"
    else
        log_fail "缺失: $file ($desc)"
    fi
done

# ===== 2. 检查依赖目录 =====
log_info "检查依赖目录..."

DEPS=(
        "deps/nuttx-apps:NuttX Apps"
)

for dep in "${DEPS[@]}"; do
    dir="${dep%%:*}"
    desc="${dep##*:}"
    if [ -d "$PROJECT_ROOT/$dir" ]; then
        log_pass "$desc ($dir)"
    else
        log_fail "缺失: $dir ($desc)"
    fi
done

# ===== 3. 检查工具链 =====
log_info "检查工具链..."

TOOLS=(
    "xtensa-esp32-elf-gcc:Xtensa交叉编译器"
    "esptool.py:烧录工具"
    "cmake:CMake"
    "ninja:Ninja构建"
)

for tool in "${TOOLS[@]}"; do
    cmd="${tool%%:*}"
    desc="${tool##*:}"
    if command -v $cmd &>/dev/null; then
        log_pass "$desc ($cmd)"
    else
        log_warn "未找到: $desc ($cmd) - 需要安装"
    fi
done

# ===== 4. 检查脚本可执行性 =====
log_info "检查脚本权限..."
for script in scripts/*.sh; do
    if [ -f "$PROJECT_ROOT/$script" ]; then
        if [ -x "$PROJECT_ROOT/$script" ]; then
            log_pass "$(basename $script) 可执行"
        else
            chmod +x "$PROJECT_ROOT/$script"
            log_pass "$(basename $script) 已设置可执行"
        fi
    fi
done

# ===== 5. 代码行数统计 =====
log_info "代码统计..."
TOTAL_LINES=$(find "$PROJECT_ROOT/src" -name "*.c" -o -name "*.h" 2>/dev/null | \
    xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
log_info "源代码总行数: $TOTAL_LINES"

DRIVER_COUNT=$(find "$PROJECT_ROOT/src" -path "*/driver/*.c" 2>/dev/null | wc -l)
log_info "驱动模块数: $DRIVER_COUNT"

# ===== 6. 检查文档 =====
log_info "检查文档..."
DOCS=(
    "REQUIREMENTS.md:需求说明书"
    "HARDWARE.md:硬件规格"
    "DEPENDENCIES.md:依赖说明"
)
for doc in "${DOCS[@]}"; do
    path="${doc%%:*}"
    name="${doc#*:}"
    if [ -f "$PROJECT_ROOT/$path" ]; then
        log_pass "$name ($path)"
    else
        log_fail "$name缺失 ($path)"
    fi
done

# ===== 6.1 检查每板硬件档案（引脚唯一事实来源）=====
HW_PROFILES=(
    "src/nuttx/esp32s3/board/hw_esp32s3_devkitc.h:ESP32-S3 DevKitC-1 N16R8/N8R8"
    "src/nuttx/esp32/board/hw_esp32cam_aithinker.h:ESP32-CAM AI-Thinker"
    "src/nuttx/esp32c3/board/hw_esp32c3_luatos.h:合宙 ESP32-C3 核心板"
)
for hw in "${HW_PROFILES[@]}"; do
    path="${hw%%:*}"
    name="${hw#*:}"
    if [ -f "$PROJECT_ROOT/$path" ]; then
        log_pass "硬件档案 ($path)"
    else
        log_fail "硬件档案缺失: $name ($path)"
    fi
done

# ===== 总结 =====
echo ""
echo "========================================"
echo " 检查结果"
echo "========================================"
echo -e "通过: ${GREEN}$PASS${NC}  |  失败: ${RED}$FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✅ 项目完整性检查通过！${NC}"
    echo ""
    echo "下一步 - 编译 NuttX:"
    echo "  source $DEPS_DIR/esp-idf/export.sh"
    echo "  cd $PROJECT_ROOT"
    echo "  ./scripts/nuttx_build.sh build"
    echo ""
else
    echo -e "${YELLOW}⚠️  有 $FAIL 项检查失败${NC}"
    echo "请修复后重新运行"
    echo ""
fi
