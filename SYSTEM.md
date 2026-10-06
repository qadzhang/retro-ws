/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * 复古工作站 Retro WS - 系统说明
 * Retro Workstation (retro-ws) - System Documentation
 */

## 文档信息 / Document Info

| 项目 | 内容 |
|------|------|
| 版本 | v0.3.1 |
| 目标芯片 | **ESP32-S3** (LX7) / **ESP32** (CAM, LX6) / **ESP32-C3** (RISC-V, 合宙核心板) / **RP2040** (Pico, Cortex-M0+) |
| 操作系统 | Apache NuttX RTOS 12.12.0 |
| 图形引擎 | LVGL 9.5.0（仅图形档 S3/CAM） |
| CLI 编辑器 | NuttX vi（系统默认）；GNU nano 8.4 为 .rpk 可选安装包（GPL-3.0 不入 ROM，2026-10-05 定稿） |
| 文本浏览器 | Links 2.30（下载脚本已支持；板端移植待办，见 NEXT_STEPS 48） |
| 状态 | **五板多目标支持已完成；应用/系统分离（ROM 包存储 + XIP 模块，2026-10-06）** |

### 多目标支持

> 开发模板为 **ESP32-S3 (N16R8/N8R8 首选板)**，ESP32-CAM 为兼容目标，
> **合宙 ESP32-C3 核心板为低资源 CLI 档（RISC-V）**，
> **Raspberry Pi Pico 为最低成本本地教学终端（RP2040，CLI，无网络）**。
> 每板对应一个硬件档案文件：`src/nuttx/esp32s3/board/hw_esp32s3_devkitc.h`、
> `src/nuttx/esp32/board/hw_esp32cam_aithinker.h`、
> `src/nuttx/esp32c3/board/hw_esp32c3_luatos.h`、
> `src/nuttx/rp2040/board/hw_rp2040_pico.h`（引脚唯一事实来源）。

| 目标 | 架构 | 芯片 | Flash | PSRAM | 显示 | 定位 |
|------|------|------|-------|-------|------|------|
| s3 / s3n8 | Xtensa LX7 | ESP32-S3 (N16R8/N8R8) | 16/8MB | 8MB (Octal) | CVBS 320x240/640x480/1024x768(实验) | 首选板/开发模板，统一构建 `./scripts/build_firmware.sh s3` |
| cam | Xtensa LX6 | ESP32 (CAM) | 4MB | 4MB (QSPI) | CVBS 320x240/640x480 | 兼容目标，`./scripts/build_firmware.sh cam` |
| c3 | **RISC-V RV32IMC** | ESP32-C3 (合宙) | 4MB | 无 | AV 字符控制台 320x240 | 低资源/低价格 CLI 档，`./scripts/build_firmware.sh c3` |
| pico | **ARM Cortex-M0+** | RP2040 (Pico) | 2MB | 无（264KB SRAM） | AV 字符控制台 320x240 | 最低成本本地教学终端，`./scripts/build_firmware.sh pico` |

---

## 系统架构

### 双核分工（全局规范：CPU0=程序核 / CPU1=媒体核）

> 2026-10-04 定稿并落地到代码（sched_setaffinity 钉核）；
> C3 单核无分工，Pico 双核同此规范（Core1 兼顾视频逐行生成与文件 IO）。

```
+------------------------------------------------------+
|            S3 / CAM / Pico 双核板                     |
|                                                      |
|  +------------------+    +------------------------+  |
|  |  CPU0 程序核      |    |  CPU1 媒体核            |  |
|  |                  |    |                        |  |
|  |  * NuttShell     |    |  * LVGL 图形引擎        |  |
|  |  * 脚本引擎       |    |  * CVBS 显示驱动        |  |
|  |  * WiFi/TCP/IP   |    |  * 音频播放/FSK 磁带     |  |
|  |  * NTP / Cron    |    |  * SD 卡文件 IO          |  |
|  |  * 看门狗 (WDT0)  |    |  * 看门狗 (WDT1)         |  |
|  +------------------+    +------------------------+  |
+------------------------------------------------------+
```

### ESP32-S3 内存布局（区域示意，精确地址见 ESP32-S3 TRM）

```
PSRAM 8MB (Octal)   图形帧缓冲 / LVGL 堆 / 字库缓存
SRAM 512KB          内核 / 栈 / 全局变量 / DMA 描述符
Flash 8/16MB        代码 + 只读数据（含 ROMFS 板级脚本）
```

### ESP32-CAM 内存布局（区域示意）

```
SRAM 520KB (396KB 可用)  内核 / 栈 / 全局变量
PSRAM 4MB (QSPI)         图形帧缓冲 / LVGL 堆 / 字库缓存
Flash 4MB                代码 + 只读数据
```

---

## 源代码结构

