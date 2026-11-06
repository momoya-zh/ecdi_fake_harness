// mcp_runtime.go —— MCP 运行期开关（2026-11-06，七版）：
//
//	· env minimal：stdio 子进程只拿白名单环境（默认 inherit，显式开启）；
//	· respawn：stdio 服务崩溃后**按需**重启（有次数上限，不做无界重启循环）。
//
// ★ 为什么是包级开关而不是 session 字段：这两项是**进程级**策略（与 sandbox 的
//
//	getSandboxConfig 同一处置），由 --mcp-env / --mcp-respawn 在启动时设定一次。
//	塞进 session 构造参数会让十几个测试调用点全部改写，收益只有形式上的显式。
//	读写走 atomic.Pointer：测试会临时改、hub 的启动 goroutine 会并发读。
package main

import (
	"fmt"
	"os"
	"strings"
	"sync/atomic"
	"time"
)

const (
	mcpEnvInherit = "inherit"
	mcpEnvMinimal = "minimal"

	// mcpRespawnMax：每个服务每会话的按需重启上限。到顶即放弃——避免
	// 「调用→崩→重启→再崩」把一轮对话拖成分钟级，也避免日志刷屏。
	mcpRespawnMax = 3

	// mcpCancelNotifyTimeout：notifications/cancelled 的投递预算。
	// 用户按停止是为了**立刻**脱身，取消路径不能等太久；发不出去就如实说发不出去。
	mcpCancelNotifyTimeout = 2 * time.Second
)

type mcpRuntimeOptions struct {
	EnvMode string // inherit | minimal
	Respawn bool   // stdio 服务崩溃后是否按需重启
}

var mcpRuntime = func() *atomic.Pointer[mcpRuntimeOptions] {
	p := &atomic.Pointer[mcpRuntimeOptions]{}
	p.Store(&mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: true})
	return p
}()

func setMCPOptions(o mcpRuntimeOptions) { mcpRuntime.Store(&o) }

func getMCPOptions() mcpRuntimeOptions {
	o := mcpRuntime.Load()
	if o == nil {
		return mcpRuntimeOptions{EnvMode: mcpEnvInherit, Respawn: true}
	}
	return *o
}

// normalizeMCPEnvMode：启动期校验，坏值**拒绝启动**而不是静默回落。
// 静默回落会让「我明明写了 minimal」与「其实一直在全量继承」无法区分。
func normalizeMCPEnvMode(v string) (string, error) {
	switch strings.ToLower(strings.TrimSpace(v)) {
	case "", mcpEnvInherit:
		return mcpEnvInherit, nil
	case mcpEnvMinimal:
		return mcpEnvMinimal, nil
	}
	return "", fmt.Errorf("--mcp-env 只支持 inherit 或 minimal，got %q", v)
}

// mcpMinimalEnvAllow：minimal 模式的**全部**放行名（小写比较，Windows 不区分
// 大小写；POSIX 上小写名本来就不该被放行——真名就是大写 PATH/HOME 等）。
//
// 口径（只放「起得来」必需的，不放「能证明你是谁」的）：
//   - Windows：SystemRoot/PATH/PATHEXT/COMSPEC/TEMP/APPDATA/LOCALAPPDATA/
//     USERPROFILE/ProgramFiles 系/PSModulePath（node/npm/uv 依赖这些）；
//   - POSIX：PATH/HOME/TMPDIR/XDG_*；
//   - 两边都留：LANG/LC_*/TZ（编码与时区）。
//
// ★ 刻意**不放行**：任何 *_KEY / *_TOKEN / 凭据、以及 http_proxy 等代理变量
//
//	（代理 URL 常嵌账号密码）。需要的话在 mcp.json 的 `env` 里逐项显式给——
//	minimal 的语义是「默认什么都不给」，不是「帮你猜哪些安全」。
var mcpMinimalEnvAllow = map[string]bool{
	// Windows
	"systemroot":             true,
	"systemdrive":            true,
	"windir":                 true,
	"comspec":                true,
	"pathext":                true,
	"temp":                   true,
	"tmp":                    true,
	"userprofile":            true,
	"homedrive":              true,
	"homepath":               true,
	"appdata":                true,
	"localappdata":           true,
	"programdata":            true,
	"programfiles":           true,
	"programfiles(x86)":      true,
	"programw6432":           true,
	"commonprogramfiles":     true,
	"psmodulepath":           true,
	"number_of_processors":   true,
	"processor_architecture": true,
	"os":                     true,
	// POSIX
	"home":            true,
	"tmpdir":          true,
	"user":            true,
	"username":        true,
	"logname":         true,
	"shell":           true,
	"xdg_config_home": true,
	"xdg_cache_home":  true,
	"xdg_data_home":   true,
	"xdg_runtime_dir": true,
	// 共通
	"path":     true,
	"lang":     true,
	"language": true,
	"lc_all":   true,
	"lc_ctype": true,
	"tz":       true,
}

// mcpChildEnv：MCP stdio 子进程的环境变量。
//
// inherit = 既有口径（全量继承 + 剔除 ECDI_*_KEY，见 tools.go childEnv）。
// minimal = 只留白名单。白名单之外的一切（含密钥、代理、厂商 SDK 凭据）不继承。
// 配置里的 `env` 由调用方在此之后追加——那是用户显式授权的增量，不归这里过滤。
func mcpChildEnv(minimal bool) []string {
	if !minimal {
		return childEnv()
	}
	env := os.Environ()
	out := make([]string, 0, len(mcpMinimalEnvAllow))
	for _, kv := range env {
		name := kv
		if eq := strings.Index(kv, "="); eq >= 0 {
			name = kv[:eq]
		}
		// Windows 的 "=C:=D:\dir" 形态：name 为空串，白名单里没有 ⇒ 自然丢弃。
		if mcpMinimalEnvAllow[strings.ToLower(name)] {
			out = append(out, kv)
		}
	}
	return out
}
