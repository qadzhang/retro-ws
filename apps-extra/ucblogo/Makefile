#
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : UCBLogo 的 NuttX LOADABLE 应用构建规则
# WHY  : 以独立 ELF 形态产出 GPL 解释器，与固件 ROM 解耦（许可证隔离）
# WHO  : ESP32-S3 Retro Project Team
# WHERE: retro-ws/apps-extra/ucblogo/Makefile（同步到
#        deps/nuttx-apps/external/ucblogo/ 后参与构建）
# WHEN : 2026-10-04 新增
# HOW  : Application.mk + SPAWN_OBJECT_TYPE=ELF；源码 src/*.c 由
#        build_packages.sh 从上游 tarball 展开（通配符收集，首次实际
#        构建时需核对文件清单与上游补丁，见 NEXT_STEPS）
#

PROGNAME  = ucblogo
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
FSDIR     = ${BIN_DIR}
MODULE    = ucblogo

# UCBLogo 上游源码（build_packages.sh 展开到 src/）
CSRCS     = $(wildcard src/*.c)
CFLAGS   += -Isrc -O2

# 独立程序入口：UCBLogo main（logo.c）；首次构建时如需 NuttX 适配垫片，
# 在 src/ 内追加 retropad.c 并取消下行注释
# CSRCS += retropad.c

include $(APPDIR)/Application.mk