```
retro-ws/                        # 项目根目录（任意位置克隆均可）
+-- README.md
+-- SYSTEM.md                    # 本文件
+-- CODING_STANDARD.md           # 编码规范
+-- .gitignore
|
+-- scripts/
|   +-- download_deps.sh         # 下载所有开源依赖（共享）
|   +-- setup_tools.sh           # 激活工具链环境（共享）
|   +-- verify.sh                # 项目验证（共享）
|   +-- convert_font.sh          # 字体转换（共享）
|   +-- setup_env.sh             # 系统依赖安装（共享）
|   +-- firmware/                # 五板固件统一构建（s3/s3n8/cam/c3/pico/all）
|   +-- esp32s3/                 # ESP32-S3 编译脚本（旧入口）
|   |   +-- build.sh             # 编译和烧录
|   |   +-- nuttx_build.sh       # NuttX 专用编译
|   +-- esp32cam/                # ESP32-CAM 编译脚本（旧入口）
|   |   +-- build.sh             # 编译和烧录
|   |   +-- nuttx_build.sh       # NuttX 专用编译
|   +-- esp32c3/                 # ESP32-C3 编译脚本（旧入口）
|       +-- build.sh
|       +-- nuttx_build.sh
|
+-- configs/
|   +-- nuttx-defconfig          # NuttX 内核完整配置
|   +-- lv_conf.h                # LVGL 9.x 配置
|
+-- src/
|   +-- nuttx/
|   |   +-- common/              # 共享代码（驱动/脚本引擎/包管理器/nano 移植层）
|   |   |   +-- bootmenu.c       # 启动菜单
|   |   |   +-- script_engines.c # 脚本引擎集成
|   |   |   +-- script_rom.c     # 板级脚本 ROM（/rom/scripts XIP）
|   |   |   +-- network_utils.c  # curl/wget 网络工具
|   |   |   +-- apps/system/
|   |   |   |   +-- nsh_cmds.c     # NSH 自定义命令（pkg 薄分发）
|   |   |   |   +-- pkg_manager.h  # .rpk 包管理器接口（deb 风格）
|   |   |   |   +-- pkg_manager.c  # 安装/卸载/列表/查询（ustar 流式解析）
|   |   |   |   +-- cmd_run_main.c # run 命令：执行 ROM 模块应用（XIP）
|   |   |   +-- pkg_mods/
|   |   |   |   +-- sysinfo_mod.c  # sysinfo 应用模块（.rpk 交付）
|   |   +-- rommod.[ch]            # ROM XIP 模块加载器（静态绑定档+RAM 窗口档）
|   |   +-- pkg_rom.[ch]           # ROM 包存储：挂载/直查/枚举/首启 seed
|   |   |   +-- driver/          # 共享驱动
|   |   |       +-- retro_gpio.c/.h     # 脚本 GPIO 统一接口（占用拦截+/dev 后端）
|   |   |       +-- retro_gpio_{bas,js,berry,py}.c  # 四引擎 GPIO 绑定
|   |   |       +-- watchdog.c   # 看门狗
|   |   |       +-- firewall.c   # 防火墙
|   |   |       +-- memmon.c     # 内存监控
|   |   |       +-- network.c    # WiFi/网络
|   |   |       +-- ntp.c        # NTP 时间同步
|   |   |       +-- cron.c       # Cron 定时任务
|   |   |       +-- drv_rtc.c    # RTC 驱动
|   |   |       +-- drv_pinyin.c # 拼音输入法驱动
|   |   |       +-- drv_player.c # 媒体播放器驱动
|   |   |       +-- drv_recorder.c # 录音机驱动
|   |   |       +-- drv_sqlite.c # SQLite 驱动
|   |   |       +-- audio/       # 音频目录（目标特定）
|   |   |       +-- cvbs/        # CVBS 目录（目标特定）
|   |   |       +-- fsk/         # FSK 目录（目标特定）
|   |   |       +-- hid/         # HID 目录（目标特定）
|   |   +-- esp32s3/             # ESP32-S3 目标代码
|   |   |   +-- esp32s3_retro.c  # 主入口 + 双核任务
|   |   |   +-- Kconfig          # menuconfig 配置
|   |   |   +-- board/
|   |   |   |   +-- board.h      # GPIO/外设定义（引入硬件档案）
|   |   |   |   +-- hw_esp32s3_devkitc.h  # 硬件档案：DevKitC-1 N16R8/N8R8
|   |   |   |   +-- board.c      # 板级初始化
|   |   |   +-- chip/
|   |   |   |   +-- esp32s3.h    # 芯片寄存器定义
|   |   |   |   +-- startup.c    # 启动代码/中断向量
|   |   |   |   +-- xt_utils.h   # 工具函数头文件
|   |   |   +-- driver/          # ESP32-S3 专用驱动
|   |   |   |   +-- ble_hid.c    # BLE HID 键盘驱动
|   |   |   |   +-- ble_hid.h    # BLE HID 头文件
|   |   |   |   +-- usb_hid.c    # USB HID 键盘驱动
|   |   |   |   +-- ws2812_rmt.c # WS2812 状态灯 RMT 驱动
|   |   |   |   +-- cvbs/drv_cvbs.c       # CVBS LCD_CAM 显示驱动
|   |   |   |   +-- fsk/drv_fsk.c         # FSK 磁带调制解调
|   |   |   |   +-- audio/drv_audio.c     # I2S 音频驱动
|   |   |   +-- include/         # deps/ bug 的覆盖头文件
|   |   +-- esp32/               # ESP32-CAM 目标代码
|   |       +-- esp32_retro.c    # 主入口 + 双核任务
|   |       +-- Kconfig.esp32    # menuconfig 配置
|   |       +-- board/
|   |       |   +-- board.h      # GPIO/外设定义（引入硬件档案）
|   |       |   +-- hw_esp32cam_aithinker.h  # 硬件档案：AI-Thinker
|   |       |   +-- board.c      # 板级初始化
|   |       +-- chip/
|   |       |   +-- esp32.h      # 芯片寄存器定义
|   |       +-- driver/          # ESP32-CAM 专用驱动
|   |       |   +-- ble_hid.c    # BLE HID 键盘驱动
|   |       |   +-- ble_hid.h    # BLE HID 头文件
|   |       |   +-- cvbs/drv_cvbs_dac.c   # CVBS DAC 显示驱动
|   |       |   +-- fsk/drv_fsk.c         # FSK 磁带调制解调
|   |       |   +-- audio/drv_audio_dac.c # DAC 音频驱动
|   |       +-- include/         # 覆盖头文件
|   +-- esp32c3/             # ESP32-C3 目标（合宙核心板，RISC-V，CLI-only）
|   |   +-- esp32c3_retro.c  # 主入口（单核，无 SMP 分工）
|   |   +-- Kconfig.esp32c3  # menuconfig（RETRO_ARCH=riscv-esp32c3）
|   |   +-- board/
|   |   |   +-- board.h      # GPIO/外设定义（引入硬件档案）
|   |   |   +-- hw_esp32c3_luatos.h  # 硬件档案：合宙两款核心板
|   |   |   +-- board.c      # 板级初始化（骨架）
|   |   +-- driver/
|   |       +-- cvbs/drv_cvbs_pdm.c  # CVBS PDM-TX（I2S0 raw + GDMA）
|   +-- rp2040/              # Raspberry Pi Pico 目标（RP2040，CLI 教学终端）
|       +-- rp2040_retro.c   # 主入口（Core0=程序 / Core1=文件IO+视频）
|       +-- Kconfig.rp2040   # menuconfig
|       +-- board/
|       |   +-- board.h      # GPIO/外设定义（引入硬件档案）
|       |   +-- hw_rp2040_pico.h    # 硬件档案：Pico 40-pin
|       |   +-- board.c      # 板级初始化
|       +-- driver/
|           +-- cvbs/drv_cvbs_pio.c # CVBS PIO+DMA 逐行（GP12-15）
|   +-- lvgl/
|   |   +-- retro_ui.c               # 脚本 UI 胶水层
|   |   +-- lvgl_app.c               # LVGL 应用框架
|   |   +-- lv_port_disp.c           # 显示端口
|   |   +-- lv_port_indev.c          # 输入设备端口
|   |   +-- i18n.c                   # 多语种框架
|   |   +-- i18n.h                   # 多语种头文件
|   |   +-- retro_win3_styles.h      # Win3 风格定义
|   |   +-- app/                     # GUI 应用程序
|   |   |   +-- desktop.c           # 桌面管理器（Win3.2 外壳 + 外壳切换）
|   |   |   +-- desktop_api.h       # 桌面外壳公共接口（app 派发/外壳切换）
|   |   |   +-- wmaker_shell.c      # WindowMaker/NeXT 风格外壳（可选）
|   |   |   +-- app_editor.c        # 记事本
|   |   |   +-- app_browser.c       # 浏览器
|   |   |   +-- app_terminal.c      # 终端
|   |   |   +-- app_pinyin.c        # 拼音输入法
|   |   |   +-- app_player.c        # 媒体播放器
|   |   |   +-- app_recorder.c      # 录音机
|   |   |   +-- app_sqlite.c        # SQLite 工具
|   |   |   +-- logo/               # Logo 海龟画图
|   |   +-- modules/
|   |   |   +-- retro_ui_bas.c      # BASIC 脚本 UI 模块
|   |   |   +-- retro_ui_js.c       # JS 脚本 UI 模块
|   |   |   +-- retro_ui_berry.c    # Berry 脚本 UI 模块（可选）
|   |   |   +-- retro_ui_py.c       # CPython 脚本 UI 模块（可选，仅 S3）
|   |   |   +-- logo_jslogo.c       # jslogo 加载器 + LVGL canvas shim（可选）
|   |   +-- audio/
|   |   |   +-- wav_decoder.c       # WAV 解码器
|   |   +-- fonts/
|   |   |   +-- pinyin_ime.c        # 拼音输入法字库
|   |   +-- assets/icons/           # 图标资源 (32x32 PNG)
|   +-- arch/xtensa/src/common/
|       +-- xtensa_cpuinfo.c          # CPU 信息接口
```

