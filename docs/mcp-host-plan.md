# MCP Host 接入方案与落地记录

日期：2026-11-05。前置：`docs/positioning.md` §5（MCP 决策 A：harness 管理 MCP 配置）、L1 沙盒（`ed85206`）、后台长任务（`5f6e1f4`）。
状态：**已落地并复核修订（2026-11-05 二版；同日三版补入参 schema 规约；2026-11-06 四版放开 ask 提案卡、五版补产品面可见性）**。

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

2. **零第三方依赖**：纯标准库（`os/exec`、`bufio`、`encoding/json`、`hash/fnv`、`sort`、`sync`、
   `net/http`）实现 stdio / Streamable HTTP / legacy HTTP+SSE 的 JSON-RPC 2.0 客户端。

3. **传输层（2026-11-06 六版更新）**：stdio（子进程）、Streamable HTTP（单端点 POST，
   响应 JSON 或 SSE）、legacy HTTP+SSE（GET endpoint + POST + SSE 回执）。
   配置：`command`（stdio）与 `url`（远程）二选一；`transport` 省略时按字段推断
   （`url` ⇒ `http`），`transport:"sse"` 显式走 legacy。详见 §九。

4. **★ 规范版本锚：stdio/legacy 降到 `2024-11-05`，Streamable HTTP 用 `2025-06-18`（连接级
   initialize 的最后一版），两者都把这个偏差写在这里。**
   `positioning.md` §5.3 记的锚是 **`2026-07-28`**（无状态、逐请求能力协商、**不再是连接级
   `initialize` 会话**）。实现选了旧版握手，理由是**存量 server 兼容**：市面上的 server
   绝大多数仍实现 `initialize` + `notifications/initialized` 这一版；新版无状态模型要等 server
   侧普及。代价与边界：
   - 各版在 `tools/list` / `tools/call` 上形状相同，功能面不受影响；
   - 服务端可以在 `initialize` 回执里挑一个**不同**的 `protocolVersion`（规范允许）。客户端
     **不中断**、但会往 stderr 打一行协商差异 —— 静默继续会让语义差异永远无人知晓（与 I4
     的「缺口显式提示」同一口径）；
   - Streamable HTTP 用 2025-06-18 而不是 2024-11-05：后者没有这个传输（只有 legacy
     HTTP+SSE），用旧版本号会让现代 server 直接 400；
   - 将来切到无状态模型时，改的是 `startAndInit` 的握手序列与 `ProtocolVersion`，不是调用面。

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
     ★ 已知取舍：除此之外**默认仍是全量继承**用户环境（`OPENAI_API_KEY` / `GITHUB_TOKEN` 也能被
     server 读到）。与 `run_command` 同一口径；但 MCP server 是第三方包、无人值守、可反复调用，
     风险面不同。★ **七版（2026-11-06）已给显式开关**：`--mcp-env=minimal` 只给白名单
     （PATH/SystemRoot/TEMP/HOME/APPDATA 等），默认 `inherit` 保持既有行为——见 §十。
   - **默认逐次过 ask 门**：MCP 工具不在 `--allow-tools` 默认值里；可显式精确豁免单个
     `mcp__<srv>__<tool>`。
   - **子代理隔离**：深度 ≥1 的子代理**根本没拿到** MCP 工具（`toolDefsFor(false,1)`），
     不是运行时拒绝。
   - **工具输出上限 24KB**（★ 二版新增，与 `run_command` 同口径）：超限保留头尾各半并**显式标注**
     「中间省略 N 字节」。一版没有上限，而读循环允许单行 4MB ⇒ 一次大表 dump 就能打爆上下文。
   - **崩溃要说出来**（★ 二版新增）：服务进程退出后工具仍在表里，调用会返回明确错误文本；
     并且 hub 登记一次，会话在下一次工具收尾时发一条 `system` 块（「MCP 服务 X 已退出，
     本会话不会自动重启它」）。一版只有模型看得到错误字符串，界面上什么都看不到。
   - **respawn（七版修订，2026-11-06）**：二版刻意「崩掉就是崩掉」，本会话不自动重启
    （避免「重启—再崩」循环刷屏）。七版改为**按需、有上限**的重启：只在**调用之前**
    发现服务已死时重启（绝不在调用失败后自动重试——工具可能有副作用），每服务每会话
    上限 3 次，`--mcp-respawn=false` 可退回旧行为。理由与边界见 §十。

