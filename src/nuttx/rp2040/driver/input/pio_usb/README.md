# Pico-PIO-USB 集成（USB 主机 / HID boot 键盘）

> **本目录核心引擎是另一个开源项目的成果**：
> [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
> （作者 sekigon-gonnoc，**MIT License**，见 `upstream/LICENSE`）。
> 它用 RP2040 的 PIO 状态机（1 SM 发 + 2 SM 收）纯软件实现 USB
> 全速主机，支持 HUB 多口与键鼠 HID——这是 RP2040 做 USB 主机的
> 社区事实标准方案。

## 许可证兼容性（2026-10-05 调研定稿）

| 组件 | 许可证 | 与本项目（固件 = Apache-2.0/MIT，零 GPL 红线） |
|------|--------|----------------------------------------------|
| Pico-PIO-USB 上游源码 | **MIT**（Copyright (c) 2021 sekigon-gonnoc） | ✅ 兼容，可编入固件 ROM（保留版权声明与 LICENSE 副本即可） |
| TinyUSB（上游可选依赖） | MIT | 本集成**不使用**（枚举序列由本项目驱动自写） |

## 目录结构

```
pio_usb/
+-- upstream/          # 上游 v?（master 快照，2026-10-05）原样引入，零修改
|   +-- LICENSE        # MIT 许可证副本（随源码分发义务）
|   +-- pio_usb.c/.h   #   总线层（NRZI 编解码/令牌/握手/1ms 帧）
|   +-- pio_usb_host.c #   主机端点管理/控制传输原语/IRQ 处理
|   +-- pio_usb_ll.h   #   低层结构与内联
|   +-- usb_crc.c/.h   #   CRC5/CRC16
|   +-- usb_definitions.h        # USB 描述符/请求定义
|   +-- pio_usb_configuration.h  # 资源布局配置
|   +-- usb_tx.pio.h / usb_rx.pio.h  # 预编译 PIO 程序指令数组
|   +-- sdk_compat.h   #   pico-sdk 版本兼容（走 SDK1.x 分支）
+-- port/              # NuttX 垫片（AGENTS.md 11.5 覆盖头路线）：
|                      # pico-sdk 的 hardware/pio.h、dma.h、gpio.h、
|                      # clocks.h、sync.h、pico/*.h 以 NuttX rp2040_*
|                      # 与寄存器直写实现同语义；port_dma.c 为 DMA 通道实现
+-- ../piousb_kbd.c    # 本项目驱动：枚举状态机 + HID boot 键盘轮询 +
                       # hid_ascii -> cvbs_console_feed_keys 键流桥，
                       # 1ms 任务钉 CPU1（媒体/IO 核，用户 2026-10-05 指示）
```

## 资源占用

| 资源 | 占用 | 与本项目冲突 |
|------|------|--------------|
| PIO1 块（SM0/1/2） | PIO-USB TX/RX/EOP | 无（CVBS 用 PIO0-SM0） |
| DMA 通道 0 | TX 数据推送（DREQ 节流） | 无（CVBS 通道经 NuttX DMAC 另配） |
| GP20（DP）/GP21（DM） | USB 差分对（串 22Ω） | 教学脚清单相应收缩（HARDWARE 3B.3） |
| ~15KB ROM/RAM | 上游声明的占用面 | 2MB Flash / 264KB SRAM 可承受 |

## 用法与接线

- D+ 接 GP20、D- 接 GP21（经 22Ω 串联电阻；usb_std A 口母座 VBUS 接 5V）
- 软件无需配置：开机自动检测键盘、枚举、按键直接进 NSH/输入法
- 引脚可经 Kconfig `RETRO_PIO_USB_DP_PIN` 调整
