// agent.go —— 子代理（协议 v5 §3.12 / docs/subagent-v5-plan.md）。
//
// 三件事，各自独立：
//
//	① **委派**：`task` 工具调用 → 一次 ask 门 → 子代理。委派即授权——
//	   这一次确认覆盖子代理能用的**只读工具全集**（read_file/list_dir/grep），
//	   它内部不再逐个询问。写/执行永远不派出去（两个子代理并行写同一个文件
//	   是竞态温床，省下的几次点击不值这个风险）。
//	② **并发**：同一条 assistant 消息里的多个 `task` 并行跑，受 --max-agents
//	   信号量约束。I3（一个未决 ask、ask 后不得有消息）由 session.runToolCalls
//	   的**相序**保证：全部 ask 都在任何子代理启动之前完成，所以不存在
//	   「子代理在 ask 期间继续吐流」这个组合。
//	③ **上下文隔离**：每个子代理一份自己的 history，不写主 ctxWriter、不参与
//	   主代理的压缩；主历史里只落一条 `tool` 消息（结论摘要）。
//
// 深度固定为 1：子代理拿到的工具表里没有 `task`（tools.go toolDefsFor），
// 运行时那道 guard 只是「将来谁改了这张表」的兜底——模型看不见就调不到。

package main

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"ecdi-fake-core/internal/contextlog"
	"ecdi-fake-core/internal/wire"
)

const (
	// 子代理 delta 合批：≥1KB 或每 60ms 写一次。
	// ★ 目的不是省带宽，是 **GUI 可读性**：两路流按 3 token 交错时，两段
	//   缩进文本会互相抢镜；按 1KB 交替，人能看清谁在长。
	agentDeltaFlushBytes = 1024
	agentDeltaFlushEvery = 60 * time.Millisecond

	defaultMaxAgents     = 2
	maxAgentsCeiling     = 8 // 再多人眼也看不过来，且 GUI 的缩进层级失去意义
	defaultAgentMaxTurns = 8
	defaultAgentTimeout  = 120 * time.Second
)

// agentOpts：`--agents` 一族。on=false 时整条委派路径不存在（工具表里没有
// task、caps 不声明 agents、发出的消息与 v4 逐字节相同）。
type agentOpts struct {
	on          bool
	maxParallel int
	maxTurns    int
	timeout     time.Duration
}

func (o agentOpts) normalized() agentOpts {
	if o.maxParallel <= 0 {
		o.maxParallel = defaultMaxAgents
	}
	if o.maxParallel > maxAgentsCeiling {
		o.maxParallel = maxAgentsCeiling
	}
	if o.maxTurns <= 0 {
		o.maxTurns = defaultAgentMaxTurns
	}
	if o.timeout <= 0 {
		o.timeout = defaultAgentTimeout
	}
	return o
}

// delegation：一次 `task` 委派的全程（ask 门 → 子代理 → 结算）。
//
// 字段分两段：前半段在 prepare 时定死（只读），后半段由 goroutine 写、
// wg.Wait 之后读 ⇒ 不需要额外锁（WaitGroup 本身就是 happens-before 边界）。
type delegation struct {
	sess    *session
	call    aggregatedCall
	callID  string // tool_call 块 id = 子代理所有块的 parent（I13）
	agentID string
	name    string
	task    string

	allowed bool
	started bool   // 真发过 `agent start` 就必须发 end（I12）
	reason  string // 未启动的原因：invalid / denied / canceled（决定 tool_call 卡的收尾文字）

	summary   string
	ok        bool
	turns     int
	inTokens  int
	outTokens int
	elapsed   time.Duration
}

