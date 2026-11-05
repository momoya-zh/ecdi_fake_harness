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
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"ecdi-fake-core/internal/contextlog"
	"ecdi-fake-core/internal/memory"
	"ecdi-fake-core/internal/wire"
)

type session struct {
	in  <-chan wire.StampedLine
	out *wire.Out
	log func(string, ...any)
	stc *atomic.Int32

	turn    int
	blockNo atomic.Int64 // 块 id 计数器（★ v5：子代理并行后必须原子；见 nextBlockID）

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

	// ── L2 上下文持久与压缩（docs/session-persistence-decisions.md §7.2）───
	sessionID        string
	resumeID         string
	failSummarize    bool
	lastCompressTurn int
	ctxWriter        *contextlog.LogWriter

	// ── L3 记忆文件（docs/session-persistence-decisions.md §8）────────
	globalMemoryDir string
	memResult       *memory.MemoryResult

	// ── P3.7（协议 v5）：子代理（docs/subagent-v5-plan.md）──────────
	agents   agentOpts     // --agents 一族；on=false 时整条委派路径不存在
	agentNo  atomic.Int64  // 子代理 id 计数器（a1 / a2 …），与块 id 分空间
	agentSem chan struct{} // 并发上限信号量（容量 = agents.maxParallel）

	// ── 协议 v6：后台长任务（docs/background-job-plan.md）──────────────
	jobs *jobHub
}

// pendingWorkspace = 一个已受理、尚未应答的工作区请求（与假 core 同构）。
type pendingWorkspace struct {
	requestID  string
	requestRaw string
	resolved   string
}

