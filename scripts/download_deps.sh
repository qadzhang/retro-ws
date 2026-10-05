#!/bin/bash
#
# download_deps.sh - 下载并验证 ESP32-CAM 开发所需开源依赖
#
# 用法: ./download_deps.sh [--verify-only] [--tools-only]
#
# 特性：
#   - 跳过已下载且完整的组件
#   - 下载后自动验证（git仓库/size/关键文件）
#   - 支持断点续传
#   - ESP-IDF 工具链安装到 deps/esp-idf-tools/
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
DEPS_DIR="$PROJECT_ROOT/deps"
DOWNLOAD_DIR="$DEPS_DIR/download"

# ESP-IDF 工具链安装到 deps 目录
export IDF_TOOLS_PATH="$DEPS_DIR/esp-idf-tools"

# 创建下载缓存目录
mkdir -p "$DOWNLOAD_DIR"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()   { echo -e "${RED}[ERR]${NC} $1"; }
log_step()  { echo -e "${BLUE}[STEP]${NC} $1"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $1"; }
log_skip()  { echo -e "${CYAN}[SKIP]${NC} $1"; }

# 参数
VERIFY_ONLY=false
TOOLS_ONLY=false
INSTALL_TOOLS=false
for arg in "$@"; do
    case "$arg" in
        --verify-only) VERIFY_ONLY=true ;;
        --tools-only) TOOLS_ONLY=true ;;
        --install-tools) INSTALL_TOOLS=true ;;
    esac
done

# ===== 验证函数 =====

verify_git_repo() {
    local dir="$1"
    local expected_tag="$2"
    local name="$3"

    if [ ! -d "$dir/.git" ]; then
        log_err "$name: 不是 git 仓库或 .git 目录缺失"
        return 1
    fi

    # 检查 git log 是否可读
    if ! git -C "$dir" log --oneline -1 > /dev/null 2>&1; then
        log_err "$name: git 仓库损坏或仓库不完整"
        return 1
    fi

    # 检查 git checkout 是否正常
    if ! git -C "$dir" status > /dev/null 2>&1; then
        log_err "$name: git 仓库状态异常"
        return 1
    fi

    # 检查关键文件是否存在
    if [ -f "$dir/README.md" ] || [ -f "$dir/README" ] || [ -f "$dir/Makefile" ]; then
        log_ok "$name: 仓库完整"
        return 0
    fi

    log_warn "$name: 仓库存在但未找到 README/Makefile"
    return 0
}

verify_nuttx() {
    local dir="$DEPS_DIR/nuttx"
    if [ ! -d "$dir" ]; then
        log_err "NuttX: 目录不存在"
        return 1
    fi

    # 检查关键目录和文件
    local missing=0
    for f in "Kconfig" "Makefile" "tools/configure.sh" "Documentation/NuttX.html"; do
        if [ ! -f "$dir/$f" ] && [ ! -d "$dir/$f" ]; then
            # 有些文件可能不在，但这些关键目录必须有
            :
        fi
    done

    # 主要检查：Kconfig 和 Makefile
    if [ ! -f "$dir/Kconfig" ]; then
        log_err "NuttX: Kconfig 缺失"
        return 1
    fi

    # 检查子模块
    if [ -f "$dir/.gitmodules" ]; then
        log_info "NuttX: 检测到子模块，检查..."
        git -C "$dir" submodule status --recursive 2>/dev/null | grep -q "^[+-]"
        if [ $? -eq 0 ]; then
            log_warn "NuttX: 部分子模块未初始化"
            return 2  # 警告但不视为失败
        fi
    fi

    log_ok "NuttX: 验证通过"
    return 0
}

verify_esp_idf() {
    local dir="$DEPS_DIR/esp-idf"
    if [ ! -d "$dir" ]; then
        log_err "ESP-IDF: 目录不存在"
        return 1
    fi

    # 检查关键文件
    if [ ! -f "$dir/tools/tools.json" ]; then
        log_err "ESP-IDF: tools.json 缺失"
        return 1
    fi

    if [ ! -f "$dir/export.sh" ]; then
        log_err "ESP-IDF: export.sh 缺失"
        return 1
    fi

    # 版本验证（version.txt 或 git tag）
    local ver=$(git -C "$dir" describe --tags 2>/dev/null || \
               cat "$dir/version.txt" 2>/dev/null || echo "")
    if [ -z "$ver" ]; then
        log_warn "ESP-IDF: 无法确定版本"
        return 2
    fi

    log_ok "ESP-IDF: 验证通过 (版本: $ver)"
    return 0
}

verify_nuttx_apps() {
    local dir="$DEPS_DIR/nuttx-apps"
    if [ ! -d "$dir" ]; then
        log_err "NuttX Apps: 目录不存在"
        return 1
    fi

    # 检查 Makefile
    if [ ! -f "$dir/Makefile" ]; then
        log_err "NuttX Apps: Makefile 缺失"
        return 1
    fi

    # 检查 Application.mk
    if [ ! -f "$dir/Application.mk" ]; then
        log_warn "NuttX Apps: Application.mk 缺失"
        return 2
    fi

    log_ok "NuttX Apps: 验证通过"
    return 0
}

