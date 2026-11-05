//go:build windows

// mcp_proc_windows.go —— MCP server 子进程的「整树纳管」（仅 Windows）。
//
// 为什么不直接复用 sandbox_windows.go 的 createAndConfigureJob：那套是给
// `run_command` 的 **L1 沙盒**（内存 ≤1GB / 活跃进程 ≤32 / 受限令牌 / 截断剪切板），
// 而 MCP server 常常就是个 npx/uvx 拉起的构建或数据库工具 —— 32 个进程、1GB、
// 禁剪切板会直接把人家按死。这里只要一样东西：**整树清退**
// （JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE ⇒ 关句柄即连同孙进程一起收）。
//
// ★ 与 run_command 同一条纪律：先 CREATE_SUSPENDED，挂进作业后才恢复
//   （关掉「挂接前派生」那个逃逸窗口），见 sandbox_windows.go 文件头 §3。
package main

import (
	"fmt"
	"os/exec"
	"sync"
	"syscall"
	"unsafe"
)

// mcpStartContained：挂起启动 → 挂进作业 → 恢复。成功返回「整树清退」闭包。
//
// 返回值的 `started` 把两种失败分开：**进程没起来**（false，调用方直接失败）与
// **起来了但没纳管上**（true，调用方降级为只杀直系子进程并告警）。
func mcpStartContained(cmd *exec.Cmd) (func(), bool, error) {
	hJobRaw, _, err := procCreateJobObjectW.Call(0, 0)
	if hJobRaw == 0 {
		return nil, false, fmt.Errorf("CreateJobObjectW: %w", err)
	}
	hJob := syscall.Handle(hJobRaw)

	// 只开「关句柄即整树」——不加进程数 / 内存 / UI 限制（理由见文件头）。
	var ext jobObjectExtendedLimitInformation
	ext.BasicLimitInformation.LimitFlags = jobObjectLimitKillOnJobClose
	r, _, e := procSetInformationJobObject.Call(
		uintptr(hJob),
		jobObjectExtendedLimitInformationClass,
		uintptr(unsafe.Pointer(&ext)),
		uintptr(unsafe.Sizeof(ext)),
	)
	if r == 0 {
		syscall.CloseHandle(hJob)
		return nil, false, fmt.Errorf("SetInformationJobObject: %w", e)
	}

	cmd.SysProcAttr = &syscall.SysProcAttr{
		HideWindow:    true,
		CreationFlags: createNoWindow | createSuspended,
	}
	if err := cmd.Start(); err != nil {
		syscall.CloseHandle(hJob)
		return nil, false, fmt.Errorf("启动进程失败: %w", err)
	}
	if err := attachAndResume(cmd, hJob); err != nil {
		_ = cmd.Process.Kill()
		_ = cmd.Wait()
		syscall.CloseHandle(hJob)
		return nil, true, err
	}

	var once sync.Once
	kill := func() {
		once.Do(func() {
			// TerminateJobObject 对空作业是 no-op；CloseHandle 才是 KILL_ON_JOB_CLOSE
			// 真正生效的那一刻 —— 句柄不关就是每会话泄一个内核对象。
			_, _, _ = procTerminateJobObject.Call(uintptr(hJob), 1)
			syscall.CloseHandle(hJob)
		})
	}
	return kill, true, nil
}
