package main

// session.go —— 假 core 的会话状态机与出站写入。
//
// 职责边界（协议 §6 / §5）：
//   * 状态机：idle → running → (waiting) → idle / cancelled / error；
//   * seq 分配：core→gui 方向从 0 起严格递增（I4 的量具）；
//   * I3 自守：发出 ask 后到收到 answer 前，**不得再发任何消息**（usage 除外）；
//   * 注入开关：drop-seq（I4 负向）、drop-block-end（I1 负向）；
//   * v3 补：prompt.client_id 回显（§4）、workspace 租约（§3.9）。

import (
	"encoding/json"
	"errors"
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"
)

type state string

const (
	stateIdle      state = "idle"
	stateRunning   state = "running"
	stateWaiting   state = "waiting"
	stateCancelled state = "cancelled"
	stateError     state = "error"
)

// 状态码：唯一目的是让**读线程**（另一个 goroutine）能原子地取到当前状态，
// 给每一行输入打上「到达时刻」的戳。整数编码而非直接传 state，是为了用
// atomic.Int32 而不引入互斥锁；映射集中在 codeOf/stateOf 两处，不散落。
const (
	codeIdle int32 = iota
	codeRunning
	codeWaiting
	codeCancelled
	codeError
)

func codeOf(v state) int32 {
	switch v {
	case stateRunning:
		return codeRunning
	case stateWaiting:
		return codeWaiting
	case stateCancelled:
		return codeCancelled
	case stateError:
		return codeError
	}
	return codeIdle
}

func stateOf(c int32) state {
	switch c {
	case codeRunning:
		return stateRunning
	case codeWaiting:
		return stateWaiting
	case codeCancelled:
		return stateCancelled
	case codeError:
		return stateError
	}
	return stateIdle
}

type session struct {
	rng   *rand.Rand
	speed float64

	dropSeqEvery int
	dropBlockEnd bool

	in  <-chan stampedLine
	log func(string, ...any)

	// 当前状态：**只经 stc 读写**（读线程要原子读它给输入打戳）。
	// 保留 stc 之外的副本必然漂移，所以 s.st 字段已删除。
	stc *atomic.Int32

	seq   int64        // core→gui 方向的下一个 seq
	turn  int          // 轮次计数（usage.turn）
	since int          // 自上次注入丢号以来发出的消息数
	blockEndSkipped bool // drop-block-end 只命中一次
	blockNo int         // 块 id 计数器（与 seq 解耦——id 不依赖 seq 算术）

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
}

// pendingWorkspace = 一个已受理、尚未应答的工作区请求。
type pendingWorkspace struct {
	requestID  string
	requestRaw string // GUI 发来的原文（失败时原样回显，§3.9）
	resolved   string // 规范化后的绝对路径（应答成功后写入 cwd）
}

func newSession(seed int64, speed float64, dropSeq int, dropBlockEnd bool,
	wsDelayMs int, in <-chan stampedLine, stc *atomic.Int32,
	log func(string, ...any)) *session {
	s := &session{
		rng:              rand.New(rand.NewSource(seed)),
		speed:            speed,
		dropSeqEvery:     dropSeq,
		dropBlockEnd:     dropBlockEnd,
		workspaceDelayMs: wsDelayMs,
		in:               in,
		log:              log,
		stc:              stc,
	}
	// 初始工作区 = core 进程自身的 cwd（与 GUI 无关：GUI 的 CreateProcess 不传 cwd，
	// 但即使传了，v3 起工作区的唯一真相也是 core 自己记的这一个字段）。
	if wd, err := os.Getwd(); err == nil {
		if norm, err := normalizeWorkspace(wd); err == nil {
			s.cwd = norm
		}
	}
	s.log("initial workspace: %s", s.cwd)
	return s
}

// ── 出站 ────────────────────────────────────────────────────────