# ===== 下载函数 =====

download_nuttx() {
    local dir="$DEPS_DIR/nuttx"
    local ver="12.12.0"
    # BFSU 镜像
    local mirror_base="https://mirrors.bfsu.edu.cn/apache/nuttx"
    local github_base="https://github.com/apache/nuttx"
    local src_file="apache-nuttx-${ver}.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "NuttX 已存在，验证..."
        if verify_git_repo "$dir" "$ver" "NuttX"; then
            local current=$(git -C "$dir" describe --tags 2>/dev/null || echo "unknown")
            if [[ "$current" == *"$ver"* ]]; then
                log_ok "NuttX 版本正确: $current"
            else
                log_warn "NuttX 版本: $current (期望 $ver)"
                log_skip "跳过重新下载"
            fi
        else
            log_warn "NuttX 仓库损坏，重新下载..."
            rm -rf "$dir"
        fi
    fi

    if [ ! -d "$dir" ]; then
        log_step "1/9 下载 Apache NuttX $ver..."
        mkdir -p "$DEPS_DIR"

        # 优先使用 BFSU 镜像，下载到缓存目录
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载 (BFSU 镜像)..."
            axel -n 20 -a "${mirror_base}/${ver}/${src_file}" \
                -o "$DOWNLOAD_DIR/$src_file" 2>/dev/null || {
                log_warn "axel BFSU 镜像下载失败，尝试 GitHub..."
                axel -n 20 -a "${github_base}/releases/download/nuttx-${ver}/${src_file}" \
                    -o "$DOWNLOAD_DIR/$src_file"
            }
        else
            wget -q "${mirror_base}/${ver}/${src_file}" \
                -O "$DOWNLOAD_DIR/$src_file" || \
            wget -q "${github_base}/releases/download/nuttx-${ver}/${src_file}" \
                -O "$DOWNLOAD_DIR/$src_file"
        fi

        if [ -f "$DOWNLOAD_DIR/$src_file" ] && [ -s "$DOWNLOAD_DIR/$src_file" ]; then
            tar -xzf "$DOWNLOAD_DIR/$src_file" -C "$DEPS_DIR"
            mv "$DEPS_DIR/apache-nuttx-$ver" "$dir"
            log_ok "NuttX 下载完成（缓存: $DOWNLOAD_DIR/$src_file）"
        elif [ -f "$DEPS_DIR/$src_file" ] && [ -s "$DEPS_DIR/$src_file" ]; then
            # 兼容旧位置
            tar -xzf "$DEPS_DIR/$src_file" -C "$DEPS_DIR"
            mv "$DEPS_DIR/apache-nuttx-$ver" "$dir"
            rm -f "$DEPS_DIR/$src_file"
            log_ok "NuttX 下载完成（缓存: $DOWNLOAD_DIR/$src_file）"
        else
            log_err "NuttX 下载失败，尝试 git clone..."
            git clone --depth=1 --branch="nuttx-${ver}" \
                https://github.com/apache/nuttx.git "$dir"
        fi
    fi

    # 初始化子模块（如果需要）
    if [ -f "$dir/.gitmodules" ]; then
        log_info "NuttX: 初始化子模块（后台进行，可能需要几分钟）..."
        git -C "$dir" submodule update --init --recursive 2>&1 | tail -3 &
    fi
}

download_nuttx_apps() {
    local dir="$DEPS_DIR/nuttx-apps"
    local ver="12.12.0"
    # BFSU 镜像
    local mirror_base="https://mirrors.bfsu.edu.cn/apache/nuttx"
    local github_base="https://github.com/apache/nuttx-apps"
    local src_file="apache-nuttx-apps-${ver}.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "NuttX Apps 已存在，验证..."
        if verify_nuttx_apps "$dir"; then
            local current=$(git -C "$dir" describe --tags 2>/dev/null || echo "unknown")
            if [[ "$current" == *"$ver"* ]]; then
                log_ok "NuttX Apps 版本正确: $current"
            else
                log_warn "NuttX Apps 版本: $current (期望 $ver)"
                log_skip "跳过重新下载"
            fi
        else
            log_warn "NuttX Apps 仓库异常，重新下载..."
            rm -rf "$dir"
        fi
    fi

    if [ ! -d "$dir" ]; then
        log_step "2/9 下载 NuttX Apps $ver..."

        # 优先使用 BFSU 镜像，下载到缓存目录
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载 (BFSU 镜像)..."
            axel -n 20 -a "${mirror_base}/${ver}/${src_file}" \
                -o "$DOWNLOAD_DIR/$src_file" 2>/dev/null || {
                log_warn "axel BFSU 镜像下载失败，尝试 GitHub..."
                axel -n 20 -a "${github_base}/releases/download/nuttx-${ver}/${src_file}" \
                    -o "$DOWNLOAD_DIR/$src_file"
            }
        else
            wget -q "${mirror_base}/${ver}/${src_file}" \
                -O "$DOWNLOAD_DIR/$src_file" || \
            wget -q "${github_base}/releases/download/nuttx-${ver}/${src_file}" \
                -O "$DOWNLOAD_DIR/$src_file"
        fi

        if [ -f "$DOWNLOAD_DIR/$src_file" ] && [ -s "$DOWNLOAD_DIR/$src_file" ]; then
            tar -xzf "$DOWNLOAD_DIR/$src_file" -C "$DEPS_DIR"
            mv "$DEPS_DIR/apache-nuttx-apps-$ver" "$dir"
            log_ok "NuttX Apps 下载完成（缓存: $DOWNLOAD_DIR/$src_file）"
        elif [ -f "$DEPS_DIR/$src_file" ] && [ -s "$DEPS_DIR/$src_file" ]; then
            # 兼容旧位置
            tar -xzf "$DEPS_DIR/$src_file" -C "$DEPS_DIR"
            mv "$DEPS_DIR/apache-nuttx-apps-$ver" "$dir"
            rm -f "$DEPS_DIR/$src_file"
            log_ok "NuttX Apps 下载完成（缓存: $DOWNLOAD_DIR/$src_file）"
        else
            log_err "NuttX Apps 下载失败，尝试 git clone..."
            git clone --depth=1 --branch="nuttx-${ver}" \
                https://github.com/apache/nuttx-apps.git "$dir"
        fi
    fi
}

