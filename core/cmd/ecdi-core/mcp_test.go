package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

// TestHelperMCPMock：供单元测试拉起的子进程 Mock MCP Server。
func TestHelperMCPMock(t *testing.T) {
	if os.Getenv("GO_WANT_HELPER_MCP") != "1" {
		return
	}

	sendResp := func(id any, result any, errObj *jsonrpcError) {
		resp := jsonrpcResponse{
			JSONRPC: "2.0",
			ID:      id,
		}
		if errObj != nil {
			resp.Error = errObj
		} else {
			raw, _ := json.Marshal(result)
			resp.Result = raw
		}
		data, _ := json.Marshal(resp)
		_, _ = os.Stdout.Write(append(data, '\n'))
	}

	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := bytes.TrimSpace(scanner.Bytes())
		if len(line) == 0 {
			continue
		}
		var req jsonrpcRequest
		if err := json.Unmarshal(line, &req); err != nil {
			continue
		}

		switch req.Method {
		case "initialize":
			res := mcpInitializeResult{
				ProtocolVersion: "2024-11-05",
				Capabilities:    map[string]any{},
			}
			res.ServerInfo.Name = "mock-server"
			res.ServerInfo.Version = "1.0.0"
			sendResp(req.ID, res, nil)

		case "notifications/initialized":
			// 通知无响应

		case "notifications/cancelled":
			// ★ 取消通知要落盘才可断言「我们真的发了」（不是只在客户端日志里说发了）。
			// 服务端此刻可能还在执行上一个 tools/call（sleep_tool），所以本行会在
			// 结果回执之后才被读到——测试按「等文件出现」而不是「立即读到」。
			if p := os.Getenv("GO_MOCK_MCP_CANCEL_LOG"); p != "" {
				b, _ := json.Marshal(req.Params)
				f, err := os.OpenFile(p, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
				if err == nil {
					_, _ = f.Write(append(b, '\n'))
					_ = f.Close()
				}
			}

		case "tools/list":
			// 分页分支（GO_MOCK_MCP_PAGINATE=1）：规范允许 nextCursor，客户端必须追到底
			var lp struct {
				Cursor string `json:"cursor"`
			}
			if pBytes, err := json.Marshal(req.Params); err == nil {
				_ = json.Unmarshal(pBytes, &lp)
			}
			if os.Getenv("GO_MOCK_MCP_PAGINATE") == "1" {
				if lp.Cursor == "" {
					sendResp(req.ID, mcpToolsListResult{
						Tools:      []mcpTool{{Name: "page1_tool", Description: "first page"}},
						NextCursor: "c2",
					}, nil)
				} else {
					sendResp(req.ID, mcpToolsListResult{
						Tools: []mcpTool{{Name: "page2_tool", Description: "second page"}},
					}, nil)
				}
				continue
			}

			tools := []mcpTool{
				{
					Name:        "echo",
					Description: "Echo back input",
					InputSchema: map[string]any{
						"type": "object",
						"properties": map[string]any{
							"msg": map[string]any{"type": "string"},
						},
						"required": []string{"msg"},
					},
				},
				{
					Name:        "failing_tool",
					Description: "A tool that reports isError=true",
					InputSchema: nil,
				},
				{
					Name:        "sleep_tool",
					Description: "A tool that sleeps",
					InputSchema: nil,
				},
				{
					Name:        "crash_tool",
					Description: "A tool that crashes the process",
					InputSchema: nil,
				},
				{
					Name:        "big_tool",
					Description: "Returns more than the output cap",
					InputSchema: nil,
				},
				{
					Name:        "spawn_tree_tool",
					Description: "Spawns a grandchild that keeps writing a heartbeat",
					InputSchema: nil,
				},
			}
			// ★ 只在环境白名单测试里出现：默认工具数（6）是若干既有断言的基线，
			//   为一个用例把计数全改一遍只会让「工具表变了」这件事失去信号。
			if os.Getenv("GO_MOCK_MCP_ENV_PROBE") == "1" {
				tools = append(tools, mcpTool{
					Name:        "env_probe",
					Description: "Return one environment variable",
					InputSchema: map[string]any{
						"type":       "object",
						"properties": map[string]any{"name": map[string]any{"type": "string"}},
						"required":   []string{"name"},
					},
				})
			}
			sendResp(req.ID, mcpToolsListResult{Tools: tools}, nil)

		case "tools/call":
			var params struct {
				Name      string         `json:"name"`
				Arguments map[string]any `json:"arguments"`
			}
			pBytes, _ := json.Marshal(req.Params)
			_ = json.Unmarshal(pBytes, &params)

			// 调用开始落盘：E2E 用它判断「exec 真的进去了」再发 cancel，
			// 否则取消可能落在 exec 之前（那测的是另一条路径）。
			if p := os.Getenv("GO_MOCK_MCP_CALL_LOG"); p != "" {
				f, err := os.OpenFile(p, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
				if err == nil {
					_, _ = f.WriteString(params.Name + "\n")
					_ = f.Close()
				}
			}

			switch params.Name {
			case "echo":
				msg, _ := params.Arguments["msg"].(string)
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{
						{Type: "text", Text: "echo: " + msg},
					},
				}, nil)
			case "failing_tool":
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{
						{Type: "text", Text: "operation failed"},
					},
					IsError: true,
				}, nil)
			case "sleep_tool":
				time.Sleep(2 * time.Second)
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{
						{Type: "text", Text: "done sleeping"},
					},
				}, nil)
			case "crash_tool":
				os.Exit(42)
			case "big_tool":
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{
						{Type: "text", Text: strings.Repeat("x", 100*1024)},
					},
				}, nil)
			case "spawn_tree_tool":
				// 拉一个**孙进程**（心跳写文件）。它的存活与否就是「整树清退」的判据：
				// 杀掉直系子进程而孙进程还活着，心跳文件会继续长。
				child := exec.Command(os.Args[0], "-test.run=TestHelperMCPHeartbeatChild", "--")
				child.Env = append(os.Environ(),
					"GO_WANT_MCP_HEARTBEAT=1",
					"GO_MOCK_MCP_HEARTBEAT="+os.Getenv("GO_MOCK_MCP_HEARTBEAT"),
				)
				_ = child.Start()
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{{Type: "text", Text: "spawned"}},
				}, nil)
			case "env_probe":
				name, _ := params.Arguments["name"].(string)
				sendResp(req.ID, mcpCallToolResult{
					Content: []mcpContentItem{{Type: "text", Text: os.Getenv(name)}},
				}, nil)
			default:
				sendResp(req.ID, nil, &jsonrpcError{Code: -32601, Message: "Method not found"})
			}
		}
	}
	os.Exit(0)
}

// TestHelperMCPHeartbeatChild：孙进程本体——每 50ms 往心跳文件追加一行。
// 只要它活着，文件就在长；整树清退生效后文件必须停止生长。
func TestHelperMCPHeartbeatChild(t *testing.T) {
	if os.Getenv("GO_WANT_MCP_HEARTBEAT") != "1" {
		return
	}
	path := os.Getenv("GO_MOCK_MCP_HEARTBEAT")
	if path == "" {
		os.Exit(0)
	}
	for i := 0; i < 400; i++ {
		f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			os.Exit(0)
		}
		_, _ = f.WriteString("tick\n")
		_ = f.Close()
		time.Sleep(50 * time.Millisecond)
	}
	os.Exit(0)
}

func TestMCP_SchemaSanitization(t *testing.T) {
	tool := mcpTool{
		Name:        "query",
		Description: "Database query",
		InputSchema: nil, // 模拟 schema 为空
	}
	defName, def := mcpToolToDef("db", tool)
	if defName != "mcp__db__query" {
		t.Fatalf("expected name mcp__db__query, got %s", defName)
	}
	if def.Name != "mcp__db__query" {
		t.Fatalf("expected def.Name mcp__db__query, got %s", def.Name)
	}
	if def.Description != "[MCP: db] Database query" {
		t.Fatalf("unexpected description: %s", def.Description)
	}

	params := def.Parameters
	if params["type"] != "object" {
		t.Fatalf("expected type object, got %v", params["type"])
	}
	req, ok := params["required"].([]string)
	if !ok || req == nil {
		t.Fatalf("required must be non-nil string slice, got %v", params["required"])
	}
}

