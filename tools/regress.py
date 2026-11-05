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
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

if sys.platform == "win32" and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

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

# ============================================================
# ★★ 2026-11-01（`TODO-06` 机制欠账）：可复核性三件套 = rc / 落盘 / 二进制指纹
# ------------------------------------------------------------
# 病灶（账本 v4.5 ①⑥ 记的就是这条）：本文件判「门绿」只看 `out.exists()`
#   （见 run_probe 与各节的 `if not out.exists()`），而 `subprocess.run(...)`
#   的返回值从 2026-10 起一直**没被赋值** ⇒ 子进程 rc 在采集环节就丢了。
#   后果有两头，方向相反、都很贵：
#     · 红门：`FAILED TO PRODUCE OUTPUT` 只说明「json 没落盘」，分不清
#       「崩在写盘前」和「根本没起来」；也正因为写盘在收尾，它**不能**证明
#       崩的时刻，账本里「退出时段错误」这类措辞就是这样滑进来的。
#     · 绿门：写盘后崩掉的门**照样绿**——判据看不见进程怎么死的。
#   修法：**只加读数，不改判据**（判据仍是 json 存在性 + 相分类，逐位等价）。
#     每次起子进程后落一行 RUNLOG：门名 | rc | json 有无。
#     每轮开头再落一次 exe 指纹（sha256 前 8 位 + size + mtime）——
#     没有它，两轮红相不同就永远无法排除「换了二进制」，那正是 TODO-06
#     立案时最缺的一环。
#   ★ rc 的口径：Windows 上进程被结构化异常终止时 rc 是 0xC0000005 这类值，
#     Python 拿到的是同一个 DWORD ⇒ 按 `0x` 大写十六进制打印，别写成负数。
# ============================================================
RUNLOG = []          # [(label, rc, json_state)] —— 供 §4 汇总，不参与 fail 计数


def rc_str(rc):
    """rc → 可读串。未采到返回 '?'，正常退出返回十进制 0。"""
    if rc is None:
        return "?"
    if rc < 0 or rc >= 0xC0000000:
        return f"0x{rc & 0xFFFFFFFF:08X}"
    return str(rc)


