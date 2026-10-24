# ECDI_fake_harness · P3 实施规格：真 agent core（真模型 + 真工具）

> **定位**：P1 的 GUI 吃夹具，P1.5 接上假 core 走真管道——两者都还不是「能干活的 agent」。
> P3 = 协议与 GUI 两侧已就绪的契约，第一次接上**真模型 + 真工具执行**。
> ★ P3 **是主线而非可选收尾**——本项目要的是「一个能真用的 agent GUI」，
> 见 [`positioning.md`](positioning.md) §2 / §6；假 core **原样保留**为回归仪器（§1.2）。
> 状态：**v1.3 P3.5 已实施**（2026-10-24；v1.1 = 第一批回写 §8.1–8.4；v1.2 = 真 API 收口
> §8.3b + 决策记录 §8.5；v1.3 = §8.5.5 实施回写——凭据/设置界面/无参启动 + DeepSeek 全链）。

---

## §0 已拍板的决策（2026-10-24，用户三连拍板）

| # | 决策 | 内容 |
|---|---|---|
| D1 | **模型接入** | **OpenAI 兼容端点**：`--base-url` / `--model` / `--key` 三参数可配，不绑定任何厂商（GLM / OpenAI / DeepSeek / 本地网关都能接）。规格不写死厂商；先用哪家跑通由启动参数决定 |
| D2 | **工具集（第一版）** | **只读三件**：`read_file` / `list_dir` / `grep`。全部只读。写/执行类工具第二批再上（§7） |
| D3 | **形态** | **同仓 `core/` 扩展**：新目录 `core/cmd/ecdi-core/`（Go），与 fake-core 共用 go module；公共帧层下沉 `core/internal/wire/`（§3），fake-core 行为**逐位不变**（验收 A2 把门） |

---

## §1 范围

### 1.1 做

| # | 内容 |
|---|---|
| 1 | **真 core `ecdi-core`**（`core/cmd/ecdi-core/`，Go，**零第三方依赖**）：stdin/stdout JSONL 双向（协议 v3）、stderr 日志、状态机形状与假 core 同源 |
| 2 | **OpenAI 兼容客户端**（`llm.go`）：`POST {base-url}/chat/completions`，`stream:true` SSE；标准库 `net/http` + `encoding/json`，**不引入 SDK**。增量聚合 `tool_calls`（按 `index` 拼 `name` / `arguments` 分片）与 `reasoning_content` |
| 3 | **agent loop**（`session.go`）：`prompt` → LLM 流式回复（文本/思维链 delta）→ `tool_calls` → 执行只读工具 → `tool` role 消息回模型 → 续推 → 轮结束发 `usage` → `idle` |
| 4 | **只读三件真执行**（`tools.go`）：`read_file`（文本，上限见 §4.4）/ `list_dir`（单层）/ `grep`（正则，工作区内递归，上限命中数）。路径以 **workspace 为根**解析 |
| 5 | **ask 门首次在真实路径承重**：每个真实工具调用**默认先过 `ask`**（§4.3）；`--allow-tools` 显式豁免名单（运行者的明确授权，不是 harness 启发式） |
| 6 | **workspace 真消费**：`hello.caps = ["tools","ask","workspace"]`；收到 `workspace` 请求 → 校验目录存在 → `os.Chdir` → 按协议 §3.9 规范化回显。GUI 侧工作区功能自此有真实对端 |
| 7 | **真值纪律**：`usage` 的 `turn/input/output` 全部取 API 返回的真值；`hello.context` 默认**不发**（分母未知 ⇒ GUI 显示 `—`），可 `--context N` 显式声明；`block_end.tokens` **不发**（流式 per-block token 不可得，估算 = 编造读数，协议 §3.5 明令禁止） |

### 1.2 不做（显式排除）

| 内容 | 理由 |
|---|---|
| 写工具 / shell 执行 | 第二批。`positioning.md` §4.3：**OS 级隔离是运行者前置条件**——本规格不实现隔离，也**不检查**它是否存在（诚实标注，见 §5） |
| MCP host | P4。README「下一步」：没有 core，host 没有可授予的对象 |
| 多会话 / resume / 重连 | 协议 `session` 字段已留，实现推后 |
| 自动重试 | 网络断 / API 错 → `error(fatal:false)` 显式可见，重试由用户重新 `prompt`。静默重试会把「网络慢」伪装成「模型停顿」，污染读数 |
| 长会话上下文管理（截断/摘要） | 第一版 messages 全量携带；成本与规模问题登记观察（§7） |
| **任何对 ECDI 框架 / GUI 的修改** | 铁律。GUI 侧协议实现已全就绪（§2 B2），预期**零改动**；实施撞墙才登记 REQ |
| **真 core 读数混入回归基线** | `positioning.md` §1.2：`ReplaySource` 的读数是契约，真 core 的读数**天然浮动**（网络、模型、调度）——两者**不得**进同一份基线（验收 A8 落盘时显式标注） |

