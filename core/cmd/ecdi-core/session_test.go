package main

// session_test.go —— 真 core 的**离线 E2E**（P3 规格 §6 A3/A4 的可确定性前身）。
//
// 思路：真 API key 到位之前，用脚本化 SSE 服务器（httptest）扮演 OpenAI 兼容
// 端点，把 agent loop 的协议全链在真 core 代码上跑通并断言。它证明的是
// **core 侧实现**（协议映射 / 状态机 / I3 / seq 连续 / cancel 打断 / 工具全链）；
// 「真实模型的流式形态能否被解析」留给真 API 的 A3/A4 验收——那是不同的问题，
// 不该混在一个判据里（判据格的文本必须限于该 face 自身覆盖，REQ-10 A3 的教训）。
//
// 全部断言都过 wire.Out 的真实出站路径（不是调内部函数看返回值）——
// 「函数对、链路通」是两件事（0c 的 buildTitle 教训）。

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"ecdi-fake-core/internal/wire"
)

// ── 测试基建 ────────────────────────────────────────────────────

// outCollector：并发安全的出站收集器，假装成 stdout。
type outCollector struct {
	mu    sync.Mutex
	lines []string
}

func (c *outCollector) Write(p []byte) (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	for _, l := range strings.Split(string(p), "\n") {
		if strings.TrimSpace(l) != "" {
			c.lines = append(c.lines, l)
		}
	}
	return len(p), nil
}

func (c *outCollector) all() []string {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]string(nil), c.lines...)
}

func (c *outCollector) count() int {
	c.mu.Lock()
	defer c.mu.Unlock()
	return len(c.lines)
}

