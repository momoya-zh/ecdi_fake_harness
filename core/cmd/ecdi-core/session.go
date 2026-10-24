package main

// session.go —— 真 core 的 agent loop 与协议映射（P3 规格 §4.2–§4.6）。
//
// 与假 core 的关键差别：
//   * 一轮对话不再是剧本，而是 **agent loop**：LLM 流式回复 →（有 tool_calls 时）
//     ask 门 → 真执行 → 结果回模型 → 续推，直到模型不再发起调用；
//   * **cancel 必须能打断 HTTP 流**（A6）——假 core 的 cancel 只在轮间生效，
//     真 core 在整轮期间开一个 listener goroutine 读 s.in，cancel 一到就
//     context.Cancel 正在进行的请求；
//   * 状态机形状、I3 自守、workspace 租约、client_id 回显与假 core 同构
//     （线层共用 wire/，交错拒绝规则逐条对应）。

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"sync/atomic"

	"ecdi-fake-core/internal/wire"
)

type session struct {
	in  <-chan wire.StampedLine
	out *wire.Out
	log func(string, ...any)
	stc *atomic.Int32

	turn    int
	blockNo int

	// ── v3：工作区租约（协议 §3.9；与假 core 同构）────────────────
	cwd       string
	pendingWs *pendingWorkspace

	// ── P3：模型与工具 ──────────────────────────────────────────
	llm        *llmClient
	allowTools map[string]bool // --allow-tools 豁免名单（运行者配置，非启发式）
	history    []chatMessage   // 完整对话历史（跨轮累积；系统消息每请求现建，§4.5 切工作区即时生效）
	lastUsage  *chatUsage      // 最近一轮 API 返回的真值 usage（§1.1 条 7：不估算）

	// ── P3.6（协议 v4）：提供商注册表与热切换 ───────────────────
	// providers = 启动时经环境注入的全部提供商（GUI 为配置里的每一家
	// 注入 ECDI_PROVIDER_<SAN>_URL / _KEY；初始提供商额外走
	// --base-url/--model/--key/--provider-id 并注册进同一张表 ⇒ 切走再切回可用）。
	providers map[string]llmConfig
	// ctxCapacity = 当下分母（model 消息可覆盖；usage.context 逐轮下发，§3.7）。
	// maxOutput 同理（请求体 max_tokens）。
	ctxCapacity int
	maxOutput   int
}

// pendingWorkspace = 一个已受理、尚未应答的工作区请求（与假 core 同构）。
type pendingWorkspace struct {
	requestID  string
	requestRaw string
	resolved   string
}

func newSession(cfg llmConfig, allowTools []string, providerEnv map[string]llmConfig,
	in <-chan wire.StampedLine, stc *atomic.Int32, log func(string, ...any),
	outW io.Writer) *session {

	s := &session{
		in:          in,
		out:         wire.NewOut(outW, false),
		log:         log,
		stc:         stc,
		llm:         newLLMClient(cfg, log),
		allowTools:  map[string]bool{},
		providers:   map[string]llmConfig{},
		ctxCapacity: cfg.Context,
		maxOutput:   cfg.MaxOutput,
	}
	// 初始提供商注册进同一张表（切走再切回可用）；环境注入的其余提供商随后并入。
	if cfg.ProviderID != "" {
		s.providers[cfg.ProviderID] = cfg
	}
	for id, pc := range providerEnv {
		if _, dup := s.providers[id]; !dup {
			s.providers[id] = pc
		}
	}
	s.log("providers registered: %d (initial=%q)", len(s.providers), cfg.ProviderID)
	for _, t := range allowTools {
		if t = strings.TrimSpace(t); t != "" {
			s.allowTools[t] = true
		}
	}
	if wd, err := os.Getwd(); err == nil {
		if norm, err := wire.NormalizeWorkspace(wd); err == nil {
			s.cwd = norm
		}
	}

	// hello（协议 §3.1）：seq 0、caps 声明 tools/ask/workspace；
	// context 按 D1 真值纪律——运行者没声明就**不发**（GUI 显示 —），不猜。
	hello := map[string]any{"t": "hello", "protocol": 4, "core": "0.4.0",
		"session": "s-real-1",
		"caps":    []string{"tools", "ask", "workspace", "model"}}
	if s.ctxCapacity > 0 {
		hello["context"] = s.ctxCapacity
	}
	s.emit(hello)
	s.log("hello sent; initial workspace: %s; model=%s", s.cwd, cfg.Model)
	return s
}

