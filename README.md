/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

> **免责声明 / Disclaimer**
>
> **本系统全部源代码均由 AI 生成**（All system source code is AI-generated）：
> 约 4.3 万行 C 代码（内核驱动、GUI、脚本引擎、包管理器、EDA 载板生成器）
> 由 AI 编写，人类仅负责提出需求、考证硬件事实与验收结果。代码为
> Vibr Coding（边想边写）风格，未经任何实机验证，何时能完成验证仍是
> 未知数。如果您对这个项目感兴趣，欢迎自行 fork 并修改，这可能比等待
> 作者完善更加高效。
>
> **感谢以下 AI 工具的加持（排名不分先后）：**
> - 豆包、Doubao、OpenClaw、OpenCode、Cline、Claude Code、Zcode、MiniMax 2.7、GLM 5.1、GLM 5.3、GLM 5.3 Flash
>
> 没有你们的加持也就没有这个项目，在此一并致谢

---

# ESP32 复古联网图形工作站 / ESP32 Retro Graphics Workstation

基于 ESP32 双核 Xtensa + Apache NuttX RTOS + LVGL 的纯嵌入式复古图形工作站。

> **多目标支持（五板）**：开发以 **ESP32-S3** (DevKitC-1, **N16R8/N8R8 首选板**) 为模板，兼容 **ESP32-CAM** (AI-Thinker)、**合宙 ESP32-C3 核心板**（CLI）与 **Raspberry Pi Pico**（CLI，本地教学终端）。
> - **ESP32-S3** (首选/开发模板)：8MB Octal PSRAM + 8/16MB Flash，USB HID 键鼠 + BLE HID，I2S -> 电阻网络 CVBS 输出，显示最高 1024x768（实验）
> - **ESP32-CAM** (兼容目标)：4MB Flash + 4MB PSRAM，内置 DAC (GPIO25/26)，BLE HID 键鼠，板载 OV2640 摄像头（可选，与 CVBS/音频互斥），显示上限 640x480
> - **合宙 ESP32-C3 核心板** (低资源/低价格第三目标)：RISC-V 单核 160MHz + 4MB Flash + 400KB SRAM（无 PSRAM），**纯 CLI 工作站**——软件经 .rpk 包管理器安装；经典款（CH343 串口）/ 简约款（原生 USB）均支持
>
> 显示分辨率三档：**320x240 控制台（240p）/ 640x480 常规（480i）/ 1024x768 最高（实验性）**
> 包架构隔离：Xtensa（S3/CAM）与 RISC-V（C3）二进制不通用，.rpk 包经 Arch 字段校验

## 项目状态 / Project Status

| 项目 | 内容（更新于 2026-10-05） |
|------|------|
| **当前阶段** | 五板固件**全部编译通过**；宿主测试 + glm 多模态视觉验收闭环；待实机烧录验证 |
| **代码规模** | 手写 C 源码 **~42,400 行** + 自研测试 **~3,700 行** + EDA/工具脚本 **~2,100 行**（另有生成的 12px 全量中文字库点阵 ~15 万行） |
| **多目标支持** | **五板**：ESP32-S3 N16R8 / N8R8 + ESP32-CAM + 合宙 C3（CLI）+ Raspberry Pi Pico（CLI） |
| **编译验证** | **五板全部通过**（2026-10-05：pico 1181KB / c3 1559KB / cam 1912KB / s3·s3n8 1852KB） |
| **宿主验证** | 单元/蜕变/差分/PBT/模糊/双变异门 ALL PASS（12 项套件，语法矩阵 85/85） |
| **EDA 载板** | 五板立创EDA工程已生成（自动布线 + 零交叉校验 PASS） |
| **多语种支持** | **中英双语**（全系唯一 12px 中文字号） |
| **依赖下载** | **完成** |
| **实机测试** | **待开发板**（唯一未开始项） |

### 系统模拟截图 / Simulation Screenshots

