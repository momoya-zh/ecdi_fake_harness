// tools.go —— 六件：只读三件（P3 规格 §4.4 / D2）+ 写入/执行三件（2026-11-04）。
//
//	只读：read_file / list_dir / grep
//	改变世界：write_file / edit_file / run_command
//
// 安全边界（规格 §4.3/§5）：只读 ≠ 无害（read_file 读得到一切可读文件），
// 写入/执行更是**直接动手**。三道闸：
//   ① **工作区边界**：解析后的路径必须在 workspace 内（绝对路径出了根 = 拒绝；
//      `..` 段在 Clean 后逃出根 = 拒绝；符号链接/junction 按**真实路径**再判一次）；
//   ② **ask 门**（session.go 负责，本文件不管）：只读三件是 `--allow-tools` 的
//      **默认豁免值**；write_file / edit_file / run_command **不在默认值里**，
//      ⇒ 逐次过门。把写/执行塞进默认值等于把 ask 门废掉，不要做；
//   ③ **子进程环境**：`run_command` 继承进程环境，但 **剔除密钥**
//      （ECDI_CORE_KEY / ECDI_PROVIDER_*_KEY）——密钥走启动环境层是本项目的
//      既定决定（P3 规格 D5），一条 `env`/`set` 就能把它印到模型眼前，
//      那等于绕开这条决定。
//
// 所有上限（40KB / 500 项 / 200 命中 / 2MB 写入 / 24KB 输出）截断时**显式标注**
// ——静默截断会让模型拿着半份结果下结论，比报错更糟。

package main

import (
	"bytes"
	"context"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"time"
)

const (
	readFileLimit  = 40 * 1024 // 单文件读取上限（超出截断并标注）
	listDirLimit   = 500       // 单层目录项上限
	grepHitLimit   = 200       // grep 命中行上限
	grepSkipBytes  = 1 << 20   // >1MB 的文件跳过
	binaryProbeLen = 8 * 1024  // 二进制探测窗口（前 8KB 见 NUL 即判二进制）

	// ── 写入/执行三件（2026-11-04）──
	writeFileLimit = 2 << 20  // 单次写入上限 2MB（再大就不是"改代码"了，是搬数据）
	editFileLimit  = 1 << 20  // 可编辑文件上限 1MB（readFileLimit 的 40KB 对编辑太窄）
	runCmdOutLimit = 24 << 10 // 命令输出上限；超出保留**头尾各半**（错误多在尾部）
	runCmdTimeoutS = 60       // 命令默认超时（秒）
	runCmdMaxTimeS = 300      // 命令超时上限（秒）——再长该用后台任务，不是卡住一轮对话
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
	case "write_file":
		return toolWriteFile(args, root)
	case "edit_file":
		return toolEditFile(args, root)
	case "run_command":
		return toolRunCommand(args, root)
	}
	return toolResult{OK: false, Output: fmt.Sprintf(
		"未知工具 %q（可用：read_file / list_dir / grep / write_file / edit_file / run_command）", name)}
}

