package wire

// in.go —— gui→core 方向的入站：消息解析 + 带状态戳的 stdin 行读线程。

import (
	"bufio"
	"encoding/json"
	"os"
	"strings"
	"sync/atomic"
)

// GuiMsg = gui→core 消息的解析结果（协议 §4 六类：hello/prompt/cancel/answer/workspace/bye）。
//
// ★ v3 补三个字段（2026-10-20）：ClientID 用于回显确认临时气泡（§4），
// RequestID/Path 用于工作区租约（§3.9）。缺了它们就得从 Raw 里再解一遍 JSON，
// 那等于两份解析器两份真相。
type GuiMsg struct {
	T         string
	Seq       int64
	Text      string
	ID        string
	Choice    string
	ClientID  string
	RequestID string
	Path      string
	// ★ v4（P3.6）：model 切换请求的载荷（协议 §4）。
	Provider  string
	Context   int64
	MaxOutput int64
	Raw       string
}

// ParseGui：一行 JSONL → GuiMsg。解析失败不报错（T 置空 + Raw 原样），
// 由会话层按 I5「未知消息忽略并记录」处置——stdin 是合作通道，解析坏行
// 不构成致命错误的证据。
func ParseGui(line string) GuiMsg {
	var raw struct {
		T         string `json:"t"`
		Seq       int64  `json:"seq"`
		Text      string `json:"text"`
		ID        string `json:"id"`
		Choice    string `json:"choice"`
		ClientID  string `json:"client_id"`
		RequestID string `json:"request_id"`
		Path      string `json:"path"`
		Provider  string `json:"provider"`
		Context   int64  `json:"context"`
		MaxOutput int64  `json:"maxOutput"`
	}
	if err := json.Unmarshal([]byte(line), &raw); err != nil {
		return GuiMsg{T: "", Raw: line}
	}
	return GuiMsg{T: raw.T, Seq: raw.Seq, Text: raw.Text, ID: raw.ID, Choice: raw.Choice,
		ClientID: raw.ClientID, RequestID: raw.RequestID, Path: raw.Path,
		Provider: raw.Provider, Context: raw.Context, MaxOutput: raw.MaxOutput}
}

// StampedLine = 一行输入 + **读到它的那一刻**的会话状态码。
//
// ★ 为什么要带状态（v3，2026-10-20）：主 goroutine 在 handlePrompt 里内联跑完整轮，
// 期间不读 channel；等它回来读时状态已回到 idle。若不在这里打戳，一条**在 running
// 期间**发来的 prompt 就会与一条 idle 时发来的 prompt 完全等价，§3.2/§3.9 的
// 「只接受 idle」「交错必须拒绝」在 core 侧根本无法判定。
type StampedLine struct {
	Text string
	St   int32
}

// ReadStdin：stdin 行读线程 → 行通道（单一消费者 = 主 goroutine）。
// 只**读** curState 给每行打戳，从不写它。返回前关闭 channel（EOF = 优雅收尾信号）。
func ReadStdin(lines chan<- StampedLine, curState *atomic.Int32, logf func(string, ...any)) {
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 0, 64*1024), 4*1024*1024)
	for sc.Scan() {
		line := sc.Text()
		if strings.TrimSpace(line) == "" {
			continue // 空行忽略（分帧约定：一行一个 JSON）
		}
		lines <- StampedLine{Text: line, St: curState.Load()}
	}
	if err := sc.Err(); err != nil {
		logf("stdin read error: %v", err)
	}
	close(lines)
}
