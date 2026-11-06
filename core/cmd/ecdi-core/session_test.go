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
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"ecdi-fake-core/internal/contextlog"
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

// isolateMCPDiscovery：让被测 session **不可能**读到「运行这台机器的人」的全局
// mcp.json。
//
// ★ 为什么必须（2026-11-05 评审）：MCP 配置的三级回退里最后一级读
//
//	`%APPDATA%\ecdi-fake-harness\mcp.json`（非 Windows 读 `$HOME/.config/...`），
//	而所有 session 测试都传 `mcpConfig=""` + 临时 workspace ⇒ 只要开发机上放了
//	这一份全局配置，`go test` 就会在每个 session 测试里**拉起真的 MCP 服务**
//	（最坏 5s/服务 × N）。测试必须密闭，不能依赖跑测试的人没配过 MCP。
func isolateMCPDiscovery(t *testing.T) {
	t.Helper()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("HOME", dir)
	t.Setenv("USERPROFILE", dir)
}

// newTestSession：建一个完整 session（真实出站路径），返回 session 与收集器。
// stc 与 in 通道由调用方驱动（模拟 GUI 侧）；进程 cwd 切到临时工作区。
func newTestSession(t *testing.T, serverURL, allowTools, wsRoot string) (*session, *outCollector, chan wire.StampedLine, *atomic.Int32) {
	t.Helper()
	return newTestSessionCfg(t, llmConfig{
		BaseURL: serverURL, Model: "test-model", Key: "test-key-123",
	}, allowTools, wsRoot)
}

// newTestSessionCfg：同上，但允许测试指定 Context/MaxOutput（§7.3 防爆测试用）。
func newTestSessionCfg(t *testing.T, cfg llmConfig, allowTools, wsRoot string) (*session, *outCollector, chan wire.StampedLine, *atomic.Int32) {
	t.Helper()
	isolateMCPDiscovery(t)
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
		cfg,
		strings.Split(allowTools, ","), nil, "", "s-test-1", "", "", false, agentOpts{},
		"", 0,
		lines, &stc,
		func(string, ...any) {}, col)
	return sess, col, lines, &stc
}

// newTestSessionAgents：开着子代理的同一套基建（--agents 的对照臂）。
func newTestSessionAgents(t *testing.T, serverURL, wsRoot string, ag agentOpts) (*session, *outCollector, chan wire.StampedLine, *atomic.Int32) {
	t.Helper()
	isolateMCPDiscovery(t)
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
		[]string{"read_file", "list_dir", "grep"}, nil, "", "s-test-1", "", "", false, ag,
		"", 0,
		lines, &stc, func(string, ...any) {}, col)
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
	// ★ v6（2026-11-05）：hello.protocol = 6（新增控制面消息 t:"job"）。
	//   本用例用 agentOpts{}（关闭）⇒ caps 里**不该**有 "agents"；"jobs" 恒在。
	if hello["protocol"].(float64) != 6 {
		t.Fatalf("hello.protocol = %v", hello["protocol"])
	}
	hasJobs := false
	for _, c := range hello["caps"].([]any) {
		if c == "agents" {
			t.Fatalf("--agents 关闭时 caps 不得声明 agents：%v", hello["caps"])
		}
		if c == "jobs" {
			hasJobs = true
		}
	}
	if !hasJobs {
		t.Fatalf("caps 必须声明 jobs（协议 §3.1）：%v", hello["caps"])
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

	// 第 1 个请求必须带 tools 定义（六件）+ 正确 model
	var req1 struct {
		Tools []toolDef `json:"tools"`
		Model string    `json:"model"`
	}
	if err := json.Unmarshal(recorded[0], &req1); err != nil {
		t.Fatal(err)
	}
	// ★ 2026-11-05：6 → 9（v6 后台作业三件上线）。这里断"件数"是为了抓
	//   「tools 没进请求体」这类整块丢失；每件的 schema 由 toolDefs 的单测盖。
	if len(req1.Tools) != 9 {
		t.Fatalf("tools 定义应 9 件，got %d", len(req1.Tools))
	}
	if req1.Model != "test-model" {
		t.Fatalf("model = %q", req1.Model)
	}
}

// ── E2E：deny 路径（拒绝 ⇒ 不执行、模型收到拒绝说明）────────────

