package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync"
	"testing"
	"time"
)

// ── 本地 httptest MCP server ─────────────────────────────────────
//
// 两个测试 server 都实现同一组 JSON-RPC 方法，只差传输：
//   · streamable：POST /mcp，JSON 或 SSE 响应；initialize 给 Mcp-Session-Id。
//   · legacy    ：GET /sse 发 endpoint 事件；POST /messages 回 202；回执走 SSE。
//
// ★ 测试只断言**协议可见面**（方法序列、头、路径、结果），不碰内部字段。

type mcpHTTPRecord struct {
	Method string
	Path   string
	Header http.Header
	Body   jsonrpcRequest
}

type mcpHTTPTestState struct {
	mu      sync.Mutex
	records []mcpHTTPRecord
	deletes int

	requireAuth  string
	sseToolsCall bool
	forceStatus  int // 非 0 ⇒ 所有请求直接回这个状态
	redirect     bool
	sessionID    string
	protocolVer  string
}

func (st *mcpHTTPTestState) record(r *http.Request, req jsonrpcRequest) {
	st.mu.Lock()
	defer st.mu.Unlock()
	st.records = append(st.records, mcpHTTPRecord{
		Method: r.Method, Path: r.URL.Path, Header: r.Header.Clone(), Body: req,
	})
}

func (st *mcpHTTPTestState) snapshot() []mcpHTTPRecord {
	st.mu.Lock()
	defer st.mu.Unlock()
	out := make([]mcpHTTPRecord, len(st.records))
	copy(out, st.records)
	return out
}

func (st *mcpHTTPTestState) rpc(req jsonrpcRequest) (any, *jsonrpcError) {
	switch req.Method {
	case "initialize":
		ver := st.protocolVer
		if ver == "" {
			ver = "2025-06-18"
		}
		res := mcpInitializeResult{ProtocolVersion: ver, Capabilities: map[string]any{"tools": map[string]any{}}}
		res.ServerInfo.Name = "http-mock"
		res.ServerInfo.Version = "1.0"
		return res, nil
	case "notifications/initialized":
		return nil, nil
	case "tools/list":
		return mcpToolsListResult{Tools: []mcpTool{
			{Name: "echo", Description: "echo", InputSchema: map[string]any{"type": "object"}},
			{Name: "sse_tool", Description: "sse", InputSchema: map[string]any{"type": "object"}},
		}}, nil
	case "tools/call":
		params, _ := req.Params.(map[string]any)
		args, _ := params["arguments"].(map[string]any)
		return mcpCallToolResult{Content: []mcpContentItem{
			{Type: "text", Text: "ok: " + mustJSON(args)},
		}}, nil
	default:
		return nil, &jsonrpcError{Code: -32601, Message: "method not found: " + req.Method}
	}
}

func writeJSONRPC(w http.ResponseWriter, id any, result any, rpcErr *jsonrpcError) {
	w.Header().Set("Content-Type", "application/json")
	resp := map[string]any{"jsonrpc": "2.0", "id": id}
	if rpcErr != nil {
		resp["error"] = rpcErr
	} else {
		resp["result"] = result
	}
	_ = json.NewEncoder(w).Encode(resp)
}

