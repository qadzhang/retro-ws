# 复古工作站 Retro WS - 需求说明书 V4.4
# Retro Workstation (retro-ws) - Requirements Specification V4.4

> 本文档是中英文双语需求说明书。
> This document is a bilingual (Chinese/English) requirements specification.

> **多目标支持（五板）**：**ESP32-S3**（首选，构建目标 s3/s3n8）、**ESP32-CAM**、
> **合宙 ESP32-C3**（CLI 档）、**Raspberry Pi Pico**（CLI 档，无网络）五种硬件构建目标，
> 横跨 Xtensa / RISC-V / ARM 三架构。

## 1. 项目概述 / Project Overview

- **项目名称 / Project Name**: Retro WS 复古工作站（仓库 retro-ws，原 ESP32-S3 Retro WS）
- **硬件平台**:
  - **ESP32-S3 (DevKitC-1, N16R8/N8R8)【首选板 / 开发模板】**: 双核 LX7 240MHz, 8MB Octal PSRAM, 8/16MB Flash, USB OTG（构建目标 s3 / s3n8）
  - ESP32-CAM (AI-Thinker)【兼容目标】: ESP32 双核 240MHz, 4MB PSRAM, 4MB Flash, 内置 DAC, BLE HID
  - **合宙 ESP32-C3 核心板【低资源/低价格 CLI 档】**: RISC-V 单核 160MHz, 400KB SRAM（无 PSRAM）, 4MB Flash；经典款（CH343 串口）/ 简约款（原生 USB）均支持；**纯 CLI 工作站**（无 LVGL 桌面，**AV 视频字符控制台全系标配**），软件经 .rpk 包安装
  - **Raspberry Pi Pico【最低成本本地教学终端】**: RP2040 双核 Cortex-M0+ @133MHz, 264KB SRAM, 2MB Flash；无网络；纯 CLI（AV 视频走 PIO+DMA）
- **操作系统**: NuttX RTOS 12.12.0
- **图形库**: LVGL v9.5.0（仅 esp32s3 / esp32cam 图形档）
- **用户界面**: Windows 3.2 经典风格（C3/Pico 为 AV 字符控制台 cvbs_console）
- **开发语言**: C (NuttX/驱动), Python (工具)
- **开发策略**: 驱动先按 ESP32-S3 模板编写，再向 ESP32-CAM / ESP32-C3 / Pico 适配；每种开发板对应一个硬件档案文件（`hw_<板名>.h`）
- **包架构隔离**: .rpk 包 Arch 字段取值 all/xtensa/riscv/esp32/esp32s3/esp32c3（Pico 为 ARM 目标，同理隔离），Xtensa / RISC-V / ARM 二进制互不通用（pkg_manager 安装前校验）

## 2. 功能需求 / Functional Requirements

### 2.1 图形用户界面 / Graphical User Interface

#### 2.1.1 桌面系统 / Desktop System
- [x] Windows 3.2 风格桌面 (青色背景)
- [x] 任务栏 (Start 按钮 + 时钟 + 任务按钮)
- [x] Start 菜单 (程序/附件/游戏/运行/关机)
- [x] Program Manager (程序管理器)
- [x] 桌面图标 (我的电脑、网上邻居)
- [x] 图标网格布局
- [x] 窗口拖动/最小化/最大化/关闭
- [x] 3D 凹陷/凸起边框效果
- [x] **WindowMaker/NeXT 风格外壳（2026-10-04 新增，可切换）**
  - 右侧竖排 Dock 栏（64px 凹陷槽位 + 时钟槽）
  - 桌面应用图标（双击启动）
  - NeXT 式根菜单（点击桌面空白处弹出，黑底白字标题条）
  - 编译期默认外壳由 Kconfig `RETRO_DESKTOP_SHELL_{WIN3,WMAKER}` 选择
  - 运行时 NSH 切换：`shell win3` / `shell wmaker`
  - 实现：`src/lvgl/app/wmaker_shell.c`（纯 LVGL 控件，经
    `desktop_api.h` 的 `retro_desktop_app_launch()` 复用窗口工厂）

