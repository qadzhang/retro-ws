/*
 * SPDX-FileCopyrightText: 2026 Retro WS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * piousb_kbd.c - Pico PIO-USB 主机 HID boot 键盘驱动（输入优先级落地）
 *
 * WHAT : RP2040 第二 PIO 块上运行 Pico-PIO-USB（上游 MIT 库，零修改经
 *        port/ 垫片编译），完成 USB 键盘枚举与报告轮询，ASCII 键流经
 *        cvbs_console_feed_keys 直喂 /dev/cvbscon 输入环
 * WHY  : 输入优先级原则（REQUIREMENTS 2.2.3，2026-10-05 用户定稿）——
 *        Pico 无蓝牙、USB 块仅设备模式，PIO-USB 是其唯一 USB 键盘路线
 *        （NEXT_STEPS 54）；库出处/许可证见本目录 README（MIT，可入 ROM）
 * WHO  : Retro WS Project Team
 * WHERE: retro-ws/src/nuttx/rp2040/driver/input/piousb_kbd.c
 * WHEN : 2026-10-05 新增（PIO 时序/枚举细节待实机联调，登记 NEXT_STEPS）
 * HOW  : ① pio_usb_host_init（PIO1：SM0 TX + SM1 RX + SM2 EOP，DP/Dm
 *        两脚 + 22Ω 串阻）；② PIO1 IRQ0 -> pio_usb_host_irq_handler；
 *        ③ 1ms 周期任务跑 frame + 枚举状态机 + IN 轮询，任务
 *        sched_setaffinity 钉 CPU1（媒体/IO 核——2026-10-05 用户指示
 *        "USB 处理扔给 1 号核心"）；④ 8 字节键盘报告 -> hid_ascii_report
 *        （按下沿差分）-> 控制台（与 UART 泵同一道 IME 门控）
 */

#include <nuttx/config.h>

#include <syslog.h>
#include <nuttx/syslog/syslog.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>

#include <nuttx/sched.h>
#include <nuttx/kthread.h>
#include <arch/irq.h>

#include "pio_usb.h"
#include "pio_usb_configuration.h"
#include "pio_usb_ll.h"
#include "usb_definitions.h"

#include "driver/hid_ascii.h"

#ifndef CONFIG_RETRO_PIO_USB_DP_PIN
#  define CONFIG_RETRO_PIO_USB_DP_PIN 20   /* DP=GP20, DM=GP21（DPDM 排列） */
#endif

#ifndef CONFIG_RETRO_PIO_USB_POLL_STACK
#  define CONFIG_RETRO_PIO_USB_POLL_STACK 2048
#endif

#ifndef CONFIG_RETRO_PIO_USB_PRIO
#  define CONFIG_RETRO_PIO_USB_PRIO 110
#endif

/* USB 标准请求（USB 2.0 9.3/9.4 与 HID 1.11 7.2） */
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_CONFIG      0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_ENDPOINT       0x05

#define USB_KBD_ADDR            1          /* 总线唯一设备，地址固定 1 */
#define USB_WAIT_US             200        /* 控制阶段轮询间隔 */
#define USB_WAIT_TIMEOUT_MS     100

/*==========================
 *  驱动状态
 *==========================*/

enum kbd_state_e
{
    KBD_DETACHED = 0,
    KBD_ENUM_SET_ADDR,    /* 检测到设备（root->connected） */
    KBD_ENUM_GET_DEV_DESC,
    KBD_ENUM_GET_CFG_DESC,
    KBD_ENUM_SET_CONFIG,
    KBD_ENUM_SET_PROTO,   /* boot 协议 + idle */
    KBD_RUNNING,          /* IN 端点轮询中 */
};

struct piousb_kbd_s
{
    volatile enum kbd_state_e state;
    endpoint_t *ep_in;                  /* 0x81：HID 中断 IN */
    uint8_t prev_report[8];
    uint8_t cfg_buf[128];
};

static struct piousb_kbd_s g_kbd;

/*==========================
 *  控制传输（同步完成等待）
 *==========================*/

/* 等待端点空闲（transfer 完成由 IRQ 处理器置回 has_transfer=false） */
static int wait_ep_idle(endpoint_t *ep)
{
    int waited = 0;

    while (ep != NULL && ep->has_transfer)
    {
        if (waited >= USB_WAIT_TIMEOUT_MS * 1000 / USB_WAIT_US)
            return -ETIMEDOUT;
        usleep(USB_WAIT_US);
        waited++;
    }

    return OK;
}