func newSession(cfg llmConfig, allowTools []string, providerEnv map[string]llmConfig,
	resumeID, sessionID, workspace, globalMemory string, failSummarize bool, ag agentOpts,
	in <-chan wire.StampedLine, stc *atomic.Int32, log func(string, ...any),
	outW io.Writer) *session {

	s := &session{
		in:              in,
		out:             wire.NewOut(outW, false),
		log:             log,
		stc:             stc,
		llm:             newLLMClient(cfg, log),
		allowTools:      map[string]bool{},
		providers:       map[string]llmConfig{},
		ctxCapacity:     cfg.Context,
		maxOutput:       cfg.MaxOutput,
		sessionID:       sessionID,
		resumeID:        resumeID,
		failSummarize:   failSummarize,
		globalMemoryDir: globalMemory,
		agents:          ag.normalized(),
	}
	s.agentSem = make(chan struct{}, s.agents.maxParallel)
	s.jobs = newJobHub(s.emit)
	if s.sessionID == "" {
		s.sessionID = "s-real-1"
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
	if workspace != "" {
		if norm, err := wire.NormalizeWorkspace(workspace); err == nil {
			s.cwd = norm
		}
	}
	if s.cwd == "" {
		if wd, err := os.Getwd(); err == nil {
			if norm, err := wire.NormalizeWorkspace(wd); err == nil {
				s.cwd = norm
			}
		}
	}

	// L2 resume 处理：从 .meta.json + .jsonl 获取 lastSeq 续号与块序号，从 .context.jsonl 顺序重放历史
	if s.resumeID != "" {
		s.sessionID = s.resumeID
		var lastSeq int64 = -1
		metaPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".meta.json")
		if meta, err := contextlog.ReadMeta(metaPath); err == nil {
			lastSeq = meta.LastSeq
			if s.ctxCapacity == 0 && meta.ContextLimit > 0 {
				s.ctxCapacity = int(meta.ContextLimit)
			}
		}
		journalPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".jsonl")
		jMaxSeq, jMaxBlockNo, jMaxTurn := contextlog.ScanJournalState(journalPath)
		if jMaxSeq > lastSeq {
			lastSeq = jMaxSeq
		}
		if lastSeq >= 0 {
			startSeq := lastSeq + 1
			s.out.SetSeq(startSeq)
			if jMaxBlockNo < int(startSeq) {
				jMaxBlockNo = int(startSeq)
			}
			s.log("resumed session %s: starting seq=%d (lastSeq=%d, blockNo=%d)", s.sessionID, startSeq, lastSeq, jMaxBlockNo)
		}
		s.blockNo.Store(int64(jMaxBlockNo))
		s.turn = jMaxTurn
		ctxPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".context.jsonl")
		if msgs, err := contextlog.Replay(ctxPath); err == nil {
			for _, m := range msgs {
				var calls []apiToolCall
				for _, tc := range m.ToolCalls {
					calls = append(calls, apiToolCall{
						ID: tc.ID, Type: tc.Type, Index: tc.Index,
						Function: funcSpec{Name: tc.Function.Name, Arguments: tc.Function.Arguments},
					})
				}
				s.history = append(s.history, chatMessage{
					Role: m.Role, Content: m.Content, ToolCalls: calls, ToolCallID: m.ToolCallID,
				})
			}
			s.log("replayed %d messages from %s", len(s.history), ctxPath)
		}
		if w, err := contextlog.OpenLogWriter(ctxPath); err == nil {
			s.ctxWriter = w
		}
	} else {
		s.ensureCtxWriter()
	}

	if s.globalMemoryDir == "" {
		s.globalMemoryDir = memory.DefaultGlobalDir()
	}
	s.loadMemories()

	// hello（协议 §3.1）：seq 0、caps 声明 tools/ask/workspace/model；
	// context 按 D1 真值纪律——运行者没声明就**不发**（GUI 显示 —），不猜。
	// ★ v6（2026-11-05）：protocol 6（新增控制面消息 t:"job"）。版本号回答
	//   「有哪些消息类型」，caps 回答「这一份 core 实际提供哪几组能力」——
	//   两者并存，不互相替代。"jobs" 随后台任务实现恒声明。
	caps := []string{"tools", "ask", "workspace", "model", "jobs"}
	if s.agents.on {
		caps = append(caps, "agents")
	}
	hello := map[string]any{"t": "hello", "protocol": 6, "core": "0.6.0",
		"session": s.sessionID, "caps": caps}
	if s.ctxCapacity > 0 {
		hello["context"] = s.ctxCapacity
	}
	if s.memResult != nil && len(s.memResult.Files) > 0 {
		hello["memories"] = s.memResult.Files
	}
	s.emit(hello)
	s.log("hello sent (seq=%d, session=%s); initial workspace: %s; model=%s", s.out.Seq()-1, s.sessionID, s.cwd, cfg.Model)

	// L3 记忆通知（§8.2 / §8.3）：
	// 1. resume 且存在已加载记忆文件 ⇒ 发一条 kind:"system" 提示「记忆已更新」
	if s.resumeID != "" && s.memResult != nil && len(s.memResult.LoadedFiles) > 0 {
		nid := s.nextBlockID()
		s.emit(map[string]any{"t": "block_start", "id": nid, "kind": "system", "text": "[记忆已更新]"})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": true})
	}
	// 2. 超限跳过文件提示
	if s.memResult != nil && len(s.memResult.SkippedFiles) > 0 {
		var skippedNames []string
		for _, f := range s.memResult.SkippedFiles {
			skippedNames = append(skippedNames, f.Name)
		}
		nid := s.nextBlockID()
		s.emit(map[string]any{
			"t": "block_start", "id": nid, "kind": "system",
			"text": fmt.Sprintf("[记忆文件超限已跳过: %s]", strings.Join(skippedNames, ", ")),
		})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": true})
	}

	return s
}

func (s *session) loadMemories() {
	if s.cwd == "" {
		return
	}
	wsMemDir := filepath.Join(s.cwd, ".ecdi", "memory")
	res, err := memory.Load(s.globalMemoryDir, wsMemDir, s.ctxCapacity)
	if err != nil {
		s.log("failed to load memories: %v", err)
		return
	}
	s.memResult = res
	s.log("loaded memories: %d loaded, %d skipped", len(res.LoadedFiles), len(res.SkippedFiles))
}

