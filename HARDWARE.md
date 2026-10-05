# 复古工作站 Retro WS - 硬件规格参考文档
# Retro Workstation (retro-ws) - Hardware Specification Reference

> 本文档记录所有经过网上搜索和上游源码验证的硬件参数。
> 当程序需要使用新的硬件特性时，**必须先更新本文档**，然后严格按照文档编程。
>
> **开发策略**：共五个构建目标（s3 / s3n8 / cam / c3 / pico）。开发以
> **ESP32-S3 (N16R8/N8R8)** 为模板（首选板），所有驱动先按 S3 模板编写，
> 再向 ESP32-CAM / 合宙 ESP32-C3 / Raspberry Pi Pico 适配。
> **AV/CVBS 视频输出全系标配**（含 CLI 档，见 3A.2A / 3B.2A / 13.1）。

---

## 1. 硬件目标概览

### 1.1 目标定位

| 目标 | 定位 | 芯片模组 |
|------|------|----------|
| **esp32s3 (s3)** | **首选板 / 开发模板** | ESP32-S3-WROOM-1 **N16R8** (16MB+8MB) |
| esp32s3 (s3n8) | 同上（8MB Flash 档，独立构建目标） | ESP32-S3-WROOM-1 **N8R8** (8MB+8MB) |
| esp32cam | 兼容目标（顺带支持） | ESP32-CAM (AI-Thinker)，ESP32 + OV2640 |
| **esp32c3** | **低资源/低价格 CLI 档** | **合宙 ESP32-C3 核心板**（经典款 CH343 / 简约款原生 USB），RISC-V |
| **pico** | **最低成本本地教学终端（CLI，无网络）** | Raspberry Pi Pico（RP2040 双核 Cortex-M0+），见 3B 章 |

> N16R8 与 N8R8 引脚完全一致，仅 Flash 容量不同（16MB / 8MB），
> 均为 R8 八线 PSRAM（GPIO35/36/37 被占用，见 2.5 节）。
> **esp32c3 为 RISC-V、pico 为 ARM 架构，与 Xtensa 目标二进制不通用**——
> .rpk 包经 Arch 字段隔离（pkg_manager.c，取值 all/xtensa/riscv/esp32c3...）。

### 1.2 每板一个硬件档案文件

每种开发板对应一个硬件设置文件（引脚唯一事实来源 / single source of pin truth），
`board.h` 通过 `#include` 引入，驱动只允许使用档案中的宏：

| 开发板 | 硬件档案文件 |
|--------|-------------|
| ESP32-S3-DevKitC-1 (N16R8/N8R8) | `src/nuttx/esp32s3/board/hw_esp32s3_devkitc.h` |
| ESP32-CAM (AI-Thinker) | `src/nuttx/esp32/board/hw_esp32cam_aithinker.h` |
| 合宙 ESP32-C3 核心板（两款） | `src/nuttx/esp32c3/board/hw_esp32c3_luatos.h` |
| Raspberry Pi Pico (RP2040) | `src/nuttx/rp2040/board/hw_rp2040_pico.h` |

> 新增开发板时：先在本文档核实并登记引脚，再新建 `hw_<板名>.h` 档案文件。

### 1.3 图形档双目标对比（S3 / CAM）

> C3 / Pico 为 CLI 档（无 LVGL 桌面），规格与引脚见 3A / 3B 章；
> C3 与其他目标的功能差异对照见 3A.4。

| 特性 | ESP32-S3 (DevKitC-1 N16R8/N8R8) | ESP32-CAM (AI-Thinker) |
|------|----------------------------------|------------------------|
| 内核 | Xtensa LX7 双核 @240MHz | Xtensa LX6 双核 @240MHz |
| 片上 SRAM | 512KB | 520KB (396KB 可用) |
| Flash | 16MB / 8MB (QSPI) | 4MB (QSPI, 板载 W25Q32) |
| PSRAM | 8MB **Octal** (R8) | 4MB (QSPI, CS=GPIO16) |
| 内置 DAC | **无**（外部 I2S DAC 或电阻网络） | **有** (GPIO25/26, 8-bit) |
| USB OTG | 有 (GPIO19 DM, GPIO20 DP) | **无** |
| WiFi / 蓝牙 | 802.11 b/g/n + BLE 5.0 | 802.11 b/g/n + BT 4.2 BR/EDR + BLE |
| 摄像头 | 无 | 板载 OV2640（可选功能，与 CVBS/音频互斥） |
| CVBS 输出 | I2S -> 电阻网络 -> GPIO2 | 内置 DAC1 -> GPIO25 |
| 音频输出 | 外部 I2S DAC (GPIO40/41/42) | 内置 DAC2 -> GPIO26 |
| 音频输入 | ADC -> GPIO1 | ADC -> GPIO34 |
| 键盘鼠标 | USB HID + BLE HID | BLE HID |
| 状态 LED | WS2812 RGB (GPIO38 v1.1 / GPIO48 v1.0) | 红色 LED GPIO33 + Flash LED GPIO4 |
| SD 卡 | SPI (GPIO10/11/13/14) | SPI (GPIO13/15/2/14) |
| RTC (软 I2C) | GPIO5(SCL) / GPIO6(SDA) | GPIO22(SCL) / GPIO21(SDA) |
| 电源 | 5V USB | 建议 5V 2A |
| 显示分辨率上限 | 1024x768（实验性 overspec） | 640x480（内置 DAC 带宽所限） |

---

## 2. ESP32-S3 寄存器地址映射

> 来源: ESP-IDF `components/soc/esp32s3/register/soc/reg_base.h` + NuttX `esp-hal-3rdparty`

### 2.1 外设基地址

| 外设 | 宏定义 | 地址 | 备注 |
|------|--------|------|------|
| SYSTEM | `ESP32S3_SYSTEM_BASE` | **0x600C0000** | 替代 ESP32 的 DPORT |
| GPIO | `ESP32S3_GPIO_BASE` | **0x60004000** | |
| RTC | `ESP32S3_RTC_BASE` | **0x60008000** | `DR_REG_RTCCNTL_BASE` |
| I2S0 | `ESP32S3_I2S0_BASE` | **0x6000F000** | |
| BT | `ESP32S3_BT_BASE` | **0x60011000** | |
| I2C0 | `ESP32S3_I2C0_BASE` | **0x60013000** | |
| I2S1 | `ESP32S3_I2S1_BASE` | **0x6002D000** | |
| UART0 | `ESP32S3_UART0_BASE` | **0x60000000** | |
| UART1 | `ESP32S3_UART1_BASE` | **0x60010000** | |
| UART2 | `ESP32S3_UART2_BASE` | **0x6002E000** | |
| SPI0 (Flash) | `ESP32S3_SPI0_BASE` | **0x60003000** | |
| SPI1 | `ESP32S3_SPI1_BASE` | **0x60002000** | |
| SPI2 (FSPI) | `ESP32S3_SPI2_BASE` | **0x60024000** | |
| SPI3 (VSPI) | `ESP32S3_SPI3_BASE` | **0x60025000** | |
| TIMG0 | `ESP32S3_TIMERGROUP0_BASE` | **0x6001F000** | |
| TIMG1 | `ESP32S3_TIMERGROUP1_BASE` | **0x60020000** | |
| I2C1 | `ESP32S3_I2C1_BASE` | **0x60027000** | |
| SDMMC | `ESP32S3_SDMMC_BASE` | **0x60028000** | |
| GDMA | `ESP32S3_DMA_BASE` | **0x6003F000** | 通用 DMA |
| SENSITIVE | `ESP32S3_SENSITIVE_BASE` | **0x600B0000** | |

### 2.2 ESP32-S3 Timer Group WDT 寄存器偏移

> 来源: `soc/esp32s3/register/soc/timer_group_reg.h`

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| `TIMG_WDTCONFIG0` | **0x48** | WDT 使能、阶段配置、复位长度 |
| `TIMG_WDTCONFIG1` | **0x4C** | 时钟预分频 |
| `TIMG_WDTCONFIG2` | **0x50** | Stage 0 超时值 |
| `TIMG_WDTCONFIG3` | **0x54** | Stage 1 超时值 |
| `TIMG_WDTCONFIG4` | **0x58** | Stage 2 超时值 |
| `TIMG_WDTCONFIG5` | **0x5C** | Stage 3 超时值 |
| `TIMG_WDTFEED` | **0x60** | 喂狗寄存器（写任意值复位计数器） |
| `TIMG_WDTWPROTECT` | **0x64** | 写保护（解锁密钥: **0x50D83AA1**） |

### 2.3 ESP32-S3 WDT CONFIG0 位域

| 位 | 名称 | 值 | 说明 |
|----|------|----|------|
| [31] | WDT_EN | 1 | 看门狗使能 |
| [30:28] | WDT_STG3 | 0~3 | Stage 3 操作 |
| [26:24] | WDT_STG2 | 0~3 | Stage 2 操作 |
| [22:20] | WDT_STG1 | 0~3 | Stage 1 操作 |
| [18:16] | WDT_STG0 | 0~3 | Stage 0 操作 |
| 操作值: 0=禁用, 1=中断, 2=CPU复位, 3=系统复位 |

### 2.4 ESP32-S3 GDMA 描述符结构

> 来源: `components/hal/include/hal/dma_types.h`