#### 2.1.2 应用程序 / Applications
- [x] **记事本 (Notepad)**
  - 多标签文本编辑
  - 行号显示
  - 搜索替换
  - 文件打开/保存
  - 菜单栏 (File/Edit/Search/View/Help)
  - 状态栏 (行号/列号/字符数)
  - **支持拼音输入法**

- [x] **浏览器 (Browser)**
  - URL 栏输入
  - 前进/后退/刷新/主页 按钮
  - HTML 简易渲染 (纯文本+链接)
  - 书签功能
  - 状态栏显示加载进度
  - **支持拼音输入法（地址栏）**

- [x] **终端 (Terminal)**
  - 命令输入行
  - 滚动输出区域
  - 命令历史
  - 16 个内置命令 (help, clear, echo, date, ps, etc.)
  - ANSI 彩色输出
  - NSH 命令集成接口
  - **CLI 拼音输入法**

- [x] **媒体播放器 (Media Player)**
  - GUI 图形界面 + CLI 命令行双模式
  - 播放/暂停/停止/进度控制
  - WAV 文件播放（DAC/I2S）
  - 音量控制
  - 文件选择器
  - 显示当前文件名、时长、状态
  - **支持 MP3 扩展（需要 minimp3 库）**

- [x] **录音机 (Recorder)**
  - GUI 图形界面 + CLI 命令行双模式
  - 录音/停止/播放按钮
  - 录音时长显示
  - 波形显示（简化条形图）
  - 保存文件选择（WAV 格式）

- [x] **扫雷 (Minesweeper)**
  - 9x9 网格
  - 地雷计数
  - 计时器
  - 左右键支持

- [x] **文件管理器 (File Manager)**
  - 驱动器选择 (C:/D:/E/)
  - 文件列表显示
  - 目录浏览

- [x] **控制面板 (Control Panel)**
  - 显示/鼠标/键盘/打印机/字体/声音/日期时间/网络/关于

#### 2.1.3 输入法 / Input Method
- [x] **中文拼音输入法**
  - 全拼输入
  - 简拼支持 (zs=张爽)
  - 候选词选择 (数字键 1-9)
  - 3000+ 常用汉字词库
  - 中英文切换
  - 与 LVGL textarea 集成

- [x] **CLI 拼音输入法**
  - 行内编辑模式
  - Backspace/Enter/Esc 支持
  - 词组联想

- [x] **输入法全环境可用（2026-10-04 需求）**
  - **GUI 模式**：输入法为系统级服务，文本框获得焦点时自动唤起候选条
  - **纯 CLI 模式（含安全模式 CLI Only）**：NSH 提供 `ime on|off|status`
    命令显式启动/关闭行内拼音输入法，`ime on` 后终端进入中文输入状态
  - 实现基座：`drv_pinyin.c`（行内 IME）+ `app_pinyin.c`（GUI IME）

### 2.2 系统功能 / System Functions

#### 2.2.1 显示驱动 / Display Driver
- [x] CVBS 复合视频输出（**全系标配，含 CLI 档**）
- **ESP32-S3**: LCD_CAM I80 并行口 + GDMA -> GPIO2/15/16/17 -> 4-bit R-2R -> CVBS
- **ESP32-CAM**: I2S0 -> 内置 DAC1 (GPIO25) -> 电阻网络 -> CVBS
- **ESP32-C3**: I2S0 PDM-TX 单脚 (GPIO1) + RC 滤波 -> CVBS（sigma-delta）
- **Pico**: PIO SM0 + DMA 逐行 -> GP12-15 4-bit R-2R -> CVBS（Core1 生成）
- [x] 8-bit 调色板模式 (256 色)
- [x] ITU-R BT.601 标清
- [x] 分辨率三档（2026-10-04 确定）:
  - **320x240 控制台模式**（240p 逐行，CLI/安全模式）
  - **640x480 常规模式**（480i 隔行，GUI 默认）
  - **1024x768 最高模式**（实验性 overspec，仅 ESP32-S3；ESP32-CAM 上限 640x480；C3/Pico 定档 320x240 字符控制台）
- [x] 帧率: 30Hz

