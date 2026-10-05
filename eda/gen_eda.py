#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
SPDX-License-Identifier: Apache-2.0

gen_eda.py - 立创EDA(标准版) 载板工程生成器（两层自动布线版）

WHAT : 为五块目标板（s3/s3n8/cam/c3/pico）生成立创EDA标准版可直开
       的 PCB 与原理图 JSON 源文件（docType 3 / 1），铜箔由内置
       两层曼哈顿布线器自动完成（冲突检测 + 通道分配 + 重试）
WHY  : 交付"每板一块最小载板"：模块经插接件（母排/FPC座）安装、
       不直接焊死（用户要求 2026-10-04）；阻容全部做成插接式
       电阻/滤波模块，载板上只有连接器与铜箔
WHO  : eda/ 目录维护者（改引脚先改 HARDWARE.md 再改本表）
WHERE: esp32-retro-ws/eda/gen_eda.py
WHEN : 2026-10-04 晚新增
HOW  : 单位换算 1 unit = 10mil = 0.254mm（EasyEDA 官方文档）；
       模块排针坐标 = 实测几何（DevKitC 2x22@22.86mm、CAM 2x8@22.86、
       Luatos C3 2x16@21.0、Pico 2x20@17.78）；
       布线：信号优先顶层 L/Z 形（多候选通道），失败自动落底层
       （src 过孔 -> 底层竖+横+竖 -> dst 过孔），每网唯一通道，
       逐段冲突检测（含压焊盘）；全部布通才算生成成功
