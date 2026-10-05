/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

# ESP32 复古图形工作站 - 编码规范
# ESP32 Retro Graphics Workstation - Coding Standards

---

## 一、代码管理 / Code Management

### 1.1 版本控制 / Version Control
- 使用 Git 管理代码，托管于 GitHub / Gitee
- 分支策略：`main`（稳定）、`dev`（开发）、`feature/*`（功能分支）
- 提交信息格式：`[type] description`（type: feat/fix/docs/style/refactor/test）

### 1.2 ⚠️ 依赖目录保护规则 / Dependency Directory Protection

**`deps/` 目录下的文件严禁随意删除或移动。**

- `deps/` 包含所有第三方依赖（NuttX、ESP-IDF、LVGL、工具链等）
- 下载脚本 `download_deps.sh` 会将 tarball 缓存到 `deps/download/`
- 如需删除依赖，必须先确认不再使用，且不影响其他目标
- **违反此规则将导致依赖丢失、编译失败**

**为什么重要：**
- 网络不通时无法重新下载
- 不同版本的依赖可能不兼容
- 重新下载耗时（大型仓库数GB）

### 1.2 编码风格 / Coding Style
- **C 语言**：遵循 Linux Kernel 风格（K&R 括号换行）
- **Python**：遵循 PEP 8
- **缩进**：4 空格（禁用 Tab）/ 4 Spaces (No Tab)
- **文件编码**：UTF-8 无 BOM / UTF-8 without BOM

---

## 二、开发环境 / Development Environment

### 2.1 系统要求 / System Requirements
- Linux（Ubuntu 22.04+）或 macOS 或 WSL2
- Python 3.10+
- Git
- CMake 3.20+
- Ninja Build
- GCC（用于 NuttX 交叉编译）

### 2.2 安装步骤 / Installation

```bash
# 1. 下载所有依赖
./scripts/download_deps.sh

# 2. 激活工具链
source scripts/setup_tools.sh

# 3a. 编译 ESP32-S3
cd scripts/esp32s3 && ./build.sh nuttx

# 3b. 编译 ESP32-CAM
cd scripts/esp32cam && ./build.sh nuttx
```

---

## 三、NuttX 配置与编译 / NuttX Configuration

### 3.1 编译 NuttX

```bash
# ESP32-S3 目标
cd scripts/esp32s3 && ./nuttx_build.sh build

# ESP32-CAM 目标
cd scripts/esp32cam && ./nuttx_build.sh build
```

### 3.2 烧录固件 / Flashing

```bash
# ESP32-S3
cd scripts/esp32s3 && ./build.sh flash

# ESP32-CAM
cd scripts/esp32cam && ./build.sh flash
```

---

## 四、多目标架构 / Multi-Target Architecture

### 4.1 目录结构

```
src/nuttx/
+-- common/              # 共享代码（14个文件）
|   +-- bootmenu.c       # 启动菜单
|   +-- script_engines.c # 脚本引擎集成
|   +-- network_utils.c  # curl/wget
|   +-- apps/system/     # NSH 命令
|   +-- driver/          # 共享驱动
|       +-- network.c, ntp.c, cron.c, firewall.c, memmon.c
|       +-- drv_rtc.c, drv_pinyin.c, drv_player.c, drv_recorder.c, drv_sqlite.c
|       +-- audio/, cvbs/, fsk/, hid/  # 目标特定（占位目录）
+-- esp32s3/             # ESP32-S3 目标
|   +-- esp32s3_retro.c  # 主入口
|   +-- driver/          # 专用驱动
|   |   +-- cvbs/drv_cvbs.c       # I2S CVBS
|   |   +-- audio/drv_audio.c     # I2S 音频
|   |   +-- usb_hid.c             # USB HID
|   |   +-- fsk/drv_fsk.c         # FSK
|   |   +-- watchdog.c            # 看门狗
|   +-- board/, chip/, include/
+-- esp32/               # ESP32-CAM 目标
    +-- esp32_retro.c    # 主入口
    +-- driver/          # 专用驱动
    |   +-- cvbs/drv_cvbs_dac.c    # DAC CVBS
    |   +-- audio/drv_audio_dac.c  # DAC 音频
    |   +-- ble_hid.c              # BLE HID
    |   +-- fsk/drv_fsk.c          # FSK
    |   +-- watchdog.c             # 看门狗
    +-- board/, chip/, include/
```

### 4.2 目标差异

| 特性 | ESP32-S3 | ESP32-CAM |
|------|----------|-----------|
| CVBS 输出 | I2S bitbang -> GPIO2 | 内置 DAC -> GPIO25 |
| 音频输出 | I2S -> GPIO40/41/42 | 内置 DAC -> GPIO26 |
| 音频输入 | ADC -> GPIO1 | ADC -> GPIO34 |
| 键鼠输入 | USB HID + BLE HID | BLE HID |
| 蓝牙 LED | WS2812 RGB（v1.1=GPIO38 / v1.0=GPIO48，需 RMT 驱动） | GPIO4 |
| SD 卡 | SPI | SPI |

