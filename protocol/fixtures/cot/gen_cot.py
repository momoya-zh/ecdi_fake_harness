#!/usr/bin/env python3
"""gen_cot.py -- chain-of-thought fixture (open-source reasoning models).

WHY this fixture exists
-----------------------
Agents running open-source reasoning models (DeepSeek-R1, QwQ, and friends)
STREAM their chain-of-thought before the answer.  That content is:

  * LONG      -- thousands of tokens per turn, far longer than the answer;
  * STREAMED  -- one token (or a few) at a time, so every delta can invalidate;
  * COLLAPSED -- users generally do not want to read it, so it is folded away.

Those three properties together are a distinct load profile, and none of the
existing fixtures exercise it:

  * gen_scale.py      uniform blocks, scale only, no reasoning text at all;
  * gen_realistic.py  human-paced markdown answers, no chain-of-thought;
  * demo-session      one small happy-path session.

Meanwhile PROTOCOL 3.3 originally had no `kind` for chain-of-thought (six values,
none of them reasoning), so this fixture also pins down what the GUI does with
content the protocol never named.  See docs/ecdi-capability-requests.md SS2 for
the protocol gap and for why the harness must render unknown kinds NEUTRALLY
(never disguised as a tool card).

STATUS UPDATE (2026-10-14): that gap is now CLOSED -- PROTOCOL v2 added
`kind:"reasoning"` as a first-class kind plus invariant I8 (unknown kinds must
render neutrally).  So `--kind reasoning` is now the REAL protocol value, and
the default rendering is a COLLAPSED block ("思考中" while streaming, "思考过程"
once ended) that expands on click.  `--kind unknown` still exercises the I8
path with a value v2 does not name (`thinking`), which is exactly what a v1-era
consumer would receive.

The intended measurement
------------------------
Three readings, all real (see .scratch/probe_cot.py):

  1. LENGTH DOSE -- replay with growing --thinking-chars and compare
     `measureChars / measureCalls` (frame-count independent).  This is the
     cost of streaming reasoning at all, and it is the direct reading for
     metrics 1 (redraw/frame time under streaming) and 4 (text measurement).
  2. KIND COMPARISON -- replay the same content emitted as `reasoning`
     (collapsed block as of v2), `thinking` (unknown => neutral visible
     bubble), and `assistant` (plain text).  Same bytes, different render path.
  3. FOLD COMPARISON -- as of v2 this is finally measurable: the reasoning
     block starts COLLAPSED and can be expanded, so the same fixture can be
     replayed in both states and the difference attributed to folding.  Before
     v2 there was no fold affordance at all (unknown kinds rendered as visible
     neutral bubbles precisely so they could not masquerade as tool cards), so
     this reading simply did not exist and was deliberately NOT faked.

Deterministic: same --blocks/--seed/--kind => identical bytes (same contract as
gen_scale.py and gen_realistic.py).  The fixture is VALID (satisfies every
invariant; an unknown `kind` is not itself a violation -- I8 requires it be
rendered neutrally and reported, which is a GUI obligation, not a stream one).

Usage:
  python gen_cot.py --turns 12 --out cot-session.jsonl
  python gen_cot.py --turns 12 --kind unknown --out cot-unknown.jsonl
  python gen_cot.py --turns 12 --thinking-chars 4000 --out cot-long.jsonl
  python gen_cot.py --turns 8 --kind assistant --out cot-assistant.jsonl
"""

import argparse
import datetime
import json
import os
import random
import sys

BASE_TIME = datetime.datetime(2026, 10, 13, 21, 0, 0,
                              tzinfo=datetime.timezone.utc)

