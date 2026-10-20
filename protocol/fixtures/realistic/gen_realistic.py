#!/usr/bin/env python3
"""gen_realistic.py -- realistic coding-agent session fixture (requirement puller).

Unlike gen_scale.py (uniform blocks for scale stress), this fixture mimics what a
REAL agent session looks like, so the harness metrics expose capability gaps:

  * assistant text is Markdown-heavy (code fences, **bold**, `inline code`,
    bullet lists, a table) -- pulls the rich-text requirement;
  * tool_result bodies are long multi-line logs / diffs / file excerpts
    (30-120 lines, up to ~8 KB in one block) -- pulls row-height, wrapping
    and text-clipping requirements;
  * user turns mix short CJK asks with long multi-line requests;
  * single lines over 200 chars exist -- pulls horizontal overflow handling;
  * ts pacing is realistic (deltas 30-300 ms, tool results 0.4-6 s later)
    so `timed` replay also behaves like a real session; --pace compresses it.

Deterministic: same --blocks/--seed => identical bytes (same contract as
gen_scale.py).  The fixture is VALID (must satisfy every invariant).

Usage:
  python gen_realistic.py --blocks 220 --out realistic-session.jsonl
  python gen_realistic.py --blocks 220 --pace 0.05 --out fast.jsonl
"""

import argparse
import datetime
import json
import os
import random
import sys

BASE_TIME = datetime.datetime(2026, 10, 5, 9, 30, 0, tzinfo=datetime.timezone.utc)

USER_TURNS = [
    "帮我看看 Widget::Paint 现在的剔除判据为什么用严格大于零",
    "滚动的时候还是会闪一下，帮我查一下是什么原因",
    """我在整理渲染管线的资料，需要你核对三件事：
1. PushClip 在命令流里的相对顺序是否和绘制命令严格交错；
2. 视口剔除关闭时命令流是否与旧版逐位一致（T27-11 的口径）；
3. FreeType 测量链的 DPI 基准现在是从哪里注入的。
每一条都给我带行号的证据，不要凭印象回答。""",
    "把 docs/README.md 里 Phase 27 那一行改成收口态，另外检查有没有引用了不存在的文档",
    "跑一遍三链测试，把失败用例的断言消息原样贴给我",
    "这个报错是什么意思：C4819 该字符在当前代码页中无效？我们不是已经全仓 /utf-8 了吗",
]

# Markdown-heavy assistant paragraphs -- each entry is one logical paragraph,
# sometimes containing fenced code / lists / tables (rich-text pullers).
ASSISTANT_MD = [
    """先说结论：**判据必须是严格大于零**，不能用 >= 0。边缘贴合（宽度恰为 0）时
交集是退化的线段，画出来不可见，但会让 PushClip/PopClip 照样进命令流——
剔除就白做了。具体推理链是：

1. `Rect::Intersects(a, b)` 要求交集宽**和**高都严格 > 0；
2. 边缘贴合时交集宽 = 0 ⇒ 判 false ⇒ 整棵子树跳过；
3. 如果放宽到 >= 0，边缘贴合的子树会发出零面积 clip + 全部绘制命令，
   执行端全部被裁掉，构建端却一分钱没省。

`PaintContext::IsRectVisible` 的注释里冻结了这个契约（Q5），不要动。""",
    """我查了一下滚动闪烁的来源，两个嫌疑都验证过了：

- **嫌疑一**：滚动条 SyncBars 每帧重设滑块位置——不是，位置没变时 SetText 同值
  仍会请求重绘，但那只是 HUD 标签，不是转录区。
- **嫌疑二**：`SetContentOffset` 的副作用链是 `ApplyLayout → ClampOffset →
  SyncBars → Invalidate`，每次滚动步进都整客户区失效——**是它**。

缓解方向有两个：脏区合帧（#21 记账项）或者接受现状。前者是框架级改动，
建议先用量具量一下每帧实际代价再决定。""",
    """DPI 基准的注入点在 `Window::Run` 之后、首次 `PaintFrame` 之前：
`m_textMeasurer->Initialize(m_platformWindow->GetRenderContext())`，GDITextMeasurer
和 FreeTypeTextMeasurer 都从这里取窗口 DPI（D-8 闭合时的定案）。测量链和渲染链
同基准这件事由 Phase 26 批一保证，跨屏漂移是另一个问题（记在 #44 M-2 附近）。

| 链路 | 基准来源 | 备注 |
|---|---|---|
| 测量 | 窗口 DPI（Initialize 注入） | Phase 26 批一 |
| 渲染 | 窗口 DPI（GetDpiForWindow） | GDI/GL 一致 |""",
    """测试结果：**305 通过、2 失败、307 总数**。失败的两个都集中在布局用例：

```
[FAIL] Layout.StretchZeroChild.NotGivenMainAxisSize
  expected (80, 18) but got (0, 18)
[FAIL] Layout.Padding.HardInsetDegenerate
  expected content origin (8, 8) but got (0, 0)
```

第一个是已知坑：stretch=0 的子布局不分配主轴尺寸，必须显式 SetSize。
第二个我怀疑是 padding 的硬 inset 语义和实现有出入，明天过一遍详设 §2.4。""",
    """这个结论我不采纳——它没有被独立复现。当时的读数是从一份旧静态库上跑出来的
（头文件形态停在 2026-09-18，lib 构筑时间早于批一提交 7.7 小时），跑的二进制
**不含剔除**。跨仓库验证必须先核对被验证方的版本指纹，这个教训已经写进
Phase 27 收口记录（纪律条款 ⑥）。

正确的复现步骤我贴在下面，跑完之前不要再引用那份读数：

```bash
cmake --install build-msvc --config Debug --prefix third_party/ecdi
cmake --build gui/build --config Debug
./gui/build/Debug/harness_gui.exe --replay ... --cull-probe --scroll-sweep 400
```""",
]

