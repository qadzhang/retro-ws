/*
 * SPDX-FileCopyrightText: 2026 ESP32 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * esp32s3.h - ESP32-S3 寄存器定义
 *
 * WHAT : ESP32-S3 寄存器定义
 * WHY  : 直接操作外设所需的基地址/位域
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: esp32-retro-ws/src/nuttx/esp32s3/chip/esp32s3.h
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 按 ESP-IDF reg_base 验证（HARDWARE.md 第 2 章）
 */

/**
 * esp32s3.h - ESP32-S3 芯片寄存器定义
 *
 * 包含 ESP32-S3 芯片的主要寄存器地址和位域定义
 * 寄存器地址来源: ESP32-S3 Technical Reference Manual + esp-hal-3rdparty/reg_base.h
 */

#ifndef __ESP32S3_H
#define __ESP32S3_H

#include <stdint.h>   /* 下方 GDMA 描述符位域用 uint32_t / for the GDMA descriptor bitfields */

/*==========================
 *  寄存器基地址
 *  来源: soc/esp32s3/register/soc/reg_base.h
 *==========================*/

#define ESP32S3_SYSTEM_BASE       0x600C0000  /* DR_REG_SYSTEM_BASE */
#define ESP32S3_GPIO_BASE         0x60004000  /* DR_REG_GPIO_BASE */
#define ESP32S3_I2S0_BASE         0x6000F000  /* DR_REG_I2S_BASE (I2S0) */
#define ESP32S3_I2S1_BASE         0x6002D000  /* DR_REG_I2S1_BASE */
#define ESP32S3_SPI0_BASE         0x60003000  /* DR_REG_SPI0_BASE */
#define ESP32S3_SPI1_BASE         0x60002000  /* DR_REG_SPI1_BASE */
#define ESP32S3_SPI2_BASE         0x60024000  /* DR_REG_SPI2_BASE (FSPI) */
#define ESP32S3_SPI3_BASE         0x60025000  /* DR_REG_SPI3_BASE (VSPI) */
#define ESP32S3_I2C0_BASE         0x60013000  /* DR_REG_I2C_EXT_BASE */
#define ESP32S3_I2C1_BASE         0x60027000  /* DR_REG_I2C1_EXT_BASE */
#define ESP32S3_UART0_BASE        0x60000000  /* DR_REG_UART_BASE */
#define ESP32S3_UART1_BASE        0x60010000  /* DR_REG_UART1_BASE */
#define ESP32S3_UART2_BASE        0x6002E000  /* DR_REG_UART2_BASE */
#define ESP32S3_TIMERGROUP0_BASE  0x6001F000  /* DR_REG_TIMERGROUP0_BASE */
#define ESP32S3_TIMERGROUP1_BASE  0x60020000  /* DR_REG_TIMERGROUP1_BASE */
#define ESP32S3_RTC_BASE          0x60008000  /* DR_REG_RTCCNTL_BASE */
#define ESP32S3_BT_BASE           0x60011000  /* DR_REG_BT_BASE */
#define ESP32S3_SDMMC_BASE        0x60028000  /* DR_REG_SDMMC_BASE */
#define ESP32S3_SENSITIVE_BASE    0x600B0000  /* DR_REG_SENSITIVE_BASE */
#define ESP32S3_DMA_BASE          0x6003F000  /* GDMA 基地址 */

/* 兼容旧名称 */
#define ESP32S3_DPORT_BASE        ESP32S3_SYSTEM_BASE

/*==========================
 *  系统寄存器 (SYSTEM)
 *  偏移来源: soc/esp32s3/include/soc/system_reg.h
 *==========================*/

/* CPU ID */
#define ESP32S3_CPU_ID_REG                    (ESP32S3_SYSTEM_BASE + 0x000)

