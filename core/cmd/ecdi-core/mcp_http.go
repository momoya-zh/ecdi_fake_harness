// mcp_http.go —— MCP 远程传输（2026-11-06）：
//   · Streamable HTTP（单端点 POST；响应 JSON 或 SSE）
//   · legacy HTTP+SSE（GET 取 endpoint；POST 消息；回执走 SSE）
//
// ★ 规范锚：Streamable HTTP 用 2025-06-18 的 initialize 语义（最后一个仍有
//   连接级握手的版本）；legacy 用 2024-11-05。positioning §5.3 的 2026-07-28
//   无状态模型**暂不采用**：存量 server 仍以 initialize 为主，切过去会主动放弃
//   兼容性。迁移点是握手序列，不是调用面——见 docs/mcp-host-plan.md §九。
//
// ★ 安全边界：MCP 不是安全边界（§二.7）。这里只做三件最低限度的事：
//   1) 默认 TLS 校验，不提供 --insecure；
//   2) 不跟随重定向（避免把 Authorization 带到另一个 host）；
//   3) headers 的值不进日志、不进 hello.mcp；HTTP 错误只带状态码 + 截断响应体。
package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const (
	// mcpHTTPProtocolVersion：Streamable HTTP 的 initialize 版本。
	// ★ 与 stdio 的 2024-11-05 不同是刻意的：Streamable HTTP 在 2025-03-26
	//   才引入，用旧版本号会让现代 server 直接 400；两个锚都写进 §九。
	mcpHTTPProtocolVersion = "2025-06-18"
	// mcpLegacySSEProtocolVersion：legacy HTTP+SSE 沿用 2024-11-05 的连接级语义。
	mcpLegacySSEProtocolVersion = "2024-11-05"

	// mcpHTTPEventMax：单个 SSE 事件上限（与 stdio 单行 4MB 同量级）。
	mcpHTTPEventMax = 4 << 20
	// mcpHTTPBodyMax：非流式 JSON 响应上限。工具输出随后还会按 24KB 截断，
	//   这里防的是「服务端一次发几百 MB」把 core 内存打爆。
	mcpHTTPBodyMax = 16 << 20
	// mcpHTTPErrorBodyMax：错误响应只读这么多，避免错误路径把 body 灌进日志。
	mcpHTTPErrorBodyMax = 512
)

var errStopSSE = errors.New("stop sse")

