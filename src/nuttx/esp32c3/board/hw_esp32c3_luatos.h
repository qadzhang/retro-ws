/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * hw_esp32c3_luatos.h - 合宙 ESP32-C3 核心板硬件档案（两款兼容）
 *                         Luatos ESP32-C3 core board hardware profile
 *
 * WHAT : 低资源/低价格第三目标（CLI 工作站）的引脚唯一事实来源
 * WHY  : C3 是 RISC-V 单核（与 Xtensa 二进制不互通），定位纯 CLI +
 *        .rpk 包管理器装软件（无 LVGL/CVBS/摄像头，400KB SRAM 无 PSRAM）
 * WHO  : src/nuttx/esp32c3/ 全部板级代码
 * WHERE: retro-ws/src/nuttx/esp32c3/board/hw_esp32c3_luatos.h
 * WHEN : 2026-10-04 新增（HARDWARE.md 第 3A 章）；同日晚按 LuatOS wiki
 *        修正 LED 极性（GPIO12/13 高有效）与 Flash 占用（11=VDD_SPI、
 *        14-17=总线），GPIO10 释放为教学脚，CVBS 注释对齐 PDM 单脚方案
 * HOW  : 两款板仅烧录/调试通道不同，其余引脚一致：
 *   - 经典款：板载 CH343 USB 转串口 -> UART0 (GPIO21 TX / GPIO20 RX)
 *   - 简约款：无串口芯片，Type-C 直连原生 USB（GPIO18 D- / GPIO19 D+，
 *     芯片内置 USB-Serial-JTAG，可直接下载/调试）
 *   由 CONFIG_RETRO_LUATOS_C3_UARTBRIDGE 选择控制台通道。
 *
 * 引脚事实来源：LuatOS wiki ESP32C3-CORE 页 + ESP32-C3 datasheet。
 */

#ifndef __HW_ESP32C3_LUATOS_H
#define __HW_ESP32C3_LUATOS_H

/*==========================
 *  芯片约束 / Chip constraints
 *==========================*/

/*
 * - CPU: RISC-V RV32IMC 单核 @160MHz（非 Xtensa！.rpk 包须 Arch 匹配）
 * - SRAM: 400KB，**不支持 PSRAM**——大帧缓冲放不下，故 LVGL 桌面不在
 *   本目标（但 /dev/cvbscon 字符控制台经 I2S0 PDM 上 AV 屏，全系标配）
 * - Flash: 板载外置 4MB SPI（**DIO 模式**，自编译固件必须配 DIO，
 *   否则 QIO 会征用 GPIO12/13 与板上 LED 冲突）
 * - WiFi 802.11 b/g/n + BLE 5，板载 PCB 天线
 * - 板尺寸 21x51mm 邮票孔，15 路数字 IO，5 通道 12-bit ADC
 */

/*==========================
 *  禁用引脚 / Reserved pins
 *==========================*/

/*
 * GPIO11       : VDD_SPI——本板 Flash 电源直挂 3.3V，严禁当 IO 用
 * GPIO14-17    : SPI Flash 总线占死（SPICS0=14 / SPICLK=15 / SPID=16 /
 *                SPIQ=17，DIO 模式，不引出）
 * GPIO12/13    : SPIHD/SPIWP 仅 QIO 模式占用——本板 DIO 可当 GPIO，
 *                但板上接 LED D4/D5，按指示灯脚避让原则不派给脚本
 * GPIO2  : strapping——启动时须浮空或低电平
 * GPIO8  : strapping——启动时须高电平（JTAG/SDIO 选择）；烧录期勿拉低
 * GPIO9  : strapping——BOOT 按键（高=Flash 启动，低=下载模式）
 * GPIO18/19: 原生 USB D-/D+（简约款为唯一烧录/调试通道，勿挪用）
 *
 * （2026-10-04 晚修正：原"GPIO11-17 全为 Flash 占用"有误，见
 *  HARDWARE.md 3A.2——LuatOS wiki 管脚表 4-3）
 */

/*==========================
 *  控制台 / Console（两款板差异点）
 *==========================*/

#ifdef CONFIG_RETRO_LUATOS_C3_UARTBRIDGE
/* 经典款：CH343 USB 转串口 -> UART0 */
#  define CONSOLE_UART_TX      21    /* GPIO21 - UART0 TX -> CH343 RX */
#  define CONSOLE_UART_RX      20    /* GPIO20 - UART0 RX <- CH343 TX */
#else
/* 简约款：原生 USB Serial/JTAG（无 GPIO 复用，芯片内置外设） */
#  define CONSOLE_USE_USB_JTAG 1
#endif

#define CONSOLE_BAUD           115200

/*==========================
 *  按键 / LED
 *==========================*/

/* 板载按键：BOOT=GPIO9（strapping），RST=EN（直接复位，非 GPIO） */
#define BTN_GPIO_BOOT           9     /* GPIO9 - BOOT 按键（低电平有效） */

