#!/usr/bin/env python3
"""gen_scale.py -- deterministic generator for long-transcript fixtures.

Why a generator instead of one committed blob: the point of the scale fixture is
to find where ECDI breaks, and that needs a knob (2000 / 5000 / 20000 blocks),
not a single fixed file.  A committed instance is still useful as a reproducible
baseline, so this script is seeded and byte-identical across runs: regenerating
the same --blocks/--seed must produce the same bytes, which is what makes a
before/after measurement comparable.

The fixture is a VALID transcript (it must satisfy every invariant), because its
purpose is scale, not hostility -- mixing the two would confound the result.

Timestamps advance 2-20 ms per message, so even a `timed` replay of the 2000
block baseline finishes in well under two minutes.  For pure rendering stress
use `instant` replay.

Usage:
  python gen_scale.py --blocks 2000 --out scale-transcript-2000.jsonl
  python gen_scale.py --blocks 20000 --out huge.jsonl
"""

import argparse
import datetime
import json
import os
import random
import sys

BASE_TIME = datetime.datetime(2026, 10, 3, 12, 0, 0, tzinfo=datetime.timezone.utc)

TOOLS = [
    ("read_file", lambda r: {"path": r.choice(["docs/README.md",
                                               "ECDI/src/Widget/Widget.cpp",
                                               "docs/roadmap-deferred.md",
                                               "tools/doc_lint.py"]),
                             "max_lines": r.choice([120, 200, 400])}),
    ("grep_repo", lambda r: {"pattern": r.choice(["GetTestRegistry",
                                                  "UpdateContentExtent",
                                                  "PostToUi", "FRAMEWORK_ASSERT"]),
                             "glob": r.choice(["*.cpp", "*.h", "*.md"])}),
    ("edit_file", lambda r: {"path": r.choice(["README.md",
                                               "docs/README.md"]),
                             "line": r.randint(20, 560),
                             "from": "153 design documents",
                             "to": "155 design documents"}),
    ("run_tests", lambda r: {"target": "ecdi_tests",
                             "jobs": r.choice([4, 8, 16])}),
    ("write_file", lambda r: {"path": "tools/note-%d.md" % r.randint(1, 99),
                              "bytes": r.randint(200, 4000)}),
]

ASSISTANT_TEXT = [
    "锚点本身是对的，但两个 README 的同一事实要分别核对，只改一处会留下矛盾。",
    "我先确认这一个数字的载体清单：根 README、docs 索引、以及账本里的引用行。",
    "这一处属于历史记录，按纪律不改写，只在新版本条目里说明。",
    "工具已经跑完，结果是 305 通过、2 失败，失败点集中在布局用例。",
    "按最小修改面处理：只动那一行，其余保持现状。",
    "这个结论我不采纳——它没有被独立复现，先当作未证实线索记录。",
    "先把只读校验全部跑完，确认无误之后再统一写盘。",
    "编码与行尾要按每个文件自己的约定写回，不能按目录一刀切。",
]

RESULT_TEXT = [
    "575 行，读取完成。",
    "命中 12 处，已列出上下文。",
    "1 处已替换，未触碰其它行。",
    "Test summary: 305 passed, 2 failed, 307 total",
    "已写入 3182 字节。",
    "无命中。",
]

ERROR_TEXT = [
    "工具超时（30s），已中止本次调用。",
    "目标文件不存在，已跳过。",
]

USER_TEXT = [
    "看一下 docs/README.md 里有没有过期的数字，有就改掉。",
    "把这两份 README 的规模锚点对齐一遍。",
    "跑一次测试，把失败的用例列出来。",
    "这一处属于历史记录还是当前态？先判断再动手。",
    "把工具链的四个构建都过一遍。",
    "解释一下为什么这里不能用 git rm。",
]

TOOL_NAMES = [t[0] for t in TOOLS]


