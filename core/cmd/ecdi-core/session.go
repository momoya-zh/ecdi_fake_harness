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

// ── L2 防爆（docs/session-persistence-decisions.md §7.3）────────────
//
// 四层：源头限流（工具已有的硬上限）→ 水位前移 → Stage 1 折叠 → Stage 2/3。
// 所有数字都是具名常量，不散落魔数（§7.2 验收要求）。
const (
	// Stage 1：原文估算 ≥ 70% 窗口时启用旧工具载荷折叠（粘性）。
	maskTriggerRatio = 0.70
	// 保留最近 N 条 tool 结果（及其 tool_call 参数）原文。
	maskKeepRecentTools = 4
	// 无 ctxCapacity 时的绝对载荷软上限：未折叠工具载荷 > 256KB 就折叠。
	maskAbsoluteBytes = 256 << 10
	// 小于这个体积的载荷不值得折（占位文案本身也要空间）。
	maskMinPayloadBytes = 1024
	maskHeadRunes       = 300
	maskTailRunes       = 200
	// 摘要输出上限（rune）与摘要输入占窗口的比例上限。
	summaryMaxRunes   = 2000
	summaryInputRatio = 0.60
	// 硬顶预留的估算误差（2%）。
	contextMarginRatio = 0.02
	// 子代理保留最近 N 条 tool 结果，其余折叠（子代理上下文是一次性探查）。
	agentMaskKeepRecent = 2
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

	// ── L2 防爆（§7.3）：请求视图折叠 + 内部估算锚点 ────────────────
	// maskOn/maskKeep 只影响 buildRequestMessages 的**视图**：不删消息节点、
	// 不改 s.history、不写 context.jsonl（tool_call/tool 配对与 compact 的
	// replaces 索引因此天然保持不变）。
	maskOn          bool
	maskKeep        int
	maskNoticedTurn int
	// usageBase* = 上一次 API usage 对应的“视图基准”。它只是内部防爆判据的
	// 下限校准，绝不写进发给 GUI 的 usage（D1：usage 只发 API 真值）。
	// 任何视图变化（折叠档位/压缩/换模型/换工作区）都必须让它失效——
	// 否则会拿“折叠后的 prompt_tokens”去判断“要不要折叠原文视图”，
	// 得出 50% 的假水位然后在下一个请求直接爆窗。
	usageBaseTokens int
	usageBaseLen    int
	usageBaseTier   int
	usageBaseValid  bool
	toolDefsEst     int
	toolDefsEstOK   bool

	// ── L3 记忆文件（docs/session-persistence-decisions.md §8）────────
	globalMemoryDir string
	memResult       *memory.MemoryResult

	// ── P3.7（协议 v5）：子代理（docs/subagent-v5-plan.md）──────────
	agents   agentOpts     // --agents 一族；on=false 时整条委派路径不存在
	agentNo  atomic.Int64  // 子代理 id 计数器（a1 / a2 …），与块 id 分空间
	agentSem chan struct{} // 并发上限信号量（容量 = agents.maxParallel）

	// ── 协议 v6：后台长任务（docs/background-job-plan.md）──────────────
	jobs *jobHub

	// ── MCP Host：外部工具能力（docs/mcp-host-plan.md）───────────────
	mcp *mcpHub
}

// pendingWorkspace = 一个已受理、尚未应答的工作区请求（与假 core 同构）。
type pendingWorkspace struct {
	requestID  string
	requestRaw string
	resolved   string
}

