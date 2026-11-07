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
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"net/url"
	"os"
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

	// defaultMCPCallTimeout：单次工具调用的默认超时预算。
	// 0 或 <=0 表示无上限；可通过配置中单服务的 timeout 或全局 --mcp-timeout 覆盖。
	defaultMCPCallTimeout = 60 * time.Second
)

// ── MCP 配置文件结构 ─────────────────────────────────────────────

type mcpServerConfig struct {
	// stdio：拉起子进程。
	Command string            `json:"command,omitempty"`
	Args    []string          `json:"args,omitempty"`
	Env     map[string]string `json:"env,omitempty"`
	// 远程：Streamable HTTP（默认）/ legacy HTTP+SSE（transport:"sse"）。
	URL       string            `json:"url,omitempty"`
	Headers   map[string]string `json:"headers,omitempty"`
	Transport string            `json:"transport,omitempty"` // stdio | http | sse（省略按 command/url 推断）
	Disabled  bool              `json:"disabled,omitempty"`
	// Timeout：单次调用的超时秒数（支持 30, 120, 0.2 等；显式 0 表示不超时；省略则继承全局默认）。
	Timeout *float64 `json:"timeout,omitempty"`
}

// callTimeout：计算该服务单次工具调用的有效超时。
func (c mcpServerConfig) callTimeout(defaultTimeout time.Duration) time.Duration {
	if c.Timeout != nil {
		if *c.Timeout <= 0 {
			return 0
		}
		return time.Duration(*c.Timeout * float64(time.Second))
	}
	return defaultTimeout
}

// transportKind：归一化传输类型，并把「配置自相矛盾」挡在启动之前。
//
// ★ 为什么在这里校验而不是等到 newMCPClient：一个坏配置必须变成 Failed 里的
//   一条**可读原因**（GUI 的失败卡/system 块），而不是一个空服务或一次半启动。
// ★ `url` 与 `command` 互斥；`headers` 只属于远程，`env/args` 只属于 stdio。
//   两类字段混写通常是用户从别的客户端抄配置抄串了，静默取其一比报错更糟。
func (c mcpServerConfig) transportKind() (string, error) {
	command := strings.TrimSpace(c.Command)
	rawURL := strings.TrimSpace(c.URL)
	kind := strings.ToLower(strings.TrimSpace(c.Transport))

	if command != "" && rawURL != "" {
		return "", errors.New("command 与 url 只能二选一（stdio 与远程是两种传输）")
	}
	if command == "" && rawURL == "" {
		return "", errors.New("缺少 command（stdio）或 url（远程）")
	}
	if kind == "" {
		if rawURL != "" {
			kind = "http"
		} else {
			kind = "stdio"
		}
	}

	switch kind {
	case "stdio":
		if command == "" {
			return "", errors.New("transport=stdio 需要 command")
		}
		if rawURL != "" {
			return "", errors.New("transport=stdio 不能同时给 url")
		}
		if len(c.Headers) > 0 {
			return "", errors.New("headers 只用于远程传输（stdio 请用 env）")
		}
	case "http", "sse":
		if rawURL == "" {
			return "", fmt.Errorf("transport=%s 需要 url", kind)
		}
		if command != "" || len(c.Args) > 0 || len(c.Env) > 0 {
			return "", fmt.Errorf("transport=%s 不能同时给 command/args/env", kind)
		}
		u, err := url.Parse(rawURL)
		if err != nil {
			return "", fmt.Errorf("url 解析失败: %w", err)
		}
		if u.Scheme != "http" && u.Scheme != "https" {
			return "", fmt.Errorf("url 只支持 http/https，got %q", u.Scheme)
		}
		if u.Host == "" {
			return "", errors.New("url 缺少 host")
		}
	default:
		return "", fmt.Errorf("未知 transport %q（支持 stdio / http / sse）", c.Transport)
	}
	return kind, nil
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
	Method  string          `json:"method,omitempty"` // 非空 = 服务端发来的**请求或通知**（不是回执）
	// Params：服务端**通知/请求**的参数（notifications/tools/list_changed、
	// notifications/progress…）。回执没有这个字段；2026-11-07 起通知不再被丢弃。
	Params json.RawMessage `json:"params,omitempty"`
	Result json.RawMessage `json:"result,omitempty"`
	Error  *jsonrpcError   `json:"error,omitempty"`
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
	// transport：stdio / Streamable HTTP / legacy HTTP+SSE 的共同面。
	// ★ JSON-RPC 方法语义（initialize / tools/list / tools/call）留在 client，
	//   传输只负责「把一条消息送出去、把对应回执拿回来、关闭时清场」。
	transport mcpTransport
	reqID     atomic.Int64
	closed    atomic.Bool
	tools     []mcpTool
	// notifyFn：服务端主动通知的落点（hub 在构造后、startAndInit 前注入）。
	// nil = 通知照旧丢弃（与 2026-11-07 之前的行为一致）。
	notifyFn func(method string, params json.RawMessage)
}

func newMCPClient(name string, cfg mcpServerConfig) *mcpClient {
	return &mcpClient{name: name, cfg: cfg}
}

// isDead：传输层已关闭/断流（进程退出、SSE 流断、Close 走过）。
func (c *mcpClient) isDead() bool {
	if c == nil || c.transport == nil {
		return true
	}
	select {
	case <-c.transport.Done():
		return true
	default:
		return false
	}
}