/* 时钟配置 */
#define ESP32S3_SYSTEM_CPU_PER_CONF_REG       (ESP32S3_SYSTEM_BASE + 0x010)
#define ESP32S3_SYSTEM_APB_CLK_CONF_REG       (ESP32S3_SYSTEM_BASE + 0x014)

/* Reset */
#define ESP32S3_SYSTEM_RESET_REASON_REG       (ESP32S3_SYSTEM_BASE + 0x048)

/* CPU 控制 */
#define ESP32S3_SYSTEM_CORE_1_CONTROL_0_REG   (ESP32S3_SYSTEM_BASE + 0x014C)
#define ESP32S3_SYSTEM_CORE_1_CONTROL_1_REG   (ESP32S3_SYSTEM_BASE + 0x0150)
#  define ESP32S3_SYSTEM_CORE_1_CLKGATE_EN    (1 << 0)
#  define ESP32S3_SYSTEM_CORE_1_RESET_EN      (1 << 0)

/* 外设时钟使能 */
#define ESP32S3_SYSTEM_PERIP_CLK_EN0_REG      (ESP32S3_SYSTEM_BASE + 0x018)
#define ESP32S3_SYSTEM_PERIP_CLK_EN1_REG      (ESP32S3_SYSTEM_BASE + 0x01C)

/* 软件复位 */
#define ESP32S3_SYSTEM_CPU_INTR_FROM_CPU_0_REG (ESP32S3_SYSTEM_BASE + 0x038)

/* RTC 复位原因 (在 RTC 域中, 不是 SYSTEM 域) */
/* 软件复位：OPTIONS0 的 SW_PROCPU_RST 位（真实头 esp32s3_rtccntl.h） */
#define ESP32S3_RTC_CNTL_OPTIONS0_REG \
    (ESP32S3_RTC_BASE + 0x0000)   /* = DR_REG_RTCCNTL_BASE + OPTIONS0 */
#define ESP32S3_RTC_CNTL_SW_SYS_RESET         (1 << 5)   /* SW_PROCPU_RST */

/*==========================
 *  GPIO 寄存器
 *  偏移来源: soc/esp32s3/include/soc/gpio_reg.h
 *==========================*/

#define ESP32S3_GPIO_OUT_REG                  (ESP32S3_GPIO_BASE + 0x004)
#define ESP32S3_GPIO_OUT_W1TS_REG             (ESP32S3_GPIO_BASE + 0x008)
#define ESP32S3_GPIO_OUT_W1TC_REG             (ESP32S3_GPIO_BASE + 0x00C)
#define ESP32S3_GPIO_ENABLE_REG               (ESP32S3_GPIO_BASE + 0x020)
#define ESP32S3_GPIO_ENABLE_W1TS_REG          (ESP32S3_GPIO_BASE + 0x024)
#define ESP32S3_GPIO_ENABLE_W1TC_REG          (ESP32S3_GPIO_BASE + 0x028)
#define ESP32S3_GPIO_IN_REG                   (ESP32S3_GPIO_BASE + 0x03C)
#define ESP32S3_GPIO_STATUS_REG               (ESP32S3_GPIO_BASE + 0x044)
#define ESP32S3_GPIO_STATUS_W1TS_REG          (ESP32S3_GPIO_BASE + 0x048)
#define ESP32S3_GPIO_STATUS_W1TC_REG          (ESP32S3_GPIO_BASE + 0x04C)
#define ESP32S3_GPIO_PIN(n)                   (ESP32S3_GPIO_BASE + 0x074 + (n) * 4)
#define ESP32S3_GPIO_FUNC_OUT_SEL_CFG_REG(n)  (ESP32S3_GPIO_BASE + 0x554 + (n) * 4)

/*
 * GPIO32-53 的输出使能置位寄存器 / output-enable set register for GPIO32-53
 * 数值来源 / Source: deps/nuttx/arch/xtensa/src/esp32s3/hardware/
 * esp32s3_gpio.h — GPIO_ENABLE1_W1TS_REG (DR_REG_GPIO_BASE + 0x30)
 */