// ── 出站薄包装（与假 core 同形；真 core 没有 drop-seq 旋钮）────────

func (s *session) emit(payload map[string]any) { s.out.Emit(payload) }
func (s *session) st() wire.State              { return wire.StateOf(s.stc.Load()) }

func (s *session) setState(v wire.State) {
	s.stc.Store(wire.CodeOf(v))
	s.emit(map[string]any{"t": "state", "value": string(v)})
}

func (s *session) nextBlockID() string {
	s.blockNo++
	return fmt.Sprintf("b%d", s.blockNo)
}

// ── 主循环 ──────────────────────────────────────────────────────

func (s *session) run() error {
	s.log("waiting for gui hello")
	for sl := range s.in {
		if s.processLine(sl) {
			return nil
		}
	}
	s.log("stdin EOF — exiting")
	return nil
}

// processLine：轮间的一条 gui→core 消息。返回 true = 已发 bye，调用方该退出。
// （轮内到达的消息由 turnListener 收走、轮后经 drain 回到这里处置——
//   判据用的是**到达态**戳，所以「轮内抢跑的 prompt」在这里照样被拒绝。）
func (s *session) processLine(sl wire.StampedLine) bool {
	msg := wire.ParseGui(sl.Text)
	arrived := wire.StateOf(sl.St)
	switch msg.T {
	case "hello":
		s.log("hello received: seq=%d", msg.Seq)
		if s.out.Seq() == 1 && msg.Seq != 0 {
			s.log("WARNING: gui hello.seq=%d (spec requires 0)", msg.Seq)
		}
	case "prompt":
		s.log("prompt received: %q (arrived=%s)", msg.Text, arrived)
		if s.pendingWs != nil {
			s.rejectPrompt(msg, "工作区请求未决（一次只允许一个），本轮 prompt 被拒绝")
			return false
		}
		if arrived != wire.StateIdle {
			s.rejectPrompt(msg, fmt.Sprintf("prompt 到达时状态是 %s，只接受 idle 时的 prompt", arrived))
			return false
		}
		s.handlePrompt(msg.Text, msg.ClientID)
	case "workspace":
		s.handleWorkspace(msg, arrived)
	case "model":
		s.handleModelSwitch(msg, arrived)
	case "cancel":
		s.log("cancel received between turns (state=%s) — ignored", s.st())
	case "answer":
		s.log("WARNING: answer for unknown/expired ask %q", msg.ID)
	case "bye":
		s.log("bye received — exiting")
		s.emit(map[string]any{"t": "bye", "reason": "eof", "code": 0})
		return true
	default:
		s.log("ignoring unknown gui message type %q (I5)", msg.T)
	}
	return false
}

// ── 一轮交互：agent loop ────────────────────────────────────────