```c
/* GDMA 描述符使用完整 32 位地址（非 20 位截断） */
typedef struct {
    volatile uint32_t size   : 12;  /* [11:0]  缓冲区大小 (最大 4095) */
    volatile uint32_t length : 12;  /* [23:12] 有效字节数 */
    volatile uint32_t        : 4;   /* [27:24] 保留 */
    volatile uint32_t err_eof: 1;   /* [28]    接收错误标志 */
    volatile uint32_t        : 1;   /* [29]    保留 */
    volatile uint32_t suc_eof: 1;   /* [30]    链表末尾标志 */
    volatile uint32_t owner  : 1;   /* [31]    0=CPU, 1=DMA */
    void *buffer;                   /* 缓冲区地址（32 位指针） */
    struct dma_descriptor_s *next;  /* 下一个描述符（NULL=末尾） */
} dma_descriptor_t;  /* sizeof = 12, __attribute__((aligned(4))) */
```

**关键约束**：
- `buf` 和 `next` 都是**完整 32 位指针**，不能用 `& 0xFFFFF` 截断
- ESP32-S3 DMA 使用 GDMA（通用 DMA），不使用 I2S 内置 DMA
- GDMA 通道分配: I2S0 TX=通道0, I2S0 RX=通道1, I2S1 TX=通道2...

### 2.5 ESP32-S3 不可用引脚（N16R8/N8R8 模组）⚠️

> 来源: ESP32-S3 datasheet + ESP32-S3-WROOM-1 模组 datasheet

| 引脚 | 原因 | 说明 |
|------|------|------|
| **GPIO26-GPIO32** | SPI Flash 总线 | 模组内部走线（Quad Flash） |
| **GPIO35** | SPICS1 | Octal PSRAM 额外语选线 |
| **GPIO36** | SPIDQS | Octal PSRAM 数据选通 |
| **GPIO37** | SPICLK_N | Octal PSRAM 差分时钟 |

> **R8 = 八线 PSRAM**：N16R8/N8R8 均为 R8 模组，GPIO35/36/37 不可用。
> （仅 N8R2 等四线 PSRAM 模组可释放这 3 脚。）

### 2.6 ESP32-S3 Strapping 引脚 ⚠️

> 来源: ESP32-S3 datasheet "Strapping Pins"

| 引脚 | 复位时功能 | 约束 |
|------|-----------|------|
| **GPIO0** | Boot 模式选择 | 高=Flash 启动，低=下载模式（BOOT 按键） |
| **GPIO3** | JTAG 信号源选择 | 保留，不外接强上/下拉 |
| **GPIO45** | VDD_SPI 电压选择 (1.8V/3.3V) | **严禁占用**，错误电平将无法启动 |
| **GPIO46** | Boot 模式 / ROM 日志输出 | 保留，不外接强上拉 |

> strapping 引脚在复位瞬间被采样，外设电路必须保证复位时电平正确。

### 2.7 ESP32-S3 GPIO 引脚分配（本项目）

> 硬件档案: `src/nuttx/esp32s3/board/hw_esp32s3_devkitc.h`

| 功能 | 引脚 | 备注 |
|------|------|------|
| **CVBS 视频输出** | **GPIO2/15/16/17** | LCD_CAM I80 并行口 DATA_OUT0-3 -> 4-bit R-2R 电阻梯（DMA 流式，见 6.5 节） |
| **CVBS ladder** | GPIO15/16/17 | 与 GPIO2 组成 4-bit R-2R 电阻网络（已启用，非预留） |
| **USB DM / DP** | GPIO19 / GPIO20 | USB OTG，接 USB HID 键鼠 |
| **音频 I2S 输出** | GPIO40 (WS) / GPIO41 (SCK) / GPIO42 (SDO) | 外部 I2S DAC（如 MAX98357A） |
| **音频输入 (ADC)** | GPIO1 | ADC1_CH0, 模拟麦克风（如 MAX9814） |
| **SD 卡 SPI** | GPIO10 (CS) / GPIO11 (MISO) / GPIO13 (MOSI) / GPIO14 (CLK) | 经 GPIO 矩阵分配 |
| **软件模拟 I2C (RTC)** | GPIO5 (SCL) / GPIO6 (SDA) | RTC 芯片 |
| **UART TX / RX** | GPIO43 / GPIO44 | 调试串口（UART0） |
| **BOOT 按键** | GPIO0 | 烧录模式（strapping） |
| **用户按键 1** | GPIO7 | 用户自定义 |
| **用户按键 2** | GPIO8 | 用户自定义 |
| **状态 LED (WS2812 RGB)** | **GPIO38 (v1.1)** / GPIO48 (v1.0) | 可寻址单线 LED，需 RMT 驱动 |

**调试通道注意**：
- GPIO39-42 为硬件 JTAG（MTCK/MTDO/MTDI/MTMS）；本设计将 40/41/42 用作 I2S
  音频后**硬件 JTAG 不可用**，调试使用 UART0 (GPIO43/44)。
- GPIO19/20 用作 USB OTG 主机后，芯片内置 USB-Serial-JTAG 同样不可用。

### 2.8 ESP32-S3 板载 LED

> 来源: ESP32-S3-DevKitC-1 v1.1 User Guide (docs.espressif.com)

| 板本 | LED GPIO | 器件 |
|------|----------|------|
| v1.0（早期） | **GPIO48** | 单个可寻址 WS2812 RGB LED |
| v1.1（当前量产） | **GPIO38** | 同上 |

> DevKitC-1 只有一颗**可寻址 WS2812**（单线协议），**没有**独立的 R/G/B 三个引脚。
> 驱动必须使用 RMT 外设编码，`gpio_set_level()` 无法点亮。
> 蓝牙状态指示复用此 LED（蓝色）：
> 灭=已连接，慢闪 (2s)=等待配对，快闪 (0.5s)=扫描中。

**WS2812 RMT 硬件驱动路径（2026-10-04 晚定稿）**：

- 走 NuttX 树内 RMT 硬件驱动链（**真外设，零 CPU 位摆**）：
  `CONFIG_ESP_RMT` + `CONFIG_RMT` → devkit 板级 `board_rmt_txinitialize(0, GPIO38/48)`
  → `/dev/rmt0` 字符设备（rmtchar）
  → 本项目 `src/nuttx/esp32s3/driver/ws2812_rmt.c` 仅做 24-bit GRB → RMT 符号编码
- NuttX devkit 板头已按板本选好引脚（v1.1=GPIO38 / v1.0=GPIO48，
  `esp32s3-devkit.h` RMT_OUTPUT_PIN），与本项目 LED_GPIO_RGB 一致
- WS2812 时序（RMT 通道时钟 80MHz，1 tick=12.5ns）：
  T0H=350ns(28 ticks) / T0L=900ns(72) / T1H=900ns(72) / T1L=350ns(28)，
  复位 >50µs 低电平；符号字 = `(T?L<<16) | (0x8000|T?H)`（高电平相在前，bit15=电平）
- **禁止软件位摆驱动 WS2812**（教学 GPIO 同理不推荐用 38/48 脚——见 2.7 占用表）

### 2.9 ESP32-S3 GPIO 寄存器偏移

| 寄存器 | 偏移 |
|--------|------|
| GPIO_OUT_REG | 0x004 |
| GPIO_OUT_W1TS | 0x008 |
| GPIO_OUT_W1TC | 0x00C |
| GPIO_ENABLE_REG | 0x020 |
| GPIO_IN_REG | 0x03C |
| GPIO_PIN(n) | 0x074 + n*4 |
| GPIO_FUNC_OUT_SEL_CFG(n) | 0x554 + n*4 |

---

## 3. ESP32 (ESP32-CAM) 寄存器地址映射

> 来源: ESP-IDF `components/soc/esp32/register/soc/reg_base.h` + NuttX `esp-hal-3rdparty`

### 3.1 外设基地址

| 外设 | 宏定义 | 地址 | 备注 |
|------|--------|------|------|
| DPORT | `ESP32_DPORT_BASE` | **0x3FF00000** | ESP32 的系统控制器（非 SYSTEM） |
| UART0 | `ESP32_UART0_BASE` | **0x3FF40000** | |
| SPI1 | `ESP32_SPI1_BASE` | **0x3FF42000** | |
| SPI0 | `ESP32_SPI0_BASE` | **0x3FF43000** | |
| GPIO | `ESP32_GPIO_BASE` | **0x3FF44000** | |
| RTC | `ESP32_RTC_BASE` | **0x3FF48000** | `DR_REG_RTCCNTL_BASE` |
| SENS | `ESP32_SENS_BASE` | **0x3FF48800** | DAC 控制通过此模块 |
| I2S0 | `ESP32_I2S0_BASE` | **0x3FF4F000** | **唯一支持 DAC 直连模式的 I2S** |
| UART1 | `ESP32_UART1_BASE` | **0x3FF50000** | |
| BT | `ESP32_BT_BASE` | **0x3FF51000** | |
| I2C0 | `ESP32_I2C0_BASE` | **0x3FF53000** | |
| TIMG0 | `ESP32_TIMERGROUP0_BASE` | **0x3FF5F000** | |
| TIMG1 | `ESP32_TIMERGROUP1_BASE` | **0x3FF60000** | |
| SPI2 (HSPI) | `ESP32_SPI2_BASE` | **0x3FF64000** | |
| SPI3 (VSPI) | `ESP32_SPI3_BASE` | **0x3FF65000** | |
| I2C1 | `ESP32_I2C1_BASE` | **0x3FF67000** | |
| SDMMC | `ESP32_SDMMC_BASE` | **0x3FF68000** | |
| I2S1 | `ESP32_I2S1_BASE` | **0x3FF6D000** | **不支持 DAC 模式** |
| UART2 | `ESP32_UART2_BASE` | **0x3FF6E000** | |

### 3.2 ESP32 Timer Group WDT 寄存器偏移

> 来源: `soc/esp32/register/soc/timer_group_reg.h`