// ── E2E：后台作业（协议 v6）─────────────────────────────────────
//
// 走真进程（echo）：证明「ask → job start → tool_result → job_output 不过门 →
// job end」整条链在会话里真的接通，而不只是 jobHub 单测。
func TestLoopE2E_BackgroundJob(t *testing.T) {
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{deltaChunk("", "", `[{"index":0,"id":"call-bg","function":{"name":"run_command","arguments":"{\"command\":\"echo hello-bg\",\"background\":true}"}}]`)}},
		{chunks: []string{deltaChunk("", "", `[{"index":0,"id":"call-o1","function":{"name":"job_output","arguments":"{\"job_id\":\"j1\",\"wait_seconds\":5}"}}]`)}},
		// ★ 第三轮用 `job_list` 而不是 `job_kill`（2026-11-05 修 flaky）：
		//   `job_output` 的语义是「有输出就回」，不是「等进程退出」——它返回时 echo
		//   常常还在跑，于是紧跟的 `job_kill` 会把作业杀成 `killed`，与「echo 应以
		//   exited/0 正常退出」的断言直接对撞。整包跑时实测约 2/3 概率红、单跑全绿。
		//   `job_list` 同样是「控制工具不过 ask 门」的同一类证据（断言 asks==1 仍成立），
		//   而 `job_kill` 的语义已由 jobs_test.go 的 `toolKill` 单测覆盖。
		{chunks: []string{deltaChunk("", "", `[{"index":0,"id":"call-l1","function":{"name":"job_list","arguments":"{}"}}]`)}},
		{chunks: []string{deltaChunk("后台任务已经结束。", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", "")
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":6,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"后台跑一下 echo"}`)

	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	if q := ask["question"].(string); !strings.Contains(q, "后台") {
		t.Fatalf("后台命令的 ask 应明说“后台”：%q", q)
	}
	detail, _ := ask["detail"].(map[string]any)
	if detail["background"] != true {
		t.Fatalf("detail 应带 background=true：%v", detail)
	}
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))

	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	// ★ 先等作业**自己**结束再 bye（2026-11-05 修 flaky）：
	//   `bye` 会 `jobs.shutdown()` 杀掉还在跑的作业（I14 要求补一条 end），
	//   于是「echo 应以 exited/0 结束」这条断言与「idle 后立刻 bye」构成竞态 ——
	//   整包跑时 echo 慢一点就被杀成 `killed`，实测约 2/3 概率红（单跑 3/3 绿）。
	//   断言要的是「正常退出」，那就等它退出；bye 只负责收尾。
	waitFor(t, col, "job end", func(m map[string]any) bool {
		return m["t"] == "job" && m["phase"] == "end"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	select {
	case <-done:
	case <-time.After(20 * time.Second):
		t.Fatal("会话未在 20s 内结束")
	}
	waitFor(t, col, "bye", func(m map[string]any) bool { return m["t"] == "bye" })
	msgs := parseAll(t, col)

	// 1) job_output / job_kill 不过门 ⇒ 全程只有 1 次 ask
	asks := 0
	for _, m := range msgs {
		if m["t"] == "ask" {
			asks++
		}
	}
	if asks != 1 {
		t.Fatalf("job 控制工具不该再问，ask 应 1 次，got %d", asks)
	}

	// 2) 生命周期：start 一次、end 一次，start 挂到 run_command 的 tool_call
	var start, end map[string]any
	for _, m := range msgs {
		if m["t"] != "job" {
			continue
		}
		switch m["phase"] {
		case "start":
			if start != nil {
				t.Fatalf("重复 start：%v", m)
			}
			start = m
		case "end":
			if end != nil {
				t.Fatalf("重复 end：%v", m)
			}
			end = m
		}
	}
	if start == nil || end == nil {
		t.Fatalf("缺 start/end：start=%v end=%v", start, end)
	}
	callID := ""
	for _, m := range msgs {
		if m["t"] == "block_start" && m["kind"] == "tool_call" && m["name"] == "run_command" {
			callID, _ = m["id"].(string)
		}
	}
	if callID == "" || start["call"] != callID {
		t.Fatalf("start.call=%v 应等于 run_command 的块 id %q", start["call"], callID)
	}
	if end["status"] != "exited" || end["exitCode"].(float64) != 0 {
		t.Fatalf("echo 应正常退出：%v", end)
	}

	// 3) 输出真的到过：某条消息里有 hello-bg
	found := false
	for _, m := range msgs {
		if m["t"] == "delta" && strings.Contains(m["text"].(string), "hello-bg") {
			found = true
		}
	}
	if !found {
		t.Fatalf("没有任何消息携带 echo 的输出 hello-bg")
	}

	// 4) I3：ask 之后到 state=running 之前不得出现 job 消息
	askIdx := -1
	for i, m := range msgs {
		if m["t"] == "ask" {
			askIdx = i
			break
		}
	}
	runningIdx := -1
	for i := askIdx + 1; i < len(msgs); i++ {
		if msgs[i]["t"] == "state" && msgs[i]["value"] == "running" {
			runningIdx = i
			break
		}
	}
	if runningIdx < 0 {
		t.Fatal("ask 之后没有 state=running")
	}
	for i := askIdx + 1; i < runningIdx; i++ {
		if msgs[i]["t"] == "job" {
			t.Fatalf("I3 违规：ask 与 running 之间出现 job 消息：%v", msgs[i])
		}
	}
}

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

// ── E2E：写工具的 ask 全链（2026-11-04）────────────────────────
//
// 这两条判的是「能不能真干活」的最小闭环：模型发起 write_file → 人看见**写成什么**
// → 允许则磁盘真的变、拒绝则**一个字节都不动**。只读三件的 E2E 证明不了这条，
// 因为它们的失败模式是"没读到"，写工具的失败模式是"静默改错/静默没改"。

func TestLoopE2E_WriteToolAskAllowLandsOnDisk(t *testing.T) {
	ws := t.TempDir()
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-w1","function":{"name":"write_file","arguments":"{\"path\":\"out/hello.txt\",\"content\":\"written by agent\\n\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("写好了。", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"写个文件"}`)

	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	q := ask["question"].(string)
	if !strings.Contains(q, "out/hello.txt") || !strings.Contains(q, "允许写入") {
		t.Fatalf("ask 问句没把「写哪个文件」说出来：%q", q)
	}
	// 默认拒绝是重要性质：模型不能靠"人没看清"改成写
	if ask["default"] != "deny" {
		t.Fatalf("ask.default = %v，期望 deny", ask["default"])
	}
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))

	waitFor(t, col, "write result", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "已新建")
	})
	got, err := os.ReadFile(filepath.Join(ws, "out", "hello.txt"))
	if err != nil {
		t.Fatalf("允许之后文件没落盘：%v", err)
	}
	if string(got) != "written by agent\n" {
		t.Fatalf("落盘内容不符：%q", string(got))
	}

	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

	// 模型拿到的 = GUI 看到的（同一份结果文本）
	var req2 struct {
		Messages []chatMessage `json:"messages"`
	}
	if err := json.Unmarshal(recorded[1], &req2); err != nil {
		t.Fatal(err)
	}
	seen := false
	for _, m := range req2.Messages {
		if m.Role == "tool" && strings.Contains(m.Content, "已新建") {
			seen = true
		}
	}
	if !seen {
		t.Fatalf("模型未收到写文件结果")
	}
	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)
}