// emit：给消息分配 seq + ts 后写 stdout 一行（每条即刷——流式语义）。
// 字段序与夹具一致：t, seq, ts, ...
func (s *session) emit(payload map[string]any) {
	if s.dropSeqEvery > 0 {
		s.since++
		if s.since >= s.dropSeqEvery {
			s.since = 0
			old := s.seq
			s.seq++ // 故意空号：下一个消息的 seq 直接跳 1 ⇒ GUI 侧 I4 必须显式提示
			s.log("INJECT drop-seq: skipping %d (I4 negative)", old)
		}
	}
	payload["seq"] = s.seq
	payload["ts"] = time.Now().UTC().Format("2006-01-02T15:04:05.000Z")
	s.seq++

	// 稳定字段序：t, seq, ts 在前（hand-rolled，避免 map 序不确定）
	var b strings.Builder
	b.WriteString("{")
	writeKV(&b, "t", payload["t"], true)
	writeKV(&b, "seq", payload["seq"], false)
	writeKV(&b, "ts", payload["ts"], false)
	for k, v := range payload {
		switch k {
		case "t", "seq", "ts":
			continue
		}
		writeKV(&b, k, v, false)
	}
	b.WriteString("}")
	fmt.Fprintln(os.Stdout, b.String())
	os.Stdout.Sync()
}

func writeKV(b *strings.Builder, key string, v any, first bool) {
	if !first {
		b.WriteString(",")
	}
	kb, _ := json.Marshal(key)
	b.Write(kb)
	b.WriteString(":")
	vb, err := json.Marshal(v)
	if err != nil {
		vb = []byte("null")
	}
	b.Write(vb)
}

// st：当前状态（唯一真相 = 共享原子量）。
func (s *session) st() state { return stateOf(s.stc.Load()) }

func (s *session) setState(v state) {
	s.stc.Store(codeOf(v))
	s.emit(map[string]any{"t": "state", "value": string(v)})
}

// nextBlockID：块 id 与 seq 解耦（id 是块身份，不是消息序号——I1 的前提）。
func (s *session) nextBlockID() string {
	s.blockNo++
	return fmt.Sprintf("b%d", s.blockNo)
}

// ── 节奏 ────────────────────────────────────────────────────────

// sleep：按 speed 缩放的等待（流式节奏的真相源）。
func (s *session) sleep(ms float64) {
	d := time.Duration(ms/s.speed) * time.Millisecond
	if d <= 0 {
		return
	}
	time.Sleep(d)
}

// ── 主循环 ──────────────────────────────────────────────────────

