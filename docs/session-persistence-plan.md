# 会话持久化 / 长期有效 —— 方案与裁决点（2026-10-27）

> **读者**：接手这件事的人。本文自包含，不需要读对话历史。
> **状态**：**设计稿，零代码改动**。事实锚点全部实地核对（HEAD `af271ea` + 工作区未提交改动）。
> **为什么现在只写不改**：并行会话正在改 6 个 UI 文件（§7 有冲突表），而本方案唯一躲不开的交集是
> `App.h` 的 `Session` 结构体。先定方案、后落代码，比两边同时改一个结构体便宜。

---

## 0. 结论（先说人话）

三句话：

1. 今天的「会话」是**进程内的一个 struct**。磁盘上唯一活下来的是 replay-history 会话的 `.ui.json`，
   而它只存「哪个 ask 答了什么 / 哪个块展开了」，**连转录都不存**。
2. 侧栏那个「历史」面板扫的是 `protocol/fixtures/` —— **测试夹具目录**。用户自己跑出来的会话
   从来没进过任何列表；他看到的历史是 `demo-session.jsonl`。
3. 「长期有效」有三档，代价差一个数量级：**能回看**（纯 GUI，零协议改动）→ **能继续**
   （core 落盘 + 协议裁决）→ **能后台跑**（换传输 + 改一条已验收契约）。

**建议**：先做第一档（它顺手修掉「用户的历史不存在」这个真缺陷），同时把第二档的两个裁决点
（§4）提出来定，第三档只在产品真需要「关窗后 agent 继续干活」时才碰。

---

## 1. 现状事实（每条可核）

| # | 事实 | 锚点 | 后果 |
|---|---|---|---|
| 1 | 会话全在内存：`std::vector<std::unique_ptr<Session>> m_sessions` | `App.h:1093` | 进程退出 = 全部消失 |
| 2 | live 会话与 core 子进程 1:1 绑定 | `AppSession.cpp:124-166`（`ChildSource`） | core 退/被杀 = 会话死 |
| 3 | core **不写任何磁盘**（非测试代码零 `os.WriteFile`） | `core/cmd/ecdi-core/*.go` | 模型上下文无处可恢复 |
| 4 | live 会话**没有任何持久化写入路径** | `SaveUiState` 仅在 `AppTranscript.cpp:411`、`:739` 对 `replay-history` 调用 | 用户真用的会话零落盘 |
| 5 | 唯一的磁盘状态是 `.ui.json`（answers / expanded） | `AppHistory.cpp:223-268` | 只存「决策」，不存转录 |
| 6 | 「历史」面板扫 `protocol/fixtures/**/*.jsonl` | `AppHistory.cpp:45-90` | 用户看到的是测试夹具 |
| 7 | 会话 id 是 core 里的**常量** | `core/session.go:155` = `s-live-1`；`core/cmd/ecdi-core/session.go:103` = `s-real-1` | 侧栏 chip 对每个会话都一样 ⇒ 身份不存在 |
| 8 | live 会话 label 恒为 `<core exe> (live)` | `AppSession.cpp:167` | 持久化后会得到一排同名会话 |
| 9 | 协议给 resume 留了字段但没实现 | `PROTOCOL.md:100`；`P3-real-core-spec.md` §1.2、`P1.5-child-process-spec.md` §7 明写「推后」 | 有钩子，无实现 |
| 10 | 协议强制 `hello.seq = 0`，且 `validate.py` 把它当 I4 硬判据 | `PROTOCOL.md:90`；`validate.py:139-141` | resume 后 seq 怎么续是**必须先裁决**的事（§4 B1） |
| 11 | 临时会话切走即删；归档只在内存 | `AppWorkspace.cpp:252`；`AppSession.cpp:789` | 持久化必须先定义这两类的语义（§6.4） |
| 12 | A7 验收要求「关窗后 core 退出、无僵尸」 | `P1.5-child-process-spec.md` §6 A7 | 方案 C 与一条**已验收**契约直接冲突 |

---

## 2. 一个「会话」由四样东西组成，缺一样就活不下来