func (s *session) ensureCtxWriter() *contextlog.LogWriter {
	if s.ctxWriter != nil {
		return s.ctxWriter
	}
	if s.sessionID == "" || s.cwd == "" {
		return nil
	}
	ctxPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".context.jsonl")
	if _, err := os.Stat(filepath.Dir(ctxPath)); err == nil {
		if w, err := contextlog.OpenLogWriter(ctxPath); err == nil {
			s.ctxWriter = w
		}
	}
	return s.ctxWriter
}

// ── 出站薄包装（与假 core 同形；真 core 没有 drop-seq 旋钮）────────

func (s *session) emit(payload map[string]any) { s.out.Emit(payload) }
func (s *session) st() wire.State              { return wire.StateOf(s.stc.Load()) }

// emitBlock：发一条 block_start，按需挂上 v5 的归属字段（协议 §3.3）。
// agentID/parentID 皆空 = 根代理的块，payload 与今天逐字节一致。
func (s *session) emitBlock(payload map[string]any, agentID, parentID string) {
	if agentID != "" {
		payload["agent"] = agentID
	}
	if parentID != "" {
		payload["parent"] = parentID
	}
	s.emit(payload)
}

func (s *session) setState(v wire.State) {
	s.stc.Store(wire.CodeOf(v))
	s.emit(map[string]any{"t": "state", "value": string(v)})
}

// nextBlockID：块 id（b1 / b2 …）。★ v5 起**原子**——子代理并行后有两路
// goroutine 同时分配块号，非原子的「读-加-写」会给出重复 id（I1 的直接违反）。
// ★ 刻意只有**一个**计数器、不按代理前缀分空间：id 只要流内唯一即满足契约，
//   归属由 `agent` 字段表达；分空间会让根代理的 id 也变（今天是 b1/b2…），
//   而「--agents 关闭时逐字节不变」是回归基线的前提。
func (s *session) nextBlockID() string {
	return fmt.Sprintf("b%d", s.blockNo.Add(1))
}

// ── 主循环 ──────────────────────────────────────────────────────

func (s *session) run() error {
	defer s.jobs.shutdown()
	defer func() {
		if s.ctxWriter != nil {
			_ = s.ctxWriter.Close()
		}
	}()
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
		// ★ I14：bye 之前必须把在跑作业的 end 发完（否则 journal 里留下
		//   "有 start 无 end" 的作业，历史回放时它永远停在运行中）。
		s.jobs.shutdown()
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
	s.ensureCtxWriter()

	// ── L2 压缩触发检查（§7.2）────────────────────────────────────
	if s.ctxCapacity > 0 && s.lastUsage != nil {
		occupancy := s.lastUsage.PromptTokens
		thresh := int(float64(s.ctxCapacity) * contextlog.CompressThresholdRatio)
		if occupancy >= thresh && (s.turn-s.lastCompressTurn >= 2 || s.lastCompressTurn == 0) {
			s.compressContext(occupancy)
		}
		if occupancy >= s.ctxCapacity {
			s.emit(map[string]any{
				"t": "error", "fatal": false,
				"message": "上下文已达 100% 且压缩无法腾出足够空间，请新开会话",
			})
			s.setState(wire.StateIdle)
			return
		}
	}

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
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(contextlog.Message{Role: "user", Content: text})
	}

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
		s.jobs.shutdown()
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
		calls, usage, err := s.llm.Stream(turnCtx, s.buildRequestMessages(), toolDefsFor(s.agents.on, 0),
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
		var tcLog []contextlog.ToolCall
		for i, c := range calls {
			apiCalls = append(apiCalls, apiToolCall{ID: c.ID, Type: "function",
				Index: i, Function: funcSpec{Name: c.Name, Arguments: c.Arguments}})
			tcLog = append(tcLog, contextlog.ToolCall{
				ID: c.ID, Type: "function", Index: i,
				Function: contextlog.FuncSpec{Name: c.Name, Arguments: c.Arguments},
			})
		}
		asstContent := textBlocks.assistantText.String()
		s.history = append(s.history, chatMessage{Role: "assistant",
			Content: asstContent, ToolCalls: apiCalls})
		if s.ctxWriter != nil {
			_ = s.ctxWriter.AppendMsg(contextlog.Message{
				Role: "assistant", Content: asstContent, ToolCalls: tcLog,
			})
		}

		// ② 执行（I3 只允许一个未决 ask ⇒ 串行；唯一例外是 `task` 委派，
		//    它的并发边界在 runToolCalls 里。规格 §7）
		s.runToolCalls(turnCtx, calls, answerCh)
	}
}