---

## §2 基线事实（2026-10-24 核对）

| # | 事实 | 影响 |
|---|---|---|
| B1 | `MessageSource` 接口（`Next`/`Send`/`IsLive`）与 `ChildSource::Start(exePath, args)` 均就位；args **原样作为命令行**传 `CreateProcessW`（`ChildSource.cpp:95`）——多 flag 透传、值可带引号 | 真 core 无需 GUI 侧任何配合；`--core core/bin/ecdi-core.exe --core-args "--base-url=… --model=… --key=…"` 直接可用 |
| B2 | 协议 v3 全部相关契约 GUI 侧已实现并验证：`prompt.client_id` 回显（§4 + I9）、`ask`/I3、`error.client_id`（§3.8）、`workspace` I10、`hello.caps` 驱动 UI（§3.1） | 真 core 是这些契约的第一个**非假 core**消费者；**协议不改**——这是 P1 冻结契约的第二次真实兑现 |
| B3 | 假 core 已有 workspace 租约模式可参照：未决期间拒绝 `prompt` 与第二个 `workspace`（`core/session.go:108`） | 真 core 的交错拒绝规则直接同构复制 |
| B4 | `state` 驱动使能、`waiting → answer` 全链、`usage`/上下文百分比渲染均已在假 core 上验证 | 真 core 只需把 `state`/`ask`/`usage` 发对，渲染侧零风险 |
| B5 | `go1.25.5 windows/amd64` 在 PATH；SSE 是纯文本协议，标准库足够 | 零第三方依赖原则可维持（B1 同上） |
| B6 | 假 core 的 `hello` 在进程启动即发、`context` 字段可选、`caps` 驱动 GUI 交互可见性 | 真 core 同形；`context` 按 D1 纪律处理（§1.1 条 7） |
| B7 | 协议 §3.5：`tokens` 可选，**禁止用字符数冒充**；§3.1：分母未知必须显示 `—`，**不得猜测或按 0 处理** | 真 core 的「真值纪律」不是风格偏好，是协议明文（§1.1 条 7） |

---

## §3 文件清单

| 文件 | 职责 |
|---|---|
| `core/internal/wire/` | **公共帧层**（从假 core 下沉，唯一真相源）：JSONL 帧读写、信封（`t`/`seq`/`ts`）、seq 分配器、stderr 日志器（含**密钥遮蔽**）、协议常量（消息类型 / caps / 状态名）。★ 下沉后假 core 改为引用，行为逐位不变（A2 把门） |
| `core/cmd/ecdi-core/main.go` | 真 core 入口：flags（§4.1）、stdin 行读循环、`bye`/EOF 收尾 |
| `core/cmd/ecdi-core/session.go` | agent loop + 状态机（idle/running/waiting/cancelled/error，与协议 §6 同形；I3 自守：`ask` 后、`answer` 前不发任何消息） |
| `core/cmd/ecdi-core/llm.go` | OpenAI 兼容客户端：SSE 解析、delta 聚合（文本 / `reasoning_content` / `tool_calls` 按 index）、usage 提取、http 请求级 cancel（§4.6） |
| `core/cmd/ecdi-core/tools.go` | 只读三件：参数校验（workspace 根解析 + 越界拒绝）、执行、结果截断（§4.4） |
| `core/go.mod` | 不变（module `ecdi-fake-core`；改名推后，避免一次实施两个动作） |
| `core/bin/ecdi-core.exe` | 构建产物（`go build -o bin/ecdi-core.exe ./cmd/ecdi-core`；`/core/bin/` 已 gitignore） |
| **gui/ 侧** | **预期零改动**（B2）。若有缺口 → 登记需求，不在本阶段顺手改 |

---

## §4 行为规格

### 4.1 flags

