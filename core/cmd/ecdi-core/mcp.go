// mcp.go —— MCP Host（Model Context Protocol 客户端宿主，2026-11-05）。
//
// 定位与职责（docs/positioning.md §5 / docs/mcp-host-plan.md）：
//   - 本项目充当 MCP Host（授予能力的一方）：拉起 MCP Server 子进程，管理配置与生命周期；
//   - 零第三方依赖：纯 Go 标准库（os/exec, bufio, encoding/json）实现 stdio JSON-RPC 2.0 传输；
//   - 命名空间隔离：工具名格式为 `mcp__<server>__<tool>`，避免与内置工具重名；
//   - 权限自守：MCP 工具默认全过 ask 门，可在 --allow-tools 中显式豁免；
//   - 进程整树安全：会话结束时强制清理所有 MCP Server 进程，杜绝僵尸残留。

package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	// mcpProtocolVersion：本客户端实现的**连接级**语义版本（initialize 握手）。
	//
	// ★ 这是**刻意的降级**，不是笔误：positioning §5.3 记的锚是 2026-07-28（无状态、
	//   逐请求协商、无连接级 initialize），而存量 stdio server 绝大多数仍实现
	//   2024-11-05 这一版握手。取舍理由与将来切换条件写在 docs/mcp-host-plan.md §二.3。
	//   服务端回一个**不同**版本时只记日志、不中断（协商差异必须被说出来，见
	//   startAndInit 的版本检查）。
	mcpProtocolVersion = "2024-11-05"

	// providerToolNameMax：OpenAI 兼容端点对 `function.name` 的硬约束是
	// `^[a-zA-Z0-9_-]{1,64}$`（DeepSeek 同）。★ MCP 规范对工具名**没有**这条约束
	// （`git.status` / 中文 / 带空格都合法）⇒ 原样上交会让**整轮请求** 400，
	// 不是这个工具失败，是这轮对话死。见 mcpToolName。
	providerToolNameMax = 64

	// mcpOutLimit：MCP 工具输出上限，与 run_command 的 24KB 同口径。
	mcpOutLimit = 24 << 10

	// mcpInitTimeoutS：单个服务的握手预算。★ 并发发起（newMCPHubFromConfig），
	// 但它仍在 hello 之前的同步路径上 ⇒ 不能长到让 GUI 首屏干等。
	mcpInitTimeoutS = 5
)

// ── MCP 配置文件结构 ─────────────────────────────────────────────

type mcpServerConfig struct {
	Command  string            `json:"command"`
	Args     []string          `json:"args"`
	Env      map[string]string `json:"env,omitempty"`
	Disabled bool              `json:"disabled,omitempty"`
}

type mcpConfigFile struct {
	MCPServers map[string]mcpServerConfig `json:"mcpServers"`
}

// ── JSON-RPC 2.0 结构体 ─────────────────────────────────────────

type jsonrpcRequest struct {
	JSONRPC string `json:"jsonrpc"`
	ID      any    `json:"id,omitempty"`
	Method  string `json:"method"`
	Params  any    `json:"params,omitempty"`
}

type jsonrpcResponse struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      any             `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"` // 非空 = 服务端发来的**请求**（不是回执）
	Result  json.RawMessage `json:"result,omitempty"`
	Error   *jsonrpcError   `json:"error,omitempty"`
}

type jsonrpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
	Data    any    `json:"data,omitempty"`
}

// ── MCP 协议定义（2024-11-05 规范）───────────────────────────────

type mcpClientInfo struct {
	Name    string `json:"name"`
	Version string `json:"version"`
}

type mcpInitializeParams struct {
	ProtocolVersion string         `json:"protocolVersion"`
	Capabilities    map[string]any `json:"capabilities"`
	ClientInfo      mcpClientInfo  `json:"clientInfo"`
}

type mcpInitializeResult struct {
	ProtocolVersion string         `json:"protocolVersion"`
	Capabilities    map[string]any `json:"capabilities"`
	ServerInfo      struct {
		Name    string `json:"name"`
		Version string `json:"version"`
	} `json:"serverInfo"`
}

