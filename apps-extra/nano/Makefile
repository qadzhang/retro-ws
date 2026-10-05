#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#

# WHAT : GNU nano 8.4 的 NuttX LOADABLE 应用构建规则（GPL-3.0，不入 ROM）
# WHY  : 以独立 ELF 形态产出 nano，与固件 ROM 解耦（许可证隔离，
#        2026-10-05 定稿：系统默认编辑器为 vi，nano 走 .rpk 包）
# WHO  : Retro WS Project Team
# WHERE: retro-ws/apps-extra/nano/Makefile（build_packages.sh 同步到
#        deps/nuttx-apps/external/nano/ 后参与构建）
# WHEN : 2026-10-05 新增（自 firmware/retro-apps 的 ROM 编入块迁出）
# HOW  : Application.mk + SPAWN_OBJECT_TYPE=ELF；上游源码 deps/nano/src
#        由 build_packages.sh 展开到 src/，nano_port 垫片到 port/；
#        HAVE_CONFIG_H 门控 nano 的 <config.h>（autoconf 惯例）
#

PROGNAME  = nano
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 65536
FSDIR     = ${BIN_DIR}
MODULE    = nano

# 上游源码（build_packages.sh 展开到 src/）+ NuttX 适配垫片（port/）
NANO_SRCS  := $(filter-out src/nano.c,$(wildcard src/*.c))
CSRCS       = src/nano.c $(NANO_SRCS) port/mini_curses.c port/compat.c

# config/curses/term 覆盖优先（nano_port），再指上游头
CFLAGS += -Iport -Isrc -DHAVE_CONFIG_H -O2

include $(APPDIR)/Application.mk