download_esp_idf() {
    local dir="$DEPS_DIR/esp-idf"
    local ver="v5.5.4"

    if [ -d "$dir" ]; then
        log_skip "ESP-IDF 已存在，验证..."
        if verify_esp_idf "$dir"; then
            local current=$(git -C "$dir" describe --tags 2>/dev/null || \
                          cat "$dir/version.txt" 2>/dev/null || echo "unknown")
            if [[ "$current" == *"$ver"* ]]; then
                log_ok "ESP-IDF 版本正确: $current"
            else
                log_warn "ESP-IDF 版本: $current (期望 $ver)"
                log_skip "跳过重新下载"
            fi
        else
            log_warn "ESP-IDF 仓库异常，重新下载..."
            rm -rf "$dir"
        fi
    fi

    if [ ! -d "$dir" ]; then
        log_step "3/9 下载 ESP-IDF $ver..."

        local esp_idf_tar="esp-idf-${ver}.tar.gz"
        # 优先使用 Gitee 镜像加速 git clone
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载..."
            axel -n 20 -a "https://github.com/espressif/esp-idf/archive/refs/tags/${ver}.tar.gz" -o "$DOWNLOAD_DIR/$esp_idf_tar" 2>/dev/null || {
                log_warn "axel 下载失败，尝试 Gitee 镜像..."
                axel -n 20 -a "https://gitee.com/EspressifSystems/esp-idf/archive/refs/tags/${ver}.tar.gz" -o "$DOWNLOAD_DIR/$esp_idf_tar" 2>/dev/null
            }
            if [ -f "$DOWNLOAD_DIR/$esp_idf_tar" ]; then
                mkdir -p "$dir"
                tar -xzf "$DOWNLOAD_DIR/$esp_idf_tar" -C "$dir" --strip-components=1
                log_ok "ESP-IDF 下载完成（缓存: $DOWNLOAD_DIR/$esp_idf_tar）"
            else
                log_err "下载失败"
            fi
        else
            # 使用 Gitee 镜像 clone（国内加速）
            log_info "使用 Gitee 镜像加速 clone..."
            git clone --recursive -b "$ver" \
                https://gitee.com/EspressifSystems/esp-idf.git "$dir" 2>/dev/null || \
            git clone --recursive -b "$ver" \
                https://github.com/espressif/esp-idf.git "$dir"
            log_ok "ESP-IDF 下载完成"
        fi
    fi

    # 安装 Python 依赖（如果需要）
    if [ -f "$dir/requirements.txt" ]; then
        log_info "安装 ESP-IDF Python 依赖..."
        pip3 install --break-system-packages -r "$dir/requirements.txt" \
            2>&1 | tail -3 &
    fi
}

download_lvgl() {
    local dir="$DEPS_DIR/lvgl"
    local tag="v9.5.0"
    local tarfile="lvgl-${tag}.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "LVGL 已存在，验证..."
        local current=$(git -C "$dir" describe --tags 2>/dev/null || echo "unknown")
        if [[ "$current" == *"$tag"* ]]; then
            log_ok "LVGL 版本正确: $current"
        else
            log_warn "LVGL 版本: $current (期望 $tag)"
            log_skip "跳过重新下载"
        fi
    fi

    if [ ! -d "$dir" ]; then
        log_step "4/9 下载 LVGL $tag..."
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载..."
            axel -n 20 -a "https://github.com/lvgl/lvgl/archive/refs/tags/${tag}.tar.gz" -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
            wget -q "https://github.com/lvgl/lvgl/archive/refs/tags/${tag}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        else
            wget -q "https://github.com/lvgl/lvgl/archive/refs/tags/${tag}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        fi
        if [ -f "$DOWNLOAD_DIR/$tarfile" ]; then
            mkdir -p "$dir"
            tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$dir" --strip-components=1
            log_ok "LVGL 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"
        else
            log_err "LVGL 下载失败"
        fi
    fi
}