func TestLoopE2E_WriteToolDeniedLeavesNoTrace(t *testing.T) {
	ws := t.TempDir()
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-w2","function":{"name":"write_file","arguments":"{\"path\":\"forbidden.txt\",\"content\":\"nope\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("好，不写了。", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"写个文件"}`)

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

	if _, err := os.Stat(filepath.Join(ws, "forbidden.txt")); err == nil {
		t.Fatalf("拒绝后文件仍然被创建了 —— ask 门没在写之前生效")
	}
	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)
}

// ── E2E：ask 的 detail 提案（2026-11-05，协议 v5 §3.6 可选字段）────
//
// ★ 问句文本被截断是这批要修的洞，所以测试盯的是 **detail 的内容**：
//   write_file 的 diff 旧侧必须来自**批准前**的磁盘内容；run_command 必须
//   给完整命令（不截断）与作用域。没有 detail 的回落路径由既有测试覆盖
//   （全部旧 E2E 的 ask 都没有 detail 字段，它们绿 = 回落未破）。

func TestLoopE2E_AskDetailProposal(t *testing.T) {
	ws := t.TempDir()
	// 预置旧文件：write_file 的 diff 若不是以批准前的磁盘内容为旧侧，
	// 下面对 "-old line" 的断言当场红。
	if err := os.WriteFile(filepath.Join(ws, "note.txt"), []byte("old line\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	var recorded [][]byte
	scripts := []sseScript{
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-w1","function":{"name":"write_file","arguments":"{\"path\":\"note.txt\",\"content\":\"new line\\n\"}"}}]`),
		}},
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-e1","function":{"name":"edit_file","arguments":"{\"path\":\"note.txt\",\"old_string\":\"new line\",\"new_string\":\"newer line\"}"}}]`),
		}},
		{chunks: []string{
			deltaChunk("", "", `[{"index":0,"id":"call-r1","function":{"name":"run_command","arguments":"{\"command\":\"echo proposal-ok\"}"}}]`),
		}},
		{chunks: []string{deltaChunk("done", "", "")}},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":5,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"改文件跑命令"}`)

	// 三次 ask 依次来（同轮内串行）；waitFor 每次从头扫，必须按 id 去重
	//（agent_test.go 的 nextAsk 同款）。
	seen := map[string]bool{}
	askDetailOf := func(name string) map[string]any {
		ask := nextAsk(t, col, seen)
		sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":99,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))
		d, ok := ask["detail"].(map[string]any)
		if !ok {
			t.Fatalf("%s 的 ask 缺 detail（或非对象）：%v", name, ask)
		}
		return d
	}

	// ① write_file：diff 旧侧 = 批准前的磁盘内容（覆盖场景，不是新文件）
	d := askDetailOf("write_file")
	if d["tool"] != "write_file" || d["path"] != "note.txt" {
		t.Fatalf("write_file detail 判别不符：%v", d)
	}
	diffW, _ := d["diff"].(string)
	if !strings.Contains(diffW, "-old line") || !strings.Contains(diffW, "+new line") {
		t.Fatalf("write_file diff 应含旧/新行（旧侧=批准前磁盘），got:\n%s", diffW)
	}
	if !strings.Contains(diffW, "--- a/note.txt") {
		t.Fatalf("diff 应带路径头，got:\n%s", diffW)
	}

	// ② edit_file：替换片段的 diff
	d = askDetailOf("edit_file")
	if d["tool"] != "edit_file" {
		t.Fatalf("edit_file detail 判别不符：%v", d)
	}
	diffE, _ := d["diff"].(string)
	if !strings.Contains(diffE, "-new line") || !strings.Contains(diffE, "+newer line") {
		t.Fatalf("edit_file diff 应为替换片段，got:\n%s", diffE)
	}
	if _, has := d["replaceAll"]; has {
		t.Fatalf("未传 replace_all 时不应带该字段：%v", d)
	}

	// ③ run_command：完整命令（不截断）+ 作用域 + 环境剥离说明
	d = askDetailOf("run_command")
	if d["tool"] != "run_command" || d["command"] != "echo proposal-ok" {
		t.Fatalf("run_command detail 判别/命令不符：%v", d)
	}
	if d["cwd"] != filepath.ToSlash(ws) {
		t.Fatalf("cwd 应为工作区根（%v），got %v", filepath.ToSlash(ws), d["cwd"])
	}
	if tv, ok := d["timeoutSeconds"].(float64); !ok || int(tv) != 60 {
		t.Fatalf("timeoutSeconds 缺省应为 60，got %v", d["timeoutSeconds"])
	}
	note, _ := d["note"].(string)
	if !strings.Contains(note, "ECDI_CORE_KEY") {
		t.Fatalf("note 应说明密钥剔除，got %q", note)
	}
	// 提案卡行不折行 ⇒ 单行 note 会被裁切；core 必须发两行（GUI 逐行建行）。
	if strings.Count(note, "\n") != 1 {
		t.Fatalf("note 必须是两行（提案卡不折行），got %q", note)
	}

	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

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