// newMCPHTTPClient：共享的 HTTP 客户端配置。
func newMCPHTTPClient() *http.Client {
	tr, _ := http.DefaultTransport.(*http.Transport)
	cloned := tr.Clone()
	return &http.Client{
		Transport: cloned,
		// ★ 不跟随重定向：3xx 由调用方当错误处理，Authorization 不会跨 host 泄漏。
		CheckRedirect: func(*http.Request, []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}
}

// applyCommonHeaders：配置 headers 先落，再由协议要求覆盖。
// ★ 覆盖顺序很重要：用户不能把 Accept/Content-Type 改成服务端看不懂的值。
func applyCommonHeaders(req *http.Request, cfg mcpServerConfig, isPost bool) {
	for k, v := range cfg.Headers {
		req.Header.Set(k, v)
	}
	if isPost {
		req.Header.Set("Content-Type", "application/json")
	}
	req.Header.Set("Accept", "application/json, text/event-stream")
	req.Header.Set("User-Agent", "ecdi-core/0.6.0")
}

func readLimitedBody(r io.Reader, limit int64) ([]byte, error) {
	data, err := io.ReadAll(io.LimitReader(r, limit+1))
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > limit {
		return nil, fmt.Errorf("响应超过 %d 字节上限", limit)
	}
	return data, nil
}

func httpStatusError(resp *http.Response) error {
	body, _ := io.ReadAll(io.LimitReader(resp.Body, mcpHTTPErrorBodyMax))
	msg := strings.TrimSpace(string(body))
	if msg == "" {
		return fmt.Errorf("HTTP %d %s", resp.StatusCode, http.StatusText(resp.StatusCode))
	}
	return fmt.Errorf("HTTP %d %s: %s", resp.StatusCode, http.StatusText(resp.StatusCode), msg)
}

// warnPlainHTTP：远程明文 HTTP 只提醒、不拦截——用户可能连内网/本机服务。
// ★ 不把 URL 的 query/userinfo 打出来（可能含 token）。
func warnPlainHTTP(name string, u *url.URL) {
	if u.Scheme != "http" {
		return
	}
	host := u.Hostname()
	if ip := net.ParseIP(host); ip != nil && (ip.IsLoopback() || ip.IsPrivate()) {
		return
	}
	if host == "localhost" || strings.HasSuffix(host, ".localhost") {
		return
	}
	mcpLogf(name, "远程 MCP 使用明文 http://%s（建议 https；本次按配置继续）", u.Host)
}

// ── Streamable HTTP ───────────────────────────────────────────────

type streamableHTTPTransport struct {
	name     string
	cfg      mcpServerConfig
	endpoint *url.URL
	client   *http.Client

	ctx    context.Context
	cancel context.CancelFunc
	done   chan struct{}
	once   sync.Once

	closed atomic.Bool
	mu     sync.Mutex
	// sessionID / protocolVersion：initialize 回执后才有；后续请求要回带。
	sessionID       string
	protocolVersion string
}

func newStreamableHTTPTransport(name string, cfg mcpServerConfig) (*streamableHTTPTransport, error) {
	u, err := url.Parse(strings.TrimSpace(cfg.URL))
	if err != nil {
		return nil, fmt.Errorf("url 解析失败: %w", err)
	}
	// transportKind 已校验；这里兜底直接构造的测试/未来调用方。
	if u.Scheme != "http" && u.Scheme != "https" || u.Host == "" {
		return nil, fmt.Errorf("非法 url: %q", cfg.URL)
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &streamableHTTPTransport{
		name:            name,
		cfg:             cfg,
		endpoint:        u,
		client:          newMCPHTTPClient(),
		ctx:             ctx,
		cancel:          cancel,
		done:            make(chan struct{}),
		protocolVersion: mcpHTTPProtocolVersion,
	}, nil
}

func (t *streamableHTTPTransport) ProtocolVersion() string {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.protocolVersion
}

func (t *streamableHTTPTransport) Done() <-chan struct{} { return t.done }
func (t *streamableHTTPTransport) closeDone()            { t.once.Do(func() { close(t.done) }) }

func (t *streamableHTTPTransport) Start(_ context.Context) error {
	warnPlainHTTP(t.name, t.endpoint)
	return nil
}

// Do：POST 一条 JSON-RPC 消息。
//   · 通知（ID==nil）期望 202（也接受 200），不等 body；
//   · 请求按 Content-Type 解析 JSON 或 SSE，直到拿到匹配 id 的回执。
func (t *streamableHTTPTransport) Do(ctx context.Context, req jsonrpcRequest) (json.RawMessage, error) {
	if t.closed.Load() {
		return nil, errors.New("MCP HTTP 传输已关闭")
	}
	data, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}

	rctx, cancel := context.WithCancel(ctx)
	stop := context.AfterFunc(t.ctx, cancel)
	defer stop()
	defer cancel()

	httpReq, err := http.NewRequestWithContext(rctx, http.MethodPost, t.endpoint.String(), bytes.NewReader(data))
	if err != nil {
		return nil, err
	}
	applyCommonHeaders(httpReq, t.cfg, true)
	t.mu.Lock()
	if t.sessionID != "" {
		httpReq.Header.Set("Mcp-Session-Id", t.sessionID)
	}
	// ★ initialize 不带版本头（此时还没协商）；其余请求带当前协商版本。
	if req.Method != "initialize" && t.protocolVersion != "" {
		httpReq.Header.Set("MCP-Protocol-Version", t.protocolVersion)
	}
	t.mu.Unlock()

	resp, err := t.client.Do(httpReq)
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, fmt.Errorf("HTTP 请求失败: %w", err)
	}
	defer resp.Body.Close()

	// 服务端可以在任意响应上给/换 session id。
	if sid := resp.Header.Get("Mcp-Session-Id"); sid != "" {
		t.mu.Lock()
		t.sessionID = sid
		t.mu.Unlock()
	}

	if req.ID == nil {
		// 规范要求 202；部分 server 用 200/204。任何 2xx 都视为「通知已收」。
		if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			return nil, nil
		}
		return nil, httpStatusError(resp)
	}
	if resp.StatusCode == http.StatusAccepted {
		return nil, errors.New("服务端对请求回了 202（应返回 JSON 或 SSE 回执）")
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, httpStatusError(resp)
	}

	ct := resp.Header.Get("Content-Type")
	switch {
	case strings.Contains(ct, "text/event-stream"):
		return t.readSSEResponse(rctx, resp.Body, fmt.Sprint(req.ID), req.Method)
	case strings.Contains(ct, "application/json") || ct == "":
		raw, err := readLimitedBody(resp.Body, mcpHTTPBodyMax)
		if err != nil {
			return nil, err
		}
		var msg jsonrpcResponse
		if err := json.Unmarshal(raw, &msg); err != nil {
			return nil, fmt.Errorf("解析 JSON 回执失败: %w", err)
		}
		return t.finishResponse(msg, fmt.Sprint(req.ID), req.Method)
	default:
		return nil, fmt.Errorf("不支持的响应 Content-Type %q", ct)
	}
}