download_duktape() {
    local dir="$DEPS_DIR/duktape"
    local tag="v2.7.0"
    local tarfile="duktape-${tag}.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "Duktape 已存在，验证..."
        local current=$(git -C "$dir" describe --tags 2>/dev/null || echo "unknown")
        if [[ "$current" == *"$tag"* ]]; then
            log_ok "Duktape 版本正确: $current"
        else
            log_warn "Duktape 版本: $current (期望 $tag)"
            log_skip "跳过重新下载"
        fi
    fi

    if [ ! -d "$dir" ]; then
        log_step "5/9 下载 Duktape $tag..."
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载..."
            axel -n 20 -a "https://github.com/svaarala/duktape/archive/refs/tags/${tag}.tar.gz" -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
            wget -q "https://github.com/svaarala/duktape/archive/refs/tags/${tag}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        else
            wget -q "https://github.com/svaarala/duktape/archive/refs/tags/${tag}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        fi
        if [ -f "$DOWNLOAD_DIR/$tarfile" ]; then
            mkdir -p "$dir"
            tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$dir" --strip-components=1
            log_ok "Duktape 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"
        else
            log_err "Duktape 下载失败"
        fi
    fi
}

download_mybasic() {
    local dir="$DEPS_DIR/my_basic"
    local repo="https://github.com/paladin-t/my_basic"
    local tarfile="my_basic-master.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "my_basic 已存在，跳过"
    else
        log_step "7/9 下载 my_basic..."
        if command -v axel &> /dev/null; then
            log_info "使用 axel 多线程下载..."
            axel -n 20 -a "https://github.com/paladin-t/my_basic/archive/refs/heads/master.tar.gz" -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
            wget -q "https://github.com/paladin-t/my_basic/archive/refs/heads/master.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        else
            wget -q "https://github.com/paladin-t/my_basic/archive/refs/heads/master.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        fi
        if [ -f "$DOWNLOAD_DIR/$tarfile" ]; then
            mkdir -p "$dir"
            tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$dir" --strip-components=1
            log_ok "my_basic 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"
        else
            git clone --depth=1 "$repo" "$dir"
            log_ok "my_basic 下载完成（git clone）"
        fi
    fi
}

download_jslogo() {
    local dir="$DEPS_DIR/jslogo"
    local repo="https://github.com/inexorabletash/jslogo"
    local tarfile="jslogo-master.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "jslogo 已存在，跳过"
    else
        log_step "下载 jslogo (UCBLogo 子集, Apache-2.0)..."
        # 可选脚本引擎：仅 CONFIG_RETRO_LOGO_JSLOGO=y 时需要
        if command -v axel &> /dev/null; then
            axel -n 20 -a "https://github.com/inexorabletash/jslogo/archive/refs/heads/master.tar.gz" -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
            wget -q "https://github.com/inexorabletash/jslogo/archive/refs/heads/master.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        else
            wget -q "https://github.com/inexorabletash/jslogo/archive/refs/heads/master.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        fi
        if [ -f "$DOWNLOAD_DIR/$tarfile" ]; then
            mkdir -p "$dir"
            tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$dir" --strip-components=1
            log_ok "jslogo 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"
            log_info "启用方法: menuconfig 勾选 RETRO_LOGO_JSLOGO，并将 $dir 的 JS 源码"
            log_info "拷贝到 SD 卡 /sdcard/scripts/logo/lib/（见 DEPENDENCIES.md）"
        else
            git clone --depth=1 "$repo" "$dir" && \
            log_ok "jslogo 下载完成（git clone）" || \
            log_warn "jslogo 下载失败（可选组件，可稍后重试）"
        fi
    fi
}

download_links() {
    local dir="$DEPS_DIR/links"
    local ver="2.30"
    local tarfile="links-${ver}.tar.gz"

    if [ -d "$dir" ]; then
        log_skip "links 已存在，跳过"
    else
        log_step "8/9 下载 Links $ver 文本浏览器..."
        if command -v axel &> /dev/null; then
            axel -n 20 -a "https://links.twibright.com/download/links-${ver}.tar.gz" -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
            wget -q "https://links.twibright.com/download/links-${ver}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        else
            wget -q "https://links.twibright.com/download/links-${ver}.tar.gz" -O "$DOWNLOAD_DIR/$tarfile"
        fi
        if [ -f "$DOWNLOAD_DIR/$tarfile" ]; then
            tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$DEPS_DIR"
            mv "$DEPS_DIR/links-${ver}" "$dir"
            log_ok "links 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"
        else
            log_err "Links 下载失败，请手动下载: https://links.twibright.com/download/"
            log_info "  cd $DOWNLOAD_DIR && axel -n 20 https://links.twibright.com/download/links-${ver}.tar.gz"
            log_info "  tar -xzf links-${ver}.tar.gz -C $DEPS_DIR && mv $DEPS_DIR/links-${ver} $dir"
        fi
    fi
}

