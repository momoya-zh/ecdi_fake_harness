#!/usr/bin/env python
# ============================================================
# 全量回归验证套件：6 夹具基线 + 24 个探针 + 1 个仪器声明 + REQ-03 live A/B
# + live 设置探针（真 core，2e）+ 协议校验
# ------------------------------------------------------------
# 判据说明（本仓纪律）：
#   * 夹具基线只看 blocks/deltas/messages —— paints/frames 会随字体与实现变化，
#     收敛动作不改渲染，但也不该把它们当不变量钉死。
#   * 探针的 `negative` 相是设计上就该红的，单独列出而不是混进失败。
#   * 2 节要真子进程（`core/bin/fake-core.exe`）：没有就 SKIP，**不是 PASS**
#     —— 跳过必须显式说出来，否则"没测"看起来和"绿了"一模一样。
#   * ★ 2026-10-26（P0 收口）：探针覆盖从 13/25 补到 25/25。三种"不绿"必须分开：
#       designed-red（结构上不可达，理由写进 EXTRA_PROBES 的注释）
#       skipped-*   （探针自述"这个跑法下不适用"）
#       UNEXPECTED  （其余一切 = 真回归）
#     `unavailable` 不算 skipped —— 前条件本该成立却没有，是失败。
# ============================================================
import json
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# 生成器决定产物落点：Ninja（单配置）= gui/build/ 根；Visual Studio（多配置）= gui/build/Release/。
# 两者都认，新布局优先——2026-10-27 构建器已迁 Ninja（同一 cl.exe，ABI 不变）。
_EXE_CANDIDATES = [
    ROOT / "gui" / "build" / "harness_gui.exe",
    ROOT / "gui" / "build" / "Release" / "harness_gui.exe",
]
EXE = next(p for p in _EXE_CANDIDATES if p.exists()) if any(
    p.exists() for p in _EXE_CANDIDATES) else _EXE_CANDIDATES[0]
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

DEMO = "protocol/fixtures/demo-session.jsonl"
SCALE = "protocol/fixtures/scale/scale-transcript-2000.jsonl"
COT = "protocol/fixtures/cot/cot-session.jsonl"

# ============================================================
# ★ 2026-10-26（P0 收口）：有实现、有判据、却从未进门的 11 个探针。
# ------------------------------------------------------------
# 分组不是随手分的，是按**跑法**分的——同一个探针在不同跑法下能守的相不一样：
#   A 类 EXTRA_PROBES   自带 matched 断言，配对夹具/开关就能全绿。
#   B 类 READOUT_PROBES 探针只记录读数，**判据必须写在这里**——
#                       否则"跑过了"和"判过了"分不清，这是本仓第 N 次栽的那类坑。
#   C 类 INSTRUMENTS    纯计数仪器，没有断言面。显式写出来并说明理由，
#                       而不是"不在门里"就等同于"漏了"（跳过必须说出来）。
# ============================================================

