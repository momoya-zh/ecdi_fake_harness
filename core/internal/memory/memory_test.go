package memory

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestLoad_NonExistentDirs(t *testing.T) {
	res, err := Load("non/existent/global", "non/existent/workspace", 10000)
	if err != nil {
		t.Fatalf("unexpected error on missing dirs: %v", err)
	}
	if len(res.Files) != 0 || len(res.LoadedFiles) != 0 {
		t.Fatalf("expected 0 files, got %d", len(res.Files))
	}
	if res.PromptText != "" {
		t.Fatalf("expected empty prompt text, got %q", res.PromptText)
	}
}

func TestLoad_NormalAndOverride(t *testing.T) {
	tmpDir := t.TempDir()
	globalDir := filepath.Join(tmpDir, "global")
	wsDir := filepath.Join(tmpDir, "workspace")

	if err := os.MkdirAll(globalDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(wsDir, 0755); err != nil {
		t.Fatal(err)
	}

	// 全局记忆：a.md, common.md, not_md.txt
	if err := os.WriteFile(filepath.Join(globalDir, "a.md"), []byte("Global A content"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(globalDir, "common.md"), []byte("Global Common"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(globalDir, "ignore.txt"), []byte("Should be ignored"), 0644); err != nil {
		t.Fatal(err)
	}

	// 工作区记忆：b.md, common.md (覆盖全局 common.md)
	if err := os.WriteFile(filepath.Join(wsDir, "b.md"), []byte("Workspace B content"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(wsDir, "common.md"), []byte("Workspace Common Overridden"), 0644); err != nil {
		t.Fatal(err)
	}

	res, err := Load(globalDir, wsDir, 0) // 无 contextLimit，无上限
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	if len(res.LoadedFiles) != 3 {
		t.Fatalf("expected 3 loaded files, got %d", len(res.LoadedFiles))
	}

	// 顺序应为：全局 a.md，工作区 b.md, common.md
	expectedOrder := []struct {
		name    string
		source  string
		content string
	}{
		{"a.md", "global", "Global A content"},
		{"b.md", "workspace", "Workspace B content"},
		{"common.md", "workspace", "Workspace Common Overridden"},
	}

	for i, exp := range expectedOrder {
		got := res.LoadedFiles[i]
		if got.Name != exp.name {
			t.Errorf("file[%d] name = %q, expected %q", i, got.Name, exp.name)
		}
		if got.Source != exp.source {
			t.Errorf("file[%d] source = %q, expected %q", i, got.Source, exp.source)
		}
		if got.Content != exp.content {
			t.Errorf("file[%d] content = %q, expected %q", i, got.Content, exp.content)
		}
	}

	// 验证 Prompt 格式
	if !strings.Contains(res.PromptText, "## 记忆 (Memory)") {
		t.Errorf("PromptText missing memory heading: %s", res.PromptText)
	}
	if !strings.Contains(res.PromptText, "### [全局] a.md") {
		t.Errorf("PromptText missing global section: %s", res.PromptText)
	}
	if !strings.Contains(res.PromptText, "### [工作区] common.md") {
		t.Errorf("PromptText missing overridden workspace section: %s", res.PromptText)
	}
	if strings.Contains(res.PromptText, "Global Common") {
		t.Errorf("PromptText contains overridden global content!")
	}
}

func TestLoad_CapacityLimitAndSkip(t *testing.T) {
	tmpDir := t.TempDir()
	wsDir := filepath.Join(tmpDir, "workspace")
	if err := os.MkdirAll(wsDir, 0755); err != nil {
		t.Fatal(err)
	}

	// 设定 contextLimit = 1000 tokens
	// 10% 上限 = 100 tokens
	// 1 token = 4 bytes => 100 tokens = 400 bytes 预算

	// 1. small.md: 100 bytes (25 tokens) -> 可加载 (剩余 75 tokens)
	smallData := strings.Repeat("A", 100)
	if err := os.WriteFile(filepath.Join(wsDir, "1_small.md"), []byte(smallData), 0644); err != nil {
		t.Fatal(err)
	}

	// 2. huge.md: 350 bytes (88 tokens) -> 25 + 88 = 113 > 100 -> 超限跳过！整份不截断
	hugeData := strings.Repeat("B", 350)
	if err := os.WriteFile(filepath.Join(wsDir, "2_huge.md"), []byte(hugeData), 0644); err != nil {
		t.Fatal(err)
	}

	// 3. tiny.md: 40 bytes (10 tokens) -> 25 + 10 = 35 <= 100 -> 可加载！
	tinyData := strings.Repeat("C", 40)
	if err := os.WriteFile(filepath.Join(wsDir, "3_tiny.md"), []byte(tinyData), 0644); err != nil {
		t.Fatal(err)
	}

	res, err := Load("", wsDir, 1000)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	if len(res.LoadedFiles) != 2 {
		t.Fatalf("expected 2 loaded files, got %d", len(res.LoadedFiles))
	}
	if len(res.SkippedFiles) != 1 {
		t.Fatalf("expected 1 skipped file, got %d", len(res.SkippedFiles))
	}

	if res.SkippedFiles[0].Name != "2_huge.md" {
		t.Errorf("expected 2_huge.md to be skipped, got %s", res.SkippedFiles[0].Name)
	}
	if !strings.Contains(res.SkippedFiles[0].SkipReason, "超出容量上限") {
		t.Errorf("expected skip reason to mention capacity limit, got %s", res.SkippedFiles[0].SkipReason)
	}

	// 确保 2_huge.md 内容未进入 PromptText
	if strings.Contains(res.PromptText, "2_huge.md") || strings.Contains(res.PromptText, hugeData) {
		t.Errorf("skipped file leaked into prompt text!")
	}
	// 确保 1_small.md 与 3_tiny.md 完整进入
	if !strings.Contains(res.PromptText, "1_small.md") || !strings.Contains(res.PromptText, "3_tiny.md") {
		t.Errorf("loaded files missing from prompt text: %s", res.PromptText)
	}
}

func TestLoad_NoLimitWhenContextZero(t *testing.T) {
	tmpDir := t.TempDir()
	wsDir := filepath.Join(tmpDir, "workspace")
	if err := os.MkdirAll(wsDir, 0755); err != nil {
		t.Fatal(err)
	}

	hugeData := strings.Repeat("X", 10000)
	if err := os.WriteFile(filepath.Join(wsDir, "huge.md"), []byte(hugeData), 0644); err != nil {
		t.Fatal(err)
	}

	// contextLimit = 0 时不设上限
	res, err := Load("", wsDir, 0)
	if err != nil {
		t.Fatal(err)
	}
	if len(res.LoadedFiles) != 1 || len(res.SkippedFiles) != 0 {
		t.Fatalf("expected 1 loaded, 0 skipped with context=0, got loaded=%d, skipped=%d",
			len(res.LoadedFiles), len(res.SkippedFiles))
	}
}
