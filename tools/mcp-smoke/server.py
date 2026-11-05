import sys, json
# 极简 MCP server：initialize / notifications/initialized / tools/list(分页) / tools/call
def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

tools_p1 = [{"name": "git.status", "description": "Show working tree status",
            "inputSchema": {"type": "object", "properties": {"short": {"type": "boolean"}}}}]
tools_p2 = [{"name": "读取文件", "description": "读一个文件", "inputSchema": {"type": "object"}}]

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
        send({"jsonrpc": "2.0", "id": rid, "result": {"content": [{"type": "text", "text": "ok: " + json.dumps((req.get("params") or {}).get("arguments"))}]}})
