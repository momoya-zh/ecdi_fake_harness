// diff.go —— ask 提案卡的 unified diff 计算（2026-11-05，协议 v5 §3.6 可选 detail）。
//
// 为什么 diff 由 core 算而不是 GUI：① 当前磁盘内容只有 core 知道（GUI 只看到
// args——「新内容」，没有「旧内容」）；② 2MB 的整文件内容不该为了审阅进 GUI
// 进程；③ GUI 侧没有 diff 库，为它写一个等于把同一逻辑维护两份。
//
// ★ 本文件是**只读**路径：write_file 的旧内容在**批准之前**读（审阅时刻看到的
//   必须就是将要发生的事），真正的写入仍在 ask 门之后（tools.go toolWriteFile）。
//
// 规模策略（不掩盖）：
//   - 公共前/后缀先整表修剪（O(n)，正确无窗口）——代码改动几乎总是落在小中段；
//   - 中段任一侧超 diffLCSWindow ⇒ **不做**行对齐（O(n·m) 会把审批卡死），
//     改为「两侧头部 + 截断标注」的粗展示——诚实标注，不假装对齐了；
//   - 产出超 diffMaxLines ⇒ 截断 + 标注。所有截断都显式，静默截断会让
//     审阅者拿着半份 diff 批一个没看全的提案（tools.go 头注同族纪律）。
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	diffMaxLines    = 400  // detail.diff 产出行数上限
	diffContext     = 3    // hunk 上下文行数
	diffLCSWindow   = 1500 // 中段超此行数不做 LCS（粗展示）
	diffCoarseLines = 60   // 粗展示每侧头部行数
)

// unifiedDiff：行级 unified diff 的 **hunk 体**（不含 `---`/`+++` 头——路径由
// 调用方拼）。两文本逐行相同 ⇒ 返回空串（调用方显示「内容无变化」）。
func unifiedDiff(oldText, newText string) string {
	a, b := splitLines(oldText), splitLines(newText)

	// 公共前/后缀**整表**修剪（O(n)）定位差异区；随后**收回 diffContext 行**
	// 作为 hunk 的上下文——审阅者需要改动点前后的原文，剪光了就没有上下文可示。
	p, sfx := 0, 0
	for p < len(a) && p < len(b) && a[p] == b[p] {
		p++
	}
	for sfx < len(a)-p && sfx < len(b)-p && a[len(a)-1-sfx] == b[len(b)-1-sfx] {
		sfx++
	}
	if p == len(a) && p == len(b) {
		return "" // 完全相同（含两侧皆空）
	}
	pKeep := p - diffContext
	if pKeep < 0 {
		pKeep = 0
	}
	sKeep := sfx - diffContext
	if sKeep < 0 {
		sKeep = 0
	}
	am, bm := a[pKeep:len(a)-sKeep], b[pKeep:len(b)-sKeep]

	var out []string
	if len(am) > diffLCSWindow || len(bm) > diffLCSWindow {
		// 差异区超窗：整文件重写一类的场景。不做行对齐（那是拿审批延迟换精度），
		// 如实给出两侧头部——审阅者至少能看到「要变成什么样」。
		out = append(out, fmt.Sprintf("@@ 差异区 %d → %d 行，超过 %d 行窗口，仅示头部",
			len(am), len(bm), diffLCSWindow))
		if len(am) > diffCoarseLines {
			am = am[:diffCoarseLines]
		}
		if len(bm) > diffCoarseLines {
			bm = bm[:diffCoarseLines]
		}
		for _, l := range am {
			out = append(out, "-"+l)
		}
		for _, l := range bm {
			out = append(out, "+"+l)
		}
		out = append(out, "…（diff 截断）")
		return strings.Join(out, "\n")
	}

	ops := diffOps(am, bm)

	// 分组为 hunk：从每个改动点向后扩，相同行间隙 > 2×context 封组；
	// 组两端再向外扩 context 行（保留的上下文行通常已够，此扩是余量）。
	var groups [][2]int // [start,end) 于 ops 序列，含 context
	for i := 0; i < len(ops); {
		if ops[i].kind == ' ' {
			i++
			continue
		}
		end := i + 1
		for j := i; j < len(ops); j++ {
			if ops[j].kind != ' ' {
				end = j + 1
				continue
			}
			run := 0
			for k := j; k < len(ops) && ops[k].kind == ' '; k++ {
				run++
			}
			if run > 2*diffContext {
				break
			}
			end = j + run
		}
		gs := i - diffContext
		if gs < 0 {
			gs = 0
		}
		ge := end + diffContext
		if ge > len(ops) {
			ge = len(ops)
		}
		groups = append(groups, [2]int{gs, ge})
		i = ge
	}

	emitted := 0
	for _, g := range groups {
		var body []string
		aStart, bStart := 0, 0
		aCount, bCount := 0, 0
		for _, op := range ops[g[0]:g[1]] {
			switch op.kind {
			case ' ':
				if aCount == 0 && bCount == 0 {
					aStart, bStart = op.ai+1+pKeep, op.bi+1+pKeep
				}
				aCount++
				bCount++
			case '-':
				if aCount == 0 && bCount == 0 {
					aStart, bStart = op.ai+1+pKeep, op.bi+1+pKeep
				}
				aCount++
			case '+':
				if aCount == 0 && bCount == 0 {
					aStart, bStart = op.ai+1+pKeep, op.bi+1+pKeep
				}
				bCount++
			}
			body = append(body, string(op.kind)+op.text)
		}
		// ★ 纯增/纯删的 start 用 after-line 语义（unified 惯例）：旧侧零行时
		//   aStart = 插入点之前已有的行数（新文件 = 0 ⇒ `@@ -0,0 +1,N @@`）。
		if aCount == 0 {
			aStart = pKeep + ops[g[0]].ai
		}
		if bCount == 0 {
			bStart = pKeep + ops[g[0]].bi
		}
		head := fmt.Sprintf("@@ -%d,%d +%d,%d @@", aStart, aCount, bStart, bCount)
		for _, l := range append([]string{head}, body...) {
			if emitted >= diffMaxLines {
				out = append(out, fmt.Sprintf("…（diff 截断：超过 %d 行）", diffMaxLines))
				return strings.Join(out, "\n")
			}
			out = append(out, l)
			emitted++
		}
	}
	return strings.Join(out, "\n")
}

