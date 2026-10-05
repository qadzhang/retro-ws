# 复古工作站 Retro WS - 构建修复日志

## 更新日期

2026-10-05（nano 出 ROM 转 .rpk 包 + 包载荷路径修复；此前同日：可移植性/字形排版/输入法修复）

---

## 2026-10-05（续5）：语法矩阵被构建产物污染（arch/ 软链）

### 问题
`check_syntax.sh` 的 `-I deps/nuttx/include` 中，`include/arch` 是 configure
生成的软链（构建哪板指向哪架构）——c3 固件构建后 xtensa 源文件经 riscv 的
types.h 解析，报 `_int64_t` 未定义，矩阵 44/88 大面积失败（s3 构建后碰巧
架构一致所以此前未暴露）。

### 修复
realinc 层放指向 `deps/nuttx/arch/xtensa/include` 的 `arch` 软链并置于
-I 最前，优先级压制生成物（deps 本体不动）。恢复 88/88。

---

## 2026-10-05（续4）：nano 出固件 ROM 转 .rpk 包（许可证定稿）

### 问题
GNU nano（GPL-3.0）自 2026-10-04 晚起编入五板固件 ROM，与 AGENTS 11.1 第 9 条
"GPL 禁止编入 ROM"红线冲突。项目所有者裁决：固件零 GPL，系统默认编辑器回 vi。

### 修复
- 五份 appconfig：删 `CONFIG_RETRO_NANO=y`，加 `CONFIG_SYSTEM_VI=y`
- Kconfig RETRO_NANO default n（实验回编开关）；Makefile nano VPATH 门控
- apps-extra/nano 包模板 + build_packages.sh 接入（同步 external/nano ->
  收集 ELF -> 打 .rpk；nano_port 垫片随包复用）
- **顺带修存量 bug**：build_packages.sh 收集 ELF 到 `package/apps/`，而
  make_package.sh 载荷树约定 `package/data/`——原路径打的 .rpk 载荷为空；
  已统一为 `package/data/apps/`（ucblogo/nano 两包）

### 验证
s3/c3 固件重编：nano 退出链接、vi（CONFIG_SYSTEM_VI）进入、ROM 体积显著下降
（体积数据见当日构建输出）；bash -n 通过。

---

## 2026-10-05（续3）：清理写死的绝对路径（可移植性）

### 问题
他人克隆仓库到任意目录后，多处写死 `/home/user/...` 的路径失效：
`cd /home/user/retro-ws`（README/REQUIREMENTS）、目录树根（SYSTEM/REQUIREMENTS）、
`bin/esptool.py` 包装器、`scripts/esp32cam/build.sh` 的 genromfs 检测、
两个宿主测试脚本的 REPO 默认值、`convert_font.sh` 的 npx 扫描（`find /home ...`）
与 `-o` 绝对输出路径（导致生成字库头注释嵌入开发机路径）、两个 defconfig 的
`CONFIG_BASE_DEFCONFIG` 绝对值。

### 修复
- 文档改为 `cd retro-ws` / 目录树根 `retro-ws/`
- esptool 包装器：PATH → `$HOME/.local/bin` → `python3 -m esptool` 三级回退
- genromfs 检测改 `$HOME/bin/genromfs`
- 测试脚本 REPO 默认值改由 `__file__` 向上三级推导（REPO 环境变量仍可覆盖）
- convert_font.sh：npx 先查 PATH 再扫 `$HOME/.nvm /usr/local /opt`；`-o` 改相对路径
- `CONFIG_BASE_DEFCONFIG` 行删除（仓库内其余 defconfig 本就无此行，符合惯例）
- setup_tools.sh 同日重写为三工具链版：PATH 改实际目录 `{xtensa,riscv,arm}/bin`
  （原引用不存在的 `xtensa-esp-elf/` 旧目录名），新增 RISC-V/ARM 自检与按板提示；
  README"下载依赖"步骤相应改为三套工具链说明（Xtensa/RISC-V 随脚本下载，
  ARM 走 apt `gcc-arm-none-eabi` 或自备放入 `deps/esp-idf-tools/arm/`）

### 验证
`bash -n` 三个脚本语法通过；`check_cli_utf8.py` 以新 ROOT 推导运行通过；
全仓 grep（排除 deps/与本文历史条目）无 `/home/user` 残留。

---

## 2026-10-04 深夜：五板全栈 ROM

### S3/CAM 全栈（GUI + 全解释器 + 图形程序）
- LVGL 9.5 全源（463 文件）+ 双外壳 + 8 个图形程序 + jslogo 入 ROM
- 解释器四件套：my-basic + Berry + Duktape + jslogo（BASIC/JS 双 UI 绑定）
- 修复链：Kconfig `if RETRO_GUI` 误吞 AV_CONSOLE/AUDIO（结构重排）；
  `---help---` 旧语法；DISPLAY 自引用 default；孤立/重复 endif（kconfiglib
  全树可见性诊断法定位）；LVGL 内建 128KB 内存池（改 CLIB malloc 入
  SPIRAM 堆，CAM dram0 111%→39%）；帧缓冲 .psram 段不存在（改 malloc）；
  be_return 宏含 return（双 return 编译错）；I2S legacy 宏名
  （ESP32S3_I2S vs ESPRESSIF_I2S0）；mbedtls pin 98ae8db 已拆
  tf-psa-crypto 子模块（固定用上游 v3.6.2 单体版）
- dram0：S3 78%（drom 1.87MB）/ CAM 40%——帧缓冲与 LVGL 堆全入 SPIRAM

### C3 CLI 全家桶
- Berry + my-basic + Duktape + 字库 + AV 控制台 + vi/cle/hexed/dd/tee/
  readline/mkrd（ping/telnetd 需 NET 栈，C3 默认无网未开）
- 1,464KB / 4MB = 35%

### 五板全栈 ROM（全部含字库+AV 控制台+双解释器）

| 板 | bin | Flash | 占用 | dram0 |
|----|-----|-------|------|-------|
| s3 | 1,802,560 | 16MB | 10% | 78%（PSRAM 堆承接大分配） |
| s3n8 | 1,797,780 | 8MB | 21% | 同上 |
| cam | 1,797,780 | 4MB | 42% | 40% |
| c3 | 1,499,880 | 4MB | 35% | — |
| pico | 1,265,980 | 2MB | 60% | sram 8.9KB/264KB |

（s3 含 GUI 全栈；s3n8 同配置 8MB 档；c3 为 CLI 全家桶；pico 保底 Berry+BASIC）

---

## 2026-10-04（晚间：全系全功能 ROM——UTF-8 字体 + AV 控制台 + 双解释器）

---

## 2026-10-04 晚间：五板全功能 ROM 定稿