/*
 * WHAT : 一次完整控制传输（SETUP -> 可选数据阶段 -> 状态阶段）
 * dir_in : 数据阶段方向（true=IN 无/有数据，false=OUT）
 * 返回   : OK / -EIO / -ETIMEDOUT；IN 数据落在 buf（上游 ep->buffer 拷回）
 */
static int ctrl_transfer(const uint8_t setup[8], bool dir_in,
                         uint8_t *buf, uint16_t len)
{
    endpoint_t *ep0 = NULL;

    if (!pio_usb_host_send_setup(0, USB_KBD_ADDR, setup))
        return -EIO;

    /* 上游 ep0 由 send_setup 内部 _find_ep 定位；数据阶段重取同一端点：
     * send_setup 已把 ep0 配成 SETUP TX，数据/状态阶段按方向翻转 */
    {
        int root_idx = 0;
        for (int i = 0; i < PIO_USB_EP_POOL_CNT; i++)
        {
            endpoint_t *ep = PIO_USB_ENDPOINT(i);
            if (ep->size != 0 && ep->dev_addr == USB_KBD_ADDR &&
                (ep->ep_num & 0x7f) == 0)
            {
                ep0 = ep;
                break;
            }
        }
    }

    if (ep0 == NULL)
        return -EIO;

    if (wait_ep_idle(ep0) != OK)
        return -ETIMEDOUT;

    if (len > 0)
    {
        uint8_t ep_addr = dir_in ? 0x80 : 0x00;

        if (!pio_usb_host_endpoint_transfer(0, USB_KBD_ADDR, ep_addr,
                                            buf, len))
            return -EIO;
        if (wait_ep_idle(ep0) != OK)
            return -ETIMEDOUT;

        if (dir_in && buf != NULL)
            memcpy(buf, ep0->buffer, len < ep0->actual_len ? len
                                                           : ep0->actual_len);
    }

    /* 状态阶段：与数据阶段反向（无数据阶段则 IN 状态） */
    {
        uint8_t status_addr = (len > 0) ? (dir_in ? 0x00 : 0x80) : 0x80;

        if (!pio_usb_host_endpoint_transfer(0, USB_KBD_ADDR, status_addr,
                                            NULL, 0))
            return -EIO;
        if (wait_ep_idle(ep0) != OK)
            return -ETIMEDOUT;
    }

    return OK;
}

#define SETUP_U16(req, val, idx, len)                     \
    {                                                     \
        (uint8_t)(req), 0x00, (uint8_t)(val),             \
            (uint8_t)((val) >> 8), (uint8_t)(idx), 0x00,  \
            (uint8_t)(len), (uint8_t)((uint16_t)(len) >> 8) \
    }

/*==========================
 *  枚举状态机（1ms 任务里步进）
 *==========================*/

