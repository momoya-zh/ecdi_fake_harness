# ============================================================
# 门 4：摘要 diff（拆分/重构的等价性判据）
# ------------------------------------------------------------
# 用途：同一条命令在「改动前 / 改动后」各跑一次 `--out <json>`，逐 key 比对。
#   纯搬运类重构必须做到：**除已知 volatile 键外，逐字节相同**。
#
# 用法：
#   python tools/summary_diff.py a.json b.json [--allow frameCount,paints]
#
# ★ volatile 键的依据（不是"跑不通就加进去"）：
#   frameCount / paints / maxPaintUs / avgPaintUs —— 随 caret 闪烁 timer 的相位浮动，
#     同一个二进制连跑三次数值可差 ±2~4（2026-10-25 实测）。它们衡量的是
#     "这段时间画了几帧"，与代码等价性无关。
#   ★ invalidations **不在** volatile 清单里：它是稳定锚（demo 恒 8 / cot 恒 20），
#     整个 P4 期间逐位不变，且曾抓出「多打两次 RequestRepaint」的真实缺陷
#     （P4.C 8→10）。若它漂移，说明重绘链路被改坏——这正是要抓的。
# ============================================================
import argparse
import json
import pathlib
import sys

VOLATILE = ("frameCount", "paints", "maxPaintUs", "avgPaintUs")


def walk(a, b, path, out, ignore):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            p = f"{path}.{k}" if path else k
            if not path and k in ignore:
                continue
            if k not in a:
                out.append((p, "<absent>", b[k]))
            elif k not in b:
                out.append((p, a[k], "<absent>"))
            else:
                walk(a[k], b[k], p, out, ignore)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append((f"{path}[len]", len(a), len(b)))
            return
        for i, (x, y) in enumerate(zip(a, b)):
            walk(x, y, f"{path}[{i}]", out, ignore)
    else:
        if a != b:
            out.append((path, a, b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--allow", default="", help="额外忽略的顶层键，逗号分隔")
    args = ap.parse_args()

    ignore = set(VOLATILE) | {s for s in args.allow.split(",") if s}
    a = json.loads(pathlib.Path(args.before).read_text(encoding="utf-8"))
    b = json.loads(pathlib.Path(args.after).read_text(encoding="utf-8"))

    diffs = []
    walk(a, b, "", diffs, ignore)
    name = pathlib.Path(args.after).name
    if not diffs:
        print(f"  {name:28s} IDENTICAL（忽略 {len(ignore)} 个 volatile 键）")
        return 0
    print(f"  {name:28s} {len(diffs)} 处不同")
    for p, x, y in diffs[:20]:
        print(f"      {p}: {x!r} -> {y!r}")
    if len(diffs) > 20:
        print(f"      ... 另有 {len(diffs) - 20} 处")
    return 1


if __name__ == "__main__":
    sys.exit(main())
