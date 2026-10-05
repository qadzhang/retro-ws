/*

### 五板全栈 ROM（2026-10-04 深夜）
- S3/S3N8/CAM：LVGL GUI + Win3/WMaker 双外壳 + 8 图形程序 + jslogo +
  BASIC/Berry/JS 四解释器 + AV 控制台 + 全量 UTF-8 字库（10%/21%/42%）
- C3：CLI 全家桶（cle/hexed/dd/tee + 三解释器 + AV 控制台，35%；编辑器当日两度
  定稿——最终：系统 vi + nano .rpk 包，见 §26）
- Pico：Berry+BASIC+AV 控制台（60%）
- RAM 双达标：LVGL 堆/帧缓冲入 SPIRAM（CAM dram0 111%→40%）
- 栈尺寸按板可配（C3/Pico 小栈防爆 SRAM）


### 全系全功能 ROM（2026-10-04 晚间）
- AGENTS.md 7.3：UTF-8 全链路 + 唯一字体 + 全系 AV 输出三铁律
- cvbs_console：UTF-8 点阵字符控制台（614 checks + GLM 视觉 PASS）
- lvgl_font_compat：CLI 无 LVGL 编译同一字体（fmt_txt 语义复刻，
  与真 LVGL dsc 差分一致）
- Berry + my-basic 五板默认入 ROM（ Berry 523KB 源码全链编译）
- 五板全功能 ROM：8%~60% 占用（见 BUILD_FIXES 表）
- 宿主门禁增至九项（+控制台测试）ALL PASS；语法矩阵 71/71


### 固件交付（2026-10-04 下午）
- 五配置真机固件全通（交叉编译产物在 dist/firmware/<板>/）
- 第四目标 Raspberry Pi Pico（RP2040）：本地 CLI 档，HARDWARE.md 3B 章
- 双核分工全局规范落地：S3/CAM/Pico = CPU0 程序核 / CPU1 媒体(或文件IO)核
- ROM 全部达标（1.2%~7.2%），无需裁剪
- 工具链/HAL/apps 集成三条管线脚本化（prepare_esp_hal.sh /
  sync_src_to_apps.sh / build_firmware.sh）


### 字体与 CLI 中文（2026-10-04 下午追加）
- 中文字库定稿 1bpp 点阵三档：FULL（Unicode 区段全量，.o 911KB/
  .c 6.2MB）、GB2312（.o 352KB/.c 2.3MB）、拉丁（montserrat_12）；
  4bpp 抗锯齿方案弃用（复古分辨率无意义，体积 4 倍）
- retro_font.h 三档开关 + Kconfig choice；全部 UI 代码统一
  RETRO_FONT_DEFAULT，杜绝散落 montserrat 硬编码
- scripts/convert_font.sh 固化实测管线（ttc→fonttools 抽面→
  lv_font_conv 1bpp）；两档字库入库 src/lvgl/fonts/
- CLI UTF-8 门禁（147 文件编码校验 + NSH 双语输出抽验）加入
  run_all（第 8 项）；串口控制台 UTF-8 透传，PC 终端渲染字形
- glm 视觉审查：CVBS 测试图 PASS、管线图（含中文标题/标签）PASS、
  两款桌面中文字形为真实点阵无 tofu


## 2026-10-04 全面审计修复与测试体系（ai-code-testing 规范落地）

### 修复（详见 BUILD_FIXES.md 同日条目）
- 删除 esp32s3 遗留副本树 15 文件（符号冲突源头）
- retro_gpio：幻觉 ioctl API 按真实 NuttX 12.12 头修正
- pkg_manager：安全+健壮性重写（包名白名单/manifest 过滤/CRC 实校验/失败回滚/短读防护）
- CVBS：抽取 cvbs_core 可移植核心（PAL 场时序），统一 drv_cvbs 层，S3/CAM 设备层重写
- 启动装配去重（board.c 唯一符号归属），startup.c 语法修复并封存
- common 驱动 40+ 缺陷修复（I2C ioctl、NTP 字节序、ping 实装、URL 边界、拼写 UTF-8 等）
- LVGL 层 30+ 缺陷修复（v7/v8 API 全面迁移 9.5、调色板方案改 L8、retro_ui 对话框崩溃等）
- 外设层（音频 DMA/BLE 封存/USB HID/FSK）修复
- retro_ui 增加 CLI 编译路径（C3 目标可链接）

### 新增
- tests/host/：五层测试体系（131+29+916 单测、545 Python 差分/PBT、
  20 万轮模糊、变异门禁 100%、性能基准）+ 真实头全仓语法矩阵（66 文件）
- tools/sim/：LVGL 无头模拟器 + CVBS 全链路管线（glm 视觉审查通过）
- deps：NuttX 12.12/nuttx-apps/LVGL 9.5/Duktape 2.7/my-basic 就位

 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * 复古工作站 Retro WS - 已完成需求
 * Retro Workstation (retro-ws) - Completed Features
 */

## 更新日期 / Update Date

2026-10-05

---

## 已完成功能清单

### 1. 多目标架构

| 需求 | 实现 | 状态 |
|------|------|------|
| 支持五板目标：S3 / S3N8 / CAM / 合宙 C3 / Pico | `src/nuttx/common/` + `src/nuttx/{esp32s3,esp32,esp32c3,rp2040}/` | 完成（五板固件 2026-10-05 全部编译通过） |
| 共享代码提取 | `src/nuttx/common/`（驱动/脚本引擎/包管理器/cvbs_console/nano 移植层） | 完成 |
| 五板统一构建入口 | `scripts/firmware/build_firmware.sh <s3|s3n8|cam|c3|pico|all>` | 完成 |
| 旧三板编译脚本保留 | `scripts/esp32s3/` + `scripts/esp32cam/` + `scripts/esp32c3/` | 完成 |
| 共享脚本保留 | `scripts/download_deps.sh`, `setup_tools.sh`, `verify.sh`, `convert_font.sh` | 完成 |

### 2. 系统架构