### 铁律落地（AGENTS.md 7.3 新增）
- 全链路 UTF-8；唯一字体 notosans_sc_16（1bpp 全量，.o 911KB x86 实测）
- **CLI 构建编译同一字体文件**：firmware/retro-apps/fontbridge 把
  lvgl/lvgl.h 桥到 lvgl_font_compat.h（复刻 LVGL 9.5 fmt_txt 类型与
  三类 cmap 查找语义）；不再有 GB2312 档（已删）
- **AV 视频字符控制台（全系标配）**：cvbs_console.c——UTF-8 解码 +
  16px 点阵直绘 L8 帧缓冲；

	、折行、整屏滚动

### 过程中的真 BUG（问题→解决）
1. **位图乱码（GLM 视觉审查抓到）**：LVGL 9.5 fmt_txt 的 1bpp 位图是
   **连续位流（行间不按字节对齐，stride==0 语义）**——按行取整字节
   解码 ASCII 全乱。修为全局 bit 偏移取位；ASCII art 逐字验证 E/A/3/0
   正确后 GLM 复验 PASS
2. **bitmap_index 必须 uint32**：全量字库位图 >64KB，uint16 截断
   （编译 -Woverflow 警告暴露）
3. **be_return 宏已含 return**：retro_gpio_berry.c 里 `return be_return()`
   双 return 编译错
4. **risc-v HAL 装配位置**：必须放 esp32c3/ 真目录（chip 是 configure
   建的符号链接，被真目录占用 → clean_dirlinks 失败）
5. **Berry 依赖**：CONFIG_ARCH_SETJMP_H（工具链 setjmp）+
   CONFIG_SYSTEM_SYSTEM（apps/system/system 的 system()）
6. **引擎被 --gc-sections 裁掉**：惰性装载无引用即裁——script_init
   改为启动即装载双引擎（也正是"默认可用"的语义）
7. **C3 CVBS 引脚冲突**：原定 GPIO4/5 与 SD SPI 冲突 → 改 GPIO1/GPIO10

### 五板全功能 ROM（字体+AV 控制台+Berry+my-basic 全默认）

| 板 | nuttx.bin | Flash | 占用 |
|----|-----------|-------|------|
| s3 (N16R8) | 1,392,032 | 16MB | 8% |
| s3n8 (N8R8) | 1,392,032 | 8MB | 16% |
| cam (4MB) | 1,389,204 | 4MB | 33% |
| c3 (4MB) | 1,428,544 | 4MB | 34% |
| pico (2MB) | 1,265,980 | 2MB | 60% + UF2 |

（此前 203KB 是"裸 CLI"——没字体没引擎没 AV 控制台；用户指出后补齐。）

---

## 2026-10-04（下午：四板真机固件全通 + RP2040 新目标）

---

## 2026-10-04 下午：五配置固件全通（真实交叉编译）

### 问题 → 尝试 → 解决

1. **工具链就位**：crosstool-NG release URL 全部 404/000——用 GitHub
   Releases **asset API**（`Accept: application/octet-stream`）拉取
   esp-15.3.0_20260914 的 xtensa(75MB)/riscv(113MB) 与 xpack ARM(256MB)。
   riscv 工具链前缀 NuttX 期望 `riscv64-unknown-elf`——建 shim 符号链接目录。
2. **esp-hal-3rdparty 装配**：NuttX 默认 git clone+submodule（本环境不可达）。
   写 `scripts/firmware/prepare_esp_hal.sh`：API tarball 按 Make.defs 锁定
   commit 装配 HAL + pinned mbedtls/esp-phy-lib + git 快照 + 预打 NuttX
   补丁 + Make.defs apply 幂等化（重跑脚本即复现）。三个芯片各自的
   HAL 版本：esp32@4eed03a / esp32s3@6b4f19b / esp32c3@bb255ca。
3. **apps 集成层**：`sync_src_to_apps.sh` 把 src/ 同步进
   deps/nuttx-apps/retro；`firmware/retro-apps/{Kconfig,Make.defs,Makefile,
   retro_boot.c}` 为模块真身（CONFIG_RETRO 三态 + 按目标挑 CSRCS）。
   retro_boot_main 为 CONFIG_INIT_ENTRYPOINT：先起板级任务再交棒 NSH。
4. **符号/配置修正**：
   - 我们 nsh_cmds 的 cmd_reboot 与 NSH 内建冲突 → 改名 retro_reboot
   - CONFIG_VERSION_STRING 在多 defconfig 缺失 → 源内 #ifndef 兜底
   - GPIO IRQ 符号各芯片不同：pico(DEV_GPIO_LOWER_HALF)/esp32(ESP32_GPIO_IRQ
     +ARCH_INTERRUPTSTACK)/c3(ESPRESSIF_GPIO_IRQ)/s3(ESP32S3_GPIO_IRQ)
   - flash 容量 choice：追加配置无法切换 choice——`# unset:` 行先
     kconfig-tweak --disable 再 enable（WROOM1N16R8/N8R8）
   - 换板必须 distclean（会删 HAL 装配）→ 构建脚本每次自动重跑
     prepare_esp_hal.sh
   - RP2040 UF2 需要 picotool → 关 RP2040_UF2_BINARY，用自带
     scripts/make_uf2.py（512B 块 + family 0xE48BFF56）从 bin 生成 UF2

### 五配置产物与 ROM 核查（dist/firmware/）

| 配置 | 板 | nuttx.bin | Flash | 占用 | SMP 双核分工 |
|------|-----|-----------|-------|------|--------------|
| s3 | DevKitC-1 **N16R8**（16MB） | 203.7KB | 16MB | 1.2% | CPU0=程序 / CPU1=媒体 |
| s3n8 | DevKitC-1 **N8R8**（8MB） | 203.7KB | 8MB | 2.4% | 同上 |
| cam | AI-Thinker **WROVER 4MB** | 200.9KB | 4MB | 4.8% | 同上 |
| c3 | 合宙 ESP32-C3（单核） | 236.5KB | 4MB | 5.7% | 单核共容 |
| pico | **RP2040 Pico 2MB** | 150.5KB + UF2 | 2MB | 7.2% | CPU0=程序 / CPU1=文件IO |

全部远低于预算（最紧的 Pico 也仅 7.2%），无需裁剪；后续加 GUI/脚本
引擎/字库仍有充足余量（S3 16MB 余 98.8%）。

### RP2040 新目标（第四板）

- HARDWARE.md 3B 章 + `src/nuttx/rp2040/`（硬件档案/板级/入口/Kconfig）
- 双核分工：Core0=NSH/程序，Core1=文件 IO 服务（rp2040_retro.c 钉核）
- rasppberry-pico:**smp** 基线（SMP=y NCPUS=2）+ retro 追加段
- 语法矩阵 70 文件全绿（含 rp2040 树）

---

## 2026-10-04（全面审计与测试日）

---

## 2026-10-04 更新：全仓审计修复 + 机器化测试体系