8. **ask 的提案卡：MCP 工具自 2026-11-06 起带 `detail`（★ 四版：与 GUI 同批放开；二版撤销记录保留）**
   `ask.detail.tool` 在协议 §3.6 里是**判别式**，GUI 按它 switch，未知值只落一行「（提案类型 X）」。
   一版把 `mcp__<srv>__<tool>` 复合名整个塞进 `detail.tool` ⇒ 卡片建出来、默认展开、
   里面只有一行垃圾。**发一个消费者不认识的 detail 比不发更糟**（它假装有提案）——
   二版撤销，回到问句路径。四版（2026-11-06）按预告与 GUI 渲染分支**同批**放开：
   判别值固定 `"mcp"`，服务名与真实工具名拆进 `server` / `toolName`，参数以 `argsKv`
   （`[{k, v}]`，键排序、值 JSON 文本化、单值 2048 码点截断）逐键成行；
   问句同批改用真实工具名（`hub.Lookup` 还原，`git.status` 而非 `git_status`）。
   GUI 走既有折叠卡机制零新机制；冒烟断言随批翻转（28→32 条）。
9. **入参 schema 规约（2026-11-05 三版补）**：`inputSchema` 不再原样上交——
   递归删 `$schema`，把本地 `$ref`（`#/$defs/...`、`#/definitions/...`）就地展开，
   再删 `$defs` / `definitions`。部分 OpenAI 兼容端点对元关键字不宽容，而失败是
   **整轮请求 400**，不是单个工具不可用。递归引用无法有限展开时，该处降级为 `{}`
   并在 stderr 点名（服务端仍会校验真实参数）；外链 `$ref` 无法展开，原样保留。
   ★ 剥键只发生在 schema 对象上：`properties` 里恰好叫 `$defs` 的**属性名**不受影响
   ——对任意 map 无脑删键会把用户参数名删掉。

## 三、 实现架构与分工

| 模块 | 职责 | 状态 |
|---|---|---|
| `core/cmd/ecdi-core/mcp.go` | JSON-RPC 2.0 协议语义（`mcpClient`：initialize / tools/list / tools/call）、`mcpHub`（多 server 注册、配置解析、工具发现与调度、死亡登记、**入参 schema 规约**、**启动期状态快照 `Status()`**） | ✅ |
| `core/cmd/ecdi-core/mcp_transport.go` | 传输接口 `mcpTransport`（Start/Do/Close/Done/ProtocolVersion）与构造分派 | ✅ 六版新增 |
| `core/cmd/ecdi-core/mcp_stdio.go` | stdio 传输：子进程、stdin/stdout、pending、整树清退（从 `mcpClient` 原样搬入） | ✅ 六版拆分 |
| `core/cmd/ecdi-core/mcp_http.go` | Streamable HTTP（POST + JSON/SSE 响应、session/版本头、DELETE）与 legacy HTTP+SSE（GET endpoint + POST + SSE 回执） | ✅ 六版新增 |
| `core/cmd/ecdi-core/mcp_proc_windows.go` | 整树纳管：Job Object（`KILL_ON_JOB_CLOSE`）+ 挂起启动→挂接→恢复 | ✅ 二版新增 |
| `core/cmd/ecdi-core/mcp_proc_other.go` | 整树纳管：独立进程组 + 杀 `-pgid` | ✅ 二版新增 |
| `core/cmd/ecdi-core/mcp_test.go` + `mcp_http_test.go` | 单测：mock server（stdio + httptest HTTP/SSE）、握手、schema 规约/剥离、名字压缩、输出上限、超时/崩溃、死亡通告、分页、**整树清退（心跳判据）**、多服务、**启动状态/配置错误可见**、**HTTP 头/SSE/会话/重定向/E2E** | ✅ 22 案全绿 |
| `tools/mcp-smoke/` | 真链路冒烟：真 core 二进制 + stdio server + HTTP/SSE server + 脚本化 SSE，四臂 **64 条断言**（含 ask 门、**提案卡 detail**、**`hello.mcp` 状态与失败块**、**Streamable/legacy 传输与头**、参数穿越、bye 后无残留）；已接进 `regress.py` §2f（缺二进制 **SKIP** / 陈旧二进制 **FAILED** / 超时 **FAILED** + 看门狗） | ✅ |
| `main.go` | `--mcp-config` 参数 | ✅ |
| `tools.go` | `askPrompt` MCP 问句（四版起用**真实工具名**）；`trimOutputTo`（截断语义抽成可配上限） | ✅ |
| `session.go` | `toolDefs` 聚合 MCP 工具；`execTool` 统一路由；`emitMCPDeadNotices`；退出清理；**`hello.mcp` + 启动失败 `system` 块** | ✅ 五版新增 |
| `diff.go` | MCP 提案卡 `mcpAskDetail`（判别值 `"mcp"` + server/toolName/argsKv；理由与教训见 §二.8） | ✅ 四版同批放开 |
| `gui/src/AppMcp.cpp` | 会话头副标 `MCP N 个服务 / M 个工具`、检视栏「MCP」卡（来源/服务/失败/禁用/远程传输标识）、失败行渲染 | ✅ 五版新增；六版加 transport |
| `gui/src/ProbeMcp.cpp` | `--mcp-probe`：模型 `hello.mcp` ⇔ 副标/卡片/失败行的两路对齐 + 负对照 | ✅ 五版新增 |
| `docs/` + `README` | 决策、偏差、已知缺口 | ✅ 二版重写；五版补可见性；六版补 HTTP/SSE |

