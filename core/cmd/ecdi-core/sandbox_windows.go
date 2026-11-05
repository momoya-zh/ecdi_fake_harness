//go:build windows

// sandbox_windows.go —— Windows 平台的 L1 沙盒实现。
//
// 机制：
// 1. Windows Job Object（作业对象）：
//   - JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE：句柄关闭或强杀时，整树清理，零僵尸/孤儿进程；
//   - JOB_OBJECT_LIMIT_ACTIVE_PROCESS：限制活跃进程数（默认 32），防 fork 炸弹；
//   - JOB_OBJECT_LIMIT_JOB_MEMORY：限制总物理内存（默认 1024MB），防内存耗尽；
//   - BELOW_NORMAL_PRIORITY_CLASS：降低 CPU 优先级，防拖垮 GUI 响应；
//   - JobObjectBasicUIRestrictions：截断剪切板读写、系统设置更改、窗口句柄操作。
//
// 2. Restricted Token（受限安全令牌）：
//   - DISABLE_MAX_PRIVILEGE + LUA_TOKEN：剥离 SeDebugPrivilege 等高危特权，降权为标准受限令牌。
//
// 3. 先 CREATE_SUSPENDED 再挂作业、再恢复：
//   - 作业只自动纳管"挂接之后"派生的子进程。先 Start 再 Assign 存在一个窗口，
//     窗口里派生的孙进程会逃出作业（超时强杀不到）——所以必须挂起→挂接→恢复。
//
// ★ 诚实边界：任何一步（受限令牌 / 作业创建 / 挂接）失败都 **fail-closed**——
// 命令不执行，而不是静默退回完整权限。提案卡的 note 由实际平台能力生成，
// 不写"已保护"若不成立。
//
// 零第三方依赖：只使用标准库 syscall / unsafe / os / os/exec。
package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os/exec"
	"strings"
	"sync"
	"syscall"
	"unsafe"
)

var (
	modkernel32                  = syscall.NewLazyDLL("kernel32.dll")
	procCreateJobObjectW         = modkernel32.NewProc("CreateJobObjectW")
	procSetInformationJobObject  = modkernel32.NewProc("SetInformationJobObject")
	procAssignProcessToJobObject = modkernel32.NewProc("AssignProcessToJobObject")
	procTerminateJobObject       = modkernel32.NewProc("TerminateJobObject")

	modadvapi32               = syscall.NewLazyDLL("advapi32.dll")
	procOpenProcessToken      = modadvapi32.NewProc("OpenProcessToken")
	procCreateRestrictedToken = modadvapi32.NewProc("CreateRestrictedToken")

	// ntdll!NtResumeProcess：Go 的 os/exec 不暴露主线程句柄，无法 ResumeThread；
	// 用 NtResumeProcess 恢复整个挂起进程（挂接窗口的收口手段）。
	modntdll            = syscall.NewLazyDLL("ntdll.dll")
	procNtResumeProcess = modntdll.NewProc("NtResumeProcess")
)

const (
	// Job Object Limits
	jobObjectExtendedLimitInformationClass = 9
	jobObjectBasicUIRestrictionsClass      = 4

	jobObjectLimitKillOnJobClose = 0x00002000
	jobObjectLimitActiveProcess  = 0x00000008
	jobObjectLimitJobMemory      = 0x00000200
	jobObjectLimitPriorityClass  = 0x00000020

	belowNormalPriorityClass = 0x00004000

	jobObjectUILimitHandles          = 0x00000001
	jobObjectUILimitReadClipboard    = 0x00000002
	jobObjectUILimitWriteClipboard   = 0x00000004
	jobObjectUILimitSystemParameters = 0x00000008
	jobObjectUILimitDisplaySettings  = 0x00000010
	jobObjectUILimitGlobalAtoms      = 0x00000020

	processSetQuota      = 0x0100
	processTerminate     = 0x0001
	processSuspendResume = 0x0800
	createSuspended      = 0x00000004
	jobTerminateExitCode = 1
)

const (
	// Token flags
	tokenDuplicate      = 0x0002
	tokenAssignPrimary  = 0x0001
	tokenQuery          = 0x0008
	disableMaxPrivilege = 0x1
	luaToken            = 0x4
)

type ioCounters struct {
	ReadOperationCount  uint64
	WriteOperationCount uint64
	OtherOperationCount uint64
	ReadTransferCount   uint64
	WriteTransferCount  uint64
	OtherTransferCount  uint64
}

type jobObjectBasicLimitInformation struct {
	PerProcessUserTimeLimit int64
	PerJobUserTimeLimit     int64
	LimitFlags              uint32
	MinimumWorkingSetSize   uintptr
	MaximumWorkingSetSize   uintptr
	ActiveProcessLimit      uint32
	Affinity                uintptr
	PriorityClass           uint32
	SchedulingClass         uint32
}