---

## 驱动说明

### 目标共享驱动（src/nuttx/common/driver/）

| 驱动 | 文件 | 行数 | 状态 |
|------|------|------|------|
| 看门狗 | watchdog.c | ~560 | 完成 |
| 防火墙 | firewall.c | 623 | 完成 |
| 内存监控 | memmon.c | 537 | 完成 |
| WiFi/网络 | network.c + wifi_conf.c | 432+180 | 完成（配置文件 /opt/etc/network.conf，dhcp|static，`wifi` 命令） |
| NTP | ntp.c | 444 | 完成 |
| Cron | cron.c | 739 | 完成 |
| RTC 驱动 | drv_rtc.c | 538 | 完成 |
| 拼音输入法驱动 | drv_pinyin.c | 1014 | 完成 |
| 媒体播放器驱动 | drv_player.c | 549 | 完成 |
| 录音机驱动 | drv_recorder.c | 518 | 完成 |
| SQLite 驱动 | drv_sqlite.c | 995 | 完成 |

### ESP32-S3 专用驱动（src/nuttx/esp32s3/driver/）

| 驱动 | 文件 | 说明 |
|------|------|------|
| CVBS 显示 | cvbs/drv_cvbs.c | LCD_CAM I80 + GDMA -> GPIO2/15/16/17（4-bit R-2R） |
| 音频 | audio/drv_audio.c | I2S -> 外部 DAC |
| FSK 磁带 | fsk/drv_fsk.c | FSK 调制解调 |
| USB HID | usb_hid.c | USB OTG 键盘鼠标 |
| WS2812 状态灯 | ws2812_rmt.c | RMT 硬件驱动（/dev/rmt0） |
| 看门狗 | esp32s3_retro.c（寄存器级）+ common/driver/watchdog.c（共享接口） | 硬件看门狗 |

