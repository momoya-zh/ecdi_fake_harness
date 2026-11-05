//go:build windows

package main

import (
	"syscall"
	"unicode/utf16"
	"unsafe"
)

// CP936 = 简体中文 GBK（Windows 代码页）。
const cpGBK = 936

var procMultiByteToWideChar = syscall.NewLazyDLL("kernel32.dll").NewProc("MultiByteToWideChar")

// decodeGBK 用系统代码页把 GBK 字节解成 UTF-8。
//   ★ flags=0：非法字节由系统替换成 U+FFFD，而不是整段失败——进程输出可能
//     被截断（超时强杀），此时仍要尽量把能读的部分显示出来。
func decodeGBK(b []byte) (string, bool) {
	if len(b) == 0 {
		return "", true
	}
	n, _, _ := procMultiByteToWideChar.Call(
		uintptr(cpGBK), 0,
		uintptr(unsafe.Pointer(&b[0])), uintptr(len(b)),
		0, 0,
	)
	if n == 0 {
		return "", false
	}
	buf := make([]uint16, n)
	r, _, _ := procMultiByteToWideChar.Call(
		uintptr(cpGBK), 0,
		uintptr(unsafe.Pointer(&b[0])), uintptr(len(b)),
		uintptr(unsafe.Pointer(&buf[0])), uintptr(n),
	)
	if r == 0 {
		return "", false
	}
	return string(utf16.Decode(buf)), true
}