**偏移与 ESP32-S3 完全相同：**

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| `TIMG_WDTCONFIG0` | **0x48** | WDT 使能、阶段配置 |
| `TIMG_WDTCONFIG1` | **0x4C** | 时钟预分频 |
| `TIMG_WDTCONFIG2` | **0x50** | Stage 0 超时值 |
| `TIMG_WDTCONFIG3` | **0x54** | Stage 1 超时值 |
| `TIMG_WDTCONFIG4` | **0x58** | Stage 2 超时值 |
| `TIMG_WDTCONFIG5` | **0x5C** | Stage 3 超时值 |
| `TIMG_WDTFEED` | **0x60** | 喂狗寄存器 |
| `TIMG_WDTWPROTECT` | **0x64** | 写保护（解锁密钥: **0x50D83AA1**） |

### 3.3 ESP32 RTC 寄存器偏移

> 来源: `soc/esp32/include/soc/rtc_cntl_reg.h`

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| RTC_CNTL_OPTIONS0 | **0x000** | 软件复位控制 |
| RTC_CNTL_STATE0 | **0x008** | |
| RTC_CNTL_RESET_STATE | **0x030** | 复位原因（低 5 位） |
| RTC_CNTL_WDTCONFIG0 | **0x048** | RTC WDT 配置 |
| RTC_CNTL_WDTFEED | **0x05C** | RTC WDT 喂狗 |
| RTC_CNTL_WDTWPROTECT | **0x060** | RTC WDT 写保护 |

**软件复位位域（OPTIONS0 寄存器）：**

| 位 | 名称 | 说明 |
|----|------|------|
| [31] | `RTC_CNTL_SW_SYS_RESET` | 写 1 触发系统复位 |
| [30] | `RTC_CNTL_SW_CPU_RESET` | 写 1 触发 CPU 复位 |
| [9] | `RTC_CNTL_SW_APPCPU_RESET` | 写 1 复位 APP CPU (Core 1) |

**复位原因（RESET_STATE 低 5 位）：**

| 值 | 含义 |
|----|------|
| 1 | POWERON_RESET |
| 2 | SW_RESET |
| 3 | OWDT_RESET |
| 4 | DEEPSLEEP_RESET |
| 6 | TG0WDT_SYS_RESET |
| 7 | TG1WDT_SYS_RESET |
| 8 | RTCWDT_SYS_RESET |
| 11 | SW_CPU_RESET |
| 12 | RTCWDT_CPU_RESET |
| 14 | RTCWDT_BROWN_OUT |
| 15 | RTCWDT_RTC_RESET |

### 3.4 ESP32 I2S 寄存器偏移

> 来源: `soc/esp32/register/soc/i2s_reg.h`

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| CONF | **0x0000** | I2S 主配置 |
| SAMPLE_RATE_CONF | **0x0008** | 采样率/位宽配置 |
| CLKM_CONF | **0x000C** | 时钟分频 |
| FIFO_CONF | **0x0010** | FIFO 配置 |
| CONF2 | **0x0018** | LCD/Camera/DAC 模式配置 |
| OUT_LINK | **0x0020** | DMA 输出链表地址 |
| IN_LINK | **0x0024** | DMA 输入链表地址 |
| INT_ENA | **0x0028** | 中断使能 |
| INT_RAW | **0x002C** | 中断原始状态 |
| INT_CLR | **0x0030** | 中断清除 |
| FIFO_DATA | **0x003C** | FIFO 数据读写 |
| CONF_CHAN | **0x0040** | 通道模式配置 |

### 3.5 ESP32 I2S DAC 模式配置

> 来源: `hal/esp32/include/hal/i2s_ll.h` → `i2s_ll_enable_builtin_adc_dac()`
> **重要：ESP32 只有 I2S0 支持 DAC 直连模式，I2S1 不支持！**

**使能 DAC 模式（通过 CONF2 寄存器，偏移 0x0018）：**

```c
/* 步骤1: 使能 LCD/DAC 模式 (CONF2 寄存器) */
I2S.conf2.lcd_en = 1;       /* bit 5 = 1, 使能 LCD/DAC 模式 */
I2S.conf2.camera_en = 0;    /* bit 0 = 0, 禁用 camera 模式 */
```

**选择 DAC 通道（通过 CONF_CHAN 寄存器，偏移 0x0028, `tx_chan_mod` 字段 bit[2:0]）：**

| tx_chan_mod | 输出通道 |
|-----------|----------|
| 0 | 双声道（DAC1 + DAC2） |
| 1 | 仅右声道 (DAC1: GPIO25) |
| 2 | 仅左声道 (DAC2: GPIO26) |
| 3 | 右声道单声道 |
| 4 | 左声道单声道 |

> **注意**：ESP32 I2S DAC 模式中，DAC1 (GPIO25) 对应右声道，DAC2 (GPIO26) 对应左声道。
> 这是一个已知的 ESP32 硬件设计怪癖——左右声道与 DAC1/DAC2 的对应关系是反的。

**CONF 寄存器 (偏移 0x0000) 关键位域：**

| 位 | 名称 | 说明 |
|----|------|------|
| [0] | TX_START | 启动 TX 传输 |
| [1] | RX_START | 启动 RX 传输 |
| [2] | TX_RESET | 复位 TX |
| [3] | RX_RESET | 复位 RX |
| [4] | TX_FIFO_RESET | 复位 TX FIFO |
| [5] | RX_FIFO_RESET | 复位 RX FIFO |
| [14] | TX_RIGHT_FIRST | 右声道先发 |
| [16] | LCD_EN | LCD 模式（已废弃，用 CONF2） |

### 3.6 ESP32 DMA 描述符结构

> ESP32 I2S 使用内置 DMA（非 GDMA），描述符同样使用 32 位地址

```c
typedef struct {
    volatile uint32_t eof    : 1;   /* [0]     链表末尾标志 */
    volatile uint32_t owner  : 1;   /* [1]     0=CPU, 1=DMA */
    volatile uint32_t length : 12;  /* [13:2]  有效字节数 */
    volatile uint32_t size   : 12;  /* [25:14] 缓冲区大小 */
    volatile uint32_t sosf   : 1;   /* [26]    子帧起始 */
    volatile uint32_t offset : 5;   /* [31:27] 非对齐偏移 */
    volatile uint32_t buf;          /* 缓冲区地址（32 位） */
    volatile uint32_t next;         /* 下一个描述符地址（32 位） */
} esp32_dma_descriptor_t;  /* sizeof = 12, aligned(4) */
```

### 3.7 ESP32 GPIO 寄存器偏移

| 寄存器 | 偏移 |
|--------|------|
| GPIO_OUT_REG | 0x004 |
| GPIO_OUT_W1TS | 0x008 |
| GPIO_OUT_W1TC | 0x00C |
| GPIO_ENABLE_REG | 0x020 |
| GPIO_IN_REG | 0x03C |
| GPIO_PIN(n) | 0x06C + n*4 |
| GPIO_FUNC_OUT_SEL_CFG(n) | 0x544 + n*4 |

### 3.8 ESP32-CAM AI-Thinker 板级规格

| 参数 | 值 | 备注 |
|------|-----|------|
| 主控 | ESP32 (Xtensa LX6 双核) | 非 ESP32-S3 |
| 主频 | 最高 240MHz | |
| SRAM | 520KB | 396KB 可用 |
| Flash | 4MB (32Mbit) | QSPI 接口，芯片 W25Q32 |
| PSRAM | 4MB (32Mbit) | QSPI 接口，芯片 ESP-PSRAM32 (**CS=GPIO16**) |
| WiFi | 802.11 b/g/n | 2.4GHz |
| 蓝牙 | 4.2 BR/EDR + BLE | |
| 摄像头 | OV2640 2MP | 板载，可选功能 |
| TF 卡 | 最高 32GB | SPI / 1-bit SDMMC 模式 |
| 板尺寸 | 27 x 40.5 x 4.5mm | |
| 电源 | 建议 5V 2A | |

### 3.9 ESP32-CAM GPIO 引脚分配（本项目）

> 硬件档案: `src/nuttx/esp32/board/hw_esp32cam_aithinker.h`
> 来源: Random Nerd Tutorials "ESP32-CAM AI-Thinker Pinout Guide"（已核实）

| 功能 | GPIO | 备注 |
|------|------|------|
| **CVBS 视频输出** | **GPIO25** | DAC1 输出复合视频 |
| **音频输出** | **GPIO26** | DAC2 输出音频 |
| **音频输入** | **GPIO34** | ADC1_CH6, 模拟麦克风（仅输入，无上拉） |
| **SD 卡 SPI** | **GPIO13 (CS) / GPIO15 (MOSI) / GPIO2 (MISO) / GPIO14 (CLK)** | 见下方纠正说明 |
| SD 卡 1-bit SDMMC（备选） | GPIO14 (CLK) / GPIO15 (CMD) / GPIO2 (D0) | 可腾出 GPIO4/12/13 |
| **软件模拟 I2C (RTC)** | GPIO21 (SDA) / GPIO22 (SCL) | 占用摄像头 D3/PCLK，与摄像头互斥 |
| **Boot 模式** | GPIO0 | 板上无按键，需排针接地进烧录模式 |
| **红色状态 LED** | **GPIO33** | **低电平点亮（逻辑反相）** |
| **闪光灯 LED** | GPIO4 | 与 SD 卡 DATA1 共享（SPI 模式下被 SD 占用） |
| **蓝牙状态 LED** | GPIO4 | 复用闪光灯 LED |
| **UART TX / RX** | GPIO1 / GPIO3 | 调试/烧录 |
| **PSRAM CS / CLK** | GPIO16 / **GPIO17** | GPIO16=PSRAM 片选（引出但严禁使用）；**GPIO17=板上 PSRAM 时钟（严禁使用，不引出排针）** |
| 摄像头 (OV2640) | 见 3.10 | 可选功能，与 CVBS/音频互斥 |