### ESP32-CAM 专用驱动（src/nuttx/esp32/driver/）

| 驱动 | 文件 | 说明 |
|------|------|------|
| CVBS 显示 | cvbs/drv_cvbs_dac.c | 内置 DAC -> GPIO25 |
| 音频 | audio/drv_audio_dac.c | 内置 DAC -> GPIO26 |
| FSK 磁带 | fsk/drv_fsk.c | FSK 调制解调 |
| BLE HID | ble_hid.c | BLE 键盘鼠标 |
| BLE Storage | ble_storage.c | Bond 信息 Flash 存储 |
| BLE NSH 命令 | ble_nsh.c | `ble` 命令行工具 |
| BLE 配对界面 | ble_pair_ui.c | LVGL 配对 UI |

### BLE HID 配对流程

**首次配对（无已配对设备）**：
```
上电 -> LED 慢闪(等待配对) -> 用户触发配对命令
  -> LED 快闪(扫描中) -> 发现键盘 -> 自动连接 -> LED 慢闪(已连接)
```

**自动重连（有已配对设备）**：
```
上电 -> 自动连接已配对设备 -> LED 慢闪(已连接)
```

**NSH 命令**：
```
nsh> ble list     # 列出已配对设备
nsh> ble scan     # 扫描附近 HID 设备
nsh> ble pair     # 进入配对模式
nsh> ble unpair 0 # 删除第 1 个设备
nsh> ble unpair all  # 删除所有配对
nsh> ble status   # 显示连接状态
```

**GPIO 使用**：
- 状态 LED: GPIO4 (Flash LED)
- 状态: 快闪=扫描中, 慢闪=已连接, 灭=未连接

### 看门狗 (watchdog.c)

| 看门狗 | 所属 | 超时 | 用途 |
|--------|------|------|------|
| WDT_CORE0 | CPU0（程序核） | 10s | 监控 NSH/脚本/系统任务 |
| WDT_CORE1 | CPU1（媒体核） | 10s | 监控图形/视频/音频/文件 IO 任务 |
| WDT_TASK | 调度器 | 15s | 监控任务调度 |

**功能：**
- 三个独立硬件看门狗，双核各一个
- 超时自动硬件复位
- 看门狗重启记录（`/opt/var/log/reboot.log`，片上，安全模式判定用）
- 连续3次看门狗重启 -> 进入安全模式（CLI Only）
- 安全模式可恢复出厂设置

### 防火墙 (firewall.c) - 623行

**功能：**
- 入站规则：默认强制封禁所有外部入站
- 出站规则：允许所有出站
- 防 ping：禁止外部 ICMP ping
- 连接跟踪：记录活跃连接（最多128个）
- 规则当前为 RAM 态（重启复位；持久化到 /opt/etc/firewall.conf 登记 NEXT_STEPS）
- `fw_enable` / `fw_disable` 动态开关
- `fw_add_rule` / `fw_del_rule` 动态管理

**默认规则：**
```
ALLOW all outbound
DENY  all inbound
ALLOW outbound ICMP (ping)
DENY  inbound ICMP (ping)
```

### 内存监控 (memmon.c) - 537行

**功能：**
- 实时监控堆内存使用（5秒间隔）
- 三级告警阈值：80%（警告）/ 90%（严重）/ 95%（紧急）
- 内存即将耗尽时自动终止最大非核心任务
- 95%以上触发看门狗重启
- 日志记录峰值、最小空闲、分配失败次数

### WiFi/网络 (network.c) - 432行

**功能：**
- STA 模式连接 WiFi
- DHCP 自动获取 IP
- DNS 解析
- `ping` / `netstat` / `ifconfig` 命令

### NTP 时间同步 (ntp.c) - 444行