// ── L3 记忆文件测试 ─────────────────────────────────────────────

func TestMemory_LoadingAndPromptInjection(t *testing.T) {
	isolateMCPDiscovery(t)
	tmpDir := t.TempDir()
	wsMemDir := filepath.Join(tmpDir, ".ecdi", "memory")
	if err := os.MkdirAll(wsMemDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(wsMemDir, "guidelines.md"), []byte("Always reply in poem"), 0644); err != nil {
		t.Fatal(err)
	}

	col := &outCollector{}
	lines := make(chan wire.StampedLine, 64)
	var stc atomic.Int32
	stc.Store(wire.CodeIdle)

	sess := newSession(
		llmConfig{BaseURL: "http://127.0.0.1:9", Model: "test-model", Key: "k", Context: 10000},
		nil, nil, "", "s-test-mem-1", tmpDir, "", false, agentOpts{}, "", 0, lines, &stc,
		func(string, ...any) {}, col)
	t.Cleanup(func() {
		if sess.ctxWriter != nil {
			_ = sess.ctxWriter.Close()
		}
	})

	// 1. 验证 hello 消息包含 memories 列表
	msgs := parseAll(t, col)
	if len(msgs) == 0 || msgs[0]["t"] != "hello" {
		t.Fatalf("first msg must be hello: %v", msgs)
	}
	mems, ok := msgs[0]["memories"].([]any)
	if !ok || len(mems) != 1 {
		t.Fatalf("expected memories in hello to have 1 item: %v", msgs[0]["memories"])
	}
	mem0 := mems[0].(map[string]any)
	if mem0["name"] != "guidelines.md" || mem0["source"] != "workspace" {
		t.Fatalf("unexpected memory item in hello: %v", mem0)
	}

	// 2. 验证 buildRequestMessages 包含了记忆区段
	reqMsgs := sess.buildRequestMessages()
	if len(reqMsgs) == 0 || reqMsgs[0].Role != "system" {
		t.Fatalf("first msg must be system: %v", reqMsgs)
	}
	sysContent := reqMsgs[0].Content
	if !strings.Contains(sysContent, "## 记忆 (Memory)") || !strings.Contains(sysContent, "Always reply in poem") {
		t.Fatalf("system prompt does not contain memory section: %s", sysContent)
	}
}

func TestMemory_ResumeNotice(t *testing.T) {
	isolateMCPDiscovery(t)
	tmpDir := t.TempDir()
	wsMemDir := filepath.Join(tmpDir, ".ecdi", "memory")
	if err := os.MkdirAll(wsMemDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(wsMemDir, "test.md"), []byte("Mem content"), 0644); err != nil {
		t.Fatal(err)
	}

	col := &outCollector{}
	lines := make(chan wire.StampedLine, 64)
	var stc atomic.Int32
	stc.Store(wire.CodeIdle)

	sess := newSession(
		llmConfig{BaseURL: "http://127.0.0.1:9", Model: "test-model", Key: "k"},
		nil, nil, "s-resume-1", "s-resume-1", tmpDir, "", false, agentOpts{}, "", 0, lines, &stc,
		func(string, ...any) {}, col)
	t.Cleanup(func() {
		if sess.ctxWriter != nil {
			_ = sess.ctxWriter.Close()
		}
	})

	msgs := parseAll(t, col)
	var sawResumeNotice bool
	for _, m := range msgs {
		if m["t"] == "block_start" && m["kind"] == "system" && m["text"] == "[记忆已更新]" {
			sawResumeNotice = true
			break
		}
	}
	if !sawResumeNotice {
		t.Fatalf("expected [记忆已更新] notice on resume with memory, but got: %v", msgs)
	}
}

