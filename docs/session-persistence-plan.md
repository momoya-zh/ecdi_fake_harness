# 会话持久化 / 长期有效 —— 方案与裁决点（2026-10-27）

> **读者**：接手这件事的人。本文自包含，不需要读对话历史。
> **状态**：**已按 [session-persistence-decisions.md](session-persistence-decisions.md) 定稿并完成落地实施 (2026-10-28)**。
> 原文分析及事实锚点保留作为历史设计考量与问题排查参考。
> **实施落地成果**：
> - L1 会话持久化（`SessionStore`、`<工作区>/.ecdi/sessions/`、journal 读点落盘、原子写 meta）已实现；
> - 启动自动发现恢复（按 `createdAt` 排序、先建工作区映射、反序列化会话、只读回放）已实现；
> - L2 只读保护与继续开会话（`[只读]` 标记置灰、输入框禁用/拦截、双重「继续」按钮继承工作区与模型）已实现；
> - `--persist-probe` 7 相探针覆盖完整生命周期并通过回归测试。

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
| 1 | 会话全在内存：`std::vector<std::unique_ptr<Session>> m_sessions` | `App.h:1146` | 进程退出 = 全部消失 |
| 2 | live 会话与 core 子进程 1:1 绑定 | `AppSession.cpp:125-180`（`ChildSource`） | core 退/被杀 = 会话死 |
| 3 | core **不写任何磁盘**（非测试代码零 `os.WriteFile`） | `core/cmd/ecdi-core/*.go` | 模型上下文无处可恢复 |
| 4 | live 会话**没有任何持久化写入路径** | `SaveUiState` 仅在 `AppTranscript.cpp:451`、`:870` 对 `replay-history` 调用 | 用户真用的会话零落盘 |
| 5 | 唯一的磁盘状态是 `.ui.json`（answers / expanded） | `AppHistory.cpp:223-268` | 只存「决策」，不存转录 |
| 6 | 「历史」面板扫 `protocol/fixtures/**/*.jsonl` | `AppHistory.cpp:45-90` | 用户看到的是测试夹具 |
| 7 | 会话 id 是 core 里的**常量** | `core/session.go:156` = `s-live-1`；`core/cmd/ecdi-core/session.go:104` = `s-real-1` | 侧栏 chip 对每个会话都一样 ⇒ 身份不存在 |
| 8 | live 会话 label 恒为 `<core exe> (live)` | `AppSession.cpp:176` | 持久化后会得到一排同名会话 |
| 9 | 协议给 resume 留了字段但没实现 | `PROTOCOL.md:100`；`P3-real-core-spec.md` §1.2、`P1.5-child-process-spec.md` §7 明写「推后」 | 有钩子，无实现 |
| 10 | 协议强制 `hello.seq = 0`，且 `validate.py` 把它当 I4 硬判据 | `PROTOCOL.md:90`；`validate.py:139-141` | resume 后 seq 怎么续是**必须先裁决**的事（§4 B1） |
| 11 | 临时会话切走即删；归档只在内存 | `AppWorkspace.cpp:267`（删除守卫）；`AppSession.cpp:828` | 持久化必须先定义这两类的语义（§6.4） |
| 12 | A7 验收要求「关窗后 core 退出、无僵尸」 | `P1.5-child-process-spec.md` §6 A7 | 方案 C 与一条**已验收**契约直接冲突 |
| 13 | **工作区注册表也是纯内存的**，id 由进程级计数器产生 | `App.h:1157`（`m_workspaces`）、`:1158`（`m_nextWorkspaceId = 1` 是**每次启动的初值**）、`AppWorkspace.cpp:209` | 「按工作区分组」缺数据来源（§9.1） |
| 14 | `HandleLine` 有 **5 个调用点，其中 3 个是回放路径** | `AppStream.cpp:125` / `:249` / `:415` / `:435`、`AppHistory.cpp:322` | journal 的写入落点取决于它（§9.3） |
| 15 | 配置 v4：提供商 1—N 模型，`activeModel` 是**全局**状态；回落规则「找不到 ⇒ `models[0]`」 | `AppConfig.h:40-47`、`AppConfig.cpp:75` | 只存 `providerId` 的恢复会**静默换模型**（§9.2） |

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
- `%APPDATA%\ecdi-fake-harness\sessions\<id>.jsonl`：在**两个 live 读点**（`AppStream.cpp:124-125`、
  `:247-249`，紧挨现成的 `++m_sess->liveLines`）把 core→gui 原始行**逐字节** append + 每行 flush。
  ★ 不要挂在 `HandleLine` 入口（`AppStream.cpp:445`）——它有 3 个回放调用点，理由见 §9.3。
