// agent_test.go —— 子代理（协议 v5）的离线 E2E。
//
// 判据都落在**出站流**上（过 wire.Out 的真实路径），不是调内部函数看返回值：
// v5 的两个不变量（I12 生命周期、I13 parent 归属）只有在成流之后才有意义，
// 「结构体字段对」证明不了「发出去的流对」。
//
// 六条覆盖：
//
//	① 委派成功：ask 一次 → agent start/end → 摘要回主代理（blocks 带归属）
//	② 并行交错：两个 task 真的同时在跑（靠服务端 hold 卡住两边断定重叠）
//	③ 取消传播：turn 取消 ⇒ 子代理收尾 ok:false，整轮照常回 idle
//	④ 深度限制：子代理拿不到 task 工具；即使调到了也被拒
//	⑤ 上下文隔离：子代理的中间过程不进主历史，主历史只落一条结论
//	⑥ 节流合批：合批后的文本 = 原增量拼接（丢字比丢块更难发现）

package main

import (
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

	"ecdi-fake-core/internal/wire"
)

// ── 可路由的脚本服务器 ───────────────────────────────────────────

// routeServer：按**请求体内容**选脚本（子代理并发到达 ⇒ 靠下标选会串）。
//
// ★ 必须先认出「这是子代理的请求」再按标记路由：根代理的第 2 个请求里
//   带着 tool_calls 的 arguments（含任务原文），只按 "TASK-A" 匹配会把
//   根代理误判成子代理，于是一直喂子代理脚本 —— 循环到轮数上限。
//   子代理的系统提示首句是固定串，用它做第一道判据。
// ★ 每个标记一条**队列**：子代理是多轮的（先工具调用、后收尾），
//   同一个脚本重放会让它一直调同一个工具。
type routeServer struct {
	mu       sync.Mutex
	root     []sseScript
	rootIdx  int
	child    map[string][]sseScript // key: 任务标记（如 "TASK-A"）
	childIdx map[string]int
	holds    map[string]chan struct{}
	bodies   []string
}

const childSysMarker = "委派出来的子代理"

func newRouteServer(root []sseScript, child map[string][]sseScript, holds map[string]chan struct{}) *routeServer {
	return &routeServer{root: root, child: child, childIdx: map[string]int{}, holds: holds}
}

func (rs *routeServer) handler(t *testing.T) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		s := string(body)

		rs.mu.Lock()
		rs.bodies = append(rs.bodies, s)
		var sc sseScript
		var hold chan struct{}
		matched := ""
		if strings.Contains(s, childSysMarker) {
			for k := range rs.child {
				if strings.Contains(s, k) {
					matched = k
					break
				}
			}
		}
		if matched != "" {
			q := rs.child[matched]
			i := rs.childIdx[matched]
			if i >= len(q) {
				// 脚本用尽：给一句收尾，别让子代理空转到轮数上限
				sc = sseScript{chunks: []string{deltaChunk("（脚本用尽）", "", "")}}
			} else {
				sc = q[i]
				rs.childIdx[matched] = i + 1
			}
			hold = rs.holds[matched]
		} else {
			if rs.rootIdx >= len(rs.root) {
				rs.mu.Unlock()
				http.Error(w, "root script exhausted", http.StatusInternalServerError)
				return
			}
			sc = rs.root[rs.rootIdx]
			rs.rootIdx++
		}
		rs.mu.Unlock()

		w.Header().Set("Content-Type", "text/event-stream")
		fl := w.(http.Flusher)
		for i, c := range sc.chunks {
			// hold 卡在**第二个 chunk 之前**：第一个增量已经发出去，
			// 但这一路还没结束 ⇒ 测试能观察"两路同时在飞"。
			if i == 1 && hold != nil {
				fl.Flush()
				<-hold
			}
			fmt.Fprintf(w, "data: %s\n\n", c)
			fl.Flush()
		}
		if sc.usage != nil {
			fmt.Fprintf(w, "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":%d,\"completion_tokens\":%d}}\n\n",
				sc.usage.PromptTokens, sc.usage.CompletionTokens)
			fl.Flush()
		}
		fmt.Fprint(w, "data: [DONE]\n\n")
		fl.Flush()
	}
}