func TestMCP_InputSchemaStripping(t *testing.T) {
	original := map[string]any{
		"$schema": "https://json-schema.org/draft/2020-12/schema",
		"type":    "object",
		"properties": map[string]any{
			"item": map[string]any{
				"$ref":        "#/$defs/Item",
				"description": "被引用的条目",
			},
			"list": map[string]any{
				"type":  "array",
				"items": map[string]any{"$ref": "#/$defs/Item"},
			},
			"legacy": map[string]any{"$ref": "#/definitions/Legacy"},
			// ★ 属性名恰好叫 $defs：剥键只发生在 schema 对象上，这里必须留下。
			"$defs":    map[string]any{"type": "string"},
			"external": map[string]any{"$ref": "https://example.com/schema.json"},
			"nestedMeta": map[string]any{
				"$schema":    "http://example.com/nested",
				"type":       "object",
				"properties": map[string]any{"a": map[string]any{"type": "string"}},
			},
		},
		"required": []string{"item"},
		"$defs": map[string]any{
			"Item": map[string]any{
				"type": "object",
				"properties": map[string]any{
					"id":    map[string]any{"type": "integer"},
					"child": map[string]any{"$ref": "#/$defs/Item"}, // 递归
				},
				"required": []string{"id"},
			},
		},
		"definitions": map[string]any{
			"Legacy": map[string]any{"type": "string", "enum": []any{"a", "b"}},
		},
	}

	// 规约本身要能报出递归降级（mcpToolToDef 只负责往 stderr 转述）。
	// 同一个 ref 被展开多次也只报一条：告警按工具去重，不能刷屏。
	warns := func() []string {
		_, w := sanitizeMCPInputSchema(original)
		return w
	}()
	if len(warns) != 1 {
		t.Fatalf("递归 $ref 应去重成 1 条 warning，got %d: %v", len(warns), warns)
	}
	if !strings.Contains(warns[0], "递归") {
		t.Fatalf("warning 应说明递归无法展开，got %v", warns)
	}

	_, def := mcpToolToDef("db", mcpTool{
		Name:        "query",
		Description: "Database query",
		InputSchema: original,
	})

	// 1. 元关键字与定义容器必须消失；本地引用字符串也不能残留。
	for _, key := range []string{"$schema", "$defs", "definitions"} {
		if _, has := def.Parameters[key]; has {
			t.Fatalf("parameters 仍含顶层 %s: %#v", key, def.Parameters)
		}
	}
	raw, err := json.Marshal(def.Parameters)
	if err != nil {
		t.Fatalf("marshal parameters: %v", err)
	}
	for _, bad := range []string{"#/$defs/Item", "#/definitions/Legacy"} {
		if strings.Contains(string(raw), bad) {
			t.Fatalf("本地 $ref 未展开（仍含 %s）：%s", bad, raw)
		}
	}

	props, ok := def.Parameters["properties"].(map[string]any)
	if !ok {
		t.Fatalf("parameters.properties 类型不对: %#v", def.Parameters["properties"])
	}

	// 2. $defs/Item 被展开，同层 description 保留。
	item, ok := props["item"].(map[string]any)
	if !ok {
		t.Fatalf("item 未被展开: %#v", props["item"])
	}
	if item["type"] != "object" || item["description"] != "被引用的条目" {
		t.Fatalf("item 展开后语义不对: %#v", item)
	}
	itemProps, ok := item["properties"].(map[string]any)
	if !ok {
		t.Fatalf("item.properties 类型不对: %#v", item["properties"])
	}
	if id, ok := itemProps["id"].(map[string]any); !ok || id["type"] != "integer" {
		t.Fatalf("item.properties.id 未展开: %#v", itemProps["id"])
	}
	// 3. 递归那一处降级为 {}，不能留下悬空 $ref 或 $defs。
	child, ok := itemProps["child"].(map[string]any)
	if !ok || len(child) != 0 {
		t.Fatalf("递归 child 应降级为空 schema，got %#v", itemProps["child"])
	}

	// 4. items 里的引用同样展开。
	list, ok := props["list"].(map[string]any)
	if !ok {
		t.Fatalf("list 类型不对: %#v", props["list"])
	}
	if items, ok := list["items"].(map[string]any); !ok || items["type"] != "object" {
		t.Fatalf("list.items 未展开: %#v", list["items"])
	}

	// 5. draft-07 的 definitions 也展开。
	legacy, ok := props["legacy"].(map[string]any)
	if !ok || legacy["type"] != "string" {
		t.Fatalf("legacy 未展开: %#v", props["legacy"])
	}
	if enum, ok := legacy["enum"].([]any); !ok || len(enum) != 2 {
		t.Fatalf("legacy.enum 丢失: %#v", legacy["enum"])
	}

	// 6. 名字叫 $defs 的**属性**不能被误删。
	if literal, ok := props["$defs"].(map[string]any); !ok || literal["type"] != "string" {
		t.Fatalf("属性名 $defs 被误删: %#v", props["$defs"])
	}

	// 7. 外链无法展开，原样保留（不替服务端改语义）。
	if external, ok := props["external"].(map[string]any); !ok ||
		external["$ref"] != "https://example.com/schema.json" {
		t.Fatalf("外链 $ref 应原样保留: %#v", props["external"])
	}

	// 7.1 嵌套 schema 里的 $schema 也要删（递归，不是只删根层）。
	if nested, ok := props["nestedMeta"].(map[string]any); !ok {
		t.Fatalf("nestedMeta 类型不对: %#v", props["nestedMeta"])
	} else if _, has := nested["$schema"]; has {
		t.Fatalf("嵌套 $schema 未剥离: %#v", nested)
	}

	// 8. 原 InputSchema 不被改写（同一 mcpTool 可能被冲突消解重算）。
	if _, has := original["$schema"]; !has {
		t.Fatal("原 schema 的 $schema 被删了（规约必须深拷贝）")
	}
	origProps := original["properties"].(map[string]any)
	if origItem, ok := origProps["item"].(map[string]any); !ok || origItem["$ref"] != "#/$defs/Item" {
		t.Fatalf("原 schema 的 item.$ref 被改写: %#v", origProps["item"])
	}

	// 9. 解析不了的本地 $ref：同样降级为 {} 并告警，不能留悬空引用。
	broken := map[string]any{
		"type": "object",
		"properties": map[string]any{
			"x": map[string]any{"$ref": "#/$defs/Missing"},
		},
		"$defs": map[string]any{},
	}
	brokenOut, brokenWarns := sanitizeMCPInputSchema(broken)
	if len(brokenWarns) != 1 || !strings.Contains(brokenWarns[0], "无法解析") {
		t.Fatalf("无法解析的 $ref 应报 1 条 warning，got %v", brokenWarns)
	}
	brokenProps := brokenOut["properties"].(map[string]any)
	if x, ok := brokenProps["x"].(map[string]any); !ok || len(x) != 0 {
		t.Fatalf("无法解析的 $ref 应降级为空 schema，got %#v", brokenProps["x"])
	}
}

