#!/usr/bin/env python
# ============================================================
# 真链路 MCP 冒烟：真 core 二进制 + 真 stdio MCP server + 脚本化 SSE 端点
# ------------------------------------------------------------
# 覆盖 Go 单测证明不了的那一段：真子进程握手 / 真分页 tools/list /
# 真 function.name 压缩 / 真 ask 门 / 真 tools/call 往返 / bye 后整树清空。
#
# 六个臂（A–F）：
#   A（正常，mcp.json 由本脚本生成）：
#       1 个服务 / 2 个工具（跨两页）→ 模型发起 mcp__smoke__git_status
#       → ask（带提案卡 detail，2026-11-06 起）→ allow → 真调用 → tool_result 回模型 → 收尾 idle
#   B（降级，mixed.json）：
#       不存在的命令 + 正常服务 ⇒ 坏服务只点名、好服务照常注册、caps 仍带 mcp
#   C（Streamable HTTP，2025-06-18）/ D（legacy HTTP+SSE，transport:"sse"）：
#       真 HTTP 端点 + 脚本化 SSE 回执，断言 hello.mcp 的 transport 标识与 DELETE 收尾
#   E（HTTP 不可达）：连接被拒 ⇒ 不注册工具、hello.mcp.failed 非空、退出码 0
#   F（运行期，七版）：--mcp-env=minimal 不泄漏 + 崩溃后按需重启
#
# 判据纪律（与 regress.py 同口径）：
#   * 不靠目视：每条断言都机读，失败即退出码 1，明细落 result.json。
#   * 前置缺失（core 二进制 / python）是 SKIP（退出码 2），不是 PASS。
#   * 模型端点是**脚本化 SSE**（不是真厂商端点）；server 是仓库自带 mock；
#     不起 GUI ⇒ GUI 渲染不在本套覆盖范围内。别把这份读数当 GUI 验收。
#
# 用法：
#   go build -o bin/ecdi-core.exe ./cmd/ecdi-core   （在 core/ 下）
#   python tools/mcp-smoke/drive.py [--core <path>] [--deadline 90]
#
# ★ 总看门狗：到 deadline（默认 90s，实测全程约 3s）先杀掉自己拉起的
#   core / server 再退出 1。接进 regress 后，门不能因为一次挂死而永远等下去，
#   也不能在超时后留下孤儿进程。
# 产物（gitignored）：.scratch/mcp-smoke/{mcp.json,mixed.json,fake-llm.log,result.json}
# ============================================================
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SMOKE = ROOT / ".scratch" / "mcp-smoke"
SERVER = Path(__file__).resolve().parent / "server.py"
LOG = SMOKE / "fake-llm.log"
RESULT = SMOKE / "result.json"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import http_server  # noqa: E402  （同目录的远程传输冒烟 server）

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

FAILS = []
CHECKS = []
PROCS = []  # 活着的 core 进程；看门狗与 finally 都靠它清场


def on_deadline(deadline):
    """总看门狗：超时先清子进程再退出 1（不写 result.json ⇒ 门看到 rc≠0 且无产物）。"""
    print(f"DEADLINE: smoke exceeded {deadline:.0f}s -- killing children and failing", flush=True)
    for p in list(PROCS):
        try:
            p.kill_tree()
        except Exception:  # noqa: BLE001
            pass
    os._exit(1)


def check(name, ok, detail=""):
    CHECKS.append({"check": name, "ok": bool(ok), "detail": str(detail)[:400]})
    if not ok:
        FAILS.append(name)
    print(("PASS " if ok else "FAIL ") + name + ("  | " + str(detail)[:200] if detail else ""))
    return ok