- `%APPDATA%\ecdi-fake-harness\sessions\<id>.meta.json`：label / **workspacePath**（不是 workspaceId，§9.1）/
  **providerId + modelName 二元组**（§9.2）/ createdAt / lastUsedAt / lastSeq / archived /
  可选 `contextLimit`（来自 `hello.context` 的事实记录）。**meta 必须走「写 `.tmp` + rename」**，
  不能照抄 `AppConfig::Save` 的 `trunc` 直写（§9.4）。
- 启动时扫目录 → 每个 meta 建一个 `originKind: "journal"` 会话，打开走**现成的**
  `OpenHistorySession` 路径（`AppHistory.cpp:289-365`：抽干 + sidecar + 跳末尾）。
- 「历史」面板改扫 `sessions/`；夹具另开一个入口（或 `--fixtures` 开关），见 §6.2。

**为什么这条路最便宜**：`OpenHistorySession` 已经把「从 jsonl 重建整个会话」走通了 ——
A 只是给它换一个数据来源，不新造恢复逻辑。**这是本仓已有能力，不是新设计。**

**改动面**：`AppHistory.cpp`（读写 + 扫描）、`AppSession.cpp`（分配 id / label）、
`AppStream.cpp`（两个 live 读点各加一行 journal 写入）、`main.cpp`（启动恢复）、
`AppWorkspace.cpp`（启动时按 `createdAt` 喂 `AcquireWorkspace`，§9.1）、`App.h`（`Session` 加 2-3 字段）、
新增 `SessionStore.cpp/.h`（CMake 是 `GLOB_RECURSE`，新 .cpp 不用改 CMake）。

**能解决**：会话活过重启、可回看、可按工作区分组、可归档。

**不能解决**：**不能继续对话**。恢复出来的是只读历史，没有 core。「继续」= 新建会话（工作区可继承，
`NewSession` 已有继承口径，`AppSession.cpp:239-244`）。
★ 但那个口径**只继承工作区，不继承模型** —— 新建的 live 会话吃的是 `DefaultProvider()` 的 `activeModel`
（`AppSession.cpp:182-183`）。所以从「只读恢复会话」点「继续」时，必须把 meta 里的
`providerId + modelName` 一起带上，否则用户以为在接着聊，实际换了个脑子（§9.2）。

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
`originPath = m_options.corePath`（`AppSession.cpp:180`）⇒ 一旦有人把 `SaveUiState` 扩到 live 会话，
**所有 live 会话共写 `ecdi-core.exe.ui.json`**，就在二进制旁边。今天没爆**只因为**
`SaveUiState` 只在 `replay-history` 分支被调用（`AppTranscript.cpp:451` / `:870`）。
这是 A 必须顺手拆掉的引信。
★ 复核补充：A 落地后这根引信**对恢复出来的会话是自动拆掉的** —— `OpenHistorySession` 建的会话
`originPath` 就是 journal 路径，sidecar 天然落在 journal 旁边、按会话分文件；
剩下的 live 那一半，把 `originPath` 设成 journal 路径即可一并解决（§9.3 末）。

**6.2 「历史」面板扫夹具目录。** 见 #6。A 落地时必须把「夹具」与「我的会话」分开，
否则持久化做完，用户看到的还是 `demo-session.jsonl`。

**6.3 live 会话 label 同名。** 见 #8。需要 label 规则（首条 prompt 前 N 字 / 工作区叶子 + 序号）。

