/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * retro_bus.c - 总线统一接口实现（硬件后端 + 软件回退）
 *
 * WHAT : retro_bus.h 接口的实现（I2C/SPI/UART 三总线）
 * WHY  : 见头注释（machine 风格统一 + 后端探测）
 * WHO  : retro_bus_{bas,js,berry}.c；tests/host 可桩测分支逻辑
 * WHERE: retro-ws/src/nuttx/common/driver/retro_bus.c
 * WHEN : 2026-10-04(晚) 新增
 * HOW  : 后端探测：open("/dev/i2cN"/"spiN"/"ttySN") 成功 → 硬件；
 *        失败 → retro_gpio 位摆软总线（I2C 开漏仿真 / SPI mode0）。
 *        软 I2C 半位延时 = 500000/freq_hz µs（100kHz→5µs）。
 *        UART 无软回退（全系有硬件 UART 驱动）
 */

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <poll.h>

#include "retro_bus.h"
#include "retro_gpio.h"

#ifdef __NuttX__
#include <nuttx/i2c/i2c_master.h>
#ifdef CONFIG_SPI_DRIVER
#include <nuttx/spi/spi_transfer.h>
#define RETRO_BUS_SPI_HW 1
#endif
#endif

/*==========================
 *  会话状态
 *==========================*/

enum bus_backend_e
{
    BUS_NONE = 0,
    BUS_HW,          /* /dev/i2cN、/dev/spiN、/dev/ttySN */
    BUS_SOFT,        /* retro_gpio 位摆 */
};

struct i2c_s
{
    int fd;              /* 硬件后端 fd，-1 无 */
    int scl, sda;
    int half_us;         /* 软总线半位延时 */
    enum bus_backend_e backend;
};

struct spi_s
{
    int fd;
    int mosi, miso, sck;
    int half_us;
    enum bus_backend_e backend;
};

struct uart_s
{
    int fd;
};

static struct i2c_s g_i2c[RETRO_BUS_I2C_MAX];
static struct spi_s g_spi[RETRO_BUS_SPI_MAX];
static struct uart_s g_uart[RETRO_BUS_UART_MAX];

static int clamp_id(int id, int max)
{
    if (id < 0 || id >= max)
        return -ENODEV;
    return OK;
}

/*==========================
 *  软 I2C（retro_gpio 位摆，开漏仿真）
 *==========================*/

static void soft_scl(struct i2c_s *b, int v)
{
    retro_gpio_config(b->scl, v ? "in_pu" : "out");
    retro_gpio_write(b->scl, 0);
}

static void soft_sda(struct i2c_s *b, int v)
{
    retro_gpio_config(b->sda, v ? "in_pu" : "out");
    retro_gpio_write(b->sda, 0);
}

static int soft_sda_read(struct i2c_s *b)
{
    return retro_gpio_read(b->sda);
}

static void soft_half_delay(struct i2c_s *b)
{
    usleep(b->half_us > 0 ? b->half_us : 1);
}

static void soft_i2c_start(struct i2c_s *b)
{
    soft_sda(b, 1);
    soft_scl(b, 1);
    soft_half_delay(b);
    soft_sda(b, 0);
    soft_half_delay(b);
    soft_scl(b, 0);
}

static void soft_i2c_stop(struct i2c_s *b)
{
    soft_sda(b, 0);
    soft_scl(b, 1);
    soft_half_delay(b);
    soft_sda(b, 1);
    soft_half_delay(b);
}

static int soft_i2c_write_byte(struct i2c_s *b, uint8_t v)
{
    int ack = 0;

    for (int i = 7; i >= 0; i--) {
        soft_sda(b, (v >> i) & 1);
        soft_half_delay(b);
        soft_scl(b, 1);
        soft_half_delay(b);
        soft_scl(b, 0);
    }

    /* 读 ACK：释放 SDA，SCL 高电平采样 */
    soft_sda(b, 1);
    soft_half_delay(b);
    soft_scl(b, 1);
    soft_half_delay(b);
    ack = soft_sda_read(b);
    soft_scl(b, 0);
    return ack == 0 ? OK : -ENXIO;
}