# ── 脚本化 OpenAI 兼容端点（第 1 轮发 tool_call，第 2 轮收尾）────────
class FakeLLM:
    def __init__(self, script=None):
        self.mcp_names = []
        self.round = 0
        self.lock = threading.Lock()
        # script：[(压缩后的工具全名, 参数对象), …] 按轮次依次发出；
        # 给 None = 既有两轮剧本（A/C/D 臂共用同一实例，行为逐字节不变）。
        self.script = script

    def handler(self):
        outer = self

        class H(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_POST(self):
                n = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(n).decode("utf-8", "replace")
                body = json.loads(raw)
                with outer.lock:
                    outer.round += 1
                    rnd = outer.round
                    with open(LOG, "a", encoding="utf-8") as f:
                        f.write("### request %d\n%s\n\n" % (rnd, json.dumps(body, ensure_ascii=False)[:4000]))
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "close")
                self.end_headers()
                out = []
                has_tool_msg = any(m.get("role") == "tool" for m in body.get("messages", []))
                if outer.script is not None:
                    # 脚本化：一轮一个调用，跑完再收尾（F 臂用）。
                    idx = rnd - 1
                    if idx < len(outer.script):
                        name, targs = outer.script[idx]
                        out.append(chunk({"role": "assistant", "content": "步骤 %d。" % (idx + 1)}))
                        out.append("data: " + json.dumps({"choices": [{"delta": {"tool_calls": [{
                            "index": 0, "id": "call-f-%d" % idx,
                            "function": {"name": name, "arguments": json.dumps(targs)}}]}}],
                            "usage": {"prompt_tokens": 50, "completion_tokens": 10}}) + "\n\n")
                    else:
                        out.append(chunk({"role": "assistant", "content": "脚本化流程结束。"}))
                        out.append("data: " + json.dumps({"choices": [], "usage": {
                            "prompt_tokens": 70, "completion_tokens": 12}}) + "\n\n")
                elif not has_tool_msg:
                    # 目标工具名从 core **真正上交**的 toolDefs 里取，不猜压缩结果
                    tools = body.get("tools") or []
                    mcp = [t["function"]["name"] for t in tools
                           if t.get("function", {}).get("name", "").startswith("mcp__")]
                    with outer.lock:
                        outer.mcp_names = mcp
                    target = next((x for x in mcp if "git" in x), (mcp[0] if mcp else ""))
                    out.append(chunk({"role": "assistant", "content": "我先查一下 git 状态。"}))
                    out.append("data: " + json.dumps({"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": "call-smoke-1",
                        "function": {"name": target, "arguments": json.dumps({"short": True})}}]}}],
                        "usage": {"prompt_tokens": 50, "completion_tokens": 10}}) + "\n\n")
                else:
                    out.append(chunk({"role": "assistant", "content": "已经在真实 MCP server 上完成调用。"}))
                    out.append("data: " + json.dumps({"choices": [], "usage": {
                        "prompt_tokens": 70, "completion_tokens": 12}}) + "\n\n")
                out.append("data: [DONE]\n\n")
                self.wfile.write("".join(out).encode("utf-8"))
                self.wfile.flush()
                self.close_connection = True

        return H


def chunk(delta):
    return "data: " + json.dumps({"choices": [{"delta": delta}]}) + "\n\n"


# ── core 管道驱动 ────────────────────────────────────────────────
class CoreProc:
    def __init__(self, core, cfg, url, session, extra_args=None, extra_env=None):
        self.lines = []
        self.stderr = []
        self.q = []
        self.qc = threading.Condition()
        env = dict(os.environ)
        env.update(extra_env or {})
        self.p = subprocess.Popen(
            [str(core), "--base-url", url, "--model", "smoke", "--key", "smoke-key",
             "--mcp-config", str(cfg), "--workspace", str(SMOKE), "--session", session,
             "--context", "160000"] + list(extra_args or []),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=str(SMOKE), env=env)
        threading.Thread(target=self._read_out, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()
        PROCS.append(self)

    def _read_out(self):
        for raw in iter(self.p.stdout.readline, b""):
            try:
                msg = json.loads(raw.decode("utf-8", "replace"))
            except Exception:
                continue
            with self.qc:
                self.lines.append(msg)
                self.q.append(msg)
                self.qc.notify_all()

    def _read_err(self):
        for raw in iter(self.p.stderr.readline, b""):
            self.stderr.append(raw.decode("utf-8", "replace").rstrip())

    def send(self, obj):
        try:
            self.p.stdin.write((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
            self.p.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            self.stderr.append("send failed: %r" % (e,))

    def wait(self, pred, timeout=15.0):
        end = time.time() + timeout
        with self.qc:
            while True:
                for i, m in enumerate(self.q):
                    if pred(m):
                        return self.q.pop(i)
                left = end - time.time()
                if left <= 0:
                    return None
                self.qc.wait(left)

    def kill_tree(self):
        if self.p.poll() is not None:
            return
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(self.p.pid), "/T", "/F"], capture_output=True)
        else:
            self.p.kill()


def wait_for(c, pred, timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        m = c.wait(pred, timeout=min(0.5, end - time.time()))
        if m is not None:
            return m
    return None


def server_procs():
    """残留的 MCP server 进程（按 server.py 绝对路径匹配）。"""
    if os.name == "nt":
        ps = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | "
             "ForEach-Object { $_.ProcessId.ToString() + ' ' + $_.CommandLine }"],
            capture_output=True, text=True)
        return [l for l in ps.stdout.splitlines() if str(SERVER) in l or "server.py" in l]
    ps = subprocess.run(["ps", "-eo", "pid=,args="], capture_output=True, text=True)
    return [l for l in ps.stdout.splitlines() if str(SERVER) in l and "drive.py" not in l]