以下截图全部由**真实固件源码**无头渲染（非示意图）：GUI 经真实
desktop.c/wmaker_shell.c + LVGL 9.5 以 RGB565 渲染后量化为 **8-bit
256 色调色板**（640x480，Win3.2 时代 VGA 规格）；CLI 经真实
cvbs_console（12px 点阵、12x14 网格）渲染（320x240，240p）。

| Win3.2 外壳（640x480 8bit 彩色） | WindowMaker/NeXT 外壳（640x480 8bit 彩色） |
|:---:|:---:|
| ![Win3.2 桌面](docs/screenshots/desktop_win3_color.png) | ![WindowMaker 桌面](docs/screenshots/desktop_wmaker_color.png) |

| AV 控制台 / CLI（320x240，NSH + 中文 12px 点阵 + CCDOS 式输入法条） |
|:---:|
| ![AV 控制台](docs/screenshots/console_320.png) |

**拼音输入法两形态**（2026-10-05）：

| GUI：Win95 式输入条（640x480，拼音+数字选字+中英按钮） | CLI：CCDOS 式底部常驻条（320x240，`ime on` 启动） |
|:---:|:---:|
| ![GUI 输入法](docs/screenshots/ime_win95.png) | 见上图控制台截图底部反色条 |

- **GUI**：文本框聚焦自动唤起；键盘 `nihao` → 候选 `1.你 2.您`，数字键选字
- **CLI**：`ime on` 屏幕最下方出现常驻反色输入法条（占末行、正文照常滚动）；
  `Ctrl+Space` 中英切换、`Ctrl+Q` 退出释放；Enter 整行回放给终端

> 截图再生成：`bash tools/sim/build.sh && /tmp/retro_sim/lvgl_sim_color
> && /tmp/retro_sim/console_sim`（宿主机渲染管线，与固件同一份 UI 源码）。

### 已完成里程碑 / Completed Milestones

- [x] M1: 需求分析和方案设计 / Requirements analysis
- [x] M2: 代码编写 / Code development
- [x] M3: 依赖下载和验证 / Dependency download
- [x] M4: 图形界面开发 / GUI development (桌面+应用)
- [x] M5: 拼音输入法开发 / Pinyin IME development
- [x] M6: 多语种框架 / i18n framework
- [x] M7: 工具链安装和编译 / Toolchain & build
- [x] M8: 多目标架构重构 / Multi-target refactoring
- [x] M9: ESP32-CAM 硬件适配 / ESP32-CAM adaptation（编译+宿主验证完成，2026-10-04）
- [x] M10: ESP32-S3 硬件适配 / ESP32-S3 adaptation（编译+宿主验证完成，2026-10-04）
- [x] M14: AV 输出全真硬件化 / True-hardware AV（LCD_CAM/DAC/PDM/PIO + DMA，2026-10-04）
- [x] M15: WS2812 RMT / FSK I2S / 硬件 I2C 收口 + 字号定档 12px（2026-10-05）
- [x] M16: 立创EDA 五板载板工程 / LCEDA carrier boards（2026-10-05）
- [ ] M11: QEMU 模拟验证 / QEMU simulation
- [ ] M12: 开发板烧录 / Board flashing
- [ ] M13: 实机测试 / Hardware testing

### 项目文档 / Project Documentation

| 文档 / Doc | 说明 / Description |
|------|------|
| `README.md` | 项目概览 / Project overview |
| `REQUIREMENTS.md` | 需求说明书 / Requirements |
| `COMPLETED.md` | 已完成需求清单 / Completed requirements |
| `NEXT_STEPS.md` | 下一步工作计划 / Next steps |
| `SYSTEM.md` | 系统架构文档 / System architecture |
| `HARDWARE.md` | 硬件规格参考 / Hardware specification |
| `CODING_STANDARD.md` | 编码规范 / Coding standards |
| `BUILD_FIXES.md` | 构建修复日志 / Build fixes log |
| `DEPENDENCIES.md` | 依赖说明 / Dependencies |

## 快速开始

### 下载依赖

