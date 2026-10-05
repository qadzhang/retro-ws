/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * esp32.h - ESP32 寄存器定义
 *
 * WHAT : ESP32 寄存器定义
 * WHY  : 直接操作外设所需的基地址/位域
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/esp32/chip/esp32.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 按 ESP-IDF reg_base 验证（HARDWARE.md 第 3 章）
 */

/**
 * esp32.h - ESP32 芯片寄存器定义
 *
 * 包含 ESP32 (LX6) 芯片的主要寄存器地址和位域定义
 * 适用于 ESP32-CAM / ESP32-WROVER 等模块
 *
 * 寄存器地址来源: ESP32 Technical Reference Manual + esp-hal-3rdparty/reg_base.h
 */

#ifndef __ESP32_H
#define __ESP32_H

#include <stdint.h>   /* 下方 DMA 描述符位域用 uint32_t / for the DMA descriptor bitfields */

/*==========================
 *  寄存器基地址
 *  来源: soc/esp32/register/soc/reg_base.h
 *==========================*/

#define ESP32_UART0_BASE          0x3FF40000  /* DR_REG_UART_BASE */
#define ESP32_SPI0_BASE           0x3FF43000  /* DR_REG_SPI0_BASE */
#define ESP32_SPI1_BASE           0x3FF42000  /* DR_REG_SPI1_BASE */
#define ESP32_GPIO_BASE           0x3FF44000  /* DR_REG_GPIO_BASE */
#define ESP32_RTC_BASE            0x3FF48000  /* DR_REG_RTCCNTL_BASE */
#define ESP32_SENS_BASE           0x3FF48800  /* DR_REG_SENS_BASE */
#define ESP32_I2S0_BASE           0x3FF4F000  /* DR_REG_I2S_BASE */
#define ESP32_I2S1_BASE           0x3FF6D000  /* DR_REG_I2S1_BASE */
#define ESP32_UART1_BASE          0x3FF50000  /* DR_REG_UART1_BASE */
#define ESP32_BT_BASE             0x3FF51000  /* DR_REG_BT_BASE */
#define ESP32_I2C0_BASE           0x3FF53000  /* DR_REG_I2C_EXT_BASE */
#define ESP32_I2C1_BASE           0x3FF67000  /* DR_REG_I2C1_EXT_BASE */
#define ESP32_TIMERGROUP0_BASE    0x3FF5F000  /* DR_REG_TIMERGROUP0_BASE */
#define ESP32_TIMERGROUP1_BASE    0x3FF60000  /* DR_REG_TIMERGROUP1_BASE */
#define ESP32_SPI2_BASE           0x3FF64000  /* DR_REG_SPI2_BASE (HSPI) */
#define ESP32_SPI3_BASE           0x3FF65000  /* DR_REG_SPI3_BASE (VSPI) */
#define ESP32_SDMMC_BASE          0x3FF68000  /* DR_REG_SDMMC_BASE */
#define ESP32_UART2_BASE          0x3FF6E000  /* DR_REG_UART2_BASE */

/* ESP32 使用 DPORT 而非 SYSTEM */
#define ESP32_DPORT_BASE          0x3FF00000

/*==========================
 *  DPORT 系统寄存器
 *  偏移来源: soc/esp32/include/soc/dport_reg.h
 *==========================*/

#define ESP32_DPORT_CPU_PER_CONF_REG          (ESP32_DPORT_BASE + 0x010)
#define ESP32_DPORT_APPCPU_CTRL_REG           (ESP32_DPORT_BASE + 0x038)
#define ESP32_DPORT_APPCPU_CTRL_B_REG         (ESP32_DPORT_BASE + 0x03C)
#  define ESP32_DPORT_APPCPU_CLKGATE_EN        (1 << 0)
#  define ESP32_DPORT_APPCPU_RUNSTALL          (1 << 0)

/* CPU 时钟选择 */
#define ESP32_DPORT_CPU_CLK_SEL_REG           (ESP32_DPORT_BASE + 0x040)
#  define ESP32_CPU_CLK_SEL_PLL               0
#  define ESP32_CPU_CLK_SEL_XTAL              1
#  define ESP32_CPU_CLK_SEL_8M                2

/* APB 时钟选择 */
#define ESP32_DPORT_APB_CLK_SEL_REG           (ESP32_DPORT_BASE + 0x04C)

/*==========================
 *  RTC 寄存器
 *  偏移来源: soc/esp32/include/soc/rtc_cntl_reg.h
 *==========================*/