func TestMCP_FindConfigFile(t *testing.T) {
	tmpDir := t.TempDir()

	// 1. 无文件时为空
	if p := findMCPConfigFile("", tmpDir); p != "" {
		t.Fatalf("expected empty path, got %s", p)
	}

	// 2. 工作区根目录下 mcp.json
	p2 := filepath.Join(tmpDir, "mcp.json")
	_ = os.WriteFile(p2, []byte(`{}`), 0644)
	if found := findMCPConfigFile("", tmpDir); found != p2 {
		t.Fatalf("expected %s, got %s", p2, found)
	}

	// 3. .ecdi/mcp.json 优先级高于根目录 mcp.json
	ecdiDir := filepath.Join(tmpDir, ".ecdi")
	_ = os.MkdirAll(ecdiDir, 0755)
	p3 := filepath.Join(ecdiDir, "mcp.json")
	_ = os.WriteFile(p3, []byte(`{}`), 0644)
	if found := findMCPConfigFile("", tmpDir); found != p3 {
		t.Fatalf("expected .ecdi path %s, got %s", p3, found)
	}

	// 4. 显式路径最高优先
	explicit := filepath.Join(tmpDir, "custom.json")
	_ = os.WriteFile(explicit, []byte(`{}`), 0644)
	if found := findMCPConfigFile(explicit, tmpDir); found != explicit {
		t.Fatalf("expected explicit path %s, got %s", explicit, found)
	}

	// 5. ★ 2026-11-06：发现来源必须能区分（hello.mcp.source）
	if p, src := findMCPConfigFileWithSource("", tmpDir); p != p3 || src != mcpConfigSourceWorkspace {
		t.Fatalf("expected workspace source at %s, got %s (%s)", p3, p, src)
	}
	if p, src := findMCPConfigFileWithSource(explicit, tmpDir); p != explicit || src != mcpConfigSourceExplicit {
		t.Fatalf("expected explicit source at %s, got %s (%s)", explicit, p, src)
	}
	if p, src := findMCPConfigFileWithSource(filepath.Join(tmpDir, "missing.json"), tmpDir); p != "" || src != mcpConfigSourceExplicit {
		t.Fatalf("missing explicit path must report explicit source, got %q (%s)", p, src)
	}
}

// TestMCP_HubStatus：产品面状态快照（hello.mcp 的数据源）。
//
// ★ 判据不是"能跑"而是"能解释"：好服务要带工具数、坏服务要带非空原因、
// 禁用服务不能混进任何一边；否则 GUI 只能显示一个无法归因的 0。
func TestMCP_HubStatus(t *testing.T) {
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"good": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env:     map[string]string{"GO_WANT_HELPER_MCP": "1"},
			},
			"broken": {Command: "ecdi-no-such-command-for-status-test"},
			"off":    {Command: "unused", Disabled: true},
		},
	}

	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	st := hub.Status()
	if len(st.Services) != 1 || st.Services[0].Name != "good" || st.Services[0].Tools != 6 ||
		st.Services[0].Transport != "stdio" {
		t.Fatalf("services 快照应为 good/6/stdio，got %+v", st.Services)
	}
	if len(st.Failed) != 1 || st.Failed[0].Name != "broken" || st.Failed[0].Error == "" {
		t.Fatalf("failed 快照应为 broken + 非空原因，got %+v", st.Failed)
	}
	if st.Disabled != 1 {
		t.Fatalf("disabled 应为 1，got %d", st.Disabled)
	}
	if !st.hasEvidence() {
		t.Fatalf("有配置/服务时 hasEvidence 必须为真：%+v", st)
	}

	// 负向：完全没配置时不得发 mcp 字段（否则每个会话都长出一张 0 服务卡）。
	empty, err := newMCPHubFromConfig(nil, t.TempDir())
	if err != nil {
		t.Fatalf("nil config: %v", err)
	}
	if empty.Status().hasEvidence() {
		t.Fatalf("无配置时 hasEvidence 必须为假：%+v", empty.Status())
	}
}

// TestMCP_ConfigProblemVisible：配置层错误（显式路径不存在 / JSON 坏）必须
// 留在 hub.status.Problem 里返回——会话靠它把"配置坏了"和"没配"分开。
func TestMCP_ConfigProblemVisible(t *testing.T) {
	ws := t.TempDir()
	missing := filepath.Join(ws, "nope.json")

	hub, err := NewMCPHub(missing, ws, defaultMCPCallTimeout)
	if err != nil {
		t.Fatalf("缺文件不应返回 error（本次不启用，但状态要可见）：%v", err)
	}
	st := hub.Status()
	if st.Problem == "" || st.Source != mcpConfigSourceExplicit || st.Config != missing {
		t.Fatalf("缺文件状态不符：%+v", st)
	}

	badPath := filepath.Join(ws, "mcp.json")
	if err := os.WriteFile(badPath, []byte(`{"mcpServers":`), 0644); err != nil {
		t.Fatal(err)
	}
	hub, err = NewMCPHub("", ws, defaultMCPCallTimeout)
	if err == nil || hub == nil {
		t.Fatalf("坏 JSON 应返回 hub + error，got hub=%v err=%v", hub, err)
	}
	st = hub.Status()
	if st.Problem == "" || st.Source != mcpConfigSourceWorkspace || st.Config != badPath {
		t.Fatalf("坏 JSON 状态不符：%+v", st)
	}
}

func TestMCP_HubLifecycleAndToolExecution(t *testing.T) {
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"mock": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
			},
		},
	}

	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 1. 验证工具注册
	if !hub.HasTool("mcp__mock__echo") {
		t.Fatalf("expected tool mcp__mock__echo registered")
	}
	if !hub.HasTool("mcp__mock__failing_tool") {
		t.Fatalf("expected tool mcp__mock__failing_tool registered")
	}

	defs := hub.ToolDefs()
	if len(defs) != 6 {
		t.Fatalf("expected 6 toolDefs, got %d", len(defs))
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// 2. 正常调用
	res, handled := hub.CallTool(ctx, "mcp__mock__echo", map[string]any{"msg": "hello from test"})
	if !handled {
		t.Fatalf("tool should be handled")
	}
	if !res.OK {
		t.Fatalf("tool call should succeed, got output: %s", res.Output)
	}
	if res.Output != "echo: hello from test" {
		t.Fatalf("unexpected output: %s", res.Output)
	}

	// 3. 失败工具调用（isError: true）
	resFail, handledFail := hub.CallTool(ctx, "mcp__mock__failing_tool", map[string]any{})
	if !handledFail {
		t.Fatalf("tool should be handled")
	}
	if resFail.OK {
		t.Fatalf("expected res.OK == false")
	}
	if resFail.Output != "operation failed" {
		t.Fatalf("unexpected fail output: %s", resFail.Output)
	}

	// 4. 未知工具（返回 handled=false）
	_, handledUnknown := hub.CallTool(ctx, "unknown_tool", nil)
	if handledUnknown {
		t.Fatalf("unknown tool should return handled=false")
	}

	// 5. 关闭后再次调用
	hub.Close()
	if len(hub.ToolDefs()) != 0 {
		t.Fatalf("toolDefs should be empty after close")
	}
}

func TestMCP_TimeoutAndCrash(t *testing.T) {
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"mock": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
			},
		},
	}

	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 1. 测试超时
	timeoutCtx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()

	resTimeout, handled := hub.CallTool(timeoutCtx, "mcp__mock__sleep_tool", map[string]any{})
	if !handled {
		t.Fatalf("tool should be handled")
	}
	if resTimeout.OK {
		t.Fatalf("expected timeout error, but got OK")
	}
	if !strings.Contains(resTimeout.Output, "超时") {
		t.Fatalf("超时结果必须说清是超时：%s", resTimeout.Output)
	}

	// 2. 测试进程崩溃
	resCrash, handledCrash := hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
	if !handledCrash {
		t.Fatalf("tool should be handled")
	}
	if resCrash.OK {
		t.Fatalf("expected crash error, but got OK")
	}

	// 3. 崩溃后再次调用：默认开启按需重启 ⇒ 应当活过来（2026-11-06 七版）
	resAfterCrash, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "hi"})
	if !resAfterCrash.OK {
		t.Fatalf("崩溃后应按需重启并成功，got %+v", resAfterCrash)
	}
}

