import os, sys, json
# 极简 MCP server：initialize / notifications/initialized / tools/list(分页) / tools/call
#
# SMOKE_EXTRA_TOOLS=1 时多出两个测试专用工具（F 臂：按需重启 / env minimal）：
#   dev.crash —— 直接退出进程（模拟服务崩溃，无回执无告别）
#   dev.env   —— 回读一个环境变量（回包固定 "env:<值>"，空值也能和错误区分）
def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

tools_p1 = [{"name": "git.status", "description": "Show working tree status",
            "inputSchema": {"type": "object", "properties": {"short": {"type": "boolean"}}}}]
tools_p2 = [{"name": "读取文件", "description": "读一个文件", "inputSchema": {"type": "object"}}]

if os.environ.get("SMOKE_EXTRA_TOOLS") == "1":
    tools_p1.append({"name": "dev.crash", "description": "Exit the server process",
                     "inputSchema": {"type": "object"}})
    tools_p2.append({"name": "dev.env", "description": "Read one environment variable",
                     "inputSchema": {"type": "object",
                                     "properties": {"name": {"type": "string"}}}})

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    req = json.loads(line)
    m = req.get("method")
    rid = req.get("id")
    if m == "initialize":
        send({"jsonrpc": "2.0", "id": rid, "result": {
            "protocolVersion": "2025-06-18",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "smoke", "version": "0.1"}}})
    elif m == "tools/list":
        cur = (req.get("params") or {}).get("cursor")
        if cur is None:
            send({"jsonrpc": "2.0", "id": rid, "result": {"tools": tools_p1, "nextCursor": "p2"}})
        else:
            send({"jsonrpc": "2.0", "id": rid, "result": {"tools": tools_p2}})
    elif m == "tools/call":
        params = req.get("params") or {}
        name = params.get("name")
        args = params.get("arguments") or {}
        if name == "dev.crash":
            # 崩溃必须够真：不回错误、不回空结果，进程直接消失。
            os._exit(7)
        if name == "dev.env":
            val = os.environ.get(str(args.get("name", "")), "")
            send({"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": "env:<%s>" % val}]}})
            continue
        send({"jsonrpc": "2.0", "id": rid, "result": {"content": [{"type": "text", "text": "ok: " + json.dumps(args)}]}})