```
--base-url string    OpenAI 兼容端点根（如 https://open.bigmodel.cn/api/paas/v4）；必填
--model string       模型名（如 glm-4.6）；必填
--key string         API 密钥；缺省读环境变量 ECDI_CORE_KEY；两者皆空 → 启动即退出（stderr 说明）
--context int        上下文窗口容量（token）；0 = 不发 hello.context（GUI 显示 —）
--allow-tools string 逗号分隔的免 ask 工具名单（如 "list_dir,grep"）；空 = 全部 ask（默认）
--workspace string   初始工作区；缺省 = 进程 cwd（与假 core 同形，stderr 记 initial workspace）
--quiet              关闭 stderr 日志
```

★ **密钥卫生**：密钥**绝不**进入 stderr 日志、摘要、转录或 `bye.reason`（日志器在 wire 层统一遮蔽）；`--key` 出现在进程命令行属可接受的第一版形态（本机单用户场景），环境变量是推荐路径（A9 把门）。

### 4.2 一轮对话的消息流（协议 v3 全契约）

```
prompt(text, client_id)
  → state running
  → block_start kind:user（回显同一 client_id，I9）… block_end
  → [LLM 流式]
      reasoning_content 分片 → block_start kind:reasoning + delta … block_end（不发 tokens）
      content 分片           → block_start kind:assistant + delta … block_end
      tool_calls 聚合完成    → block_start kind:tool_call(id, name, args)
                               → ask（§4.3）→ 执行
                               → tool_result(for=id, ok, text)
                               → block_end(ok)
                               → tool role 消息回 LLM → 续推
  → usage(turn/input/output，API 真值)
  → state idle
```

- ★ **`tool_call` 的 `block_start` 在发起执行前发**：GUI 的「等待结果」卡于是有真实的时间窗（假 core 里它是瞬时伪造的）——这正是 positioning §2.1 要的真负载。
- ★ **流式文本按 API 分片逐条 `delta`**：不做服务端聚合再发——「真时序、真突发」是 P3 的评估目的（REQ-03 的 `PostToUi` 推路径自此有真实突发可压）。
- `reasoning_content` 缺失（模型不产思维链）→ 不发 reasoning 块，无协议影响。

### 4.3 ask 门（真实路径首次承重）

- 每个工具调用执行前发 `ask {kind:"permission", question:"<name> <参数摘要>", options:[allow/deny], default:"deny"}` → `state=waiting` → **阻塞等 `answer`**（I3 自守）。
- `deny` ⇒ **不执行**；回模型的消息为工具结果「用户拒绝了该操作」（模型可据此调整），GUI 侧 `tool_result(ok:false, summary:"用户拒绝")`。
- ★ 豁免只经 `--allow-tools`（运行者的**配置**）——harness/core 不做任何「这个该不该问」的启发式（`positioning.md` §5.2 配套纪律：只做配置 + 展示 + 转发，不做策略）。
- ★ 只读 ≠ 无害：`read_file` 读得到任何可读文件（包括别的项目的密钥文件）——默认全 ask 是**实际安全意义**，不只是仪式。

### 4.4 工具规格（只读三件）

| 工具 | 参数 | 行为 | 限制 |
|---|---|---|---|
| `read_file` | `path` | 读文本（UTF-8；含 BOM 原样保留） | 相对路径以 workspace 为根；**越界拒绝**（解析后不在 workspace 内 → 工具报错，不执行）；上限 40 KB，超出截断并在结果尾部标注「（截断，共 N 字节）」 |
| `list_dir` | `path`（缺省 `.`） | 单层目录：目录在前带 `/` 尾、文件在后，按名排序 | 同越界拒绝；单层不递归（GUI 侧栏语义一致） |
| `grep` | `pattern`, `path`（缺省 `.`） | Go 正则，workspace 内递归文本文件，输出 `相对路径:行号: 行内容` | 命中上限 200 行 + 截断标注；跳过二进制（NUL 探测）与超大文件（>1 MB） |

★ **GUI 渲染与模型输入用同一份结果文本**（同一截断）——两份文本会出现「GUI 看到的和模型拿到的不一样」，那是一个新的不可判定面。

### 4.5 workspace（协议 §3.9 真消费）

- `caps` 声明 `"workspace"`；仅 `idle` 受理；未决期间拒绝 `prompt`（忽略并警告）与第二个 `workspace`（`ok:false` + `message`）——与假core 同构（B3）。
- 校验：目录存在且是目录 → `os.Chdir` → 回显规范化路径（绝对、`/` 分隔、无尾随、无 `.`/`..`）；失败 → `ok:false` + `message`，**不切**。
- ★ **不重试降级**：拒绝就是拒绝——静默换目录正是协议 §3.9 存在要禁止的「猜测」。

