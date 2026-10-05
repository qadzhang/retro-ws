# ESP32-S3 Retro WS - Agent Control File
# AI Agent 工作配置文件
# 本文件是唯一的 AI 工作标准文件（CLAUDE.md / .clinerules 已于 2026-10-04 废除）

## 1. 文件基础规范 / File Foundation

### 1.1 文件编码 / File Encoding
- **字符编码**: UTF-8 无 BOM / UTF-8 without BOM
- **缩进**: 4 空格（禁用 Tab）/ 4 Spaces (No Tab)
- **行尾**: LF（Unix 风格）/ LF (Unix style)

### 1.2 许可证头 / License Header
每个源文件必须包含以下头部：/ Every source file must include:
```c
/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
```

---

## 2. C 语言编码规范 / C Language Standards

### 2.1 命名规范 / Naming Conventions
| 类型 | 风格 | 示例 |
|------|------|------|
| 函数 | snake_case | `esp32_xxx_init()` |
| 变量 | snake_case | `g_initialized` |
| 常量 | UPPER_SNAKE | `CONFIG_ESP32_XXX` |
| 结构体 | snake_case_t | `xxx_config_s` |
| 枚举成员 | UPPER_SNAKE | `MODE_NORMAL` |
| 宏定义 | UPPER_SNAKE | `#define XXX_MAX_SIZE 1024` |
| 文件名 | snake_case.c/h | `drv_cvbs.c` |

### 2.2 头文件规范 / Header File Standards
```c
#ifndef __MODULE_NAME_H
#define __MODULE_NAME_H

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/* === 公开类型 === */
struct xxx_config_s {
    uint32_t baudrate;
    uint8_t  parity;
    bool     flow_control;
};

/* === 公开函数 === */
int module_init(void);
int module_deinit(void);

#endif /* __MODULE_NAME_H */
```

### 2.3 代码即文档 / Code as Documentation
- 所有源代码文件必须做到代码即文档 / All source code files must implement "code as documentation"
- 函数实现本身就是最好的注释 / Function implementation itself is the best comment
- 避免冗余注释，保持代码简洁清晰 / Avoid redundant comments, keep code clean and clear
- 变量命名要自解释，见名知意 / Variable names should be self-explanatory

### 2.4 代码风格（Linux Kernel 风格）/ Code Style (Linux Kernel)
- 括号换行（K&R 风格）/ Brace placement (K&R style)
- 函数返回类型与函数名不在同一行 / Return type and function name on separate lines
- 示例：
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

---

## 3. 错误处理规范 / Error Handling

### 3.1 统一错误码 / Unified Error Codes
- 使用 NuttX 标准错误码：OK、-EINVAL、-ENOMEM、-EIO 等
- 函数返回值：成功返回 OK (0)，失败返回负数错误码

### 3.2 标准错误处理模式 / Standard Error Handling Pattern
```c
int module_init(FAR struct module_config_s *config)
{
    int ret;

    /* 参数检查 */
    if (config == NULL)
        return -EINVAL;

    /* 资源申请 */
    ret = gpio_config(&config->gpio);
    if (ret < 0)
        return ret;

    /* 内存分配 */
    g_handle = kmm_malloc(sizeof(struct module_handle_s));
    if (g_handle == NULL)
        return -ENOMEM;

    return OK;
}
```

---

## 4. 注释规范 / Comment Standards

### 4.0 5W1H 总则（强制）/ 5W1H Principle (Mandatory)

**所有代码、模块、伪代码的注释必须按 5W1H 格式书写**（2026-10-04 起）。
注释内容以简体中文为主，关键术语可中英并列。

**文件/模块级头注释（每个 .c/.h 必须有，紧跟 SPDX 头之后）：**

```c
/*
 * <文件名> - <一句话标题>
 *
 * WHAT : 该文件/模块是什么、做什么
 * WHY  : 为什么存在，解决什么问题，谁依赖它
 * WHO  : 维护者（ESP32-S3 Retro Project Team，详见 git log）
 * WHERE: 所在路径及上下游模块（如 "esp32-retro-ws/src/...，上层见 SYSTEM.md"）
 * WHEN : 初版日期与最近标准化/重大修改日期
 * HOW  : 实现机制一句话（关键数据结构/外设/算法/调用链）
 */
```

**函数级注释（公共函数必须有；静态函数在逻辑不自明时补）：**