/* 板载状态 LED（LuatOS wiki 管脚表：D4=GPIO12、D5=GPIO13，
 * 均**高电平点亮**；D4 靠 USB 座左侧、D5 靠右侧。指示灯脚避让：不入教学脚） */
#define LED_GPIO_STATUS        12     /* GPIO12 - 红色 LED，1=亮 0=灭 */
#define LED_GPIO_STATUS2       13     /* GPIO13 - 第二颗 LED，1=亮 0=灭 */

/*==========================
 *  存储 / Storage
 *==========================*/

/*
 * SD 卡（SPI 模式，推荐接线——邮票孔任意空闲脚经 GPIO 矩阵可重映射）：
 * 全部选用非 strapping、非 Flash、非 USB 的空闲脚
 */
#define SD_SPI_GPIO_CS          7     /* GPIO7  - SPI CS */
#define SD_SPI_GPIO_MOSI        6     /* GPIO6  - SPI MOSI */
#define SD_SPI_GPIO_MISO        5     /* GPIO5  - SPI MISO */
#define SD_SPI_GPIO_CLK         4     /* GPIO4  - SPI CLK */

/*==========================
 *  外设 / Peripherals
 *==========================*/

/* 软 I2C（RTC 等，推荐空闲脚；NuttX 暂无 esp32c3 硬件 I2C 驱动，
 * retro_bus/RTC 走位摆回退，硬驱动登记 NEXT_STEPS 52） */
#define I2C_GPIO_SCL            3     /* GPIO3 - I2C SCL */
#define I2C_GPIO_SDA            0     /* GPIO0 - I2C SDA */

/*==========================
 *  AV 视频输出（全系标配，HARDWARE.md 3A.2A）
 *==========================*/

/* I2S0 PDM-TX raw 单脚 sigma-delta（真外设 + GDMA，CPU 零忙等） */
#define CVBS_PDM_GPIO           1     /* GPIO1 - PDM 数据脚 @13.3333MHz */

/*==========================
 *  本目标不具备 / Not available
 *==========================*/

/*
 * 无 LVGL 桌面、无音频 DAC、无摄像头、无 CPython——定位 CLI 工作站：
 * NSH + WiFi + NTP + Cron + .rpk 包管理器 + my-basic/Berry 脚本 +
 * CLI 拼音输入法 + /dev/cvbscon 字符控制台（AV 屏点阵中文）。
 */

/*==========================
 *  存储容量 / Memory sizes
 *==========================*/

#define ESP32C3_FLASH_SIZE      (4 * 1024 * 1024)    /* 4MB 外置 SPI */
#define ESP32C3_SRAM_SIZE       (400 * 1024)          /* 400KB，无 PSRAM */

/*==========================
 *  脚本 GPIO 占用表 / script GPIO occupied table
 *==========================*/

/*
 * WHAT : 第三方脚本禁用引脚（retro_gpio 直接报"已占用"，返回 -EBUSY）
 * WHY  : 防教学脚本误触系统脚/板上指示灯（2026-10-04 晚按 LuatOS wiki 修正）
 * WHO  : retro_gpio.c 的 gpio_is_blocked() 查询（经 board.c 实例化）
 * WHERE: 本档案 -> src/nuttx/esp32c3/board/board.c
 * WHEN : 2026-10-04 新增；同日晚重排（LED/Flash 修正、GPIO10 释放）
 * HOW  : 表外引脚可自由读写。推荐教学脚：**GPIO10（首选，唯一完全
 *        空闲的非 strapping 脚）**；GPIO2 可用但启动时须浮空/低电平。
 */
#define RETRO_GPIO_OCCUPIED_LIST                                           \
    {  0, "I2C RTC SDA (推荐接线)"          },                              \
    {  1, "CVBS PDM 视频输出 (I2S0)"         },                              \
    {  2, "strapping (启动须低/浮空)"        },                              \
    {  3, "I2C RTC SCL (推荐接线)"          },                              \
    {  4, "SD 卡 SPI CLK (推荐接线)"        },                              \
    {  5, "SD 卡 SPI MISO (推荐接线)"       },                              \
    {  6, "SD 卡 SPI MOSI (推荐接线)"       },                              \
    {  7, "SD 卡 SPI CS (推荐接线)"         },                              \
    {  8, "strapping (启动须高)"             },                              \
    {  9, "BOOT 按键 / strapping"           },                              \
    { 11, "VDD_SPI (Flash 电源，禁用)"       },                              \
    { 12, "板载 LED D4 (指示灯避让)"         },                              \
    { 13, "板载 LED D5 (指示灯避让)"         },                              \
    { 14, "SPI Flash SPICS0"               },                              \
    { 15, "SPI Flash SPICLK"               },                              \
    { 16, "SPI Flash SPID"                 },                              \
    { 17, "SPI Flash SPIQ"                 },                              \
    { 18, "原生 USB D-"                    },                              \
    { 19, "原生 USB D+"                    },                              \
    { 20, "UART0 RX (经典款 CH343)"         },                              \
    { 21, "UART0 TX (经典款 CH343)"         }

#endif /* __HW_ESP32C3_LUATOS_H */