### 4.6 cancel / 错误 / 退出

- `cancel`（`running` 时合法）→ `http.Request` 级 cancel（不是等这轮跑完）→ `state=cancelled` → `state=idle`；已产生的部分块照常 `block_end(ok:false)`（I6：end 必须各恰一次）。
- API 错误（HTTP 非 2xx / SSE 中断 / 超时）→ `error {fatal:false, message}`（带 `client_id` 若在拒绝 prompt 场景）→ `state=idle`；会话继续。
- `bye` / stdin EOF → `bye {reason:"eof", code:0}` 退出 0；未决 ask/未确认响应随进程消亡，GUI 按协议判定失败（§4 失败路径表）。

---

## §5 安全与信任（引用不改写）

`positioning.md` §4 是本节全部内容，这里只放**指针**：

1. **协议不是安全边界**（§4.1）——stdio 通道传消息，不提供隔离。
2. **ask 门是人在环控制**（§4.2）——让用户看清 agent 打算干什么；拦不住恶意 core。
3. **OS 级隔离是运行者前置条件**（§4.3）——容器 / VM / 受限账户；本规格**不实现也不检查**。
   第一版只读三件把风险**降**了（不可写、不可执行），**没有除**（可读一切可读文件）——
   「降」不等于「除」，这句必须留着。

---

## §6 验收 / 出口判据

| # | 判据 | 验证方式 |
|---|---|---|
| A1 | `core/` 构建成功（`go build ./...` + `go vet ./...`） | 退出码 0 |
| A2 | **假 core 逐位回归**：下沉 wire 层后，六夹具结构读数逐位不变（demo 8/19/43 · realistic 220/717/1213 · cot 44/934/1044 · scale 2000/4590/8906 · hostile ×2）+ 13 探针仅设计红 + `validate.py` exit 0 | `tools/regress.py` |
| A3 | **真模型端到端**：真实 API 流式回复渲染正常（assistant 文本流 / reasoning 折叠卡 / 工具卡 / usage 用量） | 实跑 + 目视 + 摘要读数 |
| A4 | **真实工具全链**：tool_call → ask → allow → 真执行 → tool_result → 模型续推（GUI 与 core stderr 两侧取证）；deny 路径同验（不执行、模型收到拒绝） | 实跑 + core 日志 |
| A5 | **seq 真实路径零缺口**：真 core 全程 `seqGaps=0`、`violations=0`、`unknownKinds=0` | live 摘要 |
| A6 | **cancel 真取消**：running 中点停止 → API 流即断（stderr 时间戳 vs GUI 读数）→ state 回 idle，可再发 | 实跑 |
| A7 | 退出干净：关窗后 ecdi-core 退出（无僵尸） | 任务管理器 + core 日志 |
| A8 | **读数归档带标注**：`evals/p3-*.json` 落盘，文件内显式标注「真 core 读数，天然浮动，不进回归基线」（positioning §1.2） | 文件 + git |
| A9 | **密钥零泄漏**：stderr 日志、摘要、转录、`bye.reason` 全文无 `--key` 值 | grep 取证 |

---

## §7 已知局限（明确推后）

- **无写工具 / shell**（第二批）——上 shell 前必须在运行者侧落实 OS 级隔离（§5）；
- **`block_end.tokens` 不发**——OpenAI 兼容流式无 per-block token；reasoning 副标不显示（协议允许，§3.5）；
- **`hello.context` 默认不发**——分母未知显示 `—`；要百分比就 `--context` 显式给（B7 真值纪律）；
- **无自动重试**、无多会话 / resume、无长会话上下文管理（§1.2）；
- **单轮内工具并行**不做（模型返回多个 tool_calls 时**串行**逐个 ask/执行）——I3 只允许一个未决 ask，串行是协议最省心的合规形态；
- **密钥管理**第一版形态（参数 / 环境变量）；DPAPI 或凭据管理器推后。

---

## §8 实施回写（v1.1，2026-10-24）

> 依据：条 30（实现回写三件套）· 条 80（描述被改动证伪）。§4 原措辞不抹除，差异以引用保留。

### 8.1 落地清单

