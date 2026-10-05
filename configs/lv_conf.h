/**
 * @file lv_conf.h
 * LVGL v9.x 配置文件
 * ESP32-S3 复古图形工作站
 *
 * 注意：LVGL v9.x 的颜色格式在显示驱动中通过
 * lv_display_set_color_format() 设置，而非在此头文件中定义。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <nuttx/config.h>

/*====================
   COLOR SETTINGS
 *===================*/

/* 色深: 8 (256色调色板/索引色模式)
 * LVGL v9 中，LV_COLOR_DEPTH=8 表示每像素 8 位
 * 实际颜色格式在显示驱动中通过 lv_display_set_color_format(disp, LV_COLOR_FORMAT_I8) 设置
 */
#define LV_COLOR_DEPTH 8

/*====================
   MEMORY SETTINGS
 *===================*/

/* 动态内存：走 C 库 malloc（NuttX 堆含 SPIRAM）——内建池会占 128KB SRAM */
#ifndef LV_USE_STDLIB_MALLOC
#  define LV_USE_STDLIB_MALLOC 1   /* LV_STDLIB_CLIB */
#endif
#ifndef LV_USE_STDLIB_STRING
#  define LV_USE_STDLIB_STRING 1
#endif
#ifndef LV_USE_STDLIB_SPRINTF
#  define LV_USE_STDLIB_SPRINTF 1
#endif

/* 字体缓存 */
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_FONT_SUBPX 0

/*====================
   DISPLAY SETTINGS
 *===================*/

/* 显示缓冲区刷新周期 */
#define LV_DEF_REFR_PERIOD 30     /* 30ms 刷新周期 ~33fps */
#define LV_DPI_DEF 130            /* DPI */

/*====================
   FEATURE SETTINGS
 *===================*/

/* 动画 */
#define LV_USE_ANIMATION        1
#define LV_ANIM_REFR_PERIOD     30

/* 输入设备 */
#define LV_USE_INDEV            1
#define LV_USE_KEYBOARD         1
#define LV_USE_MOUSE            1
#define LV_USE_ENCODER          1
#define LV_USE_BUTTON           1

/* 主题 */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK    0  /* 默认浅色主题 */

/* 字体 - 精简配置
 *
 * 默认只启用 Montserrat 14 (LVGL 默认字体)
 * 其他字体大小可通过 Kconfig 配置启用以节省 Flash 空间
 *
 * 各尺寸参考:
 * - 10px: 状态栏、时钟、小标签 (~30KB)
 * - 12px: 正文、按钮 (~40KB)
 * - 14px: 标题、标签 (默认, ~50KB)
 * - 16px: 大标题 (~60KB)
 * - 18px-48px: 大字体显示 (~80KB-200KB/个)
 *
 * 全部启用约占用 800KB Flash
 * 仅启用 14px 约占用 50KB Flash
 */
/* 字号启用策略 / Font enable strategy:
 * 代码实际使用 10/12/14/16/20/24 号（montserrat_11 在 9.5 不存在，已全量换 12）。
 * CONFIG_LVGL_FONT_* 存在时跟随 Kconfig，否则强制启用代码用到的字号，
 * 避免宏展开为空导致 #if 语法错误。
 * Sizes used by code: 10/12/14/16/20/24. Follow Kconfig when defined,
 * otherwise force-enable to avoid empty-macro #if errors.
 *
 * 系统默认字号 = 12px（2026-10-05 历史字号考据：中文 Win3.2/95 界面
 * 宋体 9pt=12px 点阵；Win3.x 拉丁 MS Sans Serif 8pt≈11px 同级——详见
 * HARDWARE.md 6.4 分辨率与字号档位表）。 */
#ifndef CONFIG_LVGL_FONT_10
#define CONFIG_LVGL_FONT_10 1
#endif
#ifndef CONFIG_LVGL_FONT_12
#define CONFIG_LVGL_FONT_12 1
#endif
#ifndef CONFIG_LVGL_FONT_14
#define CONFIG_LVGL_FONT_14 1
#endif
#ifndef CONFIG_LVGL_FONT_16
#define CONFIG_LVGL_FONT_16 1
#endif
#ifndef CONFIG_LVGL_FONT_18
#define CONFIG_LVGL_FONT_18 0
#endif
#ifndef CONFIG_LVGL_FONT_20
#define CONFIG_LVGL_FONT_20 1
#endif
#ifndef CONFIG_LVGL_FONT_22
#define CONFIG_LVGL_FONT_22 0
#endif
#ifndef CONFIG_LVGL_FONT_24
#define CONFIG_LVGL_FONT_24 1
#endif
#ifndef CONFIG_LVGL_FONT_26
#define CONFIG_LVGL_FONT_26 0
#endif
#ifndef CONFIG_LVGL_FONT_28
#define CONFIG_LVGL_FONT_28 0
#endif
#ifndef CONFIG_LVGL_FONT_30
#define CONFIG_LVGL_FONT_30 0
#endif
#ifndef CONFIG_LVGL_FONT_32
#define CONFIG_LVGL_FONT_32 0
#endif
#ifndef CONFIG_LVGL_FONT_34
#define CONFIG_LVGL_FONT_34 0
#endif
#ifndef CONFIG_LVGL_FONT_36
#define CONFIG_LVGL_FONT_36 0
#endif
#ifndef CONFIG_LVGL_FONT_38
#define CONFIG_LVGL_FONT_38 0
#endif
#ifndef CONFIG_LVGL_FONT_40
#define CONFIG_LVGL_FONT_40 0
#endif
#ifndef CONFIG_LVGL_FONT_42
#define CONFIG_LVGL_FONT_42 0
#endif
#ifndef CONFIG_LVGL_FONT_44
#define CONFIG_LVGL_FONT_44 0
#endif
#ifndef CONFIG_LVGL_FONT_46
#define CONFIG_LVGL_FONT_46 0
#endif
#ifndef CONFIG_LVGL_FONT_48
#define CONFIG_LVGL_FONT_48 0
#endif