"""

import json
import os

U = 1000.0 / 254.0  # 1mm = 1000/254 units（≈3.937）


def mm(v):
    """毫米 -> EasyEDA 单位（3 位小数）"""
    return round(v * U, 3)


# ---------------------------------------------------------------- 图元层

LAYERS = [
    "1~TopLayer~#FF0000~true~true~true",
    "2~BottomLayer~#0000FF~true~true~true",
    "3~TopSilkLayer~#FF00FF~true~true~true",
    "4~BottomSilkLayer~#8B4513~true~false~false",
    "5~TopPasteMaskLayer~#008000~true~false~false",
    "6~BottomPasteMaskLayer~#008000~true~false~false",
    "7~TopSolderMaskLayer~#800000~true~false~false",
    "8~BottomSolderMaskLayer~#800000~true~false~false",
    "9~Ratlines~#FF00FF~true~true~true",
    "10~BoardOutLine~#FFFF00~true~true~true",
    "11~Multi-Layer~#FFFFFF~true~true~true",
]

CANVAS_PCB = ("CA~1000~1000~#000000~yes~#CCCCCC~10~1200~1200~line~"
              "1~mil~1~45~visible~0.5~4000~3000")
CANVAS_SCH = ("CA~1000~1000~#FFFFFF~yes~#CCCCCC~10~1200~1200~line~"
              "1~mil~1~45~visible~0.5~4000~3000")


class Doc(object):
    def __init__(self, doc_type, canvas):
        self.doc_type = doc_type
        self.canvas = canvas
        self.shapes = []
        self._id = 100

    def nid(self):
        self._id += 1
        return "gge%d" % self._id

    def bbox(self):
        xs, ys = [], []
        for s in self.shapes:
            f = s.split("~")
            try:
                if f[0] == "TRACK":
                    pts = f[4].split()
                    xs += [float(p) for p in pts[0::2]]
                    ys += [float(p) for p in pts[1::2]]
                elif f[0] in ("PAD",):
                    xs.append(float(f[2]))
                    ys.append(float(f[3]))
                elif f[0] in ("VIA", "HOLE"):
                    xs.append(float(f[1]))
                    ys.append(float(f[2]))
                elif f[0] == "TEXT":
                    xs.append(float(f[2]))
                    ys.append(float(f[3]))
            except (ValueError, IndexError):
                pass
        if not xs:
            return {"x": 0, "y": 0, "width": 100, "height": 100}
        return {"x": min(xs), "y": min(ys),
                "width": max(xs) - min(xs), "height": max(ys) - min(ys)}

    def dumps(self, path):
        doc = {
            "head": {"docType": self.doc_type, "editorVersion": "6.5.0",
                     "c_para": {}, "x": "0", "y": "0"},
            "canvas": self.canvas,
            "shape": self.shapes,
        }
        if self.doc_type == "3":
            doc["layers"] = LAYERS
            doc["objects"] = ["All~true~false", "Track~true~true",
                              "Pad~true~true", "Via~true~true",
                              "Text~true~true", "Hole~true~true"]
        doc["BBox"] = self.bbox()
        doc["colors"] = {}
        with open(path, "w", encoding="utf-8") as fp:
            json.dump(doc, fp, ensure_ascii=False, indent=1)


def track(doc, net, pts_mm, layer=1, width_mm=0.25):
    p = " ".join("%.3f %.3f" % (mm(x), mm(y))
                 for x, y in zip(pts_mm[0::2], pts_mm[1::2]))
    doc.shapes.append("TRACK~%s~%d~%s~%s~%s~0"
                      % ("%.3f" % mm(width_mm), layer, net, p, doc.nid()))


def pad(doc, net, num, x, y, hole_mm=1.0, dia_mm=1.8, shape="ELLIPSE",
        smd=False):
    if smd:
        doc.shapes.append(
            "PAD~%s~%.3f~%.3f~%.3f~%.3f~1~%s~%s~0~~0~%s~0~~N~0~0~0~%.3f,%.3f"
            % (shape, mm(x), mm(y), mm(dia_mm), mm(hole_mm), net, num,
               doc.nid(), mm(x), mm(y)))
    else:
        doc.shapes.append(
            "PAD~%s~%.3f~%.3f~%.3f~%.3f~11~%s~%s~%.3f~~0~%s~0~~Y~0~0~%.3f~"
            "%.3f,%.3f"
            % (shape, mm(x), mm(y), mm(dia_mm), mm(dia_mm), net, num,
               mm(hole_mm / 2.0), doc.nid(), mm(hole_mm / 2.0),
               mm(x), mm(y)))


def via(doc, net, x, y, dia_mm=0.9, hole_mm=0.5):
    doc.shapes.append("VIA~%.3f~%.3f~%.3f~%s~%.3f~%s~0"
                      % (mm(x), mm(y), mm(dia_mm), net, mm(hole_mm / 2.0),
                         doc.nid()))


def silk_text(doc, text, x, y, size_mm=1.2, rot=0):
    doc.shapes.append(
        "TEXT~L~%.3f~%.3f~1~%d~0~3~~%.3f~%s~M%.3f,%.3f~1~%s~0"
        % (mm(x), mm(y), rot, mm(size_mm), text, mm(x), mm(y), doc.nid()))


def hole(doc, x, y, dia_mm=3.2):
    doc.shapes.append("HOLE~%.3f~%.3f~%.3f~%s~0"
                      % (mm(x), mm(y), mm(dia_mm / 2.0), doc.nid()))


def add_hole_block(carrier, x, y, dia_mm=3.2):
    """安装孔：出图元 + 布线器登记为铜箔禁入区"""
    hole(carrier.doc, x, y, dia_mm)
    carrier.router.add_pad("__HOLE__", x, y, dia_mm / 2.0)


def outline(doc, x0, y0, x1, y1):
    track(doc, "", [x0, y0, x1, y0, x1, y1, x0, y1, x0, y0],
          layer=10, width_mm=0.1)


# ---------------------------------------------------------------- 布线器

def _segs_of(pts):
    """曼哈顿折线 -> 子段矩形 [(x0,y0,x1,y1)]"""
    out = []
    for a, b in zip(zip(pts[0::2], pts[1::2]), zip(pts[2::2], pts[3::2])):
        x0, y0 = a
        x1, y1 = b
        out.append((min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)))
    return out


class Router(object):
    """两层冲突检测布线器：同网允许重叠，异网最小间距 CLEAR"""

    CLEAR = 0.35  # mm（JLC 工艺：10mil 线距 + 14mil 间距内）

    def __init__(self, doc):
        self.doc = doc
        self.top_segs = []      # (net, rect)
        self.bot_segs = []
        self.pads = []          # (net, x, y, r) —— 两层都避让

    def add_pad(self, net, x, y, r=0.9):
        self.pads.append((net, x, y, r))

    def _pool(self, layer):
        return self.top_segs if layer == 1 else self.bot_segs

    def _ok(self, layer, net, segs):
        clear = 0.30  # 线-线：0.6mm 平行间距（0.25 线宽 -> 0.35 缝隙）
        for onet, (x0, y0, x1, y1) in self._pool(layer):
            if onet == net:
                continue
            for sx0, sy0, sx1, sy1 in segs:
                if (max(x0 - clear, sx0 - clear) <=
                        min(x1 + clear, sx1 + clear) and
                        max(y0 - clear, sy0 - clear) <=
                        min(y1 + clear, sy1 + clear)):
                    return False
        for pnet, px, py, pr in self.pads:
            if pnet == net:
                continue
            # 焊盘禁入 = 盘半径 + 半线宽 + 0.15 间距（0.5mm FPC 可逃逸）
            pad_clear = pr + 0.28
            for sx0, sy0, sx1, sy1 in segs:
                if (sx0 - pad_clear <= px <= sx1 + pad_clear and
                        sy0 - pad_clear <= py <= sy1 + pad_clear):
                    return False
        return True

    def dry_ok(self, layer, net, pts):
        return self._ok(layer, net, _segs_of(pts))

    def commit(self, layer, net, pts, width=0.25):
        segs = _segs_of(pts)
        if not segs:
            return True
        if getattr(self, "bounds", None) and layer in (1, 2):
            bx0, by0, bx1, by1 = self.bounds
            for x0, y0, x1, y1 in segs:
                if (x0 < bx0 + 0.3 or x1 > bx1 - 0.3 or
                        y0 < by0 + 0.3 or y1 > by1 - 0.3):
                    return False
        if not self._ok(layer, net, segs):
            return False
        pool = self._pool(layer)
        for s in segs:
            pool.append((net, s))
        track(self.doc, net, pts, layer=layer, width_mm=width)
        return True


class ChannelGen(object):
    """通道号生成器（越取越远），供底层布线重试"""

    def __init__(self, origin, step=0.7, direction=1):
        self.origin = origin
        self.step = step
        self.direction = direction
        self.n = 0

    def next(self):
        self.n += 1
        return self.origin + self.direction * self.step * self.n


class Carrier(object):
    """载板装配：焊盘登记 + 智能布线（顶层优先，自动落底层）"""

    def __init__(self, doc, corridors=None, margins=None, bounds=None):
        self.doc = doc
        self.router = Router(doc)
        self.unrouted = []
        self.bot_y = None
        self.corridors = corridors or ()
        self.margins = margins or (None, None)
        self.bounds = bounds or None  # (bx0, by0, bx1, by1) 越界拒布

    def set_bounds(self, bx0, by0, bx1, by1):
        self.bounds = (bx0, by0, bx1, by1)

    def hole(self, x, y, dia_mm=3.2):
        add_hole_block(self, x, y, dia_mm)

    def pad(self, net, num, x, y, hole_mm=1.0, dia_mm=1.8, smd=False,
            shape="ELLIPSE"):
        pad(self.doc, net, num, x, y, hole_mm, dia_mm, shape, smd)
        self.router.add_pad(net, x, y, r=dia_mm / 2.0)

    def socket(self, nets, x, y, pitch=2.54, tag="J",
               hole_mm=1.0, dia_mm=1.8):
        """插接件排座（nets 一维；y 向下生长；返回 [(net,x,y,num)]"""
        out = []
        for i, net in enumerate(nets):
            px, py = x, y + i * pitch
            num = "%s%d" % (tag, i + 1)
            if net is not None:
                self.pad(net, num, px, py, hole_mm, dia_mm)
            out.append((net, px, py, num))
        return out

    def route(self, net, src, dst, must=True, label=None):
        """src/dst=(x,y)：顶层 L/Z 候选 -> 底层双过孔通道
        端点上的焊盘（如模块裸 GPIO 名脚座）自动改挂本网络名——
        它们电气上是同一节点，否则冲突检测会把自家端点误判为异网
        """
        for ex, ey in (src, dst):
            for idx, (pnet, px, py, pr) in enumerate(self.router.pads):
                if (abs(px - ex) < 0.2 and abs(py - ey) < 0.2 and
                        pnet != net):
                    self.router.pads[idx] = (net, px, py, pr)
        sx, sy = src
        dx, dy = dst
        if self.router.commit(1, net, [sx, sy, dx, sy, dx, dy]):
            return True
        if self.router.commit(1, net, [sx, sy, sx, dy, dx, dy]):
            return True
        for off in (1.0, -1.0, 1.6, -1.6, 2.2, -2.2, 2.8, -2.8):
            xm = sx + (dx - sx) / 2.0 + off
            if self.router.commit(1, net, [sx, sy, xm, sy, xm, dy, dx, dy]):
                return True
        # 顶层边距走廊 Z 形（板左右外缘通常干净）
        if getattr(self, "corridors", None):
            for xm in self.corridors:
                if self.router.commit(1, net, [sx, sy, xm, sy, xm, dy,
                                               dx, dy]):
                    return True
        toward = 1.0 if dx >= sx else -1.0
        # 垂直下潜：从焊盘正下方 1.3mm 过孔（密排行的唯一逃逸），
        # 底层再 L/Z 到目标
        for drop in (1.3, -1.3, 2.0, -2.0, 2.7, -2.7, 3.4, -3.4):
            yv = sy + drop
            t0 = [sx, sy, sx, yv]
            if not self.router.dry_ok(1, net, t0):
                continue
            for xo in (1.5, 1.0, 2.1, 2.8):
                xv1 = sx + toward * xo
                b0 = [sx, yv, xv1, yv, xv1, dy, dx, dy]
                if self.router.dry_ok(2, net, b0):
                    self.router.commit(1, net, t0)
                    self.router.commit(2, net, b0)
                    via(self.doc, net, sx, yv)
                    via(self.doc, net, dx, dy)
                    return True
            # 变体 B：横穿逃逸行到目标外侧立柱，竖下再水平进盘
            # （终点行有异网墙时，如 I2C_SDA 立柱切过 I2C_SCL 终点行）
            for xc_off in (2.5, 3.5, 4.5):
                xc = dx + toward * xc_off
                b0 = [sx, yv, xc, yv, xc, dy, dx, dy]
                if self.router.dry_ok(2, net, b0):
                    self.router.commit(1, net, t0)
                    self.router.commit(2, net, b0)
                    via(self.doc, net, sx, yv)
                    via(self.doc, net, dx, dy)
                    return True
        # 底层直达 L：过孔后竖直到终点行，横到目标盘（过孔落盘上）
        # 全段先干检后提交——失败不留半截残线挡后续网络
        for xo in (1.5, 1.0, 2.1, 2.8):
            xv1 = sx + toward * xo
            t1 = [sx, sy, xv1, sy]
            b1 = [xv1, sy, xv1, dy, dx, dy]
            if (self.router.dry_ok(1, net, t1) and
                    self.router.dry_ok(2, net, b1)):
                self.router.commit(1, net, t1)
                self.router.commit(2, net, b1)
                via(self.doc, net, xv1, sy)
                via(self.doc, net, dx, dy)
                return True
        # 边距绕行：竖到板顶/板底清界横道 -> 横 -> 目标侧竖下 -> 终点行
        lanes = []
        for m in (self.margins[1], self.margins[0]):
            if m is not None:
                lanes += [m] + [m + 0.9 * k * d for k in (1, 2, 3, 4, 5, 6)
                                for d in (-1, 1)]
        for my in lanes:
            for moff in (3.0, 4.5, 2.0, 6.0):
                for soff in (2.1, -2.1, 4.5, -4.5, 8.5, -8.5, 12.0, -12.0):
                    xm1 = sx + soff
                    mx = dx + toward * moff  # 落在目标"来向"一侧
                    t1 = [sx, sy, xm1, sy]
                    b1 = [xm1, sy, xm1, my, mx, my, mx, dy, dx, dy]
                    if (self.router.dry_ok(1, net, t1) and
                            self.router.dry_ok(2, net, b1)):
                        self.router.commit(1, net, t1)
                        self.router.commit(2, net, b1)
                        via(self.doc, net, xm1, sy)
                        via(self.doc, net, dx, dy)
                        return True
        # 底层：src 过孔 -> 竖+横+竖 -> dst 过孔（y 通道 x x 偏移组合迭代）
        xoffs = (1.5, -1.5, 0.9, -0.9, 2.1, -2.1, 2.8, -2.8, 3.4, -3.4)
        if self.bot_y is None:
            self.bot_y = ChannelGen(min(sy, dy) - 1.2, 0.7, -1)
        for i in range(36):
            for xo in xoffs:
                xv1 = sx + (xo if (dx >= sx) == (xo > 0) else -xo)
                xv2 = dx - (xo if (dx >= sx) == (xo > 0) else -xo)
                ych = self.bot_y.next()
                pts_top1 = [sx, sy, xv1, sy]
                # 终点行水平段走底层，过孔直接落在目标焊盘上——
                # 避免与终点行上的异网顶层横线冲突（如按键/信号横排）
                pts_bot = [xv1, sy, xv1, ych, xv2, ych, xv2, dy, dx, dy]
                if (self.router.dry_ok(1, net, pts_top1) and
                        self.router.dry_ok(2, net, pts_bot)):
                    self.router.commit(1, net, pts_top1)
                    self.router.commit(2, net, pts_bot)
                    via(self.doc, net, xv1, sy)
                    via(self.doc, net, dx, dy)
                    return True
        # 最终兜底：暴力扫描"顶层短stub + 底层 双立柱 2 弯"路径
        # （y 通道 x 两侧立柱偏移组合穷举；仍失败才认输）
        if self.margins[0] is not None and self.margins[1] is not None:
            top, bot = sorted((self.margins[0], self.margins[1]))
            ylane = top
            while ylane <= bot:
                # 前导：水平 stub 或 垂直下潜（密排 FPC 行专用）
                prologs = [(None, sy)]
                for dv in (1.3, -1.3, 2.0, -2.0, 2.7, -2.7):
                    yv = sy + dv
                    if self.router.dry_ok(1, net, [sx, sy, sx, yv]):
                        prologs.append((dv, yv))
                for dv, yv0 in prologs:
                  for xm_off in (2.1, -2.1, 3.4, -3.4, 4.5, -4.5, 6.0, -6.0,
                                 8.5, -8.5, 12.0, -12.0, 13.5, -13.5,
                                 15.0, -15.0):
                    xm1 = sx + xm_off
                    if dv is None:
                        t1 = [sx, sy, xm1, sy]
                        yv0 = sy
                    else:
                        t1 = [sx, sy, sx, yv0, xm1, yv0]
                    if not self.router.dry_ok(1, net, t1):
                        continue
                    for xc_off in (2.5, 3.5, 4.5, -2.5, -3.5, -4.5):
                        xc = dx + xc_off * (1 if dx != sx else -1)
                        b1 = [xm1, yv0, xm1, ylane, xc, ylane, xc, dy,
                              dx, dy]
                        if self.router.dry_ok(2, net, b1):
                            self.router.commit(1, net, t1)
                            self.router.commit(2, net, b1)
                            via(self.doc, net, xm1, yv0)
                            via(self.doc, net, dx, dy)
                            return True
                ylane += 0.8
        if must:
            self.unrouted.append(label or net)
        return False

    def power(self, net, pins, rail_y=None):
        """星形电源连接（2026-10-04 修订：弃长轨，改最小生成树 + route）
        pins = 同网全部脚（模块电源脚 + 插座电源脚），
        Kruskal 最近邻生成树，每条树边走 route()（顶层 L/Z ->
        底层通道过孔），同网允许交叠，全部布通否则断言失败
        """
        pts = list(pins)
        edges = []
        for i in range(len(pts)):
            for j in range(i + 1, len(pts)):
                d2 = ((pts[i][0] - pts[j][0]) ** 2 +
                      (pts[i][1] - pts[j][1]) ** 2)
                edges.append((d2, i, j))
        edges.sort()
        parent = list(range(len(pts)))

        def find(k):
            while parent[k] != k:
                parent[k] = parent[parent[k]]
                k = parent[k]
            return k

        for d2, i, j in edges:
            ri, rj = find(i), find(j)
            if ri == rj:
                continue
            parent[ri] = rj
            ok = self.route(net, pts[i], pts[j], label="PWR:%s" % net)
            assert ok, "power tree %s conflict %r->%r" % (net, pts[i],
                                                          pts[j])
        for px, py in pins:
            via(self.doc, net, px, py)


# ---------------------------------------------------------------- 原理图

def sch_label(doc, name, x, y, rot=0, color="#0000FF"):
    tx = round(mm(x)) + 2
    ty = round(mm(y))
    doc.shapes.append("N~%d~%d~%d~%s~%s~%s~start~%d~%d~Times New Roman~~0"
                      % (round(mm(x)), round(mm(y)), rot, color, name,
                         doc.nid(), tx, ty))


def make_sch(title, blocks, path):
    doc = Doc("1", CANVAS_SCH)
    x = 10
    y = 20
    for btitle, pins in blocks:
        doc.shapes.append("W~%d %d %d %d~#666666~2~0~none~%s~0"
                          % (round(mm(x)), round(mm(y)), round(mm(x)),
                             round(mm(y + 2.54 * len(pins))), doc.nid()))
        for i, (pname, net) in enumerate(pins):
            py = y + i * 2.54
            doc.shapes.append("W~%d %d %d %d~#008800~1~0~none~%s~0"
                              % (round(mm(x)), round(mm(py)),
                                 round(mm(x + 7)), round(mm(py)), doc.nid()))
            sch_label(doc, net, x + 7.2, py)
            sch_label(doc, pname, x - 0.2, py, color="#AA0000")
        sch_label(doc, btitle, x - 2, y - 4, color="#000000")
        x += 30
    doc.dumps(path)


# ---------------------------------------------------------------- S3 载板

def build_s3(title, out_pcb, out_sch, is_n8=False):
    doc = Doc("3", CANVAS_PCB)
    P = 2.54
    j1 = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17",
          "18", "8", "3", "46", "9", "10", "11", "12", "13", "14",
          "5V", "GND"]
    j3 = ["GND", "43", "44", "1", "2", "42", "41", "40", "39", "38",
          "37", "36", "35", "0", "45", "48", "47", "21", "20", "19",
          "GND", "GND"]
    xj1, xj3 = 0.0, 22.86
    ytop = 0.0

    def p(col, i):
        return (xj1 if col == "L" else xj3, ytop + (i - 1) * P)

    c = Carrier(doc, margins=(-14.0, 67.5))

    bx0, by0, bx1, by1 = -17.0, -18.0, 41.0, 72.0
    outline(doc, bx0, by0, bx1, by1)
    c.set_bounds(bx0, by0, bx1, by1)
    for hx, hy in [(bx0 + 2.5, by0 + 2.5), (bx1 - 2.5, by0 + 2.5),
                   (bx0 + 2.5, by1 - 2.5), (bx1 - 2.5, by1 - 2.5)]:
        c.hole(hx, hy)

    for i, name in enumerate(j1):
        x, y = p("L", i + 1)
        c.pad(name, "J1_%d" % (i + 1), x, y)
    for i, name in enumerate(j3):
        x, y = p("R", i + 1)
        c.pad(name, "J3_%d" % (i + 1), x, y)
    silk_text(doc, "ESP32-S3-DevKitC-1 %s" % title, xj1, by0 + 5.5, 1.6)
    silk_text(doc, "ANT->", xj1 - 6, ytop - 3, 1.1)

    # ---- 左列插座（x=-11，脚位与模块引脚行对齐 => 同行水平直达） ----
    # 电源脚放信号脚上方（行4/5）——3V3 从模块顶排就近接入，
    # 避开下方信号横排/竖墙区（2026-10-04 布线收敛修订）
    rtc = c.socket(["RTC_3V3", "RTC_GND", "I2C_SCL", "I2C_SDA"],
                   -11.0, p("L", 4)[1], tag="RTC")
    r2r = c.socket(["CVBS_B3", "CVBS_B2", "CVBS_B1", "CVBS_B0",
                    "CVBS_OUT", "R2R_GND"],
                   -11.0, p("L", 8)[1], tag="R2R")
    # 按键并进左列空行（SD 座下方 23-26 行）——走线纯底层直达 L
    k1 = c.socket(["BTN_USER1", "K1_GND"], -11.0, p("L", 23)[1], tag="K1")
    k2 = c.socket(["BTN_USER2", "K2_GND"], -11.0, p("L", 25)[1], tag="K2")
    sd = c.socket(["SD_CS", "SD_MISO", None, "SD_MOSI", "SD_CLK",
                   "SD_VCC", "SD_GND"],
                  -11.0, p("L", 16)[1], tag="SD")
    silk_text(doc, "RTC(插)", -16.0, p("L", 4)[1] - 1.2, 1.0)
    silk_text(doc, "R-2R 模块(插)", -17.0, p("L", 7)[1] - 1.2, 1.0)
    silk_text(doc, "KEY1/2", -17.0, p("L", 23)[1], 1.0)
    silk_text(doc, "microSD(插)", -17.0, p("L", 15)[1] - 1.2, 1.0)

    c.route("I2C_SCL", p("L", 5), (rtc[2][1], rtc[2][2]))
    c.route("I2C_SDA", p("L", 6), (rtc[3][1], rtc[3][2]))
    c.route("CVBS_B3", p("L", 8), (r2r[0][1], r2r[0][2]))
    c.route("CVBS_B2", p("L", 9), (r2r[1][1], r2r[1][2]))
    c.route("CVBS_B1", p("L", 10), (r2r[2][1], r2r[2][2]))
    c.route("CVBS_B0", p("R", 5), (r2r[3][1], r2r[3][2]),
            label="CVBS_B0(跨板)")
    c.route("SD_CS", p("L", 16), (sd[0][1], sd[0][2]))
    c.route("SD_MISO", p("L", 17), (sd[1][1], sd[1][2]))
    c.route("SD_MOSI", p("L", 19), (sd[3][1], sd[3][2]))
    c.route("SD_CLK", p("L", 20), (sd[4][1], sd[4][2]))
    c.route("BTN_USER1", p("L", 7), (k1[0][1], k1[0][2]),
            label="BTN1")
    c.route("BTN_USER2", p("L", 12), (k2[0][1], k2[0][2]),
            label="BTN2")

    # CVBS 输出 RCA（顶边）
    pad(doc, "CVBS_OUT", "AV1", 6.0, -13.0, 1.1, 2.5)
    c.router.add_pad("CVBS_OUT", 6.0, -13.0, 1.25)
    pad(doc, "GND", "AV2", 10.5, -13.0, 1.5, 3.0)
    c.router.add_pad("GND", 10.5, -13.0, 1.5)
    c.route("CVBS_OUT", (r2r[4][1], r2r[4][2]), (6.0, -13.0),
            label="CVBS_OUT")
    silk_text(doc, "CVBS OUT", 3.0, -16.5, 1.1)

    # ---- 右列单条排（x=+28.5，行严格对齐模块 => 全水平直达） ----
    # 行2-8 信号；行9-13 电源（VCC 相邻/GND 相邻，竖直互连不跨异网）
    right_strip = c.socket(
        [None, "UART_TX", "UART_RX", "MIC_ADC", None,
         "I2S_SDO", "I2S_SCK", "I2S_WS",
         "DAC_VCC", "MIC_VCC", "DAC_GND", "MIC_GND", "UR_GND"],
        28.5, p("R", 1)[1], tag="RT")
    silk_text(doc, "UART0(插)", 26.0, p("R", 1)[1] - 1.4, 1.0)
    silk_text(doc, "麦克风(插)", 26.0, p("R", 3)[1] - 1.4, 1.0)
    silk_text(doc, "I2S DAC(插)", 25.5, p("R", 5)[1] - 1.4, 1.0)
    silk_text(doc, "3V3 x2 | GND x3", 25.0, p("R", 9)[1] - 0.4, 0.9)
    c.route("UART_TX", p("R", 2), (right_strip[1][1], right_strip[1][2]))
    c.route("UART_RX", p("R", 3), (right_strip[2][1], right_strip[2][2]))
    c.route("MIC_ADC", p("R", 4), (right_strip[3][1], right_strip[3][2]))
    c.route("I2S_SDO", p("R", 6), (right_strip[5][1], right_strip[5][2]))
    c.route("I2S_SCK", p("R", 7), (right_strip[6][1], right_strip[6][2]))
    c.route("I2S_WS", p("R", 8), (right_strip[7][1], right_strip[7][2]))
    uart = right_strip
    mic = right_strip
    i2s = right_strip

    # ---- 电源 ----
    # right_strip 脚位：行9 DAC_VCC / 行10 MIC_VCC / 行11-13 GND
    def rt(n):
        return (right_strip[n][1], right_strip[n][2])
    c.power("GND", [p("L", 22), p("R", 1), p("R", 21), p("R", 22),
                    (10.5, -13.0), (-11.0, r2r[5][2]), (-11.0, rtc[1][2]),
                    (-11.0, k1[1][2]), (-11.0, k2[1][2]),
                    rt(10), rt(11), rt(12), (-11.0, sd[6][2])])
    c.power("3V3", [p("L", 1), p("L", 2), (-11.0, rtc[0][2]),
                    (-11.0, sd[5][2]), rt(8), rt(9)])

    assert not c.unrouted, "S3 unrouted: %r" % c.unrouted
    doc.dumps(out_pcb)

    make_sch("ESP32-S3 %s Carrier" % title, [
        ("DevKitC J1", [(j1[i], j1[i]) for i in range(22)]),
        ("DevKitC J3", [(j3[i], j3[i]) for i in range(22)]),
        ("R-2R 模块", [("B3/GPIO15", "CVBS_B3"), ("B2/GPIO16", "CVBS_B2"),
                       ("B1/GPIO17", "CVBS_B1"), ("B0/GPIO2", "CVBS_B0"),
                       ("OUT", "CVBS_OUT"), ("GND", "GND")]),
        ("I2S DAC", [("SDO/GPIO42", "I2S_SDO"), ("SCK/GPIO41", "I2S_SCK"),
                     ("WS/GPIO40", "I2S_WS"), ("VIN", "3V3"),
                     ("GND", "GND")]),
        ("RTC", [("SCL/GPIO5", "I2C_SCL"), ("SDA/GPIO6", "I2C_SDA"),
                 ("VIN", "3V3"), ("GND", "GND")]),
        ("MIC", [("OUT/GPIO1", "MIC_ADC"), ("VIN", "3V3"),
                 ("GND", "GND")]),
        ("SD", [("CS/GPIO10", "SD_CS"), ("MISO/GPIO11", "SD_MISO"),
                ("MOSI/GPIO13", "SD_MOSI"), ("CLK/GPIO14", "SD_CLK"),
                ("VIN", "3V3"), ("GND", "GND")]),
        ("KEY", [("K1/GPIO7", "BTN_USER1"), ("K2/GPIO8", "BTN_USER2")]),
        ("UART", [("TX/GPIO43", "UART_TX"), ("RX/GPIO44", "UART_RX")]),
    ], out_sch)


# ---------------------------------------------------------------- CAM 载板

def build_cam(out_pcb, out_sch):
    doc = Doc("3", CANVAS_PCB)
    P = 2.54
    p2 = ["5V", "GND", "12", "13", "15", "14", "2", "4"]
    p1 = ["3V3", "16", "0", "GND", "VCC", "3", "1", "GND"]
    xl, xr = 0.0, 22.86
    y0 = 0.0

    def p(col, i):
        return (xl if col == "L" else xr, y0 + (i - 1) * P)

    c = Carrier(doc, corridors=(-15.5, 38.0), margins=(-12.5, 37.5))
    c.set_bounds(-17.0, -16.0, 40.0, 42.0)
    bx0, by0, bx1, by1 = -17.0, -16.0, 40.0, 42.0
    outline(doc, bx0, by0, bx1, by1)
    for hx, hy in [(bx0 + 2.5, by0 + 2.5), (bx1 - 2.5, by0 + 2.5),
                   (bx0 + 2.5, by1 - 2.5), (bx1 - 2.5, by1 - 2.5)]:
        c.hole(hx, hy)

    for i, name in enumerate(p2):
        x, y = p("L", i + 1)
        c.pad(name, "P2_%d" % (i + 1), x, y)
    for i, name in enumerate(p1):
        x, y = p("R", i + 1)
        c.pad(name, "P1_%d" % (i + 1), x, y)
    silk_text(doc, "ESP32-CAM AI-Thinker 载板", xl, by0 + 5.0, 1.5)
    silk_text(doc, "ANT->", xl - 5, y0 - 2.8, 1.0)

    # 24P FPC 座（摄像头/AV 互斥；脚位见 HARDWARE.md 3.10）
    fx, fy, fp = -3.0, -9.5, 0.5
    fpc_map = {5: "I2C_SDA", 8: "I2C_SCL", 10: "GND", 11: "MIC_ADC",
               14: "3V3", 18: "CVBS_DAC", 22: "AUD_DAC"}
    for n in range(1, 25):
        net = fpc_map.get(n, "FPC_N%d" % n)
        # 0.5mm 节距：焊盘宽 0.3 / 长 1.2（宽了相邻盘会物理重叠）
        pad(doc, net, "F%d" % n, fx + (n - 1) * fp, fy, 1.2, 0.3,
            "RECT", smd=True)
        c.router.add_pad(net, fx + (n - 1) * fp, fy, 0.15)
    silk_text(doc, "CAM 24P FPC 0.5mm（摄像头/AV 互斥）", -9.0, fy - 2.6,
              1.0)

    # CVBS 滤波模块 + RCA
    cf = c.socket(["CVBS_IN", "CVBS_OUT", "CF_GND"], -10.0, -4.5, tag="CF")
    c.route("CVBS_DAC", (fx + 17 * fp, fy), (cf[0][1], cf[0][2]),
            label="CVBS_DAC(F18)")
    pad(doc, "CVBS_OUT", "AV1", 7.0, -12.0, 1.1, 2.5)
    c.router.add_pad("CVBS_OUT", 7.0, -12.0, 1.25)
    pad(doc, "GND", "AV2", 11.5, -12.0, 1.5, 3.0)
    c.router.add_pad("GND", 11.5, -12.0, 1.5)
    c.route("CVBS_OUT", (cf[1][1], cf[1][2]), (7.0, -12.0),
            label="CVBS_OUT")
    silk_text(doc, "CVBS 滤波(插 150R+75R)", -16.0, -7.0, 1.0)
    silk_text(doc, "CVBS OUT", 4.5, -15.4, 1.1)

    # 功放 / RTC / 麦克风 / UART / 电源
    # amp 下移避开 P1 排（横线须离模块盘列 ≥1.2mm）
    amp = c.socket(["AMP_IN", "AMP_VCC", "AMP_GND"], 29.0, 11.4, tag="AMP")
    rtc = c.socket(["RTC_SCL", "RTC_SDA", "RTC_VCC", "RTC_GND"],
                   -13.0, 8.0, tag="RTC")
    mic = c.socket(["MIC_ADC", "MIC_VCC", "MIC_GND"], -13.0, 18.0,
                   tag="MIC")
    ur = c.socket(["UART_TX", "UART_RX", "UR_GND", "UR_5V"], 29.0, 22.0,
                  tag="UR")
    pw = c.socket(["5V", "PW_GND"], -13.0, 32.0, tag="PW")
    silk_text(doc, "功放 PAM8403(插)", 26.5, 9.8, 1.0)
    silk_text(doc, "RTC(插)", -16.5, 6.6, 1.0)
    silk_text(doc, "麦克风(插)", -17.5, 16.6, 1.0)
    silk_text(doc, "UART0 烧录(插)", 25.5, 20.6, 1.0)
    silk_text(doc, "5V 入(插)", -16.5, 30.6, 1.0)

    c.route("AUD_DAC", (fx + 21 * fp, fy), (amp[0][1], amp[0][2]),
            label="AUD_DAC(F22)")
    c.route("I2C_SDA", (fx + 4 * fp, fy), (rtc[1][1], rtc[1][2]),
            label="I2C_SDA(F5)")
    c.route("I2C_SCL", (fx + 7 * fp, fy), (rtc[0][1], rtc[0][2]),
            label="I2C_SCL(F8)")
    c.route("MIC_ADC", (fx + 10 * fp, fy), (mic[0][1], mic[0][2]),
            label="MIC(F11)")
    c.route("UART_TX", p("R", 7), (ur[0][1], ur[0][2]))
    c.route("UART_RX", p("R", 6), (ur[1][1], ur[1][2]))
    c.route("5V", p("L", 1), (pw[0][1], pw[0][2]))

    # F10(DGND)/F14(DOVDD) 为摄像头模式供电——与 AV 模式互斥
    # （HARDWARE.md 3.10），不并入电源树
    c.power("GND", [p("L", 2), p("R", 4), p("R", 8),
                    (11.5, -12.0), (-10.0, cf[2][2]),
                    (29.0, amp[2][2]), (-13.0, rtc[3][2]),
                    (-13.0, mic[2][2]), (29.0, ur[2][2]),
                    (-13.0, pw[1][2])])
    c.power("3V3", [p("L", 8),
                    (29.0, amp[1][2]), (-13.0, rtc[2][2]),
                    (-13.0, mic[1][2])])

    assert not c.unrouted, "CAM unrouted: %r" % c.unrouted
    doc.dumps(out_pcb)

    make_sch("ESP32-CAM Carrier", [
        ("P2 左", [(p2[i], p2[i]) for i in range(8)]),
        ("P1 右", [(p1[i], p1[i]) for i in range(8)]),
        ("CAM FPC", [("F5/GPIO21", "I2C_SDA"), ("F8/GPIO22", "I2C_SCL"),
                     ("F10", "GND"), ("F11/GPIO34", "MIC_ADC"),
                     ("F14", "3V3"), ("F18/GPIO25", "CVBS_DAC"),
                     ("F22/GPIO26", "AUD_DAC")]),
        ("CVBS 滤波", [("IN", "CVBS_DAC"), ("OUT", "CVBS_OUT"),
                       ("GND", "GND")]),
        ("功放", [("IN", "AUD_DAC"), ("VIN", "3V3"), ("GND", "GND")]),
        ("RTC", [("SCL", "I2C_SCL"), ("SDA", "I2C_SDA"), ("VIN", "3V3"),
                 ("GND", "GND")]),
        ("MIC", [("OUT", "MIC_ADC"), ("VIN", "3V3"), ("GND", "GND")]),
        ("UART", [("TX/GPIO1", "UART_TX"), ("RX/GPIO3", "UART_RX")]),
        ("PWR", [("5V", "5V"), ("GND", "GND")]),
    ], out_sch)


# ---------------------------------------------------------------- C3 载板

def build_c3(out_pcb, out_sch):
    doc = Doc("3", CANVAS_PCB)
    P = 2.54
    right = ["GND", "IO0", "IO1", "IO12", "IO18", "IO19", "GND", "RX20",
             "TX21", "IO13", "NC", "RST", "3V3", "GND", "PWB", "5V"]
    left = ["GND", "3V3", "IO2", "IO3", "IO10", "IO6", "IO7", "IO11",
            "GND", "3V3", "IO5", "IO4", "IO8", "IO9", "5V", "GND"]
    xl, xr = 0.0, 21.0
    y0 = 0.0

    def p(col, i):
        return (xl if col == "L" else xr, y0 + (i - 1) * P)

    c = Carrier(doc, corridors=(-12.5, 33.5), margins=(-10.5, 52.0))
    c.set_bounds(-14.0, -14.0, 35.0, 57.0)
    bx0, by0, bx1, by1 = -14.0, -14.0, 35.0, 57.0
    outline(doc, bx0, by0, bx1, by1)
    for hx, hy in [(bx0 + 2.5, by0 + 2.5), (bx1 - 2.5, by0 + 2.5),
                   (bx0 + 2.5, by1 - 2.5), (bx1 - 2.5, by1 - 2.5)]:
        c.hole(hx, hy)

    for i, name in enumerate(left):
        x, y = p("L", i + 1)
        c.pad(name, "L%d" % (i + 1), x, y)
    for i, name in enumerate(right):
        x, y = p("R", i + 1)
        c.pad(name, "R%d" % (i + 1), x, y)
    silk_text(doc, "合宙 ESP32-C3 核心板载板（邮票孔焊公排后插入）",
              xl, by0 + 4.5, 1.3)
    silk_text(doc, "ANT->", xl - 4, y0 - 3, 1.0)

    # CVBS PDM（IO1=R3 -> 滤波模块 -> RCA）
    cf = c.socket(["PDM_OUT", "CVBS_OUT", "CF_GND"], -9.0, -8.5, tag="CF")
    c.route("IO1", p("R", 3), (cf[0][1], cf[0][2]), label="IO1->PDM")
    pad(doc, "CVBS_OUT", "AV1", 8.0, -10.5, 1.1, 2.5)
    c.router.add_pad("CVBS_OUT", 8.0, -10.5, 1.25)
    pad(doc, "GND", "AV2", 12.5, -10.5, 1.5, 3.0)
    c.router.add_pad("GND", 12.5, -10.5, 1.5)
    c.route("CVBS_OUT", (cf[1][1], cf[1][2]), (8.0, -10.5))
    silk_text(doc, "PDM 滤波(插 270R+47pF)", -13.5, -11.0, 1.0)
    silk_text(doc, "CVBS OUT", 5.5, -13.6, 1.1)

    # SD 左置行对齐（IO6=L6, IO7=L7, IO5=L11, IO4=L12 -> 同行水平直达）
    sd = c.socket(["SD_MOSI", "SD_CS", None, None, "SD_MISO", "SD_CLK",
                   "SD_VCC", "SD_GND"], -9.0, p("L", 6)[1], tag="SD")
    c.route("SD_MOSI", p("L", 6), (sd[0][1], sd[0][2]))
    c.route("SD_CS", p("L", 7), (sd[1][1], sd[1][2]))
    c.route("SD_MISO", p("L", 11), (sd[4][1], sd[4][2]))
    c.route("SD_CLK", p("L", 12), (sd[5][1], sd[5][2]))
    silk_text(doc, "microSD(插)", -13.0, p("L", 5)[1] - 1.2, 1.0)

    # UART（TX21=R9, RX20=R8；下移避开 SD 座）
    ur = c.socket(["UART_TX", "UART_RX", "UR_GND", "UR_5V"], 26.0,
                  23.5, tag="UR")
    c.route("UART_TX", p("R", 9), (ur[0][1], ur[0][2]))
    c.route("UART_RX", p("R", 8), (ur[1][1], ur[1][2]))
    silk_text(doc, "UART0(插)", 24.0, p("R", 7)[1] - 1.2, 1.0)

    # I2C/教学扩展（IO3=L4 SCL，IO0=R2 SDA，IO10=L5）
    ex = c.socket(["I2C_SCL", "I2C_SDA", "GPIO10", "EX_GND"], -9.0,
                  p("L", 14)[1], tag="EX")
    c.route("IO3", p("L", 4), (ex[0][1], ex[0][2]), label="IO3->SCL")
    c.route("IO0", p("R", 2), (ex[1][1], ex[1][2]), label="IO0->SDA")
    c.route("IO10", p("L", 5), (ex[2][1], ex[2][2]))
    silk_text(doc, "I2C+教学 GPIO10(插)", -13.5, p("L", 13)[1] - 1.2, 1.0)

    pw = c.socket(["5V", "PW_GND"], 26.0, 36.0, tag="PW")
    c.route("5V", p("R", 16), (pw[0][1], pw[0][2]))
    silk_text(doc, "5V 入(插)", 24.0, p("R", 14)[1] - 1.2, 1.0)

    c.power("GND", [p("L", 1), p("L", 9), p("L", 16), p("R", 1),
                    p("R", 7), p("R", 14), (-9.0, cf[2][2]),
                    (-9.0, sd[7][2]), (12.5, -10.5),
                    (26.0, ur[2][2]), (-9.0, ex[3][2]),
                    (26.0, pw[1][2])])
    c.power("3V3", [p("L", 2), p("L", 10), p("R", 13), (-9.0, sd[6][2])])

    assert not c.unrouted, "C3 unrouted: %r" % c.unrouted
    doc.dumps(out_pcb)

    make_sch("Luatos ESP32-C3 Carrier", [
        ("核心板左列", [(left[i], left[i]) for i in range(16)]),
        ("核心板右列", [(right[i], right[i]) for i in range(16)]),
        ("PDM 滤波", [("IN/GPIO1", "IO1"), ("OUT", "CVBS_OUT"),
                      ("GND", "GND")]),
        ("SD 模块", [("CLK/GPIO4", "IO4"), ("MISO/GPIO5", "IO5"),
                     ("MOSI/GPIO6", "IO6"), ("CS/GPIO7", "IO7"),
                     ("VIN", "3V3"), ("GND", "GND")]),
        ("UART", [("TX/GPIO21", "TX21"), ("RX/GPIO20", "RX20")]),
        ("I2C/教学", [("SCL/GPIO3", "IO3"), ("SDA/GPIO0", "IO0"),
                      ("GPIO10", "IO10")]),
        ("PWR", [("5V", "5V"), ("GND", "GND")]),
    ], out_sch)


# ---------------------------------------------------------------- Pico 载板

def build_pico(out_pcb, out_sch):
    doc = Doc("3", CANVAS_PCB)
    P = 2.54
    lft = ["GP0", "GP1", "GND", "GP2", "GP3", "GP4", "GP5", "GND", "GP6",
           "GP7", "GP8", "GP9", "GND", "GP10", "GP11", "GP12", "GP13",
           "GND", "GP14", "GP15"]
    rgt = ["VBUS", "VSYS", "GND", "3V3_EN", "3V3", "ADCREF", "GP28",
           "AGND", "GP27", "GP26", "RUN", "GP22", "GND", "GP21", "GP20",
           "GP19", "GP18", "GND", "GP17", "GP16"]
    xl, xr = 0.0, 17.78
    y0 = 0.0

    def p(col, i):
        return (xl if col == "L" else xr, y0 + (i - 1) * P)

    c = Carrier(doc, corridors=(-12.5, 30.0), margins=(-10.5, 59.5))
    c.set_bounds(-14.0, -14.0, 31.5, 64.0)
    bx0, by0, bx1, by1 = -14.0, -14.0, 31.5, 64.0
    outline(doc, bx0, by0, bx1, by1)
    for hx, hy in [(bx0 + 2.5, by0 + 2.5), (bx1 - 2.5, by0 + 2.5),
                   (bx0 + 2.5, by1 - 2.5), (bx1 - 2.5, by1 - 2.5)]:
        c.hole(hx, hy)

    for i, name in enumerate(lft):
        x, y = p("L", i + 1)
        c.pad(name, "L%d" % (i + 1), x, y)
    for i, name in enumerate(rgt):
        x, y = p("R", i + 1)
        c.pad(name, "R%d" % (i + 1), x, y)
    silk_text(doc, "Raspberry Pi Pico 载板", xl, by0 + 4.5, 1.4)
    silk_text(doc, "USB->", xl - 5, by1 - 7.0, 1.0)

    # CVBS R-2R（GP12/13/14/15 = L16/17/19/20；socket 行严格对齐）
    r2r = c.socket(["CVBS_B0", "CVBS_B1", None, "CVBS_B2", "CVBS_B3",
                    "CVBS_OUT", "R2R_GND"], -9.0, p("L", 16)[1], tag="R2R")
    c.route("GP12", p("L", 16), (r2r[0][1], r2r[0][2]))
    c.route("GP13", p("L", 17), (r2r[1][1], r2r[1][2]))
    c.route("GP14", p("L", 19), (r2r[3][1], r2r[3][2]))
    c.route("GP15", p("L", 20), (r2r[4][1], r2r[4][2]))
    pad(doc, "CVBS_OUT", "AV1", 5.5, -11.0, 1.1, 2.5)
    c.router.add_pad("CVBS_OUT", 5.5, -11.0, 1.25)
    pad(doc, "GND", "AV2", 10.0, -11.0, 1.5, 3.0)
    c.router.add_pad("GND", 10.0, -11.0, 1.5)
    c.route("CVBS_OUT", (r2r[5][1], r2r[5][2]), (5.5, -11.0))
    silk_text(doc, "R-2R 模块(插)", -13.0, p("L", 15)[1] - 1.2, 1.0)
    silk_text(doc, "CVBS OUT", 3.0, -14.1, 1.1)

    # SD（GP16=R20 CS, GP17=R19 MOSI, GP18=R17 CLK, GP19=R16 MISO）
    sd = c.socket(["SD_MISO", "SD_CLK", None, "SD_MOSI", "SD_CS",
                   "SD_VCC", "SD_GND"], 23.0, p("R", 16)[1], tag="SD")
    c.route("GP19", p("R", 16), (sd[0][1], sd[0][2]))
    c.route("GP18", p("R", 17), (sd[1][1], sd[1][2]))
    c.route("GP17", p("R", 19), (sd[3][1], sd[3][2]))
    c.route("GP16", p("R", 20), (sd[4][1], sd[4][2]))
    silk_text(doc, "microSD(插)", 21.0, p("R", 15)[1] - 1.2, 1.0)

    # ADC（GP26=R10, GP27=R9, GP28=R7）
    ax_ = c.socket(["GP26", "GP27", "GP28", "EX_GND"], -14.0,
                   p("L", 19)[1], tag="AX")
    c.route("GP26", p("R", 10), (ax_[0][1], ax_[0][2]))
    c.route("GP27", p("R", 9), (ax_[1][1], ax_[1][2]))
    c.route("GP28", p("R", 7), (ax_[2][1], ax_[2][2]))
    silk_text(doc, "ADC GP26-28(插)", -18.0, p("L", 18)[1] - 1.2, 1.0)

    # 教学 GPIO 排（行对齐左列）
    ex = c.socket(["GP2", "GP3", "GP4", "GP5", None, "GP6", "GP7",
                   "GP8", "GP9", None, "GP10", "GP11"],
                  -9.0, p("L", 4)[1], tag="EX")
    for row, idx in ((4, 0), (5, 1), (6, 2), (7, 3), (9, 5), (10, 6),
                     (11, 7), (12, 8), (14, 10), (15, 11)):
        c.route(lft[row - 1], p("L", row), (ex[idx][1], ex[idx][2]))
    silk_text(doc, "教学 GP2-GP11(插)", -13.5, p("L", 3)[1] - 1.2, 1.0)

    # UART 控制台（GP0/GP1 -> 右上座）
    ur = c.socket(["UART_TX", "UART_RX", "UR_GND", "UR_3V3"], 23.0,
                  p("R", 4)[1], tag="UR")
    c.route("GP0", p("L", 1), (ur[0][1], ur[0][2]), label="GP0->TX")
    c.route("GP1", p("L", 2), (ur[1][1], ur[1][2]), label="GP1->RX")
    silk_text(doc, "UART0(插)", 21.0, p("R", 3)[1] - 1.2, 1.0)

    c.power("GND", [p("L", 3), p("L", 8), p("L", 13), p("L", 18),
                    p("R", 3), p("R", 12), p("R", 18), (10.0, -11.0),
                    (-9.0, r2r[6][2]), (23.0, sd[6][2]),
                    (23.0, ur[2][2]), (-14.0, ax_[3][2])])
    c.power("3V3", [p("R", 5), (23.0, sd[5][2]), (23.0, ur[3][2])])

    assert not c.unrouted, "Pico unrouted: %r" % c.unrouted
    doc.dumps(out_pcb)

    make_sch("Raspberry Pi Pico Carrier", [
        ("Pico 左列", [(lft[i], lft[i]) for i in range(20)]),
        ("Pico 右列", [(rgt[i], rgt[i]) for i in range(20)]),
        ("R-2R 模块", [("B0/GP12", "GP12"), ("B1/GP13", "GP13"),
                       ("B2/GP14", "GP14"), ("B3/GP15", "GP15"),
                       ("OUT", "CVBS_OUT"), ("GND", "GND")]),
        ("SD 模块", [("MISO/GP19", "GP19"), ("CLK/GP18", "GP18"),
                     ("MOSI/GP17", "GP17"), ("CS/GP16", "GP16"),
                     ("VIN", "3V3"), ("GND", "GND")]),
        ("UART", [("TX/GP0", "GP0"), ("RX/GP1", "GP1")]),
        ("教学 GPIO", [(n, n) for n in
                       ["GP2", "GP3", "GP4", "GP5", "GP6", "GP7",
                        "GP8", "GP9", "GP10", "GP11"]]),
        ("ADC", [("GP26", "GP26"), ("GP27", "GP27"), ("GP28", "GP28")]),
    ], out_sch)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    boards = {
        "s3": lambda d: build_s3("S3", d + "/s3_carrier_pcb.json",
                                 d + "/s3_carrier_sch.json"),
        "s3n8": lambda d: build_s3("S3N8", d + "/s3n8_carrier_pcb.json",
                                   d + "/s3n8_carrier_sch.json",
                                   is_n8=True),
        "cam": lambda d: build_cam(d + "/cam_carrier_pcb.json",
                                   d + "/cam_carrier_sch.json"),
        "c3": lambda d: build_c3(d + "/c3_carrier_pcb.json",
                                 d + "/c3_carrier_sch.json"),
        "pico": lambda d: build_pico(d + "/pico_carrier_pcb.json",
                                     d + "/pico_carrier_sch.json"),
    }
    for name, fn in boards.items():
        d = os.path.join(here, name)
        os.makedirs(d, exist_ok=True)
        fn(d)
        print("[gen] %s OK" % name)
    print("[gen] all boards done")


if __name__ == "__main__":
    main()
