/*
 * SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT : NuttX <nuttx/net/ip.h> 的宿主机桩 / host-test stub
 * WHY : firewall.c 包含它（原 <nuttx/net/ipv4.h> 不存在，真实名为 ip.h）；
 *       被测代码未直接使用其中的符号
 * HOW  : 空 guard，镜像 deps/nuttx/include/nuttx/net/ip.h
 */
#ifndef __TEST_STUB_NUTTX_NET_IP_H
#define __TEST_STUB_NUTTX_NET_IP_H
#endif
