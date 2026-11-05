package main

import (
	"context"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func TestSandbox_NoteFormatting(t *testing.T) {
	cfg := SandboxConfig{
		Enabled:        true,
		MemoryLimitMB:  512,
		MaxProcesses:   16,
		DropPrivileges: true,
	}
	note := cfg.sandboxNote(60)
	// ★ 两行不是排版偏好：提案行不折行、行宽约 688px，单行会被裁掉右 1/3。
	if got := strings.Count(note, "\n"); got != 1 {
		t.Errorf("note 必须是两行（实际 %d 个换行）：%s", got, note)
	}
	if runtime.GOOS == "windows" {
		for _, want := range []string{"Windows Job Object", "512MB", "16", "特权已剥离"} {
			if !strings.Contains(note, want) {
				t.Errorf("note 缺少 %q：%s", want, note)
			}
		}
	} else {
		if strings.Contains(note, "Windows Job Object") {
			t.Errorf("非 Windows 平台不得声称 Job Object：%s", note)
		}
		if !strings.Contains(note, "进程组") {
			t.Errorf("非 Windows 平台应说明进程组边界：%s", note)
		}
	}

	cfg.DropPrivileges = false
	if runtime.GOOS == "windows" {
		if !strings.Contains(cfg.sandboxNote(30), "保留宿主特权") {
			t.Errorf("关闭特权剥离后 note 未如实说明：%s", cfg.sandboxNote(30))
		}
	}

	cfgDisabled := SandboxConfig{Enabled: false}
	noteDisabled := cfgDisabled.sandboxNote(30)
	if !strings.Contains(noteDisabled, "【警告】L1 沙盒已禁用") {
		t.Errorf("禁用时未包含警告：%s", noteDisabled)
	}
	if strings.Count(noteDisabled, "\n") != 1 {
		t.Errorf("禁用警告也必须是两行：%s", noteDisabled)
	}
}

func TestSandbox_BasicEcho(t *testing.T) {
	cfg := defaultSandboxConfig()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	cmdStr := "echo hello-sandbox"
	res := runCommandInSandbox(ctx, cmdStr, t.TempDir(), 10, cfg)
	if !res.OK {
		t.Fatalf("runCommandInSandbox failed: %v", res.Output)
	}
	if !strings.Contains(res.Output, "hello-sandbox") {
		t.Errorf("输出未包含预期内容：%q", res.Output)
	}
}

func TestSandbox_DisabledFallback(t *testing.T) {
	cfg := SandboxConfig{Enabled: false}
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	res := runCommandInSandbox(ctx, "echo fallback-mode", t.TempDir(), 10, cfg)
	if !res.OK {
		t.Fatalf("fallback failed: %v", res.Output)
	}
	if !strings.Contains(res.Output, "fallback-mode") {
		t.Errorf("fallback 输出未包含预期内容：%q", res.Output)
	}
}

func TestSandbox_PrivilegeDrop(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("特权剥离测试仅在 Windows 下执行")
	}

	// ★ 必须钉死 System32 的 whoami.exe：Git Bash 的 PATH 里 GNU whoami 在前，
	// 裸 "whoami /priv" 会解析到它（extra operand '/priv'），测试随环境红绿。
	who := filepath.Join(os.Getenv("SystemRoot"), "System32", "whoami.exe")
	if _, err := os.Stat(who); err != nil {
		t.Skipf("找不到 %s：%v", who, err)
	}

	cfg := defaultSandboxConfig()
	cfg.DropPrivileges = true
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	res := runCommandInSandbox(ctx, who+" /priv", t.TempDir(), 10, cfg)
	if !res.OK {
		t.Fatalf("whoami /priv 失败：%v", res.Output)
	}

	// 验证危险特权不存在
	dangerousPrivs := []string{
		"SeDebugPrivilege",
		"SeShutdownPrivilege",
		"SeBackupPrivilege",
		"SeRestorePrivilege",
		"SeTakeOwnershipPrivilege",
		"SeLoadDriverPrivilege",
		"SeImpersonatePrivilege",
	}
	for _, priv := range dangerousPrivs {
		if strings.Contains(res.Output, priv) {
			t.Errorf("安全漏洞：受限沙盒仍包含危险特权 %s", priv)
		}
	}
}

func TestSandbox_TreeKillOnCancel(t *testing.T) {
	if runtime.GOOS != "windows" {
		t.Skip("作业对象整树清理测试仅在 Windows 下执行")
	}

	cfg := defaultSandboxConfig()
	// 设置很短的超时，验证强制终止整树
	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	start := time.Now()
	// 用纯本地睡眠命令（不依赖网络，避免在无网络环境瞬间被拒）
	res := runCommandInSandbox(ctx, "powershell -Command Start-Sleep -s 5", t.TempDir(), 1, cfg)
	elapsed := time.Since(start)

	if res.OK {
		t.Fatalf("预期超时终止，但命令成功返回：%v", res.Output)
	}
	if elapsed > 3*time.Second {
		t.Errorf("终止耗时过长：%v，Job Object 应当在 1 秒内整树清退", elapsed)
	}
	if !strings.Contains(res.Output, "强制终止整树") && !strings.Contains(res.Output, "超时") {
		t.Errorf("错误信息未体现超时整树终止：%s", res.Output)
	}
}
