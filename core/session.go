package main

// session.go —— 假 core 的会话状态机（剧本逻辑）。
//
// 职责边界（协议 §6 / §5）：
//   * 状态机：idle → running → (waiting) → idle / cancelled / error；
//   * I3 自守：发出 ask 后到收到 answer 前，**不得再发任何消息**（usage 除外）；
//   * 注入开关：drop-seq（I4 负向）、drop-block-end（I1 负向）；
//   * v3 补：prompt.client_id 回显（§4）、workspace 租约（§3.9）。
//
// ★ 线层下沉（2026-10-24，P3 规格 §3）：seq 分配 / 信封字段序 / 即刷规则 /
//   gui→core 解析 / 状态编码 / 路径规范化移入 core/internal/wire/。本文件只保留
//   剧本与注入逻辑——emit 是 wire.Out.Emit 的一层薄包装，唯一附加行为是
//   --drop-seq 的跳号注入（真 core 没有这个旋钮）。

import (
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"

	"ecdi-fake-core/internal/contextlog"
	"ecdi-fake-core/internal/memory"
	"ecdi-fake-core/internal/wire"
)

type session struct {
	rng   *rand.Rand
	speed float64

	dropSeqEvery int
	dropBlockEnd bool

	// ★ --burst（2026-10-24）：关掉节奏等待，并把一轮输出攒在一次写出里。
	//   缓冲本体在 wire.Out（burst=true ⇒ 256KB + 控制面必刷）；
	//   这个布尔只管 sleep() 是否直接返回（积压要一次性到达，不是快一点）。
	burst bool
	out   *wire.Out

	in  <-chan wire.StampedLine
	log func(string, ...any)

	// 当前状态：**只经 stc 读写**（读线程要原子读它给输入打戳）。
	// 保留 stc 之外的副本必然漂移，所以 s.st 字段已删除。
	stc *atomic.Int32

	turn            int // 轮次计数（usage.turn）
	since           int // 自上次注入丢号以来发出的消息数
	blockEndSkipped bool // drop-block-end 只命中一次
	blockNo         int  // 块 id 计数器（与 seq 解耦——id 不依赖 seq 算术）

	// 未决 ask（I3）：非空时只接受 answer
	pendingAskID string
	// 当前进行中的回答（用于 cancel）
	cancelRequested bool

	// ── v3：工作区租约（协议 §3.9）──────────────────────────────
	// cwd = **core 实际采用的**规范化绝对路径（唯一真相；GUI 只认回显）。
	cwd string
	// 未决的 workspace 请求：非空期间拒绝 prompt 与第二个 workspace（交错）。
	pendingWs *pendingWorkspace
	// 应答延迟（毫秒）：0 = 同步应答（无未决窗口）；>0 时窗口真实存在，可被交错命中。
	workspaceDelayMs int
	// 延迟应答的定时器（nil 表示没有未决定时器；nil 通道在 select 中永不就绪）
	wsTimer <-chan time.Time

	// ── L2 上下文与压缩（docs/session-persistence-decisions.md §7.2）───
	sessionID        string
	resumeID         string
	contextLimit     int
	lastPromptTokens int
	lastCompressTurn int
	messages         []contextlog.Message
	failSummarize    bool
	ctxWriter        *contextlog.LogWriter

	// ── L3 记忆文件（docs/session-persistence-decisions.md §8）────────
	globalMemoryDir string
	memResult       *memory.MemoryResult
}

// pendingWorkspace = 一个已受理、尚未应答的工作区请求。
type pendingWorkspace struct {
	requestID  string
	requestRaw string // GUI 发来的原文（失败时原样回显，§3.9）
	resolved   string // 规范化后的绝对路径（应答成功后写入 cwd）
}

