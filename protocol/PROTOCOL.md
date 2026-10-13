# ECDI_fake_harness 协议 v1

> **这个文件的定位**：`core`（Go）与 `gui`（C++ / ECDI）之间**唯一的契约**。
> 它先于两侧任何一行实现存在——因为 GUI 可以只看它就能跑起来。
> 状态：**v1 冻结待用**（2026-10-03 起草）。改这份文件 = 改协议版本号。

---

## 0. 为什么是「假」harness

`gui` 是**协议的渲染器**。协议定好之后，GUI 可以对着**手写/录制的 JSONL** 跑完整个界面，
既不需要 Go 核心，也不需要模型。于是：

- 界面与流式渲染能**在没有 agent 的情况下**被开发与度量；
- 同一份夹具可以**离线重放**，成为回归基线；
- 项目的第一个可交付物是**评估 ECDI 的数据**，而不是一个 agent。

这是 ECDI 里 `RecordingBackend` 那个套路的第二次使用。

---

## 1. 编码与分帧

### 1.1 仓库编码约定

| 文件类型 | 编码 | 行尾 | 理由 |
|---|---|---|---|
| 文本（`.md` / `.cpp` / `.h` / `.go` / `.py`） | UTF-8 **带 BOM** | CRLF | 中文被 MSVC 按 CP936 读会 C4819 + 乱码（ECDI 条 70 的教训）；**一条规则 + 一个例外**比记一堆特例便宜 |
| **`*.jsonl`** | UTF-8 **不带 BOM** | **LF** | BOM 会破坏流式 JSON 解析；LF 是 JSONL 工具链的预期 |
| **配置文件**（`.gitignore` / `CMakeLists.txt` / `go.mod` / `*.json`） | UTF-8 **不带 BOM** | 随该工具的预期 | 与 ECDI 同源的例外：清单 / listfile 类文件以**解析兼容性优先**，BOM 会惹麻烦 |

### 1.2 分帧

- **一行一个 JSON 对象，`\n`(LF) 结尾**，行内不得有裸换行。
- 两侧各自独立地按行读；**读到不完整的最后一行（EOF 截断）必须报错，不得静默丢弃**。
- 未知字段**必须忽略**（前向兼容）；未知 `t` 值**忽略并记录**（不得崩溃）。
- 流里不允许 pretty-print（一行就是一整条消息）。

---

## 2. 信封

每条消息都是：