#### 2.2.2 音频驱动 / Audio Driver
- **ESP32-S3**: I2S 输出 (GPIO40=WS / GPIO41=SCK / GPIO42=SDO) -> 外部 DAC 芯片（如 MAX98357A）
- **ESP32-S3**: ADC 输入 (GPIO1) -> 模拟麦克风
- **ESP32-CAM**: DAC 输出 (GPIO26) -> 内置 DAC -> 功放 -> 扬声器
- **ESP32-CAM**: ADC 输入 (GPIO34) -> 模拟麦克风
- **ESP32-C3 / Pico**: 无音频输出（CLI 档不配音频外设）
- [x] 44100Hz 采样率
- [x] WAV 播放支持

#### 2.2.3 输入设备 / Input Devices

**输入优先级原则（2026-10-05 定稿）**：**USB 键盘（OTG 主机）> 蓝牙 HID > 串口键盘泵**；
芯片具备哪种能力就必须支持哪种，三者皆备的板（S3）三种都要支持。

> 注意"有 USB"指 **USB 主机（OTG host）** 能力，不是有无 USB 口：
> - **ESP32-S3**：有 OTG 主机 —— USB HID **已实现**（usb_hid.c）；另有 BLE HID
>   （ble_hid.c 已写、封存待 NimBLE 解封，NEXT_STEPS 36）+ UART 控制台，三路齐备。
>   2026-10-05 落地 AV 控制台键流桥：common hid_ascii.c（HID 键码→ASCII 纯函数，
>   按下沿差分）+ cvbs_console_feed_keys()，USB 键盘可直打 AV 屏终端
> - **ESP32-CAM**：芯片无 USB 外设 —— 走 **BLE HID（已实现）** + 串口
> - **ESP32-C3**：芯片 USB 为**设备模式**（内置 USB-Serial-JTAG，仅烧录/调试），
>   **硬件上接不了普通 USB 键盘**；芯片有 BLE 5 —— 按原则应走 **BLE HID
>   （待 NimBLE 移植，NEXT_STEPS 36，HID→ASCII 复用 hid_ascii.c）**；现有实现为
>   UART 键盘泵（avkbin，cvbs_console 内嵌，CH343 串口/原生 USB-CDC ->
>   /dev/cvbscon），BLE 就绪后与之并存
> - **Pico**：**无蓝牙射频**、USB 块仅设备模式（NuttX 树内也只有 rp2040_usbdev
>   设备驱动）—— 按"没有 USB 和蓝牙才走串口"，**UART 键盘泵即正确且唯一路线**
>   （UART0/USB-CDC；PIO-USB 软件主机属远期评估，NuttX 无现成驱动）

- **蓝牙状态 LED**: ESP32-S3 (WS2812 RGB，v1.1=GPIO38 / v1.0=GPIO48), ESP32-CAM (GPIO4 Flash LED)

#### 2.2.4 网络功能 / Network Functions
- [x] WiFi 连接
- [x] **网络配置文件 /opt/etc/network.conf**（2026-10-05，片上可写区）：
  ssid/password/ip_mode(dhcp|static)/ip/netmask/gateway/dns；SD 卡同名
  文件作搬运回退；开机 wifi_auto_connect 自动连接
- [x] `wifi` NSH 命令（status/connect；connect 即存片上，static 需 IP 组）
- [x] NTP 时间同步
- [x] TCP/UDP 协议栈
- [x] 网络配置接口

#### 2.2.5 FSK 磁带调制解调
- [x] Kansas City Standard (KCS) 标准
- [x] 1200/2400 Hz 双频（KCS 标准）
- [x] 300 波特率（KCS 标准）
- [x] 数据包封装

#### 2.2.6 定时任务
- [x] Cron 定时任务
- [x] 闹钟提醒
- [x] crontab 存片上 `/opt/etc/crontab`（无 SD 卡可用，2026-10-05 定稿）
- [x] 日志默认只串口输出不落盘（开发板哲学；reboot.log 重启计数例外）

#### 2.2.7 SD 卡存储
- **ESP32-S3**: SPI 模式 (GPIO10=CS / GPIO11=MISO / GPIO13=MOSI / GPIO14=CLK)
- **ESP32-CAM**: SPI 模式 (GPIO13=CS / GPIO15=MOSI / GPIO2=MISO / GPIO14=CLK)
- [x] FAT32 文件系统，最大 32GB