func (s *session) run() error {
	// 协议 §3.1：core→gui 第一条 = hello，seq 必须为 0。
	// ★ v3：protocol 升到 3，并声明 workspace 能力（§3.1：未声明 ⇒ GUI 不得显示选择器）。
	s.emit(map[string]any{"t": "hello", "protocol": 3, "core": "0.2.0",
		"session": "s-live-1", "caps": []string{"tools", "ask", "workspace"}})
	s.log("hello sent; waiting for gui hello")
	for {
		var sl stampedLine
		var ok bool
		select {
		case sl, ok = <-s.in:
			if !ok {
				s.log("stdin EOF — exiting")
				return nil
			}
		case <-s.wsTimer:
			// 延迟应答的时间到了：未决窗口关闭（这是唯一能让「交错」真实发生的机制）。
			s.wsTimer = nil
			s.completeWorkspace()
			continue
		}
		msg := parseGui(sl.text)
		arrived := stateOf(sl.st) // ★ 到达时刻的状态，不是「现在读到它时」的状态
		switch msg.T {
		case "hello":
			s.log("hello received: seq=%d", msg.Seq)
			// 协议 §3.1：hello 必须是第一条且 seq=0——校验但不阻塞。
			if s.seq == 0 && msg.Seq != 0 {
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
			if arrived != stateIdle {
				s.rejectPrompt(msg, fmt.Sprintf("prompt 到达时状态是 %s，只接受 idle 时的 prompt", arrived))
				continue
			}
			s.handlePrompt(msg.Text, msg.ClientID)
		case "workspace":
			s.handleWorkspace(msg, arrived)
		case "cancel":
			s.log("cancel received (state=%s)", s.st())
			s.cancelRequested = true
			if s.st() == stateRunning || s.st() == stateWaiting {
				s.setState(stateCancelled)
				s.setState(stateIdle)
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
			return nil
		default:
			s.log("ignoring unknown gui message type %q (I5)", msg.T)
		}
	}
}

// ── 一轮交互 ────────────────────────────────────────────────────

func (s *session) handlePrompt(text string, clientID string) {
	s.turn++
	s.cancelRequested = false
	s.setState(stateRunning)

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
	s.emit(map[string]any{
		"t": "usage", "turn": s.turn,
		"input": 8000 + s.turn*137, "output": 600 + s.turn*53,
	})
	if s.st() != stateIdle {
		s.setState(stateIdle)
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
}

func (s *session) toolRound(index int) {
	callID := s.nextBlockID()
	tool := scriptTool(s.rng, index)

	s.emit(map[string]any{"t": "block_start", "id": callID, "kind": "tool_call",
		"name": tool.Name, "args": tool.Args})
	s.emit(map[string]any{"t": "block_end", "id": callID, "ok": true, "summary": "已发起"})
	s.sleep(400 + s.rng.Float64()*2000)

	// 每第 3 轮插权限门（I3：ask 后停发，直到 answer）
	if index%3 == 2 {
		askID := fmt.Sprintf("q%d", s.seq)
		s.emit(map[string]any{"t": "ask", "id": askID, "kind": "permission",
			"question": fmt.Sprintf("允许执行 %s 吗？", tool.Name),
			"options": []map[string]string{
				{"key": "allow", "label": "允许"},
				{"key": "deny", "label": "拒绝"},
			},
			"default": "deny"})
		s.pendingAskID = askID
		s.setState(stateWaiting)
		s.log("waiting for answer to %s (I3: no further messages)", askID)

		// I3 自守：阻塞等待（唯一允许的例外 = usage，本实现不发）
		s.waitForAnswer()
		if s.cancelRequested {
			return
		}
		if s.st() != stateRunning {
			s.setState(stateRunning)
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
func (s *session) rejectPrompt(msg guiMsg, reason string) {
	s.log("REJECT prompt (client_id=%q): %s", msg.ClientID, reason)
	payload := map[string]any{"t": "error", "fatal": false, "message": reason}
	if msg.ClientID != "" {
		// 不带 client_id 的 error 是全局错误，**不得**被任何临时气泡认领（§3.8）。
		payload["client_id"] = msg.ClientID
	}
	s.emit(payload)
}

// handleWorkspace：受理一个工作区请求（§3.9：仅 idle；一次一个未决）。
func (s *session) handleWorkspace(msg guiMsg, arrived state) {
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
	if arrived != stateIdle {
		// 判据 = 到达时刻（见 stampedLine 的说明）。
		s.nackWorkspace(msg.RequestID, msg.Path,
			fmt.Sprintf("请求到达时状态是 %s，工作区切换只在 idle 受理", arrived))
		return
	}
	resolved, err := normalizeWorkspace(msg.Path)
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
	s.emit(map[string]any{"t": "workspace", "request_id": pw.requestID,
		"ok": true, "path": pw.resolved})
}

// nackWorkspace：拒绝一个工作区请求（ok:false 必须带 message，path 回显请求原文）。
func (s *session) nackWorkspace(requestID, rawPath, reason string) {
	s.log("REJECT workspace %s: %s", requestID, reason)
	s.emit(map[string]any{"t": "workspace", "request_id": requestID,
		"ok": false, "path": rawPath, "message": reason})
}

// normalizeWorkspace：§3.9 的路径规范化（绝对 / `/` 分隔 / 无尾随 `/` / 无 `.`·`..`）。
// 同时校验「它真是一个可访问的目录」——否则「工作区显示与 core 实际目录一致」
// 只能在字符串层面成立，而 core 实际并不在那个目录里。
func normalizeWorkspace(p string) (string, error) {
	if strings.TrimSpace(p) == "" {
		return "", errors.New("请求缺少 path")
	}
	abs, err := filepath.Abs(p)
	if err != nil {
		return "", fmt.Errorf("无法解析路径：%v", err)
	}
	abs = filepath.Clean(abs) // 顺带消掉 `.` / `..` 段
	info, err := os.Stat(abs)
	if err != nil {
		return "", fmt.Errorf("目录不可访问：%v", err)
	}
	if !info.IsDir() {
		return "", errors.New("路径不是目录")
	}
	return strings.ReplaceAll(abs, "\\", "/"), nil
}

// waitForAnswer：阻塞读 stdin 直到 answer 或 cancel/bye。
// 期间收到的任何东西都要正确处置（不丢消息、不死锁）。
func (s *session) waitForAnswer() {
	for sl := range s.in {
		msg := parseGui(sl.text)
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
