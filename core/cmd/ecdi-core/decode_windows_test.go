//go:build windows

package main

import (
	"context"
	"os/exec"
	"strings"
	"testing"
	"unicode/utf8"
)

// 真实代码页路径：用系统 GBK 解码器解「中文」的 CP936 字节。
// 这两个字在 GBK 里是 D6 D0 / CE C4 —— 与夹具无关，是代码页常量。
func TestDecodeGBK_RealCodePage(t *testing.T) {
	got, ok := decodeGBK([]byte{0xD6, 0xD0, 0xCE, 0xC4})
	if !ok {
		t.Fatal("系统 GBK 解码器不可用")
	}
	if got != "中文" {
		t.Fatalf("GBK 解码结果应为「中文」，got %q", got)
	}
	if s := decodeProcessOutput([]byte{0xD6, 0xD0, 0xCE, 0xC4}); s != "中文" {
		t.Fatalf("decodeProcessOutput 未走 GBK 路径，got %q", s)
	}
}

func TestDecodeGBK_Empty(t *testing.T) {
	if got, ok := decodeGBK(nil); !ok || got != "" {
		t.Fatalf("空输入应为 (\"\", true)，got (%q, %v)", got, ok)
	}
}
// chineseCodePage：本机控制台代码页是否 936。不是 ⇒ 跳过（用例的输入前提没了）。
func chineseCodePage(t *testing.T) bool {
	t.Helper()
	out, err := exec.Command("cmd", "/c", "chcp").Output()
	if err != nil {
		t.Logf("chcp 失败：%v", err)
		return false
	}
	return strings.Contains(string(out), "936")
}

// 端到端：真实 cmd 把中文按 CP936 写进管道，run_command 必须转回「中文」。
// 这是接线判据（decodeProcessOutput 的单测只证明解码器本身）。
func TestRunCommand_GBKEndToEnd(t *testing.T) {
	if !chineseCodePage(t) {
		t.Skip("系统控制台代码页不是 936，本用例不适用")
	}
	raw, err := exec.Command("cmd", "/c", "echo 中文").Output()
	if err != nil {
		t.Fatalf("直连 cmd 失败：%v", err)
	}
	if utf8.Valid(raw) {
		t.Skip("cmd 实际输出了 UTF-8（代码页未生效），本用例不适用")
	}
	res := runCommandUnsandboxed(context.Background(), "echo 中文", t.TempDir(), 10)
	if !res.OK {
		t.Fatalf("run_command 失败：%s", res.Output)
	}
	if !strings.Contains(res.Output, "中文") {
		t.Fatalf("GBK 输出没有解码成「中文」：%q", res.Output)
	}
}

// 后台作业同一判据：真实进程的 CP936 输出经 jobSink 后必须是 UTF-8「中文」。
func TestBackgroundJob_GBKEndToEnd(t *testing.T) {
	if !chineseCodePage(t) {
		t.Skip("系统控制台代码页不是 936，本用例不适用")
	}
	log := &msgLog{}
	h := newJobHub(log.add)
	defer h.shutdown()
	if _, err := h.start("echo 中文", t.TempDir(), "b1", SandboxConfig{}); err != nil {
		t.Fatalf("启动后台作业失败：%v", err)
	}
	log.wait(t, "end", isPhase("end"))
	var text string
	for _, m := range log.all() {
		if m["t"] == "job" && m["phase"] == "output" {
			text += m["text"].(string)
		}
	}
	if !utf8.ValidString(text) {
		t.Fatalf("job output.text 不是合法 UTF-8：%q", text)
	}
	if !strings.Contains(text, "中文") {
		t.Fatalf("后台作业的 GBK 输出未解码：%q", text)
	}
}

// 沙盒路径同一判据：受限令牌 + Job Object 下运行的 cmd 输出也要正确转码。
func TestRunCommandSandboxed_GBKEndToEnd(t *testing.T) {
	if !chineseCodePage(t) {
		t.Skip("系统控制台代码页不是 936，本用例不适用")
	}
	res := runCommandInSandbox(context.Background(), "echo 中文", t.TempDir(), 10, defaultSandboxConfig())
	if !res.OK {
		t.Fatalf("沙盒执行失败：%s", res.Output)
	}
	if !strings.Contains(res.Output, "中文") {
		t.Fatalf("沙盒路径的 GBK 输出未解码：%q", res.Output)
	}
}