### 2.3 多语种支持 / Internationalization

- [x] 多语种框架 (i18n)
- [x] 支持语言:
  - zh_CN (简体中文) - 默认
  - en_US (English)
- [x] **界面字符串默认中文**：所有菜单、标签、提示信息默认中文
- [x] 界面字符串全部使用 i18n_get()
- [x] 启动时**从存储读取**语言设置（优先级：SD卡 -> /etc -> 编译默认）
- [x] 语言切换**自动保存**到存储
- [x] 语言可在**控制面板 -> Language** 图形界面切换
- [x] CLI 命令 `setlang zh_CN|en_US` 切换

#### 持久化存储 / Persistent Storage

**读取优先级** (首个找到的生效 / First found wins):

| 优先级 | 路径 | 说明 |
|--------|------|------|
| 1 | `/opt/etc/lang.conf` | 片上系统配置（无 SD 卡可用，2026-10-05 定稿） |
| 2 | `/mnt/sd0/lang.conf` | SD卡 |
| 3 | `/mnt/spiffs0/lang.conf` | SPIFFS 分区 (片上存储) |
| 4 | `/mnt/data/lang.conf` | 数据分区 (片上存储) |
| 5 | `/flash/lang.conf` | Flash 文件系统 |
| 6 | `/etc/lang.conf` | 系统配置目录 |
| 7 | (编译默认) | CONFIG_LANG |

### 2.4 图标资源 / Icon Resources

- [x] 32x32 PNG 图标
- [x] 风格: Windows 3.2 复古像素风
- [x] 图标列表:
  - icon_notepad.png - 记事本
  - icon_browser.png - 浏览器
  - icon_terminal.png - 终端
  - icon_minesweeper.png - 扫雷
  - icon_folder.png - 文件夹
  - icon_control_panel.png - 控制面板
  - icon_pc.png - 我的电脑

### 2.5 动态程序加载 / Dynamic Program Loading

- [x] ELF 动态加载支持 (NuttX binfmt)
- [x] 配置文件: `CONFIG_ELF=y`, `CONFIG_BINFMT_LOADABLE=y`
- [x] SD 卡/TF 卡存储 `.elf` 程序
- [x] 执行命令: `nsh> /path/to/program.elf`
- [x] 程序退出后内存完全释放
- [x] 支持多程序并发（需要手动创建任务）

#### 示例程序 / Example Programs

| 文件 | 语言 | 说明 |
|------|------|------|
| `examples/hello.bas` | BASIC | 斐波那契、猜数字、循环演示 |
| `examples/hello.js` | JavaScript | 斐波那契、阶乘、对象演示 |

### 2.6 脚本引擎与 UI 胶水层 / Script Engines & UI Glue Layer

> **所有内嵌脚本语言一律可配置编译嵌入**（Kconfig `RETRO_SCRIPTS` 下独立勾选，
> 未选中的引擎不参与编译、不占资源）：

| 引擎 | 扩展名 | Kconfig | 适用目标 | 说明 |
|------|--------|---------|----------|------|
| my-basic | .bas | `RETRO_SCRIPT_TINYBASIC` | 全系五板 | BASIC 解释器 |
| Duktape | .js | `RETRO_SCRIPT_DUKTAPE` | s3 / s3n8 / cam / c3 | JavaScript ES5（Pico 档未编） |
| Berry | .be | `RETRO_SCRIPT_BERRY` | 全系五板 | 类 Python 轻量语言（<40KB ROM），nuttx-apps 集成 |
| CPython | .py | `RETRO_SCRIPT_PYTHON` | **仅 S3 N16R8** | 完整 Python 3，nuttx-apps interpreters/python |
| jslogo | .lgo | `RETRO_LOGO_JSLOGO` | 图形档 s3 / cam | UCBLogo 子集（Apache-2.0），跑在 Duktape 上 |