func (t *streamableHTTPTransport) finishResponse(msg jsonrpcResponse, wantID, method string) (json.RawMessage, error) {
	if msg.ID == nil || fmt.Sprint(msg.ID) != wantID {
		return nil, fmt.Errorf("回执 id 不匹配（期望 %s，got %v）", wantID, msg.ID)
	}
	if msg.Error != nil {
		return nil, fmt.Errorf("RPC 错误 [%d]: %s", msg.Error.Code, msg.Error.Message)
	}
	if method == "initialize" {
		var initRes mcpInitializeResult
		if err := json.Unmarshal(msg.Result, &initRes); err == nil && initRes.ProtocolVersion != "" {
			t.mu.Lock()
			t.protocolVersion = initRes.ProtocolVersion
			t.mu.Unlock()
		}
	}
	return msg.Result, nil
}

// readSSEResponse：在 POST 返回的 SSE 流里找匹配 id 的 response。
// ★ 流里可能夹 notification（忽略）或服务端请求（我们声明空 capabilities，
//   正常不该有；记录并忽略，不阻塞——反向请求不在本期范围）。
func (t *streamableHTTPTransport) readSSEResponse(ctx context.Context, body io.Reader, wantID, method string) (json.RawMessage, error) {
	var result json.RawMessage
	var rpcErr error
	err := scanSSE(body, func(_ string, data string) error {
		var msg jsonrpcResponse
		if err := json.Unmarshal([]byte(data), &msg); err != nil {
			return nil // 非 JSON-RPC 事件（keepalive/日志）忽略
		}
		if msg.Method != "" {
			if msg.ID != nil {
				mcpLogf(t.name, "忽略服务端反向请求 %s（HTTP 传输未实现 sampling/roots）", msg.Method)
			}
			return nil
		}
		if msg.ID == nil || fmt.Sprint(msg.ID) != wantID {
			return nil
		}
		raw, ferr := t.finishResponse(msg, wantID, method)
		if ferr != nil {
			rpcErr = ferr
		} else {
			result = raw
		}
		return errStopSSE
	})
	if errors.Is(err, errStopSSE) {
		if rpcErr != nil {
			return nil, rpcErr
		}
		return result, nil
	}
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, fmt.Errorf("SSE 流读取失败: %w", err)
	}
	if rpcErr != nil {
		return nil, rpcErr
	}
	return nil, errors.New("SSE 流在回执前结束")
}

func (t *streamableHTTPTransport) Close() {
	if t.closed.Swap(true) {
		return
	}
	t.mu.Lock()
	sid := t.sessionID
	ver := t.protocolVersion
	t.mu.Unlock()

	t.cancel()

	// best-effort DELETE：会话终止是规范建议，不是必须；2s 上限避免退出被远端拖住。
	if sid != "" {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		req, err := http.NewRequestWithContext(ctx, http.MethodDelete, t.endpoint.String(), nil)
		if err == nil {
			applyCommonHeaders(req, t.cfg, false)
			req.Header.Set("Mcp-Session-Id", sid)
			if ver != "" {
				req.Header.Set("MCP-Protocol-Version", ver)
			}
			if resp, err := t.client.Do(req); err == nil {
				_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1024))
				_ = resp.Body.Close()
			}
		}
		cancel()
	}
	t.client.CloseIdleConnections()
	t.closeDone()
}

// ── legacy HTTP+SSE ───────────────────────────────────────────────

