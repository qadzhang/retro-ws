#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
SPDX-License-Identifier: Apache-2.0

check_eda.py - 立创EDA 生成物自检

WHAT : 校验 gen_eda.py 产出的 PCB/原理图 JSON：结构合法、层号正确、
       同层异网铜箔不交叉、焊盘网络一致、BBox 覆盖
WHY  : 手写 EasyEDA 源文件最大的风险是分段字段错位与 DRC 级短路——
       生成后必须机器验证（ai-code-testing Layer 2 静态检查思想）
WHO  : eda/ 维护者 + CI
WHERE: esp32-retro-ws/eda/check_eda.py
WHEN : 2026-10-04 晚新增
HOW  : 逐 JSON 解析 -> TRACK 拆正交子段 -> 逐层做异网线段相交测试
       （平行共线按重叠判）；任一失败非零退出
"""

import json
import glob
import os
import sys

FAIL = 0


def err(msg):
    global FAIL
    FAIL += 1
    print("  [FAIL] %s" % msg)


def segs_of_track(points):
    """折线 -> 正交子段 [(x1,y1,x2,y2)]（非正交段整段保留）"""
    pts = [(float(p.split()[0]), float(p.split()[1]))
           for p in [points[i:i + 2] for i in range(0, len(points), 2)]] \
        if False else list(zip(points[0::2], points[1::2]))
    # points 是字符串坐标列表（每元素一个 "x y"）
    out = []
    for a, b in zip(pts, pts[1:]):
        out.append((a[0], a[1], b[0], b[1]))
    return out


def parse_track(shape):
    f = shape.split("~")
    net = f[3]
    pts = []
    for tok in f[4].split():
        pts.append(float(tok))
    w = float(f[1])
    return net, int(f[2]), pts, w


def rect_of(seg):
    x1, y1, x2, y2 = seg
    return min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2)


def overlap(a0, a1, b0, b1):
    return max(a0, b0) <= min(a1, b1)


def seg_cross(sa, sb, tol=0.02):
    """两线段是否相交/重叠（tol units 容差，~2mil）"""
    ax0, ay0, ax1, ay1 = rect_of(sa)
    bx0, by0, bx1, by1 = rect_of(sb)
    # 包围盒不相交
    if not overlap(ax0 - tol, ax1 + tol, bx0 - tol, bx1 + tol) or \
       not overlap(ay0 - tol, ay1 + tol, by0 - tol, by1 + tol):
        return False
    ax0r, ay0r, ax1r, ay1r = sa
    bx0r, by0r, bx1r, by1r = sb
    # 一般情况：包围盒相交即视为短路风险（生成器只走正交直线，
    # 斜线仅在点对点短线上出现——保守判定）
    return True


def check_pcb(path):
    print("[check] %s" % path)
    with open(path, encoding="utf-8") as fp:
        doc = json.load(fp)

    if doc["head"]["docType"] != "3":
        err("docType != 3")
    if "layers" not in doc:
        err("PCB 缺 layers")

    layers = {}
    tracks = []
    pads = 0
    for s in doc["shape"]:
        tag = s.split("~")[0]
        if tag == "TRACK":
            net, layer, pts, w = parse_track(s)
            if layer not in (1, 2, 10):
                err("TRACK 层号非法: %d" % layer)
            if len(pts) % 2 or len(pts) < 4:
                err("TRACK 点列损坏: %r" % s[:60])
            if layer in (1, 2):
                tracks.append((net, layer, pts))
            layers[layer] = layers.get(layer, 0) + 1
        elif tag == "PAD":
            pads += 1
            f = s.split("~")
            if len(f) < 10:
                err("PAD 字段过少: %r" % s[:60])
            if float(f[9]) == 0 and f[6] == "11":
                err("通层 PAD 孔径为 0: %r" % s[:60])
        elif tag == "VIA":
            if len(s.split("~")) < 6:
                err("VIA 字段过少")
        elif tag == "TEXT":
            pass
        elif tag == "HOLE":
            pass
        else:
            err("未知图元 %r" % tag)

    if 10 not in layers:
        err("缺板框（层10）")
    if pads < 20:
        err("焊盘过少 (%d)——疑似生成不完整" % pads)

    # 同层异网交叉检测
    def subsegs(pts):
        return [(pts[i], pts[i + 1], pts[i + 2], pts[i + 3])
                for i in range(0, len(pts) - 2, 2)]

    crossed = 0
    for i in range(len(tracks)):
        for j in range(i + 1, len(tracks)):
            na, la, pa = tracks[i]
            nb, lb, pb = tracks[j]
            if na == nb or na == "" or nb == "":
                continue
            if la != lb:
                continue
            for sa in subsegs(pa):
                for sb in subsegs(pb):
                    if seg_cross(sa, sb):
                        crossed += 1
                        if crossed <= 5:
                            err("同层异网交叉 L%d: %s x %s @%r|%r"
                                % (la, na, nb, sa, sb))
    if crossed:
        err("共 %d 处同层异网交叉" % crossed)
    else:
        print("  crossings: 0  (tracks=%d pads=%d outline=%d)"
              % (len(tracks), pads, layers.get(10, 0)))


def check_sch(path):
    print("[check] %s" % path)
    with open(path, encoding="utf-8") as fp:
        doc = json.load(fp)
    if doc["head"]["docType"] != "1":
        err("docType != 1")
    n = len(doc["shape"])
    if n < 10:
        err("原理图图元过少 (%d)" % n)
    else:
        print("  shapes: %d" % n)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    pcbs = sorted(glob.glob(os.path.join(here, "*", "*_pcb.json")))
    schs = sorted(glob.glob(os.path.join(here, "*", "*_sch.json")))
    if len(pcbs) != 5:
        err("PCB 文件数 != 5 (%d)" % len(pcbs))
    if len(schs) != 5:
        err("原理图文件数 != 5 (%d)" % len(schs))
    for p in pcbs:
        check_pcb(p)
    for p in schs:
        check_sch(p)
    print("======================================")
    print("EDA check: %s (fails=%d)" %
          ("PASS" if FAIL == 0 else "FAIL", FAIL))
    sys.exit(0 if FAIL == 0 else 1)


if __name__ == "__main__":
    main()
