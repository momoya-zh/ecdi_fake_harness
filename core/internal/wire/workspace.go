package wire

// workspace.go —— 工作区路径的规范化与校验（协议 §3.9）。

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// NormalizeWorkspace：§3.9 的路径规范化（绝对 / `/` 分隔 / 无尾随 `/` / 无 `.`·`..`）。
// 同时校验「它真是一个可访问的目录」——否则「工作区显示与 core 实际目录一致」
// 只能在字符串层面成立，而 core 实际并不在那个目录里。
func NormalizeWorkspace(p string) (string, error) {
	if strings.TrimSpace(p) == "" {
		return "", errors.New("请求缺少 path")
	}
	abs, err := filepath.Abs(p)
	if err != nil {
		return "", fmt.Errorf("无法解析路径：%v", err)
	}
	abs = filepath.Clean(abs) // 顺带消掉 `.` / `..` 段
	info, err := os.Stat(abs)
	if err != nil {
		return "", fmt.Errorf("目录不可访问：%v", err)
	}
	if !info.IsDir() {
		return "", errors.New("路径不是目录")
	}
	return strings.ReplaceAll(abs, "\\", "/"), nil
}