type sseHTTPTransport struct {
	name   string
	cfg    mcpServerConfig
	base   *url.URL
	client *http.Client

	ctx    context.Context
	cancel context.CancelFunc
	body   io.ReadCloser
	done   chan struct{}
	once   sync.Once

	closed  atomic.Bool
	mu      sync.Mutex
	endpoint *url.URL
	pending  map[string]chan jsonrpcResponse
}

func newSSEHTTPTransport(name string, cfg mcpServerConfig) (*sseHTTPTransport, error) {
	u, err := url.Parse(strings.TrimSpace(cfg.URL))
	if err != nil {
		return nil, fmt.Errorf("url 解析失败: %w", err)
	}
	if u.Scheme != "http" && u.Scheme != "https" || u.Host == "" {
		return nil, fmt.Errorf("非法 url: %q", cfg.URL)
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &sseHTTPTransport{
		name:    name,
		cfg:     cfg,
		base:    u,
		client:  newMCPHTTPClient(),
		ctx:     ctx,
		cancel:  cancel,
		done:    make(chan struct{}),
		pending: make(map[string]chan jsonrpcResponse),
	}, nil
}

func (t *sseHTTPTransport) ProtocolVersion() string { return mcpLegacySSEProtocolVersion }
func (t *sseHTTPTransport) Done() <-chan struct{}   { return t.done }
func (t *sseHTTPTransport) closeDone()              { t.once.Do(func() { close(t.done) }) }

// Start：GET SSE 流，等第一个 `endpoint` 事件（规范要求客户端先拿到 POST 地址）。
// ★ 流本身用 t.ctx（会话生命周期），不用握手 ctx——否则 5s 握手预算一过流就断。
func (t *sseHTTPTransport) Start(ctx context.Context) error {
	warnPlainHTTP(t.name, t.base)
	req, err := http.NewRequestWithContext(t.ctx, http.MethodGet, t.base.String(), nil)
	if err != nil {
		return err
	}
	applyCommonHeaders(req, t.cfg, false)
	req.Header.Set("Accept", "text/event-stream")
	resp, err := t.client.Do(req)
	if err != nil {
		return fmt.Errorf("打开 SSE 流失败: %w", err)
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		defer resp.Body.Close()
		return httpStatusError(resp)
	}
	if ct := resp.Header.Get("Content-Type"); !strings.Contains(ct, "text/event-stream") {
		_ = resp.Body.Close()
		return fmt.Errorf("SSE endpoint 返回 Content-Type %q（期望 text/event-stream）", ct)
	}
	t.body = resp.Body

	endpointCh := make(chan *url.URL, 1)
	errCh := make(chan error, 1)
	go t.readStream(resp.Body, endpointCh, errCh)

	select {
	case u := <-endpointCh:
		t.mu.Lock()
		t.endpoint = u
		t.mu.Unlock()
		return nil
	case err := <-errCh:
		_ = resp.Body.Close()
		return err
	case <-ctx.Done():
		_ = resp.Body.Close()
		return ctx.Err()
	case <-t.done:
		_ = resp.Body.Close()
		return errors.New("SSE 流在 endpoint 事件前结束")
	}
}

func (t *sseHTTPTransport) readStream(body io.Reader, endpointCh chan<- *url.URL, errCh chan<- error) {
	var gotEndpoint sync.Once
	err := scanSSE(body, func(event, data string) error {
		switch event {
		case "endpoint":
			u, err := t.base.Parse(strings.TrimSpace(data))
			if err != nil {
				return fmt.Errorf("endpoint 事件不是合法 URL: %w", err)
			}
			gotEndpoint.Do(func() { endpointCh <- u })
			return nil
		case "message", "":
			t.dispatch([]byte(data))
			return nil
		default:
			return nil
		}
	})
	if err != nil && !t.closed.Load() {
		select {
		case errCh <- fmt.Errorf("SSE 流读取失败: %w", err):
		default:
		}
	}
	t.failPending()
	t.closeDone()
	_ = body
}

func (t *sseHTTPTransport) dispatch(raw []byte) {
	var msg jsonrpcResponse
	if err := json.Unmarshal(raw, &msg); err != nil {
		return
	}
	if msg.Method != "" {
		// 服务端反向请求：与 stdio 同口径回 -32601（best-effort，不阻塞读流）。
		if msg.ID != nil {
			go t.replyError(msg.ID, -32601, "method not supported: "+msg.Method)
		}
		return
	}
	if msg.ID == nil {
		return
	}
	key := fmt.Sprint(msg.ID)
	t.mu.Lock()
	ch, ok := t.pending[key]
	if ok {
		delete(t.pending, key)
	}
	t.mu.Unlock()
	if ok && ch != nil {
		ch <- msg
	}
}

func (t *sseHTTPTransport) failPending() {
	t.mu.Lock()
	for key, ch := range t.pending {
		delete(t.pending, key)
		close(ch)
	}
	t.mu.Unlock()
}

func (t *sseHTTPTransport) replyError(id any, code int, msg string) {
	data, err := json.Marshal(map[string]any{
		"jsonrpc": "2.0",
		"id":      id,
		"error":   jsonrpcError{Code: code, Message: msg},
	})
	if err != nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	_ = t.post(ctx, data)
}

func (t *sseHTTPTransport) post(ctx context.Context, data []byte) error {
	t.mu.Lock()
	ep := t.endpoint
	t.mu.Unlock()
	if ep == nil {
		return errors.New("SSE endpoint 尚未就绪")
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, ep.String(), bytes.NewReader(data))
	if err != nil {
		return err
	}
	applyCommonHeaders(req, t.cfg, true)
	resp, err := t.client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 1024))
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return httpStatusError(resp)
	}
	return nil
}