| 需求 | 实现文件 | 状态 |
|------|---------|------|
| 双核分工全局规范（CPU0=程序核 / CPU1=媒体核：图形/视频/音频/文件IO） | `esp32s3_retro.c` / `esp32_retro.c` / `rp2040_retro.c`（sched_setaffinity 钉核） | 完成 |
| NuttX RTOS 12.x POSIX 兼容 | `nuttx/` | 完成 |
| 类 Unix 架构、硬实时 | `nuttx/` | 完成 |
| 系统程序固化片上 Flash，核心文件用户不可修改 | `board.c` | 完成 |

### 3. 硬件规格

#### 3.1 ESP32-S3 目标

| 需求 | 实现 | 状态 |
|------|------|------|
| ESP32-S3 主控（双核 240MHz，8MB PSRAM，16MB Flash） | `esp32s3/board.c` | 完成 |
| Core 0 专属图形+音频，Core 1 专属系统+网络 | `esp32s3_retro.c` | 完成 |
| 5 档分辨率（640x480 / 800x600 / 1024x600 / 720x576 / 1024x768） | `esp32s3/driver/cvbs/drv_cvbs.c` | 修订为三档（见 9 节）|
| 256 色调色板（8bit palette） | `esp32s3/driver/cvbs/drv_cvbs.c` | 完成 |
| LCD_CAM I80 CVBS 输出 (GPIO2/15/16/17 4-bit R-2R，2026-10-04 前误记 I2S bitbang) | `esp32s3/driver/cvbs/drv_cvbs.c` | 完成 |
| I2S 音频输出（GPIO40/41/42） | `esp32s3/driver/audio/drv_audio.c` | 完成 |
| ADC 音频输入（GPIO1） | `esp32s3/driver/audio/drv_audio.c` | 完成 |
| BLE HID 键鼠 | `esp32s3/driver/ble_hid.c` | 完成 |
| USB HID 键鼠 | `esp32s3/driver/usb_hid.c` | 完成 |
| 蓝牙状态 LED（WS2812 RGB，v1.1=GPIO38 / v1.0=GPIO48；RMT 驱动已实现——ws2812_rmt.c，2026-10-04 深夜） | `esp32s3/driver/ws2812_rmt.c` | 完成 |
| SPI SD 卡 | `esp32s3/board.c` | 完成 |

#### 3.2 ESP32-CAM 目标

| 需求 | 实现 | 状态 |
|------|------|------|
| ESP32-CAM 主控（双核 240MHz，4MB PSRAM，4MB Flash） | `esp32/board.c` | 完成 |
| 内置 DAC CVBS 输出 (GPIO25) | `esp32/driver/cvbs/drv_cvbs_dac.c` | 完成 |
| 内置 DAC 音频输出 (GPIO26) | `esp32/driver/audio/drv_audio_dac.c` | 完成 |
| ADC 音频输入 (GPIO34) | `esp32/driver/audio/drv_audio_dac.c` | 完成 |
| BLE HID 键鼠 | `esp32/driver/ble_hid.c` | 完成 |
| 蓝牙状态 LED（GPIO4） | `esp32/driver/ble_hid.c` | 完成 |
| BLE Bond 存储 | `esp32/driver/ble_storage.c` | 完成 |
| BLE NSH 命令 | `esp32/driver/ble_nsh.c` | 完成 |
| BLE 配对 UI | `esp32/driver/ble_pair_ui.c` | 完成 |
| SPI 模式 SD 卡 | `esp32/board.c` | 完成 |

#### 3.3 通用硬件

| 需求 | 实现 | 状态 |
|------|------|------|
| FSK 磁带调制解调（300-9600 baud） | `drv_fsk.c`（各目标） | 完成 |
| TF 卡 FAT32 最大 32GB | SD 驱动 | 完成 |
| WiFi 802.11 b/g/n 2.4GHz | `common/driver/network.c` | 完成 |
| RTC 可选外设（DS1307/DS1338/PCF8563/RV-3028-C7） | `common/driver/drv_rtc.c` | 完成 |

### 4. 软件系统

| 需求 | 实现 | 状态 |
|------|------|------|
| LVGL 9.x 图形引擎（Windows 3.2 风格） | `desktop.c` | 完成 |
| 中文字体支持（NotoSansSC 子集） | `tools/fonts/` + `convert_font.sh` | 完成 |
| NuttShell (NSH) CLI | `common/apps/system/nsh_cmds.c` + NuttX | 完成 |
| my_basic 脚本引擎 | `common/script_engines.c` | 完成 |
| Duktape JavaScript 引擎 | `common/script_engines.c` | 完成 |
| 两脚本引擎按需加载切换 | `common/script_engines.c` | 完成 |
| curl HTTP 客户端 | `common/network_utils.c` | 完成 |
| wget 文件下载 | `common/network_utils.c` | 完成 |
| ping / netstat / ifconfig | `common/driver/network.c` | 完成 |
| SSH 客户端（libssh2） | - | 待集成 |
| 文本浏览器 | - | 待移植（定为 Links 2.30，下载脚本已支持；w3m 弃议，见 NEXT_STEPS 48） |
| 防火墙默认封禁外部入站 | `common/driver/firewall.c` | 完成 |
| 允许所有出站 | `common/driver/firewall.c` | 完成 |
| 禁止外部 ping | `common/driver/firewall.c` | 完成 |
| 规则持久化 | `common/driver/firewall.c` | 待办（RAM 态；/opt/etc/firewall.conf 登记 NEXT_STEPS 51） |

### 5. 时间与定时任务

| 需求 | 实现 | 状态 |
|------|------|------|
| NTP 网络对时（默认阿里云 ntp.aliyun.com） | `common/driver/ntp.c` | 完成 |
| RTC 时钟（可选外置） | `common/driver/drv_rtc.c` | 完成 |
| Cron 定时任务（crontab 格式） | `common/driver/cron.c` | 完成 |
| 任务栏实时显示日期时间 | `desktop.c` | 完成 |

### 6. 系统可靠性