| 文件 | 实况 |
|---|---|
| `core/internal/wire/{wire,out,in,workspace}.go` | **只搬运，未做改进**（包注释即纪律）：State/Code、Out（seq+信封字段序+burst 必刷表）、GuiMsg/Parse/StampedLine/ReadStdin、NormalizeWorkspace；NewLogger 增加**密钥全局遮蔽**（每条日志格式化后统一过一遍——「打日志时记得遮」的纪律迟早失效，同 --burst 必刷点教训） |
| `core/{main,session}.go` | 假 core 改引用 wire；emit 变薄包装（唯一附加 = --drop-seq 注入）；**行为逐位不变**由 A2 把门 |
| `core/cmd/ecdi-core/{main,session,llm,tools}.go` | 真 core 全量：flags/密钥卫生、agent loop、OpenAI 兼容 SSE、只读三件 |
| `core/cmd/ecdi-core/{session,tools}_test.go` | **离线 E2E**（脚本化 SSE 服务器）+ 边界单测——真 API 到位前，A3/A4/A6 的**可确定性前身** |
| `core/bin/ecdi-core.exe` | 构建产物；`go build ./... && go vet ./...` 干净 |
| **gui/ 侧** | **零改动**（§2 B2 兑现：协议 v3 契约 GUI 侧本就全实现） |

### 8.2 实施期发现（规格没写、实施时定案的事）

| # | 原规格表述 | 实施后事实 / 定案 |
|---|---|---|
| R-1 | §4 未规定**轮内 stdin 消费者**的设计（只写了 cancel 要打断） | 假 core 的 cancel 本就只在轮间/waitForAnswer 生效；真 core 必须**整轮**有人读 stdin 否则「停止」永远到不了进行中的 HTTP 流。定案 = `turnListener`：cancel/answer/bye 就地处置，**prompt/workspace 进 deferred 队列、轮后按到达态戳处置**——就地处置会和 handlePrompt 的状态写入交错；到达态戳保证「轮内抢跑」不被误判成「轮后到达」（E2E `PromptDuringTurnRejected` 钉住） |
| R-2 | — | **测试先于任何运行抓到协议违规**：`streamBlocks.onDelta` 收尾 reasoning 块后未清 `reasoningID`，`closeAll` 二次发 end——正是 E2E 的 I1 断言（块 start/end 各恰一次）抓的。判据有效性的直接证据：不是测试写完就绿，是它真的红过 |
| R-3 | §6 各判据「验证方式」未写死观测点 | 全部断言走 `wire.Out` 的**真实出站路径**（`newSession` 为此注入 `io.Writer`），不调内部函数看返回值——「函数对、链路通」是两件事（0c buildTitle 教训的延续） |
| R-4 | §4.6「usage 用 API 真值」 | API 没回 usage 就**不发**（协议 §3.7 无强制）；E2E 脚本服务器的 `include_usage` 末尾块覆盖该路径（120/45 真值断言） |

### 8.3 验收状态（对照 §6）

| # | 判据 | 状态 |
|---|---|---|
| A1 | 构建 + vet | ✅ `go build ./... && go vet ./...` 干净；`ecdi-core.exe` 冒烟（缺参 exit 2 带说明 / 握手 / bye / 干净退出） |
| A2 | 假 core 逐位回归 | ✅ `tools/regress.py` **PASS**：六夹具逐位（demo 8/19/43 · realistic 220/717/1213 · cot 44/934/1044 · scale 2000/4590/8906 · hostile ×2）、13 探针仅设计红、REQ-03 live A/B 全过、`validate.py` exit 0 |
| A3/A4/A6 | 真模型端到端 / 真工具全链 / cancel 真取消 | ⏳ **离线前身全过**（脚本化 SSE：全链含思维链分片+参数撕裂聚合 / deny / 豁免 / cancel 打断挂住的流 / 轮内抢跑拒绝 / workspace 切换+新根工具执行）；真 API 验收待 BaseURL/Key |
| A5 | seq 零缺口 | ✅（离线）每条 E2E 全流 `assertSeqContinuous`；真 API 复验待密钥 |
| A8 | 读数归档标注 | ⏳ 待真跑 |
| A9 | 密钥零泄漏 | ✅（单测：stderr 管道真捕获，遮蔽后形态 `key is ***`；冒烟目视复核）+ wire 层全局遮蔽机制 |

### 8.3b 真 API 验收（2026-10-24 补，云知声 u2-flash）

