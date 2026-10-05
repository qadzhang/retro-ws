/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * 复古工作站 Retro WS - 下一步工作计划
 * Retro Workstation (retro-ws) - Next Steps
 */

## 更新日期 / Update Date

2026-10-05

---

## 当前阶段

**五板固件全部编译通过（2026-10-05），待实机烧录验证**：
s3 / s3n8（首选板 ESP32-S3 DevKitC-1 N16R8/N8R8）、cam（兼容目标）、
c3（合宙核心板 CLI 档）、pico（本地教学终端 CLI 档）。

2026-10-04 完成硬件规格核查与修正（详见 HARDWARE.md 变更记录）：
- 每板新增硬件档案文件（`hw_esp32s3_devkitc.h` / `hw_esp32cam_aithinker.h`）
- 修正 S3 用户按键（GPIO35/36 -> GPIO7/8，R8 八线 PSRAM 占用）
- 修正 S3 状态 LED 为单颗 WS2812（v1.0=GPIO48 / v1.1=GPIO38）
- 修正 CAM SD 卡 SPI 引脚（CS=13/MOSI=15/MISO=2/CLK=14）
- 分辨率定档：320x240 控制台 / 640x480 常规 / 1024x768 最高（实验）
- 依赖版本核查（结论：维持 NuttX 12.12.0 + ESP-IDF v5.5.x + LVGL 9.5.0，见 DEPENDENCIES.md）

源代码结构：
- `src/nuttx/common/` - 共享代码（驱动/脚本引擎/包管理器/cvbs_console/nano 移植层）
- `src/nuttx/esp32s3/` - ESP32-S3 目标代码（开发模板，s3/s3n8 共用）
- `src/nuttx/esp32/` - ESP32-CAM (ESP32) 目标代码（兼容）
- `src/nuttx/esp32c3/` - 合宙 ESP32-C3 目标代码（CLI 档，RISC-V）
- `src/nuttx/rp2040/` - Raspberry Pi Pico 目标代码（CLI 档，ARM）

编译脚本：
- `scripts/firmware/build_firmware.sh` - 五板统一构建入口（s3/s3n8/cam/c3/pico/all）
- `scripts/esp32s3/` / `scripts/esp32cam/` / `scripts/esp32c3/` - 旧三板单独入口
- `scripts/` - 共享脚本（download_deps.sh, setup_tools.sh, verify.sh, convert_font.sh）

---

## 待办事项

### 高优先级（硬件适配）

| 序号 | 任务 | 目标 | 说明 |
|------|------|------|------|
| ~~1~~ | ~~ESP32-S3 编译验证~~ 已完成(2026-10-04) | 统一入口 `scripts/firmware/build_firmware.sh s3`；产物 dist/firmware/s3（203.7KB/16MB） |
| ~~2~~ | ~~ESP32-CAM 编译验证~~ 已完成(2026-10-04) | `build_firmware.sh cam`；200.9KB/4MB |
| ~~2a~~ | ~~ESP32-C3 编译验证~~ 已完成(2026-10-04) | `build_firmware.sh c3`；236.5KB/4MB |
| 2b | C3 控制台双款验证 | esp32c3 | 经典款 UART0 (GPIO20/21) 与简约款原生 USB 各烧一遍 |
| ~~3~~ | ~~WS2812 状态 LED RMT 驱动~~ 已完成(2026-10-04 深夜) | esp32s3 | ws2812_rmt.c 经 /dev/rmt0（CONFIG_RMT+RMTCHAR+ESP_RMT）硬件驱动；宿主 353 测试+变异 100%；剩实机点灯验证 |
| 4 | Kconfig 板本/容量选项 | esp32s3 | 新增 `CONFIG_RETRO_DEVKITC_V10`（LED 引脚选择）与 `CONFIG_RETRO_FLASH_8MB`（N8R8 分区表） |
| 5 | QEMU 模拟运行 | esp32s3 | Espressif 专用 QEMU |
| 6 | ESP32-S3 开发板烧录（N16R8/N8R8） | esp32s3 | I2S 驱动实机验证 |
| 7 | 验证串口输出和 NSH | 全板 | 115200 8N1，S3 用 UART0 GPIO43/44（JTAG 已让渡给音频） |
| 8 | 验证 GUI 启动 | s3/cam | CVBS 输出到显示器 |
| 9 | 验证双核分工 | s3/cam/pico | 全局规范：CPU0=程序核 / CPU1=媒体核（图形/视频/音频/文件IO） |
| 10 | S3 多电平 CVBS 电阻网络验证 | esp32s3 | GPIO2 + 预留 GPIO15/16/17 组成 R-2R ladder（见 HARDWARE.md 6.5 节） |