| 需求 | 实现 | 状态 |
|------|------|------|
| 硬件看门狗 WDT（双核各一个） | `watchdog.c`（各目标） | 完成 |
| 看门狗超时自动重启 | `watchdog.c` | 完成 |
| 内存溢出监控与防护 | `common/driver/memmon.c` | 完成 |
| Core dump 崩溃记录 | `esp32s3/chip/startup.c` | 完成 |
| GUI / CLI 双启动模式 | `common/bootmenu.c` | 完成 |
| 启动菜单（3 秒倒计时） | `common/bootmenu.c` | 完成 |
| 安全模式（连续 3 次看门狗重启） | `common/bootmenu.c` | 完成 |
| 系统重启原因记录 | `watchdog.c` | 完成 |

### 7. 存储与文件系统

| 需求 | 实现 | 状态 |
|------|------|------|
| TF 卡 FAT32 32GB（可选硬件） | `board.c`（SDIO/SPI），/sdcard 大容量扩展 | 完成 |
| 片上可写分区 littlefs 挂 /opt | HARDWARE 12.4 分区定稿（NEXT_STEPS 17b 落地） | 定稿 |
| /opt 系统区 | 系统包（/opt/bin）+ 配置（/opt/etc）+ 包 DB + 用户数据 | 定稿 |
| /sdcard 第三方包 | Root: sdcard 缺省根（/sdcard/apps） | 完成 |

### 8. 编码规范

| 需求 | 实现 | 状态 |
|------|------|------|
| Linux Kernel 风格 C 代码 | `CODING_STANDARD.md` | 完成 |
| PEP 8 Python 代码风格 | `CODING_STANDARD.md` | 完成 |
| NuttX menuconfig 配置系统 | `Kconfig` + `nuttx-defconfig` | 完成 |
| SPDX 许可证头 | 所有源文件 | 完成 |

### 9. 硬件档案与规格修正（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| 首选板定为 ESP32-S3 DevKitC-1 N16R8/N8R8（开发模板） | `HARDWARE.md` 1.1 节 | 完成 |
| 每板一个硬件设置文件（引脚唯一事实来源） | `hw_esp32s3_devkitc.h` / `hw_esp32cam_aithinker.h`，board.h 引入 | 完成 |
| 修正 R8 八线 PSRAM 占用 GPIO35/36/37（S3 用户按键改 GPIO7/8） | `HARDWARE.md` 2.5 节 | 完成 |
| 修正 DevKitC-1 LED 为单颗 WS2812（v1.0=GPIO48 / v1.1=GPIO38） | `HARDWARE.md` 2.8 节 | 完成 |
| 新增 strapping 引脚约束（GPIO0/3/45/46） | `HARDWARE.md` 2.6 节 | 完成 |
| 修正 ESP32-CAM SD 卡 SPI 引脚（CS=13/MOSI=15/MISO=2/CLK=14） | `HARDWARE.md` 3.9 节 | 完成 |
| 修正 GPIO33 为红色状态 LED（低电平点亮）而非用户按键 | `HARDWARE.md` 3.12 节 | 完成 |
| 分辨率定档三档：320x240 控制台 / 640x480 常规 / 1024x768 最高（实验） | `HARDWARE.md` 6.4 节 + 硬件档案 RES_* 宏 | 完成 |
| 依赖版本核查（NuttX 13.0.0 / ESP-IDF v6.0 / LVGL 9.6 等） | `DEPENDENCIES.md` 版本核查节 | 完成（结论：维持现版本） |

### 10. 脚本引擎可配置化（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| 所有内嵌脚本语言可配置编译 | Kconfig `RETRO_SCRIPTS`：TINYBASIC/DUKTAPE/BERRY/PYTHON + `RETRO_LOGO_JSLOGO` | 完成 |
| 五引擎统一调度（按需加载/扩展名识别） | `common/script_engines.c` 重写（含宏名不一致修复） | 完成 |
| Berry 绑定（retro_ui_* 全局函数） | `lvgl/modules/retro_ui_berry.c` | 完成（2026-10-04 晚起五板入 ROM） |
| CPython 绑定（import retro_ui） | `lvgl/modules/retro_ui_py.c`（仅 S3 N16R8） | 完成（待编译验证） |
| jslogo 集成（UCBLogo 子集） | `lvgl/modules/logo_jslogo.c`（Duktape + LVGL canvas shim）；`download_deps.sh` 增 jslogo | 完成（待实机验证） |
| 示例脚本 | `examples/hello.be` / `hello.py` / `spiral.lgo` | 完成 |
| 输入法全环境需求（GUI 系统服务 + CLI `ime` 命令） | CLI `ime` 已完成（2026-10-05，§24）；GUI 输入条已落地，系统服务化待办（NEXT_STEPS 21） | 部分完成 |

### 11. WindowMaker/NeXT 风格外壳（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| 可切换桌面外壳（Win3.2 <-> WindowMaker） | `desktop.c` `retro_desktop_set_shell()` + `desktop_api.h` | 完成（2026-10-04 晚起随五板 ROM 编译；无头模拟渲染验证 pass） |
| 右侧 Dock 栏（凹陷槽位 + 时钟槽） | `wmaker_shell.c` `wm_dock_create()` | 完成 |
| 桌面应用图标（双击启动） | `wmaker_shell.c` `wm_icons_create()` | 完成 |
| NeXT 式根菜单（桌面点击弹出） | `wmaker_shell.c` `wm_menu_open()`（黑底标题条 + 分节 + 应用项） | 完成 |
| 应用统一派发 API | `retro_desktop_app_launch(id)`（10 个应用标识） | 完成 |
| 编译期外壳选择 | Kconfig `RETRO_DESKTOP_SHELL_{WIN3,WMAKER}`（图形目标） | 完成 |
| 运行时 NSH 切换 | `nsh_cmds.c` `shell win3|wmaker` 命令 | 完成 |
| i18n 双语 | `i18n.c` 新增 WM_* / APP_SETTINGS 字符串 | 完成 |

