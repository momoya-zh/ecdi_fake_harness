package contextlog

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestContextReplayAndCompact(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "test.context.jsonl")

	writer, err := OpenLogWriter(logPath)
	if err != nil {
		t.Fatalf("OpenLogWriter failed: %v", err)
	}

	// 1. 模拟写入第 1 轮
	_ = writer.AppendMsg(Message{Role: "user", Content: "hello"})
	_ = writer.AppendMsg(Message{Role: "assistant", Content: "hi there"})

	// 2. 模拟写入第 2 轮（含工具调用）
	_ = writer.AppendMsg(Message{Role: "user", Content: "check file"})
	_ = writer.AppendMsg(Message{
		Role:      "assistant",
		Content:   "",
		ToolCalls: []ToolCall{{ID: "call_1", Type: "function", Function: FuncSpec{Name: "read_file", Arguments: "{}"}}},
	})
	_ = writer.AppendMsg(Message{Role: "tool", Content: "file content 123", ToolCallID: "call_1"})
	_ = writer.AppendMsg(Message{Role: "assistant", Content: "done checking"})

	// 3. 模拟写入第 3 轮
	_ = writer.AppendMsg(Message{Role: "user", Content: "continue next"})
	_ = writer.AppendMsg(Message{Role: "assistant", Content: "ok"})

	msgs, err := Replay(logPath)
	if err != nil {
		t.Fatalf("Replay failed: %v", err)
	}
	if len(msgs) != 8 {
		t.Fatalf("expected 8 messages, got %d", len(msgs))
	}

	// 寻找切分点（保留预算 30 token：装不下第 2 轮，只能保留第 3 轮）
	cutTo, ok := FindCompressionCutPoint(msgs, 30)
	if !ok {
		t.Fatalf("FindCompressionCutPoint should have found cut point")
	}
	// cutTo 应当指向第 2 轮结束（index 5），因为第 3 轮 user 是 index 6
	if cutTo != 5 {
		t.Fatalf("expected cutTo == 5, got %d", cutTo)
	}

	// 4. 追加压缩记录：压缩 [0, 5]
	err = writer.AppendCompact("用户打了招呼并查看了文件", 0, cutTo)
	if err != nil {
		t.Fatalf("AppendCompact failed: %v", err)
	}
	writer.Close()

	// 5. 重新重放
	replayed, err := Replay(logPath)
	if err != nil {
		t.Fatalf("Replay after compact failed: %v", err)
	}
	// 压缩前 8 条，其中 [0, 5] 共 6 条被替换为 1 条 summary，剩余 index 6, 7 共 2 条
	// 期望总数：1 + 2 = 3 条
	if len(replayed) != 3 {
		t.Fatalf("expected 3 messages after compact, got %d", len(replayed))
	}
	if replayed[0].Role != "user" || replayed[0].Content != SummaryPrefix+"用户打了招呼并查看了文件" {
		t.Fatalf("unexpected summary message: %+v", replayed[0])
	}
	if replayed[1].Content != "continue next" || replayed[2].Content != "ok" {
		t.Fatalf("unexpected retained messages: %+v, %+v", replayed[1], replayed[2])
	}

	// 原始文件依然保持所有行（7 条 msg + 1 条 compact = 8 行 + 1 轮 prompt = 9 行）
	data, err := os.ReadFile(logPath)
	if err != nil {
		t.Fatalf("ReadFile failed: %v", err)
	}
	if len(data) == 0 {
		t.Fatalf("log file should not be empty")
	}
}

// ── 2026-11-05：估算口径与预算切分点 ─────────────────────────────

func TestEstimateStringTokensConservative(t *testing.T) {
	if got := EstimateStringTokens(""); got != 0 {
		t.Fatalf("空串应为 0，got %d", got)
	}
	// ASCII：4 字节/token（400 字节 ≈ 100 token）
	if got := EstimateStringTokens(strings.Repeat("a", 400)); got != 100 {
		t.Fatalf("400 个 ASCII 应估 100 token，got %d", got)
	}
	// 中文：每个字至少 1 token（按英文 4 字节/token 会少算）
	zh := strings.Repeat("中", 100)
	if got := EstimateStringTokens(zh); got < 100 {
		t.Fatalf("100 个汉字至少 100 token（保守口径），got %d", got)
	}
	// 同样的字节数，中文必须估得比 ASCII 多
	zhBytes := EstimateStringTokens(strings.Repeat("中", 40))  // 120 字节
	enBytes := EstimateStringTokens(strings.Repeat("a", 120)) // 120 字节
	if zhBytes <= enBytes {
		t.Fatalf("中文应比同字节 ASCII 估得多：zh=%d en=%d", zhBytes, enBytes)
	}
	// 补充平面字符（emoji）一个 rune 可能被切成多片
	if got := EstimateStringTokens("😀"); got < 2 {
		t.Fatalf("emoji 至少按 2 token 估，got %d", got)
	}
}