// toolDefs：上交 API 的工具定义（JSON Schema）。
func toolDefs() []toolDef {
	str := func(desc string) map[string]any {
		return map[string]any{"type": "string", "description": desc}
	}
	obj := func(props map[string]any, required ...string) map[string]any {
		// ★ 零实参的可变参数是 **nil slice** ⇒ json.Marshal 出 `"required":null`，
		//   DeepSeek 严格 schema 校验直接 400（"null is not of type array"）。
		//   必须归一成空数组。
		if required == nil {
			required = []string{}
		}
		return map[string]any{"type": "object", "properties": props, "required": required}
	}
	pathProp := func() map[string]any { return str("工作区内相对路径（或工作区内绝对路径）") }
	return []toolDef{
		{Name: "read_file", Description: "读取工作区内的一个文本文件（UTF-8，上限 40KB，超出截断并标注）",
			Parameters: obj(map[string]any{"path": pathProp()}, "path")},
		{Name: "list_dir", Description: "列出工作区内某目录的单层内容（目录在前带 / 尾，文件在后，按名排序）",
			Parameters: obj(map[string]any{"path": pathProp()})},
		{Name: "grep", Description: "在工作区内递归搜索正则（跳过二进制与 >1MB 文件，上限 200 条命中）",
			Parameters: obj(map[string]any{
				"path":    pathProp(),
				"pattern": str("Go 正则表达式（RE2 语法）"),
			}, "pattern")},
		// ── 写入/执行（每次都会先过 ask 门，除非运行者用 --allow-tools 显式豁免）──
		{Name: "write_file",
			Description: "整文件写入工作区内的文本文件（新建或覆盖；会自动建父目录；上限 2MB）。会改变磁盘内容，执行前需用户确认",
			Parameters: obj(map[string]any{
				"path":    pathProp(),
				"content": str("完整文件内容（UTF-8）。写空文件必须显式传 \"\"，缺参数 ≠ 空内容"),
			}, "path", "content")},
		{Name: "edit_file",
			Description: "在工作区内按**逐字节精确匹配**替换文本（要求 old_string 唯一，否则报错；replace_all=true 才允许替换多处）。改小段内容优先用它，别整文件重写",
			Parameters: obj(map[string]any{
				"path":        pathProp(),
				"old_string":  str("要被替换的原文，必须与文件内容逐字节一致（含缩进与换行），且在文件中唯一"),
				"new_string":  str("替换后的文本；传 \"\" 表示删除该片段"),
				"replace_all": map[string]any{"type": "boolean", "description": "为 true 时替换全部匹配（默认 false = 要求唯一）"},
			}, "path", "old_string", "new_string")},
		{Name: "run_command",
			Description: "在工作区根目录下执行一条 shell 命令（Windows 走 cmd /c），返回合并后的 stdout+stderr。会改变世界，执行前需用户确认",
			Parameters: obj(map[string]any{
				"command":         str("要执行的命令行（Windows: CMD 语法；一次只跑一条，多步请分次调用）"),
				"timeout_seconds": map[string]any{"type": "number", "description": "超时秒数，默认 60，上限 300"},
			}, "command")},
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
	// ★★ 2026-11-02：符号链接 / junction 逃逸。
	//   上面的判断是**字符串**归一（Clean），不解析链接——工作区内一个指向外面的
	//   symlink/junction 会以「看起来在根内」的形态通过，然后真读到工作区外。
	//   只读三件默认免 ask（main.go 的 --allow-tools 默认值）之后，这条从"理论"
	//   变成"实际"：中间没有人再拦一下了，闸必须在这里。
	//   ★ root 自身可能就是链接（junction 到别的盘），所以两边都解析后再比。
	//   ★ 为什么不用 filepath.EvalSymlinks：实测（2026-11-02，本机）它对 **junction**
	//     报 "The system cannot find the path specified"（`os.Readlink` 却是有效的），
	//     于是"解析失败 ⇒ 按原样放行"会让 junction 逃逸**整条漏过去**——第一版就是这么
	//     漏的，`TestResolveInWorkspace_SymlinkEscape` 当场把它抓出来。故改用逐段
	//     `os.Readlink` 自己走（对 symlink 与 junction 都有效）。
	//   ★ 返回**原始** abs 而不是解析后的真实路径：调用方拿到的路径与模型/用户写的
	//     一致（错误信息、`path` 回显都还在工作区语义里）；边界已判过。
	if real := realPathWithin(abs); real != "" {
		if !withinRoot(realPathWithin(rootClean), real) {
			return "", fmt.Errorf("路径越界：%s 经符号链接指向工作区外（%s）", p, real)
		}
	}
	return abs, nil
}

// realPathWithin：沿路径**逐段**解析重解析点（symlink / junction），返回真实路径。
//   - 每遇到一个能 `os.Readlink` 的段，就把它的目标拼上剩余段**重新从头走**——
//     链接目标本身还可能是链接（`budget` 是防环上界，用完就返回当前结果）。
//   - 解析不动（段不存在 / 无权限 / 不是链接）⇒ 原样返回已走过的部分。这是
//     **不猜**：宁可让后续 `os.Stat` 报"不可访问"，也不在这里凭空换一条路径。
func realPathWithin(p string) string { return realPathDepth(p, 32) }

func realPathDepth(p string, budget int) string {
	vol := filepath.VolumeName(p)
	sep := string(filepath.Separator)
	cur := filepath.Clean(vol + sep)
	parts := strings.Split(strings.TrimPrefix(filepath.Clean(p), vol+sep), sep)
	for i, part := range parts {
		if part == "" || part == "." {
			continue
		}
		cur = filepath.Join(cur, part)
		tgt, err := os.Readlink(cur)
		if err != nil {
			continue // 不是链接（或读不了）——按字面路径继续
		}
		if !filepath.IsAbs(tgt) {
			tgt = filepath.Join(filepath.Dir(cur), tgt)
		}
		rest := filepath.Join(append([]string{tgt}, parts[i+1:]...)...)
		if budget <= 0 {
			return filepath.Clean(rest) // 防环：跳数用尽，如实返回当前猜测（后续 Stat 兜）
		}
		return realPathDepth(rest, budget-1)
	}
	return cur
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
		// ★★ 2026-11-02：链接逃逸（与 resolveInWorkspace 同一条边界）。
		//   实测（本机，Go 1.2x）：`WalkDir` **不**跟随 junction——junction 在
		//   `DirEntry.IsDir()` 里是 false，被当成普通文件、`os.ReadFile` 打不开，
		//   所以"整棵走下去"这条不成立。真正的口子是**文件级符号链接**：
		//   它也报成非目录，但 `os.ReadFile` 会跟着它把根外的内容读进来。
		//   ⇒ 对每个重解析点按**真实路径**判一次边界，越界的不读。
		if _, lerr := os.Readlink(path); lerr == nil {
			if !withinRoot(rootAbs, realPathWithin(path)) {
				if d != nil && d.IsDir() {
					return filepath.SkipDir
				}
				return nil
			}
		}
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

// ── 写入 / 执行三件（2026-11-04）────────────────────────────────
//
// ★ 为什么现在才加（定位变更）：仓库的两个目的里，「能拿来干活」此前一直
//   被「给 ECDI 采数据」压着。可只读工具面 ⇒ agent 只能看不能改 ⇒ 这个 GUI
//   永远停在演示态。三件补上，才算「人能拿它干活」（docs/positioning.md §1）。

// toolWriteFile：整文件写入（新建或覆盖）。父目录不存在则建——「先建目录再写」
// 是模型最容易漏的一步，让它报错只会白烧一轮。
func toolWriteFile(args map[string]any, root string) toolResult {
	p, _ := args["path"].(string)
	if strings.TrimSpace(p) == "" {
		return toolResult{OK: false, Output: "write_file 缺少 path 参数"}
	}
	// ★ 用 ok 而不是零值：`content` 缺失与 `content: ""` 是两件不同的事，
	//   混起来会让「漏传参数」静默变成「清空文件」——最坏的默认值。
	content, hasContent := args["content"].(string)
	if !hasContent {
		return toolResult{OK: false, Output: `write_file 缺少 content 参数（写空文件请显式传 ""）`}
	}
	if len(content) > writeFileLimit {
		return toolResult{OK: false, Output: fmt.Sprintf(
			"内容过大：%d 字节，上限 %d 字节（大文件请分次写或改用 shell 工具）", len(content), writeFileLimit)}
	}
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}
	verb := "新建"
	if st, serr := os.Stat(abs); serr == nil {
		if st.IsDir() {
			return toolResult{OK: false, Output: "path 是目录，write_file 只写文件"}
		}
		verb = "覆盖"
	}
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("创建父目录失败：%v", err)}
	}
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("写入失败：%v", err)}
	}
	return toolResult{OK: true, Output: fmt.Sprintf("已%s %s（%d 字节，%d 行）",
		verb, filepath.ToSlash(p), len(content), countLines(content))}
}