TOOL_DEFS = [
    ("read_file", lambda r: {"path": r.choice([
        "ECDI/src/Widget/Widget.cpp", "ECDI/src/Render/PaintContext.cpp",
        "docs/roadmap-deferred.md", "ECDI/include/ECDI/Render/PaintContext.h"]),
        "offset": r.choice([1, 40, 120]), "limit": r.choice([60, 120, 240])}),
    ("grep_repo", lambda r: {"pattern": r.choice([
        "IsRectVisible", "m_cullStack", "PushClip", "SetContentOffset", "Invalidate"]),
        "glob": r.choice(["ECDI/src/**/*.cpp", "ECDI/include/**/*.h", "docs/*.md"]),
        "max_results": r.choice([20, 50])}),
    ("edit_file", lambda r: {"path": r.choice(["docs/README.md", "README.md"]),
                             "line": r.randint(40, 300),
                             "old": r.choice(["待评审", "⬜ 记账 = #44–#48", "v1 待评审"]),
                             "new": r.choice(["已收口", "✅ 已闭合", "v1.1 已实施"])}),
    ("bash", lambda r: {"command": r.choice([
        "cmake --build build-msvc --config Debug 2>&1 | tail -5",
        "python protocol/validate.py && echo GREEN",
        "git log --oneline -12 && git status --short",
        "ctest --test-dir build -R Layout --output-on-failure -j 8"])},
     ),
    ("todo_write", lambda r: {"items": [
        {"content": "核对剔除判据契约", "status": "completed"},
        {"content": "定位滚动闪烁来源", "status": "in_progress"},
        {"content": "跨屏 DPI 漂移取证", "status": "pending"}]}),
]

LOG_LINES = [
    "[ RUN      ] Clip.Culling.BoundaryTouch",
    "[       OK ] Clip.Culling.BoundaryTouch (0 ms)",
    "[ RUN      ] ScrollView.T27.ContentOffsetClamp",
    "  expected maxOffset 59350 but got 59348 (row gap rounding)",
    "[  FAILED  ] ScrollView.T27.ContentOffsetClamp (2 of 330)",
    "Info: measurement cache hit ratio 94.2% (1297/1378, dpi=120)",
    "Warning: FreeType face cache miss for size 14.0 -> loading simhei.ttf",
    "Warning: GDI font 'Microsoft YaHei UI' substituted for 'Segoe UI'",
    "Trace: PaintFrame build 0.058ms execute 6.9ms commands 168 (gdi)",
    "Trace: PaintFrame build 0.061ms execute 1.2ms commands 168 (gl)",
    "  at ECDI/src/Widget/ScrollView.cpp:128 SetContentOffset",
    "  at ECDI/src/Window/Window.cpp:139 PaintFrame initial clip seed",
    "- 旧实现：m_offsetY = max(0, min(offsetY, m_contentH - viewportH));",
    "+ 新实现：ClampOffset() 沿用 SetContentExtent 的原子顺序链；",
    "diff --git a/ECDI/src/Widget/ScrollView.cpp b/ECDI/src/Widget/ScrollView.cpp",
    "@@ -113,7 +113,9 @@ void ScrollView::SetContentOffset(int x, int y) {",
    "+++ b/docs/roadmap-deferred.md",
    "cin >> 剔除开关;   // 构造期不可变（D27-C），没有公共 setter",
    "رمز التحقق: تأكيد الحذف؟   ; DELETE FROM ledger WHERE status = 'open';",
    "invalidation storm: 47 requests coalesced into 3 WM_PAINT (30Hz merge)",
    "heap: widget pool 2000/2000 rows, 41.3 MB RSS, no growth over 500 frames",
]

