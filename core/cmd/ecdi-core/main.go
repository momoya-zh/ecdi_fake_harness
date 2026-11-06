// ecdi-core —— ECDI_fake_harness 的「真 core」（P3：真模型 + 真工具）。
//
// 定位：protocol/PROTOCOL.md 契约的**第一个非假 core 消费者**——
// OpenAI 兼容端点（--base-url/--model/--key，不绑厂商）+ 六件真工具
// （只读：read_file / list_dir / grep；写入执行：write_file / edit_file /
// run_command，后三件默认逐次过 ask 门）。协议与 GUI 两侧已就绪，本程序补齐 core 侧。
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
// read_file 读得到**工作区内**一切可读文件（`resolveInWorkspace` 兜住越界，
// tools.go:96），这正是它们可以默认免 ask、而写工具将来必须过 ask 的分别。
package main

import (
	"flag"
	"fmt"
	"os"
	"strings"
	"sync/atomic"
	"time"

	"ecdi-fake-core/internal/wire"
)

func main() {
	baseURL := flag.String("base-url", "", "OpenAI 兼容端点根（不含 /chat/completions）；必填")
	model := flag.String("model", "", "模型名；必填")
	key := flag.String("key", "", "API 密钥；缺省读环境变量 ECDI_CORE_KEY")
	contextTokens := flag.Int("context", 0, "上下文窗口容量（token）；0 = 不发 hello.context（GUI 显示 —）")
	maxOutput := flag.Int("max-output", 0, "请求体 max_tokens；0 = 不发（端点默认）")
	providerID := flag.String("provider-id", "", "初始提供商 id（model 切换按它寻址；切走再切回靠它注册）")
	// ★★ 2026-11-02：默认豁免只读三件（用户实测反馈「权限门过于严格」）。
	//   为什么可以免确认：ask 门拦的是**会改变世界**的动作；这三件都只读，且
	//   `resolveInWorkspace` 已把路径**解析后**钉在工作区内（tools.go:96——
	//   绝对路径越界、`..` 逃逸全部拒绝）⇒ 逐次确认不增加任何保护，只剩摩擦。
	//   为什么是"默认值"而不是运行时启发式：core 不去看路径猜「该不该问」，
	//   名单永远由运行者用这个 flag 整体改写（`--allow-tools=` 空值 = 恢复全 ask）。
	//   ★ 2026-11-04：写入/执行三件（write_file / edit_file / run_command）**已经
	//     有了**，且**刻意不在这里**——它们会改磁盘、跑任意命令，逐次过 ask 才是
	//     ask 门存在的理由。要豁免请自己显式写 `--allow-tools=read_file,...,run_command`，
	//     那是运行者的决定，不是我们的默认值。
	allowTools := flag.String("allow-tools", "read_file,list_dir,grep",
		"免 ask 工具名单（逗号分隔）；默认只读三件；`--allow-tools=` = 全部 ask")
	workspace := flag.String("workspace", "", "初始工作区；缺省 = 进程 cwd")
	quiet := flag.Bool("quiet", false, "关闭 stderr 日志")
	resumeID := flag.String("resume", "", "恢复会话 ID（从 .ecdi/sessions/<id> 续接）")
	sessionID := flag.String("session", "", "会话 ID（缺省为 s-real-1）")
	failSummarize := flag.Bool("fail-summarize", false, "测试注入：模拟摘要压缩失败")
	globalMemory := flag.String("global-memory", "", "全局记忆目录（缺省为 exe 同级 data/memory）")
	// ── 协议 v5：子代理（docs/subagent-v5-plan.md）────────────────────
	// ★ 默认**关**（方案原文，且「既有路径逐位不变」是本项目回归基线的前提）。
	//   开启后：hello.caps 增 "agents"、根代理的工具表多一个 `task`。
	//   关闭时 caps / 工具表 / 每个块都多一个字节都不变。
	//   ★ 待用户裁定：是否改成默认开（改这一个 false 即可，B3 会在 GUI 侧
	//     给一个「启用子代理」开关写进 coreArgs，两者取其一）。
	agents := flag.Bool("agents", false, "启用子代理（task 委派）；关闭 = v4 行为，逐字节不变")
	maxAgents := flag.Int("max-agents", 2, "同时运行的子代理上限（≤8）")
	agentMaxTurns := flag.Int("agent-max-turns", 8, "单个子代理的轮数上限")
	agentTimeout := flag.Duration("agent-timeout", 120*time.Second, "单个子代理的墙钟上限")
	// ── L1 沙盒隔离（run_command 保护）──────────────────────────────
	sandboxEnabled := flag.Bool("sandbox", true, "启用 L1 沙盒隔离（Windows Job Object + 特权剥离）；默认开启")
	sandboxMemMB := flag.Int("sandbox-memory-mb", 1024, "L1 沙盒作业内存上限（MB）")
	sandboxMaxProcs := flag.Int("sandbox-max-procs", 32, "L1 沙盒作业最大活跃进程数（防 fork 炸弹）")
	sandboxDropPrivs := flag.Bool("sandbox-drop-privs", true, "是否通过受限令牌剥离管理员/调试高危特权")
	mcpConfig := flag.String("mcp-config", "", "MCP 配置文件路径（缺省在工作区或全局配置目录自动发现）")
	mcpEnv := flag.String("mcp-env", mcpEnvInherit, "MCP stdio 子进程环境：inherit（继承并剔除密钥，默认）| minimal（只留白名单）")
	mcpRespawn := flag.Bool("mcp-respawn", true, "MCP stdio 服务崩溃后按需自动重启（每会话每服务上限 3 次）")
	mcpTimeout := flag.Duration("mcp-timeout", defaultMCPCallTimeout, "单个 MCP 工具调用的默认墙钟上限（0 表示无上限）")
	flag.Parse()

	setSandboxConfig(SandboxConfig{
		Enabled:        *sandboxEnabled,
		MemoryLimitMB:  *sandboxMemMB,
		MaxProcesses:   *sandboxMaxProcs,
		DropPrivileges: *sandboxDropPrivs,
	})
	// MCP 运行期开关：坏值拒绝启动（静默回落会让「我明明写了 minimal」变成谎言）。
	envMode, err := normalizeMCPEnvMode(*mcpEnv)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[ecdi-core] %v\n", err)
		os.Exit(2)
	}
	setMCPOptions(mcpRuntimeOptions{EnvMode: envMode, Respawn: *mcpRespawn})

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

	// ★ P3.6：多提供商注册表——GUI 为配置里每一家注入
	//   ECDI_PROVIDER_<SAN>_URL / _KEY / _ID（SAN = sanitizeProviderID）。
	//   密钥仍走启动环境层（D5）：不经协议、不经命令行、不经文件。
	providerEnv := map[string]llmConfig{}
	for _, kv := range os.Environ() {
		// ★ 后缀判断必须对**名字**做，不对整行做——整行是 "NAME=value"，
		//   它的尾部是 value（首个版本在这里查 "_URL"，永远查不到，注册表恒空）。
		const prefix = "ECDI_PROVIDER_"
		const urlSuffix = "_URL"
		eq := strings.Index(kv, "=")
		if eq < 0 || !strings.HasPrefix(kv[:eq], prefix) ||
			!strings.HasSuffix(kv[:eq], urlSuffix) {
			continue
		}
		san := kv[len(prefix) : eq-len(urlSuffix)]
		id := os.Getenv(prefix + san + "_ID")
		if id == "" {
			id = strings.ToLower(san)
		}
		base := os.Getenv(prefix + san + "_URL")
		key := os.Getenv(prefix + san + "_KEY")
		if base == "" || key == "" {
			continue
		}
		// ★ 2026-10-27：模型名也随环境注入。此前这里恒写 `Model: ""`——切到
		//   非初始提供商后 pc.Model 是空串，请求体里 `"model": ""` 端点必报错，
		//   等于「切得过去、用不了」。`model` 消息现在也能覆盖它（§4），但 env
		//   是「切过去就能用」的那条路：老 GUI 不发 model 字段时它是唯一来源。
		providerEnv[id] = llmConfig{BaseURL: base,
			Model: os.Getenv(prefix + san + "_MODEL"), Key: key}
	}
	logf("provider registry from environment: %d entr%s", len(providerEnv),
		map[bool]string{true: "y", false: "ies"}[len(providerEnv) == 1])

	lines := make(chan wire.StampedLine, 64)
	var curState atomic.Int32
	curState.Store(wire.CodeIdle)
	go wire.ReadStdin(lines, &curState, logf)

	s := newSession(
		llmConfig{BaseURL: *baseURL, Model: *model, Key: secret,
			ProviderID: *providerID, Context: *contextTokens, MaxOutput: *maxOutput},
		strings.Split(*allowTools, ","), providerEnv, *resumeID, *sessionID, *workspace, *globalMemory, *failSummarize,
		agentOpts{on: *agents, maxParallel: *maxAgents, maxTurns: *agentMaxTurns, timeout: *agentTimeout},
		*mcpConfig, *mcpTimeout,
		lines, &curState, logf, os.Stdout)
	if err := s.run(); err != nil {
		logf("fatal: %v", err)
		os.Exit(1)
	}
}
