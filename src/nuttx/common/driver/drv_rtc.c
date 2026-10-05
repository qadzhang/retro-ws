/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * drv_rtc.c - RTC 驱动
 *
 * WHAT : RTC 驱动
 * WHY  : 外置 RTC（DS1307/PCF8563/RV-3028）读写
 * WHO  : ESP32-S3 Retro Project Team
 * WHERE: retro-ws/src/nuttx/common/driver/drv_rtc.c
 * WHEN : 2026-03~04 初版，2026-10-04 按 5W1H 标准化（AGENTS.md 4.0）
 * HOW  : 软件 I2C（引脚见各板硬件档案）
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/timers/rtc.h>
#include <nuttx/i2c/i2c_master.h>
#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

/*==========================
 *  配置
 *==========================*/

#ifndef CONFIG_RETRO_RTC
#  define CONFIG_RETRO_RTC 0
#endif

#if CONFIG_RETRO_RTC

/*==========================
 *  RTC 芯片配置
 *==========================*/

#ifndef CONFIG_RTC_CHIP
#  define CONFIG_RTC_CHIP "ds1307"
#endif

#ifndef CONFIG_RTC_I2C_ADDR
#  define CONFIG_RTC_I2C_ADDR 0x68  /* DS1307/DS1338 默认地址 */
#endif

/* I2C 地址 */
#define DS1307_ADDR    0x68
#define DS1338_ADDR    0x68
#define PCF8563_ADDR   0x51
#define RV3028_ADDR    0x51

/*==========================
 *  RTC 芯片类型
 *==========================*/

typedef enum {
    RTC_CHIP_DS1307 = 0,
    RTC_CHIP_DS1338,
    RTC_CHIP_PCF8563,
    RTC_CHIP_RV3028,
    RTC_CHIP_UNKNOWN
} rtc_chip_t;

/*==========================
 *  RTC 时间结构
 *==========================*/

struct rtc_datetime {
    uint8_t  year;    /* 00-99 (2000-2099) */
    uint8_t  month;   /* 01-12 */
    uint8_t  day;     /* 01-31 */
    uint8_t  hour;    /* 00-23 */
    uint8_t  minute;  /* 00-59 */
    uint8_t  second;  /* 00-59 */
    uint8_t  weekday; /* 0-6 (0=周日) */
};

/*==========================
 *  全局状态
 *==========================*/

static bool g_rtc_initialized = false;
static bool g_rtc_present = false;
static rtc_chip_t g_rtc_chip = RTC_CHIP_UNKNOWN;
static uint8_t g_rtc_i2c_addr = CONFIG_RTC_I2C_ADDR;

/* I2C 端口实例，需要在 rtc_init() 中初始化 */
#ifndef CONFIG_RTC_I2C_PORT
#  define CONFIG_RTC_I2C_PORT 1  /* 默认使用 I2C1 */
#endif
static int g_rtc_i2c_port = -1;

/*==========================
 *  I2C 操作
 *==========================*/

/*
 * 功能描述 / WHAT:
 *   通过 I2C 字符设备 /dev/i2cN 执行一次 I2C 传输
 * WHY : NuttX 用户态驱动的标准做法是 I2C 字符设备 + I2CIOC_TRANSFER
 *       （原 I2C_TRANSFER() 宏仅内核侧可用，用户态无该符号）
 * WHO : RTC 驱动内部
 * WHERE: retro-ws/src/nuttx/common/driver/drv_rtc.c
 * WHEN : 2026-10-04 修复编译错误时重写
 * HOW  : open("/dev/i2cN") → ioctl(I2CIOC_TRANSFER, &i2c_transfer_s)
 *       结构体定义见 deps/nuttx/include/nuttx/i2c/i2c_master.h:267
 *       （struct i2c_transfer_s 仅含 msgv/msgc，频率在各 msg->frequency）
 * 返回值 / Return:
 *   OK (0) - 成功 / success
 *   -ENOENT - 字符设备不存在 / char device missing
 *   负数    - ioctl 返回的错误码 / ioctl error
 */
