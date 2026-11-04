package main

// tools_test.go —— 只读三件的边界与上限单测（P3 规格 §4.4）。
//
// 边界是这里的重点：resolveInWorkspace 是「只读 ≠ 无害」的第一道闸，
// 逃逸路径（`..`、根外绝对路径）必须逐条判 RED，而不是靠 ask 门兜底。

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"ecdi-fake-core/internal/wire"
)

func setupWS(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	if err := os.WriteFile(root+"/notes.md", []byte(strings.Repeat("alpha beta\n", 10)), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(root+"/sub", 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(root+"/sub/deep.txt", []byte("deep-grep-hit\nplain line\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(root+"/bin.dat", []byte{'O', 'K', 0x00, 0x01}, 0o644); err != nil {
		t.Fatal(err)
	}
	return root
}

// ── 工作区边界：逃逸路径必须全部拒绝 ────────────────────────────

func TestResolveInWorkspace_Boundary(t *testing.T) {
	root := setupWS(t)
	rootN, _ := wire.NormalizeWorkspace(root) // `/` 分隔的规范形（executeTool 收到的形态）
	rootWin := filepath.Clean(root)           // filepath 形（resolveInWorkspace 内部基准）

	cases := []struct {
		name, in string
		ok       bool
	}{
		{"相对路径在根内", "notes.md", true},
		{"点路径=根本身", ".", true},
		{"子目录相对", "sub/deep.txt", true},
		{"根内绝对路径", rootWin + "\\notes.md", true},
		{".. 逃出根", "../outside.txt", false},
		{"多层 .. 逃出根", "sub/../../outside.txt", false},
		{"根外绝对路径", filepath.Clean(root + "/../sibling.txt"), false},
		{"卷根绝对路径", "C:\\Windows\\win.ini", false},
	}
	for _, c := range cases {
		abs, err := resolveInWorkspace(rootWin, c.in)
		if c.ok && err != nil {
			t.Errorf("[%s] 应放行，却拒绝：%v", c.name, err)
		}
		if !c.ok && err == nil {
			t.Errorf("[%s] 应拒绝，却放行：%s", c.name, abs)
		}
	}
	_ = rootN
}

// makeDirLink：尽力建一个「指向目录」的链接——先试 os.Symlink，失败退回 junction。
//   ★ 为什么要有这个兜底：Windows 上 os.Symlink 需要 SeCreateSymbolicLinkPrivilege
//     （非管理员默认没有），而 `mklink /J` 建 junction **不需要**任何特权。
//     少了这层兜底，本机这条用例会恒 SKIP ⇒ 判据变成"恒成立的负向用例"≈ 没有
//     （本仓已踩过同款坑：`truncated-final-line.jsonl` 恒以 0x0A 结尾）。
func makeDirLink(t *testing.T, link, target string) bool {
	t.Helper()
	if err := os.Symlink(target, link); err == nil {
		return true
	}
	out, err := exec.Command("cmd", "/C", "mklink", "/J", link, target).CombinedOutput()
	if err != nil {
		t.Logf("junction 创建失败：%v %s", err, strings.TrimSpace(string(out)))
		return false
	}
	return true
}

// ── 符号链接逃逸：链接在根内、目标是根外（2026-11-02）──────────────
//   ★ 为什么单独一条：`Filepath.Clean` 只做字符串归一，链接必须按**真实路径**
//     判边界。只读三件默认免 ask（`--allow-tools` 默认值）之后，这里已经没有
//     第二道人闸兜底了。
//   ★ Windows 上 os.Symlink 需要 SeCreateSymbolicLinkPrivilege / 开发者模式；
//     建不出来就 SKIP 并**说出原因**（跳过不等于通过）。
//   ★ 带对照臂：指向**根内**的链接必须放行——否则这个闸会把正常路径一起掐了，
//     而那种"过度拦截"在单臂用例里看不出来。
func TestResolveInWorkspace_SymlinkEscape(t *testing.T) {
	root := setupWS(t)
	rootWin := filepath.Clean(root)
	outside := t.TempDir()
	if err := os.WriteFile(filepath.Join(outside, "secret.txt"), []byte("outside-secret\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	if !makeDirLink(t, filepath.Join(rootWin, "escape"), outside) {
		t.Skipf("SKIP：符号链接与 junction 都建不出来——本机无法形成这条判据")
	}

	_, err := resolveInWorkspace(rootWin, "escape/secret.txt")
	if err == nil {
		t.Fatalf("经根内符号链接读到根外文件，边界未拦下")
	}
	if !strings.Contains(err.Error(), "越界") {
		t.Fatalf("错误信息应说明越界（模型/GUI 才不会误读成「文件不存在」），got %v", err)
	}
	// 工具层同一条：read_file 也不许穿过链接
	if r := executeTool("read_file", map[string]any{"path": "escape/secret.txt"}, rootWin); r.OK {
		t.Fatalf("read_file 经符号链接读到根外文件: %s", tailOf(r.Output, 60))
	}
	// grep 是第二个入口。★ 只能拿**文件级符号链接**验：junction 实测不被 `WalkDir`
	// 跟随（`DirEntry.IsDir()` 为 false，`os.ReadFile` 打不开），拿它做的断言**修前修后
	// 都绿**——那是没有区分力的判据。文件符号链接需要 SeCreateSymbolicLinkPrivilege，
	// 建不出来就如实跳过，并说明"grep 那条闸本机未覆盖"。
	leak := filepath.Join(rootWin, "leak.txt")
	if err := os.Symlink(filepath.Join(outside, "secret.txt"), leak); err != nil {
		t.Logf("grep 链接臂跳过（建不出文件符号链接，故 grep 的 realPathWithin 闸本机未覆盖）：%v", err)
	} else if r := executeTool("grep", map[string]any{"pattern": "outside-secret", "path": "."}, rootWin); r.OK &&
		strings.Contains(r.Output, "outside-secret") {
		t.Fatalf("grep 沿文件符号链接读到了根外内容: %s", tailOf(r.Output, 80))
	}

	// 对照臂：根内链接必须照常放行
	if !makeDirLink(t, filepath.Join(rootWin, "inside"), filepath.Join(rootWin, "sub")) {
		t.Logf("对照臂跳过（第二个链接建不出来）——主判据仍有效，但「不过度拦截」这一侧未验")
		return
	}
	if _, err := resolveInWorkspace(rootWin, "inside/deep.txt"); err != nil {
		t.Fatalf("根内符号链接被误判越界（过度拦截）：%v", err)
	}
}

// ── read_file：正常 / 截断 / 二进制拒绝 ─────────────────────────

func TestToolReadFile(t *testing.T) {
	root := setupWS(t)

	// 正常
	if r := executeTool("read_file", map[string]any{"path": "notes.md"}, root); !r.OK {
		t.Fatalf("notes.md 应可读: %s", r.Output)
	}

	// 截断：写一个 >40KB 的文件，输出必须带截断标注（静默截断 = 模型拿半份结果下结论）
	big := root + "/big.txt"
	if err := os.WriteFile(big, []byte(strings.Repeat("x", 50*1024)), 0o644); err != nil {
		t.Fatal(err)
	}
	r := executeTool("read_file", map[string]any{"path": "big.txt"}, root)
	if !r.OK || !strings.Contains(r.Output, "截断") {
		t.Fatalf(">40KB 文件应截断且标注，got ok=%v tail=%q", r.OK, tailOf(r.Output, 60))
	}

	// 二进制：NUL 探测
	if r := executeTool("read_file", map[string]any{"path": "bin.dat"}, root); r.OK {
		t.Fatalf("二进制文件应被拒绝: %s", r.Output)
	}

	// 不存在
	if r := executeTool("read_file", map[string]any{"path": "nope.txt"}, root); r.OK {
		t.Fatalf("不存在的文件应报错")
	}

	// 越界
	if r := executeTool("read_file", map[string]any{"path": "../secret.txt"}, root); r.OK {
		t.Fatalf("越界读取应被拒绝")
	}
}

// ── list_dir：目录在前带 / 尾、文件在后、排序 ───────────────────

func TestToolListDir(t *testing.T) {
	root := setupWS(t)
	r := executeTool("list_dir", map[string]any{}, root)
	if !r.OK {
		t.Fatalf("list_dir 失败: %s", r.Output)
	}
	lines := strings.Split(r.Output, "\n")
	if len(lines) != 3 {
		t.Fatalf("根下应恰 3 项（sub/、bin.dat、notes.md），got %d:\n%s", len(lines), r.Output)
	}
	// 目录行带 / 尾且都在文件行之前
	lastDir := -1
	for i, l := range lines {
		if strings.HasSuffix(l, "/") {
			lastDir = i
		}
	}
	if lastDir != 0 {
		t.Fatalf("目录应恰在文件之前（这里 sub/ 应是第 0 行）:\n%s", r.Output)
	}
}

// ── grep：递归 + 命中格式 + 二进制/大文件跳过 ───────────────────

func TestToolGrep(t *testing.T) {
	root := setupWS(t)

	r := executeTool("grep", map[string]any{"pattern": "deep-grep-hit"}, root)
	if !r.OK || !strings.Contains(r.Output, "sub/deep.txt") {
		t.Fatalf("grep 应递归命中 sub/deep.txt（相对路径形式），got: %s", r.Output)
	}
	if !strings.Contains(r.Output, ":1: deep-grep-hit") {
		t.Fatalf("命中行应带行号: %s", r.Output)
	}

	// 无命中
	if r := executeTool("grep", map[string]any{"pattern": "zzz-not-there"}, root); !r.OK || !strings.Contains(r.Output, "无命中") {
		t.Fatalf("无命中应显式说明: %s", r.Output)
	}

	// 命中上限（200 条 + 截断标注）
	many := root + "/many.txt"
	if err := os.WriteFile(many, []byte(strings.Repeat("hit\n", 300)), 0o644); err != nil {
		t.Fatal(err)
	}
	r = executeTool("grep", map[string]any{"pattern": "hit"}, root)
	if !r.OK || !strings.Contains(r.Output, "截断") {
		t.Fatalf("超 200 命中应截断且标注，tail=%q", tailOf(r.Output, 80))
	}

	// 非法正则
	if r := executeTool("grep", map[string]any{"pattern": "("}, root); r.OK {
		t.Fatalf("非法正则应报错")
	}
}

// ── 未知工具 ────────────────────────────────────────────────────

func TestExecuteToolUnknown(t *testing.T) {
	root := setupWS(t)
	if r := executeTool("bash", map[string]any{}, root); r.OK {
		t.Fatalf("未知/未实现工具必须拒绝（bash 不在只读三件内）")
	}
}

// ── 日志器密钥遮蔽（wire.NewLogger）────────────────────────────

func TestLoggerMasksSecrets(t *testing.T) {
	// 换掉 os.Stderr（包级变量，可重指派）用管道真捕获——
	// 「遮蔽语义存在」必须由输出证明，不能由代码形状推断。
	secret := "sk-super-secret-123"
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	oldErr := os.Stderr
	os.Stderr = w
	defer func() { os.Stderr = oldErr }()

	logf := wire.NewLogger("[t] ", false, secret)
	logf("key is %s and again %s", secret, secret)
	w.Close()

	buf := make([]byte, 4096)
	n, _ := r.Read(buf)
	out := string(buf[:n])
	if strings.Contains(out, secret) {
		t.Fatalf("密钥泄漏进 stderr: %q", out)
	}
	if !strings.Contains(out, "key is *** and again ***") {
		t.Fatalf("遮蔽后的日志形态不对: %q", out)
	}
}

// ── 小工具 ──────────────────────────────────────────────────────

func tailOf(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[len(s)-n:]
}

// ── 写入 / 执行三件（2026-11-04）────────────────────────────────
//
// 判据都落在**磁盘上真的变没变**：这三件的失败模式不是"报错"，是"静默改错"或
// "静默没改"。所以每条 RED 用例都要配一条 Green 用例证明判据有区分力。

func TestToolDefs_SixToolsWithSchemas(t *testing.T) {
	// ★ v5：这张表是「子代理关闭」的那一臂——六件，没有 task。
	//   有 task 的七件表由 TestToolDefsFor_AgentsAddTaskOnlyAtRoot 盖。
	defs := toolDefsFor(false, 0)
	want := map[string]bool{"read_file": false, "list_dir": false, "grep": false,
		"write_file": false, "edit_file": false, "run_command": false}
	for _, d := range defs {
		if _, ok := want[d.Name]; !ok {
			t.Errorf("多出未预期的工具 %q", d.Name)
		}
		want[d.Name] = true
		// ★ 元断言：`required` 必须是数组而不是 null（DeepSeek 严格 schema 会 400，
		//   第一次就是这么撞的）。零实参工具也要是空数组。
		if d.Parameters["required"] == nil {
			t.Errorf("%s 的 required 是 nil（JSON 会出 null）", d.Name)
		}
	}
	for name, seen := range want {
		if !seen {
			t.Errorf("工具 %q 未下发（模型看不到它 ⇒ 永远不会被调用）", name)
		}
	}
}

func TestWriteFile_CreateThenOverwrite(t *testing.T) {
	root := setupWS(t)
	rootN, _ := wire.NormalizeWorkspace(root)

	// ① 新建（父目录不存在 ⇒ 应自动建）
	res := executeTool("write_file", map[string]any{
		"path": "newdir/a.txt", "content": "第一行\n第二行\n"}, rootN)
	if !res.OK {
		t.Fatalf("新建失败：%s", res.Output)
	}
	if !strings.Contains(res.Output, "新建") || !strings.Contains(res.Output, "2 行") {
		t.Errorf("新建回执形态不对：%s", res.Output)
	}
	got, err := os.ReadFile(filepath.Join(root, "newdir", "a.txt"))
	if err != nil {
		t.Fatalf("文件没落盘：%v", err)
	}
	if string(got) != "第一行\n第二行\n" {
		t.Errorf("内容不符：%q", string(got))
	}

	// ② 覆盖同名文件
	res = executeTool("write_file", map[string]any{"path": "newdir/a.txt", "content": "x"}, rootN)
	if !res.OK || !strings.Contains(res.Output, "覆盖") {
		t.Fatalf("覆盖回执不对：%v %s", res.OK, res.Output)
	}
	got, _ = os.ReadFile(filepath.Join(root, "newdir", "a.txt"))
	if string(got) != "x" {
		t.Errorf("覆盖后内容不符：%q", string(got))
	}

	// ③ 缺 content ≠ 空内容（最坏的默认值是「漏参数⇒清空文件」）
	res = executeTool("write_file", map[string]any{"path": "newdir/a.txt"}, rootN)
	if res.OK {
		t.Errorf("缺 content 竟然成功：%s", res.Output)
	}
	got, _ = os.ReadFile(filepath.Join(root, "newdir", "a.txt"))
	if string(got) != "x" {
		t.Errorf("被拒的写入改动了文件：%q", string(got))
	}

	// ④ 越界（工具层再判一次，不只靠 resolveInWorkspace 的单测）
	res = executeTool("write_file", map[string]any{"path": "../escape.txt", "content": "no"}, rootN)
	if res.OK {
		t.Errorf("越界写入竟然成功：%s", res.Output)
	}
}

func TestEditFile_UniqueAmbiguousReplaceAll(t *testing.T) {
	root := setupWS(t)
	rootN, _ := wire.NormalizeWorkspace(root)
	target := filepath.Join(root, "dup.txt")
	if err := os.WriteFile(target, []byte("a\nhit\nb\nhit\nc\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	// ① 不唯一 ⇒ 拒绝，且文件**一个字节都没动**
	before, _ := os.ReadFile(target)
	res := executeTool("edit_file", map[string]any{
		"path": "dup.txt", "old_string": "hit", "new_string": "HIT"}, rootN)
	if res.OK {
		t.Fatalf("不唯一竟然放行：%s", res.Output)
	}
	if after, _ := os.ReadFile(target); string(after) != string(before) {
		t.Fatalf("被拒的编辑动了文件：%q", string(after))
	}

	// ② 找不到 ⇒ 拒绝，并给出「逐字节匹配」的提示（CRLF/缩进是常见真因）
	res = executeTool("edit_file", map[string]any{
		"path": "dup.txt", "old_string": "nope", "new_string": "x"}, rootN)
	if res.OK || !strings.Contains(res.Output, "逐字节") {
		t.Errorf("未命中的回执应提示逐字节匹配：%v %s", res.OK, res.Output)
	}

	// ③ replace_all=true ⇒ 两处都换
	res = executeTool("edit_file", map[string]any{
		"path": "dup.txt", "old_string": "hit", "new_string": "HIT", "replace_all": true}, rootN)
	if !res.OK {
		t.Fatalf("replace_all 失败：%s", res.Output)
	}
	if got, _ := os.ReadFile(target); string(got) != "a\nHIT\nb\nHIT\nc\n" {
		t.Errorf("replace_all 结果不符：%q", string(got))
	}

	// ④ 唯一片段 ⇒ 只改那一处，回执带行号
	if err := os.WriteFile(target, []byte("l1\nuniq\nl3\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	res = executeTool("edit_file", map[string]any{
		"path": "dup.txt", "old_string": "uniq", "new_string": "UNIQ"}, rootN)
	if !res.OK || !strings.Contains(res.Output, "第 2 行") {
		t.Errorf("唯一替换回执/行号不对：%v %s", res.OK, res.Output)
	}

	// ⑤ 空 old_string ⇒ 拒绝（会匹配一切）
	res = executeTool("edit_file", map[string]any{
		"path": "dup.txt", "old_string": "", "new_string": "x"}, rootN)
	if res.OK {
		t.Errorf("空 old_string 竟然放行：%s", res.Output)
	}
}

func TestRunCommand_OutputExitCodeAndCwd(t *testing.T) {
	root := setupWS(t)
	rootN, _ := wire.NormalizeWorkspace(root)

	res := executeTool("run_command", map[string]any{"command": "echo hello-from-test"}, rootN)
	if !res.OK {
		t.Fatalf("echo 失败：%s", res.Output)
	}
	if !strings.Contains(res.Output, "hello-from-test") {
		t.Errorf("输出没回传：%q", res.Output)
	}

	// ① 非 0 退出码 ⇒ OK=false（否则模型会把失败当成功继续推）
	res = executeTool("run_command", map[string]any{"command": "exit 3"}, rootN)
	if res.OK {
		t.Errorf("非 0 退出码竟然 OK：%s", res.Output)
	}

	// ② cwd = 工作区根：在这里建个文件，必须落在工作区里
	res = executeTool("run_command", map[string]any{"command": "echo x > cwd-probe.txt"}, rootN)
	if !res.OK {
		t.Fatalf("重定向失败：%s", res.Output)
	}
	if _, err := os.Stat(filepath.Join(root, "cwd-probe.txt")); err != nil {
		t.Errorf("cwd 不是工作区根：%v", err)
	}
}

func TestRunCommand_StripsApiKeys(t *testing.T) {
	root := setupWS(t)
	rootN, _ := wire.NormalizeWorkspace(root)
	const secret = "sk-this-must-not-leak-into-child"
	t.Setenv("ECDI_CORE_KEY", secret)
	t.Setenv("ECDI_PROVIDER_DEEPSEEK_KEY", secret+"-2")

	// `set NAME` 在变量不存在时返回非 0 —— 正好当存在性判据（不看本地化文案）。
	for _, name := range []string{"ECDI_CORE_KEY", "ECDI_PROVIDER_DEEPSEEK_KEY"} {
		res := executeTool("run_command", map[string]any{"command": "set " + name}, rootN)
		if res.OK {
			t.Errorf("%s 仍存在于子进程环境：%s", name, res.Output)
		}
		if strings.Contains(res.Output, secret) {
			t.Errorf("%s 的值泄漏进输出：%s", name, res.Output)
		}
	}
}

func TestAskPrompt_SingleLineAndSpecific(t *testing.T) {
	// GUI 的 ask 行有折叠摘要（UiCommon.cpp AskSummary）⇒ 问句必须是单行，
	// 且三个会改变世界的工具都要把「改成什么」说出来，不能只剩一个工具名。
	cases := []struct {
		name string
		args map[string]any
		want string
	}{
		{"write_file", map[string]any{"path": "a.txt", "content": "hi"}, "a.txt"},
		{"edit_file", map[string]any{"path": "b.txt", "old_string": "x", "new_string": "y"}, "b.txt"},
		{"run_command", map[string]any{"command": "go test ./..."}, "go test"},
	}
	for _, c := range cases {
		q := askPrompt(c.name, c.args, "")
		if strings.Contains(q, "\n") {
			t.Errorf("[%s] 问句含换行：%q", c.name, q)
		}
		if !strings.Contains(q, c.want) {
			t.Errorf("[%s] 问句没把关键内容说出来：%q", c.name, q)
		}
	}
}

func TestTrimOutput_KeepsHeadAndTail(t *testing.T) {
	big := strings.Repeat("头", 20000) + "TAIL-MARK"
	got := trimOutput(big)
	if len(got) > runCmdOutLimit+200 {
		t.Errorf("截断后仍过大：%d 字节", len(got))
	}
	if !strings.Contains(got, "TAIL-MARK") {
		t.Errorf("尾部被砍掉了（错误信息通常在这里）")
	}
	if !strings.Contains(got, "中间省略") {
		t.Errorf("截断没有显式标注（静默截断比报错更糟）")
	}
	// 切点必须落在字符边界：混进 U+FFFD 说明切了半个汉字
	if strings.ContainsRune(got, '\uFFFD') {
		t.Errorf("截断切出了半个字符")
	}
}

func TestCountLines(t *testing.T) {
	cases := map[string]int{"": 0, "a": 1, "a\n": 1, "a\nb": 2, "a\nb\n": 2}
	for in, want := range cases {
		if got := countLines(in); got != want {
			t.Errorf("countLines(%q) = %d，期望 %d", in, got, want)
		}
	}
}