**功能：**
- 自动 NTP 对时（默认阿里云 ntp.aliyun.com）
- 备选服务器：ntp.aliyun.com（阿里云）、time.windows.com（微软）、pool.ntp.org（国际）
- 每小时同步一次（可配置）
- 同步成功后写入 RTC

### Cron 定时任务 (cron.c) - 739行

**功能：**
- crontab 格式配置
- 支持命令类型：`shell` / `audio:` / `tts:` / `notify:` / `reboot` / `wifi_reconnect`
- 日志默认只串口输出（不落盘，2026-10-05 定稿；显式 CONFIG_CRON_LOG 才写文件）
- 任务保存到 `/opt/etc/crontab`（片上可写分区），重启不丢失、无 SD 卡可用

---


### 2A. AV 视频输出硬件层（2026-10-04 晚）⭐

| 板 | 文件 | 机制 | 时钟 |
|----|------|------|------|
| S3/S3N8 | src/nuttx/esp32s3/driver/cvbs/drv_cvbs.c | LCD_CAM I80 + GDMA 环形链（PSRAM 帧环 625×853×4bit 量化） | PLL160M÷12=13.3333MHz |
| CAM | src/nuttx/esp32/driver/cvbs/drv_cvbs_dac.c | I2S0+内置 DAC1(GPIO25)，16-bit 槽帧环（PSRAM 1.07MB） | APB80M÷6=13.3333MHz |
| C3 | src/nuttx/esp32c3/driver/cvbs/drv_cvbs_pdm.c | I2S0 PDM-TX raw + GDMA，一阶 sigma-delta 位流场环（SRAM 32.6KB） | 160M÷4×170/510 |
| Pico | src/nuttx/rp2040/driver/cvbs/drv_cvbs_pio.c | PIO SM0 `out pins,4` + DMA DREQ 逐行（4 槽乒乓，Core1 生成任务） | 125M÷1.8515625÷5 |

共用：common/driver/cvbs_core.c（行长运行时可配 + 单行 API）、drv_cvbs.c（weak emit_line/frame 供板覆盖）。

### 2B. /dev/cvbscon 字符控制台（2026-10-04 晚；2026-10-06 IME 三层语义）
- common/driver/cvbs_console.c：UTF-8 点阵渲染 + ANSI/CSI 子集 + 可见光标 + 输入环 + poll 等待队列 + TIOCGWINSZ
- UART 键盘泵（avkbin 任务）→ NSH 经 CONFIG_NSH_CONDEV=/dev/cvbscon 跑 AV 屏（C3/Pico）
- **输入多源化（2026-10-05，输入优先级原则 USB > 蓝牙 > 串口）**：
  `cvbs_console_feed_keys()` 公开 API——外部 HID 源（S3 的 usb_hid.c 桥、
  未来 BLE HID）经 common hid_ascii.c（HID 键码→ASCII 纯函数，按下沿差分）
  直喂输入环，与 UART 泵共用同一道 IME 门控；串口泵在 C3 为 BLE 就绪前
  的过渡、在 Pico 为唯一路线（无蓝牙/无 USB 主机）

### 2C. 总线兼容层 / 脚本 ROM（2026-10-04 晚）
- common/driver/retro_bus.[ch]：I2C/SPI/UART machine 风格（硬后端探测 + 位摆软回退）+ retro_bus_{bas,berry,js}.c
- common/script_rom.c：/dev/rom0 内存盘 + /rom/scripts 挂载 + retro_romfs_find Flash 直查 + `script` 命令
- firmware/scripts/<板>/：板级脚本源目录（tools/mkromfs.py 生成 scripts_romfs.c）
- common/nano_port/：GNU nano 8.4 移植层（mini-curses + compat + config）——
  2026-10-05 起 nano 出 ROM 转 .rpk 包（apps-extra/nano），本垫片随包构建复用

### 2D. 内置程序（2026-10-04 晚）
- Application.mk PROGNAME/MAINSRC 配对注册：retro_boot(init)、script、pkg、sysinfo、shell、ime
- src/nuttx/common/apps/system/cmd_*_main.c：薄壳 main → cmd_*()
- 系统编辑器 vi 为 nuttx-apps 内置（CONFIG_SYSTEM_VI）；nano 为 .rpk 包（不占 builtin）

### 2F. 全系唯一字号 12px（2026-10-05 定稿；同日晚半格化+点阵化修订）⭐
- 汉字主体：Noto Sans SC 12px 1bpp 点阵（lv_font_notosans_sc_12，
  UTF-8 全量字符集；嵌入式体积优先，CLI/GUI 共用）
- console 半角/全角标点：Fusion Pixel Font 12px 等宽（OFL-1.1，
  TakWolf 缝合像素字体）——lv_font_ascii_6（半角 94 字形，6px 等宽）
  + lv_font_fullwidth（全角标点/符号 169 字形，12px 满格），由
  scripts/gen_pixel_fonts.py 生成；Noto 矢量比例字形光栅化后半角
  溢出重叠、全角标点墨迹 1-3px 分不清，2026-10-05 晚弃用于 console