# ESP32 HAL 3rdparty 组件（包含 mbedtls、WiFi、BLE、PHY 等子模块）
# 优先使用 NuttX 捆绑版（deps/nuttx/chip/esp-hal-3rdparty），包含完整子模块
download_esp_hal_3rdparty() {
    local dir="$DEPS_DIR/esp-hal-3rdparty"
    local nuttx_hal="$DEPS_DIR/nuttx/chip/esp-hal-3rdparty"
    local nuttx_ver="nuttx-12.12.0"
    # 需要的版本：release/master.a @ 4eed03a（与 NuttX 12.12.0 捆绑版本一致）
    local required_sha="4eed03a15b2678a81dfd1ed0f3bde042b1fdd4c4"

    # 检查是否已有完整内容（至少要有 components 目录）
    if [ -d "$dir/components" ] && [ -n "$(ls -A "$dir/components" 2>/dev/null)" ]; then
        log_skip "esp-hal-3rdparty 已存在且完整，跳过"
        return 0
    fi

    # 优先从 nuttx 捆绑版复制（这是 release/master.a @ 4eed03a，子模块完整）
    if [ -d "$nuttx_hal/components" ] && [ -n "$(ls -A "$nuttx_hal/components" 2>/dev/null)" ]; then
        log_step "9/9 从 NuttX 捆绑版提取 esp-hal-3rdparty..."
        if [ -d "$dir" ]; then
            rm -rf "$dir"
        fi
        cp -a "$nuttx_hal" "$dir"
        log_ok "esp-hal-3rdparty 下载完成（来源: nuttx 捆绑版, commit $required_sha）"
        return 0
    fi

    # 最后方案：从 GitHub 下载 release/master.a archive
    log_step "9/9 下载 esp-hal-3rdparty (release/master.a @ $required_sha)..."
    local mirror_base="https://ghfast.top/https://github.com"
    local tarfile="esp-hal-3rdparty-${required_sha:0:7}.tar.gz"
    local download_url="${mirror_base}/espressif/esp-hal-3rdparty/archive/${required_sha}.tar.gz"

    if command -v axel &> /dev/null; then
        log_info "使用 axel 多线程下载..."
        axel -n 20 -a "$download_url" \
            -o "$DOWNLOAD_DIR/$tarfile" 2>/dev/null || \
        wget -q "$download_url" -O "$DOWNLOAD_DIR/$tarfile"
    else
        wget -q "$download_url" -O "$DOWNLOAD_DIR/$tarfile"
    fi

    if [ -f "$DOWNLOAD_DIR/$tarfile" ] && [ -s "$DOWNLOAD_DIR/$tarfile" ]; then
        tar -xzf "$DOWNLOAD_DIR/$tarfile" -C "$DEPS_DIR"
        # tarball 解压后目录名是完整 sha，用 mv 重命名
        local extracted=$(tar -tzf "$DOWNLOAD_DIR/$tarfile" | head -1 | cut -f1 -d"/")
        if [ -d "$DEPS_DIR/$extracted" ]; then
            rm -rf "$dir"
            mv "$DEPS_DIR/$extracted" "$dir"
        fi
        log_ok "esp-hal-3rdparty 下载完成（缓存: $DOWNLOAD_DIR/$tarfile）"

        # 初始化子模块
        if [ -f "$dir/.gitmodules" ]; then
            log_info "esp-hal-3rdparty: 初始化子模块..."
            init_esp_hal_submodules "$dir"
        fi
    else
        log_err "esp-hal-3rdparty 下载失败（网络不通）"
        log_info "提示: nuttx 捆绑版已有完整内容，编译脚本会自动使用"
    fi
}