| 要素 | 今天在哪 | 要持久化的是什么 |
|---|---|---|
| **内容**（转录） | 内存 `timeline` + 控件树 | core→gui 的**协议行字节流**（core 是唯一作者） |
| **身份** | 常量字符串（#7） | 稳定 id + 显示名 + 工作区归属 + 归档位 |
| **上下文** | core 进程内的 `messages` 数组 | 模型对话历史（只读重放 ≠ 重新执行） |
| **进程** | 子进程，关窗即死 | （仅方案 C）守护 + 重连 |

★ **关键区分：转录 ≠ 上下文。** 转录是「给人看的」，上下文是「给模型吃的」。
前者落盘就能**回看**，后者落盘才能**继续**。这句话就是方案 A 与 B 的分界线，也是本文最重要的一个划分。

---

## 3. 三个方案

### 方案 A：GUI 旁路日志（能回看，不能继续）

**做什么**

- GUI 侧生成会话 id（`s-<utc>-<4hex>`），live 会话创建时分配，进 `Session` 新字段。
- `%APPDATA%\ecdi-fake-harness\sessions\<id>.jsonl`：在 `HandleLine` 入口
  （`AppStream.cpp:445`）把 core→gui 原始行**逐字节** append + 每行 flush。
- `%APPDATA%\ecdi-fake-harness\sessions\<id>.meta.json`：label / workspaceId / workspacePath /
  providerId / modelName / createdAt / lastUsedAt / lastSeq / archived。
- 启动时扫目录 → 每个 meta 建一个 `originKind: "journal"` 会话，打开走**现成的**
  `OpenHistorySession` 路径（`AppHistory.cpp:289-365`：抽干 + sidecar + 跳末尾）。
- 「历史」面板改扫 `sessions/`；夹具另开一个入口（或 `--fixtures` 开关），见 §6.2。

**为什么这条路最便宜**：`OpenHistorySession` 已经把「从 jsonl 重建整个会话」走通了 ——
A 只是给它换一个数据来源，不新造恢复逻辑。**这是本仓已有能力，不是新设计。**

**改动面**：`AppHistory.cpp`（读写 + 扫描）、`AppSession.cpp`（分配 id / label）、
`AppStream.cpp`（`HandleLine` 一行写入）、`main.cpp`（启动恢复）、`App.h`（`Session` 加 2-3 字段）、
新增 `SessionStore.cpp/.h`（CMake 是 `GLOB_RECURSE`，新 .cpp 不用改 CMake）。

**能解决**：会话活过重启、可回看、可按工作区分组、可归档。

**不能解决**：**不能继续对话**。恢复出来的是只读历史，没有 core。「继续」= 新建会话（工作区可继承，
`NewSession` 已有继承口径，`AppSession.cpp:230-235`）。

**风险**

- 崩溃时最后一行可能是半行 → `ReplaySource` 已报截断并上屏（`pendingTruncated`），如实显示，**不修**。
- 大会话：scale 夹具 921 KB / 8906 条；真实长会话可能几十 MB。**不做截断**（截断 = 伪造历史），
  改为 meta 记 size，超阈值在侧栏显示体积。
- `--out` 摘要新增字段会打漂 6 份夹具基线 ⇒ 新字段必须 flag-gated（纪律见 `P4-gui-usability-plan.md` §0.3）。

### 方案 B：core 权威日志 + `--resume`（能继续）

**做什么**

- core 侧：`--resume <id>`，读自己的 `core.jsonl`（它 emit 过的每一行 + 收到的 gui 行），
  重建 `messages` 数组（user / assistant / tool），继续。
- GUI 侧：spawn 时带 `--resume`；`hello.resumed` 到达后显示本地 journal 的历史 + 接上新行。
- meta 里的 `providerId` / `modelName` / `workspacePath` 必须一起恢复 —— 否则「同一个会话」
  悄悄换了模型而用户不知道。

**前置裁决**：§4 B1（seq）、B2（唯一作者）、B3（身份）。

**★ 语义陷阱（最容易做错的一处）**：**resume 是「只读重放历史」，不是「重新执行」。**
重放时**不得**重新发 `ask`（否则用户被要求重复授权同一件事），也**不得**重放工具调用
（会真的再读一遍文件、再花一次 token）。

**不能解决**：GUI 关闭期间 core 还在跑（那是 C）。

### 方案 C：常驻 core（能后台跑）