```bash
# 1. 下载所有依赖（源码）
cd /home/user/esp32-retro-ws
./scripts/download_deps.sh

# 2. 下载 ESP-IDF 工具链（使用 axel 多线程下载）
./scripts/download_deps.sh --tools-only
./scripts/download_deps.sh --install-tools

# 3. 安装系统依赖
sudo apt-get install cmake ninja-build

# 4. 激活工具链环境
source scripts/setup_tools.sh
```

### 一键整体构建（推荐）

```bash
# 固件（Apache-2.0，零 GPL）+ 可选安装包（GPL 组件为独立 ELF）
./scripts/build_all.sh esp32s3          # 或 esp32cam / all / --no-packages

# 产物：deps/nuttx/nuttx.bin（固件） + dist/sdcard/（SD 安装目录）
# 安装：dist/sdcard 整体拷入 SD 卡，固件侧 pkg list 验证
```

GPL 组件（如 UCBLogo）不编入固件 ROM，以**独立安装包**交付（mere aggregation，
许可证隔离，详见 DEPENDENCIES.md）。

### 脚本直接操作 GPIO（教学模式）

五种脚本语言均可直接读写 GPIO/ADC/PWM（`retro_gpio` 统一接口），
系统占用引脚会被直接拒绝并提示占用原因：

```basic
' BASIC：按键点灯（ESP32-S3，用户按键 GPIO8 -> 扩展脚 GPIO4 接 LED）
CALL retro_gpio_config(8, "in")
CALL retro_gpio_config(4, "out")
DO
  IF retro_gpio_read(8) = 0 THEN CALL retro_gpio_write(4, 1) ELSE CALL retro_gpio_write(4, 0)
LOOP
```
```berry
# Berry：LED 呼吸灯
for i : 0..100
  retro_gpio_pwm(4, i, 1000)
  sleep(10)
end
```
```javascript
// JS：读模拟量
var v = retro_gpio.adc(0);
```
```python
# Python：占用脚会被拦截
import retro_gpio
retro_gpio.config(2, "out")   # -> GPIO2 已被系统占用: CVBS 视频输出
```

### 软件包管理（deb 风格 .rpk）

软件不是裸拷 ELF，而是完整包管理（参考 Debian）：

```bash
# 主机侧打包（包源目录: control/manifest/维护脚本/data 载荷树）
./scripts/make_package.sh <包源目录> <输出目录>

# 设备侧（NSH）
nsh> pkg install /sdcard/pkg/ucblogo-6.2.2-1.rpk   # 安装（依赖检查+CRC 校验）
nsh> pkg list                                        # 已安装列表
nsh> pkg info ucblogo                                # 包详情
nsh> pkg remove ucblogo                              # 卸载（执行 prerm/postrm）
nsh> /sdcard/apps/ucblogo                            # 运行（binfmt 独立进程）
```

### ESP32-S3 目标编译

```bash
# 配置
cd scripts/esp32s3 && ./nuttx_build.sh defconfig

# 编译
./build.sh nuttx

# 烧录
./build.sh flash
```

### ESP32-CAM 目标编译

```bash
# 配置
cd scripts/esp32cam && ./nuttx_build.sh defconfig

# 编译
./build.sh nuttx

# 烧录
./build.sh flash
```

### ESP32-C3 目标编译（合宙核心板，RISC-V CLI 工作站）

```bash
# 配置
cd scripts/esp32c3 && ./nuttx_build.sh defconfig

# 编译
./build.sh nuttx

# 烧录（经典款 /dev/ttyUSB0，简约款原生 USB 通常为 /dev/ttyACM0）
./build.sh flash
```

## 已实现功能 / Implemented Features

