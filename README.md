# ECDI_fake_harness

> **一个能真用的 agent GUI，外加对 ECDI 的评估数据。** 本项目是 [ECDI](https://github.com/momoya-zh/ECDI)
> 的**外部消费者**：GUI 是**协议的渲染器**，对着手写/录制的 JSONL 就能跑完整个界面
> （**不需要**真模型）。
>
> ★ **两个目的都是第一等的**：① 对 ECDI 的**评估数据 + 能力需求账本**；
> ② **一个真正能用的 agent GUI**。
> ② 不是 ① 的副产品——**只做展示的话，本项目就该待在 ECDI 的 `examples/` 里，不该单独成仓**。
> 详见 [`docs/positioning.md`](docs/positioning.md)（定位与信任模型）。
>
> ★★ **现状（2026-10-24 核对）**：协议、假 core、GUI 三侧**都已到 v3 并收口**，界面可以拿回放夹具
> 跑完整流程、也能真起子进程。但 **`core/` 里只有假 core**——**接真 agent（P3）尚未开始**，
> 所以「能拿它干活」目前还只是设计方案而非既成事实。P3 是本项目的主线（`positioning.md` §2/§6）。

---

## 这是什么 / 不是什么

| 是 | 不是 |
|---|---|
| ECDI 的**外部消费者**：`find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI` | 不是 ECDI 的一部分，不随框架一起构建 |
| **协议先于两侧实现**：`protocol/` 是唯一契约，GUI 只看它就能跑 | 不是「先写实现、事后补协议」 |
| **假 core**（Go，零第三方）：真子进程 + 双向管道，但不调模型 | 不是真 agent；不接任何 LLM（真 core = **P3**，会用另一个程序替换它，协议不变） |
| **评估数据**的产线：`evals/` 下机器可读读数 | 不是「为展示而存在」——**展示品不该单独成仓** |
| **一个能真用的 agent GUI**：接上真 agent 后能拿它干活 | 不是**商业产品**（无 roadmap / 无支持承诺 / 无工期） |

★ **铁律**：**任何对 ECDI 框架的修改都不在本仓库做**——这里只登记需求、只给证据、只判归属。
判「ECDI 缺能力」之前必须先查**基类公共头 + ECDI 源码仓**，且归属结论须有**只改一个自变量的
受控对照**支撑。（这条纪律是被代价换来的：`REQ-01`/`REQ-03`/`REQ-04` 三条都曾误判为 ECDI 缺陷，
复核后全部反转为 harness 侧。）

## 仓库结构

| 目录 | 职责 |
|---|---|
| `protocol/` | **唯一契约**。`PROTOCOL.md`（**v6**）+ `fixtures/`（**17 份夹具**）+ `validate.py`（**可执行规格**） |
| `gui/` | 独立 CMake 工程 `harness_gui`——外部消费 ECDI。**25 个源文件 / 14,178 行**（`App.cpp` 一直是大头，~0.5 MB） |
| `core/` | **假 core**（Go，零第三方）：`main.go` / `script.go` / `session.go`，3 文件 / **839 行** |
| `evals/` | 机器可读评估读数（JSON）——评估产出的落点（口径见 §当前状态） |
| `docs/` | **定位与信任模型**（`positioning.md`）· P1 / P1.5 规格 · **能力需求账本** · **variant-c 落地对照**（`variant-c-ecdi-mapping.md`）· GUI 原型（单文件 HTML） |
| `tools/` | `precommit-check.py`（BOM / 行尾 / 夹具校验，已接入 git hook）· `regress.py`（全量回归门）· `mcp-smoke/`（真链路 MCP 冒烟：调用级 83 条断言，六臂（A–F）含 Streamable HTTP 与 legacy SSE，已进 `regress.py` §2f） |

## 协议要点

- **分帧**：一行一个 JSON，`\n` 结尾；EOF 截断的最后一行**必须报错**，不得静默丢弃。
- **信封**：`{t, seq, ts}`；`seq` 每方向独立、从 0 起、严格递增。
- **不变量 I1–I10**。★ **I4（`seq` 缺口必须显式提示「消息丢失」）是本项目的核心评估钩子**——
  它对应 `Application::PostToUi` 的乱序/丢失语义，是「靠它才看得见」的那一类失败。
  ★ **I8**（v2 新增）：未知 `kind` 必须**中性渲染 + 显式提示**，不得伪装成有语义的形态。
  ★ **I9/I10**（v3 新增，只约束声明 `protocol >= 3` 的流）：`kind:"user"` 块必须带**唯一**
  `client_id`，`workspace` 响应必须带 `request_id` / 布尔 `ok` / `path`（失败还须 `message`）。
  ★ **I12/I13**（v5 新增，只约束声明 `protocol >= 5` 的流）：`agent` 的 `id` 一份流内唯一、
  `phase=start` 先于一切携带该 `agent` 的块、`phase=end` 后不得再有；带 `parent` 的块必须指向
  一个已存在的、属于父代理的 `tool_call` 块；根代理的块不得带 `agent`。
  ★ **I14**（v6 新增，只约束声明 `protocol >= 6` 的流）：`job` 的 `id` 一份流内唯一；首条必须是
  `phase=start`（`call` 指向已存在的 tool_call 块），`output`/`end` 只能跟随自己的 start；
  `end` 恰好一次且之后不得再有该 id 的任何消息。忽略这些 ⇒ 卡片会挂在错的调用上、或永远不结束。
- ★ **子代理（v5，`--agents` 开关）**：`block_start` 增**可选** `agent` / `parent`，新增独立控制消息
  `agent`（生命周期：`start`/`end`）——**只加字段与控制消息，不加 `kind`**（七个 kind 语义不动，
  I8 的「未知」与「已知」两条路径不混）。主 `state` 仍只表示根代理（按钮使能门控零风险）；
  `usage` 也只描述根代理，子代理用量只在 `agent.phase=end` 上（不污染上下文百分比）。
  ★ **`ask.detail`（2026-11-05 增补，仍属 v5）**：可选结构化提案——write/edit 带 unified diff
  （旧侧 = 批准前的磁盘内容）、run/task 带完整作用域；渲染数据非不变量对象，缺失 ⇒ 回落问句文本。
- **`kind` 七个值**（v2 起）：`user` / `assistant` / **`reasoning`** / `tool_call` / `tool_result` / `system` / `error`。
- **转录的唯一作者是 core**：GUI 不产生转录块，用户输入由 core 回显为 `kind:"user"`。
  ★ **v3 的边界**：GUI 可以先画一个**待确认**的本地气泡（`· 发送中`），但它不是转录块——
  core 用同一 `client_id` 回显后该气泡**原位确认**，绝不插入第二块；被拒或断连则标 `· 未确认`
  并**不进入**规范转录（因此也不进入回放）。
- **工作区切换是租约，不是命令**（v3）：`workspace` 只在 `idle` 被受理，一次只允许一个未决请求；
  未决期间 GUI **必须禁用发送**，core 也**必须拒绝**交错的 `prompt` / 第二个 `workspace`。
  成功后 GUI 只认 core 回的**实际路径**（规范化：绝对 / `/` 分隔 / 无尾随 `/` / 无 `.`·`..`）。
- **回放两模式**：`instant`（一次性灌入，测稳态渲染）/ `timed`（按 `ts` 真实间隔，测流式刷新）。
- **版本门的三个不对称**（`AppInternal.h:290` `kProtocolMaxSupported = 6`，随版本逐版上调）：① core 声明的主版本**高于**上限才拒连，
  且拒连 = **停止消费**（`--instant` 下不 break 就会「边抱怨边照做」）；② **低于**上限从不拒连；
  ③ 缺 `caps["workspace"]` **不是**拒连条件，只隐藏工作区交互（v3 §3.1）——早前版本把 ① 和 ③ 混成一个条件，
  导致两份 hostile 夹具的 `hello` 一行都没被解析，I1/I4/I5 的负对照全部空转。
- ★ **`ask` 权限门是合作性契约，不是安全边界**（详见 `docs/positioning.md` §4.2）。

## 怎么跑

**① 校验协议夹具**（不需要构建任何东西）

```bash
python protocol/validate.py            # 退出码 = 未通过预期的夹具数（0 = 全绿）
```

`fixtures/hostile/**` **必须**触发至少一条不变量；其余**必须**满足全部不变量。
★ hostile 夹具「什么都没触发」= **失败**——一个恒成立的负向用例，与没有这个用例无法区分。

**② 构建 GUI**（需要已安装的 ECDI 前缀）

```bash
# 生成器用 Ninja（2026-10-27 起；同一 cl.exe 工具链，静态库 ABI 不变，
# 换库后增量链接可靠，不再依赖 msbuild 的 --clean-first 纪律）
cmake -S gui -B gui/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DECDI_PREFIX=<ECDI 安装前缀>
cmake --build gui/build
```

★ Ninja 是单配置生成器，**没有** `--config`：配置期就写死 `CMAKE_BUILD_TYPE=Release`
（ECDI 前缀只有 Release，CMakeLists.txt 顶部的配置期守卫会拦住其他取值）。
★ Ninja 下需要先有 MSVC 环境（`vcvars64.bat`）再跑 cmake，产物在 `gui/build/harness_gui.exe`
（多配置生成器才落 `Release/` 子目录；`tools/regress.py` 两种布局都认）。

★ **`third_party/ecdi` 不入库**（在 `.gitignore` 里），clone 下来是**没有**的。
本仓库**不随 ECDI 一起构建**，所以必须先自己在 ECDI 仓库里产出安装前缀：

```bash
# 在 ECDI 仓库内（多配置生成器的库落在 Release/ 子目录，注意 --config）
cmake -S . -B build
cmake --build build --config Release
cmake --install build --config Release --prefix <此仓库>/third_party/ecdi
```

`ECDI_PREFIX` 的默认值就是 `third_party/ecdi`；装到那里则不必显式传参。
★ 消费口是**安装前缀**（`lib/cmake/ECDI/`），不是构建树 ⇒ 只编不 install 是接不上的。

**③ 回放一份夹具**

```bash
harness_gui.exe --replay protocol/fixtures/demo-session.jsonl --instant --out eval.json
```

**④ 拉起真子进程**（P1.5 通道）

先构建假 core（纯 Go、**零第三方**，标准库即可）：

```bash
cd core && go build -o bin/fake-core.exe . && cd ..
```

再跑：

```bash
harness_gui.exe --core core/bin/fake-core.exe --core-args --speed=0.2 --auto-prompt "hello"
```

★ `core/bin/` 不入库（`.gitignore`），`fake-core.exe` 需要自己编。
★ 两个易踩的点：`--core-args` 的值**按空格切分** ⇒ 用等号形式（`--speed=0.2`，不要 `--speed 0.2`）；
`--out` 摘要**只在正常退出后写** ⇒ 跑批用 `--auto-close <ms>` 让 app 自己退，别 kill。
★ **live 用例必须带 `--auto-prompt`**：假 core 是被动的，只回应用户输入——不给 prompt 时双方
握手后即静默，读数是 `blocks=0`（看起来像「core 挂了」，实为没喂输入）。
★ **负载旋钮 `--burst`**（`--core-args --burst`）：关掉节奏等待 + 把一轮输出攒成**一次写出**
⇒ 真造出队列积压。REQ-03 的「单回调预算」判据靠它才**可证伪**——不加时队列最多 8 行、那条
判据恒真；加上后 `maxLinesPerCallback` 恰好顶到 **256**（去掉预算则 430）。
★ **声明旋钮 `--protocol` / `--caps`**（2026-11-07）：只改 hello 的 `"protocol"`/`"caps"`
两个字段，剧本行为不变；默认 = 历史基线（3 + tools,ask,workspace）⇒ 既有跑法逐字节不变。
regress 2d-b 用它们构造「声明 4/6」「未声明 workspace」的合法 core——GUI 侧解析结果由
`--state-probe` 行回读断言（闭合 2026-11-06「选择文件夹死钮」那类版本守卫的语料盲区）。

**④b 拉起真 core**（P3：真模型 + 真工具；规格见 `docs/P3-real-core-spec.md`）

```bash
cd core && go build -o bin/ecdi-core.exe ./cmd/ecdi-core && cd ..
harness_gui.exe --core core/bin/ecdi-core.exe \
  --core-args "--base-url=https://…/v4 --model=glm-4.6 --key=sk-…" --auto-prompt "看看 README"
```

★ 密钥推荐走环境变量 `ECDI_CORE_KEY`（不进命令行历史）；`--key` 与环境变量都空 ⇒ 拒绝启动。
★ 工具集第一版 = 只读三件（read_file / list_dir / grep），**默认免 ask**（2026-11-02 起）：
三件全只读，且路径按**解析后的真实路径**钉在工作区内（`..` 逃逸、根外绝对路径、
工作区内的符号链接/junction 指向外部——全部拒绝）⇒ 逐次确认没有增量保护，只剩摩擦。
`--allow-tools=<a,b>` 整体改写名单，`--allow-tools=`（显式空）恢复全 ask；写工具必须留在 ask 门内侧。
★ **内置 L1 沙盒隔离（2026-11-05）**：Windows 平台下 run_command 默认运行在 Job Object + 受限令牌中（整树强杀防孤儿残留、内存≤1GB、进程≤32、特权剥离），提案卡两行透明呈现；令牌/作业初始化任一步失败即**拒绝执行**（fail-closed），只有 --sandbox=false 能显式降级；非 Windows 退守进程组（整组 SIGKILL，无内存/进程硬上限）。更高层级的物理网络/全盘隔离仍建议由运行者保证（docs/positioning.md §4.3）。
★ **子代理委派（协议 v5，`--agents`，2026-11-04）**：GUI 拉起 core 时**默认注入** `--agents`
（`AppSettings.cpp` `kCoreAgentArgs`，两处 coreArgs 装配点同加；core 自己默认**关**——那是
「关闭时逐字节退回 v4」判据的对照臂，手动 `--core-args` 跑法要自己带）。开启后根代理的工具表
多出 `task`：**委派即授权**（一次 ask 覆盖子代理的全部只读工具 + 轮数/时间预算，确认文案显式
列范围）；子代理并行受 `--max-agents`（默认 2，≤8）、`--agent-max-turns`（默认 8）、
`--agent-timeout`（默认 120s）约束；深度固定 1（子代理工具表里没有 `task`）；上下文隔离
（主历史只落一条结论摘要）；用量单独记账（见协议要点 v5 条）。决策与授权模型详见
`docs/P3-real-core-spec.md` §4.4.2。

★ **MCP Host（stdio + HTTP/SSE，2026-11-05 / 2026-11-06）**：core 是 MCP **host**——按配置拉起
MCP server 子进程或连远程端点、握手、`tools/list`（分页追到底）、把外部工具以
`mcp__<srv>__<tool>` 注册进模型工具表。
配置三级发现：`--mcp-config=<path>` → `<workspace>/.ecdi/mcp.json` 或 `<workspace>/mcp.json`
→ `%APPDATA%\ecdi-fake-harness\mcp.json`；格式 `{"mcpServers":{"<name>":{…}}}`：
本地 `{"command","args","env","disabled","timeout"}`，远程 `{"url","headers","transport","disabled","timeout"}`
（`transport` 省略按 `url` ⇒ `http`；`"sse"` 走 legacy HTTP+SSE）。`command` 与 `url`
二选一，混写显式失败而不是半启动。
纪律：外部工具**默认全过 ask 门**（可在 `--allow-tools` 里按全名逐个豁免）、输出上限 24KB
（超出头尾各半并标注）、`hello.caps` 增 `"mcp"`、子代理**拿不到**外部工具、会话退出**整树清退**
（Windows Job Object / POSIX 进程组）、崩溃时发一条 `system` 块；stdio 服务在下一次调用前
**按需重启**（每会话每服务上限 3 次，`--mcp-respawn=false` 退回「崩掉就是崩掉」）。
★ 工具名会被压进 provider 的 `function.name` 空间（`[A-Za-z0-9_-]`、≤64、超长带哈希）
——MCP 规范不管这条，OpenAI 兼容端点管；不压就是**整轮 400**。
★ 入参 schema 先规约再上交：递归删 `$schema`、本地 `$ref` 从 `$defs` / `definitions`
就地展开后删容器；递归引用降级为 `{}` 并告警——部分端点对这些元关键字不宽容，
失败同样是**整轮 400**。规则与诚实边界见 `docs/mcp-host-plan.md` §二.9 / §七。
决策、偏差（规范锚：stdio/legacy 用 `2024-11-05`、Streamable HTTP 用 `2025-06-18`，
均不同于 `positioning` §5.3 的 `2026-07-28`）与已知缺口见
`docs/mcp-host-plan.md`（HTTP/SSE 边界见 §九）。
★ **产品面可见性（2026-11-06）**：`hello` 增可选 `mcp` 启动期状态（配置来源/路径、
每个服务的工具数、失败服务与原因、禁用数、配置层错误）。会话头副标显示
`MCP N 个服务 / M 个工具`（失败追加「K 个失败」），检视栏新增只读「MCP」卡；
启动失败/配置错误另发 `[MCP] …` `system` 块。GUI 不扫 `mcp.json`，状态只来自 core。
每个服务带 `transport`（`stdio`/`http`/`sse`），检视栏远程行显示 `<name> · http · N 工具`，
stdio 行文案不变——用户才分得清「配的是本地进程还是远端端点」。
★ **运行期语义（2026-11-06）**：`--mcp-env=minimal` 让 stdio server 只拿白名单环境
（默认 `inherit`：全量继承但剔除 `ECDI_*_KEY`）；调用被取消/超时会真发
`notifications/cancelled`（stdio 写 stdin、HTTP 发通知 POST），tool_result 按「送没送到」
如实措辞——GUI 的「已取消」不再替服务端打包票；崩溃的 stdio 服务在**下一次调用前**
按需重启（最多 3 次），重启成功/失败各发一条 `system` 块。规则与诚实边界见方案 §十。
★ **单次调用超时（2026-11-06 八版）**：`--mcp-timeout`（默认 60s；0 不限）控制单个 MCP
调用的墙钟上限；`mcp.json` 支持服务级细粒度覆盖 `"timeout": <sec>`（0 针对该服务禁用）。
超时真发取消通知，且通知未能送达（如 stdio 彻底死锁）的主动清退并触发下次按需 respawn。
★ 真链路冒烟：`python tools/mcp-smoke/drive.py`——真 core 二进制 + 真 stdio server + 真
HTTP/SSE server + 脚本化 SSE 端点，六臂（A–F）**83 条断言**（含 ask 门、提案卡 detail、`hello.mcp`
状态与传输标识、参数穿越、bye 后无残留、HTTP 不可达降级，以及 F 臂的
`--mcp-env=minimal` 不泄漏与崩溃后按需重启）；读数与边界见方案 §四.5 / §十；已接进
`regress.py` §2f（缺二进制 SKIP / 陈旧 FAILED）。

**④c 无参数启动**（P3.5，2026-10-24）

```bash
harness_gui.exe        # 双击即可：读 %APPDATA%\ecdi-fake-harness\config.json 直连
```

- 配置不存在 ⇒ **首启设置面板**（BaseURL / API Key / 模型三字段）；也可经中栏 `···` 菜单的
  「设置」随时改。API Key 存 **Windows 凭据管理器**（目标名 `ECDI.FakeHarness.v1.Provider.<provider-id>`），
  **配置文件永不携带密钥值**；key 经启动环境层（`ECDI_CORE_KEY`）注入 core——不经协议、不经命令行。
- `--core` / `--replay` 手动模式保留（诊断与回归路径）；`--config <path>` 可覆盖配置位置（探针隔离）。
- 决策与实施回写见 `docs/P3-real-core-spec.md` §8.5 / §8.5.5。
- ★ **P3.6（2026-10-25，协议 v4）**：多提供商 + 模型热切换。设置面板支持保存多家
  baseURL/APIKey（凭据库按 `ECDI.FakeHarness.v1.Provider.<id>` 分家存），并带
  上下文挡位（200K/500K/1M）与输出上限挡位（4K~64K）；检查器新增「模型」卡片，
  点行即切——**正在输出时排队**，本轮完整结束后才切换（协议 §4 排队语义，历史不丢）。
  v4 详情见 `protocol/PROTOCOL.md` §9 版本表与 `docs/P3-real-core-spec.md` §9。

**⑤ 回归门**（改完必跑；全绿才算收口）

```bash
python tools/regress.py     # 6 夹具结构基线 + 13 探针 + 2b live A/B（REQ-03）+ validate.py
```

读数落 `.scratch/`（gitignored）。★ 判据口径：夹具基线只钉 `blocks/deltas/messages`，
不钉 `paints/frames`（后者随字体与实现变化，钉死它会把一次合法收敛判成回归）。
★ 2b 节要真子进程（`core/bin/fake-core.exe`）：没有就 **SKIP**，不是 PASS——跳过必须说出来。

### 探针（**37 个 `--*-probe`，默认全关** —— 关闭时既有路径逐位不变）

| 探针 | 读什么 |
|---|---|
| `--mode-probe` | 用户模式 vs 开发者模式切换、可逆性与偏好记忆（**P4 Phase J 缺口闭合**：5 相覆盖 baseline-dev / switch-user / dev-restored / negative / inspector-memory，断言顶栏胶囊、26px底栏、侧栏回放区、检视栏探针卡、中栏三按钮尺寸文案、视图切换菜单项、检视栏手动开关双槽记忆及负对照区分力） |
| `--cull-probe` | 命令分类 / 帧时 / 测量成本 |
| `--state-probe` | 回读按钮使能位（REQ-04）＋ picker 使能/可见、caps、protocolVer 同点回读（2026-11-07，2d-b 协议臂的断言面） |
| `--hit-probe` | 命中门控受控对照（REQ-04 **边界**：禁用 ⇒ 点击不达） |
| `--order-probe` | 块**到达序**可从模型恢复（step 0a） |
| `--rebuild-probe` | 运行期整树重建（换主题的唯一诚实做法） |
| `--session-probe` | 多会话：创建 / 切换 / 冻结时钟 |
| `--sidebar-probe` | 侧栏会话列表与折叠 |
| `--answer-probe` | `ask` 的决定**入模型**（否则重建后答案丢失） |
| `--theme-probe` | 双主题令牌 |
| `--history-probe` | 磁盘文件作会话（replay 到末尾） |
| `--workspace-probe` | 工作区租约 + 目录列表（含 `bubbleShapeOk` 负对照） |
| `--menu-probe` | `···` 内联菜单（ECDI 无浮层，挂进 `VerticalLayout` 排位） |
| `--resize-probe` | 尺寸链：root / scroll / 中栏 / caption 逐相同步 |
| `--enter-probe` | Enter 发送 / Shift+Enter 换行 |
| `--hud-probe` | HUD 字段**读写配对**（抓「只读不写」的死分支） |
| `--grow-probe` | Composer 自动长高 12 相（含软折行 wrap 与 max-clamp） |
| `--jump-probe` | 「跳到最新」按钮：显隐状态机 + 点击回底（需长夹具，短夹具自述 `skipped-short-fixture`） |
| `--key-probe` | 全局快捷键：Ctrl+B / I / , / N + Esc 逐层退出 |
| `--empty-probe` | 空状态引导：回放负对照 + firstRun 正对照（含「卡在树里却被 `extent=0` 裁掉」这一相） |
| `--model-probe` | 模型浮窗（Ctrl+M）+ v4 上下文分母上屏 |
| `--select-probe` | 转录正文选中复制（**配 `--selectable-rows` 用**，否则自述 `skipped-no-selectable-row`） |
| `--delprov-probe` | 删除提供商：拒绝删最后一个（需一份有 provider 的 `--config`） |
| `--settings-probe` | 设置面板 + 凭据注入 + 模型热切换回执（★ **强制要求 `--config`**，见下） |
| `--persist-probe` | 会话持久化与启动恢复（7 相覆盖：元数据发现、会话反序列化与转录复原、只读 UI 置灰、发送拦截守卫、新会话继承工作区/模型上下文、残损容错、测试隔离清理；★ **强制要求 `--config`**） |
| `--reasoning-probe` | 思维链折叠行为取证 |
| `--async-probe` | **REQ-03**：投递 vs 轮询的 A/B（**live**）——`blockedUs` / 唤醒承载占比 / 单回调预算 / 送达延迟分位 |
| `--overlay-probe` | 浮层拓扑与 Light Dismiss（模态遮罩 / 下拉 / tooltip 全走注册式浮层口，11 相含 tooltip **配色**判据） |
| `--table-probe` | Markdown 表格渲染（富内容二期）判据 |
| `--copy-probe` | 按块复制与复制全部判据 |
| `--valign-probe` | 单行输入框垂直居中（**REQ-14**：caret 几何公开判据 + Top→Center 翻转臂 / 恢复臂） |
| `--preview-probe` | I 路径可点 + 只读预览（8 相） |
| `--agent-probe` | 子代理缩进 / 归属 / 代理卡（**协议 v5**：缩进行数=归属块数、竖线行数=缩进行数、缩进吃宽、卡行数=代理数+1 且随有无代理显隐；★ **负对照另跑一支**——无 agent 夹具上缩进恒 0，见 `tools/regress.py`） |
| `--job-probe` | 后台作业卡（**协议 v6** `job` 消息）：卡片挂在发起它的 `run_command` 工具行，标题含 job id+状态、正文含 start.command 与尾部输出，模型/视图两路对齐；★ **负对照另跑一支**——无 `job` 的 demo 夹具上不得出现任何「后台」字样 |
| `--proposal-probe` | ask 提案卡（**协议 v5** `detail`，2026-11-05）：diff 行数/±行数与模型侧重切行数两路对齐、无空提案体；★ **负对照另跑一支**——无 detail 夹具（demo）上提案行恒 0（回落路径完好） |
| `--mcp-probe` | MCP 产品面可见性（**`hello.mcp`**，2026-11-06）：副标 `MCP N 个服务 / M 个工具（K 个失败）`、检视栏卡行数/服务名/失败名、失败 `system` 行与模型侧两路对齐；★ **负对照另跑一支**——无 `mcp` 字段的 demo 上卡片/副标/失败行整段缺席 |
| `--fold-probe` | **回合级折叠**（2026-11-06）：回答结束（`state:idle`）后把本回合的过程行（`tool_call`/`tool_result`/`reasoning`/中间 `assistant`）收成一条「› 已完成 · N 步 · X.Xs」摘要，`user` 与最终回答保持可见。读数四路对齐：模型 `foldHidden` == 视图不可见行数、可见过程行 == 过程行 − 隐藏数、摘要条文案含步数、**展开↔折叠往返后 `rows.size()` 不变**；★ **负对照另跑一支**——过程行不足两条的夹具上 `folds` 恒 0（否则「无脑全折」在正臂也绿） |

另有模式开关：`--legacy-poll`（**REQ-03 对照世界**：逐字复原每 tick 等 8ms 的旧路径）·
`--probe-contrast`（探针专用白底黑字）· `--row-mode autosize|listlayout`（布局 A/B）·
`--scroll-sweep`。

★ **2026-10-26：25 个探针全部接入 `tools/regress.py`**（此前 13/25）。门里分四类，
不是「绿了 / 红了」两类：`designed-red`（结构上不可达，理由写在 `EXTRA_PROBES` 注释）·
`skipped-*`（探针自述该跑法下不适用）· `UNEXPECTED`（其余一切 = 真回归）· `INSTRUMENT`
（`--cull-probe` 只记读数、无断言面，显式声明而不是默默漏掉）。

★ **`--settings-probe` 必须带 `--config <隔离路径>`**，否则**拒绝启动**（exit 2）。原因：
它的 cleanup 相会删掉本轮跑的配置文件，没隔离时那就是用户真实的
`%APPDATA%\ecdi-fake-harness\config.json`（2026-10-26 修，见 `status.txt`）。

## 交付物在哪

| 想要什么 | 去哪看 |
|---|---|
| ★ **定位与信任模型** | `docs/positioning.md`（**两个目的** · 协议不是安全边界 · MCP 决策） |
| **ECDI 能力需求 / 缺陷登记** | `docs/ecdi-capability-requests.md`（**唯一账本**） |
| **评估读数**（机器可读 JSON） | `evals/`（口径见 §当前状态）· 工作读数在 `.scratch/`（gitignored） |
| **协议契约** | `protocol/PROTOCOL.md`（v6） |
| 阶段规格 | `docs/P1-gui-implementation-spec.md` · `docs/P1.5-child-process-spec.md` |
| **会话持久化方案与实施** | `docs/session-persistence-decisions.md`（**已定稿并完成落地**：L1 会话持久 + L2 延续模型/工作区 + `--persist-probe` 7相验收）· `docs/session-persistence-plan.md`（架构分析与事实账本） |
| GUI 设计原型 | `docs/prototype/`（单文件 HTML） |

## 能力账本现状

| 条目 | 内容 | 状态 |
|---|---|---|
| `REQ-01` | 含 `\n` 的短文本丢换行 | ✅ 闭合（**归属反转**：harness 侧 `needWrap` 只看宽度） |
| `REQ-02` | `tool_call` 的 `summary` 被吞 | ✅ 闭合（harness 侧） |
| `REQ-03` | `PostToUi` 能力已具备，harness 从未走异步投递路径（每 tick **8ms 预算**拉队列） | ✅ **已闭合（2026-10-24）**：改成「读线程投递唤醒 + tick 非阻塞兜底」，`PostToUi` 第一次有**框架外真实消费者**。实测 legacy `avgBlockedUs` **11599** → **0**、wake 承载 **99.8%** 的行、`--speed=1` 下送达 p50 **49µs vs 14774µs**。★ 顺带发现「8ms 预算」实测是 **8.8–11.8ms / max 23.9ms**（`cv.wait_for` 受时钟粒度约束） |
| `REQ-04` | 按钮使能缺 API | ✅ 闭合（**归属反转**）；**边界**（禁用 ⇒ 点击不达）亦已取证 |
| `REQ-05` | GDI / GL 的 `LineHeight` 不一致（14px vs 16px） | ✅ 闭合（**真 ECDI 缺陷**，Phase 31 已修）——账本**首条走完「登记 → 修复 → 跨仓库验证 → 回写」闭环**的需求 |
| `REQ-06` | 跨后端基线偏移（原登记「拉丁 +1 · CJK +2」） | ✅ **已裁决结案（2026-10-17，③ 记为已知现状）**；★ 读数订正：112 格实测 Δ∈[−13,+3] 且符号翻转，**+1/+2 只是 SimSun@14 单点** |
| `REQ-07` | （候选）折叠面板仍测量隐藏内容 | ❌ **复核否决**（ECDI 的 Layout 已跳过不可见子节点）——归属 harness 侧 |
| `REQ-08` | 环形进度条需**圆弧原语**（`DrawArc` 不存在） | ✅ **闭合（2026-10-16）**：ECDI **Phase 33** 落地 `DrawArc` + `RingProgressBar`，本项目作为**首个真实消费者**兑现（右栏上下文环，`commandMix.arc > 0` 机读证据） |
| `REQ-09` | `Font::family` **双链语义矛盾**（GDI=族名 / GL=文件名） | ✅ **已闭合（2026-10-17）**：ECDI `Win32FontSource` 补族名反查（禁子串匹配、文件名直查保留且优先）；验收 8 族名 **8/8 两链同 face**，`ecdi_tests` **465/465**。⇒ 我方 `kMonoFamily="Consolas"` **无需改动即恢复等宽** |
| `REQ-10` | `Font` **表达力**不足——字重 / face index / 复合族名第二项**三者皆不可达** | 🔄 **开放（等 ECDI 立项排期）**：`Font` 只有 `size`+`family`，两处硬编码 `FW_NORMAL`，`FT_New_Face(..., 0, ...)` 恒取 face 0。实测 `lfWeight=FW_BOLD` 在 GDI 侧对 5 个真实族全部 ≥1.29× 墨迹，但 **GL 无字段**；CJK 加粗**两链无公共解** ⇒ 当前唯一同时可用的是「不加粗」。★ **2026-10-24 ECDI 对码核验三条全部属实**，登记 `roadmap-deferred` **#70**（登记不立项） |
| `REQ-11` | `Color.a` **双链覆盖面不对等**（GDI 的 `DrawText` / `DrawFocusRect` 恒丢 alpha）+ 无虚线 | 📋 **已登记推后**：**当前零消费者**（`Theme.h` 全表 alpha=255）；实测 GDI 侧四档 alpha（255/128/64/16）输出**逐位相同**。★ **2026-10-24 ECDI 核验属实**，登记 `roadmap-deferred` **#71**，并改写 `Color.h` 的 alpha 注释声明双链发散 |
| `REQ-12` | 多行 `TextBox` 的 `GetPreferredSize` 返回**当前尺寸**（与 `TextWidget` 的 wrap 分支自相矛盾） | ✅ **已闭合（2026-10-21）**：ECDI 采纳「订正语义」而非「加查询」⇒ `TextBox::MultilinePreferred` 落地；本仓组合实现**同日收敛回框架**，`--grow-probe` 七相读数逐位不变。★ 负对照 4 条变异全 RED |
| `REQ-13` | 窗口**最小尺寸**约束（无 API，只能事后钳制 ⇒ 可见回弹） | ✅ **已闭合（2026-10-23）**：ECDI 落地 `Window::SetMinimumSize`（`WM_GETMINMAXINFO` + `WM_WINDOWPOSCHANGING` 两路拦截，客户区 DIP 口径）；本仓删除 `ClampToMinimum` 绕行。★ ECDI 测试 487 → **490** |

★ 三条路径值得对照着读：`REQ-12` 是「**同一框架两条规则矛盾**」（修答案即可），`REQ-13` 是
「**框架根本没有这条能力**」（只能加 API），`REQ-11` 是「**两后端行为发散**」（零消费者，先记账）。
判别方法随之不同：分别是**分支对读** · **公开 API 全表核对** · **逐原语 alpha 覆盖扫描**。
★ **harness 侧待办**（`F7` 夹具真缺口 / `F8` 数据处理卫生）不在本表，见账本 §4。

### 思维链：协议 v2 已闭合（原「协议空白」）

面向**开源推理模型**的 agent 会流式传**思维链**。v1 的 `kind` 只有六个值、**没有它的位置**，
而 I5 只管信封层的 `t` ⇒ 未知 `kind` **不被任何不变量覆盖**。

后果（已实测）：未知 `kind` 曾**静默掉进工具卡分支**，标题写死 `等待结果`——那正是
`tool_result` 在 `block_end` 到达**前**的占位标题 ⇒ **思维链与「正在等的工具结果」视觉上无法区分**，
且 `validate.py` 对它**返回 0**（一份满屏思维链的夹具会全绿通过）。

★ **处置（v2，2026-10-14）**：`kind:"reasoning"` 成为**一等公民** + 新不变量 **I8**
（未知 `kind` 必须中性渲染）+ §7 渲染形态（**默认收起**，标题 `﹀ 思考中` / `﹀ 已完成`，
点击展开箭头翻 `︿`）。另增 `hello.context`（用量百分比的分母）与 `block_end.tokens`。
详见 `protocol/PROTOCOL.md` §3.3 / §5 / §7 与账本 §2。

## 夹具

| 文件 | 用途 |
|---|---|
| `fixtures/demo-session.jsonl` | 正常路径：完整一场会话（43 条消息） |
| `fixtures/hostile/seq-gap.jsonl` | **I4 负向**：`seq` 缺口 + 未知 `t` + 悬空 `block_end` + 迟到 `delta` |
| `fixtures/hostile/truncated-final-line.jsonl` | **§1.2 负向**：最后一行被截断 |
| `fixtures/hostile/missing-client-id.jsonl` | **I9 负向（v3）**：`kind:"user"` 块没有 `client_id`——GUI 无处确认自己的临时气泡 |
| `fixtures/hostile/duplicate-client-id.jsonl` | **I9 负向（v3）**：两个用户块共用同一 `client_id`——确认会错位到其中任意一个 |
| `fixtures/hostile/workspace-bad-ack.jsonl` | **I10 负向（v3）**：`ok:false` 无 `message` + 重复 `request_id` + `ok:"yes"`（真值字符串会被当成成功） |
| `fixtures/realistic/realistic-session.jsonl` | 真实编码会话形态（Markdown / 长日志 / 1213 条消息） |
| `fixtures/scale/scale-transcript-2000.jsonl` | 规模基线（2000 块 / 8906 条消息 / **921 KB**） |
| `fixtures/cot/cot-session.jsonl` | **思维链**：长 + 流式 + **默认折叠**（1044 条消息 / 44 块）。★ `hello` 带 `context`、思维链 `block_end` 带 `tokens` |
| `fixtures/subagent/parallel-two-agents.jsonl` | **v5 子代理正向**：两个 `task` 并行，`delta` 在 a1/a2 间交错 |
| `fixtures/proposal/write-edit-run-asks.jsonl` | **v5/v6 提案卡正向**：四个 `ask` 各带 `detail`（write/edit diff + run 键值 + **mcp 服务/工具/argsKv**，2026-11-06 增补，声明升 `6`） |
| `fixtures/mcp/status.jsonl` | **2026-11-06 MCP 状态正向**：`hello.mcp` 2 个正常服务（fs/smoke）+ 1 个失败 + 1 个禁用，随后一条 `[MCP] 服务 broken 启动失败` `system` 块；负对照 = demo（无 `mcp` 字段） |
| `fixtures/jobs/background-task.jsonl` | **v6 后台作业正向**：`job start/output/end` + `job_output`/`job_kill` 控制链 |
| `fixtures/hostile/job-output-before-start.jsonl` | **v6 / I14 负向**：`output` 出现在 `start` 之前 |

★ **十八份夹具的版本声明**：低版本夹具声明 `protocol:3`（I9/I10 只约束 v3 流）；v5 夹具声明 `5`
（子代理 + 三份 hostile）；v6 夹具声明 `6`（后台作业正向 + 一份 I14 hostile + 提案卡 + MCP 状态）。
**hostile 必须与其针对的不变量同版本**——若停在低版本，负向用例会被版本门豁免成空转。

三个生成器都是**确定性**的（同参 ⇒ 逐字节相同），所以 2000 / 5000 / 20000 是**旋钮**，不是砖：
`gen_scale.py` · `gen_realistic.py` · `gen_cot.py`（`--turns --thinking-chars --kind --context --tokens`）。

## 编码约定

★ **先分清两条互不相干的轴**。把它们混成「一套文件该怎么存」正是上一版表格的错误：
它说「文本一律 BOM + CRLF」，于是有人照着它把 `PROTOCOL.md` 与 `validate.py` 改成了 CRLF ——
一次「照文档办事」制造了两份坏文件。

### 轴一：行尾 —— 全仓一条规则，且已强制

`.gitattributes` 声明 `* text=auto eol=lf`；`git ls-files --eol` 对每个文本文件都回 `i/lf`。
**仓库里存的就是 LF，没有例外。** 工作区偶尔出现 CRLF 副本是本地工具留下的漂移，提交时被归一化，
`git checkout` 之后也会消失 —— 所以**不要拿工作区字节当约定**。

唯一的硬后果在 `*.jsonl`：CRLF 会被 `validate.py` 的 `read_fixture` 判 `ENC`（分帧靠 `\n`）。

### 轴二：BOM —— 按文件，且必须与该文件在 HEAD 中的字节一致

实测 **198 个**受版本控制的文件里 **48 个带 BOM、150 个不带**（2026-10-28 按 `git ls-files` 重数；原记录 146/37/109 是 2026-10-24 的，之后新增的文档与原型都在涨），与扩展名无关：

| 带 BOM（UTF-8 `EF BB BF`） | 不带 BOM |
|---|---|
| `README.md` · `status.txt` · `core/*.go` | `.gitattributes` · `.gitignore` · `LICENSE` · `core/go.mod` |
| `docs/positioning.md` · `docs/P1-gui-implementation-spec.md` · `docs/P1.5-child-process-spec.md` · `docs/prototype/*.html`（**8 个中 5 个**） | `docs/ecdi-capability-requests.md` · `docs/variant-c-ecdi-mapping.md` · `docs/P4-gui-usability-plan.md` · `docs/session-persistence-plan.md` · `docs/prototype/harness-hifi-variant-a.html` · `docs/prototype/gui-v4-ux-mockup.html` · `docs/prototype/session-restore-mockup.html` |
| `gui/src/` 的 19 个文件：`ChildSource.*` · `CountingPanel.*` · `Json.*` · `MeasureProbe.*` · `MessageSource.h` · `PaintProbe.*` · `PromptBar.h` · `ProtocolWriter.*` · `ReplaySource.*` · `Transcript.*` · `UiKit.h` | `gui/src/` 的 6 个文件：`App.cpp` · `App.h` · `PromptBar.cpp` · `Theme.h` · `ThemedCaptionBar.h` · `main.cpp` |
| `protocol/PROTOCOL.md` · `protocol/validate.py` | 三个 `gen_*.py` · **全部 `*.jsonl`**（十九份夹具）· 全部 `evals/*.json`（156 份） |

★ **2026-10-24 复核订正两处**（原表面描述与 `HEAD` 不符）：① 三个 `gen_*.py` **无** BOM（原表把它们列在 BOM 侧）；
② `docs/prototype/` 现有 **6** 份 HTML（原表写「5 个中 4 个」）。其余逐文件重验一致。

★ **2026-10-28 再订正两处**：③ 原型涨到 **8** 份，其中 5 份带 BOM —— 新增的两份
`gui-v4-ux-mockup.html`（P4 的上游原型，从 Qoder 会话可视化目录按字节收进仓库，sha1 `2c1db367…`）
与 `session-restore-mockup.html`（会话持久化 §9.5）**都无 BOM**；
④ 顺带立一条纪律：**文档里出现 `C:\` 绝对路径算缺陷**，原型一律用 `docs/prototype/` 仓库相对路径引用
（`docs/P4-gui-usability-plan.md` 原引的是机器路径，已改）。

★ **BOM 在本仓库不是编译正确性问题，别把它说成问题。** `gui/CMakeLists.txt:20` 给
`harness_gui` 传了 `/utf-8`（自初始提交 `f243579` 起），MSVC 不看 BOM 也知道源文件是 UTF-8——
证据是 `gui/src/App.cpp` 带 **31692 个非 ASCII 字符、无 BOM**，编译干净。
（`C4819` 那类教训属于**没有** `/utf-8` 的构建，比如 ECDI 自己的工程。）

那为什么还要按文件对齐 BOM？**因为逐字节回归基线**：本项目的判据大量依赖
「探针关闭时摘要逐字节不变」和「重建前后快照相等」。一次没有理由的加 BOM 会让 `git diff`、
sha256 基线与历史 A/B 全部失去可比性，而且这种改动看起来毫无意义，最难被复核。
⇒ **规则是「别动它」，不是「统一它」。**

复现这张表（**读 HEAD，不读工作区**）：

```bash
python - <<'PY'
import subprocess
files = [f for f in subprocess.run(['git', 'ls-files'], capture_output=True, text=True
                                  ).stdout.split('\n') if f]
bom = [f for f in files
       if subprocess.run(['git', 'cat-file', 'blob', 'HEAD:' + f],
                         capture_output=True).stdout.startswith(b'\xef\xbb\xbf')]
print('%d/%d files carry a BOM' % (len(bom), len(files)))
for f in bom:
    print('  ', f)
PY
```

★ 真正**可强制**的只有一处，它已经写进验证器：`*.jsonl` **无 BOM + LF（含末字节，
2026-11-07 起实测**——此前两份正向夹具长期缺尾 LF、每轮回放都在放假截断通告而无人可见）。
其余按现状描述 —— **不要发明统一规则**，那是上一版表格的原罪。

## 当前状态

> 时点：**2026-11-07**。逐轮进度摘要（机器向）在 `status.txt`；本 README 是人向入口。

### 已收口

| 项 | 读数 / 依据 |
|---|---|
| P1（GUI 骨架）· P1.5（子进程真路径，含反向通道） | 已收口 |
| **协议 v3 基线 + v4 增补** | 契约层（`PROTOCOL.md` + `validate.py` + **9 份夹具** + 3 个生成器）· core（真 core `hello protocol:4` + `caps=[tools,ask,workspace,model]`；**假 core 恒 `protocol:3`**，回归基线不动）· GUI（`kProtocolMaxSupported = 4`，接受 ≤4；cap 缺失则隐藏切换器）。九份夹具全部声明 `protocol:3`，`validate.py` 退出码 **0** |
| ECDI 消费前缀 | **`0.1.0` MSVC Release 静态**，安装于 2026-10-23 21:09；对应 ECDI 仓 **`096991b`**（含 `REQ-13` 的 `Window::SetMinimumSize`）；`ECDI.lib` **11,063,564 B** / sha256 `68247cd0…` |
| ECDI 自带套件 | `490 passed / 0 failed`（`REQ-13` 闭合时点；早期 `445/445` 见 `status.txt`） |
| **P3 真 core + P3.5 无参启动 + P3.6 多提供商** | `core/cmd/ecdi-core`（OpenAI 兼容端点 + 只读三件 `read_file`/`list_dir`/`grep` + 默认全 ask）+ `core/internal/wire` 线层下沉（假 core 逐位回归 PASS）。**两个真端点全链验收**：云知声 `u2-flash`（`evals/p3-u2flash-live-1.json`）与 DeepSeek 官方**无参启动**（`evals/p35-deepseek-noarg-live-1.json`）。协议 v4 = 模型热切换（等当前轮跑完再切）+ `usage.context` 逐轮分母 + 容量挡位；凭据只进 Windows 凭据管理器，配置文件**没有** key 字段（类型层面放不下） |
| 回归门 | `python tools/regress.py` = 6 夹具结构基线 + **25 个探针**（13 原有 + 11 补齐 + 1 MCP 可见性）+ 1 个仪器声明 + 3 段 live（REQ-03 A/B · workspace · 无 core 时显式 SKIP）+ `validate.py`；`tools/precommit-check.py` 已接 git hook（BOM 漂移 / `*.jsonl` 行尾 / 残留 lock） |
| **P4 可用性（A–J）** | 跳到最新 / 全局快捷键 / 空状态引导 / ask 已答留痕 / 设置改模态浮层（**半透明遮罩 = ECDI 首个半透明绘制消费者**）/ tooltip / 顶栏模型 chip + 用量常驻 / 转录可选中（尖峰）/ **I 路径可点 + 只读预览**（`--preview-probe` 8/8）/ **J 开发者视图 ↔ 用户模式分流**（无参启动=用户模式，`--replay`/`--core`/任一探针=开发者模式，`Ctrl+Shift+D` 或 `···` 切换。方案 §12） |
| **取数通路（REQ-03）** | 读线程经 `Application::PostToUi` 投递**唤醒**，UI 线程在消息循环里 drain；tick 只做**非阻塞**兜底。实测 UI 线程阻塞 `11599µs → 0`、唤醒承载 **99.8%** 的行、节奏化流下送达 p50 **49µs**（同口径轮询 14774µs）。单回调预算在 `--burst` 下恰好顶到 **256**（去掉预算 430）。`--legacy-poll` 逐字复原旧路径（对照 + 回滚） |
| **协议 v5 子代理（2026-11-04）** | **只加字段与控制消息，不加 kind**：`block_start` 可选 `agent`/`parent` + `agent` 生命周期消息 + **I12/I13**（`validate.py` 已查）。夹具 **14 份**（v5 增 1 正向并行 + 1 cap 缺失 + 3 hostile）。core：`agent.go`（`task` 特判走 ask 门、semaphore 并行 `--max-agents=2`、独立 history、逐子代理 delta 合批；`wire.Out` 加锁保 I4）、深度 1、`--agent-max-turns=8` / `--agent-timeout=120s`、**委派即授权**（`docs/P3-real-core-spec.md` §4.4.2）。GUI：缩进行 + 2px 归属竖线、检视栏「代理」卡（点击定位）、状态胶囊活跃数、`--agent-probe` 正/负对照进门。全量回归 **PASS** + `validate.py` 绿。已声明未做：连续归属竖线（ECDI 逐控件 PushClip，另批）、代理卡定位无高亮、GUI 侧 `--agents` 默认注入 |
| **协议 v6 后台长任务（2026-11-05）** | **只加控制面消息，不加 kind**：`job start/output/end` + **I14**（`validate.py` 已查，只约束 `protocol>=6`）。core：`jobs.go`（jobHub / 256KB 环窗 / 200ms 合批 / 30min 上限 / 并发 4 / ask 期缓冲即 I3）；`run_command background=true` + `job_output` / `job_kill` / `job_list`（控制工具不过 ask 门）；会话退出全杀。GUI：作业状态贴在发起它的 `run_command` 工具卡（标题 `j1 状态`、折叠体尾部输出），重建从 `jobsByCall` 重放；`kProtocolMaxSupported=6`。★ 同日补齐**子进程输出转码**：GBK 控制台 → UTF-8（前台整段判定、后台流式增量，切点对齐字符边界），协议 §1.3。夹具 **17 份**（v6 增 1 正向 + 1 hostile）；`--job-probe` 正/负对照进门。 |
| **MCP Host（2026-11-05/06）** | stdio JSON-RPC 2.0 + Streamable HTTP（`2025-06-18`）/ legacy HTTP+SSE（`transport:"sse"` + `url/headers`）；工具命名空间 `mcp__<srv>__<tool>`，ask 门默认拦截 · 24KB 输出上限 · 整树清退 · 崩溃显式提示与按需重启（上限 3 次）· 取消真发 `notifications/cancelled` · `--mcp-env=minimal` 白名单 · per-call 超时 `--mcp-timeout`（默认 60s）+ 服务级 `"timeout"`（送达则保留复用，未送达才清退 respawn）；产品面：`hello.mcp` + 会话头副标 + 检视栏「MCP」卡 + 启动失败 `system` 块 + GUI 侧提案卡（`detail.tool:"mcp"`）。六臂（A–F）冒烟 **83 条**；方案与诚实边界 `docs/mcp-host-plan.md` |
| **回合级折叠（2026-11-07）** | 描述见下「现在能做什么」；`--fold-probe` 正/负对照进门（正向 realistic folds=31 / 负向「过程行不足两条 ⇒ 一行不折」folds=0）。折叠态挂 `user` 条目（`RetagTurns` 每轮重建摘要条，挂摘要条必丢——0c 缺陷同型）；展开走无重建快路径（文本测量缓存不失效）。四项跟进缺口同日闭合（2026-11-07 第二批） |


### 现在能做什么（都有探针与负对照）

回放 `instant` / `timed` 两种节奏 · 真子进程（`ChildSource`，双向管道）· 双主题运行期切换
（**整树重建**，不是换色）· 多会话（创建 / 切换 / 冻结时钟 / 删除）· 工作区**租约** + 真实目录列表 +
临时会话 · 会话归档 · 转录导出 · `···` 内联菜单 · 窗口缩放 + **最小尺寸 880×460**（框架拦，不回弹）·
Enter 发送 / Shift+Enter 换行 · Composer 随内容**自动长高**（封顶后内部滚动）·
思维链默认折叠 · 上下文用量环 · **转录可选中**（`--selectable-rows`：user/assistant/system/
error/markdown/tool 折叠体全可选中复制，`--select-probe` 三相进门）· 子代理并行委派（`task` 缩进转录 + 代理卡，**委派即授权**）·
**回合级折叠**（回答结束即把本回合的过程行收成一条「› 已完成 · N 步 · X.Xs」摘要，
`user` 与最终回答保持可见；点摘要条展开／再收起，**行数不变**）·
ask **提案卡**（write/edit 审批前看 unified diff、run/task 看完整作用域）·
run_command 默认 **L1 沙盒**（Job Object 整树强杀 / 内存≤1GB / 进程≤32 / 特权剥离，提案卡两行展示边界）·
后台长任务（协议 v6 `job`，job_id / 增量回读 / 取消 / GBK 控制台转码）·
MCP Host（stdio JSON-RPC 2.0 + HTTP/SSE，工具命名空间 `mcp__<srv>__<tool>`，ask 门默认拦截 ·
24KB 输出上限 · 整树清退 · 崩溃显式提示与**按需重启** · 取消通知 `notifications/cancelled` ·
`--mcp-env=minimal` 白名单 · **启动状态副标/检视栏卡/失败告警**）·
I4（`seq` 缺口）/ I8（未知 `kind`）**显式提示**

### **还**不能做什么（★ 这是下一步的输入）

| 缺什么 | 状态 |
|---|---|
| ~~★ **长会话上下文管理（截断/摘要）**~~ **已落地（2026-10-28）** | Stage 1 旧工具载荷折叠（无 LLM）/ Stage 2 LLM 摘要 / Stage 3 确定性兜底（`summaryMaxRunes=2000` · `summaryInputRatio=0.60`，`core/cmd/ecdi-core/session.go:48`）；`context.jsonl` 留 `compact` 记录；`--fail-summarize` 注入失败路径。方案与验收：`docs/session-persistence-decisions.md` §7 |
| ~~**MCP HTTP 的服务端通知流**~~ **已落地（2026-11-07）** | Streamable HTTP 的 GET 通知流（`notifications/*` → hub 分发；405=不支持即停；断流带 `Last-Event-ID` 退避重连）+ `tools/list_changed` 真兑现（重拉工具表，新工具免重启可调）+ legacy SSE 断流自愈（重连 5 次耗尽才判死）+ stdio 通知同口径。`scanSSE` 透传 `id`。判据：Go 5 用例 + 冒烟 G 臂 18 条（83→101）。**仍未做**：OAuth 动态注册；`notifications/progress` 的 token 往返与 GUI 进度面（真 MCP server 跑几十秒的「卡住感」要靠它，另批） |
| ~~**MCP 单次调用超时**~~ **已落地（2026-11-06）** | `--mcp-timeout`（默认 60s；0 不限）+ 服务级 `mcp.json` `"timeout": <sec>`；超时真发取消通知——送达则保留连接复用，未送达（stdio 僵死）才清退并触发按需 respawn。规则与诚实边界见方案 §十一 |
| ~~★ **用户模式的机器判据**~~ **已落地（2026-11-06）** | `--mode-probe` 五相进门（`baseline-dev` / `switch-user` / `dev-restored` / `negative` 负对照 / `inspector-memory`）：顶栏胶囊、26px 底栏、侧栏回放区、检视栏探针卡、中栏三按钮尺寸文案、视图切换菜单项、检视栏手动开关双槽记忆，负对照带确认过的牙。闭合记录：`docs/P4-gui-usability-plan.md` §12 |
| ~~**转录可选中：只到尖峰**~~ **已落地（2026-11-07）** | **订正：这行的「user/system 未铺」本身是漂移**——user/system/error/markdown 段落在 G 之后的批次里已 TextBox 化，真正的缺口只有 tool 行。本批铺完：`--selectable-rows` 下 tool_call/tool_result 折叠体换只读 TextBox（mono/wrap 同形，header 保持折叠钮，作业增量 SetText 路径不受只读影响），探针新增 `tool-select-and-copy` 相（ToggleRow 展开 → Ctrl+A/C → 剪贴板 == 体部全文，变异体实测会红）。**reasoning 折叠体刻意不进**（思维链默认折叠、复制场景弱）。默认关不变 ⇒ 基线路径零变化（REALISTIC A/B：invalidations 29=29 逐位同） |
| 字重 / 视觉层级 | `REQ-10` **已闭合**（2026-10-24，ECDI Phase 36：`Font{family,weight}` + 库外验收 14/14，harness 已消费）；剩余视觉层级缺口（半透明文本等）受 `REQ-11` 影响（ECDI `#71`，**登记推后**，零消费者） |
| ~~**`--settings-probe` 的 4 相**~~ **已机判（2026-10-27 起）** | 四相在回放跑法下结构上不可达（designed-red 登记，理由见 `regress.py` EXTRA_PROBES 注释）；由 **§2e live 设置探针真跑真断**——真 core 重建 + 首启分支（配置必须不存在），必判九相含上述四相，另带 `protocolDeclared == 6` 身份判据 ⇒ **不是人工验收** |

### `evals/` 的口径（**已拍板 2026-11-07：② 全部读数**）

`evals/` 是评估数据的**唯一落点**：每轮工作收口时，把当轮的正/负对照读数（机器可读 JSON，
**轮级**粒度）从 `.scratch/` 归档进来；中间调试件（`_b3_*`、`_asan_*` 这类）不进。
**现状欠账（2026-11-07 已清）**：10-24 以来各轮的收口读数已按轮归档 **68 份**
（总量 84 → **152**，最新 2026-11-07）：App.cpp 拆分 before/after 全套含四阶段指纹
（`split-*`）· 10-26 的 25 探针进门横评（`probes25-*`）· P4 逐相位基线链 + G 尖峰
可选中 A/B 对 + H 模型浮窗（`p4-*`）· P3.6 设置探针 live/replay 双臂（`p36-*`）·
#71 换库 GL 回放（`lib71-*`）· 会话持久化（`session-persist-*`）· REQ-14（`req14-*`）·
v5 子代理正/负/cap 门（`v5-agent-*`）· ask 提案卡正/负（`v5-ask-*`）· MCP 可见性与
HTTP/SSE 正/负 + death-race 牙齿对（`mcp-*`）· P4 J（`p4j-*`）· 协议声明臂三件 +
83 断言冒烟（`protoarm-*` / `mcp-smoke-83assert`）。逐份来源映射见 `status.txt`
2026-11-07 EVALS-BACKFILL 条目。**不进**：被门反复覆盖的 `_reg*`（每轮重 derive）、
调查中间件（重试链/变异探索）。今后每轮收口即按本口径归档。

### 下一步（按依赖顺序，不按诱人程度）

**2026-10-26 这批收的是债务，不是功能**：25 个探针全部进门（13/25 → 25/25）· 门面文档按
`git show | wc -l` 实测重建（`App.h` 的账此前**加减不闭合**）· 关闭「`--settings-probe` 会删
用户真实配置」的缺陷（现在无 `--config` 直接拒绝启动）。

其后的顺序（2026-11-05 对齐）——**已全部收完**：

1. ~~**③ 后台长任务**——`run_command` 拆出后台作业（job_id / 输出回读 / 取消），
   复用 L1 的 Job Object 句柄~~ **已落地（2026-11-05）**（见「已收口」协议 v6 行）。
2. ~~**P4 Phase I：路径可点 + 只读预览**~~ **已落地**（见「已收口」P4 A–J 行）。
3. ~~**MCP host**~~ **已落地（2026-11-05）**：stdio 传输 + 整树清退 + ask 门 + 24KB 输出上限，
   见 `docs/mcp-host-plan.md`。~~产品面可见性~~ **已落地（2026-11-06）**：
   `hello.mcp` + 会话头副标 + 检视栏「MCP」卡 + 启动失败 `system` 块 + `--mcp-probe` 正反对照。
   ~~GUI 侧提案卡~~ **已落地（2026-11-06）**：`detail.tool:"mcp"` 判别值 +
   渲染分支 core/GUI/协议/测试同批放开（server/toolName/argsKv，问句同批改真实工具名）。
   ~~HTTP/SSE 传输~~ **已落地（2026-11-06）**：Streamable HTTP（`2025-06-18`）+ legacy
   HTTP+SSE（`transport:"sse"`）+ `url/headers` 配置 + 状态里的传输标识。
   ~~运行期语义~~ **已落地（2026-11-06）**：`--mcp-env=minimal`、崩溃后按需重启（上限 3 次）、
   取消时真发 `notifications/cancelled`；六臂（A–F）冒烟 **83 条**。

**2026-11-07 对齐**：

1. ~~**protocol 覆盖洞**：假 core 恒发 `"protocol": 3`，全部 live 支线只跑 fake-core ⇒
   2026-11-06「无参启动点『选择文件夹』无反应」（`AppHud` 的 `liveReady` 守卫 `== 3` 未随
   v4 迁移）自动化语料 100% 看不见（`status.txt` 登记 COVERAGE HOLE）。做法：假 core 加
   `--protocol=N`（默认 3 ⇒ 既有基线逐字节不动）+ 门里 live 臂声明 4/6，断言
   `sendEnabled`/`pickerEnabled` 在 idle 真转真、caps 缺 `workspace`/`model` 时对应交互隐藏。
   真 core 现声明 **6**（`core/cmd/ecdi-core/session.go:277`），洞在变宽。~~
   **已落地（2026-11-07）**：假 core `--protocol/--caps`（默认逐字节不变）+ 门 2d-b 三臂
   （v4-full / v6-full / v6-no-workspace），判据建立在 `--state-probe` 新增的同点回读字段上
   （pickerEnabled/pickerVisible/workspaceCap/protocolVer——判 GUI 的解析结果，不判传参；
   身份判据钉死「GUI 眼中的 protocolVer == 声明值」防钳位洗绿）。牙齿实测：守卫退回
   `== 3` 的变异体上 v6 臂三判据全红。范围边界：caps 缺 `model` 的热切换拒绝是交互门
   （state-probe 观测不到），有 cap 一侧由 §2e 真核 switch-ok 臂覆盖。
2. ~~**evals/ 补库**：按上面已拍板的口径，把 2026-10-24 以来各轮收口读数从 `.scratch/`
   归档进 `evals/`（每轮挑正/负对照，不搬中间调试件）。~~ **已落地（2026-11-07）**：
   68 份按轮归档（84 → 152），映射见 `status.txt` EVALS-BACKFILL 条目；今后每轮收口即归档。
3. **TODO-06 门自身可信度**：`regress.py` live 分支判据仍是 `out.exists()`（写盘后崩也绿）；
   若能拿到 PDB（gui 目标 `/Zi+/DEBUG` 或 `/MAP`）先把 F1/F2 调用链落到源行——拿得到就排
   第 2 条之前：「归属定不了之前不开新功能」。

功能侧（MCP 服务端主动通知流 / HUD 脏位修复进门 / MCP 小修包：hello.mcp 快照 respawn 后
不跟手、切工作区不重发现 `mcp.json`、model-cap 拒绝侧 live 断言）等以上收完再挑。

★ **P3（真 agent core）是主线，不是可选收尾**——见 `docs/positioning.md` §2 / §6。

## 许可证

[MIT](LICENSE) © 2026 momoya-zh