> ⚠️ **SD 卡 SPI 引脚纠正（2026-10-04）**：此前文档误写为
> GPIO2(CS)/GPIO12(MISO)/GPIO13(MOSI)。经 RNT 引脚图核实，AI-Thinker
> 的 microSD 走线为：CLK=GPIO14、CMD/MOSI=GPIO15、DATA0/MISO=GPIO2、
> DATA1=GPIO4、DATA2=GPIO12、DATA3=GPIO13，SPI 模式片选取 **GPIO13**。

### 3.10 ESP32-CAM 摄像头引脚（可选功能）

> 来源: Random Nerd Tutorials 引脚图（Arduino 例程中数据位顺序略有出入，
> 本项目摄像头未启用，仅作冲突分析用）

| 信号 | GPIO | 信号 | GPIO |
|------|------|------|------|
| D0 / Y2 | GPIO5 | D7 / Y9 | GPIO35（仅输入） |
| D1 / Y3 | GPIO18 | XCLK | GPIO0 |
| D2 / Y4 | GPIO19 | PCLK | GPIO22 |
| D3 / Y5 | GPIO21 | VSYNC | **GPIO25** ⚡ |
| D4 / Y6 | GPIO36（仅输入） | HREF | GPIO23 |
| D5 / Y7 | GPIO39（仅输入） | SIOD (SDA) | **GPIO26** ⚡ |
| D6 / Y8 | **GPIO34** ⚡（仅输入） | SIOC (SCL) | GPIO27 |
| PWDN | GPIO32 | RESET | 无（悬空） |

**⚡ 互斥约束（本项目占用）**：
- GPIO25 = VSYNC <-> 被 CVBS DAC1 占用 → **摄像头与 CVBS 不能同时使用**
- GPIO26 = SIOD <-> 被音频 DAC2 占用 → **摄像头与音频不能同时使用**
- GPIO34 = D6 <-> 被麦克风 ADC 占用 → 启用麦克风需断开摄像头数据线

> 摄像头为可选功能：默认 CVBS+音频模式下不启用；需要拍照时切换模式。

### 3.11 ESP32-CAM RTC I2C 引脚

| RTC 芯片 | I2C 地址 | SDA | SCL | 备注 |
|----------|----------|-----|-----|------|
| DS1307 | 0x68 | GPIO21 | GPIO22 | 经典款 |
| DS1338 | 0x68 | GPIO21 | GPIO22 | 更高精度 |
| PCF8563 | 0x51 | GPIO21 | GPIO22 | 低功耗 |
| RV-3028-C7 | 0x51 | GPIO21 | GPIO22 | 高精度 0.4ppm |

> **接线方式**：SDA → GPIO21，SCL → GPIO22，VCC → 3.3V，GND → GND
>
> **重要约束**：
> - GPIO21/22 同时是摄像头 D3/PCLK（本项目摄像头默认不启用，无冲突）
> - **ESP32 只有 I2S0 支持 DAC 模式**，CVBS（DAC1）和音频（DAC2）共用 I2S0，需要时分复用
> - 使用 TF 卡时，GPIO2/4/12/13/14/15 被占用
> - GPIO34-39 仅输入模式，无内部上拉电阻
> - GPIO16 为 PSRAM 片选，严禁挪用

### 3.12 ESP32-CAM LED 与按键

| LED/按键 | GPIO | 说明 |
|----------|------|------|
| 红色状态 LED | **GPIO33** | **低电平点亮（反逻辑）**，驱动需取反 |
| 闪光灯 LED | GPIO4 | 高电平点亮；与 SD DATA1 共享 |
| Boot 模式 | GPIO0 | 板上无按键，排针接地进烧录 |
| RST 按键 | EN | 板上唯一按键 |

**蓝牙状态指示（复用 GPIO4 闪光灯）**：

| LED 状态 | 含义 |
|---------|------|
| **灭** | 连接成功（正常工作） |
| **慢闪 (2s/次)** | 等待配对 / 未找到键鼠 |
| **快闪 (0.5s/次)** | 扫描中 / 正在连接 / 断线重连 |

---

## 3A. ESP32-C3（合宙核心板，CLI 档）⭐ 2026-10-04 新增

