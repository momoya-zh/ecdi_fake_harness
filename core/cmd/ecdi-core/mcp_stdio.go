// mcp_stdio.go —— MCP stdio 传输（2026-11-05；2026-11-06 从 mcpClient 拆出）。
//
// ★ 拆分动机：HTTP/SSE 接入后，`mcpClient` 只该保留 JSON-RPC 方法语义；
//   进程、stdin/stdout、整树清退是**传输**的事。语义没动：本文件的 readLoop/
//   pending/清退逻辑逐字来自拆分前的 mcpClient，判据仍是既有 MCP 测试。
package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sync"
	"sync/atomic"
	"time"
)

type stdioTransport struct {
	name    string
	cfg     mcpServerConfig
	rootDir string
	cmd     *exec.Cmd

	// mu 只保护 pending 与 stdin 字段。★ **不覆盖阻塞写**：Close 要拿这把锁才能
	// 关 stdin，若一次 64KB 以上的写正卡在「不读 stdin 的服务端」上并占着锁，
	// 退出路径就会与它互等。写由 wmu 单独串行化（见 writeLine）。
	mu      sync.Mutex
	wmu     sync.Mutex
	stdin   io.WriteCloser
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
}

func newStdioTransport(name string, cfg mcpServerConfig, rootDir string) *stdioTransport {
	return &stdioTransport{
		name:    name,
		cfg:     cfg,
		rootDir: rootDir,
		pending: make(map[string]chan jsonrpcResponse),
		done:    make(chan struct{}),
	}
}

func (t *stdioTransport) ProtocolVersion() string { return mcpProtocolVersion }
func (t *stdioTransport) Done() <-chan struct{}   { return t.done }
func (t *stdioTransport) closeDone()              { t.doneOnce.Do(func() { close(t.done) }) }

// Start：拉起子进程、建立 stdio 读写循环。握手不在这里（mcpClient 的语义层）。
func (t *stdioTransport) Start(_ context.Context) error {
	cmd := exec.Command(t.cfg.Command, t.cfg.Args...)
	if t.rootDir != "" {
		cmd.Dir = t.rootDir
	}

	// 环境变量：inherit = 继承但剔除密钥；minimal = 只留白名单（--mcp-env）。
	// 两种模式之后都追加配置里的私有 env（用户显式授权的增量）。
	env := mcpChildEnv(getMCPOptions().EnvMode == mcpEnvMinimal)
	for k, v := range t.cfg.Env {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	cmd.Env = env

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return fmt.Errorf("打开 stdin 失败: %w", err)
	}
	t.stdin = stdin

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = stdin.Close()
		return fmt.Errorf("打开 stdout 失败: %w", err)
	}

	// stderr 透传到 core 的 stderr，每行带服务名前缀（多服务时才知道是谁在叫）
	stderrW := &prefixWriter{prefix: "[mcp:" + t.name + "] ", w: os.Stderr}
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
		t.killTree = killTree
	case !started:
		_ = stdin.Close()
		return containErr
	default:
		fmt.Fprintf(stderrW, "整树纳管失败，降级为只杀直系子进程：%v\n", containErr)
	}
	t.cmd = cmd

	go t.readLoop(stdout)
	return nil
}

// Do：一条请求或通知。通知（ID==nil）只写一行；请求等对应的回执。
func (t *stdioTransport) Do(ctx context.Context, req jsonrpcRequest) (json.RawMessage, error) {
	if t.closed.Load() {
		return nil, errors.New("MCP stdio 传输已关闭")
	}
	data, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}
	data = append(data, '\n')

	if req.ID == nil {
		if err := t.writeLine(data); err != nil {
			return nil, fmt.Errorf("写入 stdin 失败: %w", err)
		}
		return nil, nil
	}

	idKey := fmt.Sprint(req.ID)
	ch := make(chan jsonrpcResponse, 1)
	t.mu.Lock()
	t.pending[idKey] = ch
	t.mu.Unlock()

	if err := t.writeLine(data); err != nil {
		t.mu.Lock()
		delete(t.pending, idKey)
		t.mu.Unlock()
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
		t.mu.Lock()
		delete(t.pending, idKey)
		t.mu.Unlock()
		return nil, ctx.Err()

	case <-t.done:
		return nil, errors.New("MCP 服务进程已退出")
	}
}