func (s *session) handlePrompt(text, clientID string) {
	s.turn++
	s.setState(wire.StateRunning)

	// ① 回显用户（协议 §4 + I9；缺失 client_id 补 auto- 前缀本地 id——
	//    与假 core 同一条理由：core 声明了 v3，就不能由自己写出违规消息）。
	echoID := clientID
	if echoID == "" {
		echoID = fmt.Sprintf("auto-%d", s.turn)
		s.log("WARNING: prompt without client_id — echoing synthesised id %q", echoID)
	}
	userID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": userID,
		"kind": "user", "text": text, "client_id": echoID})
	s.emit(map[string]any{"t": "block_end", "id": userID, "ok": true})

	// ② 历史追加用户消息（多轮累积；系统消息不进历史，见 buildRequestMessages）
	s.history = append(s.history, chatMessage{Role: "user", Content: text})

	// ③ 整轮期间的输入监听：cancel 打断 HTTP 流（A6）、answer 喂给 ask 门、
	//    prompt/workspace 先进 deferred 队列（轮后按**到达态**处置——
	//    stampedLine 的戳保证「轮内到达」不会被误判成「轮后到达」）。
	turnCtx, cancelTurn := context.WithCancel(context.Background())
	answerCh := make(chan wire.GuiMsg, 4)
	l := &turnListener{sess: s, cancel: cancelTurn, answers: answerCh, done: make(chan struct{})}
	l.start()

	canceled := s.chatLoop(turnCtx, answerCh)
	cancelTurn()
	l.stop() // listener 退出后 s.in 的消费者只剩主循环（两个消费者 = 抢消息）

	if l.exitBye {
		s.log("bye during turn — exiting")
		s.emit(map[string]any{"t": "bye", "reason": "eof", "code": 0})
		os.Exit(0)
	}
	// 轮内到达、被 listener 收走的 prompt/workspace：按到达态补处置。
	//（到达态 = 戳；即便此刻已回 idle，§3.2/§3.9 的判据也是「到达时刻」。）
	for _, sl := range l.drain() {
		s.processLine(sl)
	}

	// ④ usage（API 真值；API 没给就**不发**——估算 = 编造读数）。
	//   ★ v4：带 context 分母（§3.7 按轮覆盖——热切换改变容量后 GUI 的
	//     百分比环靠它跟上）。
	if s.lastUsage != nil {
		u := map[string]any{"t": "usage", "turn": s.turn,
			"input": s.lastUsage.PromptTokens, "output": s.lastUsage.CompletionTokens}
		if s.ctxCapacity > 0 {
			u["context"] = s.ctxCapacity
		}
		s.emit(u)
	}

	// ⑤ 状态收尾（cancel 路径与假 core 同形：cancelled → idle）
	if canceled {
		s.setState(wire.StateCancelled)
	}
	s.setState(wire.StateIdle)
}

// listenDuringTurn 已由 turnListener 取代（见下）——turn 期间必须有人读 stdin，
// 否则用户点「停止」永远到不了正在进行的 HTTP 流。

// turnListener = 整轮运行期间的 stdin 消费者（P3 与假 core 的关键差别）。
//
//   * cancel → cancelTurn()：正在进行的 HTTP 流立刻断（A6 的机制本体）；
//   * answer → answers 通道（ask 门消费）；
//   * bye    → exitBye + cancelTurn（handlePrompt 收尾后按协议发 bye 并退出）；
//   * prompt/workspace → **deferred 队列**：不就地处置（此刻状态机正在轮中间，
//     就地处置容易和 handlePrompt 的状态写入交错），轮后由 handlePrompt 用
//     **到达态**戳统一处置——「轮内到达」不会被误判成「轮后到达」。
type turnListener struct {
	sess    *session
	cancel  context.CancelFunc
	answers chan wire.GuiMsg
	done    chan struct{} // close = 本轮结束，listener 退出

	mu       sync.Mutex
	deferred []wire.StampedLine
	exitBye  bool
	wg       sync.WaitGroup
}