## 四、 验收读数（2026-11-05 二版 + 同日三版补丁 + 2026-11-06 四/五版增补，实测）

1. **单元与 E2E**（`go test -run 'TestMCP_|TestLoopE2E_MCPTool'`）：**22 案全绿**，
   `-race -count=3` 无竞态报告。新增的判据都带**区分力**：
   - `TestMCP_ProcessTreeKilled`：孙进程心跳文件在 `hub.Close()` 后必须停止生长。
     ★ 做了变异验证——把整树句柄拿掉（只留 `Process.Kill`）这条**会红**
     （实测 `心跳 25 -> 75 字节`），证明它测的是「整树」而不是「调用过 kill」；
   - `TestMCP_ToolNameSanitization`：`^[a-zA-Z0-9_-]{1,64}$` + 同名同输入恒同名 + 撞名消解；
   - `TestMCP_InputSchemaStripping`：`$schema`/`$defs`/`definitions` 必须消失；
     本地 `$ref` 展开后语义仍在（含兄弟 `description`、`items` 内的引用）；递归引用
     降级为 `{}` 且同一 ref 只报一条 warning；`properties` 里叫 `$defs` 的**属性名**
     不能被误删；外链 `$ref` 原样保留；原 `InputSchema` 不被改写。
   - `TestMCP_OutputClip`：超限必须带「中间省略」标注，未超限逐字不动；
   - `TestMCP_DeadServerNotice`：崩溃后必须**非空**说明，且死亡通告「只说一次」；
   - `TestMCP_ToolsListPagination`：`nextCursor` 追到底，两页工具都在；
   - `TestMCP_HubStatus`（五版新增）：好服务带工具数、坏服务带非空原因、禁用不混进
     Services/Failed；无配置时 `hasEvidence()` 为假（不得长出 0 服务卡）；
   - `TestMCP_ConfigProblemVisible`（五版新增）：显式路径不存在 ⇒ hub 非 nil 且
     `status.Problem` 非空（不静默退回下一级）；坏 JSON ⇒ 返回 hub + error，状态仍可见。
   - `TestMCP_HTTPStreamableJSON`（六版新增）：JSON 回执 + `Mcp-Session-Id` /
     `MCP-Protocol-Version` 逐请求回带 + `Close` 发 DELETE；
   - `TestMCP_HTTPStreamableSSE`（六版新增）：POST 返回 SSE（keepalive / notification /
     网络分片）仍能按 id 找到回执；
   - `TestMCP_HTTPAuthError` / `TestMCP_HTTPRedirectRejected`（六版新增）：
     Authorization 到达服务端；401/302 显式失败且错误文本不泄 token；
   - `TestMCP_LegacySSE`（六版新增）：GET endpoint → POST `/messages` → SSE 回执；
     断言 initialize 也走 POST、版本为 2024-11-05；
   - `TestMCP_HTTPStartFailureStatus`（六版新增）：连接拒绝 / `command+url` 坏配置
     必须进 `Failed`，不得留空服务；
   - `TestMCP_TransportKindValidation` / `TestScanSSE`（六版新增）：配置互斥、scheme、
     headers/env 归类；SSE 的 CRLF / 注释 / 多行 data / 无尾空行。
2. **E2E 口径**：`TestLoopE2E_MCPTool` 四版起断言 `ask` **带** `detail`（判别值
   `"mcp"`、server/toolName 真名、argsKv 与调用参数一致）；五版同批硬化：
   配置里加一个必然失败的服务，断言 `hello.mcp.services` 为 mock/6、`failed` 含
   `broken` 且 error 非空，并等到一条 `[MCP] 服务 broken 启动失败` 的 `system` 块。
   六版新增 `TestLoopE2E_MCPToolHTTP`：Streamable HTTP 走完整循环
   （`hello.mcp.services[].transport == "http"` → ask 提案卡 → 真实 HTTP 调用 → idle）。
3. **回归门**：`go build ./...` OK · `go vet` 干净 · `GOOS=linux` / `GOOS=darwin/arm64`
   交叉编译 OK · `protocol/validate.py` **18/18** · `tools/precommit-check.py` BOM 漂移 0。