// ── 断言小工具 ──────────────────────────────────────────────────

func agentMsgs(t *testing.T, col *outCollector, phase string) []map[string]any {
	t.Helper()
	var out []map[string]any
	for _, m := range parseAll(t, col) {
		if m["t"] == "agent" && m["phase"] == phase {
			out = append(out, m)
		}
	}
	return out
}

func hasDeltaWith(col *outCollector, sub string) bool {
	for _, l := range col.all() {
		var m map[string]any
		if json.Unmarshal([]byte(l), &m) != nil {
			continue
		}
		if m["t"] == "delta" {
			if s, ok := m["text"].(string); ok && strings.Contains(s, sub) {
				return true
			}
		}
	}
	return false
}

// blockExistsWithKind：流里是否存在某 id 的块且 kind 匹配（I13 的前半）。
func blockExistsWithKind(msgs []map[string]any, id, kind string) bool {
	for _, m := range msgs {
		if m["t"] == "block_start" && m["id"] == id && m["kind"] == kind {
			return true
		}
	}
	return false
}

// nextAsk：取一个**还没答过**的 ask（两次委派会发两条，靠内容区分不出来）。
func nextAsk(t *testing.T, col *outCollector, seen map[string]bool) map[string]any {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		for _, m := range parseAll(t, col) {
			if m["t"] != "ask" {
				continue
			}
			id, _ := m["id"].(string)
			if !seen[id] {
				seen[id] = true
				return m
			}
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("等待新的 ask 超时；已收 %d 行", col.count())
	return nil
}

// taskCallArgs：构造一个 task 工具调用的 SSE 增量。
func taskCallArgs(id, task, name string) string {
	args := map[string]any{"task": task}
	if name != "" {
		args["name"] = name
	}
	b, _ := json.Marshal(args)
	return fmt.Sprintf(`[{"index":0,"id":%q,"function":{"name":"task","arguments":%s}}]`,
		id, mustJSON(string(b)))
}

// ── ① 委派成功 ──────────────────────────────────────────────────

func TestAgent_DelegateSuccess(t *testing.T) {
	ws := t.TempDir()
	if err := os.WriteFile(ws+"/a.md", []byte("子代理该看到的内容 pin-42"), 0o644); err != nil {
		t.Fatal(err)
	}

	rs := newRouteServer(
		[]sseScript{
			{chunks: []string{deltaChunk("", "", taskCallArgs("call-t1", "TASK-A 读 a.md 并报告内容", "检索员"))}},
			{chunks: []string{deltaChunk("子代理说找到了 pin-42。", "", "")},
				usage: &chatUsage{PromptTokens: 300, CompletionTokens: 20}},
		},
		map[string][]sseScript{
			"TASK-A": {
				{chunks: []string{
					deltaChunk("", "先读 a.md", ""),
					deltaChunk("", "", `[{"index":0,"id":"c1","function":{"name":"read_file","arguments":"{\"path\":\"a.md\"}"}}]`),
				}},
				{chunks: []string{deltaChunk("a.md 里是 pin-42。", "", "")}},
			},
		}, nil)
	srv := httptest.NewServer(rs.handler(t))
	t.Cleanup(srv.Close)

	s, col, lines, stc := newTestSessionAgents(t, srv.URL, ws, agentOpts{on: true})
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":5,"gui":"test"}`)
	hello := waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })

	// caps 必须声明 agents（GUI 据此显示代理面）
	found := false
	for _, c := range hello["caps"].([]any) {
		if c == "agents" {
			found = true
		}
	}
	if !found {
		t.Fatalf("--agents 开启时 caps 必须声明 agents：%v", hello["caps"])
	}

	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"派个人看看 a.md"}`)

	// 委派门：问句必须列出授权范围（工具集 + 预算），否则等于没确认
	ask := waitFor(t, col, "delegation ask", func(m map[string]any) bool {
		return m["t"] == "ask" && strings.Contains(fmt.Sprint(m["question"]), "子代理")
	})
	q := ask["question"].(string)
	for _, want := range []string{"检索员", "TASK-A", "read_file", "8 轮"} {
		if !strings.Contains(q, want) {
			t.Fatalf("委派问句缺 %q：%q", want, q)
		}
	}
	if strings.Contains(q, "\n") {
		t.Fatalf("委派问句必须单行（GUI 折叠摘要会压扁多行）：%q", q)
	}
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))

	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)

	starts := agentMsgs(t, col, "start")
	ends := agentMsgs(t, col, "end")
	if len(starts) != 1 || len(ends) != 1 {
		t.Fatalf("agent start/end 应各 1 条，got %d/%d", len(starts), len(ends))
	}
	agID := starts[0]["id"].(string)
	if starts[0]["depth"].(float64) != 1 {
		t.Fatalf("depth 应恒为 1，got %v", starts[0]["depth"])
	}
	// parent_call 必须指向一个真实存在、且是 task 调用的 tool_call 块（I13 同源）
	parentCall := starts[0]["parent_call"].(string)
	if !blockExistsWithKind(msgs, parentCall, "tool_call") {
		t.Fatalf("parent_call %q 不是已存在的 tool_call 块", parentCall)
	}
	if ends[0]["ok"] != true {
		t.Fatalf("成功委派的 end.ok = %v", ends[0]["ok"])
	}
	summary, _ := ends[0]["summary"].(string)
	if !strings.Contains(summary, "pin-42") {
		t.Fatalf("end.summary 应是子代理的结论原文，got %q", summary)
	}
	if ends[0]["turns"].(float64) < 2 {
		t.Fatalf("子代理跑了 2 轮（读文件 + 收尾），end.turns = %v", ends[0]["turns"])
	}

	// 归属：子代理的块全部带 agent + parent
	attributed := 0
	for _, m := range msgs {
		if m["t"] != "block_start" || m["agent"] == nil {
			continue
		}
		if m["agent"] != agID {
			t.Fatalf("块归属 agent=%v，期望 %s", m["agent"], agID)
		}
		if m["parent"] != parentCall {
			t.Fatalf("块的 parent=%v，期望 %s", m["parent"], parentCall)
		}
		attributed++
	}
	if attributed == 0 {
		t.Fatalf("没有任何块带 agent 归属")
	}
	// 根代理的块绝不能带 agent（I13 后半）
	for _, m := range msgs {
		if m["t"] == "block_start" && m["kind"] == "user" && m["agent"] != nil {
			t.Fatalf("根代理的 user 块带了 agent 字段")
		}
	}

	// 结论必须回到主代理的 tool_result 里（GUI 看到的 = 模型拿到的）
	foundResult := false
	for _, m := range msgs {
		if m["t"] == "delta" && strings.Contains(fmt.Sprint(m["text"]), "pin-42") {
			foundResult = true
		}
	}
	if !foundResult {
		t.Fatalf("子代理结论没有出现在流里")
	}
}

// ── ② 并行交错 ──────────────────────────────────────────────────

func TestAgent_ParallelTwoAgentsOverlap(t *testing.T) {
	holdA := make(chan struct{})
	holdB := make(chan struct{})
	t.Cleanup(func() {
		// 兜底：用例失败时也不能让 handler 永久阻塞（srv.Close 会等它）
		for _, c := range []chan struct{}{holdA, holdB} {
			select {
			case <-c:
			default:
				close(c)
			}
		}
	})

	rs := newRouteServer(
		[]sseScript{
			{chunks: []string{deltaChunk("", "", `[` +
				`{"index":0,"id":"call-a","function":{"name":"task","arguments":"{\"task\":\"TASK-A 甲路\",\"name\":\"甲\"}"}},` +
				`{"index":1,"id":"call-b","function":{"name":"task","arguments":"{\"task\":\"TASK-B 乙路\",\"name\":\"乙\"}"}}` +
				`]`)}},
			{chunks: []string{deltaChunk("两路都回来了。", "", "")}},
		},
		map[string][]sseScript{
			"TASK-A": {{chunks: []string{deltaChunk("A1", "", ""), deltaChunk("A2 结论", "", "")}}},
			"TASK-B": {{chunks: []string{deltaChunk("B1", "", ""), deltaChunk("B2 结论", "", "")}}},
		},
		map[string]chan struct{}{"TASK-A": holdA, "TASK-B": holdB})
	srv := httptest.NewServer(rs.handler(t))
	t.Cleanup(srv.Close)

	s, col, lines, stc := newTestSessionAgents(t, srv.URL, t.TempDir(), agentOpts{on: true})
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":5,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"两路并行"}`)

	// 两次 ask 必须**都在任何子代理启动之前**发生（I3 的相序保证）：
	// 相 2a 逐个过门、相 2b 才一起启动。
	seen := map[string]bool{}
	for i := 0; i < 2; i++ {
		ask := nextAsk(t, col, seen)
		// 门还开着的时候不许有子代理在跑（否则 I3 就破了）
		if n := len(agentMsgs(t, col, "start")); n > 0 {
			t.Fatalf("第 %d 次委派门未答时已有 %d 个子代理启动 ⇒ I3 破", i+1, n)
		}
		sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":%d,"id":%s,"choice":"allow"}`,
			10+i, mustJSON(ask["id"].(string))))
	}

	// 两路的第一片增量都到 ⇒ 两边都在飞。此时**不允许**任何 agent end。
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) && !(hasDeltaWith(col, "A1") && hasDeltaWith(col, "B1")) {
		time.Sleep(5 * time.Millisecond)
	}
	if !(hasDeltaWith(col, "A1") && hasDeltaWith(col, "B1")) {
		t.Fatalf("两路子代理没有同时开始；已收 %d 行", col.count())
	}
	if n := len(agentMsgs(t, col, "end")); n != 0 {
		t.Fatalf("两路都还在跑的时候出现了 %d 条 agent end ⇒ 不是并行", n)
	}
	if n := len(agentMsgs(t, col, "start")); n != 2 {
		t.Fatalf("应有 2 条 agent start，got %d", n)
	}

	close(holdA)
	close(holdB)

	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)

	ends := agentMsgs(t, col, "end")
	if len(ends) != 2 {
		t.Fatalf("两个子代理必须各发一条 end（否则 I12 悬空），got %d", len(ends))
	}
	for _, e := range ends {
		if e["ok"] != true {
			t.Fatalf("成功委派的 end.ok = %v（%v）", e["ok"], e["summary"])
		}
	}
	// 两个 end 的 agent id 必须不同（I12：id 唯一）
	if ends[0]["id"] == ends[1]["id"] {
		t.Fatalf("两个子代理共用了 id %v", ends[0]["id"])
	}
}