#define ESP32_RTC_CNTL_OPTIONS0_REG           (ESP32_RTC_BASE + 0x000)
#define ESP32_RTC_CNTL_STATE0_REG             (ESP32_RTC_BASE + 0x008)
#define ESP32_RTC_CNTL_WDTCONFIG0_REG         (ESP32_RTC_BASE + 0x048)
#define ESP32_RTC_CNTL_WDTCONFIG1_REG         (ESP32_RTC_BASE + 0x04C)
#define ESP32_RTC_CNTL_WDTCONFIG2_REG         (ESP32_RTC_BASE + 0x050)
#define ESP32_RTC_CNTL_WDTCONFIG3_REG         (ESP32_RTC_BASE + 0x054)
#define ESP32_RTC_CNTL_WDTCONFIG4_REG         (ESP32_RTC_BASE + 0x058)
#define ESP32_RTC_CNTL_WDTFEED_REG            (ESP32_RTC_BASE + 0x05C)
#define ESP32_RTC_CNTL_WDTWPROTECT_REG        (ESP32_RTC_BASE + 0x060)
#define ESP32_RTC_CNTL_SW_CPU_STALL_REG       (ESP32_RTC_BASE + 0x0BC)
#define ESP32_RTC_CNTL_RESET_STATE_REG        (ESP32_RTC_BASE + 0x030)

/* 软件复位位 */
#define ESP32_RTC_CNTL_SW_SYS_RESET           (1 << 31)
#define ESP32_RTC_CNTL_SW_CPU_RESET           (1 << 30)
#define ESP32_RTC_CNTL_SW_APPCPU_RESET        (1 << 9)

/* 复位原因 (RTC_CNTL_RESET_STATE_REG) */
#define ESP32_RTC_RESET_CAUSE_SHIFT           0
#define ESP32_RTC_RESET_CAUSE_MASK            0x1F

/*==========================
 *  GPIO 寄存器
 *  偏移来源: soc/esp32/include/soc/gpio_reg.h
 *==========================*/

#define ESP32_GPIO_OUT_REG                    (ESP32_GPIO_BASE + 0x004)
#define ESP32_GPIO_OUT_W1TS_REG               (ESP32_GPIO_BASE + 0x008)
#define ESP32_GPIO_OUT_W1TC_REG               (ESP32_GPIO_BASE + 0x00C)
#define ESP32_GPIO_ENABLE_REG                 (ESP32_GPIO_BASE + 0x020)
#define ESP32_GPIO_ENABLE_W1TS_REG            (ESP32_GPIO_BASE + 0x024)
#define ESP32_GPIO_ENABLE_W1TC_REG            (ESP32_GPIO_BASE + 0x028)
#define ESP32_GPIO_IN_REG                     (ESP32_GPIO_BASE + 0x03C)
#define ESP32_GPIO_STATUS_REG                 (ESP32_GPIO_BASE + 0x044)
#define ESP32_GPIO_STATUS_W1TS_REG            (ESP32_GPIO_BASE + 0x048)
#define ESP32_GPIO_STATUS_W1TC_REG            (ESP32_GPIO_BASE + 0x04C)
#define ESP32_GPIO_PIN(n)                     (ESP32_GPIO_BASE + 0x06C + (n) * 4)
#define ESP32_GPIO_FUNC_OUT_SEL_CFG_REG(n)    (ESP32_GPIO_BASE + 0x544 + (n) * 4)
#define ESP32_GPIO_FUNC_IN_SEL_CFG_REG(n)     (ESP32_GPIO_BASE + 0x178 + (n) * 4)

/* GPIO 数量 */
#define ESP32_GPIO_PIN_COUNT                  40   /* GPIO0-39, 其中 34-39 仅输入 */

/*==========================
 *  I2S 寄存器 (ESP32 经典版)
 *  偏移来源: soc/esp32/include/soc/i2s_reg.h
 *==========================*/

#define ESP32_I2S_BASE(n)                     ((n) == 0 ? ESP32_I2S0_BASE : ESP32_I2S1_BASE)

