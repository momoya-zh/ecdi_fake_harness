// mcp_transport.go —— MCP 传输接口（2026-11-06）。
//
// ★ 为什么拆这一层：stdio 与 HTTP/SSE 的差别不只是「消息怎么发」，还有生命周期
//   （子进程整树清退 vs HTTP 会话 DELETE）、关联方式（stdin/stdout 单流 vs
//   每请求响应/SSE 事件流）、取消（杀进程 vs 取消请求）。把这些塞进 mcpClient
//   的 if 分支会让 stdio 的既有行为（已由 MCP 测试钉住）暴露在不必要的改动里。
//
// ★ 接口只做三件事：Start / Do（请求或通知）/ Close；`Done` 供 hub 判死，
//   `ProtocolVersion` 供 initialize 与版本协商。JSON-RPC 方法语义仍在 mcpClient。
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
)

type mcpTransport interface {
	Start(ctx context.Context) error
	// Do：ID 非 nil = 请求，等回执；ID 为 nil = 通知，不等回执。
	Do(ctx context.Context, req jsonrpcRequest) (json.RawMessage, error)
	Close()
	Done() <-chan struct{}
	ProtocolVersion() string
}

func newMCPTransport(name string, cfg mcpServerConfig, rootDir string) (mcpTransport, error) {
	kind, err := cfg.transportKind()
	if err != nil {
		return nil, err
	}
	switch kind {
	case "stdio":
		return newStdioTransport(name, cfg, rootDir), nil
	case "http":
		return newStreamableHTTPTransport(name, cfg)
	case "sse":
		return newSSEHTTPTransport(name, cfg)
	default:
		return nil, fmt.Errorf("transport %q 尚未实现", kind)
	}
}

// mcpLogf：给协议协商/降级这类「连接级事实」一个统一前缀。
// ★ 不带任何 header/token 内容——HTTP 的 headers 可能是密钥。
func mcpLogf(server, format string, args ...any) {
	fmt.Fprintf(os.Stderr, "[mcp:%s] %s\n", server, fmt.Sprintf(format, args...))
}