type mcpTool struct {
	Name        string         `json:"name"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
}

type mcpToolsListResult struct {
	Tools      []mcpTool `json:"tools"`
	NextCursor string    `json:"nextCursor,omitempty"` // 规范允许分页；不追到底 = 静默丢工具
}

type mcpContentItem struct {
	Type     string `json:"type"`
	Text     string `json:"text,omitempty"`
	Data     string `json:"data,omitempty"`
	MimeType string `json:"mimeType,omitempty"`
}

type mcpCallToolResult struct {
	Content []mcpContentItem `json:"content"`
	IsError bool             `json:"isError,omitempty"`
}

// ── 单个 MCP 客户端连接 ─────────────────────────────────────────

type mcpClient struct {
	name string
	cfg  mcpServerConfig
	cmd  *exec.Cmd

	// mu 只保护 pending 与 stdin 字段。★ **不覆盖阻塞写**：Close 要拿这把锁才能
	// 关 stdin，若一次 64KB 以上的写正卡在「不读 stdin 的服务端」上并占着锁，
	// 退出路径就会与它互等。写由 wmu 单独串行化（见 writeLine）。
	mu      sync.Mutex
	wmu     sync.Mutex
	stdin   io.WriteCloser
	reqID   atomic.Int64
	pending map[string]chan jsonrpcResponse
	closed  atomic.Bool
	done    chan struct{}
	// doneOnce：Close 与 readLoop 都会关闭 done（进程自己退出 vs 我们关它），
	// 两处各自 `select{case <-done: default: close(done)}` 是**无互斥**的，
	// 撞在一起就 "close of closed channel" panic ⇒ 收口到 once。
	doneOnce sync.Once
	// killTree：整树清退（Windows = Job Object 关句柄即整树；POSIX = 杀 -pgid）。
	// nil = 纳管失败已降级，只能杀直系子进程。
	killTree func()
	tools    []mcpTool
}

func newMCPClient(name string, cfg mcpServerConfig) *mcpClient {
	return &mcpClient{
		name:    name,
		cfg:     cfg,
		pending: make(map[string]chan jsonrpcResponse),
		done:    make(chan struct{}),
	}
}

func (c *mcpClient) closeDone() { c.doneOnce.Do(func() { close(c.done) }) }

// isDead：进程已退出（readLoop 收到 EOF 或 Close 走过）。
func (c *mcpClient) isDead() bool {
	select {
	case <-c.done:
		return true
	default:
		return false
	}
}

// startAndInit：拉起子进程、建立 stdio 读写循环、完成 initialize 握手并列出工具。
func (c *mcpClient) startAndInit(ctx context.Context, rootDir string) error {
	cmd := exec.Command(c.cfg.Command, c.cfg.Args...)
	if rootDir != "" {
		cmd.Dir = rootDir
	}

	// 环境变量：继承但剔除密钥，再追加配置中的私有 env
	env := childEnv()
	for k, v := range c.cfg.Env {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	cmd.Env = env

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("打开 stdin 失败: %w", err)
	}
	c.stdin = stdin

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = stdin.Close()
		return fmt.Errorf("打开 stdout 失败: %w", err)
	}

	// stderr 透传到 core 的 stderr，每行带服务名前缀（多服务时才知道是谁在叫）
	stderrW := &prefixWriter{prefix: "[mcp:" + c.name + "] ", w: os.Stderr}
	cmd.Stderr = stderrW

	// 启动：优先「挂起 → 挂进作业 → 恢复」把整棵树纳管（mcp_proc_*.go）。
	// ★ 纳管失败**只降级并告警**，不 fail-closed：MCP 不是安全边界
	//   （positioning §5.2「harness 只做配置 + 展示 + 转发」），这里要的是
	//   孤儿清理，不是沙盒。降级后我们只能杀直系子进程（npx 的 node 孙进程会活）。
	// ★ `started` 区分两件事：「进程起来了但没纳管上」（降级）vs「进程根本没起来」
	//   （直接失败）—— 混成一句会让「找不到可执行文件」被报成「整树纳管失败」。
	killTree, started, containErr := mcpStartContained(cmd)
	switch {
	case containErr == nil:
		c.killTree = killTree
	case !started:
		_ = stdin.Close()
		return containErr
	default:
		fmt.Fprintf(stderrW, "整树纳管失败，降级为只杀直系子进程：%v\n", containErr)
	}
	c.cmd = cmd

	// 启动读取协程
	go c.readLoop(stdout)

	// 1. 发送 initialize
	initParams := mcpInitializeParams{
		ProtocolVersion: mcpProtocolVersion,
		Capabilities:    map[string]any{},
		ClientInfo: mcpClientInfo{
			Name:    "ecdi-core",
			Version: "0.6.0",
		},
	}
	initRaw, err := c.sendRequest(ctx, "initialize", initParams)
	if err != nil {
		c.Close()
		return fmt.Errorf("initialize 握手失败: %w", err)
	}

	var initRes mcpInitializeResult
	if err := json.Unmarshal(initRaw, &initRes); err != nil {
		c.Close()
		return fmt.Errorf("解析 initialize 回执失败: %w", err)
	}

	// ★ 版本协商要**说出来**：服务端可以挑一个不同的 protocolVersion（规范允许），
	//   而我们只按 2024-11-05 的连接级语义实现。静默继续会让「2026 版无状态语义」
	//   与「连接级会话」的差异永远无人知晓——本仓对 I4 那类「缺了什么」的处置是
	//   显式提示，这里同一口径。不中断：tools/list 与 tools/call 在两版里是同一形状。
	if v := initRes.ProtocolVersion; v != "" && v != mcpProtocolVersion {
		fmt.Fprintf(stderrW, "服务端协商到 protocolVersion=%s（本客户端按 %s 语义实现，差异见 docs/mcp-host-plan.md §二.3）\n",
			v, mcpProtocolVersion)
	}

	// 2. 发送 notifications/initialized
	// ★ 错误只能吞：通知没有回执，连接已断的话下一步 tools/list 会失败并报到那里。
	_ = c.sendNotification("notifications/initialized", map[string]any{})

	// 3. 拉取 tools/list——**追到底**：规范允许分页（nextCursor），只取第一页
	//    等于静默丢工具（模型看不见就不可能调用，且没有任何提示）。
	var allTools []mcpTool
	cursor := ""
	for page := 0; page < 32; page++ {
		params := map[string]any{}
		if cursor != "" {
			params["cursor"] = cursor
		}
		listRaw, err := c.sendRequest(ctx, "tools/list", params)
		if err != nil {
			c.Close()
			return fmt.Errorf("拉取 tools/list 失败: %w", err)
		}
		var listRes mcpToolsListResult
		if err := json.Unmarshal(listRaw, &listRes); err != nil {
			c.Close()
			return fmt.Errorf("解析 tools/list 回执失败: %w", err)
		}
		allTools = append(allTools, listRes.Tools...)
		if listRes.NextCursor == "" || listRes.NextCursor == cursor {
			break
		}
		cursor = listRes.NextCursor
	}
	c.tools = allTools

	return nil
}

func (c *mcpClient) readLoop(r io.Reader) {
	scanner := bufio.NewScanner(r)
	// 允许单行长至 4MB（某些数据工具返回大 JSON）
	const maxLine = 4 * 1024 * 1024
	buf := make([]byte, 64*1024)
	scanner.Buffer(buf, maxLine)

	for scanner.Scan() {
		line := bytes.TrimSpace(scanner.Bytes())
		if len(line) == 0 {
			continue
		}
		var resp jsonrpcResponse
		if err := json.Unmarshal(line, &resp); err != nil {
			// 非 JSON-RPC 消息行（服务端往 stdout 打日志），忽略
			continue
		}
		if resp.Method != "" {
			// ★ 这是**服务端发来的请求**（sampling / roots / elicitation 之类），
			//   不是回执。我们声明了空 capabilities，正常不该出现；但静默丢弃会让
			//   对方挂等一个永不到来的答复 ⇒ 明确回 -32601，别让它卡住。
			if resp.ID != nil {
				c.replyError(resp.ID, -32601, "method not supported: "+resp.Method)
			}
			continue
		}
		if resp.ID == nil {
			// 通知或空 ID 响应，跳过
			continue
		}

		idKey := fmt.Sprint(resp.ID)
		c.mu.Lock()
		ch, ok := c.pending[idKey]
		if ok {
			delete(c.pending, idKey)
		}
		c.mu.Unlock()

		if ok && ch != nil {
			ch <- resp
		}
	}

	// 管道断开
	c.mu.Lock()
	for idKey, ch := range c.pending {
		delete(c.pending, idKey)
		close(ch)
	}
	c.mu.Unlock()

	c.closeDone()
}

// replyError：给**服务端发来的请求**回一个 JSON-RPC error（我们只做 -32601）。
func (c *mcpClient) replyError(id any, code int, msg string) {
	data, err := json.Marshal(map[string]any{
		"jsonrpc": "2.0",
		"id":      id,
		"error":   jsonrpcError{Code: code, Message: msg},
	})
	if err != nil {
		return
	}
	_ = c.writeLine(append(data, '\n'))
}

func (c *mcpClient) sendRequest(ctx context.Context, method string, params any) (json.RawMessage, error) {
	if c.closed.Load() {
		return nil, errors.New("MCP 客户端已关闭")
	}

	id := c.reqID.Add(1)
	idKey := strconv.FormatInt(id, 10)
	ch := make(chan jsonrpcResponse, 1)

	c.mu.Lock()
	c.pending[idKey] = ch
	c.mu.Unlock()

	req := jsonrpcRequest{
		JSONRPC: "2.0",
		ID:      id,
		Method:  method,
		Params:  params,
	}

	data, err := json.Marshal(req)
	if err != nil {
		c.mu.Lock()
		delete(c.pending, idKey)
		c.mu.Unlock()
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}
	data = append(data, '\n')

	if err := c.writeLine(data); err != nil {
		c.mu.Lock()
		delete(c.pending, idKey)
		c.mu.Unlock()
		return nil, fmt.Errorf("写入 stdin 失败: %w", err)
	}

	select {
	case resp, ok := <-ch:
		if !ok {
			return nil, errors.New("连接断开")
		}
		if resp.Error != nil {
			return nil, fmt.Errorf("RPC 错误 [%d]: %s", resp.Error.Code, resp.Error.Message)
		}
		return resp.Result, nil

	case <-ctx.Done():
		c.mu.Lock()
		delete(c.pending, idKey)
		c.mu.Unlock()
		return nil, ctx.Err()

	case <-c.done:
		return nil, errors.New("MCP 服务进程已退出")
	}
}

func (c *mcpClient) sendNotification(method string, params any) error {
	if c.closed.Load() {
		return errors.New("MCP 客户端已关闭")
	}
	req := jsonrpcRequest{
		JSONRPC: "2.0",
		Method:  method,
		Params:  params,
	}
	data, err := json.Marshal(req)
	if err != nil {
		return err
	}
	return c.writeLine(append(data, '\n'))
}

// writeLine：把一行 JSON 写进 stdin。
//
// ★ 写**不持** `c.mu`：`Close()` 要拿那把锁才能关 stdin，若一次阻塞写（服务端
//   不读 stdin、管道 64KB 写满）占着锁，退出路径就与它互等 —— 会话退不掉。
//   串行化改由 wmu 负责；stdin 字段本身仍由 mu 保护（取完就放）。
func (c *mcpClient) writeLine(data []byte) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	c.mu.Lock()
	w := c.stdin
	c.mu.Unlock()
	if w == nil {
		return errors.New("stdin 未就绪")
	}
	_, err := w.Write(data)
	return err
}

func (c *mcpClient) callTool(ctx context.Context, origToolName string, args map[string]any) (toolResult, error) {
	params := map[string]any{
		"name":      origToolName,
		"arguments": args,
	}
	raw, err := c.sendRequest(ctx, "tools/call", params)
	if err != nil {
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 工具调用错误: %v", err),
		}, nil
	}

	var callRes mcpCallToolResult
	if err := json.Unmarshal(raw, &callRes); err != nil {
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 响应反序列化失败: %v", err),
		}, nil
	}

	var buf strings.Builder
	for i, item := range callRes.Content {
		if i > 0 {
			buf.WriteString("\n")
		}
		switch item.Type {
		case "text":
			buf.WriteString(item.Text)
		default:
			if item.MimeType != "" {
				buf.WriteString(fmt.Sprintf("[%s content, %d bytes]", item.Type, len(item.Data)))
			} else {
				buf.WriteString(fmt.Sprintf("[%s content]", item.Type))
			}
		}
	}
	output := buf.String()
	if output == "" && callRes.IsError {
		output = "MCP 工具返回失败且无输出内容"
	}

	return toolResult{
		OK:     !callRes.IsError,
		Output: clipMCPOutput(output),
	}, nil
}

// clipMCPOutput：MCP 输出按 mcpOutLimit 截断并**显式标注**。
// ★ 本仓纪律（tools.go 文件头：「所有上限截断时显式标注——静默截断会让模型拿着
//   半份结果下结论，比报错更糟」）。读循环允许单行 4MB、content 又是直接 join，
//   这里不设上限等于把「大表 dump」直接灌进 history。
func clipMCPOutput(s string) string { return trimOutputTo(s, mcpOutLimit) }

func (c *mcpClient) Close() {
	if c.closed.Swap(true) {
		return
	}

	c.mu.Lock()
	if c.stdin != nil {
		_ = c.stdin.Close()
	}
	c.mu.Unlock()

	if c.cmd != nil && c.cmd.Process != nil {
		// 优雅退出给 300ms 宽限期，否则整树强杀
		done := make(chan struct{})
		go func() {
			_ = c.cmd.Wait()
			close(done)
		}()
		select {
		case <-done:
		case <-time.After(300 * time.Millisecond):
			if c.killTree != nil {
				c.killTree()
			}
			_ = c.cmd.Process.Kill()
		}
	}

	// 整树清退无条件再来一次：Windows 那边 `killTree` = 关 Job 句柄（KILL_ON_JOB_CLOSE），
	// 句柄不关就是每会话泄一个内核对象；POSIX 那边关组是幂等的（组空了返回 ESRCH）。
	if c.killTree != nil {
		c.killTree()
	}

	c.closeDone()
}

// ── MCP Hub（多服务聚合调度中心）─────────────────────────────────

type mcpToolRef struct {
	serverName string
	origName   string
	desc       string
}

type mcpHub struct {
	mu       sync.RWMutex
	clients  map[string]*mcpClient
	toolMap  map[string]mcpToolRef // mcp__<server>__<tool> -> ref
	toolDefs []toolDef
	rootDir  string
	// dead：server -> 死因。★ 只记「自上次被取走以来」的，会话取走后发 system 块。
	// 没有这一层，服务悄悄死掉的表现是「工具反复失败」，用户完全不知道发生了什么。
	dead map[string]string
}

// findMCPConfigFile：按三级优先级寻找配置文件：
// 1. 显式指定参数；
// 2. 当前工作区根目录下的 .ecdi/mcp.json 或 mcp.json；
// 3. 全局应用配置目录下的 mcp.json。
func findMCPConfigFile(explicitPath string, workspaceRoot string) string {
	if explicitPath != "" {
		if _, err := os.Stat(explicitPath); err == nil {
			return explicitPath
		}
		return ""
	}
	if workspaceRoot != "" {
		p1 := filepath.Join(workspaceRoot, ".ecdi", "mcp.json")
		if _, err := os.Stat(p1); err == nil {
			return p1
		}
		p2 := filepath.Join(workspaceRoot, "mcp.json")
		if _, err := os.Stat(p2); err == nil {
			return p2
		}
	}

	// 全局 APPDATA / .config
	if appData := os.Getenv("APPDATA"); appData != "" {
		p := filepath.Join(appData, "ecdi-fake-harness", "mcp.json")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	} else if home, err := os.UserHomeDir(); err == nil && home != "" {
		p := filepath.Join(home, ".config", "ecdi-fake-harness", "mcp.json")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return ""
}

func loadMCPConfig(path string) (*mcpConfigFile, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var cfg mcpConfigFile
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("解析 %s 失败: %w", path, err)
	}
	return &cfg, nil
}

// mcpToolToDef：将 MCP 工具定义转换为 OpenAI 兼容的 toolDef。
// ★ 严格规约：parameters["type"] 恒为 "object"，"required" 恒为 string 数组（不能为 nil）。
func mcpToolToDef(serverName string, t mcpTool) (string, toolDef) {
	return mcpToolDefNamed(mcpToolName(serverName, t.Name), serverName, t)
}

// mcpToolDefWithHash：撞名消解用——名字尾部强制带 8 位哈希。
func mcpToolDefWithHash(serverName string, t mcpTool) (string, toolDef) {
	return mcpToolDefNamed(mcpToolNameHashed(serverName, t.Name, true), serverName, t)
}

func mcpToolDefNamed(name, serverName string, t mcpTool) (string, toolDef) {
	desc := t.Description
	if desc == "" {
		desc = fmt.Sprintf("MCP 工具 %s（服务：%s）", t.Name, serverName)
	} else {
		desc = fmt.Sprintf("[MCP: %s] %s", serverName, desc)
	}

	schema := t.InputSchema
	if schema == nil {
		schema = map[string]any{
			"type":       "object",
			"properties": map[string]any{},
			"required":   []string{},
		}
	} else {
		cpy := make(map[string]any, len(schema))
		for k, v := range schema {
			cpy[k] = v
		}
		if _, ok := cpy["type"]; !ok {
			cpy["type"] = "object"
		}
		if _, ok := cpy["properties"]; !ok {
			cpy["properties"] = map[string]any{}
		}
		req, ok := cpy["required"]
		if !ok || req == nil {
			cpy["required"] = []string{}
		}
		schema = cpy
	}

	return name, toolDef{
		Name:        name,
		Description: desc,
		Parameters:  schema,
	}
}

// ── 工具命名（provider 约束 vs MCP 约束的落差）────────────────────

// sanitizeNamePart：压到 `[A-Za-z0-9_-]`。非 ASCII（中文工具名不是没可能）与
// 其它标点一律折成 `_`，逐 rune 处理（不会切出半个汉字）。
func sanitizeNamePart(s string) string {
	var b strings.Builder
	b.Grow(len(s))
	for _, r := range s {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9', r == '_', r == '-':
			b.WriteRune(r)
		default:
			b.WriteByte('_')
		}
	}
	return b.String()
}

// mcpToolName：MCP 原始名 → provider 能接受的 `function.name`（≤64，字符集受限）。
//
// ★ 为什么必须有：OpenAI 兼容端点（含 DeepSeek）对 `function.name` 的约束是
//   `^[a-zA-Z0-9_-]{1,64}$`，而 MCP 规范对工具名**没有任何约束**——`git.status`、
//   带空格、中文都合法。原样上交的后果不是「这个工具调不动」，而是**整轮请求 400**，
//   即一次配置就把整个会话打死。名字只是我们发给模型的外号，真实调用走
//   `mcpToolRef.origName`，所以压名字是**无损**的。
func mcpToolName(server, tool string) string { return mcpToolNameHashed(server, tool, false) }

func mcpToolNameHashed(server, tool string, forceHash bool) string {
	s, t := sanitizeNamePart(server), sanitizeNamePart(tool)
	if s == "" {
		s = "srv"
	}
	if t == "" {
		t = "tool"
	}
	plain := "mcp__" + s + "__" + t
	if !forceHash && len(plain) <= providerToolNameMax {
		return plain
	}

	// 超长 / 撞名：可读前缀 + 8 位哈希。同一对 (server, tool) 恒得同名
	// ⇒ 审批记录与日志可复现，不会每进程漂。
	h := fnv.New32a()
	_, _ = h.Write([]byte(server))
	_, _ = h.Write([]byte{0})
	_, _ = h.Write([]byte(tool))
	suffix := fmt.Sprintf("_%08x", h.Sum32())

	budget := providerToolNameMax - len("mcp__") - len("__") - len(suffix) // = 48
	if len(s) > 16 {
		s = s[:16]
	}
	if room := budget - len(s); len(t) > room {
		t = t[:room]
	}
	return "mcp__" + s + "__" + t + suffix
}

// newMCPHubFromConfig：从配置对象构造并启动 Hub。
//
// ★ 排序 + 并发（2026-11-05 评审后改）：① map 遍历顺序不定会让 toolDefs 顺序
// 每次进程都不同（伤前缀缓存，也让 A/B 快照不可复现）；② 串行握手发生在
// **hello 之前**的同步路径上，N 个服务最坏 N × 超时 —— GUI 首屏干等且无任何反馈。
func newMCPHubFromConfig(cfg *mcpConfigFile, rootDir string) (*mcpHub, error) {
	hub := &mcpHub{
		clients: make(map[string]*mcpClient),
		toolMap: make(map[string]mcpToolRef),
		rootDir: rootDir,
		dead:    make(map[string]string),
	}

	if cfg == nil || len(cfg.MCPServers) == 0 {
		return hub, nil
	}

	names := make([]string, 0, len(cfg.MCPServers))
	for n, c := range cfg.MCPServers {
		if c.Disabled {
			continue
		}
		names = append(names, n)
	}
	sort.Strings(names)

	type initOut struct {
		client *mcpClient
		err    error
	}
	outs := make([]initOut, len(names))
	var wg sync.WaitGroup
	for i, sName := range names {
		wg.Add(1)
		go func(i int, sName string) {
			defer wg.Done()
			client := newMCPClient(sName, cfg.MCPServers[sName])
			ctx, cancel := context.WithTimeout(context.Background(), mcpInitTimeoutS*time.Second)
			defer cancel()
			outs[i] = initOut{client: client, err: client.startAndInit(ctx, rootDir)}
		}(i, sName)
	}
	wg.Wait()

	// 注册按 names 的顺序做（并发只影响启动耗时，不影响工具表顺序）
	for i, sName := range names {
		if outs[i].err != nil {
			// 单个服务失败记录日志，不阻断宿主及其它服务
			fmt.Fprintf(os.Stderr, "[mcp] 启动服务 %s 失败: %v\n", sName, outs[i].err)
			continue
		}
		client := outs[i].client
		hub.clients[sName] = client
		for _, t := range client.tools {
			defName, def := mcpToolToDef(sName, t)
			if prev, dup := hub.toolMap[defName]; dup {
				// sanitize 后撞名（`git.status` 与 `git-status`）或服务端自己重复声明：
				// **不能**静默覆盖——toolDefs 里会留两份同名定义，provider 照样 400。
				fmt.Fprintf(os.Stderr, "[mcp] 工具名冲突 %s（%s/%s 与 %s/%s），后者改用哈希形\n",
					defName, sName, t.Name, prev.serverName, prev.origName)
				defName, def = mcpToolDefWithHash(sName, t)
				if _, still := hub.toolMap[defName]; still {
					fmt.Fprintf(os.Stderr, "[mcp] 冲突无法消解，跳过工具 %s/%s\n", sName, t.Name)
					continue
				}
			}
			hub.toolMap[defName] = mcpToolRef{
				serverName: sName,
				origName:   t.Name,
				desc:       t.Description,
			}
			hub.toolDefs = append(hub.toolDefs, def)
		}
	}

	return hub, nil
}

// NewMCPHub：自动探测路径并初始化。如果无配置返回空 Hub。
func NewMCPHub(explicitPath string, workspaceRoot string) (*mcpHub, error) {
	cfgPath := findMCPConfigFile(explicitPath, workspaceRoot)
	if cfgPath == "" {
		// ★ 显式给了路径却找不到 = 用户以为开了 MCP。静默吞掉是这条链上最容易
		//   让人怀疑「是不是根本没实现」的失败方式，必须说出来。
		if explicitPath != "" {
			fmt.Fprintf(os.Stderr, "[mcp] 警告: --mcp-config 指定的文件不存在：%s（本次不启用 MCP）\n", explicitPath)
		}
		return &mcpHub{
			clients: make(map[string]*mcpClient),
			toolMap: make(map[string]mcpToolRef),
			rootDir: workspaceRoot,
			dead:    make(map[string]string),
		}, nil
	}
	cfg, err := loadMCPConfig(cfgPath)
	if err != nil {
		return nil, err
	}
	hub, err := newMCPHubFromConfig(cfg, workspaceRoot)
	if err != nil {
		return nil, err
	}
	fmt.Fprintf(os.Stderr, "[mcp] 配置 %s：%d 个服务、%d 个工具\n", cfgPath, len(hub.clients), len(hub.toolDefs))
	return hub, nil
}

func (h *mcpHub) ToolDefs() []toolDef {
	if h == nil {
		return nil
	}
	h.mu.RLock()
	defer h.mu.RUnlock()
	res := make([]toolDef, len(h.toolDefs))
	copy(res, h.toolDefs)
	return res
}

func (h *mcpHub) HasTool(name string) bool {
	if h == nil {
		return false
	}
	h.mu.RLock()
	defer h.mu.RUnlock()
	_, ok := h.toolMap[name]
	return ok
}

func (h *mcpHub) CallTool(ctx context.Context, name string, args map[string]any) (toolResult, bool) {
	if h == nil {
		return toolResult{}, false
	}
	h.mu.RLock()
	ref, ok := h.toolMap[name]
	client := h.clients[ref.serverName]
	h.mu.RUnlock()

	if !ok {
		return toolResult{}, false
	}
	if client == nil {
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 服务 %s 不存在或已断开", ref.serverName),
		}, true
	}
	if client.isDead() {
		// 进程已退出但我们没有 respawn：明说，别让模型以为是自己参数写错了。
		h.noteDead(ref.serverName, "进程已退出")
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 服务 %s 已退出，工具 %s 不可用（本会话不会自动重启它）", ref.serverName, ref.origName),
		}, true
	}

	res, err := client.callTool(ctx, ref.origName, args)
	if err != nil {
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 工具执行错误: %v", err),
		}, true
	}
	if client.isDead() {
		h.noteDead(ref.serverName, "进程在调用期间退出")
	}
	return res, true
}

// noteDead：登记「某服务死了」。同名只记第一次（死因以首次为准）。
func (h *mcpHub) noteDead(server, reason string) {
	if h == nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.dead == nil {
		h.dead = make(map[string]string)
	}
	if _, ok := h.dead[server]; !ok {
		h.dead[server] = reason
	}
}

// TakeDeadNotices：取走「自上次取走以来新死掉的服务」的人向说明（取过即清）。
//
// ★ 为什么要有：服务崩掉之后 `close(done)`，所有后续调用都返回错误字符串，
//   但那是**模型**看得到的；界面上除了「工具反复失败」什么都没有。本仓对
//   「发生了什么但没人说」的既有处置是发一条 system 块（上下文压缩失败就是
//   这么做的），这里同一口径。会话在每次工具调用收尾时调用一次。
func (h *mcpHub) TakeDeadNotices() []string {
	if h == nil {
		return nil
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if len(h.dead) == 0 {
		return nil
	}
	out := make([]string, 0, len(h.dead))
	for name, why := range h.dead {
		out = append(out, fmt.Sprintf("[MCP 服务 %s 已退出：%s；它的工具自本轮起不可用]", name, why))
	}
	h.dead = make(map[string]string)
	sort.Strings(out)
	return out
}

func (h *mcpHub) Close() {
	if h == nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	for _, client := range h.clients {
		client.Close()
	}
	h.clients = make(map[string]*mcpClient)
	h.toolMap = make(map[string]mcpToolRef)
	h.toolDefs = nil
}

// ── stderr 行前缀 ───────────────────────────────────────────────

// prefixWriter：给 MCP server 的 stderr 每行加 `[mcp:<name>] ` 前缀。
// 多个服务共用 core 的一个 stderr，不带前缀时排障只能靠猜。
// ★ 逐 Write 加前缀（不攒行）：按行攒会丢掉「卡在半个行上的最后一句话」。
type prefixWriter struct {
	prefix string
	w      io.Writer
	mu     sync.Mutex
}

func (p *prefixWriter) Write(b []byte) (int, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	out := make([]byte, 0, len(b)+len(p.prefix))
	out = append(out, p.prefix...)
	for i, c := range b {
		out = append(out, c)
		if c == '\n' && i != len(b)-1 {
			out = append(out, p.prefix...)
		}
	}
	if _, err := p.w.Write(out); err != nil {
		return 0, err
	}
	return len(b), nil
}
