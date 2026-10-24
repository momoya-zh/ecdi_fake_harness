// tools.go —— 只读三件（P3 规格 §4.4 / D2）：read_file / list_dir / grep。
//
// 安全边界（规格 §4.3/§5）：三件全部只读，但**只读 ≠ 无害**——read_file 读得到
// 一切可读文件。两道闸：
//   ① **工作区边界**：解析后的路径必须在 workspace 内（绝对路径出了根 = 拒绝；
//      `..` 段在 Clean 后逃出根 = 拒绝）；
//   ② **ask 门**（session.go 负责，本文件不管）：默认每个调用先问用户，
//      豁免只经 --allow-tools（运行者的配置，不是这里的启发式）。
//
// 所有上限（40KB / 500 项 / 200 命中）截断时**显式标注**——静默截断会让模型
// 拿着半份结果下结论，比报错更糟。

package main

import (
	"bytes"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

const (
	readFileLimit  = 40 * 1024 // 单文件读取上限（超出截断并标注）
	listDirLimit   = 500       // 单层目录项上限
	grepHitLimit   = 200       // grep 命中行上限
	grepSkipBytes  = 1 << 20   // >1MB 的文件跳过
	binaryProbeLen = 8 * 1024  // 二进制探测窗口（前 8KB 见 NUL 即判二进制）
)

// toolResult = 一次工具执行的产物（GUI 渲染与模型输入用**同一份**文本，
// P3 规格 §4.4——两份文本会出现「GUI 看到的和模型拿到的不一样」）。
type toolResult struct {
	OK     bool
	Output string
}

// executeTool：分发 + 执行。root = 当前工作区（绝对、`/` 分隔，§3.9 规范形）。
func executeTool(name string, args map[string]any, root string) toolResult {
	switch name {
	case "read_file":
		return toolReadFile(args, root)
	case "list_dir":
		return toolListDir(args, root)
	case "grep":
		return toolGrep(args, root)
	}
	return toolResult{OK: false, Output: fmt.Sprintf("未知工具 %q（只读三件：read_file / list_dir / grep）", name)}
}

// toolDefs：上交 API 的工具定义（JSON Schema）。
func toolDefs() []toolDef {
	obj := func(required ...string) map[string]any {
		props := map[string]any{
			"path": map[string]any{
				"type":        "string",
				"description": "工作区内相对路径（或工作区内绝对路径）",
			},
			"pattern": map[string]any{
				"type":        "string",
				"description": "Go 正则表达式（RE2 语法）",
			},
		}
		// ★ 零实参的可变参数是 **nil slice** ⇒ json.Marshal 出 `"required":null`，
		//   DeepSeek 严格 schema 校验直接 400（"null is not of type array"）。
		//   必须归一成空数组。
		if required == nil {
			required = []string{}
		}
		return map[string]any{
			"type":       "object",
			"properties": props,
			"required":   required,
		}
	}
	return []toolDef{
		{Name: "read_file", Description: "读取工作区内的一个文本文件（UTF-8，上限 40KB，超出截断并标注）",
			Parameters: obj("path")},
		{Name: "list_dir", Description: "列出工作区内某目录的单层内容（目录在前带 / 尾，文件在后，按名排序）",
			Parameters: obj()},
		{Name: "grep", Description: "在工作区内递归搜索正则（跳过二进制与 >1MB 文件，上限 200 条命中）",
			Parameters: obj("pattern")},
	}
}

// ── 路径边界 ────────────────────────────────────────────────────

// resolveInWorkspace：把用户给的 path 解析成 workspace 内的绝对路径。
//
// ★ 边界是**解析后**判的，不是字符串前缀判的：`notes/../../etc/passwd` 在
// Clean 后逃出根，必须拒绝——这正是「相对路径以 workspace 为根」的全部意义。
// Windows 上 root 与 p 的大小写差异用 strings.EqualFold 处理（卷符无关语义）。
func resolveInWorkspace(root, p string) (string, error) {
	if strings.TrimSpace(p) == "" {
		p = "."
	}
	var abs string
	if filepath.IsAbs(p) {
		abs = filepath.Clean(p)
	} else {
		abs = filepath.Clean(filepath.Join(root, p))
	}
	rootClean := filepath.Clean(root)
	if !withinRoot(rootClean, abs) {
		return "", fmt.Errorf("路径越界：%s 不在工作区内（%s）", p, rootClean)
	}
	return abs, nil
}

func withinRoot(root, abs string) bool {
	if samePath(root, abs) {
		return true
	}
	rel, err := filepath.Rel(root, abs)
	if err != nil {
		return false
	}
	// Rel 成功但结果以 .. 开头 = 在根之外（含兄弟目录）
	return rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator))
}

