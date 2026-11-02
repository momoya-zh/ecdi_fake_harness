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
