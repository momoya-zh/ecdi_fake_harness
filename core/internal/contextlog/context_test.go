package contextlog

import (
	"os"
	"path/filepath"
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

	// 寻找切分点（保留最近 2 条）
	cutTo, ok := FindCompressionCutPoint(msgs, 2)
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