type jobObjectExtendedLimitInformation struct {
	BasicLimitInformation jobObjectBasicLimitInformation
	IoInfo                ioCounters
	ProcessMemoryLimit    uintptr
	JobMemoryLimit        uintptr
	PeakProcessMemoryUsed uintptr
	PeakJobMemoryUsed     uintptr
}

type jobObjectBasicUIRestrictions struct {
	UIRestrictionsClass uint32
}

// platformSandboxNote：第一行只写**本平台真的做了**的边界。
func (cfg SandboxConfig) platformSandboxNote() string {
	parts := []string{"L1 沙盒（Windows Job Object）"}
	if cfg.MemoryLimitMB > 0 {
		parts = append(parts, fmt.Sprintf("内存≤%dMB", cfg.MemoryLimitMB))
	} else {
		parts = append(parts, "内存不限")
	}
	parts = append(parts, fmt.Sprintf("进程≤%d", cfg.effectiveMaxProcesses()), "整树强杀")
	if cfg.DropPrivileges {
		parts = append(parts, "特权已剥离")
	} else {
		parts = append(parts, "保留宿主特权")
	}
	return strings.Join(parts, " · ")
}

// createSandboxedToken 创建剥离了特权的受限安全令牌。
func createSandboxedToken() (syscall.Handle, error) {
	procCurrent, err := syscall.GetCurrentProcess()
	if err != nil {
		return 0, err
	}
	var hToken syscall.Handle
	r, _, err := procOpenProcessToken.Call(
		uintptr(procCurrent),
		tokenDuplicate|tokenAssignPrimary|tokenQuery,
		uintptr(unsafe.Pointer(&hToken)),
	)
	if r == 0 {
		return 0, fmt.Errorf("OpenProcessToken: %w", err)
	}
	defer syscall.CloseHandle(hToken)

	var hRestricted syscall.Handle
	// 优先尝试 DISABLE_MAX_PRIVILEGE | LUA_TOKEN (UAC 降权)
	r, _, _ = procCreateRestrictedToken.Call(
		uintptr(hToken),
		disableMaxPrivilege|luaToken,
		0, 0,
		0, 0,
		0, 0,
		uintptr(unsafe.Pointer(&hRestricted)),
	)
	if r == 0 {
		// 若已是非特权账户导致 LUA_TOKEN 失败，退回仅使用 DISABLE_MAX_PRIVILEGE
		r, _, err = procCreateRestrictedToken.Call(
			uintptr(hToken),
			disableMaxPrivilege,
			0, 0,
			0, 0,
			0, 0,
			uintptr(unsafe.Pointer(&hRestricted)),
		)
		if r == 0 {
			return 0, fmt.Errorf("CreateRestrictedToken: %w", err)
		}
	}
	return hRestricted, nil
}

// createAndConfigureJob 创建并配置 Job Object。
func createAndConfigureJob(cfg SandboxConfig) (syscall.Handle, error) {
	hJobRaw, _, err := procCreateJobObjectW.Call(0, 0)
	if hJobRaw == 0 {
		return 0, fmt.Errorf("CreateJobObjectW: %w", err)
	}
	hJob := syscall.Handle(hJobRaw)

	var extInfo jobObjectExtendedLimitInformation
	extInfo.BasicLimitInformation.LimitFlags = jobObjectLimitKillOnJobClose |
		jobObjectLimitActiveProcess |
		jobObjectLimitPriorityClass

	extInfo.BasicLimitInformation.ActiveProcessLimit = uint32(cfg.effectiveMaxProcesses())
	extInfo.BasicLimitInformation.PriorityClass = belowNormalPriorityClass

	if cfg.MemoryLimitMB > 0 {
		extInfo.BasicLimitInformation.LimitFlags |= jobObjectLimitJobMemory
		extInfo.JobMemoryLimit = uintptr(cfg.MemoryLimitMB) * 1024 * 1024
	}

	r, _, err := procSetInformationJobObject.Call(
		uintptr(hJob),
		jobObjectExtendedLimitInformationClass,
		uintptr(unsafe.Pointer(&extInfo)),
		uintptr(unsafe.Sizeof(extInfo)),
	)
	if r == 0 {
		syscall.CloseHandle(hJob)
		return 0, fmt.Errorf("SetInformationJobObject (extended limits): %w", err)
	}

	var uiInfo jobObjectBasicUIRestrictions
	uiInfo.UIRestrictionsClass = jobObjectUILimitHandles |
		jobObjectUILimitReadClipboard |
		jobObjectUILimitWriteClipboard |
		jobObjectUILimitSystemParameters |
		jobObjectUILimitDisplaySettings |
		jobObjectUILimitGlobalAtoms

	_, _, _ = procSetInformationJobObject.Call(
		uintptr(hJob),
		jobObjectBasicUIRestrictionsClass,
		uintptr(unsafe.Pointer(&uiInfo)),
		uintptr(unsafe.Sizeof(uiInfo)),
	)

	return hJob, nil
}

