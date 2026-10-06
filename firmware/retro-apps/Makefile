############################################################################
# firmware/retro-apps/Makefile - Retro WS 固件模块构建规则
# SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : Retro WS 核心的 apps 模块（同步副本在 deps/nuttx-apps/retro）
# WHY  : NuttX 只编 apps 内模块；按 CONFIG 挑源（每型号裁剪点）
# WHO  : NuttX apps 构建系统（$(APPDIR)/Application.mk）
# WHERE: retro-ws/firmware/retro-apps/Makefile（真身）
# WHEN : 2026-10-04 新增；同日加 AV 控制台/字库/双解释器
# HOW  : CSRCS 按 RETRO_* 挑选；引擎源经 $(APPDIR)/../ 指向 deps；
#        字体经 fontbridge（CLI 构建无 LVGL 时桥到兼容层）
############################################################################

include $(APPDIR)/Make.defs

ENGINES := $(APPDIR)/../

# ---- 公共核心（全部目标） ----
CSRCS  = src/common/apps/system/nsh_cmds.c

ifeq ($(CONFIG_RETRO_PKG_MANAGER),y)
CSRCS += src/common/apps/system/pkg_manager.c
endif

# ---- ROM XIP 模块加载器 + ROM 包存储（应用/系统分离，2026-10-06） ----
ifeq ($(CONFIG_RETRO_ROMMOD),y)
CSRCS += src/common/rommod.c
endif

ifeq ($(CONFIG_RETRO_PKG_STORE),y)
CSRCS += src/common/pkg_rom.c
# ROMFS 包存储镜像（build_romapps.sh 生成；缺失=空存储构建）
ifneq ($(wildcard pkg_romfs.c),)
CSRCS += pkg_romfs.c
endif
# 基础符号表（动态档：固件 pass1 后生成）
ifneq ($(wildcard rom_symtab.c),)
CSRCS += rom_symtab.c
endif
# 符号保持器（stage 生成：模块所需而固件无引用的符号，防 gc-sections 回收）
ifneq ($(wildcard rom_keep.c),)
CSRCS += rom_keep.c
endif
endif

# 静态绑定档 arena（Xtensa；RETRO_ROMMOD_ARENA_SIZE=0 时不编）
ifneq ($(CONFIG_RETRO_ROMMOD_ARENA_SIZE),)
ifneq ($(CONFIG_RETRO_ROMMOD_ARENA_SIZE),0)
CSRCS += rommod_arena.c
endif
endif

ifeq ($(CONFIG_RETRO_GPIO_SCRIPT),y)
CSRCS += src/common/driver/retro_gpio.c
endif

# ---- 总线兼容层（I2C/SPI/UART，machine 风格 + 引擎绑定） ----
ifeq ($(CONFIG_RETRO_BUS),y)
CSRCS += src/common/driver/retro_bus.c
ifeq ($(CONFIG_RETRO_SCRIPT_TINYBASIC),y)
CSRCS += src/common/driver/retro_bus_bas.c
endif
ifeq ($(CONFIG_RETRO_SCRIPT_BERRY),y)
CSRCS += src/common/driver/retro_bus_berry.c
endif
ifeq ($(CONFIG_RETRO_SCRIPT_DUKTAPE),y)
CSRCS += src/common/driver/retro_bus_js.c
endif
endif

ifeq ($(CONFIG_RETRO_PINYIN_CLI),y)
CSRCS += src/common/driver/drv_pinyin.c
# CCDOS 式输入法状态条（2026-10-05）：底部常驻条 + 键盘接管
CSRCS += src/common/driver/cvbs_ime.c
endif

ifeq ($(CONFIG_RETRO_MEMMON),y)
CSRCS += src/common/driver/memmon.c
endif

ifeq ($(CONFIG_RETRO_WIFI),y)
CSRCS += src/common/driver/network.c
CSRCS += src/common/driver/wifi_conf.c
CSRCS += src/common/driver/ntp.c
CSRCS += src/common/driver/firewall.c
CSRCS += src/common/network_utils.c
endif

ifeq ($(CONFIG_RETRO_CRON),y)
CSRCS += src/common/driver/cron.c
endif