#define ESP32_I2S_CONF_REG(n)                 (ESP32_I2S_BASE(n) + 0x0000)
#define ESP32_I2S_CONF2_REG(n)                (ESP32_I2S_BASE(n) + 0x0018)
#define ESP32_I2S_SAMPLE_RATE_CONF_REG(n)     (ESP32_I2S_BASE(n) + 0x0008)
#define ESP32_I2S_CLKM_CONF_REG(n)            (ESP32_I2S_BASE(n) + 0x000C)
#define ESP32_I2S_FIFO_CONF_REG(n)            (ESP32_I2S_BASE(n) + 0x0010)
#define ESP32_I2S_RX_DESC_CONF_REG(n)         (ESP32_I2S_BASE(n) + 0x0014)
#define ESP32_I2S_OUT_LINK_REG(n)             (ESP32_I2S_BASE(n) + 0x0020)
#define ESP32_I2S_IN_LINK_REG(n)              (ESP32_I2S_BASE(n) + 0x0024)
#define ESP32_I2S_INT_ENA_REG(n)              (ESP32_I2S_BASE(n) + 0x0028)
#define ESP32_I2S_INT_RAW_REG(n)              (ESP32_I2S_BASE(n) + 0x002C)
#define ESP32_I2S_INT_CLR_REG(n)              (ESP32_I2S_BASE(n) + 0x0030)
#define ESP32_I2S_TIMING_REG(n)               (ESP32_I2S_BASE(n) + 0x0038)
#define ESP32_I2S_FIFO_DATA_REG(n)            (ESP32_I2S_BASE(n) + 0x003C)
#define ESP32_I2S_CONF_CHAN_REG(n)            (ESP32_I2S_BASE(n) + 0x0040)
#define ESP32_I2S_SIGLE_DATA_REG(n)           (ESP32_I2S_BASE(n) + 0x0044)

/*
 * I2S OUT_LINK 寄存器位域
 * I2S_OUT_LINK register bit fields
 *
 * 数值来源 / Source: deps/nuttx/arch/xtensa/src/esp32/hardware/esp32_i2s.h
 * (ESP32 Technical Reference Manual, I2S chapter):
 *   OUTLINK_ADDR    [19:0] DMA 描述符链首地址 / first descriptor address
 *   OUTLINK_STOP    bit 28 停止 DMA / stop DMA
 *   OUTLINK_START   bit 29 启动 DMA / start DMA
 *   OUTLINK_RESTART bit 30 重启 DMA / restart DMA
 */
#define ESP32_I2S_OUTLINK_ADDR_MASK           0x000FFFFF
#define ESP32_I2S_OUTLINK_STOP                (1u << 28)
#define ESP32_I2S_OUTLINK_START               (1u << 29)
#define ESP32_I2S_OUTLINK_RESTART             (1u << 30)

/*
 * I2S DMA 描述符 owner 位约定 / DMA descriptor owner bit convention
 * 1 = DMA 所有（硬件正在搬运）/ owned by DMA (hardware in progress)
 * 0 = CPU 所有（软件可改写）  / owned by CPU (software may rewrite)
 */
#define ESP32_I2S_DMA_DESC_OWNER_CPU          0
#define ESP32_I2S_DMA_DESC_OWNER_DMA          1

/* I2S CONF 寄存器位域 */
#define ESP32_I2S_TX_START                    (1 << 0)
#define ESP32_I2S_RX_START                    (1 << 1)
#define ESP32_I2S_TX_RESET                    (1 << 2)
#define ESP32_I2S_RX_RESET                    (1 << 3)
#define ESP32_I2S_TX_FIFO_RESET               (1 << 4)
#define ESP32_I2S_RX_FIFO_RESET               (1 << 5)
#define ESP32_I2S_TX_MSB_SHIFT                (1 << 6)
#define ESP32_I2S_RX_MSB_SHIFT                (1 << 7)
#define ESP32_I2S_TX_SHORT_SYNC               (1 << 12)
#define ESP32_I2S_RX_SHORT_SYNC               (1 << 13)
#define ESP32_I2S_TX_RIGHT_FIRST              (1 << 14)
#define ESP32_I2S_RX_RIGHT_FIRST              (1 << 15)
#define ESP32_I2S_LCD_EN                      (1 << 16)
#define ESP32_I2S_DAC_MODE_EN                 (1 << 17)
#define ESP32_I2S_DAC_RIGHT_ENA               (1 << 18)
#define ESP32_I2S_DAC_LEFT_ENA                (1 << 19)

/* I2S CONF2 寄存器位域 */
#define ESP32_I2S_LCD_TX_BCK_EN               (1 << 22)
#define ESP32_I2S_LCD_TX_WS_EN                (1 << 23)