// attachAndResume：把**挂起**的子进程挂进作业，成功后才恢复执行。
// 任何一步失败都返回错误（由调用方终止进程），绝不退回"无作业裸跑"。
func attachAndResume(cmd *exec.Cmd, hJob syscall.Handle) error {
	hProc, err := syscall.OpenProcess(
		processSetQuota|processTerminate|processSuspendResume,
		false,
		uint32(cmd.Process.Pid),
	)
	if err != nil {
		return fmt.Errorf("打开子进程句柄失败：%w", err)
	}
	defer syscall.CloseHandle(hProc)

	r, _, e := procAssignProcessToJobObject.Call(uintptr(hJob), uintptr(hProc))
	if r == 0 {
		return fmt.Errorf("挂入 Job Object 失败：%w", e)
	}

	// NTSTATUS：0 = STATUS_SUCCESS。非 0 即失败。
	r, _, e = procNtResumeProcess.Call(uintptr(hProc))
	if r != 0 {
		return fmt.Errorf("恢复挂起进程失败：ntstatus=0x%x (%v)", r, e)
	}
	return nil
}

// startSandboxed：挂起启动 → 挂作业 → 恢复。失败时确保进程被终止。
func startSandboxed(cmd *exec.Cmd, hJob syscall.Handle) error {
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("启动命令失败：%w", err)
	}
	if err := attachAndResume(cmd, hJob); err != nil {
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
		_ = cmd.Wait()
		return err
	}
	return nil
}

// startBackgroundProcess：后台作业（协议 v6）的启动原语。
//
// 与 runCommandInSandbox 同源（同一套令牌 / 作业 / 挂起→挂接→恢复），区别只在
// 生命周期：这里把 Wait 与作业句柄交给 jobHub，作业跨轮存活；输出直接写 sink。
// 失败语义同为 fail-closed：调用方拿到 error 时命令没有在裸权限下跑。
func startBackgroundProcess(command, root string, cfg SandboxConfig, sink io.Writer) (*bgProcess, error) {
	if !cfg.Enabled {
		cmd := exec.Command("cmd", "/c", command)
		cmd.Dir = root
		cmd.Env = childEnv()
		cmd.Stdout, cmd.Stderr = sink, sink
		hideChildWindow(cmd)
		if err := cmd.Start(); err != nil {
			return nil, fmt.Errorf("启动后台命令失败：%w", err)
		}
		return &bgProcess{
			wait:  cmd.Wait,
			kill:  func() { if cmd.Process != nil { _ = cmd.Process.Kill() } },
			close: func() {},
			exitCode: func() int {
				if cmd.ProcessState != nil { return cmd.ProcessState.ExitCode() }
				return -1
			},
		}, nil
	}

	var hToken syscall.Handle
	if cfg.DropPrivileges {
		tok, err := createSandboxedToken()
		if err != nil {
			return nil, fmt.Errorf("L1 沙盒初始化失败（受限令牌）：%w", err)
		}
		hToken = tok
	}
	hJob, err := createAndConfigureJob(cfg)
	if err != nil {
		if hToken != 0 {
			syscall.CloseHandle(hToken)
		}
		return nil, fmt.Errorf("L1 沙盒初始化失败（Job Object）：%w", err)
	}

	cmd := exec.Command("cmd", "/c", command)
	cmd.Dir = root
	cmd.Env = childEnv()
	cmd.Stdout, cmd.Stderr = sink, sink
	cmd.WaitDelay = sandboxWaitDelay
	sysAttr := &syscall.SysProcAttr{
		HideWindow:    true,
		CreationFlags: createNoWindow | createSuspended,
	}
	if hToken != 0 {
		sysAttr.Token = syscall.Token(hToken)
	}
	cmd.SysProcAttr = sysAttr

	if err := startSandboxed(cmd, hJob); err != nil {
		syscall.CloseHandle(hJob)
		if hToken != 0 {
			syscall.CloseHandle(hToken)
		}
		return nil, err
	}
	if hToken != 0 {
		// 进程已创建，主令牌句柄不再需要（进程持有自己的副本）。
		syscall.CloseHandle(hToken)
	}
	return &bgProcess{
		wait: cmd.Wait,
		kill: func() {
			procTerminateJobObject.Call(uintptr(hJob), jobTerminateExitCode)
			if cmd.Process != nil {
				_ = cmd.Process.Kill()
			}
		},
		close: func() { syscall.CloseHandle(hJob) },
		exitCode: func() int {
			if cmd.ProcessState != nil { return cmd.ProcessState.ExitCode() }
			return -1
		},
	}, nil
}