| 功能 / Feature | 模块 / Module | 行数 / Lines | 状态 / Status |
|------|------|------|------|
| 双核 SMP | esp32s3_retro.c / esp32_retro.c | ~750 | 完成 |
| **WindowMaker/NeXT 外壳（可切换）** | wmaker_shell.c + desktop_api.h | ~430 | 完成（可选） |
| 看门狗 WDT | watchdog.c | ~860 | 完成 |
| 防火墙 Firewall | firewall.c | 623 | 完成 |
| 内存监控 | memmon.c | 537 | 完成 |
| WiFi 连接 | network.c | 432 | 完成 |
| NTP 对时 | ntp.c | 444 | 完成 |
| Cron 定时任务 | cron.c | 739 | 完成 |
| CVBS 显示驱动 | drv_cvbs.c / drv_cvbs_dac.c | ~855 | 完成 |
| FSK 磁带机 | drv_fsk.c | ~886 | 完成 |
| I2S/DAC 音频 | drv_audio.c / drv_audio_dac.c | ~1234 | 完成 |
| RTC 时钟 | drv_rtc.c | 538 | 完成 |
| USB HID 键鼠 | usb_hid.c (S3) | 424 | 完成 |
| BLE HID 驱动 | ble_hid.c (CAM) | 1001 | 完成 |
| BLE Bond 存储 | ble_storage.c | 391 | 完成 |
| BLE NSH 命令 | ble_nsh.c | 289 | 完成 |
| BLE 配对 UI | ble_pair_ui.c | 252 | 完成 |
| 启动菜单 | bootmenu.c | 502 | 完成 |
| 脚本引擎 | script_engines.c | 649 | 完成 |
| curl/wget | network_utils.c | 487 | 完成 |
| NSH 命令 | nsh_cmds.c | 275 | 完成 |
| LVGL 桌面 | desktop.c | 1136 | 完成 |
| 记事本编辑器 | app_editor.c | 636 | 完成 |
| 浏览器 | app_browser.c | 564 | 完成 |
| 终端模拟器 | app_terminal.c | 673 | 完成 |
| 拼音输入法 | app_pinyin.c | 557 | 完成 |
| 多语种框架 | i18n.c | 493 | 完成 |
| Logo 海龟画图 | logo/ | ~400 | 完成 |
| 扫雷游戏 | desktop.c | 内嵌 | 完成 |
| 文件管理器 | desktop.c | 内嵌 | 完成 |
| 控制面板 | desktop.c | 内嵌 | 完成 |

## 图形界面应用 / GUI Applications

| 应用 / App | 图标 / Icon | 说明 / Description |
|------|------|------|
| 记事本 Notepad | icon_notepad.png | 文本编辑器，支持行号/搜索/多标签 |
| 浏览器 Browser | icon_browser.png | 简易网页浏览器，URL 栏 + HTML 渲染 |
| 终端 Terminal | icon_terminal.png | 命令行终端，16 个内置命令 |
| 扫雷 Minesweeper | icon_minesweeper.png | 经典扫雷游戏 |
| 文件管理器 | icon_folder.png | 磁盘文件浏览 |
| 控制面板 | icon_control_panel.png | 系统设置面板 |
| 我的电脑 | icon_pc.png | 桌面图标 |
| 媒体播放器 | icon_player.png | WAV 音频播放，进度条/音量控制 |
| 录音机 | icon_recorder.png | 音频录制，波形显示 |
| SQLite 工具 | icon_sqlite.png | SQL 查询/执行/结果导出 |
| Logo 海龟画图 | icon_logo.png | 小海龟画图，Logo 语言编程 |

## 硬件规格对比