func newSession(seed int64, speed float64, dropSeq int, dropBlockEnd bool,
	wsDelayMs int, burst bool, resumeID, sessionID, workspace string,
	contextTokens int, failSummarize bool, globalMemory string,
	in <-chan wire.StampedLine, stc *atomic.Int32,
	log func(string, ...any)) *session {
	s := &session{
		rng:              rand.New(rand.NewSource(seed)),
		speed:            speed,
		dropSeqEvery:     dropSeq,
		dropBlockEnd:     dropBlockEnd,
		workspaceDelayMs: wsDelayMs,
		burst:            burst,
		out:              wire.NewOut(os.Stdout, burst),
		in:               in,
		log:              log,
		stc:              stc,
		contextLimit:     contextTokens,
		failSummarize:    failSummarize,
		sessionID:        sessionID,
		resumeID:         resumeID,
		globalMemoryDir:  globalMemory,
	}
	if s.sessionID == "" {
		s.sessionID = "s-live-1"
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
	s.log("initial workspace: %s", s.cwd)

	// L2 resume 处理：从 .meta.json 获取 lastSeq 续号，从 .context.jsonl 顺序重放历史
	if s.resumeID != "" {
		s.sessionID = s.resumeID
		metaPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".meta.json")
		if meta, err := contextlog.ReadMeta(metaPath); err == nil {
			startSeq := meta.LastSeq + 1
			s.out.SetSeq(startSeq)
			s.log("resumed session %s: starting seq=%d (from lastSeq=%d)", s.sessionID, startSeq, meta.LastSeq)
			if s.contextLimit == 0 && meta.ContextLimit > 0 {
				s.contextLimit = int(meta.ContextLimit)
			}
		}
		ctxPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".context.jsonl")
		if msgs, err := contextlog.Replay(ctxPath); err == nil {
			s.messages = msgs
			s.log("replayed %d messages from %s", len(msgs), ctxPath)
		}
		if w, err := contextlog.OpenLogWriter(ctxPath); err == nil {
			s.ctxWriter = w
		}
	} else if s.sessionID != "" && s.cwd != "" {
		ctxPath := filepath.Join(s.cwd, ".ecdi", "sessions", s.sessionID+".context.jsonl")
		if _, err := os.Stat(filepath.Dir(ctxPath)); err == nil {
			if w, err := contextlog.OpenLogWriter(ctxPath); err == nil {
				s.ctxWriter = w
			}
		}
	}

	if s.globalMemoryDir == "" {
		s.globalMemoryDir = memory.DefaultGlobalDir()
	}
	s.loadMemories()

	return s
}

func (s *session) loadMemories() {
	if s.cwd == "" {
		return
	}
	wsMemDir := filepath.Join(s.cwd, ".ecdi", "memory")
	res, err := memory.Load(s.globalMemoryDir, wsMemDir, s.contextLimit)
	if err != nil {
		s.log("failed to load memories: %v", err)
		return
	}
	s.memResult = res
	s.log("loaded memories: %d loaded, %d skipped", len(res.LoadedFiles), len(res.SkippedFiles))
}

// ── 出站 ────────────────────────────────────────────────────────

// emit：wire.Out.Emit 的薄包装，唯一附加行为 = --drop-seq 的跳号注入
// （真 core 不调 Skip；这是假 core 的 I4 负向注入旋钮）。
func (s *session) emit(payload map[string]any) {
	if s.dropSeqEvery > 0 {
		s.since++
		if s.since >= s.dropSeqEvery {
			s.since = 0
			old := s.out.Seq()
			s.out.Skip() // 故意空号：下一个消息的 seq 直接跳 1 ⇒ GUI 侧 I4 必须显式提示
			s.log("INJECT drop-seq: skipping %d (I4 negative)", old)
		}
	}
	s.out.Emit(payload)
}

// flush：--burst 缓冲的推出点（逐行即刷模式下 no-op；必刷点表见 wire.Out.Flush）。
func (s *session) flush() { s.out.Flush() }

// st：当前状态（唯一真相 = 共享原子量）。
func (s *session) st() wire.State { return wire.StateOf(s.stc.Load()) }

func (s *session) setState(v wire.State) {
	s.stc.Store(wire.CodeOf(v))
	s.emit(map[string]any{"t": "state", "value": string(v)})
}

