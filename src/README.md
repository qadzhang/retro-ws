# src/ 源代码目录

## 目录结构

```
src/
├── README.md           # 本文件
├── nuttx/              # NuttX 适配层
│   ├── Kconfig         # NuttX menuconfig 配置
│   └── esp32s3/        # ESP32-S3 特定代码
│       ├── esp32s3_retro.c    # 双核主入口
│       ├── bootmenu.c         # 启动菜单
│       ├── script_engines.c   # 脚本引擎集成
│       ├── network_utils.c    # curl/wget
│       ├── board/             # 板级支持包
│       ├── chip/              # 芯片寄存器定义
│       ├── driver/            # 设备驱动
│       │   ├── cvbs/          # CVBS 显示驱动
│       │   ├── audio/         # I2S 音频驱动
│       │   ├── fsk/           # FSK 磁带调制解调驱动
│       │   ├── watchdog.c      # 看门狗驱动
│       │   ├── firewall.c      # 防火墙
│       │   ├── memmon.c       # 内存监控
│       │   ├── network.c      # WiFi/网络
│       │   ├── ntp.c          # NTP 时间同步
│       │   ├── cron.c         # Cron 定时任务
│       │   ├── drv_rtc.c      # RTC 驱动
│       │   ├── usb_hid.c      # USB HID
│       │   ├── drv_pinyin.c   # 拼音输入法
│       │   ├── drv_player.c   # 媒体播放
│       │   ├── drv_recorder.c # 录音
│       │   └── drv_sqlite.c   # SQLite
│       └── apps/system/       # NSH 命令
├── lvgl/               # LVGL 驱动与应用
│   ├── lv_port_disp.c  # 显示端口 ⭐
│   ├── lv_port_disp.h  # 显示端口头文件 ⭐
│   ├── lv_port_indev.c # 输入设备端口 ⭐
│   ├── lv_port_indev.h # 输入设备端口头文件 ⭐
│   ├── lvgl_app.c      # LVGL 初始化入口
│   ├── i18n.c          # 多语种框架
│   ├── i18n.h          # 多语种头文件
│   ├── retro_ui.c      # 脚本 UI 胶水层
│   ├── app/            # LVGL 应用程序
│   │   ├── desktop.c   # 桌面管理器
│   │   ├── app_editor.c    # 记事本
│   │   ├── app_browser.c  # 浏览器
│   │   ├── app_terminal.c # 终端
│   │   ├── app_pinyin.c   # 拼音输入法
│   │   ├── app_player.c   # 媒体播放器
│   │   ├── app_recorder.c # 录音机
│   │   └── app_sqlite.c   # SQLite 工具
│   ├── assets/icons/   # 图标资源
│   ├── fonts/          # 字体文件
│   ├── audio/         # 音频处理
│   └── modules/       # 脚本胶水层模块
└── scripts/            # 脚本引擎集成
    ├── mybasic/       # my_basic
    └── duktape/       # Duktape JS
```

## 编译说明

所有源代码通过 NuttX menuconfig 集成，单独源文件位于各自模块目录中。

## 编码规范

参见项目根目录 `CODING_STANDARD.md`

---

_最后更新: 2026-03-29_
