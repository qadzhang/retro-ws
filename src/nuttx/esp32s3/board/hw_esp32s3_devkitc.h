/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * hw_esp32s3_devkitc.h - DevKitC-1 N16R8/N8R8 硬件档案
 *
 * WHAT : DevKitC-1 N16R8/N8R8 硬件档案
 * WHY  : 首选板引脚唯一事实来源（HARDWARE.md 1.2）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/board/hw_esp32s3_devkitc.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 全部 GPIO 宏 + strapping/模组保留脚说明 + 分辨率档位
 */

#ifndef __HW_ESP32S3_DEVKITC_H
#define __HW_ESP32S3_DEVKITC_H

/*==========================
 *  禁用引脚 / Reserved pins
 *==========================*/

/*
 * 以下引脚被模组内部占用或为 strapping 引脚，严禁在驱动中使用：
 *
 * GPIO26-GPIO32 : SPI Flash 总线占用（模组内部走线）
 * GPIO35/36/37  : Octal PSRAM 占用（SPICS1/SPIDQS/SPICLK_N）
 *                 —— N16R8/N8R8 均为 R8 八线 PSRAM，此 3 脚不可用
 * GPIO0         : strapping（Boot 模式），已由 BOOT 按键占用
 * GPIO3         : strapping（JTAG 信号源选择），保留
 * GPIO45        : strapping（VDD_SPI 电压选择），保留
 * GPIO46        : strapping（Boot 模式/ROM 日志），保留
 */

/*==========================
 *  显示 / Display
 *==========================*/

/* CVBS 复合视频输出（LCD_CAM I80 并行口 + GDMA -> 4-bit R-2R 电阻梯） */
#define CVBS_GPIO_DATA          2    /* GPIO2 - LCD_DATA_OUT0（LSB） */

/*
 * 4-bit R-2R 电阻梯（HARDWARE.md 6.5）：GPIO2/15/16/17 分别接
 * LCD_CAM 数据口 DATA_OUT0-3，样本字节 = 4-bit 量化电平直出；
 * DMA 环形描述符永续供数，CPU 零忙等。
 */
#define CVBS_LADDER_PIN0        2    /* GPIO2  - LCD_DATA_OUT0 */
#define CVBS_LADDER_PIN1        15   /* GPIO15 - LCD_DATA_OUT1 */
#define CVBS_LADDER_PIN2        16   /* GPIO16 - LCD_DATA_OUT2 */
#define CVBS_LADDER_PIN3        17   /* GPIO17 - LCD_DATA_OUT3 */

/* 分辨率档位 / Resolution tiers（详见 HARDWARE.md 第 6 章） */
#define RES_CONSOLE_WIDTH      320   /* 控制台模式 320x240 (240p 逐行) */
#define RES_CONSOLE_HEIGHT     240
#define RES_NORMAL_WIDTH       640   /* 常规模式   640x480 (480i 隔行) */
#define RES_NORMAL_HEIGHT      480
#define RES_MAX_WIDTH         1024   /* 最高模式  1024x768 (实验性 overspec) */
#define RES_MAX_HEIGHT         768

/*==========================
 *  音频 / Audio
 *==========================*/

/* I2S 音频输出（外部 I2S DAC，如 MAX98357A） */
#define AUDIO_I2S_WS           40    /* GPIO40 - WS  (MTDO, 硬件 JTAG 复用) */
#define AUDIO_I2S_SCK          41    /* GPIO41 - SCK (MTDI, 硬件 JTAG 复用) */
#define AUDIO_I2S_SDO          42    /* GPIO42 - SDO (MTMS, 硬件 JTAG 复用) */

/*
 * 注意：GPIO39-42 为硬件 JTAG 引脚（MTCK/MTDO/MTDI/MTMS）。
 * 本设计将 40/41/42 用作 I2S 音频后，硬件 JTAG 不可用，
 * 调试请改用 UART0 (GPIO43/44)。
 */

/* 音频输入（ADC 模拟麦克风，如 MAX9814） */
#define AUDIO_GPIO_IN           1    /* GPIO1 - ADC1_CH0 */

/*==========================
 *  输入设备 / Input devices
 *==========================*/

/* USB OTG（USB HID 键盘鼠标） */
#define USB_GPIO_DM            19    /* GPIO19 - USB D-（与内置 USB-JTAG 复用） */
#define USB_GPIO_DP            20    /* GPIO20 - USB D+（与内置 USB-JTAG 复用） */

/* 按键输入 */
#define BTN_GPIO_START          0    /* GPIO0 - BOOT/Start 按键（strapping） */
#define BTN_GPIO_USER1          7    /* GPIO7 - 用户按键 1 */
#define BTN_GPIO_USER2          8    /* GPIO8 - 用户按键 2 */

/*==========================
 *  存储 / Storage
 *==========================*/

/* SD 卡（SPI 模式，VSPI/FSPI 经 GPIO 矩阵任意分配） */
#define SD_SPI_GPIO_CS         10    /* GPIO10 - SPI CS */
#define SD_SPI_GPIO_MOSI       13    /* GPIO13 - SPI MOSI */
#define SD_SPI_GPIO_MISO       11    /* GPIO11 - SPI MISO */
#define SD_SPI_GPIO_CLK        14    /* GPIO14 - SPI CLK */

/*==========================
 *  外设 / Peripherals
 *==========================*/