- [x] 五引擎按需加载切换（`script engine basic|js|berry|py|logo`）
- [x] 按扩展名自动选择引擎（`script run xxx.{bas,js,be,py,lgo}`）
- [x] retro_ui 统一接口 (C 核心)
- [x] Duktape JS 模块 (`retro_ui.*`)
- [x] my_basic 接口 (`CALL retro_ui_*`)
- [x] Berry 全局函数（`retro_ui_msgbox(...)` 等）
- [x] CPython 内建模块（`import retro_ui`）
- [x] jslogo 海龟绘图走 LVGL 后端（logo_draw.c 回调）
- [x] **i18n 多语种支持**（按钮标签/对话框自动翻译）

#### 脚本 GPIO 接口（2026-10-04，教学定位）

- [x] retro_gpio 统一接口（`AGENTS.md` 8.1 扩展）：config/write/read/adc/pwm/release
- [x] 四引擎绑定：BASIC（`CALL retro_gpio_*`）/ JS（`retro_gpio.*`）/
      Berry（全局函数）/ Python（`import retro_gpio`）
- [x] **系统占用引脚拦截**：各板硬件档案提供占用表（含原因），
      第三方脚本访问时直接报"GPIOx 已被系统占用: <原因>"并返回 -EBUSY
- [x] 后端走 NuttX 通用设备驱动（/dev/gpioN、/dev/adc0、/dev/pwmN）
- [x] 教学可用脚（表外引脚）：S3 推荐 4/7/8/9/12/18/21/33/34/39/47；
      C3 推荐 GPIO1/GPIO10；CAM 无富余教学脚（文档已注明）

#### 示例程序 / Example Programs

| 文件 | 语言 | 说明 |
|------|------|------|
| `examples/hello.bas` | BASIC | 斐波那契、猜数字、循环演示 |
| `examples/hello.js` | JavaScript | 斐波那契、阶乘、对象演示 |
| `examples/hello.be` | Berry | 递归斐波那契、retro_ui 演示 |
| `examples/hello.py` | Python | 斐波那契、retro_ui 演示（仅 S3） |
| `examples/spiral.lgo` | Logo | 螺旋/圆花/五角星海龟画图（需 jslogo） |

#### GPL 独立程序包与包管理器（2026-10-04）

- [x] **GPL 组件禁止编入固件 ROM**（AGENTS.md 11.1）：以独立 ELF 交付
- [x] **deb 风格包管理器**（参考 Debian）：
  - 包格式 `.rpk` = USTAR 容器：`control`（元数据）/ `manifest`（CRC32 清单）/
    `preinst|postinst|prerm|postrm`（NSH 维护脚本）/ `data/`（载荷树）
  - 数据库仿 `/var/lib/dpkg`：`/opt/var/lib/rpkg/`（片上可写分区，无 SD 卡可用）
    （`<包名>.control` 快照 + `manifest/<包名>` + `info/*` 脚本存档）
  - **双安装根（2026-10-05 定稿，HARDWARE 12.4）**：官方系统包 control 声明
    `Root: system` → 装片上 `/opt`（`/opt/bin`、`/opt/share/<包>`，无需 SD 卡）；
    第三方包缺省（或显式 `Root: sdcard`）→ `/sdcard`；非法 Root 取值安装拒绝；
    SD 卡为可选硬件，系统包管理不依赖它
  - 设备端 512 字节流式解析（大文件不整包入内存）、CRC32 校验、
    路径穿越防护（拒绝 `..`）、Arch 字段目标检查、Depends 依赖检查
  - NSH 命令：`pkg install/remove/list/info`
- [x] 主机侧打包器 `scripts/make_package.sh`（包源目录 → .rpk，含 manifest 生成）
- [x] `scripts/build_packages.sh`：源码下载 → LOADABLE 构建 → .rpk 打包 →
      `dist/sdcard/pkg/`（GPL 源码副本随包提供，满足分发义务）
- [x] `scripts/build_all.sh`：固件 + 安装包一键构建（`--no-packages` 跳过）
- [x] `apps-extra/ucblogo/package/`：UCBLogo 包模板（control + postinst）
- 许可证边界：mere aggregation（单纯聚合），固件保持零 GPL

#### 胶水层 API