- cvbs_console 半格步进网格（宋体 9pt 半角/全角体系：半角 6px /
  全角 12px，1 汉字=2 字母宽、行高 14；全角右半标记位图支持 
  连退；320x240→53 半格列x17 行；640x480→106 半格列x34 行）
- RETRO_FONT_DEFAULT/RETRO_FONT_CONSOLE 同指 12px；LVGL
  LV_FONT_DEFAULT=montserrat_12；CLI 兼容层 lvgl_font_compat 同步

### 2E. 硬件全真外设收口（2026-10-04 深夜）⭐
- esp32s3/driver/ws2812_rmt.c：WS2812 状态灯 RMT 硬件驱动（/dev/rmt0，
  板级绑 GPIO38/48；编码纯函数供宿主测试直链）
- fsk/drv_fsk.c（S3/CAM）：fsk_send() 经 audio_play_pcm() 走 I2S/DAC DMA；
  Kconfig RETRO_FSK_BAUD；drv_fsk.c 首次入构建
- lv_port_disp.c：init 顺序修复（先 drv_cvbs_init 再取帧缓冲——原实现
  memset 空指针，设备 GUI 启动即崩）
- 硬件 I2C0：s3/s3n8（SCL=5/SDA=6）cam（SCL=22/SDA=21）→ /dev/i2c0
  （RTC drv_rtc.c 的 I2CIOC_TRANSFER 路径自此有真实设备节点）
- 四板 hw_*.h 档案与 HARDWARE.md 同步（Pico CVBS=GP12-15、C3 LED 极性/
  Flash 脚修正、CAM GPIO17、S3 LCD_CAM 注释）；tests/host/test_hw_profiles.c
  契约钉死（LED 避让/教学脚/修正回归，219 检查）
- eda/：五板立创EDA 载板工程（gen_eda.py 自动布线 + check_eda.py 零交叉
  校验；全插接件、最小面积；板卡几何经官方 DXF/wiki 核实）

### 2G. 应用/系统分离：ROM 包存储 + XIP 模块（2026-10-06）⭐
- 系统边界：内核/驱动/CVBS/控制台/包管理器/桌面外壳+窗口管理/输入法服务留固件；
  **应用软件全部 .rpk 包化**（editor/browser/terminal/player/recorder/sqlite/
  minesweeper/sysinfo；GPL 组件 nano/ucblogo 仍走 SD 卡包通道）
- 板级默认名单 firmware/packages/<板>.list -> build_romapps.sh 编 .rmo 模块 +
  打 .rpk + mkromfs 树镜像（pkg_romfs.c，4096 对齐入可执行 flash 段）
- 静态绑定档两遍构建（pass0 定槽 pad -> 固件 pass1 定地址 -> defsym 烘焙重链
  落槽 -> pass2 收口；重定位归零、镜像布局一次收敛，详见 HARDWARE 14.3）
- rommod.c 装载：RO 段 flash 原址执行零拷贝，RW 段直拷 arena 固定槽；
  rommod_load_from_mem_inline 为 RAM 窗口动态档保留通道（宿主测试用）
- **构建期离线安装**（tools/gen_pkgdb.py，2026-10-06 定稿：直接安装
  到位）：名单包在编译 ROM 时生成 db/ 预装数据库随镜像分发，首启零
  安装动作；pkg_manager 两级 DB（ROM 预装层 + 片上覆盖层/墓碑）；
  CLI `run <名>` / GUI 桌面注册表（desktop.c 按两级 DB 的 Type: gui 包
  动态装配图标，末窗关闭卸载）

## 网络架构

```
WiFi 802.11 b/g/n (2.4GHz)
    |
    +---> WPA2-PSK 客户端模式
    |
    +---> 网络服务
            +---> DHCP (自动获取 IP)
            +---> DNS 解析
            +---> NTP 客户端 (阿里云 ntp.aliyun.com)
            +---> HTTP 服务器 (可选)
            +---> SSH 客户端
            +---> curl / wget
            +---> Cron (定时任务)
```

---

## 文件系统

```
/rom/scripts/         # 板级脚本 ROMFS（只读，编入固件镜像）
                      #   每板演示/教学脚本（XIP 直跑）

/rom/pkg/             # ROM 包存储（pkg_romfs.c，应用/系统分离 2026-10-06）
                      #   bin/<名> = .rmo XIP 模块载荷（4096 对齐）
                      #   db/ = 预装数据库（构建期离线安装产物，14.5）

/opt/                 # 片上可写数据分区（littlefs，HARDWARE 12.4）
+-- bin/ + share/     #   系统包装载位（Root: system，如 /opt/bin/nano）
+-- etc/              #   系统配置：crontab、boot.cfg、lang.conf、
                      #     network.conf（WiFi 凭据/静态 IP）
+-- var/lib/rpkg/     #   包数据库（control 快照 + manifest + 维护脚本）
+-- var/log/          #   仅重启计数（reboot.log，安全模式判定用）
+-- var/ble_bond.dat  #   BLE 配对信息（蓝牙板）
+-- home/             #   用户数据写入区（无 SD 卡时的主数据区）

/sdcard/              # TF 卡（FAT32，可选硬件；最大 32GB）
+-- scripts/          #   用户脚本（basic/js/berry）
+-- apps/             #   第三方包装载位（Root: sdcard 缺省）
+-- pkg/ + pkg-src/   #   .rpk 安装包与 GPL 源码副本

> 日志默认只从串口输出、不写文件（开发板哲学，2026-10-05 定稿）；
> 唯一例外 reboot.log（连续看门狗重启计数，安全模式判定依赖持久化）。
```