| 项目 | ESP32-S3 (DevKitC-1 N16R8/N8R8) | ESP32-CAM (AI-Thinker) |
|------|---------------------|----------------------|
| 定位 | **首选板 / 开发模板** | 兼容目标（顺带支持） |
| 主控 | ESP32-S3 (双核 LX7 240MHz, 8MB Octal PSRAM, 8/16MB Flash) | ESP32 (双核 LX6 240MHz, 4MB PSRAM, 4MB Flash) |
| 显示 | CVBS AV 输出（I2S -> 电阻网络 -> GPIO2） | CVBS AV 输出（内置 DAC -> GPIO25） |
| 显示分辨率 | 320x240 / 640x480 / 1024x768（实验） | 320x240 / 640x480（DAC 带宽所限） |
| 音频输出 | 外部 I2S DAC（GPIO40/41/42） | 内置 DAC（GPIO26） |
| 音频输入 | ADC（GPIO1） | ADC（GPIO34） |
| 键盘鼠标 | USB HID + 蓝牙 BLE HID | 蓝牙 BLE HID |
| 状态 LED | WS2812 RGB（GPIO38 v1.1 / GPIO48 v1.0） | 红色 LED GPIO33 + Flash LED GPIO4 |
| SD 卡 | SPI 模式（GPIO10/11/13/14） | SPI 模式（GPIO13/15/2/14） |
| 网络 | WiFi 802.11 b/g/n 2.4GHz | WiFi 802.11 b/g/n 2.4GHz |
| 摄像头 | 无 | 板载 OV2640（可选，与 CVBS/音频互斥） |
| RTC | 软件模拟 I2C（GPIO5/6） | 软件模拟 I2C（GPIO21/22） |
| 按键 | GPIO0/7/8 | GPIO0（GPIO33 为状态 LED） |

### 为什么以 ESP32-S3 为开发模板？

**ESP32-S3（首选板）的优势：**
- 8MB Octal PSRAM + 8/16MB Flash，资源充裕
- USB OTG 直接连接 USB 键盘鼠标
- 更新的 LX7 内核架构，GDMA 通用 DMA
- 1024x768 实验性高分辨率输出能力

**ESP32-CAM（兼容目标）的优势：**
- 内置 8-bit DAC（GPIO25/26），可直接输出 CVBS 视频和音频，无需外部芯片
- 板载 OV2640 摄像头，可选拍照功能（与 CVBS/音频互斥）
- 价格低廉，货源充足

> **结论**：驱动一律先按 ESP32-S3 模板编写，再向 ESP32-CAM 适配。
> ESP32-CAM 的内置 DAC 使其成为低成本 CVBS 方案的补充选择。

## 软件架构

```
+-------------------------------------+
|           LVGL 9.x 图形引擎           |
|     (Windows 3.2 风格复古桌面)        |
+-------------------------------------+
|         NuttShell (NSH) CLI          |
|        my_basic / Duktape         |
+-------------------------------------+
|    Apache NuttX RTOS (POSIX)         |
|  双核调度 / 文件系统 / 网络协议栈     |
+-------------------------------------+
|         ESP-IDF 驱动层               |
|   WiFi / SDIO/SPI / I2S/DAC / GPIO  |
+-------------------------------------+
```

## 目录结构

