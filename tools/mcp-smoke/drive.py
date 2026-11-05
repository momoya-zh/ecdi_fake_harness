#!/usr/bin/env python
# ============================================================
# 真链路 MCP 冒烟：真 core 二进制 + 真 stdio MCP server + 脚本化 SSE 端点
# ------------------------------------------------------------
# 覆盖 Go 单测证明不了的那一段：真子进程握手 / 真分页 tools/list /
# 真 function.name 压缩 / 真 ask 门 / 真 tools/call 往返 / bye 后整树清空。
#
# 两个臂：
#   A（正常，mcp.json 由本脚本生成）：
#       1 个服务 / 2 个工具（跨两页）→ 模型发起 mcp__smoke__git_status
#       → ask（detail 必须缺席）→ allow → 真调用 → tool_result 回模型 → 收尾 idle
#   B（降级，mixed.json）：
#       不存在的命令 + 正常服务 ⇒ 坏服务只点名、好服务照常注册、caps 仍带 mcp
#
# 判据纪律（与 regress.py 同口径）：
#   * 不靠目视：每条断言都机读，失败即退出码 1，明细落 result.json。
#   * 前置缺失（core 二进制 / python）是 SKIP（退出码 2），不是 PASS。
#   * 模型端点是**脚本化 SSE**（不是真厂商端点）；server 是仓库自带 mock；
#     不起 GUI ⇒ GUI 渲染不在本套覆盖范围内。别把这份读数当 GUI 验收。
#
# 用法：
#   go build -o bin/ecdi-core.exe ./cmd/ecdi-core   （在 core/ 下）
#   python tools/mcp-smoke/drive.py [--core <path>]
#
# 产物（gitignored）：.scratch/mcp-smoke/{mcp.json,mixed.json,fake-llm.log,result.json}
# ============================================================
import argparse
import json
import os
import re
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

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

FAILS = []
CHECKS = []


def check(name, ok, detail=""):
    CHECKS.append({"check": name, "ok": bool(ok), "detail": str(detail)[:400]})
    if not ok:
        FAILS.append(name)
    print(("PASS " if ok else "FAIL ") + name + ("  | " + str(detail)[:200] if detail else ""))
    return ok


# ── 脚本化 OpenAI 兼容端点（第 1 轮发 tool_call，第 2 轮收尾）────────
class FakeLLM:
    def __init__(self):
        self.mcp_names = []
        self.round = 0
        self.lock = threading.Lock()

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
                if not has_tool_msg:
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
    def __init__(self, core, cfg, url, session):
        self.lines = []
        self.stderr = []
        self.q = []
        self.qc = threading.Condition()
        self.p = subprocess.Popen(
            [str(core), "--base-url", url, "--model", "smoke", "--key", "smoke-key",
             "--mcp-config", str(cfg), "--workspace", str(SMOKE), "--session", session,
             "--context", "160000"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(SMOKE))
        threading.Thread(target=self._read_out, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

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


def write_configs():
    SMOKE.mkdir(parents=True, exist_ok=True)
    good = {"mcpServers": {"smoke": {"command": sys.executable, "args": [str(SERVER)]}}}
    mixed = {"mcpServers": {
        "broken": {"command": "no-such-mcp-server.exe"},
        "smoke": {"command": sys.executable, "args": [str(SERVER)]},
    }}
    (SMOKE / "mcp.json").write_text(json.dumps(good, ensure_ascii=False), encoding="utf-8")
    (SMOKE / "mixed.json").write_text(json.dumps(mixed, ensure_ascii=False), encoding="utf-8")
    return SMOKE / "mcp.json", SMOKE / "mixed.json"


def arm_a(core, llm, port):
    c = CoreProc(core, SMOKE / "mcp.json", "http://127.0.0.1:%d/v1" % port, "s-smoke-1")
    try:
        hello = wait_for(c, lambda m: m.get("t") == "hello", 10)
        check("A/hello 到达", hello is not None)
        if hello:
            check("A/hello.protocol == 6", hello.get("protocol") == 6, hello.get("protocol"))
            check("A/caps 含 mcp", "mcp" in (hello.get("caps") or []), hello.get("caps"))
        c.send({"t": "hello", "seq": 0})
        c.send({"t": "prompt", "seq": 1, "text": "请查看 git 状态", "client_id": "smoke-c1"})

        ask = wait_for(c, lambda m: m.get("t") == "ask", 20)
        check("A/ask 门被触发（外部工具默认过门）", ask is not None)
        if ask:
            q = ask.get("question", "")
            check("A/问句带服务与工具", "MCP 工具" in q and "smoke" in q and "git_status" in q, q)
            check("A/ask 不带 detail（判别式护栏）", "detail" not in ask, list(ask.keys()))
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=str(ROOT / "core" / "bin" / "ecdi-core.exe"))
    args = ap.parse_args()
    core = Path(args.core)
    if not core.exists():
        print("SKIP: core 二进制不存在：%s\n先跑：cd core && go build -o bin/ecdi-core.exe ./cmd/ecdi-core" % core)
        return 2
    write_configs()
    if LOG.exists():
        LOG.unlink()

    llm = FakeLLM()
    srv = ThreadingHTTPServer(("127.0.0.1", 0), llm.handler())
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print("core=%s  mtime=%s" % (core, time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(core.stat().st_mtime))))
    try:
        arm_a(core, llm, port)
        arm_b(core, port)
    finally:
        srv.shutdown()
    summary = {"passed": sum(1 for c in CHECKS if c["ok"]), "failed": len(FAILS), "fails": FAILS,
               "core": str(core), "checks": CHECKS}
    RESULT.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("passed", "failed", "fails")}, ensure_ascii=False))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())