// nextBlockID：块 id 与 seq 解耦（id 是块身份，不是消息序号——I1 的前提）。
func (s *session) nextBlockID() string {
	s.blockNo++
	return fmt.Sprintf("b%d", s.blockNo)
}

// ── 节奏 ────────────────────────────────────────────────────────

// sleep：按 speed 缩放的等待（流式节奏的真相源）。
// ★ `--burst` 下直接返回：积压要的是「一次性到达」，不是「快一点」——
//   逐行加快只会让 reader 一行一行地塞，队列永远厚不起来。
func (s *session) sleep(ms float64) {
	if s.burst {
		return
	}
	d := time.Duration(ms/s.speed) * time.Millisecond
	if d <= 0 {
		return
	}
	time.Sleep(d)
}

// ── 主循环 ──────────────────────────────────────────────────────

func (s *session) run() error {
	defer func() {
		if s.ctxWriter != nil {
			_ = s.ctxWriter.Close()
		}
	}()
	// 协议 §3.1：core→gui 第一条 = hello，新会话 seq 从 0 起，resume 从 lastSeq+1 续。
	// ★ v3：protocol 升到 3，并声明 workspace 能力（§3.1：未声明 ⇒ GUI 不得显示选择器）。
	hello := map[string]any{
		"t": "hello", "protocol": 3, "core": "0.2.0",
		"session": s.sessionID, "caps": []string{"tools", "ask", "workspace"},
	}
	if s.contextLimit > 0 {
		hello["context"] = s.contextLimit
	}
	if s.memResult != nil && len(s.memResult.Files) > 0 {
		hello["memories"] = s.memResult.Files
	}
	s.emit(hello)
	s.flush()   // ★ 必刷点①：握手的第一条不能留在缓冲里
	s.log("hello sent (seq=%d, session=%s); waiting for gui hello", s.out.Seq()-1, s.sessionID)

	// L3 记忆通知（§8.2 / §8.3）：
	// 1. resume 且存在已加载记忆文件 ⇒ 发一条 kind:"system" 提示「记忆已更新」
	if s.resumeID != "" && s.memResult != nil && len(s.memResult.LoadedFiles) > 0 {
		nid := s.nextBlockID()
		s.emit(map[string]any{"t": "block_start", "id": nid, "kind": "system", "text": "[记忆已更新]"})
		s.emit(map[string]any{"t": "block_end", "id": nid, "ok": true})
		s.flush()
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
		s.flush()
	}
	for {
		var sl wire.StampedLine
		var ok bool
		select {
		case sl, ok = <-s.in:
			if !ok {
				s.log("stdin EOF — exiting")
				s.flush()   // ★ 必刷点③：退出前把最后一批推出去
				return nil
			}
		case <-s.wsTimer:
			// 延迟应答的时间到了：未决窗口关闭（这是唯一能让「交错」真实发生的机制）。
			s.wsTimer = nil
			s.completeWorkspace()
			continue
		}
		msg := wire.ParseGui(sl.Text)
		arrived := wire.StateOf(sl.St) // ★ 到达时刻的状态，不是「现在读到它时」的状态
		switch msg.T {
		case "hello":
			s.log("hello received: seq=%d", msg.Seq)
			// 协议 §3.1：hello 必须是第一条且 seq=0——校验但不阻塞。
			if s.out.Seq() == 0 && msg.Seq != 0 {
				s.log("WARNING: hello.seq=%d (spec requires 0)", msg.Seq)
			}
		case "prompt":
			s.log("prompt received: %q (arrived=%s now=%s)", msg.Text, arrived, s.st())
			// ★ v3 §3.9/§4：未决工作区期间收到 prompt = 交错操作，**必须拒绝**，
			//   且必须回带该 client_id 的 error，否则 GUI 无从知道是哪条气泡失败。
			if s.pendingWs != nil {
				s.rejectPrompt(msg, "工作区请求未决（一次只允许一个），本轮 prompt 被拒绝")
				continue
			}
			// ★ v3 §4：prompt 仅 idle 合法。v2 这里是「警告但照常处理」——那会让
			//   一条被受理的 prompt 与一条被违反契约的 prompt 在界面上无法区分。
			//   ★ 判据用 arrived（到达时刻）而不是 now：主循环在上一轮运行期间不读
			//     channel，用 now 会把一条抢跑的 prompt 当成合法输入。
			if arrived != wire.StateIdle {
				s.rejectPrompt(msg, fmt.Sprintf("prompt 到达时状态是 %s，只接受 idle 时的 prompt", arrived))
				continue
			}
			s.handlePrompt(msg.Text, msg.ClientID)
		case "workspace":
			s.handleWorkspace(msg, arrived)
		case "cancel":
			s.log("cancel received (state=%s)", s.st())
			s.cancelRequested = true
			if s.st() == wire.StateRunning || s.st() == wire.StateWaiting {
				s.setState(wire.StateCancelled)
				s.setState(wire.StateIdle)
			}
		case "answer":
			s.log("answer received: id=%s choice=%s", msg.ID, msg.Choice)
			if s.pendingAskID != "" && msg.ID == s.pendingAskID {
				s.pendingAskID = ""
			} else {
				s.log("WARNING: answer for unknown/expired ask %q (pending=%q)", msg.ID, s.pendingAskID)
			}
		case "bye":
			s.log("bye received — exiting")
			s.emit(map[string]any{"t": "bye", "reason": "eof", "code": 0})
			s.flush()   // ★ 必刷点③
			return nil
		default:
			s.log("ignoring unknown gui message type %q (I5)", msg.T)
		}
	}
}