# Reasoning reads differently from the answer: hedging, backtracking, explicit
# self-correction.  That shape matters for wrapping and for line count, so the
# text is not just filler -- but it is intentionally repetitive enough that a
# long --thinking-chars is realistic rather than absurd.
REASONING_PARAGRAPHS = [
    "先明确用户真正要的是什么。他说的是「滚动延迟」，但前面又提到内存——这两个指标"
    "可能在同一个改动上互相拉扯，我得先把它们分开量。",
    "第 1 步：确认现象是不是稳定复现。上一次的日志只出现一次，单次采样不能作为判据。",
    "等一下，我刚才那个推断有问题。剔除判据用的是严格大于零，那么边缘贴合的子树"
    "本来就会被整棵跳过——所以闪烁不可能来自这里，我换个方向。",
    "候选二：是否是重绘合并的时机问题。定时器 33ms 一次，而 delta 是连续到达的，"
    "如果每个 delta 都触发一次 Invalidate，那合并窗口内会有多次请求。",
    "让我把这个假设写成可证伪的形式：如果重绘合并正常，那么 10 秒内 paints 的次数"
    "应该接近 300 而不是 3000。这个可以直接从摘要里读出来。",
    "不过要小心：「paints 次数接近 300」这个预期本身依赖帧率假设。如果机器负载高，"
    "帧率本来就达不到 30Hz，那这个判据会把正常行为误判成缺陷。",
    "更稳的做法是看比例而不是绝对值——把「每个 delta 引发的 paints」作为读数，"
    "它不受机器负载影响。",
    "回来看第一性原理：这里到底哪一层负责合并？如果合并发生在应用侧，那么框架"
    "只是如实执行；如果发生在框架侧，那这就是一条能力缺口。归属判错会白写一份需求。",
    "我倾向于先查源码再下结论。上一轮就是因为凭印象判断「框架没消费换行」，"
    "结果反转了两次，代价很大。",
    "还有一个没排除的可能：文本测量成本。如果折叠起来的内容仍然被逐帧测量，"
    "那长思维链会拖慢每一帧——这个假设可以用 measureChars 直接验证。",
    "三个假设并列，优先级：① 测量成本（最能解释「滚动时卡」）② 重绘合并 ③ 剔除判据。"
    "先测①，因为它的判据最干净。",
    "补一句边界：即使①成立，也不能直接说框架有问题——得看控件是否知道自己的内容"
    "不可见。知道却不跳过，才是框架的事；不知道，那是调用方没把状态传下去。",
]

ANSWER_PARAGRAPHS = [
    "结论：**卡顿来自折叠内容仍在被逐帧测量**，与重绘合并和剔除判据都无关。",
    "证据：把同一份夹具跑两遍，唯一变量是折叠体的文本长度。",
    "修法：折叠态跳过 `AutoSize()`，展开态照旧每帧跟随。",
]

USER_TURNS = [
    "滚动的时候卡一下，帮我查是什么原因",
    "这段思维链太长了，能不能默认折叠起来",
    "为什么你思考了这么久？把过程给我看看",
    "先别改代码，告诉我你的判断依据是什么",
]

TOOLS = [
    ("read_file", lambda r: {"path": r.choice(
        ["gui/src/App.cpp", "gui/src/Transcript.cpp", "docs/PROTOCOL.md"]),
        "max_lines": r.choice([200, 400])}),
    ("grep_repo", lambda r: {"pattern": r.choice(
        ["AutoSize", "unknownKind", "measureChars"]), "glob": "*.cpp"}),
]

RESULT_TEXT = [
    "读到 412 行，已截取相关段落。",
    "命中 7 处，上下文已附。",
    "无命中。",
]