```c
/*
 * WHAT : 函数做什么
 * WHY  : 设计意图/约束（可选，一行）
 * HOW  : 关键实现要点 + 参数/返回值说明
 *   param1 - ...
 *   返回   - OK / -EINVAL / ...
 */
```

**伪代码/算法注释**：同样按 5W1H 六要素描述（算法用途 WHY、输入输 WHAT、
步骤 HOW、适用场景 WHEN/WHERE）。

**执行要求**：
1. 新增文件必须带 5W1H 头注释，否则视为不合规
2. 修改文件时保持头注释的 WHEN/HOW 与实际同步
3. 全库已于 2026-10-04 完成文件级 5W1H 迁移（90 文件）；遗留函数级
   注释按"改到哪补到哪"渐进迁移，专项任务见 NEXT_STEPS.md
4. 5W1H 是"代码即文档"（2.3 节）的结构化形式，禁止用空洞占位文字
   填充六要素（如 WHY: 无）——写不出 WHY 说明模块该删而不是注释该编

### 4.1 文件头部注释 / File Header Comments

> 4.0 节的 5W1H 模板为本节模板的替代形式（2026-10-04 起以 5W1H 为准）。

```c
/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件: module_name.c
 * 描述: 模块功能描述
 * 作者: ESP32-S3 Retro Project Team
 * 版本: 0.1.0
 * 日期: 2026-03-29
 */
```

### 4.2 函数注释 / Function Comments
```c
/*
 * 功能描述:
 *   函数的主要功能说明
 *
 * 参数:
 *   param1 - 参数1说明
 *   param2 - 参数2说明
 *
 * 返回值:
 *   OK (0)    - 成功
 *   -EINVAL   - 参数错误
 *   -ENOMEM   - 内存分配失败
 *
 * 注意:
 *   - 使用前确保系统已初始化
 *   - 非线程安全
 */
```

### 4.3 代码内注释 / Inline Comments
- 使用中英双语注释（项目要求）/ Use Chinese-English bilingual comments (project requirement)
- 单行注释：`/* 单行注释 */`
- 块注释用于复杂逻辑说明 / Block comments for complex logic

---

## 5. 内存与性能规范 / Memory & Performance

### 5.1 内存分配 / Memory Allocation
- 使用 NuttX 内存分配器：`kmm_malloc()`、`kmm_free()`
- 避免在中断上下文中分配内存
- 及时释放不再使用的内存

### 5.2 PSRAM 使用 / PSRAM Usage
- LVGL 堆：128KB（配置项）/ LVGL Heap: 128KB (configurable)
- 图形帧缓冲使用 PSRAM / Frame buffer uses PSRAM
- 字库缓存使用 PSRAM / Font cache uses PSRAM

---

## 6. LVGL 驱动规范 / LVGL Driver Standards

### 6.1 显示驱动模板 / Display Driver Template
```c
void my_disp_flush(lv_display_t *disp, const lv_area_t *area,
                   uint8_t *px_map)
{
    /* DMA 或 I2S 发送图像数据到 CVBS DAC */
    uint32_t size = (area->x2 - area->x1 + 1) *
                    (area->y2 - area->y1 + 1) * COLOR_DEPTH / 8;

    esp32_i2s_transmit(size, px_map);

    /* 通知 LVGL 本区域已刷新 */
    lv_display_flush_ready(disp);
}
```

### 6.2 输入设备驱动模板 / Input Device Driver Template
```c
void my_keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t last_key = 0;
    uint32_t key = esp32_keyboard_get_key();

    data->key = key;
    data->state = (key != 0) ? LV_INDEV_STATE_PRESSED
                              : LV_INDEV_STATE_RELEASED;
}
```

---

## 7. 多语种规范 / i18n Standards

### 7.1 字符串规范 / String Standards
- 所有界面字符串使用 `i18n_get()` 函数获取
- 字符串键使用常量定义
- 支持语言：zh_CN（简体中文）、en_US（English）
- 简体中文是默认语言，所有图形界面程序都要有简体中文实现


### 7.3 编码与字体铁律 / Encoding & Font Rules（2026-10-04 定稿；2026-10-05 补字号档）

- **系统编码全链路 UTF-8**：源码、NSH/CLI 输入输出、脚本引擎字符串、
  文件内容、.rpk 包内文本，一律 UTF-8，不做 GB2312/GBK 转换层
