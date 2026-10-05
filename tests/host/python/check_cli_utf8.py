# -*- coding: utf-8 -*-
# pylint: disable=missing-docstring
"""
SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
SPDX-License-Identifier: Apache-2.0

WHAT: CLI 中文可用性校验 / CLI Chinese usability check
WHY : 用户要求"CLI 也可以正常显示使用中文"——串口控制台按
      UTF-8 字节透传，设备侧保证：源内所有中文字符串是合法
      UTF-8、无控制字符混入、NSH 输出串（printf 双语提示）
      编码正确；终端字形由 PC 侧 UTF-8 终端渲染
WHO : tests/host/run_all.sh 第 8 项门禁
WHERE: esp32-retro-ws/tests/host/python/check_cli_utf8.py
WHEN : 2026-10-04 新增
HOW : 1) 全仓 .c/.h/.bas/.be/.js/.py/.lgo 逐文件 UTF-8 解码校验
      2) 扫描源内字符串字面量中是否混入 C0 控制字符（\t \n \r 除外）
      3) 抽验已知 NSH 双语输出串可解码且含中文
"""

import os
import re
import sys

ROOT = os.environ.get("REPO", "/home/user/esp32-retro-ws")
EXTS = (".c", ".h", ".bas", ".be", ".js", ".py", ".lgo")
SKIP_DIRS = {"deps", "tests/host/realinc", ".git"}

fail = 0
checked = 0

for root, dirs, files in os.walk(ROOT):
    dirs[:] = [d for d in dirs if d not in {".git"}]
    rel = os.path.relpath(root, ROOT)
    if any(rel.startswith(s.rstrip("/")) for s in SKIP_DIRS if s):
        continue
    for f in files:
        if not f.endswith(EXTS):
            continue
        path = os.path.join(root, f)
        try:
            with open(path, "rb") as fp:
                text = fp.read().decode("utf-8")
            checked += 1
        except UnicodeDecodeError as e:
            print("INVALID-UTF8:", path, e)
            fail += 1
            continue

        # 字符串字面量内控制字符（除 \t\n\r）会打乱终端输出
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
            lit = m.group(1)
            if any(ord(c) < 32 and c not in "\t\n\r" for c in lit):
                # C 源里 \\x01 之类转义在解码后不会出现控制字符，
                # 命中即说明字面量本身含原始控制字节
                print("CONTROL-CHAR-IN-LITERAL:", path,
                      repr(lit[:40]))
                fail += 1

# 已知 NSH 双语输出抽验（pkg/gpio 的提示语）
KNOWN = [
    ("src/nuttx/common/apps/system/pkg_manager.c",
     ["缺少依赖", "已安装", "架构不符"]),
    ("src/nuttx/common/driver/retro_gpio.c",
     ["已被系统占用"]),
]
for path, needles in KNOWN:
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        print("MISSING:", path)
        fail += 1
        continue
    text = open(full, "rb").read().decode("utf-8", "strict")
    for n in needles:
        if n not in text:
            print("MISSING-ZH-OUTPUT:", path, n)
            fail += 1

print("[check_cli_utf8] %d files checked, %d problems -> %s"
      % (checked, fail, "PASS" if fail == 0 else "FAIL"))
sys.exit(1 if fail else 0)
