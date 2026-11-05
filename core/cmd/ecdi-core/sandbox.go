package main

import (
	"fmt"
	"sync"
	"time"
)

// SandboxConfig 存储 run_command 的 L1 沙盒配置。
type SandboxConfig struct {
	Enabled        bool // 是否启用 L1 沙盒（默认 true）
	MemoryLimitMB  int  // 单个命令作业内存限制（MB，默认 1024；≤0 = 不设上限）
	MaxProcesses   int  // 单个命令作业最大并发进程数（默认 32，防 fork 炸弹；≤0 = 用默认）
	DropPrivileges bool // 是否剥离管理与调试等高危特权（默认 true）
}

// defaultSandboxConfig 返回出厂配置。
func defaultSandboxConfig() SandboxConfig {
	return SandboxConfig{
		Enabled:        true,
		MemoryLimitMB:  1024,
		MaxProcesses:   32,
		DropPrivileges: true,
	}
}

// sandboxWaitDelay：超时/取消后，Wait 等待 I/O 收尾的上限。
const sandboxWaitDelay = 3 * time.Second

// effectiveMaxProcesses：作业里的 ActiveProcessLimit 必须 >0 才有意义，
// 配置为 0/负数时落到出厂值（与 sandbox_windows.go 的兜底保持同一数字）。
func (cfg SandboxConfig) effectiveMaxProcesses() int {
	if cfg.MaxProcesses > 0 {
		return cfg.MaxProcesses
	}
	return 32
}

var (
	sandboxMu           sync.RWMutex
	activeSandboxConfig = defaultSandboxConfig()
)

func setSandboxConfig(cfg SandboxConfig) {
	sandboxMu.Lock()
	defer sandboxMu.Unlock()
	activeSandboxConfig = cfg
}

func getSandboxConfig() SandboxConfig {
	sandboxMu.RLock()
	defer sandboxMu.RUnlock()
	return activeSandboxConfig
}

// sandboxNote 返回 ask 提案卡里展示给用户看的沙盒状态。
//
// ★ 必须**两行**：提案卡的提案行是定高 mono Label、`SetWordWrap(false)`、
// 行宽约 688px（gui/src/AppTranscript.cpp AppendProposalLines / AppHud.cpp
// wrapW）。1024MB 默认配置的完整说明约 1000px，单行会被右侧裁掉 1/3——
// 用户在批准前恰恰看不到"特权/密钥/超时"那半句。GUI 侧按 `\n` 逐行建行。
//
// 第一行 = 平台实际提供的边界（见各平台 platformSandboxNote），
// 第二行 = 所有平台都成立的"密钥剔除 + 超时"。
func (cfg SandboxConfig) sandboxNote(timeoutSeconds int) string {
	if !cfg.Enabled {
		return "【警告】L1 沙盒已禁用：命令以宿主完整权限直接执行\n" +
			envTimeoutNote(timeoutSeconds)
	}
	return cfg.platformSandboxNote() + "\n" + envTimeoutNote(timeoutSeconds)
}

func envTimeoutNote(timeoutSeconds int) string {
	return fmt.Sprintf("密钥环境变量（ECDI_CORE_KEY / ECDI_PROVIDER_*_KEY）已剔除；超时 %d 秒强制终止", timeoutSeconds)
}

// sandboxBackgroundNote：后台作业的 note（同样两行；第二行换作业预算）。
// ★ 两行是 GUI 侧定高 Label 的硬约束（不折行，单行 ~1000px 会被裁）。
func (cfg SandboxConfig) sandboxBackgroundNote() string {
	tail := fmt.Sprintf("密钥环境变量（ECDI_CORE_KEY / ECDI_PROVIDER_*_KEY）已剔除；后台作业不阻塞本轮，最长 %d 分钟",
		int(jobMaxAge.Minutes()))
	if !cfg.Enabled {
		return "【警告】L1 沙盒已禁用：命令以宿主完整权限直接执行\n" + tail
	}
	return cfg.platformSandboxNote() + "\n" + tail
}