// ── ③ 取消传播 ──────────────────────────────────────────────────

func TestAgent_CancelPropagates(t *testing.T) {
	hold := make(chan struct{})
	t.Cleanup(func() {
		select {
		case <-hold:
		default:
			close(hold)
		}
	})

	rs := newRouteServer(
		[]sseScript{
			{chunks: []string{deltaChunk("", "", taskCallArgs("call-t9", "TASK-A 慢慢来", "拖延者"))}},
		},
		map[string][]sseScript{
			"TASK-A": {{chunks: []string{deltaChunk("开始", "", ""), deltaChunk("永不抵达的结论", "", "")}}},
		},
		map[string]chan struct{}{"TASK-A": hold})
	srv := httptest.NewServer(rs.handler(t))
	t.Cleanup(srv.Close)

	s, col, lines, stc := newTestSessionAgents(t, srv.URL, t.TempDir(), agentOpts{on: true})
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":5,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"派一个"}`)

	ask := waitFor(t, col, "delegation ask", func(m map[string]any) bool { return m["t"] == "ask" })
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))

	// 等子代理真的开始（agent start + 第一片增量），再按停止
	waitFor(t, col, "agent start", func(m map[string]any) bool { return m["t"] == "agent" && m["phase"] == "start" })
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) && !hasDeltaWith(col, "开始") {
		time.Sleep(5 * time.Millisecond)
	}

	sendLine(lines, stc, `{"t":"cancel","seq":3}`)
	close(hold)

	waitFor(t, col, "agent end after cancel", func(m map[string]any) bool {
		return m["t"] == "agent" && m["phase"] == "end"
	})
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":4}`)
	<-done

	ends := agentMsgs(t, col, "end")
	if len(ends) != 1 {
		t.Fatalf("取消也必须发恰好一条 agent end（不许悬空），got %d", len(ends))
	}
	if ends[0]["ok"] != false {
		t.Fatalf("取消路径 end.ok 应为 false，got %v", ends[0]["ok"])
	}
	if !strings.Contains(fmt.Sprint(ends[0]["summary"]), "取消") {
		t.Fatalf("取消摘要应说明原因，got %q", ends[0]["summary"])
	}

	msgs := parseAll(t, col)
	assertSeqContinuous(t, msgs)
	assertBlockPairs(t, msgs)
	// 状态收尾仍是 cancelled → idle（与 v4 的取消路径同形）
	sawCancelled := false
	for _, m := range msgs {
		if m["t"] == "state" && m["value"] == "cancelled" {
			sawCancelled = true
		}
	}
	if !sawCancelled {
		t.Fatalf("取消后应发 state=cancelled")
	}
}