- 传输从 stdin/stdout 换成命名管道；core 变守护进程；GUI 断开不杀。
- 需要：单实例锁、孤儿策略、断线期间消息的环形缓冲 + `resume_from_seq`。
- **与 A7 冲突**：`P1.5-child-process-spec.md` §6 明确验收「关窗后 core 退出（无僵尸）」。
  做 C 不是加功能，是**改一条已验收契约**，必须用户拍板。`--auto-close`、ChildSource 的
  kill-on-exit 全部要重新设计。

### 3.4 对比

| | A 旁路日志 | B core 日志 + resume | C 常驻 core |
|---|---|---|---|
| 能回看 | ✅ | ✅ | ✅ |
| 能继续对话 | ❌ | ✅ | ✅ |
| 关窗后还在跑 | ❌ | ❌ | ✅ |
| 协议改动 | **0** | hello + resume 语义（需裁决） | 传输层 + 重连协议 |
| core 改动 | **0** | 大（日志 + messages 重建） | 大（守护化） |
| 改已验收契约 | 否 | 否（新增入口） | **是**（A7） |
| 主要失败模式 | 半行截断（已可见） | 两份日志漂移 | 孤儿进程 / 断线丢消息 |
| 建议 | **先做** | 裁决后做 | 真需要才做 |

---

## 4. 三个必须先裁决的点（不裁决就会做出错的东西）

### B1. resume 之后 seq 怎么走

**事实**：`PROTOCOL.md:90` 要求「hello 的 `seq` 必须为 0」；`validate.py:139-141` 把它当 I4 硬判据；
`Transcript.cpp:20` 按 `lastSeq + 1` 判跳号（`seqGaps` 进评估摘要与基线）。

⇒ 恢复一个吃了 N 条历史的会话后，新 core 进程的第一条消息 seq=0，**必然**被判成一次跳号。

| 选项 | 做法 | 代价 | 后果 |
|---|---|---|---|
| ① **core 从 N+1 续** | core 读自己的日志，`hello.seq = lastSeq + 1` | 改协议一句 + 改 `validate.py` 判据 + fake core 同步 | I4 全程有效；现有夹具不受影响（夹具不 resume） |
| ② **GUI 开新 epoch** | `Transcript::BeginNewEpoch()`，`seqGaps` 语义改为「同一进程内」 | 零协议改动 | I4 在进程边界处失去分辨力 |

**建议 ①**：它保住不变量（seq 在整个会话生命周期内单调），代价是协议要写清「resume 时 hello.seq
不是 0」，并同步校验器。② 作为退路，但必须在转录里插一条**可见分隔行**
（`[已恢复 N 条；core 重连，序号从 0 重新开始]`）—— 静默换 epoch 是本仓最反对的形状。

### B2. 转录的唯一作者

协议与代码都写死「转录的唯一作者是 core」（`App.cpp:324` 注释）。
⇒ A 的 journal 是**副本**，不是第二作者。因此：

- journal **逐字节**只记 core 发的行，不得夹带 GUI 自己的事件（否则 `validate.py` 与回放基线会被污染）。
- GUI 侧决策（answer / expanded）继续走 sidecar，但 sidecar 的**定位键必须从 `originPath` 换成
  `sessionId`** —— 现在对 live 会话它会算成 core exe 路径（§6.1）。

### B3. 会话身份

`hello.session` 是常量（#7），所以持久身份只能由 GUI 生成。两条路：

- **GUI 生成**（零 core 改动）：GUI id = 持久身份；core 的 `session` 字段降级为「本进程的运行实例 id」。
  两者关系必须写进协议注释，否则下一个人会以为是同一个东西。
- **core 接受 `--session-id`**（更干净）：GUI 指定，core 回显。要改 core 参数表 + fake core。

---

## 5. 推荐路径与每阶段的机读判据

**S0（本文）**：方案定稿 + 裁决点提交。零代码。

**S1 = 方案 A**

- 新探针 `--persist-probe`：写 → 退出 → 重启 → 比对（会话数 / 每会话 blocks·deltas·首末 seq /
  label / 工作区归属）。
- **负对照**：删掉 journal 后必须报「0 个会话」，不是「打开一个空会话」。
- 回归：6 份夹具基线逐位不变（journal 只在 live 路径写，回放不写 ⇒ 天然不变，但要证明）。
- ★ 新 flag 要登记 **5 处**（`CliOptions.h` / `ProbeState.h` / 派发表 / `App.h` 声明 / `Probe*.cpp`），
  外加 `HasAnyProbe()` 那份手维护清单（`CliOptions.h:281`）—— 漏了不会报错，只会静默在 USER 模式下跑。

