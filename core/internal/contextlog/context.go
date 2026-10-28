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

// FindCompressionCutPoint = 寻找不破坏 tool_call 与 tool 配对的合法切分点
// 切分点必须落在完整一轮边界上（即被保留段的第一条必须是 role == "user"）。
// 寻找满足切分后旧消息为 [0, to] 的最大合法 to。
func FindCompressionCutPoint(msgs []Message, retainBudgetCount int) (int, bool) {
	if len(msgs) <= 2 {
		return -1, false
	}
	// 从后往前保留约 retainBudgetCount 条消息，寻找其前方的最近一个 user 消息位置
	targetRetainStart := len(msgs) - retainBudgetCount
	if targetRetainStart < 1 {
		targetRetainStart = 1
	}

	for i := targetRetainStart; i < len(msgs); i++ {
		if msgs[i].Role == "user" {
			// msgs[i] 是一轮的起点，故 [0, i-1] 是前几轮的完整区间
			return i - 1, true
		}
	}
	// 若后面找不到，尝试往前找
	for i := targetRetainStart - 1; i >= 1; i-- {
		if msgs[i].Role == "user" {
			return i - 1, true
		}
	}
	return -1, false
}
