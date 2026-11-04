//go:build windows

// proc_windows.go —— run_command 的子进程创建标志（仅 Windows）。
//
// ★ 为什么必须显式给：GUI 用 CREATE_NO_WINDOW 拉起 core（`ChildSource.cpp:96`），
// 于是 core **自己没有控制台**；而 cmd.exe 是控制台子系统程序，从无控制台的父进程
// 启动时 Windows 会**新建**一个控制台窗口 ⇒ 每跑一条命令就闪一个黑框，
// 而这条命令可能是 agent 循环里的十几次调用。
//
// CREATE_NO_WINDOW(0x08000000) 让它根本不创建；HideWindow 是第二道保险
// （STARTF_USESHOWWINDOW + SW_HIDE），两者同给不冲突。
package main

import (
	"os/exec"
	"syscall"
)

const createNoWindow = 0x08000000

func hideChildWindow(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: createNoWindow}
}
