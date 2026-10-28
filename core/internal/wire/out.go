package wire

// out.go —— core→gui 方向的出站写入：seq 分配 + 信封字段序 + 即刷/缓冲。

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"time"
)

// Out = 一条 core→gui 流的出站端。持有该方向的 seq（I4 的量具）。
//
// burst=false：逐行即刷（默认；流式语义——GUI 的 delta 渲染按到达节奏走）。
// burst=true：内容攒进缓冲、一次写出（fake-core 的 --burst 负载旋钮），
// 但**控制面**消息仍立即刷（见 Emit 内的必刷表）。
type Out struct {
	w   io.Writer
	buf *bufio.Writer // 非 nil = burst 缓冲写
	seq int64         // 下一个待发 seq（core→gui 方向从 0 起）
}

// NewOut：w = 逐行模式的底层写出（fmt.Fprintln + 可 Sync 则 Sync）；
// burst = fake-core 的负载旋钮（缓冲开 256KB——比一轮输出大，见 fake-core 注释）。
func NewOut(w io.Writer, burst bool) *Out {
	o := &Out{w: w}
	if burst {
		o.buf = bufio.NewWriterSize(w, 256*1024)
	}
	return o
}

// Seq：当前 seq（下一条消息将用的号）。fake-core 的 ask id 用它派生、
// hello 校验用它判断握手时点；调用方**不得**用它自行跳号——跳号只经 Skip。
func (o *Out) Seq() int64 { return o.seq }

// SetSeq：设置当前 seq（仅在 resume 恢复会话时设置为 lastSeq+1）。
func (o *Out) SetSeq(seq int64) { o.seq = seq }

// Skip：故意空一个 seq 号（I4 负向注入的唯一合法入口）。
// 真 core 永远不调它；假 core 的 --drop-seq 用它造「消息丢失」。
func (o *Out) Skip() { o.seq++ }

// Emit：给消息分配 seq + ts 后写一行。字段序与夹具一致：t, seq, ts, ...
// ★ 每条即刷（流式语义）——burst 模式下仅控制面消息立即刷。
func (o *Out) Emit(payload map[string]any) {
	payload["seq"] = o.seq
	payload["ts"] = time.Now().UTC().Format("2006-01-02T15:04:05.000Z")
	o.seq++

	// 稳定字段序：t, seq, ts 在前（hand-rolled，避免 map 序不确定）
	var b strings.Builder
	b.WriteString("{")
	writeKV(&b, "t", payload["t"], true)
	writeKV(&b, "seq", payload["seq"], false)
	writeKV(&b, "ts", payload["ts"], false)
	for k, v := range payload {
		switch k {
		case "t", "seq", "ts":
			continue
		}
		writeKV(&b, k, v, false)
	}
	b.WriteString("}")
	if o.buf != nil {
		// burst：**内容**先攒着（这里不能无条件 flush，否则等于没缓冲）。
		o.buf.WriteString(b.String())
		o.buf.WriteByte('\n')
		// ★ 但**控制面**消息必须立即推：`hello` / `state` / `workspace` / `error` /
		//   `ask` / `bye` 全都是「对面正在等它」的消息，攒在缓冲里不是慢，是**死锁**。
		//   ★ 第一版只留了 hello/bye/ask 三个显式 flush 点，漏了 `workspace` ack
		//   ⇒ GUI 等初始工作区路径等到超时，整场只收到 1 条消息（blocks=0）。
		//   教训同 REQ-03 的 M 系列：**"我列了三个必刷点"不等于"我列全了"**。
		if t, ok := payload["t"].(string); ok {
			switch t {
			case "hello", "state", "workspace", "error", "ask", "bye":
				o.buf.Flush()
			}
		}
		return
	}
	fmt.Fprintln(o.w, b.String())
	if s, ok := o.w.(interface{ Sync() error }); ok {
		_ = s.Sync()
	}
}

// Flush：把 burst 缓冲推给管道（逐行即刷模式下是 no-op）。
// ★ 三个必刷点，漏一个就是死锁而不是"慢一点"：
//  ① hello 之后——否则 GUI 等的另一半握手永远不出现；
//  ② **阻塞等 answer 之前**——否则 ask 留在缓冲里，GUI 不知道要回答什么；
//  ③ 一轮结束 / 退出之前——否则最后几十行烂在缓冲里。
func (o *Out) Flush() {
	if o.buf != nil {
		o.buf.Flush()
	}
}

func writeKV(b *strings.Builder, key string, v any, first bool) {
	if !first {
		b.WriteString(",")
	}
	kb, _ := json.Marshal(key)
	b.Write(kb)
	b.WriteString(":")
	vb, err := json.Marshal(v)
	if err != nil {
		vb = []byte("null")
	}
	b.Write(vb)
}