**6.4 临时会话与归档的语义。** 临时会话切走即删（`AppWorkspace.cpp:267` 的 `temporary` 守卫）⇒ 它**不该**落盘
（否则每次点「＋」都留一个空文件）。归档只在内存（`AppSession.cpp:828`）⇒ 要进 meta。
★ 复核补充：转正时机也不能漏 —— `temporary` 是在 `PostPrompt` 里、**过了 idle 校验之后**才清掉的
（`AppWorkspace.cpp:78-79`），不是建会话时。所以 journal 的开写点要么等转正、要么容忍「删会话 = 删文件」，
但**不能**在 `CreateSessionFromOptions` 里就建文件，否则每次点「＋」都会留一个孤儿 journal。
删除路径必须连 `<id>.jsonl` + `<id>.meta.json` + `<id>.jsonl.ui.json` **三个一起删**（少删一个 = 侧栏出现打不开的幽灵行）。

**6.5 密钥绝不落盘。** `ECDI_CORE_KEY` / provider key 走启动环境层（`AppSession.cpp:127-157`），
**绝不进** journal / meta / 摘要（`positioning.md` §4）。

**6.6 大文件与截断。** 见 §3 A 风险。截断 = 伪造历史，只能提示，不能悄悄删。

**6.7 seqGaps 基线。** 任何 epoch 改动都会让 hostile 夹具的 `seqGaps` 读数变化 ⇒
必须证明只影响 resume 路径。

**6.8 探针登记清单。** 见 §5 S1 的 ★。

---

## 7. 与并行会话的协作边界（**整节已作废**，2026-10-28）

> **作废原因**：占坑的两个提交都已合入 —— `d57aa5d`（版心模型）、`2149170`（一密钥多模型 / 配置 v4）。
> `git status` 干净，当年唯一躲不开的交集 `App.h` 现在自由。
> **但 `2149170` 不是无害地落地的**：它把模型改成「提供商 1—N 子项 + 全局 `activeModel`」，
> 直接改写了本文 §3 B 那句「`providerId` / `modelName` 必须一起恢复」的性质 —— 见 §9.2。
> 下面这张表留作「为什么当时只写不改」的记录，**不要再据它排期**。

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

★ **哪几条真的挡住 S1**（2026-10-28 复核注）：只有 **2** 和 **5**。
1（档位）可以先按「A」开工，B/C 是后面加不改存储格式的事（§9.2/§9.4 把接口预留好了）；
3（夹具入口）是 UI 组织问题，做 S1 时顺手分栏；4（大会话上限）不改变任何判据，谁提醒都可以。