```
esp32-retro-ws/
+-- README.md              # 本文件
+-- CODING_STANDARD.md     # 编码规范
+-- SYSTEM.md              # 系统架构文档
+-- HARDWARE.md            # 硬件规格参考
+-- REQUIREMENTS.md        # 需求说明书
+-- COMPLETED.md           # 已完成需求清单
+-- NEXT_STEPS.md          # 下一步工作计划
+-- BUILD_FIXES.md         # 构建修复日志
+-- DEPENDENCIES.md        # 依赖说明
+-- AGENTS.md              # AI Agent 配置文件
+-- scripts/               # 构建脚本
|   +-- download_deps.sh   # 下载所有开源依赖（共享）
|   +-- setup_tools.sh     # 激活工具链环境（共享）
|   +-- verify.sh          # 项目验证（共享）
|   +-- convert_font.sh    # 字体转换（共享）
|   +-- setup_env.sh       # 系统依赖安装（共享）
|   +-- esp32s3/           # ESP32-S3 编译脚本
|   |   +-- build.sh
|   |   +-- nuttx_build.sh
|   +-- esp32cam/          # ESP32-CAM 编译脚本
|       +-- build.sh
|       +-- nuttx_build.sh
|   +-- esp32c3/           # ESP32-C3（合宙核心板）编译脚本
|       +-- build.sh
|       +-- nuttx_build.sh
+-- tools/                 # 工具和字体资源
|   +-- fonts/            # 字体文件
+-- configs/               # 配置文件
|   +-- nuttx-defconfig           # NuttX 内核配置
|   +-- nuttx-defconfig-combined  # 组合配置
|   +-- nuttx-minimal.defconfig   # 最小配置
|   +-- lv_conf.h                # LVGL 配置
+-- examples/              # 示例脚本程序
|   +-- hello.bas         # BASIC 示例
|   +-- hello.js          # JavaScript 示例
|   +-- hello.be          # Berry 示例
|   +-- hello.py          # CPython 示例（仅 S3）
|   +-- spiral.lgo        # Logo 海龟画图示例（jslogo）
+-- deps/                  # 第三方依赖（不纳入版本控制）
+-- src/                   # 源代码
    +-- nuttx/
    |   +-- common/         # 共享代码（启动菜单、脚本引擎、共享驱动）
    |   +-- esp32s3/        # ESP32-S3 目标
    |   |   +-- driver/     # CVBS I2S、音频 I2S、BLE HID、USB HID、FSK
    |   |   +-- board/      # ESP32-S3-DevKitC-1 板级
    |   |   +-- chip/       # ESP32-S3 寄存器定义
    |   |   +-- include/    # 覆盖头文件
    |   +-- esp32/          # ESP32-CAM 目标
    |       +-- driver/     # CVBS DAC、音频 DAC、BLE HID、FSK
    |       +-- board/      # ESP32-CAM AI-Thinker 板级
    |       +-- chip/       # ESP32 寄存器定义
    |       +-- include/    # 覆盖头文件
    +-- lvgl/              # LVGL 驱动和应用
    |   +-- app/           # GUI 应用程序
    |   |   +-- logo/      # Logo 海龟画图
    |   +-- assets/icons/  # 图标资源
    |   +-- audio/         # 音频解码
    |   +-- fonts/         # 字体
    |   +-- modules/       # 脚本模块适配
    +-- arch/xtensa/       # 架构相关代码
```

## 开源组件清单 / Open Source Components

| 组件 / Component | 版本 / Version | 用途 / Purpose | 许可证 / License |
|------|------|------|--------|
| Apache NuttX | 12.12.0 | RTOS 内核 / RTOS kernel | Apache 2.0 |
| LVGL | 9.5.0 | 图形引擎 / Graphics library | MIT |
| ESP-IDF | v5.5.4 | 乐鑫 ESP32 SDK | Apache 2.0 |
| littlefs | v2.5.1 | 文件系统 / Filesystem | BSD-3-Clause |
| SQLite | 3.45.1 | 数据库引擎 / Database engine | Public Domain |
| curl | 8.0+ | HTTP 客户端 / HTTP client | MIT |
| Duktape | 2.7.0 | JavaScript 引擎 / JS engine | MIT |
| my_basic | 1.0.0 | BASIC 解释器 / BASIC interpreter | MIT |
| Berry（可选） | nuttx-apps 固定 | 类 Python 轻量脚本 / Python-like scripting | MIT |
| CPython（可选，仅 S3） | nuttx-apps 固定 | 完整 Python 3 / Full Python 3 | PSF |
| jslogo（可选） | master | UCBLogo 子集海龟画图 / Turtle graphics | Apache 2.0 |
| Logo Turtle | 自研 | Logo 海龟画图解释器 | MIT |
| NotoSansSC | - | 中文字体 / Chinese font | OFL-1.1 |

## 示例程序 / Example Programs

项目自带示例程序，可直接在 NSH 中运行（五种脚本引擎均可配置编译嵌入）：

```bash
# BASIC 程序 (my_basic)
nsh> script run /path/to/hello.bas

# JavaScript 程序 (Duktape)
nsh> script run /path/to/hello.js

# Berry 程序（menuconfig 勾选 RETRO_SCRIPT_BERRY）
nsh> script run /path/to/hello.be

# Python 程序（仅 S3，勾选 RETRO_SCRIPT_PYTHON）
nsh> script run /path/to/hello.py

# Logo 海龟画图（勾选 RETRO_LOGO_JSLOGO，jslogo 库装入 SD 卡）
nsh> script run /path/to/spiral.lgo

# 查看引擎状态 / engine status
nsh> script status
```