```json
{ "t": "<类型>", "seq": 0, "ts": "2026-10-03T10:40:00.000Z" }
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `t` | ✅ | 类型判别符，见 §3 / §4 |
| `seq` | ✅ | **每方向独立、从 0 起、严格递增**。缺口 = 丢失，见 I4 |
| `ts` | core→gui 必填 | ISO-8601 UTC（毫秒）。**回放的节奏唯一来源**（§8） |

`seq` 与 `ts` 不是装饰：它们**就是**评估指标 ①③ 的量具（§10）。

---

## 3. core → gui 消息

### 3.1 `hello`

握手的**第一条**消息，`seq` 必须为 0。

```json
{"t":"hello","seq":0,"ts":"...","protocol":1,"core":"0.1.0","session":"s-0001","caps":["tools","ask"]}
```

| 字段 | 说明 |
|---|---|
| `protocol` | 协议**主版本**。GUI 的版本较低时必须拒绝并显示错误（§9） |
| `core` | 核心版本，仅用于显示 |
| `session` | 会话 id；`--resume` 时回放同一 id |
| `caps` | 能力声明，GUI 据此决定是否显示对应交互 |

### 3.2 `state`

```json
{"t":"state","seq":1,"ts":"...","value":"running"}
```

| `value` | 含义 |
|---|---|
| `idle` | 空闲，可接受 `prompt` |
| `running` | 正在处理 |
| `waiting` | **阻塞在一个未回答的 `ask` 上**（I3） |
| `cancelled` | 因 `cancel` 而停止 |
| `error` | 致命错误，随后进程退出 |

### 3.3 `block_start`

转录块的开始。**每个 id 只能出现一次**（I1）。

```json
{"t":"block_start","seq":2,"ts":"...","id":"b2","kind":"assistant"}
{"t":"block_start","seq":3,"ts":"...","id":"b1","kind":"user","text":"帮我看看 README"}
{"t":"block_start","seq":4,"ts":"...","id":"b3","kind":"tool_call","name":"read_file","args":{"path":"docs/README.md"}}
```

| `kind` | 用途 | 附带字段 |
|---|---|---|
| `user` | 用户输入（**由 core 回显**，见 §8.3） | `text` |
| `assistant` | 模型输出（通常流式） | — |
| `tool_call` | 一次工具调用 | `name`、`args`（原样 JSON 对象） |
| `tool_result` | 对应的结果 | `for`（被引用块的 id） |
| `system` | 系统提示 / 分隔 | `text` |
| `error` | 错误 | `text` |

可选 `text`：块的**初始文本**。最终文本 = `block_start.text` + 该块所有 `delta.text` 顺序拼接。

### 3.4 `delta`

流式增量。同一块的多次 `delta` 按 `seq` 顺序拼接。

```json
{"t":"delta","seq":5,"ts":"...","id":"b2","text":"我先看"}
```

### 3.5 `block_end`

块的终结。**此后该块永不再变**（I6）。

```json
{"t":"block_end","seq":13,"ts":"...","id":"b2","ok":true,"summary":"共 3 段"}
{"t":"block_end","seq":20,"ts":"...","id":"b8","ok":false,"summary":"2 项失败"}
```

### 3.6 `ask`

权限门 / 提问。**core 会阻塞**直到 GUI 回 `answer`（一次只能有一个未决 `ask`，I3）。

```json
{"t":"ask","seq":29,"ts":"...","id":"q1","kind":"permission","question":"允许修改 docs/README.md 吗？","options":[{"key":"allow","label":"允许"},{"key":"deny","label":"拒绝"}],"default":"deny"}
```

> `default` 只在 GUI 侧超时/关闭时使用；正常路径必须显式回答。

### 3.7 `usage`

```json
{"t":"usage","seq":40,"ts":"...","turn":3,"input":18422,"output":913}
```

### 3.8 `error`

```json
{"t":"error","seq":41,"ts":"...","fatal":false,"message":"工具超时"}
```

### 3.9 `bye`

core 退出前的**最后一条**。

```json
{"t":"bye","seq":42,"ts":"...","reason":"eof","code":0}
```

| `reason` | 含义 |
|---|---|
| `eof` | stdin 关闭，正常收尾 |
| `cancel` | 被取消 |
| `error` | 异常退出，配合 `code` |

---

## 4. gui → core 消息

| `t` | 载荷 | 何时发 |
|---|---|---|
| `hello` | `protocol`、`gui` | 收到 core 的 `hello` 之后，`seq` 0 |
| `prompt` | `text` | 用户提交输入（仅 `idle` 时合法） |
| `cancel` | — | 用户点停止（`running` 时合法；`waiting` 时应改发 `answer`） |
| `answer` | `id`、`choice` | 回答一个未决 `ask` |
| `bye` | — | GUI 即将关闭，请 core 退出 |

```json
{"t":"hello","seq":0,"protocol":1,"gui":"0.0.1"}
{"t":"prompt","seq":1,"text":"帮我看看 README"}
{"t":"answer","seq":2,"id":"q1","choice":"allow"}
{"t":"cancel","seq":3}
{"t":"bye","seq":4}
```

**GUI 不产生转录块。** 用户输入由 core 回显为 `kind:"user"` 块——转录的**唯一作者是 core**（§8.3）。

---

## 5. 不变量

| # | 内容 | 违反时的可见症状 |
|---|---|---|
| **I1** | 一个 `id` 的 `block_start` / `block_end` **各恰好一次**，且 end 在 start 之后 | 卡片重复或永远转圈 |
| **I2** | 一个块的 `delta` 只能落在它的 start 与 end **之间** | 文本落到错误的卡片 |
| **I3** | 发出 `ask` 之后、收到 `answer` 之前，core **不得再发任何消息**（`usage` 除外）；`state=waiting` 是 `ask` 的**伴随消息**，必须紧随其后；**未决 `ask` 至多一个**。★ 夹具里 `answer` 是隐含的（§8.3）⇒ **可机检形式** = 「`ask` 后必须紧跟 `state=waiting`；在 `state` 回到 `running` 之前不得再发 `ask`」 | 权限门叠层 / 死锁 |
| **I4** | `seq` 每方向严格递增**且连续**；出现缺口 ⇒ **GUI 必须显式提示"消息丢失"**，不得静默继续 | 静默丢帧——本项目最想抓的失败 |
| **I5** | 未知 `t` / 未知字段被忽略并记录 | 崩溃 |
| **I6** | `block_end` 之后该块**只读** | 重放与实时两条路径行为不一致 |
| **I7** | `tool_result` 必须带 `for`，指向一个已存在的 `tool_call` id | 结果卡片挂不上调用卡片 |

> **I4 是本项目的核心评估钩子**：`Application::PostToUi` 的乱序/丢失只有靠它才看得见。

---

## 6. 状态机

```
        hello
          │
          ▼
       ┌──────┐  prompt   ┌─────────┐
       │ idle │──────────▶│ running │
       └──────┘           └────┬────┘
          ▲                    │  ▲
          │ block_end(all)     │  │ answer
          │                    ▼  │
          │              ┌──────────┐
          └──────────────│ waiting  │
                         └──────────┘
   running ──cancel──▶ cancelled ──▶ idle
   任意状态 ──fatal error──▶ error ──▶ 进程退出