4. **全量单测**（`go test ./...`）连跑 **5 次全绿**。★ 顺带修掉了一条**既存 flaky**
   （`TestLoopE2E_BackgroundJob`，来源 `5f6e1f4`，与 MCP 无关）：它在 `bye` 之后断言
   echo 应以 `exited/0` 结束，而脚本第 3 轮发的正是 `job_kill` —— `job_output` 的语义是
   「有输出就回」不是「等退出」，于是 kill 常常赢在 echo 前面，整包跑约 2/3 概率红、单跑全绿。
   **不是产品缺陷，是测试自己前后矛盾**：断言要「正常退出」，脚本却在杀它。
   修法：第 3 轮改 `job_list`（同样是「控制工具不过 ask 门」的证据，`job_kill` 的语义由
   `jobs_test.go:297` 的 `toolKill` 单测覆盖）+ bye 之前先等 `job end`。
5. **真链路（调用级）冒烟（2026-11-05 21:00，`tools/mcp-smoke/drive.py`；四版增补提案卡、五版增补 `hello.mcp`、六版增补 HTTP/SSE、七版增补 F 臂）**：不是只握手——
   一次真实工具调用从模型请求穿到 server 再穿回来。真 `ecdi-core.exe` + 真 stdio server
   + 真 HTTP/SSE server（python）+ 脚本化 SSE 端点，**五臂 83 条断言全绿**：
   - **A 正常**：`hello protocol=6` 且 `caps` 增 `"mcp"`；服务端协商到 `2025-06-18`（只告警，
     见 §二.4）；`tools/list` 两页全追（2 个工具）；上交模型的压缩名是
     `mcp__smoke__git_status` 与 `mcp__smoke______`（中文名逐 rune 折 `_`，全部 ≤64 且落在
     `[A-Za-z0-9_-]`）；**ask 默认拦下**且**带提案卡 detail**（判别值 `mcp`、server/toolName
     为真名 `smoke`/`git.status`、argsKv 与调用参数一致——问句亦用真实名）；allow 后
     `tools/call` 真执行（server 回执 `ok: {"short": true}`，证明参数穿过 JSON-RPC）；结果
     回模型、第二轮收尾回 `idle`；bye 退出码 0 且 server **无残留**（整树清退的第三个独立判据）。
   - **B 降级**：不存在的命令 + 正常服务 ⇒ 坏服务被点名（`启动服务 broken 失败`）、好服务
     照常注册 2 个工具、`caps` 仍带 `mcp`、退出码 0；五版起同臂断言
     `hello.mcp.services` 含 smoke/2、`failed` 含 broken 且 error 非空，并等到一条
     `[MCP] 服务 broken 启动失败` 的 `system` 块。
   - **边界（如实）**：模型端点是脚本化 SSE，不是真厂商端点；MCP server 是仓库自带 mock；
     不起 GUI ⇒ **GUI 渲染不在本套覆盖范围内**（提案卡由 `--proposal-probe`、MCP 状态由 `--mcp-probe` 的夹具覆盖）。读数落 `.scratch/mcp-smoke/`
     （`result.json` + `fake-llm.log`，gitignored），复跑只差一条 `go build`。
   - **C Streamable HTTP**：`POST /mcp`；server 严格校验 `Mcp-Session-Id` 与
     `MCP-Protocol-Version`（错一个就 400）⇒ `hello.mcp.services[].transport=="http"`、
     ask 提案卡（server/remote、toolName/git.status）、`tools/call` 真实走 SSE 回执
     （`ok: {"short": true}`）、第二轮 idle；`Close` 必须发 DELETE（server 计数）。
   - **D legacy HTTP+SSE**：`GET /sse` 取 `endpoint`，initialize 也走 `POST /messages`，
     回执从 SSE 流回来；`hello.mcp...transport=="sse"`；ask/结果/idle 同 C。
   - **E HTTP 不可达**：连接被拒 ⇒ `caps` 不含 mcp、`hello.mcp.failed` 非空、
     一条 `[MCP] 服务 remote 启动失败` system 块，退出码 0（失败不挡会话）。
   - **F 运行期（七版）**：`--mcp-env=minimal` + 脚本化三步（`dev.crash` → `dev.env` → 收尾）。
     崩溃调用 `tool_result ok=false`、system 块预告「下次调用将尝试自动重启」；
     第二次调用前服务被拉起（system 块「第 1/3 次」），`dev.env` 回包 **`env:<>`**
     （父进程的 `SMOKE_LEAK` 没进子进程，minimal 生效）；退出码 0。
   - **回归门**：`tools/regress.py` §2f 已接线——判据是 `passed==83`（钉死，少一条即红）、
     `failed==0`、`rc==0`。缺二进制显式 **SKIP**（§2e 的 rebuild 已计过失败，不重复计数）；
     二进制比 `core/**/*.go` 旧按 **FAILED**（测旧代码比不测更糟）；超时按 FAILED（drive.py
     自带 90s 看门狗，先清子进程再退出）。负对照实测：拿掉 `server.py` ⇒ **A/B/F 红、
     C/D/E 仍绿**（HTTP 臂不依赖 stdio；F 用的是同一个真 stdio server，因此也依赖它）
     ——这正是「各臂独立」的证据；HTTP 失败路径由常驻的 E 臂覆盖。