# 初始化 esp-hal-3rdparty 子模块（mbedtls, esp_phy, esp_wifi, bt, esp_coex）
init_esp_hal_submodules() {
    local hal_dir="$1"
    local mirror_base="https://ghfast.top/https://github.com"

    # 子模块列表
    local submodules="mbedtls esp_phy esp_wifi bt esp_coex"

    for sm in $submodules; do
        local sm_path="components/$sm"
        local sm_dir="$hal_dir/$sm_path"

        if [ -d "$sm_dir" ] && [ -n "$(ls -A "$sm_dir" 2>/dev/null)" ]; then
            continue  # 已有内容，跳过
        fi

        log_info "下载子模块: $sm..."

        # 获取子模块的 commit hash
        local sm_sha=""
        if [ -f "$hal_dir/.gitmodules" ]; then
            sm_sha=$(grep -A2 "path = $sm_path" "$hal_dir/.gitmodules" 2>/dev/null | \
                     grep "url =" | sed 's/.*\/espressif\///' | tr -d ' ')
        fi

        # 通过 GitHub API 获取最新 commit（如果没有 submodule .gitmodules 记录）
        if [ -z "$sm_sha" ]; then
            sm_sha=$(curl -s "https://api.github.com/repos/espressif/esp-hal-3rdparty/contents/$sm_path?ref=HEAD" | \
                     python3 -c "import sys,json; print(json.load(sys.stdin).get('sha',''))" 2>/dev/null)
        fi

        # 尝试下载子模块 tarball
        if [ -n "$sm_sha" ]; then
            local sm_tarfile="${sm}-${sm_sha:0:7}.tar.gz"
            wget -q "${mirror_base}/espressif/esp-hal-3rdparty/archive/${sm_sha}.tar.gz" \
                -O "$DOWNLOAD_DIR/$sm_tarfile" 2>/dev/null || \
            curl -sL "${mirror_base}/espressif/esp-hal-3rdparty/archive/${sm_sha}.tar.gz" \
                -o "$DOWNLOAD_DIR/$sm_tarfile" 2>/dev/null

            if [ -f "$DOWNLOAD_DIR/$sm_tarfile" ]; then
                mkdir -p "$sm_dir"
                tar -xzf "$DOWNLOAD_DIR/$sm_tarfile" -C "$sm_dir" --strip-components=1
                log_info "  $sm 下载完成"
            fi
        fi
    done
}