示例文件位于 `examples/` 目录：
- `hello.bas` - my_basic 示例：变量、斐波那契、FOR/WHILE 循环、字符串、数组、INPUT 语句
- `hello.js` - JavaScript 示例：斐波那契、阶乘
- `hello.be` - Berry 示例：递归斐波那契、retro_ui 胶水层调用
- `hello.py` - CPython 示例：斐波那契、retro_ui 胶水层调用（仅 ESP32-S3）
- `spiral.lgo` - Logo 示例：TO 过程、REPEAT、递归（螺旋/圆花/五角星）

## 中文字体 / Chinese Font

项目使用 NotoSansSC 子集字体，支持 GBK 中文、ASCII、标点符号和常用表情。

### 字体转换工具

```bash
# 生成字体文件（需要 Node.js 和 npx）
./scripts/convert_font.sh

# 仅提取字符集（不生成字体）
./scripts/convert_font.sh --charset-only
```

## 脚本 UI 胶水层 / Script UI Glue Layer

脚本可以调用固件里预制的 LVGL UI 组件，无需重新编译固件：

```javascript
// JavaScript 示例
retro_ui.msgbox("标题", "内容");
var result = retro_ui.input("输入", "请输入:", 64);
var choice = retro_ui.list("选择", "选择一个:", ["选项1", "选项2"]);
retro_ui.status("处理中...");
retro_ui.progress(50, 100);
```

```basic
' BASIC 示例
CALL retro_ui_msgbox("标题", "内容")
CALL retro_ui_status("处理中")
```

## 测试

```bash
# ESP32-S3 编译
cd scripts/esp32s3 && ./build.sh nuttx

# ESP32-CAM 编译
cd scripts/esp32cam && ./build.sh nuttx

# ESP32-C3 编译
cd scripts/esp32c3 && ./build.sh nuttx
```

### 串口连接

```bash
# 查看串口
ls /dev/ttyUSB*

# 连接 NSH
minicom -D /dev/ttyUSB0 -b 115200
```

## 参考文档