// ── §7.3：轮内防爆（Stage 1 折叠 / Stage 2 压缩）───────────────

// TestLoopE2E_ContextPruningMidTurn：单轮连续读大文件。
// 判据：任何一次请求都没有超过 provider 的硬窗口（服务端超过 100KB 就回
// 400 context_length_exceeded），且折叠提示可见、usage 仍是 API 真值。
func TestLoopE2E_ContextPruningMidTurn(t *testing.T) {
	ws := t.TempDir()
	if err := os.WriteFile(filepath.Join(ws, "big.txt"), []byte(strings.Repeat("A", 8192)), 0o644); err != nil {
		t.Fatal(err)
	}

	const rounds = 14
	var mu sync.Mutex
	var bodies [][]byte
	reqNo := 0
	var lastUsage chatUsage
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		mu.Lock()
		bodies = append(bodies, body)
		n := reqNo
		reqNo++
		mu.Unlock()

		var req struct {
			Messages  []chatMessage     `json:"messages"`
			Tools     []json.RawMessage `json:"tools"`
			MaxTokens int               `json:"max_tokens"`
		}
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "bad body", http.StatusBadRequest)
			return
		}
		// 模拟 provider 的真实硬窗口：请求里未折叠的工具载荷超过 100KB 就 400。
		payload := 0
		for _, m := range req.Messages {
			if m.Role == "tool" {
				payload += len(m.Content)
			}
			for _, tc := range m.ToolCalls {
				payload += len(tc.Function.Arguments)
			}
		}
		if payload > 100<<10 {
			w.Header().Set("Content-Type", "application/json")
			w.WriteHeader(http.StatusBadRequest)
			fmt.Fprint(w, `{"error":{"message":"context_length_exceeded","code":"context_length_exceeded"}}`)
			return
		}

		u := chatUsage{PromptTokens: 1200 + n*500, CompletionTokens: 40}
		mu.Lock()
		lastUsage = u
		mu.Unlock()

		w.Header().Set("Content-Type", "text/event-stream")
		fl := w.(http.Flusher)
		if n < rounds {
			calls := fmt.Sprintf(
				`[{"index":0,"id":"call-%d","function":{"name":"read_file","arguments":"{\"path\":\"big.txt\"}"}}]`, n)
			fmt.Fprintf(w, "data: %s\n\n", deltaChunk("", "", calls))
		} else {
			fmt.Fprintf(w, "data: %s\n\n", deltaChunk("全部读完了。", "", ""))
		}
		fmt.Fprintf(w, "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d}}\n\n",
			u.PromptTokens, u.CompletionTokens)
		fmt.Fprint(w, "data: [DONE]\n\n")
		fl.Flush()
	}))
	t.Cleanup(srv.Close)

	s, col, lines, stc := newTestSessionCfg(t, llmConfig{
		BaseURL: srv.URL, Model: "test-model", Key: "test-key-123",
		Context: 24000, MaxOutput: 1000,
	}, "read_file", ws)
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":3,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })

	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"把 big.txt 多读几遍再总结"}`)
	waitFor(t, col, "final assistant", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "全部读完了")
	})
	waitFor(t, col, "idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":2}`)
	<-done

	all := parseAll(t, col)
	for _, m := range all {
		if m["t"] == "error" {
			t.Fatalf("不应出现错误（说明请求超窗了）：%v", m)
		}
	}
	foundFold := false
	for _, m := range all {
		if m["t"] == "block_start" && m["kind"] == "system" &&
			strings.Contains(m["text"].(string), "已折叠") {
			foundFold = true
		}
	}
	if !foundFold {
		t.Fatalf("应出现「已折叠」system notice；收到 %v", all)
	}

	mu.Lock()
	gotBodies := append([][]byte(nil), bodies...)
	gotLastUsage := lastUsage
	mu.Unlock()
	if len(gotBodies) != rounds+1 {
		t.Fatalf("应恰好 %d 次 LLM 请求，got %d", rounds+1, len(gotBodies))
	}
	for i, b := range gotBodies {
		var req struct {
			Messages []chatMessage `json:"messages"`
		}
		if err := json.Unmarshal(b, &req); err != nil {
			t.Fatalf("第 %d 个请求体解析失败: %v", i, err)
		}
		payload := 0
		for _, m := range req.Messages {
			if m.Role == "tool" {
				payload += len(m.Content)
			}
			for _, tc := range m.ToolCalls {
				payload += len(tc.Function.Arguments)
			}
		}
		if payload > 100<<10 {
			t.Fatalf("第 %d 个请求超窗：工具载荷 %d 字节", i, payload)
		}
	}

	// 最后一个请求里必须真的出现了折叠占位（不是只发了 notice），
	// 且 tool_call 与 tool 结果一一配对。
	var lastReq struct {
		Messages []chatMessage `json:"messages"`
	}
	if err := json.Unmarshal(gotBodies[len(gotBodies)-1], &lastReq); err != nil {
		t.Fatal(err)
	}
	calls := map[string]bool{}
	tools := map[string]bool{}
	folded := false
	for _, m := range lastReq.Messages {
		if strings.Contains(m.Content, "[历史工具输出已折叠") {
			folded = true
		}
		if m.Role == "tool" {
			tools[m.ToolCallID] = true
		}
		for _, tc := range m.ToolCalls {
			calls[tc.ID] = true
		}
	}
	if !folded {
		t.Fatalf("最后一个请求里没有折叠占位：%s", gotBodies[len(gotBodies)-1])
	}
	for id := range calls {
		if !tools[id] {
			t.Fatalf("tool_call %s 没有配对的 tool 结果", id)
		}
	}
	for id := range tools {
		if !calls[id] {
			t.Fatalf("tool 结果 %s 没有配对的 tool_call", id)
		}
	}

	// D1：usage 仍是最后一次 API 返回值，不是估算。
	usageMsg := waitFor(t, col, "usage", func(m map[string]any) bool { return m["t"] == "usage" })
	if int(usageMsg["input"].(float64)) != gotLastUsage.PromptTokens ||
		int(usageMsg["output"].(float64)) != gotLastUsage.CompletionTokens {
		t.Fatalf("usage 必须是 API 真值 %d/%d，got %v",
			gotLastUsage.PromptTokens, gotLastUsage.CompletionTokens, usageMsg)
	}
}