ifeq ($(CONFIG_RETRO_RTC),y)
CSRCS += src/common/driver/drv_rtc.c
endif

# ---- CVBS 显示驱动（GUI/AV 控制台共用核心） ----
ifeq ($(CONFIG_RETRO_DISPLAY),y)
CSRCS += src/common/driver/cvbs_core.c
CSRCS += src/common/driver/drv_cvbs.c
ifeq ($(CONFIG_RETRO_BOARD_ESP32S3),y)
CSRCS += src/esp32s3/driver/cvbs/drv_cvbs.c
endif
ifeq ($(CONFIG_RETRO_BOARD_ESP32),y)
CSRCS += src/esp32/driver/cvbs/drv_cvbs_dac.c
endif
endif

# ---- AV 视频字符控制台（全系标配） ----
ifeq ($(CONFIG_RETRO_AV_CONSOLE),y)
ifneq ($(CONFIG_RETRO_DISPLAY),y)
CSRCS += src/common/driver/cvbs_core.c
CSRCS += src/common/driver/drv_cvbs.c
endif
CSRCS += src/common/driver/cvbs_console.c
CSRCS += src/common/driver/lvgl_font_compat.c
# HID 键码 -> ASCII（USB/BLE 键盘桥共用纯函数件，输入优先级原则 2026-10-05）
CSRCS += src/common/driver/hid_ascii.c
# 唯一字体 12px（全系 CLI+GUI 单字号，AGENTS.md 7.3 / HARDWARE.md 6.4）
CSRCS += src/lvgl/fonts/lv_font_notosans_sc_12.c
# console 点阵表（Fusion Pixel 12px 等宽 OFL-1.1，gen_pixel_fonts.py 生成）：
# 半角 6px 等宽 + 全角标点 12px 满格（cvbs_console 半格网格专用）
CSRCS += src/lvgl/fonts/lv_font_ascii_6.c
CSRCS += src/lvgl/fonts/lv_font_fullwidth.c
endif

# ---- LVGL 图形界面（S3/CAM 全栈） ----
ifeq ($(CONFIG_RETRO_GUI),y)
LVGL := $(ENGINES)lvgl
LVGL_SRCS := $(shell find $(LVGL)/src -name '*.c' 2>/dev/null)
CSRCS += $(LVGL_SRCS)

# 我们的 GUI 树（装配 + 桌面 + 外壳 + 输入法；2026-10-06 应用/系统
# 分离：editor/browser/terminal/player/recorder/sqlite/minesweeper 七
# 应用改 .rpk 包交付=ROM XIP 模块，不再静态链编——构建见
# firmware/packages/pkgs/，桌面注册表见 desktop.c）
GUI_SRCS := src/lvgl/lvgl_app.c src/lvgl/lv_port_disp.c src/lvgl/lv_port_indev.c \
            src/lvgl/i18n.c src/lvgl/retro_ui.c \
            src/lvgl/app/desktop.c src/lvgl/app/wmaker_shell.c \
            src/lvgl/app/app_pinyin.c src/lvgl/fonts/pinyin_ime.c
CSRCS += $(GUI_SRCS)

# 引擎的 GUI 绑定（随引擎走）
ifeq ($(CONFIG_RETRO_SCRIPT_TINYBASIC),y)
CSRCS += src/lvgl/modules/retro_ui_bas.c
endif
ifeq ($(CONFIG_RETRO_SCRIPT_DUKTAPE),y)
CSRCS += src/lvgl/modules/retro_ui_js.c
endif
ifeq ($(CONFIG_RETRO_SCRIPT_BERRY),y)
CSRCS += src/lvgl/modules/retro_ui_berry.c
endif

# jslogo 小海龟（Duktape + LVGL canvas）
ifeq ($(CONFIG_RETRO_SCRIPT_JSLOGO),y)
CSRCS += src/lvgl/modules/logo_jslogo.c
CSRCS += src/lvgl/app/logo/logo_draw.c src/lvgl/app/logo/logo_turtle.c \
          src/lvgl/app/logo/logo_vm.c
endif
endif

# ---- 脚本引擎（Berry + my-basic 默认入 ROM） ----
ifeq ($(CONFIG_RETRO_SCRIPTS),y)
CSRCS += src/common/script_engines.c

