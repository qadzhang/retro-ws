/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * hw_esp32cam_aithinker.h - ESP32-CAM AI-Thinker 硬件档案
 *
 * WHAT : ESP32-CAM AI-Thinker 硬件档案
 * WHY  : 兼容目标引脚唯一事实来源（HARDWARE.md 1.2）
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32/board/hw_esp32cam_aithinker.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 全部 GPIO 宏 + SD 两种模式 + 摄像头互斥说明 + 分辨率档位
 */

#ifndef __HW_ESP32CAM_AITHINKER_H
#define __HW_ESP32CAM_AITHINKER_H

/*==========================
 *  禁用引脚 / Reserved pins
 *==========================*/

/*
 * GPIO16      : PSRAM 片选（/CS），严禁使用
 * GPIO17      : PSRAM 时钟（CLK），严禁使用（不引出排针）
 * GPIO6-11    : SPI Flash 总线（模组内部）
 * GPIO34-39   : 仅输入，无内部上拉
 *
 * 板载 OV2640 摄像头与 CVBS+音频模式互斥（见下方 CAM_GPIO_* 注释），
 * 摄像头为可选功能，按需切换启用。
 */

/*==========================
 *  显示 / Display
 *==========================*/

/* CVBS 复合视频输出（I2S0 -> 内置 DAC1 -> 电阻网络 -> AV 接口） */
#define CVBS_GPIO_DATA          25    /* GPIO25 - DAC1, CVBS 复合视频输出 */
#define CVBS_DAC_CHANNEL        1     /* DAC 通道 1 */

/* 分辨率档位 / Resolution tiers（详见 HARDWARE.md 第 6 章） */
#define RES_CONSOLE_WIDTH      320   /* 控制台模式 320x240 (240p 逐行) */
#define RES_CONSOLE_HEIGHT     240
#define RES_NORMAL_WIDTH       640   /* 常规模式   640x480 (480i 隔行) */
#define RES_NORMAL_HEIGHT      480
#define RES_MAX_WIDTH          640   /* 最高模式 640x480（内置 DAC 带宽所限） */
#define RES_MAX_HEIGHT         480

/*==========================
 *  音频 / Audio
 *==========================*/

/* 音频输出（I2S0 时分复用 -> 内置 DAC2 -> 功放 -> 扬声器） */
#define AUDIO_GPIO_OUT          26    /* GPIO26 - DAC2, 音频输出 */
#define AUDIO_DAC_CHANNEL       2     /* DAC 通道 2 */

/* 音频输入（ADC 模拟麦克风） */
#define AUDIO_GPIO_IN           34    /* GPIO34 - ADC1_CH6（仅输入，无上拉） */

/*
 * 注意：ESP32 仅 I2S0 支持 DAC 直连模式；CVBS (DAC1) 与音频 (DAC2)
 * 共用 I2S0，需时分复用（详见 HARDWARE.md 3.5 节）。
 */

/*==========================
 *  存储 / Storage
 *==========================*/

/* SD 卡 — SPI 模式（默认） */
#define SD_SPI_GPIO_CS          13    /* GPIO13 - SPI CS   (SD DATA3/D3) */
#define SD_SPI_GPIO_MOSI        15    /* GPIO15 - SPI MOSI (SD CMD) */
#define SD_SPI_GPIO_MISO         2    /* GPIO2  - SPI MISO (SD DATA0/D0) */
#define SD_SPI_GPIO_CLK         14    /* GPIO14 - SPI CLK */

/* SD 卡 — 备选 1-bit SDMMC 模式（可腾出 GPIO4/12/13） */
#define SD_MMC_GPIO_CLK         14    /* GPIO14 - SDMMC CLK */
#define SD_MMC_GPIO_CMD         15    /* GPIO15 - SDMMC CMD */
#define SD_MMC_GPIO_D0           2    /* GPIO2  - SDMMC DATA0 */

/*==========================
 *  外设 / Peripherals
 *==========================*/

/* I2C（RTC 芯片；占用摄像头 PCLK/D3 引脚，与摄像头模式互斥）
 * 2026-10-04 晚改硬件路径：CONFIG_I2C_DRIVER + CONFIG_ESP32_I2C0 +
 * SCLPIN=22/SDAPIN=21 -> /dev/i2c0（GPIO21/22 恰为 ESP32 硬件 I2C 脚） */
#define I2C_GPIO_SCL            22    /* GPIO22 - I2C0 SCL（原摄像头 PCLK） */
#define I2C_GPIO_SDA            21    /* GPIO21 - I2C0 SDA（原摄像头 D3/Y5） */

/* UART 调试/烧录 */
#define UART_TX_GPIO            1     /* GPIO1 - UART0 TX */
#define UART_RX_GPIO            3     /* GPIO3 - UART0 RX */

/*==========================
 *  按键 / LED
 *==========================*/

/* 按键：板上仅 RST 键，GPIO0 需外接排针（低电平进入烧录模式） */
#define BTN_GPIO_BOOT            0    /* GPIO0 - Boot 按键（低电平有效） */
#define BTN_GPIO_USER           -1    /* 无独立用户按键 */