static int rtc_i2c_xfer(struct i2c_msg_s *msgs, int n)
{
#ifdef CONFIG_I2C
    char devpath[16];
    int fd;
    int ret;
    struct i2c_transfer_s xfer;

    snprintf(devpath, sizeof(devpath), "/dev/i2c%d", g_rtc_i2c_port);

    fd = open(devpath, O_RDWR);
    if (fd < 0) {
        syslog(LOG_ERR, "[RTC] cannot open %s\n", devpath);
        return -ENOENT;
    }

    xfer.msgv = msgs;
    xfer.msgc = (size_t)n;

    ret = ioctl(fd, I2CIOC_TRANSFER, (unsigned long)(uintptr_t)&xfer);
    close(fd);

    return ret;
#else
    return -ENOSYS;
#endif
}

/*
 * 功能描述 / WHAT: 读取 RTC 寄存器 / Read one RTC register
 * HOW  : 写寄存器地址 + 读 1 字节，两次消息一次传输
 */
static int rtc_read_reg(uint8_t reg, uint8_t *val)
{
#ifdef CONFIG_I2C
    struct i2c_msg_s msg[2] = {
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = 0, .buffer = &reg, .length = 1 },
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = I2C_M_READ, .buffer = val, .length = 1 },
    };

    return rtc_i2c_xfer(msg, 2);
#else
    return -ENOSYS;
#endif
}

/*
 * 功能描述 / WHAT: 写入 RTC 寄存器 / Write one RTC register
 * HOW  : 寄存器地址 + 数据合成 2 字节写消息
 */
static int rtc_write_reg(uint8_t reg, uint8_t val)
{
#ifdef CONFIG_I2C
    uint8_t buf[2] = { reg, val };
    struct i2c_msg_s msg[1] = {
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = 0, .buffer = buf, .length = 2 },
    };

    return rtc_i2c_xfer(msg, 1);
#else
    return -ENOSYS;
#endif
}

/*
 * 功能描述 / WHAT: 读取多个 RTC 寄存器 / Read multiple RTC registers
 * HOW  : 写起始寄存器地址 + 连续读 len 字节
 */
static int rtc_read_regs(uint8_t reg, uint8_t *buf, int len)
{
#ifdef CONFIG_I2C
    struct i2c_msg_s msg[2] = {
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = 0, .buffer = &reg, .length = 1 },
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = I2C_M_READ, .buffer = buf, .length = len },
    };

    return rtc_i2c_xfer(msg, 2);
#else
    return -ENOSYS;
#endif
}

/*==========================
 *  BCD 转换
 *==========================*/

