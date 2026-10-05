# -*- coding: utf-8 -*-
# pylint: disable=missing-docstring
"""
SPDX-FileCopyrightText: 2026 ESP32-S3 Retro Project
SPDX-License-Identifier: Apache-2.0

WHAT: .rpk 包管理器差分 + PBT 测试（Python 工业级 oracle）
WHY : C 实现自证不可信——用 Python 标准库（zlib.crc32 / tarfile /
      hashlib / 文件系统）作第三方裁判逐值比对
WHO : tests/host/run_all.sh 调用
WHERE: esp32-retro-ws/tests/host/python/test_rpkg_diff.py
WHEN : 2026-10-04 新增
HOW : 1) Hypothesis 生成随机包树 -> GNU tar 打 ustar -> rpk_tool 安装
         -> 落盘字节/CRC/数据库逐项比对 -> 卸载 -> 状态还原（蜕变）
      2) 损坏注入（翻转载荷字节）-> 安装必须失败且回滚
      3) 恶意包（路径穿越/坏包名/伪 manifest）-> 必须拒绝
      4) 双架构二进制差分：arch=all/xtensa/esp32s3 过，riscv 拒
"""

import os
import shutil
import struct
import subprocess
import sys
import zlib

from hypothesis import given, settings, strategies as st, HealthCheck

REPO = os.environ.get("REPO", "/home/user/esp32-retro-ws")
TOOL = os.environ.get("RPK_TOOL", "/tmp/retro_test/rpk_tool")
TOOL_S3 = os.environ.get("RPK_TOOL_S3", "/tmp/retro_test/rpk_tool_s3")
SB = os.environ.get("RPK_SB", "/tmp/retro_pbt")

SD = os.path.join(SB, "sd")       # PKG_INSTALL_PREFIX
DB = os.path.join(SB, "db")       # PKG_DB_ROOT
STAGE = os.path.join(SB, "stage")

FAILURES = []
CHECKS = [0]


def check(cond, msg):
    CHECKS[0] += 1
    if not cond:
        FAILURES.append(msg)
        print("FAIL:", msg)


def reset_sb():
    shutil.rmtree(SB, ignore_errors=True)
    os.makedirs(SD, exist_ok=True)
    os.makedirs(DB, exist_ok=True)


def tool(*args, binary=TOOL):
    r = subprocess.run([binary] + list(args), capture_output=True,
                       text=True, timeout=60)
    return r.returncode, r.stdout + r.stderr


# ---------------------------------------------------------------- packer

def build_pkg(name, version="1.0", arch="all", files=None,
              manifest=True, scripts=None, depends=None,
              extra_control=""):
    """files: dict rel_path -> bytes；GNU tar ustar 打包（同 make_package.sh）"""
    files = files or {}
    stage = os.path.join(STAGE, name)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "data"), exist_ok=True)

    ctl = "Package: %s\nVersion: %s\nArch: %s\n" % (name, version, arch)
    if depends:
        ctl += "Depends: %s\n" % depends
    ctl += "Description: pbt package\n" + extra_control
    with open(os.path.join(stage, "control"), "wb") as f:
        f.write(ctl.encode())

    for rel, data in files.items():
        p = os.path.join(stage, "data", rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)

    if manifest:
        lines = []
        for rel, data in sorted(files.items()):
            crc = zlib.crc32(data) & 0xFFFFFFFF
            lines.append("%08x %s/%s" % (crc, SD, rel))
        with open(os.path.join(stage, "manifest"), "wb") as f:
            f.write(("\n".join(lines) + ("\n" if lines else "")).encode())

    for sname, body in (scripts or {}).items():
        with open(os.path.join(stage, sname), "wb") as f:
            f.write(body.encode())

    out = os.path.join(SB, name + "-" + version + ".rpk")
    subprocess.run(
        "cd '%s' && find . -mindepth 1 \\( -type f -o -type d \\) -print | "
        "sed 's|^\\./||' | sort | "
        "tar --format=ustar --no-recursion -cf '%s' -T -" % (stage, out),
        shell=True, check=True)
    return out