func (l *turnListener) start() {
	l.wg.Add(1)
	go func() {
		defer l.wg.Done()
		for {
			select {
			case sl, ok := <-l.sess.in:
				if !ok {
					// stdin EOF（GUI 关了管道）：打断请求让轮尽快收尾；
					// 轮后主循环读 s.in 也会拿到关闭，按协议静默退出（与假 core 同形）。
					l.sess.log("stdin EOF during turn — aborting request")
					l.cancel()
					return
				}
				msg := wire.ParseGui(sl.Text)
				switch msg.T {
				case "cancel":
					l.sess.log("cancel during turn — aborting current request")
					l.cancel()
				case "answer":
					l.answers <- msg
				case "bye":
					l.sess.log("bye during turn — will exit after this turn")
					l.mu.Lock()
					l.exitBye = true
					l.mu.Unlock()
					l.cancel()
				default:
					// prompt / workspace / hello / 未知：轮后处置（deferred）
					l.mu.Lock()
					l.deferred = append(l.deferred, sl)
					l.mu.Unlock()
				}
			case <-l.done:
				return
			}
		}
	}()
}

// stop：关 done 并等 listener 真正退出。
func (l *turnListener) stop() {
	close(l.done)
	l.wg.Wait()
}

// drain：取走轮内 deferred 的消息。
func (l *turnListener) drain() []wire.StampedLine {
	l.mu.Lock()
	defer l.mu.Unlock()
	out := l.deferred
	l.deferred = nil
	return out
}

// chatLoop：LLM ⇄ 工具循环。返回是否被 cancel。
func (s *session) chatLoop(turnCtx context.Context, answerCh chan wire.GuiMsg) bool {
	for {
		if turnCtx.Err() != nil {
			return true
		}
		textBlocks := &streamBlocks{sess: s}
		calls, usage, err := s.llm.Stream(turnCtx, s.buildRequestMessages(), toolDefs(),
			func(content, reasoning string) {
				textBlocks.onDelta(content, reasoning, turnCtx)
			})
		textBlocks.closeAll(turnCtx.Err() == nil)
		if usage != nil {
			s.lastUsage = usage
		}
		if err != nil {
			if turnCtx.Err() != nil {
				s.log("turn canceled by user")
				return true
			}
			// API 错误：fatal:false + state=idle，会话继续（§4.6；不自动重试）
			s.log("LLM error: %v", err)
			s.emit(map[string]any{"t": "error", "fatal": false,
				"message": fmt.Sprintf("模型请求失败：%v", err)})
			return false
		}
		if len(calls) == 0 {
			return false // 模型不再发起调用 ⇒ 一轮结束
		}

		// ① 追加 assistant 消息（content + tool_calls 原样回传——OpenAI 兼容
		//    协议要求后续 tool 消息能对上这些 id）
		var apiCalls []apiToolCall
		for i, c := range calls {
			apiCalls = append(apiCalls, apiToolCall{ID: c.ID, Type: "function",
				Index: i, Function: funcSpec{Name: c.Name, Arguments: c.Arguments}})
		}
		s.history = append(s.history, chatMessage{Role: "assistant",
			Content: textBlocks.assistantText.String(), ToolCalls: apiCalls})

		// ② 串行执行（I3 只允许一个未决 ask ⇒ 不并行，规格 §7）
		for _, c := range calls {
			if turnCtx.Err() != nil {
				// cancel 打断：剩余调用补一条占位 tool 消息，保持历史完整
				//（否则下一轮请求会带一个没有 tool 响应的 tool_calls）。
				s.history = append(s.history, chatMessage{Role: "tool",
					ToolCallID: c.ID, Content: "（用户取消，未执行）"})
				continue
			}
			s.runToolCall(turnCtx, c, answerCh)
		}
	}
}