// newStreamableTestServer：Streamable HTTP 测试端点。
func newStreamableTestServer(t *testing.T, st *mcpHTTPTestState) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if st.forceStatus != 0 {
			http.Error(w, "forced", st.forceStatus)
			return
		}
		if st.redirect {
			http.Redirect(w, r, "/elsewhere", http.StatusFound)
			return
		}
		if st.requireAuth != "" && r.Header.Get("Authorization") != st.requireAuth {
			http.Error(w, "missing or bad Authorization", http.StatusUnauthorized)
			return
		}
		switch r.Method {
		case http.MethodDelete:
			st.mu.Lock()
			st.deletes++
			st.mu.Unlock()
			w.WriteHeader(http.StatusNoContent)
			return
		case http.MethodPost:
		default:
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}

		body, _ := io.ReadAll(r.Body)
		var req jsonrpcRequest
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "bad json", http.StatusBadRequest)
			return
		}
		st.record(r, req)

		if req.Method == "initialize" {
			if sid := st.sessionID; sid != "" {
				w.Header().Set("Mcp-Session-Id", sid)
			}
		}
		if req.ID == nil {
			w.WriteHeader(http.StatusAccepted)
			return
		}

		result, rpcErr := st.rpc(req)
		if st.sseToolsCall && req.Method == "tools/call" {
			w.Header().Set("Content-Type", "text/event-stream")
			w.WriteHeader(http.StatusOK)
			f, _ := w.(http.Flusher)
			fmt.Fprint(w, ": keepalive\n\n")
			fmt.Fprint(w, "event: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\n\n")
			payload, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": req.ID, "result": result})
			line := "data: " + string(payload) + "\n\n"
			// 故意拆成两次写：SSE 解析必须容忍网络分片。
			_, _ = w.Write([]byte(line[:len(line)/2]))
			if f != nil {
				f.Flush()
			}
			_, _ = w.Write([]byte(line[len(line)/2:]))
			if f != nil {
				f.Flush()
			}
			return
		}
		writeJSONRPC(w, req.ID, result, rpcErr)
	}))
	t.Cleanup(srv.Close)
	return srv
}

// newLegacySSETestServer：legacy HTTP+SSE 测试端点。
func newLegacySSETestServer(t *testing.T, st *mcpHTTPTestState) *httptest.Server {
	t.Helper()
	msgCh := make(chan []byte, 16)
	mux := http.NewServeMux()
	mux.HandleFunc("/sse", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		w.WriteHeader(http.StatusOK)
		f, _ := w.(http.Flusher)
		fmt.Fprint(w, "event: endpoint\ndata: /messages\n\n")
		if f != nil {
			f.Flush()
		}
		for {
			select {
			case msg := <-msgCh:
				fmt.Fprintf(w, "event: message\ndata: %s\n\n", msg)
				if f != nil {
					f.Flush()
				}
			case <-r.Context().Done():
				return
			}
		}
	})
	mux.HandleFunc("/messages", func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		var req jsonrpcRequest
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "bad json", http.StatusBadRequest)
			return
		}
		st.record(r, req)
		w.WriteHeader(http.StatusAccepted)
		if req.ID == nil {
			return
		}
		result, rpcErr := st.rpc(req)
		resp := map[string]any{"jsonrpc": "2.0", "id": req.ID}
		if rpcErr != nil {
			resp["error"] = rpcErr
		} else {
			resp["result"] = result
		}
		raw, _ := json.Marshal(resp)
		msgCh <- raw
	})
	srv := httptest.NewServer(mux)
	t.Cleanup(srv.Close)
	return srv
}

func startHTTPClient(t *testing.T, cfg mcpServerConfig) *mcpClient {
	t.Helper()
	c := newMCPClient("remote", cfg)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := c.startAndInit(ctx, t.TempDir()); err != nil {
		t.Fatalf("startAndInit: %v", err)
	}
	t.Cleanup(c.Close)
	return c
}

