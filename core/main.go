// ecdi-fake-core —— ECDI_fake_harness 的「假 core」（P1.5 子进程真路径）。
//
// 定位：只实现 protocol/PROTOCOL.md v3 的消息流，与 core↔gui 契约严格同构。
// 不接真实模型、不执行真实工具、不读写用户文件——它的唯一职责是让 GUI 面对
// 「一个真的在管道另一头的进程」，从而把跨进程/线程边界的问题压出来。
//
// 用法（由 harness_gui 拉起；也可手工对管道调试）：
//
//	fake-core [--speed 1.0] [--seed 20261007] [--drop-seq N] [--drop-block-end]
//	          [--workspace-delay-ms N] [--burst] [--quiet]
//
// stdin  = gui→core 消息（JSONL，一行一条）
// stdout = core→gui 消息（JSONL，一行一条，每条即刷；`--burst` 例外，见下）
// stderr = 日志（harness 侧重定向到 core-stderr.log）
//
// ★ `--burst`（2026-10-24）：**负载旋钮**，不是剧本模式。
//   关掉节奏等待并把一轮的所有行攒进一个缓冲、一次写出 ⇒ 读线程会在
//   UI 线程来不及 drain 之前把几百行塞进队列，从而**真的造出积压**。
//   它存在的理由只有一个：REQ-03 的「单回调预算」判据需要一个能把队列
//   压到 256 行以上的突发源，否则那条判据无法被证伪（负对照会"绿得"
//   毫无意义）。默认关 ⇒ 既有行为逐字节不变。
//
// ★ 线层下沉（2026-10-24，P3 规格 §3）：帧读写 / 信封 / seq / 状态编码 /
//   stdin 打戳读线程 / 路径规范化移入 core/internal/wire/（与真 core 共用
//   同一份机械规则）。本文件只保留剧本入口与注入旋钮——**行为逐位不变**，
//   由 P3 验收 A2（六夹具 + 13 探针逐位回归）把门。
package main

import (
	"flag"
	"os"
	"sync/atomic"

	"ecdi-fake-core/internal/wire"
)

func main() {
	speed := flag.Float64("speed", 1.0, "节奏缩放（>1 更快；1.0 = 真实节奏）")
	seed := flag.Int64("seed", 20261007, "RNG 种子——同参必得同脚本（确定性）")
	dropSeq := flag.Int("drop-seq", 0, "每 N 条消息故意跳一个 seq 号（0 = 关；I4 负向注入）")
	dropBlockEnd := flag.Bool("drop-block-end", false, "故意漏一个 block_end（I1 负向注入）")
	wsDelay := flag.Int("workspace-delay-ms", 0,
		"工作区请求的应答延迟（毫秒；>0 时产生真实的「未决」窗口，用于验证交错拒绝）")
	burst := flag.Bool("burst", false,
		"负载旋钮：关掉节奏等待并把一轮攒成一次写出（造队列积压；默认关 ⇒ 行为不变）")
	quiet := flag.Bool("quiet", false, "关闭 stderr 日志")
	resumeID := flag.String("resume", "", "恢复会话 ID（从 .ecdi/sessions/<id> 续接）")
	sessionID := flag.String("session", "", "会话 ID（缺省为 s-live-1）")
	workspace := flag.String("workspace", "", "初始工作区根目录（缺省为当前进程 cwd）")
	contextTokens := flag.Int("context", 0, "上下文容量分母（token 数；0 = 不发 hello.context）")
	failSummarize := flag.Bool("fail-summarize", false, "测试注入：强制模拟摘要压缩失败（用于负对照验证）")
	globalMemory := flag.String("global-memory", "", "全局记忆目录（缺省为 exe 同级 data/memory）")
	flag.Parse()

	logf := wire.NewLogger("[fake-core] ", *quiet)

	lines := make(chan wire.StampedLine, 64)
	var curState atomic.Int32 // 初值 0 = idle（CodeIdle 的零值必须就是 idle）
	curState.Store(wire.CodeIdle)
	go wire.ReadStdin(lines, &curState, logf)

	s := newSession(*seed, *speed, *dropSeq, *dropBlockEnd, *wsDelay, *burst,
		*resumeID, *sessionID, *workspace, *contextTokens, *failSummarize, *globalMemory,
		lines, &curState, logf)
	if err := s.run(); err != nil {
		logf("fatal: %v", err)
		os.Exit(1)
	}
}