## 五、 明确的已知缺口（下一步的输入）

| 缺什么 | 说明 |
|---|---|
| HTTP 服务端通知流 | Streamable HTTP 只实现了「请求-响应」方向；GET 上的服务端主动通知、legacy 的重连/`Last-Event-ID` 不在本期（§九边界） |
| 单测密闭性 | `findMCPConfigFile` 的第三级读用户主目录；测试侧已用 `isolateMCPDiscovery` 把 `APPDATA`/`HOME`/`USERPROFILE` 指到临时目录，但**被测代码**仍会读真主目录（生产行为正确，测试靠环境覆盖） |

★ **HTTP/SSE 传输已于 2026-11-06 闭合（六版，§九）**：Streamable HTTP + legacy
HTTP+SSE（`transport:"sse"`）、`url/headers` 配置、状态里的 transport 标识、四臂冒烟。
本表不再把它列为缺口。

★ **产品面可见性已于 2026-11-06 闭合（五版，§八）**：`hello.mcp` 启动期状态 +
会话头副标 + 检视栏「MCP」卡 + 失败 `system` 块 + `--mcp-probe` 正反对照。
本表不再把它列为缺口。

★ 提案卡（GUI 侧）与问句工具名两项已于 2026-11-06 闭合（§二.8 四版）：`tool:"mcp"`
判别值 + 渲染分支 core/GUI/协议/测试同批落地，问句经 `hub.Lookup` 用真实工具名。

★ **取消语义与 respawn 已于 2026-11-06 闭合（七版，§十）**：取消会真发
`notifications/cancelled`（三种传输）并把「送没送到」写进 tool_result；stdio 服务
崩溃后按需重启（每会话每服务上限 3 次，`--mcp-respawn=false` 退回旧行为）。
本表不再把这两项列为缺口。


★ 入参 schema 的 `$schema` / `$defs` 已不再“原样透传”：三版补入规约（§二.9 / §七），
本表不再把它列为缺口。真厂商端点是否接受**外链** `$ref` 仍未验证，但本地 `$ref`
已展开、两个元关键字已消失。

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

## 七、 三版补丁（2026-11-05）：入参 schema 规约

- **问题**：一/二版把 MCP 的 `inputSchema` 原样上交。zod / pydantic 生成的 schema
  常带 `$schema`、`$defs` 与指向 `$defs` 的 `$ref`；部分 OpenAI 兼容端点对这类
  元关键字不宽容，失败是**整轮请求 400**——一个 MCP 服务就能把会话打死。
- **改法**：`mcpToolDefNamed` 先过 `sanitizeMCPInputSchema`——递归删 `$schema`，
  本地 `$ref` 就地从 `$defs` / `definitions` 展开，再删这两个容器；同层兄弟键
  （如 `description`）保留并覆盖目标同名字段。递归引用无法有限展开时该处降级为
  `{}`，同一个 ref 只往 stderr 报一条；外链 `$ref` 原样保留。全程深拷贝，不改
  服务端给的原始结构；剥键只发生在 schema 对象上，属性名层不剥。
- **判据**：`TestMCP_InputSchemaStripping`（本地引用展开 / 递归降级 / 属性名
  `$defs` 不误删 / 外链保留 / 原 schema 不变）；`TestMCP_SchemaSanitization`
  仍盖 nil schema 的默认补齐。MCP 相关 **12 案全绿**。
- **未验证**：真厂商端点是否接受**外链** `$ref`，以及各家对 `$defs` 的实际拒收
  行为——本补丁只保证我们不再主动上交这两个元关键字；远端引用不替服务端展开。
- **诚实边界**：展开是文本级深拷贝，同一 `$defs` 被多处引用时会被复制，请求体
  可能变大；当前**没有**节点/字节预算，指数级 `$ref` 图理论上可放大请求体。
  MCP 不是安全边界（§二.7），这条按已知边界记录，不假装它是沙盒。

## 八、 五版（2026-11-06）：产品面可见性

- **问题**：`positioning` §5.1 把「配置管理」画在 `harness_gui` 上，实现是 core 全包、
  GUI 零入口 ⇒ 用户写了 `mcp.json` 界面上没有任何确认（只有 stderr）；服务启动失败
  与「根本没配」在界面上完全一样。MCP 的 ask 提案卡与工具名已在四版闭合，本版补的
  是**配置/启动状态**这最后一块产品面。
- **数据通道**：`hello` 增可选 `mcp` 对象（§3.1 / 协议 §3.1）：
  `source`（explicit/workspace/global）、`config`（实际路径）、`services:[{name,tools}]`、
  `failed:[{name,error}]`、`disabled`、`problem`（配置层错误）。数组恒为数组、按服务名
  升序；只要发现/尝试过配置就发（全失败/全禁用也发），完全没配置则不发。
  ★ 与 `caps.mcp` **正交**：`caps.mcp` 仍只表示"已注册 ≥1 个工具"；失败时 caps 不声明，
  但 `hello.mcp.failed` 必须让 GUI 能说清楚。