// TestMCP_HTTPStreamableJSON：JSON 响应路径 + 会话/版本头 + DELETE。
func TestMCP_HTTPStreamableJSON(t *testing.T) {
	st := &mcpHTTPTestState{sessionID: "sess-1"}
	srv := newStreamableTestServer(t, st)
	c := startHTTPClient(t, mcpServerConfig{URL: srv.URL + "/mcp"})

	if len(c.tools) != 2 {
		t.Fatalf("tools 应为 2，got %d", len(c.tools))
	}
	res, err := c.callTool(context.Background(), "echo", map[string]any{"msg": "hi"})
	if err != nil || !res.OK || !strings.Contains(res.Output, `"msg":"hi"`) {
		t.Fatalf("callTool = %+v, err=%v", res, err)
	}

	recs := st.snapshot()
	if len(recs) < 4 {
		t.Fatalf("请求太少：%d", len(recs))
	}
	if recs[0].Body.Method != "initialize" {
		t.Fatalf("首请求应为 initialize，got %s", recs[0].Body.Method)
	}
	if recs[0].Header.Get("MCP-Protocol-Version") != "" {
		t.Fatalf("initialize 不应带 MCP-Protocol-Version：%q", recs[0].Header.Get("MCP-Protocol-Version"))
	}
	if got := recs[0].Header.Get("Accept"); !strings.Contains(got, "application/json") || !strings.Contains(got, "text/event-stream") {
		t.Fatalf("Accept 不符：%q", got)
	}
	for _, rec := range recs[1:] {
		if rec.Body.Method == "notifications/initialized" {
			continue
		}
		if rec.Header.Get("Mcp-Session-Id") != "sess-1" {
			t.Fatalf("%s 缺 session 头：%v", rec.Body.Method, rec.Header)
		}
		if rec.Header.Get("MCP-Protocol-Version") != "2025-06-18" {
			t.Fatalf("%s 版本头不符：%q", rec.Body.Method, rec.Header.Get("MCP-Protocol-Version"))
		}
	}

	c.Close()
	if !c.isDead() {
		t.Fatalf("Close 后 isDead 应为 true")
	}
	// DELETE 是 best-effort：给服务端一个观察窗口。
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		st.mu.Lock()
		n := st.deletes
		st.mu.Unlock()
		if n > 0 {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatalf("Close 未发 DELETE")
}

// TestMCP_HTTPStreamableSSE：POST 返回 SSE（含 keepalive/notification/网络分片）。
func TestMCP_HTTPStreamableSSE(t *testing.T) {
	st := &mcpHTTPTestState{sseToolsCall: true}
	srv := newStreamableTestServer(t, st)
	c := startHTTPClient(t, mcpServerConfig{URL: srv.URL + "/mcp"})

	res, err := c.callTool(context.Background(), "echo", map[string]any{"x": 1})
	if err != nil || !res.OK || !strings.Contains(res.Output, `"x":1`) {
		t.Fatalf("SSE 回执解析失败：%+v err=%v", res, err)
	}
}

// TestMCP_HTTPAuthError：Authorization 到达服务端；失败信息不泄 header。
func TestMCP_HTTPAuthError(t *testing.T) {
	st := &mcpHTTPTestState{requireAuth: "Bearer secret-token"}
	srv := newStreamableTestServer(t, st)
	c := startHTTPClient(t, mcpServerConfig{
		URL:     srv.URL + "/mcp",
		Headers: map[string]string{"Authorization": "Bearer secret-token"},
	})
	if res, err := c.callTool(context.Background(), "echo", nil); err != nil || !res.OK {
		t.Fatalf("带 auth 的调用应成功：%+v err=%v", res, err)
	}
	recs := st.snapshot()
	if len(recs) == 0 || recs[0].Header.Get("Authorization") != "Bearer secret-token" {
		t.Fatalf("Authorization 未到达服务端")
	}

	bad := &mcpHTTPTestState{requireAuth: "Bearer secret-token"}
	badSrv := newStreamableTestServer(t, bad)
	c2 := newMCPClient("bad", mcpServerConfig{URL: badSrv.URL + "/mcp"})
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	err := c2.startAndInit(ctx, t.TempDir())
	if err == nil {
		t.Fatalf("401 应让握手失败")
	}
	if !strings.Contains(err.Error(), "401") {
		t.Fatalf("错误应带状态码：%v", err)
	}
	if strings.Contains(err.Error(), "secret-token") {
		t.Fatalf("错误泄露了 token：%v", err)
	}
}

// TestMCP_HTTPRedirectRejected：不跟随重定向（Authorization 不跨 host）。
func TestMCP_HTTPRedirectRejected(t *testing.T) {
	st := &mcpHTTPTestState{redirect: true}
	srv := newStreamableTestServer(t, st)
	c := newMCPClient("redir", mcpServerConfig{URL: srv.URL + "/mcp"})
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	err := c.startAndInit(ctx, t.TempDir())
	if err == nil || !strings.Contains(err.Error(), "302") {
		t.Fatalf("应拒绝 302，got %v", err)
	}
}

// TestMCP_LegacySSE：GET endpoint → POST messages → 回执走 SSE。
func TestMCP_LegacySSE(t *testing.T) {
	st := &mcpHTTPTestState{protocolVer: "2024-11-05"}
	srv := newLegacySSETestServer(t, st)
	c := startHTTPClient(t, mcpServerConfig{URL: srv.URL + "/sse", Transport: "sse"})

	if len(c.tools) != 2 {
		t.Fatalf("tools 应为 2，got %d", len(c.tools))
	}
	res, err := c.callTool(context.Background(), "echo", map[string]any{"msg": "legacy"})
	if err != nil || !res.OK || !strings.Contains(res.Output, "legacy") {
		t.Fatalf("legacy callTool = %+v err=%v", res, err)
	}

	recs := st.snapshot()
	posted, initPosted := 0, 0
	for _, rec := range recs {
		if rec.Path == "/messages" {
			posted++
			if rec.Body.Method == "initialize" {
				initPosted++
			}
		}
	}
	if posted == 0 || initPosted != 1 {
		t.Fatalf("legacy 的 initialize 必须 POST 到 /messages：posted=%d init=%d", posted, initPosted)
	}
	if got := c.transport.ProtocolVersion(); got != "2024-11-05" {
		t.Fatalf("legacy 版本应为 2024-11-05，got %s", got)
	}
}

// TestMCP_HTTPStartFailureStatus：连接失败/坏配置必须变成 Failed，而不是空服务。
func TestMCP_HTTPStartFailureStatus(t *testing.T) {
	closed := newStreamableTestServer(t, &mcpHTTPTestState{})
	closedURL := closed.URL
	closed.Close()

	cfg := &mcpConfigFile{MCPServers: map[string]mcpServerConfig{
		"refused": {URL: closedURL + "/mcp"},
		"bad":     {Command: "x", URL: closedURL + "/mcp"},
	}}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("hub: %v", err)
	}
	defer hub.Close()
	st := hub.Status()
	if len(st.Services) != 0 {
		t.Fatalf("不应有可用服务：%+v", st.Services)
	}
	if len(st.Failed) != 2 {
		t.Fatalf("应有 2 条失败：%+v", st.Failed)
	}
	for _, f := range st.Failed {
		if f.Error == "" {
			t.Fatalf("失败原因不能为空：%+v", f)
		}
	}
}

