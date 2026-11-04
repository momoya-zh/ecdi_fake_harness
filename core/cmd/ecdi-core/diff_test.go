// diff_test.go —— unifiedDiff 的五个合同面（2026-11-05，ask 提案卡）。
//
// ★ 为什么这些断言值得钉：detail.diff 是**审阅数据**——"新文件显示为全加行"
//   与"完全相同显示为空"不是格式偏好，是审阅者信任的根基：显示成"有删除"
//   的新文件会让人以为要覆盖别的东西。
package main

import (
	"fmt"
	"strings"
	"testing"
)

func TestUnifiedDiff_NewFileAllAdditions(t *testing.T) {
	body := unifiedDiff("", "line1\nline2\nline3\n")
	if !strings.HasPrefix(body, "@@ -0,0 +1,3 @@\n") {
		t.Fatalf("新文件的 hunk 头应为 @@ -0,0 +1,3 @@，got:\n%s", body)
	}
	for _, l := range strings.Split(body, "\n")[1:] {
		if !strings.HasPrefix(l, "+") {
			t.Fatalf("新文件的每个正文行都应是 +，got %q", l)
		}
	}
}

func TestUnifiedDiff_ReplacementLocalized(t *testing.T) {
	oldText := "a\nb\nc\n"
	newText := "a\nB1\nB2\nc\n" // 中段 = ["b"]→["B1","B2"]，真替换不是纯插入
	body := unifiedDiff(oldText, newText)
	lines := strings.Split(body, "\n")
	// context=3 覆盖全文 ⇒ hunk 含两侧的 a/c 上下文行，头是 @@ -1,3 +1,4 @@。
	if lines[0] != "@@ -1,3 +1,4 @@" {
		t.Fatalf("替换 hunk 头不符，got %q", lines[0])
	}
	if !strings.Contains(body, " a") || !strings.Contains(body, " c") {
		t.Fatalf("上下文行（a/c）应带空格前缀保留，got:\n%s", body)
	}
	if !strings.Contains(body, "-b") || !strings.Contains(body, "+B1") || !strings.Contains(body, "+B2") {
		t.Fatalf("应含 -/+ 替换内容，got:\n%s", body)
	}
	if !strings.Contains(body, " a") {
		t.Fatalf("上下文行应带空格前缀，got:\n%s", body)
	}
}

func TestUnifiedDiff_IdenticalIsEmpty(t *testing.T) {
	s := "same\ncontent\n"
	if body := unifiedDiff(s, s); body != "" {
		t.Fatalf("相同文本必须返回空串（GUI 显示无变化），got:\n%s", body)
	}
}

func TestUnifiedDiff_TruncationMarked(t *testing.T) {
	// 500 行改动 > diffMaxLines(400)：产出必须封顶且带显式截断标注。
	var b strings.Builder
	for i := 0; i < 500; i++ {
		fmt.Fprintf(&b, "line %d\n", i)
	}
	newText := b.String() + "appended\n"
	body := unifiedDiff("", newText)
	n := strings.Count(body, "\n") + 1
	if n > diffMaxLines+2 { // 头行 + 截断标注行
		t.Fatalf("产出 %d 行，应封顶在 %d 附近", n, diffMaxLines)
	}
	if !strings.Contains(body, "diff 截断") {
		t.Fatalf("截断必须显式标注，got tail:\n%s", body[strings.LastIndex(body, "\n")-40:])
	}
}

func TestUnifiedDiff_OversizeWindowCoarse(t *testing.T) {
	// 中段超窗（>1500 行）：不做行对齐，粗展示两侧头部 + 截断标注——
	// 不得假装对齐，也不得卡死审批。
	var o, n strings.Builder
	for i := 0; i < 3000; i++ {
		fmt.Fprintf(&o, "old %d\n", i)
		fmt.Fprintf(&n, "new %d\n", i)
	}
	body := unifiedDiff(o.String(), n.String())
	if !strings.Contains(body, "超过 1500 行窗口") {
		t.Fatalf("超窗必须标注窗口上限，got head:\n%s", body[:120])
	}
	if strings.Count(body, "\n") > 200 {
		t.Fatalf("粗展示应只出头部（每侧 %d 行），got %d 行", diffCoarseLines, strings.Count(body, "\n"))
	}
}

func TestUnifiedDiff_ContextMergesCloseHunks(t *testing.T) {
	// 两处改动相距 ≤ 2×context ⇒ 并入同一个 hunk（unified diff 惯例：
	// 两侧各 3 行上下文覆盖得了间隙）；相距 > 2×context ⇒ 分开。
	// ★ 计数按「以 @@ 开头的行」——每条 hunk 头形如 "@@ -a,b +c,d @@"，
	//   对 "@@" 串计数每头会得 2。
	hunks := func(body string) int {
		n := 0
		for _, l := range strings.Split(body, "\n") {
			if strings.HasPrefix(l, "@@ ") {
				n++
			}
		}
		return n
	}
	oldText := "a\nb\nc\nd\ne\nf\ng\n"
	newText := "A\nb\nc\nd\ne\nF\ng\n" // 间隔 4 行（b..e）
	if n := hunks(unifiedDiff(oldText, newText)); n != 1 {
		t.Fatalf("相距 4 行的两处改动应合并为一个 hunk（got %d）:\n%s", n, unifiedDiff(oldText, newText))
	}

	oldText = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\n"
	newText = "A\nb\nc\nd\ne\nf\ng\nh\ni\nj\nK\n" // 间隔 9 行
	if n := hunks(unifiedDiff(oldText, newText)); n != 2 {
		t.Fatalf("相距 9 行（>2×context）应拆为两个 hunk（got %d）", n)
	}
}