---

## 双启动模式

### GUI 模式（默认）
上电 -> NuttX 启动 -> LVGL 桌面 -> Windows 3.2 风格界面

### CLI 模式（NSH）
上电 -> NuttX 启动 -> NSH Shell -> 命令行界面

### 切换方式
- **启动菜单**：上电时 3 秒倒计时，可选模式
- **NSH 命令**：`bootmode gui` / `bootmode cli`

---

## 安全机制

### 看门狗重启

```
系统卡死 -> WDT 超时 -> 硬件复位 -> 重启记录 -> 正常启动
```

### 连续重启保护

```
连续 3 次看门狗重启 -> 进入安全模式（CLI Only）-> 用户干预
```

### 内存保护

```
内存告警阈值: 80%（黄色）/ 90%（红色）/ 95%（紧急）
内存耗尽 -> 终止非核心任务 -> 仍不够 -> 看门狗重启
```

### 防火墙

```
默认封禁所有入站，允许所有出站
动态规则管理，防 ping
```

---

## 待验证/待优化功能

| 模块 | 优先级 | 说明 |
|------|--------|------|
| QEMU 模拟运行 | 高 | Espressif 专用 QEMU（仅 S3） |
| 开发板烧录 + 实机测试 | 高 | 五板固件均已编译通过，待上板（唯一未开始项） |
| CPython 编译验证 | 中 | `RETRO_SCRIPT_PYTHON=y`（仅 S3 N16R8），ROMFS 标准库镜像 |
| GUI 输入法系统服务化 | 中 | 焦点自动唤起 + 词组整词上屏（见 NEXT_STEPS） |
| curl/wget 优化 | 中 | 完善 HTTP 客户端，支持 HTTPS、断点续传 |
| SSH 客户端 libssh2 | 低 | 需交叉编译 libssh2 |

---

## 测试与调试

### 编译

```bash
# 五板统一入口（推荐）
./scripts/build_firmware.sh all       # 产物 dist/firmware/<板>/

# 单板旧入口
cd scripts/esp32s3 && ./build.sh nuttx     # 或 esp32cam / esp32c3
```

### 串口输出

```
115200 8N1
连接后按回车进入 NSH
```

---

## 实现清单