# (flag, 摘要 key, 夹具, 额外参数, designed-red 相名)
# ★ designed-red 只收「在该跑法下**结构上不可达**的相」，理由逐条写在注释里；
#   不收「跑起来才发现红」的相——那叫回归，不叫设计。
EXTRA_PROBES = [
    ("--empty-probe", "emptyProbe", DEMO, [], []),
    ("--key-probe", "keyProbe", DEMO, [], []),
    # 短夹具装不满视口 ⇒ jump 探针自述 skipped-short-fixture。scale 才有可滚动内容。
    ("--jump-probe", "jumpProbe", SCALE, [], []),
    # 不给这个开关，select 探针自述 skipped-no-selectable-row——那是 SKIP，不是 PASS。
    ("--select-probe", "selectProbe", DEMO, ["--selectable-rows"], []),
    # delprov 需要一份**有 provider 的**配置，否则自述 skipped-empty-config。
    ("--delprov-probe", "delProvProbe", DEMO, ["--config", "@DELPROV@"], []),
    # 四相在该跑法下不可达，逐条理由：
    #   first-run    要「无 --replay/--core 的无参启动 + 无配置文件」——与回放互斥；
    #   save         依赖 first-run 把面板打开（面板没开就没有"点保存"）；
    #   session-live 要探针自己拉起活会话——回放没有写通道；
    #   switch-ok    要 protocol 4 + caps["model"]，而 fake-core 恒为 protocol 3
    #                （P3.6 明文：回归基线不动），回放夹具也声明 protocol 3。
    # ⇒ 这四相只能在「无参启动 + 真实配置 + protocol 4 的 core」下验收；**回放
    #   跑法**里它们不可达，所以在这里登记成 designed-red。
    #   ★ 2026-10-27 起它们由 §2e（live 设置探针）真跑真断——登记项不再是"没人
    #     管"，而是"这个跑法管不了、另一个跑法管"。别看到 designed-red 就当已覆盖。
    # ★ 2026-10-27（§8.5.6）：env-key-ok / env-key-missing 两相**不在 designed-red 里**
    #   ——它们自己 SetSettingsVisible(true) 把面板打开，不依赖 first-run 分支，
    #   所以在回放跑法下真的能绿。它们是门，不是登记项（负对照实测：把
    #   ReadEnvProviderKey 打成恒 false ⇒ env-key-ok 红、env-key-missing 仍绿）。
    ("--settings-probe", "settingsProbe", DEMO, ["--config", "@SETTINGS@"],
     ["first-run", "save", "session-live", "switch-ok"]),
    # ctx-denominator 要求右栏上下文百分比不是 "—"，即需要真分母；
    # fake-core 的 hello.context 恒为 0（实测 contextLimit=0）⇒ 结构上不可达。
    ("--model-probe", "modelProbe", DEMO, [], ["ctx-denominator"]),
    # P4 Phase I：全部判据写在 RunPreviewProbe 内（regex 抽取 / IO 纪律 / 双闸 /
    # Esc）。工作区与样本文件由探针在 %TEMP% 自建，回放模式即可全绿；无 designed-red。
    ("--preview-probe", "previewProbe", DEMO, [], []),
    # REQ-14（ECDI Phase 37 verticalAlign）：单行 TextBox 垂直居中。回放即可全绿
    # （读回走公开 GetCaretClientGeometry，设置面板/模型浮窗用产品入口真开真关）；
    # 无 designed-red——翻转臂（Top 强制）就是剂量对照，没有结构性不可达的相。
    ("--valign-probe", "valignProbe", DEMO, [], []),
]

DELPROV_CONFIG = OUT / "_reg-delprov-config.json"
SETTINGS_CONFIG = OUT / "_reg-settings-config.json"

# C 类：跑得动、有读数，但没有断言面。写在这里是为了让"没有门"这件事本身可见。
INSTRUMENTS = [
    ("--cull-probe", "命令分类/执行帧时/测量成本读数；判据是 measureChars 与 chars/call，"
                     "两者都帧数相关（本仓纪律 3）⇒ 跨轮不可比，不能当门，只能同轮 A/B 比"),
]


def provider(id_, base_url, model, context=0, max_output=0):
    """一条 provider 配置。credentialTarget 只存**目标名引用**，永不存密钥值。"""
    return {"id": id_, "baseUrl": base_url, "model": model,
            "context": context, "maxOutput": max_output,
            "credentialTarget": f"ECDI.FakeHarness.v1.Provider.{id_}"}