def write_configs(base_url="", dead_url=""):
    SMOKE.mkdir(parents=True, exist_ok=True)
    good = {"mcpServers": {"smoke": {"command": sys.executable, "args": [str(SERVER)]}}}
    mixed = {"mcpServers": {
        "broken": {"command": "no-such-mcp-server.exe"},
        "smoke": {"command": sys.executable, "args": [str(SERVER)]},
    }}
    (SMOKE / "mcp.json").write_text(json.dumps(good, ensure_ascii=False), encoding="utf-8")
    (SMOKE / "mixed.json").write_text(json.dumps(mixed, ensure_ascii=False), encoding="utf-8")
    if base_url:
        remote = {"mcpServers": {"remote": {"url": base_url + "/mcp"}}}
        legacy = {"mcpServers": {"legacy": {"url": base_url + "/sse", "transport": "sse"}}}
        (SMOKE / "http.json").write_text(json.dumps(remote, ensure_ascii=False), encoding="utf-8")
        (SMOKE / "legacy.json").write_text(json.dumps(legacy, ensure_ascii=False), encoding="utf-8")
    if dead_url:
        dead = {"mcpServers": {"remote": {"url": dead_url + "/mcp"}}}
        (SMOKE / "http-dead.json").write_text(json.dumps(dead, ensure_ascii=False), encoding="utf-8")
    # F 臂：测试专用工具（崩溃 / 环境回读）+ minimal 环境。
    runtime = {"mcpServers": {"smoke": {
        "command": sys.executable, "args": [str(SERVER)],
        "env": {"SMOKE_EXTRA_TOOLS": "1"},
    }}}
    (SMOKE / "runtime.json").write_text(json.dumps(runtime, ensure_ascii=False), encoding="utf-8")
    return SMOKE / "mcp.json", SMOKE / "mixed.json"