// ── 一轮交互 ────────────────────────────────────────────────────

func (s *session) handlePrompt(text string, clientID string) {
	// ★ `--burst`：一轮的输出在函数出口一次性推出（所有 return 路径都覆盖）。
	//   放在这里而不是每个 return 前，是因为 cancel 有两条提前返回的路径——
	//   那种地方最容易漏，而漏了就是"最后几十行烂在缓冲里"。
	defer s.flush()
	s.turn++
	s.cancelRequested = false
	s.setState(wire.StateRunning)

	// ── L2 压缩触发检查（§7.2）────────────────────────────────────
	if s.contextLimit > 0 {
		occupancy := s.lastPromptTokens
		if occupancy == 0 {
			for _, m := range s.messages {
				occupancy += len([]rune(m.Content))/2 + 100
			}
		}
		thresh := int(float64(s.contextLimit) * contextlog.CompressThresholdRatio)
		if occupancy >= thresh && (s.turn-s.lastCompressTurn >= 2 || s.lastCompressTurn == 0) {
			if s.failSummarize {
				s.log("compression failed due to --fail-summarize")
				nid := s.nextBlockID()
				s.emit(map[string]any{
					"t": "block_start", "id": nid, "kind": "system",
					"text": "[上下文压缩失败: 摘要生成失败，本轮照常发送]",
				})
				s.emit(map[string]any{"t": "block_end", "id": nid, "ok": false})
			} else {
				cutTo, ok := contextlog.FindCompressionCutPoint(s.messages, 2)
				if ok && cutTo >= 0 {
					beforeOccupancy := occupancy
					count := cutTo + 1
					summary := fmt.Sprintf("此前对话涵盖 %d 条历史消息的讨论，已确认关键决定与进展。", count)
					if s.ctxWriter != nil {
						_ = s.ctxWriter.AppendCompact(summary, 0, cutTo)
					}
					sumMsg := contextlog.Message{
						Role:    "user",
						Content: contextlog.SummaryPrefix + summary,
					}
					newMsgs := make([]contextlog.Message, 0, len(s.messages)-count+1)
					newMsgs = append(newMsgs, sumMsg)
					newMsgs = append(newMsgs, s.messages[cutTo+1:]...)
					s.messages = newMsgs

					afterOccupancy := int(float64(s.contextLimit) * contextlog.TargetRatio)
					occupancy = afterOccupancy
					s.lastPromptTokens = afterOccupancy
					s.lastCompressTurn = s.turn

					beforePct := int(float64(beforeOccupancy) * 100 / float64(s.contextLimit))
					afterPct := int(float64(afterOccupancy) * 100 / float64(s.contextLimit))

					nid := s.nextBlockID()
					noticeText := fmt.Sprintf("[上下文已压缩:%d 条 → 摘要,占用 %d%% → %d%%]", count, beforePct, afterPct)
					s.emit(map[string]any{
						"t": "block_start", "id": nid, "kind": "system", "text": noticeText,
					})
					s.emit(map[string]any{"t": "block_end", "id": nid, "ok": true})
					s.log("compressed %d messages: %s", count, noticeText)
				}
			}
		}

		if occupancy >= s.contextLimit {
			s.emit(map[string]any{
				"t": "error", "fatal": false,
				"message": "上下文已达 100% 且压缩无法腾出足够空间，请新开会话",
			})
			s.setState(wire.StateIdle)
			return
		}
	}

	// ① 回显用户（协议 §4：转录的唯一作者仍是 core；GUI 的「发送中」气泡靠
	//    client_id 认领这一块，所以回显**必须**带同一个 id，否则气泡永远停在发送中）。
	//
	//    ★ 缺失 client_id 时补一个**本地** id 并警告，而不是发一条违反 I9 的块：
	//      v2 的 GUI 不会带这个字段，而 I9 只约束 protocol>=3 的流——core 自己
	//      声明了 3，就不能由自己写出违规消息。补的 id 带 `auto-` 前缀，
	//      一眼能看出不是 GUI 给的。
	echoID := clientID
	if echoID == "" {
		echoID = fmt.Sprintf("auto-%d", s.turn)
		s.log("WARNING: prompt without client_id (v2 gui?) — echoing synthesised id %q", echoID)
	}
	userID := s.nextBlockID()
	s.emit(map[string]any{
		"t": "block_start", "id": userID,
		"kind": "user", "text": text, "client_id": echoID,
	})
	s.emit(map[string]any{"t": "block_end", "id": userID, "ok": true})

	userMsg := contextlog.Message{Role: "user", Content: text}
	s.messages = append(s.messages, userMsg)
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(userMsg)
	}

	// ② 助手 Markdown 流
	s.streamAssistant()

	// ③ 工具对（1–3 次）
	rounds := 1 + s.rng.Intn(3)
	for i := 0; i < rounds; i++ {
		if s.cancelRequested {
			s.cancelRequested = false
			return
		}
		s.toolRound(i)
	}

	// ④ 收尾
	inputTokens := 8000 + s.turn*137
	if s.contextLimit > 0 {
		if s.lastCompressTurn > 0 {
			inputTokens = s.lastPromptTokens + s.turn*10
		} else {
			inputTokens = int(float64(s.contextLimit)*0.75) + s.turn*int(float64(s.contextLimit)*0.08)
		}
	}
	s.lastPromptTokens = inputTokens
	u := map[string]any{
		"t": "usage", "turn": s.turn,
		"input": inputTokens, "output": 600 + s.turn*53,
	}
	if s.contextLimit > 0 {
		u["context"] = s.contextLimit
	}
	s.emit(u)
	if s.st() != wire.StateIdle {
		s.setState(wire.StateIdle)
	}
}