// splitLines：按 \n 切行，去掉末尾换行产生的空尾元素（"a\n" = 1 行，不是 2 行）。
func splitLines(s string) []string {
	if s == "" {
		return nil
	}
	lines := strings.Split(s, "\n")
	if lines[len(lines)-1] == "" {
		lines = lines[:len(lines)-1]
	}
	return lines
}

type diffOp struct {
	kind byte   // ' ' 相同 / '-' 旧删 / '+' 新增
	text string // 行内容（不含前缀字符）
	ai   int    // 旧文本行下标（0 起）
	bi   int    // 新文本行下标（0 起）
}

// diffOps：LCS 动态规划，产出对齐脚本。规模受 diffLCSWindow 约束
// （1500×1500 的 int32 表 ≈ 9MB，一次性、非热路径——可接受）。
func diffOps(a, b []string) []diffOp {
	n, m := len(a), len(b)
	dp := make([]int32, (n+1)*(m+1))
	w := m + 1
	for i := n - 1; i >= 0; i-- {
		for j := m - 1; j >= 0; j-- {
			var v int32
			if a[i] == b[j] {
				v = dp[(i+1)*w+(j+1)] + 1
			} else if dp[(i+1)*w+j] >= dp[i*w+j+1] {
				v = dp[(i+1)*w+j]
			} else {
				v = dp[i*w+j+1]
			}
			dp[i*w+j] = v
		}
	}

	ops := make([]diffOp, 0, n+m)
	i, j := 0, 0
	for i < n && j < m {
		switch {
		case a[i] == b[j]:
			ops = append(ops, diffOp{kind: ' ', text: a[i], ai: i, bi: j})
			i++
			j++
		case dp[(i+1)*w+j] >= dp[i*w+j+1]:
			ops = append(ops, diffOp{kind: '-', text: a[i], ai: i, bi: j})
			i++
		default:
			ops = append(ops, diffOp{kind: '+', text: b[j], ai: i, bi: j})
			j++
		}
	}
	for ; i < n; i++ {
		ops = append(ops, diffOp{kind: '-', text: a[i], ai: i, bi: j})
	}
	for ; j < m; j++ {
		ops = append(ops, diffOp{kind: '+', text: b[j], ai: i, bi: j})
	}
	return ops
}

// ── ask 的结构化提案（协议 v5 §3.6 可选 detail）─────────────────