static uint8_t soft_i2c_read_byte(struct i2c_s *b, int send_nak)
{
    uint8_t v = 0;

    soft_sda(b, 1);
    for (int i = 7; i >= 0; i--) {
        soft_half_delay(b);
        soft_scl(b, 1);
        soft_half_delay(b);
        v = (uint8_t)((v << 1) | (soft_sda_read(b) ? 1 : 0));
        soft_scl(b, 0);
    }

    /* ACK/NAK */
    soft_sda(b, send_nak ? 1 : 0);
    soft_half_delay(b);
    soft_scl(b, 1);
    soft_half_delay(b);
    soft_scl(b, 0);
    soft_sda(b, 1);
    return v;
}

/*==========================
 *  I2C 公开接口
 *==========================*/

int retro_bus_i2c_init(int id, int scl, int sda, int freq_hz)
{
    int ret = clamp_id(id, RETRO_BUS_I2C_MAX);

    if (ret != OK)
        return ret;

    struct i2c_s *b = &g_i2c[id];
    char path[16];

    b->scl = scl;
    b->sda = sda;
    b->half_us = freq_hz > 0 ? 500000 / freq_hz : 5;

    snprintf(path, sizeof(path), "/dev/i2c%d", id);
    b->fd = open(path, O_RDONLY);
    if (b->fd >= 0) {
        b->backend = BUS_HW;
        syslog(LOG_INFO, "[retro_bus] i2c%d: hardware (%s)\n", id, path);
        return OK;
    }

    /* 软总线：引脚交 retro_gpio 管理（占用表拦截系统脚） */
    ret = retro_gpio_config(scl, "in_pu");
    if (ret < 0)
        return ret;
    ret = retro_gpio_config(sda, "in_pu");
    if (ret < 0)
        return ret;

    b->backend = BUS_SOFT;
    syslog(LOG_INFO, "[retro_bus] i2c%d: soft bit-bang scl=%d sda=%d\n",
           id, scl, sda);
    return OK;
}

int retro_bus_i2c_write(int id, uint16_t addr, const uint8_t *buf, int len)
{
    int ret = clamp_id(id, RETRO_BUS_I2C_MAX);

    if (ret != OK)
        return ret;

    struct i2c_s *b = &g_i2c[id];

    if (b->backend == BUS_NONE)
        return -ENODEV;

    if (b->backend == BUS_SOFT) {
        soft_i2c_start(b);
        ret = soft_i2c_write_byte(b, (uint8_t)(addr << 1 | 0));
        for (int i = 0; ret == OK && i < len; i++)
            ret = soft_i2c_write_byte(b, buf ? buf[i] : 0);
        soft_i2c_stop(b);
        return ret;
    }

#ifdef __NuttX__
    {
        struct i2c_msg_s msg;
        struct i2c_transfer_s xfer;

        msg.addr = addr;
        msg.flags = 0;
        msg.buffer = (uint8_t *)(uintptr_t)buf;
        msg.length = len;
        xfer.msgv = &msg;
        xfer.msgc = 1;
        return ioctl(b->fd, I2CIOC_TRANSFER, (unsigned long)&xfer);
    }
#else
    return -ENOSYS;
#endif
}

int retro_bus_i2c_read(int id, uint16_t addr, uint8_t *buf, int len)
{
    int ret = clamp_id(id, RETRO_BUS_I2C_MAX);

    if (ret != OK)
        return ret;

    struct i2c_s *b = &g_i2c[id];

    if (b->backend == BUS_NONE)
        return -ENODEV;

    if (buf == NULL || len <= 0)
        return -EINVAL;

    if (b->backend == BUS_SOFT) {
        soft_i2c_start(b);
        ret = soft_i2c_write_byte(b, (uint8_t)(addr << 1 | 1));
        if (ret != OK) {
            soft_i2c_stop(b);
            return ret;
        }
        for (int i = 0; i < len; i++)
            buf[i] = soft_i2c_read_byte(b, i == len - 1);
        soft_i2c_stop(b);
        return len;
    }

#ifdef __NuttX__
    {
        struct i2c_msg_s msg;
        struct i2c_transfer_s xfer;

        msg.addr = addr;
        msg.flags = I2C_M_READ;
        msg.buffer = buf;
        msg.length = len;
        xfer.msgv = &msg;
        xfer.msgc = 1;
        if (ioctl(b->fd, I2CIOC_TRANSFER, (unsigned long)&xfer) == OK)
            return len;
        return -EIO;
    }
#else
    return -ENOSYS;
#endif
}