func TestMCP_MultipleServers(t *testing.T) {
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"srv1": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
			},
			"srv2": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
			},
			"disabled_srv": {
				Command:  os.Args[0],
				Args:     []string{"-test.run=TestHelperMCPMock", "--"},
				Disabled: true,
			},
		},
	}

	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 验证 srv1 与 srv2 工具分别注册且带前缀
	if !hub.HasTool("mcp__srv1__echo") {
		t.Fatalf("expected mcp__srv1__echo")
	}
	if !hub.HasTool("mcp__srv2__echo") {
		t.Fatalf("expected mcp__srv2__echo")
	}
	if hub.HasTool("mcp__disabled_srv__echo") {
		t.Fatalf("disabled server should not be registered")
	}

	// 验证两服务并行调用独立工作
	res1, _ := hub.CallTool(context.Background(), "mcp__srv1__echo", map[string]any{"msg": "from srv1"})
	if !res1.OK || res1.Output != "echo: from srv1" {
		t.Fatalf("unexpected res1: %+v", res1)
	}

	res2, _ := hub.CallTool(context.Background(), "mcp__srv2__echo", map[string]any{"msg": "from srv2"})
	if !res2.OK || res2.Output != "echo: from srv2" {
		t.Fatalf("unexpected res2: %+v", res2)
	}
}

// TestMCP_ProcessTreeKilled：**整树**清退（不是只杀直系子进程）。
//
// ★ 判据是行为，不是调用：让 MCP server 拉一个会持续写心跳文件的**孙进程**，
//
//	然后关掉 hub —— 心跳必须停。只 `Process.Kill()` 直系子进程的实现会红
//	（`npx` → node 正是这个形状）。
func TestMCP_ProcessTreeKilled(t *testing.T) {
	hb := filepath.Join(t.TempDir(), "heartbeat.txt")
	size := func() int64 {
		st, err := os.Stat(hb)
		if err != nil {
			return 0
		}
		return st.Size()
	}

	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"mock": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP":    "1",
					"GO_MOCK_MCP_HEARTBEAT": hb,
				},
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}

	if _, ok := hub.CallTool(context.Background(), "mcp__mock__spawn_tree_tool", map[string]any{}); !ok {
		t.Fatalf("spawn_tree_tool 应被处理")
	}
	// 先确认孙进程真的活起来了（否则后面的「停止生长」没有区分力）
	deadline := time.Now().Add(3 * time.Second)
	for size() == 0 && time.Now().Before(deadline) {
		time.Sleep(20 * time.Millisecond)
	}
	if size() == 0 {
		t.Fatalf("孙进程没起来，判据无效（心跳文件一直是空的）")
	}

	hub.Close()
	time.Sleep(200 * time.Millisecond) // 让在途 tick 落地
	before := size()
	time.Sleep(500 * time.Millisecond)
	after := size()
	if after != before {
		t.Fatalf("孙进程仍在跑（心跳 %d -> %d 字节）：整树清退没生效，只杀了直系子进程", before, after)
	}
}

// TestMCP_ToolNameSanitization：MCP 名字空间 → provider 的 function.name 空间。
//
// ★ 判据是**真实约束**（`^[a-zA-Z0-9_-]{1,64}$`，OpenAI 兼容端点含 DeepSeek 都按
//
//	这条校验），不是「看着顺眼」：MCP 规范对工具名没有任何约束，原样上交会让
//	**整轮请求** 400 —— 一次配置就把会话打死。同名同输入必须恒得同名（可复现）。
func TestMCP_ToolNameSanitization(t *testing.T) {
	valid := regexp.MustCompile(`^[a-zA-Z0-9_-]{1,64}$`)

	exact := []struct{ server, tool, want string }{
		{"db", "query", "mcp__db__query"},
		{"fs", "git.status", "mcp__fs__git_status"},
		{"fs", "read file", "mcp__fs__read_file"},
		{"s", "a:b/c", "mcp__s__a_b_c"},
	}
	for _, c := range exact {
		got := mcpToolName(c.server, c.tool)
		if got != c.want {
			t.Fatalf("mcpToolName(%q,%q) = %q, want %q", c.server, c.tool, got, c.want)
		}
	}

	// 中文 + 超长 + 特殊字符：只要求「合法、≤64、稳定、可区分」
	longTool := strings.Repeat("工具名", 50) // 中文，且远超 64 字节
	names := []string{
		mcpToolName("中文服务", longTool),
		mcpToolName("very-long-server-name-that-eats-the-budget", longTool),
		mcpToolName("very-long-server-name-that-eats-the-budget", longTool+"X"),
	}
	for i, n := range names {
		if !valid.MatchString(n) {
			t.Fatalf("name[%d] 不合法：%q", i, n)
		}
		if len(n) > providerToolNameMax {
			t.Fatalf("name[%d] 超长 %d：%q", i, len(n), n)
		}
	}
	if names[1] == names[2] {
		t.Fatalf("两个不同工具压出了同名：%q（截断后必须靠哈希区分）", names[1])
	}
	if again := mcpToolName("very-long-server-name-that-eats-the-budget", longTool); again != names[1] {
		t.Fatalf("同名同输入必须恒得同名（可复现），got %q vs %q", again, names[1])
	}

	// 撞名消解：`git.status` 与 `git_status` 压成同一个 plain name（`.` 与 `_` 不同源）
	a := mcpToolName("s", "git.status")
	b := mcpToolName("s", "git_status")
	if a != b {
		t.Fatalf("前提错了：两个工具本该压成同名，got %q / %q", a, b)
	}
	hashed := mcpToolNameHashed("s", "git_status", true)
	if hashed == a || !valid.MatchString(hashed) {
		t.Fatalf("撞名消解失败：%q", hashed)
	}
}

// TestMCP_OutputClip：输出上限必须**显式标注**（本仓纪律），不能静默截。
func TestMCP_OutputClip(t *testing.T) {
	small := "hello"
	if got := clipMCPOutput(small); got != small {
		t.Fatalf("未超限时不该动：%q", got)
	}

	big := strings.Repeat("y", mcpOutLimit*2)
	got := clipMCPOutput(big)
	if len(got) >= len(big) {
		t.Fatalf("超限输出没有被截：%d -> %d", len(big), len(got))
	}
	if !strings.Contains(got, "中间省略") {
		t.Fatalf("截断必须显式标注，got 前 80 字：%q", got[:80])
	}
	if len(got) > mcpOutLimit+200 {
		t.Fatalf("截断后仍超上限：%d", len(got))
	}
}

// TestMCP_DeadServerNotice：服务崩掉不能只体现在「工具反复失败」上——
// hub 必须登记一次，会话取走后清空（同一次死亡只说一次）。
func TestMCP_DeadServerNotice(t *testing.T) {
	// 关掉重启：这条测的是「彻底放弃时怎么说明白」的旧路径。
	withMCPOptions(t, mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: false})
	hub := newTestHub(t)
	defer hub.Close()

	if notes := hub.TakeNotices(); len(notes) != 0 {
		t.Fatalf("一开始不该有死亡通告：%v", notes)
	}

	// 撞崩进程
	res, handled := hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
	if !handled || res.OK {
		t.Fatalf("crash_tool 应被处理且失败：handled=%v res=%+v", handled, res)
	}
	// 崩溃后再次调用：必须回明确文本（不是空输出、不是死锁）
	res2, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "hi"})
	if res2.OK {
		t.Fatalf("崩溃后不该成功")
	}
	if strings.TrimSpace(res2.Output) == "" {
		t.Fatalf("崩溃后必须给出非空说明（空输出会让模型以为参数写错了）")
	}

	if !strings.Contains(res2.Output, "本会话不会自动重启它") {
		t.Fatalf("关掉重启时必须说清楚不重启：%s", res2.Output)
	}

	notes := hub.TakeNotices()
	if len(notes) != 1 || !strings.Contains(notes[0].Text, "mock") {
		t.Fatalf("应有且仅有一条 mock 的死亡通告：%v", notes)
	}
	if notes[0].OK {
		t.Fatalf("死亡通告的 ok 必须是 false：%v", notes[0])
	}
	if again := hub.TakeNotices(); len(again) != 0 {
		t.Fatalf("取走后必须清空（同一次死亡只说一次）：%v", again)
	}
}