// runCommandInSandbox 在沙盒约束下执行命令。
func runCommandInSandbox(ctx context.Context, command string, root string, timeoutS int, cfg SandboxConfig) toolResult {
	if !cfg.Enabled {
		return runCommandUnsandboxed(ctx, command, root, timeoutS)
	}

	var hRestrictedToken syscall.Handle
	if cfg.DropPrivileges {
		tok, err := createSandboxedToken()
		if err != nil {
			// ★ fail-closed：拿不到受限令牌 ≠ 可以按宿主完整权限跑。
			return toolResult{OK: false, Output: fmt.Sprintf(
				"L1 沙盒初始化失败（受限令牌）：%v\n命令未执行。若确认要在无沙盒保护下运行，请用 --sandbox=false。", err)}
		}
		hRestrictedToken = tok
		defer syscall.CloseHandle(hRestrictedToken)
	}

	hJob, err := createAndConfigureJob(cfg)
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf(
			"L1 沙盒初始化失败（Job Object）：%v\n命令未执行。若确认要在无沙盒保护下运行，请用 --sandbox=false。", err)}
	}
	defer syscall.CloseHandle(hJob)

	cmd := exec.Command("cmd", "/c", command)
	cmd.Dir = root
	cmd.Env = childEnv()
	sysAttr := &syscall.SysProcAttr{
		HideWindow: true,
		// CREATE_SUSPENDED：先挂起，等挂进作业后再恢复（关掉"挂接前派生"的窗口）。
		CreationFlags: createNoWindow | createSuspended,
	}
	if hRestrictedToken != 0 {
		sysAttr.Token = syscall.Token(hRestrictedToken)
	}
	cmd.SysProcAttr = sysAttr

	var outBuf bytes.Buffer
	cmd.Stdout = &outBuf
	cmd.Stderr = &outBuf
	// 孙进程若仍持有 stdout 管道，Wait 的 I/O 等待不该无限期挂着。
	cmd.WaitDelay = sandboxWaitDelay

	if err := startSandboxed(cmd, hJob); err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("L1 沙盒启动失败，命令未执行：%v", err)}
	}

	// 超时或取消 ⇒ 强杀整个作业；再补一刀直接杀根进程，保证 Wait 不会挂死。
	done := make(chan struct{})
	var watcher sync.WaitGroup
	watcher.Add(1)
	go func() {
		defer watcher.Done()
		select {
		case <-done:
			return
		case <-ctx.Done():
			procTerminateJobObject.Call(uintptr(hJob), jobTerminateExitCode)
			if cmd.Process != nil {
				_ = cmd.Process.Kill()
			}
		}
	}()

	waitErr := cmd.Wait()
	// ★ GBK 归一：Windows 控制台命令按 CP936 输出，整段转一次再进工具结果
	//   （详见 decode.go 头注；GUI/protocol 只接受 UTF-8）。
	text := trimOutput(decodeProcessOutput(outBuf.Bytes()))
	close(done)
	watcher.Wait()

	switch {
	case ctx.Err() == context.DeadlineExceeded:
		return toolResult{OK: false, Output: fmt.Sprintf("命令超时（%d 秒）已通过 L1 沙盒强制终止整树：\n%s", timeoutS, text)}
	case ctx.Err() == context.Canceled:
		return toolResult{OK: false, Output: fmt.Sprintf("命令已取消，已通过 L1 沙盒强制终止整树：\n%s", text)}
	case waitErr != nil:
		return toolResult{OK: false, Output: fmt.Sprintf("退出码非 0（%v）：\n%s", waitErr, text)}
	}
	if strings.TrimSpace(text) == "" {
		return toolResult{OK: true, Output: "（无输出，退出码 0）"}
	}
	return toolResult{OK: true, Output: text}
}

// runCommandUnsandboxed 无沙盒回退路径（仅 --sandbox=false 时可达）。
func runCommandUnsandboxed(ctx context.Context, command string, root string, timeoutS int) toolResult {
	cmd := exec.CommandContext(ctx, "cmd", "/c", command)
	cmd.Dir = root
	cmd.Env = childEnv()
	hideChildWindow(cmd)
	out, err := cmd.CombinedOutput()
	text := trimOutput(decodeProcessOutput(out))
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