static void kbd_enum_step(void)
{
    switch (g_kbd.state)
    {
        case KBD_ENUM_SET_ADDR:
        {
            static const uint8_t set_addr[8] =
                SETUP_U16(USB_REQ_SET_ADDRESS, USB_KBD_ADDR, 0, 0);

            if (ctrl_transfer(set_addr, false, NULL, 0) == OK)
                g_kbd.state = KBD_ENUM_GET_DEV_DESC;
            break;
        }

        case KBD_ENUM_GET_DEV_DESC:
        {
            static const uint8_t get_dd[8] =
                SETUP_U16(USB_REQ_GET_DESCRIPTOR, (USB_DESC_DEVICE << 8), 0, 18);
            uint8_t dev_desc[18];

            if (ctrl_transfer(get_dd, true, dev_desc, 18) == OK)
            {
                syslog(LOG_INFO, "[piousb] kbd vid=%04x pid=%04x\n",
                       (uint16_t)dev_desc[8] | ((uint16_t)dev_desc[9] << 8),
                       (uint16_t)dev_desc[10] | ((uint16_t)dev_desc[11] << 8));
                g_kbd.state = KBD_ENUM_GET_CFG_DESC;
            }
            break;
        }

        case KBD_ENUM_GET_CFG_DESC:
        {
            /* 先取 9 字节配置头拿总长，再取全量（找 HID IN 端点用） */
            static const uint8_t get_cd9[8] =
                SETUP_U16(USB_REQ_GET_DESCRIPTOR, (USB_DESC_CONFIGURATION << 8), 0, 9);
            uint8_t head[9];

            if (ctrl_transfer(get_cd9, true, head, 9) == OK)
            {
                uint16_t total = (uint16_t)head[2] | ((uint16_t)head[3] << 8);

                if (total > 0 && total <= sizeof(g_kbd.cfg_buf))
                {
                    uint8_t get_cd[8] =
                        SETUP_U16(USB_REQ_GET_DESCRIPTOR,
                                  (USB_DESC_CONFIGURATION << 8), 0, total);

                    if (ctrl_transfer(get_cd, true, g_kbd.cfg_buf, total) == OK)
                        g_kbd.state = KBD_ENUM_SET_CONFIG;
                }
            }
            break;
        }

        case KBD_ENUM_SET_CONFIG:
        {
            uint8_t cfg_value = g_kbd.cfg_buf[5];   /* bConfigurationValue */
            uint16_t total = (uint16_t)g_kbd.cfg_buf[2] |
                             ((uint16_t)g_kbd.cfg_buf[3] << 8);
            bool found = false;

            /* 从配置描述符找中断 IN 端点（HID 键盘）并按描述符开端点 */
            for (uint16_t i = 0; i + 7 < total; i += g_kbd.cfg_buf[i])
            {
                if (g_kbd.cfg_buf[i + 1] == USB_DESC_ENDPOINT &&
                    (g_kbd.cfg_buf[i + 2] & 0x80) &&
                    (g_kbd.cfg_buf[i + 3] & 0x03) == 0x03)
                {
                    found = pio_usb_host_endpoint_open(
                        0, USB_KBD_ADDR, &g_kbd.cfg_buf[i], false);

                    if (found)
                        syslog(LOG_INFO, "[piousb] kbd IN ep 0x%02x (%u B)\n",
                               g_kbd.cfg_buf[i + 2],
                               (uint16_t)g_kbd.cfg_buf[i + 4] |
                                   ((uint16_t)g_kbd.cfg_buf[i + 5] << 8));
                    break;
                }
            }

            if (found)
            {
                uint8_t set_cfg[8] =
                    SETUP_U16(USB_REQ_SET_CONFIG, cfg_value, 0, 0);

                if (ctrl_transfer(set_cfg, false, NULL, 0) == OK)
                    g_kbd.state = KBD_ENUM_SET_PROTO;
            }
            break;
        }

        case KBD_ENUM_SET_PROTO:
        {
            /* HID boot 协议：8 字节定长报告，无需解析报告描述符 */
            static const uint8_t set_proto[8] =
                SETUP_U16(HID_REQ_SET_PROTOCOL, 0, 0, 0);
            static const uint8_t set_idle[8] =
                SETUP_U16(HID_REQ_SET_IDLE, 0, 0, 0);
            static const uint8_t get_ep_desc[8] =
                SETUP_U16(USB_REQ_GET_DESCRIPTOR, 0x2200, 0, 0); /* 占位不读 */

            (void)get_ep_desc;

            if (ctrl_transfer(set_proto, false, NULL, 0) == OK)
            {
                ctrl_transfer(set_idle, false, NULL, 0);   /* 失败可容忍 */
                memset(g_kbd.prev_report, 0, sizeof(g_kbd.prev_report));

                /* IN 端点对象：ep_pool 内 dev_addr=1 的 0x8x 端点 */
                for (int i = 0; i < PIO_USB_EP_POOL_CNT; i++)
                {
                    endpoint_t *ep = PIO_USB_ENDPOINT(i);
                    if (ep->size != 0 && ep->dev_addr == USB_KBD_ADDR &&
                        ep->ep_num & 0x80)
                    {
                        g_kbd.ep_in = ep;
                        break;
                    }
                }

                syslog(LOG_INFO, "[piousb] keyboard enumerated, polling\n");
                g_kbd.state = KBD_RUNNING;
            }
            break;
        }

        default:
            break;
    }
}

/*==========================
 *  轮询与键流桥
 *==========================*/

extern int cvbs_console_feed_keys(const char *buf, size_t len);