依据 ai-code-testing 规范（蜕变/差分/PBT/模糊/变异五层）完成全仓审计修复。
问题 → 尝试 → 解决 三段式记录如下。

### A. 结构性清理：删除 esp32s3 遗留副本树（14 文件）

- **问题**: `src/nuttx/esp32s3/{bootmenu,network_utils,script_engines}.c`、
  `apps/system/nsh_cmds.c`、`driver/{cron,drv_pinyin,drv_player,drv_recorder,
  drv_rtc,drv_sqlite,firewall,memmon,network,ntp}.c` 是 common/ 树的旧拷贝，
  自述"遗留副本"，且带着已被 common 修掉的 bug 回归（cron fields[5] 越界、
  SOCK_DGRAM ping、单包 HTTP 截断）。任何把两棵树编进同一目标的构建都会
  链接期符号冲突。
- **尝试**: 考虑逐文件对齐修复两份。
- **解决**: 直接 `git rm` 遗留副本（符号唯一归属 common/）；S3/CAM 的
  `driver/watchdog.c` 同为重复定义，看门狗实现唯一归属 `esp32*_retro.c`。
  verify.sh 清单同步更新。

### B. retro_gpio.c：幻觉 ioctl API（CRITICAL）

- **问题**: 使用了 NuttX 12.12 不存在的 `GPIOC_WRITE1/GPIOC_WRITE0`、
  `struct gpio_pinreq_s`；包含路径 `<nuttx/ioctl/gpio.h>` 不存在。
- **解决**: 对照 deps/nuttx/include/nuttx/ioexpander/gpio.h 逐字修正：
  `GPIOC_WRITE`（参数 0/1）、`GPIOC_READ`（bool* 出参）、
  `GPIOC_SETPINTYPE`（枚举值）；补 `<nuttx/timers/pwm.h>`、
  `<nuttx/analog/adc.h>`。宿主机 29 项策略层测试全绿。

### C. pkg_manager.c：安全 + 健壮性重写

- **问题**: ①维护脚本按 `info/<脚本名>` 存档，多包互相覆盖、卸载误删；
  ②Package 名可含 `/`/`..`，数据库文件名路径穿越；③打包器 manifest
  注入的路径未过滤，卸载时可删任意文件；④CRC 只算不校验（校验功能
  名存实亡）；⑤manifest_out 拼接 snprintf 截断后长度下溢；⑥`data/`
  目录条目 `rel[strlen(rel)-1]` 空串越界；⑦read() 短读会整体错位；
  ⑧postrm 在脚本删除之后才执行（永不运行）。
- **解决**: 脚本改 `info/<包名>.<脚本名>` 随包隔离；`pkg_name_is_valid`
  白名单；manifest 行仅接受安装前缀内且 path_is_safe 的路径；实测 CRC
  与打包清单逐项比对（不符 -EILSEQ 并回滚已落盘文件）；所有 tar 读
  走 read_full；空 rel 防护；postrm 先执行后清理；大缓冲改堆分配
  （NSH 栈安全）；新增 USTAR 头 checksum 校验。
- **测试**: 131 项单测 + 545 项 Python 差分/PBT（zlib/GNU tar 为
  oracle）+ 20 万轮结构感知模糊（ASan）+ 变异测试 16/16 全杀（100%）。

### D. CVBS 视频链路重写（drv_cvbs_core 抽取）

- **问题**: 两块板驱动各自为政：S3 `cvbs_flush_frame()` 为空、I2S 时钟
  10MHz 与 864 样本/行不匹配（行频偏差 35%，电视无法锁定）；CAM 版
  `cvbs_flush_frame()` 逐行覆盖同一双缓冲（无效逻辑）、OUT_LINK 写裸
  指针且从未置 OUTLINK_START（DMA 永不启动）；两版均无垂直同步。
- **解决**: 抽出可移植核心 `common/driver/cvbs_core.[ch]`（调色板/
  帧缓冲/绘制/PAL 场时序：3 均衡 + 3 宽脉冲 + 3 均衡 + 288 活跃行）；
  统一驱动层 `common/driver/drv_cvbs.[ch]`（weak 硬件钩子模式）；
  S3/CAM 设备层只实现 `drv_cvbs_emit_line`；CAM 时钟改 APB/6≈13.33MHz
  并置 OUTLINK_START；补 `cvbs_core_set_direct_luma()`（LVGL L8 直通）。
  lv_port_disp 引用的 drv_cvbs_init/send/deinit 接口此前根本不存在，
  一并补齐。
- **测试**: 916 项断言（行结构/场结构/解码器逐像素差分）+ 50 帧性能
  基准 10164fps（203 倍实时余量）+ glm 视觉审查解码测试图。

### E. 启动装配去重与语法修复

- **问题**: `board_late_initialize/board_get_reset_reason/board_get_version`
  在 board.c 与 esp32*_retro.c 双定义；`chip/startup.c` 含 `up AttachInterrupt`
  非法标识符、西里尔乱码 `_текст`、rsr 汇编约束错误，且整体与 NuttX
  自带 esp32s3_start.c 冲突；esp32s3_retro.c 的 WDT ticks 乘法 32 位
  溢出、`ntp_sync/net_service_init` 未定义、WiFi 宏双重条件缺失。
- **解决**: 任务装配收敛为 `esp32*_retro_start()` 由 board.c 唯一回调；
  startup.c 修复全部语法错误并整体收进 `CONFIG_RETRO_CUSTOM_STARTUP`
  （默认不参与编译，留档教学）；WDT 64 位乘法 + 53s 钳制；对外调用
  改为真实 API（ntp_sync_start 等）。

### F. 真实头文件全仓语法矩阵

- **问题**: 大量幻觉包含路径（`<nuttx/rtc.h>`、`<nuttx/reboot.h>`、
  `<nuttx/watchdog.h>` 均不存在）；`nuttx/syslog/syslog.h` 不提供
  LOG_* 级别（在 libc `<syslog.h>`）——真实头检查才暴露。
- **解决**: `tests/host/check_syntax.sh` 组装真实 NuttX 12.12 +
  esp32s3 硬件头 + 最小 core-isa 的 include 层，全仓 -fsyntax-only；
  已修复本人负责文件集；其余由修复代理收敛后复跑。

### G. Kconfig 管道

- esp32s3 Kconfig 补 `RETRO_CVBS`（I2S 电阻网络输出）与
  `RETRO_MEMMON` 符号；RETRO_GPIO 占用表 include 路径改
  `driver/retro_gpio.h`（匹配构建 -I common）。

### H. 中文字库管线定稿（1bpp 点阵，三档）

- **问题**: 界面硬编码 montserrat（无 CJK）→ 中文全部 tofu；
  用户要求通用系统全量字体、CLI 也需中文；4bpp 抗锯齿在
  320/640 复古档位无意义且体积 4 倍。
- **解决**: lv_font_conv 实测管线（ttc→fonttools 抽 SC 面→转换）；
  1bpp 点阵三档制（retro_font.h + Kconfig choice）：