def exe_fingerprint():
    """(sha256 前 8 位, size, mtime ISO) —— 门的「被测对象」身份。"""
    try:
        h = hashlib.sha256()
        with open(EXE, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        st = EXE.stat()
        import datetime
        mt = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%dT%H:%M:%S")
        return h.hexdigest()[:8], st.st_size, mt
    except OSError as e:
        return f"?{e.errno}", 0, "?"


def record(label, rc, out_path):
    """落一行可复核性读数。out_path=None 表示该跑法本就没有 json 产物。"""
    state = "-" if out_path is None else ("yes" if out_path.exists() else "MISSING")
    RUNLOG.append((label, rc, state))


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
REALISTIC = "protocol/fixtures/realistic/realistic-session.jsonl"
SCALE = "protocol/fixtures/scale/scale-transcript-2000.jsonl"
COT = "protocol/fixtures/cot/cot-session.jsonl"
# ★★ 协议 v5（2026-11-04）：子代理正向夹具（两个 task 并行、各自带 tool_call）。
SUBAGENT = "protocol/fixtures/subagent/parallel-two-agents.jsonl"
# ★★ 协议 v5（2026-11-05）：ask 提案卡正向夹具（三个 ask 各带 detail）。
PROPOSAL = "protocol/fixtures/proposal/write-edit-run-asks.jsonl"
# ★★ 协议 v6（2026-11-05）：后台作业正向夹具（run_command background → job start/
#   output/end → job_output/job_kill 两条控制工具链）。
JOB = "protocol/fixtures/jobs/background-task.jsonl"
# ★★ 门控反面（2026-11-04）：发 agent 消息但 hello.caps **不含** agents
#   ⇒ 内容照旧渲染，代理卡**不得**出现（协议 §3.1）。
AGENT_CAP_MISSING = "protocol/fixtures/subagent/agent-cap-missing.jsonl"

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
    ("--table-probe", "tableProbe", DEMO, [], []),
    ("--copy-probe", "copyProbe", REALISTIC, [], []),
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
    # 会话持久化与恢复探针：L1~L4 全链判据在 RunPersistProbe 内（发现 / 恢复 / 只读 / 守卫 / 继续 / 负对照 / 清理）
    ("--persist-probe", "persistProbe", DEMO, ["--config", "@PERSIST@"], []),
    # ★★ 2026-10-30（浮层迁移）：浮层拓扑与 Light Dismiss。
    #   全部判据写在 RunOverlayProbe 内（closed / mount / modal-blank / rebuild /
    #   theme / tip / tip-palette / tip-through / popup / popup-rebuild /
    #   popup-dismiss 共 11 相），回放模式即可全绿。
    #   ★ 2026-11-04 订正两处：此前这里写「九相」（漏了 popup-rebuild ⇒ 实际是 10 相，
    #     注释早已与代码分叉），同批补进 `tip-palette`——它守的是 tooltip 的**配色**
    #     （此前本探针对 tooltip 只有结构断言，于是「深底浅字」退化成近白压近白
    #     (1.08:1)、字已溶解而 10 相全绿）。相位数**只写在这里**，别在别处再写一遍。
    #   无 designed-red：本探针守的是**本仓自己的挂载约定**（模态托管挂载、下拉与
    #   tooltip 走注册式浮层），全部可在回放跑法下观测——不依赖 live 写通道，
    #   也不依赖 protocol 4。
    #   ★ 它补的是一个真实存在的盲区：迁移前**没有任何探针读过 `GetOverlayRoot()`**，
    #     `--theme-probe` 的 toggle 相只问「面板还在不在」，而"两套同屏"时它全绿。
    ("--overlay-probe", "overlayProbe", DEMO, [], []),
    # ★★ 协议 v5（2026-11-04）：子代理缩进与生命周期。
    #   正向夹具里两个 `task` 各带一个 tool_call + assistant 块 ⇒ 4 个归属块，
    #   屏上必须正好 4 个缩进行（探针的 `matched` 就是这条元断言）。
    #   ★ 负对照必须**另跑一支**（无 agent 的夹具 ⇒ 缩进恒 0），否则「每行都缩 24」
    #     这种实现在正向上也是绿的 —— 见 `check_agent_probe_negative`。
    ("--agent-probe", "agentProbe", SUBAGENT, [], []),
    # ★★ 协议 v5（2026-11-05）：ask 提案卡（detail）。
    #   正向夹具三个 ask 各带 detail（write 新文件全 + 行 / edit 替换片段 /
    #   run_command 键值），探针的 `matched` = 模型侧重切行与建行期统计逐项相等。
    #   ★ 负对照另跑一支（无 detail 的 demo ⇒ detailRows==0），否则"每个 ask
    #     都画提案体"的实现正向上也绿 —— 见 `check_proposal_probe_negative`。
    ("--proposal-probe", "proposalProbe", PROPOSAL, [], []),
    # ★★ 协议 v6（2026-11-05）：后台作业卡片。
    #   正向：job start 把状态贴到发起它的 run_command 工具行（标题含 j1+状态、
    #   正文含命令与尾部输出）。负向对照另跑一支（demo 无 job ⇒ 不得出现「后台」）。
    ("--job-probe", "jobProbe", JOB, [], []),
]