用户提供云知声 OpenAI 兼容端点（`https://maas-api.unisound.com/v1`，**免费至 2026-10 月底**）。
管道级（`.scratch/p3_pipe_driver.py`，仪器在 .scratch 不入库）+ GUI 级两路验收：

| 判据 | 读数 |
|---|---|
| **A3 真流式** | u2-flash 逐 token 流式 34 delta 落 GUI；`reasoning_content` 真实出现并被 reasoning 块接住（b2=英文思维链、b3=中文正文）；usage 真值 710/60（API 返回，非估算） |
| **A4 真工具全链** | read_file 真执行：`tool_call` 块 → ask（allow）→ 真读 README.md → 结果文本回模型（GUI 渲染与模型输入同一份）→ 模型据实作答；`--allow-tools` 豁免路径同验（跳过 ask 直接执行） |
| **A5 seq 零缺口** | GUI 级摘要 **seqGaps=0 / violations=0 / unknownKinds=0 / unknownTypes=0**，protocolDeclared=3；管道级全流逐行扫描无缺口 |
| **GUI 端到端** | `--core core/bin/ecdi-core.exe` 拉起真 core：7 块 / 60 delta / 82 消息，`usageInput=9151 / contextLimit=160000` —— 上下文占用百分比第一次拿到**真 core 的真分母** |
| **A8 归档** | `evals/p3-u2flash-live-1.json`（带「天然浮动，不进基线」标注 + 判据口径说明） |
| **实施期修正** | ★ **真 API 抓到离线 E2E 抓不到的 schema 错误**：tools 定义缺 OpenAI 兼容协议的 `{"type":"function","function":{…}}` 包装层（云知声报 `'tools[0].type' must be 'function'`）——已在 `llm.go` 加 `toolEnvelope`。这正是规格 §8.3 分工预言的那类缺陷：脚本服务器只记录不校验 |

**未验（留待）**：A6 cancel 真取消的 GUI 级实跑（离线前身已过；GUI 级需要交互式点停止，无现成 flag）；workspace 切换的 GUI 级实跑（离线已过）。

### 8.4 下一步

1. ~~**真 API 验收**~~ ✅ 已完成（§8.3b，云知声 u2-flash）；云知声免费至 2026-10 月底，**11 月起需换端点**（`--base-url/--model/--key` 三参即换，无代码改动）；
2. ~~**harness GUI 拉起真 core 的端到端**~~ ✅ 已完成（§8.3b GUI 级，gui/ 零改动兑现）；
3. E2E 接入 `tools/regress.py` 的 `go test ./...` 步骤（当前假 core 回归门不含真 core 测试——登记待办）；
4. A6 cancel 的 GUI 级实跑与 workspace 切换的 GUI 级实跑（离线前身均已过）。

### 8.5 密钥保存与无参启动（2026-10-24 用户拍板，**决策记录，未实施**）

> 背景讨论三方参与：用户（提出配置文件 + 哈希的想法并自我纠正）、外部评审（GPT，建议
> Credential Manager 由 core 自读）、本仓（修正前提后给推荐）。**用户拍板采纳修正版**。

#### 8.5.1 事实前提（讨论中核实，防止方案建立在错误图景上）

- **key 从来不走协议管道**：stdin 六类消息（hello/prompt/cancel/answer/workspace/bye）无任何秘密。
  key 到达 core 的路径是**进程环境继承**——GUI `CreateProcessW` 不传显式 env block，子进程继承父环境。
- **core 侧恰有一条摄入路径**：`--key` > `ECDI_CORE_KEY`，皆空拒启（exit 2）；core 无凭据缓存、无配置文件。
- **GUI 侧零凭据代码**；"两边各造一套"的风险不存在（只有一个读取点）。
- ⇒ 真正的决策点是**谁解析凭据**，不是"走不走管道"。

#### 8.5.2 拍板结论（D4–D7）

