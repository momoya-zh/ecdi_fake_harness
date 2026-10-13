package main

// session.go —— 假 core 的会话状态机与出站写入。
//
// 职责边界（协议 §6 / §5）：
//   * 状态机：idle → running → (waiting) → idle / cancelled / error；
//   * seq 分配：core→gui 方向从 0 起严格递增（I4 的量具）；
//   * I3 自守：发出 ask 后到收到 answer 前，**不得再发任何消息**（usage 除外）；
//   * 注入开关：drop-seq（I4 负向）、drop-block-end（I1 负向）。

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"strings"
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

type session struct {
	rng   *rand.Rand
	speed float64

	dropSeqEvery int
	dropBlockEnd bool

	in  <-chan string
	log func(string, ...any)

	seq   int64        // core→gui 方向的下一个 seq
	st    state        // 当前状态
	turn  int          // 轮次计数（usage.turn）
	since int          // 自上次注入丢号以来发出的消息数
	blockEndSkipped bool // drop-block-end 只命中一次
	blockNo int         // 块 id 计数器（与 seq 解耦——id 不依赖 seq 算术）

	// 未决 ask（I3）：非空时只接受 answer
	pendingAskID string
	// 当前进行中的回答（用于 cancel）
	cancelRequested bool
}

func newSession(seed int64, speed float64, dropSeq int, dropBlockEnd bool,
	in <-chan string, log func(string, ...any)) *session {
	return &session{
		rng:          rand.New(rand.NewSource(seed)),
		speed:        speed,
		dropSeqEvery: dropSeq,
		dropBlockEnd: dropBlockEnd,
		in:           in,
		log:          log,
		st:           stateIdle,
	}
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

func (s *session) setState(v state) {
	s.st = v
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
	s.emit(map[string]any{"t": "hello", "protocol": 1, "core": "0.1.0",
		"session": "s-live-1", "caps": []string{"tools", "ask"}})
	s.log("hello sent; waiting for gui hello")
	for line := range s.in {
		msg := parseGui(line)
		switch msg.T {
		case "hello":
			s.log("hello received: seq=%d", msg.Seq)
			// 协议 §3.1：hello 必须是第一条且 seq=0——校验但不阻塞。
			if s.seq == 0 && msg.Seq != 0 {
				s.log("WARNING: hello.seq=%d (spec requires 0)", msg.Seq)
			}
		case "prompt":
			s.log("prompt received: %q (state=%s)", msg.Text, s.st)
			if s.st != stateIdle {
				s.log("WARNING: prompt in state=%s (spec: only idle) — still handled", s.st)
			}
			s.handlePrompt(msg.Text)
		case "cancel":
			s.log("cancel received (state=%s)", s.st)
			s.cancelRequested = true
			if s.st == stateRunning || s.st == stateWaiting {
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
	// stdin EOF（GUI 关窗未发 bye）：正常收尾
	s.log("stdin EOF — exiting")
	return nil
}

// ── 一轮交互 ────────────────────────────────────────────────────

func (s *session) handlePrompt(text string) {
	s.turn++
	s.cancelRequested = false
	s.setState(stateRunning)

	// ① 回显用户（协议 §4：转录唯一作者是 core）
	userID := s.nextBlockID()
	s.emit(map[string]any{
		"t": "block_start", "id": userID,
		"kind": "user", "text": text,
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
	if s.st != stateIdle {
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
		if s.st != stateRunning {
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

// waitForAnswer：阻塞读 stdin 直到 answer 或 cancel/bye。
// 期间收到的任何东西都要正确处置（不丢消息、不死锁）。
func (s *session) waitForAnswer() {
	for line := range s.in {
		msg := parseGui(line)
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
		case "bye":
			s.log("bye during waiting — exiting")
			os.Exit(0)
		default:
			s.log("ignoring %q while waiting (I3)", msg.T)
		}
	}
}