```c
// C 核心接口 / C Core API
int retro_ui_msgbox(const char *title, const char *msg);
int retro_ui_input(const char *title, const char *prompt, char *buf, int bufsize);
int retro_ui_list(const char *title, const char *prompt, const char **items, int count);
int retro_ui_confirm(const char *title, const char *msg);
int retro_ui_status(const char *msg);
int retro_ui_progress(int value, int max);
int retro_ui_set_lang(const char *lang);   // 设置语言: "zh_CN" 或 "en_US"
const char *retro_ui_get_lang(void);       // 获取当前语言
```

### 2.7 SQLite 数据库工具 / SQLite Database Tool

- [x] SQLite C 核心库 (nuttx-apps/database/sqlite)
- [x] GUI 图形界面 (`app_sqlite.c`)
- [x] CLI 命令行 (`drv_sqlite.c`)
- [x] 支持 SELECT/INSERT/UPDATE/DELETE
- [x] 支持 .tables / .schema 等元命令
- [x] 查询结果表格显示
- [x] **导出为 CSV 文件**
- [x] **导出为新 SQLite 数据库**

## 3. 非功能需求 / Non-Functional Requirements

### 3.1 性能 / Performance
- LVGL 刷新率: 30-60 fps
- 终端响应: < 100ms
- 拼音输入延迟: < 50ms

### 3.2 资源限制 / Resource Constraints

| 资源 | ESP32-S3 | ESP32-CAM | ESP32-C3 | Pico |
|------|----------|-----------|----------|------|
| PSRAM | 8MB (OSPI) | 4MB (QSPI) | 无 | 无 |
| SRAM | 512KB | 520KB (396KB 可用) | 400KB | 264KB |
| Flash | 16/8MB | 4MB (QSPI) | 4MB | 2MB |
| CPU | Xtensa LX7 双核 @ 240MHz | Xtensa LX6 双核 @ 240MHz | RISC-V 单核 @ 160MHz | Cortex-M0+ 双核 @ 133MHz |
| LVGL 堆 | 128KB | 128KB（优化后） | 无 LVGL | 无 LVGL |

### 3.3 代码规范 / Coding Standards
- 所有注释: **中英双语**
- 缩进: 4 空格
- 编码: UTF-8
- 遵循 NuttX Coding Standards

## 4. 技术栈 / Technology Stack

| 组件 / Component | 版本 / Version | 许可证 / License | 用途 / Purpose |
|-------------------|---------------|------------------|----------------|
| NuttX | 12.12.0 | Apache 2.0 | RTOS 内核（含 RP2040 支持，Pico 免外部 SDK） |
| LVGL | 9.5.0 | MIT | 图形库（仅图形档） |
| ESP-IDF | v5.5.4 | Apache 2.0 | 乐鑫 HAL（S3/CAM/C3） |
| esp-hal-3rdparty | NuttX 配套 | Apache 2.0 | 乐鑫 HAL 装配（mbedtls/WiFi/BLE/PHY） |
| littlefs | v2.5.1 | BSD-3-Clause | 文件系统 |
| SQLite | 3.45.1 | Public Domain | 数据库（nuttx-apps） |
| curl | 8.x | MIT | HTTP 客户端（nuttx-apps） |
| Duktape | v2.7.0 | MIT | JavaScript 引擎 |
| my_basic | 1.0.0 | MIT | BASIC 脚本引擎 |
| Berry | nuttx-apps 固定版本 | MIT | 类 Python 轻量脚本引擎（可选） |
| CPython | nuttx-apps 固定版本 | PSF | 完整 Python 3（可选，仅 S3） |
| jslogo | master | Apache 2.0 | UCBLogo 子集海龟画图（可选） |
| **GNU nano** | **8.4** | **GPL-3.0** | **CLI 编辑器（.rpk 独立包交付，不入固件 ROM；系统默认 vi——2026-10-05 定稿）** |
| NotoSansSC | - | OFL-1.1 | 12px 中文字库点阵源 |

## 5. 文件结构 / File Structure