// TestMCP_TransportKindValidation：配置自相矛盾在启动前被挡住。
func TestMCP_TransportKindValidation(t *testing.T) {
	cases := []struct {
		name string
		cfg  mcpServerConfig
		want string
		ok   bool
	}{
		{"stdio", mcpServerConfig{Command: "x"}, "stdio", true},
		{"url default", mcpServerConfig{URL: "https://x/mcp"}, "http", true},
		{"sse", mcpServerConfig{URL: "https://x/sse", Transport: "sse"}, "sse", true},
		{"both", mcpServerConfig{Command: "x", URL: "https://x"}, "", false},
		{"neither", mcpServerConfig{}, "", false},
		{"bad transport", mcpServerConfig{URL: "https://x", Transport: "ws"}, "", false},
		{"bad scheme", mcpServerConfig{URL: "ftp://x"}, "", false},
		{"headers on stdio", mcpServerConfig{Command: "x", Headers: map[string]string{"A": "b"}}, "", false},
		{"args on http", mcpServerConfig{URL: "https://x", Args: []string{"a"}}, "", false},
	}
	for _, tc := range cases {
		got, err := tc.cfg.transportKind()
		if tc.ok {
			if err != nil || got != tc.want {
				t.Fatalf("%s: got (%q,%v), want %q", tc.name, got, err, tc.want)
			}
		} else if err == nil {
			t.Fatalf("%s: 应报错，got %q", tc.name, got)
		}
	}
}

// TestScanSSE：CRLF / 注释 / 多行 data / 无尾空行。
func TestScanSSE(t *testing.T) {
	input := ": comment\r\nevent: message\r\ndata: {\"a\":\r\ndata: 1}\r\n\r\n" +
		"data: tail"
	var events []string
	if err := scanSSE(strings.NewReader(input), func(event, data string) error {
		events = append(events, event+":"+data)
		return nil
	}); err != nil {
		t.Fatalf("scanSSE: %v", err)
	}
	if len(events) != 2 || events[0] != "message:{\"a\":\n1}" || events[1] != "message:tail" {
		t.Fatalf("events = %#v", events)
	}
}