// runToolCalls：把一条 assistant 消息里的全部工具调用执行完。
//
// ★ 没有 `task` 调用、或子代理没开启时，就是今天那条**顺序循环**——
//   逐字节不变（`--agents` 关闭是回归基线的对照臂）。
//
// ★ 有 `task` 时按**三相**走，而不是「边跑边并行」。理由是 I3：
//
//	I3 只允许一个未决 ask，且 ask 之后、answer 之前**不得有任何消息**。
//	若让子代理在 ask 期间继续吐流，I3 就被破坏（GUI 会在等待回答的同时
//	不断收到缩进行——那不是"看起来乱"，是硬违规）。两条出路：
//	  (a) 给 Emit 加"静默门"写锁，ask 全程持锁 ⇒ 子代理全部阻塞在 Emit，
//	      用户的思考时间 = 整条转录冻结（可读性更差，且死锁面更大）；
//	  (b) **把 ask 全部搬到子代理启动之前**（本实现）⇒ 任何 ask 发生时
//	      都没有子代理在跑，I3 由**顺序**保证，不靠锁纪律。
//	这也是并行只发生在「同一条消息里的多个 task」上的原因——那正是
//	真正需要并行的场景（一次派三路探查）。
func (s *session) runToolCalls(turnCtx context.Context, calls []aggregatedCall, answerCh chan wire.GuiMsg) {
	if !s.agents.on || !hasTaskCall(calls) {
		for _, c := range calls {
			if turnCtx.Err() != nil {
				s.markToolCancelled(c)
				continue
			}
			s.runToolCall(turnCtx, c, answerCh)
		}
		return
	}

	// 相 1：非 task 调用照原样串行（此刻还没有子代理在跑 ⇒ 它们的 ask 安全）。
	for _, c := range calls {
		if c.Name == "task" {
			continue
		}
		if turnCtx.Err() != nil {
			s.markToolCancelled(c)
			continue
		}
		s.runToolCall(turnCtx, c, answerCh)
	}

	// 相 2a：逐个 task 过权限门（委派即授权）。仍无子代理在跑。
	var delegations []*delegation
	for _, c := range calls {
		if c.Name != "task" {
			continue
		}
		if turnCtx.Err() != nil {
			s.markToolCancelled(c)
			continue
		}
		delegations = append(delegations, s.prepareDelegation(turnCtx, c, answerCh))
	}

	// 相 2b：一起启动（受 --max-agents 信号量约束）。
	var wg sync.WaitGroup
	for _, d := range delegations {
		if !d.allowed || turnCtx.Err() != nil {
			continue
		}
		wg.Add(1)
		go func(d *delegation) {
			defer wg.Done()
			s.runDelegation(turnCtx, d)
		}(d)
	}
	wg.Wait()

	// 相 3：按原顺序结算（tool_result + tool_call 收尾 + 回模型的历史）。
	for _, d := range delegations {
		s.finishDelegation(d)
	}
}

// markToolCancelled：取消后未执行的调用补一条占位 tool 消息，保持历史完整
//（否则下一轮请求会带一个没有 tool 响应的 tool_calls）。
// ★ 不发块：那个 tool_call 块在取消路径下也没有 start（本循环在 emit 之前就
//   退出了），凭空补一个 end 会违反 I1。
func (s *session) markToolCancelled(c aggregatedCall) {
	s.history = append(s.history, chatMessage{Role: "tool",
		ToolCallID: c.ID, Content: "（用户取消，未执行）"})
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(contextlog.Message{
			Role: "tool", Content: "（用户取消，未执行）", ToolCallID: c.ID,
		})
	}
}

func hasTaskCall(calls []aggregatedCall) bool {
	for _, c := range calls {
		if c.Name == "task" {
			return true
		}
	}
	return false
}