int retro_bus_i2c_scan(int id, int (*cb)(int addr, void *arg), void *arg)
{
    int hits = 0;
    int ret = clamp_id(id, RETRO_BUS_I2C_MAX);

    if (ret != OK)
        return ret;

    for (int addr = 0x08; addr < 0x78; addr++) {
        if (retro_bus_i2c_write(id, (uint16_t)addr, NULL, 0) == OK) {
            hits++;
            if (cb != NULL && cb(addr, arg) != 0)
                break;
        }
    }
    return hits;
}

int retro_bus_i2c_write_reg8(int id, uint16_t addr, uint8_t reg, uint8_t val)
{
    uint8_t tmp[2];

    tmp[0] = reg;
    tmp[1] = val;
    return retro_bus_i2c_write(id, addr, tmp, 2);
}

int retro_bus_i2c_read_regs8(int id, uint16_t addr, uint8_t reg,
                             uint8_t *buf, int len)
{
    int ret = retro_bus_i2c_write(id, addr, &reg, 1);

    if (ret != OK)
        return ret;
    return retro_bus_i2c_read(id, addr, buf, len);
}

const char *retro_bus_i2c_backend(int id)
{
    if (id < 0 || id >= RETRO_BUS_I2C_MAX)
        return "none";
    return g_i2c[id].backend == BUS_HW ? "hw" :
           g_i2c[id].backend == BUS_SOFT ? "soft" : "none";
}

/*==========================
 *  软 SPI（mode0 位摆）
 *==========================*/

static void soft_spi_byte(struct spi_s *b, uint8_t *tx, uint8_t *rx)
{
    uint8_t v_tx = tx ? *tx : 0xff;
    uint8_t v_rx = 0;

    for (int i = 7; i >= 0; i--) {
        retro_gpio_write(b->mosi, (v_tx >> i) & 1);
        usleep(b->half_us > 0 ? b->half_us : 1);
        retro_gpio_write(b->sck, 1);
        usleep(b->half_us > 0 ? b->half_us : 1);
        v_rx = (uint8_t)((v_rx << 1) | (retro_gpio_read(b->miso) ? 1 : 0));
        retro_gpio_write(b->sck, 0);
    }

    if (rx)
        *rx = v_rx;
}

/*==========================
 *  SPI 公开接口
 *==========================*/

int retro_bus_spi_init(int id, int mosi, int miso, int sck, int freq_hz)
{
    int ret = clamp_id(id, RETRO_BUS_SPI_MAX);

    if (ret != OK)
        return ret;

    struct spi_s *b = &g_spi[id];
    char path[16];

    b->mosi = mosi;
    b->miso = miso;
    b->sck = sck;
    b->half_us = freq_hz > 0 ? 500000 / freq_hz : 1;

    snprintf(path, sizeof(path), "/dev/spi%d", id);
    b->fd = open(path, O_RDONLY);
    if (b->fd >= 0) {
        b->backend = BUS_HW;
        syslog(LOG_INFO, "[retro_bus] spi%d: hardware (%s)\n", id, path);
        return OK;
    }

    ret = retro_gpio_config(mosi, "out");
    if (ret < 0)
        return ret;
    ret = retro_gpio_config(sck, "out");
    if (ret < 0)
        return ret;
    if (miso >= 0) {
        ret = retro_gpio_config(miso, "in");
        if (ret < 0)
            return ret;
    }
    retro_gpio_write(b->sck, 0);

    b->backend = BUS_SOFT;
    syslog(LOG_INFO, "[retro_bus] spi%d: soft bit-bang mosi=%d miso=%d "
           "sck=%d\n", id, mosi, miso, sck);
    return OK;
}