- **唯一字型唯一字号 12px**（2026-10-05 用户定稿：嵌入式体积优先，
  全系 CLI/GUI 单一字号档）：`src/lvgl/fonts/lv_font_notosans_sc_12.c`
  （Noto Sans SC **1bpp 点阵**，Unicode 区段全量 ≈2.1 万字形，UTF-8
  码点索引）。12px = 中文 Windows 3.2/95 界面宋体 9pt 点阵的历史
  标准字号；cvbs_console 网格按 12x14 设计；字号考证表见
  HARDWARE.md 6.4；不再提供 GB2312/GBK 子集档，不再有第二档字号
- **所有板必须能显示中文**：AV/CVBS 视频输出是全系标配（含 CLI 档），
  字符界面通过 `cvbs_console`（帧缓冲点阵控制台）渲染上屏——
  Pico/C3 的"字符输出"也必须走 AV 视频输出，不允许只有串口
- 禁止再引入第二套中文字型、第二档字号或字符集转换表；换字型/字号 =
  改 `scripts/convert_font.sh` 重新生成全量 UTF-8 版本

### 7.2 语言存储优先级 / Language Storage Priority
| 优先级 | 路径 |
|--------|------|
| 1 | `/mnt/sd0/lang.conf` |
| 2 | `/etc/lang.conf` |
| 3 | 编译默认值 |

### 7.4 CLI 文本编辑器 / CLI Text Editor（2026-10-04 增）
- 所有开发板 CLI 文本编辑器统一 **GNU nano 8.4**（deps/nano 真源码移植 + src/nuttx/common/nano_port 垫片）
- 一律不编入 vi（CONFIG_SYSTEM_VI 禁用）；构建新增 CLI 编辑功能优先评估 nano 内实现（nanorc/拼贴板等）


---

## 8. 脚本引擎胶水层 / Script Engine Glue Layer

### 8.1 统一接口 / Unified Interface
所有脚本语言使用统一的 retro_ui 接口：
```c
int retro_ui_msgbox(const char *title, const char *msg);
int retro_ui_input(const char *title, const char *prompt, char *buf, int bufsize);
int retro_ui_list(const char *title, const char *prompt, const char **items, int count);
int retro_ui_confirm(const char *title, const char *msg);
int retro_ui_status(const char *msg);
int retro_ui_progress(int value, int max);
int retro_ui_set_lang(const char *lang);
const char *retro_ui_get_lang(void);
/* 教学 GPIO 接口（2026-10-04 扩展，系统占用脚返回 -EBUSY） */
int retro_gpio_config(int pin, const char *mode);   /* in/out/in_pu/in_pd */
int retro_gpio_write(int pin, int value);
int retro_gpio_read(int pin);
int retro_gpio_adc_read(int channel);
int retro_gpio_pwm_set(int pin, int duty, int freq);
int retro_gpio_release(int pin);
```

### 8.2 示例程序路径 / Example Program Paths
- `examples/hello.bas` - BASIC
- `examples/hello.js` - JavaScript
- `examples/hello.be` - Berry（可选引擎）
- `examples/hello.py` - CPython（可选引擎，仅 ESP32-S3）
- `examples/spiral.lgo` - Logo 海龟画图（可选 jslogo）

---

## 9. 构建规范 / Build Standards

### 9.1 编译环境 / Build Environment
- Python 3.10+
- CMake 3.20+
- Ninja Build
- GCC 交叉编译器（ESP32-S3 Xtensa 工具链）

### 9.2 固件烧录 / Firmware Flashing
```bash
# ESP32-S3 / ESP32-CAM（Xtensa）
esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash \
    0x1000 bootchain/esp32s3.bin \
    0x8000 partitions.csv \
    0x10000 nuttx.bin

# ESP32-C3（合宙核心板，RISC-V，从 0x0 引导；简约款原生 USB 常为 /dev/ttyACM0）
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 nuttx.bin
```

编译输出：`deps/nuttx/nuttx.bin`；编译日志：项目根目录 `build.log`。

---

## 10. 安全规范 / Security Standards

### 10.1 连续重启保护 / Continuous Reboot Protection
- 连续 3 次看门狗重启 → 进入安全模式（CLI Only）

### 10.2 内存告警阈值 / Memory Alert Thresholds
| 阈值 | 级别 | 动作 |
|------|------|------|
| 80% | 警告 | 日志记录 |
| 90% | 严重 | 警告日志 |
| 95% | 紧急 | 终止任务或重启 |