DELPROV_CONFIG = OUT / "_reg-delprov-config.json"
SETTINGS_CONFIG = OUT / "_reg-settings-config.json"
PERSIST_CONFIG = OUT / "_reg-persist-config.json"

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
        elif a == "@PERSIST@":
            resolved.append(str(configs["persist"]))
        else:
            resolved.append(a)
    out = OUT / f"_reg{flag}.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--replay", str(ROOT / fixture), "--instant",
                            "--theme", "light", flag] + resolved +
                           ["--auto-close", "30000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        record(flag, None, out)
        return False, "TIMEOUT"
    record(flag, r.returncode, out)          # ★ 只看读数，判据仍是下面那行存在性
    if not out.exists():
        return False, f"FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}"
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
    另加一条身份判据 `protocolDeclared == 6`：否则说明连的不是真 core，
    全绿也是假的。（v6 于 2026-11-05 落地：后台作业；真 core 的 hello.protocol
    恒 6，能力集由 caps 表达——`jobs` 恒声明、`agents` 是否声明由 --agents 决定。）
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

    check("core identity", d.get("protocolDeclared") == 6,
          f"protocolDeclared={d.get('protocolDeclared')} "
          f"label={d.get('activeSessionLabel')!r}")
    return fail


def run_live(core, core_args, auto_close_ms, extra, out_name):
    """跑一次 live（真子进程）；返回摘要 dict 或 None。
    `core_args` 是假 core 的参数串（`--speed=N` 或 `--burst`）——注意它只能有
    一个 token：`--core-args` 的值按空格切分，带空格就变成两个参数。"""
    out = OUT / out_name
    out.unlink(missing_ok=True)
    r = subprocess.run([str(EXE), "--core", str(core), "--core-args", core_args,
                        "--auto-prompt", "hello", "--async-probe",
                        "--auto-close", str(auto_close_ms), "--out", str(out)] + extra,
                       cwd=ROOT, capture_output=True)
    record(f"live:{out_name}", r.returncode, out)
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
    # ★★ 2026-10-30：判据换代（旧判据是「burst 下应**恰好** 256 行」）。
    #   旧判据守的是「行数上限被顶到」，但那要求 256 行能在 `kBudgetUs=4000` 内跑完
    #   ⇒ 实际是在断言**机器速度**（单行成本 ≤ 15.6µs）。本机实测 195–252，
    #   于是这条门每轮随机红，而它红的时候什么缺陷也没说明。
    #   预算本来就是**双界**（行数 / 时间，谁先到谁生效）⇒ 正确的命题是
    #   「预算真的顶到了」，即**撞到任一界**：
    #     · 快机：撞行数上限 ⇒ `maxLinesPerCallback == 256`（`capStops ≥ 1`）
    #     · 慢机：撞时间预算 ⇒ `budgetStops ≥ 1`，行数自然 < 256
    #   两者都为 0 才说明 burst 不够大、判据在这一轮无区分力 —— 那时必须红。
    #   ★ 代价（诚实记账）：行数上限这道保险在慢机上**不被判据覆盖**。要覆盖它
    #     得让单行成本降到 ~15µs 以下，那是性能工作，不是这条判据的事。
    #   ★ 代价（诚实记账，两条）：
    #     ① 行数上限这道保险在慢机上**不被判据覆盖**（要覆盖得让单行成本降到
    #        ~15µs 以下，那是性能工作，不是这条判据的事）；
    #     ② 反过来，**删掉时间预算也不会让 C3/C3b 变红**——行数上限仍把回调钳在 256，
    #        而 `capStops` 会接手变成 ≥1。要覆盖它得加「单次回调耗时 ≤ 预算 + 单行成本」
    #        这类帧时判据（现有 `--cull-probe` 的帧时读数跨轮不可比，接不上）。
    #        这里只保证「预算的双界里至少有一个真的在工作」，不声称两个都被覆盖。
    cap_hit = ba["maxLinesPerCallback"] == 256 or ba["capStops"] >= 1
    budget_hit = ba["budgetStops"] >= 1
    check("C3b 预算被真的顶到（撞到行数上限或时间预算，任一双界）",
          cap_hit or budget_hit,
          f"burst={ba['maxLinesPerCallback']} capStops={ba['capStops']} "
          f"budgetStops={ba['budgetStops']}（无 burst 时 {pa['maxLinesPerCallback']}）")

    check("C5 节奏化流下单行送达中位数 ≤1ms",
          ka["latencySamples"] > 0 and ka["latencyP50Us"] <= 1000,
          f"p50={ka['latencyP50Us']}µs p99={ka['latencyP99Us']}µs "
          f"(同口径 legacy p50={la['latencyP50Us']}µs)")

    return fail


def check_agent_probe_negative():
    """`--agent-probe` 的负对照：无 `agent` 的夹具上缩进必须恒 0。

    ★ 为什么必须单独跑一支：正对照只证明「有 agent 时会缩进」，
      「每行都缩 24」这种实现同样能把它照亮。只有"没有 agent 时一行都不缩"
      才能把这两者分开（本仓的老纪律：正反对照各自成立才算判据成立）。
    """
    out = OUT / "_reg-agent-probe-negative.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--replay", str(ROOT / DEMO), "--instant",
                            "--theme", "light", "--agent-probe",
                            "--auto-close", "30000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        record("--agent-probe (negative)", None, out)
        return 1, "TIMEOUT"
    record("--agent-probe (negative)", r.returncode, out)
    if not out.exists():
        return 1, f"FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}"
    d = json.loads(out.read_text(encoding="utf-8"))
    rows = d.get("agentProbe") or []
    if not rows:
        return 1, "NO ROWS (key=agentProbe)"
    rec = rows[0]
    ok = (rec.get("indentRows") == 0 and rec.get("agents") == 0
          and rec.get("attributedBlocks") == 0 and rec.get("gutterRows") == 0
          and rec.get("matched") is True)
    detail = (f"indentRows={rec.get('indentRows')} agents={rec.get('agents')} "
              f"attributedBlocks={rec.get('attributedBlocks')} matched={rec.get('matched')}")
    return (0 if ok else 1), detail


def check_proposal_probe_negative():
    """`--proposal-probe` 的负对照：无 `detail` 的夹具上提案体必须恒缺。

    ★ 为什么必须单独跑一支：正对照只证明「有 detail 时会画提案体」，
      「每个 ask 都画一个提案体」的实现同样能把它照亮。只有"没有 detail 时
      一行提案都不画（回落问句）"才能把这两者分开——回落路径是 v4 行为
      逐位保留的承诺（正反对照各自成立才算判据成立）。
    ★ 判据读数与 agent-probe 负对照同形，但**不要求 matched**——探针的
      `matched` 含 `detailRows>=1`（正向臂专用），负对照看的是具体读数。
    """
    out = OUT / "_reg-proposal-probe-negative.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--replay", str(ROOT / DEMO), "--instant",
                            "--theme", "light", "--proposal-probe",
                            "--auto-close", "30000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        record("--proposal-probe (negative)", None, out)
        return 1, "TIMEOUT"
    record("--proposal-probe (negative)", r.returncode, out)
    if not out.exists():
        return 1, f"FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}"
    d = json.loads(out.read_text(encoding="utf-8"))
    rows = d.get("proposalProbe") or []
    if not rows:
        return 1, "NO ROWS (key=proposalProbe)"
    rec = rows[0]
    ok = (rec.get("askRows", 0) >= 1 and rec.get("detailRows") == 0
          and rec.get("diffRows") == 0 and rec.get("plusRows") == 0
          and rec.get("minusRows") == 0 and rec.get("kvRows") == 0
          and rec.get("modelDetailRows") == 0)
    detail = (f"askRows={rec.get('askRows')} detailRows={rec.get('detailRows')} "
              f"diffRows={rec.get('diffRows')} kvRows={rec.get('kvRows')}")
    return (0 if ok else 1), detail


def check_job_probe_negative():
    """`--job-probe` 的负对照：没有 job 消息的夹具上，工具行不得出现「后台」。

    ★ 为什么必须单独跑一支：正向只证明「有 job 时会贴卡片」，
      「每张 run_command 卡都预先贴后台/预先建卡」的实现同样能绿。
      只有"没有 job 消息时一行都不贴"才能把两者分开（回落路径 = v5 行为）。
    """
    out = OUT / "_reg-job-probe-negative.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--replay", str(ROOT / DEMO), "--instant",
                            "--theme", "light", "--job-probe",
                            "--auto-close", "30000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        record("--job-probe (negative)", None, out)
        return 1, "TIMEOUT"
    record("--job-probe (negative)", r.returncode, out)
    if not out.exists():
        return 1, f"FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}"
    d = json.loads(out.read_text(encoding="utf-8"))
    rows = d.get("jobProbe") or []
    if not rows:
        return 1, "NO ROWS (key=jobProbe)"
    rec = rows[0]
    ok = (rec.get("matched") is True and rec.get("modelJobs") == 0
          and rec.get("viewRows") == 0 and rec.get("staleRows") == 0)
    detail = (f"modelJobs={rec.get('modelJobs')} viewRows={rec.get('viewRows')} "
              f"staleRows={rec.get('staleRows')} matched={rec.get('matched')}")
    return (0 if ok else 1), detail


def check_agent_probe_cap_gate():
    """`--agent-probe` 的**门控**反面：core 发了 agent 消息却没声明 `caps.agents`。

    ★ 为什么必须有这一支（否则前两支都在骗自己）：正向夹具声明了 cap，
      负向夹具连 agent 消息都没有——**门控整个不实现**的实现两支都绿。
      只有"有 agent 消息、没 cap"这一支能把「有门控」与「没门控」分开：
        · 有门控 ⇒ 卡片不出现（cardRows=0, cardVisible=false），块照旧缩进渲染
        · 没门控 ⇒ 卡片出现 3 行，`matched` 必须变红
    ★ 判据在探针里（`cardOk` 按 `hasAgentsCap` 分两支），这里只看 `matched`。
    """
    out = OUT / "_reg-agent-probe-capgate.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--replay", str(ROOT / AGENT_CAP_MISSING), "--instant",
                            "--theme", "light", "--agent-probe",
                            "--auto-close", "30000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=180)
    except subprocess.TimeoutExpired:
        record("--agent-probe (cap gate)", None, out)
        return 1, "TIMEOUT"
    record("--agent-probe (cap gate)", r.returncode, out)
    if not out.exists():
        return 1, f"FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}"
    d = json.loads(out.read_text(encoding="utf-8"))
    rows = d.get("agentProbe") or []
    if not rows:
        return 1, "NO ROWS (key=agentProbe)"
    rec = rows[0]
    # 期望：块照旧缩进（indentRows == attributedBlocks == 2），但卡片不存在
    ok = (rec.get("matched") is True and rec.get("cardRows") == 0
          and rec.get("cardVisible") is False
          and rec.get("indentRows") == rec.get("attributedBlocks"))
    detail = (f"cardRows={rec.get('cardRows')} cardVisible={rec.get('cardVisible')} "
              f"indentRows={rec.get('indentRows')} attributedBlocks={rec.get('attributedBlocks')} "
              f"matched={rec.get('matched')}")
    return (0 if ok else 1), detail


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


def check_sidebar_geom():
    """侧栏**行宽**判据（2026-11-03）：读 `sidebarGeom.ok`。

    为什么必须在脚本侧再判一次：判据写在探针里（`RunSidebarProbe` ⑯），但它在摘要里
    是**独立键**，不在这条门读的 `sidebarProbe` 数组里 —— 不显式读，新键就会悬空，
    "行宽又脱钩"时门照样绿（`wsPlusProbe` 的同款教训，2026-11-02 栽过一次）。
    """
    out = OUT / "_reg--sidebar-probe.json"
    if not out.exists():
        print("  sidebarGeom         RED: --sidebar-probe 没有产物（它自己那条会先红）")
        return 1
    doc = json.loads(out.read_text(encoding="utf-8"))
    g = doc.get("sidebarGeom") or {}
    if not g.get("done"):
        print("  sidebarGeom         RED: 相没有跑到（done=false）")
        return 1
    ok = bool(g.get("ok"))
    detail = (f"side={g.get('sideWidth')} row={g.get('rowWidth')}/{g.get('expectWidth')} "
              f"hist={g.get('histRowWidth')} "
              f"wide({g.get('wideSideWidth')})={g.get('wideRowWidth')}/{g.get('wideExpectWidth')}")
    if ok:
        print(f"  sidebarGeom         OK      {detail}")
        return 0
    print(f"  sidebarGeom         RED     {detail}")
    print(f"                      {g.get('detail') or '(no detail)'}")
    return 1


def check_workspace_probe(core):
    """workspace 探针需要**活会话的写通道**：回放没有 writer ⇒ unavailable（那是失败，不是跳过）。"""
    out = OUT / "_reg--workspace-probe.json"
    out.unlink(missing_ok=True)
    try:
        r = subprocess.run([str(EXE), "--core", str(core), "--auto-prompt", "hello",
                            "--workspace-probe", "--auto-close", "100000", "--out", str(out)],
                           cwd=ROOT, capture_output=True, timeout=240)
    except subprocess.TimeoutExpired:
        record("--workspace-probe", None, out)
        print("  --workspace-probe    TIMEOUT")
        return 1
    record("--workspace-probe", r.returncode, out)
    if not out.exists():
        print(f"  --workspace-probe    FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}")
        return 1
    doc = json.loads(out.read_text(encoding="utf-8"))
    rows = doc.get("workspaceProbe") or []
    if not rows:
        print("  --workspace-probe    NO ROWS (key=workspaceProbe)")
        return 1
    green, designed_red, skipped, unexpected = classify(rows, [])
    # ★★ 2026-11-02：ws-plus 相（点工作区分组标题行的「＋」⇒ 新会话必须挂在**那一个**
    #   工作区下）。它不塞进 `workspaceProbe` 那 9 条里（那 9 条的计数断言是既有基线，
    #   塞进去会让历史读数失去可比性），而是单独成键 —— 所以这里必须**显式**读，
    #   否则判据写在探针里却没人判，等于又退回"只有人眼能发现"。
    plus = doc.get("wsPlusProbe") or {}
    plus_ok = bool(plus.get("ok"))
    ok = (not unexpected) and plus_ok
    if not plus_ok:
        status = "WS-PLUS-RED: " + str(plus.get("detail") or "(phase did not run)")
    elif unexpected:
        status = "UNEXPECTED-RED=" + str(unexpected)
    else:
        status = "OK"
    print(f"  {'--workspace-probe':18s} {len(green)}/{len(rows)} green  "
          f"designed-red={designed_red or '-'}  "
          f"ws-plus={'OK' if plus_ok else 'RED'}  {status}")
    return 0 if ok else 1


def main():
    if not EXE.exists():
        print(f"Error: GUI executable not found at {EXE}")
        print("Please build it first: cmake --build gui\\build --config Release")
        return 1

    OUT.mkdir(exist_ok=True)
    fail = 0

    # ★ 2026-11-01：被测对象的身份，打在最前面。跨轮比较红相前先看这一行——
    #   两轮 sha256 不同，红相差异就不能当「同一构建的抖动」来解读。
    _sha, _size, _mt = exe_fingerprint()
    print(f"BINARY: {EXE}  sha256:{_sha}  size={_size}  mtime={_mt}")

    print("== 1. 夹具结构基线 (6 fixtures) ==")
    for name, rel, exp in FIXTURES:
        out = OUT / f"_reg_{name}.json"
        out.unlink(missing_ok=True)
        r = subprocess.run([str(EXE), "--replay", str(ROOT / rel), "--instant",
                            "--theme", "light", "--auto-close", "40000", "--out", str(out)],
                           cwd=ROOT, capture_output=True)
        record(name, r.returncode, out)
        if not out.exists():
            print(f"  {name:20s} FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}")
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
        r = subprocess.run([str(EXE), "--replay", str(ROOT / "protocol/fixtures/demo-session.jsonl"),
                            "--instant", "--theme", "light", flag, "--auto-close", "20000",
                            "--out", str(out)], cwd=ROOT, capture_output=True)
        record(flag, r.returncode, out)
        if not out.exists():
            print(f"  {flag:18s} FAILED TO PRODUCE OUTPUT  rc={rc_str(r.returncode)}")
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

    print("\n== 1b. 侧栏行宽判据 (sidebarGeom) ==")
    fail += check_sidebar_geom()

    print("\n== 2a. 补齐探针 (8) ==")
    # 配置必须在跑批前落盘：delprov 要有 provider（否则自述 skipped-empty-config），
    # settings 要一个**隔离**路径（它的 cleanup 会删掉这一轮跑的配置文件）。
    write_probe_config(DELPROV_CONFIG,
                       [provider("probe-delprov.invalid",
                                 "https://probe-delprov.invalid/v1", "probe-model")])
    SETTINGS_CONFIG.unlink(missing_ok=True)
    PERSIST_CONFIG.unlink(missing_ok=True)
    configs = {"delprov": DELPROV_CONFIG, "settings": SETTINGS_CONFIG, "persist": PERSIST_CONFIG}
    for flag, key, fixture, extra, designed in EXTRA_PROBES:
        ok, detail = run_probe(flag, key, fixture, extra, designed, configs)
        fail += 0 if ok else 1
        print(f"  {flag:18s} {detail}")

    print("\n== 2b. 读数探针判据 (2) ==")
    print("  （探针只记读数、不写 matched ⇒ 判据在本文件里，不在探针里）")
    fail += check_readout_probes()
    n, detail = check_agent_probe_negative()
    fail += n
    print(f"  {'--agent-probe':18s} {detail}  (negative/DEMO：无 agent 就一行都不缩)")
    n, detail = check_agent_probe_cap_gate()
    fail += n
    print(f"  {'--agent-probe':18s} {detail}  (cap gate：没声明 agents ⇒ 卡片不得出现)")
    n, detail = check_proposal_probe_negative()
    fail += n
    print(f"  {'--proposal-probe':18s} {detail}  (negative/DEMO：无 detail 就一行提案都不画)")
    n, detail = check_job_probe_negative()
    fail += n
    print(f"  {'--job-probe':18s} {detail}  (negative/DEMO：无 job 就不得出现后台字样)")

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

    print("\n== 4. 可复核性读数（rc / 落盘；TODO-06 机制欠账的补齐）==")
    print("  （判据未改，仍是 json 存在性 + 相分类。本节只把 rc 摆出来：")
    print("    rc=0 且 json=yes ⇒ 真跑完；json=MISSING ⇒ 崩在写盘前；")
    print("    json=yes 但 rc≠0 ⇒ **写盘后崩**——旧判据对这一格全盲，门照样绿）")
    for label, rc, state in RUNLOG:
        # ★ 2026-11-01：RUNLOG 里存的是**原始 rc（int）**，打印时才转字符串——
        #   第一版写成 `rc={rc:>10s}`，第一次全量跑就在这里 ValueError 炸掉
        #   （前面所有门都跑完了，却因为最后一张表拿不到 RESULT 行）。
        #   ⇒ 台架补丁必须真跑一遍全量，光 py_compile 不够。
        print(f"  {label:26s} rc={rc_str(rc):>10s}  json={state}")

    print()
    print(f"RESULT: {'PASS' if fail == 0 else f'{fail} FAILURE(S)'}")
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