// runToolCall：一次真实工具调用的完整协议链：
// tool_call 块 → ask 门（§4.3）→ 执行 → tool_result 块 → tool_call 收尾 → 回模型。
func (s *session) runToolCall(turnCtx context.Context, c aggregatedCall, answerCh chan wire.GuiMsg) {
	argsMap := parseArgs(c.Arguments)
	callID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": callID,
		"kind": "tool_call", "name": c.Name, "args": argsMap})

	denied := false
	if s.allowTools[c.Name] {
		s.log("tool %s allowed by --allow-tools (skipping ask)", c.Name)
	} else {
		askID := fmt.Sprintf("q%d", s.out.Seq())
		s.emit(map[string]any{"t": "ask", "id": askID, "kind": "permission",
			"question": fmt.Sprintf("允许执行 %s 吗？参数：%s", c.Name, summarizeArgs(c.Arguments)),
			"options": []map[string]string{
				{"key": "allow", "label": "允许"},
				{"key": "deny", "label": "拒绝"},
			},
			"default": "deny"})
		s.setState(wire.StateWaiting)
		s.log("waiting for answer to %s (I3: no further messages)", askID)

		// I3 自守：阻塞等 answer / cancel / bye（唯一允许的例外 = usage，本实现不发）
	waitLoop:
		for {
			select {
			case m := <-answerCh:
				if m.ID == askID {
					s.log("ask %s resolved: %s", m.ID, m.Choice)
					if m.Choice != "allow" {
						denied = true
					}
					break waitLoop
				}
				s.log("WARNING: answer for unknown ask %q (pending=%q)", m.ID, askID)
			case <-turnCtx.Done():
				s.log("cancel during waiting → cancelled")
				break waitLoop
			}
		}
		if s.st() != wire.StateRunning {
			s.setState(wire.StateRunning)
		}
	}

	var res toolResult
	switch {
	case turnCtx.Err() != nil:
		res = toolResult{OK: false, Output: "（用户取消）"}
	case denied:
		res = toolResult{OK: false, Output: "用户拒绝了该操作。"}
	default:
		res = executeTool(c.Name, argsMap, s.cwd)
		s.log("tool %s executed: ok=%v len=%d", c.Name, res.OK, len(res.Output))
	}

	// tool_result 块（I7：必须带 for 指向已存在的 tool_call）
	resultID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": resultID,
		"kind": "tool_result", "for": callID})
	s.emit(map[string]any{"t": "delta", "id": resultID, "text": res.Output})
	s.emit(map[string]any{"t": "block_end", "id": resultID, "ok": res.OK})

	// tool_call 收尾（REQ-02：GUI 的工具卡标题用 block_end.summary 更新）
	summary := "已执行"
	if denied {
		summary = "用户拒绝"
	} else if turnCtx.Err() != nil {
		summary = "已取消"
	} else if !res.OK {
		summary = "执行失败"
	}
	s.emit(map[string]any{"t": "block_end", "id": callID, "ok": res.OK, "summary": summary})

	// 结果回模型（GUI 渲染与模型输入同一份文本，§4.4）
	s.history = append(s.history, chatMessage{Role: "tool",
		ToolCallID: c.ID, Content: res.Output})
}

// ── LLM 流的块映射 ──────────────────────────────────────────────

// streamBlocks：把 LLM 的 content/reasoning 增量映射成协议块。
// reasoning 块在**第一个正文增量**到达时收尾（正文跟在思维链后面）；
// 全部块在流结束时收尾。cancel/错误路径 closeAll(false)。
type streamBlocks struct {
	sess          *session
	reasoningID   string
	textID        string
	assistantText strings.Builder
}

func (b *streamBlocks) onDelta(content, reasoning string, ctx context.Context) {
	if ctx.Err() != nil {
		return
	}
	if reasoning != "" {
		if b.reasoningID == "" {
			b.reasoningID = b.sess.nextBlockID()
			b.sess.emit(map[string]any{"t": "block_start", "id": b.reasoningID, "kind": "reasoning"})
		}
		b.sess.emit(map[string]any{"t": "delta", "id": b.reasoningID, "text": reasoning})
	}
	if content != "" {
		if b.reasoningID != "" && b.textID == "" {
			// 正文开始 ⇒ 思维链结束（同一次增量也可能两者都有）。
			// ★ 必须清 reasoningID：closeAll 只管「仍打开的块」——
			//   不清会二次发 end，正是 I1 抓出来的那类缺陷。
			b.sess.emit(map[string]any{"t": "block_end", "id": b.reasoningID, "ok": true})
			b.reasoningID = ""
		}
		if b.textID == "" {
			b.textID = b.sess.nextBlockID()
			b.sess.emit(map[string]any{"t": "block_start", "id": b.textID, "kind": "assistant"})
		}
		b.sess.emit(map[string]any{"t": "delta", "id": b.textID, "text": content})
		b.assistantText.WriteString(content)
	}
}