### 12. GPL 独立程序包体系与包管理器（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| GPL 组件与固件许可证隔离（mere aggregation） | `apps-extra/ucblogo/`（README/Kconfig/Makefile） | 完成（打包链路就绪；binfmt 实机验证待办，NEXT_STEPS 17） |
| **deb 风格包管理器（.rpk）** | `pkg_manager.[ch]`：ustar 流式解析/CRC32/路径防护/Arch 检查/Depends | 完成（格式契约已经主机端模拟验证） |
| 安装数据库（仿 /var/lib/dpkg） | `/opt/var/lib/rpkg/`（片上可写分区——无 SD 卡可用；2026-10-05 前在 /sdcard） | 完成 |
| 维护脚本 | preinst/postinst/prerm/postrm（NSH 脚本，system("sh")） | 完成 |
| 主机侧打包器 | `scripts/make_package.sh`（含 manifest CRC 生成；重复条目缺陷已修复并经 tar 结构验证） | 完成 |
| 安装包构建器 | `scripts/build_packages.sh`（下载→LOADABLE→.rpk→dist/sdcard/pkg） | 完成 |
| 一键整体构建 | `scripts/build_all.sh`（固件+安装包，--no-packages 可跳过） | 完成 |
| 固件侧命令 | `nsh_cmds.c` `pkg install/remove/list/info`（薄分发到 pkg_manager） | 完成 |
| UCBLogo 包模板 | `apps-extra/ucblogo/package/`（control + postinst） | 完成 |
| GPL 红线入规范 | AGENTS.md 11.1 第 9 条 | 完成 |
| 许可证策略文档 | DEPENDENCIES.md "GPL 独立程序包策略" 节 | 完成 |

### 13. 第三目标 esp32c3 与包架构隔离（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| 新增合宙 ESP32-C3 核心板目标（低资源/低价格，RISC-V） | `src/nuttx/esp32c3/`（retro 主入口/board/Kconfig） | 完成（2026-10-04 晚起五板 ROM 编译通过） |
| 两款板支持（经典款 CH343 / 简约款原生 USB） | `hw_esp32c3_luatos.h` + Kconfig `RETRO_LUATOS_C3_{UARTBRIDGE,NATIVEUSB}` | 完成 |
| .rpk 包架构隔离（Xtensa vs RISC-V vs ARM） | `pkg_manager.c` `arch_match()`：all/架构族/芯片名 三级匹配，四目标 Kconfig 定义 `RETRO_ARCH`（esp32s3/esp32/esp32c3/rp2040） | 完成 |
| CLI-only 定位（无 LVGL，包管理器为主通道） | `configs/nuttx-defconfig-esp32c3` 最小配置 + `RETRO_PKG_MANAGER` | 完成（待校准） |
| C3 编译脚本 | `scripts/esp32c3/{build,nuttx_build}.sh`（riscv32-esp-elf 工具链） | 完成 |
| CLI 目标命令桩 | nsh_cmds.c：cmd_shell 在无 LVGL 目标返回 ENOSYS | 完成 |

### 14. 脚本 GPIO 教学接口（2026-10-04）

| 需求 | 实现 | 状态 |
|------|------|------|
| retro_gpio 统一接口（config/write/read/adc/pwm/release） | `common/driver/retro_gpio.[ch]`（策略层+设备后端） | 完成（五板编译通过 + 宿主占用拦截测试；/dev 后端实机验证待办） |
| 系统占用引脚拦截（报"已占用"返回 -EBUSY） | `gpio_is_blocked()` + 各板占用表（hw 档案宏 -> board.c 实例化） | 完成（全板覆盖，2026-10-04 起含 Pico） |
| BASIC 绑定 | `retro_gpio_bas.c`（mb_register_func） | 完成 |
| JS 绑定 | `retro_gpio_js.c`（retro_gpio.* 对象） | 完成 |
| Berry 绑定 | `retro_gpio_berry.c`（全局函数，C3 CLI 可用） | 完成 |
| Python 绑定 | `retro_gpio_py.c`（import retro_gpio） | 完成 |
| script_engines 接线 | 四引擎 init 注册（含修复 retro_ui_bas_register 缺调、LVGL 目标守卫） | 完成 |
| Kconfig 开关 | `RETRO_GPIO_SCRIPT`（全板，默认 y） | 完成 |
| 统一接口入规范 | AGENTS.md 8.1 扩展 retro_gpio_* | 完成 |

---



### 15. AV 输出全真硬件化（2026-10-04 晚）⭐
- **S3/S3N8**：LCD_CAM I80 并行口 + NuttX GDMA API 环形描述符链，PLL160M÷12=13.3333MHz 整数分频，4-bit R-2R（GPIO2/15/16/17），帧环 625×853 样本 PSRAM（HARDWARE.md 6.5）
- **C3**：I2S0 PDM-TX raw 模式 + GDMA，一阶 sigma-delta 1-bit@13.3333MHz 单脚 GPIO1 + RC 滤波，313 行场环 32.6KB SRAM（HARDWARE.md 3A.2A）
- **Pico**：PIO SM0 `out pins,4` + DMA DREQ 节流逐行，GP20-23 4-bit R-2R，Core1 视频生成任务（双核分工规范落地）（HARDWARE.md 3B.2A；**当晚已移脚 GP12-15**——GP23 为 SMPS 省电脚会污染 3V3 基准，见同日"硬件全真外设收口"条）
- **CAM**：内置 DAC1(GPIO25) I2S0 DMA 整帧环，时钟改整数 ÷6 + 853 样本行（消小数分频抖动），描述符补 owner 位
- **cvbs_core**：行长运行时可配（cvbs_core_set_line_layout）+ 单行生成 API（cvbs_core_field_line/line_kind）供逐行架构；每板采样时钟表入 HARDWARE.md 6.2

### 16. NSH 控制台走 AV 屏（2026-10-04 晚）
- cvbs_console 升级完整终端：ANSI/CSI 子集（光标定位/移动/清屏/清行/光标可见性/SGR 忽略）、可见下划线光标（字形区避让底部 2 行）
- /dev/cvbscon 字符设备（read=键盘输入环 + poll 等待队列 + TIOCGWINSZ 报 13×40）
- UART 键盘输入泵任务（/dev/console 读入喂输入环）
- C3/Pico：CONFIG_NSH_ALTCONDEV=/dev/cvbscon——**NSH 与全屏程序跑在 AV 电视屏上**