// prepareDelegation：发 tool_call 块 + 过 ask 门。**不启动**子代理——
// 全部 task 的 ask 必须都完成之后才允许任何子代理开跑（见 runToolCalls）。
func (s *session) prepareDelegation(turnCtx context.Context, c aggregatedCall,
	answerCh chan wire.GuiMsg) *delegation {

	argsMap := parseArgs(c.Arguments)
	callID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": callID,
		"kind": "tool_call", "name": c.Name, "args": argsMap})

	d := &delegation{sess: s, call: c, callID: callID}
	d.task, _ = argsMap["task"].(string)
	d.task = strings.TrimSpace(d.task)
	d.name, _ = argsMap["name"].(string)
	d.name = strings.TrimSpace(d.name)
	if d.name == "" {
		d.name = clipRunes(firstLine(d.task), 24)
	}
	d.agentID = fmt.Sprintf("a%d", s.agentNo.Add(1))
	s.log("delegation %s requested: name=%q task=%q", d.agentID, d.name, clipRunes(d.task, 80))

	// 缺 task 直接失败：不必为一句话都说不清的任务占用一次确认。
	// （也不能「猜一个任务」——子代理会拿着猜测读一堆无关文件烧 token。）
	if d.task == "" {
		d.summary = "task 缺少 task 参数（要交给子代理的完整任务描述）"
		d.ok = false
		d.reason = "invalid"
		return d
	}

	// ★ task **永远过 ask 门**，即便运行者把它写进了 --allow-tools。
	//   这一问就是授权本身（委派即授权）；豁免它等于让子代理在无人
	//   知情的情况下获得一次读取整个工作区的机会。
	//   detail（2026-11-05）：任务全文/工具集/预算进结构化提案，问句保留摘要句式。
	if s.askQuestion(turnCtx, delegationAskPrompt(d.name, d.task, s.agents),
		delegationAskDetail(d.name, d.task, s.agents), answerCh) {
		s.log("delegation %s denied by user", d.agentID)
		d.summary = "用户拒绝了这次委派。"
		d.ok = false
		d.reason = "denied"
		return d
	}
	if turnCtx.Err() != nil {
		d.summary = "（用户取消）"
		d.ok = false
		d.reason = "canceled"
		return d
	}
	d.allowed = true
	return d
}

// runDelegation：信号量 + 超时 + 子代理本体。在独立 goroutine 里跑。
func (s *session) runDelegation(turnCtx context.Context, d *delegation) {
	// 信号量：拿不到就排队（不拒）。start 消息在拿到槽位之后才发——
	// 否则 GUI 的代理卡会把「排队中」显示成「运行中」，轮数耗时刻度跟着错。
	select {
	case s.agentSem <- struct{}{}:
		defer func() { <-s.agentSem }()
	case <-turnCtx.Done():
		d.summary = "（用户取消）"
		d.ok = false
		d.reason = "canceled"
		return
	}

	ctx, cancel := context.WithTimeout(turnCtx, s.agents.timeout)
	defer cancel()

	d.started = true
	s.emit(map[string]any{"t": "agent", "id": d.agentID, "phase": "start",
		"name": d.name, "parent_call": d.callID, "depth": 1, "task": d.task,
		"model": s.llm.cfg.Model})

	sub := &subagent{sess: s, id: d.agentID, parentCall: d.callID, task: d.task}
	startedAt := time.Now()
	d.summary, d.turns, d.inTokens, d.outTokens, d.ok = sub.loop(ctx)
	d.elapsed = time.Since(startedAt)
	s.log("delegation %s done: ok=%v turns=%d elapsed=%s summary=%q",
		d.agentID, d.ok, d.turns, d.elapsed, clipRunes(d.summary, 80))
}

// finishDelegation：按原调用顺序结算——agent end → tool_result → tool_call 收尾
// → 结论进主历史。全部在 wg.Wait 之后，所以顺序稳定、可断言。
func (s *session) finishDelegation(d *delegation) {
	if d.started {
		// I12：end 之后不得再有该 agent 的块 ⇒ 必须排在子代理最后一个块之后。
		s.emit(map[string]any{"t": "agent", "id": d.agentID, "phase": "end",
			"ok": d.ok, "summary": d.summary, "turns": d.turns,
			"input": d.inTokens, "output": d.outTokens,
			"elapsedMs": d.elapsed.Milliseconds()})
	}

	resultID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": resultID,
		"kind": "tool_result", "for": d.callID})
	s.emit(map[string]any{"t": "delta", "id": resultID, "text": d.summary})
	s.emit(map[string]any{"t": "block_end", "id": resultID, "ok": d.ok})

	// tool_call 卡的收尾文字：看**状态**，不看摘要文本
	//（拿 summary 去比字符串会让"改一句文案"变成"改一个判据"）。
	summary := "子代理完成"
	switch {
	case d.started && !d.ok:
		summary = "子代理失败"
	case d.started:
		summary = "子代理完成"
	case d.reason == "denied":
		summary = "用户拒绝"
	case d.reason == "canceled":
		summary = "已取消"
	case d.reason == "invalid":
		summary = "参数缺失"
	default:
		summary = "未启动"
	}
	s.emit(map[string]any{"t": "block_end", "id": d.callID, "ok": d.ok, "summary": summary})

	s.history = append(s.history, chatMessage{Role: "tool",
		ToolCallID: d.call.ID, Content: d.summary})
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(contextlogMessage(d.call.ID, d.summary))
	}
}