- [NuttX 官方文档](https://cwiki.apache.org/confluence/display/NUTTX)
- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)
- [LVGL 文档](https://docs.lvgl.io/)

---

_最后更新: 2026-04-02_

## 2026-10-04（晚）全真硬件化升级

- **AV 视频输出真硬件化**：S3=LCD_CAM 并行口+GDMA、CAM=内置 DAC+整帧环、C3=I2S PDM sigma-delta、Pico=PIO+DMA 逐行（Core1 生成）——全部 DMA 流式，CPU 零忙等
- **真 GNU nano 8.4 移植**（deps/nano 上游源码 + mini-curses 垫片）：全系 CLI 文本编辑器统一 nano，vi 移除
- **NSH 跑 AV 电视屏**：/dev/cvbscon 字符终端（ANSI/CSI + 反色 + 光标）+ UART 键盘泵（C3/Pico）
- **retro_bus 总线兼容层**：I2C/SPI/UART（MicroPython machine 风格，硬件后端 + 位摆软回退，三引擎绑定）
- **板级脚本 ROM XIP**：firmware/scripts/<板>/ 打包 ROMFS，\`script hello\` 从 Flash 直跑（零 RAM 拷贝）
- **builtin 接线**：sysinfo/pkg/shell/script/nano 成为真实 NSH 命令（原先命令表是死代码）

五板产物 dist/firmware/：pico 1.40MB/2MB(68%)、c3 1.77MB/4MB(43%)、cam 2.13MB/4MB(52%)、s3/s3n8 2.07MB（16MB/8MB 13%/25%）；dram0 静态占用 16%~44%，全部达标。

## 2026-10-04（深夜）硬件全真外设收口 + EDA 载板

- **WS2812 状态灯真硬件化**：新增 `ws2812_rmt.c`（RMT 外设经 NuttX
  /dev/rmt0，替代点不亮的 gpio 直驱）；s3/s3n8 固件已开 `CONFIG_ESP_RMT`
- **FSK 磁带 TX 接通硬件**：`fsk_send() → audio_play_pcm()`（I2S/DAC DMA），
  drv_fsk.c 首次纳入构建（补 Kconfig `RETRO_FSK_BAUD`）
- **RTC/总线硬件 I2C**：s3/s3n8（I2C0 @GPIO5/6）与 cam（I2C0 @GPIO21/22）
  appconfig 开 `CONFIG_I2C_DRIVER` + 芯片 I2C0 → /dev/i2c0
- **GUI 启动崩溃修复**：`lv_port_disp_init()` 曾在帧缓冲分配前 memset
  空指针（cvbs_pipeline 模拟器捕获）
- **硬件档案修正**（网络核实）：合宙 C3 板载 LED=GPIO12/13 **高电平**
  点亮（原文档误写低电平）、Flash 占用=GPIO11(VDD_SPI)/14-17；Pico CVBS
  移脚 GP12-15（GP23=SMPS 脚会污染 3V3 基准）；CAM 补 GPIO17=PSRAM CLK；
  新增"板上指示灯引脚避让"原则（test_hw_profiles.c 契约测试钉死）
- **eda/ 五板立创EDA载板工程**：全插接件设计（模块+外围+电阻网络全走
  母排/FPC 座），内置两层自动布线器，同层异网零交叉校验 PASS——详见
  `eda/README.md`

五板固件复验：pico 1396KB/c3 1774KB/cam 2130KB/s3/s3n8 2069KB，全部 OK；
宿主测试套件 ALL PASS（含 ws2812 353 项 + 占用表契约 219 项 + 双变异门 ≥80%）。

## 2026-10-05 字号考据定档：GUI=12px / 控制台=16px

上网考证老系统界面字号后定档（详见 HARDWARE.md 6.4 字号表）：

- **GUI（640x480）默认 12px**（`lv_font_notosans_sc_12.c`，新增）——对齐
  中文 Windows 3.2/95 界面**宋体 9pt=12px 内置点阵**的传世标准；拉丁侧
  Win3.x MS Sans Serif 8pt@96DPI≈11px 同级。原 16px 相当于"大字体"模式，
  原默认 14px（LVGL montserrat）不符合中文老系统传统
- **控制台（320x240 240p）保持 16px**——恰为 FC/SFC 中文游戏 16x16
  点阵标准（cvbs_console 网格 CELL 16x18 即按此设计），不改
- 用户直觉的 7-9px 对应 DOS CGA 8x8 / VGA 9x16 的**拉丁等宽终端格子**；
  中文点阵历史下限即 12px，7-9px 中文在老系统上不存在
- 落地：`convert_font.sh` 两档齐生、`RETRO_FONT_DEFAULT` 按档自动选择
  （GUI 构建=12 / CLI=16）、`LV_FONT_DEFAULT`=montserrat_12；
  glm53f 多模态验收 12px 中文点阵清晰可辨（两种外壳截图 pass）

### 2026-10-05（同日修订）：收敛为全系唯一 12px 字号

早间方案（GUI=12 / 控制台=16 两档）当日即被推翻：**嵌入式体积优先，
全系 CLI/GUI 只保留一个字号档——12px**：

- `lv_font_notosans_sc_16.c` 废除删除；`convert_font.sh` 只生 12px 档
- `cvbs_console` 网格 16x18 → **12x14**（320x240→26 列x17 行；
  640x480→53 列x34 行，达到 DOS 80x25 量级信息密度）
- `RETRO_FONT_DEFAULT`/`RETRO_FONT_CONSOLE` 统一指向 12px；CLI 兼容层
  （lvgl_font_compat）同步；LVGL 默认字体 montserrat_12
- glm53f 多模态验收：640/240p 两档控制台 + Win3/wmaker 桌面全 pass
  （240p 的 12px 为可读下限，实机 CRT 抽验登记 NEXT_STEPS）
- 体积收益：字体 .o 从 911KB（16px）降到约 0.6MB（12px），五板 CLI 档
  固件全体缩小
