# 复古工作站 Retro WS - 依赖说明

## 依赖版本

| 组件 | 版本 | 下载地址 | 备注 |
|------|------|----------|------|
| Apache NuttX | 12.12.0 | https://mirrors.bfsu.edu.cn/apache/nuttx/12.12.0/ | 实时操作系统 |
| Apache NuttX Apps | 12.12.0 | https://mirrors.bfsu.edu.cn/apache/nuttx/12.12.0/ | 应用程序 |
| ESP-IDF | v5.5.4 | https://github.com/espressif/esp-idf | HAL 子集（ESP32/ESP32-S3/ESP32-C3；Pico 走 NuttX 树内 RP2040 支持，不用 ESP-IDF） |
| LVGL | v9.5.0 | https://github.com/lvgl/lvgl | 图形库 |
| Duktape | v2.7.0 | https://github.com/svaarala/duktape | JavaScript 引擎 |
| my_basic | master | https://github.com/paladin-t/my_basic | BASIC 解释器 |
| Berry（可选） | nuttx-apps 固定 | nuttx-apps `interpreters/berry` | 类 Python 轻量语言（<40KB ROM），MIT |
| CPython（可选，仅 S3） | nuttx-apps 固定 | nuttx-apps `interpreters/python` | 完整 Python 3，需 ROMFS 标准库镜像 |
| jslogo（可选） | master | https://github.com/inexorabletash/jslogo | UCBLogo 子集，Apache-2.0，跑在 Duktape 上 |
| UCBLogo（独立 ELF，不入 ROM） | 6.2.2 | https://sourceforge.net/projects/ucblogo | **GPL-2.0+**，mere aggregation 交付，见下节 |
| GNU nano | 8.4 | https://www.nano-editor.org/dist/v8/nano-8.4.tar.xz | **GPL-3.0**，编辑器真源码移植；适配层 `src/nuttx/common/nano_port`（上游不改），sha256 前 16 位 5ad29222bbd55624；**2026-10-05 定稿不入固件 ROM**——经 `apps-extra/nano` + `build_packages.sh` 打 .rpk 包交付（系统默认编辑器为 vi）；源码手动放置 `deps/nano/`（下载脚本未含） |
| esp-hal-3rdparty | NuttX 配套 | 随 NuttX 仓库（子模块） | 乐鑫 HAL 装配：mbedtls（pin v3.6.2 单体版）/ esp_wifi / bt / esp_phy / esp_coex，Apache 2.0 |
| littlefs | v2.5.1 | NuttX 树内 | 文件系统，BSD-3-Clause（独立版本号仅供参考） |
| SQLite | 3.45.1 | nuttx-apps `database/sqlite` | Public Domain |
| curl | 8.x | nuttx-apps `netutils/webclient` 等 | MIT |
| Links（待移植） | 2.30 | https://links.twibright.com | GPL-2.0，下载脚本已支持；板端集成见 NEXT_STEPS 48 |
| NotoSansSC 字体 | - | 系统字体 / 字体源 NotoSansCJK-Regular.ttc | OFL-1.1（`convert_font.sh` 生成 12px 点阵） |
| Fusion Pixel 字体 | 12px 等宽 | GitHub releases（download_deps.sh 自动下载 deps/fonts/） | OFL-1.1（`gen_pixel_fonts.py` 生成 console 半角/全角标点表） |
| Berry（codegen 副本） | nuttx-apps 同源 | 手动放置 `deps/berry/` | 构建期宿主 gcc 跑 `make prebuild` 生成 be_const_strtab；固件编入走 nuttx-apps |

## GPL 独立程序包策略（2026-10-04）⭐

> **红线**：GPL 组件禁止编入固件 ROM（AGENTS.md 11.1 第 9 条）。