def iso(moment):
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + \
        "%03dZ" % (moment.microsecond // 1000)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate a deterministic long-transcript fixture.")
    parser.add_argument("--blocks", type=int, default=2000,
                        help="number of transcript blocks (rows) to emit")
    parser.add_argument("--out", required=True, help="output .jsonl path")
    parser.add_argument("--seed", type=int, default=20261003,
                        help="RNG seed; same seed must give identical bytes")
    parser.add_argument("--session", default=None,
                        help="session id (default derived from block count)")
    opts = parser.parse_args(argv)

    if opts.blocks < 1:
        sys.stderr.write("gen_scale: --blocks must be at least 1\n")
        return 2
    if not opts.out.lower().endswith(".jsonl"):
        sys.stderr.write("gen_scale: --out must end in .jsonl\n")
        return 2

    rng = random.Random(opts.seed)
    session = opts.session or "s-scale-%d" % opts.blocks
    now = BASE_TIME
    messages = []

    def emit(payload):
        nonlocal now
        payload["seq"] = len(messages)
        payload["ts"] = iso(now)
        ordered = {"t": payload.pop("t"), "seq": payload.pop("seq"),
                   "ts": payload.pop("ts")}
        ordered.update(payload)
        messages.append(ordered)
        now += datetime.timedelta(milliseconds=rng.uniform(2.0, 20.0))

    emit({"t": "hello", "protocol": 1, "core": "0.1.0", "session": session,
          "caps": ["tools", "ask"]})
    emit({"t": "state", "value": "running"})

    made = 0
    turn = 0
    while made < opts.blocks:
        turn += 1
        # a user turn roughly every eight blocks
        if made % 8 == 0 and made + 1 <= opts.blocks:
            emit({"t": "block_start", "id": "b%d" % made, "kind": "user",
                  "text": rng.choice(USER_TEXT)})
            emit({"t": "block_end", "id": "b%d" % made, "ok": True})
            made += 1
            if made >= opts.blocks:
                break

        # a tool pair roughly every fourth remaining block
        if made % 4 == 0 and made + 2 <= opts.blocks:
            name, build = TOOLS[rng.randrange(len(TOOLS))]
            call_id = "b%d" % made
            result_id = "b%d" % (made + 1)
            args = build(rng)
            emit({"t": "block_start", "id": call_id, "kind": "tool_call",
                  "name": name, "args": args})
            emit({"t": "block_end", "id": call_id, "ok": True,
                  "summary": "已发起"})
            emit({"t": "block_start", "id": result_id, "kind": "tool_result",
                  "for": call_id})
            emit({"t": "delta", "id": result_id,
                  "text": rng.choice(RESULT_TEXT)})
            emit({"t": "block_end", "id": result_id, "ok": True})
            made += 2
            # every eighth tool call asks for permission first
            if turn % 8 == 0:
                emit({"t": "ask", "id": "q%d" % turn, "kind": "permission",
                      "question": "允许执行 %s 吗？" % name,
                      "options": [{"key": "allow", "label": "允许"},
                                  {"key": "deny", "label": "拒绝"}],
                      "default": "deny"})
                emit({"t": "state", "value": "waiting"})
                emit({"t": "state", "value": "running"})
            continue

        # otherwise an assistant block, streamed in a few chunks
        if made + 1 > opts.blocks:
            break
        block_id = "b%d" % made
        emit({"t": "block_start", "id": block_id, "kind": "assistant"})
        text = rng.choice(ASSISTANT_TEXT)
        cut = max(1, len(text) // rng.randint(2, 4))
        for start in range(0, len(text), cut):
            emit({"t": "delta", "id": block_id, "text": text[start:start + cut]})
        emit({"t": "block_end", "id": block_id, "ok": True})
        made += 1
        if made % 16 == 0:
            emit({"t": "error", "fatal": False,
                  "message": rng.choice(ERROR_TEXT)})

    emit({"t": "usage", "turn": turn,
          "input": 12000 + opts.blocks * 3, "output": 900 + opts.blocks})
    emit({"t": "state", "value": "idle"})
    emit({"t": "bye", "reason": "eof", "code": 0})

    body = "\n".join(json.dumps(m, ensure_ascii=False, separators=(",", ":"))
                     for m in messages) + "\n"
    data = body.encode("utf-8")
    tmp = opts.out + ".tmp"
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.replace(tmp, opts.out)

    blocks = len([m for m in messages if m["t"] == "block_start"])
    tool_pairs = len([m for m in messages if m["t"] == "block_start"
                      and m.get("kind") == "tool_call"])
    sys.stdout.write("gen_scale: %d blocks (%d tool pairs), %d messages, "
                     "%d bytes -> %s\n"
                     % (blocks, tool_pairs, len(messages), len(data), opts.out))
    sys.stdout.write("  deltas=%d  asks=%d  seed=%d\n"
                     % (len([m for m in messages if m["t"] == "delta"]),
                        len([m for m in messages if m["t"] == "ask"]),
                        opts.seed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
