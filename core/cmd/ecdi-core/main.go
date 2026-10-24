// ecdi-core —— ECDI_fake_harness 的「真 core」（P3：真模型 + 真工具）。
//
// 定位：protocol/PROTOCOL.md v3 契约的**第一个非假 core 消费者**——
// OpenAI 兼容端点（--base-url/--model/--key，不绑厂商）+ 只读三件真执行
// （read_file / list_dir / grep）。协议与 GUI 两侧已就绪，本程序补齐 core 侧。
//
// 用法（由 harness_gui 拉起；也可手工对管道调试）：
//
//	ecdi-core --base-url https://…/v4 --model glm-4.6 [--key sk-…]
//	          [--context 128000] [--allow-tools list_dir,grep]
//	          [--workspace D:/work/project] [--quiet]
//
// 密钥：--key 优先；缺省读环境变量 ECDI_CORE_KEY（推荐——不进命令行历史）。
// 两者皆空 ⇒ 启动即退出（stderr 说明，不猜、不带病运行）。
//
// ★ 安全边界（docs/positioning.md §4）：协议不是安全边界；ask 门是人在环控制；
// OS 级隔离是**运行者**的前置条件。只读三件把风险降了，没有除——
// read_file 读得到一切可读文件，这正是默认全 ask 的原因。
package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"sync/atomic"

	"ecdi-fake-core/internal/wire"
)

func main() {
	baseURL := flag.String("base-url", "", "OpenAI 兼容端点根（不含 /chat/completions）；必填")
	model := flag.String("model", "", "模型名；必填")
	key := flag.String("key", "", "API 密钥；缺省读环境变量 ECDI_CORE_KEY")
	contextTokens := flag.Int("context", 0, "上下文窗口容量（token）；0 = 不发 hello.context（GUI 显示 —）")
	allowTools := flag.String("allow-tools", "", "免 ask 工具名单（逗号分隔）；空 = 全部 ask（默认）")
	workspace := flag.String("workspace", "", "初始工作区；缺省 = 进程 cwd")
	quiet := flag.Bool("quiet", false, "关闭 stderr 日志")
	flag.Parse()

	// 密钥：参数优先，环境变量兜底；都空 = 拒绝启动（stderr 说明）。
	secret := *key
	keySource := "flag"
	if secret == "" {
		secret = os.Getenv("ECDI_CORE_KEY")
		keySource = "env"
	}
	missing := []string{}
	if strings.TrimSpace(*baseURL) == "" {
		missing = append(missing, "--base-url")
	}
	if strings.TrimSpace(*model) == "" {
		missing = append(missing, "--model")
	}
	if secret == "" {
		missing = append(missing, "--key（或环境变量 ECDI_CORE_KEY）")
	}
	if len(missing) > 0 {
		fmt.Fprintf(os.Stderr, "[ecdi-core] 缺少必填参数：%s —— 不猜测、不带病运行\n", strings.Join(missing, " / "))
		os.Exit(2)
	}

	// 初始工作区：显式指定优先；校验失败 = 启动失败（不静默换目录——
	// 静默替换正是协议 §3.9 存在要禁止的「猜测」）。
	if ws := strings.TrimSpace(*workspace); ws != "" {
		norm, err := wire.NormalizeWorkspace(ws)
		if err != nil {
			fmt.Fprintf(os.Stderr, "[ecdi-core] --workspace 不可用：%v\n", err)
			os.Exit(2)
		}
		if err := os.Chdir(norm); err != nil {
			fmt.Fprintf(os.Stderr, "[ecdi-core] --workspace 切换失败：%v\n", err)
			os.Exit(2)
		}
	}

	// 日志器持有密钥做**全局遮蔽**（wire.NewLogger：每条日志格式化后统一过一遍，
	// 不靠「打日志时记得遮」的纪律——那种纪律迟早会在某个新日志点失效）。
	logf := wire.NewLogger("[ecdi-core] ", *quiet, secret, *key)
	// 凭据**在场性**日志（运营诊断用）：只记长度与来源，值由上面的遮蔽器兜底。
	logf("credential: %s (len=%d, source=%s)",
		map[bool]string{true: "present", false: "absent"}[secret != ""], len(secret), keySource)

	lines := make(chan wire.StampedLine, 64)
	var curState atomic.Int32
	curState.Store(wire.CodeIdle)
	go wire.ReadStdin(lines, &curState, logf)

	s := newSession(llmConfig{BaseURL: *baseURL, Model: *model, Key: secret},
		strings.Split(*allowTools, ","), *contextTokens, lines, &curState, logf,
		os.Stdout)
	if err := s.run(); err != nil {
		logf("fatal: %v", err)
		os.Exit(1)
	}
}