| # | 决策 |
|---|---|
| **D4** | 凭据存储 = **Windows Credential Manager**（`CredWriteW/CredReadW`），读写**都在 GUI 侧**。core **不接触**凭据 API——保住"零第三方"（Go 标准库与 `x/sys/windows` 均无 `CredRead` 绑定，core 自读要么引依赖要么手写 syscall，且把 core 绑死 Windows）。目标名命名空间：`ECDI.FakeHarness.v1.Provider.<provider-id>`（多提供商 = 多目标名） |
| **D5** | 传递 = `CreateProcessW` **显式 `lpEnvironment` 注入** `ECDI_CORE_KEY`——不经协议、不经命令行、不经任何文件。core 侧 `--key/ECDI_CORE_KEY` 接缝**保持不变**（继续服务于手工管道调试的独立运行场景） |
| **D6** | 非秘密配置（base-url / model / provider-id / **凭据目标名引用**）持久化在本地 JSON 配置文件；**配置文件永不携带密钥值**（dsh 的 refs 模式：存名字不存值） |
| **D7** | 目标体验 = **无参数双击启动**：有配置 → 直连默认提供商；无配置 → **首次运行设置对话框**（三个字段：BaseURL、APIKey、模型）。BaseURL/模型进配置文件，APIKey 进 Credential Manager |

#### 8.5.3 外部评审约束清单 → 现状对照

| GPT 约束 | 现状 |
|---|---|
| key 不进日志/转录/错误详情 | ✅ **已满足且更严**：wire 层 `NewLogger` 全局遮蔽 + A9 单测（stderr 管道真捕获）；实施后需把 A9 **扩展到 GUI 侧**（GUI 自己的日志/摘要同样不得含 key） |
| 读取失败 → 明确非敏感错误 | ✅ 已满足：缺 key = exit 2 带说明 |
| 连接测试失败不自动删 key、不判无效 | ✅ 已满足：API 错误走 `error(fatal:false)` 显式显示 |
| 稳定命名空间 | ⬜ 随 D4 实施（采纳 `ECDI.FakeHarness.v1.Provider.<provider-id>`） |
| 清理输入缓冲区 | ⬜ 随设置对话框实施（C++ 侧小事） |
| core 保留独立凭据存储接口（跨平台） | ✅ 天然满足：core 只认环境变量，本身就是那个接口 |

**诚实边界**（GPT 与 positioning §4 同一句话）：Credential Manager 防的是**静态文件读取**；
同一 Windows 用户上下文的程序仍可枚举凭据、检查进程内存——这不是安全边界，是 at-rest 卫生。

#### 8.5.4 实施注意（登记，实施时逐条核）

- `ChildSource::Start` 需新增显式 env block 参数（当前继承父环境）。取舍：**显式 block**（干净、
  不污染 GUI 全进程环境）vs GUI 进程 `SetEnvironmentVariable` 后靠继承（简单但全进程可见）——倾向前者，实施时定。
- key 在 GUI 侧的内存生命周期：CredReadW → 注入 env block → **尽早释放**；不落 GUI 可读字符串的长期持有物。
- 配置文件位置：用户目录（不进仓库、不进安装目录），具体路径实施时定（%APPDATA% 或本地 home 目录，参考 dsh 的 HOME 形态）。
- 无配置的首启降级：`--core`/`--replay` 手动模式**保持可用**（诊断与回归路径）；设置对话框只做"无参数时"的默认入口。
- ECDI 侧**无新需求**（普通 Widget 即可搭对话框），不触铁律。
- 时机：**独立 GUI 功能批次（P3.5 候选）**，与 MCP host 无强耦合——修正本节早先"等 MCP host 立项"的说法；
  dsh 式 describe（只报 set/source/writable、不回显值）与多提供商目录在多提供商需求出现时再引入。

#### 8.5.5 实施回写（2026-10-24 深夜，P3.5 已落地）

**落地清单**：`gui/src/Credentials.{h,cpp}`（CredWriteW/CredReadW 薄封装 + 密钥零错误文本）、
`gui/src/AppConfig.{h,cpp}`（%APPDATA%\ecdi-fake-harness\config.json；**结构体无密钥字段**——防泄漏靠类型）、
`ChildSource::Start`（见 R-5）、设置面板（中栏视图，占 scroll+composer 槽；key 输入框用
ECDI P1 的 `TextBox::EchoMode::Password`——显示层打点、数据层不变）、菜单「设置」入口、
无参启动分支（`--help` 承载用法文档）、`--config <path>` 路径覆盖（探针隔离）、`--settings-probe`。

**实施期发现（规格没写、实施时定案的事）**：