---

## 五、C 语言编码规范 / C Language Standards

### 5.1 文件结构 / File Structure

```c
/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: driver_xxx.c
 * 描述: XXX 驱动
 * 作者: ESP32 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */

#include <nuttx/config.h>
#include <stdio.h>
#include <string.h>

/* === 全局变量 === */
static const char *g_driver_name = "esp32-xxx";
static bool g_initialized = false;

/* === 函数声明 === */
static int xxx_init(void);
static int xxx_read(FAR char *buf, size_t len);

/* === 驱动操作函数 === */
FAR struct file_operations_vtable g_xxx_fops = {
    .read    = xxx_read,
    .write   = NULL,
    .seek    = NULL,
    .ioctl   = xxx_ioctl,
    .open    = xxx_open,
    .close   = xxx_close,
};

/* === 公开函数 === */
int esp32_xxx_init(void)
{
    /* 初始化代码 */
    g_initialized = true;
    return OK;
}
```

### 5.2 命名规范 / Naming Conventions

| 类型 | 风格 | 示例 |
|------|------|------|
| 函数 | snake_case | `esp32_xxx_init()` |
| 变量 | snake_case | `g_initialized` |
| 常量 | UPPER_SNAKE | `CONFIG_ESP32_XXX` |
| 结构体 | snake_case_t | `xxx_config_s` |
| 枚举成员 | UPPER_SNAKE | `MODE_NORMAL` |
| 宏 | UPPER_SNAKE | `#define XXX_MAX_SIZE 1024` |
| 文件 | snake_case.c/h | `drv_cvbs.c` |

### 5.3 头文件规范 / Header File Standards

```c
#ifndef __DRIVER_XXX_H
#define __DRIVER_XXX_H

#include <nuttx/compiler.h>
#include <stdint.h>
#include <stdbool.h>

/* === 公开类型 === */
struct xxx_config_s {
    uint32_t baudrate;
    uint8_t  parity;
    bool     flow_control;
};

/* === 公开函数 === */
int esp32_xxx_init(void);
int esp32_xxx_deinit(void);
int esp32_xxx_write(FAR const uint8_t *buf, size_t len);

#endif /* __DRIVER_XXX_H */
```

### 5.4 代码即文档 / Code as Documentation
- 所有源代码文件必须做到代码即文档
- 函数实现本身就是最好的注释
- 避免冗余注释，保持代码简洁清晰
- 变量命名要自解释，见名知意

### 5.5 代码风格 / Code Style (Linux Kernel)
- 括号换行（K&R 风格）
- 函数返回类型与函数名不在同一行
```c
int module_function(int param)
{
    int ret;

    if (param == NULL)
        return -EINVAL;

    ret = do_something();
    if (ret < 0)
        return ret;

    return OK;
}
```

### 5.6 注释规范 / Comment Standards

#### 5.6.0 5W1H 强制格式（2026-10-04 起）

**所有代码、模块、伪代码的注释按 5W1H 六要素书写**（与 AGENTS.md 4.0 一致）：

文件/模块级头注释（紧跟 SPDX 之后，每个 .c/.h 必须有）：

```c
/*
 * <文件名> - <一句话标题>
 *
 * WHAT : 该文件/模块是什么、做什么
 * WHY  : 为什么存在，解决什么问题，谁依赖它
 * WHO  : 维护者（ESP32-S3 Retro Project Team，详见 git log）
 * WHERE: 所在路径及上下游模块（上层文档 SYSTEM.md）
 * WHEN : 初版日期与最近标准化/重大修改日期
 * HOW  : 实现机制一句话（关键数据结构/外设/算法/调用链）
 */
```

函数级注释（公共函数必须）：

```c
/*
 * WHAT : 函数做什么
 * WHY  : 设计意图/约束（可选，一行）
 * HOW  : 关键实现要点 + 参数/返回值说明
 *   param1 - ...
 *   返回   - OK / -EINVAL / ...
 */
```

伪代码/算法注释同样按六要素（算法用途 WHY、输入输 WHAT、步骤 HOW、
适用场景 WHEN/WHERE）。禁止空洞占位（"WHY: 无"）——写不出 WHY 说明
模块该删。全库文件级迁移已完成（2026-10-04，90 文件）；遗留函数级
注释按"改到哪补到哪"渐进迁移（见 NEXT_STEPS.md）。

#### 5.6.1 传统函数注释模板（5W1H 的展开形式）

```c
/*
 * 功能描述:
 *   这个函数负责初始化 XXX 驱动。
 *
 * 参数:
 *   config - XXX 配置结构体指针
 *
 * 返回值:
 *   OK (0)    - 初始化成功
 *   -EINVAL   - 参数错误
 *   -ENOMEM   - 内存分配失败
 *
 * 注意:
 *   - 调用前确保 GPIO 时钟已使能
 *   - 不支持热插拔
 */

/* 单行注释 */
static inline uint32_t xxx_calc_divider(uint32_t freq)
{
    return (240000000 / freq) - 1;  /* 主频 240MHz */
}
```

