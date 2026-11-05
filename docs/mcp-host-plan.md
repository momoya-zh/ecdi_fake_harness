# MCP Host 接入方案与落地记录

日期：2026-11-05。前置：`docs/positioning.md` §5（MCP 决策 A：harness 管理 MCP 配置）、L1 沙盒（`ed85206`）、后台长任务（`5f6e1f4`）。
状态：**已落地并复核修订（2026-11-05 二版）**。

> ★ 二版说明：一版（外部实现）把功能跑通了，评审发现 8 处需要收口的地方，
> 二版全部改完并补了判据。改动清单见 §六「修订记录」——本文正文描述的是
> **现在**的行为，不是一版的行为。

## 一、 用户需求与背景

在完成可用性差距评估前三项（① ask 提案卡、② run_command L1 沙盒、③ 后台长任务）之后，进入能力侧核心大件：
**④ MCP Host（能力接入与调度）**。

**现状与动机**：
- `ecdi-core` 有 **9 件内置工具**（只读三件 `read_file`/`list_dir`/`grep` + 写入执行三件
  `write_file`/`edit_file`/`run_command` + 作业三件 `job_output`/`job_kill`/`job_list`，后者协议 v6）
  以及子代理 `task` 委派；
- 面对专业场景（数据库查询、Git 复杂操作、外部 API/服务集成），硬编码内置工具扩展性差；
- 本项目在 `docs/positioning.md` §5.2 早已由用户拍板**决策 A（harness 管理 MCP 配置）**；
  此前因未接真 core 暂缓，现前置障碍已全部扫清。

## 二、 决策与取舍

1. **协议层零改动（复用已有契约）**：MCP 调用经 core 翻译成既有的 `tool_call` / `ask` 门 /
   `tool_result` 块；协议保持 **v6**，不新增类型；GUI 转录渲染零改动。
   ★ `hello.caps` 增 `"mcp"`（只是能力声明，GUI 对未知 cap 宽容，见 `PROTOCOL` §3.1）。

2. **零第三方依赖**：纯标准库（`os/exec`、`bufio`、`encoding/json`、`hash/fnv`、`sort`、`sync`）
   实现 stdio JSON-RPC 2.0 客户端。

3. **传输层只做 stdio**；HTTP / Streamable HTTP 留作后续。

4. **★ 规范版本锚：刻意降到 `2024-11-05`（连接级 initialize），并把这个偏差写在这里。**
   `positioning.md` §5.3 记的锚是 **`2026-07-28`**（无状态、逐请求能力协商、**不再是连接级
   `initialize` 会话**）。实现选了旧版握手，理由是**存量 server 兼容**：市面上的 stdio server
   绝大多数仍实现 `initialize` + `notifications/initialized` 这一版；新版无状态模型要等 server
   侧普及。代价与边界：
   - 两版在 `tools/list` / `tools/call` 上形状相同，功能面不受影响；
   - 服务端可以在 `initialize` 回执里挑一个**不同**的 `protocolVersion`（规范允许）。客户端
     **不中断**、但会往 stderr 打一行协商差异 —— 静默继续会让语义差异永远无人知晓（与 I4
     的「缺口显式提示」同一口径）；
   - 将来切到无状态模型时，改的是 `startAndInit` 的握手序列，不是调用面。
   ★ 这条偏差一版是**没写**的（一版把「2024-11-05 是行业标准协议」当依据），属于与自家文档
   正面冲突；二版按「要么按锚走、要么把降级理由写下来」补上。

5. **命名空间与 provider 字符集（★ 二版新增，这条不改会打死整轮请求）**
   - 工具名对外统一为 `mcp__<server>__<tool>`，避免与内置 9 件套冲突；
   - 但 `function.name` 在 OpenAI 兼容端点（含 DeepSeek）上是 `^[a-zA-Z0-9_-]{1,64}$`，
     而 **MCP 规范对工具名没有任何约束**（`git.status` / 带空格 / 中文都合法）。
     ⇒ 入库前压缩：非 `[A-Za-z0-9_-]` 逐 rune 折成 `_`；超 64 保留可读前缀 + `fnv32a` 8 位哈希
     （同一对 `(server, tool)` 恒得同名，可复现）；压完撞名（`git.status` vs `git_status`）
     后者强制哈希形，**绝不静默覆盖**（`toolDefs` 里两份同名定义照样 400）。
   - 名字只是发给模型的外号：真实调用走 `mcpToolRef.origName`，压名字无损。

6. **配置发现三级回退**：`--mcp-config=<path>` → `<workspace>/.ecdi/mcp.json` 或
   `<workspace>/mcp.json` → `%APPDATA%/ecdi-fake-harness/mcp.json`（非 Windows 走
   `$HOME/.config/...`）。格式 `{"mcpServers": {"<name>": {"command","args","env","disabled"}}}`。
   ★ 显式路径打错**不再静默**：打一行 warning 并本次不启用（一版是完全无声的）。

