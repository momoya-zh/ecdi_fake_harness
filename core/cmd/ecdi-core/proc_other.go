//go:build !windows

// proc_other.go —— 非 Windows 侧的空实现（run_command 走 /bin/sh -c，
// 没有「无控制台的父进程弹出黑框」这回事）。
package main

import "os/exec"

func hideChildWindow(cmd *exec.Cmd) {}