```
retro-ws/                            # 项目根目录（任意位置克隆均可）
+-- README.md                    # 项目说明
+-- CODING_STANDARD.md          # 编码规范
+-- REQUIREMENTS.md             # 需求说明书 (本文档)
+-- COMPLETED.md                # 已完成需求清单
+-- NEXT_STEPS.md               # 下一步工作计划
+-- SYSTEM.md                   # 系统架构文档
+-- HARDWARE.md                 # 硬件规格参考
+-- DEPENDENCIES.md             # 依赖说明
+-- BUILD_FIXES.md              # 构建修复日志
+-- AGENTS.md                   # AI Agent 配置
+-- docs/                       # 文档目录
+-- configs/                    # 配置文件
|   +-- nuttx-defconfig         # NuttX 内核配置
|   +-- nuttx-defconfig-combined # 组合配置
|   +-- nuttx-minimal.defconfig  # 最小配置
|   +-- lv_conf.h              # LVGL v9.x 配置
+-- scripts/                    # 构建脚本
|   +-- download_deps.sh        # 下载依赖（共享）
|   +-- setup_tools.sh          # 激活工具链（共享）
|   +-- verify.sh               # 项目验证（共享）
|   +-- convert_font.sh         # 字体转换（共享）
|   +-- setup_env.sh            # 系统依赖安装（共享）
|   +-- firmware/               # 五板统一构建入口（build_firmware.sh）
|   +-- esp32s3/                # ESP32-S3 编译脚本（旧入口）
|   |   +-- build.sh
|   |   +-- nuttx_build.sh
|   +-- esp32cam/               # ESP32-CAM 编译脚本（旧入口）
|   |   +-- build.sh
|   |   +-- nuttx_build.sh
|   +-- esp32c3/                # ESP32-C3 编译脚本（旧入口）
|       +-- build.sh
|       +-- nuttx_build.sh
+-- examples/                   # 示例脚本
|   +-- hello.bas               # BASIC 示例
|   +-- hello.py                # Python 示例
|   +-- hello.js                # JavaScript 示例
+-- tools/fonts/                # 字体资源
+-- src/                        # 源代码
|   +-- nuttx/
|   |   +-- common/             # 共享代码（14个文件）
|   |   |   +-- bootmenu.c      # 启动菜单
|   |   |   +-- script_engines.c # 脚本引擎集成
|   |   |   +-- network_utils.c # curl/wget
|   |   |   +-- apps/system/    # NSH 命令
|   |   |   +-- driver/         # 共享驱动（网络/防火墙/NTP/Cron/RTC等）
|   |   +-- esp32s3/            # ESP32-S3 目标
|   |   |   +-- esp32s3_retro.c # 双核主入口
|   |   |   +-- driver/         # ESP32-S3 专用驱动
|   |   |   |   +-- cvbs/drv_cvbs.c      # LCD_CAM CVBS
|   |   |   |   +-- audio/drv_audio.c    # I2S 音频
|   |   |   |   +-- ble_hid.c            # BLE HID (NimBLE)
|   |   |   |   +-- ble_hid.h            # BLE HID 头文件
|   |   |   |   +-- usb_hid.c            # USB HID
|   |   |   |   +-- fsk/drv_fsk.c        # FSK
|   |   |   +-- board/          # ESP32-S3 板级
|   |   |   +-- chip/           # ESP32-S3 芯片定义
|   |   |   +-- include/        # 覆盖头文件
|   |   +-- esp32/              # ESP32-CAM 目标
|   |   |   +-- esp32_retro.c   # 双核主入口
|   |   |   +-- driver/         # ESP32-CAM 专用驱动
|   |   |   |   +-- cvbs/drv_cvbs_dac.c  # DAC CVBS
|   |   |   |   +-- audio/drv_audio_dac.c # DAC 音频
|   |   |   |   +-- ble_hid.c             # BLE HID (NimBLE)
|   |   |   |   +-- ble_hid.h             # BLE HID 头文件
|   |   |   |   +-- ble_storage.c         # BLE Bond 存储
|   |   |   |   +-- ble_nsh.c             # BLE NSH 命令
|   |   |   |   +-- ble_pair_ui.c         # BLE 配对 UI
|   |   |   |   +-- fsk/drv_fsk.c         # FSK
|   |   |   +-- board/          # ESP32-CAM 板级
|   |   |   +-- chip/           # ESP32 芯片定义
|   |   |   +-- include/        # 覆盖头文件
|   |   +-- esp32c3/            # 合宙 ESP32-C3 目标（CLI，RISC-V）
|   |   |   +-- esp32c3_retro.c # 单核主入口
|   |   |   +-- driver/cvbs/    # PDM CVBS（drv_cvbs_pdm.c）
|   |   |   +-- board/          # 合宙核心板板级（hw_esp32c3_luatos.h）
|   |   +-- rp2040/             # Raspberry Pi Pico 目标（CLI，ARM）
|   |       +-- rp2040_retro.c  # 双核主入口（Core0=程序/Core1=媒体）
|   |       +-- driver/cvbs/    # PIO CVBS（drv_cvbs_pio.c）
|   |       +-- board/          # Pico 板级（hw_rp2040_pico.h）
|   +-- lvgl/                   # LVGL 驱动和应用
|   |   +-- i18n.[c|h]          # 多语种框架
|   |   +-- retro_ui.c          # UI 胶水层
|   |   +-- retro_win3_styles.h # Win3.2 样式常量
|   |   +-- lvgl_app.c          # LVGL 初始化
|   |   +-- lv_port_disp.c      # 显示端口
|   |   +-- lv_port_indev.c     # 输入设备端口
|   |   +-- app/                # GUI 应用程序
|   |   +-- modules/            # 脚本模块适配
|   |   +-- audio/              # 音频解码
|   |   +-- fonts/              # 字体
|   |   +-- assets/icons/       # 图标资源
|   +-- arch/xtensa/            # 架构相关代码
+-- deps/                       # 第三方依赖（不纳入版本控制）
```