// TestCompressContext_StaleOccupancyDoesNotReject：复现旧 bug——
// 压缩成功后仍拿压缩前的 occupancy 判 100%，会把本轮直接拒掉。
// 同时验证：50% 假读数没有被写回 lastUsage，compact 落盘，history 首条是摘要。
func TestCompressContext_StaleOccupancyDoesNotReject(t *testing.T) {
	const ctxCap = 8000
	const mainPromptTokens = 4321
	const mainCompletion = 55

	var mu sync.Mutex
	summaryCalls, mainCalls := 0, 0
	var mainBody []byte
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		var req struct {
			Messages []chatMessage     `json:"messages"`
			Tools    []json.RawMessage `json:"tools"`
		}
		if err := json.Unmarshal(body, &req); err != nil {
			http.Error(w, "bad body", http.StatusBadRequest)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		fl := w.(http.Flusher)
		mu.Lock()
		if len(req.Tools) == 0 {
			summaryCalls++
		} else {
			mainCalls++
			mainBody = append([]byte(nil), body...)
		}
		mu.Unlock()
		if len(req.Tools) == 0 {
			fmt.Fprintf(w, "data: %s\n\n", deltaChunk("用户目标是测试压缩；前两轮已完成读取。", "", ""))
			fmt.Fprint(w, "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":900,\"completion_tokens\":20}}\n\n")
		} else {
			fmt.Fprintf(w, "data: %s\n\n", deltaChunk("已压缩并继续。", "", ""))
			fmt.Fprintf(w, "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d}}\n\n",
				mainPromptTokens, mainCompletion)
		}
		fmt.Fprint(w, "data: [DONE]\n\n")
		fl.Flush()
	}))
	t.Cleanup(srv.Close)

	ws := t.TempDir()
	sessDir := filepath.Join(ws, ".ecdi", "sessions")
	if err := os.MkdirAll(sessDir, 0755); err != nil {
		t.Fatal(err)
	}

	s, col, _, _ := newTestSessionCfg(t, llmConfig{
		BaseURL: srv.URL, Model: "test-model", Key: "test-key-123",
		Context: ctxCap, MaxOutput: 500,
	}, "", ws)
	t.Cleanup(func() {
		if s.ctxWriter != nil {
			_ = s.ctxWriter.Close()
		}
	})

	// 3 轮，每轮 10KB tool 结果：原文 ≈ 9k token，压缩后只剩最后 1 轮 + 摘要。
	big := strings.Repeat("Z", 10240)
	for turn := 0; turn < 3; turn++ {
		callID := fmt.Sprintf("c%d", turn)
		s.history = append(s.history,
			chatMessage{Role: "user", Content: fmt.Sprintf("第 %d 轮目标", turn)},
			chatMessage{Role: "assistant", ToolCalls: []apiToolCall{{
				ID: callID, Type: "function",
				Function: funcSpec{Name: "read_file", Arguments: `{"path":"a.txt"}`},
			}}},
			chatMessage{Role: "tool", ToolCallID: callID, Content: big},
			chatMessage{Role: "assistant", Content: fmt.Sprintf("第 %d 轮完成", turn)},
		)
	}
	// 旧实现会把这个值当 occupancy：8000 >= 8000 ⇒ 压缩成功后仍报 100% 拒绝。
	s.lastUsage = &chatUsage{PromptTokens: ctxCap, CompletionTokens: 0}
	origLen := len(s.history)

	done := make(chan struct{})
	go func() {
		s.handlePrompt("继续", "p1")
		close(done)
	}()
	waitFor(t, col, "idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	<-done

	mu.Lock()
	gotSummaryCalls, gotMainCalls := summaryCalls, mainCalls
	gotMainBody := append([]byte(nil), mainBody...)
	mu.Unlock()

	all := parseAll(t, col)
	for _, m := range all {
		if m["t"] == "error" {
			t.Fatalf("压缩成功后不应再报错（旧 bug：拿压缩前 occupancy 判 100%%）：%v", m)
		}
	}
	if gotSummaryCalls != 1 {
		t.Fatalf("应恰好 1 次摘要请求，got %d", gotSummaryCalls)
	}
	if gotMainCalls != 1 {
		t.Fatalf("压缩后主请求必须发出，got %d 次", gotMainCalls)
	}
	foundCompressed := false
	for _, m := range all {
		if m["t"] == "block_start" && m["kind"] == "system" &&
			strings.Contains(m["text"].(string), "上下文已压缩") {
			foundCompressed = true
		}
	}
	if !foundCompressed {
		t.Fatalf("应出现「上下文已压缩」system notice；收到 %v", all)
	}
	if len(s.history) >= origLen {
		t.Fatalf("压缩后 history 应变短：%d -> %d", origLen, len(s.history))
	}
	if len(s.history) == 0 || !strings.HasPrefix(s.history[0].Content, contextlog.SummaryPrefix) {
		t.Fatalf("history[0] 应是摘要消息，got %+v", s.history)
	}
	if !strings.Contains(string(gotMainBody), "以下为此前对话摘要") {
		t.Fatalf("主请求必须带上摘要消息：%s", gotMainBody)
	}
	// D1：lastUsage 必须是最后一次 API 真值，不是 50% 假读数。
	if s.lastUsage == nil || s.lastUsage.PromptTokens != mainPromptTokens ||
		s.lastUsage.CompletionTokens != mainCompletion {
		t.Fatalf("lastUsage 应为 API 真值 %d/%d，got %+v",
			mainPromptTokens, mainCompletion, s.lastUsage)
	}
	usageMsg := waitFor(t, col, "usage", func(m map[string]any) bool { return m["t"] == "usage" })
	if int(usageMsg["input"].(float64)) != mainPromptTokens ||
		int(usageMsg["output"].(float64)) != mainCompletion {
		t.Fatalf("usage 应为 API 真值 %d/%d，got %v",
			mainPromptTokens, mainCompletion, usageMsg)
	}

	// compact 行落盘（原始 msg 行仍在，压缩只是追加一条记录）。
	data, err := os.ReadFile(filepath.Join(sessDir, "s-test-1.context.jsonl"))
	if err != nil {
		t.Fatalf("读 context.jsonl 失败: %v", err)
	}
	if !strings.Contains(string(data), `"t":"compact"`) {
		t.Fatalf("context.jsonl 应有 compact 记录：%s", data)
	}
	if !strings.Contains(string(data), `"t":"msg"`) {
		t.Fatalf("context.jsonl 仍应保留原始 msg 行：%s", data)
	}
}