- **失败告警**：`hello` 之后逐条发 `system` 块（与运行期 `TakeNotices` 同形态）：
  `[MCP] 服务 <name> 启动失败：<error>（本次未启用）`、`[MCP] 配置错误：<problem>`。
  成功不刷屏——状态在副标/卡里。产品模式默认视图（用户模式）也能看到失败，且进 journal。
- **GUI**：`AppMcp.cpp` 负责会话头副标 `MCP N 个服务 / M 个工具`（失败追加
  `（K 个失败）`、禁用追加 `（K 个禁用）`、配置错误显示 `MCP 配置错误`）与检视栏
  「MCP」卡（来源 / 服务行 / 红色失败行 / 禁用行）。**GUI 不扫 `mcp.json`、不猜服务名**，
  `failed[].error` 原样透出。缺 `hello.mcp` 时副标与卡片整段不出现。
- **判据**：
  - core：`TestMCP_HubStatus`（好/坏/禁用三路 + 无配置不发）、
    `TestMCP_ConfigProblemVisible`（显式路径不存在 / 坏 JSON 仍带 Problem）、
    `TestLoopE2E_MCPTool`（`hello.mcp` 字段 + 失败 `system` 块）；
  - 冒烟：83 条（A/B 为 stdio；C/D 为 HTTP/SSE；E 为 HTTP 不可达失败臂；F 为运行期
  env minimal + 按需重启；均含 `hello.mcp` 与传输标识断言）；
  - GUI：`--mcp-probe`（`ProbeMcp.cpp`）——模型侧 `hello.mcp` 与副标/卡片行数/服务名/
    失败名/`system` 行逐项对齐；夹具 `fixtures/mcp/status.jsonl`；
    **负对照另跑一支**（demo 无 `mcp` 字段 ⇒ 卡片、副标 MCP 段、失败行全缺）。
- **诚实边界**：
  1. 这是**启动期快照**：运行期服务崩掉仍只走 `noteDead` → `system` 块，卡片不会把
     已死服务改成红行（要实时状态得另开控制面消息，不在本期）。
  2. MCP hub 在会话创建时按**初始工作区**初始化；切换工作区不会重新发现 `mcp.json`
     （与工具表/记忆的既有边界同源，未在本期改动）。
  3. 失败 `system` 块沿用 `system` 的弱化色（与死亡通告一致）；红色只在检视栏卡的
     失败行与副标的「K 个失败」上。产品模式默认可见，但不靠颜色叫醒。
  4. `--mcp-probe` 跑在开发者模式（任一探针即开发者模式）；用户模式走**同一个**
     `McpStatusText()`，但「用户模式零探针」的既有缺口（README「还」不能做）未在本期闭合。

## 九、 六版（2026-11-06）：HTTP / SSE 传输

- **动机**：此前传输写死在 `startAndInit`（`exec.Command` + stdin/stdout），只能连本地
  子进程。远程 MCP server（托管端点、内网服务）接不进来；`docs/positioning.md` §5.3
  的传输清单里本来就有 Streamable HTTP。
- **配置契约**：
  ```json
  {
    "mcpServers": {
      "local":  {"command": "npx", "args": ["-y", "…"]},
      "remote": {"url": "https://mcp.example.com/mcp",
                 "headers": {"Authorization": "Bearer …"}},
      "legacy": {"url": "https://mcp.example.com/sse", "transport": "sse"}
    }
  }
  ```
  - `command`（stdio）与 `url`（远程）**二选一**；`args/env` 只属于 stdio，`headers`
    只属于远程。混写报错并进 `hello.mcp.failed`，不半启动。
  - `transport` 省略时：有 `url` ⇒ `http`（Streamable），有 `command` ⇒ `stdio`；
    `transport:"sse"` 显式走 legacy HTTP+SSE。
  - URL 只接受 `http/https`；非 loopback 的明文 `http://` 打 warning（不拦截：用户可能
    连内网/本机），TLS 默认校验，无 `--insecure`。
- **架构**：`mcpTransport`（Start / Do / Close / Done / ProtocolVersion）+ 三个实现：
  - `mcp_stdio.go`：从旧 `mcpClient` 原样搬入，行为逐位不变（既有 MCP 测试即判据）；
  - Streamable HTTP：单端点 POST；`application/json` 或 `text/event-stream` 响应；
    initialize 回执里的 `Mcp-Session-Id` 后续回带；非 initialize 请求带
    `MCP-Protocol-Version`；通知期望 2xx，`Close` best-effort `DELETE`；
  - legacy HTTP+SSE：GET `/sse` 等 `endpoint` 事件 → POST 消息 → 回执从 SSE 流回来；
    `initialize` 也走 POST；不重连。
