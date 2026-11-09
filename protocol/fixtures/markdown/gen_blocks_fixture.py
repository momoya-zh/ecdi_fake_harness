#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 protocol/fixtures/markdown/blocks-session.jsonl（富内容 B+ 块级全套夹具）。

★ 为什么需要一份**专用夹具**：实测七份既有夹具里 headings / quotes / hr / links 的
  出现次数全是 0，只有 realistic 含围栏（14）与列表（20）。块级渲染在既有夹具上
  「跑得通」只证明它不崩，证明不了它对——因为根本没有素材可渲染。
  既有夹具一律不动（基线 220/717/1213 等是逐字节判据），本文件是**新增**的。

★ 纪律：
  · 输出全部 ASCII 以外的内容用 UTF-8 写出，**不加 BOM**，行尾 **LF**（*.jsonl 硬规则）；
  · 生成是确定性的：无时间戳随机、无字典序依赖 ⇒ 重复运行 sha256 必须相同；
  · seq 连续、ts 单调、结尾 bye=eof（同其余夹具口径）。

用法：python protocol/fixtures/markdown/gen_blocks_fixture.py
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blocks-session.jsonl")

T0 = "2026-11-08T10:00:00.000Z"
_lines = []
_seq = [0]


def ts(ms):
    """单调时间戳：按毫秒偏移生成，避免时区/浮动。"""
    base_h, base_m = 10, 0
    total = base_h * 60 + base_m
    total_ms = total * 60 * 1000 + ms
    h = total_ms // 3600000
    m = (total_ms % 3600000) // 60000
    s = (total_ms % 60000) // 1000
    frac = total_ms % 1000
    return f"2026-11-08T{h:02d}:{m:02d}:{s:02d}.{frac:03d}Z"


def emit(obj):
    obj["seq"] = _seq[0]
    _seq[0] += 1
    _lines.append(json.dumps(obj, ensure_ascii=False, separators=(",", ":")))


_ms = [0]


def tick(step=20):
    _ms[0] += step
    return ts(_ms[0])


emit({"t": "hello", "ts": tick(0), "protocol": 3, "core": "0.1.0",
      "session": "s-md0001", "caps": ["tools", "ask", "workspace"]})
emit({"t": "state", "ts": tick(), "value": "running"})

# ── 块 1：user 提问 ────────────────────────────────────────────────
emit({"t": "block_start", "ts": tick(), "id": "b1", "kind": "user",
      "client_id": "p1", "text": "给我一份带标题、代码、列表、引用和分隔线的回答"})
emit({"t": "block_end", "ts": tick(), "id": "b1"})

# ── 块 2：assistant，块级全套（**流式 delta 送**，模拟真实输出）──
BODY_PARTS = [
    "# 一级标题\n\n",
    "这是一段普通引言正文，用来验证段落没有被块级解析吃掉。\n\n",
    "## 二级标题\n\n",
    "### 三级标题\n\n",
    "下面是围栏代码块，含一个**超长行**用来验证横向滚动：\n\n",
    "```cpp\n",
    "// 代码块内的 # 与 - 都不应被当成标题或列表\n",
    "int main(int argc, char** argv) {\n",
    "    const std::string very_long_line = \"这一行故意写得很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长很长，目的是超出版心 760px 逼出横向滚动条\";\n",
    "    return 0;\n",
    "}\n",
    "```\n\n",
    "无序列表：\n\n",
    "- 第一项\n",
    "- 第二项，带点长度看看悬挂缩进对不对\n",
    "  - 嵌套的子项\n",
    "- 第三项\n\n",
    "有序列表（原文序号 1/2/3，不重排）：\n\n",
    "1. 第一步\n",
    "2. 第二步\n",
    "3. 第三步\n\n",
    "> 这是一段引用。\n",
    "> 引用可以跨行合并成一段。\n\n",
    "---\n\n",
    "分隔线之后的收尾段落。\n",
]

emit({"t": "block_start", "ts": tick(), "id": "b2", "kind": "assistant",
      "client_id": "p1", "text": ""})
for part in BODY_PARTS:
    emit({"t": "delta", "ts": tick(10), "id": "b2", "text": part})

# 块 2 结束前再补一条 reasoning，验证块级不影响其它 kind
emit({"t": "block_start", "ts": tick(), "id": "b3", "kind": "reasoning",
      "client_id": "p1", "text": ""})
emit({"t": "delta", "ts": tick(10), "id": "b3", "text": "先列大纲，再写正文。"})

emit({"t": "block_end", "ts": tick(), "id": "b3"})
emit({"t": "block_end", "ts": tick(), "id": "b2"})

# ── 块 4：表格 + 列表混排（验证 mixed-order 在渲染侧也成立）──────
TABLE_PARTS = [
    "## 结果汇总\n\n",
    "| 构造 | 状态 | 覆盖 |\n",
    "| :--- | :---: | ---: |\n",
    "| 标题 | 已渲染 | 3 级 |\n",
    "| 代码块 | 已渲染 | 横滚 |\n",
    "| 列表 | 已渲染 | 嵌套 |\n\n",
    "- 混排后的列表项\n",
    "- 最后一项\n",
]

emit({"t": "block_start", "ts": tick(), "id": "b4", "kind": "assistant",
      "client_id": "p1", "text": ""})
for part in TABLE_PARTS:
    emit({"t": "delta", "ts": tick(10), "id": "b4", "text": part})
emit({"t": "block_end", "ts": tick(), "id": "b4"})

# ── 块 5：**负对照**——长得像块级但不是，必须逐字原样 ────────────
PLAIN_PARTS = [
    "这段里 `# 不是标题` 与 `- 不是列表` 都在行内，整段应原样显示。\n",
    "顶格的 #hashtag 没有空格，也不是标题。\n",
    "   这是缩进四行，但上一行不是列表，所以不该变成代码块。\n",
    "| 单个竖线 | 下一行不是分隔行 |\n",
    "[链接](https://example.com) 本批不解析（等 REQ-19-B），按原文显示。\n",
]

emit({"t": "block_start", "ts": tick(), "id": "b5", "kind": "assistant",
      "client_id": "p1", "text": ""})
for part in PLAIN_PARTS:
    emit({"t": "delta", "ts": tick(10), "id": "b5", "text": part})
emit({"t": "block_end", "ts": tick(), "id": "b5"})

emit({"t": "state", "ts": tick(), "value": "idle"})
emit({"t": "bye", "ts": tick(), "reason": "eof", "code": 0})

with open(OUT, "wb") as f:
    for line in _lines:
        f.write(line.encode("utf-8") + b"\n")   # ★ 无 BOM、LF、末尾有换行

print(f"wrote {OUT}: {len(_lines)} lines, {os.path.getsize(OUT)} bytes")
