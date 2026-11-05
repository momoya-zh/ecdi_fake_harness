//go:build windows

// sandbox_windows_test.go —— 只在 Windows 上跑的"承诺级"验证。
//
// 这两个案不是在测"命令能跑"，而是在测 note 里写给用户的两句话：
//
//	① 命令退出/作业关闭后，后台派生的孙进程必须一起死（KILL_ON_JOB_CLOSE）；
//	② 作业内存上限必须真的能拒绝超额分配（JOB_MEMORY）。
//
// 它们依赖 PowerShell（Windows 自带）与进程退出码，故用 GetExitCodeProcess
// 判活——不能用 OpenProcess 是否成功判，已终止的进程对象在句柄关闭前仍可打开。
package main

import (
	"context"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

func sandboxProbeAlive(pid int) bool {
	h, err := syscall.OpenProcess(0x1000, false, uint32(pid))
	if err != nil {
		return false
	}
	defer syscall.CloseHandle(h)
	var code uint32
	if err := syscall.GetExitCodeProcess(h, &code); err != nil {
		return false
	}
	return code == 259 // STILL_ACTIVE
}

func TestSandbox_BackgroundChildKilledOnJobClose(t *testing.T) {
	dir := t.TempDir()
	ps := filepath.Join(dir, "bg.ps1")
	script := "$psi = New-Object System.Diagnostics.ProcessStartInfo\n" +
		"$psi.FileName = 'cmd.exe'\n" +
		"$psi.Arguments = '/c ping -n 30 127.0.0.1 >nul'\n" +
		"$p = [System.Diagnostics.Process]::Start($psi)\n" +
		"$p.Id | Set-Content -Path bg.pid\n"
	if err := os.WriteFile(ps, []byte(script), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	res := runCommandInSandbox(ctx,
		"powershell -NoProfile -ExecutionPolicy Bypass -File "+ps, dir, 20, defaultSandboxConfig())
	if !res.OK {
		t.Fatalf("启动后台子进程失败：%v", res.Output)
	}

	b, err := os.ReadFile(filepath.Join(dir, "bg.pid"))
	if err != nil {
		t.Fatalf("脚本未写出 pid：%v", err)
	}
	pid, err := strconv.Atoi(strings.TrimSpace(string(b)))
	if err != nil {
		t.Fatalf("pid 非法 %q：%v", b, err)
	}

	// 作业句柄在 runCommandInSandbox 返回时已关闭；给内核一点清理时间。
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		if !sandboxProbeAlive(pid) {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	t.Fatalf("后台孙进程 %d 在作业关闭后仍存活（KILL_ON_JOB_CLOSE 未生效）", pid)
}

func TestSandbox_MemoryLimitEnforced(t *testing.T) {
	dir := t.TempDir()
	ps := filepath.Join(dir, "mem.ps1")
	script := "$ErrorActionPreference = 'Stop'\n" +
		"$b = New-Object byte[] 536870912\n" +
		"Write-Output 'alloc-ok'\n"
	if err := os.WriteFile(ps, []byte(script), 0o644); err != nil {
		t.Fatal(err)
	}

	cfg := defaultSandboxConfig()
	cfg.MemoryLimitMB = 256
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	res := runCommandInSandbox(ctx,
		"powershell -NoProfile -ExecutionPolicy Bypass -File "+ps, dir, 60, cfg)
	if res.OK {
		t.Fatalf("256MB 作业里分配 512MB 应当失败，但命令成功了：%s", res.Output)
	}
	if !strings.Contains(res.Output, "OutOfMemory") {
		t.Errorf("失败原因不是内存上限（缺 OutOfMemory）：%s", res.Output)
	}
}