| 档位 | 字符集 | 字形数 | .c 源 | .o 目标（Flash 实占） | 适用 |
|------|--------|--------|-------|------------------------|------|
| RETRO_FONT_CJK_FULL | Unicode 区段全量（CJK U+4E00-9FFF+符号+全角+ASCII） | ~21,400 | 6.2MB | **911KB** | S3 16MB |
| RETRO_FONT_CJK_GB2312 | 完整 GB2312+ASCII | 7,445 | 2.3MB | **352KB** | CAM 4MB / 2MB GUI |
| （拉丁） | montserrat_12 | 96 | - | 22KB | 最小 CLI |
| （对照 1bpp）GBK 全量 | GBK | 21,003 | 6.4MB | 950KB | 与 FULL 近同覆盖，未入库 |
| （对照 4bpp）FULL | 同 FULL | 21,400 | 18.0MB | 2,882KB | 已弃（抗锯齿无意义） |
| （对照 4bpp）GB2312 | 同 GB2312 | 7,445 | 6.3MB | 1,022KB | 已弃 |

  字体按 Unicode 码点索引：任意 UTF-8 文本直接渲染（GBK/GB2312
  仅是生成时的字符集筛选）。CLI 串口方案字体 0 开销——设备只发
  UTF-8 字节，PC 端终端渲染。2MB 预算实测（-Os 宿主目标近似）：
  NuttX+NSH ~450KB + 拼音 IME 36KB + my-basic 285KB + Duktape
  430KB ≈ 1.2MB，可容纳 CLI+IME+两脚本引擎；GUI 变体用 GB2312 档。
  全部 UI 代码统一 RETRO_FONT_DEFAULT（retro_font.h），
  禁止散落 montserrat 硬编码。

### 测试与工具产出

| 产出 | 位置 | 状态 |
|------|------|------|
| C 单测（pkg/gpio/cvbs） | tests/host/test_*.c | 131+29+916 全绿 |
| 模糊器（tar 三模式） | tests/host/fuzz_tar.c | 20 万轮 0 crash |
| Python 差分+PBT | tests/host/python/test_rpkg_diff.py | 545 checks 全绿 |
| 变异测试门禁 | tests/host/mutation/run_mutation.sh | 16/16 杀死(100%) |
| 性能基准 | tests/host/bench_core.c | CRC 420MB/s、CVBS 10164fps |
| 全仓语法矩阵 | tests/host/check_syntax.sh | 修复中→收敛门禁 |
| 一键调度 | tests/host/run_all.sh | CI 入口 |
| LVGL 无头模拟器 | tools/sim/{lvgl_sim,app_stubs}.c + build.sh | 渲染→PPM→glm 审查 |
| CVBS 全链路管线 | tools/sim/cvbs_pipeline.c | LVGL→flush→波形→解码 |

---

## 2026-04-02 更新（历史）

---

## Python 脚本引擎移除

**原因**: ESP32-CAM (4MB Flash / 4MB PSRAM) 资源不足，无法运行 CPython

**移除内容**:
1. `CONFIG_INTERPRETER_CPYTHON=y` - 已禁用
2. `examples/hello.py` - 已删除
3. `src/nuttx/common/script_engines.c` - 移除 MicroPython 代码
4. `src/nuttx/esp32s3/script_engines.c` - 移除 MicroPython 代码
5. `src/nuttx/esp32/Kconfig.esp32` - 移除 MicroPython 配置项
6. `src/nuttx/esp32s3/Kconfig` - 移除 MicroPython 配置项

**文档清理**:
- CLAUDE.md - 移除 MicroPython 引用
- REQUIREMENTS.md - 移除 MicroPython 条目
- COMPLETED.md - 更新脚本引擎描述
- SYSTEM.md - 移除 python/ 目录
- NEXT_STEPS.md - 移除 MicroPython 任务
- DEPENDENCIES.md - 移除 MicroPython 条目
- README.md - 移除 Python 示例和说明

**当前脚本引擎**:
- `CONFIG_INTERPRETERS_BAS=y` - my-basic (BASIC)
- `CONFIG_INTERPRETERS_DUKTAPE=y` - Duktape (JavaScript)

---

## 更新日期

2026-04-01

---

## 问题概述

NuttX 12.7.0 编译失败，报错 `XTENSA_CP0_SA` 等宏未声明。

**错误信息**:
```
common/xtensa_fpucmp.c:46:3: error: 'XTENSA_CP0_SA' undeclared here (not in a function)
common/xtensa_fpucmp.c:46:18: error: 'XTENSA_CP1_SA' undeclared here (not in a function)
common/xtensa_fpucmp.c:46:33: error: 'XTENSA_CP2_SA' undeclared here (not in a function)
...
make[1]: *** [Makefile:146：xtensa_fpucmp.o] 错误 1
```

---

## 根本原因分析

### XCHAL_CP_NUM 定义冲突

ESP32-S3 实际上有 2 个协处理器（FPU + cop_ai），但 nuttx 的 `tie.h` 文件错误地定义了 `XCHAL_CP_NUM=0`。

**问题文件**: `deps/nuttx/include/arch/chip/tie.h`

```c
// 错误定义 - 说没有协处理器
#define XCHAL_CP_NUM         0     /* number of coprocessors */

// 但同一文件中却定义了 CP0 和 CP3 的信息！
#define XCHAL_CP0_NAME       "FPU"
#define XCHAL_CP0_SA_SIZE    72
#define XCHAL_CP3_NAME       "cop_ai"
#define XCHAL_CP3_SA_SIZE    208
```

**正确的定义**在 esp-hal-3rdparty 的 `tie.h` 中：
```c
#define XCHAL_CP_NUM         2     /* number of coprocessors */
```

### 头文件包含链

```
xtensa_fpucmp.c
  +-- #include "xtensa.h"               (symlink -> src/arch/xtensa/src/common/xtensa.h)
        +-- #include <arch/chip/tie.h>  (deps/nuttx/include/arch/chip/tie.h - XCHAL_CP_NUM=0)
              +-- #include "xtensa_coproc.h"  (deps/nuttx/arch/xtensa/include/xtensa/xtensa_coproc.h)
                    +-- #if XCHAL_CP_NUM > 0   <- 这个条件为假，因为 XCHAL_CP_NUM=0！
                          +-- XTENSA_CP0_SA 等宏定义在这里！
```

---

## 尝试过的解决方案

### 方案 1: 使用 symlink 重定向 include 路径

**做法**: 创建 symlink 将 nuttx 的 xtensa.h 指向我们的覆盖版本

**失败原因**:
- 编译器的 include 路径优先级问题，`-isystem` 目录优先于 `-I` 目录
- 即使我们的 xtensa.h 被使用，它内部 `#include <arch/chip/tie.h>` 仍然找到 nuttx 的版本
- `#include_next` 在 NuttX 环境中无法正确工作

