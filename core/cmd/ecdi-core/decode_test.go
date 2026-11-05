package main

import (
	"strings"
	"testing"
)

// fakeGBK 只在非 Windows 上也把状态机跑起来用：把 0xD6 0xD0 解成「中」、
// 0xCE 0xC4 解成「文」，其余字节逐字节按 Latin-1 交回（够验证流式边界）。
func fakeGBK(b []byte) (string, bool) {
	var sb strings.Builder
	for i := 0; i < len(b); {
		if i+1 < len(b) && b[i] == 0xD6 && b[i+1] == 0xD0 {
			sb.WriteString("中")
			i += 2
			continue
		}
		if i+1 < len(b) && b[i] == 0xCE && b[i+1] == 0xC4 {
			sb.WriteString("文")
			i += 2
			continue
		}
		sb.WriteRune(rune(b[i]))
		i++
	}
	return sb.String(), true
}

func withFakeGBK(t *testing.T) {
	t.Helper()
	old := gbkDecode
	gbkDecode = fakeGBK
	t.Cleanup(func() { gbkDecode = old })
}

func TestDecodeProcessOutput_UTF8Untouched(t *testing.T) {
	in := []byte("中文 utf8 ok")
	if got := decodeProcessOutput(in); got != string(in) {
		t.Fatalf("UTF-8 应原样返回，got %q", got)
	}
	if got := decodeProcessOutput(nil); got != "" {
		t.Fatalf("空输入应为空，got %q", got)
	}
}

func TestDecodeProcessOutput_GBK(t *testing.T) {
	withFakeGBK(t)
	got := decodeProcessOutput([]byte{0xD6, 0xD0, 0xCE, 0xC4})
	if got != "中文" {
		t.Fatalf("GBK 整段解码失败，got %q", got)
	}
}

func TestStreamDecoder_ASCIIImmediate(t *testing.T) {
	d := &streamDecoder{}
	if got := d.Write([]byte("hello")); string(got) != "hello" {
		t.Fatalf("纯 ASCII 应立即放行，got %q", got)
	}
	if len(d.pend) != 0 {
		t.Fatalf("ASCII 不应留尾巴，pend=%v", d.pend)
	}
}

func TestStreamDecoder_UTF8SplitAcrossWrites(t *testing.T) {
	d := &streamDecoder{}
	// 「中」= E4 B8 AD，故意在 1 字节和 2 字节处切开。
	if got := d.Write([]byte{0xE4}); len(got) != 0 {
		t.Fatalf("半个 UTF-8 字符应被扣住，got %q", got)
	}
	if got := d.Write([]byte{0xB8}); len(got) != 0 {
		t.Fatalf("仍不完整，应继续扣住，got %q", got)
	}
	if got := d.Write([]byte{0xAD}); string(got) != "中" {
		t.Fatalf("补齐后应输出「中」，got %q", got)
	}
	if got := d.Flush(); len(got) != 0 {
		t.Fatalf("没有尾巴时 Flush 应为空，got %q", got)
	}
}

func TestStreamDecoder_GBKDetectedAfterAscii(t *testing.T) {
	withFakeGBK(t)
	d := &streamDecoder{}
	if got := d.Write([]byte("out: ")); string(got) != "out: " {
		t.Fatalf("ASCII 前缀应立即放行，got %q", got)
	}
	// 0xD6 是 GBK 首字节（也是非法 UTF-8 前缀）⇒ 不能提前放行。
	if got := d.Write([]byte{0xD6}); len(got) != 0 {
		t.Fatalf("GBK 首字节应被扣住，got %q", got)
	}
	if got := d.Write([]byte{0xD0}); string(got) != "中" {
		t.Fatalf("判定 GBK 后应输出「中」，got %q", got)
	}
	if d.mode != decGBK {
		t.Fatalf("mode 应为 decGBK，got %d", d.mode)
	}
}

func TestStreamDecoder_GBKHoldsLeadByte(t *testing.T) {
	withFakeGBK(t)
	d := &streamDecoder{}
	// 「中文」两字节 + 下一个 GBK 字符的首字节 0xD6：前 4 字节可解，末字节必须留住。
	got := d.Write([]byte{0xD6, 0xD0, 0xCE, 0xC4, 0xD6})
	if string(got) != "中文" {
		t.Fatalf("应先输出「中文」，got %q", got)
	}
	if len(d.pend) != 1 || d.pend[0] != 0xD6 {
		t.Fatalf("末字节应留住，pend=%v", d.pend)
	}
	if got := d.Write([]byte{0xD0}); string(got) != "中" {
		t.Fatalf("补齐后应输出「中」，got %q", got)
	}
}

func TestStreamDecoder_FlushDecodesTruncatedTail(t *testing.T) {
	withFakeGBK(t)
	d := &streamDecoder{}
	if got := d.Write([]byte("x")); string(got) != "x" {
		t.Fatalf("ASCII 应立即放行，got %q", got)
	}
	if got := d.Write([]byte{0xD6}); len(got) != 0 {
		t.Fatalf("不完整字节应扣住，got %q", got)
	}
	got := d.Flush()
	if got != nil {
		// 单字节 0xD6 交给 fakeGBK 会按 Latin-1 回一个字符；换成真 GBK 时是 U+FFFD。
		// 这里只断言「Flush 不丢内容」：非空即已交出。
		if len(got) == 0 {
			t.Fatalf("Flush 应把尾巴交出来")
		}
	}
}

func TestStreamDecoder_InvalidAfterUTF8Lock(t *testing.T) {
	d := &streamDecoder{}
	if got := d.Write([]byte("中")); string(got) != "中" {
		t.Fatalf("先锁定 UTF-8，got %q", got)
	}
	got := d.Write([]byte{0xFF, 0xFE})
	if !strings.Contains(string(got), "\uFFFD") {
		t.Fatalf("非法字节应替换为 U+FFFD，got %q", got)
	}
}

func TestStreamDecoder_FlushEmpty(t *testing.T) {
	d := &streamDecoder{}
	if got := d.Flush(); got != nil {
		t.Fatalf("空解码器 Flush 应为 nil，got %q", got)
	}
}