func (b *streamBlocks) closeAll(ok bool) {
	if b.reasoningID != "" {
		b.sess.emit(map[string]any{"t": "block_end", "id": b.reasoningID, "ok": ok})
		b.reasoningID = "" // closeAll 只该调一次；防重入
	}
	if b.textID != "" {
		b.sess.emit(map[string]any{"t": "block_end", "id": b.textID, "ok": ok})
		b.textID = ""
	}
}

// ── 请求消息组装 ────────────────────────────────────────────────

// buildRequestMessages：系统消息（现建——工作区切换即时生效）+ 历史。
func (s *session) buildRequestMessages() []chatMessage {
	sys := fmt.Sprintf("你是一个运行在用户本机 GUI（harness）里的 agent。当前工作区：%s。\n"+
		"可用工具均为只读，相对路径以工作区为根解析。用用户的语言回答。", s.cwd)
	msgs := make([]chatMessage, 0, len(s.history)+1)
	msgs = append(msgs, chatMessage{Role: "system", Content: sys})
	return append(msgs, s.history...)
}

// parseArgs：arguments 字符串 → map（GUI 的 args 卡片直接显示）。
// 解析失败回退为 {"_raw": 原文}——参数串是模型生成的，不保证合法 JSON。
func parseArgs(raw string) map[string]any {
	var m map[string]any
	if raw != "" && json.Unmarshal([]byte(raw), &m) == nil {
		return m
	}
	if raw == "" {
		return map[string]any{}
	}
	return map[string]any{"_raw": raw}
}

// summarizeArgs：ask 问题里的参数摘要（≤200 字符——问题要一眼能读完）。
// ★ 按 rune 截断：省略号「…」占 3 字节，按字节切会把摘要切在多字节字符中间。
func summarizeArgs(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		raw = "（无参数）"
	}
	runes := []rune(raw)
	if len(runes) > 200 {
		raw = string(runes[:200]) + "…"
	}
	return raw
}

// ── P3.6（协议 v4）：model 热切换 ────────────────────────────────

// sanitizeProviderID：提供商 id → 环境变量片段（非字母数字 → '_'，大写）。
// GUI 注入 ECDI_PROVIDER_<SAN>_URL / _KEY / _ID；core 按同一函数对齐——
// 两端各写一份是必须的（环境变量名不能带 '.'），但**映射规则只有这一条**。
func sanitizeProviderID(id string) string {
	out := make([]rune, 0, len(id))
	for _, r := range strings.ToUpper(id) {
		if (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') {
			out = append(out, r)
		} else {
			out = append(out, '_')
		}
	}
	return string(out)
}