// askDetail：为一次 ask 组装 detail。**问句文本（askPrompt）不动**——detail 是
// 叠加的审阅数据，问句仍是单行摘要与回落路径（GUI 拿不到/看不懂 detail 时显示它）。
//
// 返回 nil = 不带该字段（未知工具「看不懂就不发」——比发一个残缺的诚实）。
func askDetail(name string, args map[string]any, root string) map[string]any {
	switch name {
	case "write_file":
		return writeFileDetail(args, root)
	case "edit_file":
		return editFileDetail(args)
	case "run_command":
		return runCommandDetail(args, root)
	}
	return nil
}

// writeFileDetail：diff 的旧侧 = **批准前**的磁盘内容（不存在 = 新文件，标准
// `--- /dev/null` 头）。读不了（权限/目录/二进制）⇒ 不带 diff——审阅回落问句，
// 不编一个假 diff 出来。内容与现状完全一致 ⇒ diff 显示「无变化」而不是空卡。
func writeFileDetail(args map[string]any, root string) map[string]any {
	p, _ := args["path"].(string)
	content, hasContent := args["content"].(string)
	d := map[string]any{"tool": "write_file", "path": filepath.ToSlash(p)}
	if !hasContent {
		return d
	}
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return d // 越界等会在执行时如实报错；这里不预演错误文本
	}
	body := ""
	if data, rerr := os.ReadFile(abs); rerr == nil && !hasNUL(data) {
		body = unifiedDiff(string(data), content)
	} else if rerr != nil && os.IsNotExist(rerr) {
		body = unifiedDiff("", content)
	}
	if body != "" {
		d["diff"] = fmt.Sprintf("--- a/%s\n+++ b/%s\n", filepath.ToSlash(p), filepath.ToSlash(p)) + body
	} else {
		d["diff"] = "（内容无变化）"
	}
	return d
}

// editFileDetail：替换**片段**的 diff（old_string→new_string）——那正是本次
// 提议的改动本体；整文件上下文已在 tool_call 块的 args 里，不重复搬运。
func editFileDetail(args map[string]any) map[string]any {
	p, _ := args["path"].(string)
	d := map[string]any{"tool": "edit_file", "path": filepath.ToSlash(p)}
	if replaceAll, _ := args["replace_all"].(bool); replaceAll {
		d["replaceAll"] = true
	}
	oldStr, ok1 := args["old_string"].(string)
	newStr, ok2 := args["new_string"].(string)
	if ok1 && ok2 && oldStr != "" {
		if body := unifiedDiff(oldStr, newStr); body != "" {
			d["diff"] = fmt.Sprintf("--- a/%s\n+++ b/%s\n", filepath.ToSlash(p), filepath.ToSlash(p)) + body
		} else {
			d["diff"] = "（替换前后相同）"
		}
	}
	return d
}

// runCommandDetail：没有 diff，但**完整命令**（不截断——问句截 160 字符会让
// `&&` 后面那半看不见，恰恰是最危险的一半）与作用域三件必须摆出来。
func runCommandDetail(args map[string]any, root string) map[string]any {
	command, _ := args["command"].(string)
	timeout := runCmdTimeoutS
	if v, ok := args["timeout_seconds"].(float64); ok && v > 0 {
		timeout = int(v)
	}
	if timeout > runCmdMaxTimeS {
		timeout = runCmdMaxTimeS
	}
	if timeout < 1 {
		timeout = 1
	}
	d := map[string]any{
		"tool":           "run_command",
		"command":        command,
		"cwd":            filepath.ToSlash(root),
		"timeoutSeconds": timeout,
		"note":           getSandboxConfig().sandboxNote(timeout),
	}
	// v6：后台作业的 note 第二行换成作业预算（60 秒的 tool 超时对它不适用）。
	if bg, _ := args["background"].(bool); bg {
		d["background"] = true
		d["note"] = getSandboxConfig().sandboxBackgroundNote()
	}
	return d
}

// delegationAskDetail：task 委派的 detail（agent.go prepareDelegation 调用）。
// 工具集写死三件只读 = toolDefsFor(depth>=1) 裁剪后的表——「子代理看不见 task」
// 的实现就是从那张表来的，detail 与它必须同源（两处各写一份迟早漂移，注释钉死）。
func delegationAskDetail(name, task string, o agentOpts) map[string]any {
	return map[string]any{
		"tool":           "task",
		"task":           task,
		"name":           name,
		"tools":          []string{"read_file", "list_dir", "grep"},
		"maxTurns":       o.maxTurns,
		"timeoutSeconds": int(o.timeout.Seconds()),
	}
}
