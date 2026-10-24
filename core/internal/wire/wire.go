// Package wire —— 假 core 与真 core 共用的「线层」（协议 §1/§2/§5 的机械部分）。
//
// 抽取自 fake-core（2026-10-24，P3 规格 §3）：两边面对的是**同一条协议**，
// 帧读写、信封字段序、seq 分配、状态编码、gui→core 消息解析、路径规范化
// 这些机械规则只允许有一份真相。**会话逻辑不属于这里**：假 core 的剧本
// （script.go）与真 core 的 agent loop（cmd/ecdi-core）各自持有自己的
// handlePrompt——下沉它们只会把两种不同的轮语义搅在一起。
//
// ★ 行为逐位不变是本包成立的条件：fake-core 改为引用后，六夹具基线与
// live 读数必须逐位不变（P3 验收 A2 把门）。本包**只做搬运，不做改进**——
// 任何"顺手优化"都会让 A2 的对照失去意义。
package wire

import (
	"fmt"
	"os"
	"strings"
)

// State = 协议 §6 的五种会话状态（字符串形式直接上协议）。
type State string

const (
	StateIdle      State = "idle"
	StateRunning   State = "running"
	StateWaiting   State = "waiting"
	StateCancelled State = "cancelled"
	StateError     State = "error"
)

// 状态码：唯一目的是让**读线程**（另一个 goroutine）能原子地取到当前状态，
// 给每一行输入打上「到达时刻」的戳。整数编码而非直接传 State，是为了用
// atomic.Int32 而不引入互斥锁；映射集中在 CodeOf/StateOfCode 两处，不散落。
const (
	CodeIdle int32 = iota
	CodeRunning
	CodeWaiting
	CodeCancelled
	CodeError
)

func CodeOf(v State) int32 {
	switch v {
	case StateRunning:
		return CodeRunning
	case StateWaiting:
		return CodeWaiting
	case StateCancelled:
		return CodeCancelled
	case StateError:
		return CodeError
	}
	return CodeIdle
}

func StateOf(c int32) State {
	switch c {
	case CodeRunning:
		return StateRunning
	case CodeWaiting:
		return StateWaiting
	case CodeCancelled:
		return StateCancelled
	case CodeError:
		return StateError
	}
	return StateIdle
}

// NewLogger：stderr 日志器。prefix 形如 "[fake-core] " / "[ecdi-core] "。
//
// ★ secrets：**绝不**落盘的字符串（真 core 传 API key；假 core 为空）。
// 每条日志在格式化**之后**整体过一遍遮蔽——按"别在打日志处记得遮"来设计，
// 那种纪律迟早会在某个新日志点失效（同 --burst 必刷点的教训：列举不等于列全）。
func NewLogger(prefix string, quiet bool, secrets ...string) func(string, ...any) {
	return func(format string, args ...any) {
		if quiet {
			return
		}
		line := fmt.Sprintf(prefix+format+"\n", args...)
		for _, s := range secrets {
			if s != "" {
				line = strings.ReplaceAll(line, s, "***")
			}
		}
		fmt.Fprint(os.Stderr, line)
	}
}
