//go:build !windows

// sandbox_other.go —— 非 Windows 平台的 L1 沙盒（进程组级）。
//
// 机制：Setpgid 让子进程自成独立进程组；超时/取消时向 -pid 发 SIGKILL，
// 清掉整组子孙进程。★ 这里**没有**内存与进程数硬上限（那需要 cgroup/rlimit
// 或平台专用机制），所以 note 只写进程组这一条，不冒充 Job Object。
//
// 输出捕获与 Windows 侧同源：Stdout/Stderr 都接 bytes.Buffer，
// WaitDelay 兜住"孙进程仍持有管道"导致的 Wait 挂死。
package main

import (
	"bytes"
	"context"
	"fmt"
	"os/exec"
	"strings"
	"sync"
	"syscall"
)

// platformSandboxNote：第一行只写**本平台真的做了**的边界。
func (cfg SandboxConfig) platformSandboxNote() string {
	return "L1 沙盒（进程组）：整组 SIGKILL · 无内存/进程硬上限 · 未剥离特权"
}

func runCommandInSandbox(ctx context.Context, command string, root string, timeoutS int, cfg SandboxConfig) toolResult {
	if !cfg.Enabled {
		return runCommandUnsandboxed(ctx, command, root, timeoutS)
	}

	cmd := exec.Command("/bin/sh", "-c", command)
	cmd.Dir = root
	cmd.Env = childEnv()
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.WaitDelay = sandboxWaitDelay

	var outBuf bytes.Buffer
	cmd.Stdout = &outBuf
	cmd.Stderr = &outBuf

	if err := cmd.Start(); err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("启动命令失败：%v", err)}
	}

	done := make(chan struct{})
	var watcher sync.WaitGroup
	watcher.Add(1)
	go func() {
		defer watcher.Done()
		select {
		case <-done:
			return
		case <-ctx.Done():
			if cmd.Process != nil && cmd.Process.Pid > 0 {
				_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
			}
		}
	}()

	waitErr := cmd.Wait()
	text := trimOutput(outBuf.String())
	close(done)
	watcher.Wait()

	switch {
	case ctx.Err() == context.DeadlineExceeded:
		return toolResult{OK: false, Output: fmt.Sprintf("命令超时（%d 秒）已通过进程组强制终止：\n%s", timeoutS, text)}
	case ctx.Err() == context.Canceled:
		return toolResult{OK: false, Output: fmt.Sprintf("命令已取消，已通过进程组强制终止：\n%s", text)}
	case waitErr != nil:
		return toolResult{OK: false, Output: fmt.Sprintf("退出码非 0（%v）：\n%s", waitErr, text)}
	}
	if strings.TrimSpace(text) == "" {
		return toolResult{OK: true, Output: "（无输出，退出码 0）"}
	}
	return toolResult{OK: true, Output: text}
}

func runCommandUnsandboxed(ctx context.Context, command string, root string, timeoutS int) toolResult {
	cmd := exec.CommandContext(ctx, "/bin/sh", "-c", command)
	cmd.Dir = root
	cmd.Env = childEnv()
	out, err := cmd.CombinedOutput()
	text := trimOutput(string(out))
	switch {
	case ctx.Err() == context.DeadlineExceeded:
		return toolResult{OK: false, Output: fmt.Sprintf("命令超时（%d 秒）已请求终止：\n%s", timeoutS, text)}
	case ctx.Err() == context.Canceled:
		return toolResult{OK: false, Output: fmt.Sprintf("命令已取消：\n%s", text)}
	case err != nil:
		return toolResult{OK: false, Output: fmt.Sprintf("退出码非 0（%v）：\n%s", err, text)}
	}
	if strings.TrimSpace(text) == "" {
		return toolResult{OK: true, Output: "（无输出，退出码 0）"}
	}
	return toolResult{OK: true, Output: text}
}