// toolEditFile：逐字节精确替换。**唯一匹配**是硬要求——模型给个 `return nil`
// 这种片段就能同时命中八处，静默改第一处是最难查的一类事故。
func toolEditFile(args map[string]any, root string) toolResult {
	p, _ := args["path"].(string)
	if strings.TrimSpace(p) == "" {
		return toolResult{OK: false, Output: "edit_file 缺少 path 参数"}
	}
	oldStr, ok1 := args["old_string"].(string)
	newStr, ok2 := args["new_string"].(string)
	if !ok1 || !ok2 {
		return toolResult{OK: false, Output: "edit_file 需要 old_string 与 new_string 两个字符串参数（删除片段时 new_string 传 \"\"）"}
	}
	if oldStr == "" {
		return toolResult{OK: false, Output: "old_string 不能为空（空串会匹配一切）；整文件替换请用 write_file"}
	}
	replaceAll, _ := args["replace_all"].(bool)
	abs, err := resolveInWorkspace(root, p)
	if err != nil {
		return toolResult{OK: false, Output: err.Error()}
	}
	info, serr := os.Stat(abs)
	if serr != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("文件不可访问：%v（新建文件请用 write_file）", serr)}
	}
	if info.IsDir() {
		return toolResult{OK: false, Output: "path 是目录，edit_file 只改文件"}
	}
	if info.Size() > editFileLimit {
		return toolResult{OK: false, Output: fmt.Sprintf(
			"文件过大：%d 字节，edit_file 上限 %d 字节（改用 run_command 的脚本化改写）", info.Size(), editFileLimit)}
	}
	data, rerr := os.ReadFile(abs)
	if rerr != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("读取失败：%v", rerr)}
	}
	if hasNUL(data) {
		return toolResult{OK: false, Output: "疑似二进制文件（含 NUL 字节），edit_file 只处理文本"}
	}
	text := string(data)
	n := strings.Count(text, oldStr)
	switch {
	case n == 0:
		return toolResult{OK: false, Output: fmt.Sprintf(
			"未找到 old_string（%d 字节）：匹配是逐字节的，缩进、行尾空格、CRLF/LF 都要与文件一致。先 read_file 看一眼原文再改", len(oldStr))}
	case n > 1 && !replaceAll:
		return toolResult{OK: false, Output: fmt.Sprintf(
			"old_string 在文件中出现 %d 次，不唯一；请带上更多上下文让它唯一，或传 replace_all=true 明确表示改全部", n)}
	}
	limit := 1
	if replaceAll {
		limit = -1
	}
	out := strings.Replace(text, oldStr, newStr, limit)
	if err := os.WriteFile(abs, []byte(out), info.Mode().Perm()); err != nil {
		return toolResult{OK: false, Output: fmt.Sprintf("写入失败：%v", err)}
	}
	firstLine := 1 + strings.Count(text[:strings.Index(text, oldStr)], "\n")
	return toolResult{OK: true, Output: fmt.Sprintf("已修改 %s：替换 %d 处（首次在第 %d 行；该片段 −%d +%d 行）",
		filepath.ToSlash(p), n, firstLine, strings.Count(oldStr, "\n"), strings.Count(newStr, "\n"))}
}

