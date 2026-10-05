# eda/ - 立创EDA 载板工程（五板）

> SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
> SPDX-License-Identifier: Apache-2.0

## WHAT

为五块目标开发板各生成一块**最小载板（carrier）**的立创EDA（标准版）
工程文件：PCB（docType 3）+ 原理图连接图（docType 1）。

设计约束（用户要求，2026-10-04）：

1. **全部插接件**：开发板模块、外围功能模块（DAC/RTC/麦克风/SD/按键/
   电阻网络）一律经**母排插座（2.54mm）或 FPC 座**安装，不焊死在
   载板上——模块可随时插拔更换；
2. **载板上只有连接器与铜箔**：R-2R 电阻梯 / RC 滤波也做成
   "电阻模块"插接（retro 口味，类似老式 DIP 电阻排），板上无
   需焊接的分立阻容（RCA 视频座本体即连接器，属允许范围）；
3. **面积尽量小**：每板尺寸 = 模块包络 + 两侧插座走廊 + 边距。

## 文件与板型

| 目录 | 板 | 载板尺寸 (mm) | 模块插座 | 外设插座 |
|------|-----|---------------|----------|----------|
| `s3/` | ESP32-S3-DevKitC-1 N16R8 | 58 x 90 | 2x22 母排 @22.86mm | R-2R/I2S DAC/RTC/麦克风/microSD/2 按键/UART + RCA |
| `s3n8/` | ESP32-S3-DevKitC-1 N8R8 | 同上（同封装） | 同上 | 同上 |
| `cam/` | ESP32-CAM (AI-Thinker) | 57 x 58 | 2x8 母排 @22.86mm + 24P FPC 0.5mm | CVBS 滤波/功放/RTC/麦克风/UART/5V + RCA |
| `c3/` | 合宙 ESP32-C3 核心板 | 49 x 71 | 2x16 母排 @21.0mm（邮票孔焊公排后插入） | PDM 滤波/microSD/UART/I2C+教学/5V + RCA |
| `pico/` | Raspberry Pi Pico | 48.5 x 78 | 2x20 母排 @17.78mm | R-2R/microSD/UART/教学 GP2-11/ADC GP26-28/USB 键盘 GP20-21(22Ω) + RCA |

引脚分配严格对齐 `HARDWARE.md`（含 2026-10-04 晚修订：S3 CVBS=
LCD_CAM GPIO2/15/16/17、C3 PDM=GPIO1、Pico R-2R=GP12-15、
CAM 摄像头/AV 互斥 FPC 引脚表）。

## 如何打开（立创EDA标准版）

1. 打开 [lceda.cn](https://lceda.cn)（或专业版）；
2. **文件 > 打开 > 嘉立创EDA源文件**（专业版：文件 > 导入 >
   嘉立创EDA(标准版)），选择 `*_carrier_pcb.json` 或 `*_carrier_sch.json`；
3. PCB 打开后：设定板层为 2 层、确认板框（黄色层 10 矩形）、
   可直接"放置 > 铺铜"给 GND 补铜（生成文件未铺铜，保持铜箔最小）。

## 生成与校验（改引脚流程）

改引脚必须先改 `HARDWARE.md` 与 `src/nuttx/*/board/hw_*.h`，再改
`gen_eda.py` 中对应板型的引脚表，然后：

```bash
cd eda
python3 gen_eda.py     # 重新生成五板（布不通会直接断言报错）
python3 check_eda.py   # 结构 + 同层异网交叉校验（0 交叉才 PASS）
```

`gen_eda.py` 内置两层曼哈顿自动布线器（顶层 L/Z、底层过孔通道、
边距绕行、FPC 垂直下潜、暴力兜底），逐段做间距/压盘冲突检测；
`check_eda.py` 独立复核：docType/层号合法、板框存在、焊盘数量、
**同层不同网络铜箔零交叉**。

## 各板互连说明（要点）

- **s3/s3n8**：CVBS R-2R 四位（GPIO2/15/16/17）插电阻模块后经 RCA 出；
  I2S DAC（MAX98357A）、RTC（PCF8563）、麦克风（MAX9814）、microSD
  模块各一个插座；两个教学按键（GPIO7/8）座；UART0 调试座。
- **cam**：**GPIO25/26/34/21/22 只在摄像头 FPC 上引出**——载板带
  24P/0.5mm FPC 座，AV 模式用 FPC 线从 CAM 板摄像头座引到本座
  （摄像头与 AV 互斥，见 HARDWARE.md 3.10）；F10(DGND)/F14(DOVDD)
  为摄像头模式供电，不并入载板电源树。
- **c3**：核心板为邮票孔封装——先在核心板边缘焊公排（2.54mm 间距
  正好兼容），再插入载板 2x16 母排；两排中心距 21.0mm（**不在
  2.54 网格上**，母排坐标为定制值）；CVBS 为 PDM 单脚（GPIO1）+
  插接 RC 滤波模块（270Ω+47pF）。
- **pico**：R-2R 在 GP12-15（GP23=SMPS 省电脚禁用，见 HARDWARE.md
  3B.2A 修订）；GP2-11 与 GP26-28 全部引出到教学插座。

## 已知限制

- 原理图为**网络标号连接图**（母排引脚名 + 网络名），非标准符号
  原理图——PCB 为权威交付物；
- RCA 座焊盘为通用 3 脚足迹，打板前请按实际选购型号微调；
- 未铺铜/未加泪滴/未做 DRC 丝印避让——建议在立创EDA 里完成这些
  收尾再下单（JLC 经济板 2 层即可）。