// handleModelSwitch：`model` 请求（协议 §4 / I11）。
// 仅 idle 受理（GUI 在 running/waiting 时排队，idle 后才发——「等上一个完整
// 输出完」在 GUI 侧兑现；非 idle 到达 = 回 ok:false，与交错拒绝同规则）。
// 切换不改 state、不丢 history——agent loop 的 messages 原样带给新模型。
func (s *session) handleModelSwitch(msg wire.GuiMsg, arrived wire.State) {
	s.log("model switch request: id=%q provider=%q context=%d maxOutput=%d (arrived=%s)",
		msg.ID, msg.Provider, msg.Context, msg.MaxOutput, arrived)

	ack := map[string]any{"t": "model", "id": msg.ID, "ok": false,
		"provider": msg.Provider}
	if msg.ID == "" {
		ack["message"] = "model 缺少 id（I11）"
		s.emit(ack)
		return
	}
	if arrived != wire.StateIdle {
		ack["message"] = fmt.Sprintf("请求到达时状态是 %s，模型切换只在 idle 受理", arrived)
		s.emit(ack)
		return
	}
	san := sanitizeProviderID(msg.Provider)
	pc, ok := s.providers[msg.Provider]
	if !ok {
		for id, cand := range s.providers {
			if sanitizeProviderID(id) == san {
				pc, ok = cand, true
				break
			}
		}
	}
	if !ok {
		ack["message"] = "未知提供商（启动时未注入该提供商的凭据）"
		s.emit(ack)
		return
	}
	if msg.Context > 0 {
		s.ctxCapacity = int(msg.Context)
	}
	if msg.MaxOutput > 0 {
		s.maxOutput = int(msg.MaxOutput)
	}
	pc.Context = s.ctxCapacity
	pc.MaxOutput = s.maxOutput
	s.providers[msg.Provider] = pc
	s.llm = newLLMClient(pc, s.log)

	ack["ok"] = true
	ack["model"] = pc.Model
	ack["context"] = s.ctxCapacity
	ack["maxOutput"] = s.maxOutput
	s.emit(ack)
	s.log("model switched: provider=%q model=%q context=%d maxOutput=%d (history kept)",
		msg.Provider, pc.Model, s.ctxCapacity, s.maxOutput)
}

// ── v3：工作区租约（与假 core 同构；注释从简，语义见 wire/session 原始出处）──

func (s *session) rejectPrompt(msg wire.GuiMsg, reason string) {
	s.log("REJECT prompt (client_id=%q): %s", msg.ClientID, reason)
	payload := map[string]any{"t": "error", "fatal": false, "message": reason}
	if msg.ClientID != "" {
		payload["client_id"] = msg.ClientID
	}
	s.emit(payload)
}

func (s *session) handleWorkspace(msg wire.GuiMsg, arrived wire.State) {
	s.log("workspace request: id=%q path=%q (arrived=%s, pending=%v)",
		msg.RequestID, msg.Path, arrived, s.pendingWs != nil)
	if msg.RequestID == "" {
		s.emit(map[string]any{"t": "error", "fatal": false,
			"message": "workspace 缺少 request_id（v3 §3.9）"})
		return
	}
	if s.pendingWs != nil {
		s.nackWorkspace(msg.RequestID, msg.Path, "已有一个未决的工作区请求（一次只允许一个）")
		return
	}
	if arrived != wire.StateIdle {
		s.nackWorkspace(msg.RequestID, msg.Path,
			fmt.Sprintf("请求到达时状态是 %s，工作区切换只在 idle 受理", arrived))
		return
	}
	resolved, err := wire.NormalizeWorkspace(msg.Path)
	if err != nil {
		s.nackWorkspace(msg.RequestID, msg.Path, err.Error())
		return
	}
	// 同步完成（真 core 的目录检查是本地操作，无未决窗口；§3.9 不发 state）
	if err := os.Chdir(resolved); err != nil {
		s.nackWorkspace(msg.RequestID, msg.Path, fmt.Sprintf("切换失败：%v", err))
		return
	}
	s.cwd = resolved
	s.log("workspace switched: %s", s.cwd)
	s.emit(map[string]any{"t": "workspace", "request_id": msg.RequestID,
		"ok": true, "path": resolved})
}

func (s *session) nackWorkspace(requestID, rawPath, reason string) {
	s.log("REJECT workspace %s: %s", requestID, reason)
	s.emit(map[string]any{"t": "workspace", "request_id": requestID,
		"ok": false, "path": rawPath, "message": reason})
}