> 来源: [LuatOS wiki ESP32C3-CORE](https://wiki-zh.luatos.org) +
> [ESP32-C3 datasheet](https://www.espressif.com/en/products/socs/esp32-c3)

### 3A.1 板级规格与两款差异

| 参数 | 值 |
|------|-----|
| 主控 | ESP32-C3（**RISC-V RV32IMC 单核** @160MHz） |
| SRAM | 400KB（**不支持 PSRAM**——故本目标为 CLI-only） |
| Flash | 板载外置 4MB SPI（DIO；芯片有带/不带内置 Flash 两种封装） |
| 无线 | WiFi 802.11 b/g/n + BLE 5，板载 PCB 天线 |
| 板尺寸 | 21 x 51mm 邮票孔 |
| IO | 15 路数字 IO，5 通道 12-bit ADC，UART x2 / I2C / SPI |

| 版本 | 差异 | 控制台/烧录通道 |
|------|------|----------------|
| **经典款** | 板载 CH343 USB 转串口芯片 | UART0（GPIO21 TX / GPIO20 RX），USB 接 CH343 |
| **简约款** | 无串口芯片 | Type-C 直连**原生 USB**（GPIO18 D- / GPIO19 D+，内置 USB-Serial-JTAG） |

> Kconfig `RETRO_LUATOS_C3_UARTBRIDGE`（经典款，默认）/ `NATIVEUSB`（简约款）。
> 硬件档案: `src/nuttx/esp32c3/board/hw_esp32c3_luatos.h`

### 3A.2 禁用引脚与 strapping

| 引脚 | 原因 |
|------|------|
| **GPIO11** | VDD_SPI——本板 Flash 电源直挂 3.3V，**严禁当 IO 用**（烧 VDD_SPI_AS_GPIO 熔丝可释放，一次性不可逆，不做） |
| **GPIO14-17** | SPI Flash 总线占死（SPICS0=14 / SPICLK=15 / SPID=16 / SPIQ=17，外置 4MB Flash DIO 模式，不引出） |
| GPIO12/13 | SPIHD/SPIWP 仅 **QIO** 模式占用；本板 Flash 为 **DIO** 模式故可当 GPIO——板上接 LED D4/D5（高电平点亮），**教学脚本勿用（板上指示灯脚避让原则）** |
| **GPIO2** | strapping——启动时须浮空或低电平 |
| **GPIO8** | strapping——启动时须高电平（JTAG/SDIO 选择）；烧录期不可被拉低 |
| **GPIO9** | strapping——BOOT 按键（高=Flash 启动，低=下载模式） |
| GPIO18/19 | 原生 USB D-/D+（简约款为唯一烧录/调试通道） |

> ⚠️ 自编译固件 **必须配置 Flash 为 DIO 模式**，否则 QIO 会征用 GPIO12/13 与板上 LED 冲突。
> 2026-10-04 前本文误记"GPIO11-17 全为 Flash 占用"，已按 LuatOS wiki 管脚表 4-3 修正。

### 3A.2A AV 视频输出（全系标配）⭐ 2026-10-04 定稿（PDM 单脚方案）

C3 无内置 DAC、无 LCD_CAM 并行口，CVBS 采用 **I2S0 PDM-TX 原始模式
（raw PDM）单脚 sigma-delta** 方案——真外设、真 DMA，不占用 CPU：

| 功能 | 引脚 | 说明 |
|------|------|------|
| CVBS PDM 数据 | GPIO1 | I2S0 PDM TX 数据脚，1-bit @13.3333MHz |
| RC 低通滤波 | GPIO1 后级 | R≈270Ω + C≈47pF（截止 ~5MHz）→ 75Ω 端接 |

原理：cvbs_core 产生的 8-bit 亮度样本经**一阶 sigma-delta** 调制成
1-bit 流（每个样本 1 bit），I2S0 PDM TX 以 13.3333MHz 引脚速率直出，
RC 滤波后还 original 复合波形。同步沿由全 0 位流给出，时基 = 晶振级。
时钟链（PLL_F160M=160MHz 源，全整数分频）：
`160MHz / 4 = 40MHz (mclk) × fp/fs (170/510) = 13.3333MHz 引脚速率`，
DMA 供数速率 = 13.3333MHz/16 = 833.33k 半字/秒（GDMA 通道 0-2 任取空闲）。
1-bit@13.3MHz 对 4 电平（同步/消隐/文本灰/白）足够——C3 定位为字符控制台。
原 GPIO1+GPIO10 两脚 R-2R 方案作废（C3 无并行 DMA 输出外设）。
**GPIO10 已释放为教学脚**（原"PDM line1 预留"取消——单线方案足够，
双线实验需用户自行承担占用，默认不占）。
**字符输出也必须走本 AV 输出**（cvbs_console 点阵渲染，AGENTS.md 7.3）。

### 3A.3 GPIO 引脚分配（本项目）

| 功能 | 引脚 | 备注 |
|------|------|------|
| **CVBS PDM 输出** | **GPIO1** | I2S0 PDM-TX 单脚 sigma-delta（见 3A.2A），全系标配 |
| UART0 TX/RX（经典款） | GPIO21 / GPIO20 | 经 CH343 |
| 原生 USB（简约款） | GPIO18 (D-) / GPIO19 (D+) | 内置 USB-Serial-JTAG |
| BOOT 按键 | GPIO9 | 板载 |
| RST 按键 | EN | 板载（非 GPIO） |
| 红色状态 LED D4 | **GPIO12** | **高电平点亮**（SPIHD 复用，DIO 模式可用；板上指示灯脚，脚本勿用） |
| 状态 LED D5 | **GPIO13** | **高电平点亮**（SPIWP 复用，同上） |
| SD 卡 SPI | GPIO7 (CS) / GPIO6 (MOSI) / GPIO5 (MISO) / GPIO4 (CLK) | 推荐接线（邮票孔经 GPIO 矩阵可重映射） |
| 软 I2C (RTC) | GPIO3 (SCL) / GPIO0 (SDA) | 推荐接线（NuttX 暂无 esp32c3 硬件 I2C 驱动，retro_bus 走位摆回退，见 13.3） |
| **教学 GPIO** | **GPIO10（首选）** | 唯一完全空闲的非 strapping 脚；GPIO2 可用但需注意启动电平 |

### 3A.4 目标定位（与图形档两目标差异）

| 特性 | esp32c3 | esp32s3 / esp32cam |
|------|---------|--------------------|
| 图形界面 | **无 LVGL 桌面**（无 PSRAM 放不下帧缓冲；有 /dev/cvbscon 字符控制台上 AV 屏，2026-10-04 晚） | Win3.2/WindowMaker 桌面 |
| 脚本引擎 | my-basic + Berry（CLI 模式） | 五引擎全量 |
| CPython | 无 | 仅 S3 |
| 软件交付 | **.rpk 包管理器为主通道**（CLI 程序经包安装） | 同 |
| 架构 | **riscv-esp32c3** | xtensa-esp32s3 / xtensa-esp32 |

---

## 3B. Raspberry Pi Pico（RP2040，CLI 教学终端）⭐ 2026-10-04 新增

### 3B.1 定位与规格

**定位：本地 CLI 教学终端（无网络）**——最低成本档，仅 NSH 命令行 +
脚本引擎 + 拼音输入法 + SD 卡文件 IO；无 CVBS/无音频/无 WiFi。

| 项 | 值 |
|----|-----|
| SoC | RP2040（双核 ARM Cortex-M0+ @133MHz） |
| RAM | 264KB 片上 SRAM（无 PSRAM） |
| Flash | 板载 2MB QSPI（专用 QSPI 引脚，不占 GPIO 矩阵） |
| 网络 | 无（本地终端；需网络请用 S3/CAM/C3） |
| 双核分工 | Core0 = NSH/程序与脚本运行；Core1 = 文件 IO/SD 服务（本板无图形音频） |
| 默认控制台 | UART0：GP0=TX / GP1=RX，115200 8N1 |
| NuttX 板基 | raspberrypi-pico:nsh（arch/arm/src/rp2040） |

### 3B.2 引脚分配（Pico 40-pin）

| 功能 | 引脚 | 说明 |
|------|------|------|
| UART0 控制台 | GP0(TX)/GP1(RX) | 默认 NSH 控制台 |
| SD 卡 SPI0 | GP19(MOSI)/GP16(MISO)/GP18(SCK)/GP17(CS) | 教学文件 IO |
| 板载 LED | GP25 | 状态指示（脚本可点灯） |
| ADC | GP26/GP27/GP28 | ADC0/1/2（3.3V 量程） |
| USB | 原生 USB device | BOOTSEL 进固件烧录模式；也作 CDC 控制台（可选） |
| QSPI Flash | 专用 QSPI_SS/CLK/SD0-3 | 不在排针上，禁用 |

### 3B.2A AV 视频输出（全系标配）⭐ 2026-10-04 定稿（PIO 4-bit 并行，晚间修订移脚）

RP2040 用 **PIO 状态机 + DMA** 生成复合视频（真外设，波形时基由
PIO SM 时钟锁定，DMA 只负责供数，抖动为零）：

| 功能 | 引脚 | 说明 |
|------|------|------|
| CVBS bit0 (LSB) | GP12 | 4-bit R-2R（1kΩ/2kΩ 系） |
| CVBS bit1 | GP13 | 同上 |
| CVBS bit2 | GP14 | 同上 |
| CVBS bit3 (MSB) | GP15 | 同上 |
| R-2R 梯 | GP12-15 | 4-bit = 16 电平 → 75Ω CVBS，RCA 输出 |

> ⚠️ **为什么不在 GP20-23（2026-10-04 晚修订）**：
> 1. **GP23 是 Pico 板 SMPS（RT6150）省电模式控制脚**——视频位以
>    13.5MHz 翻转会把 3V3 电源纹波直接调制进 R-2R 输出（R-2R 输出
>    电平以 3V3 为基准），画面出现开关噪声，故 GP23 禁用于视频；
> 2. PIO 的 `OUT PINS,4` 只能驱动**连续编号**引脚，GP20/21/22+GP26
>    非连续，硬件上不可行；
> 3. 迁到 GP12-15（连续、无复用、非 ADC、非 strapping）后，原 GP20-22
>    释放为教学脚，教学脚总数不减反增。
> GP24=VBUS 检测、GP29=VSYS 监测（ADC3）维持禁用。

实现：PIO SM0 装载单指令程序 `out pins,4 [4]`——每 5 个 SM 周期
输出一个 4-bit 样本；SM 时钟 = 125MHz/1.8515625 ≈ 67.5316MHz
（Q16.8 分频 int=1/frac=218），样本率 ≈ 13.5063MHz（+0.047%，PAL 容差内）。
DMA 通道以 `DREQ = PIO0 TX FIFO` 节流，乒乓行缓冲（864B×4）由
**Core1 视频任务**逐行填充（双核分工：Core0=程序，Core1=视频/文件 IO）。
PIO FIFO 深度天然吸收 ISR 抖动——SM 时钟不中断，行时序永远精确。
**字符输出也必须走本 AV 输出**（cvbs_console 点阵渲染）。
原 GP20 同步+GP21 视频两脚 1-bit 方案作废；GP20-23 四脚方案亦作废。

### 3B.3 教学可用 GPIO 与占用表

排针可用 GP0-GP28。系统占用：GP0/1（控制台 UART0）、GP12-15（CVBS
R-2R，见 3B.2A）、GP16-19（SD SPI0）；板上专用禁用：GP23（SMPS 省电
控制，勿动）、GP24（VBUS 检测）、GP25（板载 LED，指示灯脚避让）、
GP29（VSYS 监测，不在排针）。

**教学脚（GP25 板载 LED 遵循"指示灯脚避让"原则不派给脚本）**：
GP2-GP11（数字 IO 十只）、GP20-GP22（数字 IO 三只）、
GP26-GP28（ADC0/1/2 三只，3.3V 量程）。

### 3B.4 烧录

BOOTSEL 按住上电进入 UF2 模式，拖入 nuttx.uf2；或用
`openocd -f interface/raspberrypi-swd.cfg` SWD 烧 ELF。

## 4. ESP32 与 ESP32-S3 的关键差异

| 特性 | ESP32 (ESP32-CAM) | ESP32-S3 (DevKitC-1) | 本项目影响 |
|------|-------|----------|------------|
| 定位 | 兼容目标 | **开发模板（首选）** | 驱动先按 S3 编写再适配 |
| 系统控制器 | DPORT (0x3FF00000) | SYSTEM (0x600C0000) | 寄存器地址完全不同 |
| 内核 | LX6 双核 | LX7 双核 | 指令集兼容 |
| 内置 DAC | **有** (GPIO25/26, 8-bit) | 无 | CAM 直接输出 CVBS/音频；S3 需电阻网络/外部 DAC |
| I2S DAC 模式 | I2S0 支持，通过 conf2.lcd_en | 不支持 | 完全不同的 CVBS/音频方案 |
| USB OTG | 无 | 有 | CAM 用 BLE HID 键鼠 |
| DMA | I2S 内置 DMA | GDMA（通用 DMA） | 描述符格式不同 |
| PSRAM | 4MB (QSPI)，CS=GPIO16 | 8MB (Octal R8) | S3 的 GPIO35/36/37 被占用 |
| Flash | 4MB (QSPI) | 8/16MB (QSPI) | CAM 存储空间受限 |
| GPIO 数量 | 40 个 (34-39 仅输入) | 45 个 (26-37 模组占用) | 可用脚数相近 |
| ADC | 2x 12-bit | 2x 12-bit | |
| 蓝牙 | BT 4.2 BR/EDR + BLE | BLE 5.0 | CAM 支持 BR/EDR |
| 状态 LED | GPIO33 红 + GPIO4 闪光灯 | WS2812 RGB (GPIO38/48) | 驱动方式不同（GPIO vs RMT） |

---

## 5. DAC 与模拟输出

### 5.1 ESP32 内置 DAC（仅 ESP32-CAM 目标）

ESP32 内置 2 通道 8-bit DAC（GPIO25/26），可通过 I2S0 DMA 直接输出模拟信号。

| 参数 | 值 |
|------|-----|
| DAC 通道 | 2 通道（DAC1: GPIO25, DAC2: GPIO26）|
| 位深 | 8-bit |
| 输出范围 | 0 ~ VDD3P3_RTC (约 3.3V) |
| 最大采样率 | ~200kHz（通过 I2S DMA）|
| DAC1 映射 | I2S 右声道 → GPIO25 |
| DAC2 映射 | I2S 左声道 → GPIO26 |

**使能流程：**
1. 配置 I2S0 时钟分频和采样率
2. 置 `conf2.lcd_en = 1`（CONF2 偏移 0x0018, bit 5）
3. 清 `conf2.camera_en = 0`
4. 设置 `conf_chan.tx_chan_mod` 选择 DAC 通道
5. 配置 DMA 描述符链表
6. 置 `conf.tx_start = 1`

### 5.2 ESP32-S3 音频/视频方案

ESP32-S3 无内置 DAC，需外部方案：
- **CVBS 视频输出**：LCD_CAM I80 并行口 + GDMA -> GPIO2/15/16/17 -> 4-bit R-2R 电阻梯 -> CVBS（2026-10-04 定稿，详见 6.5；原"I2S -> GPIO2"记载作废）
- **音频输出**：I2S -> 外部 DAC 芯片（如 MAX98357A）-> 扬声器
- **音频输入**：ADC -> GPIO1 -> 模拟麦克风（如 MAX9814）

### 5.3 ESP32-CAM 音频/视频方案

利用内置 DAC：
- **CVBS 视频输出**：I2S0 → conf2.lcd_en → 内置 DAC1 (GPIO25) → 电阻网络 → CVBS 接口
- **音频输出**：I2S0（时分复用）→ 内置 DAC2 (GPIO26) → 功放 → 扬声器

> 参考项目：[bitluni's ESP32 Composite Video](https://bitluni.net/esp32-composite-video)

---

## 6. CVBS 复合视频输出

### 6.1 CVBS 标准

| 参数 | NTSC (60Hz) | PAL (50Hz) |
|------|-------------|------------|
| 扫描线 | 525 线 (480 有效) | 625 线 (576 有效) |
| 有效像素宽度 | ~720 像素 | ~720 像素 |
| 帧率 | 29.97 fps (隔行 ~60 场/秒) | 25 fps (隔行 50 场/秒) |
| 模拟带宽 | ~4.2 MHz (NTSC) | ~5.5 MHz (PAL) |
| 色度副载波 | 3.579545 MHz | 4.433619 MHz |

### 6.2 PAL 时序参数（本项目使用）

| 参数 | 值 |
|------|-----|
| 行周期 | 64µs（标准），工程目标 63.97~64.00µs |
| 帧总行数 | 625 |
| 活跃行 | 576（行23-310 奇场 + 行336-623 偶场） |
| 同步电平 | Y ≈ 0 |
| 消隐电平 | Y ≈ 16 |
| 白电平 | Y ≈ 235 |

**每板采样时钟与行长（2026-10-04 定稿）⭐**：cvbs_core 行结构改为
运行时可配（前沿/同步/后沿/活跃四段），各板按**真外设可达的整数分频**
选择采样率，行时间误差全部压进 ±0.05%（PAL 容差内）：

| 板 | 外设 | 采样率推导 | 采样率 | 行样本数 | 行时间 | 误差 |
|----|------|-----------|--------|---------|--------|------|
| S3/S3N8 | LCD_CAM I80 | PLL160M ÷ 12 | 13.3333MHz | 853 | 63.975µs | -0.04% |
| CAM | I2S0+内置DAC | APB 80M ÷ 6 | 13.3333MHz | 853 | 63.975µs | -0.04% |
| C3 | I2S0 PDM raw | 160M÷4×170/510 | 13.3333MHz | 853 | 63.975µs | -0.04% |
| Pico | PIO SM0 | 125M÷1.8515625÷5 | 13.5063MHz | 864 | 63.970µs | -0.05% |

853 样本行的四段划分：前沿 12 / 同步 63 / 后沿 68 / 活跃 710（=853）。
864 样本行维持 12/64/68/720。全部用**整数分频**——避免小数分频的
周期性抖动（视频沿抖动会爬行）。原"像素时钟 10MHz"记载作废。

### 6.3 ITU-R BT.601 亮度公式

```
Y = 0.299R + 0.587G + 0.114B
整数近似: Y = (77*R + 150*G + 29*B) >> 8
```

### 6.4 显示分辨率与字号档位（2026-10-04 确定；2026-10-05 补字号标准）⭐

本项目采用**三档分辨率**，档位定义在各板硬件档案中：

| 档位 | 分辨率 | 扫描方式 | 用途 | 状态 |
|------|--------|---------|------|------|
| 控制台 / Console | **320x240** | 240p 逐行（复古主机标准） | CLI/NSH、安全模式 | 标准模式，可靠 |
| 常规 / Normal | **640x480** | 480i 隔行 | GUI 桌面默认 | 标准模式，可靠 |
| 最高 / Max | **1024x768** | 非标准 overspec（自定义行频约 48kHz） | 高分辨率实验 | **实验性** |

**各目标支持范围**：

| 目标 | 控制台 320x240 | 常规 640x480 | 最高 1024x768 |
|------|---------------|--------------|---------------|
| ESP32-S3 | ✔ | ✔ | ⚠️ 实验性（GDMA + 外部电阻网络） |
| ESP32-CAM | ✔ | ✔ | ✖（内置 DAC 带宽所限，上限即 640x480） |
| ESP32-C3 | ✔（cvbs_console 字符控制台） | ✖（400KB SRAM，appconfig 定档 320x240） | ✖ |
| Pico | ✔（cvbs_console 字符控制台） | ✖（264KB SRAM，appconfig 定档 320x240） | ✖ |

**可行性说明（重要）**：
- **320x240 (240p)**：复古游戏主机标准制式，所有电视/采集卡兼容，内存占用小（75KB@8bpp），控制台模式首选。
- **640x480 (480i)**：NTSC 标准隔行，实践验证可靠（bitluni 等项目），帧缓冲 ~300KB@8bpp 放 PSRAM。
- **1024x768**：**超出 CVBS 标准带宽**（需 ~50-65MHz 像素时钟，模拟带宽远超 4.2/5.5MHz），
  属于超规格（overspec）实验模式：需自定义非标行频/场频时序 + 外部高速电阻网络 DAC，
  仅部分显示器/采集卡可同步，画面清晰度受模拟带宽物理限制。
  作为规格最高档保留，**实际可用性以实机测试为准**，常规使用请选 640x480。
- 内置 8-bit DAC 有效带宽有限（ESP32-CAM），高像素时钟下失真明显——CAM 目标最高档即 640x480。

**字号（2026-10-05 定稿：全系唯一 12px）⭐**：嵌入式体积优先（用户定稿），
CLI 控制台与 GUI 桌面共用同一字号档：

| 项 | 值 | 说明 |
|----|-----|------|
| 字号 | **12px**（`lv_font_notosans_sc_12`，全系唯一） | 中文 Windows 3.2/95/98 界面**宋体 9pt=12px 内置点阵**的历史标准（考证于 2026-10-05；拉丁侧 Win3.x MS Sans Serif 8pt@96DPI≈11px 同级） |
| 控制台网格 | 12x14（`cvbs_console` CELL_W/H） | 320x240→26 列x17 行；640x480→53 列x34 行（glm53f 验收：两档可读，240p 为下限、实机 CRT 待抽验）。**基线规范**（2026-10-05 定）：cell 基线=底-3，字形底边=基线-ofs_y（与 LVGL 同语义），全角按 adv_w>CELL_W/2 推进 2 格——tests/host 位置矩阵 666 检查锁定 |
| 已废档 | ~~16px（lv_font_notosans_sc_16）~~ | 当日早版曾按 FC/SFC 16x16 分两档；因体积与单档原则收敛废除 |

> 考据结论备注：用户直觉的"7-9px"对应 DOS CGA 8x8（纯 ASCII 320x200 图形
> 模式）与 DOS VGA 文本 9x16 半宽单元——那是**拉丁等宽终端**的格子，不是
> 中文点阵字号；中文点阵历史下限即 12px（宋体小字号专用点阵）。7-9px
> 中文在任何老系统上都不存在。
> LVGL 内部默认字体同步为 montserrat_12（lv_conf.h LV_FONT_DEFAULT）。

### 6.5 多电平 CVBS 生成（ESP32-S3：LCD_CAM 并行口）⭐ 2026-10-04 定稿

复合视频至少需要 3 个电平（同步 0V / 消隐 ~0.3V / 白 ~1V），单根 GPIO 只有 2 个电平。
ESP32-S3 无内置 DAC；**S3 的 I2S（HW v2）没有并行/LCD 模式**，真并行
输出口是 **LCD_CAM 外设的 I80 模式**（soc_caps：SOC_LCDCAM_I80_LCD_SUPPORTED，
数据宽度可达 16-bit，GDMA 供数）——2026-10-04 起视频走 LCD_CAM，
音频继续走 I2S（互不抢占，GDMA 多通道）：

```
GDMA(通道) <- 环形描述符 <- 场信号缓冲（PSRAM，双场乒乓）
  -> LCD_CAM I80 模式，8-bit 总线，PCLK=PLL160M/12=13.3333MHz
     LCD_DATA_OUT0-3 -> GPIO2/15/16/17（4-bit 接线）
  -> 4-bit R-2R 电阻梯 -> 运放缓冲 -> 75Ω CVBS 输出
```

- 8-bit 总线只接**低 4 位**（DATA_OUT0-3 → GPIO2/15/16/17），样本字节 =
  cvbs_core 输出的 4-bit 量化电平（0-15 直出，无需移位；四电平以上全部保留）
- LCD_USER：`lcd_dout=1, lcd_cmd=0, lcd_dummy=0, lcd_always_out_en=1`——
  DOUT 相持续输出直到 DMA 断流，配合环形描述符 = 永续视频流
- GPIO 矩阵信号索引：LCD_CS=132、LCD_DATA_OUT0-15=133-148、
  LCD_DC=153、LCD_PCLK=154（PCLK 可不外接引脚，外设照常走节拍）
- 4-bit ladder = 16 电平，可生成灰度 CVBS（彩色需色度副载波调制，见 6.1）
- 原记载"I2S 并行输出"系误（S3 I2S 无并行能力），已按硅片事实修订

---

## 7. I2S 音频接口

### 7.1 ESP32 I2S 规格

| 参数 | 值 |
|------|-----|
| I2S 控制器数量 | 2 个 (I2S0: 0x3FF4F000, I2S1: 0x3FF6D000) |
| DAC 模式 | **仅 I2S0** 可直接输出到内置 DAC |
| 采样率 | 8kHz ~ 96kHz（典型：44100Hz、48000Hz）|
| 位深度 | 8/16/24/32 bit |
| 通道 | 单声道/双声道 |
| APB 时钟 | 80MHz |

### 7.2 ESP32-S3 I2S 规格

| 参数 | 值 |
|------|-----|
| I2S 控制器数量 | 2 个 (I2S0: 0x6000F000, I2S1: 0x6002D000) |
| DAC 模式 | **不支持**（无内置 DAC） |
| DMA | 通过 GDMA（非内置 DMA） |

---

## 8. WiFi 无线网络

| 参数 | 值 |
|------|-----|
| 标准 | IEEE 802.11 b/g/n |
| 频段 | 2.4 GHz（不支持 5GHz） |
| 带宽 | 20MHz / 40MHz |
| 最大速率 | 150 Mbps（802.11n, 1T1R） |
| 安全 | WPA/WPA2/WPA3 |

---

## 9. 蓝牙

### 9.1 ESP32-CAM 蓝牙 BLE HID

| 参数 | 值 |
|------|-----|
| 版本 | Bluetooth 4.2 BR/EDR + BLE |
| HID Profile | 支持键盘、鼠标设备 |
| 发射功率 | 最高 ~20dBm |
| 共存 | 与 WiFi 共用天线 |

### 9.2 ESP32-S3 USB HID / BLE HID

| 参数 | 值 |
|------|-----|
| USB OTG | 全速/高速 USB 2.0 |
| HID 支持 | USB HID 键盘、鼠标（GPIO19/20）+ BLE 5.0 HID |
| 状态指示 | WS2812 RGB LED（GPIO38/48，蓝色通道） |

---

## 10. SD 卡 / TF 卡

### 10.1 ESP32-S3 SD 卡

| 参数 | 值 |
|------|-----|
| 接口 | SPI 模式 |
| 引脚 | GPIO10(CS), GPIO11(MISO), GPIO13(MOSI), GPIO14(CLK) |
| 文件系统 | FAT32 / LittleFS |
| SPI 时钟 | 最高 25MHz |

### 10.2 ESP32-CAM SD 卡

| 参数 | 值 |
|------|-----|
| 接口 | SPI 模式（默认）/ 1-bit SDMMC（备选） |
| 引脚 (SPI) | **GPIO13(CS), GPIO15(MOSI), GPIO2(MISO), GPIO14(CLK)** |
| 引脚 (SDMMC) | GPIO14(CLK), GPIO15(CMD), GPIO2(D0) |
| 文件系统 | FAT32 / LittleFS |
| SPI 时钟 | 最高 25MHz |

---

## 11. FSK 磁带调制解调

### 11.1 Kansas City Standard (KCS) 标准

| 参数 | KCS 标准值 | 本项目采用值 |
|------|-----------|-------------|
| 波特率 | 300 baud | **300 baud** |
| 逻辑 0 (Space) | 1200 Hz（4 周期/bit） | **1200 Hz** |
| 逻辑 1 (Mark) | 2400 Hz（8 周期/bit） | **2400 Hz** |
| 采样率 | - | 44100 Hz |
| Goertzel N | - | 64 |

---

## 12. 硬件资源分配

### 12.1 核间分工（双核板全局规范，2026-10-04 定稿）

**CPU0 = 程序核（NSH/脚本/系统任务），CPU1 = 媒体核（图形/视频/音频/文件 IO）**
——kthread_create + sched_setaffinity 钉核实现
（esp32s3_retro.c / esp32_retro.c / rp2040_retro.c 同一规范）。

| 资源 | CPU 0（程序核） | CPU 1（媒体核） |
|------|----------------|----------------|
| NuttShell / 脚本引擎 | ✔ | — |
| LVGL 渲染 / CVBS 扫描 | — | ✔ |
| I2S/DAC 音频 / FSK | — | ✔ |
| SD 卡 / 文件 IO | — | ✔ |
| WiFi / NTP / Cron 等系统任务 | ✔ | — |
| 看门狗 | TIMG0 | TIMG1 |

### 12.2 各板核间差异

- **S3 / CAM**：按 12.1 全局规范（媒体核含 LVGL + CVBS + 音频）
- **C3**：单核，无分工
- **Pico**：Core0 = 程序，Core1 = 视频逐行生成（PIO）+ 文件 IO（见 3B.1）

### 12.3 ESP32-S3 内存分配（N16R8: 16MB Flash + 8MB PSRAM）

| 区域 | 大小 | 来源 |
|------|------|------|
| LVGL 堆 | 128KB | PSRAM |
| 图形帧缓冲 | ~300KB (640x480x1) | PSRAM |
| 字库缓存 | ~200KB | PSRAM |
| LVGL 颜色缓冲 | 32KB | PSRAM |
| NuttX 内核 | ~64KB | SRAM |
| 任务栈 | ~4-8KB/任务 | SRAM |

> N8R8 仅 Flash 为 8MB，其余布局相同；分区表需相应缩减
> （`CONFIG_RETRO_FLASH_8MB`）。

### 12.4 ESP32-CAM 内存分配（4MB PSRAM）

| 区域 | 大小 | 来源 |
|------|------|------|
| LVGL 堆 | 128KB | PSRAM |
| 图形帧缓冲 | ~300KB (640x480x1) | PSRAM |
| 字符缓存 | ~150KB | PSRAM（优化） |
| LVGL 颜色缓冲 | 32KB | PSRAM |
| NuttX 内核 | ~64KB | SRAM |
| 任务栈 | ~4-8KB/任务 | SRAM |
| **PSRAM 合计** | ~674KB | 4MB PSRAM 够用但较紧张 |

---

## 13. 真硬件外设使用规范（2026-10-04 定稿）⭐

### 13.1 "能用真外设就不用模拟"原则

音视频输入输出等实时流必须使用芯片真外设 + DMA，禁止 CPU 忙等/软模拟：

| 功能 | 板 | 真外设 | 状态 |
|------|----|--------|------|
| CVBS 视频出 | S3/S3N8 | LCD_CAM I80 + GDMA 环形链 | 本日定稿 |
| CVBS 视频出 | CAM | I2S0 + 内置 DAC1(GPIO25) + DMA | 已有，时钟改整除 |
| CVBS 视频出 | C3 | I2S0 PDM-TX raw + GDMA | 本日定稿 |
| CVBS 视频出 | Pico | PIO SM0 + DMA DREQ 节流（GP12-15） | 本日定稿，晚间移脚 |
| 音频出 | S3/S3N8 | I2S + GDMA + 外部 DAC（MAX98357A） | 已有 |
| 音频出 | CAM | I2S0 + 内置 DAC2(GPIO26) | 已有 |
| **WS2812 状态 LED** | S3/S3N8 | **RMT 外设（NuttX /dev/rmt0，ws2812_rmt.c）** | 本日实现（原 gpio 直驱点不亮，已换 RMT） |
| **FSK 磁带出** | S3/CAM | **audio_play_pcm() -> I2S/DAC DMA 环** | 本日接线（原 TODO 占位） |
| RTC I2C | S3/CAM | NuttX esp32s3/esp32 硬件 I2C 驱动 -> /dev/i2cN（引脚经 Kconfig 配到 5/6、21/22） | 本日登记（drv_rtc.c 已走 ioctl，补 appconfig 开关） |
| RTC I2C | C3 | 位摆软总线（NuttX 无 esp32c3 i2c 驱动，retro_bus 回退） | 登记于 NEXT_STEPS |
| PWM | S3/CAM/C3 | NuttX PWM 驱动（ESP32 系=LEDC 硬件定时器）-> /dev/pwmN | 已走硬件（retro_gpio_pwm_set） |
| NSH 字符 IO | C3/Pico | /dev/cvbscon（AV 屏）+ UART 输入泵 | 本日定稿 |
| 脚本 GPIO/总线 | 全系 | /dev/gpioN、/dev/i2cN、/dev/spiN、/dev/ttySN（缺驱动板回退软总线） | 本日定稿 |

**板上指示灯引脚避让原则（2026-10-04 晚新增，全局）**：
系统/教学 GPIO 分配优先避开板上指示灯引脚（S3=38/48 WS2812、
CAM=33 红 LED 与 4 闪光灯、C3=12/13 LED D4/D5、Pico=25 板载 LED），
仅当无其他合适引脚时才允许使用（如 CAM 全板无富余，4/33 仅作状态灯）。

DMA 环形描述符 = "永续流"模式：CPU 只在内容变化时改写场缓冲，
DMA 环持续输出，中断仅用于场翻转同步（suc_eof 心跳）。

### 13.2 CLI 文本编辑器 = GNU nano 8.4 移植（全系统一）

**GNU nano**（著名开源编辑器，GPL）**真源码移植**，非仿制：
`deps/nano/`（上游 nano-8.4 原版源码，tar.xz sha256 前 16 位
5ad29222bbd55624）；NuttX 无 ncurses，移植层 `src/nuttx/common/
nano_port/` 提供 mini-curses 垫片（curses.h/term.h/mini_curses.c，
ANSI 转义直出 + 转义键解码），config.h 裁剪（无色彩/无鼠标/无
speller fork；UTF-8 + 帮助 + 文件浏览器 + 行号保留）。AV 控制台
（cvbs_console 支持 CSI 子集）与串口终端均可跑。**全系所有开发板
的 CLI 编辑器都是 nano，vi 一律不编入**（CONFIG_SYSTEM_VI 禁用）。

### 13.3 retro_bus 总线兼容层（I2C/SPI/UART，machine 风格）

MicroPython machine 风格统一 API（教学一致性，同 retro_gpio 模式）：

```c
retro_bus_i2c_init(id, scl, sda, freq_hz)      /* I2C(id=N, scl=, sda=, freq=) */
retro_bus_i2c_scan(id, cb)                      /* scan() */
retro_bus_i2c_write(id, addr, buf, len)         /* writeto(addr, buf) */
retro_bus_i2c_read(id, addr, buf, len)          /* readfrom(addr, len) */
retro_bus_i2c_write_reg(id, addr, reg, val)     /* writeto_mem */
retro_bus_i2c_read_regs(id, addr, reg, buf, n)  /* readfrom_mem */
retro_bus_spi_init(id, mosi, miso, sck, freq)   /* SPI(id, baudrate=, ...) */
retro_bus_spi_xfer(id, tx, rx, len)             /* write_readinto */
retro_bus_uart_init(id, tx, rx, baud)           /* UART(id, baudrate=, tx=, rx=) */
retro_bus_uart_write / read / available
```

后端选择（运行时探测）：
1. NuttX 硬件驱动 /dev/i2cN（S3/CAM/Pico 有 i2c 驱动）——ioctl I2CIOC_TRANSFER
2. /dev/spiN（同上）——SPIIOC_TRANSFER 序列
3. /dev/ttySN（全系 UART 驱动）——termios 原始读写
4. 缺驱动的板（C3 无 i2c/spi 驱动）→ **软总线**（retro_gpio 位摆 I2C 100kHz /
   SPI 半双工），API 不变；硬驱动移植登记 NEXT_STEPS

### 13.4 板级脚本目录 + ROM 直跑（XIP 语义）

每板一个脚本源目录（编译期打包进固件 ROM）：

```
firmware/scripts/<板名>/*.bas|*.be|*.js   ← 每板自带演示/教学脚本
        │ tools/mkromfs.py（自研 ROMFS 生成器，无外部依赖）
        ▼
firmware/retro-apps/scripts_romfs.c        ← ROMFS 镜像 C 数组（链接进 .rodata=Flash）
        ▼
/rom/scripts（NuttX ROMFS 挂载）+ retro_scripts_rom.c 直查镜像内文件指针
        ▼
script <名字>   → 引擎从 Flash 指针直接执行（script_exec_buffer）
```

**XIP 语义（对标 MicroPython frozen bytecode）**：脚本源码常驻 Flash，
执行时**不整档读入 RAM**——引擎 parser 直接消费 Flash 指针；Berry 编译
产物（字节码）落在堆里，运行结束释放。RAM 占用 = 0（源码）+ 瞬时（编译）。
（进一步：NuttX CROMFS+mmap 可让字节码也 XIP，登记 NEXT_STEPS。）

---

## 变更记录 / Changelog

| 日期 | 内容 |
|------|------|
| **2026-10-05** | **文档全面修正为 retro-ws 五板定位：标题/开发策略去 ESP32 单板前缀；1.1 目标表补 s3n8 与 pico；1.2 档案表补 hw_rp2040_pico.h；6.4 支持范围表补 C3/Pico（320x240 字符控制台档）；12.1/12.2 核间分工改为全局规范（CPU0=程序核 / CPU1=媒体核，与代码 sched_setaffinity 实现同步，原"Core0 图形/Core1 系统"旧表作废）** |
| **2026-10-05** | **字号定稿（6.4）：全系唯一 12px（lv_font_notosans_sc_12，CLI/GUI 共用，嵌入式体积优先）；cvbs_console 网格 16x18→12x14（320x240→26x17、640x480→53x34）；16px 档废除；LVGL 默认字体 montserrat_12；glm53f 验收 640/240p 两档控制台+双桌面全 pass（240p 为可读下限，实机 CRT 抽验登记 NEXT_STEPS）** |
| **2026-10-04(深夜)** | **硬件全真外设收口：WS2812 改 RMT 真外设驱动（ws2812_rmt.c + /dev/rmt0，2.8 节）；FSK TX 接 audio_play_pcm() I2S/DAC DMA（13.1）；Pico CVBS 移脚 GP12-15（GP23=SMPS 省电脚会把电源纹波耦合进 R-2R 基准 + PIO 只能连续映射，3B.2A）；合宙 C3 板载 LED 修正为 GPIO12/13 **高电平**点亮、Flash 占用修正为 11=VDD_SPI+14-17 总线（GPIO12/13 DIO 模式可用，3A.2/3A.3）；C3 GPIO10 释放为教学脚；CAM 补 GPIO17=PSRAM CLK 禁用；S3 CVBS 注释由"I2S bitbang"修正为 LCD_CAM I80（6.5 对齐驱动实现 OUT0-3 低 4 位）；新增"板上指示灯引脚避让"全局原则（13.1）** |
| **2026-10-04(晚)** | **AV 输出全真硬件化定稿：S3=LCD_CAM I80 并行(6.5)、C3=I2S0 PDM raw 单脚(3A.2A)、Pico=PIO 4-bit 并行(3B.2A)、CAM 时钟改整除；每板采样时钟表(6.2)；新增 13 章真外设规范（nano 全系统一 / retro_bus / 板级脚本 ROM XIP）** |
| 2026-04-01 | 初版：验证 ESP32-S3 规格 |
| 2026-04-01 | 切换为 ESP32-CAM 方案：主控改为 ESP32、内置 DAC 支持、蓝牙 BLE HID 键鼠 |
| 2026-04-01 | 重构为多目标支持：同时支持 ESP32-S3 和 ESP32-CAM |
| 2026-04-01 | 完善寄存器地址映射：从 ESP-IDF 上游源码验证所有基地址和偏移 |
| 2026-04-01 | 修正 WDT WDTFEED 偏移 0x5C→0x60；修正 ESP32 I2S DAC 使能方式为 conf2.lcd_en |
| 2026-04-01 | 添加 GDMA/DMA 描述符结构、ESP32 I2S DAC 通道映射说明 |
| 2026-04-02 | 添加 BLE 状态 LED (GPIO4)、RTC I2C 引脚 (GPIO21/22) |
| 2026-04-02 | 添加 ESP32-S3 RTC I2C (GPIO17/18)、板载 RGB LED (GPIO45/48) |
| 2026-04-02 | 添加音频输入定义：ESP32-CAM GPIO34 (ADC)、ESP32-S3 GPIO38 (I2S) |
| 2026-04-02 | 重构 ESP32-S3 GPIO：改用 SPI SD 卡、软件模拟 I2C (RTC)、GPIO1 ADC 音频输入、GPIO21 复用为蓝牙状态 LED |
| **2026-10-04** | **开发板定为首选 ESP32-S3 N16R8/N8R8（开发模板），ESP32-CAM 为兼容目标** |
| **2026-10-04** | **修正 ESP32-S3：R8 八线 PSRAM 占用 GPIO35/36/37（用户按键改 GPIO7/8）；DevKitC-1 实为单颗 WS2812（v1.0=GPIO48 / v1.1=GPIO38），删除错误的 R=48/G=45/B=21 三引脚说法（GPIO45 为 strapping）；新增 strapping 引脚表（GPIO0/3/45/46）** |
| **2026-10-04** | **修正 ESP32-CAM：SD 卡 SPI 引脚更正为 CS=13/MOSI=15/MISO=2/CLK=14；GPIO33 为红色状态 LED（低电平点亮）而非用户按键；新增 GPIO16=PSRAM CS 禁用说明；补全摄像头引脚表** |
| **2026-10-04** | **分辨率定为三档：320x240 控制台（240p）/ 640x480 常规（480i）/ 1024x768 最高（实验性 overspec）；新增 6.4/6.5 节** |
| **2026-10-04** | **新增每板硬件档案文件（1.2 节）：hw_esp32s3_devkitc.h / hw_esp32cam_aithinker.h** |
| **2026-10-04** | **新增第三目标 esp32c3：合宙 ESP32-C3 核心板（经典款 CH343 / 简约款原生 USB），RISC-V CLI 工作站，3A 章；.rpk 包 Arch 架构隔离（all/xtensa/riscv/芯片名）** |
| **2026-10-04** | **三块板硬件档案新增"脚本 GPIO 占用表"（RETRO_GPIO_OCCUPIED_LIST）：第三方脚本访问系统占用脚直接报"已占用"返回 -EBUSY（retro_gpio.c）** |
| **2026-10-04** | **新增第四目标 Raspberry Pi Pico（RP2040）：本地无网 CLI 终端，双核分工 Core0=程序/Core1=文件IO，3B 章；双核分工原则同日定为全局规范（S3/CAM：Core0=程序，Core1=图形视频音频文件IO）** |

---

_本文档中所有参数均通过以下来源验证：_
- _[ESP32-S3 datasheet (Strapping/模组引脚)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/chip/esp32-s3.html)_
- _[ESP32-S3-DevKitC-1 v1.1 User Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/user-guide.html)_
- _[ESP32-S3 reg_base.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/register/soc/reg_base.h)_
- _[ESP32 reg_base.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32/register/soc/reg_base.h)_
- _[ESP32 timer_group_reg.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32/register/soc/timer_group_reg.h)_
- _[ESP32-S3 timer_group_reg.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/register/soc/timer_group_reg.h)_
- _[ESP32 I2S LL Driver](https://github.com/espressif/esp-idf/blob/master/components/hal/esp32/include/hal/i2s_ll.h)_
- _[GDMA dma_types.h](https://github.com/espressif/esp-idf/blob/master/components/hal/include/hal/dma_types.h)_
- _[ESP32-CAM AI-Thinker Pinout (Random Nerd Tutorials)](https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/)_
- _[bitluni ESP32 Composite Video](https://bitluni.net/esp32-composite-video)_
- _[ITU-R BT.601 标准](https://www.itu.int/rec/R-REC-BT.601)_