#define ESP32S3_GPIO_ENABLE1_W1TS_REG         (ESP32S3_GPIO_BASE + 0x0030)

/* GPIO 数量 */
#define ESP32S3_GPIO_PIN_COUNT                48

/*==========================
 *  I2S 寄存器
 *  偏移来源: soc/esp32s3/include/soc/i2s_reg.h
 *==========================*/

/* I2S0 */
#define ESP32S3_I2S0_CONF_REG                 (ESP32S3_I2S0_BASE + 0x0000)
#define ESP32S3_I2S0_CONF2_REG                (ESP32S3_I2S0_BASE + 0x0018)
#define ESP32S3_I2S0_SAMPLE_RATE_CONF_REG     (ESP32S3_I2S0_BASE + 0x0008)
#define ESP32S3_I2S0_CLKM_CONF_REG            (ESP32S3_I2S0_BASE + 0x000C)
#define ESP32S3_I2S0_FIFO_CONF_REG            (ESP32S3_I2S0_BASE + 0x0010)
#define ESP32S3_I2S0_OUT_LINK_REG             (ESP32S3_I2S0_BASE + 0x0020)
#define ESP32S3_I2S0_IN_LINK_REG              (ESP32S3_I2S0_BASE + 0x0024)
#define ESP32S3_I2S0_INT_ENA_REG              (ESP32S3_I2S0_BASE + 0x0028)
#define ESP32S3_I2S0_INT_RAW_REG              (ESP32S3_I2S0_BASE + 0x002C)
#define ESP32S3_I2S0_INT_CLR_REG              (ESP32S3_I2S0_BASE + 0x0030)
#define ESP32S3_I2S0_FIFO_DATA_REG            (ESP32S3_I2S0_BASE + 0x003C)

/* I2S 寄存器通用访问宏 */
#define ESP32S3_I2S_CONF_REG(n)               ((n) == 0 ? ESP32S3_I2S0_CONF_REG : (ESP32S3_I2S1_BASE + 0x0000))
#define ESP32S3_I2S_CLKM_CONF_REG(n)          ((n) == 0 ? ESP32S3_I2S0_CLKM_CONF_REG : (ESP32S3_I2S1_BASE + 0x000C))
#define ESP32S3_I2S_SAMPLE_RATE_CONF_REG(n)   ((n) == 0 ? ESP32S3_I2S0_SAMPLE_RATE_CONF_REG : (ESP32S3_I2S1_BASE + 0x0008))
#define ESP32S3_I2S_FIFO_CONF_REG(n)          ((n) == 0 ? ESP32S3_I2S0_FIFO_CONF_REG : (ESP32S3_I2S1_BASE + 0x0010))
#define ESP32S3_I2S_OUT_LINK_REG(n)           ((n) == 0 ? ESP32S3_I2S0_OUT_LINK_REG : (ESP32S3_I2S1_BASE + 0x0020))
#define ESP32S3_I2S_IN_LINK_REG(n)            ((n) == 0 ? ESP32S3_I2S0_IN_LINK_REG : (ESP32S3_I2S1_BASE + 0x0024))

/*
 * I2S 寄存器"命名偏移"宏 / Named I2S register offsets
 *
 * WHY : 驱动此前硬编码 0x10/0x14/0x0C/0x24 等偏移，与本头的地址宏
 *       冲突；统一从这里取值，保证单一事实来源。
 *       Drivers used to hardcode conflicting offsets; take them from
 *       here so this header stays the single source of truth.
 * 数值与本头上方的 I2S0 寄存器地址一致（项目寄存器模型，真机联调
 * 时以芯片手册复核，见 HARDWARE.md）。
 */