7. **生命周期与安全边界**：
   - **整树清退**（★ 二版重做）：不是 `Process.Kill()`（那只有直系子进程，`npx` 拉起的
     node 会活下来）。Windows 侧用 **Job Object + `KILL_ON_JOB_CLOSE`**，`CREATE_SUSPENDED`
     启动→挂进作业→恢复（关掉「挂接前派生逃逸」的窗口）；POSIX 侧用**独立进程组** + 杀 `-pgid`。
     句柄无条件关闭（不关就是每会话泄一个内核对象）。**纳管失败只降级 + 告警，不 fail-closed**
     —— MCP 不是安全边界（§5.2「harness 只做配置 + 展示 + 转发」），这里要的是孤儿清理，不是沙盒。
   - **敏感环境变量剔除**：`childEnv()` 剔 `ECDI_*_KEY`（同 `tools.go` 纪律）。
     ★ 已知取舍：除此之外是**全量继承**用户环境（`OPENAI_API_KEY` / `GITHUB_TOKEN` 也能被 server
     读到）。与 `run_command` 同一口径；但 MCP server 是第三方包、无人值守、可反复调用，
     风险面不同。真要收紧应做成显式开关（`--mcp-env=minimal`），不在本期。
   - **默认逐次过 ask 门**：MCP 工具不在 `--allow-tools` 默认值里；可显式精确豁免单个
     `mcp__<srv>__<tool>`。
   - **子代理隔离**：深度 ≥1 的子代理**根本没拿到** MCP 工具（`toolDefsFor(false,1)`），
     不是运行时拒绝。
   - **工具输出上限 24KB**（★ 二版新增，与 `run_command` 同口径）：超限保留头尾各半并**显式标注**
     「中间省略 N 字节」。一版没有上限，而读循环允许单行 4MB ⇒ 一次大表 dump 就能打爆上下文。
   - **崩溃要说出来**（★ 二版新增）：服务进程退出后工具仍在表里，调用会返回明确错误文本；
     并且 hub 登记一次，会话在下一次工具收尾时发一条 `system` 块（「MCP 服务 X 已退出，
     本会话不会自动重启它」）。一版只有模型看得到错误字符串，界面上什么都看不到。
   - **不做 respawn**：崩掉就是崩掉，本会话不自动重启（避免「重启—再崩」循环刷屏）。

8. **ask 的提案卡：MCP 工具暂不带 `detail`（★ 二版撤销）**
   `ask.detail.tool` 在协议 §3.6 里是**判别式**（枚举 `write_file`/`edit_file`/`run_command`/`task`），
   GUI 按它 switch（`AppTranscript.cpp:833/850/863`），未知值只落一行「（提案类型 X）」。
   MCP 的判别值只能是 `mcp__<srv>__<tool>` 复合名 ⇒ 一版发出去的结果是：卡片建出来、
   默认展开、里面只有一行垃圾，`note` 里的服务/工具/参数一个字都不显示。
   **发一个消费者不认识的 detail 比不发更糟**（它假装有提案）。现在回到问句路径，
   `askPrompt` 的 MCP 分支带 200 字符参数摘要；等 GUI 那批加 `tool:"mcp"` 分支后两边一起改。

## 三、 实现架构与分工

| 模块 | 职责 | 状态 |
|---|---|---|
| `core/cmd/ecdi-core/mcp.go` | JSON-RPC 2.0 协议层、`mcpClient`（单 server 的 stdio 握手/调用）、`mcpHub`（多 server 注册、配置解析、工具发现与调度、死亡登记） | ✅ |
| `core/cmd/ecdi-core/mcp_proc_windows.go` | 整树纳管：Job Object（`KILL_ON_JOB_CLOSE`）+ 挂起启动→挂接→恢复 | ✅ 二版新增 |
| `core/cmd/ecdi-core/mcp_proc_other.go` | 整树纳管：独立进程组 + 杀 `-pgid` | ✅ 二版新增 |
| `core/cmd/ecdi-core/mcp_test.go` | 单测：mock server、握手、schema 规约、名字压缩、输出上限、超时/崩溃、死亡通告、分页、**整树清退（心跳判据）**、多服务、E2E | ✅ 11 案全绿 |
| `main.go` | `--mcp-config` 参数 | ✅ |
| `tools.go` | `askPrompt` MCP 问句；`trimOutputTo`（截断语义抽成可配上限） | ✅ |
| `session.go` | `toolDefs` 聚合 MCP 工具；`execTool` 统一路由；`emitMCPDeadNotices`；退出清理 | ✅ |
| `diff.go` | **不再**给 MCP 发 `detail`（理由见 §二.8） | ✅ 二版回退 |
| `docs/` + `README` | 决策、偏差、已知缺口 | ✅ 二版重写 |

## 四、 验收读数（2026-11-05 二版，实测）