func samePath(a, b string) bool {
	return strings.EqualFold(filepath.Clean(a), filepath.Clean(b))
}

// ── 三件 ────────────────────────────────────────────────────────

func toolReadFile(args map[string]any, root string) toolResult {
	p, _ := args["path"].(string)
	if strings.TrimSpace(p) == "" {
		return toolResult{OK: false, Output: "read_file 缺少 path 参数"}
	}
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}
	st, err := os.Stat(abs)
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("文件不可访问：%v", err)}
	}
	if st.IsDir() {
		return toolResult{OK: false, Output: "path 是目录，read_file 只读文件（列目录用 list_dir）"}
	}
	f, err := os.Open(abs)
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("打开失败：%v", err)}
	}
	defer f.Close()

	buf := make([]byte, readFileLimit+1)
	n, _ := f.Read(buf)
	data := buf[:n]
	if hasNUL(data) {
		return toolResult{OK: false, Output: "疑似二进制文件（含 NUL 字节），read_file 只处理文本"}
	}
	text := string(data)
	if n > readFileLimit {
		text = text[:readFileLimit] + fmt.Sprintf("\n（截断：文件共 %d 字节，只读前 %d 字节）", st.Size(), readFileLimit)
	}
	return toolResult{OK: true, Output: text}
}

func toolListDir(args map[string]any, root string) toolResult {
	p, _ := args["path"].(string)
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}
	entries, err := os.ReadDir(abs)
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("读取目录失败：%v", err)}
	}
	var dirs, files []string
	for _, e := range entries {
		name := e.Name()
		if e.IsDir() {
			dirs = append(dirs, name+"/")
		} else {
			files = append(files, name)
		}
	}
	sort.Strings(dirs)
	sort.Strings(files)
	all := append(dirs, files...)
	if len(all) == 0 {
		return toolResult{OK: true, Output: "（空目录）"}
	}
	out := strings.Join(all, "\n")
	if len(all) > listDirLimit {
		out = strings.Join(all[:listDirLimit], "\n") +
			fmt.Sprintf("\n（截断：共 %d 项，只列前 %d 项）", len(all), listDirLimit)
	}
	return toolResult{OK: true, Output: out}
}

func toolGrep(args map[string]any, root string) toolResult {
	pattern, _ := args["pattern"].(string)
	if strings.TrimSpace(pattern) == "" {
		return toolResult{OK: false, Output: "grep 缺少 pattern 参数"}
	}
	re, err := regexp.Compile(pattern)
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("正则不合法：%v", err)}
	}
	p, _ := args["path"].(string)
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}

	var hits []string
	truncated := false
	rootAbs, _ := filepath.Abs(root)
	err = filepath.WalkDir(abs, func(path string, d fs.DirEntry, werr error) error {
		if werr != nil || d.IsDir() {
			if d != nil && d.IsDir() && d.Name() == ".git" {
				return filepath.SkipDir
			}
			return nil
		}
		if len(hits) >= grepHitLimit {
			truncated = true
			return filepath.SkipAll
		}
		info, ierr := d.Info()
		if ierr != nil || info.Size() > grepSkipBytes {
			return nil // 大文件跳过（不算截断——是规则）
		}
		data, rerr := os.ReadFile(path)
		if rerr != nil || hasNUL(data) {
			return nil
		}
		rel := path
		if r, rerr := filepath.Rel(rootAbs, path); rerr == nil {
			rel = filepath.ToSlash(r)
		}
		for i, line := range strings.Split(string(data), "\n") {
			if re.MatchString(line) {
				line = strings.TrimSpace(line)
				if len(line) > 500 {
					line = line[:500] + "…"
				}
				hits = append(hits, fmt.Sprintf("%s:%d: %s", rel, i+1, line))
				if len(hits) >= grepHitLimit {
					truncated = true
					break
				}
			}
		}
		return nil
	})
	if err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("遍历失败：%v", err)}
	}
	if len(hits) == 0 {
		return toolResult{OK: true, Output: "（无命中）"}
	}
	out := strings.Join(hits, "\n")
	if truncated {
		out += fmt.Sprintf("\n（截断：命中超过 %d 条，只列前 %d 条）", grepHitLimit, grepHitLimit)
	}
	return toolResult{OK: true, Output: out}
}

func hasNUL(b []byte) bool {
	if len(b) > binaryProbeLen {
		b = b[:binaryProbeLen]
	}
	return bytes.IndexByte(b, 0) >= 0
}