// askQuestion：ask 门（§4.3 / I3）。返回「用户是否拒绝」。
//
// 从 runToolCall 里抽出来是为了 `task` 委派能复用同一道门（委派即授权 =
// 这一次确认覆盖整个子代理的只读工具面）。抽的是**整个 I3 自守块**，
// 不是"发消息那一行"——自守与该 ask 是同一条不变量的两半，分开就没人管。
//
// 问句由调用方给：普通工具走 askPrompt，委派走 delegationAskPrompt
//（后者要带授权范围，两条问题不是同一个句式）。
//
// ★ detail（2026-11-05，协议 v5 §3.6 可选字段）：结构化提案——write/edit 的
// unified diff、run/task 的作用域字段。nil = 不带该字段（消息与 v5.7 之前逐字节
// 同构；GUI 拿不到时回落问句文本）。问句仍是单行摘要，两者叠加不替代。
//
// ★ 返回 denied 而不是 allowed：取消与拒绝是两件事（取消由调用方看
// turnCtx.Err() 判定，优先级更高）。合并成 bool 会把「用户按了停止」
// 误报成「用户拒绝了该操作」，历史里那段文本会一直是错的。
func (s *session) askQuestion(turnCtx context.Context, question string,
	detail map[string]any, answerCh chan wire.GuiMsg) bool {

	askID := fmt.Sprintf("q%d", s.out.Seq())
	msg := map[string]any{"t": "ask", "id": askID, "kind": "permission",
		"question": question,
		"options": []map[string]string{
			{"key": "allow", "label": "允许"},
			{"key": "deny", "label": "拒绝"},
		},
		"default": "deny"}
	if detail != nil {
		msg["detail"] = detail
	}
	// ★ I3：ask 未答期间不得发任何消息 ⇒ 先把 job 事件门闩关上（emit 与 setPaused
	//   在 jobHub 的同一把锁上互斥，在途 flush 要么先发完、要么被挡住等 resume）。
	s.jobs.setPaused(true)
	s.emit(msg)
	s.setState(wire.StateWaiting)
	s.log("waiting for answer to %s (I3: no further messages)", askID)

	// I3 自守：阻塞等 answer / cancel / bye（唯一允许的例外 = usage，本实现不发）
	denied := false
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
	s.jobs.resume()
	return denied
}

// runToolCall：一次真实工具调用的完整协议链：
// tool_call 块 → ask 门（§4.3）→ 执行 → tool_result 块 → tool_call 收尾 → 回模型。
func (s *session) runToolCall(turnCtx context.Context, c aggregatedCall, answerCh chan wire.GuiMsg) {
	argsMap := parseArgs(c.Arguments)
	callID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": callID,
		"kind": "tool_call", "name": c.Name, "args": argsMap})

	denied := false
	control := s.jobs != nil && s.jobs.isControlTool(c.Name)
	if s.allowTools[c.Name] {
		s.log("tool %s allowed by --allow-tools (skipping ask)", c.Name)
	} else if control {
		// 作业控制工具不再过门：它们只作用于用户**已经批准过**的那条命令，
		// 读取输出 / 终止进程不扩大授权面（job_kill 也只认本会话自己的 job id）。
		s.log("tool %s is a job control tool (skipping ask)", c.Name)
	} else {
		// ★ detail 在问句之前组装（2026-11-05）：write_file 要读**批准前**的磁盘
		//   内容算 diff（diff.go）——只读，安全；执行仍在门后。
		denied = s.askQuestion(turnCtx, askPrompt(c.Name, argsMap, c.Arguments),
			askDetail(c.Name, argsMap, s.cwd), answerCh)
	}

	var res toolResult
	switch {
	case turnCtx.Err() != nil:
		res = toolResult{OK: false, Output: "（用户取消）"}
	case denied:
		res = toolResult{OK: false, Output: "用户拒绝了该操作。"}
	default:
		res = executeToolWithJobs(c.Name, argsMap, s.cwd, s.jobs, callID)
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
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(contextlog.Message{Role: "tool", Content: res.Output, ToolCallID: c.ID})
	}
}