1. **单元与 E2E**（`go test -run 'TestMCP_|TestLoopE2E_MCPTool'`）：**11 案全绿**，
   `-race -count=3` 无竞态报告。新增的判据都带**区分力**：
   - `TestMCP_ProcessTreeKilled`：孙进程心跳文件在 `hub.Close()` 后必须停止生长。
     ★ 做了变异验证——把整树句柄拿掉（只留 `Process.Kill`）这条**会红**
     （实测 `心跳 25 -> 75 字节`），证明它测的是「整树」而不是「调用过 kill」；
   - `TestMCP_ToolNameSanitization`：`^[a-zA-Z0-9_-]{1,64}$` + 同名同输入恒同名 + 撞名消解；
   - `TestMCP_OutputClip`：超限必须带「中间省略」标注，未超限逐字不动；
   - `TestMCP_DeadServerNotice`：崩溃后必须**非空**说明，且死亡通告「只说一次」；
   - `TestMCP_ToolsListPagination`：`nextCursor` 追到底，两页工具都在。
2. **E2E 口径修正**：`TestLoopE2E_MCPTool` 原先断言 core 自己发的
   `detail.tool/server/mcpTool` 三个字段——那是**把错误的契约认证成了绿的**。
   现在断言的是 `ask` **没有** `detail`，并核对问句里带服务名与工具名。
3. **回归门**：`go build ./...` OK · `go vet` 干净 · `GOOS=linux` / `GOOS=darwin/arm64`
   交叉编译 OK · `protocol/validate.py` 17/17 · `tools/precommit-check.py` BOM 漂移 0。
4. **全量单测**（`go test ./...`）连跑 **5 次全绿**。★ 顺带修掉了一条**既存 flaky**
   （`TestLoopE2E_BackgroundJob`，来源 `5f6e1f4`，与 MCP 无关）：它在 `bye` 之后断言
   echo 应以 `exited/0` 结束，而脚本第 3 轮发的正是 `job_kill` —— `job_output` 的语义是
   「有输出就回」不是「等退出」，于是 kill 常常赢在 echo 前面，整包跑约 2/3 概率红、单跑全绿。
   **不是产品缺陷，是测试自己前后矛盾**：断言要「正常退出」，脚本却在杀它。
   修法：第 3 轮改 `job_list`（同样是「控制工具不过 ask 门」的证据，`job_kill` 的语义由
   `jobs_test.go:297` 的 `toolKill` 单测覆盖）+ bye 之前先等 `job end`。

## 五、 明确的已知缺口（下一步的输入）

| 缺什么 | 说明 |
|---|---|
| HTTP / Streamable HTTP 传输 | 只有 stdio 进程。远程或持久连接留作扩展（传输层现在写死在 `startAndInit` 里） |
| 提案卡（GUI 侧） | `ask.detail` 缺 `tool:"mcp"` 判别值与对应渲染分支；补的时候 core 与 GUI 同批改 |
| 产品面可见性 | `positioning` §5.1 的图把「配置管理」画在 `harness_gui` 上，实现是 core 全包、GUI 零入口 ⇒ 用户写了 `mcp.json` 界面上没有任何确认（只有 stderr）。至少要有「MCP：N 个服务 / M 个工具」的只读展示 + 启动失败告警 |
| 取消语义 | 取消只让我们不再等，server 侧调用照跑（没发 `notifications/cancelled`），而 GUI 已显示「已取消」 |
| respawn | 崩掉不重启（刻意的，见 §二.7） |
| 工具入参 schema 透传 | MCP 的 `inputSchema` 原样上交；`$schema` / `$defs` 之类在部分端点可能被拒，未验证也未剥离 |
| 单测密闭性 | `findMCPConfigFile` 的第三级读用户主目录；测试侧已用 `isolateMCPDiscovery` 把 `APPDATA`/`HOME`/`USERPROFILE` 指到临时目录，但**被测代码**仍会读真主目录（生产行为正确，测试靠环境覆盖） |

## 六、 修订记录（一版 → 二版）

| # | 一版 | 二版 |
|---|---|---|
| 1 | 工具名原样 `mcp__srv__tool` 上交 | 压到 `[A-Za-z0-9_-]{1,64}` + 哈希/撞名消解 |
| 2 | MCP 工具发 `detail`（`tool`=复合名） | 不发（GUI 判别式不接受），走问句 + 200 字参数摘要 |
| 3 | 输出无上限 | 24KB 头尾各半 + 显式标注 |
| 4 | `Process.Kill()` 被描述成「整树清退」 | 真整树（Windows Job Object / POSIX 进程组），失败降级并告警 |
| 5 | 规范锚 2024-11-05 未记录偏差 | 写进 §二.4，且协商差异打日志 |
| 6 | 串行 10s×N 握手挡在 hello 前 | 并发 + 5s；服务名排序保证 `toolDefs` 顺序稳定 |
| 7 | 崩溃后只有错误字符串 | hub 登记 + 会话发 `system` 块 |
| 8 | 「全量单测全绿」 | 改为「本功能 11 案全绿 + 已知 flaky 单列」→ 连 flaky 一起修掉，回到 5/5 全绿 |
| 9 | `HasTool` → `CallTool` 的 `handled` 被丢弃 | `execTool` 统一入口，`handled=false` 回落内置分发 |
| 10 | `done` 双 close 无互斥 / 阻塞写持锁 | `sync.Once`；写走独立 `wmu` |
