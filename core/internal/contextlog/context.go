package contextlog

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// 具名常量（协议与规格 docs/session-persistence-decisions.md §7.2）
const (
	CompressThresholdRatio = 0.85 // 触发阈值 85%
	RetainRatio            = 0.25 // 保留最近预算约 25% 窗口
	TargetRatio            = 0.50 // 压缩后目标占用约 50%
	SummaryPrefix          = "[以下为此前对话摘要]\n"
)

// FuncSpec = 工具调用的函数规格（OpenAI 兼容形态）
type FuncSpec struct {
	Name      string `json:"name"`
	Arguments string `json:"arguments"`
}

// ToolCall = assistant 消息中的工具调用
type ToolCall struct {
	ID       string   `json:"id"`
	Type     string   `json:"type"` // "function"
	Index    int      `json:"index,omitempty"`
	Function FuncSpec `json:"function"`
}

// Message = 对话历史中的单条消息
type Message struct {
	Role       string     `json:"role"`
	Content    string     `json:"content"`
	ToolCalls  []ToolCall `json:"tool_calls,omitempty"`
	ToolCallID string     `json:"tool_call_id,omitempty"`
}

// Record = context.jsonl 中的行记录格式
// 格式一：{"t":"msg", "role":"...", ...}
// 格式二：{"t":"compact", "summary":"...", "replaces":[from, to]}
type Record struct {
	T          string     `json:"t"` // "msg" | "compact"
	Role       string     `json:"role,omitempty"`
	Content    string     `json:"content,omitempty"`
	ToolCalls  []ToolCall `json:"tool_calls,omitempty"`
	ToolCallID string     `json:"tool_call_id,omitempty"`
	Summary    string     `json:"summary,omitempty"`
	Replaces   []int      `json:"replaces,omitempty"` // [from, to] inclusive
}

// SessionMetaLite = 读取 .meta.json 中的 lastSeq 与 contextLimit
type SessionMetaLite struct {
	ID           string `json:"id"`
	LastSeq      int64  `json:"lastSeq"`
	ContextLimit int64  `json:"contextLimit"`
}

// ReadMeta = 读取元数据文件
func ReadMeta(metaPath string) (*SessionMetaLite, error) {
	data, err := os.ReadFile(metaPath)
	if err != nil {
		return nil, err
	}
	var meta SessionMetaLite
	if err := json.Unmarshal(data, &meta); err != nil {
		return nil, err
	}
	return &meta, nil
}

// ScanJournalState = 扫描会话转录 .jsonl，提取最大 seq、最大块序号 b<N> 与最大轮次 turn。
// 用于 --resume 时兜底校准（防止上次异常退出导致 .meta.json 的 lastSeq 滞后，以及防止块 id b1 冲突触发 I1）。
func ScanJournalState(journalPath string) (maxSeq int64, maxBlockNo int, maxTurn int) {
	maxSeq = -1
	f, err := os.Open(journalPath)
	if err != nil {
		return maxSeq, 0, 0
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	buf := make([]byte, 64*1024)
	scanner.Buffer(buf, 10*1024*1024)

	type journalEnvelope struct {
		T    string `json:"t"`
		Seq  *int64 `json:"seq"`
		ID   string `json:"id"`
		Turn int    `json:"turn"`
	}
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		var env journalEnvelope
		if err := json.Unmarshal([]byte(line), &env); err != nil {
			continue
		}
		if env.Seq != nil && *env.Seq > maxSeq {
			maxSeq = *env.Seq
		}
		if env.T == "block_start" && strings.HasPrefix(env.ID, "b") {
			if n, err := strconv.Atoi(env.ID[1:]); err == nil && n > maxBlockNo {
				maxBlockNo = n
			}
		}
		if env.T == "usage" && env.Turn > maxTurn {
			maxTurn = env.Turn
		}
	}
	return maxSeq, maxBlockNo, maxTurn
}