// waitFor：轮询直到 collector 出现满足 cond 的行（超时 fail）。
func waitFor(t *testing.T, c *outCollector, desc string, cond func(map[string]any) bool) map[string]any {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		for _, l := range c.all() {
			var m map[string]any
			if json.Unmarshal([]byte(l), &m) != nil {
				continue
			}
			if cond(m) {
				return m
			}
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("等待 %s 超时；已收 %d 行：\n%s", desc, c.count(), strings.Join(c.all(), "\n"))
	return nil
}

// newTestSession：建一个完整 session（真实出站路径），返回 session 与收集器。
// stc 与 in 通道由调用方驱动（模拟 GUI 侧）；进程 cwd 切到临时工作区。
func newTestSession(t *testing.T, serverURL, allowTools, wsRoot string) (*session, *outCollector, chan wire.StampedLine, *atomic.Int32) {
	t.Helper()
	if wsRoot == "" {
		wsRoot = t.TempDir()
	}
	old, _ := os.Getwd()
	if err := os.Chdir(wsRoot); err != nil {
		t.Fatalf("chdir: %v", err)
	}
	t.Cleanup(func() { _ = os.Chdir(old) })

	col := &outCollector{}
	lines := make(chan wire.StampedLine, 64)
	var stc atomic.Int32
	stc.Store(wire.CodeIdle)

	sess := newSession(
		llmConfig{BaseURL: serverURL, Model: "test-model", Key: "test-key-123"},
		strings.Split(allowTools, ","), nil, lines, &stc,
		func(string, ...any) {}, col)
	return sess, col, lines, &stc
}

// runSession：run() 在 goroutine 里跑，bye/EOF 后自然退出。
func runSession(s *session) chan struct{} {
	done := make(chan struct{})
	go func() {
		_ = s.run()
		close(done)
	}()
	return done
}

func sendLine(ch chan wire.StampedLine, stc *atomic.Int32, raw string) {
	ch <- wire.StampedLine{Text: raw, St: stc.Load()}
}

func mustNormalize(t *testing.T, p string) string {
	t.Helper()
	n, err := wire.NormalizeWorkspace(p)
	if err != nil {
		t.Fatalf("normalize %q: %v", p, err)
	}
	return n
}

// ── 协议级断言 ──────────────────────────────────────────────────

func parseAll(t *testing.T, c *outCollector) []map[string]any {
	t.Helper()
	var out []map[string]any
	for _, l := range c.all() {
		var m map[string]any
		if err := json.Unmarshal([]byte(l), &m); err != nil {
			t.Fatalf("出站行不是合法 JSON: %v\n%s", err, l)
		}
		out = append(out, m)
	}
	return out
}

// assertSeqContinuous：I4 的 core 侧——seq 严格递增且连续。
func assertSeqContinuous(t *testing.T, msgs []map[string]any) {
	t.Helper()
	want := 0.0
	for i, m := range msgs {
		seq, ok := m["seq"].(float64)
		if !ok {
			t.Fatalf("第 %d 行缺 seq: %v", i, m)
		}
		if seq != want {
			t.Fatalf("I4 违规：第 %d 行 seq=%v，期望 %v（缺口/乱序）", i, seq, want)
		}
		want = seq + 1
	}
}

// assertBlockPairs：I1——每个块 start/end 各恰一次且 end 在 start 后。
func assertBlockPairs(t *testing.T, msgs []map[string]any) {
	t.Helper()
	open := map[string]bool{}
	for i, m := range msgs {
		switch m["t"] {
		case "block_start":
			id, _ := m["id"].(string)
			if id == "" {
				t.Fatalf("第 %d 行 block_start 缺 id: %v", i, m)
			}
			if open[id] {
				t.Fatalf("I1 违规：块 %s 二次 start", id)
			}
			open[id] = true
		case "block_end":
			id, _ := m["id"].(string)
			if !open[id] {
				t.Fatalf("I1 违规：块 %s 的 end 无 start（第 %d 行）", id, i)
			}
			delete(open, id)
		}
	}
	if len(open) > 0 {
		t.Fatalf("I1 违规：流结束仍有未关闭块 %v", open)
	}
}

// ── 脚本化 SSE 服务器 ───────────────────────────────────────────

// sseScript = 一轮 /chat/completions 的脚本（一个请求消费一个元素）。
type sseScript struct {
	chunks []string   // 原样写出的 data: 行载荷（[DONE] 自动补）
	usage  *chatUsage // 非 nil 时末尾补一个带 usage 的空 choices 块（include_usage 形态）
}

// newScriptedServer：逐请求消费脚本；记录每个请求体供断言；
// Authorization 头必须带 key（密钥不进日志 ≠ 密钥不进请求）。
func newScriptedServer(t *testing.T, key string, scripts []sseScript, recorded *[][]byte) *httptest.Server {
	t.Helper()
	mu := sync.Mutex{}
	idx := 0
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/chat/completions" {
			http.Error(w, "not found", http.StatusNotFound)
			return
		}
		if got := r.Header.Get("Authorization"); got != "Bearer "+key {
			http.Error(w, "bad auth", http.StatusUnauthorized)
			return
		}
		body, _ := io.ReadAll(r.Body)
		mu.Lock()
		*recorded = append(*recorded, body)
		i := idx
		idx++
		mu.Unlock()
		if i >= len(scripts) {
			http.Error(w, "script exhausted", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		fl := w.(http.Flusher)
		for _, c := range scripts[i].chunks {
			fmt.Fprintf(w, "data: %s\n\n", c)
			fl.Flush()
		}
		if scripts[i].usage != nil {
			u := scripts[i].usage
			fmt.Fprintf(w, "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d}}\n\n",
				u.PromptTokens, u.CompletionTokens)
			fl.Flush()
		}
		fmt.Fprint(w, "data: [DONE]\n\n")
		fl.Flush()
	}))
	t.Cleanup(srv.Close)
	return srv
}

// deltaChunk：构造一个 choices.delta 载荷（content / reasoning / tool_calls 任选）。
func deltaChunk(content, reasoning, tools string) string {
	var b strings.Builder
	b.WriteString(`{"choices":[{"delta":{`)
	first := true
	add := func(s string) {
		if !first {
			b.WriteString(",")
		}
		first = false
		b.WriteString(s)
	}
	if content != "" {
		add(fmt.Sprintf(`"content":%s`, mustJSON(content)))
	}
	if reasoning != "" {
		add(fmt.Sprintf(`"reasoning_content":%s`, mustJSON(reasoning)))
	}
	if tools != "" {
		add(`"tool_calls":` + tools)
	}
	b.WriteString(`}}]}`)
	return b.String()
}

func mustJSON(v any) string {
	b, _ := json.Marshal(v)
	return string(b)
}

// ── E2E：思维链 + 正文 + 工具全链（A3/A4 的离线前身）────────────