def iso(moment):
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + \
        "%03dZ" % (moment.microsecond // 1000)


def reasoning_text(rng, target_chars, paragraphs):
    """Build reasoning prose of roughly target_chars characters.

    Assembled from whole paragraphs so the text stays human-readable at any
    length (a sliced paragraph would look like a truncation bug in the UI).
    """
    out = []
    size = 0
    while size < target_chars:
        para = rng.choice(paragraphs)
        out.append(para)
        size += len(para) + 1
        # Occasionally interleave a short backtrack line -- real reasoning
        # does this, and it keeps line lengths varied (wrapping stress).
        if rng.random() < 0.25:
            aside = rng.choice([
                "（这条先记下，待验证。）",
                "——不对，再想想。",
                "总之，先按①推进。",
            ])
            out.append(aside)
            size += len(aside) + 1
    return "\n".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate a deterministic chain-of-thought fixture.")
    parser.add_argument("--turns", type=int, default=12,
                        help="number of user turns; each turn gets reasoning")
    parser.add_argument("--thinking-chars", type=int, default=2600,
                        help="target characters of reasoning per turn")
    parser.add_argument("--kind", default="reasoning",
                        choices=["reasoning", "unknown", "assistant"],
                        help="how to emit the reasoning block "
                             "(reasoning = PROTOCOL v2 first-class kind, rendered "
                             "collapsed; unknown = a value v2 does not name, "
                             "exercising I8's neutral path; assistant = plain text)")
    parser.add_argument("--emit-empty", action="store_true",
                        help="emit the reasoning block with NO body text at all "
                             "(a baseline with the row present but empty; distinct "
                             "from the default COLLAPSED state, which still has a "
                             "body that gets measured)")
    parser.add_argument("--out", required=True, help="output .jsonl path")
    parser.add_argument("--seed", type=int, default=20261013,
                        help="RNG seed; same seed must give identical bytes")
    parser.add_argument("--session", default=None,
                        help="session id (default derived from turn count)")
    parser.add_argument("--pace", type=float, default=1.0,
                        help="ts pacing multiplier (1.0 = realistic, 0.05 = fast)")
    opts = parser.parse_args(argv)

    if opts.turns < 1:
        sys.stderr.write("gen_cot: --turns must be at least 1\n")
        return 2
    if opts.thinking_chars < 0:
        sys.stderr.write("gen_cot: --thinking-chars must be >= 0\n")
        return 2
    if not opts.out.lower().endswith(".jsonl"):
        sys.stderr.write("gen_cot: --out must end in .jsonl\n")
        return 2
    if opts.pace <= 0:
        sys.stderr.write("gen_cot: --pace must be > 0\n")
        return 2

    rng = random.Random(opts.seed)
    session = opts.session or "s-cot-%d" % opts.turns
    # The block kind actually written. `unknown` picks a name that is plainly
    # not in PROTOCOL 3.3 -- `thinking` is what several real clients use.
    kind = {"reasoning": "reasoning", "unknown": "thinking",
            "assistant": "assistant"}[opts.kind]

    now = BASE_TIME
    messages = []

    def emit(payload, gap_ms):
        nonlocal now
        now += datetime.timedelta(milliseconds=max(1.0, gap_ms * opts.pace))
        payload["seq"] = len(messages)
        payload["ts"] = iso(now)
        ordered = {"t": payload.pop("t"), "seq": payload.pop("seq"),
                   "ts": payload.pop("ts")}
        ordered.update(payload)
        messages.append(ordered)

    emit({"t": "hello", "protocol": 2, "core": "0.1.0", "session": session,
          "caps": ["tools", "ask"]}, 0)
    emit({"t": "state", "value": "running"}, 10)

    made = 0
    for turn in range(1, opts.turns + 1):
        # ── user turn (core echoes it: the transcript's only author is core) ──
        uid = "b%d" % made
        emit({"t": "block_start", "id": uid, "kind": "user",
              "text": rng.choice(USER_TURNS)}, rng.uniform(3000, 20000))
        emit({"t": "block_end", "id": uid, "ok": True}, 15)
        made += 1

        # ── chain-of-thought: long, streamed, meant to be folded ──
        # ONE delta per chunk, small chunks: this is the streaming shape that
        # stresses invalidation merging (metric 1), unlike a single big delta.
        if opts.thinking_chars > 0 or opts.emit_empty:
            rid = "b%d" % made
            emit({"t": "block_start", "id": rid, "kind": kind}, 400)
            # Length is captured HERE, not recomputed for the summary: an earlier
            # draft called reasoning_text() twice (once for the deltas, once for
            # the summary) and the two draws differed, so the reported size
            # disagreed with the body.  Build the text once.
            text = "" if opts.emit_empty else reasoning_text(
                rng, opts.thinking_chars, REASONING_PARAGRAPHS)
            if text:
                cut = max(4, len(text) // rng.randint(40, 90))
                for start in range(0, len(text), cut):
                    emit({"t": "delta", "id": rid, "text": text[start:start + cut]},
                         rng.uniform(8, 60))
            emit({"t": "block_end", "id": rid, "ok": True,
                  "summary": "%d 字" % len(text)}, 200)
            made += 1

        # ── a tool round sometimes: interleaving reasoning and tools is what
        #    makes the "which kind is this row" question concrete on screen ──
        if turn % 3 == 0:
            name, build = TOOLS[rng.randrange(len(TOOLS))]
            cid, xid = "b%d" % made, "b%d" % (made + 1)
            emit({"t": "block_start", "id": cid, "kind": "tool_call",
                  "name": name, "args": build(rng)}, 600)
            emit({"t": "block_end", "id": cid, "ok": True,
                  "summary": "已发起"}, 20)
            emit({"t": "block_start", "id": xid, "kind": "tool_result",
                  "for": cid}, 300)
            emit({"t": "delta", "id": xid, "text": rng.choice(RESULT_TEXT)}, 40)
            emit({"t": "block_end", "id": xid, "ok": True}, 10)
            made += 2

        # ── the answer (short -- that is the whole point) ──
        aid = "b%d" % made
        emit({"t": "block_start", "id": aid, "kind": "assistant"}, 500)
        text = "\n".join(ANSWER_PARAGRAPHS)
        cut = max(4, len(text) // rng.randint(4, 8))
        for start in range(0, len(text), cut):
            emit({"t": "delta", "id": aid, "text": text[start:start + cut]},
                 rng.uniform(30, 200))
        emit({"t": "block_end", "id": aid, "ok": True}, 100)
        made += 1

        emit({"t": "usage", "turn": turn,
              "input": 9000 + turn * 1500, "output": 400 + turn * 90}, 30)
        # A permission gate now and then, so this fixture also exercises I3.
        if turn % 5 == 0:
            emit({"t": "ask", "id": "q%d" % turn, "kind": "permission",
                  "question": "允许执行 %s 吗？" % "read_file",
                  "options": [{"key": "allow", "label": "允许"},
                              {"key": "deny", "label": "拒绝"}],
                  "default": "deny"}, 200)
            emit({"t": "state", "value": "waiting"}, 20)
            emit({"t": "state", "value": "running"}, 400)

    emit({"t": "state", "value": "idle"}, 200)
    emit({"t": "bye", "reason": "eof", "code": 0}, 50)

    body = "\n".join(json.dumps(m, ensure_ascii=False, separators=(",", ":"))
                     for m in messages) + "\n"
    data = body.encode("utf-8")
    tmp = opts.out + ".tmp"
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.replace(tmp, opts.out)

    blocks = len([m for m in messages if m["t"] == "block_start"])
    cot = len([m for m in messages if m["t"] == "block_start"
               and m.get("kind") == kind])
    deltas = len([m for m in messages if m["t"] == "delta"])
    sys.stdout.write("gen_cot: %d blocks (%d %s), %d messages, %d deltas, "
                     "%d bytes -> %s\n"
                     % (blocks, cot, kind, len(messages), deltas, len(data),
                        opts.out))
    sys.stdout.write("  turns=%d  thinking-chars=%d  emit-empty=%s  seed=%d  pace=%s\n"
                     % (opts.turns, opts.thinking_chars, opts.emit_empty,
                        opts.seed, opts.pace))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
