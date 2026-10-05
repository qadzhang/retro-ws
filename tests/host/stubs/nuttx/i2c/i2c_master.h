/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <nuttx/i2c/i2c_master.h> 的宿主机桩 / host-test stub
 * WHY : drv_rtc.c 通过 /dev/i2cN + I2CIOC_TRANSFER 访问 RTC，
 *       宿主机语法检查需要这些类型/宏
 * HOW  : 结构体与宏逐字段复制自 deps/nuttx/include/nuttx/i2c/i2c_master.h
 *       （struct i2c_msg_s 见原文件 ~L100，struct i2c_transfer_s 见 L267，
 *        I2C_M_READ=0x0001 见 L95，I2CIOC_TRANSFER 见 L121）
 * 注意 : 真实 struct i2c_transfer_s 仅含 msgv/msgc（无 frequency 字段），
 *       频率放在每个 i2c_msg_s.frequency
 */
#ifndef __TEST_STUB_NUTTX_I2C_I2C_MASTER_H
#define __TEST_STUB_NUTTX_I2C_I2C_MASTER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>

/* I2C message flags（真实值 deps/nuttx/include/nuttx/i2c/i2c_master.h:95）*/
#define I2C_M_READ        0x0001  /* Read data, from slave to master */

/* ioctl 命令码：真实值 _I2CIOC(0x0001)（原文件 L121，_I2CIOC 见
 * deps/nuttx/include/nuttx/fs/ioctl.h:589）；语法检查只求类型正确 */
#define I2CIOC_TRANSFER   0x0701

struct i2c_master_s;

/* 单条 I2C 消息 / one I2C message（真实头 L100 起）*/
struct i2c_msg_s
{
    uint32_t frequency;         /* I2C frequency */
    uint16_t addr;              /* Slave address (7- or 10-bit) */
    uint16_t flags;             /* See I2C_M_* definitions */
    uint8_t *buffer;            /* Buffer to be transferred */
    ssize_t length;             /* Length of the buffer in bytes */
};

/* I2C 字符设备 ioctl 载荷 / ioctl payload（真实头 L267 起）*/
struct i2c_transfer_s
{
    struct i2c_msg_s *msgv;     /* Array of I2C messages for the transfer */
    size_t msgc;                /* Number of messages in the array. */
};

#endif /* __TEST_STUB_NUTTX_I2C_I2C_MASTER_H */