### 5.7 错误处理 / Error Handling

```c
int esp32_xxx_init(void)
{
    int ret;

    /* 参数检查 */
    if (config == NULL)
        return -EINVAL;

    /* 资源申请 */
    ret = esp32_gpio_config(&config->gpio);
    if (ret < 0)
        return ret;

    /* 内存分配 */
    g_xxx_handle = kmm_malloc(sizeof(struct xxx_handle_s));
    if (g_xxx_handle == NULL)
        return -ENOMEM;

    return OK;
}
```

---

## 六、LVGL 驱动规范 / LVGL Driver Standards

### 6.1 显示驱动模板 / Display Driver Template

**ESP32-S3 (I2S bitbang):**
```c
void my_disp_flush(lv_display_t *disp, const lv_area_t *area,
                   uint8_t *px_map)
{
    /* I2S DMA 发送图像数据到 CVBS (GPIO2) */
    esp32s3_i2s_cvbs_transmit(px_map, size);
    lv_display_flush_ready(disp);
}
```

**ESP32-CAM (内置 DAC):**
```c
void my_disp_flush(lv_display_t *disp, const lv_area_t *area,
                   uint8_t *px_map)
{
    /* I2S DMA 发送图像数据到 DAC (GPIO25) */
    esp32_dac_cvbs_transmit(px_map, size);
    lv_display_flush_ready(disp);
}
```

### 6.2 输入设备驱动模板 / Input Device Driver Template

**ESP32-S3 (USB HID):**
```c
void my_keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint32_t key = esp32s3_usb_keyboard_get_key();
    data->key = key;
    data->state = (key != 0) ? LV_INDEV_STATE_PRESSED
                              : LV_INDEV_STATE_RELEASED;
}
```

**ESP32-CAM (BLE HID):**
```c
void my_keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint32_t key = esp32_ble_keyboard_get_key();
    data->key = key;
    data->state = (key != 0) ? LV_INDEV_STATE_PRESSED
                              : LV_INDEV_STATE_RELEASED;
}
```

---

## 七、双核编程规范 / Dual-Core Programming

### 7.1 Core 0 职责 / Core 0 Responsibilities
- LVGL 图形引擎
- CVBS 显示
- 音频播放
- FSK 磁带
- 看门狗 WDT0

### 7.2 Core 1 职责 / Core 1 Responsibilities
- NuttShell (NSH)
- WiFi/TCP/IP
- NTP、Cron
- SD 卡、RTC
- 看门狗 WDT1

### 7.3 核间通信 / Inter-Core Communication

```c
/* 使用消息队列或共享内存 */
static QueueHandle_t g_core0_to_core1_queue;
static DRAM_ATTR uint8_t g_shared_buffer[1024];
```

---

## 八、FSK 磁带调制解调 / FSK Modem

| 码速率 | 逻辑0频率 | 逻辑1频率 |
|--------|-----------|-----------|
| 4800 baud | 4800 Hz | 9600 Hz（默认）|
| 9600 baud | 9600 Hz | 19200 Hz |

---

## 九、编译脚本规范 / Build Script Standards

每个目标有独立的编译脚本目录：
- `scripts/esp32s3/build.sh` - ESP32-S3 编译和烧录
- `scripts/esp32s3/nuttx_build.sh` - ESP32-S3 NuttX 编译
- `scripts/esp32cam/build.sh` - ESP32-CAM 编译和烧录
- `scripts/esp32cam/nuttx_build.sh` - ESP32-CAM NuttX 编译

共享脚本：
- `scripts/download_deps.sh` - 下载依赖
- `scripts/setup_tools.sh` - 激活工具链
- `scripts/verify.sh` - 验证项目
- `scripts/convert_font.sh` - 字体转换

```bash
#!/bin/bash
# build.sh - 目标编译脚本
set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }

# 编译 NuttX
build_nuttx() {
    log_info "Building NuttX for $TARGET..."
    cd "$NUTTX_PATH"
    make -j$(nproc)
}
```

---

## 十、许可证头 / License Header

每个源文件必须包含以下 SPDX 许可证头：

```c
/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
```

---

## 十一、内存与性能 / Memory & Performance

### 11.1 内存分配 / Memory Allocation
- 使用 NuttX 内存分配器：`kmm_malloc()`、`kmm_free()`
- 避免在中断上下文中分配内存

### 11.2 PSRAM 使用 / PSRAM Usage

**ESP32-S3 (8MB PSRAM):**
- LVGL 堆：128KB（配置项）
- 图形帧缓冲使用 PSRAM
- 字库缓存使用 PSRAM

**ESP32-CAM (4MB PSRAM):**
- LVGL 堆：128KB（配置项）
- 图形帧缓冲使用 PSRAM（已优化）
- 字库缓存使用 PSRAM（已优化，较紧张）

### 11.3 性能要求 / Performance Requirements
- LVGL 刷新率：30-60 fps
- 终端响应：< 100ms
- 拼音输入延迟：< 50ms

---

_最后更新: 2026-04-02_