// TestMaskHistoryViewOnlyAndPairingSafe：折叠是纯视图——
// 条数/顺序不变、最近 K 条原文不动、原 history 不被改写、参数折叠后仍是合法 JSON。
func TestMaskHistoryViewOnlyAndPairingSafe(t *testing.T) {
	bigArgs := `{"path":"x.txt","content":"` + strings.Repeat("Q", 5000) + `"}`
	big := strings.Repeat("R", 5000)
	msgs := []chatMessage{
		{Role: "assistant", ToolCalls: []apiToolCall{
			{ID: "c1", Type: "function", Function: funcSpec{Name: "write_file", Arguments: bigArgs}},
			{ID: "c2", Type: "function", Function: funcSpec{Name: "write_file", Arguments: bigArgs}},
		}},
		{Role: "tool", ToolCallID: "c1", Content: big},
		{Role: "tool", ToolCallID: "c2", Content: big},
	}

	view, stats := maskHistory(msgs, 1)
	if len(view) != len(msgs) {
		t.Fatalf("视图条数必须不变：%d -> %d", len(msgs), len(view))
	}
	if stats.ToolResults != 1 || stats.ToolArgs != 1 {
		t.Fatalf("应折 1 条旧结果 + 1 处旧参数，got %+v", stats)
	}
	// 最近 1 条（c2）原文完整
	if view[2].Content != big {
		t.Fatalf("最近 1 条 tool 结果不应被折叠")
	}
	if view[0].ToolCalls[1].Function.Arguments != bigArgs {
		t.Fatalf("最近 1 条 tool_call 参数不应被折叠")
	}
	// 旧结果被折叠，但仍是同一条目、id 不变
	if view[1].ToolCallID != "c1" || !strings.Contains(view[1].Content, "历史工具输出已折叠") {
		t.Fatalf("旧 tool 结果应折叠且保留 id：%+v", view[1])
	}
	// 旧参数折叠后仍是合法 JSON，且保留 path
	var folded map[string]any
	if err := json.Unmarshal([]byte(view[0].ToolCalls[0].Function.Arguments), &folded); err != nil {
		t.Fatalf("折叠后的参数必须是合法 JSON: %v", err)
	}
	if folded["path"] != "x.txt" {
		t.Fatalf("折叠后应保留 path 元信息：%v", folded)
	}
	// 纯函数：原 history 一字未改
	if msgs[0].ToolCalls[0].Function.Arguments != bigArgs || msgs[1].Content != big {
		t.Fatalf("maskHistory 不得改写原 history")
	}
}