- **版本**：stdio/legacy 用 `2024-11-05`；Streamable HTTP 用 `2025-06-18`（有连接级
  initialize 的最后一版）。`2026-07-28` 无状态模型仍**未采用**——存量 server 兼容优先，
  迁移点是 `startAndInit` 的握手序列，不是调用面（§二.4）。
- **安全/生命周期**：
  - 不跟随重定向（`CheckRedirect` 返回 `ErrUseLastResponse`）⇒ Authorization 不跨 host；
  - headers 的值不进日志、不进 `hello.mcp`；HTTP 错误只带状态码 + 512B 截断 body；
  - 单 SSE 事件 4MB、非流式 JSON 16MB 上限；工具输出仍按 24KB 截断；
  - `Done` 关闭 = 传输死（stdio 进程退出 / SSE 断流 / Close）；HTTP 请求失败不自动
    标死（避免一次瞬时 5xx 永久禁用服务）；
  - HTTP 没有进程，不适用整树清退/respawn；`Close` 取消在途请求 + 关闭 idle 连接。
- **可见性**：`hello.mcp.services[].transport`（`stdio`/`http`/`sse`）；检视栏远程行显示
  `name · http · N 工具`，stdio 行文案不变；协议 §3.1 只增字段。
- **判据**：`mcp_http_test.go` 覆盖 JSON/SSE 响应、session/版本头、auth、302 拒绝、
  连接拒绝进 Failed、配置互斥、legacy POST/SSE、SSE 解析边界；E2E
  `TestLoopE2E_MCPToolHTTP` 走完整 ask→call→idle；冒烟五臂 83 条（C Streamable、
  D legacy、E 不可达，A/B 仍覆盖 stdio，F 为七版运行期臂）。
- **诚实边界（本期不做）**：
  1. 不实现 GET 上的服务端主动通知流、legacy 重连/`Last-Event-ID`；tools-only 场景
     不受影响，但 sampling/roots 类反向能力在 HTTP 下不可用（stdio 仍回 -32601）。
  2. 不实现 OAuth/动态客户端注册；`headers` 由用户静态配置。
  3. 不实现 `2026-07-28` 的逐请求能力协商；`initialize` 仍保留。
  4. legacy 的 `endpoint` 事件 URL 不做白名单校验（配置即信任；MCP 不是安全边界）。
  5. 非 loopback 的 `http://` 只警告；需要强制 HTTPS 时应在部署侧做，不在客户端硬拦。

## 十、 七版（2026-11-06）：运行期语义 —— env minimal / 按需重启 / 取消通知

本批**不动线协议、不动 GUI**：wire 一个字节没改，GUI 侧复用既有 `system` 块与
tool_result 行；全部改动在 core（`mcp_runtime.go` / `mcp.go` / `mcp_stdio.go` /
`mcp_http.go` / `session.go`）。这三点都是二~六版明确记账、留到现在的缺口（§五）。

### 1. 环境白名单：`--mcp-env=minimal`

- **动机**（二版 §二.6 原话）：默认全量继承意味着 `OPENAI_API_KEY` / `GITHUB_TOKEN`
  等「与 MCP 无关的凭据」也能被第三方 server 读到；MCP server 无人值守、可反复调用，
  风险面与一次用户主动批准的 `run_command` 不同。
- **口径**（`mcp_runtime.go` `mcpMinimalEnvAllow`）：只放「起得来」必需的——
  Windows 侧 `SystemRoot`/`PATH`/`PATHEXT`/`COMSPEC`/`TEMP`/`APPDATA`/`LOCALAPPDATA`/
  `USERPROFILE`/`ProgramFiles` 系/`PSModulePath`，POSIX 侧 `PATH`/`HOME`/`TMPDIR`/`XDG_*`，
  两边都留 `LANG`/`LC_*`/`TZ`。**代理变量刻意不放行**（URL 常嵌账号密码）；需要就在
  `mcp.json` 的 `env` 里逐项显式给。
- **默认 `inherit`**：保持旧行为（全量继承 + 剔除 `ECDI_*_KEY`，同 `tools.go`）——
  「默认不变」是本项目对既有路径的纪律，收紧是**用户显式选择**。
- **配置 `env` 是增量**：两种模式都在过滤之后追加，那是用户显式授权的，不归白名单管。
- **判据**：`TestMCP_EnvFilter`（纯函数：普通变量→inherit 有 / minimal 无；密钥两边都无；
  PATH 两边都在）+ `TestMCP_MinimalEnvChild`（真子进程 `env_probe` 读回：密钥空、
  配置 env 在、PATH 在）。