### 方案 2: 在 xtensa.h 中用 #undef/#define 修正 XCHAL_CP_NUM

**失败原因**:
- `xtensa_coproc.h` 在开头就检查 `#if XCHAL_CP_NUM > 0`
- 此时 XCHAL_CP_NUM 仍然是 0（因为我们的 `#define` 在 `#include` 之后才生效）

### 方案 3: 使用 EXTRAFLAGS 添加 -D 编译参数

**失败原因**:
- EXTRAFLAGS 追加在编译命令的最后
- 头文件中的 `#define XCHAL_CP_NUM 0` 已经在预处理时被处理
- 编译参数无法覆盖已预处理的值

### 方案 4: 使用 include_next 技巧

**失败原因**:
- `#include_next` 是 GCC 扩展，在某些构建环境下行为不一致
- NuttX 的 include 路径配置复杂，难以正确设置

### 方案 5: 修改 esp-hal-3rdparty 的 tie.h（创建 override）

**失败原因**:
- 这个路径不在编译器的 include 搜索路径中
- esp-hal-3rdparty 的 tie.h 通过 `<arch/chip/tie.h>` 被包含时，搜到的是 nuttx 的版本

---

## 最终解决方案

### 方案: 直接修改 nuttx 的 tie.h

这是最简单直接的方案。NuttX 的 tie.h 明显是个 bug：

1. 它说 `XCHAL_CP_NUM=0`（没有协处理器）
2. 但它同时定义了 CP0 和 CP3 的详细信息

对于 ESP32-S3，正确的值就是 `XCHAL_CP_NUM=2`。

**修改文件**: `deps/nuttx/include/arch/chip/tie.h`

```c
// 修改前
#define XCHAL_CP_NUM         0     /* number of coprocessors */

// 修改后
#define XCHAL_CP_NUM         2     /* number of coprocessors (FPU + cop_ai) */
```

---

## 后续问题

### 问题 1: INTSTACK_SIZE 缺失

在修复 XCHAL_CP_NUM 后，遇到新的汇编错误：

```
common/xtensa_int_handlers.S:81: Error: .space, .nops or .fill specifies non-absolute value
```

**原因**: 我们的覆盖版 `src/arch/xtensa/src/common/xtensa.h` 缺少 `INTSTACK_SIZE` 宏定义。

**解决**: 在 xtensa.h 中添加：

```c
#if CONFIG_ARCH_INTERRUPTSTACK > 0
#  define INTSTACK_ALIGNMENT    16
#  define INTSTACK_ALIGN_MASK   (INTSTACK_ALIGNMENT - 1)
#  define INTSTACK_ALIGNDOWN(s) ((s) & ~INTSTACK_ALIGN_MASK)
#  define INTSTACK_ALIGNUP(s)   (((s) + INTSTACK_ALIGN_MASK) & ~INTSTACK_ALIGN_MASK)
#  define INTSTACK_SIZE         INTSTACK_ALIGNUP(CONFIG_ARCH_INTERRUPTSTACK)
#endif
```

### 问题 2: esptool.py 版本不匹配

```
esptool.py not found. Please run: 'pip install esptool'
```

**原因**: 构建系统需要 esptool >= 4.8.0，但系统安装的是 4.7.0。

**解决**:
```bash
# 升级 esptool
pip install esptool --break-system-packages

# 创建包装脚本 bin/esptool.py
#!/bin/bash
exec /home/user/.local/bin/esptool "$@"
```

---

## ESP32-S3 构建成功

```
LD: nuttx
Memory region         Used Size  Region Size  %age Used
             ROM:      207804 B    4194272 B      4.95%
     iram0_0_seg:         36 KB       304 KB     11.84%
     irom0_0_seg:      147138 B    4194272 B      3.51%
     dram0_0_seg:       34692 B       288 KB     11.76%
CP: nuttx.hex
MKIMAGE: ESP32-S3 binary
Successfully created ESP32-S3 image.
Generated: nuttx.bin
```

**输出文件**:
- `deps/nuttx/nuttx` - ELF 可执行文件 (3.7MB)
- `deps/nuttx/nuttx.bin` - ESP32-S3 二进制镜像 (207KB)
- `deps/nuttx/nuttx.hex` - Intel HEX 格式

---

## 构建命令

```bash
# ESP32-S3 目标
cd scripts/esp32s3 && ./build.sh nuttx

# ESP32-CAM 目标
cd scripts/esp32cam && ./build.sh nuttx

# 清理
cd scripts/esp32s3 && ./nuttx_build.sh clean
cd scripts/esp32cam && ./nuttx_build.sh clean
```

---

## Logo 小海龟画图解释器实现

从头实现了一个简单、轻量的 Logo 解释器：

**文件结构**:
```
src/lvgl/app/logo/
+-- logo_turtle.h    # 头文件，数据结构，API 定义
+-- logo_turtle.c    # 海龟图形核心实现
+-- logo_vm.c        # 解释器/命令解析器
+-- logo_draw.c      # LVGL 绑定
+-- Makefile         # 构建脚本
```

**主要特性**:
- MIT 许可证，完全兼容 Apache 2.0
- 轻量级，约 400 行代码
- 支持基本海龟绘图命令
- 可选的 LVGL 绑定

**支持的命令**:
```
FD n / FORWARD n   - 前进 n 步
BK n / BACK n      - 后退 n 步
LT n / LEFT n      - 左转 n 度
RT n / RIGHT n     - 右转 n 度
PU / PENUP         - 抬笔
PD / PENDOWN       - 落笔
HOME               - 回到原点
CS / CLEARSCREEN   - 清屏
ST / SHOWTURTLE    - 显示海龟
HT / HIDETURTLE    - 隐藏海龟
SETPENCOLOR [r g b] - 设置颜色
SETPENSIZE n       - 设置画笔粗细
SETX n             - 设置 X 坐标
SETY n             - 设置 Y 坐标
SETH n             - 设置朝向
```

---

## 2026-04-01 更新：QEMU 构建与运行

### 问题 1: NuttX 12.7.0 编译失败 - cop_ai 汇编指令错误

**错误信息**:
```
common/xtensa_coproc.S:287: Error: unknown opcode or format name 'rur.accx_0'
common/xtensa_coproc.S:378: Error: unknown opcode or format name 'wur.qacc_l_4'
common/xtensa_context.S:256: Error: unknown opcode or format name 'ld.qr'
```

**根本原因**:
- 设置 `XCHAL_CP_NUM=2` 后，`xtensa_coproc_savestate` 宏尝试保存 cop_ai (CP3) 状态
- 但 QEMU 使用的工具链 `xtensa-esp32s3-elf-gcc` 不支持 `cop_ai` 相关指令
- 这些是模拟器特有的协处理器指令

**解决方案**:
修改 `deps/nuttx/include/arch/chip/tie.h`：