## 6. 编译说明 / Build Instructions

```bash
# 1. 下载依赖 / Download dependencies —— 进入克隆出的项目目录
cd retro-ws
./scripts/download_deps.sh

# 2. 激活工具链环境（Xtensa + RISC-V + ARM 三套）
source scripts/setup_tools.sh

# 3. 五板固件统一构建（产物 dist/firmware/<板>/）
./scripts/build_firmware.sh all          # 或 s3 / s3n8 / cam / c3 / pico

# 4. 烧录（ESP32 系 esptool / C3 从 0x0 / Pico UF2，详见 AGENTS.md 9.2）

# 旧三板单独入口（s3 / cam / c3）
cd scripts/esp32s3 && ./build.sh nuttx && ./build.sh flash
```

## 7. 待完成事项 / TODO

- [ ] QEMU 模拟验证（仅 S3）
- [ ] 开发板烧录 + 实机测试（五板固件均已编译通过，唯一未开始项）
- [ ] CPython 编译验证（仅 S3 N16R8，ROMFS 标准库镜像）
- [x] Berry 编译验证（2026-10-04 五板默认入 ROM）
- [x] CLI 模式 `ime` 命令接入（2026-10-05，CCDOS 式输入条）
- [ ] GUI 输入法系统服务化 + 词组整词上屏
- [ ] SSH 客户端 libssh2 集成
- [ ] HTTPS / 断点续传（curl/wget 完善）

---

**版本历史 / Version History**:
- V4.4: 项目更名 retro-ws；五板多架构定位（S3/S3N8/CAM/C3/Pico）；修正 C3 "无 CVBS" 误载（AV 输出全系标配）；资源表/文件结构/编译说明补 C3 与 Pico
- V4.3: 五种脚本引擎全部可配置编译（新增 Berry/CPython/jslogo）；输入法全环境需求（GUI 系统级服务 + CLI `ime` 命令）
- V4.2: 首选板定为 ESP32-S3 N16R8/N8R8（开发模板），CAM 为兼容目标；每板新增硬件档案文件；修正 GPIO 错误；分辨率定档 320x240/640x480/1024x768
- V4.1: 完成 BLE HID 驱动（NimBLE）、Bond 存储、NSH 命令、配对 UI
- V4.0: 多目标支持（ESP32-S3 + ESP32-CAM），目录结构重构，双目标编译脚本
- V3.5: 添加硬件方案变更说明（ESP32-CAM 适配）
- V3.3: 添加拼音输入法、多语种支持、图标资源
- V3.2: 添加终端、浏览器、记事本应用
- V3.1: 基础桌面系统、窗口管理、任务栏

**最后更新 / Last Updated**: 2026-10-05