// Cancel：把 notifications/cancelled 写进 stdin（尽力而为）。
//
// ★ 写必须**有界**：writeLine 会等 wmu（可能排在一次卡死的阻塞写后面），
//   而取消路径恰恰是用户等不下去的时刻。这里用 goroutine + 超时兜住，
//   超时只是「没送出去」，不能反过来把取消本身挂住。
// ★ 不发就**不假称发了**：返回错误，由 mcpClient 决定给用户看哪一句。
func (t *stdioTransport) Cancel(requestID any, reason string) error {
	if t.closed.Load() {
		return errors.New("MCP stdio 传输已关闭")
	}
	data, err := json.Marshal(mcpCancelledRequest(requestID, reason))
	if err != nil {
		return fmt.Errorf("序列化取消通知失败: %w", err)
	}
	data = append(data, '\n')

	done := make(chan error, 1)
	go func() { done <- t.writeLine(data) }()
	select {
	case err := <-done:
		if err != nil {
			return fmt.Errorf("写入取消通知失败: %w", err)
		}
		return nil
	case <-time.After(mcpCancelNotifyTimeout):
		return errors.New("写入取消通知超时（服务端可能没在读 stdin）")
	case <-t.done:
		return errors.New("MCP 服务进程已退出")
	}
}

func (t *stdioTransport) readLoop(r io.Reader) {
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
				t.replyError(resp.ID, -32601, "method not supported: "+resp.Method)
			}
			continue
		}
		if resp.ID == nil {
			// 通知或空 ID 响应，跳过
			continue
		}

		idKey := fmt.Sprint(resp.ID)
		t.mu.Lock()
		ch, ok := t.pending[idKey]
		if ok {
			delete(t.pending, idKey)
		}
		t.mu.Unlock()

		if ok && ch != nil {
			ch <- resp
		}
	}

	// 管道断开：**先**关 done，**再**排空 pending。
	// ★ 顺序不能反：等者被释放后第一件事就是读 isDead()。若此刻 done 还没关，
	//   它看到的是普通「连接断开」而不是「服务已死」⇒ 死亡通告漏发，用户只看到
	//   工具莫名失败。真实现象：`tools/mcp-smoke` 的 F 臂偶发 82/83。
	t.closeDone()

	t.mu.Lock()
	for idKey, ch := range t.pending {
		delete(t.pending, idKey)
		close(ch)
	}
	t.mu.Unlock()
}

// replyError：给**服务端发来的请求**回一个 JSON-RPC error（我们只做 -32601）。
func (t *stdioTransport) replyError(id any, code int, msg string) {
	data, err := json.Marshal(map[string]any{
		"jsonrpc": "2.0",
		"id":      id,
		"error":   jsonrpcError{Code: code, Message: msg},
	})
	if err != nil {
		return
	}
	_ = t.writeLine(append(data, '\n'))
}

// writeLine：把一行 JSON 写进 stdin。
//
// ★ 写**不持** `t.mu`：`Close()` 要拿那把锁才能关 stdin，若一次阻塞写（服务端
//   不读 stdin、管道 64KB 写满）占着锁，退出路径就与它互等 —— 会话退不掉。
//   串行化改由 wmu 负责；stdin 字段本身仍由 mu 保护（取完就放）。
func (t *stdioTransport) writeLine(data []byte) error {
	t.wmu.Lock()
	defer t.wmu.Unlock()
	t.mu.Lock()
	w := t.stdin
	t.mu.Unlock()
	if w == nil {
		return errors.New("stdin 未就绪")
	}
	_, err := w.Write(data)
	return err
}

func (t *stdioTransport) Close() {
	if t.closed.Swap(true) {
		return
	}

	t.mu.Lock()
	if t.stdin != nil {
		_ = t.stdin.Close()
	}
	t.mu.Unlock()

	if t.cmd != nil && t.cmd.Process != nil {
		// 优雅退出给 300ms 宽限期，否则整树强杀
		done := make(chan struct{})
		go func() {
			_ = t.cmd.Wait()
			close(done)
		}()
		select {
		case <-done:
		case <-time.After(300 * time.Millisecond):
			if t.killTree != nil {
				t.killTree()
			}
			_ = t.cmd.Process.Kill()
		}
	}

	// 整树清退无条件再来一次：Windows 那边 `killTree` = 关 Job 句柄（KILL_ON_JOB_CLOSE），
	// 句柄不关就是每会话泄一个内核对象；POSIX 那边关组是幂等的（组空了返回 ESRCH）。
	if t.killTree != nil {
		t.killTree()
	}

	t.closeDone()
}