// TestTransport_BrokenPipeClosesDoneFirst：管道断开时，**等者被唤醒之前** `Done()`
// 必须已经关闭。
//
// ★ 为什么这条判据重要：`CallTool` 只在 `client.isDead()`（= `<-transport.Done()`
// 可读）时才 `noteDead`。若 readLoop 先释放 pending 等者、再关闭 done，那么等者拿到
// 的是普通错误 `"连接断开"`，而此刻 `isDead()` 还是 false ⇒ 死亡通告漏发，用户看到
// 的是「工具莫名失败」而没有任何 system 块。真实现象：`tools/mcp-smoke` 的 F 臂
// （崩溃后应有 system 块）偶发 82/83。
//
// ★ 为什么放 N 个等者：这个窗口只有几微秒，单个等者能否撞上取决于调度（实测
// 2/5 概率）。放多个等者并要求**全部**满足，就把「有没有修对」变成确定性判据：
// 修好之后 closeDone 在排空之前，N 个等者必然全见 done 已关；没修则至少有一个
// 撞进窗口。
func TestTransport_BrokenPipeClosesDoneFirst(t *testing.T) {
	const n = 128
	for i := 0; i < 8; i++ {
		tr := newStdioTransport("t", mcpServerConfig{}, "")
		woke := make([]chan bool, n)
		for j := 0; j < n; j++ {
			ch := make(chan jsonrpcResponse, 1)
			tr.pending[fmt.Sprint(j)] = ch
			woke[j] = make(chan bool, 1)
			go func(ch chan jsonrpcResponse, out chan bool) {
				<-ch // 等者：请求被「断开」释放
				select {
				case <-tr.Done():
					out <- true
				default:
					out <- false
				}
			}(ch, woke[j])
		}

		tr.readLoop(strings.NewReader("")) // 立刻 EOF，模拟服务端进程消失

		for j := 0; j < n; j++ {
			if ok := <-woke[j]; !ok {
				t.Fatalf("第 %d 轮（等者 %d/%d）：等者先于 Done() 被唤醒 ⇒ "+
					"CallTool 会看到 isDead()=false，死亡通告漏发", i, j, n)
			}
		}
	}
}

// TestTransport_SSEBrokenStreamClosesDoneFirst：legacy SSE 的同一判据。
func TestTransport_SSEBrokenStreamClosesDoneFirst(t *testing.T) {
	const n = 128
	for i := 0; i < 8; i++ {
		tr := &sseHTTPTransport{
			name:    "t",
			pending: make(map[string]chan jsonrpcResponse),
			done:    make(chan struct{}),
		}
		woke := make([]chan bool, n)
		for j := 0; j < n; j++ {
			ch := make(chan jsonrpcResponse, 1)
			tr.pending[fmt.Sprint(j)] = ch
			woke[j] = make(chan bool, 1)
			go func(ch chan jsonrpcResponse, out chan bool) {
				<-ch
				select {
				case <-tr.Done():
					out <- true
				default:
					out <- false
				}
			}(ch, woke[j])
		}

		tr.readStream(strings.NewReader(""), make(chan *url.URL, 1), make(chan error, 1))

		for j := 0; j < n; j++ {
			if ok := <-woke[j]; !ok {
				t.Fatalf("第 %d 轮（等者 %d/%d）：SSE 等者先于 Done() 被唤醒 ⇒ 死亡通告漏发",
					i, j, n)
			}
		}
	}
}

// ── 七版（2026-11-06）：env minimal / 取消语义 / 按需重启 ────────────

// TestMCP_EnvFilter：白名单本身的行为。纯函数断言，不起进程。
func TestMCP_EnvFilter(t *testing.T) {
	t.Setenv("MCP_SMOKE_SECRET_TOKEN", "leak-me")
	t.Setenv("ECDI_CORE_KEY", "sk-core")

	inherit := envMap(mcpChildEnv(false))
	if inherit["mcp_smoke_secret_token"] != "leak-me" {
		t.Fatalf("inherit 模式应继承普通环境变量（这是既有口径）：%v", inherit["mcp_smoke_secret_token"])
	}
	if _, ok := inherit["ecdi_core_key"]; ok {
		t.Fatalf("inherit 模式仍必须剔除 ECDI_*_KEY（既有纪律）")
	}

	minimal := envMap(mcpChildEnv(true))
	if _, ok := minimal["mcp_smoke_secret_token"]; ok {
		t.Fatalf("minimal 模式不得继承白名单外的变量")
	}
	if _, ok := minimal["ecdi_core_key"]; ok {
		t.Fatalf("minimal 模式不得继承密钥")
	}
	if minimal["path"] == "" {
		t.Fatalf("minimal 模式必须保留 PATH（否则 node/python server 起不来）")
	}
}

// TestMCP_MinimalEnvChild：真子进程视角的白名单——配置 env 是显式增量，必须保留。
func TestMCP_MinimalEnvChild(t *testing.T) {
	withMCPOptions(t, mcpRuntimeOptions{EnvMode: mcpEnvMinimal, Respawn: true})
	t.Setenv("MCP_SMOKE_SECRET_TOKEN", "leak-me")
	t.Setenv("ECDI_CORE_KEY", "sk-core")

	hub := newTestHubWithEnv(t, map[string]string{
		"GO_MOCK_MCP_ENV_PROBE": "1",
		"MCP_CONF_VISIBLE":      "yes",
	})
	defer hub.Close()

	probe := func(name string) string {
		t.Helper()
		res, handled := hub.CallTool(context.Background(), "mcp__mock__env_probe", map[string]any{"name": name})
		if !handled || !res.OK {
			t.Fatalf("env_probe(%s) 失败：handled=%v res=%+v", name, handled, res)
		}
		return res.Output
	}

	if got := probe("MCP_SMOKE_SECRET_TOKEN"); got != "" {
		t.Fatalf("minimal 模式泄漏了普通环境变量：%q", got)
	}
	if got := probe("ECDI_CORE_KEY"); got != "" {
		t.Fatalf("minimal 模式泄漏了 core 密钥：%q", got)
	}
	if got := probe("MCP_CONF_VISIBLE"); got != "yes" {
		t.Fatalf("配置里的 env 是显式增量，必须保留；got %q", got)
	}
	if got := probe("PATH"); strings.TrimSpace(got) == "" {
		t.Fatalf("PATH 必须保留（否则 server 起不来）")
	}
}

// TestMCP_CancelNotifiesServer：取消不能只是「我们不等了」——必须真发
// notifications/cancelled（落盘在 mock 侧），并且结果文本如实说明送没送到。
func TestMCP_CancelNotifiesServer(t *testing.T) {
	logPath := filepath.Join(t.TempDir(), "cancelled.log")
	hub := newTestHubWithEnv(t, map[string]string{"GO_MOCK_MCP_CANCEL_LOG": logPath})
	defer hub.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 150*time.Millisecond)
	defer cancel()
	res, handled := hub.CallTool(ctx, "mcp__mock__sleep_tool", map[string]any{})
	if !handled || res.OK {
		t.Fatalf("超时调用应被处理且失败：handled=%v res=%+v", handled, res)
	}
	if !strings.Contains(res.Output, "取消通知") || !strings.Contains(res.Output, "超时") {
		t.Fatalf("结果文本必须说清超时与取消通知：%s", res.Output)
	}

	// mock 正卡在 sleep_tool 里，通知要等它回到读循环才被看到。
	deadline := time.Now().Add(5 * time.Second)
	var raw []byte
	for time.Now().Before(deadline) {
		raw, _ = os.ReadFile(logPath)
		if len(bytes.TrimSpace(raw)) > 0 {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if len(bytes.TrimSpace(raw)) == 0 {
		t.Fatalf("mock 未收到 notifications/cancelled（取消只停了我们自己）")
	}
	var params map[string]any
	if err := json.Unmarshal(bytes.TrimSpace(raw), &params); err != nil {
		t.Fatalf("取消通知不是 JSON：%v (%q)", err, raw)
	}
	if params["reason"] != "调用超时" {
		t.Fatalf("reason 不符：%v", params["reason"])
	}
	if _, ok := params["requestId"]; !ok {
		t.Fatalf("取消通知必须带原请求 id：%v", params)
	}

	// 取消 ≠ 断连：服务进程还在，后续调用照常。
	res2, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "alive"})
	if !res2.OK || res2.Output != "echo: alive" {
		t.Fatalf("取消后连接应仍可用：%+v", res2)
	}
}

