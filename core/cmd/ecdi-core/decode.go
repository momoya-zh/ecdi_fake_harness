// ============================================================
// 子进程输出的编码归一（GBK → UTF-8）
// ------------------------------------------------------------
// 为什么需要：Windows 控制台程序（cmd 内建命令、老工具、按 CP936 输出的
//   Python/Java 程序）吐的是 GBK 字节；协议与 GUI 只吃 UTF-8——
//   `encoding/json` 会把非法 UTF-8 字节替换成 U+FFFD，中文会整段变成「？」。
//   本文件在**进入工具结果 / protocol 之前**转一次，后续消费者（GUI、journal、
//   后台 job_output 游标）就只见 UTF-8。
//
// 判据（为什么不一律按 GBK 解）：现代工具（node / go / `python -X utf8`）输出
//   UTF-8，硬按 GBK 解会把它们的中文解成乱码。所以走**探测**而不是声明：
//     ① 整段是合法 UTF-8 ⇒ 原样返回；
//     ② 否则按 GBK 解（Windows：MultiByteToWideChar(936)）；
//     ③ 平台没有 GBK 解码器（非 Windows）⇒ 逐字节替换，绝不静默丢内容。
//
// 后台作业是**流式**的：write 边界可能切开一个多字节字符，所以另有
//   `streamDecoder` 做增量解码——纯 ASCII 立即放行（两种编码里 ASCII 同字节），
//   首个非 ASCII 字节才判定编码，判定后按该编码续解并留住不完整的尾巴。
//   ★ 探测的固有局限：一段**恰好也是合法 UTF-8** 的 GBK 字节会被当成 UTF-8。
//     这种流在真实中文输出里几乎不会出现（GBK 常用字的高字节组合绝大多数
//     不是合法 UTF-8 续字节），且误判也不会崩，只是个别字显示不对。
// ============================================================

package main

import (
	"strings"
	"unicode/utf8"
)

// gbkDecode 是平台解码器的测试接缝：生产值是 decodeGBK，测试可在非 Windows
// 上替换它来验证流式状态机（见 decode_test.go）。
var gbkDecode = decodeGBK

// decodeProcessOutput 把**整段**进程输出转成 UTF-8。
// 前台 run_command 走这里（输出已收集完，一次性判定最稳）。
func decodeProcessOutput(raw []byte) string {
	if len(raw) == 0 {
		return ""
	}
	if utf8.Valid(raw) {
		return string(raw)
	}
	if s, ok := gbkDecode(raw); ok {
		return s
	}
	return strings.ToValidUTF8(string(raw), "\uFFFD")
}

// utf8Complete 判断 b 是否「完整且合法」的 UTF-8。
// incomplete=true 表示 b 停在一个多字节序列中间（需要更多字节才能判）。
func utf8Complete(b []byte) (ok bool, incomplete bool) {
	for len(b) > 0 {
		r, size := utf8.DecodeRune(b)
		if r == utf8.RuneError && size == 1 {
			if !utf8.FullRune(b) {
				return false, true
			}
			return false, false
		}
		b = b[size:]
	}
	return true, false
}

// incompleteTail 返回 b 里「合法前缀」的长度：b[tail:] 是一个被截断的
// 多字节序列。只在 utf8Complete 报 incomplete 时调用。
func incompleteTail(b []byte) int {
	for i := len(b) - 1; i >= 0 && i >= len(b)-3; i-- {
		if utf8.RuneStart(b[i]) && !utf8.FullRune(b[i:]) {
			return i
		}
	}
	return len(b)
}

const (
	decUnknown = iota // 尚未见到非 ASCII 字节
	decUTF8
	decGBK
)

// streamDecoder 是流式输出解码器。
//   ★ 状态只在「首个非 ASCII 字节」处定一次；ASCII 字节在两种编码下相同，
//     所以可以立即放行、不必等判定。
//   ★ 判定后不再回头：混合编码流里后半段换个编码属于畸形输入，按替换字符
//     显示（不崩、不吞行）。
type streamDecoder struct {
	mode int
	pend []byte // 尚未能解码的尾巴（≤3 字节）
}

// Write 吸收一块原始字节，返回**现在就能发出去**的 UTF-8 字节（可能为空）。
func (d *streamDecoder) Write(p []byte) []byte {
	if len(p) == 0 {
		return nil
	}
	d.pend = append(d.pend, p...)
	return d.drain(false)
}

// Flush 在流结束时调用：把留住的尾巴按已判定编码强行解出来。
func (d *streamDecoder) Flush() []byte {
	return d.drain(true)
}

// gbkCompletePrefix 返回 b 中「能组成完整 GBK 字符」的前缀长度。
//   ★ 不能只看末字节是否 ≥0x81：0xD0 既可能是首字节，也可能是上一字符的
//     第二个字节；按两字节配对从头扫才不会把 0xD6 0xD0 拆成两个半字符。
//   ★ CP936 的 0x80 是单字节（€），所以 <0x81 一律按单字节推进。
func gbkCompletePrefix(b []byte) int {
	n := 0
	for n < len(b) {
		if b[n] < 0x81 {
			n++
			continue
		}
		if n+1 >= len(b) {
			break // 半个字符：留到下一块
		}
		n += 2
	}
	return n
}

func (d *streamDecoder) drain(final bool) []byte {
	var out []byte
	for len(d.pend) > 0 {
		if d.mode == decUnknown {
			i := 0
			for i < len(d.pend) && d.pend[i] < 0x80 {
				i++
			}
			if i > 0 {
				out = append(out, d.pend[:i]...)
				d.pend = append([]byte(nil), d.pend[i:]...)
				continue
			}
			ok, incomplete := utf8Complete(d.pend)
			switch {
			case ok:
				d.mode = decUTF8
			case incomplete && !final:
				return out // 等下一块补齐
			default:
				d.mode = decGBK
			}
			continue
		}

		if d.mode == decUTF8 {
			ok, incomplete := utf8Complete(d.pend)
			if ok {
				out = append(out, d.pend...)
				d.pend = nil
				continue
			}
			if incomplete && !final {
				cut := incompleteTail(d.pend)
				out = append(out, d.pend[:cut]...)
				d.pend = append([]byte(nil), d.pend[cut:]...)
				return out
			}
			// 判成 UTF-8 之后又出现非法字节（混合/损坏流）：替换但保留可读部分。
			out = append(out, []byte(strings.ToValidUTF8(string(d.pend), "\uFFFD"))...)
			d.pend = nil
			continue
		}

		// decGBK
		cut := len(d.pend)
		if !final {
			cut = gbkCompletePrefix(d.pend)
		}
		if cut == 0 {
			return out
		}
		body := d.pend[:cut]
		s, ok := gbkDecode(body)
		if !ok {
			s = strings.ToValidUTF8(string(body), "\uFFFD")
		}
		out = append(out, []byte(s)...)
		d.pend = append([]byte(nil), d.pend[cut:]...)
	}
	return out
}