### 17. 真 GNU nano 8.4 移植（2026-10-04 晚）⭐（2026-10-05 翻转：出 ROM 转 .rpk 包，见 §26）
- deps/nano 上游 nano-8.4 原版源码（GPL，sha256 前 16 位 5ad29222bbd55624）
- mini-curses 垫片（~700 行）：虚拟屏 diff 刷新 + termios raw + 转义键解码 + UTF-8 宽字符感知单元格（CJK 双列）
- NuttX 缺口补齐：mkstemps/locale 存根/REG_STARTEND/键码表（config.h autoconf 惯例——关闭项不定义）
- 宿主 pty 全链路验证：编辑/^O 写盘/^X 退出/快捷键栏 ✓；五板全部编入，vi（CONFIG_SYSTEM_VI）全部移除

### 18. retro_bus 总线兼容层（2026-10-04 晚，NEXT_STEPS 49 落地）
- MicroPython machine 风格统一 API（HARDWARE.md 13.3）：I2C(scan/writeto/readfrom/mem) + SPI(全双工 xfer) + UART(读写/超时/available)
- 后端运行时探测：/dev/i2cN、/dev/spiN（ioctl I2CIOC_TRANSFER/SPIIOC_TRANSFER）优先，缺驱动板回退 retro_gpio 位摆软总线（I2C 开漏仿真 100kHz / SPI mode0）
- 三引擎绑定：Berry(retro_i2c_* 全局函数 + bytes 类型)、my-basic（十进制串字节流）、Duktape（retro_bus 对象 + number[]）

### 19. 板级脚本 ROM 化 XIP + builtin 接线（2026-10-04 晚，NEXT_STEPS 50 落地）
- firmware/scripts/<五板>/ 演示脚本（hello.be/bas/js，中文注释）
- tools/mkromfs.py 自研 ROMFS 生成器（无外部依赖，round-trip 解析验证 ✓）
- /dev/rom0 内存盘块设备 + /rom/scripts 挂载 + retro_romfs_find 直查 Flash 指针
- `script <名>` 命令：XIP 执行——Berry be_loadbuffer/Duktape duk_peval_lstring 长度型接口直吃 Flash 指针（零 RAM 拷贝）
- nsh_cmds 死代码表转 NuttX builtin：sysinfo/pkg/shell/script/nano 五命令真实可用（Application.mk PROGNAME/MAINSRC 配对）

### 20. 硬件全真外设收口（2026-10-04 深夜）⭐
- **WS2812 RMT 硬件驱动**（ws2812_rmt.c + /dev/rmt0 + s3/s3n8 appconfig
  开 RMT；替代点不亮的 gpio 直驱；宿主 353 测试 + 变异 100%）
- **FSK TX 接 I2S/DAC DMA**（fsk_send → audio_play_pcm；S3/CAM 双版；
  Kconfig 补 RETRO_FSK_BAUD；drv_fsk.c 首次纳入构建）
- **RTC/总线硬件 I2C**（s3/s3n8: ESP32S3_I2C0 SCL=5/SDA=6；cam:
  ESP32_I2C0 SCL=22/SDA=21——恰为 ESP32 硬件 I2C 脚；drv_rtc.c 原已
  走 I2CIOC_TRANSFER，本日补齐设备节点开关）
- **lv_port_disp NULL 修复**（GUI 启动即崩的真 BUG，模拟器捕获）
- **四板硬件档案同步**（Pico CVBS→GP12-15、C3 LED 高有效/Flash 脚修正/
  GPIO10 释放、CAM GPIO17、S3 CVBS 注释对齐 LCD_CAM）
- **板上指示灯引脚避让原则**入 HARDWARE.md 13.1 并由
  test_hw_profiles.c 契约测试钉死（四板 219 检查）

### 21. 立创EDA 五板载板工程（2026-10-04 深夜）⭐
- eda/gen_eda.py：EasyEDA 标准版 JSON 生成器（docType 3 PCB + 1 原理图，
  单位 1=10mil）内置两层曼哈顿自动布线器（L/Z/底层通道/边距绕行/FPC
  下潜/暴力兜底，逐段冲突检测含压盘与板界）
- 五板载板（s3/s3n8/cam/c3/pico）全插接件设计（母排+FPC 座+电阻模块
  插座），check_eda.py 零交叉校验 PASS
- 板卡几何经官方 DXF/wiki 核实（DevKitC 22.86mm/CAM 22.86/合宙 21.0
  非 2.54 网格/Pico 17.78）

### 29. Pico PIO-USB 主机落地（2026-10-05 晚）⭐
- **许可证调研**：Pico-PIO-USB = MIT（sekigon-gonnoc），与本项目零 GPL
  红线兼容，可编入固件 ROM（LICENSE 随源码保留）
- **vendor**：上游 master 快照 12 文件原样入
  src/nuttx/rp2040/driver/input/pio_usb/upstream/（零修改）
- **port/ 垫片**：pico-sdk 的 pio/dma/gpio/clocks/sync/platform/
  bootrom/sysinfo 共 9 个替身头 + port_dma.c——桥到 NuttX rp2040_pio_*
  与寄存器直写；PIO1 SM0/1/2 + DMA 通道 0 + GP20/21，与 CVBS(PIO0) 零冲突
- **piousb_kbd.c**：自写枚举状态机（SET_ADDRESS→GET_DESCRIPTOR×2→
  SET_CONFIGURATION→SET_PROTOCOL boot→SET_IDLE→IN 端点轮询），
  8 字节报告经 hid_ascii 按下沿差分→cvbs_console_feed_keys 入环；
  1ms 轮询任务 sched_setaffinity 钉 **CPU1（媒体/IO 核，用户指示）**；
  PIO1 IRQ0→上游 pio_usb_host_irq_handler
- **验证**：Pico 固件编译通过 1123.1KB（+17.7KB = PIO-USB 全链入）；
  语法矩阵 95/95；宿主全套 ALL PASS；剩实机 USB 时序联调
- **EDA 载板同步（同日）**：pico 载板新增 USB 键盘 4P 座（GP20=DP/
  GP21=DM 串 22Ω + VBUS 5V + GND；座落 SD 座下方 x=29 避 3V3 走线
  走廊，板框右界 31.5→34.0）；check_eda 五板零交叉 PASS