// TestMasking_AbsoluteCapWithoutContext：无 hello.context 时的 Q1 裁决——
// 不做比例压缩，但未折叠工具载荷 > 256KB 仍要折叠；小会话不动。
func TestMasking_AbsoluteCapWithoutContext(t *testing.T) {
	s, _, _, _ := newTestSessionCfg(t, llmConfig{
		BaseURL: "http://127.0.0.1:9", Model: "test-model", Key: "k",
	}, "", t.TempDir())
	s.turn = 1
	s.history = []chatMessage{{Role: "tool", ToolCallID: "c1", Content: strings.Repeat("x", 100<<10)}}
	s.maybeEnableMasking(0)
	if s.maskOn {
		t.Fatalf("100KB < 256KB，不应触发绝对上限折叠")
	}
	s.history = []chatMessage{{Role: "tool", ToolCallID: "c1", Content: strings.Repeat("x", 300<<10)}}
	s.maybeEnableMasking(0)
	if !s.maskOn {
		t.Fatalf("300KB > 256KB，无 ctxCapacity 也应触发折叠")
	}
}

// TestEmergencyCompact_WritesVisibleDeterministicSummary：Stage 3 的本体。
// 不调 LLM、只在合法 user 边界切；落盘 compact，并给出可见 notice。
func TestEmergencyCompact_WritesVisibleDeterministicSummary(t *testing.T) {
	ws := t.TempDir()
	sessDir := filepath.Join(ws, ".ecdi", "sessions")
	if err := os.MkdirAll(sessDir, 0755); err != nil {
		t.Fatal(err)
	}
	s, col, _, _ := newTestSessionCfg(t, llmConfig{
		BaseURL: "http://127.0.0.1:9", Model: "test-model", Key: "k", Context: 4000,
	}, "", ws)
	t.Cleanup(func() {
		if s.ctxWriter != nil {
			_ = s.ctxWriter.Close()
		}
	})
	s.turn = 1
	s.ensureCtxWriter()

	big := strings.Repeat("Z", 10240)
	for turn := 0; turn < 3; turn++ {
		callID := fmt.Sprintf("c%d", turn)
		s.history = append(s.history,
			chatMessage{Role: "user", Content: fmt.Sprintf("第 %d 轮目标", turn)},
			chatMessage{Role: "assistant", ToolCalls: []apiToolCall{{
				ID: callID, Type: "function",
				Function: funcSpec{Name: "read_file", Arguments: `{"path":"a.txt"}`},
			}}},
			chatMessage{Role: "tool", ToolCallID: callID, Content: big},
			chatMessage{Role: "assistant", Content: fmt.Sprintf("第 %d 轮完成", turn)},
		)
	}
	before := s.estimateRequest(s.buildRequestMessages())
	if !s.emergencyCompact(before) {
		t.Fatalf("应能做出确定性的紧急压缩（before=%d）", before)
	}
	if len(s.history) == 0 || !strings.HasPrefix(s.history[0].Content, contextlog.SummaryPrefix) {
		t.Fatalf("history[0] 应是摘要，got %+v", s.history)
	}
	if !strings.Contains(s.history[0].Content, "自动紧急摘要") {
		t.Fatalf("紧急摘要应带显式标记：%s", s.history[0].Content)
	}
	found := false
	for _, m := range parseAll(t, col) {
		if m["t"] == "block_start" && m["kind"] == "system" &&
			strings.Contains(m["text"].(string), "紧急压缩") {
			found = true
		}
	}
	if !found {
		t.Fatalf("紧急压缩必须可见")
	}
	data, err := os.ReadFile(filepath.Join(sessDir, "s-test-1.context.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), `"t":"compact"`) {
		t.Fatalf("紧急压缩也应追加 compact 记录：%s", data)
	}
}