func (s *session) streamAssistant() {
	id := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": id, "kind": "assistant"})
	text := scriptAssistantText(s.rng)
	chunks := chunkText(s.rng, text, 4, 12)
	for _, c := range chunks {
		if s.cancelRequested {
			break
		}
		s.emit(map[string]any{"t": "delta", "id": id, "text": c})
		s.sleep(30 + s.rng.Float64()*270)
	}
	s.emit(map[string]any{"t": "block_end", "id": id, "ok": true})
	s.sleep(120)

	asstMsg := contextlog.Message{Role: "assistant", Content: text}
	s.messages = append(s.messages, asstMsg)
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(asstMsg)
	}
}

func (s *session) toolRound(index int) {
	callID := s.nextBlockID()
	tool := scriptTool(s.rng, index)

	s.emit(map[string]any{"t": "block_start", "id": callID, "kind": "tool_call",
		"name": tool.Name, "args": tool.Args})
	s.emit(map[string]any{"t": "block_end", "id": callID, "ok": true, "summary": "已发起"})
	s.sleep(400 + s.rng.Float64()*2000)

	callMsg := contextlog.Message{
		Role: "assistant",
		ToolCalls: []contextlog.ToolCall{{
			ID: callID, Type: "function",
			Function: contextlog.FuncSpec{Name: tool.Name, Arguments: "{}"},
		}},
	}
	s.messages = append(s.messages, callMsg)
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(callMsg)
	}

	// 每第 3 轮插权限门（I3：ask 后停发，直到 answer）
	if index%3 == 2 {
		askID := fmt.Sprintf("q%d", s.out.Seq())
		s.emit(map[string]any{"t": "ask", "id": askID, "kind": "permission",
			"question": fmt.Sprintf("允许执行 %s 吗？", tool.Name),
			"options": []map[string]string{
				{"key": "allow", "label": "允许"},
				{"key": "deny", "label": "拒绝"},
			},
			"default": "deny"})
		s.pendingAskID = askID
		s.setState(wire.StateWaiting)
		s.log("waiting for answer to %s (I3: no further messages)", askID)

		// I3 自守：阻塞等待（唯一允许的例外 = usage，本实现不发）
		s.waitForAnswer()
		if s.cancelRequested {
			return
		}
		if s.st() != wire.StateRunning {
			s.setState(wire.StateRunning)
		}
	}

	resultID := s.nextBlockID()
	s.emit(map[string]any{"t": "block_start", "id": resultID, "kind": "tool_result",
		"for": callID})
	body := scriptToolResult(s.rng, tool.Name)
	for _, c := range chunkText(s.rng, body, 3, 10) {
		s.emit(map[string]any{"t": "delta", "id": resultID, "text": c})
		s.sleep(10 + s.rng.Float64()*110)
	}
	ok := s.rng.Float64() > 0.12
	s.emit(map[string]any{"t": "block_end", "id": resultID, "ok": ok})
	if !ok {
		s.log("injecting tool failure for %s", tool.Name)
	}

	resMsg := contextlog.Message{Role: "tool", Content: body, ToolCallID: callID}
	s.messages = append(s.messages, resMsg)
	if s.ctxWriter != nil {
		_ = s.ctxWriter.AppendMsg(resMsg)
	}

	// I1 负向注入：漏一次 block_end（只在第一次工具轮）
	if s.dropBlockEnd && !s.blockEndSkipped {
		s.blockEndSkipped = true
		s.log("INJECT drop-block-end: next block_end will be omitted (I1 negative)")
		// 下一轮真造一个漏掉 end 的块：这里直接标记，由 handlePrompt 的下一轮消费
		s.dropBlockEnd = false
		s.emit(map[string]any{"t": "block_start", "id": s.nextBlockID(),
			"kind": "system", "text": "(this block will never end — I1 negative injection)"})
	}
}