static void kbd_poll_report(void)
{
    uint8_t report[8];
    char ascii[6];
    int n;

    if (g_kbd.ep_in == NULL)
        return;

    /* 返回 >0 = 收到新报告（hid_ascii 做按下沿差分，长按不重复） */
    if (pio_usb_get_in_data(g_kbd.ep_in, report, sizeof(report)) <= 0)
        return;

    n = hid_ascii_report(g_kbd.prev_report, report, ascii, sizeof(ascii));
    if (n > 0)
        cvbs_console_feed_keys(ascii, (size_t)n);

    memcpy(g_kbd.prev_report, report, sizeof(report));
}

/*==========================
 *  PIO1 IRQ（RX 完成/连接事件 -> 上游处理器）
 *==========================*/

static int piousb_irq_handler(int irq, void *context, void *arg)
{
    pio_usb_host_irq_handler(0);      /* root port 0 */
    return OK;
}

/*==========================
 *  1ms 周期任务（钉 CPU1：媒体/IO 核）
 *==========================*/

static int kbd_task(int argc, char *argv[])
{
    cpu_set_t cpumask;
    root_port_t *root = PIO_USB_ROOT_PORT(0);

    CPU_ZERO(&cpumask);
    CPU_SET(1, &cpumask);
    sched_setaffinity(0, sizeof(cpumask), &cpumask);

    syslog(LOG_INFO, "[piousb] poll task on CPU1 (USB 与 IO 同核)\n");

    while (1)
    {
        pio_usb_host_frame();          /* skip_alarm_pool 模式的 1ms 帧 */
                                     /* 注：无 TinyUSB，host_task 无实现；
                                      * 事件处理在 IRQ（piousb_irq_handler）*/

        if (root->connected)
        {
            if (g_kbd.state == KBD_DETACHED)
            {
                syslog(LOG_INFO, "[piousb] device attached, enumerating\n");
                g_kbd.state = KBD_ENUM_SET_ADDR;
            }

            kbd_enum_step();
            kbd_poll_report();
        }
        else if (g_kbd.state != KBD_DETACHED)
        {
            syslog(LOG_INFO, "[piousb] device detached\n");
            g_kbd.state = KBD_DETACHED;
            g_kbd.ep_in = NULL;
            memset(g_kbd.prev_report, 0, sizeof(g_kbd.prev_report));
        }

        usleep(1000);
    }

    return 0;
}

/*==========================
 *  初始化（板级/启动入口调用）
 *==========================*/

int piousb_kbd_init(void)
{
    static pio_usb_configuration_t cfg;
    int ret;
    int pid;

    memset(&g_kbd, 0, sizeof(g_kbd));

    /* PIO1：SM0=TX、SM1=RX、SM2=EOP；DMA 通道 0（PIO-USB 默认占用面，
     * 与 CVBS（PIO0/SM0）零冲突）；skip_alarm_pool=true：帧节拍由本驱动
     * 的 1ms 任务提供 */
    cfg = (pio_usb_configuration_t)PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = CONFIG_RETRO_PIO_USB_DP_PIN;
    cfg.pio_tx_num = 1;
    cfg.pio_rx_num = 1;
    cfg.skip_alarm_pool = true;
    cfg.alarm_pool = NULL;

    if (pio_usb_host_init(&cfg) == NULL)
    {
        syslog(LOG_ERR, "[piousb] host init failed\n");
        return -EIO;
    }

    ret = irq_attach(RP2040_PIO1_IRQ_0, piousb_irq_handler, NULL);
    if (ret < 0)
    {
        syslog(LOG_WARNING, "[piousb] IRQ attach failed: %d\n", ret);
        return ret;
    }

    up_enable_irq(RP2040_PIO1_IRQ_0);

    pid = kthread_create("piousb", CONFIG_RETRO_PIO_USB_PRIO,
                         CONFIG_RETRO_PIO_USB_POLL_STACK, kbd_task, NULL);
    if (pid < 0)
    {
        syslog(LOG_ERR, "[piousb] task create failed: %d\n", pid);
        return pid;
    }

    syslog(LOG_INFO, "[piousb] PIO-USB host up (PIO1, DP=GP%d)\n",
           CONFIG_RETRO_PIO_USB_DP_PIN);
    return OK;
}