ERROR_TEXT = [
    "工具超时（30s），已中止本次调用。",
    "目标文件不存在：ECDI/src/Render/GoneFile.cpp，已跳过。",
    "grep_repo 返回 1：模式未命中（ExitStatus=1，非致命）。",
]

ASK_QUESTIONS = [
    "允许执行 bash（cmake --build build-msvc）吗？",
    "允许 edit_file 修改 docs/README.md 吗？",
    "允许 bash 运行测试套件（ctest -j 8）吗？",
]


def iso(moment):
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % (moment.microsecond // 1000)


def build_result_text(rng, name):
    """Tool result body: length follows a realistic long-tail distribution."""
    roll = rng.random()
    if roll < 0.30:                                   # short ack / one-liner
        return rng.choice(["已读取 240 行。", "命中 12 处，已列出上下文。",
                           "1 处已替换，未触碰其它行。", "已写入 3182 字节。"])
    if roll < 0.70:                                   # medium: 15-40 log lines
        n = rng.randint(15, 40)
    else:                                             # long tail: 60-120 lines
        n = rng.randint(60, 120)
    header = {
        "read_file": "阅读 ECDI/src/Widget/ScrollView.cpp 的第 100-240 行：",
        "grep_repo": "在 ECDI/src 中命中以下位置（含上下文）：",
        "bash": "命令输出：",
        "edit_file": "替换完成，diff 如下：",
        "todo_write": "清单已更新。",
    }.get(name, "输出：")
    lines = [header] + [rng.choice(LOG_LINES) for _ in range(n)]
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate a deterministic realistic-session fixture.")
    parser.add_argument("--blocks", type=int, default=220,
                        help="number of transcript blocks to emit")
    parser.add_argument("--out", required=True, help="output .jsonl path")
    parser.add_argument("--seed", type=int, default=20261005,
                        help="RNG seed; same seed must give identical bytes")
    parser.add_argument("--session", default=None,
                        help="session id (default derived from block count)")
    parser.add_argument("--pace", type=float, default=1.0,
                        help="ts pacing multiplier (1.0 = realistic, 0.05 = compressed)")
    opts = parser.parse_args(argv)

    if opts.blocks < 8:
        sys.stderr.write("gen_realistic: --blocks must be at least 8\n")
        return 2
    if not opts.out.lower().endswith(".jsonl"):
        sys.stderr.write("gen_realistic: --out must end in .jsonl\n")
        return 2
    if opts.pace <= 0:
        sys.stderr.write("gen_realistic: --pace must be > 0\n")
        return 2

    rng = random.Random(opts.seed)
    session = opts.session or "s-real-%d" % opts.blocks
    now = BASE_TIME
    messages = []

    def advance(ms):
        nonlocal now
        now += datetime.timedelta(milliseconds=max(1.0, ms * opts.pace))

    def emit(payload, gap_ms):
        nonlocal now
        advance(gap_ms)
        payload["seq"] = len(messages)
        payload["ts"] = iso(now)
        ordered = {"t": payload.pop("t"), "seq": payload.pop("seq"),
                   "ts": payload.pop("ts")}
        ordered.update(payload)
        messages.append(ordered)

    emit({"t": "hello", "protocol": 3, "core": "0.1.0", "session": session,
          "caps": ["tools", "ask", "workspace"]}, 0)
    emit({"t": "state", "value": "running"}, 10)

    made = 0
    turn = 0
    while made < opts.blocks:
        turn += 1

        # ── user turn ──
        uid = "b%d" % made
        emit({"t": "block_start", "id": uid, "kind": "user",
              "client_id": "p%d" % turn,
              "text": rng.choice(USER_TURNS)}, rng.uniform(8000, 45000))
        emit({"t": "block_end", "id": uid, "ok": True}, 15)
        made += 1
        if made >= opts.blocks:
            break

        # ── assistant preamble (markdown, streamed) ──
        aid = "b%d" % made
        emit({"t": "block_start", "id": aid, "kind": "assistant"},
             rng.uniform(600, 2500))
        text = rng.choice(ASSISTANT_MD)
        cut = max(2, len(text) // rng.randint(6, 14))
        for start in range(0, len(text), cut):
            emit({"t": "delta", "id": aid, "text": text[start:start + cut]},
                 rng.uniform(30, 300))
        emit({"t": "block_end", "id": aid, "ok": True}, 120)
        made += 1
        if made >= opts.blocks:
            break

        # ── 1-3 tool rounds, some gated by ask ──
        for _ in range(rng.randint(1, 3)):
            if made + 2 > opts.blocks:
                break
            name, build = TOOL_DEFS[rng.randrange(len(TOOL_DEFS))]
            call_id = "b%d" % made
            result_id = "b%d" % (made + 1)
            emit({"t": "block_start", "id": call_id, "kind": "tool_call",
                  "name": name, "args": build(rng)}, rng.uniform(400, 3000))
            emit({"t": "block_end", "id": call_id, "ok": True,
                  "summary": "已发起"}, 10)
            made += 1
            if turn % 3 == 0 and rng.random() < 0.5:
                emit({"t": "ask", "id": "q%d-%s" % (turn, call_id),
                      "kind": "permission",
                      "question": rng.choice(ASK_QUESTIONS),
                      "options": [{"key": "allow", "label": "允许"},
                                  {"key": "deny", "label": "拒绝"}],
                      "default": "deny"}, rng.uniform(50, 300))
                emit({"t": "state", "value": "waiting"}, 40)
                emit({"t": "state", "value": "running"}, rng.uniform(800, 4000))
            if made >= opts.blocks:
                break
            failed = rng.random() < 0.12
            emit({"t": "block_start", "id": result_id, "kind": "tool_result",
                  "for": call_id}, rng.uniform(400, 6000))
            body = (rng.choice(ERROR_TEXT) if failed
                    else build_result_text(rng, name))
            cut = max(1, len(body) // rng.randint(1, 4))
            for start in range(0, len(body), cut):
                emit({"t": "delta", "id": result_id,
                      "text": body[start:start + cut]}, rng.uniform(10, 120))
            emit({"t": "block_end", "id": result_id, "ok": not failed}, 60)
            made += 1

        if made >= opts.blocks:
            break

        # ── assistant wrap-up (shorter, still markdown) ──
        wid = "b%d" % made
        emit({"t": "block_start", "id": wid, "kind": "assistant"},
             rng.uniform(300, 1500))
        text = rng.choice(ASSISTANT_MD).split("\n")[0]
        cut = max(2, len(text) // rng.randint(3, 8))
        for start in range(0, len(text), cut):
            emit({"t": "delta", "id": wid, "text": text[start:start + cut]},
                 rng.uniform(30, 200))
        emit({"t": "block_end", "id": wid, "ok": True}, 100)
        made += 1

        if made % 3 == 0:
            emit({"t": "usage", "turn": turn,
                  "input": 8000 + made * 37, "output": 600 + made * 11}, 20)
        if rng.random() < 0.08:
            emit({"t": "error", "fatal": False,
                  "message": rng.choice(ERROR_TEXT)}, rng.uniform(100, 800))

    emit({"t": "usage", "turn": turn,
          "input": 8000 + made * 37, "output": 600 + made * 11}, 20)
    emit({"t": "state", "value": "idle"}, 30)
    emit({"t": "bye", "reason": "eof", "code": 0}, 10)

    body = "\n".join(json.dumps(m, ensure_ascii=False, separators=(",", ":"))
                     for m in messages) + "\n"
    data = body.encode("utf-8")
    tmp = opts.out + ".tmp"
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.replace(tmp, opts.out)

    blocks = len([m for m in messages if m["t"] == "block_start"])
    tools = len([m for m in messages if m["t"] == "block_start"
                 and m.get("kind") == "tool_call"])
    sys.stdout.write("gen_realistic: %d blocks (%d tool pairs), %d messages, "
                     "%d bytes -> %s\n"
                     % (blocks, tools, len(messages), len(data), opts.out))
    sys.stdout.write("  deltas=%d  asks=%d  max_result_bytes=%d  pace=%g  seed=%d\n"
                     % (len([m for m in messages if m["t"] == "delta"]),
                        len([m for m in messages if m["t"] == "ask"]),
                        max((len(m.get("text", "")) for m in messages), default=0),
                        opts.pace, opts.seed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())