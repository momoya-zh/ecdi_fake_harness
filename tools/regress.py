#!/usr/bin/env python
# ============================================================
# 全量回归验证套件：6 夹具基线 + 13 探针 + REQ-03 live A/B + 协议校验
# ------------------------------------------------------------
# 判据说明（本仓纪律）：
#   * 夹具基线只看 blocks/deltas/messages —— paints/frames 会随字体与实现变化，
#     收敛动作不改渲染，但也不该把它们当不变量钉死。
#   * 探针的 `negative` 相是设计上就该红的，单独列出而不是混进失败。
#   * 2b 节要真子进程（`core/bin/fake-core.exe`）：没有就 SKIP，**不是 PASS**
#     —— 跳过必须显式说出来，否则"没测"看起来和"绿了"一模一样。
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


def run_live(core, core_args, auto_close_ms, extra, out_name):
    """跑一次 live（真子进程）；返回摘要 dict 或 None。
    `core_args` 是假 core 的参数串（`--speed=N` 或 `--burst`）——注意它只能有
    一个 token：`--core-args` 的值按空格切分，带空格就变成两个参数。"""
    out = OUT / out_name
    out.unlink(missing_ok=True)
    subprocess.run([str(EXE), "--core", str(core), "--core-args", core_args,
                    "--auto-prompt", "hello", "--async-probe",
                    "--auto-close", str(auto_close_ms), "--out", str(out)] + extra,
                   cwd=ROOT, capture_output=True)
    if not out.exists():
        return None
    return json.loads(out.read_text(encoding="utf-8"))


def check_async_delivery(core):
    """REQ-03：`Application::PostToUi` 投递 vs 每 tick 8ms 轮询的受控对照。

    ★ 自变量只有一个：唤醒机制（`--legacy-poll` 与否）。同一个二进制、同一份
      core、同一组参数、同一套仪器（入队时间戳两侧都开）。
    ★ 三跑的分工：
        · 两跑在 `--speed=8`：流能在窗口内跑完 ⇒ `blocks/deltas/messages` 可以**直接比**
          （跑不完的两跑会在不同位置截断，那样比计数是假的）。
        · 第三跑 `--speed=1`：只为"送达有多快"。它跑不完不要紧——延迟与是否跑完无关。
    """
    fail = 0

    def check(label, ok, detail):
        nonlocal fail
        fail += 0 if ok else 1
        print(f"  {label:42s} {'OK' if ok else 'FAILED':6s}  {detail}")

    legacy = run_live(core, "--speed=8", 15000, ["--legacy-poll"], "_reg_async-legacy.json")
    pushed = run_live(core, "--speed=8", 15000, [], "_reg_async-async.json")
    paced = run_live(core, "--speed=1", 12000, [], "_reg_async-paced.json")
    # ★ 第四跑用 `--burst`：它把一轮攒成一次写出 ⇒ 队列真的厚到 400+ 行。
    #   没有这一跑，C3 的「单回调 ≤256」在 `--speed=8` 下**恒真而未被触碰**
    #   （实测 maxCB 只有 6~8），等于一条永不失败的判据。
    burst = run_live(core, "--burst", 8000, [], "_reg_async-burst.json")
    for name, d in (("legacy", legacy), ("async", pushed), ("paced", paced),
                    ("burst", burst)):
        if d is None:
            print(f"  {name:8s} FAILED TO PRODUCE OUTPUT")
            return fail + 1

    la = legacy["asyncProbe"]
    pa = pushed["asyncProbe"]
    ka = paced["asyncProbe"]

    lc = (legacy["blocks"], legacy["deltas"], legacy["messages"])
    pc = (pushed["blocks"], pushed["deltas"], pushed["messages"])
    check("C4 功能等价（流跑完，计数逐项相同）", lc == pc and lc[1] > 0,
          f"legacy={lc} async={pc}")

    check("C1a 对照世界确实在阻塞", la["avgBlockedUs"] >= 1000,
          f"avgBlockedUs={la['avgBlockedUs']}µs max={la['maxBlockedUs']}µs "
          f"({la['blockedTicks']}/{la['ticks']} ticks)")
    check("C1b 推的世界零阻塞", pa["avgBlockedUs"] == 0 and pa["blockedTicks"] == 0,
          f"avgBlockedUs={pa['avgBlockedUs']}µs blockedTicks={pa['blockedTicks']}")

    total = pa["linesViaWake"] + pa["linesViaTick"]
    share = (pa["linesViaWake"] / total) if total else 0.0
    check("C2 数据确实走唤醒通路（≥95%）", share >= 0.95,
          f"{pa['linesViaWake']}/{total} = {share:.1%}（tick 兜底 {pa['linesViaTick']} 行）")

    ba = burst["asyncProbe"]
    check("C3 单回调预算生效（≤256 行）", ba["maxLinesPerCallback"] <= 256,
          f"burst 下 maxLinesPerCallback={ba['maxLinesPerCallback']} "
          f"callbacks={ba['callbacks']} posts={ba['posts']}")

    bc = (burst["blocks"], burst["deltas"], burst["messages"])
    check("C4b burst 不改变内容（只改到达节奏）", bc == lc and bc[1] > 0,
          f"burst={bc} legacy={lc}")
    check("C3b 预算被真的顶到（burst 下应恰好 256）",
          ba["maxLinesPerCallback"] == 256 and pa["maxLinesPerCallback"] <= 256,
          f"burst={ba['maxLinesPerCallback']}（无 burst 时 {pa['maxLinesPerCallback']}）")

    check("C5 节奏化流下单行送达中位数 ≤1ms",
          ka["latencySamples"] > 0 and ka["latencyP50Us"] <= 1000,
          f"p50={ka['latencyP50Us']}µs p99={ka['latencyP99Us']}µs "
          f"(同口径 legacy p50={la['latencyP50Us']}µs)")

    return fail


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

    print("\n== 2b. REQ-03 异步投递 (live A/B) ==")
    core = ROOT / "core" / "bin" / "fake-core.exe"
    if not core.exists():
        print("  SKIP: core/bin/fake-core.exe 不存在"
              "（cd core && go build -o bin/fake-core.exe .）")
    else:
        fail += check_async_delivery(core)

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