// ── 子代理本体 ──────────────────────────────────────────────────

type subagent struct {
	sess       *session
	id         string
	parentCall string // 发起委派的 tool_call 块 id（I13）
	task       string
	history    []chatMessage
}

// loop：独立的 agent loop。返回**结论摘要**（回给主代理的同一段文本）。
//
// 与主循环的差别只有三点：工具表剔除 task（深度 1）、不过 ask 门
// （委派即授权）、历史不落主 ctxWriter。其余（流映射、工具聚合、
// cancel 收尾）完全复用——两份实现漂移是这类代码最贵的一类债。
func (a *subagent) loop(ctx context.Context) (summary string, turns, inTok, outTok int, ok bool) {
	for turns < a.sess.agents.maxTurns {
		if ctx.Err() != nil {
			return "（子代理已取消）", turns, inTok, outTok, false
		}
		turns++

		blk := &streamBlocks{sess: a.sess, agentID: a.id, parentID: a.parentCall}
		calls, usage, err := a.sess.llm.Stream(ctx, a.messages(), toolDefsFor(false, 1),
			func(content, reasoning string) { blk.onDelta(content, reasoning, ctx) })
		blk.closeAll(err == nil && ctx.Err() == nil)

		if usage != nil {
			inTok += usage.PromptTokens
			outTok += usage.CompletionTokens
		}
		if err != nil {
			if ctx.Err() != nil {
				return "（子代理已取消）", turns, inTok, outTok, false
			}
			return fmt.Sprintf("（子代理请求失败：%v）", err), turns, inTok, outTok, false
		}
		if len(calls) == 0 {
			text := strings.TrimSpace(blk.assistantText.String())
			if text == "" {
				text = "（子代理没有产出结论）"
			}
			return text, turns, inTok, outTok, true
		}

		// assistant 消息（含 tool_calls）必须先落历史，tool 消息才能对上 id
		var apiCalls []apiToolCall
		for i, c := range calls {
			apiCalls = append(apiCalls, apiToolCall{ID: c.ID, Type: "function",
				Index: i, Function: funcSpec{Name: c.Name, Arguments: c.Arguments}})
		}
		a.history = append(a.history, chatMessage{Role: "assistant",
			Content: blk.assistantText.String(), ToolCalls: apiCalls})

		for _, c := range calls {
			if ctx.Err() != nil {
				a.history = append(a.history, chatMessage{Role: "tool",
					ToolCallID: c.ID, Content: "（用户取消，未执行）"})
				continue
			}
			res := a.runTool(c)
			a.history = append(a.history, chatMessage{Role: "tool",
				ToolCallID: c.ID, Content: res.Output})
		}
	}
	return fmt.Sprintf("（子代理已达轮数上限 %d 轮，未给出结论）", a.sess.agents.maxTurns),
		turns, inTok, outTok, false
}

// runTool：一次只读工具调用。**不过 ask 门**（委派即授权），但有第二道闸：
// 工具名必须在只读集合里。工具表已经裁过，这条是防「谁改了表」的兜底。
func (a *subagent) runTool(c aggregatedCall) toolResult {
	argsMap := parseArgs(c.Arguments)

	var res toolResult
	switch {
	case c.Name == "task":
		res = toolResult{OK: false, Output: "子代理不能再派子代理（深度固定为 1）；需要更多探查就把结论说清，让主代理再派"}
	case !isReadOnlyTool(c.Name):
		res = toolResult{OK: false, Output: fmt.Sprintf(
			"子代理只能使用只读工具（read_file / list_dir / grep），“%s” 需要主代理亲自执行并由用户确认", c.Name)}
	default:
		res = executeTool(c.Name, argsMap, a.sess.cwd)
	}

	callID := a.sess.nextBlockID()
	a.sess.emitBlock(map[string]any{"t": "block_start", "id": callID,
		"kind": "tool_call", "name": c.Name, "args": argsMap}, a.id, a.parentCall)

	resultID := a.sess.nextBlockID()
	a.sess.emitBlock(map[string]any{"t": "block_start", "id": resultID,
		"kind": "tool_result", "for": callID}, a.id, a.parentCall)
	a.sess.emit(map[string]any{"t": "delta", "id": resultID, "text": res.Output})
	a.sess.emit(map[string]any{"t": "block_end", "id": resultID, "ok": res.OK})

	summary := "已执行"
	if !res.OK {
		summary = "执行失败"
	}
	a.sess.emit(map[string]any{"t": "block_end", "id": callID, "ok": res.OK, "summary": summary})
	return res
}

