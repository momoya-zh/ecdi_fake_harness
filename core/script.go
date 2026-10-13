package main

// script.go —— 假 core 的"剧本"：模板文本 / 假工具 / 结果文本。
//
// ★ 语料同源于 protocol/fixtures/realistic/gen_realistic.py（同一批
//   中文技术文本 + 同款工具名），避免"第二份真相"：harness 的评估夹具与
//   假 core 讲的应该是同一个故事，否则两条路径的读数不可比。

import (
	"math/rand"
	"strings"
)

// ── 助手 Markdown 文本（长段落——wrap 路径的真实压力）─────────────

var assistantTexts = []string{
	`先说结论：**判据必须是严格大于零**，不能用 >= 0。边缘贴合（宽度恰为 0）时
交集是退化的线段，画出来不可见，但会让 PushClip/PopClip 照样进命令流——
剔除就白做了。具体推理链是：

1. Rect::Intersects(a, b) 要求交集宽和高都严格 > 0；
2. 边缘贴合时交集宽 = 0 ⇒ 判 false ⇒ 整棵子树跳过；
3. 如果放宽到 >= 0，边缘贴合的子树会发出零面积 clip + 全部绘制命令。

PaintContext::IsRectVisible 的注释里冻结了这个契约，不要动。`,
	`我查了一下滚动闪烁的来源，两个嫌疑都验证过了：

- **嫌疑一**：滚动条 SyncBars 每帧重设滑块位置——不是，位置没变时 SetText 同值
  仍会请求重绘，但那只是 HUD 标签，不是转录区。
- **嫌疑二**：SetContentOffset 的副作用链是 ApplyLayout → ClampOffset →
  SyncBars → Invalidate，每次滚动步进都整客户区失效——**是它**。

缓解方向有两个：脏区合帧（#21 记账项）或者接受现状。`,
	`DPI 基准的注入点在 Window::Run 之后、首次 PaintFrame 之前：
m_textMeasurer->Initialize(m_platformWindow->GetRenderContext())，
GDITextMeasurer 和 FreeTypeTextMeasurer 都从这里取窗口 DPI。测量链和渲染链
同基准这件事由 Phase 26 批一保证，跨屏漂移是另一个问题。

| 链路 | 基准来源 | 备注 |
|---|---|---|
| 测量 | 窗口 DPI（Initialize 注入） | Phase 26 批一 |
| 渲染 | 窗口 DPI（GetDpiForWindow） | GDI/GL 一致 |`,
	`测试结果：**305 通过、2 失败、307 总数**。失败的两个都集中在布局用例：

    [FAIL] Layout.StretchZeroChild.NotGivenMainAxisSize
      expected (80, 18) but got (0, 18)
    [FAIL] Layout.Padding.HardInsetDegenerate
      expected content origin (8, 8) but got (0, 0)

第一个是已知契约：stretch=0 的子布局不分配主轴尺寸，必须显式 SetSize。
第二个我怀疑是 padding 的硬 inset 语义和实现有出入。`,
	`这个结论我不采纳——它没有被独立复现。当时的读数是从一份旧静态库上跑出来的
（头文件形态停在旧版本，lib 构筑时间早于该批提交），跑的二进制不含剔除。

跨仓库验证必须先核对被验证方的版本指纹，这个教训已经写进收口记录。
正确的复现步骤我贴在下面：

    cmake --install build-msvc --config Debug --prefix third_party/ecdi
    cmake --build gui/build --config Release
    harness_gui.exe --replay ... --cull-probe --scroll-sweep 400`,
}

// ── 工具（名字同 gen_realistic.py）─────────────────────────────

type tool struct {
	Name string
	Args map[string]any
}

var toolNames = []string{"read_file", "grep_repo", "edit_file", "bash", "todo_write"}

