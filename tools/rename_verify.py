# ============================================================
# 门 2 的第二种替代：纯改名改动的等价性判据
# ------------------------------------------------------------
# 为什么需要它：当改动的全部内容是「把 m_x 改成 p.x」这类改名时，
#   函数体指纹（split_verify.py）**必然**报 MISMATCH，而「变了」这个信号
#   不含任何信息量 —— 你无法从指纹差异里区分「只改了名」和「顺手改了逻辑」。
#
# 做法：取 git ref 里的旧文件，套同一个改名映射，与工作区文件**逐字节**比。
#   全等 ⇒ 除映射命中的标识符外一个字符都没动（含注释、缩进、空行）。
#   覆盖面比函数指纹更广：函数外的代码（成员声明、初始化列表、字符串）也覆盖。
#
# 用法：
#   python tools/rename_verify.py --map .scratch/rename.txt --ref af7e505
#   python tools/rename_verify.py --map .scratch/rename.txt --ref af7e505 gui/src/App.h
#
# 映射文件格式（一行一条，三种分隔符都认，# 开头为注释）：
#   m_stateProbeLog      -> stateProbeLog
#   m_stateProbeLog	stateProbeLog
#   m_stateProbeLog stateProbeLog
#
# 判据：UNCHANGED（ref 与工作区本就相同，跳过）或 RENAME-EQ（改名后全等）为绿；
#   DIFF 判红（打印前 20 行 unified diff）。退出码 0 / 1。
#
# ★ 两条前置检查（不满足则「逐字节全等」这个证明不成立）：
#   1. 旧名不得出现在**字符串字面量**里 —— 字符串里的名字不会被映射，
#      会导致 ref 与工作区在该处必然不等，或者更糟：真的该改却没改。
#      本工具会扫出这类位置并默认判红，`--allow-literal` 可放行（确属巧合时）。
#   2. 映射必须**不重叠**（同一个旧名不得映射到两处；新名不得再作为旧名出现），
#      否则映射顺序会改变结果。本工具在加载时校验。
# ============================================================
import argparse
import difflib
import pathlib
import re
import subprocess
import sys


def load_map(path):
    m = {}
    for lineno, raw in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p for p in re.split(r"\s*->\s*|\t+|\s+", line) if p]
        if len(parts) != 2:
            sys.exit(f"{path}:{lineno}: 需要「旧名 新名」两列，实际 {len(parts)} 列: {raw!r}")
        old, new = parts
        if old in m:
            sys.exit(f"{path}:{lineno}: 旧名 {old} 重复定义")
        m[old] = new
    if not m:
        sys.exit(f"{path}: 映射为空")
    # 前置检查 2：新名不得再作为旧名出现（否则是链式改名，一次替换做不对）
    chained = sorted(set(m.values()) & set(m))
    if chained:
        sys.exit(f"映射存在链式改名（新名同时是旧名）：{', '.join(chained[:5])}")
    return m


def string_spans(text):
    """返回字符串/字符字面量在 text 中的 [start, end) 区间。

    必须跳过反斜杠转义：`"a\\"b"` 里的引号不是字面量结束。
    （曾用朴素引号计数判错位置，把正常代码报成「名字在字符串里」。）
    """
    spans = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c:
                    break
                j += 1
            spans.append((i, min(j + 1, n)))
            i = j + 1
        else:
            i += 1
    return spans


def literal_hits(text, names):
    """旧名出现在字符串字面量里的位置（行号 + 片段）。"""
    hits = []
    for start, end in string_spans(text):
        seg = text[start:end]
        for name in names:
            if re.search(r"\b" + re.escape(name) + r"\b", seg):
                line = text.count("\n", 0, start) + 1
                hits.append((line, name, seg[:60]))
    return hits


def ref_text(ref, path):
    r = subprocess.run(["git", "show", f"{ref}:{path.as_posix()}"],
                       capture_output=True)
    if r.returncode != 0:
        return None
    return r.stdout.decode("utf-8")


def main():
    ap = argparse.ArgumentParser(description="纯改名改动的逐字节等价性校验")
    ap.add_argument("--map", required=True, help="改名映射文件（旧名 新名 两列）")
    ap.add_argument("--ref", default="HEAD", help="比对基准 git ref，默认 HEAD")
    ap.add_argument("--allow-literal", action="store_true",
                    help="旧名出现在字符串字面量里时不判红（确属巧合时用）")
    ap.add_argument("paths", nargs="*", default=["gui/src"],
                    help="要检查的路径（文件或目录），默认 gui/src")
    args = ap.parse_args()

    rmap = load_map(args.map)
    pat = re.compile(r"\b(" + "|".join(
        re.escape(k) for k in sorted(rmap, key=len, reverse=True)) + r")\b")

    files = []
    for p in args.paths:
        p = pathlib.Path(p)
        if p.is_dir():
            files += sorted(p.rglob("*.cpp")) + sorted(p.rglob("*.h"))
        elif p.suffix in (".cpp", ".h"):
            files.append(p)

    print(f"映射 {len(rmap)} 条，基准 {args.ref}，候选 {len(files)} 个文件\n")

    lit_fail = 0
    changed = 0
    fail = 0
    for f in files:
        old = ref_text(args.ref, f)
        if old is None:
            print(f"  {f.name:26s} MISSING(ref 里没有)  !!")
            fail += 1
            continue

        hits = literal_hits(old, rmap)
        if hits:
            lit_fail += len(hits)
            for line, name, seg in hits[:5]:
                print(f"  {f.name}:{line}  旧名 {name} 在字符串字面量里: {seg}")
            if not args.allow_literal:
                fail += 1
                continue

        got_path = f
        if not got_path.exists():
            print(f"  {f.name:26s} MISSING(工作区已删)")
            fail += 1
            continue
        got = got_path.read_text(encoding="utf-8")
        if got == old:
            continue  # 本文件没被这次改名碰到

        changed += 1
        expect = pat.sub(lambda m: rmap[m.group(1)], old)
        if expect == got:
            print(f"  {f.name:26s} RENAME-EQ")
        else:
            fail += 1
            print(f"  {f.name:26s} DIFF !!")
            for line in list(difflib.unified_diff(
                    expect.splitlines(), got.splitlines(),
                    "expect(改名后)", "got(工作区)", lineterm=""))[:20]:
                print("    " + line)

    print()
    if lit_fail and not args.allow_literal:
        print(f"前置检查失败：{lit_fail} 处旧名落在字符串字面量里，逐字节证明不成立。")
    print(f"改动文件 {changed} 个；判定: {'PASS' if fail == 0 else f'FAIL ({fail} 个文件)'}")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