int retro_bus_spi_xfer(int id, const uint8_t *tx, uint8_t *rx, int len)
{
    int ret = clamp_id(id, RETRO_BUS_SPI_MAX);

    if (ret != OK)
        return ret;

    struct spi_s *b = &g_spi[id];

    if (b->backend == BUS_NONE || len <= 0)
        return -ENODEV;

    if (b->backend == BUS_SOFT) {
        for (int i = 0; i < len; i++)
            soft_spi_byte(b, tx ? &((uint8_t *)(uintptr_t)tx)[i] : NULL,
                          rx ? &rx[i] : NULL);
        return len;
    }

#ifdef RETRO_BUS_SPI_HW
    {
        struct spi_trans_s trans;
        struct spi_sequence_s seq;

        memset(&trans, 0, sizeof(trans));
        memset(&seq, 0, sizeof(seq));

        trans.deselect = true;
        trans.nwords = len;
        trans.txbuffer = tx;
        trans.rxbuffer = rx;

        seq.dev = 0;
        seq.mode = 0;             /* mode0 */
        seq.nbits = 8;
        seq.ntrans = 1;
        seq.frequency = 1000000;  /* 板级可调（软约定） */
        seq.trans = &trans;

        if (ioctl(b->fd, SPIIOC_TRANSFER, (unsigned long)&seq) == OK)
            return len;
        return -EIO;
    }
#else
    return -ENOSYS;
#endif
}

const char *retro_bus_spi_backend(int id)
{
    if (id < 0 || id >= RETRO_BUS_SPI_MAX)
        return "none";
    return g_spi[id].backend == BUS_HW ? "hw" :
           g_spi[id].backend == BUS_SOFT ? "soft" : "none";
}

/*==========================
 *  UART 公开接口
 *==========================*/

int retro_bus_uart_init(int id, int tx, int rx, int baud)
{
    int ret = clamp_id(id, RETRO_BUS_UART_MAX);
    struct uart_s *u;
    char path[16];
    struct termios tio;

    (void)tx;
    (void)rx;

    if (ret != OK)
        return ret;

    u = &g_uart[id];

    snprintf(path, sizeof(path), "/dev/ttyS%d", id);
    u->fd = open(path, O_RDWR);
    if (u->fd < 0)
        return -ENOENT;

    if (tcgetattr(u->fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetspeed(&tio, baud > 0 ? baud : 115200);
        tcsetattr(u->fd, TCSANOW, &tio);
    }

    syslog(LOG_INFO, "[retro_bus] uart%d: %s @ %d 8N1\n", id, path,
           baud > 0 ? baud : 115200);
    return OK;
}

int retro_bus_uart_write(int id, const void *buf, int len)
{
    if (clamp_id(id, RETRO_BUS_UART_MAX) != OK)
        return -ENODEV;

    if (g_uart[id].fd < 0 || buf == NULL || len <= 0)
        return -EINVAL;

    return (int)write(g_uart[id].fd, buf, (size_t)len);
}

int retro_bus_uart_read(int id, void *buf, int len, int timeout_ms)
{
    if (clamp_id(id, RETRO_BUS_UART_MAX) != OK)
        return -ENODEV;

    if (g_uart[id].fd < 0 || buf == NULL || len <= 0)
        return -EINVAL;

    if (timeout_ms == 0) {
        int avail = 0;

        ioctl(g_uart[id].fd, FIONREAD, &avail);
        if (avail <= 0)
            return 0;
        if (avail > len)
            avail = len;
        return (int)read(g_uart[id].fd, buf, (size_t)avail);
    }

    if (timeout_ms > 0) {
        struct pollfd pfd;

        pfd.fd = g_uart[id].fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, timeout_ms) <= 0)
            return 0;
    }

    return (int)read(g_uart[id].fd, buf, (size_t)len);
}

int retro_bus_uart_available(int id)
{
    int avail = 0;

    if (clamp_id(id, RETRO_BUS_UART_MAX) != OK)
        return -ENODEV;

    if (g_uart[id].fd < 0)
        return -EINVAL;

    ioctl(g_uart[id].fd, FIONREAD, &avail);
    return avail;
}
