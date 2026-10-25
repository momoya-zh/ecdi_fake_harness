# ============================================================
# 门 2：函数体指纹比对（纯搬运的硬证明）
# ------------------------------------------------------------
# 用途：拆分/搬运代码时证明「函数体逐字节没变」。比人眼 review 可靠，
#   也能证明搬运者没有顺手改注释、改缩进、改逻辑。
#
# 用法：
#   python tools/split_verify.py --save .scratch/split-baseline/fp-before.json
#   ...（搬完代码）...
#   python tools/split_verify.py --check .scratch/split-baseline/fp-before.json
#
# 判据（--check 的退出码）：
#   0 = 全部一致
#   1 = 有 MISMATCH / MISSING / 新增（新增只提示，不算失败——见下）
#
# ★ 设计取舍（写清楚，免得后来人以为是 bug）：
#   · 指纹 = 定义首行到配对右花括号的**原文**（去行尾空白、归一 CRLF），
#     **包含注释与缩进**。因为「只搬不改」要求注释也一字不动。
#   · 函数名集合允许**新增**（Stage 2 会把 BuildMainArea 拆成 6 个 builder），
#     新增只打印不判红；MISSING 与 MISMATCH 判红。
#   · 只认**列 0 起始**的定义（本仓风格如此：成员定义与匿名 ns 里的自由函数
#     都在列 0）。缩进的嵌套 lambda / 局部函数不参与比对。
#   · 不解析模板、不解析宏生成的函数——本仓没有。
# ============================================================
import argparse
import hashlib
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "gui" / "src"

# 列 0 起始、但不是函数定义的关键字（跳过）
NOT_FUNC = re.compile(
    r"^(namespace|struct|class|enum|union|template|using|typedef|"
    r"#|//|/\*|\*|}|\{|extern\s*\"C\")"
)


def strip_comments_and_strings(text):
    """把注释/字符串内容替换成空格，只为括号配对时不误判。
    返回与原文等长的字符串（保留下标对齐）。"""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif c == '"':
            out[i] = " "
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n:
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == '"':
                    out[i] = " "
                    i += 1
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
        elif c == "'":
            out[i] = " "
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n:
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] == "'":
                    out[i] = " "
                    i += 1
                    break
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def parse_functions(path):
    """返回 [(name, start_line_idx, end_line_idx, text)]，行号 0-based 闭区间。"""
    raw = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
    lines = raw.split("\n")
    masked_text = strip_comments_and_strings(raw)
    masked = masked_text.split("\n")

    funcs = []
    i = 0
    while i < len(lines):
        line = lines[i]
        mline = masked[i]
        if line[:1].isspace() or not line.strip() or NOT_FUNC.match(line):
            i += 1
            continue
        # 候选：列 0、含 '('、且不是宏/声明
        if "(" not in mline:
            i += 1
            continue
        # 累积签名，直到遇到 '{'（允许跨行）
        j = i
        sig_masked = mline
        while "{" not in sig_masked and j + 1 < len(lines) and (j - i) < 12:
            j += 1
            sig_masked += " " + masked[j]
        if "{" not in sig_masked:
            i += 1
            continue
        # 签名里出现 ';' 在 '{' 之前 ⇒ 是声明，不是定义
        if sig_masked.find(";") != -1 and sig_masked.find(";") < sig_masked.find("{"):
            i += 1
            continue
        # 名字：签名里第一个 '(' 之前最后一个标识符（含 :: 限定）
        head = sig_masked.split("(")[0]
        names = re.findall(r"[A-Za-z_][A-Za-z0-9_:]*", head)
        if not names:
            i += 1
            continue
        name = names[-1]
        if name in ("if", "for", "while", "switch", "return", "sizeof"):
            i += 1
            continue

        # 从 '{' 开始配对（在 masked 全文上做，字符级）
        # 定位 '{' 的绝对字符偏移
        base = sum(len(x) + 1 for x in masked[:i])
        brace = masked_text.find("{", base)
        if brace < 0:
            i += 1
            continue
        # find 的 start 参数已是绝对偏移 ⇒ brace 就是绝对下标，不能再加 base
        pos = brace
        depth = 0
        while pos < len(masked_text):
            ch = masked_text[pos]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    break
            pos += 1
        if depth != 0:
            i += 1
            continue
        end_line = masked_text[:pos].count("\n")
        funcs.append((name, i, end_line, "\n".join(lines[i:end_line + 1])))
        i = end_line + 1
    return funcs


def fingerprint(text):
    norm = "\n".join(l.rstrip() for l in text.split("\n"))
    return hashlib.sha256(norm.encode("utf-8")).hexdigest()[:16]


def scan(files=None):
    files = files or sorted(list(SRC.glob("*.cpp")) + list(SRC.glob("*.h")))
    # 同名重载（本仓只有 WorkspaceById 的 const/非 const 一对）：
    #   按同文件内的出现次序编号 name#1 / name#2，搬运时两者一起走、相对次序不变。
    seen = {}
    out = {}
    dup = []
    for f in files:
        for name, s, e, text in parse_functions(f):
            n = seen.get(name, 0) + 1
            seen[name] = n
            key = name if n == 1 else f"{name}#{n}"
            rec = {"file": f.name, "lines": e - s + 1, "sha": fingerprint(text)}
            if key in out and out[key]["sha"] != rec["sha"]:
                dup.append((key, out[key], rec))
            out.setdefault(key, rec)
    return out, dup


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--save")
    ap.add_argument("--check")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    cur, dup = scan()
    print(f"扫描 {len(list(SRC.glob('*.cpp')))} 个 .cpp + {len(list(SRC.glob('*.h')))} 个 .h"
          f" ⇒ {len(cur)} 个列-0 函数定义")

    if dup:
        print("\n!! 同名函数体不同（重载或 ODR 风险，人工确认）：")
        for name, a, b in dup:
            print(f"   {name}: {a['file']}({a['sha']}) vs {b['file']}({b['sha']})")

    if args.save:
        p = pathlib.Path(args.save)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(cur, indent=1, sort_keys=True), encoding="utf-8")
        print(f"\n已保存基线：{p}  ({len(cur)} 个函数)")
        return 0

    if args.check:
        before = json.loads(pathlib.Path(args.check).read_text(encoding="utf-8"))
        same = changed = missing = added = 0
        print(f"\n{'函数':44s} {'拆前':>6s} {'拆后':>6s}  判定")
        print("-" * 78)
        for name in sorted(before):
            b = before[name]
            if name not in cur:
                missing += 1
                print(f"{name:44s} {b['lines']:6d} {'-':>6s}  MISSING (拆前在 {b['file']})")
            elif cur[name]["sha"] != b["sha"]:
                changed += 1
                print(f"{name:44s} {b['lines']:6d} {cur[name]['lines']:6d}  MISMATCH"
                      f"  {b['file']} -> {cur[name]['file']}")
            else:
                same += 1
                if args.verbose:
                    print(f"{name:44s} {b['lines']:6d} {cur[name]['lines']:6d}  ok")
        for name in sorted(cur):
            if name not in before:
                added += 1
                print(f"{name:44s} {'-':>6s} {cur[name]['lines']:6d}  NEW"
                      f"  ({cur[name]['file']})")
        print("-" * 78)
        print(f"一致 {same} / 变了 {changed} / 丢失 {missing} / 新增 {added}")
        ok = (changed == 0 and missing == 0)
        print("RESULT:", "PASS" if ok else "FAIL")
        return 0 if ok else 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