### 28. 无实机代码项批量关单（2026-10-05 晚，用户方针：代码先写好跑通，实测日后）⭐
- **firewall 持久化（NEXT_STEPS 51 关）**：/opt/etc/firewall.conf CSV 文本，
  init 载入（无文件→默认规则）、增/删/启停即存；点分 IP 互转对称可回读
- **静态 IP 真接线（52 关）**：wifi_apply_static_ip 接 netlib 三件套 +
  dns_add_nameserver（NuttX 12.12 真实 API）；WiFi 板开 NETUTILS_NETLIB/DNSCLIENT
- **网络管理层全量编入（47 代码侧）**：s3/s3n8/cam/c3 开 RETRO_WIFI/NTP
  （此前 network.c 从未进固件）；WiFi 驱动 bring-up 归实机
- **Kconfig 板本/容量（4 关）**：RETRO_DEVKITC_V10→select 树内
  ESP32S3_DEVKITC_1_V10（WS2812=48）；RETRO_FLASH_8MB→ESP32S3_FLASH_8M
- **retro_gpio /dev 后端全开（19a 关）**：五板按芯片能力 DEV_GPIO/ADC/PWM
  （S3 全三件、CAM GPIO+LEDC、C3 GPIO、Pico 全三件；ESP32/C3 芯片层
  无 ADC/PWM lower half 属 NuttX 12.12 现状）
- **复核关单**：16 jslogo（RetroCanvas shim 已备）、35（被 HARDWARE 6.2
  整数分频表取代）、38（词库已重写 Unicode）、39（五板持续编译通过）
- **网络栈编入连环修（同日）**：①select choice 违规（板本/模组 wrapper
  改 appconfig 直写树内 choice，并纠正树内默认 v1.0→本项目 v1.1/GPIO38）；
  ②gethostbyname/getaddrinfo 在目标工具链头环境被宏门控（ntp.c/
  network_utils.c/network.c 三处统一改 getaddrinfo+显式原型）；③esp 三板
  板级 defconfig 无网络（补 NET/IPv4/TCP/UDP/ICMP_SOCKET/NETDB_DNSCLIENT/
  WORKQUEUE 权威符号集，对照树内 c3 wifi defconfig）；④network.c 三个
  cmd_* 与 NSH 内置网络命令撞名（改名 retro_* 库 API）；⑤retro_boot 接
  wifi_auto_connect+ntp_sync_start（此前网络层是库内死代码）；⑥WiFi 硬件
  驱动（ESPRESSIF_WIFI→esp-hal mbedtls 补丁链对不上 v3.6.2 单体源）bring-up
  暂缓登记 NEXT_STEPS 53
- 验证：**五板全编译通过**（pico 1105.4 / c3 1497.3 / cam 1852.0 /
  s3·s3n8 1855.3 KB，网络管理层+DNS+NTP+防火墙全链入）+ 宿主全套
  ALL PASS + 语法矩阵 90/90

### 27. 网络配置文件化 + 持久化状态盘点收口（2026-10-05）⭐
- **盘点结果**：WiFi 凭据原本无任何配置文件（调用方传入）、firewall 规则
  纯 RAM（文档曾失实声称存 Flash）、BLE bond 存 SD 卡（无卡不可用）
- **wifi_conf.[ch]**（纯函数件，宿主 44 检查）：/opt/etc/network.conf
  读写（ssid/password/ip_mode dhcp|static/ip/netmask/gateway/dns，
  点分十进制校验、超长值拒绝、逐级建目录）；SD 同名文件作搬运回退
- **network.c**：wifi_auto_connect（开机读配置自动连）、
  wifi_apply_static_ip（静态 IP 状态记录，netlib ioctl 接线登记
  NEXT_STEPS）、wifi_connect_and_save（连即存）
- **`wifi` NSH 命令**（cmd_wifi_main.c + Makefile builtin）：status/connect
- **BLE bond 迁片上** /opt/var/ble_bond.dat（原 /mnt/sd0）
- **firewall 文档改实况**：RAM 态，持久化登记 NEXT_STEPS 51
- 语法矩阵 90/90（新增两文件）；宿主全套 ALL PASS

### 26. 编辑器许可证边界定稿：nano 出 ROM 转 .rpk，系统默认 vi（2026-10-05）⭐
- **项目所有者裁决**（解除 7.4 与 11.1 第 9 条的张力）：固件 ROM 零 GPL，
  系统 CLI 默认编辑器 = NuttX 内置 vi（CONFIG_SYSTEM_VI，五板 appconfig 已切）
- **nano 转 .rpk 包**：apps-extra/nano/ 模板（README/Makefile/Kconfig/
  control/postinst，LOADABLE 构建，上游 deps/nano + nano_port 垫片复用）；
  build_packages.sh 接入（同步 external/nano -> 收集 ELF -> 打包）；
  CONFIG_RETRO_NANO 默认关（仅实验回编）
- **顺带修复存量 bug**：build_packages.sh 原把 ELF 收集到 package/apps/，
  而 make_package.sh 载荷树约定为 package/data/——原路径打的包载荷为空，
  已改为 data/bin/（ucblogo/nano 两包统一，系统根装 /opt/bin）
- **双安装根（同日，用户定稿）**：pkg_manager 新增 control `Root` 字段
  （system→片上 /opt，缺省/sdcard→SD 卡，非法值拒绝）；包数据库迁址
  /opt/var/lib/rpkg（无 SD 卡时包管理完整可用）；打包器 manifest 改相对
  路径（与安装根解耦）；宿主新增 dual-root 测试；HARDWARE 12.4 定稿
  安卓式分区布局（固件只读区 + littlefs 可写区挂 /opt），分区落地登记
  NEXT_STEPS 17b
- **存储策略三定稿（同日，用户指示）**：①日志默认只串口不落盘（cron.c
  改造：CONFIG_CRON_LOG 显式才写文件）；②crontab/boot.cfg/重启计数迁
  片上 /opt（bootmenu.c、cron.c、nsh_cmds.c 路径同步）；③挂载点弃 /usr
  取 /opt（Unix 语义：附加软件位）