// ── ④ 深度限制 ──────────────────────────────────────────────────

func TestAgent_DepthLimit(t *testing.T) {
	// 子代理的工具表里不能有 task（看得见才调得到）
	for _, d := range toolDefsFor(true, 1) {
		if d.Name == "task" {
			t.Fatalf("子代理的工具表里出现了 task ⇒ 深度 1 失效")
		}
	}
	if len(toolDefsFor(true, 1)) != 6 {
		t.Fatalf("子代理应看到 6 件只读/写工具（无 task），got %d", len(toolDefsFor(true, 1)))
	}
	// 根代理开启时才有 task
	hasTask := func(defs []toolDef) bool {
		for _, d := range defs {
			if d.Name == "task" {
				return true
			}
		}
		return false
	}
	if !hasTask(toolDefsFor(true, 0)) {
		t.Fatalf("根代理在 --agents 开启时必须拿到 task")
	}
	if hasTask(toolDefsFor(false, 0)) {
		t.Fatalf("--agents 关闭时根代理不得拿到 task（v4 逐字节不变的前提）")
	}

	// 运行时兜底：即便调到了，也必须是可读的拒绝而不是执行
	s, _, _, _ := newTestSessionAgents(t, "http://127.0.0.1:9", "", agentOpts{on: true})
	sub := &subagent{sess: s, id: "a1", parentCall: "b1", task: "x"}
	res := sub.runTool(aggregatedCall{ID: "c1", Name: "task", Arguments: `{"task":"再派一个"}`})
	if res.OK || !strings.Contains(res.Output, "深度") {
		t.Fatalf("子代理调用 task 必须被拒并说明深度限制，got %+v", res)
	}
	res = sub.runTool(aggregatedCall{ID: "c2", Name: "write_file", Arguments: `{"path":"x","content":"y"}`})
	if res.OK || !strings.Contains(res.Output, "只读") {
		t.Fatalf("子代理不得写文件，got %+v", res)
	}
}

