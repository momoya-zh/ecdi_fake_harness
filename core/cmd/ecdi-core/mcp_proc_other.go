//go:build !windows

// mcp_proc_other.go —— MCP server 子进程的「整树清退」（POSIX 侧）。
//
// 手段与 Windows 侧不同、目的相同：把子进程放进**独立进程组**，清退时杀
// `-pgid`（整组），这样 `npx` 拉起的 node 孙进程不会活下来变孤儿。
// ★ 不用 `Process.Kill()`：它只杀直系子进程。
package main

import (
	"fmt"
	"os/exec"
	"sync"
	"syscall"
)

func mcpStartContained(cmd *exec.Cmd) (func(), bool, error) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	if err := cmd.Start(); err != nil {
		return nil, false, fmt.Errorf("启动进程失败: %w", err)
	}
	pgid := cmd.Process.Pid

	var once sync.Once
	kill := func() {
		once.Do(func() {
			// 组已空时返回 ESRCH，幂等。
			_ = syscall.Kill(-pgid, syscall.SIGKILL)
		})
	}
	return kill, true, nil
}