func newSession(cfg llmConfig, allowTools []string, providerEnv map[string]llmConfig,
	resumeID, sessionID, workspace, globalMemory string, failSummarize bool, ag agentOpts,
	mcpConfig string,
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

	// ── MCP Host 初始化（发现并启动配置的服务，拉取工具列表）──
	if mcpHub, err := NewMCPHub(mcpConfig, s.cwd); err == nil {
		s.mcp = mcpHub
		if len(s.mcp.ToolDefs()) > 0 {
			s.log("mcp hub initialized: %d tools registered", len(s.mcp.ToolDefs()))
		}
	} else {
		s.log("WARNING: mcp hub initialization failed: %v", err)
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
	if s.mcp != nil && len(s.mcp.ToolDefs()) > 0 {
		caps = append(caps, "mcp")
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
//
//	归属由 `agent` 字段表达；分空间会让根代理的 id 也变（今天是 b1/b2…），
//	而「--agents 关闭时逐字节不变」是回归基线的前提。
func (s *session) nextBlockID() string {
	return fmt.Sprintf("b%d", s.blockNo.Add(1))
}

// ── 主循环 ──────────────────────────────────────────────────────

func (s *session) run() error {
	defer s.jobs.shutdown()
	defer func() {
		if s.mcp != nil {
			s.mcp.Close()
		}
	}()
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
//
//	判据用的是**到达态**戳，所以「轮内抢跑的 prompt」在这里照样被拒绝。）
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
		if s.mcp != nil {
			s.mcp.Close()
		}
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

	// ── L2 防爆预检（§7.3）────────────────────────────────────────
	// 轮首就把“即将追加的新 user 消息”计入硬顶判据（旧实现只看上一轮 API 的
	// lastUsage，不含新消息），并在压缩成功后用**重算的估算**再判一次——
	// 旧代码拿压缩前的 occupancy 判 100%，会出现“压完还是拒”。
	extra := contextlog.MessageOverheadTokens + contextlog.EstimateStringTokens(text)
	if _, ok := s.ensureContextBudget(extra); !ok {
		s.setState(wire.StateIdle)
		return
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
		if s.mcp != nil {
			s.mcp.Close()
		}
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
//   - cancel → cancelTurn()：正在进行的 HTTP 流立刻断（A6 的机制本体）；
//   - answer → answers 通道（ask 门消费）；
//   - bye    → exitBye + cancelTurn（handlePrompt 收尾后按协议发 bye 并退出）；
//   - prompt/workspace → **deferred 队列**：不就地处置（此刻状态机正在轮中间，
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

// toolDefs：上交给模型的根代理完整工具列表（内置 + 子代理 task + MCP 扩展工具）。
func (s *session) toolDefs() []toolDef {
	defs := toolDefsFor(s.agents.on, 0)
	if s.mcp != nil {
		defs = append(defs, s.mcp.ToolDefs()...)
	}
	return defs
}

// chatLoop：LLM ⇄ 工具循环。返回是否被 cancel。
func (s *session) chatLoop(turnCtx context.Context, answerCh chan wire.GuiMsg) bool {
	for {
		if turnCtx.Err() != nil {
			return true
		}
		// ── 每一次请求前都过水位（§7.3）：单轮连续 5–15 次工具调用时，
		//    轮首那一次检查远远不够。ensure 在硬顶仍破时已发 error。
		reqMsgs, ok := s.ensureContextBudget(0)
		if !ok {
			return false
		}
		textBlocks := &streamBlocks{sess: s}
		calls, usage, err := s.llm.Stream(turnCtx, reqMsgs, s.toolDefs(),
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
		asstContent := textBlocks.assistantText.String()
		if len(calls) == 0 {
			// 收尾的 assistant 文本也必须进历史：下一轮模型看不到自己上一轮
			// 的结论时，多轮对话会退化成“只记得用户说过什么”。（fake core
			// 一直是这样做的；真 core 原先漏了。）
			if strings.TrimSpace(asstContent) != "" {
				s.history = append(s.history, chatMessage{Role: "assistant", Content: asstContent})
				if s.ctxWriter != nil {
					_ = s.ctxWriter.AppendMsg(contextlog.Message{
						Role: "assistant", Content: asstContent,
					})
				}
				s.recordUsageBase(usage, len(s.history), true)
			} else {
				s.recordUsageBase(usage, len(s.history), false)
			}
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
		s.history = append(s.history, chatMessage{Role: "assistant",
			Content: asstContent, ToolCalls: apiCalls})
		if s.ctxWriter != nil {
			_ = s.ctxWriter.AppendMsg(contextlog.Message{
				Role: "assistant", Content: asstContent, ToolCalls: tcLog,
			})
		}
		// 基准点放在 assistant 之后、tool 结果之前：usage.CompletionTokens
		// 已经覆盖这条 assistant（含 tool_calls 参数），后面的工具结果才算“新增”。
		s.recordUsageBase(usage, len(s.history), true)

		// ② 执行（I3 只允许一个未决 ask ⇒ 串行；唯一例外是 `task` 委派，
		//    它的并发边界在 runToolCalls 里。规格 §7）
		s.runToolCalls(turnCtx, calls, answerCh)
	}
}

// runToolCalls：把一条 assistant 消息里的全部工具调用执行完。
//
// ★ 没有 `task` 调用、或子代理没开启时，就是今天那条**顺序循环**——
//
//	逐字节不变（`--agents` 关闭是回归基线的对照臂）。
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
// （否则下一轮请求会带一个没有 tool 响应的 tool_calls）。
// ★ 不发块：那个 tool_call 块在取消路径下也没有 start（本循环在 emit 之前就
//
//	退出了），凭空补一个 end 会违反 I1。
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
// （后者要带授权范围，两条问题不是同一个句式）。
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
		res = s.execTool(turnCtx, c.Name, argsMap, callID)
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

	// MCP 服务死掉要**说出来**：一条 system 块（与「上下文压缩失败」同一处置）。
	// 不说的话，用户看到的是「工具反复失败」，而真相是子进程没了、本会话不会重启它。
	s.emitMCPDeadNotices()

	// 结果回模型（GUI 渲染与模型输入同一份文本，§4.4）
	s.history = append(s.history, chatMessage{Role: "tool",
		ToolCallID: c.ID, Content: res.Output})
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(contextlog.Message{Role: "tool", Content: res.Output, ToolCallID: c.ID})
	}
}

// execTool：内置分发 / MCP 路由的**唯一**入口。
//
// ★ 必须看 CallTool 的 `handled`（2026-11-05 评审修）：`HasTool` 与 `CallTool`
//
//	是两次独立加锁，中间被 `hub.Close()` 清表（bye / EOF 路径）时，旧写法
//	`res, _ =` 会拿到零值 toolResult ⇒ 发出一条**空输出、ok=false** 的
//	tool_result 进 history，模型看到的是空字符串而不是「未知工具」。
func (s *session) execTool(ctx context.Context, name string, args map[string]any, callID string) toolResult {
	if s.mcp != nil {
		if r, handled := s.mcp.CallTool(ctx, name, args); handled {
			return r
		}
	}
	return executeToolWithJobs(name, args, s.cwd, s.jobs, callID)
}

// emitMCPDeadNotices：把「MCP 服务已退出」发成一条 system 块。
// 取走即清（hub 侧幂等），所以同一次死亡只说一次。发不出去（nil hub）就什么都不做。
func (s *session) emitMCPDeadNotices() {
	if s.mcp == nil {
		return
	}
	for _, note := range s.mcp.TakeDeadNotices() {
		nid := s.nextBlockID()
		s.emit(map[string]any{"t": "block_start", "id": nid, "kind": "system", "text": note})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": false})
		s.log("mcp: %s", note)
	}
}

// ── L2 防爆：估算、折叠、预算压缩（docs/session-persistence-decisions.md §7.3）──

// estimateChatMessage = 单条请求消息的保守 token 估算（与 contextlog 同口径）。
func estimateChatMessage(m chatMessage) int {
	n := contextlog.MessageOverheadTokens +
		contextlog.EstimateStringTokens(m.Content) +
		contextlog.EstimateStringTokens(m.ToolCallID)
	for _, tc := range m.ToolCalls {
		n += contextlog.MessageOverheadTokens +
			contextlog.EstimateStringTokens(tc.ID) +
			contextlog.EstimateStringTokens(tc.Function.Name) +
			contextlog.EstimateStringTokens(tc.Function.Arguments)
	}
	return n
}

func estimateChatMessages(msgs []chatMessage) int {
	total := 0
	for _, m := range msgs {
		total += estimateChatMessage(m)
	}
	return total
}

// toolDefsTokens：tools 数组的估算（工具 schema 也是 prompt 的一部分）。
// 同一会话内工具表不变，缓存一次即可。
func (s *session) toolDefsTokens() int {
	if s.toolDefsEstOK {
		return s.toolDefsEst
	}
	defs := s.toolDefs()
	n := 0
	if len(defs) > 0 {
		if b, err := json.Marshal(defs); err == nil {
			n = contextlog.EstimateStringTokens(string(b))
		}
	}
	s.toolDefsEst = n
	s.toolDefsEstOK = true
	return n
}

// outputReserve = 必须为请求体 max_tokens 预留的窗口。
// 只在配置合理（0 < maxOutput < ctx/2）时预留；否则视为配置异常，不拿它算硬顶。
func (s *session) outputReserve() int {
	if s.ctxCapacity <= 0 || s.maxOutput <= 0 || s.maxOutput >= s.ctxCapacity/2 {
		return 0
	}
	return s.maxOutput
}

// hardCap = 允许发送的 prompt token 上限；留 2% 给估算误差。
func (s *session) hardCap() int {
	if s.ctxCapacity <= 0 {
		return 0
	}
	margin := int(float64(s.ctxCapacity) * contextMarginRatio)
	cap := s.ctxCapacity - s.outputReserve() - margin
	if cap < 0 {
		cap = 0
	}
	return cap
}

// compressTrigger = Stage 2 软触发线。
// ★ 必须和 hardCap 取小：100k 窗口 + 32k max_tokens 时硬顶只有 68k，
//
//	若还按 85%×100k = 85k 触发，请求会先 400。
func (s *session) compressTrigger() int {
	if s.ctxCapacity <= 0 {
		return 0
	}
	trigger := int(float64(s.ctxCapacity) * contextlog.CompressThresholdRatio)
	if h := int(float64(s.hardCap()) * 0.90); h < trigger {
		trigger = h
	}
	return trigger
}

func (s *session) compressAllowed() bool {
	return s.lastCompressTurn == 0 || s.turn-s.lastCompressTurn >= 2
}

func (s *session) currentMaskTier() int {
	if !s.maskOn || s.maskKeep <= 0 {
		return 0
	}
	return s.maskKeep
}

func (s *session) invalidateUsageBase() { s.usageBaseValid = false }

// recordUsageBase：把“上一次请求的视图基准”记下来，供下次估算取下限。
// includeCompletion：assistant 消息（含 tool_calls）是否已追加进 history；
// 已追加则 completion 覆盖它，否则只算 prompt（最终回答未落历史时）。
func (s *session) recordUsageBase(u *chatUsage, historyLen int, includeCompletion bool) {
	if u == nil {
		return
	}
	base := u.PromptTokens
	if includeCompletion {
		base += u.CompletionTokens
	}
	s.usageBaseTokens = base
	s.usageBaseLen = historyLen
	s.usageBaseTier = s.currentMaskTier()
	s.usageBaseValid = true
}

// estimateRequest：渲染后请求的保守 token 估算。
// 扫描视图是主判据（视图变化时不会拿旧锚点算错）；API usage 锚点只在
// **同一视图**下作为下限校准（provider 的真实 tokenizer 通常比估算大）。
func (s *session) estimateRequest(msgs []chatMessage) int {
	total := estimateChatMessages(msgs) + s.toolDefsTokens()
	if s.usageBaseValid && s.usageBaseTier == s.currentMaskTier() &&
		s.usageBaseLen >= 0 && s.usageBaseLen <= len(s.history) {
		anchored := s.usageBaseTokens
		for _, m := range s.history[s.usageBaseLen:] {
			anchored += estimateChatMessage(m)
		}
		if anchored > total {
			total = anchored
		}
	}
	return total
}

// historyToolPayloadBytes：未折叠的工具载荷总量（含最近 K 条）。
func historyToolPayloadBytes(history []chatMessage) int {
	total := 0
	for _, m := range history {
		if m.Role == "tool" {
			total += len(m.Content)
		}
		for _, tc := range m.ToolCalls {
			total += len(tc.Function.Arguments)
		}
	}
	return total
}

type maskStats struct {
	ToolResults int
	ToolArgs    int
	BytesSaved  int
}

// maskHistory 返回请求视图：消息条数/顺序不变，只把较早的大工具载荷折叠。
// ★ 纯函数：不改 history；ToolCalls 切片先拷贝再改写，避免别名污染。
func maskHistory(history []chatMessage, keepRecent int) ([]chatMessage, maskStats) {
	var stats maskStats
	if len(history) == 0 || keepRecent <= 0 {
		return history, stats
	}
	keep := map[string]bool{}
	kept := 0
	for i := len(history) - 1; i >= 0 && kept < keepRecent; i-- {
		if history[i].Role == "tool" {
			keep[history[i].ToolCallID] = true
			kept++
		}
	}
	names := map[string]string{}
	for _, m := range history {
		for _, tc := range m.ToolCalls {
			names[tc.ID] = tc.Function.Name
		}
	}
	out := make([]chatMessage, len(history))
	for i, m := range history {
		out[i] = m
		switch m.Role {
		case "tool":
			if keep[m.ToolCallID] || len(m.Content) <= maskMinPayloadBytes {
				continue
			}
			folded := foldToolOutput(m.Content, names[m.ToolCallID])
			if len(folded) >= len(m.Content) {
				continue // 折了反而更大：不折
			}
			stats.ToolResults++
			stats.BytesSaved += len(m.Content) - len(folded)
			out[i].Content = folded
		case "assistant":
			if len(m.ToolCalls) == 0 {
				continue
			}
			calls := make([]apiToolCall, len(m.ToolCalls))
			copy(calls, m.ToolCalls)
			changed := false
			for j, tc := range calls {
				if keep[tc.ID] || len(tc.Function.Arguments) <= maskMinPayloadBytes {
					continue
				}
				folded := foldToolArguments(tc.Function.Arguments)
				if len(folded) >= len(tc.Function.Arguments) {
					continue
				}
				stats.ToolArgs++
				stats.BytesSaved += len(tc.Function.Arguments) - len(folded)
				calls[j].Function.Arguments = folded
				changed = true
			}
			if changed {
				out[i].ToolCalls = calls
			}
		}
	}
	return out, stats
}

// foldToolOutput：旧工具结果的头尾预览 + 显式长度标注。
// 只读工具可以“重新调用”；写/执行/委派类不能建议重跑（run_command 不幂等）。
func foldToolOutput(content, toolName string) string {
	runes := []rune(content)
	head := clipRunes(content, maskHeadRunes)
	tail := ""
	if len(runes) > maskHeadRunes+maskTailRunes {
		tail = string(runes[len(runes)-maskTailRunes:])
	}
	hint := "需要原文请重新调用对应工具。"
	switch {
	case isReadOnlyTool(toolName):
		hint = "需要原文请重新调用该工具。"
	case toolName != "":
		hint = "不要为了恢复原文重复执行该操作；需要时重新读取/重新生成。"
	}
	return fmt.Sprintf("[历史工具输出已折叠：原长 %d 字节，工具 %s。%s]\n%s\n…\n%s",
		len(content), toolName, hint, head, tail)
}

// foldToolArguments：把参数 JSON 里的大字段折成短预览，保留 path/command
// 这类元信息，且输出仍是合法 JSON（部分端点会对历史参数做 JSON 校验）。
func foldToolArguments(raw string) string {
	var m map[string]any
	if err := json.Unmarshal([]byte(raw), &m); err != nil {
		return fmt.Sprintf(`{"_folded":"[历史参数已折叠：原长 %d 字节]"}`, len(raw))
	}
	for k, v := range m {
		switch val := v.(type) {
		case string:
			if len(val) > 512 {
				m[k] = clipRunes(val, 200) + fmt.Sprintf("…[已折叠 %d 字节]", len(val))
			}
		default:
			b, err := json.Marshal(v)
			if err != nil || len(b) <= 512 {
				continue
			}
			m[k] = fmt.Sprintf("[已折叠 %d 字节]", len(b))
		}
	}
	if b, err := json.Marshal(m); err == nil {
		return string(b)
	}
	return fmt.Sprintf(`{"_folded":"[历史参数已折叠：原长 %d 字节]"}`, len(raw))
}

// emitNotice：system 块。压缩/折叠/失败都必须可见（本仓“反对静默改写”）。
func (s *session) emitNotice(text string, ok bool) {
	nid := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": nid, "kind": "system", "text": text})
	s.emit(map[string]any{"t": "block_end", "id": nid, "ok": ok})
}

func (s *session) emitMaskNoticeIfNeeded(stats maskStats) {
	if stats.ToolResults == 0 && stats.ToolArgs == 0 {
		return
	}
	if s.maskNoticedTurn == s.turn {
		return
	}
	s.maskNoticedTurn = s.turn
	s.emitNotice(fmt.Sprintf(
		"[已折叠较早的工具输出以控制上下文：%d 条结果、%d 处调用参数（原文仍在转录中，可重新读取）]",
		stats.ToolResults, stats.ToolArgs), true)
	s.log("context pruning: folded %d results / %d args, saved %d bytes",
		stats.ToolResults, stats.ToolArgs, stats.BytesSaved)
}

// maybeEnableMasking：Stage 1 的启用判据。
// 有 ctxCapacity：按原文估算 70% 触发（粘性，避免“折一次→读数变小→不折了”振荡）；
// 无 ctxCapacity：按绝对载荷 256KB 触发（§7.3 的裁决，不是猜分母）。
func (s *session) maybeEnableMasking(extraTokens int) {
	if s.maskOn {
		return
	}
	if s.ctxCapacity > 0 {
		raw := s.estimateRequest(s.buildRequestMessages()) + extraTokens
		if raw >= int(float64(s.ctxCapacity)*maskTriggerRatio) {
			s.maskOn = true
			s.maskKeep = maskKeepRecentTools
			s.invalidateUsageBase()
			s.log("context pruning enabled: raw estimate=%d tokens (ctx=%d)", raw, s.ctxCapacity)
		}
		return
	}
	if n := historyToolPayloadBytes(s.history); n > maskAbsoluteBytes {
		s.maskOn = true
		s.maskKeep = maskKeepRecentTools
		s.invalidateUsageBase()
		s.log("context pruning enabled by absolute payload cap (%d bytes)", n)
	}
}

// ensureContextBudget：每次发请求前的唯一水位入口（轮首 / 每次 Stream）。
// extraTokens = 尚未进入 history 的新内容（轮首的新 user 消息）。
// 返回最终应发送的 messages；ok=false 表示硬顶仍破，已发 error。
func (s *session) ensureContextBudget(extraTokens int) ([]chatMessage, bool) {
	s.maybeEnableMasking(extraTokens)

	msgs, stats := s.renderRequest()
	s.emitMaskNoticeIfNeeded(stats)
	base := s.estimateRequest(msgs)

	if s.ctxCapacity <= 0 {
		// 无分母：不做比例压缩 / LLM 摘要；只保留绝对载荷折叠（§7.3）。
		return msgs, true
	}

	trigger := s.compressTrigger()
	hard := s.hardCap()
	attempted := false

	if base >= trigger && s.compressAllowed() {
		attempted = true
		if s.compressContext(base) {
			msgs, stats = s.renderRequest()
			s.emitMaskNoticeIfNeeded(stats)
			base = s.estimateRequest(msgs)
		}
	}
	// 硬顶：即便冷却/未到软线也必须再试一次（只试一次，失败就进兜底）。
	if base+extraTokens > hard && !attempted {
		attempted = true
		if s.compressContext(base) {
			msgs, stats = s.renderRequest()
			s.emitMaskNoticeIfNeeded(stats)
			base = s.estimateRequest(msgs)
		}
	}
	// 硬顶压力下把折叠档位降到 1（只保留最后 1 条 tool 结果），再试。
	if base+extraTokens > hard && s.maskKeep > 1 {
		s.maskKeep = 1
		s.maskNoticedTurn = 0 // 更激进的折叠不是静默的：强制再发一条可见提示
		s.invalidateUsageBase()
		s.log("context hard cap: lowering mask keep to 1")
		msgs, stats = s.renderRequest()
		s.emitMaskNoticeIfNeeded(stats)
		base = s.estimateRequest(msgs)
	}
	// 最后兜底：确定性紧急压缩（只在合法 user 边界上切）。
	if base+extraTokens > hard {
		if s.emergencyCompact(base) {
			msgs, stats = s.renderRequest()
			s.emitMaskNoticeIfNeeded(stats)
			base = s.estimateRequest(msgs)
		}
	}
	if base+extraTokens > hard {
		s.log("context hard cap exceeded: estimate=%d extra=%d hard=%d ctx=%d",
			base, extraTokens, hard, s.ctxCapacity)
		s.emit(map[string]any{"t": "error", "fatal": false,
			"message": "上下文已达硬上限且压缩无法腾出足够空间，请新开会话"})
		return nil, false
	}
	return msgs, true
}

func (s *session) pct(tokens int) int {
	if s.ctxCapacity <= 0 {
		return 0
	}
	return int(float64(tokens) * 100 / float64(s.ctxCapacity))
}

// contextMessages：把当前请求视图转成 contextlog 消息（供切分/摘要）。
func (s *session) contextMessages() []contextlog.Message {
	hist := s.history
	if s.maskOn && s.maskKeep > 0 {
		hist, _ = maskHistory(s.history, s.maskKeep)
	}
	cmsgs := make([]contextlog.Message, 0, len(hist))
	for _, m := range hist {
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
	return cmsgs
}

func (s *session) summaryInputLimit() int {
	limit := int(float64(s.ctxCapacity) * summaryInputRatio)
	// 摘要请求自己也要给 max_tokens 留位置：不能占满 hardCap。
	if hard := s.hardCap(); hard > 0 {
		if h := int(float64(hard) * 0.90); h < limit {
			limit = h
		}
	}
	if limit < 1 {
		limit = 1
	}
	return limit
}

// buildSummaryPrompt：逐条预览 + 总长封顶。
// ★ 与旧实现的关键差别：assistant 的 tool_calls 参数也进摘要（旧实现只拼
//
//	Content，`write_file` 改过哪个文件、`run_command` 跑了什么都丢失）。
func (s *session) buildSummaryPrompt(cmsgs []contextlog.Message, cutTo int) string {
	const header = "请对以下对话历史做精炼摘要。必须保留：用户目标、已做出的决定、已改动的文件、未完成事项、关键约束。直接输出摘要正文，不要寒暄，不超过 500 字：\n\n"
	lines := make([]string, 0, cutTo+1)
	for i := 0; i <= cutTo; i++ {
		lines = append(lines, summaryLine(cmsgs[i]))
	}
	limit := s.summaryInputLimit()
	body := strings.Join(lines, "")
	dropped := 0
	for len(lines) > 1 && contextlog.EstimateStringTokens(header+body) > limit {
		// 保留第一条（通常是用户目标），从第二条开始丢最旧的。
		lines = append(lines[:1], lines[2:]...)
		dropped++
		body = fmt.Sprintf("[已省略较早的 %d 条消息]\n", dropped) + strings.Join(lines, "")
	}
	if contextlog.EstimateStringTokens(header+body) > limit {
		body = clipRunes(body, limit*2) // 最后手段：2 字符/token
	}
	return header + body
}

func summaryLine(m contextlog.Message) string {
	switch m.Role {
	case "tool":
		return fmt.Sprintf("[tool]: %s\n", clipRunes(m.Content, 200))
	case "assistant":
		var b strings.Builder
		b.WriteString(fmt.Sprintf("[assistant]: %s\n", clipRunes(m.Content, 300)))
		for _, tc := range m.ToolCalls {
			b.WriteString(fmt.Sprintf("  (调用 %s：%s)\n",
				tc.Function.Name, clipRunes(tc.Function.Arguments, 200)))
		}
		return b.String()
	default:
		return fmt.Sprintf("[%s]: %s\n", m.Role, clipRunes(m.Content, 500))
	}
}

// compressContext：Stage 2（LLM 摘要）。返回是否真的压缩了。
func (s *session) compressContext(before int) bool {
	if s.ctxCapacity <= 0 {
		return false
	}
	if s.failSummarize {
		s.log("compression failed due to --fail-summarize")
		s.emitNotice("[上下文压缩失败: 摘要生成失败，本轮照常发送]", false)
		return false
	}
	cmsgs := s.contextMessages()
	budget := int(float64(s.ctxCapacity) * contextlog.RetainRatio)
	if budget < 1 {
		budget = 1
	}
	cutTo, ok := contextlog.FindCompressionCutPoint(cmsgs, budget)
	if !ok || cutTo < 0 {
		s.log("compression skipped: no legal cut point")
		return false
	}
	summary, err := s.generateSummary(s.buildSummaryPrompt(cmsgs, cutTo))
	if err != nil {
		s.log("summary generation failed: %v", err)
		s.emitNotice("[上下文压缩失败: 摘要生成超时或报错，本轮照常发送]", false)
		return false
	}
	summary = clipRunes(strings.TrimSpace(summary), summaryMaxRunes)
	count := cutTo + 1

	// 先做“干跑”：换完历史后重算。若没变小（例如只剩一条旧摘要可换，
	// 或保留段本来就很小），就不要写 compact、不要发 notice——那只是
	// 把摘要越摘要越薄。★ 绝不再假写 50%。
	old := s.history
	s.history = append([]chatMessage{
		{Role: "user", Content: contextlog.SummaryPrefix + summary},
	}, old[cutTo+1:]...)
	s.invalidateUsageBase()
	after := s.estimateRequest(s.buildRequestMessages())
	if after >= before {
		s.history = old
		s.log("compression discarded: no size reduction (%d -> %d)", before, after)
		return false
	}
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendCompact(summary, 0, cutTo)
	}
	s.lastCompressTurn = s.turn

	notice := fmt.Sprintf("[上下文已压缩:%d 条 → 摘要,占用约 %d%% → 约 %d%%]",
		count, s.pct(before), s.pct(after))
	s.emitNotice(notice, true)
	s.log("compressed %d messages: %s", count, notice)
	return true
}

// deterministicSummary：Stage 3 的纯本地摘要（无 LLM、无网络）。
func deterministicSummary(msgs []contextlog.Message, cutTo int) string {
	var sb strings.Builder
	sb.WriteString("[自动紧急摘要] 以下为压缩前对话的确定性提取，可能丢失细节：\n")
	for i := 0; i <= cutTo; i++ {
		m := msgs[i]
		switch m.Role {
		case "user":
			if t := strings.TrimSpace(m.Content); t != "" {
				sb.WriteString("- 用户: " + clipRunes(t, 200) + "\n")
			}
		case "assistant":
			if t := strings.TrimSpace(m.Content); t != "" {
				sb.WriteString("- 助手: " + clipRunes(firstLine(t), 150) + "\n")
			}
			for _, tc := range m.ToolCalls {
				sb.WriteString(fmt.Sprintf("  - 调用 %s %s\n",
					tc.Function.Name, clipRunes(tc.Function.Arguments, 120)))
			}
		}
		if sb.Len() > 8000 {
			break
		}
	}
	return sb.String()
}

// emergencyCompact：硬顶将破且 LLM 摘要不可用时的最后兜底。
// ★ 只能在合法 user 边界上切；单轮内部无法安全压缩（会拆散 tool_call/tool 配对）。
func (s *session) emergencyCompact(before int) bool {
	if s.ctxCapacity <= 0 {
		return false
	}
	cmsgs := s.contextMessages()
	cutTo, ok := contextlog.FindCompressionCutPoint(cmsgs, 0)
	if !ok || cutTo < 0 {
		return false
	}
	summary := clipRunes(deterministicSummary(cmsgs, cutTo), summaryMaxRunes)
	if strings.TrimSpace(summary) == "" {
		return false
	}
	old := s.history
	s.history = append([]chatMessage{
		{Role: "user", Content: contextlog.SummaryPrefix + summary},
	}, old[cutTo+1:]...)
	s.invalidateUsageBase()
	after := s.estimateRequest(s.buildRequestMessages())
	// 最后兜底也要“至少腾出 10%”才值得写：否则只是在反复摘摘要。
	if after >= before || float64(after) > float64(before)*0.90 {
		s.history = old
		s.log("emergency compaction discarded: no meaningful reduction (%d -> %d)", before, after)
		return false
	}
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendCompact(summary, 0, cutTo)
	}
	s.lastCompressTurn = s.turn
	s.emitNotice(fmt.Sprintf(
		"[上下文已达硬上限，已用确定性规则紧急压缩：%d 条 → 摘要（可能丢失细节），建议新开会话]",
		cutTo+1), true)
	s.log("emergency compaction: %d messages, %d%% -> %d%%",
		cutTo+1, s.pct(before), s.pct(after))
	return true
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
//
//	且 delta 走**合批器**（见 agent.go deltaCoalescer）。根代理两者皆空 ⇒
//	输出与 v4 逐字节相同（delta 粒度是既有探针的读数基线，不动它）。
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

// renderRequest：系统消息（现建——工作区切换即时生效）+ **折叠后的历史视图**。
// ★ 返回的 stats 供 ensureContextBudget 发可见提示；它是纯函数，不改 s.history。
func (s *session) renderRequest() ([]chatMessage, maskStats) {
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
	hist := s.history
	var stats maskStats
	if s.maskOn && s.maskKeep > 0 {
		hist, stats = maskHistory(s.history, s.maskKeep)
	}
	msgs := make([]chatMessage, 0, len(hist)+1)
	msgs = append(msgs, chatMessage{Role: "system", Content: sys})
	return append(msgs, hist...), stats
}

// buildRequestMessages：系统消息 + 历史视图（折叠只影响这里，不改历史本身）。
func (s *session) buildRequestMessages() []chatMessage {
	msgs, _ := s.renderRequest()
	return msgs
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

	// 换模型 = 换 tokenizer + 换分母：折叠档位重新按新窗口评估，
	// 估算锚点立即失效（折叠可逆是选 B 的核心理由：切到大窗口能再用回原文）。
	s.maskOn = false
	s.maskKeep = maskKeepRecentTools
	s.maskNoticedTurn = 0
	s.invalidateUsageBase()

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
	// 记忆区段是 system prompt 的一部分：换了工作区，估算锚点即失效。
	s.invalidateUsageBase()
	s.emit(map[string]any{"t": "workspace", "request_id": msg.RequestID,
		"ok": true, "path": resolved})
}

func (s *session) nackWorkspace(requestID, rawPath, reason string) {
	s.log("REJECT workspace %s: %s", requestID, reason)
	s.emit(map[string]any{"t": "workspace", "request_id": requestID,
		"ok": false, "path": rawPath, "message": reason})
}