// TestMCP_CancelText：四种组合的人话（送没送到 × 取消/超时），一个都不能含糊。
func TestMCP_CancelText(t *testing.T) {
	cases := []struct {
		err      *mcpCanceledError
		want     string
		notWant  string
	}{
		{&mcpCanceledError{Reason: context.Canceled, Notified: true}, "已向 MCP 服务发送取消通知", "未能送达"},
		{&mcpCanceledError{Reason: context.Canceled, Notified: false}, "未能送达", "已向 MCP 服务发送"},
		{&mcpCanceledError{Reason: context.DeadlineExceeded, Notified: true}, "调用超时", "用户取消"},
		{&mcpCanceledError{Reason: context.DeadlineExceeded, Notified: false}, "调用超时", "已向 MCP 服务发送"},
	}
	for i, c := range cases {
		got := c.err.toolText()
		if !strings.Contains(got, c.want) || strings.Contains(got, c.notWant) {
			t.Fatalf("case %d: got %q（应含 %q，不应含 %q）", i, got, c.want, c.notWant)
		}
	}
}

// TestMCP_RespawnAfterCrash：崩溃后的下一次调用应把服务拉起来，工具表重注册
// 且**不重复**（naive append 会让 provider 收到两份同名定义 ⇒ 整轮 400）。
func TestMCP_RespawnAfterCrash(t *testing.T) {
	withMCPOptions(t, mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: true})
	hub := newTestHub(t)
	defer hub.Close()

	res, handled := hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
	if !handled || res.OK {
		t.Fatalf("crash_tool 应失败：handled=%v res=%+v", handled, res)
	}

	res2, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "revived"})
	if !res2.OK || res2.Output != "echo: revived" {
		t.Fatalf("重启后调用应成功：%+v", res2)
	}

	notes := hub.TakeNotices()
	if len(notes) != 1 || !notes[0].OK || !strings.Contains(notes[0].Text, "已自动重启") {
		t.Fatalf("应有一条「已自动重启」的人向通告：%+v", notes)
	}

	defs := hub.ToolDefs()
	if len(defs) != 6 {
		t.Fatalf("重启后工具数应仍为 6（重复注册会让 provider 400）：%d", len(defs))
	}
	seen := make(map[string]bool, len(defs))
	for _, d := range defs {
		if seen[d.Name] {
			t.Fatalf("工具定义重复：%s", d.Name)
		}
		seen[d.Name] = true
	}
}

// TestMCP_RespawnDisabled：关掉开关时退回旧行为（明确说不重启）。
func TestMCP_RespawnDisabled(t *testing.T) {
	withMCPOptions(t, mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: false})
	hub := newTestHub(t)
	defer hub.Close()

	hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
	res, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "hi"})
	if res.OK {
		t.Fatalf("关掉重启后调用不该成功：%+v", res)
	}
	if !strings.Contains(res.Output, "不会自动重启") {
		t.Fatalf("必须说清楚不重启：%s", res.Output)
	}
	if notes := hub.TakeNotices(); len(notes) != 1 || notes[0].OK {
		t.Fatalf("应有一条失败通告：%+v", notes)
	}
}

// TestMCP_RespawnCap：崩→起→崩不是无界的。到上限后放弃并说明白。
func TestMCP_RespawnCap(t *testing.T) {
	withMCPOptions(t, mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: true})
	hub := newTestHub(t)
	defer hub.Close()

	for i := 0; i < mcpRespawnMax; i++ {
		hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
		res, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "again"})
		if !res.OK {
			t.Fatalf("第 %d 次重启应成功：%+v", i+1, res)
		}
	}
	hub.CallTool(context.Background(), "mcp__mock__crash_tool", map[string]any{})
	res, _ := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "nope"})
	if res.OK {
		t.Fatalf("超过上限后不该成功：%+v", res)
	}
	if !strings.Contains(res.Output, "上限") {
		t.Fatalf("超上限必须说清原因：%s", res.Output)
	}
	notes := hub.TakeNotices()
	if len(notes) == 0 {
		t.Fatalf("超上限必须有通告")
	}
	last := notes[len(notes)-1]
	if last.OK || !strings.Contains(last.Text, "不再尝试") {
		t.Fatalf("最后一条通告应说明放弃重启：%+v", notes)
	}
}

// TestMCP_ToolsListPagination：nextCursor 必须追到底，否则分页服务端的工具
// 会被静默丢掉一半（模型看不见就不会调用，且没有任何提示）。
func TestMCP_ToolsListPagination(t *testing.T) {
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"paged": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP":   "1",
					"GO_MOCK_MCP_PAGINATE": "1",
				},
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	for _, want := range []string{"mcp__paged__page1_tool", "mcp__paged__page2_tool"} {
		if !hub.HasTool(want) {
			t.Fatalf("缺工具 %s（tools/list 分页没追到底）；已注册：%v", want, hub.ToolDefs())
		}
	}
}

// newTestHub：拉起一个 mock MCP 服务（单测公共夹具）。
func newTestHub(t *testing.T) *mcpHub {
	t.Helper()
	return newTestHubWithEnv(t, nil)
}

// newTestHubWithEnv：同上，外加子进程配置里的私有 env（GO_* 开关 / 落盘路径）。
// ★ 走配置 env 而不是 t.Setenv：那正是「用户显式给的增量」在真实链路上的形态。
func newTestHubWithEnv(t *testing.T, extra map[string]string) *mcpHub {
	t.Helper()
	env := map[string]string{"GO_WANT_HELPER_MCP": "1"}
	for k, v := range extra {
		env[k] = v
	}
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"mock": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env:     env,
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	return hub
}

// withMCPOptions：临时改运行期开关（env minimal / respawn），测完还原。
func withMCPOptions(t *testing.T, o mcpRuntimeOptions) {
	t.Helper()
	old := getMCPOptions()
	setMCPOptions(o)
	t.Cleanup(func() { setMCPOptions(old) })
}

func envMap(env []string) map[string]string {
	out := make(map[string]string, len(env))
	for _, kv := range env {
		if eq := strings.Index(kv, "="); eq >= 0 {
			out[strings.ToLower(kv[:eq])] = kv[eq+1:]
		}
	}
	return out
}