- 2026-10-04 的"全系统一 nano"决定（§17）就此翻转为历史

### 25. 输入优先级原则落地（2026-10-05）⭐
- **原则定稿（REQUIREMENTS 2.2.3）**：USB 键盘（OTG 主机）> 蓝牙 HID > 串口键盘泵；
  芯片具备哪种能力就必须支持哪种（用户 2026-10-05 指示）
- **逐板对照芯片事实**：S3=OTG 主机+BLE+UART 三路；CAM=无 USB 走 BLE（已实现）；
  C3=USB 仅设备模式（接不了普通 USB 键盘）→ BLE 是其唯一 HID 路线（待 NimBLE）；
  Pico=无蓝牙射频+USB 仅设备模式 → 串口泵即正确且唯一
- **common hid_ascii.[ch]**：HID 键码→ASCII 纯函数（base/shift 两档表 + 字母
  大写 + 控制键约定 Enter=CR/BS=0x08/Tab/Esc + 按下沿差分抑制长按重发），
  USB 与未来 BLE 桥共用；宿主 test_hid_ascii 39 检查全绿（ASan/UBSan）
- **cvbs_console_feed_keys()**：HID 源直喂 /dev/cvbscon 输入环，与 UART 泵
  共用同一道 IME 门控；usb_hid.c（S3）键盘报告双路分发（LVGL 回调 + 控制台桥）
- avkbin UART 泵定位修正：C3 为 BLE 就绪前的过渡方案，Pico 为唯一路线
  （SYSTEM 2B / HARDWARE 13.1 同步）

### 24. 拼音输入法两形态落地（2026-10-05）⭐
- **GUI Win95 式**：app_pinyin 输入条（拼音行 / 1.你 2.您 候选按钮 / 中英钮）——
  全 label 补 12px 中文字体、面板三行重排 72px、词库 GB2312 码**原地重写为
  Unicode**（94 单字 + 43 词组含 nihao/shijie）、search 词组并入修复（原搜到
  即丢）；glm53f 六轮迭代验收 pass（最终候选按钮 1.你/2.您 完整可读）
- **CLI CCDOS 式**：cvbs_console_statusbar（末行划归反色条，正文 rows-1 照常
  滚动）+ cvbs_ime（键盘接管 / 选字上屏回显 / Enter 整行回放）+ NSH
  `ime on/off/status` builtin + Ctrl+Space 切换 / Ctrl+Q 退出组合键；
  引擎三 bug 修复（数字选字清行 / 中文 Enter 清行 / 返回值语义混叠）
- 宿主 test_ime 38 检查全绿挂入 run_all；输入环提为通用层

### 23. 控制台字形排版三连修复 + 位置矩阵测试（2026-10-05）⭐
- 用户目视发现标点悬浮；连环修复：垂直居中→基线对齐、双重 py0、
  全角步进 +1（历史 BUG）——BUILD_FIXES 2026-10-05(续) 全记录
- 字形位置矩阵测试（666 检查）：大小写/数字/中文/全半角符号显示+位置
  双验证；cvbs_console_cursor_visible() 新 API；glm53f 逐符号验收 pass

### 22. 字号定稿：全系唯一 12px（2026-10-05）⭐
- 上网考证：中文 Win3.2/95 界面宋体 9pt=12px 点阵 / Win3.x 拉丁 8pt≈11px /
  DOS VGA 9x16 / SFC 汉化 16 宽点阵——早版两档方案（GUI=12/控制台=16）
  当日按用户"嵌入式体积优先"指示收敛为**全系单档 12px**
- lv_font_notosans_sc_16.c 删除；convert_font.sh 只生 12px；
  cvbs_console 网格 12x14（320x240→26x17、640x480→53x34）；
  RETRO_FONT_DEFAULT/CONSOLE、lvgl_font_compat、LV_FONT_DEFAULT 全部
  统一 12px
- glm53f 多模态验收：640/240p 控制台（满屏密度+滚动截断+光标）与
  Win3/wmaker 桌面全 pass；宿主测试 ALL PASS、语法矩阵 85/85

## 代码统计

| 类别 | 数量 | 总行数 |
|------|------|--------|
| 共享驱动模块 | 11 个 | ~7,000 行 |
| ESP32-S3 专用 | 8 个 | ~3,200 行 |
| ESP32-CAM 专用 | 7 个 | ~2,200 行 |
| ESP32-C3 / RP2040 专用 | 2 个 CVBS 发射器 + 板级 | ~1,000 行 |
| LVGL 应用 | 12+ 个 | ~8,000 行 |
| 系统集成 | 多个 | ~1,100 行 |
| 架构相关 | 1 个 | ~229 行 |
| **总计** | **约 60+ 个模块** | **~42,400 行**（2026-10-05） |

---

## 源码文件清单