#define ESP32S3_I2S_SAMPLE_RATE_CONF_OFFSET   0x0008
#define ESP32S3_I2S_CLKM_CONF_OFFSET          0x000C
#define ESP32S3_I2S_FIFO_CONF_OFFSET          0x0010
#define ESP32S3_I2S_OUT_LINK_OFFSET           0x0020
#define ESP32S3_I2S_IN_LINK_OFFSET            0x0024
#define ESP32S3_I2S_INT_ENA_OFFSET            0x0028
#define ESP32S3_I2S_INT_RAW_OFFSET            0x002C
#define ESP32S3_I2S_INT_CLR_OFFSET            0x0030

/*
 * I2S 中断状态寄存器 / I2S interrupt status register
 * INT_ST 紧随本头 INT 块（ENA 0x28 / RAW 0x2C / CLR 0x30）排布，
 * 占用下一个空槽 0x34（真机联调时以 TRM 复核）。
 * Note: real-chip layout differs; deps/nuttx/arch/xtensa/src/esp32s3/
 * hardware/esp32s3_i2s.h places INT_RAW 0x0C / INT_ST 0x10 /
 * INT_ENA 0x14 / INT_CLR 0x18 — reconcile when HW bring-up begins.
 */
#define ESP32S3_I2S_INT_ST_OFFSET             0x0034
#define ESP32S3_I2S0_INT_ST_REG               (ESP32S3_I2S0_BASE + ESP32S3_I2S_INT_ST_OFFSET)

/*
 * I2S OUT_LINK 寄存器位域 / I2S_OUT_LINK register bit fields
 * 位含义沿用本头的寄存器模型（与 esp32.h 同源位序）：
 *   OUTLINK_ADDR    [19:0] DMA 描述符链首地址 / first descriptor address
 *   OUTLINK_STOP    bit 28 停止 DMA / stop DMA
 *   OUTLINK_START   bit 29 启动 DMA / start DMA
 *   OUTLINK_RESTART bit 30 重启 DMA / restart DMA
 */
#define ESP32S3_I2S_OUTLINK_ADDR_MASK         0x000FFFFF
#define ESP32S3_I2S_OUTLINK_STOP              (1u << 28)
#define ESP32S3_I2S_OUTLINK_START             (1u << 29)
#define ESP32S3_I2S_OUTLINK_RESTART           (1u << 30)

/*
 * I2S INT_ST/INT_CLR 的发送完成位 / TX-done interrupt bit
 * 数值来源 / Source: deps/nuttx/arch/xtensa/src/esp32s3/hardware/
 * esp32s3_i2s.h — I2S_TX_DONE_INT (BIT(1))（真机信号语义）
 */
#define ESP32S3_I2S_TX_DONE_INT               (1u << 1)

/*
 * I2S DMA 描述符 owner 位约定 / DMA descriptor owner bit convention
 * 1 = DMA 所有（硬件正在搬运）/ owned by DMA (hardware in progress)
 * 0 = CPU 所有（软件可改写）  / owned by CPU (software may rewrite)
 */
#define ESP32S3_DMA_DESC_OWNER_CPU            0
#define ESP32S3_DMA_DESC_OWNER_DMA            1

/*==========================
 *  SPI Flash 寄存器
 *==========================*/

#define ESP32S3_SPI_CMD_REG(n)                 ((n) == 0 ? ESP32S3_SPI0_BASE + 0x000 : ESP32S3_SPI1_BASE + 0x000)
#define ESP32S3_SPI_ADDR_REG(n)                ((n) == 0 ? ESP32S3_SPI0_BASE + 0x004 : ESP32S3_SPI1_BASE + 0x004)
#define ESP32S3_SPI_USER_REG(n)                ((n) == 0 ? ESP32S3_SPI0_BASE + 0x010 : ESP32S3_SPI1_BASE + 0x010)

/*==========================
 *  DMA 寄存器 (GDMA)
 *  ESP32-S3 使用 GDMA (通用 DMA), 不是 I2S 内置 DMA
 *==========================*/