// ── ⑤ 上下文隔离 ────────────────────────────────────────────────

func TestAgent_ContextIsolation(t *testing.T) {
	ws := t.TempDir()
	if err := os.WriteFile(ws+"/a.md", []byte("pin-42"), 0o644); err != nil {
		t.Fatal(err)
	}

	rs := newRouteServer(
		[]sseScript{
			{chunks: []string{deltaChunk("", "", taskCallArgs("call-t1", "TASK-A 读 a.md", "甲"))}},
			{chunks: []string{deltaChunk("收到结论。", "", "")}},
		},
		map[string][]sseScript{
			"TASK-A": {
				{chunks: []string{
					deltaChunk("", "", `[{"index":0,"id":"c1","function":{"name":"read_file","arguments":"{\"path\":\"a.md\"}"}}]`),
				}},
				{chunks: []string{deltaChunk("a.md 是 pin-42。", "", "")}},
			},
		}, nil)
	srv := httptest.NewServer(rs.handler(t))
	t.Cleanup(srv.Close)

	s, col, lines, stc := newTestSessionAgents(t, srv.URL, ws, agentOpts{on: true})
	done := runSession(s)
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":5,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"ROOT-USER-TEXT"}`)

	ask := waitFor(t, col, "delegation ask", func(m map[string]any) bool { return m["t"] == "ask" })
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"].(string))))
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})
	sendLine(lines, stc, `{"t":"bye","seq":3}`)
	<-done

	// 按**系统消息**区分两路请求：子代理的系统提示以「委派出来的子代理」开头，
	// 不能靠 TASK-A 这种字面量——根代理的 tool_calls 参数里也有它。
	var rootBodies []string
	childReq := ""
	for _, b := range rs.bodies {
		if strings.Contains(b, "委派出来的子代理") {
			childReq = b
			continue
		}
		rootBodies = append(rootBodies, b)
	}
	if childReq == "" {
		t.Fatalf("没抓到子代理的请求")
	}
	if len(rootBodies) < 2 {
		t.Fatalf("根代理应发 2 次请求（工具前后各一次），got %d", len(rootBodies))
	}
	// 子代理看不到主对话
	if strings.Contains(childReq, "ROOT-USER-TEXT") {
		t.Fatalf("子代理的上下文里混进了主代理的用户消息")
	}
	// 子代理的系统消息必须写明只读边界 + 任务原文
	if !strings.Contains(childReq, "只读") || !strings.Contains(childReq, "TASK-A 读 a.md") {
		t.Fatalf("子代理系统消息缺任务或只读约束")
	}

	// 主历史只有一条 tool 消息，内容是结论摘要（不是过程）
	var req2 struct {
		Messages []chatMessage `json:"messages"`
	}
	if err := json.Unmarshal([]byte(rootBodies[len(rootBodies)-1]), &req2); err != nil {
		t.Fatal(err)
	}
	toolMsgs := 0
	for _, m := range req2.Messages {
		if m.Role == "tool" {
			toolMsgs++
			if strings.Contains(m.Content, "read_file") || strings.Contains(m.Content, "c1") {
				t.Fatalf("子代理的中间过程漏进了主历史：%q", m.Content)
			}
			if strings.TrimSpace(m.Content) == "" {
				t.Fatalf("结论摘要为空 ⇒ 主代理会收到一条空工具结果")
			}
		}
	}
	if toolMsgs != 1 {
		t.Fatalf("主历史应只有 1 条 tool 消息（子代理结论），got %d", toolMsgs)
	}
}