func isReadOnlyTool(name string) bool {
	switch name {
	case "read_file", "list_dir", "grep":
		return true
	}
	return false
}

// messages：系统消息现建（与主代理同一条理由：工作区可能已切换）+ 本代理历史。
func (a *subagent) messages() []chatMessage {
	sys := fmt.Sprintf("你是主代理委派出来的子代理，负责一个**只读**探查任务。当前工作区：%s。\n"+
		"任务：%s\n"+
		"你只能用 read_file / list_dir / grep（相对路径以工作区为根，越界会被拒绝），"+
		"不能修改任何文件、不能执行命令、不能再派子代理。\n"+
		"做完后用**简短结论**回答（不超过 200 字，不复述过程、不贴大段原文）；"+
		"任务无法完成就直接说明原因。用用户的语言回答。", a.sess.cwd, a.task)
	msgs := make([]chatMessage, 0, len(a.history)+1)
	msgs = append(msgs, chatMessage{Role: "system", Content: sys})
	// ★ 子代理没有 API usage 锚点（它的 usage 只累加到 inTok/outTok），
	//   所以只能做纯本地的“保留最近 2 条 tool 结果”折叠，零额外请求。
	//   历史本身不动：每次组装请求时现算视图（与根代理同一条纪律）。
	hist, _ := maskHistory(a.history, agentMaskKeepRecent)
	return append(msgs, hist...)
}

// delegationAskPrompt：委派问句（单行——GUI 的 ask 行有折叠摘要）。
// 必须把**授权范围**摆出来：任务原文、可用工具、轮数/时间预算。
// 没写清「授权了什么」的确认框等于没确认。
func delegationAskPrompt(name, task string, o agentOpts) string {
	return fmt.Sprintf("允许派子代理「%s」吗？任务：%s（授权：read_file / list_dir / grep 只读，限 %d 轮 / %s）",
		name, clipRunes(task, 120), o.maxTurns, o.timeout)
}

// ── 合批器 ──────────────────────────────────────────────────────

// deltaCoalescer：把一路子代理的细碎 delta 攒成 ≥1KB 或 ≤60ms 的块再写出。
// 每块一个实例（reasoning 与正文各自独立），close 之前必须把余量冲出去——
// 否则最后几个 token 会丢，而「丢一点」在转录里表现为**文本缺字**，
// 比整块丢失更难发现。
type deltaCoalescer struct {
	sess *session
	id   string

	mu   sync.Mutex
	buf  strings.Builder
	quit chan struct{}
	done chan struct{}
	once sync.Once
}

func newDeltaCoalescer(sess *session, id string) *deltaCoalescer {
	c := &deltaCoalescer{sess: sess, id: id, quit: make(chan struct{}), done: make(chan struct{})}
	go c.tick()
	return c
}

func (c *deltaCoalescer) tick() {
	defer close(c.done)
	t := time.NewTicker(agentDeltaFlushEvery)
	defer t.Stop()
	for {
		select {
		case <-c.quit:
			return
		case <-t.C:
			c.flush()
		}
	}
}

func (c *deltaCoalescer) add(text string) {
	c.mu.Lock()
	c.buf.WriteString(text)
	n := c.buf.Len()
	c.mu.Unlock()
	if n >= agentDeltaFlushBytes {
		c.flush()
	}
}

func (c *deltaCoalescer) flush() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.flushLocked()
}

func (c *deltaCoalescer) flushLocked() {
	if c.buf.Len() == 0 {
		return
	}
	text := c.buf.String()
	c.buf.Reset()
	// ★ 持锁 emit：delta 的顺序必须与 add 的调用顺序一致（锁序：本锁 → Out 锁，
	//   反向路径不存在 ⇒ 不会死锁）。
	c.sess.emit(map[string]any{"t": "delta", "id": c.id, "text": text})
}

func (c *deltaCoalescer) close() {
	c.once.Do(func() { close(c.quit) })
	<-c.done
	c.flush()
}

// ── 小工具 ──────────────────────────────────────────────────────

func firstLine(s string) string {
	if i := strings.IndexByte(s, '\n'); i >= 0 {
		s = s[:i]
	}
	return strings.TrimSpace(s)
}

// clipRunes：按**字符**截断（按字节切会把摘要切在多字节字符中间）。
func clipRunes(s string, n int) string {
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n]) + "…"
}

// contextlogMessage：只为 finishDelegation 少一行噪音。
func contextlogMessage(toolCallID, content string) contextlog.Message {
	return contextlog.Message{Role: "tool", Content: content, ToolCallID: toolCallID}
}