/* I2C（RTC 芯片：DS1307/PCF8563/RV-3028-C7）
 * 2026-10-04 晚改硬件路径：CONFIG_I2C_DRIVER + CONFIG_ESP32S3_I2C0 +
 * SCLPIN=5/SDAPIN=6 -> /dev/i2c0（drv_rtc.c 走 I2CIOC_TRANSFER） */
#define I2C_GPIO_SCL            5    /* GPIO5  - I2C0 SCL */
#define I2C_GPIO_SDA            6    /* GPIO6  - I2C0 SDA */

/* UART 调试 */
#define UART_TX_GPIO           43    /* GPIO43 - UART0 TX */
#define UART_RX_GPIO           44    /* GPIO44 - UART0 RX */

/*==========================
 *  LED
 *==========================*/

/* 板载可寻址 WS2812 RGB LED（单线协议，需 RMT 外设驱动） */
#ifdef CONFIG_RETRO_DEVKITC_V10
#  define LED_GPIO_RGB         48    /* v1.0: GPIO48 */
#else
#  define LED_GPIO_RGB         38    /* v1.1: GPIO38（默认） */
#endif

/*
 * 蓝牙状态指示复用 WS2812 RGB LED（蓝色通道）：
 *   灭       = 已连接（正常工作）
 *   慢闪 2s  = 等待配对 / 未找到键鼠
 *   快闪 0.5s = 扫描中 / 正在连接
 *
 * 驱动路径（2026-10-04 晚定稿，HARDWARE.md 2.8）：
 * CONFIG_RMT + CONFIG_RMTCHAR + CONFIG_ESP_RMT -> /dev/rmt0
 * -> src/nuttx/esp32s3/driver/ws2812_rmt.c（RMT 硬件外设编码，
 *    gpio_set_level() 无法点亮 WS2812，禁止软件位摆）。
 */
#define BT_LED_GPIO       LED_GPIO_RGB

/*==========================
 *  存储容量 / Memory sizes
 *==========================*/

/* N16R8=16MB / N8R8=8MB，由 Kconfig 选择，默认 N16R8 */
#ifdef CONFIG_RETRO_FLASH_8MB
#  define ESP32S3_FLASH_SIZE   (8 * 1024 * 1024)
#else
#  define ESP32S3_FLASH_SIZE   (16 * 1024 * 1024)
#endif

/* R8 = 8MB Octal PSRAM（两型号相同） */
#define ESP32S3_PSRAM_SIZE     (8 * 1024 * 1024)

/*==========================
 *  脚本 GPIO 占用表 / script GPIO occupied table
 *==========================*/

/*
 * WHAT : 第三方脚本禁用引脚（retro_gpio 直接报"已占用"，返回 -EBUSY）
 * WHY  : 防止教学脚本误触系统引脚（CVBS/SD/USB/strapping）搞挂系统
 * WHO  : retro_gpio.c 的 gpio_is_blocked() 查询（经 board.c 实例化）
 * WHERE: 本档案 -> src/nuttx/esp32s3/board/board.c
 * WHEN : 2026-10-04 新增
 * HOW  : 表外引脚脚本可用（推荐教学脚：4/7/8/9/12/18/21/33/34/39/47，
 *        其中 7/8 为用户按键；板上 WS2812=38/48 遵循指示灯脚避让原则不入表）
 */
#define RETRO_GPIO_OCCUPIED_LIST                                           \
    {  0, "BOOT 按键 / strapping"          },                              \
    {  1, "ADC 麦克风输入"                  },                              \
    {  2, "CVBS 视频输出 (LCD_CAM D0)"       },                              \
    {  3, "strapping (JTAG 选择)"           },                              \
    {  5, "I2C RTC SCL"                    },                              \
    {  6, "I2C RTC SDA"                    },                              \
    { 10, "SD 卡 SPI CS"                   },                              \
    { 11, "SD 卡 SPI MISO"                 },                              \
    { 13, "SD 卡 SPI MOSI"                 },                              \
    { 14, "SD 卡 SPI CLK"                  },                              \
    { 15, "CVBS R-2R 梯 (LCD_CAM D1)"        },                              \
    { 16, "CVBS R-2R 梯 (LCD_CAM D2)"        },                              \
    { 17, "CVBS R-2R 梯 (LCD_CAM D3)"        },                              \
    { 19, "USB D-"                         },                              \
    { 20, "USB D+"                         },                              \
    { 26, "模组 SPI Flash"                  },                              \
    { 27, "模组 SPI Flash"                  },                              \
    { 28, "模组 SPI Flash"                  },                              \
    { 29, "模组 SPI Flash"                  },                              \
    { 30, "模组 SPI Flash"                  },                              \
    { 31, "模组 SPI Flash"                  },                              \
    { 32, "模组 SPI Flash"                  },                              \
    { 35, "Octal PSRAM"                    },                              \
    { 36, "Octal PSRAM"                    },                              \
    { 37, "Octal PSRAM"                    },                              \
    { 38, "WS2812 状态 LED (v1.1)"          },                              \
    { 40, "I2S 音频 WS"                    },                              \
    { 41, "I2S 音频 SCK"                   },                              \
    { 42, "I2S 音频 SDO"                   },                              \
    { 43, "UART0 调试 TX"                  },                              \
    { 44, "UART0 调试 RX"                  },                              \
    { 45, "strapping (VDD_SPI)"            },                              \
    { 46, "strapping (Boot 模式)"           },                              \
    { 48, "WS2812 状态 LED (v1.0)"          }

#endif /* __HW_ESP32S3_DEVKITC_H */
