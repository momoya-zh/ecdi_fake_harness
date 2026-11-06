#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""MCP 远程传输冒烟 server（2026-11-06）。

一个进程同时提供两种传输，供 drive.py 的 HTTP 臂使用：
  · POST /mcp      —— Streamable HTTP（initialize 用 2025-06-18；tools/call 走 SSE 响应）
  · GET  /sse      —— legacy HTTP+SSE（endpoint 事件 + /messages POST + SSE 回执）
  · DELETE /mcp    —— Streamable 的会话终止（204）

★ 它会**校验**客户端必须带对的头（session / protocol version），错了回 400：
  这样「客户端只是碰巧能跑」不会混过去。所有断言仍由 drive.py 做。
"""
import json
import queue
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

TOOLS = [
    {"name": "git.status", "description": "Show working tree status",
     "inputSchema": {"type": "object", "properties": {"short": {"type": "boolean"}}}},
    {"name": "读取文件", "description": "读一个文件", "inputSchema": {"type": "object"}},
]

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

    def record(self, path, method, headers, body):
        with self.lock:
            self.requests.append((path, method, dict(headers), body))


def rpc_result(req):
    """返回 (result, error) —— 只实现冒烟需要的四个方法。"""
    method = req.get("method")
    if method == "initialize":
        # legacy 调用方会把版本改写成 2024-11-05
        return {"protocolVersion": "2025-06-18",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "smoke-http", "version": "0.1"}}, None
    if method == "tools/list":
        return {"tools": TOOLS}, None
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

        result, error = rpc_result(req)
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

    # ── legacy HTTP+SSE ──────────────────────────────────────
    def _legacy_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(b"event: endpoint\ndata: /messages\n\n")
        self.wfile.flush()
        self.state.legacy_ready.set()
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
        result, error = rpc_result(req)
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