// toolRunCommand：在工作区根目录跑一条 shell 命令。
//
// ★ 已知局限（不掩盖）：`exec.CommandContext` 到超时只杀**直接子进程**
//
//	（cmd.exe / sh），孙进程可能留下来。要根治得用作业对象
//	（Windows: CreateJobObject + KILL_ON_JOB_CLOSE）——那是下一步，
//	不在这里假装已经解决。
func toolRunCommand(args map[string]any, root string) toolResult {
	command, _ := args["command"].(string)
	if strings.TrimSpace(command) == "" {
		return toolResult{OK: false, Output: "run_command 缺少 command 参数"}
	}
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
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(timeout)*time.Second)
	defer cancel()

	var cmd *exec.Cmd
	if runtime.GOOS == "windows" {
		cmd = exec.CommandContext(ctx, "cmd", "/c", command)
	} else {
		cmd = exec.CommandContext(ctx, "/bin/sh", "-c", command)
	}
	cmd.Dir = root
	cmd.Env = childEnv()
	hideChildWindow(cmd) // 见 proc_windows.go：否则每跑一条命令闪一个黑色控制台
	out, err := cmd.CombinedOutput()
	text := trimOutput(string(out))

	switch {
	case ctx.Err() == context.DeadlineExceeded:
		return toolResult{OK: false, Output: fmt.Sprintf("命令超时（%d 秒）已请求终止：\n%s", timeout, text)}
	case err != nil:
		return toolResult{OK: false, Output: fmt.Sprintf("退出码非 0（%v）：\n%s", err, text)}
	}
	if strings.TrimSpace(text) == "" {
		return toolResult{OK: true, Output: "（无输出，退出码 0）"}
	}
	return toolResult{OK: true, Output: text}
}