#define LV_FONT_MONTSERRAT_10   CONFIG_LVGL_FONT_10
#define LV_FONT_MONTSERRAT_12   CONFIG_LVGL_FONT_12
#define LV_FONT_MONTSERRAT_14   CONFIG_LVGL_FONT_14

/* 默认字体 12px：与 RETRO_FONT_DEFAULT（GUI 档中文 12px 点阵）对齐 */
#ifndef LV_FONT_DEFAULT
#  define LV_FONT_DEFAULT        &lv_font_montserrat_12
#endif
#define LV_FONT_MONTSERRAT_16   CONFIG_LVGL_FONT_16
#define LV_FONT_MONTSERRAT_18   CONFIG_LVGL_FONT_18
#define LV_FONT_MONTSERRAT_20   CONFIG_LVGL_FONT_20
#define LV_FONT_MONTSERRAT_22   CONFIG_LVGL_FONT_22
#define LV_FONT_MONTSERRAT_24   CONFIG_LVGL_FONT_24
#define LV_FONT_MONTSERRAT_26   CONFIG_LVGL_FONT_26
#define LV_FONT_MONTSERRAT_28   CONFIG_LVGL_FONT_28
#define LV_FONT_MONTSERRAT_30   CONFIG_LVGL_FONT_30
#define LV_FONT_MONTSERRAT_32   CONFIG_LVGL_FONT_32
#define LV_FONT_MONTSERRAT_34   CONFIG_LVGL_FONT_34
#define LV_FONT_MONTSERRAT_36   CONFIG_LVGL_FONT_36
#define LV_FONT_MONTSERRAT_38   CONFIG_LVGL_FONT_38
#define LV_FONT_MONTSERRAT_40   CONFIG_LVGL_FONT_40
#define LV_FONT_MONTSERRAT_42   CONFIG_LVGL_FONT_42
#define LV_FONT_MONTSERRAT_44   CONFIG_LVGL_FONT_44
#define LV_FONT_MONTSERRAT_46   CONFIG_LVGL_FONT_46
#define LV_FONT_MONTSERRAT_48   CONFIG_LVGL_FONT_48

/* 中文字体 - 文泉驿 */
#define LV_USE_FONT_WQY_MICRO_HEI 1

/* Unicode 支持 */
#define LV_USE_UNICODE 1
#define LV_BIG_ENDIAN_SYSTEM 0

/* 样式 */
#define LV_USE_STYLE 1
#define LV_STYLE_MAX 64

/* 日志 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* GPU - ESP32 上使用软件渲染 */
#define LV_USE_GPU_ESP32 0
#define LV_USE_GPU_SDL 0

/* 其他 */
#define LV_USE_SYSTRAY       1
#define LV_USE_FILE_EXPLORER 1
#define LV_USE_IMGFONT       1

/*====================
   COMPILER SETTINGS
 *===================*/

#define LV_ATTRIBUTE_TICK_HANDLER __attribute__((section(".iram1")))
#define LV_ATTRIBUTE_TIMER_HANDLER __attribute__((section(".iram1")))
#define LV_ATTRIBUTE_FLUSH_ATTR __attribute__((aligned(64)))

/*====================
   ESP32 SPECIFIC
 *===================*/

/* NuttX 下使用 clock_gettime 获取 tick */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <time.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (clock_gettime_monotonic_ns() / 1000000)

/* PSRAM 颜色缓冲区大小 */
#define LV_COLOR_BUF_SIZE (32 * 1024)  /* 32KB - 可根据实际帧缓冲大小调整 */

/*====================
   WINDOWS 3.2 复古主题
 *===================*/

/* LVGL v9.x 的 lv_theme_default_init 签名为:
 * lv_theme_t *lv_theme_default_init(lv_display_t *disp,
 *     lv_color_t color_primary, lv_color_t color_secondary,
 *     bool dark_bg, const lv_font_t *font)
 * 在代码中直接调用而非通过宏
 */

#endif /* LV_CONF_H */