def arm_a(core, llm, port):
    c = CoreProc(core, SMOKE / "mcp.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-1")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("A/hello 到达", hello is not None)
        if hello:
            check("A/hello.protocol == 6", hello.get("protocol") == 6, hello.get("protocol"))
            check("A/caps 含 mcp", "mcp" in (hello.get("caps") or []), hello.get("caps"))
            # ★ 2026-11-06：产品面状态快照（GUI 副标/卡片的唯一数据源）。
            mcp = hello.get("mcp")
            check("A/hello.mcp 在场且来源为显式配置",
                  isinstance(mcp, dict) and mcp.get("source") == "explicit"
                  and str(mcp.get("config", "")).endswith("mcp.json"), mcp)
            if isinstance(mcp, dict):
                svcs = mcp.get("services") or []
                fails = mcp.get("failed") or []
                check("A/hello.mcp 列出 smoke/2 工具且无失败",
                      svcs == [{"name": "smoke", "tools": 2, "transport": "stdio"}] and fails == [],
                      {"services": svcs, "failed": fails})
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "prompt", "seq": 1, "text": "请查看 git 状态", "client_id": "smoke-c1"})

        ask = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("A/ask 门被触发（外部工具默认过门）", ask is not None)
        if ask:
            q = ask.get("question", "")
            check("A/问句带服务与真实工具名", "MCP 工具" in q and "smoke" in q and "git.status" in q, q)
            # ★ 2026-11-06 起 MCP 的 ask 带提案卡 detail（判别值 "mcp"，与 GUI
            #   渲染分支同批放开）。此前这里断言的是「不带 detail」——那条判据
            #   把一版复合名事故后的回落形态认证成了绿的，同日随批翻转。
            det = ask.get("detail")
            check("A/ask 带提案卡 detail", isinstance(det, dict), type(det).__name__)
            if isinstance(det, dict):
                check("A/detail 判别值与服务", det.get("tool") == "mcp" and det.get("server") == "smoke",
                      {k: det.get(k) for k in ("tool", "server")})
                check("A/detail 真实工具名", det.get("toolName") == "git.status", det.get("toolName"))
                kv = det.get("argsKv")
                check("A/detail argsKv 是键值数组", isinstance(kv, list) and len(kv) >= 1,
                      type(kv).__name__)
                if isinstance(kv, list):
                    hit = [e for e in kv if isinstance(e, dict) and e.get("k") == "short" and e.get("v") == "true"]
                    check("A/argsKv 含 short=true（与调用参数一致）", bool(hit), kv)
            c.send({"t": "answer", "seq": 2, "id": ask.get("id"), "choice": "allow"})

        tc = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_call", 15)
        check("A/tool_call 指向真实 MCP 工具", tc is not None and tc.get("name", "").startswith("mcp__smoke__"),
              tc.get("name") if tc else None)

        tr = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_result", 15)
        check("A/tool_result 块出现", tr is not None)
        delta = wait_for(c, lambda m: m.get("t") == "delta" and m.get("id") == (tr or {}).get("id"), 10)
        txt = (delta or {}).get("text", "")
        check("A/结果来自真 server（ok: 前缀）", txt.startswith("ok:"), txt[:120])
        check("A/参数真的传到了 server", '"short": true' in txt or '"short":true' in txt, txt[:160])
        end = wait_for(c, lambda m: m.get("t") == "block_end" and m.get("id") == (tr or {}).get("id"), 10)
        check("A/tool_result ok:true", (end or {}).get("ok") is True, end)

        idle = wait_for(c, lambda m: m.get("t") == "state" and m.get("value") == "idle", 20)
        check("A/第二轮收尾回到 idle", idle is not None)
        finals = [m for m in c.lines if m.get("t") == "delta" and "完成调用" in m.get("text", "")]
        check("A/模型看到了工具结果（第二轮文本）", bool(finals), len(finals))

        c.send({"t": "bye", "seq": 3})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("A/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("A/退出码 0", code == 0, code)
        time.sleep(0.5)
        left = server_procs()
        check("A/bye 后无 MCP server 残留", not left, left[:2])
    finally:
        c.kill_tree()

    err = "\n".join(c.stderr)
    check("A/stderr: 1 个服务 2 个工具", "1 个服务、2 个工具" in err, err[:200])
    check("A/stderr: hub 注册 2 个工具", "mcp hub initialized: 2 tools registered" in err)
    check("A/stderr: 协商差异被说出来", "2025-06-18" in err and "协商" in err)
    # ★ 成功路径不得往转录里塞失败行（失败块只在 Failed/Problem 非空时发）。
    mcp_fail_blocks = [m for m in c.lines
                       if m.get("t") == "block_start" and m.get("kind") == "system"
                       and "[MCP]" in m.get("text", "")]
    check("A/无 MCP 失败 system 块", not mcp_fail_blocks, len(mcp_fail_blocks))

    names = list(llm.mcp_names)
    check("A/上交给模型的 MCP 工具数 == 2", len(names) == 2, names)
    check("A/每个名字都满足 function.name 字符集",
          bool(names) and all(re.fullmatch(r"[A-Za-z0-9_-]{1,64}", n) for n in names), names)
    tail = [n[len("mcp__smoke__"):] for n in names if n.startswith("mcp__smoke__")]
    check("A/中文工具名被折叠成下划线",
          any(t and set(t) == {"_"} for t in tail), names)


def arm_b(core, port):
    c = CoreProc(core, SMOKE / "mixed.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-2")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("B/坏服务不挡 hello", hello is not None)
        if hello:
            check("B/caps 仍含 mcp", "mcp" in (hello.get("caps") or []), hello.get("caps"))
            mcp = hello.get("mcp")
            svcs = (mcp or {}).get("services") or []
            fails = (mcp or {}).get("failed") or []
            check("B/hello.mcp 列出好服务与 broken 失败",
                  isinstance(mcp, dict)
                  and any(s.get("name") == "smoke" and s.get("tools") == 2 for s in svcs)
                  and any(f.get("name") == "broken" and f.get("error") for f in fails),
                  {"services": svcs, "failed": fails})
            # ★ 失败同时进 hello.mcp（状态）与 system 块（事件）——后者不受
            #   检视栏是否打开影响，产品模式默认视图也看得见。
            block = wait_for(c, lambda m: m.get("t") == "block_start"
                             and m.get("kind") == "system"
                             and "[MCP]" in m.get("text", "")
                             and "broken" in m.get("text", ""), 10)
            check("B/失败 system 块可见", block is not None,
                  (block or {}).get("text", "")[:160])
        c.send({"t": "hello", "seq": 0})
        time.sleep(0.5)
        c.send({"t": "bye", "seq": 1})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("B/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("B/退出码 0", code == 0, code)
    finally:
        c.kill_tree()
    err = "\n".join(c.stderr)
    check("B/坏服务被点名", "启动服务 broken 失败" in err, err[:300])
    check("B/好服务仍然注册（1 个服务、2 个工具）", "1 个服务、2 个工具" in err, err[:200])


def arm_c(core, llm, port, base_url, http_state):
    """Streamable HTTP：POST /mcp；header 由本地 server 严格校验。"""
    c = CoreProc(core, SMOKE / "http.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-http")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("C/hello 到达", hello is not None)
        if hello:
            check("C/caps 含 mcp", "mcp" in (hello.get("caps") or []), hello.get("caps"))
            mcp = hello.get("mcp")
            svcs = (mcp or {}).get("services") or []
            check("C/hello.mcp 标注 http 传输",
                  isinstance(mcp, dict)
                  and any(s.get("name") == "remote" and s.get("tools") == 2
                          and s.get("transport") == "http" for s in svcs),
                  {"services": svcs})
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "prompt", "seq": 1, "text": "请查看远程 git 状态", "client_id": "smoke-c1"})

        ask = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("C/ask 门被触发（远程工具默认过门）", ask is not None)
        if ask:
            q = ask.get("question", "")
            check("C/问句带服务与真实工具名", "MCP 工具" in q and "remote" in q and "git.status" in q, q)
            det = ask.get("detail")
            check("C/ask 带提案卡 detail",
                  isinstance(det, dict) and det.get("tool") == "mcp"
                  and det.get("server") == "remote" and det.get("toolName") == "git.status", det)
            c.send({"t": "answer", "seq": 2, "id": ask.get("id"), "choice": "allow"})

        tc = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_call", 15)
        check("C/tool_call 指向远程 MCP 工具",
              tc is not None and tc.get("name", "").startswith("mcp__remote__"),
              tc.get("name") if tc else None)
        tr = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_result", 15)
        check("C/tool_result 块出现", tr is not None)
        delta = wait_for(c, lambda m: m.get("t") == "delta" and m.get("id") == (tr or {}).get("id"), 10)
        txt = (delta or {}).get("text", "")
        check("C/结果来自 HTTP server（ok: 前缀）", txt.startswith("ok:"), txt[:120])
        idle = wait_for(c, lambda m: m.get("t") == "state" and m.get("value") == "idle", 20)
        check("C/第二轮收尾回到 idle", idle is not None)

        c.send({"t": "bye", "seq": 3})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("C/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("C/退出码 0", code == 0, code)
    finally:
        c.kill_tree()
    err = "\n".join(c.stderr)
    check("C/stderr: 1 个服务 2 个工具", "1 个服务、2 个工具" in err, err[:200])
    # server 端的 header 校验就是客户端是否合规的判据：任何一条不满足都会 400。
    check("C/session 头全程合规", not http_state.session_errors, http_state.session_errors[:3])
    check("C/protocol-version 头全程合规", not http_state.protocol_errors, http_state.protocol_errors[:3])
    check("C/Close 发 DELETE 终止会话", http_state.deletes > 0, http_state.deletes)


def arm_d(core, llm, port, base_url, http_state):
    """legacy HTTP+SSE：GET /sse 取 endpoint，POST /messages，回执走 SSE。"""
    c = CoreProc(core, SMOKE / "legacy.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-sse")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("D/hello 到达", hello is not None)
        if hello:
            svcs = (hello.get("mcp") or {}).get("services") or []
            check("D/hello.mcp 标注 sse 传输",
                  any(s.get("name") == "legacy" and s.get("tools") == 2
                      and s.get("transport") == "sse" for s in svcs),
                  {"services": svcs})
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "prompt", "seq": 1, "text": "请查看 legacy git 状态", "client_id": "smoke-d1"})

        ask = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("D/ask 门被触发", ask is not None)
        if ask:
            det = ask.get("detail")
            check("D/ask 带提案卡 detail",
                  isinstance(det, dict) and det.get("server") == "legacy"
                  and det.get("toolName") == "git.status", det)
            c.send({"t": "answer", "seq": 2, "id": ask.get("id"), "choice": "allow"})

        tr = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_result", 15)
        check("D/tool_result 块出现", tr is not None)
        delta = wait_for(c, lambda m: m.get("t") == "delta" and m.get("id") == (tr or {}).get("id"), 10)
        txt = (delta or {}).get("text", "")
        check("D/结果来自 SSE 回执（ok: 前缀）", txt.startswith("ok:"), txt[:120])
        idle = wait_for(c, lambda m: m.get("t") == "state" and m.get("value") == "idle", 20)
        check("D/第二轮收尾回到 idle", idle is not None)

        c.send({"t": "bye", "seq": 3})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("D/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("D/退出码 0", code == 0, code)
    finally:
        c.kill_tree()
    err = "\n".join(c.stderr)
    check("D/stderr: 1 个服务 2 个工具", "1 个服务、2 个工具" in err, err[:200])
    # legacy 的所有 JSON-RPC 消息都必须走 POST /messages（含 initialize）。
    posted = [r for r in http_state.requests if r[0] == "/messages"]
    check("D/initialize 走 POST /messages",
          any(r[3].get("method") == "initialize" for r in posted), len(posted))


def arm_e(core, port, dead_url):
    """HTTP 不可达：失败必须进 hello.mcp + system 块，而不是静默空服务。"""
    c = CoreProc(core, SMOKE / "http-dead.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-http-dead")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("E/hello 到达（不可达服务不应挡住会话）", hello is not None)
        if hello:
            check("E/不可达服务不进 caps", "mcp" not in (hello.get("caps") or []), hello.get("caps"))
            mcp = hello.get("mcp")
            fails = (mcp or {}).get("failed") or []
            check("E/hello.mcp 记录远程失败",
                  isinstance(mcp, dict)
                  and any(f.get("name") == "remote" and f.get("error") for f in fails),
                  {"failed": fails})
            block = wait_for(c, lambda m: m.get("t") == "block_start"
                             and m.get("kind") == "system"
                             and "[MCP]" in m.get("text", "")
                             and "remote" in m.get("text", ""), 10)
            check("E/失败 system 块可见", block is not None,
                  (block or {}).get("text", "")[:160])
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "bye", "seq": 1})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("E/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("E/退出码 0", code == 0, code)
    finally:
        c.kill_tree()
    err = "\n".join(c.stderr)
    check("E/stderr 点名不可达服务", "启动服务 remote 失败" in err, err[:240])


def arm_f(core, port):
    """运行期语义：--mcp-env=minimal + 崩溃后按需重启（七版，2026-11-06）。

    同一个真 stdio server 上串起两件事：
      1) dev.crash 让进程真死 ⇒ tool_result 失败 + system 块预告「下次调用会重启」；
      2) 下一次调用 dev.env ⇒ core 先重启服务（system 块「已自动重启」），
         且 minimal 模式下父进程的普通环境变量**读不到**（回包 "env:<>"）。
    """
    c = CoreProc(core, SMOKE / "runtime.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-runtime",
                 extra_args=["--mcp-env=minimal"],
                 extra_env={"SMOKE_LEAK": "must-not-reach-server"})
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("F/hello 到达", hello is not None)
        if hello:
            svcs = (hello.get("mcp") or {}).get("services") or []
            check("F/hello.mcp 报 smoke/4 工具（2 常规 + 2 测试专用）",
                  any(s.get("name") == "smoke" and s.get("tools") == 4
                      and s.get("transport") == "stdio" for s in svcs), {"services": svcs})
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "prompt", "seq": 1, "text": "跑两步：先崩一个，再读环境", "client_id": "smoke-f1"})

        # ── 第 1 轮：dev.crash ─────────────────────────────────────────
        ask1 = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("F/第一次 ask 是崩溃工具",
              ask1 is not None and "dev.crash" in ask1.get("question", ""),
              (ask1 or {}).get("question", "")[:160])
        if ask1:
            c.send({"t": "answer", "seq": 2, "id": ask1.get("id"), "choice": "allow"})

        tr1 = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_result", 15)
        end1 = wait_for(c, lambda m: m.get("t") == "block_end" and m.get("id") == (tr1 or {}).get("id"), 10)
        check("F/崩溃调用 tool_result ok=false", tr1 is not None and (end1 or {}).get("ok") is False,
              {"tr": bool(tr1), "end": end1})

        death = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "system"
                         and "已退出" in m.get("text", ""), 10)
        check("F/崩溃有 system 块且预告重启",
              death is not None and "重启" in death.get("text", ""),
              (death or {}).get("text", "")[:200])

        # ── 第 2 轮：dev.env（先重启，再回读环境）────────────────────
        ask2 = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("F/第二次 ask 是环境回读",
              ask2 is not None and "dev.env" in ask2.get("question", ""),
              (ask2 or {}).get("question", "")[:160])
        if ask2:
            c.send({"t": "answer", "seq": 3, "id": ask2.get("id"), "choice": "allow"})

        tr2 = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "tool_result", 15)
        d2 = wait_for(c, lambda m: m.get("t") == "delta" and m.get("id") == (tr2 or {}).get("id"), 10)
        txt = (d2 or {}).get("text", "")
        check("F/minimal 模式不泄漏普通环境变量（回包 env:<>）", txt == "env:<>", txt[:120])

        restart = wait_for(c, lambda m: m.get("t") == "block_start" and m.get("kind") == "system"
                           and "已自动重启" in m.get("text", ""), 10)
        check("F/重启有 system 块（第 1/3 次）",
              restart is not None and "第 1/3 次" in restart.get("text", ""),
              (restart or {}).get("text", "")[:200])

        idle = wait_for(c, lambda m: m.get("t") == "state" and m.get("value") == "idle", 20)
        check("F/收尾回到 idle", idle is not None)

        c.send({"t": "bye", "seq": 4})
        bye = wait_for(c, lambda m: m.get("t") == "bye", 10)
        check("F/bye 回执", bye is not None)
        code = c.p.wait(timeout=10)
        check("F/退出码 0", code == 0, code)
    finally:
        c.kill_tree()
    err = "\n".join(c.stderr)
    check("F/stderr: 1 个服务 4 个工具", "1 个服务、4 个工具" in err, err[:240])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=str(ROOT / "core" / "bin" / "ecdi-core.exe"))
    ap.add_argument("--deadline", type=float, default=90.0, help="总墙钟上限（秒）")
    args = ap.parse_args()
    core = Path(args.core)
    if not core.exists():
        print("SKIP: core 二进制不存在：%s\n先跑：cd core && go build -o bin/ecdi-core.exe ./cmd/ecdi-core" % core)
        return 2
    http_srv, http_state, http_base = http_server.start()
    # 一个"刚关掉"的本地端口：保证连接被拒，而不是依赖某个固定端口空闲。
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    dead_port = sock.getsockname()[1]
    sock.close()
    dead_url = "http://127.0.0.1:%d" % dead_port
    write_configs(http_base, dead_url)
    if LOG.exists():
        LOG.unlink()
    RESULT.unlink(missing_ok=True)
    timer = threading.Timer(args.deadline, on_deadline, args=(args.deadline,))
    timer.daemon = True
    timer.start()

    llm = FakeLLM()
    srv = ThreadingHTTPServer(("127.0.0.1", 0), llm.handler())
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    # F 臂要自己的剧本（崩溃 → 环境回读 → 收尾），单独一个端点，
    # 免得把 A/C/D 共用的两轮剧本改成状态机。
    llm_f = FakeLLM(script=[
        ("mcp__smoke__dev_crash", {}),
        ("mcp__smoke__dev_env", {"name": "SMOKE_LEAK"}),
    ])
    srv_f = ThreadingHTTPServer(("127.0.0.1", 0), llm_f.handler())
    port_f = srv_f.server_address[1]
    threading.Thread(target=srv_f.serve_forever, daemon=True).start()
    print("core=%s  mtime=%s" % (core, time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(core.stat().st_mtime))))
    try:
        arm_a(core, llm, port)
        arm_b(core, port)
        arm_c(core, llm, port, http_base, http_state)
        arm_d(core, llm, port, http_base, http_state)
        arm_e(core, port, dead_url)
        arm_f(core, port_f)
    finally:
        srv.shutdown()
        srv_f.shutdown()
        http_srv.shutdown()
        timer.cancel()
    summary = {"passed": sum(1 for c in CHECKS if c["ok"]), "failed": len(FAILS), "fails": FAILS,
               "core": str(core), "checks": CHECKS}
    RESULT.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("passed", "failed", "fails")}, ensure_ascii=False))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())