def read_manifest_db(pkg):
    p = os.path.join(DB, "manifest", pkg)
    if not os.path.exists(p):
        return {}
    out = {}
    with open(p, "r", encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split(" ", 1)
            if len(parts) == 2:
                out[parts[1]] = parts[0]
    return out


# ---------------------------------------------------------------- props

NAME_ST = st.from_regex(r"[a-z][a-z0-9+-]{2,10}", fullmatch=True)
REL_ST = st.from_regex(r"[a-z]{1,6}(/[a-z]{1,6}){0,2}", fullmatch=True)


@st.composite
def pkg_tree(draw):
    nfiles = draw(st.integers(min_value=1, max_value=6))
    rels = set()
    while len(rels) < nfiles:
        rels.add(draw(REL_ST))
    # 文件树合法性：同一路径既是文件又是目录会让 staging 的
    # makedirs 抛 FileExistsError——真包不会这样（tar 有序），
    # 剔除与其它路径互为前缀的组合（Hypothesis 2026-10-04 捕获）
    rels = {
        r for r in rels
        if not any(o != r and (o.startswith(r + "/") or
                               r.startswith(o + "/")) for o in rels)
    }
    files = {}
    for rel in rels:
        size = draw(st.integers(min_value=0, max_value=900))
        data = bytes(draw(st.lists(st.integers(0, 255),
                                   min_size=size, max_size=size)))
        # 避免全零内容（区分"没写入"与"写入全零"）
        if size and set(data) == {0}:
            data = b"\x01" + data[1:]
        files[rel] = data
    return files


def prop_install_roundtrip(files):
    reset_sb()
    rpk = build_pkg("pbt", files=files)
    rc, out = tool("install", rpk)
    check(rc == 0, "install rc=%d out=%s tree=%r" % (rc, out, list(files)))

    # 差分：落盘字节 == 源字节（Python 直接读回比对）
    for rel, data in files.items():
        p = os.path.join(SD, rel)
        if not os.path.exists(p):
            check(False, "missing installed file %s" % rel)
            continue
        got = open(p, "rb").read()
        check(got == data, "content mismatch %s (%d vs %d bytes)"
              % (rel, len(got), len(data)))

    # 差分：设备算的 CRC == zlib.crc32（两条独立实现互证）
    mani = read_manifest_db("pbt")
    for rel, data in files.items():
        expect = "%08x" % (zlib.crc32(data) & 0xFFFFFFFF)
        got = mani.get(os.path.join(SD, rel))
        check(got == expect, "crc mismatch %s: device=%s zlib=%s"
              % (rel, got, expect))

    # 卸载 -> 文件全清（状态还原蜕变）
    rc, out = tool("remove", "pbt")
    check(rc == 0, "remove rc=%d" % rc)
    for rel in files:
        check(not os.path.exists(os.path.join(SD, rel)),
              "not cleaned after remove: %s" % rel)
    check(tool("isinstalled", "pbt")[0] == 0, "db not cleaned")

    # 蜕变：重装一次结果一致
    rc, _ = tool("install", rpk)
    check(rc == 0, "reinstall after remove failed")
    for rel, data in files.items():
        p = os.path.join(SD, rel)
        check(os.path.exists(p) and open(p, "rb").read() == data,
              "reinstall content differs: %s" % rel)
    tool("remove", "pbt")


@settings(max_examples=25, deadline=None,
          suppress_health_check=[HealthCheck.too_slow])
@given(files=pkg_tree())
def test_pbt_roundtrip(files):
    prop_install_roundtrip(files)


# ---------------------------------------------------------------- directed

def test_corrupt_payload():
    reset_sb()
    files = {"app/tool": b"PAYLOAD-" + bytes(range(256)),
             "etc/cfg": b"cfgdata"}
    rpk = build_pkg("corrupt", files=files)

    data = bytearray(open(rpk, "rb").read())
    idx = data.find(b"cfgdata")
    assert idx > 0
    data[idx + 2] ^= 0x55
    bad = rpk + ".bad"
    open(bad, "wb").write(bytes(data))

    rc, out = tool("install", bad)
    check(rc == 1, "corrupt package installed! rc=%d" % rc)
    check("CRC mismatch" in out or "校验失败" in out,
          "no CRC diagnostic: %s" % out)
    for rel in files:
        check(not os.path.exists(os.path.join(SD, rel)),
              "corrupt install left file: %s" % rel)
    check(tool("isinstalled", "corrupt")[0] == 0, "db polluted")


def test_no_manifest():
    reset_sb()
    files = {"x": b"abc", "y/z": b"0123456789"}
    rpk = build_pkg("nomani", files=files, manifest=False)
    rc, _ = tool("install", rpk)
    check(rc == 0, "no-manifest install rc=%d" % rc)
    mani = read_manifest_db("nomani")
    for rel, data in files.items():
        expect = "%08x" % (zlib.crc32(data) & 0xFFFFFFFF)
        check(mani.get(os.path.join(SD, rel)) == expect,
              "device crc != zlib for %s" % rel)
    tool("remove", "nomani")


def test_traversal_evil():
    reset_sb()
    # Python tarfile 构造恶意 ustar（data/../evil 条目）
    import tarfile, io
    evil = os.path.join(SB, "evil.rpk")
    with tarfile.open(evil, "w", format=tarfile.USTAR_FORMAT) as tf:
        ctl = b"Package: evil\nVersion: 1\nArch: all\n"
        ti = tarfile.TarInfo("control")
        ti.size = len(ctl)
        tf.addfile(ti, io.BytesIO(ctl))
        ti = tarfile.TarInfo("data/../evil")
        ti.size = 4
        tf.addfile(ti, io.BytesIO(b"boom"))
    rc, out = tool("install", evil)
    check(rc == 1, "traversal package installed!")
    check(not os.path.exists(os.path.join(SB, "evil")),
          "traversal file escaped sandbox")


def test_bad_names_and_arch():
    reset_sb()
    import tarfile, io
    pwn = os.path.join(SB, "pwn.rpk")
    with tarfile.open(pwn, "w", format=tarfile.USTAR_FORMAT) as tf:
        ctl = b"Package: ../../etc/pwn\nVersion: 1\n"
        ti = tarfile.TarInfo("control")
        ti.size = len(ctl)
        tf.addfile(ti, io.BytesIO(ctl))
    check(tool("install", pwn)[0] == 1, "bad package name accepted")

    # 架构差分：同包对 xtensa 目标 vs riscv 目标
    files = {"bin/x": b"\x7fELF"}
    rpk = build_pkg("archpkg", files=files, arch="riscv-esp32c3")
    rc, out = tool("install", rpk, binary=TOOL_S3)
    check(rc == 1, "riscv package accepted on xtensa target!")
    check("架构不符" in out or "arch mismatch" in out, "no arch diagnostic")

    rpk = build_pkg("archpkg2", files=files, arch="esp32s3")
    check(tool("install", rpk, binary=TOOL_S3)[0] == 0,
          "esp32s3 package rejected on xtensa-esp32s3")
    tool("remove", "archpkg2", binary=TOOL_S3)

    rpk = build_pkg("archpkg3", files=files, arch="xtensa")
    check(tool("install", rpk, binary=TOOL_S3)[0] == 0,
          "xtensa family package rejected")
    tool("remove", "archpkg3", binary=TOOL_S3)


def test_depends_chain():
    reset_sb()
    a = build_pkg("liba", files={"lib/a.so": b"AAA"})
    rc, _ = tool("install", a)
    check(rc == 0, "install liba")

    b = build_pkg("needb", files={"bin/b": b"BBB"}, depends="liba")
    rc, _ = tool("install", b)
    check(rc == 0, "depends satisfied install")

    c = build_pkg("needc", files={"bin/c": b"CCC"}, depends="libmissing")
    rc, out = tool("install", c)
    check(rc == 1, "missing dep accepted")
    check("缺少依赖" in out or "missing dependency" in out,
          "no dep diagnostic")


def test_scripts_execute():
    reset_sb()
    marker_pre = os.path.join(SB, "pre.marker")
    marker_post = os.path.join(SB, "post.marker")
    scripts = {
        "preinst": "echo x > %s\n" % marker_pre,
        "postinst": "echo x > %s\n" % marker_post,
    }
    rpk = build_pkg("scripty", files={"f": b"data"}, scripts=scripts)
    rc, _ = tool("install", rpk)
    check(rc == 0, "scripty install")
    check(os.path.exists(marker_pre), "preinst did not run")
    check(os.path.exists(marker_post), "postinst did not run")
    tool("remove", "scripty")


def test_double_install_rejected():
    reset_sb()
    rpk = build_pkg("twice", files={"f": b"d"})
    check(tool("install", rpk)[0] == 0, "first install")
    rc, out = tool("install", rpk)
    check(rc == 1, "double install allowed")
    check("already installed" in out or "已安装" in out, "no diag")


def main():
    tests = [
        test_pbt_roundtrip,
        test_corrupt_payload,
        test_no_manifest,
        test_traversal_evil,
        test_bad_names_and_arch,
        test_depends_chain,
        test_scripts_execute,
        test_double_install_rejected,
    ]
    for t in tests:
        print("==>", t.__name__)
        t()

    print("[%s] %d checks, %d failed -> %s" % (
        "test_rpkg_diff", CHECKS[0], len(FAILURES),
        "PASS" if not FAILURES else "FAIL"))
    for f in FAILURES:
        print("  -", f)
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