func (t *sseHTTPTransport) Do(ctx context.Context, req jsonrpcRequest) (json.RawMessage, error) {
	if t.closed.Load() {
		return nil, errors.New("MCP SSE 传输已关闭")
	}
	data, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}
	if req.ID == nil {
		return nil, t.post(ctx, data)
	}

	key := fmt.Sprint(req.ID)
	ch := make(chan jsonrpcResponse, 1)
	t.mu.Lock()
	t.pending[key] = ch
	t.mu.Unlock()

	if err := t.post(ctx, data); err != nil {
		t.mu.Lock()
		delete(t.pending, key)
		t.mu.Unlock()
		return nil, err
	}

	select {
	case resp, ok := <-ch:
		if !ok {
			return nil, errors.New("SSE 连接断开")
		}
		if resp.Error != nil {
			return nil, fmt.Errorf("RPC 错误 [%d]: %s", resp.Error.Code, resp.Error.Message)
		}
		return resp.Result, nil
	case <-ctx.Done():
		t.mu.Lock()
		delete(t.pending, key)
		t.mu.Unlock()
		return nil, ctx.Err()
	case <-t.done:
		return nil, errors.New("SSE 连接已结束")
	}
}

func (t *sseHTTPTransport) Close() {
	if t.closed.Swap(true) {
		return
	}
	t.cancel()
	if t.body != nil {
		_ = t.body.Close()
	}
	t.failPending()
	t.client.CloseIdleConnections()
	t.closeDone()
}

// scanSSE：RFC 兼容的最小 SSE 解析器（event/data/注释/CRLF/多行 data）。
// ★ 不实现 Last-Event-ID/retry/reconnect：本期只做单次会话。
func scanSSE(r io.Reader, fn func(event, data string) error) error {
	sc := bufio.NewScanner(r)
	sc.Buffer(make([]byte, 64*1024), mcpHTTPEventMax)
	var event string
	var data bytes.Buffer
	dataLen := 0
	dispatch := func() error {
		if dataLen == 0 {
			event = ""
			return nil
		}
		name := event
		if name == "" {
			name = "message"
		}
		payload := data.String()
		event = ""
		data.Reset()
		dataLen = 0
		return fn(name, payload)
	}
	for sc.Scan() {
		line := sc.Text()
		if line == "" {
			if err := dispatch(); err != nil {
				return err
			}
			continue
		}
		if strings.HasPrefix(line, ":") {
			continue
		}
		field, value, found := strings.Cut(line, ":")
		if found {
			value = strings.TrimPrefix(value, " ")
		} else {
			value = ""
		}
		switch field {
		case "event":
			event = value
		case "data":
			if dataLen > 0 {
				data.WriteByte('\n')
				dataLen++
			}
			data.WriteString(value)
			dataLen += len(value)
			if dataLen > mcpHTTPEventMax {
				return fmt.Errorf("SSE 事件超过 %d 字节上限", mcpHTTPEventMax)
			}
		}
	}
	if err := sc.Err(); err != nil {
		return err
	}
	return dispatch()
}