// ── L2 上下文压缩（docs/session-persistence-decisions.md §7.2）──────────

func (s *session) compressContext(occupancy int) {
	if s.failSummarize {
		s.log("compression failed due to --fail-summarize")
		nid := s.nextBlockID()
		s.emit(map[string]any{
			"t": "block_start", "id": nid, "kind": "system",
			"text": "[上下文压缩失败: 摘要生成失败，本轮照常发送]",
		})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": false})
		return
	}

	var cmsgs []contextlog.Message
	for _, m := range s.history {
		var tcs []contextlog.ToolCall
		for _, tc := range m.ToolCalls {
			tcs = append(tcs, contextlog.ToolCall{
				ID: tc.ID, Type: tc.Type, Index: tc.Index,
				Function: contextlog.FuncSpec{Name: tc.Function.Name, Arguments: tc.Function.Arguments},
			})
		}
		cmsgs = append(cmsgs, contextlog.Message{
			Role: m.Role, Content: m.Content, ToolCalls: tcs, ToolCallID: m.ToolCallID,
		})
	}

	cutTo, ok := contextlog.FindCompressionCutPoint(cmsgs, 2)
	if !ok || cutTo < 0 {
		return
	}

	var sb strings.Builder
	for i := 0; i <= cutTo; i++ {
		m := cmsgs[i]
		sb.WriteString(fmt.Sprintf("[%s]: ", m.Role))
		content := m.Content
		if m.Role == "tool" && len([]rune(content)) > 200 {
			content = string([]rune(content)[:200]) + "…(截断)"
		}
		sb.WriteString(content)
		sb.WriteString("\n")
	}

	prompt := "请对以下对话历史做精炼摘要。必须保留：用户目标、已做出的决定、已改动的文件、未完成事项、关键约束。直接输出摘要正文，不要寒暄：\n\n" + sb.String()

	summary, err := s.generateSummary(prompt)
	if err != nil {
		s.log("summary generation failed: %v", err)
		nid := s.nextBlockID()
		s.emit(map[string]any{
			"t": "block_start", "id": nid, "kind": "system",
			"text": "[上下文压缩失败: 摘要生成超时或报错，本轮照常发送]",
		})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": false})
		return
	}

	count := cutTo + 1
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendCompact(summary, 0, cutTo)
	}

	sumMsg := chatMessage{
		Role:    "user",
		Content: contextlog.SummaryPrefix + summary,
	}
	newHistory := make([]chatMessage, 0, len(s.history)-count+1)
	newHistory = append(newHistory, sumMsg)
	newHistory = append(newHistory, s.history[cutTo+1:]...)
	s.history = newHistory

	afterOccupancy := int(float64(s.ctxCapacity) * contextlog.TargetRatio)
	if s.lastUsage != nil {
		s.lastUsage.PromptTokens = afterOccupancy
	}
	s.lastCompressTurn = s.turn

	beforePct := int(float64(occupancy) * 100 / float64(s.ctxCapacity))
	afterPct := int(float64(afterOccupancy) * 100 / float64(s.ctxCapacity))

	nid := s.nextBlockID()
	noticeText := fmt.Sprintf("[上下文已压缩:%d 条 → 摘要,占用 %d%% → %d%%]", count, beforePct, afterPct)
	s.emit(map[string]any{
		"t": "block_start", "id": nid, "kind": "system", "text": noticeText,
	})
	s.emit(map[string]any{"t": "block_end", "id": nid, "ok": true})
	s.log("compressed %d messages: %s", count, noticeText)
}