ifeq ($(CONFIG_RETRO_SCRIPT_TINYBASIC),y)
CSRCS += $(ENGINES)my_basic/core/my_basic.c
ifeq ($(CONFIG_RETRO_GPIO_SCRIPT),y)
CSRCS += src/common/driver/retro_gpio_bas.c
endif
endif

ifeq ($(CONFIG_RETRO_SCRIPT_BERRY),y)
BERRY_SRCS := $(wildcard $(ENGINES)berry/src/*.c)
CSRCS += $(BERRY_SRCS)
CSRCS += $(ENGINES)berry/default/be_port.c
CSRCS += $(ENGINES)berry/default/be_modtab.c
ifeq ($(CONFIG_RETRO_GPIO_SCRIPT),y)
CSRCS += src/common/driver/retro_gpio_berry.c
endif
endif

ifeq ($(CONFIG_RETRO_SCRIPT_DUKTAPE),y)
DUK_SRCS := $(wildcard $(ENGINES)duktape/src/duktape.c)
CSRCS += $(DUK_SRCS)
ifeq ($(CONFIG_RETRO_GPIO_SCRIPT),y)
CSRCS += src/common/driver/retro_gpio_js.c
endif
endif

endif # RETRO_SCRIPTS

# ---- 板级脚本 ROM 化（/rom/scripts + script 命令） ----
ifeq ($(CONFIG_RETRO_SCRIPTS_ROM),y)
CSRCS += src/common/script_rom.c
CSRCS += scripts_romfs.c
endif

# ---- GNU nano 8.4（真源码移植 + mini-curses 垫片） ----
# GPL-3.0：默认不入 ROM（.rpk 包交付，AGENTS 11.1 红线）；本块仅
# RETRO_NANO 实验回编时生效（2026-10-05 定稿，见 apps-extra/nano）
ifeq ($(CONFIG_RETRO_NANO),y)
NANO := $(ENGINES)nano/src/
NANO_LIB_SRCS := $(filter-out $(NANO)nano.c,$(wildcard $(NANO)*.c))
CSRCS += $(NANO_LIB_SRCS)
CSRCS += src/common/nano_port/mini_curses.c
CSRCS += src/common/nano_port/compat.c
endif

# ---- 板级装配（每目标一棵） ----
ifeq ($(CONFIG_RETRO_BOARD_ESP32S3),y)
CSRCS += src/esp32s3/board/board.c
CSRCS += src/esp32s3/esp32s3_retro.c
# WS2812 状态灯：RMT 硬件外设（/dev/rmt0，HARDWARE.md 2.8/13.1）
CSRCS += src/esp32s3/driver/ws2812_rmt.c
ifeq ($(CONFIG_RETRO_GUI),y)
CSRCS += src/esp32s3/driver/audio/drv_audio.c
endif
# FSK 磁带：调制送 audio_play_pcm（I2S+GDMA 硬件出声，13.1）
ifeq ($(CONFIG_RETRO_FSK),y)
CSRCS += src/esp32s3/driver/fsk/drv_fsk.c
endif
endif

ifeq ($(CONFIG_RETRO_BOARD_ESP32),y)
CSRCS += src/esp32/board/board.c
CSRCS += src/esp32/esp32_retro.c
ifeq ($(CONFIG_RETRO_AUDIO_DAC),y)
CSRCS += src/esp32/driver/audio/drv_audio_dac.c
endif
# FSK 磁带：调制送 audio_play_pcm（I2S0+内置 DAC 硬件出声，13.1）
ifeq ($(CONFIG_RETRO_FSK),y)
CSRCS += src/esp32/driver/fsk/drv_fsk.c
endif
endif

ifeq ($(CONFIG_RETRO_BOARD_ESP32C3),y)
CSRCS += src/esp32c3/board/board.c
CSRCS += src/esp32c3/esp32c3_retro.c
ifeq ($(CONFIG_RETRO_AV_CONSOLE),y)
CSRCS += src/esp32c3/driver/cvbs/drv_cvbs_pdm.c
endif
endif

ifeq ($(CONFIG_RETRO_BOARD_RP2040),y)
CSRCS += src/rp2040/board/board.c
CSRCS += src/rp2040/rp2040_retro.c
ifeq ($(CONFIG_RETRO_AV_CONSOLE),y)
CSRCS += src/rp2040/driver/cvbs/drv_cvbs_pio.c
endif

# ---- PIO-USB 主机 HID 键盘（Pico-PIO-USB MIT 移植，NEXT_STEPS 54） ----
ifeq ($(CONFIG_RETRO_PIO_USB),y)
PIOUSB := src/rp2040/driver/input/pio_usb
# 上游原样 + NuttX 垫片：pico-sdk 头重定向经 -I port（优先于系统路径）
CFLAGS += -I$(PIOUSB)/port -I$(PIOUSB)/upstream
CSRCS += $(PIOUSB)/upstream/pio_usb.c
CSRCS += $(PIOUSB)/upstream/pio_usb_host.c
CSRCS += $(PIOUSB)/upstream/usb_crc.c
CSRCS += $(PIOUSB)/port/port_dma.c
CSRCS += src/rp2040/driver/input/piousb_kbd.c
endif
endif

# ---- 头文件搜索路径 ----
CFLAGS += -Isrc/common -Isrc/common/driver -Isrc/common/apps/system
CFLAGS += -Isrc/lvgl/fonts   # 半角点阵表（cvbs_console CLI 档也用）
CFLAGS += -Isrc/esp32s3/board -Isrc/esp32/board
CFLAGS += -Isrc/esp32c3/board -Isrc/rp2040/board
CFLAGS += -Isrc/esp32s3 -Isrc/esp32s3/chip
CFLAGS += -Isrc/esp32s3/driver -Isrc/esp32/driver
CFLAGS += -Isrc/esp32 -Isrc/esp32/chip
CFLAGS += -Isrc/esp32c3 -Isrc/rp2040

# nano 移植层（config/curses/term 覆盖优先）+ nano 上游源码头
# HAVE_CONFIG_H：nano definitions.h 按 autoconf 惯例门控 <config.h>
ifeq ($(CONFIG_RETRO_NANO),y)
CFLAGS += -Isrc/common/nano_port -I$(ENGINES)nano/src -DHAVE_CONFIG_H
endif

# Pico：PIO/DMA 驱动头（只读引用 nuttx arch 源树；arm_internal.h
# 在 armv6m，rp2040_pio.h -> rp2040_gpio.h -> hardware 头链依赖）
ifeq ($(CONFIG_RETRO_BOARD_RP2040),y)
CFLAGS += -I$(APPDIR)/../nuttx/arch/arm/src/rp2040
CFLAGS += -I$(APPDIR)/../nuttx/arch/arm/src/rp2040/hardware
CFLAGS += -I$(APPDIR)/../nuttx/arch/arm/src/armv6m
CFLAGS += -I$(APPDIR)/../nuttx/arch/arm/src/common
endif

# 头文件路径：GUI 构建用真 LVGL；纯 CLI 用 fontbridge 桥字体
ifeq ($(CONFIG_RETRO_GUI),y)
# 源码树的 #ifdef CONFIG_LVGL 守卫在裸 apps 构建里由 -D 点亮
# （不用 Kconfig 定义 LVGL，避免与 apps/graphics/lvgl 撞名）
CFLAGS += -DCONFIG_LVGL=1
CFLAGS += -I$(ENGINES) -I$(ENGINES)lvgl -Ilv_conf_dir
CFLAGS += -Isrc/lvgl -Isrc/lvgl/app -Isrc/lvgl/fonts
# lv_conf.h 的字体开关（Kconfig 宏在裸 apps 构建里没有定义者）
CFLAGS += -DCONFIG_LVGL_FONT_10=1 -DCONFIG_LVGL_FONT_12=1 \
          -DCONFIG_LVGL_FONT_14=1 -DCONFIG_LVGL_FONT_16=1 \
          -DCONFIG_LVGL_FONT_20=1 -DCONFIG_LVGL_FONT_24=1 \
          -DCONFIG_LVGL_FONT_28=1 -DCONFIG_LVGL_FONT_32=1 \
          -DLV_CONF_INCLUDE_SIMPLE
else
ifeq ($(CONFIG_RETRO_AV_CONSOLE),y)
CFLAGS += -Ifontbridge
endif
endif
CFLAGS += -Isrc/common/driver

# 引擎头
ifeq ($(CONFIG_RETRO_SCRIPT_TINYBASIC),y)
CFLAGS += -I$(ENGINES)my_basic/core
endif
ifeq ($(CONFIG_RETRO_SCRIPT_BERRY),y)
CFLAGS += -I$(ENGINES)berry/src -I$(ENGINES)berry/default
endif
ifeq ($(CONFIG_RETRO_SCRIPT_DUKTAPE),y)
CFLAGS += -I$(ENGINES)duktape/src
endif

VPATH += src:src/common:src/common/driver:src/common/apps/system
VPATH += src/esp32s3:src/esp32s3/board:src/esp32s3/driver/fsk
VPATH += src/esp32:src/esp32/board:src/esp32/driver/fsk:src/esp32/driver/audio
VPATH += src/esp32c3:src/esp32c3/board:src/esp32c3/driver/cvbs
VPATH += src/rp2040:src/rp2040/board:src/rp2040/driver/cvbs
ifeq ($(CONFIG_RETRO_NANO),y)
VPATH += src/common/nano_port:$(ENGINES)nano/src
endif
VPATH += $(ENGINES)my_basic/core
VPATH += $(ENGINES)berry/src:$(ENGINES)berry/default
VPATH += $(ENGINES)duktape/src
VPATH += src/lvgl:src/lvgl/app:src/lvgl/fonts:src/lvgl/modules
VPATH += src/lvgl/app/logo:src/lvgl/audio

# ---- 内置程序：一命令一 main（Application.mk 按序配对 PROGNAME）----
# init 入口（CONFIG_INIT_ENTRYPOINT="retro_boot_main"）
MAINSRC   = retro_boot.c
PROGNAME  = retro_boot

# script：ROM 脚本执行（XIP）
ifeq ($(CONFIG_RETRO_SCRIPTS_ROM),y)
PROGNAME += script
MAINSRC  += src/common/apps/system/cmd_script_main.c
endif

# pkg：.rpk 包管理
ifeq ($(CONFIG_RETRO_PKG_MANAGER),y)
PROGNAME += pkg
MAINSRC  += src/common/apps/system/cmd_pkg_main.c
endif

# sysinfo：系统信息（2026-10-06 起默认 .rpk 包交付 `run sysinfo`；
# 无包存储档的裁剪构建回退为 builtin）
ifeq ($(CONFIG_RETRO_PKG_STORE),)
PROGNAME += sysinfo
MAINSRC  += src/common/apps/system/cmd_sysinfo_main.c
endif

# run：执行 ROM 模块应用（XIP 原址执行）
ifeq ($(CONFIG_RETRO_ROMMOD),y)
PROGNAME += run
MAINSRC  += src/common/apps/system/cmd_run_main.c
endif

# shell：GUI 外壳切换（无 GUI 时提示）
PROGNAME += shell
MAINSRC  += src/common/apps/system/cmd_shell_main.c

# ime：CCDOS 式拼音输入法开关（on/off/status）
ifeq ($(CONFIG_RETRO_PINYIN_CLI),y)
PROGNAME += ime
MAINSRC  += src/common/apps/system/cmd_ime_main.c
endif

# wifi：连接与配置管理（status/connect；凭据存 /opt/etc，2026-10-05）
ifeq ($(CONFIG_RETRO_WIFI),y)
PROGNAME += wifi
MAINSRC  += src/common/apps/system/cmd_wifi_main.c
endif

# nano：GNU nano 8.4（全系 CLI 文本编辑器）
ifeq ($(CONFIG_RETRO_NANO),y)
PROGNAME += nano
MAINSRC  += $(ENGINES)nano/src/nano.c
endif

PRIORITY  = SCHED_PRIORITY_MAX
STACKSIZE = 16384
ifneq ($(CONFIG_RETRO_NANO_STACK),)
STACKSIZE_nano.bdat = $(CONFIG_RETRO_NANO_STACK)
endif
MODULE    = $(CONFIG_RETRO)

include $(APPDIR)/Application.mk