### 中优先级（功能完善）

| 序号 | 任务 | 说明 |
|------|------|------|
| ~~11~~ | ~~320x240 控制台模式实现~~ 已完成(2026-10-04 晚) | cvbs_console 240p 逐行上 AV 屏（12x14 网格），glm53f 验收 pass；剩实机 CRT 抽验 |
| 12 | 1024x768 实验模式评估 | 非标准 overspec 时序，先在采集卡上验证可行性 |
| 13 | CAM 摄像头模式（可选功能） | OV2640 与 CVBS/音频互斥，按需切换启用 |
| 14 | Berry 编译验证 | `RETRO_SCRIPT_BERRY=y`：核对 be_getindex/be_loadfile 与 nuttx-apps 固定 berry 版本的 API 一致性 |
| 15 | CPython 编译验证 | `RETRO_SCRIPT_PYTHON=y`（仅 S3 N16R8）：配置 ROMFS 标准库镜像（`esp32s3-devkit:python` 配置可参考），核对 `<Python.h>` 包含路径 |
| 16 | jslogo 集成验证 | 下载 deps/jslogo 后：把 JS 源码拷入 SD `/sdcard/scripts/logo/lib/`，将 jslogo 前端 canvas 获取处改为全局 RetroCanvas（预计一处改动） |
| 17 | binfmt/LOADABLE 链路验证 | defconfig 开 `CONFIG_ELF`+`CONFIG_BUILD_LOADABLE`+`CONFIG_EXTERNAL_UCBLOGO`，先跑通 NuttX 官方 hello ELF 再跑 ucblogo.elf |
| 18 | .rpk 包管理器实机验证 | `pkg install` 全链路：ustar 解析/CRC/维护脚本（需 CONFIG_SYSTEM 与 NSH 脚本支持）；格式契约已经主机端模拟验证通过 |
| 19 | UCBLogo NuttX 适配 | 核对 src/*.c 文件清单、终端 IO（-termios/textscreen 依赖需换 NSH stdin/stdout）、Makefile 通配符改显式列表 |
| ~~19a~~ | ~~retro_gpio 后端验证~~ 已完成(2026-10-04) | ioctl 已按真实头修正（ioexpander/gpio.h，WRITE=0/1、READ=bool*、SETPINTYPE=枚举）；占用拦截 29 项宿主测试全绿；剩：defconfig 开 CONFIG_DEV_GPIO/ADC/PWM 实机验证 |
| ~~19b~~ | ~~WindowMaker 外壳编译验证~~ 已完成(2026-10-04) | 真实 LVGL9.5 头语法矩阵 + 无头模拟渲染（两种外壳截图 + glm 视觉审查）通过；剩实机 Dock/根菜单操作验证 |
| ~~20~~ | ~~CLI `ime` 命令~~ 已完成(2026-10-05) | CCDOS 式：ime on 底部常驻反色条（cvbs_ime.c）+ Ctrl+Space 切换 + Ctrl+Q 退出；宿主 38 检查 + glm53f 验收 pass |
| 20a | GUI 词组完整上屏 | 候选机制现为单字（词组取首字）；升级 candidates 为码点串以整词上屏（你好）；CLI 侧已整词 |
| 21 | GUI 输入法系统服务化 | 文本框焦点驱动自动唤起候选条（app_pinyin.c 与 desktop.c 集成） |
| ~~22~~ | ~~LVGL PC 模拟器~~ 已完成(2026-10-04) | tools/sim/build.sh：无头 LVGL9.5 + 真实 desktop/wmaker/i18n 渲染出图；含 cvbs_pipeline 全链路波形验证 |
| 23 | 完善 curl/wget HTTP 客户端 | 支持 HTTPS、断点续传 |
| 24 | SSH 客户端 libssh2 集成 | 需要交叉编译 libssh2 |

### 低优先级（功能增强 / 版本升级）

| 序号 | 任务 | 说明 |
|------|------|------|
| 25 | 遗留函数级 5W1H 注释迁移 | 全库文件级已完成（2026-10-04，90 文件）；存量函数注释按"改到哪补到哪"渐进补 5W1H（AGENTS.md 4.0） |
| 26 | 文泉驿字库下载 | GitHub 连接不稳定 |
| 27 | 添加开机音乐 | DAC/I2S 音频输出 |
| 28 | 窗口拖拽和缩放 | LVGL 桌面增强 |
| 29 | 截图功能 | LVGL 屏幕保存 |
| 30 | 脚本管理器 UI | 图形化脚本运行界面 |
| 31 | LVGL 9.5 -> 9.6 升级评估 | 同 v9 API 线，风险低，实机验证后再做 |
| 32 | NuttX 12.12 -> 13.0 升级评估 | 需重做 esp-hal-3rdparty 配对与补丁 workaround |
| 33 | ESP-IDF v5.5 -> v6.x 升级评估 | v5.5 LTS 至 2028-01，无迫切性 |
| 34 | esp32s3.h I2S 寄存器模型对表 | 现有偏移集与 deps esp32s3_i2s.h（INT 块 0x0C-0x18、TX_CLKM 0x34、走 GDMA）存在两说，硬件联调时以 TRM 终裁 |
| 35 | I2S 13.5MHz 采样实现 | S3 GDMA 分频能否精确到 864 样本/行×15625Hz；CAM 为 APB/6≈13.33MHz（1.25% 偏差），不同步则改 M/D 小数分频 |
| 36 | BLE NimBLE 移植（键鼠 HID） | S3/CAM ble_* 现封存于 CONFIG_RETRO_BLE_STACK_IDF（IDF 路线不适用于 NuttX）；NimBLE 主机 + HID-IN 是正路。**输入优先级原则（2026-10-05）：USB > 蓝牙 > 串口**——S3/CAM/C3 都要有 BLE HID（C3 芯片有 BLE 5 且无 USB 主机，BLE 是其唯一 HID 路线；Pico 无蓝牙不适用） |
| 37 | FSK 完整成帧 | TX 已接 audio_play_pcm 硬件出声（2026-10-04 深夜）；RX 仍仅载波监测——起止位检测/字节组装待做（需 ADC DMA 输入通道） |
| 38 | 拼音 GB2312→Unicode 映射表 | 候选字当前按码点显示，需码表才能出正确字形 |
| 39 | 构建集成 include 路径 | 固件构建需为 my_basic/duktape/berry 补 -I（代码已 __has_include 双路径兼容） |
| 40 | 崩溃计数持久化 | 看门狗重启计数需写 Flash（RTC 内存或易失文件），安全模式逻辑才真正生效 |
| 41 | Pico 文件 IO 服务实义化 | rp2040 Core1 服务当前为心跳占位；接 SD/SPI0 驱动后挂真实作业 |
| 42 | 五板实机烧录验证 | esptool(ESP 系)/UF2 拖入(Pico)；验 NSH 控制台、双核日志、pkg 命令 |
| 43 | GUI/字库入固件 | 当前五配置为 CLI 档（含拼音 CLI）；S3 加 LVGL+notosans_sc_16(911KB) 的 GUI 档 appconfig |
| 44 | WiFi/BLE 档 | RETRO_WIFI/BLE 默认关（体积优先）；开启需重验 ROM |
| 45 | C3/Pico CVBS 硬件钩子 | **已完成（2026-10-04 晚）**：C3=I2S0 PDM raw 单脚 sigma-delta、Pico=PIO 4-bit + DMA 逐行（Core1 生成） |
| 46 | NSH 全输出上屏 | **已完成（2026-10-04 晚）**：/dev/cvbscon + UART 键盘泵 + NSH_ALTCONDEV（C3/Pico） |
| 47 | C3 网络 + ping/telnetd | C3 有 WiFi（未开 NET 栈）；开启后加 ping/telnetd/w3m |
| 48 | 浏览器（Links 2.30 移植） | nuttx-apps 无现成浏览器；需移植（webclient 库已有；w3m 弃议）|
| 49 | GPIO 兼容层扩总线 | **已完成（2026-10-04 晚）**：retro_bus（硬后端探测 + 软总线回退 + 三引擎绑定） |
| 50 | 脚本 XIP（ROM 直跑） | **已完成（2026-10-04 晚）**：mkromfs.py + /rom/scripts + script 命令（长度型接口直吃 Flash 指针）|

| 51 | 真机联调（示波器） | AV 各板首板联调：S3 LCD PCLK 13.3333MHz、C3 PDM 位率/位序、Pico SM 时钟校准（代码已按 TRM 推导，标注见各驱动头注释）|
| 52 | C3/Pico 硬件 I2C/SPI 驱动 | C3=NuttX 无 esp32c3 i2c/spi 驱动（现走位摆软总线）；Pico=rp2040_i2c 已有可接 /dev/i2c0（注：S3/CAM 已于 2026-10-04 深夜开硬件 I2C0，见 HARDWARE.md 13.1） |
| 54 | 五板载板打样验证 | eda/ 立创EDA 工程已生成（零交叉校验）；打开后补铺铜/泪滴/DRC 收尾再下单；合宙 C3 需先在核心板焊公排 |
| 55a | 240p 实机 CRT 抽验 12px | cvbs_console 240p 档（26x17 网格）的 12px 中文在宿主验收为可读下限——实机 CRT TV 上抽验密笔画字（警/编类），发糊则评估该档行距/字重微调 |
| 55 | RMT/I2C 实机验证 | s3/s3n8 appconfig 已开 RMT+I2C0；上机验 /dev/rmt0 点灯与 /dev/i2c0 探测 RTC |
| 53 | nano 增强 | nanorc 语法定义（现裁剪 ENABLE_COLOR=0）、bracketed paste、undo 加深测试 |

---

## 验证清单

### 系统启动
- [ ] NuttX 内核正常启动（ESP32-S3）
- [ ] NuttX 内核正常启动（ESP32-CAM）
- [ ] NSH 命令行可输入
- [ ] `help` 命令显示所有内置命令
- [ ] `free` 显示内存状态
- [ ] `ps` 显示任务列表

### ESP32-S3 专用
- [ ] USB HID 键鼠识别
- [ ] BLE HID 键鼠识别
- [ ] LCD_CAM CVBS 输出到显示器（GPIO2/15/16/17 4-bit R-2R）
- [ ] I2S 外部 DAC 音频输出（GPIO40/41/42）
- [ ] ADC 音频输入（GPIO1）
- [ ] SPI SD 卡识别

### ESP32-CAM 专用
- [x] BLE HID 键鼠驱动（NimBLE 已实现）
- [ ] BLE HID 键鼠连接（需测试）
- [ ] 内置 DAC CVBS 输出到显示器（GPIO25）
- [ ] 内置 DAC 音频输出（GPIO26）
- [ ] ADC 音频输入（GPIO34）
- [ ] SPI 模式 SD 卡识别

### ESP32-C3 专用
- [ ] 经典款 UART0 控制台输出（GPIO21 TX / GPIO20 RX，CH343）
- [ ] 简约款原生 USB 控制台与烧录（GPIO18/19，Kconfig 切 NATIVEUSB）
- [ ] `pkg install` 安装 riscv 架构包（xtensa 包应被拒装）
- [ ] retro_gpio 教学 GPIO（GPIO1/GPIO10 可用，占用脚报错）

### 网络功能
- [ ] WiFi 连接成功（需要配置 SSID）
- [ ] `ifconfig` 显示 IP 地址
- [ ] `ping baidu.com` 网络连通
- [ ] `ntp status` NTP 对时成功
- [ ] `curl http://example.com` HTTP 请求

### 图形界面
- [ ] CVBS 输出到显示器正常显示
- [ ] LVGL 桌面渲染正常
- [ ] 任务栏显示时间和日期
- [ ] 窗口可以打开和关闭
- [ ] 键盘鼠标操作正常

### 音频
- [ ] 音频输出声音
- [ ] FSK 磁带录制/播放
- [ ] 音量调节正常

### 存储
- [ ] SD 卡识别和挂载
- [ ] `/sdcard` 可读写
- [ ] cron 日志写入正常

### 看门狗
- [ ] `sysinfo` 显示看门狗状态
- [ ] 手动触发看门狗复位

### 脚本引擎
- [ ] `script engine bas` 切换到 my-basic
- [ ] `script run /sdcard/scripts/basic/test.bas` 执行脚本
- [ ] `script engine js` 切换到 Duktape
- [ ] `script run /sdcard/scripts/js/test.js` 执行 JS 脚本

### 防火墙
- [ ] `fw status` 显示防火墙状态
- [ ] 默认拒绝入站连接
- [ ] 允许出站连接

### 定时任务
- [ ] `cron list` 显示定时任务
- [ ] `cron add` 添加新任务
- [ ] 定时执行符合预期

---

## 工具链安装步骤

### 自动化安装（推荐）

```bash
# 使用 download_deps.sh 自动下载所有依赖
./scripts/download_deps.sh

# 激活工具链
source scripts/setup_tools.sh
```

---

## 编译脚本说明

| 脚本 | 用途 |
|------|------|
| `./scripts/download_deps.sh` | 下载/验证开源依赖（包括工具链） |
| `./scripts/setup_tools.sh` | 激活 ESP-IDF 工具链环境 |
| `./scripts/verify.sh` | 验证项目完整性 |
| `./scripts/convert_font.sh` | 字体转换工具 |
| `./scripts/esp32s3/build.sh` | ESP32-S3 编译和烧录 |
| `./scripts/esp32s3/nuttx_build.sh` | ESP32-S3 NuttX 编译 |
| `./scripts/esp32cam/build.sh` | ESP32-CAM 编译和烧录 |
| `./scripts/esp32cam/nuttx_build.sh` | ESP32-CAM NuttX 编译 |

### 编译命令

```bash
# ========== ESP32-S3 ==========
cd scripts/esp32s3
./nuttx_build.sh defconfig    # 默认配置
./build.sh nuttx              # 编译
./build.sh flash              # 烧录

# ========== ESP32-CAM ==========
cd scripts/esp32cam
./nuttx_build.sh defconfig    # 默认配置
./build.sh nuttx              # 编译
./build.sh flash              # 烧录
```

---

## 已知问题

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| GitHub 连接不稳定 | 网络限制 | 使用代理或镜像 |
| 文泉驿字库下载失败 | GitHub 连接超时 | 稍后重试或手动下载 |
| Ninja 未安装 | 系统依赖缺失 | `sudo apt install ninja-build` |
| QEMU 不支持 ESP32 | 标准 QEMU 无 ESP 支持 | 安装 Espressif 专用 QEMU |
| kconfiglib 版本冲突 | Python 包版本不兼容 | 使用独立虚拟环境 |

---

## 开发板测试计划

### 第一阶段：最小系统验证（五板）
1. 烧录空白 NuttX 固件
2. 验证串口输出
3. 验证 NSH 命令
4. 验证 `reboot` 重启

### 第二阶段：硬件驱动
1. 验证 GPIO 输出（LED 闪烁）
2. 验证 GPIO 输入（按键）
3. 验证 I2C 总线（RTC）
4. 验证 SD 卡识别
5. 验证 WiFi 连接（S3/CAM/C3；Pico 无网络）

### 第三阶段：图形界面（图形档 S3/CAM；CLI 档验证 cvbs_console 上屏）
1. 验证 CVBS 输出
2. 验证 LVGL 渲染
3. 验证显示分辨率切换
4. 验证键鼠操作

### 第四阶段：完整功能
1. 验证所有 NSH 命令
2. 验证网络工具
3. 验证脚本引擎
4. 验证定时任务
5. 验证看门狗重启

---

## 里程碑

| 阶段 | 目标 | 状态 | 日期 |
|------|------|------|------|
| M1 | 代码编写完成 | 完成 | 2026-03-29 |
| M2 | 依赖下载完成 | 完成 | 2026-03-29 |
| M3 | 工具链安装 | 完成 | 2026-04-01 |
| M4 | NuttX 编译通过 | 完成 | 2026-04-01 |
| M5 | 多目标架构重构 | 完成 | 2026-04-01 |
| M6 | ESP32-CAM 编译验证 | 待开始 | - |
| M7 | ESP32-S3 编译验证 | 待开始 | - |
| M8 | 开发板烧录成功 | 待开始 | - |
| M9 | 实机测试全部通过 | 待开始 | - |

---

_最后更新: 2026-04-02_
