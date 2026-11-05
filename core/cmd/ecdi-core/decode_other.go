//go:build !windows

package main

// decodeGBK：非 Windows 没有系统代码页转换原语。
//   ★ 这里**不**假装能解：返回 false，由调用方走逐字节替换路径。
//     非 Windows 的终端/进程默认 UTF-8，GBK 输出属于罕见的跨平台误用；
//     真需要时再引入纯 Go 的 GBK 表（不为此在 stdlib-only 的 core 里加依赖）。
func decodeGBK(b []byte) (string, bool) {
	return "", false
}