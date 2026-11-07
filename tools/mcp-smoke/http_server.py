#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""MCP 远程传输冒烟 server（2026-11-06）。

一个进程同时提供两种传输，供 drive.py 的 HTTP 臂使用：
  · POST /mcp      —— Streamable HTTP（initialize 用 2025-06-18；tools/call 走 SSE 响应）
  · GET  /sse      —— legacy HTTP+SSE（endpoint 事件 + /messages POST + SSE 回执）
  · GET  /mcp      —— Streamable 的服务端主动通知流（2025-06-18；推 tools/list_changed）
  · DELETE /mcp    —— Streamable 的会话终止（204）

★ 它会**校验**客户端必须带对的头（session / protocol version），错了回 400：
  这样「客户端只是碰巧能跑」不会混过去。所有断言仍由 drive.py 做。
"""
import json
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

TOOLS = [
    {"name": "git.status", "description": "Show working tree status",
     "inputSchema": {"type": "object", "properties": {"short": {"type": "boolean"}}}},
    {"name": "读取文件", "description": "读一个文件", "inputSchema": {"type": "object"}},
]

# ★ 2026-11-07（G 臂）：tools/list_changed 重拉后的「新工具表」——通知推送前
#   tools/list 返回 TOOLS，推送后返回 TOOLS + EXTRA（drive 据此断言重拉真兑现）。
EXTRA_TOOL = {"name": "cloud.ping", "description": "Ping the cloud",
              "inputSchema": {"type": "object"}}

STREAMABLE_SESSION = "smoke-http-session-1"


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.requests = []          # (path, method, headers, body)
        self.deletes = 0
        # ★ 必须在 GET /sse 之前建好：否则 POST /messages 可能在 GET handler
        #   赋值前先到一个新队列，回执写进没人读的队列，客户端握手超时。
        self.legacy_queue = queue.Queue()
        self.legacy_ready = threading.Event()
        self.protocol_errors = []
        self.session_errors = []
        # ── G 臂（2026-11-07）：通知流与断流自愈 ──────────────────
        # ★ notify_push_enabled 默认关：C/D 臂的状态必须与六版行为逐位一致
        #   （GET /mcp 只挂心跳、不推通知）——一个 server 实例服务多个臂时，
        #   任何无条件的状态翻转都会跨臂泄漏（实测：C 的 tools 2→3、D 2→4）。
        self.notify_push_enabled = False
        self.first_list_done = threading.Event()  # 首次 tools/list 完成（推送时序锚）
        self.list_changed_sent = False            # GET 流上推过 tools/list_changed
        self.notify_stream_opens = 0              # GET /mcp 次数
        self.last_notify_lei = None               # 最后一次 GET /mcp 的 Last-Event-ID
        self.tools_list_calls = 0                 # tools/list 被调用次数（首拉+重拉）
        self.sse_gets = 0                         # GET /sse 次数
        self.sse_last_lei = None                  # 最后一次 GET /sse 的 Last-Event-ID
        self.sse_drop_armed = False               # True ⇒ 下一「首次」legacy 连接发完通知就断
        self.sse_drop_consumed = False

    def record(self, path, method, headers, body):
        with self.lock:
            self.requests.append((path, method, dict(headers), body))


def rpc_result(req, state=None):
    """返回 (result, error) —— 只实现冒烟需要的方法。state 非 None 时提供 G 臂语义。"""
    method = req.get("method")
    if method == "initialize":
        # legacy 调用方会把版本改写成 2024-11-05
        return {"protocolVersion": "2025-06-18",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "smoke-http", "version": "0.1"}}, None
    if method == "tools/list":
        if state is not None:
            with state.lock:
                state.tools_list_calls += 1
                changed = state.list_changed_sent
            state.first_list_done.set()
        else:
            changed = False
        # 通知推送后工具表多一项（drive 据此断言「重拉真兑现」）。
        return {"tools": TOOLS + ([EXTRA_TOOL] if changed else [])}, None
    if method == "tools/call":
        args = (req.get("params") or {}).get("arguments") or {}
        return {"content": [{"type": "text", "text": "ok: " + json.dumps(args)}]}, None
    return None, {"code": -32601, "message": "method not found: %s" % method}


def rpc_response(req, result, error):
    out = {"jsonrpc": "2.0", "id": req.get("id")}
    if error:
        out["error"] = error
    else:
        out["result"] = result
    return out


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    state = None

    def log_message(self, *args):
        pass

    # ── Streamable HTTP ──────────────────────────────────────
    def _streamable(self):
        if self.path != "/mcp":
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        try:
            req = json.loads(raw.decode("utf-8"))
        except Exception:
            self.send_error(400, "bad json")
            return
        self.state.record("/mcp", self.command, self.headers, req)
        method = req.get("method")
        is_init = method == "initialize"

        if not is_init:
            if self.headers.get("Mcp-Session-Id") != STREAMABLE_SESSION:
                with self.state.lock:
                    self.state.session_errors.append(method)
                self.send_error(400, "missing/bad Mcp-Session-Id")
                return
            if self.headers.get("MCP-Protocol-Version") != "2025-06-18":
                with self.state.lock:
                    self.state.protocol_errors.append(method)
                self.send_error(400, "missing/bad MCP-Protocol-Version")
                return
        accept = self.headers.get("Accept", "")
        if "text/event-stream" not in accept or "application/json" not in accept:
            self.send_error(400, "bad Accept")
            return

        if req.get("id") is None:
            self.send_response(202)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        result, error = rpc_result(req, self.state)
        if method == "tools/call":
            payload = json.dumps(rpc_response(req, result, error), ensure_ascii=False)
            body = ("event: message\n"
                    "data: " + payload + "\n\n").encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.send_header("Content-Length", str(len(body)))
            if is_init:
                self.send_header("Mcp-Session-Id", STREAMABLE_SESSION)
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()
            self.close_connection = True
            return

        body = json.dumps(rpc_response(req, result, error), ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        if is_init:
            self.send_header("Mcp-Session-Id", STREAMABLE_SESSION)
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()

    # ── Streamable HTTP 的服务端主动通知流（2025-06-18，G 臂）──────────
    def _streamable_get(self):
        with self.state.lock:
            self.state.notify_stream_opens += 1
            self.state.last_notify_lei = self.headers.get("Last-Event-ID") or None
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        # ★ 首字节立即下发（SSE 心跳注释）：头部之后体部长时间静音会触发客户端
        #   响应体检测的竞速（实测 1/3 概率推送永不可见）——先冲一条注释把体部
        #   「点亮」，再等推送时序。
        try:
            self.wfile.write(b": ping\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            return
        if self.state.notify_push_enabled:
            # ★ 先等首次 tools/list 完成再推 list_changed——否则推送与首拉竞速，
            #   drive 的「首拉 2 个工具」前提不稳。推完后 tools/list 升级为 3 项。
            self.state.first_list_done.wait(timeout=5)
            try:
                self.wfile.write(b"data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}\n\n")
                self.wfile.flush()
                with self.state.lock:
                    self.state.list_changed_sent = True
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
        # 保持打开（1s 心跳）；客户端断开（会话结束）⇒ 写失败退出。
        while True:
            time.sleep(1)
            try:
                self.wfile.write(b": ping\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                return

    # ── legacy HTTP+SSE ──────────────────────────────────────
    def _legacy_sse(self):
        with self.state.lock:
            self.state.sse_gets += 1
            self.state.sse_last_lei = self.headers.get("Last-Event-ID") or None
            drop_this = self.state.sse_drop_armed and not self.state.sse_drop_consumed
            if drop_this:
                self.state.sse_drop_consumed = True
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(b"event: endpoint\ndata: /messages\n\n")
        self.wfile.flush()
        self.state.legacy_ready.set()
        if drop_this:
            # G 臂断流场景：推一条带 id 的通知，然后**真正断开**——客户端必须
            # 带 Last-Event-ID: g1 重连（drive 断言），会话不得判死。
            # ★ close_connection 必须显式：HTTP/1.1 keep-alive 下 handler return
            #   只会回到「读下一条请求」循环，客户端还挂着流等事件 ⇒ 双方对峙，
            #   客户端永远看不到 EOF（实测：重连永不触发、判据全假红）。
            try:
                self.wfile.write(b"id: g1\nevent: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\"}\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
            return
        q = self.state.legacy_queue
        while True:
            try:
                payload = q.get(timeout=30)
            except Exception:
                return
            if payload is None:
                return
            try:
                self.wfile.write(b"event: message\ndata: " + payload + b"\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                return

    def _legacy_message(self):
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        try:
            req = json.loads(raw.decode("utf-8"))
        except Exception:
            self.send_error(400, "bad json")
            return
        self.state.record("/messages", self.command, self.headers, req)
        self.send_response(202)
        self.send_header("Content-Length", "0")
        self.end_headers()
        if req.get("id") is None:
            return
        result, error = rpc_result(req, self.state)
        if req.get("method") == "initialize":
            result["protocolVersion"] = "2024-11-05"
        payload = json.dumps(rpc_response(req, result, error), ensure_ascii=False).encode("utf-8")
        self.state.legacy_queue.put(payload)

    def do_POST(self):
        if self.path == "/messages":
            self._legacy_message()
        elif self.path == "/mcp":
            self._streamable()
        else:
            self.send_error(404)

    def do_GET(self):
        if self.path == "/sse":
            self._legacy_sse()
        elif self.path == "/mcp":
            self._streamable_get()
        else:
            self.send_error(405)

    def do_DELETE(self):
        if self.path == "/mcp":
            with self.state.lock:
                self.state.deletes += 1
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self.send_error(404)


def start(state=None):
    """启动 server 线程，返回 (server, state, base_url)。"""
    state = state or State()
    handler = type("BoundHandler", (Handler,), {"state": state})
    srv = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    srv.daemon_threads = True
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    host, port = srv.server_address
    return srv, state, "http://%s:%d" % (host, port)