| # | 事实 |
|---|---|
| R-5 | **手搓环境块废弃，改"启动环境层"**（dsh 的 launching-environment 同层）：GUI 进程 `SetEnvironmentVariableW("ECDI_CORE_KEY",…)` + `lpEnvironment=nullptr` 继承。原方案（父环境复制+覆盖+显式传入）在当前启动环境**恒报 87**（ERROR_INVALID_PARAMETER），负对照（nullptr）正常 ⇒ 缺陷确在块本身；**未决问题**：87 的精确成因（环境块规模超限的假说未验证）。保证不变：key 仍不经协议/命令行/配置文件。设置对话框只读路径同构（CredReadW → m_coreApiKey → spawn 前注入） |
| R-6 | **探针跨 tick 推进点必须铺满所有会话形态**：--settings-probe 的派发点第一版只放在 replay settle 分支，save 之后活动会话已是 live ⇒ tick 走 live 分支提前 return，stage 1 永不推进（摘要只有 2 相）。reasoning-probe"第一版只放末尾"教训的再现——这次是"分支维度的遗漏" |
| R-7 | **固定等待窗口对进程冷启动太紧**：session-live 首版 400ms 一锤定音，偶发 LinesReceived==0（新 exe 首启抖动）。改 5s 轮询（每 tick 回看，超时才判红） |
| R-8 | **判据自家的 bug 让负对照不可观测**：first-run 相的 scroll 检查写成 `!bad.empty() &&`——只有先红了才查，而面板默认可见恰好让第一判通过 ⇒ M4（投影缺失）永远绿。修成独立判后 M4 立刻红。负对照"NOT-RED"先查判据、再查实现 |
| R-9 | **真 API 的 schema 校验是离线 E2E 结构上抓不到的类（第二例）**：Go 可变参数零实参 = **nil slice** ⇒ `"required":null`，DeepSeek 严格校验 400。与 toolEnvelope 同类；curl 复测三变体全过时更说明必须 dump **实际序列化字节**对拍，不能拿手写等价物测 |
| R-10 | **`cmdkey` + Git Bash 的 MSYS 路径转换坑**：`/generic:` `/pass:` 被转成 `C:/Program Files/Git/...`，凭据值实际只存进 "s"——而产品的日志掩码形态（`work***pace`）当场暴露了这件事。教训：写凭据走 ctypes `CredWriteW`，不过 shell |

**验收**：`--settings-probe` 5 相（first-run / save / session-live / env-check / cleanup）
**×3 连跑全绿**；负对照 `.scratch/probe_settings_negctl.py` **4/4 RED**（M1 摘注入→session-live 红
[lines=0 exited=no]、M2 配置带 apiKey 字段→save 红[结构性泄漏]、M3 跳过 CredWrite→save 红[凭据缺失]、
M4 投影缺失→first-run 红[scroll+composer 未腾槽]）+ clean 全绿；全量回归 **PASS**（六夹具逐位 +
13 探针仅设计红 + REQ-03 live A/B + validate exit 0）；**DeepSeek 官方端点无参启动全链**
（预置凭据 + 配置 → TryAutoConnectFromConfig → 真模型 → read_file 真执行 len=38702 → 结果回模型 →
232 delta；seqGaps=0/violations=0/unknownKinds=0；usage 真值 12134）——A8 归档
`evals/p35-deepseek-noarg-live-1.json`。密钥零泄漏复核（tracked 文件 grep = 0）。

**R-11（用户实测抓到，同日修）**：双击 exe 的默认 cwd = `gui/build/Release`，`ResolveCoreExecutable` 只查 cwd 和 exe 目录两个固定点 ⇒ 找不到仓库根布局的 `core/bin/ecdi-core.exe` ⇒ 自动连接失败、首启面板里保存也失败（同一函数）⇒ **"会话没有输入框"的死胡同**（设置面板占满中栏、保存又永远失败）。修法 = 从 cwd 与 exe 目录**向上找祖先**（≤6 级，同时覆盖仓库根跑 / Release 目录双击 / 平铺安装布局），已用"无 corePath 的配置 + Release 目录启动"的截图实测连通；面板另加「关闭」按钮（无 live 会话时补引导通知，composer 回归、发送得到诚实的「只记录」降级——不是无声空白）。★ 教训：无参启动的**验收截图全在仓库根 cwd 下跑的**，双击场景（exe 目录 cwd）一次都没拍过——"能跑"的路径覆盖 ≠ 用户实际路径覆盖。

**登记未做**：`--settings-probe` 未接入 `tools/regress.py`（第 14 条探针，同前）；R-5 的 87 成因；设置界面暂不暴露 context 字段（真值纪律下 0 = 不发 hello.context，GUI 显示 —）。