**S2 = 方案 B**

- 前置：B1 / B3 裁决 + fake core 支持 `--resume`（否则测不了）+ 一份 resume 专用夹具
  （独立于现有 6 份基线，别动它们）。
- 判据：恢复后首轮 prompt 的模型上下文里，历史消息条数与 journal 对得上；ask 不重发；
  工具不重放。

**S3 = 方案 C**：前置是 A7 契约变更拍板。

---

## 6. 已知陷阱（每条都是真事，不是假想）

**6.1 sidecar 路径对 live 会话算成 core exe 路径。**
`UiStatePath` = `originPath + ".ui.json"`（`AppHistory.cpp:158-168`），而 live 会话的
`originPath = m_options.corePath`（`AppSession.cpp:171`）⇒ 一旦有人把 `SaveUiState` 扩到 live 会话，
**所有 live 会话共写 `ecdi-core.exe.ui.json`**，就在二进制旁边。今天没爆**只因为**
`SaveUiState` 只在 `replay-history` 分支被调用（`AppTranscript.cpp:411` / `:739`）。
这是 A 必须顺手拆掉的引信。

**6.2 「历史」面板扫夹具目录。** 见 #6。A 落地时必须把「夹具」与「我的会话」分开，
否则持久化做完，用户看到的还是 `demo-session.jsonl`。

**6.3 live 会话 label 同名。** 见 #8。需要 label 规则（首条 prompt 前 N 字 / 工作区叶子 + 序号）。

**6.4 临时会话与归档的语义。** 临时会话切走即删（`AppWorkspace.cpp:252`）⇒ 它**不该**落盘
（否则每次点「＋」都留一个空文件）。归档只在内存（`AppSession.cpp:789`）⇒ 要进 meta。

**6.5 密钥绝不落盘。** `ECDI_CORE_KEY` / provider key 走启动环境层（`AppSession.cpp:127-157`），
**绝不进** journal / meta / 摘要（`positioning.md` §4）。

**6.6 大文件与截断。** 见 §3 A 风险。截断 = 伪造历史，只能提示，不能悄悄删。

**6.7 seqGaps 基线。** 任何 epoch 改动都会让 hostile 夹具的 `seqGaps` 读数变化 ⇒
必须证明只影响 resume 路径。

**6.8 探针登记清单。** 见 §5 S1 的 ★。

---

## 7. 与并行会话的协作边界

`git diff --numstat`（2026-10-27 18:26）显示并行会话正在改：

| 文件 | 改动量 | 本方案能不能动 |
|---|---|---|
| `AppUiMain.cpp` | +21/−1 | ❌ 不能 |
| `AppHud.cpp` | +55/−11 | ❌ 不能 |
| `AppTranscript.cpp` | +25/−15 | ❌ 不能 |
| `App.cpp` | +32/−1 | ❌ 不能 |
| `AppInternal.h` | +7/−0 | ❌ 不能 |
| `App.h` | +25/−0 | ❌ **唯一躲不开的交集** |

- `App.h` 里给 `Session` 加字段是**唯一**不可避免的冲突面 ⇒ 建议**等并行会话落地后**再加，
  且只加在 struct 尾部一个块里（减少 merge 面）。
- 其余落点全部在**没人在动**的文件：`AppSession.cpp`、`AppHistory.cpp`、`AppStream.cpp`、
  `main.cpp`、新增 `SessionStore.cpp/.h`。

---

## 8. 未决问题（要拍板）

1. **「长期有效」要哪一档？** 回看（A）/ 继续（B）/ 后台跑（C）。
2. **存储位置**：`%APPDATA%\ecdi-fake-harness\sessions\`，还是跟工作区走
   （每个工作区一个 `.ecdi/sessions/`）？后者「项目搬走带着历史走」，与 sidecar 的现有取舍同源
   （`AppHistory.cpp:159` 注释），但会把历史写进用户的代码仓。
3. **「夹具历史」面板保留吗？** 保留 = 两个入口；去掉 = 评估流程要另找入口。
4. **大会话上限策略**：不截断的前提下，谁来提醒、提醒几次。