// TestLoopE2E_MCPToolHTTP：Streamable HTTP 走完整 Agent 循环
// （hello.mcp 可见 → ask 门 → 提案卡 → tools/call → 结果回灌 → idle）。
func TestLoopE2E_MCPToolHTTP(t *testing.T) {
	ws := t.TempDir()
	st := &mcpHTTPTestState{}
	mcpSrv := newStreamableTestServer(t, st)
	mcpPath := ws + string(os.PathSeparator) + "mcp.json"
	cfg := fmt.Sprintf(`{"mcpServers":{"remote":{"url":%s}}}`, mustJSON(mcpSrv.URL+"/mcp"))
	if err := os.WriteFile(mcpPath, []byte(cfg), 0644); err != nil {
		t.Fatal(err)
	}

	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "调用远程 MCP", ""),
			deltaChunk("", "", `[{"index":0,"id":"call-http-1","function":{"name":"mcp__remote__echo","arguments":"{\"msg\":\"http-ok\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("HTTP MCP 回执已收到。", "", "")},
			usage: &chatUsage{PromptTokens: 70, CompletionTokens: 20}},
	}
	llm := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, llm.URL, "", ws)
	done := runSession(s)

	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":6,"gui":"test"}`)
	hello := waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	stMap, _ := hello["mcp"].(map[string]any)
	svcs, _ := stMap["services"].([]any)
	if len(svcs) != 1 {
		t.Fatalf("hello.mcp.services 应含 remote：%v", stMap)
	}
	svc, _ := svcs[0].(map[string]any)
	if svc["name"] != "remote" || svc["transport"] != "http" {
		t.Fatalf("服务/传输不符：%v", svc)
	}

	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"请调用远程 echo"}`)
	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	det, _ := ask["detail"].(map[string]any)
	if det["tool"] != "mcp" || det["server"] != "remote" || det["toolName"] != "echo" {
		t.Fatalf("ask detail 不符：%v", det)
	}
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"])))

	waitFor(t, col, "HTTP tool result", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "http-ok")
	})
	waitFor(t, col, "final text", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "HTTP MCP 回执已收到。")
	})
	waitFor(t, col, "idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})

	sendLine(lines, stc, `{"t":"bye"}`)
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("session did not exit after bye")
	}
}

// TestMCP_HTTPCancelNotifiesServer：远程传输的取消也要真的发出去（2026-11-06 七版）。
// ★ 两种传输都必须过：Streamable 的取消是一条通知 POST；legacy 走 messages 端点。
func TestMCP_HTTPCancelNotifiesServer(t *testing.T) {
	cases := []struct {
		name string
		cfg  func(base string) mcpServerConfig
	}{
		{"streamable", func(base string) mcpServerConfig { return mcpServerConfig{URL: base + "/mcp"} }},
		{"legacy-sse", func(base string) mcpServerConfig {
			return mcpServerConfig{URL: base + "/sse", Transport: "sse"}
		}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			st := &mcpHTTPTestState{}
			var url string
			if tc.name == "streamable" {
				url = newStreamableTestServer(t, st).URL
			} else {
				url = newLegacySSETestServer(t, st).URL
			}
			c := startHTTPClient(t, tc.cfg(url))

			callCtx, callCancel := context.WithCancel(context.Background())
			callCancel()
			res, err := c.callTool(callCtx, "echo", map[string]any{"msg": "x"})
			if err != nil {
				t.Fatalf("callTool 不该返回 error（取消是可解释结果）：%v", err)
			}
			if res.OK || !strings.Contains(res.Output, "已向 MCP 服务发送取消通知") {
				t.Fatalf("取消结果文本不符：%+v", res)
			}

			var got *mcpHTTPRecord
			recs := st.snapshot()
			for i := range recs {
				if recs[i].Body.Method == "notifications/cancelled" {
					got = &recs[i]
				}
			}
			if got == nil {
				t.Fatalf("%s 未发出 notifications/cancelled", tc.name)
			}
			params, _ := got.Body.Params.(map[string]any)
			if params == nil || params["requestId"] == nil {
				t.Fatalf("取消通知必须带 requestId：%v", got.Body.Params)
			}
		})
	}
}