// ── ⑥ 节流合批 ──────────────────────────────────────────────────

func TestDeltaCoalescer_Concatenation(t *testing.T) {
	col := &outCollector{}
	s := &session{out: wire.NewOut(col, false)}

	coal := newDeltaCoalescer(s, "a1-b7")
	parts := []string{"你", "好"}
	for _, p := range parts {
		coal.add(p)
	}
	// 阈值前不该急着写（<1KB 且离上个 tick 不到 60ms）
	before := len(parseAll(t, col))

	// 超阈值 ⇒ 立刻写一次（这一条是"合批器还活着"的判据）
	big := strings.Repeat("x", 2000)
	coal.add(big)
	parts = append(parts, big)
	if got := len(parseAll(t, col)); got <= before {
		t.Fatalf("超过 %d 字节没有触发即时刷写", agentDeltaFlushBytes)
	}

	// close 必须把余量冲出去（否则转录静默缺字）
	coal.add("，世界")
	parts = append(parts, "，世界")
	coal.close()

	msgs := parseAll(t, col)
	if len(msgs) == 0 {
		t.Fatalf("合批器一条 delta 都没发出来")
	}
	var got strings.Builder
	for _, m := range msgs {
		if m["t"] != "delta" || m["id"] != "a1-b7" {
			t.Fatalf("意外的消息：%v", m)
		}
		got.WriteString(m["text"].(string))
	}
	want := strings.Join(parts, "")
	if got.String() != want {
		t.Fatalf("合批后文本与拼接不一致：got %d 字节，want %d 字节", got.Len(), len(want))
	}
	// 合批确实减少了消息数：4 次 add → 2 条 delta（阈值一次 + close 一次）
	if len(msgs) >= len(parts) {
		t.Fatalf("合批没有减少消息数：add %d 次 → %d 条 delta", len(parts), len(msgs))
	}
}