| 层 | 内容 | 许可证 |
|----|------|--------|
| 固件（deps/nuttx → nuttx.bin） | NuttX/LVGL/本项目 src/（含内置 vi 编辑器） | Apache-2.0 + MIT 等宽松协议，**零 GPL** |
| 独立程序（apps-extra/ → dist/sdcard/apps/*.elf） | UCBLogo（GPL-2.0+）、GNU nano（GPL-3.0） | GPL，binfmt 独立进程加载 |

- 架构依据：GPL 的 **mere aggregation（单纯聚合）** 豁免——独立 ELF 与固件
  仅通过 NSH 执行/文件系统松耦合，无符号链接进固件映像；GPL 程序调用固件
  系统服务视同"应用调用 OS API"（GPLv3 系统库例外）
- GPL 分发义务：`build_packages.sh` 在 deps/download/ 保留源码 tarball 副本，
  分发 .elf 时一并提供
- 构建/安装：`scripts/build_all.sh` 一键完成固件+安装包；固件侧
  `pkg list` 验证安装，NSH 输入完整路径运行（如 `/sdcard/apps/ucblogo`）
- 现实约束：活跃 GPL 解释器中仅 **UCBLogo** 可移植（FMSLogo 需 Win32、
  KTurtle 需 Qt/X11，均无法运行于 NuttX）

> **多目标支持（五板）**：ESP-IDF HAL 覆盖 ESP32 / ESP32-S3 / ESP32-C3；
> Raspberry Pi Pico（RP2040）由 NuttX 树内 ARM 支持编译，不依赖 ESP-IDF。
> **开发模板**为 ESP32-S3 (N16R8/N8R8)，其余目标适配。
> **脚本引擎全部可选**：Berry/CPython/jslogo 由 menuconfig 的 `RETRO_SCRIPTS` 菜单独立勾选，
> Berry/CPython 由 nuttx-apps 构建系统拉取固定版本，jslogo 由 `download_deps.sh` 下载到
> `deps/jslogo` 并在运行时从 SD 卡 `/sdcard/scripts/logo/lib/` 加载。

## 版本核查（2026-10-04）⭐

> 联网核查各上游项目最新版本，评估当前选型是否仍为最优：

| 组件 | 当前版本 | 最新版本（2026-10） | 评估结论 |
|------|---------|--------------------|---------|
| NuttX | 12.12.0 | **13.0.0**（2026-07-12 发布） | **保持 12.12.0**：本项目已在 12.12.0 上完成双目标编译验证（含 esp-hal-3rdparty 补丁 workaround，见 BUILD_FIXES.md），大版本升级风险高；13.0.0 列入后续升级计划 |
| ESP-IDF | v5.5.4 | **v6.0.2 / v6.0.3（LTS）**，v6.1 已发布 | **保持 v5.5.x**：v5.5 为 LTS（支持至 2028-01）；NuttX esp-hal-3rdparty 与 v5.x 配对，v6 兼容性未验证；NuttX 编译实际只用其 HAL 子集 |
| LVGL | v9.5.0 | **v9.6.x**（无 v10） | **可用 9.5.0，可选升级 9.6**：同属 v9 API 稳定线，升级风险低，收益有限；建议在实机验证通过后再考虑 |
| littlefs | v2.5.1 | **v2.11.3**（2026-03-25） | **无需动作**：NuttX 使用其内核内建 littlefs 版本，独立版本号仅供参考 |
| SQLite | 3.45.1 | **3.52.0**（2026-03-06） | **可选升级**：amalgamation 单文件替换成本低，建议与 nuttx-apps 集成点一起评估 |
| Duktape | 2.7.0 | **2.7.0**（最新即此，维护模式） | **已是最新**：项目低活跃但稳定，广泛打包于各发行版 |
| my_basic | master | master（无版本标签，项目停更） | **保持现状**：无新版本可升 |
| curl | 8.0+ | 8.x 持续更新 | 经 NuttX apps 提供，跟随 NuttX 版本 |

**总体结论**：当前选型整体仍然合理——核心约束是 **NuttX ↔ ESP-IDF(HAL) 的版本配对**，
盲目追新（NuttX 13 / IDF v6）会推翻已验证的编译链。建议策略：
1. 短期锁定 NuttX 12.12.0 + ESP-IDF v5.5.x + LVGL 9.5.0（已验证组合）
2. 实机验证完成后，评估 LVGL 9.6 / SQLite 3.52 小步升级
3. NuttX 13.0.0 / ESP-IDF v6 作为里程碑任务单独排期（需重做 HAL 配对验证）

## 国内镜像

### BFSU 镜像 (NuttX)

- 主页: https://mirrors.bfsu.edu.cn/apache/nuttx/
- 同步周期: 每日
- 优点: 国内访问快，支持 HTTP

### Gitee 镜像 (ESP-IDF)

- 主页: https://gitee.com/EspressifSystems/esp-idf
- 优点: 国内访问快，支持 git clone
- 缺点: release 页面不是最新版本

## 工具链版本

| 工具 | 版本 | 下载地址 | 支持目标 |
|------|------|----------|----------|
| crosstool-NG | esp-15.2.0_20251204 | https://github.com/espressif/crosstool-NG/releases | ESP32 + ESP32-S3（xtensa-esp-elf） |
| binutils-gdb | esp-gdb-v16.3_20250913 | https://github.com/espressif/binutils-gdb/releases | ESP32 + ESP32-S3 |
| esp32ulp-elf | 2.38_20240113 | https://github.com/espressif/binutils-gdb/releases | ESP32 |
| openocd-esp32 | v0.12.0 | https://github.com/espressif/openocd-esp32/releases | ESP32 + ESP32-S3 |
| llvm | esp-16.0.0-20230516 | https://github.com/espressif/llvm-project/releases | ESP32-S3 |
| qemu | esp-develop-9.0.0-20240606 | https://github.com/espressif/qemu/releases | ESP32-S3 |
| riscv32-esp-elf | 随 crosstool-NG 同源 | 同 crosstool-NG releases | **ESP32-C3（合宙，RISC-V）** |
| arm-none-eabi | 任一主流 GCC-ARM 发行版（实测 xPack 13.2.1） | https://developer.arm.com/downloads/-/gnu-rm 或 `sudo apt-get install gcc-arm-none-eabi` | **Raspberry Pi Pico（RP2040，ARM）**：预期位于 `deps/esp-idf-tools/arm/bin`；该目录缺失时构建回退系统 PATH（apt 安装即可），下载脚本暂未集成 |

> **注意**：QEMU 仅支持 ESP32-S3 模拟，不支持 ESP32-CAM (ESP32)。
> **RISC-V / ARM 工具链**：riscv32-esp-elf 供合宙 C3 目标使用（`download_deps.sh` 一并下载）；Pico 用
> arm-none-eabi（`deps/esp-idf-tools/arm/bin`，apt 可装）——Xtensa / RISC-V / ARM 三套由
> `setup_tools.sh` 一次激活并自检；二进制互不通用，.rpk 包经 Arch 字段隔离。
> **调试注意**：本项目 ESP32-S3 目标将 GPIO39-42 用作 I2S 音频（硬件 JTAG 失效）、
> GPIO19/20 用作 USB HID（USB-JTAG 失效），调试统一走 UART0（GPIO43/44）。

## 下载脚本

使用 `scripts/download_deps.sh` 下载所有依赖（五板共享）：

```bash
# 下载全部依赖
./scripts/download_deps.sh

# 仅验证已下载的依赖
./scripts/download_deps.sh --verify-only

# 仅下载工具链
./scripts/download_deps.sh --tools-only

# 安装已下载的工具链
./scripts/download_deps.sh --install-tools
```

## 目录结构

```
deps/
+-- nuttx/           # Apache NuttX RTOS（含 esp-hal-3rdparty 子模块、RP2040 支持）
+-- nuttx-apps/      # NuttX 应用程序（含 interpreters/berry、interpreters/python、SQLite、curl）
+-- esp-idf/         # ESP-IDF（乐鑫 HAL 源；S3/CAM/C3 用，Pico 不用）
+-- lvgl/           # LVGL 图形库
+-- duktape/        # Duktape JavaScript 引擎
+-- my_basic/       # my_basic 解释器
+-- jslogo/         # jslogo (UCBLogo 子集, Apache-2.0, 可选)
+-- nano/           # GNU nano 8.4 上游源码（手动放置，下载脚本未含）
+-- berry/          # Berry 副本（手动放置；构建期 codegen 用）
+-- build-pico/     # （空遗留目录，可删）
+-- esp-idf-tools/  # 工具链（实际布局，与 build_firmware.sh 一致）
    +-- xtensa/bin            # Xtensa 交叉编译器（ESP32 + ESP32-S3）
    +-- riscv/bin             # RISC-V 交叉编译器（ESP32-C3）
    +-- arm/bin               # ARM 交叉编译器（Pico；apt 安装或自备解压）
    +-- openocd-esp32/        # OpenOCD 调试器
    +-- qemu/                 # QEMU 模拟器（仅 ESP32-S3）
    +-- dist/                 # 下载的工具包
```

## 手动下载

如果需要手动下载，可以使用以下命令：

```bash
# NuttX (使用 BFSU 镜像)
wget https://mirrors.bfsu.edu.cn/apache/nuttx/12.12.0/apache-nuttx-12.12.0.tar.gz
wget https://mirrors.bfsu.edu.cn/apache/nuttx/12.12.0/apache-nuttx-apps-12.12.0.tar.gz

# 使用 axel 多线程下载（更快）
axel -n 20 https://mirrors.bfsu.edu.cn/apache/nuttx/12.12.0/apache-nuttx-12.12.0.tar.gz
```

## 编译脚本

下载脚本五板共享，编译脚本分统一入口与旧三板入口：

```
scripts/
+-- download_deps.sh    # 下载依赖（共享）
+-- setup_tools.sh      # 激活工具链（共享）
+-- verify.sh           # 项目验证（共享）
+-- convert_font.sh     # 字体转换（共享）
+-- setup_env.sh        # 系统依赖安装（共享）
+-- firmware/           # 五板统一构建入口
|   +-- build_firmware.sh   # <s3|s3n8|cam|c3|pico|all>
+-- esp32s3/            # ESP32-S3 编译脚本（旧入口）
|   +-- build.sh
|   +-- nuttx_build.sh
+-- esp32cam/           # ESP32-CAM 编译脚本（旧入口）
|   +-- build.sh
|   +-- nuttx_build.sh
+-- esp32c3/            # ESP32-C3 编译脚本（旧入口）
    +-- build.sh
    +-- nuttx_build.sh
```

---

_最后更新: 2026-10-05（项目更名 retro-ws：五板多架构定位全面修订；nano 行并入依赖表）_