// 发现配置 → 拉起 Server → caps 声明 → LLM 生成 MCP tool_call → ask 门及 detail 提案卡
// → 批准执行 → tool_result 回灌 → LLM 产出最终回复。
// TestLoopE2E_MCPTool：端到端验证 MCP 工具在完整 Agent 循环中的闭环表现：
// 发现配置 → 拉起 Server → caps 声明 → LLM 生成 MCP tool_call → ask 门
// → 批准执行 → tool_result 回灌 → LLM 产出最终回复。
func TestLoopE2E_MCPTool(t *testing.T) {
	ws := t.TempDir()
	mcpPath := filepath.Join(ws, "mcp.json")
	mcpCfg := fmt.Sprintf(`{
		"mcpServers": {
			"mock": {
				"command": %s,
				"args": ["-test.run=TestHelperMCPMock", "--"],
				"env": {
					"GO_WANT_HELPER_MCP": "1"
				}
			},
			"broken": {
				"command": "ecdi-no-such-mcp-server-for-test"
			}
		}
	}`, mustJSON(os.Args[0]))
	if err := os.WriteFile(mcpPath, []byte(mcpCfg), 0644); err != nil {
		t.Fatal(err)
	}

	var recorded [][]byte
	scripts := []sseScript{
		{ // 第 1 轮：模型调用 mcp__mock__echo 工具
			chunks: []string{
				deltaChunk("", "调用外部 MCP 服务", ""),
				deltaChunk("", "", `[{"index":0,"id":"call-mcp-1","function":{"name":"mcp__mock__echo","arguments":"{\"msg\":\"mcp-ok\"}"}}]`),
			},
		},
		{ // 第 2 轮：拿到 MCP 执行结果后回复
			chunks: []string{deltaChunk("MCP 回执已收到。", "", "")},
			usage:  &chatUsage{PromptTokens: 88, CompletionTokens: 33},
		},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)

	// 1. 握手与 caps 校验
	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":6,"gui":"test"}`)
	hello := waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })
	hasMCP := false
	for _, c := range hello["caps"].([]any) {
		if c == "mcp" {
			hasMCP = true
		}
	}
	if !hasMCP {
		t.Fatalf("mcp cap missing in hello: %v", hello["caps"])
	}

	// 1b. ★ 2026-11-06：产品面状态（hello.mcp）——好服务带工具数、坏服务带原因、
	//    来源/路径可核对。这是 GUI 副标/卡片/失败告警的唯一数据源。
	st, ok := hello["mcp"].(map[string]any)
	if !ok {
		t.Fatalf("hello 应带 mcp 状态对象：%v", hello)
	}
	if st["source"] != "workspace" || st["config"] != mcpPath {
		t.Fatalf("mcp 来源/路径不符：%v", st)
	}
	services, _ := st["services"].([]any)
	if len(services) != 1 {
		t.Fatalf("mcp.services 应只有 mock（broken 不算服务）：%v", st["services"])
	}
	svc, _ := services[0].(map[string]any)
	if svc["name"] != "mock" || svc["tools"] != float64(6) || svc["transport"] != "stdio" {
		t.Fatalf("mcp.services[0] 应为 mock/6/stdio：%v", svc)
	}
	failed, _ := st["failed"].([]any)
	if len(failed) != 1 {
		t.Fatalf("mcp.failed 应含 broken：%v", st["failed"])
	}
	fail, _ := failed[0].(map[string]any)
	if fail["name"] != "broken" {
		t.Fatalf("mcp.failed[0].name 应为 broken：%v", fail)
	}
	if msg, _ := fail["error"].(string); msg == "" {
		t.Fatalf("mcp.failed[0].error 必须非空：%v", fail)
	}

	// 1c. 启动失败必须发 system 块（产品模式默认可见，且进 journal）。
	waitFor(t, col, "[MCP] 启动失败块", func(m map[string]any) bool {
		if m["t"] != "block_start" || m["kind"] != "system" {
			return false
		}
		text, _ := m["text"].(string)
		return strings.Contains(text, "服务 broken 启动失败")
	})

	// 2. 发 prompt
	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"请调用 echo 服务"}`)

	// 3. ask 门出现（MCP 工具默认全过 ask 门）
	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	askID := ask["id"].(string)
	q := ask["question"].(string)
	if !strings.Contains(q, "echo") || !strings.Contains(q, "mock") {
		t.Fatalf("ask.question 应包含 MCP 工具及服务名，got %q", q)
	}

	// ★ MCP 的 ask 自 2026-11-06 起带 detail（与 GUI 渲染分支同批放开）：
	//   判别值固定 "mcp"，服务名与真实工具名拆进 server/toolName（一版把复合名
	//   塞进 detail.tool，建出「展开了但什么都没有」的空卡——已撤销的教训）。
	//   echo 无压缩，问句断言（echo/mock）不受影响。
	det, ok := ask["detail"].(map[string]any)
	if !ok {
		t.Fatalf("MCP ask 应带 detail 对象：%v", ask)
	}
	if det["tool"] != "mcp" || det["server"] != "mock" || det["toolName"] != "echo" {
		t.Fatalf("detail 判别值/服务/真实工具名不符：%v", det)
	}
	kv, ok := det["argsKv"].([]any)
	if !ok || len(kv) != 1 {
		t.Fatalf("argsKv 应为 1 项键值数组：%v", det["argsKv"])
	}
	entry, ok := kv[0].(map[string]any)
	if !ok || entry["k"] != "msg" || entry["v"] != `"mcp-ok"` {
		t.Fatalf("argsKv[0] 应为 msg=`\"mcp-ok\"`（值是 JSON 文本）：%v", kv[0])
	}

	// 4. 用户允许
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(askID)))

	// 5. 工具结果输出（真执行来自 mock MCP server）
	waitFor(t, col, "tool_result delta", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "echo: mcp-ok")
	})

	// 6. 最终助手回复
	waitFor(t, col, "final text", func(m map[string]any) bool {
		return m["t"] == "delta" && strings.Contains(m["text"].(string), "MCP 回执已收到。")
	})

	// 7. 回到 idle
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})

	// 8. 收尾
	sendLine(lines, stc, `{"t":"bye"}`)
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("session did not exit after bye")
	}
}

// waitFileContains：等一个由**子进程**写的文件出现内容（跨进程的判据）。
func waitFileContains(t *testing.T, path, want string, d time.Duration) {
	t.Helper()
	deadline := time.Now().Add(d)
	for time.Now().Before(deadline) {
		if b, err := os.ReadFile(path); err == nil && strings.Contains(string(b), want) {
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
	b, _ := os.ReadFile(path)
	t.Fatalf("%s 在 %s 内未出现 %q（现有内容 %q）", path, d, want, b)
}

// TestLoopE2E_MCPCancelHonest：用户在 MCP 调用中途按停止。
// 判据三件：① tool_result 说清「已通知服务端」；② tool_call 收尾写「已取消」；
// ③ 服务端**真的**收到了 notifications/cancelled（跨进程落盘，不是客户端自说自话）。
func TestLoopE2E_MCPCancelHonest(t *testing.T) {
	ws := t.TempDir()
	callLog := filepath.Join(t.TempDir(), "calls.log")
	cancelLog := filepath.Join(t.TempDir(), "cancelled.log")
	mcpPath := filepath.Join(ws, "mcp.json")
	mcpCfg := fmt.Sprintf(`{
		"mcpServers": {
			"mock": {
				"command": %s,
				"args": ["-test.run=TestHelperMCPMock", "--"],
				"env": {
					"GO_WANT_HELPER_MCP": "1",
					"GO_MOCK_MCP_CALL_LOG": %s,
					"GO_MOCK_MCP_CANCEL_LOG": %s
				}
			}
		}
	}`, mustJSON(os.Args[0]), mustJSON(callLog), mustJSON(cancelLog))
	if err := os.WriteFile(mcpPath, []byte(mcpCfg), 0644); err != nil {
		t.Fatal(err)
	}

	var recorded [][]byte
	scripts := []sseScript{
		{ // 只有一轮：取消后本轮不会再请求模型
			chunks: []string{
				deltaChunk("", "启动一个长任务。", ""),
				deltaChunk("", "", `[{"index":0,"id":"call-mcp-cancel","function":{"name":"mcp__mock__sleep_tool","arguments":"{}"}}]`),
			},
		},
	}
	srv := newScriptedServer(t, "test-key-123", scripts, &recorded)

	s, col, lines, stc := newTestSession(t, srv.URL, "", ws)
	done := runSession(s)

	sendLine(lines, stc, `{"t":"hello","seq":0,"protocol":6,"gui":"test"}`)
	waitFor(t, col, "hello", func(m map[string]any) bool { return m["t"] == "hello" })

	sendLine(lines, stc, `{"t":"prompt","seq":1,"client_id":"p1","text":"跑个长任务"}`)
	ask := waitFor(t, col, "ask", func(m map[string]any) bool { return m["t"] == "ask" })
	sendLine(lines, stc, fmt.Sprintf(`{"t":"answer","seq":2,"id":%s,"choice":"allow"}`, mustJSON(ask["id"])))

	// ★ 等**服务端**确实收到调用再按停止：否则取消可能落在 exec 之前，
	//   测到的就只是「没执行」，而不是「执行中被取消」。
	waitFileContains(t, callLog, "sleep_tool", 5*time.Second)
	sendLine(lines, stc, `{"t":"cancel"}`)

	waitFor(t, col, "tool_result 诚实文本", func(m map[string]any) bool {
		text, _ := m["text"].(string)
		return m["t"] == "delta" && strings.Contains(text, "用户取消") && strings.Contains(text, "已向 MCP 服务发送取消通知")
	})
	waitFor(t, col, "tool_call summary=已取消", func(m map[string]any) bool {
		return m["t"] == "block_end" && m["summary"] == "已取消"
	})
	waitFor(t, col, "state cancelled", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "cancelled"
	})
	waitFor(t, col, "state idle", func(m map[string]any) bool {
		return m["t"] == "state" && m["value"] == "idle"
	})

	waitFileContains(t, cancelLog, `"reason":"用户取消"`, 5*time.Second)

	sendLine(lines, stc, `{"t":"bye"}`)
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("session did not exit after bye")
	}
}