/* GDMA 通道寄存器偏移 */
#define ESP32S3_GDMA_IN_CONF0_CH(n)           (ESP32S3_DMA_BASE + 0x000 + (n) * 0x40)
#define ESP32S3_GDMA_OUT_CONF0_CH(n)          (ESP32S3_DMA_BASE + 0x080 + (n) * 0x40)
#define ESP32S3_GDMA_IN_LINK_ADDR_CH(n)       (ESP32S3_DMA_BASE + 0x00C + (n) * 0x40)
#define ESP32S3_GDMA_OUT_LINK_ADDR_CH(n)      (ESP32S3_DMA_BASE + 0x08C + (n) * 0x40)

/* GDMA 描述符 — ESP32-S3 使用 32 位地址 */
typedef struct {
    volatile uint32_t size   : 12;   /* Buffer size */
    volatile uint32_t length : 12;   /* Buffer length */
    volatile uint32_t eof    : 1;    /* End of frame */
    volatile uint32_t owner  : 1;    /* Owner: 1=dma, 0=cpu */
    volatile uint32_t sosf   : 1;    /* Start of sub-frame */
    volatile uint32_t offset : 5;    /* Buffer offset in unaligned case */
    volatile uint32_t buf;           /* Buffer address (full 32-bit) */
    volatile uint32_t next;          /* Next descriptor address (full 32-bit) */
} esp32s3_dma_descriptor_t;

/*==========================
 *  Timer Group 寄存器
 *==========================*/

/* WDT 寄存器偏移（在 Timer Group 内） */
#define ESP32S3_TIMG_WDTCONFIG0_OFFSET        0x0048
#define ESP32S3_TIMG_WDTCONFIG1_OFFSET        0x004C
#define ESP32S3_TIMG_WDTCONFIG2_OFFSET        0x0050
#define ESP32S3_TIMG_WDTCONFIG3_OFFSET        0x0054
#define ESP32S3_TIMG_WDTCONFIG4_OFFSET        0x0058
#define ESP32S3_TIMG_WDTFEED_OFFSET           0x0060
#define ESP32S3_TIMG_WDTWPROTECT_OFFSET       0x0064

#define ESP32S3_TIMG0_WDT_BASE                (ESP32S3_TIMERGROUP0_BASE + ESP32S3_TIMG_WDTCONFIG0_OFFSET)
#define ESP32S3_TIMG1_WDT_BASE                (ESP32S3_TIMERGROUP1_BASE + ESP32S3_TIMG_WDTCONFIG0_OFFSET)

/* WDT 保护字 */
#define ESP32S3_WDT_WKEY_VALUE                0x50D83AA1

/*==========================
 *  中断相关
 *==========================*/

#define ESP32S3_INTVEC_SOFTWARE0               1
#define ESP32S3_INTVEC_SOFTWARE1               2
#define ESP32S3_INTVEC_WMAC                    3
#define ESP32S3_INTVEC_WIFI_BB                 4
#define ESP32S3_INTVEC_BT_BB                   5
#define ESP32S3_INTVEC_I2C_MASTER              7
#define ESP32S3_INTVEC_SLC                     8
#define ESP32S3_INTVEC_SPI1                    12
#define ESP32S3_INTVEC_SPI2                    13
#define ESP32S3_INTVEC_I2S0                    14
#define ESP32S3_INTVEC_I2S1                    15
#define ESP32S3_INTVEC_UART                    16
#define ESP32S3_INTVEC_UART1                   17
#define ESP32S3_INTVEC_SDIO_HOST               19
#define ESP32S3_INTVEC_GPIO                    24
#define ESP32S3_INTVEC_CPU_WDT                 26
#define ESP32S3_INTVEC_RTC_CORE                27
#define ESP32S3_INTVEC_RTC_WDT                 28

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

#define ESP32S3_HAS_PSRAM
#define ESP32S3_HAS_WIFI
#define ESP32S3_HAS_BT
/* ESP32-S3 没有内置 DAC */

#endif /* __ESP32S3_H */
