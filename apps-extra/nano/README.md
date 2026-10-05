# GNU nano 8.4 —— .rpk 独立安装包（不入固件 ROM）

> **许可证边界（2026-10-05 项目所有者定稿）**：GNU nano 为 **GPL-3.0**，
> 按 AGENTS.md 11.1 第 9 条红线**禁止编入固件 ROM**；固件默认 CLI 编辑器
> 为 NuttX 内置 vi（`CONFIG_SYSTEM_VI=y`）。nano 以 deb 风格 .rpk 安装包
> 交付（mere aggregation：独立 ELF，binfmt 独立进程，不与固件链结）。

## 组成

| 项 | 内容 |
|----|------|
| 上游源码 | `deps/nano/`（nano-8.4 原版 tar.xz 手动解压，sha256 前 16 位 `5ad29222bbd55624`） |
| NuttX 适配层 | `src/nuttx/common/nano_port/`（mini-curses 垫片 + compat + config，上游不改） |
| 构建规则 | `Makefile` + `Kconfig`（同步到 `deps/nuttx-apps/external/nano/` 参与 LOADABLE 构建） |
| 包模板 | `package/`（control / postinst / data 载荷树） |

## 构建流程

```bash
# 1. 固件构建（nano 不在其中，默认编辑器为 vi）
./scripts/firmware/build_firmware.sh s3

# 2. 打包（同步源码到 external/ -> 提示 LOADABLE 构建 -> 收集 ELF -> .rpk）
./scripts/build_packages.sh --target esp32s3

# 3. 设备侧安装（SD 卡）
nsh> pkg install /sdcard/pkg/nano-8.4-1.rpk
nsh> /opt/bin/nano
```

LOADABLE 构建需目标 defconfig 开 `CONFIG_ELF=y` + `CONFIG_BUILD_LOADABLE=y`
+ `CONFIG_EXTERNAL_NANO=y`（binfmt 链路与 UCBLogo 同走 NEXT_STEPS 验证任务）。

## 历史注记

2026-10-04 晚 nano 曾按"全系统一编辑器"编入五板固件（mini-curses 移植层
即彼时产物，保留复用）；2026-10-05 按许可证红线翻转为包交付 + 系统默认 vi。