def write_probe_config(path, providers):
    """写探针用的**隔离**配置。

    ★ 隔离是硬要求，不是整洁癖：settings 探针的 cleanup 相会删掉它这一轮跑的配置路径。
      没有 `--config` 时那个路径就是用户的真实 `%APPDATA%\\ecdi-fake-harness\\config.json`，
      跑一次跑批就删掉全部提供商配置（2026-10-26 修的缺陷）。main.cpp 现已拒绝无
      `--config` 的 settings 启动，这里的隔离是第二道。
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    cfg = {"version": 2,
           "defaultProviderId": providers[0]["id"],
           "providers": providers}
    path.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")


def classify(rows, designed):
    """一相列表 → (green, designed_red, skipped, unexpected_red)。

    ★ 三种「不绿」必须分开，否则门会同时犯两个相反的错：
      · `skipped-*` 是探针自述「这个跑法下不适用」⇒ SKIP，不是失败；
      · 落在 designed 里的相是结构上不可达 ⇒ 设计如此，不是回归；
      · 其余才是真回归。
    ★ `unavailable` **不算 SKIP**——它表示前条件本该成立却没有（例如 live 会话没建起来）。
      把它当跳过，等于让门在 core 起不来的时候保持绿。
    """
    green, designed_red, skipped, unexpected = [], [], [], []
    for r in rows:
        name = r.get("phase") or r.get("scene") or r.get("state") or "?"
        if r.get("matched") is not False:
            green.append(name)
        elif name.startswith("skipped-"):
            skipped.append(name)
        elif name in designed or "negative" in name.lower():
            designed_red.append(name)
        else:
            unexpected.append(name)
    return green, designed_red, skipped, unexpected


def run_probe(flag, key, fixture, extra, designed, configs):
    """跑一个探针并返回 (ok, 描述串)。extra 里的 @DELPROV@/@SETTINGS@ 换成真路径。"""
    resolved = []
    for a in extra:
        if a == "@DELPROV@":
            resolved.append(str(configs["delprov"]))
        elif a == "@SETTINGS@":
            resolved.append(str(configs["settings"]))
        else:
            resolved.append(a)
    out = OUT / f"_reg{flag}.json"
    out.unlink(missing_ok=True)
    try:
        subprocess.run([str(EXE), "--replay", str(ROOT / fixture), "--instant",
                        "--theme", "light", flag] + resolved +
                       ["--auto-close", "30000", "--out", str(out)],
                       cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        return False, "TIMEOUT"
    if not out.exists():
        return False, "FAILED TO PRODUCE OUTPUT"
    d = json.loads(out.read_text(encoding="utf-8"))
    rows = d.get(key) or []
    if not rows:
        return False, f"NO ROWS (key={key})"
    green, designed_red, skipped, unexpected = classify(rows, designed)
    detail = (f"{len(green)}/{len(rows)} green  "
              f"designed-red={designed_red or '-'}  skipped={skipped or '-'}")
    return (not unexpected), (detail if not unexpected else "UNEXPECTED-RED=" + str(unexpected))


CORE_REAL = ROOT / "core" / "bin" / "ecdi-core.exe"
LIVE_SETTINGS_CONFIG = OUT / "_reg-live-settings-config.json"

# 设置探针在 live 跑法下**必须**绿的相。前四个在回放跑法里是 designed-red
# （见 EXTRA_PROBES 的注释），switch-bad / env-check 两边都跑，一并钉住。
# ★ 2026-10-27（v4 第三步）：multi-add / multi-merge / multi-del 两跑法都跑——
#   它们判的是**配置文件内容**（同家第二个模型有没有把第一个冲掉、删模型会不会
#   连带删掉兄弟/整家），与有没有写通道无关，所以回放里也能真绿，不是登记项。
LIVE_SETTINGS_REQUIRED = ("first-run", "save", "session-live", "switch-ok",
                          "switch-bad", "env-check",
                          "multi-add", "multi-merge", "multi-del")


def rebuild_core():
    """重建 core/bin/ecdi-core.exe → (ok, detail)。

    ★ 为什么门必须自己构建：门测的是**这个二进制**。源码改了不重建，门就在旧
      代码上绿——那比不跑更糟，它给出的是"已验证"的错觉。
    ★ 没有 go 工具链时退化为「二进制不得比 core/*.go 旧」；过期即 FAILED，
      不降级成 SKIP（SKIP 会静默变成"没测"）。
    """
    go = shutil.which("go")
    if not go:
        if not CORE_REAL.exists():
            return False, "no go toolchain and core/bin/ecdi-core.exe absent"
        newest = max(p.stat().st_mtime for p in (ROOT / "core").rglob("*.go"))
        if CORE_REAL.stat().st_mtime < newest:
            return False, "no go toolchain and ecdi-core.exe older than core/*.go"
        return True, "reused existing binary (no go toolchain)"
    r = subprocess.run([go, "build", "-o", str(CORE_REAL), "./cmd/ecdi-core"],
                       cwd=ROOT / "core", capture_output=True, text=True)
    if r.returncode != 0:
        tail = (r.stderr or r.stdout or "").strip().splitlines()
        return False, "go build failed: " + (tail[0] if tail else "?")
    return True, "rebuilt"


def check_live_settings_probe():
    """2e：设置探针的 live 四相（first-run / save / session-live / switch-ok）。

    为什么单开一节：这四相要「无参启动 + 无配置文件 + protocol 4 的 core」，
    **回放跑法**里结构上不可达，所以一直登记成 designed-red。登记久了就等于
    没有——而 `switch-ok` 恰好是**唯一**能端到端验证 GUI→core `model` 消息回环
    的探针（2026-10-27 给 `model` 请求增补可选 `model` 字段后，它成了这条链路
    上唯一的门；在那之前没有任何门碰过这条链）。

    判据：必需相**全部出现且全绿**。缺相 = FAILED 而不是 SKIP——缺相说明跑法
    退化了（比如没走进首启分支），那正是最需要看见的失败。
    另加一条身份判据 `protocolDeclared == 4`：否则说明连的不是真 core，
    全绿也是假的。
    """
    fail = 0

    def check(label, ok, detail):
        nonlocal fail
        fail += 0 if ok else 1
        print(f"  {label:26s} {'OK' if ok else 'FAILED':6s}  {detail}")

    ok, detail = rebuild_core()
    check("core rebuild", ok, detail)
    if not ok:
        return fail

    # 配置文件必须**不存在** ⇒ 应用从首启分支进来（设置面板可见）。
    LIVE_SETTINGS_CONFIG.unlink(missing_ok=True)
    out = OUT / "_reg-live-settings.json"
    out.unlink(missing_ok=True)
    try:
        subprocess.run([str(EXE), "--settings-probe",
                        "--config", str(LIVE_SETTINGS_CONFIG),
                        "--theme", "light", "--auto-close", "40000", "--out", str(out)],
                       cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        check("--settings-probe (live)", False, "TIMEOUT")
        return fail
    if not out.exists():
        check("--settings-probe (live)", False, "FAILED TO PRODUCE OUTPUT")
        return fail

    try:
        d = json.loads(out.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        # 摘要坏了（实测：探针把**未转义的双引号**写进 firstDivergence，
        # 而 escS 不转义引号）——"门自己炸了"必须与"判据红了"分开报，
        # 否则 traceback 会盖掉结论，看的人只会以为脚本有 bug。
        check("--settings-probe (live)", False, f"summary unparsable: {e}")
        return fail
    rows = d.get("settingsProbe") or []
    if not rows:
        # 没有行 = 探针根本没跑（不是"全绿"）。静默当成通过是最坏的一种假绿。
        check("--settings-probe (live)", False, "NO ROWS (settingsProbe)")
        return fail
    # 同一相可能报多次（switch-ok 在「发起」与「确认」两处各报一次）⇒ 按相聚合：
    #   只要有一次红这一相就算红，不能拿"后面那次绿了"盖住前面的红。
    verdict = {}
    for r in rows:
        name = r.get("phase") or "?"
        if r.get("matched") is False:
            verdict[name] = r.get("firstDivergence") or "unmatched"
        else:
            verdict.setdefault(name, "")
    missing = [p for p in LIVE_SETTINGS_REQUIRED if p not in verdict]
    red = [(p, verdict[p]) for p in LIVE_SETTINGS_REQUIRED if verdict.get(p)]
    green_n = sum(1 for v in verdict.values() if not v)
    required_ok = len(LIVE_SETTINGS_REQUIRED) - len(missing) - len(red)
    detail = (f"{green_n}/{len(verdict)} green  "
              f"required={required_ok}/{len(LIVE_SETTINGS_REQUIRED)}")
    if missing:
        detail += "  MISSING=" + ",".join(missing)
    if red:
        detail += "  RED=" + "; ".join(f"{p}: {m}" for p, m in red)
    check("--settings-probe (live)", not missing and not red, detail)

    check("core identity", d.get("protocolDeclared") == 4,
          f"protocolDeclared={d.get('protocolDeclared')} "
          f"label={d.get('activeSessionLabel')!r}")
    return fail


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


def check_readout_probes():
    """B 类：探针只记录读数，**判据在这里写**。

    ★ 这两个探针此前"跑过但没进门"，等于读数存在、无人判读。探针本身不写 matched，
      所以判据只能落在门里——这也说明"探针有输出"和"有门"是两件事。
    """
    fail = 0

    def check(label, ok, detail):
        nonlocal fail
        fail += 0 if ok else 1
        print(f"  {label:44s} {'OK' if ok else 'FAILED':6s}  {detail}")

    # ① --reasoning-probe（协议 v2 §7）：只有 cot 夹具带 reasoning 块。
    out = OUT / "_reg--reasoning-probe.json"
    out.unlink(missing_ok=True)
    subprocess.run([str(EXE), "--replay", str(ROOT / COT), "--instant", "--theme", "light",
                    "--reasoning-probe", "--auto-close", "30000", "--out", str(out)],
                   cwd=ROOT, capture_output=True)
    d = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {}
    rows = d.get("reasoningProbe") or []
    check("R1 折叠三态齐全（default/展开/收回）", len(rows) == 3, f"rows={len(rows)}")
    if len(rows) == 3:
        d0, d1, d2 = rows
        check("R2 默认折叠：内容不可见",
              d0["expanded"] is False and d0["contentVisible"] is False,
              f"expanded={d0['expanded']} visible={d0['contentVisible']}")
        check("R3 点开：可见且面板变高",
              d1["expanded"] is True and d1["contentVisible"] is True
              and d1["panelH"] > d0["panelH"],
              f"h {d0['panelH']}->{d1['panelH']} visible={d1['contentVisible']}")
        check("R4 再点收回：回到不可见",
              d2["expanded"] is False and d2["contentVisible"] is False,
              f"expanded={d2['expanded']} visible={d2['contentVisible']}")
    title = d.get("reasoningStreamingTitle") or ""
    check("R5 流式态标题在**建行时刻**抓到（非事后终态）", bool(title), f"title={title!r}")

    # ② --order-probe（Step 0a）：到达序。
    # ★ 判据必须带一条"夹具区分力"断言：如果夹具的块数太少，自然序恰好等于字典序，
    #   那么"到达序正确"这条断言在字典序实现下也会绿——等于没测。
    out = OUT / "_reg--order-probe.json"
    out.unlink(missing_ok=True)
    subprocess.run([str(EXE), "--replay", str(ROOT / COT), "--instant", "--theme", "light",
                    "--order-probe", "--auto-close", "30000", "--out", str(out)],
                   cwd=ROOT, capture_output=True)
    order = []
    if out.exists():
        order = json.loads(out.read_text(encoding="utf-8")).get("blockOrder") or []
    if not order or not all(len(s) > 1 and s[1:].isdigit() for s in order):
        check("O0 blockOrder 可解析为 b<N>", False, f"sample={order[:3]}")
    else:
        natural = sorted(order, key=lambda s: int(s[1:]))
        lexical = sorted(order)
        check("O1 夹具让自然序与字典序可区分（否则下一条无区分力）",
              lexical != natural, f"n={len(order)} 字典序头={lexical[:4]}")
        check("O2 块到达序 == 自然序（不是字典序）",
              order == natural, f"头={order[:4]} 尾={order[-3:]}")

    return fail


def check_workspace_probe(core):
    """workspace 探针需要**活会话的写通道**：回放没有 writer ⇒ unavailable（那是失败，不是跳过）。"""
    out = OUT / "_reg--workspace-probe.json"
    out.unlink(missing_ok=True)
    try:
        subprocess.run([str(EXE), "--core", str(core), "--auto-prompt", "hello",
                        "--workspace-probe", "--auto-close", "100000", "--out", str(out)],
                       cwd=ROOT, capture_output=True, timeout=240)
    except subprocess.TimeoutExpired:
        print("  --workspace-probe    TIMEOUT")
        return 1
    if not out.exists():
        print("  --workspace-probe    FAILED TO PRODUCE OUTPUT")
        return 1
    rows = json.loads(out.read_text(encoding="utf-8")).get("workspaceProbe") or []
    if not rows:
        print("  --workspace-probe    NO ROWS (key=workspaceProbe)")
        return 1
    green, designed_red, skipped, unexpected = classify(rows, [])
    ok = not unexpected
    print(f"  {'--workspace-probe':18s} {len(green)}/{len(rows)} green  "
          f"designed-red={designed_red or '-'}  "
          f"{'OK' if ok else 'UNEXPECTED-RED=' + str(unexpected)}")
    return 0 if ok else 1


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

    print("\n== 2a. 补齐探针 (7) ==")
    # 配置必须在跑批前落盘：delprov 要有 provider（否则自述 skipped-empty-config），
    # settings 要一个**隔离**路径（它的 cleanup 会删掉这一轮跑的配置文件）。
    write_probe_config(DELPROV_CONFIG,
                       [provider("probe-delprov.invalid",
                                 "https://probe-delprov.invalid/v1", "probe-model")])
    SETTINGS_CONFIG.unlink(missing_ok=True)
    configs = {"delprov": DELPROV_CONFIG, "settings": SETTINGS_CONFIG}
    for flag, key, fixture, extra, designed in EXTRA_PROBES:
        ok, detail = run_probe(flag, key, fixture, extra, designed, configs)
        fail += 0 if ok else 1
        print(f"  {flag:18s} {detail}")

    print("\n== 2b. 读数探针判据 (2) ==")
    print("  （探针只记读数、不写 matched ⇒ 判据在本文件里，不在探针里）")
    fail += check_readout_probes()

    print("\n== 2c. REQ-03 异步投递 (live A/B) ==")
    core = ROOT / "core" / "bin" / "fake-core.exe"
    if not core.exists():
        print("  SKIP: core/bin/fake-core.exe 不存在"
              "（cd core && go build -o bin/fake-core.exe .）")
        print("  SKIP: --workspace-probe 同样跳过（它需要活会话的写通道）")
    else:
        fail += check_async_delivery(core)
        print("\n== 2d. live 探针 (workspace) ==")
        fail += check_workspace_probe(core)

    print("\n== 2e. live 设置探针 (真 core，覆盖 4 个 designed-red 相) ==")
    # 不放进上面的 else：它用的是 ecdi-core.exe，与 fake-core.exe 在不在无关。
    fail += check_live_settings_probe()

    print("\n== 2f. 不入断言门的仪器 (1) ==")
    for flag, why in INSTRUMENTS:
        print(f"  {flag:18s} INSTRUMENT (not a gate): {why}")

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