func TestLoopE2E_FullChain(t *testing.T) {
	// 工作区里放一个真文件，工具要真的读到它
	ws := t.TempDir()
	if err := os.WriteFile(ws+"/notes.md", []byte("工作区里的内容：hello-p3"), 0o644); err != nil {
		t.Fatal(err)
	}

	var recorded [][]byte
	scripts := []sseScript{
		{ // 第 1 轮：思维链 + 正文 + 发起一次 read_file（参数故意撕成两片）
			chunks: []string{
				deltaChunk("", "先看文件再回答", ""),
				deltaChunk("我需要读取 notes.md。", "", ""),
				deltaChunk("", "", `[{"index":0,"id":"call-1","function":{"name":"read_file","arguments":"{\"path\":"}}]`),
				deltaChunk("", "", `[{"index":0,"function":{"arguments":"\"notes.md\"}"}}]`),
			},
		},
		{ // 第 2 轮：拿到工具结果后收尾（usage 真值随末尾块到达）
			chunks: []string{deltaChunk("文件内容是 hello-p3。", "", "")},
			usage:  &chatUsage{PromptTokens: 120, CompletionTokens: 45},
		},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)

	// 握手
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	hello := waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	if hello["protocol"].(float64) != 4 {
		t.Fatalf("hello.protocol = %v", hello["protocol"])
	}
	if hello["context"] != nil {
		t.Fatalf("未提供 --context 时 hello 不得发 context（真值纪律），收到 %v", hello["context"])
	}

	// 发 prompt（带 client_id）
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"看看 notes.md"}`)

	// ask 出现（默认全 ask：read_file 不在豁免名单）
	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	if ask["kind"] != "permission" {
		t.Fatalf("ask.kind = %v", ask["kind"])
	}
	if q := ask["question"].(string); !strings.Contains(q, "read_file") {
		t.Fatalf("ask.question 应含工具名，got %q", q)
	}
	askID := ask["id"].(string)

	// ★ I3 断言：ask 后、answer 前不得再有任何消息（state=waiting 是 ask 的
	//   伴随，必须在 ask 之后同批到达）。给 120ms 无消息窗口。
	time.Sleep(120 * time.Millisecond)
	msgs := parseAll(t, col)
	for i, m := range msgs {
		if m["t"] == "ask" {
			if i+1 >= len(msgs) || msgs[i+1]["t"] != "state" || msgs[i+1]["value"] != "waiting" {
				t.Fatalf("I3 机检形态违规：ask 后未紧跟 state=waiting（第 %d 行起）", i)
			}
			if i+2 < len(msgs) {
				t.Fatalf("I3 违规：ask/waiting 之后、answer 之前出现了 %v", msgs[i+2])
			}
			break
		}
	}

	// 回答允许
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(askID)))

	// 工具结果真的包含文件内容（真执行，不是剧本）
	waitFor(t, col, "tool_result with file content", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "hello-p3")
	})
	// 第 2 轮的最终文本
	waitFor(t, col, "final assistant text", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "文件内容是 hello-p3")
	})
	// usage（API 真值；API 没给就不发——这里给了就必须发对）
	usage := waitFor(t, col, "usage", func(m map[string]any) bool { return m["t"] == "usage" })
	if usage["input"].(float64) != 120 || usage["output"].(float64) != 45 {
		t.Fatalf("usage 应为 API 真值 120/45，got %v", usage)
	}
	// state 回 idle
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})

	// bye 收尾
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done
	waitFor(t, col, "bye", func(m map[string]any) bool { return m["t"] == "bye" })

	// ── 全流结构断言 ──
	msgs = parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)

	// user 回显必须带 client_id（I9）
	foundEcho := false
	for _, m := range msgs {
		if m["t"] == "block_start" && m["kind"] == "user" {
			if m["client_id"] != "p1" {
				t.Fatalf("user 回显 client_id = %v，期望 p1", m["client_id"])
			}
			foundEcho = true
		}
	}
	if !foundEcho {
		t.Fatalf("未见 kind:user 回显块")
	}

	// reasoning 块存在且先于 assistant 块
	reasoningSeen, assistantSeen := false, false
	for _, m := range msgs {
		if m["t"] != "block_start" {
			continue
		}
		switch m["kind"] {
		case "reasoning":
			if assistantSeen {
				t.Fatalf("reasoning 块出现在 assistant 块之后")
			}
			reasoningSeen = true
		case "assistant":
			assistantSeen = true
		}
	}
	if !reasoningSeen || !assistantSeen {
		t.Fatalf("reasoning=%v assistant=%v", reasoningSeen, assistantSeen)
	}

	// 恰好 2 次 LLM 请求；第 2 轮必须带 role:tool 的真结果（模型拿到的 = GUI 看到的）
	if len(recorded) != 2 {
		t.Fatalf("应恰好 2 次 LLM 请求，got %d", len(recorded))
	}
	var req2 struct {
		Messages []chatMessage `json:"messages"`
	}
	if err := json.Unmarshal(recorded[1], &req2); err != nil {
		t.Fatal(err)
	}
	toolMsgSeen := false
	for _, m := range req2.Messages {
		if m.Role == "tool" && strings.Contains(m.Content, "hello-p3") {
			toolMsgSeen = true
		}
	}
	if !toolMsgSeen {
		t.Fatalf("第 2 轮请求缺 role:tool 的真结果消息: %s", recorded[1])
	}

	// 第 1 个请求必须带 tools 定义（三件）+ 正确 model
	var req1 struct {
		Tools []toolDef `json:"tools"`
		Model string    `json:"model"`
	}
	if err := json.Unmarshal(recorded[0], &req1); err != nil {
		t.Fatal(err)
	}
	if len(req1.Tools) != 3 {
		t.Fatalf("tools 定义应 3 件，got %d", len(req1.Tools))
	}
	if req1.Model != "test-model" {
		t.Fatalf("model = %q", req1.Model)
	}
}

// ── E2E：deny 路径（拒绝 ⇒ 不执行、模型收到拒绝说明）────────────

func TestLoopE2E_Deny(t *testing.T) {
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-9","function":{"name":"read_file","arguments":"{\"path\":\"secret.txt\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("好的，不读了。", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", "")
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"读 secret.txt"}`)

	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"deny"}`, mustJSON(ask["id"].(string))))

	waitFor(t, col, "deny summary", func(m map[string]any) bool {
		return m["t"] == "block_end" && m["summary"] == "用户拒绝"
	})
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

	// 模型收到的 tool 消息 = 拒绝说明
	var req2 struct {
		Messages []chatMessage `json:"messages"`
	}
	if err := json.Unmarshal(recorded[1], &req2); err != nil {
		t.Fatal(err)
	}
	deniedToModel := false
	for _, m := range req2.Messages {
		if m.Role == "tool" && strings.Contains(m.Content, "用户拒绝") {
			deniedToModel = true
		}
	}
	if !deniedToModel {
		t.Fatalf("deny 后模型未收到拒绝说明")
	}
	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)
}

// ── E2E：allow-tools 豁免（运行者配置 ⇒ 不问直接执行）────────────

func TestLoopE2E_AllowToolSkipsAsk(t *testing.T) {
	ws := t.TempDir()
	if err := os.WriteFile(ws+"/a.txt", []byte("data-allowed"), 0o644); err != nil {
		t.Fatal(err)
	}
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"c1","function":{"name":"read_file","arguments":"{\"path\":\"a.txt\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("done", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "read_file", ws)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"读 a.txt"}`)

	waitFor(t, col, "tool result", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "data-allowed")
	})
	waitFor(t, col, "idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":2}`)
	<-done

	// 全流不得出现 ask（豁免名单生效）
	for _, m := range parseAll(t, col) {
		if m["t"] == "ask" {
			t.Fatalf("read_file 在豁免名单内，却出现了 ask")
		}
	}
}

// ── E2E：cancel 打断正在进行的流（A6 的离线前身）────────────────

func TestLoopE2E_CancelMidStream(t *testing.T) {
	block := make(chan struct{}) // 服务器卡住流，模拟「模型还在生成」
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		fmt.Fprintf(w, "data: %s\n\n", deltaChunk("第一段。", "", ""))
		w.(http.Flusher).Flush()
		<-block // 挂住：直到测试放行（客户端应先 cancel 断开）
	}))
	t.Cleanup(func() { close(block); srv.Close() })

	s, col, lines, stc := newTestSession(t, srv.URL, "", "")
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"长任务"}`)
	waitFor(t, col, "first delta", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "第一段")
	})

	// cancel：必须打断挂着的流（不是等它跑完）
	sendLine(lines, stc, `{"t":"cancel","seq":2}`)
	waitFor(t, col, "state cancelled", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "cancelled"
	})
	waitFor(t, col, "state idle after cancel", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})

	// 未闭合块必须被 closeAll(false) 收口（I1）
	msgs := parseAll(t, col)
	assertBlockPairs(t, msgs)
	assertSeqContinuous(t, msgs)

	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done
}