func scriptTool(r *rand.Rand, index int) tool {
	name := toolNames[r.Intn(len(toolNames))]
	var args map[string]any
	switch name {
	case "read_file":
		args = map[string]any{
			"path":  []string{"docs/README.md", "ECDI/src/Widget/Widget.cpp", "protocol/PROTOCOL.md"}[r.Intn(3)],
			"limit": []int{60, 120, 240}[r.Intn(3)],
		}
	case "grep_repo":
		args = map[string]any{
			"pattern": []string{"IsRectVisible", "PushClip", "SetContentOffset", "Invalidate"}[r.Intn(4)],
			"glob":    []string{"ECDI/src/**/*.cpp", "docs/*.md"}[r.Intn(2)],
		}
	case "edit_file":
		args = map[string]any{
			"path": "docs/README.md",
			"line": 40 + r.Intn(260),
			"old":  []string{"待评审", "⬜ 记账"}[r.Intn(2)],
			"new":  []string{"已收口", "✅ 已闭合"}[r.Intn(2)],
		}
	case "bash":
		args = map[string]any{
			"command": []string{
				"cmake --build build-msvc --config Debug 2>&1 | tail -5",
				"python protocol/validate.py && echo GREEN",
				"ctest --test-dir build -R Layout --output-on-failure -j 8",
			}[r.Intn(3)],
		}
	case "todo_write":
		args = map[string]any{
			"items": []map[string]string{
				{"content": "核对剔除判据契约", "status": "completed"},
				{"content": "定位滚动闪烁来源", "status": "in_progress"},
			},
		}
	}
	return tool{Name: name, Args: args}
}

// ── 工作区相对路径（假工作区，不碰真实文件系统）────────────────

var logLines = []string{
	"[ RUN      ] Clip.Culling.BoundaryTouch",
	"[       OK ] Clip.Culling.BoundaryTouch (0 ms)",
	"[ RUN      ] ScrollView.ContentOffsetClamp",
	"  expected maxOffset 59350 but got 59348 (row gap rounding)",
	"Info: measurement cache hit ratio 94.2% (1297/1378)",
	"Warning: FreeType face cache miss for size 14.0",
	"Trace: PaintFrame build 0.058ms execute 6.9ms commands 168 (gdi)",
	"Trace: PaintFrame build 0.061ms execute 1.2ms commands 168 (gl)",
	"  at ECDI/src/Widget/ScrollView.cpp:128 SetContentOffset",
	"  at ECDI/src/Window/Window.cpp:139 PaintFrame initial clip seed",
	"- 旧实现：m_offsetY = max(0, min(offsetY, m_contentH - viewportH));",
	"+ 新实现：ClampOffset() 沿用 SetContentExtent 的原子顺序链；",
	"@@ -113,7 +113,9 @@ void ScrollView::SetContentOffset(int x, int y) {",
	"heap: widget pool 2000/2000 rows, 41.3 MB RSS, no growth over 500 frames",
}

func scriptToolResult(r *rand.Rand, name string) string {
	header := map[string]string{
		"read_file":  "阅读完成，摘录如下：",
		"grep_repo":  "在 ECDI/src 中命中以下位置（含上下文）：",
		"edit_file":  "替换完成，diff 如下：",
		"bash":       "命令输出：",
		"todo_write": "清单已更新。",
	}[name]

	roll := r.Float64()
	var n int
	switch {
	case roll < 0.30:
		return header + "\n（输出很短，无尾部日志）"
	case roll < 0.70:
		n = 12 + r.Intn(20)
	default:
		n = 40 + r.Intn(50)
	}
	var b strings.Builder
	b.WriteString(header)
	for i := 0; i < n; i++ {
		b.WriteString("\n")
		b.WriteString(logLines[r.Intn(len(logLines))])
	}
	return b.String()
}

// chunkText：按 rune 边界切成若干块（模拟流式 delta；不切坏 UTF-8）。
func chunkText(r *rand.Rand, text string, minChunk, maxChunk int) []string {
	runes := []rune(text)
	var out []string
	for i := 0; i < len(runes); {
		n := minChunk + r.Intn(maxChunk-minChunk+1)
		end := i + n
		if end > len(runes) {
			end = len(runes)
		}
		// 避免切在换行符前留下孤立的 \r
		for end < len(runes) && runes[end-1] == '\r' {
			end++
		}
		out = append(out, string(runes[i:end]))
		i = end
	}
	return out
}

func scriptAssistantText(r *rand.Rand) string {
	return assistantTexts[r.Intn(len(assistantTexts))]
}