```
src/nuttx/common/                    # 共享代码（14个文件）
+-- bootmenu.c                       # 启动菜单
+-- script_engines.c                 # 脚本引擎集成
+-- network_utils.c                  # curl/wget
+-- apps/system/nsh_cmds.c           # NSH 命令
+-- driver/
    +-- watchdog.c                   # 看门狗（目标共享接口）
    +-- firewall.c                   # 防火墙
    +-- memmon.c                     # 内存监控
    +-- network.c                    # WiFi/网络
    +-- ntp.c                        # NTP 对时
    +-- cron.c                       # 定时任务
    +-- drv_rtc.c                    # RTC 驱动
    +-- drv_pinyin.c                 # 拼音输入驱动
    +-- drv_player.c                 # 媒体播放器驱动
    +-- drv_recorder.c               # 录音机驱动
    +-- drv_sqlite.c                 # SQLite 驱动

src/nuttx/esp32s3/                   # ESP32-S3 目标
+-- esp32s3_retro.c                  # 双核主入口
+-- Kconfig                          # menuconfig
+-- board/
|   +-- board.c                      # 板级初始化
|   +-- board.h                      # GPIO/外设定义
+-- chip/
|   +-- esp32s3.h                    # 寄存器定义
|   +-- xt_utils.h                   # 工具函数头文件
|   +-- startup.c                    # 启动代码
+-- driver/
|   +-- usb_hid.c                    # USB HID 键鼠
|   +-- watchdog.c                   # 看门狗
|   +-- cvbs/drv_cvbs.c             # I2S CVBS 显示驱动
|   +-- fsk/drv_fsk.c               # FSK 磁带驱动
|   +-- audio/drv_audio.c           # I2S 音频驱动
+-- include/                         # deps/ bug 的覆盖头文件

src/nuttx/esp32/                     # ESP32-CAM 目标
+-- esp32_retro.c                    # 双核主入口
+-- Kconfig.esp32                    # menuconfig
+-- board/
|   +-- board.c                      # 板级初始化
|   +-- board.h                      # GPIO/外设定义
+-- chip/
|   +-- esp32.h                      # 寄存器定义
+-- driver/
|   +-- ble_hid.c                    # BLE HID 驱动 (NimBLE)
|   +-- ble_hid.h                    # BLE HID 头文件
|   +-- ble_storage.c                 # BLE Bond 存储
|   +-- ble_nsh.c                    # BLE NSH 命令
|   +-- ble_pair_ui.c                 # BLE 配对 UI
|   +-- watchdog.c                   # 看门狗
|   +-- cvbs/drv_cvbs_dac.c         # DAC CVBS 显示驱动
|   +-- fsk/drv_fsk.c               # FSK 磁带驱动
|   +-- audio/drv_audio_dac.c       # DAC 音频驱动
+-- include/                         # 覆盖头文件

src/nuttx/esp32c3/                   # 合宙 ESP32-C3 目标（CLI，RISC-V）
+-- esp32c3_retro.c                  # 单核主入口
+-- Kconfig.esp32c3                  # menuconfig
+-- board/
|   +-- board.c                      # 板级初始化
|   +-- board.h                      # GPIO/外设定义
|   +-- hw_esp32c3_luatos.h          # 硬件档案（合宙两款核心板）
+-- driver/
    +-- cvbs/drv_cvbs_pdm.c          # PDM CVBS（I2S0 raw + GDMA）

src/nuttx/rp2040/                    # Raspberry Pi Pico 目标（CLI，ARM）
+-- rp2040_retro.c                   # 双核主入口（Core0=程序/Core1=媒体）
+-- Kconfig.rp2040                   # menuconfig
+-- board/
|   +-- board.c                      # 板级初始化
|   +-- board.h                      # GPIO/外设定义
|   +-- hw_rp2040_pico.h             # 硬件档案（Pico 40-pin）
+-- driver/
    +-- cvbs/drv_cvbs_pio.c          # PIO CVBS（SM0 + DMA，GP12-15）

src/lvgl/                            # LVGL 应用（图形档目标共享）
+-- i18n.c                           # 多语种框架
+-- i18n.h                           # 多语种头文件
+-- retro_ui.c                       # UI 胶水层
+-- retro_win3_styles.h              # Win3.2 样式常量
+-- lvgl_app.c                       # LVGL 初始化
+-- lv_port_disp.c                   # 显示端口
+-- lv_port_indev.c                  # 输入设备端口
+-- app/
|   +-- desktop.c                    # LVGL 桌面 (1136行)
|   +-- app_editor.c                 # 记事本编辑器 (636行)
|   +-- app_browser.c                # 简易浏览器 (564行)
|   +-- app_terminal.c               # 终端模拟器 (673行)
|   +-- app_pinyin.c                 # 拼音输入法 (557行)
|   +-- app_player.c                 # 媒体播放器 (742行)
|   +-- app_recorder.c               # 录音机 (705行)
|   +-- app_sqlite.c                 # SQLite 工具 (1314行)
|   +-- logo/                        # Logo 海龟画图 (~400行)
+-- modules/
|   +-- retro_ui_bas.c              # BASIC UI适配
|   +-- retro_ui_js.c               # JS UI适配
+-- audio/
|   +-- wav_decoder.c               # WAV 解码器
+-- fonts/
    +-- pinyin_ime.c                # 拼音输入法词库
+-- assets/icons/                   # 32x32 PNG 图标

src/arch/xtensa/src/common/
+-- xtensa_cpuinfo.c                # CPU 信息

tools/fonts/
+-- NotoSansSC-Medium.otf           # 字体源文件
# 全系唯一 12px 点阵字库（src/lvgl/fonts/lv_font_notosans_sc_12.c）由
# scripts/convert_font.sh 从上表源文件生成；16px 档已于 2026-10-05 废除
```

---

## 需求文档对应

| 需求文档章节 | 实现章节 | 状态 |
|-------------|---------|------|
| 2.1 核心主控 | 系统架构（五板目标） | 完成 |
| 2.2 RTC 时钟 | common/drv_rtc.c | 完成 |
| 2.3 显示模块 | cvbs 驱动（各目标） | 完成 |
| 2.4 音频模块 | audio 驱动（各目标） + fsk | 完成 |
| 2.5 存储模块 | board.c（SDIO/SPI） | 完成 |
| 2.6 外设模块 | usb_hid.c / ble_hid.c | 完成 |
| 3.1 操作系统 | nuttx/ | 完成 |
| 3.2 图形界面 | desktop.c | 完成 |
| 3.3 音频软件 | audio 驱动 | 完成 |
| 3.4 网络系统 | network.c + network_utils.c | 完成 |
| 3.5 时间与定时 | ntp.c + cron.c + drv_rtc.c | 完成 |
| 3.6 脚本语言 | script_engines.c | 完成 |
| 3.7 中文字符 | LVGL 字体支持 | 完成 |
| 3.8 外设驱动 | 各 driver/ 文件 | 完成 |
| 3.9 系统启动 | bootmenu.c | 完成 |
| 3.10 看门狗 | watchdog.c + memmon.c | 完成 |
| 4.x 性能需求 | 双核分工 + 内存管理 | 完成 |
| 5.x 安全需求 | 防火墙 + 安全模式 | 完成 |

---

_最后更新: 2026-10-05（项目更名 retro-ws：五板多架构定位全面修订）_
