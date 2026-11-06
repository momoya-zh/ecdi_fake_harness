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

// sandboxProbeErrDetail：把探针脚本自己写下的失败原因接进 Go 侧断言。
// ★ 为什么需要这一步：脚本内的启动失败发生在 ShellExecuteEx 层，默认只会变成
//   宿主的错误弹窗 + 一份空输出。于是在测试输出里「脚本根本没起来」与「起来了
//   但没被杀掉」长得一模一样——而后者正是这条用例要防的假绿。脚本把原因写进
//   bg.err，这里读出来附在断言后面，两种失败才分得开。
func sandboxProbeErrDetail(dir string) string {
	b, err := os.ReadFile(filepath.Join(dir, "bg.err"))
	if err != nil {
		return ""
	}
	return "；脚本报告：" + strings.TrimSpace(string(b))
}

func TestSandbox_BackgroundChildKilledOnJobClose(t *testing.T) {
	dir := t.TempDir()
	ps := filepath.Join(dir, "bg.ps1")
	// ★ 这两条属性必须显式钉住，别依赖宿主默认值：
	//   UseShellExecute=true：让孙进程**不继承** powershell 的 stdio 管道（ShellExecuteEx
	//     路径）。若为 false，孙进程会持有同一个管道写端，于是 Go 侧 cmd.Wait() 要等
	//     未读完的 I/O，sandboxWaitDelay 到点后报 `exec: WaitDelay expired before I/O
	//     complete`——用例变成假红。实测（沙箱内，每臂 8 轮）：true 8/8 通过；false 0/8；
	//     false + RedirectStandard* 0/8。PowerShell 5.1 默认 true，pwsh 7 默认 false，
	//     所以这里写死不随宿主漂移。
	//   ErrorDialog=false：ShellExecuteEx 层的失败否则只弹宿主对话框、输出为空，测试
	//     侧看不见原因（见 sandboxProbeErrDetail）。
	// ★ 脚本体保持**纯 ASCII**：PS 5.1 把无 BOM 的 .ps1 按本地 ANSI(GBK) 解码，UTF-8
	//   中文能否存活取决于字节边界（奇数尾巴会吞掉紧跟的 ASCII 引号 ⇒ ParserError），
	//   是掷骰子而不是「含中文就挂」。错误文本因此改用 WriteAllText + UTF8 无 BOM 写盘：
	//   5.1 的 Set-Content 写 ANSI，Go 读回来是乱码。
	script := "$ErrorActionPreference = 'Stop'\n" +
		"function Write-Err([string]$m) {\n" +
		"  if (-not $m) { $m = 'unknown' }\n" +
		"  [System.IO.File]::WriteAllText((Join-Path $PWD 'bg.err'), $m, (New-Object System.Text.UTF8Encoding($false)))\n" +
		"}\n" +
		"$psi = New-Object System.Diagnostics.ProcessStartInfo\n" +
		"$psi.FileName = 'cmd.exe'\n" +
		"$psi.Arguments = '/c ping -n 30 127.0.0.1 >nul'\n" +
		"$psi.UseShellExecute = $true\n" +
		"$psi.ErrorDialog = $false\n" +
		"try {\n" +
		"  $p = [System.Diagnostics.Process]::Start($psi)\n" +
		"} catch {\n" +
		"  $inner = ''\n" +
		"  if ($_.Exception.InnerException) { $inner = $_.Exception.InnerException.Message }\n" +
		"  Write-Err ('start failed, hresult=0x{0:X8}, type={1}, msg={2}; inner={3}' -f $_.Exception.HResult, $_.Exception.GetType().Name, $_.Exception.Message, $inner)\n" +
		"  exit 3\n" +
		"}\n" +
		"Start-Sleep -Milliseconds 300\n" +
		"if ($p.HasExited) {\n" +
		"  Write-Err ('child exited immediately, code=' + $p.ExitCode)\n" +
		"  exit 4\n" +
		"}\n" +
		"$p.Id | Set-Content -Path bg.pid\n"
	if err := os.WriteFile(ps, []byte(script), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	res := runCommandInSandbox(ctx,
		"powershell -NoProfile -ExecutionPolicy Bypass -File "+ps, dir, 20, defaultSandboxConfig())
	if !res.OK {
		t.Fatalf("启动后台子进程失败：%v%s", res.Output, sandboxProbeErrDetail(dir))
	}

	b, err := os.ReadFile(filepath.Join(dir, "bg.pid"))
	if err != nil {
		t.Fatalf("脚本未写出 pid：%v%s", err, sandboxProbeErrDetail(dir))
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