| 模块 | 文件 | 行数 | 状态 |
|------|------|------|------|
| **共享代码** | | | |
| 启动菜单 | common/bootmenu.c | 502 | 完成 |
| 脚本引擎集成 | common/script_engines.c | 649+ | 完成（五引擎可配置：bas/js/be/py/lgo） |
| curl/wget | common/network_utils.c | 487 | 完成 |
| NSH 命令 | common/apps/system/nsh_cmds.c | 400 | 完成 |
| .rpk 包管理器 | common/apps/system/pkg_manager.c | 600+ | 完成（deb 风格 + 双安装根 + Xip ROM 载荷登记，待实机验证） |
| ROM XIP 模块加载器 | common/rommod.c | ~700 | 完成（静态绑定档全板 + RAM 窗口动态档；宿主端到端/蜕变/dlopen 差分/模糊 2 万轮全绿） |
| ROM 包存储 | common/pkg_rom.c | ~380 | 完成（/rom/pkg 挂载/直查/control 直读/XIP 寻址接线） |
| run 命令 | common/apps/system/cmd_run_main.c | 70 | 完成（CLI 模块统一入口） |
| 模块应用包 | firmware/packages/pkgs/* | 8 包 | 完成（sysinfo + 7 GUI 应用，五板名单默认安装） |
| 离线安装器 | tools/gen_pkgdb.py | ~120 | 完成（构建期直装 db/；格式与 pkg_manager 同构，测试契约锁定） |
| 脚本 GPIO 接口 | common/driver/retro_gpio.c | 280 | 完成（占用拦截+四引擎绑定） |
| 防火墙 | common/driver/firewall.c | 623 | 完成 |
| CVBS 时序核心 | common/driver/cvbs_core.c | - | 完成（宿主解码器差分验证） |
| CVBS 统一驱动 | common/driver/drv_cvbs.c | - | 完成（weak 硬件钩子） |
| 内存监控 | common/driver/memmon.c | 537 | 完成 |
| WiFi/网络 | common/driver/network.c | 432 | 完成 |
| NTP | common/driver/ntp.c | 444 | 完成 |
| Cron | common/driver/cron.c | 739 | 完成 |
| RTC 驱动 | common/driver/drv_rtc.c | 538 | 完成 |
| 拼音输入法驱动 | common/driver/drv_pinyin.c | 1014 | 完成 |
| CCDOS 输入法（三层语义+自启） | common/driver/cvbs_ime.c | ~210 | 完成（默认不启动；Ctrl+Space 调出/收起=中英；Ctrl+Q/ime off 退出；ime autostart 配置随系统启动） |
| 媒体播放器驱动 | common/driver/drv_player.c | 549 | 完成 |
| 录音机驱动 | common/driver/drv_recorder.c | 518 | 完成 |
| SQLite 驱动 | common/driver/drv_sqlite.c | 995 | 完成 |
| **ESP32-S3 专用** | | | |
| 主入口 | esp32s3/esp32s3_retro.c | 403 | 完成 |
| 板级初始化 | esp32s3/board/board.c | ~250 | 完成 |
| 芯片定义 | esp32s3/chip/esp32s3.h | ~200 | 完成 |
| 启动代码 | esp32s3/chip/startup.c | ~180 | 完成 |
| CVBS 驱动 (I2S) | esp32s3/driver/cvbs/drv_cvbs.c | 455 | 完成 |
| FSK 驱动 | esp32s3/driver/fsk/drv_fsk.c | 486 | 完成 |
| 音频驱动 (I2S) | esp32s3/driver/audio/drv_audio.c | 734 | 完成 |
| USB HID | esp32s3/driver/usb_hid.c | 424 | 完成 |
| 看门狗 | esp32s3/esp32s3_retro.c（寄存器级，唯一实现） | - | 完成 |
| **ESP32-CAM 专用** | | | |
| 主入口 | esp32/esp32_retro.c | ~350 | 完成 |
| 板级初始化 | esp32/board/board.c | ~200 | 完成 |
| 芯片定义 | esp32/chip/esp32.h | ~150 | 完成 |
| CVBS 驱动 (DAC) | esp32/driver/cvbs/drv_cvbs_dac.c | ~400 | 完成 |
| FSK 驱动 | esp32/driver/fsk/drv_fsk.c | ~400 | 完成 |
| 音频驱动 (DAC) | esp32/driver/audio/drv_audio_dac.c | ~500 | 完成 |
| BLE HID | esp32/driver/ble_hid.c | ~200 | 完成 |
| 看门狗 | common/driver/watchdog.c（共享接口，寄存器级在各板主入口） | - | 完成 |
| **ESP32-C3 专用** | | | |
| 主入口 | esp32c3/esp32c3_retro.c | - | 完成（单核，CLI） |
| CVBS 驱动 (PDM) | esp32c3/driver/cvbs/drv_cvbs_pdm.c | - | 完成（I2S0 PDM-TX + GDMA） |
| **RP2040 (Pico) 专用** | | | |
| 主入口 | rp2040/rp2040_retro.c | - | 完成（Core0=程序/Core1=媒体） |
| CVBS 驱动 (PIO) | rp2040/driver/cvbs/drv_cvbs_pio.c | - | 完成（PIO SM0 + DMA，GP12-15） |
| **LVGL 应用** | | | |
| 桌面管理器 | lvgl/app/desktop.c | 1280 | 完成（含外壳切换/app 派发） |
| WindowMaker 外壳 | lvgl/app/wmaker_shell.c | 430 | 完成（可选 RETRO_DESKTOP_SHELL_WMAKER） |
| 记事本 | lvgl/app/app_editor.c | 636 | 完成 |
| 浏览器 | lvgl/app/app_browser.c | 564 | 完成 |
| 终端 | lvgl/app/app_terminal.c | 673 | 完成 |
| 拼音输入法 | lvgl/app/app_pinyin.c | 557 | 完成 |
| 媒体播放器 | lvgl/app/app_player.c | 742 | 完成 |
| 录音机 | lvgl/app/app_recorder.c | 705 | 完成 |
| SQLite 工具 | lvgl/app/app_sqlite.c | 1314 | 完成 |
| Logo 海龟画图 | lvgl/app/logo/ | ~400 | 完成 |
| 多语种框架 | lvgl/i18n.c | 493 | 完成 |
| 脚本 UI 胶水层 | lvgl/retro_ui.c | 860 | 完成 |
| LVGL 应用框架 | lvgl/lvgl_app.c | 43 | 完成 |
| WAV 解码器 | lvgl/audio/wav_decoder.c | 534 | 完成 |
| 拼音输入法字库 | lvgl/fonts/pinyin_ime.c | 470 | 完成 |
| BASIC 脚本 UI | lvgl/modules/retro_ui_bas.c | 226 | 完成 |
| JS 脚本 UI | lvgl/modules/retro_ui_js.c | 340 | 完成 |
| Berry 脚本 UI | lvgl/modules/retro_ui_berry.c | 285 | 完成（可选 CONFIG_RETRO_SCRIPT_BERRY） |
| CPython 脚本 UI | lvgl/modules/retro_ui_py.c | 300 | 完成（可选，仅 S3） |
| jslogo 加载器 | lvgl/modules/logo_jslogo.c | 330 | 完成（可选 CONFIG_RETRO_LOGO_JSLOGO） |
| CPU 信息接口 | arch/xtensa/xtensa_cpuinfo.c | 229 | 完成 |
| **总计** | | **~42,400**（2026-10-05 含五脚本引擎/包管理器/双外壳/retro_gpio/五板目标） | **完成** |

---

_最后更新: 2026-10-06（应用/系统分离：ROM 包存储 + XIP 模块体系）_
