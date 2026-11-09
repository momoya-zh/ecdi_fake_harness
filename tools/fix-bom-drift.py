#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按 HEAD 的 BOM 状态批量修复工作树编码漂移。

★ 为什么需要这个脚本：2026-11-08 一天之内，我自己的编辑把 5 个文件的 BOM 弄丢
  或加上（Transcript.h / Transcript.cpp / README.md / AppHud.cpp / AppStream.cpp /
  AppTranscript.cpp），每次都是靠 `tools/precommit-check.py` 事后发现再手工补。
  「靠记性维持的纪律」= 迟早再犯，所以把修复动作本身固化成一行命令。

★ 判据与 `tools/precommit-check.py` 完全同源：只认 `git HEAD` 里那种文件的
  BOM 有无，**不发明规则**。新增文件（HEAD 里没有）跳过并显式说明。

用法：
  python tools/fix-bom-drift.py            # 只报告
  python tools/fix-bom-drift.py --apply    # 真的修
"""
import subprocess
import sys

BOM = b"\xef\xbb\xbf"


def head_bytes(path):
    try:
        return subprocess.run(["git", "show", "HEAD:" + path],
                              capture_output=True, check=True).stdout
    except subprocess.CalledProcessError:
        return None


def changed_files():
    out = subprocess.run(["git", "diff", "--name-only", "HEAD"],
                         capture_output=True, text=True, check=True).stdout
    return [ln.strip() for ln in out.splitlines() if ln.strip()]


def main():
    apply = "--apply" in sys.argv
    fixed, skipped, ok = [], [], 0
    for path in changed_files():
        hb = head_bytes(path)
        if hb is None:
            skipped.append((path, "not in HEAD"))
            continue
        want_bom = hb.startswith(BOM)
        try:
            with open(path, "rb") as f:
                cur = f.read()
        except FileNotFoundError:
            skipped.append((path, "deleted"))
            continue
        has_bom = cur.startswith(BOM)
        if has_bom == want_bom:
            ok += 1
            continue
        fixed.append((path, want_bom))
        if apply:
            body = cur[len(BOM):] if has_bom else cur
            with open(path, "wb") as f:
                f.write((BOM if want_bom else b"") + body)
    for p, w in fixed:
        print(("FIXED " if apply else "DRIFT ") + p + (" (BOM added)" if w else " (BOM removed)"))
    for p, why in skipped:
        print("SKIP  " + p + " -- " + why)
    print(f"\nok={ok} drift={len(fixed)} skipped={len(skipped)}"
          + ("" if apply else "   (re-run with --apply to fix)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
