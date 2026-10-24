package main

// tools_test.go —— 只读三件的边界与上限单测（P3 规格 §4.4）。
//
// 边界是这里的重点：resolveInWorkspace 是「只读 ≠ 无害」的第一道闸，
// 逃逸路径（`..`、根外绝对路径）必须逐条判 RED，而不是靠 ask 门兜底。

import (
	"os"
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
