// TestSession_WorkspaceMCPRediscovery：工作区切换后 MCP 配置重发现（2026-11-07）。
//
// 修复的缺陷：handleWorkspace 只 chdir + loadMemories——旧工作区的 mcp.json
// 服务变成幽灵工具（cwd 已走、进程还挂着旧目录），新工作区的 mcp.json 被无视；
// 而 hello.mcp 是启动期快照，运行期变化没有任何通道到达 GUI。
//
// 判据（会话级，outCollector 收全量 JSONL，协议 §3.14）：
//   ① 两头都没配过 MCP ⇒ 静默（无 t:"mcp"、无 [MCP] 通知）——没有可说的变化；
//   ② 切到带 mcp.json 的工作区 ⇒ ack 之后恰一条 t:"mcp"（source=workspace、
//      config 指向新工作区、mock 服务 6 工具），hub 工具表真实注册；
//   ③ 切回无配置工作区 ⇒ t:"mcp" 空载荷（无证据字段 = 「现在没有 MCP」）+
//      「外部工具已清空」system 块，工具表清空。
package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"ecdi-fake-core/internal/wire"
)

func writeMockMCPConfig(t *testing.T, dir string) string {
	t.Helper()
	cfg := map[string]any{"mcpServers": map[string]any{
		"mock": map[string]any{
			"command": os.Args[0],
			"args":    []string{"-test.run=TestHelperMCPMock", "--"},
			"env":     map[string]string{"GO_WANT_HELPER_MCP": "1"},
		},
	}}
	b, err := json.Marshal(cfg)
	if err != nil {
		t.Fatalf("marshal mcp.json: %v", err)
	}
	p := filepath.Join(dir, "mcp.json")
	if err := os.WriteFile(p, b, 0o644); err != nil {
		t.Fatalf("write mcp.json: %v", err)
	}
	return p
}

func collectorLinesOf(c *outCollector) []string {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]string(nil), c.lines...)
}

func TestSession_WorkspaceMCPRediscovery(t *testing.T) {
	wsB := t.TempDir()
	cfgPath := writeMockMCPConfig(t, wsB)
	wsA := t.TempDir()   // 起点：无 MCP 配置
	wsC := t.TempDir()   // 终点：同样无 MCP 配置

	sess, col, _, stc := newTestSessionCfg(t,
		llmConfig{BaseURL: "http://127.0.0.1:1", Model: "m"}, "read_file", wsA)
	if sess.mcp == nil {
		t.Fatal("hub 应存在（空），nil 意味着初始化路径变了")
	}
	if sess.mcp.Status().hasEvidence() {
		t.Fatalf("起点（无配置工作区）不应有 MCP 证据: %+v", sess.mcp.Status())
	}
	defer sess.mcp.Close()

	switchTo := func(id, dir string) {
		t.Helper()
		sess.handleWorkspace(wire.GuiMsg{T: "workspace", RequestID: id, Path: dir},
			wire.StateOf(stc.Load()))
	}

	mcpLines := func() []map[string]any {
		var out []map[string]any
		for _, l := range collectorLinesOf(col) {
			var m map[string]any
			if json.Unmarshal([]byte(l), &m) != nil {
				continue
			}
			if m["t"] == "mcp" {
				out = append(out, m)
			}
		}
		return out
	}

	// ① 两头都无配置 ⇒ 静默（A → C 都没有 mcp.json）
	switchTo("w0", wsC)
	if got := mcpLines(); len(got) != 0 {
		t.Fatalf("两头都无配置时不得发 t:\"mcp\"，got %d 条: %v", len(got), got)
	}
	for _, l := range collectorLinesOf(col) {
		if strings.Contains(l, "[MCP]") {
			t.Fatalf("两头都无配置时不得有 [MCP] 通知: %s", l)
		}
	}

	// ② 切到带 mcp.json 的 B ⇒ 恰一条 t:"mcp"，source/config/tools 全对
	switchTo("w1", wsB)
	got := mcpLines()
	if len(got) != 1 {
		t.Fatalf("切到 B 后应恰一条 t:\"mcp\"，got %d 条: %v", len(got), got)
	}
	m := got[0]
	if m["source"] != "workspace" {
		t.Fatalf("source 应为 workspace，got %v", m["source"])
	}
	if m["config"] != cfgPath {
		t.Fatalf("config 应为新工作区配置路径 %s，got %v", cfgPath, m["config"])
	}
	svcs, _ := m["services"].([]any)
	if len(svcs) != 1 {
		t.Fatalf("services 应恰 1 个（mock），got %v", m["services"])
	} else if svc, _ := svcs[0].(map[string]any); svc["name"] != "mock" ||
		svc["tools"] != float64(6) || svc["transport"] != "stdio" {
		t.Fatalf("mock 服务快照应为 mock/6/stdio，got %v", svcs[0])
	}
	if len(sess.mcp.ToolDefs()) == 0 {
		t.Fatal("重发现后工具表应非空")
	}
	if _, _, ok := sess.mcp.Lookup("mcp__mock__echo"); !ok {
		t.Fatalf("工具表应含压缩名 mcp__mock__echo，got %v", sess.mcp.ToolDefs())
	}

	// ③ 切回无配置的 C ⇒ 空载荷 t:"mcp" + 清空通知
	switchTo("w2", wsC)
	got = mcpLines()
	if len(got) != 2 {
		t.Fatalf("切到 C 后应累计 2 条 t:\"mcp\"，got %d 条: %v", len(got), got)
	}
	m = got[1]
	if m["source"] != nil || m["config"] != nil || m["problem"] != nil {
		t.Fatalf("空载荷不得带证据字段，got %v", m)
	}
	if svcs, _ := m["services"].([]any); len(svcs) != 0 {
		t.Fatalf("空载荷 services 应为空数组，got %v", m["services"])
	}
	if len(sess.mcp.ToolDefs()) != 0 {
		t.Fatalf("离开 B 后工具表应清空，got %d 个", len(sess.mcp.ToolDefs()))
	}
	sawNotice := false
	for _, l := range collectorLinesOf(col) {
		if strings.Contains(l, "外部工具已清空") {
			sawNotice = true
		}
	}
	if !sawNotice {
		t.Fatal("应有「外部工具已清空」system 通知")
	}
}