static inline uint8_t bcd2bin(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline uint8_t bin2bcd(uint8_t bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

/*==========================
 *  DS1307 操作
 *==========================*/

/**
 * 从 DS1307 读取时间
 */
static int ds1307_get_datetime(struct rtc_datetime *dt)
{
    uint8_t buf[7];
    int ret = rtc_read_regs(0, buf, 7);
    if (ret < 0)
        return ret;

    dt->second  = bcd2bin(buf[0] & 0x7F);
    dt->minute  = bcd2bin(buf[1] & 0x7F);
    dt->hour    = bcd2bin(buf[2] & 0x3F);  /* 24小时模式 */
    dt->weekday = bcd2bin(buf[3] & 0x07);
    dt->day     = bcd2bin(buf[4] & 0x3F);
    dt->month   = bcd2bin(buf[5] & 0x1F);
    dt->year    = bcd2bin(buf[6]);

    return OK;
}

/**
 * 设置 DS1307 时间
 */
static int ds1307_set_datetime(const struct rtc_datetime *dt)
{
    uint8_t buf[8];

    buf[0] = 0;  /* 寄存器0: 秒 */
    buf[1] = bin2bcd(dt->second) & 0x7F;
    buf[2] = bin2bcd(dt->minute) & 0x7F;
    buf[3] = bin2bcd(dt->hour)   & 0x3F;  /* 24小时模式 */
    buf[4] = bin2bcd(dt->weekday) & 0x07;
    buf[5] = bin2bcd(dt->day)     & 0x3F;
    buf[6] = bin2bcd(dt->month)   & 0x1F;
    buf[7] = bin2bcd(dt->year);

    /* 写入 8 个寄存器（从地址0开始）/ Write 8 registers starting at 0 */
#ifdef CONFIG_I2C
    struct i2c_msg_s msg[1] = {
        { .frequency = 400000, .addr = g_rtc_i2c_addr,
          .flags = 0, .buffer = buf, .length = 8 },
    };

    return rtc_i2c_xfer(msg, 1);
#else
    return -ENOSYS;
#endif
}

/**
 * 检测 DS1307 是否存在
 */
static bool ds1307_detect(void)
{
    uint8_t val;
    int ret = rtc_read_reg(0, &val);
    return (ret == OK);
}

/*==========================
 *  通用 RTC 操作
 *==========================*/

/**
 * 获取 RTC 时间（返回 Unix 时间戳）
 */
time_t rtc_get_time(void)
{
    if (!g_rtc_initialized || !g_rtc_present)
        return 0;

    struct rtc_datetime dt;
    int ret;

    switch (g_rtc_chip) {
        case RTC_CHIP_DS1307:
        case RTC_CHIP_DS1338:
            ret = ds1307_get_datetime(&dt);
            break;
        default:
            return 0;
    }

    if (ret < 0)
        return 0;

    /* 转换为 Unix 时间戳 */
    struct tm tm = {
        .tm_sec  = dt.second,
        .tm_min  = dt.minute,
        .tm_hour = dt.hour,
        .tm_mday = dt.day,
        .tm_mon  = dt.month - 1,
        .tm_year = dt.year + 100,  /* 2000年起 */
        .tm_isdst = 0,
    };

    return mktime(&tm);
}

/**
 * 设置 RTC 时间
 */
int rtc_set_time(time_t timestamp)
{
    if (!g_rtc_initialized || !g_rtc_present)
        return -ENODEV;

    struct tm tm;
    localtime_r(&timestamp, &tm);

    struct rtc_datetime dt = {
        .second  = tm.tm_sec,
        .minute  = tm.tm_min,
        .hour    = tm.tm_hour,
        .day     = tm.tm_mday,
        .month   = tm.tm_mon + 1,
        .year    = tm.tm_year - 100,  /* 2000年起 */
        .weekday = tm.tm_wday,
    };

    int ret;
    switch (g_rtc_chip) {
        case RTC_CHIP_DS1307:
        case RTC_CHIP_DS1338:
            ret = ds1307_set_datetime(&dt);
            break;
        default:
            return -ENOSYS;
    }

    if (ret == OK) {
        syslog(LOG_INFO, "[RTC] Time set: %s", ctime(&timestamp));
    }

    return ret;
}

/**
 * 检测 RTC 芯片类型
 */
static rtc_chip_t rtc_detect_chip(void)
{
    /* 尝试 DS1307 */
    g_rtc_i2c_addr = DS1307_ADDR;
    if (ds1307_detect()) {
        /* 验证：读取秒寄存器，检查 CH 位 */
        uint8_t sec;
        rtc_read_reg(0, &sec);
        if ((sec & 0x80) == 0) {
            syslog(LOG_INFO, "[RTC] DS1307 detected at 0x%02X\n", DS1307_ADDR);
            return RTC_CHIP_DS1307;
        }
    }

    /* 尝试 DS1338 */
    g_rtc_i2c_addr = DS1338_ADDR;
    if (ds1307_detect()) {
        syslog(LOG_INFO, "[RTC] DS1338 detected at 0x%02X\n", DS1338_ADDR);
        return RTC_CHIP_DS1338;
    }

    /* 尝试 PCF8563 */
    g_rtc_i2c_addr = PCF8563_ADDR;
    uint8_t val;
    if (rtc_read_reg(0, &val) == OK) {
        syslog(LOG_INFO, "[RTC] PCF8563 detected at 0x%02X\n", PCF8563_ADDR);
        return RTC_CHIP_PCF8563;
    }

    /* 尝试 RV-3028-C7 */
    g_rtc_i2c_addr = RV3028_ADDR;
    if (rtc_read_reg(0, &val) == OK) {
        syslog(LOG_INFO, "[RTC] RV-3028-C7 detected at 0x%02X\n", RV3028_ADDR);
        return RTC_CHIP_RV3028;
    }

    syslog(LOG_WARNING, "[RTC] No RTC chip detected\n");
    return RTC_CHIP_UNKNOWN;
}

/*==========================
 *  初始化
 *==========================*/

/**
 * 初始化 RTC
 */
int rtc_init(void)
{
    if (g_rtc_initialized) {
        syslog(LOG_INFO, "[RTC] Already initialized\n");
        return OK;
    }

    syslog(LOG_INFO, "[RTC] Initializing RTC...\n");

    /* 初始化 I2C 端口 */
#ifdef CONFIG_I2C
    extern int board_i2c_initialize(int port);
    g_rtc_i2c_port = board_i2c_initialize(CONFIG_RTC_I2C_PORT);
    if (g_rtc_i2c_port < 0) {
        syslog(LOG_ERR, "[RTC] I2C init failed: %d\n", g_rtc_i2c_port);
        return g_rtc_i2c_port;
    }
#endif

    /* 检测 RTC 芯片 */
    g_rtc_chip = rtc_detect_chip();

    if (g_rtc_chip == RTC_CHIP_UNKNOWN) {
        g_rtc_present = false;
        g_rtc_initialized = true;
        syslog(LOG_WARNING, "[RTC] No RTC hardware detected\n");
        return -ENODEV;
    }

    g_rtc_present = true;

    /* 读取当前时间测试 */
    time_t now = rtc_get_time();
    if (now > 0) {
        syslog(LOG_INFO, "[RTC] Current time: %s", ctime(&now));
    }

    g_rtc_initialized = true;
    syslog(LOG_INFO, "[RTC] Initialized successfully\n");

    return OK;
}

/**
 * 打印 RTC 状态
 */
void rtc_print_status(void)
{
    const char *chip_name;

    switch (g_rtc_chip) {
        case RTC_CHIP_DS1307:   chip_name = "DS1307";  break;
        case RTC_CHIP_DS1338:  chip_name = "DS1338";  break;
        case RTC_CHIP_PCF8563: chip_name = "PCF8563"; break;
        case RTC_CHIP_RV3028:  chip_name = "RV-3028-C7"; break;
        default:                chip_name = "Unknown";  break;
    }

    printf("\n");
    printf("=== RTC Status ===\n");
    printf("Chip:     %s\n", chip_name);
    printf("I2C Addr: 0x%02X\n", g_rtc_i2c_addr);
    printf("Present:  %s\n", g_rtc_present ? "YES" : "NO");
    printf("Time:     ");

    if (g_rtc_present) {
        time_t now = rtc_get_time();
        if (now > 0) {
            printf("%s", ctime(&now));
        } else {
            printf("(read error)\n");
        }
    } else {
        printf("N/A (no hardware)\n");
    }
    printf("\n");
}

/*==========================
 *  NTP 同步后写入 RTC
 *==========================*/

/**
 * NTP 同步成功后调用此函数写入 RTC
 */
int rtc_sync_from_ntp(void)
{
    if (!g_rtc_present)
        return -ENODEV;

    /* 获取当前系统时间（由 NTP 更新的）*/
    time_t now = time(NULL);
    if (now < 1000000000) {
        /* 时间不合理（小于 2001年）*/
        syslog(LOG_WARNING, "[RTC] NTP time not valid, skipping RTC sync\n");
        return -EINVAL;
    }

    int ret = rtc_set_time(now);
    if (ret == OK) {
        syslog(LOG_INFO, "[RTC] Synced from NTP: %s", ctime(&now));
    }

    return ret;
}

/*==========================
 *  NSH 命令
 *==========================*/

int cmd_rtc(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: rtc <status|get|set|test>\n");
        printf("  status  显示 RTC 状态\n");
        printf("  get     读取当前时间\n");
        printf("  set <timestamp>  设置时间戳\n");
        printf("  test    写入测试时间\n");
        return OK;
    }

    if (strcmp(argv[1], "status") == 0) {
        rtc_print_status();
    } else if (strcmp(argv[1], "get") == 0) {
        time_t now = rtc_get_time();
        if (now > 0) {
            printf("RTC time: %s", ctime(&now));
        } else {
            printf("RTC read error or no hardware\n");
        }
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc > 2) {
            time_t t = (time_t)atol(argv[2]);
            int ret = rtc_set_time(t);
            if (ret == OK)
                printf("RTC time set to: %s", ctime(&t));
            else
                printf("Failed to set RTC time: %d\n", ret);
        } else {
            printf("Usage: rtc set <timestamp>\n");
        }
    } else if (strcmp(argv[1], "test") == 0) {
        /* 写入测试时间: 2026-03-29 12:00:00 */
        struct rtc_datetime dt = {
            .year    = 26,  /* 2026 */
            .month   = 3,
            .day     = 29,
            .hour    = 12,
            .minute  = 0,
            .second  = 0,
            .weekday = 0,  /* 周日 */
        };

        int ret = ds1307_set_datetime(&dt);
        if (ret == OK) {
            printf("Test time written to RTC\n");
            rtc_print_status();
        } else {
            printf("Failed to write test time: %d\n", ret);
        }
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return OK;
}

#endif /* CONFIG_RETRO_RTC */