```

GUI 侧对 `prompt` / `cancel` 的按钮使能**只由 `state` 驱动**（不要自己推测）。

---

## 7. 渲染映射（v1）

| `kind` | v1 渲染 | 用到的 ECDI 能力 |
|---|---|---|
| `user` | 右对齐、次要底色的一行文本 | `Label` / `Panel` |
| `assistant` | 纯文本，保留换行 | `Label` |
| `tool_call` | **可折叠卡片**，默认折叠；标题 = `name`，展开体 = `args` 美化 | `CollapsiblePanel` |
| `tool_result` | 可折叠卡片，标题 = `ok ? 成功 : 失败` + `summary` | `CollapsiblePanel` |
| `system` / `error` | 弱化色、居中 | `Label` |

- 转录容器 = `ScrollView` + `ListLayout`（Phase 25 的布局正好是这个形状）。
- 流式指示：`running` 且最后一块未 `block_end` 时显示；用 `AnimationManager` 或不确定态 `ProgressBar`。

---

## 8. 回放（「假」的部分）

### 8.1 夹具是什么

**一份夹具 = 一段 core→gui 的消息流**（只有 core 侧，因为 GUI 侧的消息是对它的反应）。

### 8.2 两种回放模式

| 模式 | 行为 | 用来测 |
|---|---|---|
| `instant` | 一次性灌入全部行 | 稳态渲染 / 布局与滚动的极限 |
| `timed` | **按 `ts` 的真实间隔**逐条投递 | 流式刷新、重绘合并、帧时 |

`timed` 模式下，`ts` 的差值是**节奏的唯一真相源**——不要自己编延迟。

### 8.3 遇到 `ask` 时

`ask` 的正确回答**不可能存在于夹具里**（它是 GUI→core 的消息）。因此：

- `timed` 回放遇到 `ask` 时，把它**照常显示**并暂停在 `waiting`；
- 用户点选后，回放**继续**下一条 core 消息；
- 无人值守的自动跑批时用 `--auto-answer allow` 之类开关跳过等待。

★★ **回答的隐含边界（这条是 I3 能落成机检的前提）**：夹具里 core 的 `state` 由 `waiting` **回到 `running`**，
就代表它**已经收到了回答**。于是 I3 在夹具上不必检查不可见的 `answer`，只检查
「`ask` → 紧跟 `state=waiting` → 在 `state=running` 之前不得再发 `ask`」。

### 8.4 随附夹具

| 文件 | 用途 |
|---|---|
| `fixtures/demo-session.jsonl` | **正常路径**：完整一场会话（流式 / 工具成功 / 工具失败 / 权限门 / usage / 收尾），43 条消息，`seq` 连续无缺口 |
| `fixtures/hostile/seq-gap.jsonl` | **I4 的负向用例**：`seq` 缺口（3→5）+ 一个未知 `t`（`telemetry`）+ 一个指向不存在块的 `block_end` + 一条落在 `block_end` 之后的 `delta` |
| `fixtures/hostile/truncated-final-line.jsonl` | **§1.2 的负向用例**：最后一行被截断（半个 JSON） |
| `fixtures/scale/scale-transcript-2000.jsonl` | **规模基线**：2000 块 / 250 工具对 / 8906 条消息 / 916 KB，全部不变量成立。用于评估指标 ②（长转录滚动延迟与内存）；**建议 `instant` 回放**（`timed` 约 1.6 分钟） |
| `fixtures/scale/gen_scale.py` | **确定性生成器**：`--blocks N --seed S`，同参数必得**逐字节相同**输出 ⇒ 2000 / 5000 / 20000 是**旋钮**，不是砖 |

**否定型断言必须配正对照**：先用 `demo-session.jsonl` 证明路径通，再用 hostile 目录证明失败被**显式**暴露。

### 8.5 可执行规格

`protocol/validate.py` 是 §5 不变量与 §1 编码约定的**可执行形式**——协议文档说「必须怎样」，它负责「真的拦得住」：

```
python protocol/validate.py            # 退出码 = 未通过预期的夹具数
```

| 目录 | 期望 |
|---|---|
| `fixtures/hostile/**` | **必须触发至少一条**不变量 |
| 其余全部 | **必须满足全部**不变量 |

★★ **hostile 夹具「什么都没触发」= 失败**，不是通过。理由与 ECDI 里那条一致：
**一个恒成立的负向用例，与没有这个用例无法区分**。

---

## 9. 版本与兼容

- `protocol` 是**主版本**。core 的 `protocol` > GUI 支持的上限 ⇒ **GUI 拒绝连接并显示原因**，不做降级猜测。
- 同一主版本内：**只增不改不移除**字段；新增字段必须可被旧 GUI 忽略（I5）。
- 改任何已列出的字段语义 ⇒ 主版本 +1。

---

## 10. 这份协议服务于哪些评估

| 评估指标（对 ECDI） | 协议里对应的量具 |
|---|---|
| ① 流式 N token/s 下的重绘次数与帧时 | `delta` 的条数与 `ts` 间隔；GUI 侧统计每次 `delta` 引发的重绘 |
| ② 长转录的滚动延迟与内存 | `block_*` 的条数（夹具可放大到数千） |
| ③ `PostToUi` 突发下的顺序与丢失 | **`seq` 连续性（I4）** |
| ④ 跨 DPI 文本测量一致性 | 同一夹具在两个 DPI 下渲染，比对换行与滚动宽度 |
| ⑤ 做一个真实 GUI 花了多少行 / 多久 | 本协议的实现规模本身就是这个数字 |