```c
// 修改后 - 仅启用 FPU (CP0)，禁用 cop_ai (CP3)
#define XCHAL_CP_NUM         1     /* number of coprocessors (FPU only, for QEMU) */
#define XCHAL_CP3_SA_SIZE    0     /* size of state save area */
```

---

### 问题 2: imgtool 未安装

**解决方案**:
```bash
pip install imgtool --break-system-packages
```

---

### 问题 3: 系统 QEMU 不支持 ESP32

**原因**: Ubuntu/Debian 仓库的 qemu-system-xtensa 是通用版本，不支持 Espressif ESP32 芯片。

**解决方案**: 安装 Espressif 专用 QEMU

```bash
# 从 GitHub 下载
axel -n 20 https://github.com/espressif/qemu/releases/download/esp-develop-9.0.0-20240606/qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz

# 解压到 deps 目录
cd deps/esp-idf-tools
tar -xf qemu-xtensa-softmmu-esp_develop_9.0.0_20240606-x86_64-linux-gnu.tar.xz
```

---

### 问题 4: QEMU 网络权限不足

**错误信息**:
```
qemu-system-xtensa: -netdev tap,id=tap0,ifname=tap0,script=no: could not configure /dev/net/tun (tap0): Operation not permitted
```

**解决方案**:
1. 使用 `sudo` 运行（需要正确配置）
2. 或使用 Docker 方式：
```bash
docker run --rm -it espressif/idf-qemu
```

---

### 最终构建结果（ESP32-S3）

```
LD: nuttx
Memory region         Used Size  Region Size  %age Used
        metadata:          80 B         96 B     83.33%
             ROM:      277236 B    4194176 B      6.61%
     iram0_0_seg:       28160 B       280 KB      9.82%
     irom0_0_seg:      132503 B         4 MB      3.16%
     dram0_0_seg:       43152 B       264 KB     15.96%
CP: nuttx.hex
MKIMAGE: ESP32-S3 binary
Generated: nuttx.bin (MCUboot compatible)
```

**输出文件**:
- `deps/nuttx/nuttx` - ELF 可执行文件 (554KB)
- `deps/nuttx/nuttx.bin` - ESP32-S3 二进制镜像 (1.0MB)
- `deps/nuttx/nuttx.hex` - Intel HEX 格式 (500KB)

---

### QEMU 运行命令

```bash
# 方法1: 使用构建脚本（需要 root 权限或网络配置）
source scripts/setup_tools.sh
cd scripts/esp32s3 && ./nuttx_build.sh qemu

# 方法2: Docker 方式
docker run --rm -it espressif/idf-qemu

# 方法3: 直接运行（无网络）
source scripts/setup_tools.sh
qemu-system-xtensa -machine esp32 -nographic -kernel deps/nuttx/nuttx
```

---

## 2026-04-01 更新：依赖版本与镜像源

### 依赖版本更新

| 组件 | 原版本 | 新版本 | 备注 |
|------|--------|--------|------|
| Apache NuttX | 12.7.0 | **12.12.0** | 2025-12-31 发布 |
| Apache NuttX Apps | 12.7.0 | **12.12.0** | 与 NuttX 同步 |
| ESP-IDF | v5.3.3 | **v5.5.4** | 2025 最新稳定版 |
| LVGL | v9.5.0 | v9.5.0 | 最新稳定版 |
| Duktape | v2.7.0 | v2.7.0 | 最新稳定版 |
| my_basic | master | master | 无版本标签 |

### 国内镜像源

| 组件 | 镜像源 | 下载地址 |
|------|--------|----------|
| Apache NuttX | BFSU 镜像 | https://mirrors.bfsu.edu.cn/apache/nuttx/ |
| Apache NuttX Apps | BFSU 镜像 | https://mirrors.bfsu.edu.cn/apache/nuttx/ |
| ESP-IDF | Gitee 镜像 | https://gitee.com/EspressifSystems/esp-idf |
| 其他组件 | GitHub | https://github.com/ |

### 下载加速

推荐使用 `axel` 多线程下载工具：

```bash
# 安装 axel
sudo apt install axel

# 使用示例
axel -n 20 -a https://example.com/file.tar.gz -o output/
```

### 工具链版本更新

| 工具 | 原版本 | 新版本 |
|------|--------|--------|
| crosstool-NG | esp-13.2.0 | **esp-15.2.0** |
| binutils-gdb | esp-gdb-v14.2 | **esp-gdb-v16.3** |

### 使用下载脚本

```bash
# 下载所有依赖（使用镜像源和 axel 加速）
./scripts/download_deps.sh

# 仅下载工具链
./scripts/download_deps.sh --tools-only

# 安装工具链
./scripts/download_deps.sh --install-tools
```

---

## 经验总结

1. **问题定位**: 遇到 "undeclared" 错误时，先检查预处理后的结果 `gcc -E`

2. **Include 路径优先级**:
   - `-isystem` > `-I` > 默认系统路径
   - 理解编译器的搜索顺序很重要

3. **预处理指令顺序**: `#include` 在编译前展开，`#define` 的位置必须正确

4. **Bug vs 兼容性**: 当第三方库有明显 bug 时，直接修改可能比 workaround 更可靠

5. **版本依赖**: 嵌入式构建系统通常有严格的工具版本要求

6. **多目标编译**: 不同目标（ESP32-S3 / ESP32-CAM）需要独立的编译脚本和配置

---

_最后更新: 2026-04-02（移除 Python 脚本引擎）_

---

## 2026-04-02 更新：ESP32-CAM 目标首次成功编译

### 概述

ESP32-CAM（AI-Thinker，ESP32 非 S3）目标首次成功编译 NuttX 12.12.0 固件。

### 遇到的问题

#### 问题 1: NuttX tarball 无 .git 目录

**现象**: 从 BFSU 镜像下载的 NuttX tarball 解压后没有 `.git` 目录。

**解决**: 手动 `git init && git add -A && git commit`，创建本地 git 仓库。

#### 问题 2: esp-hal-3rdparty 无法 git clone（GitHub 不可达）

**现象**: `make context` 阶段尝试 `git clone` esp-hal-3rdparty，但 GitHub 无法访问。

**解决**:
1. 通过 ghfast.top 錡像（`https://ghfast.top/https://github.com/...`）下载 tarball
2. 解压到 `deps/nuttx/chip/esp-hal-3rdparty`
3. 创建本地 git 仓库
4. 使用 `STORAGETMP=y USE_NXTMPDIR_ESP_REPO_DIRECTLY=y` 参数让 NuttX 跳过 clone/checkout

#### 问题 3: esp-hal-3rdparty 子模块未初始化

**现象**: esp-hal-3rdparty 的 mbedtls/esp_phy/esp_wifi/bt/esp_coex 等子模块目录为空。

