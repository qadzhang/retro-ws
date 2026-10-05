#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 Retro WS Project
# SPDX-License-Identifier: Apache-2.0
#
# WHAT : esptool 包装器（兼容历史 `esptool.py` 调用名）
# WHY  : 烧录命令统一入口，不依赖某个用户的安装路径
# WHO  : Retro WS Project Team
# WHERE: retro-ws/bin/esptool.py（build.sh flash 经 PATH 调用）
# WHEN : 2026-10-04 初版；2026-10-05 去写死的 /home/user 绝对路径
# HOW  : 依次尝试 PATH 中的 esptool -> pip --user 安装位 -> python -m esptool
#
if [ -x "$HOME/.local/bin/esptool" ]; then
    exec "$HOME/.local/bin/esptool" "$@"
elif command -v esptool >/dev/null 2>&1; then
    exec esptool "$@"
else
    exec python3 -m esptool "$@"
fi