// ── v3：工作区租约 ──────────────────────────────────────────────

// rejectPrompt：拒绝一条 prompt（§3.8：fatal:false 的 error，且带该 client_id）。
func (s *session) rejectPrompt(msg wire.GuiMsg, reason string) {
	s.log("REJECT prompt (client_id=%q): %s", msg.ClientID, reason)
	payload := map[string]any{"t": "error", "fatal": false, "message": reason}
	if msg.ClientID != "" {
		// 不带 client_id 的 error 是全局错误，**不得**被任何临时气泡认领（§3.8）。
		payload["client_id"] = msg.ClientID
	}
	s.emit(payload)
}

// handleWorkspace：受理一个工作区请求（§3.9：仅 idle；一次一个未决）。
func (s *session) handleWorkspace(msg wire.GuiMsg, arrived wire.State) {
	s.log("workspace request: id=%q path=%q (arrived=%s now=%s, pending=%v)",
		msg.RequestID, msg.Path, arrived, s.st(), s.pendingWs != nil)

	if msg.RequestID == "" {
		// 没有 request_id 就没法应答（I10 要求回显它）——只能报全局错误。
		s.log("REJECT workspace: missing request_id")
		s.emit(map[string]any{"t": "error", "fatal": false,
			"message": "workspace 缺少 request_id（v3 §3.9）"})
		return
	}
	if s.pendingWs != nil {
		s.nackWorkspace(msg.RequestID, msg.Path,
			"已有一个未决的工作区请求（一次只允许一个）")
		return
	}
	if arrived != wire.StateIdle {
		// 判据 = 到达时刻（见 wire.StampedLine 的说明）。
		s.nackWorkspace(msg.RequestID, msg.Path,
			fmt.Sprintf("请求到达时状态是 %s，工作区切换只在 idle 受理", arrived))
		return
	}
	resolved, err := wire.NormalizeWorkspace(msg.Path)
	if err != nil {
		s.nackWorkspace(msg.RequestID, msg.Path, err.Error())
		return
	}
	s.pendingWs = &pendingWorkspace{requestID: msg.RequestID,
		requestRaw: msg.Path, resolved: resolved}
	if s.workspaceDelayMs > 0 {
		// ★ 不除以 speed：这是**验证仪器**的旋钮（要能稳定造出未决窗口），
		//   不是剧本节奏。曾被 speed 缩放成 10ms，导致交错测试永远打不中。
		s.wsTimer = time.After(time.Duration(s.workspaceDelayMs) * time.Millisecond)
		s.log("workspace %s pending for %dms (interleaving window open)",
			msg.RequestID, s.workspaceDelayMs)
		return
	}
	s.completeWorkspace()
}