**解决**: 通过 GitHub API 获取每个子模块的 commit hash，然后用 ghfast.top 镜像逐个下载 tarball 并解压到正确位置。

```bash
# 示例：获取子模块 commit hash
curl -s "https://api.github.com/repos/espressif/esp-hal-3rdparty/git/trees/<commit>?recursive=1" | \
  python3 -c "import json,sys; data=json.load(sys.stdin); [print(f'{i[\"path\"]}: {i[\"sha\"]}') for i in data.get('tree',[]) if i.get('mode')=='160000']"
```

#### 问题 4: mbedtls 补丁重复应用

**现象**: NuttX 编译系统每次 `make context` 都会 `git apply` mbedtls 补丁，第二次编译时补丁已应用导致冲突。

**解决**:
1. 手动 `git apply` 补丁并 commit
2. 将补丁文件替换为空操作补丁（创建一个无害的 marker 文件）
3. 编译脚本中添加清理 marker 文件的步骤

#### 问题 5: download_deps.sh 交互式提示阻塞

**现象**: 脚本中的 `read -p "是否重新下载? (y/N): "` 在非交互环境下导致脚本挂起。

**解决**: 移除所有 `read -p` 交互式提示，改为自动跳过。

#### 问题 6: 配置缺失导致链接错误

**现象**: 多个 undefined reference 错误。

**解决**:

| 错误 | 配置 | 说明 |
|------|------|------|
| `rt_timer_time_us` | `CONFIG_ESP32_RT_TIMER=y` | ESP32 RTC timer 驱动 |
| `work_queue/work_cancel` | `CONFIG_SCHED_WORKQUEUE=y` + 基础 nsh defconfig 已包含 |
| `xtensa_netinitialize` | `CONFIG_NETDEV_LATEINIT=y` | 无以太网时跳过网络初始化 |
| `esp32_spibus_initialize` | `CONFIG_ESP32_SPI2=y` | SD 卡所需 SPI |
| `__MIN_TCP_MSS` | `CONFIG_NET_ETHERNET=y` | TCP/IP 需要以太网帧定义 |

### 最终编译结果

```
Memory region         Used Size  Region Size  %age Used
             ROM:      471868 B    4194272 B     11.25%
     iram0_0_seg:       70608 B       168 KB     41.04%
     irom0_0_seg:      340772 B    3342304 B     10.20%
     dram0_0_seg:       25904 B     180736 B     14.33%
     drom0_0_seg:       88988 B    4194272 B      2.12%
    rtc_iram_seg:          0 GB         8 KB      0.00%
    rtc_slow_seg:          24 B         4 KB      0.59%
rtc_reserved_seg:          24 B         24 B    100.00%
      extmem_seg:          0 GB         4 MB      0.00%

固件: deps/nuttx/nuttx.bin (476KB)
```

### ESP32-CAM 构建命令

```bash
# 完整流程
./scripts/download_deps.sh        # 下载依赖
source scripts/setup_tools.sh     # 激活工具链
cd scripts/esp32cam
./nuttx_build.sh defconfig         # 应用配置
./nuttx_build.sh build             # 编译
```

### 关键修改文件

| 文件 | 修改内容 |
|------|----------|
| `scripts/download_deps.sh` | 移除 5 处交互式 `read -p` 提示 |
| `scripts/esp32cam/nuttx_build.sh` | 添加 `STORAGETMP/USE_NXTMPDIR_ESP_REPO_DIRECTLY` 参数， |
| `scripts/esp32cam/build.sh` | 同步 EXTRAFLAGS 和 STORAGETMP 参数 |
| `configs/nuttx-defconfig-esp32cam` | 緻加 `ESP32_RT_TIMER`、`ESP32_SPI2`、`NETDEV_LATEINIT` 等关键配置 |

## 2026-10-04（晚）：全真硬件化 + nano 移植构建修复链

### 问题 1：真 GNU nano 8.4 NuttX 移植（六连修）
1. `#define HAVE_X 0` 命中 nano 的 `#ifdef HAVE_X`（autoconf 惯例：关闭项必须【不定义】）——config.h 重写
2. 键码/octal 值错误（KEY_SDC=0576→0600 等）——按 ncurses 数值表全量修正；KEY_MOUSE/KEY_EOL/curscr/isendwin/beep/define_key/wredrawln/mvwaddch/wscrl/printf 族补齐
3. NuttX libc 缺口：mkstemps（compat.c 实现）、setlocale/nl_langinfo（存根恒 UTF-8）、REG_STARTEND（置 0 退化全串匹配）
4. CONFIG_LIBC_REGEX 依赖 ALLOW_MIT_COMPONENTS（TRE 为 MIT 许可）——appconfig 两行
5. HAVE_CONFIG_H 未定义（definitions.h 门控 <config.h>）——Makefile CFLAGS
6. mini_curses poll：初版假 poll 会 busy-loop——改 NuttX 标准 pollfd 等待队列模式（pipe 驱动同款）

### 问题 2：riscv 工具链 PATH
- olddefconfig 阶段也需工具链（kconfig 编译探测）+ c3 需 shim-riscv64（riscv64-unknown-elf 前缀）并入 PATH

### 问题 3：build 脚本 SIGPIPE 误杀
- `make | grep | head` 管道：head 提前退出令 make 收 SIGPIPE 假败——改全量日志落盘后抽错；RP2040 无 nuttx.bin 时 `ls` 非零退出在 set -e 下炸脚本——`|| true`

### 问题 4：CAM 离线 submodule 清单
- esp32@4eed03a 快照 index 无 gitlink，Make.defs:280 的 `git submodule update --init <五路径>` 报"路径规格未匹配"——prepare_esp_hal.sh 幂等补丁：清单裁成 mbedtls-only（phy/无线库已 tarball 预置 + mkdir 建仓），守卫条件放宽（"already applied" 标记不再短路新补丁）

### 问题 5：宿主门适配
- cvbs_console 引 drv_cvbs_frame：宿主测试不链硬件层——weak 符号 + 空判
- 语法矩阵：rp2040 arch 头路径 + stubs/arch/rp2040/chip.h（84/84 全绿）


## 2026-10-04（深夜）：硬件全真外设收口 + EDA 载板生成

### 问题 1：WS2812 状态灯 gpio 直驱点不亮（真 BUG）
- ble_hid.c 用 `gpio_set_level()` 驱 WS2812（单线 ns 级协议，GPIO 直驱无效，
  HARDWARE.md 2.8 早有记载但驱动一直是占位）——新增
  `src/nuttx/esp32s3/driver/ws2812_rmt.c`：走 NuttX 树内 RMT 硬件外设链
  （CONFIG_RMT+RMTCHAR+ESP_RMT → 板级 /dev/rmt0，devkit 板头已绑 GPIO38/48），
  仅做 24-bit GRB→RMT 符号编码；esp32s3_retro.c 开机自检点灯、ble_hid.c 改调