// ── 单次调用超时测试 ──────────────────────────────────────────────

func TestMCP_CallTimeout_HubDefault(t *testing.T) {
	isolateMCPDiscovery(t)
	cancelLog := filepath.Join(t.TempDir(), "cancel.log")
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"mock": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP":    "1",
					"GO_MOCK_MCP_CANCEL_LOG": cancelLog,
				},
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 设置全局单次调用超时为 150ms
	hub.SetCallTimeout(150 * time.Millisecond)

	// 1. 调用 sleep_tool（会 sleep 1s）触发超时，传入普通 context.Background()
	res, handled := hub.CallTool(context.Background(), "mcp__mock__sleep_tool", map[string]any{})
	if !handled {
		t.Fatal("sleep_tool should be handled")
	}
	if res.OK {
		t.Fatal("expected timeout failure, got OK=true")
	}
	if !strings.Contains(res.Output, "调用超时") || !strings.Contains(res.Output, "已向 MCP 服务发送取消通知") {
		t.Fatalf("预期超时提示人话，got: %s", res.Output)
	}

	// 确认服务端真收到了 cancellation 通知
	waitFileContains(t, cancelLog, `"reason":"调用超时"`, 3*time.Second)

	// 2. 超时后子进程未被强杀，同一连接继续可用
	resEcho, handledEcho := hub.CallTool(context.Background(), "mcp__mock__echo", map[string]any{"msg": "alive after timeout"})
	if !handledEcho {
		t.Fatal("echo should be handled")
	}
	if !resEcho.OK || !strings.Contains(resEcho.Output, "alive after timeout") {
		t.Fatalf("expected echo success after timeout, got: %+v", resEcho)
	}
}

func TestMCP_CallTimeout_ConfigOverride(t *testing.T) {
	isolateMCPDiscovery(t)
	timeoutSec := 0.15 // 150ms 服务级超时
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"custom": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
				Timeout: &timeoutSec,
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 全局设为 60s，但该服务配置了 150ms
	hub.SetCallTimeout(60 * time.Second)

	start := time.Now()
	res, handled := hub.CallTool(context.Background(), "mcp__custom__sleep_tool", map[string]any{})
	elapsed := time.Since(start)
	if !handled {
		t.Fatal("tool should be handled")
	}
	if res.OK {
		t.Fatal("expected timeout, got OK=true")
	}
	if !strings.Contains(res.Output, "调用超时") {
		t.Fatalf("expected timeout text, got: %s", res.Output)
	}
	// 验证确实是在 ~150ms 超时，而不是等了 60s
	if elapsed > 2*time.Second {
		t.Fatalf("expected quick timeout (~150ms), but took %v", elapsed)
	}
}

func TestMCP_CallTimeout_ConfigDisableZero(t *testing.T) {
	isolateMCPDiscovery(t)
	zeroTimeout := 0.0 // 0 = 显式禁用超时
	cfg := &mcpConfigFile{
		MCPServers: map[string]mcpServerConfig{
			"custom": {
				Command: os.Args[0],
				Args:    []string{"-test.run=TestHelperMCPMock", "--"},
				Env: map[string]string{
					"GO_WANT_HELPER_MCP": "1",
				},
				Timeout: &zeroTimeout,
			},
		},
	}
	hub, err := newMCPHubFromConfig(cfg, t.TempDir())
	if err != nil {
		t.Fatalf("newMCPHubFromConfig failed: %v", err)
	}
	defer hub.Close()

	// 全局设为 100ms 超时，但该服务配置了 timeout: 0（不设限）
	hub.SetCallTimeout(100 * time.Millisecond)

	// 传入普通 echo 调用，应该顺利完成
	res, handled := hub.CallTool(context.Background(), "mcp__custom__echo", map[string]any{"msg": "zero-timeout-ok"})
	if !handled || !res.OK {
		t.Fatalf("expected tool success with zero timeout, got: %+v", res)
	}
}

// TestMCP_DiscoveryChain_Injected：发现链纯核心的全分支表测（2026-11-07，
// mcp-host-plan §五「单测密闭性」闭合）。目录来源全部由参数注入，不读进程环境——
// 第三级 UserHomeDir 分支在此前「APPDATA 恒有」的开发机上结构性测不到，这里补齐。
// 产品包装层（os.Getenv / os.UserHomeDir）由 isolateMCPDiscovery 的环境隔离守。
func TestMCP_DiscoveryChain_Injected(t *testing.T) {
	root := t.TempDir()
	wsBoth := filepath.Join(root, "ws-both")   // .ecdi/mcp.json 与 mcp.json 并存
	wsPlain := filepath.Join(root, "ws-plain") // 只有 mcp.json
	wsEmpty := filepath.Join(root, "ws-empty") // 两处都没有
	appData := filepath.Join(root, "appdata")
	home := filepath.Join(root, "home")
	for _, d := range []string{
		filepath.Join(wsBoth, ".ecdi"), wsPlain, wsEmpty,
		filepath.Join(appData, "ecdi-fake-harness"),
		filepath.Join(home, ".config", "ecdi-fake-harness"),
	} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	dotEcdi := filepath.Join(wsBoth, ".ecdi", "mcp.json")
	wsCfg := filepath.Join(wsPlain, "mcp.json")
	globalCfg := filepath.Join(appData, "ecdi-fake-harness", "mcp.json")
	homeCfg := filepath.Join(home, ".config", "ecdi-fake-harness", "mcp.json")
	for _, p := range []string{dotEcdi, wsCfg, globalCfg, homeCfg} {
		if err := os.WriteFile(p, []byte(`{"servers":{}}`), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	cases := []struct {
		name       string
		explicitIn string
		wsRoot     string
		appData    string
		home       string
		wantPath   string
		wantSource string
	}{
		{"explicit hit", wsCfg, "", "", "", wsCfg, mcpConfigSourceExplicit},
		// 契约：显式路径不存在 ⇒ ("", "explicit")——不静默换下一级文件。
		{"explicit miss", filepath.Join(root, "nope", "mcp.json"), wsPlain, appData, "", "", mcpConfigSourceExplicit},
		{"workspace .ecdi 优先于 mcp.json", "", wsBoth, appData, home, dotEcdi, mcpConfigSourceWorkspace},
		{"workspace mcp.json", "", wsPlain, appData, home, wsCfg, mcpConfigSourceWorkspace},
		{"workspace 两者皆无 ⇒ 落全局 appData", "", wsEmpty, appData, home, globalCfg, mcpConfigSourceGlobal},
		{"appData 空 ⇒ 回落 home/.config（此前测不到的分支）", "", wsEmpty, "", home, homeCfg, mcpConfigSourceGlobal},
		// else-if 语义（与旧链逐位一致）：appData 非空时 home 分支不参与，
		// 即使 appData 下没有配置文件也不回落——Windows 上 APPDATA 恒有 ⇒
		// home 分支在生产是死码，这正是它此前测不到的第二重原因。
		{"appData 非空但无文件 ⇒ 双空（不回落 home）", "", wsEmpty, filepath.Join(root, "no-appdata"), home, "", ""},
		{"全部为空/不存在 ⇒ 双空", "", wsEmpty, "", "", "", ""},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			gotPath, gotSource := findMCPConfigFileIn(tc.explicitIn, tc.wsRoot, tc.appData, tc.home)
			if gotPath != tc.wantPath || gotSource != tc.wantSource {
				t.Fatalf("findMCPConfigFileIn(%q, %q, %q, %q) = (%q, %q), want (%q, %q)",
					tc.explicitIn, tc.wsRoot, tc.appData, tc.home,
					gotPath, gotSource, tc.wantPath, tc.wantSource)
			}
		})
	}
}