- **诚实边界**：白名单只保证常见 node/python/uv server 起得来，不是通用最小化；
  某个 server 需要别的变量时由用户补在配置里（漏了会在启动失败里显形，不会静默）。

### 2. 按需重启：`--mcp-respawn`（默认开，每服务每会话上限 3 次）

- **语义**：只在**调用之前**发现传输已死时重启，然后执行本次调用；
  调用**执行中**崩掉的不自动重试——工具可能有副作用，重试等于替用户再执行一次。
- **为什么改掉「永不重启」**（二版 §二.7）：崩掉一次就整会话废掉一个服务，代价太大；
  而「重启—再崩」刷屏可以用**上限**挡住，不需要用「永不重启」挡住。
- **上限**：`mcpRespawnMax = 3`；失败也计数（含握手失败），到顶后明说放弃。
  命中上限时不再尝试，避免每轮对话都白等 5s 握手。
- **工具表**：重启后走**同一份** `registerClientLocked`（撞名消解不分叉），先摘掉该服务
  旧定义再注册新的；定义追加到 `toolDefs` 末尾（前缀缓存失效一次，可接受）。
- **并发**：`respawnMu` 串行化；不持 `h.mu` 做进程 I/O（握手最长 5s）；换客户端与换
  工具表在同一把写锁内完成。`Close()` 置 `closed` 后拒绝重启。
- **只 stdio**：HTTP 没有进程可拉起（远端失败仍按普通 HTTP 错误处理，不标死）。
  启动期就失败的服务不进工具表，`CallTool` 永远走不到，因此不会「反复重试启动」。
- **通告**：重启成功发 `system` 块（`ok=true`：「已自动重启（第 N/3 次）：M 个工具已重新
  注册」）；失败发 `ok=false` 并说明第几次、下次还试不试；彻底放弃时随死亡通告给出原因。
  协议不加新 `kind`——既有 system 行就是为「发生了什么但没人说」准备的。
- **判据**：`TestMCP_RespawnAfterCrash`（重启后调用成功、工具定义不重复）、
  `TestMCP_RespawnDisabled`（`--mcp-respawn=false` 退回旧行为且说明白）、
  `TestMCP_RespawnCap`（4 次崩溃后放弃，最后一条通告含「不再尝试」）。

### 3. 取消通知：`notifications/cancelled`

- **问题**（§五 原话）：取消只让我们不再等，server 侧调用照跑，而 GUI 已显示「已取消」。
- **改法**：`mcpClient.sendRequest` 在 `ctx` 结束时（用户取消或超时）用**原请求 id**
  发 `notifications/cancelled`，`reason` 区分「用户取消」/「调用超时」；
  投递预算 `mcpCancelNotifyTimeout = 2s`，失败只记日志，**绝不拖住取消本身**。
- **传输**：三种各按本义投递——stdio 写一行 stdin（goroutine + 超时兜住可能卡死的写）；
  Streamable HTTP 用 `t.ctx` 派生 2s 上下文发一条通知 POST（原 ctx 已 canceled，拿它发
  什么都发不出去）；legacy SSE 走 `messages` 端点。
- **人话**：tool_result 按「用户取消/超时 × 通知送到/没送到」四组合各写一句
  （`mcpCanceledError.toolText`）：送到说「已向 MCP 服务发送取消通知；服务端可能仍在执行，
  迟到的结果会被丢弃」，没送到说「取消通知未能送达，该调用可能仍在服务端执行」。
  tool_call 的 `summary` 仍是「已取消」（既有 GUI 断言不动）。
- **连接不关**：取消 ≠ 断连；迟到回执按 id 找不到 pending 后丢弃，同一 client 继续可用。
- **判据**：`TestMCP_CancelNotifiesServer`（stdio：mock 落盘收到 `notifications/cancelled`，
  `reason`/`requestId` 逐项，取消后同连接还能调用）、
  `TestMCP_HTTPCancelNotifiesServer`（Streamable 与 legacy 两传输都发出通知 POST）、
  `TestMCP_CancelText`（四组合文本）、`TestLoopE2E_MCPCancelHonest`（GUI 侧看到诚实文本 +
  `summary=已取消` + `state cancelled→idle`，且 mock 真收到通知）。
- **诚实边界**：规范是 SHOULD，服务端可以不遵守；我们只说「已发送」，不说「已停止」。

### 4. 本版明确不做

- **per-call 超时**：MCP 调用仍无单次墙钟上限，取消是用户手动的逃生门（下一批候选；
  需要先定「超时该不该杀服务」——杀会丢掉同连接上的后续调用）。
- **后台/主动重启**：没有调用就不重启；不做崩溃计数持久化（每会话重置）。
- **GUI 运行期状态刷新**：`hello.mcp.services[].tools` 仍是**启动期快照**；重启后若服务
  端工具集变了，副标/卡片上的数字不会跟着变（转录里的 system 通告才是真值）。