func TestEstimateMessageTokensCountsToolArguments(t *testing.T) {
	args := `{"path":"big.txt","content":"` + strings.Repeat("x", 4000) + `"}`
	m := Message{Role: "assistant", ToolCalls: []ToolCall{{
		ID: "call_1", Type: "function",
		Function: FuncSpec{Name: "write_file", Arguments: args},
	}}}
	got := EstimateMessageTokens(m)
	if got < 1000 {
		t.Fatalf("4000 字节参数至少 1000 token，got %d", got)
	}
}

func TestFindCompressionCutPointMaximalRetentionWithinBudget(t *testing.T) {
	msgs := []Message{
		{Role: "user", Content: "goal zero"},
		{Role: "assistant", Content: "answer zero"},
		{Role: "user", Content: "goal one"},
		{Role: "assistant", Content: "answer one"},
		{Role: "user", Content: "goal two"},
		{Role: "assistant", Content: "answer two"},
		{Role: "user", Content: "goal three"},
		{Role: "assistant", Content: "answer three"},
	}
	// 预算刚好够保留 [u2, a2, u3, a3]，再加 u1/a1 就超。
	budget := 0
	for _, m := range msgs[4:] {
		budget += EstimateMessageTokens(m)
	}
	cutTo, ok := FindCompressionCutPoint(msgs, budget)
	if !ok {
		t.Fatalf("应找到切分点")
	}
	if cutTo != 3 {
		t.Fatalf("应切在 u2 之前（cutTo=3），got %d", cutTo)
	}
}

func TestFindCompressionCutPointGiantLastTurnFallsBack(t *testing.T) {
	msgs := []Message{
		{Role: "user", Content: "first"},
		{Role: "assistant", Content: "ok"},
		{Role: "user", Content: "do a lot of reads"},
		{Role: "assistant", ToolCalls: []ToolCall{{ID: "c0", Type: "function",
			Function: FuncSpec{Name: "read_file", Arguments: `{"path":"a"}`}}}},
	}
	for i := 0; i < 20; i++ {
		msgs = append(msgs, Message{
			Role: "tool", ToolCallID: fmt.Sprintf("c%d", i),
			Content: strings.Repeat("x", 2000),
		})
	}
	// 预算 1 token：最后 1 轮根本装不下。合法解只能是“退回最近的 user 边界”，
	// 即保留整轮、切在 u1 之前——绝不能返回 false，也不能切进 tool 配对中间。
	cutTo, ok := FindCompressionCutPoint(msgs, 1)
	if !ok {
		t.Fatalf("大单轮应退回最近 user 边界，而不是放弃压缩")
	}
	if cutTo != 1 {
		t.Fatalf("应切在最近 user（index 2）之前，cutTo=1，got %d", cutTo)
	}
	if msgs[cutTo+1].Role != "user" {
		t.Fatalf("保留段必须以 user 开头，got %q", msgs[cutTo+1].Role)
	}
}

func TestFindCompressionCutPointNoUserBoundary(t *testing.T) {
	if _, ok := FindCompressionCutPoint([]Message{
		{Role: "user", Content: "only"},
		{Role: "assistant", Content: "no next turn"},
	}, 100); ok {
		t.Fatalf("只有一轮时不应有合法切分点")
	}
	if _, ok := FindCompressionCutPoint([]Message{
		{Role: "user", Content: "only"},
		{Role: "assistant", Content: "reply"},
		{Role: "tool", ToolCallID: "c1", Content: "result"},
	}, 100); ok {
		t.Fatalf("msgs[1:] 里没有 user 时不应有合法切分点")
	}
}