// ── E2E：轮内抢跑 prompt 必须被拒绝（到达态判据）────────────────

func TestLoopE2E_PromptDuringTurnRejected(t *testing.T) {
	block := make(chan struct{})
	var once sync.Once
	release := func() { once.Do(func() { close(block) }) }
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		fmt.Fprintf(w, "data: %s\n\n", deltaChunk("第一段。", "", ""))
		w.(http.Flusher).Flush()
		<-block
	}))
	t.Cleanup(func() { release(); srv.Close() })

	s, col, lines, stc := newTestSession(t, srv.URL, "", "")
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"第一轮"}`)
	waitFor(t, col, "first delta", func(m map[string]any) bool { return m["t"] == "delta" })

	// 轮内抢跑的 prompt：listener 收走，轮后按到达态处置 ⇒ 回带 client_id 的 error
	sendLine(lines, stc, `{"t":"prompt","seq":2,"client_id":"p2","text":"抢跑"}`)
	release() // 放行：轮结束后 deferred 里的抢跑 prompt 才会被处置
	waitFor(t, col, "rejection error", func(m map[string]any) bool {
		return m["t"] == "error" && m["client_id"] == "p2"
	})

	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done
	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)
}

// ── E2E：工作区切换（idle 受理 + 规范化回显 + 工具切到新根）──────

func TestLoopE2E_WorkspaceSwitch(t *testing.T) {
	wsOld := t.TempDir()
	wsNew := t.TempDir()
	if err := os.WriteFile(wsNew+"/in-new.txt", []byte("new-root"), 0o644); err != nil {
		t.Fatal(err)
	}

	var recorded [][]byte
	scripts := []sseScript{
		{ // 切换后那一轮：读新根下的文件（脚本 0）
			chunks: []string{
				deltaChunk("", "", `[{"index":0,"id":"c1","function":{"name":"read_file","arguments":"{\"path\":\"in-new.txt\"}"}}]`),
			},
		},
		{chunks: []string{deltaChunk("ok", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "read_file", wsOld)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })

	sendLine(lines, stc, fmt.Sprintf(`{"t":"workspace","seq":1,"request_id":"w1","path":%s}`, mustJSON(wsNew)))
	wsAck := waitFor(t, col, "workspace ack", func(m map[string]any) bool {
		return m["t"] == "workspace" && m["request_id"] == "w1"
	})
	if wsAck["ok"] != true {
		t.Fatalf("workspace ack 应成功: %v", wsAck)
	}
	if wsAck["path"].(string) != mustNormalize(t, wsNew) {
		t.Fatalf("workspace ack path = %v，期望规范化后的 %s", wsAck["path"], mustNormalize(t, wsNew))
	}

	// 切换后一轮：read_file 解析以**新根**为基准
	sendLine(lines, stc, `{"t":"prompt","seq":2,"client_id":"p2","text":"读 in-new.txt"}`)
	waitFor(t, col, "tool result from new root", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "new-root")
	})
	waitFor(t, col, "idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done
	assertSeqContinuous(t, parseAll(t, col))
}

// ── 单元：parseArgs / summarizeArgs ────────────────────────────

func TestParseArgsFallback(t *testing.T) {
	if m := parseArgs(`{"path":"a.txt"}`); m["path"] != "a.txt" {
		t.Fatalf("合法 JSON 应解析成 map: %v", m)
	}
	if m := parseArgs(`{"path": broken`); m["_raw"] == nil {
		t.Fatalf("非法 JSON 应回退 _raw: %v", m)
	}
	if m := parseArgs(""); len(m) != 0 {
		t.Fatalf("空参数应为空 map: %v", m)
	}
}

func TestSummarizeArgsTruncates(t *testing.T) {
	long := strings.Repeat("x", 500)
	got := summarizeArgs(long)
	// ★ 判据按 rune 数（省略号「…」占 3 字节，按字节判会把正确行为判 RED）
	if n := len([]rune(got)); n > 201 {
		t.Fatalf("摘要应截断到 ~200 rune: %d", n)
	}
}