func (s *session) generateSummary(prompt string) (string, error) {
	reqMsgs := []chatMessage{
		{Role: "user", Content: prompt},
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	var out strings.Builder
	cb := func(content, reasoning string) {
		out.WriteString(content)
	}
	_, _, err := s.llm.Stream(ctx, reqMsgs, nil, cb)
	if err != nil {
		return "", err
	}
	res := strings.TrimSpace(out.String())
	if res == "" {
		return "历史对话已汇总，此前确认的目标、改动与约束已保留。", nil
	}
	return res, nil
}

// ── LLM 流的块映射 ──────────────────────────────────────────────

// streamBlocks：把 LLM 的 content/reasoning 增量映射成协议块。
// reasoning 块在**第一个正文增量**到达时收尾（正文跟在思维链后面）；
// 全部块在流结束时收尾。cancel/错误路径 closeAll(false)。
//
// ★ v5：agentID/parentID 非空时，每个 block_start 都带上归属（§3.3），
//   且 delta 走**合批器**（见 agent.go deltaCoalescer）。根代理两者皆空 ⇒
//   输出与 v4 逐字节相同（delta 粒度是既有探针的读数基线，不动它）。
type streamBlocks struct {
	sess     *session
	agentID  string
	parentID string

	reasoningID   string
	textID        string
	reasoningCoal *deltaCoalescer // 仅子代理非 nil
	textCoal      *deltaCoalescer
	assistantText strings.Builder
}

func (b *streamBlocks) onDelta(content, reasoning string, ctx context.Context) {
	if ctx.Err() != nil {
		return
	}
	if reasoning != "" {
		if b.reasoningID == "" {
			b.reasoningID = b.sess.nextBlockID()
			b.sess.emitBlock(map[string]any{"t": "block_start", "id": b.reasoningID, "kind": "reasoning"},
				b.agentID, b.parentID)
			b.reasoningCoal = b.newCoalescer(b.reasoningID)
		}
		b.writeDelta(b.reasoningID, b.reasoningCoal, reasoning)
	}
	if content != "" {
		if b.reasoningID != "" && b.textID == "" {
			// 正文开始 ⇒ 思维链结束（同一次增量也可能两者都有）。
			// ★ 必须清 reasoningID：closeAll 只管「仍打开的块」——
			//   不清会二次发 end，正是 I1 抓出来的那类缺陷。
			b.closeReasoning(true)
		}
		if b.textID == "" {
			b.textID = b.sess.nextBlockID()
			b.sess.emitBlock(map[string]any{"t": "block_start", "id": b.textID, "kind": "assistant"},
				b.agentID, b.parentID)
			b.textCoal = b.newCoalescer(b.textID)
		}
		b.writeDelta(b.textID, b.textCoal, content)
		b.assistantText.WriteString(content)
	}
}

func (b *streamBlocks) newCoalescer(id string) *deltaCoalescer {
	if b.agentID == "" {
		return nil // 根代理：直发，保持 v4 的 delta 粒度
	}
	return newDeltaCoalescer(b.sess, id)
}

func (b *streamBlocks) writeDelta(id string, coal *deltaCoalescer, text string) {
	if coal != nil {
		coal.add(text)
		return
	}
	b.sess.emit(map[string]any{"t": "delta", "id": id, "text": text})
}

func (b *streamBlocks) closeReasoning(ok bool) {
	if b.reasoningID == "" {
		return
	}
	if b.reasoningCoal != nil {
		b.reasoningCoal.close() // 先把积压的 delta 冲出去，再发 end
		b.reasoningCoal = nil
	}
	b.sess.emit(map[string]any{"t": "block_end", "id": b.reasoningID, "ok": ok})
	b.reasoningID = ""
}

func (b *streamBlocks) closeText(ok bool) {
	if b.textID == "" {
		return
	}
	if b.textCoal != nil {
		b.textCoal.close()
		b.textCoal = nil
	}
	b.sess.emit(map[string]any{"t": "block_end", "id": b.textID, "ok": ok})
	b.textID = ""
}

func (b *streamBlocks) closeAll(ok bool) {
	b.closeReasoning(ok)
	b.closeText(ok)
}

// ── 请求消息组装 ────────────────────────────────────────────────

// buildRequestMessages：系统消息（现建——工作区切换即时生效）+ 历史。
func (s *session) buildRequestMessages() []chatMessage {
	sys := fmt.Sprintf("你是一个运行在用户本机 GUI（harness）里的编码 agent。当前工作区：%s。\n"+
		"相对路径以工作区为根解析；越界路径（含符号链接指向外面）会被直接拒绝。\n"+
		"工具：read_file / list_dir / grep 可直接用；write_file / edit_file / run_command "+
		"每次都要经过用户确认，被拒绝时不要重试同一操作，先解释你要做什么或换个做法。\n"+
		"改动代码优先 edit_file（逐字节精确替换），只有新建或整体重写才用 write_file；"+
		"一条命令只做一件事，别把多步 shell 塞进一次调用。用用户的语言回答。", s.cwd)
	if s.agents.on {
		sys += fmt.Sprintf("\n你还可以用 task 工具把**只读**探查任务派给子代理"+
			"（同一条回复里给多个 task 会并行，最多 %d 路；每个子代理上限 %d 轮 / %s）。"+
			"子代理看不到我们的对话，任务描述必须自足；它只回一段结论，过程不进你的上下文。"+
			"适合互不依赖的调查（找入口、查定义、比对两处实现）；写文件/跑命令不要派——子代理只能读。",
			s.agents.maxParallel, s.agents.maxTurns, s.agents.timeout)
	}
	if s.memResult != nil && s.memResult.PromptText != "" {
		sys += s.memResult.PromptText
	}
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
	// ★ 两个容量参数是**指针**（省略 ≠ 0）：日志里要能看出"没带"与"带了 0"的区别，
	//   否则排查"切过去分母还是上一个模型的"时，日志会显示 context=0 而看不出根因。
	ctxDesc := "absent"
	if msg.Context != nil {
		ctxDesc = strconv.FormatInt(*msg.Context, 10)
	}
	outDesc := "absent"
	if msg.MaxOutput != nil {
		outDesc = strconv.FormatInt(*msg.MaxOutput, 10)
	}
	s.log("model switch request: id=%q provider=%q model=%q context=%s maxOutput=%s (arrived=%s)",
		msg.ID, msg.Provider, msg.Model, ctxDesc, outDesc, arrived)

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
	// ★ 2026-10-27：请求可指定**同一提供商下的另一个模型**（§4 可选 `model`）。
	//   空 = 沿用注册表里已登记的模型名。注册表条目也可能是空的（GUI 没注入
	//   `_MODEL`、老 GUI 也不发这个字段）——那种情况必须**如实拒绝**：放行会
	//   构造出请求体 `"model": ""`，端点回 400 时用户只能看到一句无关的 API
	//   错误，根因（模型名没传过来）被埋掉。
	if msg.Model != "" {
		pc.Model = msg.Model
	}
	if pc.Model == "" {
		ack["message"] = "该提供商没有可用模型名（请求未带 model，启动时也未注入 ECDI_PROVIDER_*_MODEL）"
		s.emit(ack)
		return
	}
	// ★★ 2026-10-27（v4 第三步）：两个参数**跟着模型走**，所以"没带"与"带了 0"
	//   必须分开处理：没带（老 GUI）= 沿用；带了 0 = 这个模型没标容量 ⇒ **清零**，
	//   让 usage 不再带 context（GUI 显示 —），请求体也不再带 max_tokens。
	//   原来一律 `> 0 才覆盖`，于是切到"没标容量"的模型时分母还是上一个模型的
	//   （百分比是假的），max_tokens 也照旧发上一个模型的上限。
	if msg.Context != nil {
		s.ctxCapacity = int(*msg.Context)
	}
	if msg.MaxOutput != nil {
		s.maxOutput = int(*msg.MaxOutput)
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
	if s.ctxWriter != nil {
		_ = s.ctxWriter.Close()
		s.ctxWriter = nil
	}
	s.log("workspace switched: %s", s.cwd)
	s.loadMemories()
	s.emit(map[string]any{"t": "workspace", "request_id": msg.RequestID,
		"ok": true, "path": resolved})
}

func (s *session) nackWorkspace(requestID, rawPath, reason string) {
	s.log("REJECT workspace %s: %s", requestID, reason)
	s.emit(map[string]any{"t": "workspace", "request_id": requestID,
		"ok": false, "path": rawPath, "message": reason})
}