download_esp_idf_tools() {
    local tools_dir="$DEPS_DIR/esp-idf-tools"
    mkdir -p "$tools_dir/dist"

    # 检查 ESP-IDF 是否存在
    if [ ! -d "$DEPS_DIR/esp-idf" ]; then
        log_err "ESP-IDF 目录不存在，请先运行 download_esp_idf"
        return 1
    fi

    log_step "10/10 安装 ESP-IDF 工具链..."
    log_info "工具将安装到: $IDF_TOOLS_PATH"

    # 检查axel是否可用
    local axel_cmd=""
    if command -v axel &> /dev/null; then
        axel_cmd="axel -n 20 -a"
        log_info "axel 已安装，将使用多线程下载"
    else
        axel_cmd="wget -q"
        log_warn "axel 未安装，将使用 wget 下载（建议安装 axel: sudo apt install axel）"
    fi

    # 检查是否已有下载的工具
    local downloaded=$(ls -1 "$tools_dir/dist"/*.tar.* "$tools_dir/dist"/*.zip 2>/dev/null | wc -l)
    if [ "$downloaded" -gt 0 ]; then
        log_ok "发现 $downloaded 个已下载的工具包"
        log_info "请使用以下命令手动安装（需要 sudo）："
        echo ""
        echo "  # 解压工具链到 IDF_TOOLS_PATH"
        echo "  cd $tools_dir/dist"
        echo "  for f in *.tar.*; do tar -xf \"\$f\" -C \"$tools_dir\" 2>/dev/null || true; done"
        echo "  for f in *.zip; do unzip -o \"\$f\" -d \"$tools_dir\" 2>/dev/null || true; done"
        echo ""
        echo "  # 安装系统依赖（需要 sudo）"
        echo "  sudo apt-get install cmake ninja-build"
        echo ""
    else
        log_info "使用 axel/wget 下载工具..."
        echo ""
        echo "  # ESP-IDF 工具链下载链接（linux-amd64）："
        echo "  # xtensa 交叉编译器 (ESP32/ESP32-S3)"
        $axel_cmd "https://github.com/espressif/crosstool-NG/releases/download/esp-15.2.0_20251204/xtensa-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz" -o "$tools_dir/dist/"
        $axel_cmd "https://github.com/espressif/crosstool-NG/releases/download/esp-15.2.0_20251204/riscv32-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz" -o "$tools_dir/dist/"
        echo "  # GDB 调试器"
        $axel_cmd "https://github.com/espressif/binutils-gdb/releases/download/esp-gdb-v16.3_20250913/xtensa-esp-elf-gdb-16.3_20250913-x86_64-linux-gnu.tar.gz" -o "$tools_dir/dist/"
        $axel_cmd "https://github.com/espressif/binutils-gdb/releases/download/esp-gdb-v16.3_20250913/riscv32-esp-elf-gdb-16.3_20250913-x86_64-linux-gnu.tar.gz" -o "$tools_dir/dist/"
        echo "  # ESP32ULP 协处理器工具"
        $axel_cmd "https://github.com/espressif/binutils-gdb/releases/download/esp32ulp-elf-2.38_20240113/esp32ulp-elf-2.38_20240113-linux-amd64.tar.gz" -o "$tools_dir/dist/"
        echo "  # OpenOCD"
        $axel_cmd "https://github.com/espressif/openocd-esp32/releases/download/v0.12.0-esp32-20241016/openocd-esp32-linux-amd64-0.12.0-esp32-20241016.tar.gz" -o "$tools_dir/dist/"
        echo "  # LLVM"
        $axel_cmd "https://github.com/espressif/llvm-project/releases/download/esp-16.0.0-20230516/llvm-esp-16.0.0-20230516-linux-amd64.tar.xz" -o "$tools_dir/dist/"
        echo ""
        echo "  # QEMU（可选，用于模拟）"
        $axel_cmd "https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz" -o "$tools_dir/dist/"
        $axel_cmd "https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-riscv32-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz" -o "$tools_dir/dist/"
        echo ""
        echo "  # 安装系统依赖（需要 sudo）"
        echo "  sudo apt-get install cmake ninja-build"
        echo ""
    fi
}

install_esp_idf_tools() {
    local tools_dir="$DEPS_DIR/esp-idf-tools"

    log_step "安装 ESP-IDF 工具链..."

    # 创建工具目录
    mkdir -p "$tools_dir"

    # 解压所有 tar.xz 和 tar.gz 文件
    for f in "$tools_dir/dist"/*.tar.*; do
        if [ -f "$f" ]; then
            log_info "解压: $(basename "$f")"
            tar -xf "$f" -C "$tools_dir" 2>/dev/null || true
        fi
    done

    # 解压所有 zip 文件
    for f in "$tools_dir/dist"/*.zip; do
        if [ -f "$f" ]; then
            log_info "解压: $(basename "$f")"
            unzip -o "$f" -d "$tools_dir" 2>/dev/null || true
        fi
    done

    # 安装系统依赖
    log_info "检查系统依赖..."
    if ! command -v cmake &> /dev/null; then
        log_warn "cmake 未安装，请运行: sudo apt-get install cmake"
    fi
    if ! command -v ninja &> /dev/null; then
        log_warn "ninja 未安装，请运行: sudo apt-get install ninja-build"
    fi

    log_ok "ESP-IDF 工具链安装完成"
    echo ""
    echo "下一步："
    echo "  1. source $DEPS_DIR/esp-idf/export.sh"
    echo "  2. cd $PROJECT_ROOT"
    echo "  3. ./scripts/nuttx_build.sh build"
}

# WHAT : Fusion Pixel 12px 等宽点阵字体下载（cvbs_console 半角/
#        全角标点表源字体，OFL-1.1）
# WHY  : Noto 矢量比例字形 12px 光栅化后半角溢出重叠、全角标点
#        墨迹 1-3px 分不清（2026-10-05 用户确认）；Fusion 为逐像素
#        设计点阵（半角 adv=6/全角 adv=12），生成管线见
#        scripts/gen_pixel_fonts.py / convert_font.sh
download_fusion_pixel() {
    local font_dir="$DEPS_DIR/fonts"
    mkdir -p "$font_dir"

    local latin="$font_dir/fusion-pixel-12px-monospaced-latin.ttf"
    local hans="$font_dir/fusion-pixel-12px-monospaced-zh_hans.ttf"

    if [ -f "$latin" ] && [ -f "$hans" ]; then
        log_info "Fusion Pixel 字体已存在"
        return 0
    fi

    log_step "下载 Fusion Pixel Font 12px monospaced..."
    local url
    url="$(curl -sL https://api.github.com/repos/TakWolf/fusion-pixel-font/releases/latest \
        | grep -o '"browser_download_url": *"[^"]*12px-monospaced-ttf-v[^"]*\.zip"' \
        | head -1 | cut -d'"' -f4)"
    if [ -z "$url" ]; then
        log_warn "无法获取 Fusion Pixel 下载地址（网络），跳过——已生成表仍可用"
        return 0
    fi

    local tmp_zip="$DOWNLOAD_DIR/fusion12.zip"
    wget -q -O "$tmp_zip" "$url" || { log_warn "Fusion Pixel 下载失败，跳过"; return 0; }
    unzip -oq "$tmp_zip" -d "$DOWNLOAD_DIR/fusion12" \
        "fusion-pixel-12px-monospaced-*/fusion-pixel-12px-monospaced-latin.ttf" \
        "fusion-pixel-12px-monospaced-*/fusion-pixel-12px-monospaced-zh_hans.ttf"
    find "$DOWNLOAD_DIR/fusion12" -name "fusion-pixel-12px-monospaced-latin.ttf" \
        -exec mv {} "$latin" \;
    find "$DOWNLOAD_DIR/fusion12" -name "fusion-pixel-12px-monospaced-zh_hans.ttf" \
        -exec mv {} "$hans" \;
    rm -rf "$DOWNLOAD_DIR/fusion12" "$tmp_zip"
    log_info "Fusion Pixel 字体就绪: deps/fonts/"
}

download_notosans_sc() {
    local font_dir="$PROJECT_ROOT/tools/fonts"
    mkdir -p "$font_dir"

    local src_font="$font_dir/NotoSansSC-Medium.otf"
    local out_font="$font_dir/lv_font_notosans_sc_16.c"

    # 优先使用 LVGL demos 自带的字体（已包含在 deps/lvgl 中）
    local lvgl_font="$DEPS_DIR/lvgl/demos/multilang/assets/fonts/NotoSansSC-Medium.otf"

    if [ -f "$lvgl_font" ]; then
        log_info "使用 LVGL 自带的 NotoSansSC 字体"
        ln -sf "$lvgl_font" "$src_font" 2>/dev/null || cp -f "$lvgl_font" "$src_font"
    fi

    # 如果 LVGL 版本不存在，从网络下载
    if [ ! -f "$src_font" ]; then
        log_step "下载 NotoSansSC 字体..."
        wget -q -O "$src_font" \
            "https://github.com/googlefonts/noto-cjk/releases/download/Sans2.004R/03_NotoSansSC.zip" \
            2>/dev/null || \
        wget -q -O "$src_font" \
            "https://mirrors.tuna.tsinghua.edu.cn/github-release/googlefonts/noto-cjk/Sans2.004R/03_NotoSansSC.zip" \
            2>/dev/null || \
        log_warn "NotoSansSC 下载失败，尝试其他源..."

        # 解压（如果下载的是 zip）
        if [ -f "$src_font" ] && file "$src_font" | grep -q "Zip"; then
            unzip -oq "$src_font" -d "$font_dir" 2>/dev/null
            find "$font_dir" -name "NotoSansSC-*.otf" -exec mv {} "$src_font" \; 2>/dev/null
        fi
    fi

    # 确保 lv_font_conv 可用
    if ! command -v lv_font_conv &> /dev/null; then
        log_step "安装 lv_font_conv..."
        npm install -g lv_font_conv 2>&1 | tail -1
    fi

    # 转换字体
    if [ -f "$src_font" ] && [ ! -f "$out_font" ]; then
        log_step "转换 NotoSansSC 为 LVGL 字体 (16px, 4bpp)..."

        # 提取项目所需的所有汉字字符
        local chars=$(python3 -c "
import re, os
chars = set()
for root, dirs, files in os.walk('$PROJECT_ROOT/src'):
    for f in files:
        if f.endswith(('.c', '.h', '.py')):
            path = os.path.join(root, f)
            try:
                with open(path, 'rb') as fp:
                    content = fp.read().decode('utf-8', errors='ignore')
                    chars.update(re.findall(r'[\u4e00-\u9fff]', content))
            except: pass
for f in ['lv_conf.h', 'i18n.c', 'i18n.h']:
    path = '$PROJECT_ROOT/' + f
    try:
        with open(path, 'rb') as fp:
            content = fp.read().decode('utf-8', errors='ignore')
            chars.update(re.findall(r'[\u4e00-\u9fff]', content))
    except: pass
print(''.join(sorted(chars)))
" 2>&1)

        lv_font_conv --no-compress --no-prefilter --bpp 4 --size 16 \
            --format lvgl --font "$src_font" --symbols "$chars" \
            -o "$out_font" 2>&1

        if [ -f "$out_font" ]; then
            log_ok "字体转换完成: $out_font ($(du -h "$out_font" | cut -f1))"
        else
            log_err "字体转换失败"
        fi
    elif [ -f "$out_font" ]; then
        log_skip "LVGL 中文字体已存在，跳过转换"
    else
        log_err "未找到 NotoSansSC 源字体"
    fi
}

# ===== 主流程 =====

main() {
    echo "========================================"
    echo " ESP32-CAM 开发环境 - 依赖下载与验证"
    echo "========================================"
    echo ""
    echo "目标目录: $DEPS_DIR"
    echo "工具链目录: $IDF_TOOLS_PATH"
    echo ""

    if [ "$VERIFY_ONLY" = true ]; then
        log_info "仅验证模式，跳过下载"
        echo ""
    fi

    if [ "$TOOLS_ONLY" = true ]; then
        log_info "仅下载工具链模式"
        echo ""
        download_esp_idf_tools
        exit 0
    fi

    if [ "$INSTALL_TOOLS" = true ]; then
        log_info "安装工具链模式"
        echo ""
        install_esp_idf_tools
        exit 0
    fi

    if [ "$VERIFY_ONLY" = false ]; then
        mkdir -p "$DEPS_DIR"
        mkdir -p "$DOWNLOAD_DIR"
        download_nuttx
        download_nuttx_apps
        download_esp_idf
        download_lvgl
        download_duktape
        download_mybasic
        download_jslogo
        download_links
        download_esp_hal_3rdparty
        download_notosans_sc
        download_fusion_pixel
        download_esp_idf_tools
    fi

    # 验证阶段
    echo ""
    echo "========================================"
    echo " 验证已下载的组件"
    echo "========================================"
    echo ""

    local all_ok=true

    verify_nuttx || all_ok=false
    verify_nuttx_apps || all_ok=false
    verify_esp_idf || all_ok=false

    echo ""
    echo "========================================"
    if [ "$all_ok" = true ]; then
        log_ok "所有依赖验证完成！"
        echo ""
        echo "下一步:"
        echo "  1. source $DEPS_DIR/esp-idf/export.sh"
        echo "  2. cd $PROJECT_ROOT"
        echo "  3. ./scripts/nuttx_build.sh build"
    else
        log_err "部分依赖验证失败，请重新运行下载"
    fi
    echo "========================================"
}

main
