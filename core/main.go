// ecdi-fake-core —— ECDI_fake_harness 的「假 core」（P1.5 子进程真路径）。
//
// 定位：只实现 protocol/PROTOCOL.md v1 的消息流，与 core↔gui 契约严格同构。
// 不接真实模型、不执行真实工具、不读写用户文件——它的唯一职责是让 GUI 面对
// 「一个真的在管道另一头的进程」，从而把跨进程/线程边界的问题压出来。
//
// 用法（由 harness_gui 拉起；也可手工对管道调试）：
//
//	fake-core [--speed 1.0] [--seed 20261007] [--drop-seq N] [--drop-block-end] [--quiet]
//
// stdin  = gui→core 消息（JSONL，一行一条）
// stdout = core→gui 消息（JSONL，一行一条，每条即刷）
// stderr = 日志（harness 侧重定向到 core-stderr.log）
package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"
)

// guiMsg = gui→core 消息的解析结果（协议 §4 五类：hello/prompt/cancel/answer/bye）。
type guiMsg struct {
	T      string
	Seq    int64
	Text   string
	ID     string
	Choice string
	Raw    string
}

func parseGui(line string) guiMsg {
	var raw struct {
		T      string `json:"t"`
		Seq    int64  `json:"seq"`
		Text   string `json:"text"`
		ID     string `json:"id"`
		Choice string `json:"choice"`
	}
	if err := json.Unmarshal([]byte(line), &raw); err != nil {
		return guiMsg{T: "", Raw: line}
	}
	return guiMsg{T: raw.T, Seq: raw.Seq, Text: raw.Text, ID: raw.ID, Choice: raw.Choice}
}

// readStdin：stdin 行读线程 → 行通道（单一消费者 = 主 goroutine）。
func readStdin(lines chan<- string, logf func(string, ...any)) {
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 0, 64*1024), 4*1024*1024)
	for sc.Scan() {
		line := sc.Text()
		if strings.TrimSpace(line) == "" {
			continue // 空行忽略（分帧约定：一行一个 JSON）
		}
		lines <- line
	}
	if err := sc.Err(); err != nil {
		logf("stdin read error: %v", err)
	}
	close(lines)
}

func main() {
	speed := flag.Float64("speed", 1.0, "节奏缩放（>1 更快；1.0 = 真实节奏）")
	seed := flag.Int64("seed", 20261007, "RNG 种子——同参必得同脚本（确定性）")
	dropSeq := flag.Int("drop-seq", 0, "每 N 条消息故意跳一个 seq 号（0 = 关；I4 负向注入）")
	dropBlockEnd := flag.Bool("drop-block-end", false, "故意漏一个 block_end（I1 负向注入）")
	quiet := flag.Bool("quiet", false, "关闭 stderr 日志")
	flag.Parse()

	logf := func(format string, args ...any) {
		if !*quiet {
			fmt.Fprintf(os.Stderr, "[fake-core] "+format+"\n", args...)
		}
	}

	lines := make(chan string, 64)
	go readStdin(lines, logf)

	s := newSession(*seed, *speed, *dropSeq, *dropBlockEnd, lines, logf)
	if err := s.run(); err != nil {
		logf("fatal: %v", err)
		os.Exit(1)
	}
}