/* 板载红色状态 LED（低电平点亮，逻辑反相） */
#define LED_GPIO_STATUS         33    /* GPIO33 - 红色 LED, 0=亮 1=灭 */

/* 闪光灯 LED（与 SD 卡 DATA1 共享：SPI 模式下被 SD 占用） */
#define LED_GPIO_FLASH           4    /* GPIO4 - Flash LED */

/*
 * 蓝牙状态指示复用 Flash LED（GPIO4）：
 *   灭        = 已连接（正常工作）
 *   慢闪 2s   = 等待配对 / 未找到键鼠
 *   快闪 0.5s = 扫描中 / 正在连接
 */
#define BT_LED_GPIO              4

/*==========================
 *  摄像头（可选功能，仅记录引脚用于冲突分析）
 *==========================*/

/*
 * OV2640 为可选功能：默认 CVBS+音频模式下不启用摄像头，
 * 需要拍照功能时可切换到摄像头模式（二者互斥）：
 *   - GPIO25 = VSYNC  <-> CVBS DAC1 占用
 *   - GPIO26 = SIOD   <-> 音频 DAC2 占用
 *   - GPIO34 = Y8/D6  <-> 麦克风 ADC 占用
 * 完整映射（RNT）：D0/Y2=5, D1/Y3=18, D2/Y4=19, D3/Y5=21,
 * D4/Y6=36, D5/Y7=39, D6/Y8=34, D7/Y9=35, XCLK=0, PCLK=22,
 * VSYNC=25, HREF=23, SIOD=26, SIOC=27, PWDN=32。
 */
#define CAM_GPIO_PWDN           32
#define CAM_GPIO_RESET          -1
#define CAM_GPIO_XCLK            0
#define CAM_GPIO_SIOD           26
#define CAM_GPIO_SIOC           27
#define CAM_GPIO_VSYNC          25
#define CAM_GPIO_HREF           23
#define CAM_GPIO_PCLK           22
#define CAM_GPIO_Y2              5
#define CAM_GPIO_Y3             18
#define CAM_GPIO_Y4             19
#define CAM_GPIO_Y5             21
#define CAM_GPIO_Y6             36
#define CAM_GPIO_Y7             39
#define CAM_GPIO_Y8             34
#define CAM_GPIO_Y9             35

/*==========================
 *  存储容量 / Memory sizes
 *==========================*/

#define ESP32_FLASH_SIZE        (4 * 1024 * 1024)     /* 4MB QSPI Flash */
#define ESP32_PSRAM_SIZE        (4 * 1024 * 1024)     /* 4MB QSPI PSRAM */

/*==========================
 *  脚本 GPIO 占用表 / script GPIO occupied table
 *==========================*/

/*
 * WHAT : 第三方脚本禁用引脚（retro_gpio 直接报"已占用"，返回 -EBUSY）
 * WHY  : 本板引脚几乎全部被系统/SD/摄像头使用，防误触保系统稳定
 * WHO  : retro_gpio.c 的 gpio_is_blocked() 查询（经 board.c 实例化）
 * WHERE: 本档案 -> src/nuttx/esp32/board/board.c
 * WHEN : 2026-10-04 新增
 * HOW  : 无富余教学脚——教学 GPIO 场景建议使用 S3 或 C3 目标
 */
#define RETRO_GPIO_OCCUPIED_LIST                                           \
    {  0, "BOOT 模式 / 摄像头 XCLK"         },                              \
    {  1, "UART0 TX"                       },                              \
    {  2, "SD DATA0 / MISO"                },                              \
    {  3, "UART0 RX"                       },                              \
    {  4, "BT 状态 LED / 闪光灯"            },                              \
    {  5, "摄像头 D0/Y2 (可选)"             },                              \
    { 12, "SD DATA2"                       },                              \
    { 13, "SD DATA3 / SPI CS"              },                              \
    { 14, "SD CLK"                         },                              \
    { 15, "SD CMD / SPI MOSI"              },                              \
    { 16, "PSRAM CS (严禁)"                 },                              \
    { 17, "PSRAM CLK (严禁，不引出)"          },                              \
    { 18, "摄像头 D1/Y3 (可选)"             },                              \
    { 19, "摄像头 D2/Y4 (可选)"             },                              \
    { 21, "I2C RTC SDA"                    },                              \
    { 22, "I2C RTC SCL"                    },                              \
    { 23, "摄像头 HREF (可选)"              },                              \
    { 25, "CVBS DAC1 视频输出"              },                              \
    { 26, "音频 DAC2 输出"                  },                              \
    { 27, "摄像头 SIOC (可选)"              },                              \
    { 32, "摄像头 PWDN (可选)"              },                              \
    { 33, "红色状态 LED"                    },                              \
    { 34, "麦克风 ADC 输入"                 },                              \
    { 35, "摄像头 D7/Y9 (可选)"             },                              \
    { 36, "摄像头 D4/Y6 (可选)"             },                              \
    { 39, "摄像头 D5/Y7 (可选)"             }

#endif /* __HW_ESP32CAM_AITHINKER_H */