- 宿主测试 353 项（契约时序窗/蜕变/差分对标 nuttx-apps ws2812esp32rmt 例程/
  2^20 PBT 回读/模糊）+ 变异 12/12 杀死 100%

### 问题 2：FSK 编入构建即编译失败
- Makefile 首次纳入 drv_fsk.c（原 CONFIG_RETRO_FSK 默认 y 但源码从未编入），
  立即暴露 `CONFIG_RETRO_FSK_BAUD` 未定义：源码的兜底 `#ifndef CONFIG_RETRO_FSK`
  逻辑反了（开启时反而缺定义）——双修：Kconfig 补 `RETRO_FSK_BAUD` int 项 +
  源码改无条件 `#ifndef CONFIG_RETRO_FSK_BAUD` 兜底
- 同步把 fsk_send() 的 TODO 占位换成 audio_play_pcm() 硬件出声
  （I2S+GDMA/内置 DAC，符合 HARDWARE.md 13.1 禁 CPU 位摆）

### 问题 3：lv_port_disp_init() NULL 指针（真 BUG，设备 GUI 启动即崩）
- 旧实现在 drv_cvbs_init()（帧缓冲分配者）之前 `memset(g_fb,...)`，且从未把
  g_fb 接到 cvbs_core 缓冲——tools/sim cvbs_pipeline 模拟器段错误捕获；
  修复：先 init 驱动再 `g_fb = drv_cvbs_get_fb()`，修复后全链路 PASS
  （625 波形行/480 解码行）

### 问题 4：硬件档案三处与 HARDWARE.md 失同步
- hw_rp2040_pico.h 仍为旧双脚 CVBS 方案（GP20 同步/GP21 视频）且占用表
  与 GP23=SMPS 冲突——按 3B.2A 修订同步为 GP12-15 四位梯（GP23 会把 3V3
  纹波耦合进 R-2R 基准 + PIO OUT PINS 只能连续映射）
- hw_esp32c3_luatos.h：LED 极性修正（GPIO12/13 高电平点亮，LuatOS wiki
  管脚表）、Flash 占用修正（11=VDD_SPI、14-17=总线、12/13 DIO 模式可用）、
  GPIO10 释放为教学脚（PDM 单脚方案）、CVBS 注释对齐
- hw_esp32s3_devkitc.h：CVBS 注释由"I2S bitbang"修正为 LCD_CAM I80
  （DATA_OUT0-3 → GPIO2/15/16/17，对齐驱动实现）
- hw_esp32cam_aithinker.h：补 GPIO17=PSRAM CLK 禁用项

### 问题 5：测试基建三处
- check_syntax.sh 头注释缺 `#`（shell 尝试执行注释行）
- run_all.sh 依赖外部 /tmp/fontbridge 无装配步骤——补字体桥拷贝
- test_rpkg_diff.py Hypothesis 捕获同径文件/目录冲突（{'a','a/a'}）——
  pkg_tree 策略剔除互为前缀路径

### 问题 6：立创EDA 载板自动布线（eda/gen_eda.py）
- 多处布线器缺陷逐个捕获修复：多段提交失败残留半截走线（无回滚）、
  焊盘禁入区漏算焊盘半径（走线可压盘边缘=物理短路）、平行线边界 0.35
  过保守（0.7 间距物理合法）、缺板界约束（走线出板外）、FPC 0.5mm 密排
  的水平逃逸全堵（新增垂直下潜模板）、焊盘改名半径 0.6 误吞相邻 FPC 脚
  （收窄 0.2）——最终五板 PCB 同层异网零交叉（check_eda.py PASS）


## 2026-10-05（续）：控制台字形排版三连 BUG（用户发现标点悬浮）

### 现象
用户目视截图发现 `,` `.` `_` 等标点不在行底部而飘在行中间。

### 问题 1：字形墨迹框"垂直居中"
- glyph_put 用 `oy=(CELL_H-2-box_h)/2` 居中——逗号/句号/下划线的墨迹
  本来就在基线附近及以下，居中后悬浮。修复：**基线对齐**（cell 基线=
  底-3；字形底边=基线-ofs_y，ofs_y 语义与 LVGL/兼容层一致"基线以上为正"）

### 问题 2：双重 py0（居中修复引入，随即被新测试抓出）
- 修复 1 的 gy0 算出的是**绝对像素行号**（已含 py0），循环里又写
  `py0 + gy0 + gy`——cy=3 的字画到 2×42=84 行（首行巧合正确掩盖）。
  最小复现（写 6 行 L00-L05）+ 逐字符 trace + 图像分析定位；修复为
  px 直接用绝对 gy0

### 问题 3：全角字符光标只推进 1 格（历史 BUG，被位置矩阵暴露）
- putc 的 UTF-8 完成路径恒 `++cx`——"你界"连写 cursor=2（应为 4），
  全角与后续字符重叠错位。修复：按字形 adv_w 判宽（adv>CELL_W/2 → +2）

### 测试锁定（tests/host/test_cvbs_console.c 666 检查全绿）
- 新增**字形位置矩阵**（用户需求：大小写/数字/中文/全角半角符号查
  显示也查位置）：表驱动断言每类墨迹带——大写/数字底贴基线、下伸
  g/p/y 入基线下、引号在上半带、CJK 占 2 格填满、全角，在右下带等
- 新增 cvbs_console_cursor_visible() API（测试扫描去光标下划线污染）
- glm53f 逐符号视觉验收：半角 , . ; : ! ? " ' _ 与全角 ，：；""
  位置全部正确（console_320 截图，README 已更新）


## 2026-10-05（续2）：输入法落地过程中的连环修复

1. **GUI 词库码制错**（历史 TODO 自爆）：单字/词组表存 GB2312 码而 encode 按
   Unicode——候选乱码。修法（用户点拨）：Python 一次性把词库数据**原地重写为
   Unicode**（替代运行时映射表方案——零运行时开销零额外数据）
2. **GUI search 词组丢弃**：find_phrases 结果未并入 candidates（复合拼音
   nihao 恒无候选）；修为词组优先并入
3. **GUI 面板从未渲染过的一串布局 bug**：面板高 36+20 叠加超屏且行重叠、
   候选容器高=按钮高被 LVGL 默认 padding 挤出可视区（按钮变空框）、按钮/
   状态 label 缺 12px 中文字体、screen 布局覆盖 set_pos（改挂 lv_layer_top）
4. **CLI 引擎三 bug**：数字选字后 reset 清掉刚选的字；中文 Enter 清整行；
   返回值语义混叠（字母 1=需刷候选 vs Enter=行完成）——cvbs_ime 按 ch 判
   行完成、引擎只清拼音
5. **IME 键路径**：UART 字节流组合键 Ctrl+Space=0x00 / Ctrl+Q=0x11 天然可辨；
   输入环从 #ifdef __NuttX__ 提为通用层（IME 行回放与设备 read 共用）
