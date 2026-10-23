#!/usr/bin/env python
# ============================================================
# 全量回归验证套件：6 夹具基线 + 13 探针 + 协议校验
# ------------------------------------------------------------
# 判据说明（本仓纪律）：
#   * 夹具基线只看 blocks/deltas/messages —— paints/frames 会随字体与实现变化，
#     收敛动作不改渲染，但也不该把它们当不变量钉死。
#   * 探针的 `negative` 相是设计上就该红的，单独列出而不是混进失败。
# ============================================================
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "gui" / "build" / "Release" / "harness_gui.exe"
OUT = ROOT / ".scratch"

FIXTURES = [
    ("demo", "protocol/fixtures/demo-session.jsonl", (8, 19, 43)),
    ("realistic", "protocol/fixtures/realistic/realistic-session.jsonl", (220, 717, 1213)),
    ("cot", "protocol/fixtures/cot/cot-session.jsonl", (44, 934, 1044)),
    ("scale", "protocol/fixtures/scale/scale-transcript-2000.jsonl", (2000, 4590, 8906)),
    ("hostile-seq-gap", "protocol/fixtures/hostile/seq-gap.jsonl", (1, 3, 13)),
    ("hostile-truncated", "protocol/fixtures/hostile/truncated-final-line.jsonl", (1, 1, 5)),
]

PROBES = [
    ("--sidebar-probe", "sidebarProbe"),
    ("--answer-probe", "answerProbe"),
    ("--theme-probe", "themeProbe"),
    ("--history-probe", "historyProbe"),
    ("--rebuild-probe", "rebuildProbe"),
    ("--session-probe", "sessionProbe"),
    ("--hit-probe", "hitProbe"),
    ("--state-probe", "stateProbe"),
    ("--menu-probe", "menuProbe"),
    ("--resize-probe", "resizeProbe"),
    ("--enter-probe", "enterProbe"),
    ("--hud-probe", "hudProbe"),
    ("--grow-probe", "growProbe"),
]


def main():
    if not EXE.exists():
        print(f"Error: GUI executable not found at {EXE}")
        print("Please build it first: cmake --build gui\\build --config Release")
        return 1

    OUT.mkdir(exist_ok=True)
    fail = 0

    print("== 1. 夹具结构基线 (6 fixtures) ==")
    for name, rel, exp in FIXTURES:
        out = OUT / f"_reg_{name}.json"
        out.unlink(missing_ok=True)
        subprocess.run([str(EXE), "--replay", str(ROOT / rel), "--instant",
                        "--theme", "light", "--auto-close", "40000", "--out", str(out)],
                       cwd=ROOT, capture_output=True)
        if not out.exists():
            print(f"  {name:20s} FAILED TO PRODUCE OUTPUT")
            fail += 1
            continue
        d = json.loads(out.read_text(encoding="utf-8"))
        got = (d["blocks"], d["deltas"], d["messages"])
        ok = (got == exp)
        fail += 0 if ok else 1
        print(f"  {name:20s} {got[0]}/{got[1]}/{got[2]:<12} exp {exp[0]}/{exp[1]}/{exp[2]:<12} {'OK' if ok else 'MISMATCH'}")

    print("\n== 2. 探针断言 (13 probes) ==")
    for flag, key in PROBES:
        out = OUT / f"_reg{flag}.json"
        out.unlink(missing_ok=True)
        subprocess.run([str(EXE), "--replay", str(ROOT / "protocol/fixtures/demo-session.jsonl"),
                        "--instant", "--theme", "light", flag, "--auto-close", "20000",
                        "--out", str(out)], cwd=ROOT, capture_output=True)
        if not out.exists():
            print(f"  {flag:18s} FAILED TO PRODUCE OUTPUT")
            fail += 1
            continue
        d = json.loads(out.read_text(encoding="utf-8"))
        rows = d.get(key) or []
        if not rows:
            print(f"  {flag:18s} NO ROWS (key={key})")
            fail += 1
            continue
        red = [r for r in rows if r.get("matched") is False]
        names = [r.get("phase") or r.get("scene") or r.get("state") or "?" for r in red]
        designed = [n for n in names if "negative" in n.lower()]
        unexpected = [n for n in names if "negative" not in n.lower()]
        fail += 1 if unexpected else 0
        tag = "OK" if not unexpected else "UNEXPECTED-RED"
        print(f"  {flag:18s} {len(rows)-len(red)}/{len(rows)} green  "
              f"designed-red={designed or '-'}  {tag}")

    print("\n== 3. 协议与格式验证 (protocol/validate.py) ==")
    r = subprocess.run([sys.executable, str(ROOT / "protocol" / "validate.py")],
                       cwd=ROOT, capture_output=True, text=True)
    print(f"  exit={r.returncode}")
    fail += 0 if r.returncode == 0 else 1

    print()
    print(f"RESULT: {'PASS' if fail == 0 else f'{fail} FAILURE(S)'}")
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