---

## 11. Agent 工作规则 / Agent Working Rules

### 11.1 代码编写 / Code Writing
1. 遵循上述 C 语言编码规范
2. 所有注释使用中英双语
3. 每个文件包含 SPDX 许可证头
4. **文件/模块/伪代码注释一律按 5W1H 格式（4.0 节，2026-10-04 起）**
5. 函数/变量命名符合规范
6. 错误处理使用 NuttX 标准错误码
7. **不要修改 deps 目录下的任何文件**
8. **使用新的硬件特性前，必须先更新 `HARDWARE.md`，然后严格按照文档内容编程**
9. **GPL 组件禁止编入固件 ROM**：一律放 `apps-extra/`，由
    `scripts/build_packages.sh` 打成 **.rpk 安装包**（deb 风格，独立 ELF，
    binfmt/进程隔离，mere aggregation）交付，设备端经 `pkg` 命令安装；
    包 Arch 字段按架构隔离（xtensa/riscv）；固件本体保持纯
    Apache-2.0/MIT 宽松协议栈

### 11.2 文件操作 / File Operations
1. 创建/修改文件后更新相关文档
2. 新增驱动/模块时更新 SYSTEM.md
3. 新增功能时更新 REQUIREMENTS.md 和 COMPLETED.md

### 11.3 任务执行 / Task Execution
1. 复杂任务使用 task_progress 跟踪进度
2. 完成后验证（编译/测试）
3. 遵循项目编码规范

### 11.4 回答规范 / Response Standards
- **所有交流使用中文**（自 CLAUDE.md 迁移，2026-10-04）
- 简洁直接，不超过 4 行
- 避免不必要的开场白/总结
- 直接回答问题

### 11.5 处理第三方源码问题 / Third-Party Source Code Issues
当 deps 目录下的开源代码存在 bug 导致编译失败时：
1. **不要修改 deps 目录下的任何文件**（保证他人同步下载时不会遇到相同问题）
2. 在 src 目录下创建正确的替代文件，放在合适的模块目录中，并且要写好注释，说明这个文件的来源和修改的地方以及修改的原因
3. 通过以下方式使项目优先使用 src 目录下的版本：
   - 在 Kconfig 或 Makefile 中添加额外的 include 路径，使 src 目录优先
   - 使用 `EXTRAFLAGS` 或 `CFLAGS` 添加 `-I` 路径
   - 在 board 或 chip 的 Make.defs 中设置覆盖编译选项

示例：如果 `deps/nuttx/chip/esp-hal-3rdparty/components/xtensa/include/xt_utils.h` 有问题
```bash
# 创建替代文件
src/nuttx/esp32s3/chip/xt_utils.h

# 添加编译路径（优先级更高）
EXTRAFLAGS += -I$(TOPDIR)/../src/nuttx/esp32s3/chip
```

---

## 12. 必需维护文档 / Required Documents

### 12.1 文档清单 / Document List

| 文件 | 变更时需要维护 |
|------|---------------|
| README.md | 新增功能模块 |
| CODING_STANDARD.md | 修改编码规范 |
| SYSTEM.md | 新增驱动、修改系统架构 |
| REQUIREMENTS.md | 修改需求 |
| COMPLETED.md | 新增功能、完成需求 |
| NEXT_STEPS.md | 修改需求、修改构建流程 |
| BUILD_FIXES.md | 修复编译问题 |
| **HARDWARE.md** | **使用新硬件特性前必须先更新，然后严格按照文档编程** |

### 12.2 变更维护规则 / Maintenance Rules
| 变更类型 | 需要维护的文档 |
|----------|----------------|
| 新增功能模块 | README.md、CODING_STANDARD.md、SYSTEM.md、REQUIREMENTS.md、COMPLETED.md |
| 修改编码规范 | CODING_STANDARD.md、AGENTS.md |
| 修复编译问题 | BUILD_FIXES.md（必须记录问题、尝试方案、解决方案）|
| **涉及硬件特性** | **HARDWARE.md（必须先更新再编程）** |

---

## 13. 版本控制 / Version Control

### 13.1 Git 分支策略 / Git Branch Strategy
- `main` - 稳定分支
- `dev` - 开发分支
- `feature/*` - 功能分支

### 13.2 提交规范 / Commit Convention
- 格式：`[type] description`
- Type: feat/fix/docs/style/refactor/test

---

_最后更新: 2026-10-04_