// Replay = 顺序重放 context.jsonl 重构当前 messages 列表
// 规则：遇到 "msg" 压入，遇到 "compact" 将 [from, to] 区间替换为一条 summary 消息
func Replay(path string) ([]Message, error) {
	f, err := os.Open(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return []Message{}, nil
		}
		return nil, err
	}
	defer f.Close()

	var msgs []Message
	scanner := bufio.NewScanner(f)
	buf := make([]byte, 64*1024)
	scanner.Buffer(buf, 10*1024*1024)

	lineNo := 0
	for scanner.Scan() {
		lineNo++
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		var rec Record
		if err := json.Unmarshal([]byte(line), &rec); err != nil {
			return nil, fmt.Errorf("context.jsonl line %d unmarshal error: %w", lineNo, err)
		}
		switch rec.T {
		case "msg":
			msgs = append(msgs, Message{
				Role:       rec.Role,
				Content:    rec.Content,
				ToolCalls:  rec.ToolCalls,
				ToolCallID: rec.ToolCallID,
			})
		case "compact":
			if len(rec.Replaces) == 2 {
				from, to := rec.Replaces[0], rec.Replaces[1]
				if from >= 0 && to >= from && from < len(msgs) {
					if to >= len(msgs) {
						to = len(msgs) - 1
					}
					sumMsg := Message{
						Role:    "user",
						Content: SummaryPrefix + rec.Summary,
					}
					newMsgs := make([]Message, 0, len(msgs)-(to-from+1)+1)
					newMsgs = append(newMsgs, msgs[:from]...)
					newMsgs = append(newMsgs, sumMsg)
					newMsgs = append(newMsgs, msgs[to+1:]...)
					msgs = newMsgs
				}
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	return msgs, nil
}

// LogWriter = 追加写 context.jsonl 的文件写入器
type LogWriter struct {
	path string
	file *os.File
}

// OpenLogWriter = 打开或创建 context.jsonl
func OpenLogWriter(path string) (*LogWriter, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return nil, err
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		return nil, err
	}
	return &LogWriter{path: path, file: f}, nil
}

// AppendMsg = 追加一条消息记录
func (w *LogWriter) AppendMsg(msg Message) error {
	if w == nil || w.file == nil {
		return nil
	}
	rec := Record{
		T:          "msg",
		Role:       msg.Role,
		Content:    msg.Content,
		ToolCalls:  msg.ToolCalls,
		ToolCallID: msg.ToolCallID,
	}
	b, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	if _, err := w.file.Write(b); err != nil {
		return err
	}
	return w.file.Sync()
}

// AppendCompact = 追加一条压缩记录
func (w *LogWriter) AppendCompact(summary string, from, to int) error {
	if w == nil || w.file == nil {
		return nil
	}
	rec := Record{
		T:        "compact",
		Summary:  summary,
		Replaces: []int{from, to},
	}
	b, err := json.Marshal(rec)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	if _, err := w.file.Write(b); err != nil {
		return err
	}
	return w.file.Sync()
}

// Close = 关闭文件
func (w *LogWriter) Close() error {
	if w == nil || w.file == nil {
		return nil
	}
	err := w.file.Close()
	w.file = nil
	return err
}

// FindCompressionCutPoint = 按“保留预算”寻找不破坏 tool_call/tool 配对的合法切分点。
//
// 切分点必须落在完整一轮边界上：被保留段的第一条必须是 role == "user"。
// 从末尾向前累计估算 token，返回**最靠后**的 user 边界 i（切在 i-1），
// 使保留段 msgs[i:] 的估算 ≤ retainBudgetTokens。
//
// ★ 2026-11-05：从“保留 N 条消息”改成“保留 N token”。
//
//	旧实现固定保留 2 条，会让最后 1 轮里挂着的 20 条工具消息整轮留在保留区，
//	一刀都切不进当前轮；RetainRatio 也从未被用上。
//
// ★ 找不到预算内的边界时，**退回最近的 user 边界**（至少保留最后 1 轮），
//
//	而不是返回 false——大单轮本来就无法既守预算又保住“完整一轮”，
//	此时应由调用方的硬顶/折叠逻辑决定下一步，而不是在这里放弃压缩。
//	只有 msgs[1:] 里根本没有 user（连一轮都切不出来）时才返回 false。
func FindCompressionCutPoint(msgs []Message, retainBudgetTokens int) (int, bool) {
	if len(msgs) <= 2 {
		return -1, false
	}
	fallback := -1
	best := -1
	suffix := 0
	for i := len(msgs) - 1; i >= 1; i-- {
		suffix += EstimateMessageTokens(msgs[i])
		if msgs[i].Role != "user" {
			continue
		}
		if fallback < 0 {
			fallback = i // 离末尾最近的 user 边界（保留段最短）
		}
		if suffix > retainBudgetTokens {
			// 再往前的 user 边界只会把更多消息算进保留段 ⇒ 不可能重新满足预算。
			break
		}
		best = i // 预算内更早的边界 = 保留更多最近上下文
	}
	if best >= 0 {
		return best - 1, true
	}
	if fallback >= 0 {
		return fallback - 1, true
	}
	return -1, false
}

// ── 内部估算（防爆判据用；绝不用于生成 GUI 的 usage 读数）──────────
//
// 口径刻意取“宁可高估”：估算只决定“要不要修剪/压缩”，高估的代价是
// 多折一点旧工具输出；低估的代价是 HTTP 400 context_length_exceeded。

const (
	// MessageOverheadTokens：单条消息的 role/分隔/JSON 包装开销。
	MessageOverheadTokens = 8
	// asciiBytesPerToken：ASCII/代码的通用折算率（英文 ≈ 4 字节/token）。
	asciiBytesPerToken = 4
)

// EstimateStringTokens：保守估算一段文本的 token 数。
//
// ASCII 按 4 字节/token；非 ASCII rune 每个至少按 1 token 计——中文常见
// tokenizer 约 1 token/字，UTF-8 3 字节/字，按英文的 4 字节/token 会把中文
// 少算 25%–55%。emoji 等补充平面字符再按 2 token 计（一个 rune 常被切成多片）。
func EstimateStringTokens(s string) int {
	if s == "" {
		return 0
	}
	tokens, asciiBytes := 0, 0
	for _, r := range s {
		switch {
		case r < 0x80:
			asciiBytes++
		case r >= 0x1F000:
			tokens += 2
		default:
			tokens++
		}
	}
	return tokens + (asciiBytes+asciiBytesPerToken-1)/asciiBytesPerToken
}

// EstimateMessageTokens：单条 Message 的保守 token 估算（含 tool_calls 参数）。
func EstimateMessageTokens(m Message) int {
	n := MessageOverheadTokens +
		EstimateStringTokens(m.Content) +
		EstimateStringTokens(m.ToolCallID)
	for _, tc := range m.ToolCalls {
		n += MessageOverheadTokens +
			EstimateStringTokens(tc.ID) +
			EstimateStringTokens(tc.Type) +
			EstimateStringTokens(tc.Function.Name) +
			EstimateStringTokens(tc.Function.Arguments)
	}
	return n
}