// childEnv：继承环境但**剔除密钥**（见文件头 ③）。
// 上限：ECDI_*_KEY 一族（ECDI_CORE_KEY / ECDI_PROVIDER_<SAN>_KEY）。
func childEnv() []string {
	env := os.Environ()
	out := make([]string, 0, len(env))
	for _, kv := range env {
		name := kv
		if eq := strings.Index(kv, "="); eq >= 0 {
			name = kv[:eq]
		}
		up := strings.ToUpper(name)
		if strings.HasPrefix(up, "ECDI_") && strings.HasSuffix(up, "_KEY") {
			continue
		}
		out = append(out, kv)
	}
	return out
}

// trimOutput：超限时保留头尾各半（错误信息几乎都在尾部，只留头等于把最有用的截掉）。
func trimOutput(s string) string {
	if len(s) <= runCmdOutLimit {
		return s
	}
	half := runCmdOutLimit / 2
	head := safeCut(s, half)
	tail := safeTail(s, half)
	dropped := len(s) - len(head) - len(tail)
	if dropped < 0 {
		dropped = 0
	}
	return head + fmt.Sprintf("\n\n（中间省略 %d 字节）\n\n", dropped) + tail
}

// safeCut：把切点退到 UTF-8 字符边界（续字节 10xxxxxx 一律退），返回前缀。
// safeTail：取约 n 字节的后缀，起点**前进**到字符边界——两处都不能切出半个汉字。
func safeCut(s string, i int) string {
	if i <= 0 {
		return ""
	}
	if i >= len(s) {
		return s
	}
	for i > 0 && s[i]&0xC0 == 0x80 {
		i--
	}
	return s[:i]
}

func safeTail(s string, n int) string {
	if n <= 0 {
		return ""
	}
	if n >= len(s) {
		return s
	}
	start := len(s) - n
	for start < len(s) && s[start]&0xC0 == 0x80 {
		start++
	}
	return s[start:]
}

// countLines：行数（空串 = 0；不以 \n 结尾的最后一行也算一行）。
func countLines(s string) int {
	if s == "" {
		return 0
	}
	n := strings.Count(s, "\n")
	if !strings.HasSuffix(s, "\n") {
		n++
	}
	return n
}

// askPrompt：ask 门的问句（session.go 调用）。
//
// ★ 写/执行三件必须把**要动的东西**摆到人眼前，而 `summarizeArgs` 的 200 字符
//
//	参数摘要对一条长命令不够。问句保持**单行**：GUI 的 ask 行有折叠摘要
//	（UiCommon.cpp AskSummary），多行问句在折叠态会被压成一行，反而不如一开始就
//	写清楚；完整参数仍在工具卡的 args 里（block_start 下发过）。
func askPrompt(name string, args map[string]any, raw string) string {
	clip := func(s string, n int) string {
		r := []rune(s)
		if len(r) <= n {
			return s
		}
		return string(r[:n]) + "…"
	}
	switch name {
	case "write_file":
		p, _ := args["path"].(string)
		c, _ := args["content"].(string)
		return fmt.Sprintf("允许写入 %s 吗？（整文件覆盖，%d 字节 / %d 行）", p, len(c), countLines(c))
	case "edit_file":
		p, _ := args["path"].(string)
		o, _ := args["old_string"].(string)
		nw, _ := args["new_string"].(string)
		return fmt.Sprintf("允许修改 %s 吗？替换「%s」为「%s」",
			p, clip(strings.TrimSpace(o), 60), clip(strings.TrimSpace(nw), 60))
	case "run_command":
		c, _ := args["command"].(string)
		return fmt.Sprintf("允许执行命令吗？%s", clip(strings.TrimSpace(c), 160))
	}
	return fmt.Sprintf("允许执行 %s 吗？参数：%s", name, summarizeArgs(raw))
}
