# apps-extra/ucblogo - UCBLogo 独立安装包（GPL-2.0-or-later）

## 许可证与架构声明（为什么它在这里而不在 src/）

UCBLogo 采用 **GPL-2.0-or-later**，与本项目固件的 Apache-2.0 存在传染性问题。
因此采用**进程隔离（mere aggregation / 单纯聚合）**架构：

- 固件（ROM 中的 nuttx.bin）：**不含任何 GPL 代码**，保持 Apache-2.0
- UCBLogo：编译为**独立 ELF 可执行文件**，放 SD 卡 `/sdcard/apps/`，
  由 NuttX binfmt 以独立任务加载运行
- 通信边界：NSH 命令行执行、文件读写（进程间松耦合，无静态/动态符号
  绑定到固件映像），GPL 义务不延伸到固件本体
- GPL 程序调用固件提供的系统服务（如未来的 retro_ui 导出符号）视同
  "应用程序调用操作系统 API"（GPLv3 系统库例外），同样不传染固件

## 构建

由 `scripts/build_packages.sh` 驱动（不要手工编译）：

1. 从 SourceForge 下载 UCBLogo 源码（GPL-2.0+）
2. 同步本目录到 `deps/nuttx-apps/external/ucblogo`
3. 在 NuttX 构建中以 LOADABLE 应用方式产出 `ucblogo.elf`
4. 收集到 `dist/sdcard/apps/ucblogo.elf`

## 安装（用户侧）

把 `dist/sdcard/` 整体拷入 SD 卡后：

```bash
nsh> pkg list                      # 列出已安装的独立程序
nsh> /sdcard/apps/ucblogo          # 直接运行（binfmt 加载）
```

## 分发注意

分发 `ucblogo.elf` 时必须同时提供 UCBLogo 完整源码（GPL 要求）；
`build_packages.sh` 产出的 dist 目录已包含源码包副本。