// startAndInit：建立传输（stdio 子进程 / Streamable HTTP / legacy SSE），
// 完成 initialize 握手并列出工具。
//
// ★ 传输差异全部收在 mcpTransport 后面；这里只剩**协议语义**：握手、版本协商、
//   initialized 通知、tools/list 分页。2026-11-06 引入 HTTP/SSE 时特意没有在
//   本函数里堆 `if cfg.URL != ""`——那会把进程清退与 HTTP 会话搅在一起。
func (c *mcpClient) startAndInit(ctx context.Context, rootDir string) error {
	tr, err := newMCPTransport(c.name, c.cfg, rootDir)
	if err != nil {
		return err
	}
	c.transport = tr
	// ★ 服务端主动通知（stdio 行内 / SSE message / GET 通知流）不再丢弃：
	//   hub 在构造 client 时注入 notifyFn；nil = 保持旧行为（测试/未接 hub 时）。
	if c.notifyFn != nil {
		tr.SetNotifyHandler(c.notifyFn)
	}
	if err := tr.Start(ctx); err != nil {
		c.Close()
		return err
	}

	// 1. 发送 initialize
	clientVersion := tr.ProtocolVersion()
	initParams := mcpInitializeParams{
		ProtocolVersion: clientVersion,
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
	//   而我们按传输各自的连接级语义实现。静默继续会让「2026 版无状态语义」
	//   与「连接级会话」的差异永远无人知晓——本仓对 I4 那类「缺了什么」的处置是
	//   显式提示，这里同一口径。不中断：tools/list 与 tools/call 在各版里形状相同。
	if v := initRes.ProtocolVersion; v != "" && v != clientVersion {
		mcpLogf(c.name, "服务端协商到 protocolVersion=%s（本传输按 %s 语义实现，差异见 docs/mcp-host-plan.md §二.3）",
			v, clientVersion)
	}

	// 2. 发送 notifications/initialized
	// ★ 错误只能吞：通知没有回执，连接已断的话下一步 tools/list 会失败并报到那里。
	_ = c.sendNotification(ctx, "notifications/initialized", map[string]any{})

	// 3. 拉取 tools/list（分页追到底；失败关连接由本函数负责）。
	if err := c.listAllTools(ctx); err != nil {
		c.Close()
		return err
	}

	return nil
}

// listAllTools：tools/list 分页追到底，结果写入 c.tools。
// ★ 追到底的理由（原 startAndInit 内联注释，2026-11-07 抽出复用）：规范允许分页，
//   只取第一页等于静默丢工具（模型看不见就不可能调用，且没有任何提示）。
//   ★ 错误时**不关**连接——调用方决定（握手路径关，tools/list_changed 重拉路径
//   保留连接只报失败）。
func (c *mcpClient) listAllTools(ctx context.Context) error {
	var allTools []mcpTool
	cursor := ""
	for page := 0; page < 32; page++ {
		params := map[string]any{}
		if cursor != "" {
			params["cursor"] = cursor
		}
		listRaw, err := c.sendRequest(ctx, "tools/list", params)
		if err != nil {
			return fmt.Errorf("拉取 tools/list 失败: %w", err)
		}
		var listRes mcpToolsListResult
		if err := json.Unmarshal(listRaw, &listRes); err != nil {
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

// mcpCanceledError：调用因 ctx 结束而中止（用户取消 / 超时），并记录
// 「取消通知有没有真的送出去」。★ 这不是内部细节：UI 上写「已取消」而
// 服务端还在跑，是**撒谎**；送没送出去决定给用户看哪一句（mcp-host-plan §五）。
type mcpCanceledError struct {
	Reason   error // context.Canceled / context.DeadlineExceeded
	Notified bool  // notifications/cancelled 是否送出
}

func (e *mcpCanceledError) Error() string {
	return fmt.Sprintf("MCP 调用中止（%v，取消通知已送达=%v）", e.Reason, e.Notified)
}

func (e *mcpCanceledError) Unwrap() error { return e.Reason }

// toolText：写进 tool_result 的人话（模型与 GUI 看同一份，§4.4）。
func (e *mcpCanceledError) toolText() string {
	timedOut := errors.Is(e.Reason, context.DeadlineExceeded)
	switch {
	case timedOut && e.Notified:
		return "（调用超时：已向 MCP 服务发送取消通知；服务端可能仍在执行，迟到的结果会被丢弃）"
	case timedOut:
		return "（调用超时：取消通知未能送达 MCP 服务，该调用可能仍在服务端执行）"
	case e.Notified:
		return "（用户取消：已向 MCP 服务发送取消通知；服务端可能仍在执行，迟到的结果会被丢弃）"
	default:
		return "（用户取消：取消通知未能送达 MCP 服务，该调用可能仍在服务端执行）"
	}
}

func (c *mcpClient) sendRequest(ctx context.Context, method string, params any) (json.RawMessage, error) {
	if c.closed.Load() {
		return nil, errors.New("MCP 客户端已关闭")
	}
	id := c.reqID.Add(1)
	req := jsonrpcRequest{
		JSONRPC: "2.0",
		ID:      id,
		Method:  method,
		Params:  params,
	}
	raw, err := c.transport.Do(ctx, req)
	if err != nil {
		// ★ ctx 已结束 = 我们主动不等了。按 MCP §Cancellation 告诉服务端别做了，
		//   并把「通知是否送达」带回调用方——否则 UI 只能笼统写「已取消」。
		if ctxErr := ctx.Err(); ctxErr != nil {
			ce := &mcpCanceledError{Reason: ctxErr}
			reason := "用户取消"
			if errors.Is(ctxErr, context.DeadlineExceeded) {
				reason = "调用超时"
			}
			if cerr := c.transport.Cancel(id, reason); cerr != nil {
				mcpLogf(c.name, "取消通知未送达：%v", cerr)
			} else {
				ce.Notified = true
			}
			return nil, ce
		}
		return nil, err
	}
	return raw, nil
}

func (c *mcpClient) sendNotification(ctx context.Context, method string, params any) error {
	if c.closed.Load() {
		return errors.New("MCP 客户端已关闭")
	}
	req := jsonrpcRequest{
		JSONRPC: "2.0",
		Method:  method,
		Params:  params,
	}
	_, err := c.transport.Do(ctx, req)
	return err
}

func (c *mcpClient) callTool(ctx context.Context, origToolName string, args map[string]any) (toolResult, error) {
	params := map[string]any{
		"name":      origToolName,
		"arguments": args,
	}
	raw, err := c.sendRequest(ctx, "tools/call", params)
	if err != nil {
		// 取消/超时走诚实文本：GUI 的「已取消」只在取消通知确实送出时才是全话。
		var ce *mcpCanceledError
		if errors.As(err, &ce) {
			// ★ 超时且取消通知未送达（如 stdio 彻底死锁未读 stdin）：主动关闭连接清退僵死进程
			if errors.Is(ce.Reason, context.DeadlineExceeded) && !ce.Notified {
				mcpLogf(c.name, "调用超时且取消通知未送达（服务端可能已无响应），关闭连接")
				c.Close()
			}
			return toolResult{OK: false, Output: ce.toolText()}, nil
		}
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
	if c == nil || c.closed.Swap(true) {
		return
	}
	if c.transport != nil {
		c.transport.Close()
	}
}

// ── MCP Hub（多服务聚合调度中心）─────────────────────────────────

type mcpToolRef struct {
	serverName string
	origName   string
	desc       string
}

// ── 启动期状态（hello.mcp 的数据源，2026-11-06）─────────────────
//
// ★ 为什么要在 hub 上留这一层：MCP 配置与启动完全发生在 hello 之前，而产品面
// （GUI）此前只能看到 `caps` 里有没有 "mcp"——用户写了 mcp.json 却没有任何
// 确认，启动失败也只有 stderr。把「读了哪个文件、起了哪些服务、各自几个工具、
// 谁失败了、禁用了几个」做成快照随 hello 下发，GUI 才能显示而不靠猜。
//
// ★ 边界：这是**启动期快照**，不是运行期实时状态。服务之后崩掉/被重启仍走
// `noteDead` / `noteNotice` → `TakeNotices` 的 system 块路径（不在这里静默改写历史）。
type mcpServerStatus struct {
	Name      string `json:"name"`
	Tools     int    `json:"tools"`
	Transport string `json:"transport,omitempty"` // stdio / http / sse（GUI 用来区分远程）
}

type mcpFailureStatus struct {
	Name  string `json:"name"`
	Error string `json:"error"`
}

type mcpHubStatus struct {
	// Source：配置来自哪一级发现（explicit / workspace / global）。空 = 未发现配置。
	Source string `json:"source,omitempty"`
	// Config：实际读取的配置路径（显式路径不存在时也带上用户给的那个路径）。
	Config string `json:"config,omitempty"`
	// Services/Failed 恒为数组（不是 null）：GUI 不必区分"没有"和"字段缺失"。
	Services []mcpServerStatus  `json:"services"`
	Failed   []mcpFailureStatus `json:"failed"`
	// Disabled：配置里 `"disabled": true` 的服务数（不计入 Services/Failed）。
	Disabled int `json:"disabled"`
	// Problem：配置层面的错误（解析失败 / 显式路径不存在）。服务启动失败走 Failed。
	Problem string `json:"problem,omitempty"`
}

// hasEvidence：这份快照值不值得发给 GUI。
//
// ★ 无配置时一个字段都不发（否则每个没配 MCP 的会话都会多一个 "mcp" 字段，
//   而 GUI 会显示一张 0 服务的卡）；发现过/尝试过配置（哪怕全部失败、全部禁用）
//   就必须发——那正是用户需要确认的时刻。
func (st mcpHubStatus) hasEvidence() bool {
	return st.Source != "" || st.Config != "" || st.Problem != "" ||
		len(st.Services) > 0 || len(st.Failed) > 0 || st.Disabled > 0
}

// mcpNotice：运行期事件（重启成功/失败），会话取走后发 system 块。
// ★ 与 dead 分开：dead 是「已死且这次没人救回来」，notices 是「发生了什么」。
// OK 只影响块的 ok 字段——GUI 用同一套 system 行渲染，不加新 kind。
type mcpNotice struct {
	Text string
	OK   bool
}

const (
	// mcpRefreshToolsTimeout：notifications/tools/list_changed 触发的工具表重拉预算。
	// 比单次调用超时宽（分页追到底），比会话生命周期短。
	mcpRefreshToolsTimeout = 30 * time.Second
)

type mcpHub struct {
	mu       sync.RWMutex
	clients  map[string]*mcpClient
	toolMap  map[string]mcpToolRef // mcp__<server>__<tool> -> ref
	toolDefs []toolDef
	rootDir  string
	// serverCfg：启动配置快照。respawn 要用它重新拉起同一个服务（client.cfg 也行，
	// 但客户端会被换掉；配置属于 hub，不该随客户端生命周期消失）。
	serverCfg map[string]mcpServerConfig
	// dead：server -> 死因。★ 只记「自上次被取走以来」的，会话取走后发 system 块。
	// 没有这一层，服务悄悄死掉的表现是「工具反复失败」，用户完全不知道发生了什么。
	dead map[string]string
	// respawn：server -> 已尝试重启次数（成功与失败都计）。到 mcpRespawnMax 放弃。
	respawn map[string]int
	// notices：待发的运行期事件（见 mcpNotice）。
	notices []mcpNotice
	// respawnMu：串行化按需重启。★ 不能拿 mu 做进程 I/O——startAndInit 最长 5s，
	// 握着写锁会让 ToolDefs/Lookup 陪等，而它们正是下一轮请求的前置。
	respawnMu sync.Mutex
	// closed：会话退出后禁止重启（否则 bye 之后还能拉起新子进程）。
	closed atomic.Bool
	// defaultCallTimeout：未在配置中指定超时时的默认单次工具调用超时。
	defaultCallTimeout time.Duration
	// status：启动期快照（见上方类型注释）。构造期写入，之后只读。
	status mcpHubStatus
}

func newEmptyMCPHub(rootDir string) *mcpHub {
	return &mcpHub{
		clients:            make(map[string]*mcpClient),
		toolMap:            make(map[string]mcpToolRef),
		serverCfg:          make(map[string]mcpServerConfig),
		rootDir:            rootDir,
		dead:               make(map[string]string),
		respawn:            make(map[string]int),
		defaultCallTimeout: defaultMCPCallTimeout,
		status: mcpHubStatus{
			Services: []mcpServerStatus{},
			Failed:   []mcpFailureStatus{},
		},
	}
}

// SetCallTimeout：设置未单独配置超时时的默认工具调用超时。
func (h *mcpHub) SetCallTimeout(d time.Duration) {
	if h == nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	h.defaultCallTimeout = d
}

const (
	mcpConfigSourceExplicit  = "explicit"
	mcpConfigSourceWorkspace = "workspace"
	mcpConfigSourceGlobal    = "global"
)

// findMCPConfigFile：按三级优先级寻找配置文件：
// 1. 显式指定参数；
// 2. 当前工作区根目录下的 .ecdi/mcp.json 或 mcp.json；
// 3. 全局应用配置目录下的 mcp.json。
func findMCPConfigFile(explicitPath string, workspaceRoot string) string {
	p, _ := findMCPConfigFileWithSource(explicitPath, workspaceRoot)
	return p
}

// findMCPConfigFileWithSource：与 findMCPConfigFile 同一条发现链，额外返回
// **哪一级命中**。产品面要回答的正是"你读的是我那个文件吗"，只给路径不够
// （工作区 mcp.json 与全局同名文件长得一样）。
//
// ★ 显式路径不存在时返回 ("", "explicit")：调用方据此把"你以为开了 MCP"报出来，
// 而不是退回下一级去读一个用户没指定的文件（静默换文件比不启用更糟）。
//
// ★ 2026-11-07（mcp-host-plan §五「单测密闭性」闭合）：本函数只是薄包装——环境
// 目录在此读取，发现链本体下沉到 findMCPConfigFileIn（目录来源全部由参数给出，
// 不读进程环境）。此前被测代码直接读 APPDATA/UserHomeDir，测试只能靠改环境变量
// 间接控制，且第三级 UserHomeDir 分支在「APPDATA 恒有」的开发机上结构性测不到。
// isolateMCPDiscovery 的环境隔离保留，守的是产品包装层（纵深防御）。
func findMCPConfigFileWithSource(explicitPath string, workspaceRoot string) (path, source string) {
	return findMCPConfigFileIn(explicitPath, workspaceRoot, os.Getenv("APPDATA"), userHomeDirOrEmpty())
}

// userHomeDirOrEmpty：os.UserHomeDir 的无错版——出错一律当空串（与旧链的
// `err == nil && home != ""` 判据逐位一致）。
func userHomeDirOrEmpty() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return ""
	}
	return home
}

// findMCPConfigFileIn：发现链的纯核心，目录来源全部由参数给出、不读进程环境。
// appData 非空时优先用它（Windows 全局级），否则回落 home/.config（POSIX 惯例）。
func findMCPConfigFileIn(explicitPath, workspaceRoot, appData, home string) (path, source string) {
	if explicitPath != "" {
		if _, err := os.Stat(explicitPath); err == nil {
			return explicitPath, mcpConfigSourceExplicit
		}
		return "", mcpConfigSourceExplicit
	}
	if workspaceRoot != "" {
		p1 := filepath.Join(workspaceRoot, ".ecdi", "mcp.json")
		if _, err := os.Stat(p1); err == nil {
			return p1, mcpConfigSourceWorkspace
		}
		p2 := filepath.Join(workspaceRoot, "mcp.json")
		if _, err := os.Stat(p2); err == nil {
			return p2, mcpConfigSourceWorkspace
		}
	}

	// 全局 APPDATA / .config
	if appData != "" {
		p := filepath.Join(appData, "ecdi-fake-harness", "mcp.json")
		if _, err := os.Stat(p); err == nil {
			return p, mcpConfigSourceGlobal
		}
	} else if home != "" {
		p := filepath.Join(home, ".config", "ecdi-fake-harness", "mcp.json")
		if _, err := os.Stat(p); err == nil {
			return p, mcpConfigSourceGlobal
		}
	}
	return "", ""
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
		// ★ 先规约再补默认：`$schema` / `$defs` 不能原样交给端点（见
		//   sanitizeMCPInputSchema 的注释），本地 `$ref` 会在这里展开成实体。
		cpy, warns := sanitizeMCPInputSchema(schema)
		for _, w := range warns {
			fmt.Fprintf(os.Stderr, "[mcp] 工具 %s/%s 的 inputSchema: %s\n", serverName, t.Name, w)
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

// ── 入参 schema 规约（2026-11-05 三版）────────────────────────────

// sanitizeMCPInputSchema：MCP 服务给的 inputSchema → 上交给模型的 parameters。
//
// 为什么必须动它：MCP 服务常用 zod / pydantic 生成 schema，会带 `$schema`、
// `$defs` 与指向 `$defs` 的 `$ref`。部分 OpenAI 兼容端点不接受这些元关键字，
// 而失败的代价是**整轮请求 400**（一次配置打死整个会话），不是单个工具不可用。
//
// 做法（顺序不能换）：
//  1. 递归删 `$schema`；
//  2. 本地 `$ref`（`#/...` JSON Pointer）**就地展开**——把 `$defs`/`definitions`
//     里的定义填到引用处，同层的兄弟键（`description` 之类）保留并覆盖目标同名字段；
//  3. 展开完再删 `$defs` / `definitions`（draft-07 旧名）。
//
// 递归引用无法有限展开：那一处降级成 `{}`（允许任意值）并记 warning。宁可让
// 模型看到宽松的入参，也不留一个指向已删 `$defs` 的悬空 `$ref`——后者是确定的
// 400，前者只是校验变松（真正的参数校验仍在 MCP 服务端）。
//
// 返回的是**深拷贝**，不改 `t.InputSchema`；外链 `$ref`（`http(s)://...`）原样
// 保留（我们无从展开，也不该替服务端改语义）。`properties` 里名字恰好叫
// `$schema` / `$defs` 的**属性**不会被误删：剥键只发生在 schema 对象上，不发生在
// 「属性名 → schema」的映射层。
func sanitizeMCPInputSchema(schema map[string]any) (map[string]any, []string) {
	if schema == nil {
		return map[string]any{}, nil
	}
	root := cloneSchemaValue(schema).(map[string]any)
	s := &schemaSanitizer{root: root}
	out, _ := s.walkSchema(root, nil).(map[string]any)
	if out == nil {
		out = map[string]any{}
	}
	return out, s.warnings
}

type schemaSanitizer struct {
	root     map[string]any
	warnings []string
}

// warn：同一工具里同一个 ref 可能被展开多次（每个引用点都会撞一次递归），
// 去重后再交给 stderr，避免一条 schema 刷出一屏同样的告警。
func (s *schemaSanitizer) warn(format string, args ...any) {
	msg := fmt.Sprintf(format, args...)
	for _, w := range s.warnings {
		if w == msg {
			return
		}
	}
	s.warnings = append(s.warnings, msg)
}

// walkSchema：按 JSON Schema 的关键字表递归。★ 不能对任意 map 无脑剥键——
// `properties` 的键是**用户属性名**，那里出现 `$defs` 是合法的。
func (s *schemaSanitizer) walkSchema(v any, stack []string) any {
	switch x := v.(type) {
	case map[string]any:
		if ref, ok := x["$ref"].(string); ok && ref != "" {
			return s.expandRef(ref, x, stack)
		}
		return s.walkSchemaObject(x, stack)
	case []any:
		out := make([]any, len(x))
		for i, item := range x {
			out[i] = s.walkSchema(item, stack)
		}
		return out
	case []string:
		return append([]string(nil), x...)
	default:
		return v
	}
}

// walkSchemaObject：schema 对象的键级规约（不含 `$ref` 展开）。
func (s *schemaSanitizer) walkSchemaObject(obj map[string]any, stack []string) map[string]any {
	out := make(map[string]any, len(obj))
	for k, v := range obj {
		switch k {
		case "$schema", "$defs", "definitions":
			// 元关键字与定义容器：展开完引用后不再上交。
			continue
		case "$ref":
			// 由 expandRef 处理；外链路径会单独放回。
			continue
		case "properties", "patternProperties", "dependentSchemas":
			out[k] = s.walkSchemaMap(v, stack)
		case "dependencies":
			out[k] = s.walkDependencies(v, stack)
		case "allOf", "anyOf", "oneOf", "prefixItems":
			out[k] = s.walkSchemaArray(v, stack)
		case "items":
			out[k] = s.walkSchemaOrArray(v, stack)
		case "additionalProperties", "additionalItems", "contains", "propertyNames",
			"not", "if", "then", "else", "unevaluatedProperties", "unevaluatedItems",
			"contentSchema":
			out[k] = s.walkSchema(v, stack)
		default:
			// 未知关键字（含 vendor 扩展）：形状原样保留，只做深拷贝。
			out[k] = cloneSchemaValue(v)
		}
	}
	return out
}

func (s *schemaSanitizer) walkSchemaMap(v any, stack []string) any {
	m, ok := v.(map[string]any)
	if !ok {
		return cloneSchemaValue(v)
	}
	out := make(map[string]any, len(m))
	for name, sub := range m {
		out[name] = s.walkSchema(sub, stack) // name 是属性名，不是关键字
	}
	return out
}

func (s *schemaSanitizer) walkSchemaArray(v any, stack []string) any {
	arr, ok := v.([]any)
	if !ok {
		return cloneSchemaValue(v)
	}
	out := make([]any, len(arr))
	for i, sub := range arr {
		out[i] = s.walkSchema(sub, stack)
	}
	return out
}

// walkSchemaOrArray：`items` 在 draft-07 既可以是 schema，也可以是 schema 数组。
func (s *schemaSanitizer) walkSchemaOrArray(v any, stack []string) any {
	if _, ok := v.([]any); ok {
		return s.walkSchemaArray(v, stack)
	}
	return s.walkSchema(v, stack)
}

// walkDependencies：属性依赖的值是字符串数组（不是 schema），模式依赖的
// 值才是 schema —— 两种都必须保持原样语义。
func (s *schemaSanitizer) walkDependencies(v any, stack []string) any {
	m, ok := v.(map[string]any)
	if !ok {
		return cloneSchemaValue(v)
	}
	out := make(map[string]any, len(m))
	for name, dep := range m {
		if _, isArray := dep.([]any); isArray {
			out[name] = cloneSchemaValue(dep)
			continue
		}
		out[name] = s.walkSchema(dep, stack)
	}
	return out
}

// expandRef：展开一个 `$ref`。外链保留原样；本地引用按 JSON Pointer 解析。
func (s *schemaSanitizer) expandRef(ref string, obj map[string]any, stack []string) any {
	if !strings.HasPrefix(ref, "#") {
		// 外链：不猜远端内容，保留 `$ref`，只规约同层兄弟键。
		out := s.walkSchemaObject(obj, stack)
		out["$ref"] = ref
		return out
	}

	for _, seen := range stack {
		if seen == ref {
			s.warn("递归 $ref %s 无法有限展开，该处降级为 {}（允许任意值）", ref)
			return map[string]any{}
		}
	}

	target, ok := s.resolve(ref)
	if !ok {
		s.warn("$ref %s 无法解析，该处降级为 {}（允许任意值）", ref)
		return map[string]any{}
	}

	next := append(append([]string(nil), stack...), ref)
	expanded := s.walkSchema(cloneSchemaValue(target), next)
	out, isMap := expanded.(map[string]any)
	if !isMap {
		// 目标是布尔 schema：没有可合并的兄弟键，原样返回。
		return expanded
	}
	// 兄弟键覆盖目标同名字段（JSON Schema 2020-12 的 $ref 兄弟语义）。
	for k, v := range s.walkSchemaObject(obj, stack) {
		out[k] = v
	}
	return out
}

// resolve：解析本地 JSON Pointer（`#` = 根，`#/a/b` = 逐层下钻）。
func (s *schemaSanitizer) resolve(ref string) (any, bool) {
	if ref == "#" {
		return s.root, true
	}
	if !strings.HasPrefix(ref, "#/") {
		return nil, false
	}
	var cur any = s.root
	for _, raw := range strings.Split(ref[2:], "/") {
		token := strings.ReplaceAll(raw, "~1", "/")
		token = strings.ReplaceAll(token, "~0", "~")
		switch node := cur.(type) {
		case map[string]any:
			next, ok := node[token]
			if !ok {
				return nil, false
			}
			cur = next
		case []any:
			idx, err := strconv.Atoi(token)
			if err != nil || idx < 0 || idx >= len(node) {
				return nil, false
			}
			cur = node[idx]
		default:
			return nil, false
		}
	}
	return cur, true
}

// cloneSchemaValue：只克隆 JSON 会出现的容器类型；标量不可变，直接复用。
func cloneSchemaValue(v any) any {
	switch x := v.(type) {
	case map[string]any:
		out := make(map[string]any, len(x))
		for k, val := range x {
			out[k] = cloneSchemaValue(val)
		}
		return out
	case []any:
		out := make([]any, len(x))
		for i, val := range x {
			out[i] = cloneSchemaValue(val)
		}
		return out
	case []string:
		return append([]string(nil), x...)
	default:
		return v
	}
}

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
//
//	`^[a-zA-Z0-9_-]{1,64}$`，而 MCP 规范对工具名**没有任何约束**——`git.status`、
//	带空格、中文都合法。原样上交的后果不是「这个工具调不动」，而是**整轮请求 400**，
//	即一次配置就把整个会话打死。名字只是我们发给模型的外号，真实调用走
//	`mcpToolRef.origName`，所以压名字是**无损**的。
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

// registerClientLocked：把一个 client 的工具注册进 toolMap/toolDefs。
// 调用方持 mu 写锁。返回成功注册数（撞名无法消解的不计）。
//
// ★ 启动与 respawn 共用这一份：两处各写一遍撞名消解，迟早会分叉——
//   respawn 漏掉消解就会在 toolDefs 里留两份同名定义，provider 整轮 400。
// ★ 顺序：respawn 后该服务的定义追加到末尾（不插回原位）。只发生一次，
//   比「记住原下标再插入」简单可靠；代价是前缀缓存失效一次，可接受。
func (h *mcpHub) registerClientLocked(server string, client *mcpClient) int {
	registered := 0
	for _, t := range client.tools {
		defName, def := mcpToolToDef(server, t)
		if prev, dup := h.toolMap[defName]; dup {
			// sanitize 后撞名（`git.status` 与 `git-status`）或服务端自己重复声明：
			// **不能**静默覆盖——toolDefs 里会留两份同名定义，provider 照样 400。
			fmt.Fprintf(os.Stderr, "[mcp] 工具名冲突 %s（%s/%s 与 %s/%s），后者改用哈希形\n",
				defName, server, t.Name, prev.serverName, prev.origName)
			defName, def = mcpToolDefWithHash(server, t)
			if _, still := h.toolMap[defName]; still {
				fmt.Fprintf(os.Stderr, "[mcp] 冲突无法消解，跳过工具 %s/%s\n", server, t.Name)
				continue
			}
		}
		h.toolMap[defName] = mcpToolRef{
			serverName: server,
			origName:   t.Name,
			desc:       t.Description,
		}
		h.toolDefs = append(h.toolDefs, def)
		registered++
	}
	return registered
}

// unregisterServerLocked：摘掉某服务的全部工具（respawn 前调用）。调用方持写锁。
func (h *mcpHub) unregisterServerLocked(server string) {
	removed := make(map[string]bool)
	for name, ref := range h.toolMap {
		if ref.serverName == server {
			removed[name] = true
			delete(h.toolMap, name)
		}
	}
	if len(removed) == 0 {
		return
	}
	kept := h.toolDefs[:0]
	for _, def := range h.toolDefs {
		if !removed[def.Name] {
			kept = append(kept, def)
		}
	}
	h.toolDefs = kept
}

// newMCPHubFromConfig：从配置对象构造并启动 Hub。
//
// ★ 排序 + 并发（2026-11-05 评审后改）：① map 遍历顺序不定会让 toolDefs 顺序
// 每次进程都不同（伤前缀缓存，也让 A/B 快照不可复现）；② 串行握手发生在
// **hello 之前**的同步路径上，N 个服务最坏 N × 超时 —— GUI 首屏干等且无任何反馈。
func newMCPHubFromConfig(cfg *mcpConfigFile, rootDir string) (*mcpHub, error) {
	hub := newEmptyMCPHub(rootDir)

	if cfg == nil || len(cfg.MCPServers) == 0 {
		return hub, nil
	}

	names := make([]string, 0, len(cfg.MCPServers))
	disabled := 0
	for n, c := range cfg.MCPServers {
		if c.Disabled {
			disabled++
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
			// ★ 服务端主动通知的落点（tools/list_changed 重拉等）——构造后、
			//   startAndInit 前注入；respawn 路径同款。
			client.notifyFn = hub.onServerNotify(sName)
			ctx, cancel := context.WithTimeout(context.Background(), mcpInitTimeoutS*time.Second)
			defer cancel()
			outs[i] = initOut{client: client, err: client.startAndInit(ctx, rootDir)}
		}(i, sName)
	}
	wg.Wait()

	// 注册按 names 的顺序做（并发只影响启动耗时，不影响工具表顺序）。
	// ★ 状态快照同批填：失败服务**不能只是打一行 stderr**——GUI 看不到 stderr，
	//   用户会以为"配置了但没生效"是没实现。失败的 name/error 进 Failed 随 hello 下发。
	for i, sName := range names {
		if outs[i].err != nil {
			// 单个服务失败记录日志，不阻断宿主及其它服务
			fmt.Fprintf(os.Stderr, "[mcp] 启动服务 %s 失败: %v\n", sName, outs[i].err)
			hub.status.Failed = append(hub.status.Failed, mcpFailureStatus{
				Name:  sName,
				Error: outs[i].err.Error(),
			})
			continue
		}
		client := outs[i].client
		hub.clients[sName] = client
		hub.serverCfg[sName] = cfg.MCPServers[sName]
		hub.registerClientLocked(sName, client)
	}
	// ★ 服务行报的是**真正注册进工具表的**数量，不是服务端声明的数量：
	//   撞名无法消解时后者会虚高，而 GUI 的"N 个工具"必须与模型工具表一致。
	for _, sName := range names {
		if _, ok := hub.clients[sName]; !ok {
			continue
		}
		registered := 0
		// 按 toolMap 反查：注册循环里再维护一张表也能做，但这里顺序无关、更不易错。
		for _, ref := range hub.toolMap {
			if ref.serverName == sName {
				registered++
			}
		}
		kind, _ := cfg.MCPServers[sName].transportKind()
		hub.status.Services = append(hub.status.Services, mcpServerStatus{
			Name:      sName,
			Tools:     registered,
			Transport: kind,
		})
	}
	hub.status.Disabled = disabled

	return hub, nil
}

// NewMCPHub：自动探测路径并初始化。如果无配置返回空 Hub。
//
// ★ 2026-11-06：配置层错误不再只返回 nil——返回的 hub 仍带 status.Problem，
// 会话把它放进 hello.mcp 并把失败发成 system 块。否则"文件写错了"在界面上
// 与"根本没配"完全一样（都只有一行 stderr），正是产品面可见性要补的洞。
func NewMCPHub(explicitPath string, workspaceRoot string, defaultCallTimeout time.Duration) (*mcpHub, error) {
	cfgPath, source := findMCPConfigFileWithSource(explicitPath, workspaceRoot)
	if cfgPath == "" {
		// ★ 显式给了路径却找不到 = 用户以为开了 MCP。静默吞掉是这条链上最容易
		//   让人怀疑「是不是根本没实现」的失败方式，必须说出来。
		hub := newEmptyMCPHub(workspaceRoot)
		hub.defaultCallTimeout = defaultCallTimeout
		if explicitPath != "" {
			problem := fmt.Sprintf("--mcp-config 指定的文件不存在：%s", explicitPath)
			fmt.Fprintf(os.Stderr, "[mcp] 警告: %s（本次不启用 MCP）\n", problem)
			hub.status.Source = source
			hub.status.Config = explicitPath
			hub.status.Problem = problem
		}
		return hub, nil
	}
	cfg, err := loadMCPConfig(cfgPath)
	if err != nil {
		hub := newEmptyMCPHub(workspaceRoot)
		hub.defaultCallTimeout = defaultCallTimeout
		hub.status.Source = source
		hub.status.Config = cfgPath
		hub.status.Problem = err.Error()
		return hub, err
	}
	hub, err := newMCPHubFromConfig(cfg, workspaceRoot)
	if err != nil {
		return hub, err
	}
	hub.defaultCallTimeout = defaultCallTimeout
	hub.status.Source = source
	hub.status.Config = cfgPath
	fmt.Fprintf(os.Stderr, "[mcp] 配置 %s：%d 个服务、%d 个工具\n", cfgPath, len(hub.clients), len(hub.toolDefs))
	return hub, nil
}

// Status：启动期状态快照的副本。
//
// ★ 返回副本而不是指针：GUI/hello 组装是读方，不能再看见后续写入；同时把
//   Services/Failed 复制成新切片，避免调用方 append 时改到 hub 内部容量。
func (h *mcpHub) Status() mcpHubStatus {
	if h == nil {
		return mcpHubStatus{Services: []mcpServerStatus{}, Failed: []mcpFailureStatus{}}
	}
	h.mu.RLock()
	defer h.mu.RUnlock()
	st := h.status
	st.Services = append([]mcpServerStatus{}, h.status.Services...)
	st.Failed = append([]mcpFailureStatus{}, h.status.Failed...)
	return st
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

// Lookup：压缩名 → （配置里的服务名, 原始工具名）。ask 门在组装问句与提案卡
// （diff.go mcpAskDetail）时用它把 `mcp__smoke__git_status` 还原成
// `git.status`——压缩名是给模型的，给人看的必须是配置里的真名。
func (h *mcpHub) Lookup(name string) (serverName, origName string, ok bool) {
	if h == nil {
		return "", "", false
	}
	h.mu.RLock()
	defer h.mu.RUnlock()
	ref, ok := h.toolMap[name]
	return ref.serverName, ref.origName, ok
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
		// ★ 只在**调用之前**发现已死才重启；绝不在调用失败后自动重试——
		//   工具可能有副作用，重试等于替用户再执行一次没授权过的动作。
		if revived, why := h.respawnServer(ctx, ref.serverName); revived != nil {
			client = revived
		} else {
			return toolResult{
				OK:     false,
				Output: fmt.Sprintf("MCP 服务 %s 已退出，工具 %s 不可用（%s）", ref.serverName, ref.origName, why),
			}, true
		}
	}

	// ★ 单次调用超时控制：优先服务级配置，否则继承全局默认；<=0 表示无超时上限。
	h.mu.RLock()
	defaultTimeout := h.defaultCallTimeout
	h.mu.RUnlock()
	timeout := client.cfg.callTimeout(defaultTimeout)

	callCtx := ctx
	var cancel context.CancelFunc
	if timeout > 0 {
		callCtx, cancel = context.WithTimeout(ctx, timeout)
		defer cancel()
	}

	res, err := client.callTool(callCtx, ref.origName, args)
	if err != nil {
		return toolResult{
			OK:     false,
			Output: fmt.Sprintf("MCP 工具执行错误: %v", err),
		}, true
	}
	if client.isDead() {
		// 调用期间崩的（或因死锁超时被主动清退的）：本轮结果已丢，不重试；登记一次，下次调用再重启。
		h.noteDead(ref.serverName, "服务无响应或进程已退出")
	}
	return res, true
}

// respawnServer：按需重启一个已死的 stdio 服务。返回可用客户端；失败返回 nil + 人话原因。
//
// 设计取舍：
//   - **只按需**（工具调用发现已死才重启）：启动期失败的服务不进工具表，
//     永远不会走到这里；也不存在「后台无限循环重启」。
//   - **有上限**（mcpRespawnMax）：崩→起→崩的循环最多 3 次，然后明说放弃。
//   - **只 stdio**：HTTP 没有进程可拉起；远端断了就等下次调用按普通 HTTP 错误处理
//     （与既有约定一致：一次瞬时 5xx 不能把服务永久标死）。
//   - 不持 h.mu 做进程 I/O（见 respawnMu 注释）；换 client 与换工具表在同一把写锁里完成。
func (h *mcpHub) respawnServer(ctx context.Context, server string) (*mcpClient, string) {
	if h == nil {
		return nil, "hub 不存在"
	}
	opts := getMCPOptions()

	h.mu.RLock()
	cfg, hasCfg := h.serverCfg[server]
	attempts := h.respawn[server]
	closed := h.closed.Load()
	h.mu.RUnlock()

	if !hasCfg {
		return nil, "没有该服务的配置，无法重启"
	}
	if kind, err := cfg.transportKind(); err != nil || kind != "stdio" {
		// 远程传输没有进程可拉起；断流就等下次调用按普通 HTTP 错误处理。
		h.noteDead(server, "连接已断开")
		return nil, "远程传输不支持自动重启"
	}
	if !opts.Respawn {
		h.noteDead(server, "进程已退出")
		return nil, "本会话不会自动重启它"
	}
	if closed {
		return nil, "会话正在退出"
	}
	if attempts >= mcpRespawnMax {
		h.noteDead(server, "自动重启已达上限")
		return nil, fmt.Sprintf("本会话已重启 %d 次，达到上限", attempts)
	}

	// 串行化：并发调用（目前只有串行工具循环，但不把正确性押在调用方）只重启一次。
	h.respawnMu.Lock()
	defer h.respawnMu.Unlock()

	// 双检：等锁期间别的调用可能已经重启成功。
	h.mu.RLock()
	cur := h.clients[server]
	attempts = h.respawn[server]
	h.mu.RUnlock()
	if cur != nil && !cur.isDead() {
		return cur, ""
	}
	if attempts >= mcpRespawnMax {
		h.noteDead(server, "自动重启已达上限")
		return nil, fmt.Sprintf("本会话已重启 %d 次，达到上限", attempts)
	}

	// 用调用方的 ctx 派生：用户取消时重启立刻中止（startAndInit 失败路径会清进程），
	// 而不是让「按了停止」再等 5s 握手。
	initCtx, cancel := context.WithTimeout(ctx, mcpInitTimeoutS*time.Second)
	defer cancel()
	client := newMCPClient(server, cfg)
	client.notifyFn = h.onServerNotify(server)   // 与初启路径同一落点
	if err := client.startAndInit(initCtx, h.rootDir); err != nil {
		client.Close()
		h.mu.Lock()
		h.respawn[server] = attempts + 1
		h.mu.Unlock()
		if ctx.Err() != nil {
			return nil, "用户取消，本次不重启"
		}
		newCount := attempts + 1
		text := fmt.Sprintf("[MCP 服务 %s 自动重启失败（第 %d/%d 次）：%v", server, newCount, mcpRespawnMax, err)
		if newCount >= mcpRespawnMax {
			text += "；本会话不再尝试重启]"
			h.noteDead(server, "自动重启失败且已达上限")
		} else {
			text += "；下次调用会再试]"
		}
		h.noteNotice(text, false)
		return nil, fmt.Sprintf("自动重启失败（第 %d/%d 次）：%v", newCount, mcpRespawnMax, err)
	}

	h.mu.Lock()
	if h.closed.Load() {
		h.mu.Unlock()
		client.Close()
		return nil, "会话正在退出"
	}
	h.clients[server] = client
	h.unregisterServerLocked(server)
	count := h.registerClientLocked(server, client)
	h.respawn[server] = attempts + 1
	delete(h.dead, server)
	h.mu.Unlock()

	// 旧客户端已经死了，但句柄（Job Object 等）要关：不关就是每会话泄一个内核对象。
	if cur != nil {
		cur.Close()
	}
	h.noteNotice(fmt.Sprintf("[MCP 服务 %s 已自动重启（第 %d/%d 次）：%d 个工具已重新注册]",
		server, attempts+1, mcpRespawnMax, count), true)
	return client, ""
}

// onServerNotify：某服务主动通知的落点（三种传输共用；hub 构造 client 时注入）。
//   notifications/tools/list_changed ⇒ 重拉该服务工具表（异步、串行于 respawn）；
//   其余 ⇒ 记 stderr（通知无回执，这里一切 best-effort；params 不进日志——
//   notifications/message 可能带任意内容，不信任也不透传）。
func (h *mcpHub) onServerNotify(server string) func(string, json.RawMessage) {
	return func(method string, params json.RawMessage) {
		if h == nil {
			return
		}
		if method == "notifications/tools/list_changed" {
			go h.refreshServerTools(server)
			return
		}
		mcpLogf(server, "服务端通知 %s（已记录，无动作）", method)
	}
}

// refreshServerTools：notifications/tools/list_changed 的兑现——重拉该服务的
// tools/list 并重注册。复用 respawn 的两把锁：respawnMu 串行化（不与重启赛跑）、
// mu 写锁只包注册表换血。失败保留旧工具表并如实记日志——「通知到了但没兑现」
// 必须可见，静默失败比不改更糟。★ hello.mcp 是启动期快照（§八边界），本刷新
// 只影响模型侧工具表，副标/检视栏卡不跟手——那是已登记的独立边界。
func (h *mcpHub) refreshServerTools(server string) {
	if h == nil || h.closed.Load() {
		return
	}
	h.respawnMu.Lock()
	defer h.respawnMu.Unlock()

	h.mu.RLock()
	client := h.clients[server]
	h.mu.RUnlock()
	if client == nil || client.isDead() {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), mcpRefreshToolsTimeout)
	defer cancel()
	if err := client.listAllTools(ctx); err != nil {
		mcpLogf(server, "notifications/tools/list_changed 重拉失败（保留旧工具表）：%v", err)
		return
	}

	h.mu.Lock()
	if h.closed.Load() {
		h.mu.Unlock()
		return
	}
	h.unregisterServerLocked(server)
	count := h.registerClientLocked(server, client)
	h.mu.Unlock()
	mcpLogf(server, "notifications/tools/list_changed → 工具表已更新（%d 个工具）", count)
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

// noteNotice：登记一条运行期事件（重启成功/失败），等会话取走发 system 块。
func (h *mcpHub) noteNotice(text string, ok bool) {
	if h == nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	h.notices = append(h.notices, mcpNotice{Text: text, OK: ok})
}

// TakeNotices：取走「自上次取走以来」的运行期事件（取过即清）。
//
// ★ 为什么要有：服务崩掉之后 `close(done)`，所有后续调用都返回错误字符串，
//
//	但那是**模型**看得到的；界面上除了「工具反复失败」什么都没有。本仓对
//	「发生了什么但没人说」的既有处置是发一条 system 块（上下文压缩失败就是
//	这么做的），这里同一口径。会话在每次工具调用收尾时调用一次。
//
// 排序：事件按发生顺序在前；死亡通告按服务名排序在后（一次死亡只说一次）。
func (h *mcpHub) TakeNotices() []mcpNotice {
	if h == nil {
		return nil
	}
	opts := getMCPOptions()
	h.mu.Lock()
	defer h.mu.Unlock()
	if len(h.notices) == 0 && len(h.dead) == 0 {
		return nil
	}
	out := append([]mcpNotice{}, h.notices...)
	h.notices = nil
	names := make([]string, 0, len(h.dead))
	for name := range h.dead {
		names = append(names, name)
	}
	sort.Strings(names)
	for _, name := range names {
		why := h.dead[name]
		label := "已退出"
		if kind, err := h.serverCfg[name].transportKind(); err == nil && kind != "stdio" {
			label = "不可用" // 远程传输没有进程，说「退出」是假的
		}
		text := fmt.Sprintf("[MCP 服务 %s %s：%s", name, label, why)
		switch {
		case !opts.Respawn:
			text += "；本会话不会自动重启它"
		case h.respawn[name] >= mcpRespawnMax:
			text += fmt.Sprintf("；自动重启已达上限（%d 次），本会话不再尝试", mcpRespawnMax)
		default:
			text += "；下次调用将尝试自动重启"
		}
		out = append(out, mcpNotice{Text: text + "]"})
	}
	h.dead = make(map[string]string)
	return out
}

func (h *mcpHub) Close() {
	if h == nil {
		return
	}
	h.closed.Store(true)
	h.mu.Lock()
	defer h.mu.Unlock()
	for _, client := range h.clients {
		client.Close()
	}
	h.clients = make(map[string]*mcpClient)
	h.toolMap = make(map[string]mcpToolRef)
	h.toolDefs = nil
	h.notices = nil
	h.dead = make(map[string]string)
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