// completeWorkspace：把待决请求转成成功应答并切换 cwd。
// ★ 不发 state：§3.9 明确 workspace 是控制面操作，不得发 running/idle。
func (s *session) completeWorkspace() {
	pw := s.pendingWs
	if pw == nil {
		return
	}
	s.pendingWs = nil
	s.cwd = pw.resolved
	s.log("workspace switched: %s", s.cwd)
	s.loadMemories()
	s.emit(map[string]any{"t": "workspace", "request_id": pw.requestID,
		"ok": true, "path": pw.resolved})
}

// nackWorkspace：拒绝一个工作区请求（ok:false 必须带 message，path 回显请求原文）。
func (s *session) nackWorkspace(requestID, rawPath, reason string) {
	s.log("REJECT workspace %s: %s", requestID, reason)
	s.emit(map[string]any{"t": "workspace", "request_id": requestID,
		"ok": false, "path": rawPath, "message": reason})
}

// waitForAnswer：阻塞读 stdin 直到 answer 或 cancel/bye。
// 期间收到的任何东西都要正确处置（不丢消息、不死锁）。
func (s *session) waitForAnswer() {
	// ★ 必刷点②：再往下就是**阻塞**等 answer。ask 若还在缓冲里，GUI 不知道要回答
	//   什么 ⇒ 双方互等 ⇒ 死锁（而 `--burst` 让这种情况从"不可能"变成"默认"）。
	s.flush()
	for sl := range s.in {
		msg := wire.ParseGui(sl.Text)
		switch msg.T {
		case "answer":
			if msg.ID == s.pendingAskID {
				s.pendingAskID = ""
				s.log("ask %s resolved: %s", msg.ID, msg.Choice)
			}
			return
		case "cancel":
			s.cancelRequested = true
			s.pendingAskID = ""
			s.log("cancel during waiting → cancelled")
			return
		case "workspace":
			// §3.9：只在 idle 受理。waiting 不是 idle ⇒ 必须拒绝（且必须应答，
			// 否则 GUI 的未决请求会一直挂着）。
			if msg.RequestID != "" {
				s.nackWorkspace(msg.RequestID, msg.Path,
					"当前状态是 waiting（有未决 ask），工作区切换只在 idle 受理")
			}
		case "bye":
			s.log("bye during waiting — exiting")
			os.Exit(0)
		default:
			s.log("ignoring %q while waiting (I3)", msg.T)
		}
	}
}