/* I2S DMA 描述符 — ESP32 使用 32 位地址字段 */
typedef struct {
    volatile uint32_t eof    : 1;
    volatile uint32_t owner  : 1;
    volatile uint32_t length : 12;
    volatile uint32_t size   : 12;
    volatile uint32_t sosf   : 1;
    volatile uint32_t offset : 5;
    volatile uint32_t buf;             /* Buffer address (full 32-bit) */
    volatile uint32_t next;            /* Next descriptor address (full 32-bit) */
} esp32_dma_descriptor_t;

/*==========================
 *  SENS 寄存器 (DAC 控制)
 *  ESP32 的 DAC 通过 SENS 和 RTC 模块控制
 *==========================*/

#define ESP32_SENS_SAR_DAC_CTRL1_REG          (ESP32_SENS_BASE + 0x098)
#define ESP32_SENS_SAR_DAC_CTRL2_REG          (ESP32_SENS_BASE + 0x09C)

/* DAC 通道 */
#define ESP32_DAC_CHANNEL_1                   1   /* GPIO25 */
#define ESP32_DAC_CHANNEL_2                   2   /* GPIO26 */
#define ESP32_DAC_CHANNEL_BOTH                3

/*==========================
 *  Timer Group WDT 寄存器偏移
 *==========================*/

#define ESP32_TIMG_WDTCONFIG0_OFFSET          0x0048
#define ESP32_TIMG_WDTCONFIG1_OFFSET          0x004C
#define ESP32_TIMG_WDTCONFIG2_OFFSET          0x0050
#define ESP32_TIMG_WDTCONFIG3_OFFSET          0x0054
#define ESP32_TIMG_WDTCONFIG4_OFFSET          0x0058
#define ESP32_TIMG_WDTFEED_OFFSET             0x0060
#define ESP32_TIMG_WDTWPROTECT_OFFSET         0x0064

/* WDT 配置0 位 */
#define ESP32_WDT_STG0_INT                    (0x01 << 0)
#define ESP32_WDT_STG0_RESET_CPU              (0x02 << 0)
#define ESP32_WDT_STG0_RESET_SYS              (0x03 << 0)
#define ESP32_WDT_STG1_RESET_SYS              (0x03 << 2)
#define ESP32_WDT_EN                          (1 << 31)
#define ESP32_WDT_WKEY_VALUE                  0x50D83AA1

/*==========================
 *  中断源编号
 *  来源: soc/esp32/include/soc/interrupt_reg.h
 *==========================*/

#define ESP32_INTVEC_WMAC                     1
#define ESP32_INTVEC_BT_BB                    2
#define ESP32_INTVEC_SLC                      7
#define ESP32_INTVEC_SPI1                     8
#define ESP32_INTVEC_SPI2                     9
#define ESP32_INTVEC_I2S0                     14
#define ESP32_INTVEC_I2S1                     15
#define ESP32_INTVEC_UART                     16
#define ESP32_INTVEC_UART1                    17
#define ESP32_INTVEC_UART2                    18
#define ESP32_INTVEC_SDIO_HOST                19
#define ESP32_INTVEC_GPIO                     24
#define ESP32_INTVEC_CPU_WDT                  26
#define ESP32_INTVEC_RTC_CORE                 27
#define ESP32_INTVEC_RTC_WDT                  28

/*==========================
 *  寄存器访问宏
 *==========================*/

#ifndef getreg32
#define getreg32(addr)           (*(volatile uint32_t *)(addr))
#endif
#ifndef putreg32
#define putreg32(val, addr)      (*(volatile uint32_t *)(addr) = (val))
#endif
#ifndef modreg32
#define modreg32(addr, mask, val) putreg32((getreg32(addr) & ~(mask)) | (val), addr)
#endif

/*==========================
 *  芯片特性宏
 *==========================*/

#define ESP32_HAS_DAC              /* ESP32 内置双通道 DAC (GPIO25/26) */
#define ESP32_HAS_PSRAM            /* 支持 SPI PSRAM */
#define ESP32_HAS_BT               /* 支持蓝牙 */
#define ESP32_HAS_WIFI             /* 支持 WiFi */

/* 内存映射 */
#define ESP32_DATA_SRAM_BASE       0x3FFAE000
#define ESP32_DATA_SRAM_SIZE       (520 * 1024)
#define ESP32_PSRAM_BASE           0x3F800000
#define ESP32_FLASH_BASE           0x3F400000

#endif /* __ESP32_H */