1. **「长期有效」要哪一档？** 回看（A）/ 继续（B）/ 后台跑（C）。
2. **存储位置**：`%APPDATA%\ecdi-fake-harness\sessions\`，还是跟工作区走
   （每个工作区一个 `.ecdi/sessions/`）？后者「项目搬走带着历史走」，与 sidecar 的现有取舍同源
   （`AppHistory.cpp:159` 注释），但会把历史写进用户的代码仓。
   ★ 复核倾向 **APPDATA**：`AppConfig` 已经占了这个位置（`AppConfig.cpp:31`），
   两处存储分家会让「备份/清空我的数据」变成两件要做的事；而「跟工作区走」在 §9.1 的
   口径下并不丢东西 —— meta 存的是路径，工作区搬走时用户搬的是目录本身，历史仍然按路径指得回去。
3. **「夹具历史」面板保留吗？** 保留 = 两个入口；去掉 = 评估流程要另找入口。
4. **大会话上限策略**：不截断的前提下，谁来提醒、提醒几次。
5. ★ **恢复出来的会话是只读的，这个「只能看」怎么表达？** S1 唯一的 UI 设计量在这里，
   已出原型待验收（§9.5）。判据：用户点进去、试着打字，必须在**打第一下之前**就知道接不上，
   而不是敲完一段话按回车才发现没反应。

---

## 9. 2026-10-28 复核：本文原先漏掉的三处缺口（外加一处可白捡的先例）

复核动机：§7 的约束消失后动手不再受限，所以先把「照原来那张图做会做错」的几处补齐。
9.1 / 9.2 / 9.3 每一条都会让功能**看起来跑通了、实际是错的**，不是风格问题。

### 9.1 ★ 工作区注册表也不落盘 ⇒「按工作区分组」做出来是空的

**事实**（#13）：`m_workspaces` 是纯内存的 `vector<unique_ptr<Workspace>>`（`App.h:1157`），
id 由**进程级**计数器产生 —— `ws->id = "w" + std::to_string(m_nextWorkspaceId++)`
（`AppWorkspace.cpp:209`），而 `m_nextWorkspaceId = 1` 是成员**初值**（`App.h:1158`），
每次启动都从 1 重新数。全仓没有任何工作区落盘路径（`grep` 于 `gui/src/` 只有 `AppConfig` 写盘）。

**后果**：本文 §3 A 让 meta 存 `workspaceId`，重启后恢复出来的会话**全部指向不存在的实体**。
侧栏「按工作区分组」静默退化成「一堆没有归属的会话」，而且 `WorkspaceById()` 返回 nullptr
是合法路径（`App.h:567-573` 明写空 `workspaceId` = replay/new 类），所以连判空都发现不了它。

**修法（存路径，不存 id）**：meta 存 `workspacePath`；启动时把各 meta 的路径**按 `createdAt` 升序**
逐个喂给现成的 `AcquireWorkspace()`（`AppWorkspace.cpp:200`），id 由本次进程重新派生。
三条不变量正好都支持这条路：② 按路径去重 ⇒ 同目录的多个历史会话天然共享一个实体；
③ 只由 core 确认过的路径创建 ⇒ 存进 meta 的本来就是 ack 过的规范化路径，喂回去不引入「GUI 猜的路径」。

★ **前提是喂入顺序确定**。「下标即创建序」是探针判据的地基（`App.cpp:796`、`AppWorkspace.cpp:167-169`），
所以排序键必须钉死 `createdAt`、同值再比 `sessionId`，**不能**靠目录枚举序 ——
`FindHistoryFiles` 当年专门加了一次显式排序（`AppHistory.cpp:85-88`）就是为这个。

★ 另一条不能忘：`AcquireWorkspace` 是**追加**语义，而恢复时新会话也会建工作区。
所以启动顺序是「先按 `createdAt` 建全部工作区，再建会话」，反过来会让同一个目录拿到两个 id
（第一个由恢复建、第二个由新建会话建），去重就废了。

### 9.2 ★ v4 之后，模型身份必须存 (providerId, modelName) 二元组

本文 §3 B 那句「`providerId` / `modelName` 必须一起恢复」在 `2149170` 之后**变了性质**：
配置 v4 把模型降成提供商的子项（`ProviderConfig::models[]` + `activeModel`，`AppConfig.h:40-47`），
一家之下可以有 N 个模型，而 `activeModel` 是**全局**配置状态。

于是「只存 providerId、恢复时取 `ActiveModelName()`」这条最省事的写法**必然**换模型 ——
用户在 v4 之后最常做的动作恰恰是「在同一家里切模型」，他一切，全局 `activeModel` 就变了，
所有「按 providerId 恢复」的旧会话下次打开集体换脑子。更糟的是回落规则
「`activeModel` 找不到 ⇒ `models[0]`」（`AppConfig.cpp:75-81`）会把这件事做得**很安静**：
既不报错也不上屏。

**修法**：
- meta 存 `providerId` + `modelName` 两个字段（`Session` 里本来就有：`App.h:613` / `:616`）；
- 恢复时**校验**这个名字还在不在那家的 `models[]` 里；不在 ⇒ 转录头部插一条可见 notice
  （`[原模型 <name> 已不在提供商 <id> 的模型表中；本会话按只读显示]`），
  **不得**静默回落 `models[0]`；
- chip 显示的是 **meta 里那个名字**，不是 `ActiveModelName()` —— 只读会话的「它当时用什么模型」
  是个历史事实，跟现在的激活项无关；
- ★ 「继续」（从只读会话开新 live 会话）也要带上这对二元组，见 §3 A「不能解决」那段：
  现成的继承口径只继承工作区，模型走的是 `DefaultProvider()` 的 `activeModel`（`AppSession.cpp:182-183`）；
- 顺带存 `contextLimit`（`App.h:611`，来源只有 `hello.context`）：这是**记录事实**，
  与「不得猜测容量、不得按 0 当分母」（`App.h:606-611`）不冲突；meta 里没有 ⇒ 照旧显示 `—`。

### 9.3 ★ journal 的落点：不是 `HandleLine`，是两个 live 读点

本文 §3 A 原文把写入挂在 `HandleLine` 入口（`AppStream.cpp:445`）。复核发现它有 **5 个调用点**（#14），
其中 **3 个是回放路径**：`DrainInstant`（`AppStream.cpp:415`）、`DrainTimed`（`:435`）、
以及 `OpenHistorySession` 自己的抽干循环（`AppHistory.cpp:322`）。

**后果**：挂 `HandleLine` 就必须加 `originKind == "live"` 守卫。守卫是**纪律**不是**结构** ——
漏一次（而新增第 6 个调用点在 0d/Step 5 之后是会发生的）就变成
**「用户每点开一个历史会话，就把那份夹具重新记成一份自己的新 journal」**。
届时 §5 S1 承诺的「6 份夹具基线逐位不变」不再天然成立，而且这条回归**测不出来**：
基线跑的是 `--replay`，探针不会去点历史面板。

**修法**：挂在 live 独有的两行旁边 —— `AppStream.cpp:124-125` 与 `:247-249`，两处都是
`++m_sess->liveLines;` 紧接 `HandleLine(line);`。`liveLines` 这个计数器本身就是
「这是一行 core→gui 原始行」的现成判据（全仓只有这两处自增，读点 `AppHud.cpp:992`/`:1184`、
`AppSummary.cpp:71`），所以**回放路径结构上不可能经过，无需守卫**，基线不变才是结构性的。

★ 附带确认过一件好事：GUI 自己的事件（`AddNoticeRow`、pending prompt 视图）不进 `HandleLine`，
所以 §4 B2「journal 逐字节只记 core 发的行、不夹带 GUI 事件」这条，在这个落点上天然成立。

### 9.4 一处可以白捡的先例：`AppConfig` 已经是完整的落盘模板

不用新造持久化纪律，`AppConfig` 那套直接照抄五件：
`DefaultDir()` 的 APPDATA + cwd 回落（`AppConfig.cpp:31`）、`SetPathForTesting`
（探针隔离用户真实配置，`:48`）、版本号单一真相 + Load 时迁移（`kCurrentVersion`，`AppConfig.h:65`，
教训见 `:62-64`「版本号散两处必漏一处」）、**结构上无密钥字段**（只存凭据目标名引用，`AppConfig.h:4-6`）、
以及迁移测试 `tools/appconfig-migrate-test.cpp` 这个形态。
密钥这条正好是 §6.5 的结构化实现 —— 别在 `SessionMeta` 里给 key 留字段，让它**放不下**而不是「不该写」。

★ 唯一**别**照抄的一处：`AppConfig::Save` 是 `ofstream(binary|trunc)` 直写（`AppConfig.cpp:358`）。
配置写坏了用户可以重填，**meta 写坏了那个会话就永久回不来**。所以 meta 走「写 `.tmp` + `rename` 覆盖」，
journal 走 append + 每行 flush。两个文件的写入纪律本来就不同，别为了「复用 Save」把 meta 也做成 trunc 直写。

### 9.5 只读恢复的交互（S1 唯一的 UI 设计量）

原型：`docs/prototype/session-restore-mockup.html`（2026-10-28，待验收）。
要覆盖四个观感点：侧栏「我的会话 / 夹具」两组怎么分、恢复出来的会话在列表里长什么样、
点进去后输入框如何表达「接不上」、以及「继续」按钮把 §9.2 的模型二元组带过去。
