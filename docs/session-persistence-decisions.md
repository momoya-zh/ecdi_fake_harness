# 会话持久化 —— 定稿方案(2026-10-28)

> 补充并覆盖 [session-persistence-plan.md](session-persistence-plan.md) 的 §8 裁决点。
> 事实锚点、陷阱(§6、§9)仍以原文为准。
> **状态：L1 会话持久化与启动恢复、L2 上下文连续与 85% 压缩、L3 记忆文件系统已全量落地实施并验证通过 (2026-10-28)**。

## 1. 已拍板

| # | 决定 |
|---|---|
| 档位 | 三层递进:**L1 会话持久 → L2 上下文连续 → L3 记忆文件**。不做原方案 C(常驻 core,与 A7 冲突)。 |
| 存储 | **工作区下 `<工作区>/.ecdi/`**,不用 APPDATA(避免占 C 盘,项目搬家历史跟着走)。 |
| 原型 | `docs/prototype/session-restore-mockup.html` **仅作参考**,布局与现有 GUI 不一致;UI 按真实控件重做,只守 §5 四条交互要求。 |

## 2. 目录结构

```
<工作区>/.ecdi/
├─ .gitignore                  # 内容 "*",首次建目录时自动写,默认不进用户仓库
├─ sessions/<id>.jsonl         # L1 转录:core→gui 原始行,逐字节 append + 每行 flush
├─ sessions/<id>.meta.json     # L1 元数据:.tmp + rename 原子写
├─ sessions/<id>.jsonl.ui.json # GUI 决策 sidecar(answers / expanded),按会话分文件
├─ sessions/<id>.context.json  # L2 模型上下文(messages 数组),core 每轮结束原子写
└─ memory/                     # L3 记忆文件(Markdown,用户可编辑)
```

- 归属由**文件所在位置**决定,meta 里**不存** `workspacePath`(取代原 §9.1 的存路径方案;
  但 §9.1 的「先建全部工作区、再建会话」「按 `createdAt` 升序喂 `AcquireWorkspace`」仍适用)。
- meta 字段:`id`、`label`、`providerId`+`modelName`(二元组,§9.2)、`createdAt`、`lastUsedAt`、
  `lastSeq`、`archived`、`sizeBytes`、可选 `contextLimit`。**结构上不留密钥字段**。
- 删除会话 = 同时删 `.jsonl` / `.meta.json` / `.jsonl.ui.json` / `.context.json`(缺一留幽灵行)。

## 3. 启动发现

工作区注册表是纯内存的,启动时无从得知有哪些 `.ecdi/`。

- `AppConfig` 增加「已知工作区路径列表」(仅路径,几 KB,仍在 APPDATA,配置版本号 bump + 迁移测试)。
- 启动:读列表 → 逐个扫 `<ws>/.ecdi/sessions/*.meta.json` → 按 `createdAt`(同值比 id)排序 →
  先建工作区,再建会话 → 会话以 `originKind:"journal"` 走 `OpenHistorySession`。
- 路径不存在 / 不可写:侧栏标「不可用」,**不删任何记录**;写入失败在转录里给**可见提示**,不静默丢。
- 无工作区的会话:**默认不持久化**(备选:exe 同目录 `data/`)。临时会话不落盘,
  journal 在会话「转正」(首条 prompt 通过 idle 校验)后才开写。

## 4. 分层路线与验收

### L1 会话持久(能回看)—— 原方案 A
- 会话 id:GUI 生成 `s-<utc>-<4hex>`;core 的 `hello.session` 降级为运行实例 id,并在协议注释写清。
- journal 挂在两个 live 读点(`AppStream.cpp` 的 `++liveLines` 旁),**不挂 `HandleLine`**(§9.3)。
- label:首条 prompt 前 N 字,回落「工作区叶子 + 序号」。
- 恢复出的会话**只读**;「继续」= 新 live 会话,带上工作区 + 模型二元组。
- 模型名不在 `models[]` ⇒ 转录头插可见 notice,不静默回落 `models[0]`。
- 验收:`--persist-probe`(写→退出→重启→比对会话数/blocks/seq/label/工作区);
  负对照(删 journal 后报 0 会话);6 份夹具基线逐位不变;新 flag 登记 5 处 + `HasAnyProbe()`;
  探针必须隔离真实用户目录(参照 `AppConfig::SetPathForTesting`)。

### L2 上下文连续(能继续)—— 原方案 B,改良
- **上下文不从协议 journal 反推**:core 每轮结束原子写 `context.json`,`--resume <id>` 直接加载。
  转录给人看、上下文给模型吃,两份各自为权威,不会漂移。
- resume 是**只读重放**:不重发 `ask`,不重放工具调用。
- 先裁决 B1:`hello.seq` 从 `lastSeq+1` 续(改协议一句 + `validate.py` I4 + fake core)。
- 长会话超 `contextLimit` 的压缩/截断策略:**L2 必须单独立项**,不能默认静默截断。
- 验收:恢复后首轮 prompt 中历史条数与 context.json 一致;ask 不重发;工具不重放;
  fake core 支持 `--resume`,另备一份 resume 专用夹具。

### L3 记忆文件(跨会话)
- 与会话解耦,可与 L1/L2 并行。`<工作区>/.ecdi/memory/*.md`,另可有全局一份。
- 新会话启动时 core 读入并注入 system prompt。
- 第一版:**用户手写、模型只读**;模型自己写记忆(去重、污染、用户确认)后置。
- `memory/` 如需团队共享,由用户在 `.ecdi/.gitignore` 里单独放行。

## 5. 只读恢复的 UI 要求(布局待按真实 GUI 设计)
1. 侧栏区分「我的会话」与「夹具」。
2. 只读会话在列表中有可识别标记。
3. 输入框在用户打第一个字**之前**就表明「接不上」。
4. 「继续」把模型二元组与工作区一并带到新会话。

## 6. 仍开放(不挡 L1)
- 无工作区会话:不持久化 vs `data/` 目录。
- 大会话上限:不截断;侧栏显示体积,提醒策略待定。
- 「夹具历史」入口保留形式(第二入口 / `--fixtures`)。

## 7. 实施完成记录 (2026-10-28)

分步实施已全部完成并通过回归验证：

1. **Step 1 配置与存储基础**：
   - `AppConfig` 升级至 v5，增加 `knownWorkspaces`，实现新增/移除/去重与 v4→v5 自动迁移。`tools/appconfig-migrate-test.cpp` 覆盖测试 8a~8j 全 PASS。
   - `SessionStore`（`SessionStore.h` / `SessionStore.cpp`）实现目录安全保障、`s-<utc>-<4hex>` 生成、元数据原子写与加载、journal 追加写入、扫描与物理级连删除。`tools/session-store-test.cpp` 单元测试全 PASS。
2. **Step 2 Live 写入与转正落盘**：
   - 会话接入持久化字段 `meta`、`journal`、`journalPromoted`、`prePromotionLines`；core 的 `hello.session` 降级为运行实例 ID `coreInstanceId`，全局会话 ID 采用 `SessionStore::GenerateSessionId()`。
   - 会话生命周期转正：首条 Prompt 提交时调用 `PromoteSessionJournal()`，若存在首条 prompt 之前的预握手/空闲行，则完整回填至 journal 文件头；
   - 在 `AppStream.cpp` 的两个 live 读点（`++liveLines` 处）调用 `HandleLiveJournalLine()`，并在 `idle` 与 `MarkLiveEof()` 时原子写更新 `meta.json`。
3. **Step 3 启动发现与恢复**：
   - `DiscoverAndRestoreJournalSessions()` 遍历 `knownWorkspaces` 扫描 `.ecdi/sessions/*.meta.json`，按 `(createdAt, id)` 升序排序，先建工作区映射，再以 `originKind = "journal"` 与 `temporary = false` 幂等加载会话并抽干回放行至 timeline。
   - 启动时在 `win.Show()` 前完成发现与恢复，无参启动时自动清理未输入的空壳会话并激活最新恢复的会话。
4. **Step 4 UI 呈现与「继续」逻辑**：
   - 侧栏呈现：恢复的历史会话标签追加 ` [只读]` 标识，文本样式置灰（`kDarkMuted`）。
   - 只读防护：`PromptBar` 暴露 `SetReadOnlyMode(true)`，输入框设为只读且禁用，占位文案提示只读，严格拦截回车发送；隐藏发送/停止按钮，在原发送按钮位置呈现主样式的「继续」按钮；中栏顶栏增加「⎘ 继续」按钮。`SendPrompt` 入口硬拦截。
   - 「继续」逻辑：`ContinueCurrentSession()` 读取当前工作区与 `(providerId, modelName)` 二元组，新建 live 会话并聚焦输入框。
5. **Step 5 探针编写与全量回归**：
   - 新增 `--persist-probe`，在 `ProbePersist.cpp` 中覆盖 7 个阶段：
     - `discover-metas`：多工作区下元数据发现与元数据字段核对；
     - `restore-sessions`：会话反序列化、转录行数、seq、label、时间戳核对；
     - `readonly-ui`：侧栏 `[只读]` 标记与输入框只读/禁用/占位符核对；
     - `readonly-guard`：只读会话防误发拦截校验；
     - `continue-flow`：点击继续继承工作区与模型二元组，并切换至新 live 会话；
     - `negative-missing-file`：文件缺失残损容错与幂等重入去重校验；
     - `cleanup`：临时测试环境物理清理与断言。
   - 探针运行前强制校验 `--config` 隔离路径，防止污染用户真实 APPDATA。
   - `tools/regress.py` 全量回归实测：
     - 6 份夹具基线：100% OK（逐位不变）；
     - 13 个基准探针：100% OK；
     - 8 个专项/补齐探针（含 `--persist-probe`）：全部 PASS；
     - REQ-03、live workspace、validate.py 全部符合预期。


---

## 7. L2 补充裁决(2026-10-28):seq 与上下文压缩

L1 已完成。以下两条已拍板,L2 按此开工。

### 7.1 seq:resume 后从 `lastSeq+1` 续
- `hello.seq` 不再恒为 0:新会话为 0,resume 为 `lastSeq+1`。
- 同步改:`PROTOCOL.md` 一句话、`validate.py` I4 判据、fake core。
- 现有夹具不 resume,基线不受影响;需证明(`seqGaps` 读数不变)。

### 7.2 上下文占用约 85% 时摘要压缩

**存储改良**:`context.json` 改为**追加式** `<id>.context.jsonl`,每行一条记录:
`{"t":"msg",...}` 或 `{"t":"compact","summary":"...","replaces":[from,to]}`。
resume 时顺序重放得到当前 `messages`。好处:原始消息永不被覆盖,压缩只是追加一条记录,崩溃不丢数据。
转录 journal(`.jsonl`)不受影响,GUI 回看仍能看到完整历史。

**触发**
- 每次向模型发请求前检查:占用 = 上一轮 provider 返回的 `prompt_tokens`(没有则用估算)。
- `占用 ≥ 0.85 × contextLimit` → 先压缩再发。
- `contextLimit` 来自 `hello.context`。**没有就不压缩**(沿用「不得猜测容量」),
  只在 HUD 显示 `—`;不要用默认值 fallback。

**压缩做法**
1. 保留:system prompt + 最近的若干轮(预算约 25% 窗口)。
2. 其余旧消息交给**同一模型**生成摘要,替换成一条 `summary` 消息(角色 user,带固定前缀,标明「以下为此前对话摘要」)。
3. 切分点不得拆开 `tool_call` 与其 `tool` 结果,必须落在完整一轮的边界上。
4. 压缩后目标占用 ≤ 约 50%,避免刚压完下一轮又触发。
5. 已有摘要再次被压缩时,把旧摘要作为输入的一部分合并,不叠加多条摘要。
6. 摘要提示词要求保留:用户目标、已做出的决定、已改动的文件、未完成事项、关键约束;
   旧工具输出在送入摘要前按固定上限截断(只影响摘要输入,不影响 context.jsonl 里的原文)。

**必须可见**(本仓反对一切静默改写)
- 压缩发生时,core 发一条可见的 notice 行:`[上下文已压缩:N 条 → 摘要,占用 87% → 46%]`。
  该行由 core 发出,因此自然进入 journal,GUI 回看也能看到压缩发生在哪。
- **已核实(2026-10-28)**:协议没有 `notice` 类型(`t` 与 block `kind` 都没有,发了会触发 I5 / I8)。
  GUI 的 `AddNoticeRow` 只是内部时间线视图模型,不走协议。
- **裁决:复用 `block_start(kind:"system", text:"...")` + `block_end`**(PROTOCOL §3.3 / §7,弱化色居中),
  零协议改动、`validate.py` 不动、夹具基线不受影响。仍由 core 发出,**GUI 不得自己造这条行**。
- 机器侧不依赖这条 system 块识别压缩;权威记录是 `context.jsonl` 里的 `compact` 行。
- 注意:`messages` 数组只由 `context.jsonl` 重放得到,**不要**从转录里的 system 块反推,
  也不要把这类 system 块当作 system prompt 再喂回模型。
- 若将来需要专用 notice 语义(GUI 要对压缩事件做特殊渲染),再另走协议裁决,不挡 L2。

**失败处理**
- 摘要调用失败 / 超时:**不截断、不丢消息**。发 notice 说明失败,本轮照常发(若未超 100%),下一轮重试。
- 占用 ≥ 100% 且压缩仍失败:本轮报错并提示用户新开会话(带摘要继续属后续功能),不发送。
- 压缩冷却:同一会话连续两轮内不重复触发,防止摘要后仍高占用时死循环。

**验收**
- fake core 用极小的 `contextLimit` 触发压缩;判据:触发点在 85% 附近;
  压缩后 `messages` 条数与占用符合预期;`tool_call/tool` 配对完整;
  原始消息仍在 `context.jsonl`;notice 出现在 journal;resume 后重放结果与压缩后一致。
- 负对照:无 `hello.context` 时**不**触发压缩;摘要失败时消息条数不减。
- 阈值 `0.85`、保留预算 `25%`、目标 `50%` 做成 core 内的具名常量,不要散落魔数。

---

## 8. L2 实施完成记录 (2026-10-28)

L2 已全部落地并通过全量回归验证：

1. **协议与校验器同步**：
   - `protocol/PROTOCOL.md`：§3.1 更新握手规则——新会话 `seq` 从 0 起，resume 会话从 `lastSeq+1` 续起；§5 不变量 I4 明确会话生命周期内的单调连续要求；
   - `protocol/validate.py`：扩展支持 `--last-seq` 与识别 resume 夹具；现有 9 份夹具基线保持 100% 通过（exit=0）。
2. **存储与重放引擎（`core/internal/contextlog`）**：
   - 实现追加式 `<id>.context.jsonl`：每行一条记录，包含原始消息 `{"t":"msg", ...}` 与压缩替换 `{"t":"compact", "summary":"...", "replaces":[from, to]}`；
   - 实现顺序重放 `Replay` 与不破坏 `tool_call/tool_result` 配对的轮边界切分 `FindCompressionCutPoint`；
   - 包含单元测试覆盖重放、替换与文件完整性，全 PASS。
3. **Core 侧（fake-core 与 ecdi-core）落地**：
   - 命令行支持 `--resume <id>`、`--workspace <path>`、`--context <N>` 与测试注入 `--fail-summarize`；
   - resume 时从 `.meta.json` 读取 `lastSeq`，出站 `wire.Out` 续接为 `lastSeq+1`；
   - 85% 压缩状态机：具名常量 `CompressThresholdRatio = 0.85`、`RetainRatio = 0.25`、`TargetRatio = 0.50`；
   - 压缩发生时，core 发送可见 system 块 `[上下文已压缩:N 条 → 摘要,占用 X% → Y%]`；
   - 负对照：无 `context` 不触发压缩，`--fail-summarize` 发失败通知且保留全部消息不截断；
   - `tools/verify_core_l2.py` 自动化 5 项核心指标全 PASS。
4. **GUI 侧对接与继续操作升级**：
   - `SessionStore::ContextPath` 指向 `.context.jsonl`，级联删除四件套与向前兼容 `.context.json` 自动清理（32 个单元测试全 PASS）；
   - `ContinueCurrentSession()` 升级支持 live 模式下原位 `ResumeSession`，继承工作区与模型并无缝衔接 `lastSeq+1`，消除 I4 跳号误报；
   - 侧栏只读标记动态刷新，输入框解除只读锁定。
5. **全量回归验证（`tools/regress.py`）**：
   - 6 份夹具基线：100% OK（逐位不变）；
   - 13 个基准探针：100% OK；
   - 8 个补齐探针（含 `--persist-probe`、`--select-probe`）：全部 PASS；
   - REQ-03 异步投递：C1~C5 全部 OK；
   - live 设置探针（真 core）：16/16 green，全部 PASS；
   - 协议校验：exit=0。


---

## 8. L3 定稿(2026-10-28):记忆文件

L1、L2 已完成。L3 与会话解耦,可独立开工。

### 8.1 位置与读取
- 工作区记忆:`<工作区>/.ecdi/memory/*.md`。
- 全局记忆:exe 同目录 `data/memory/*.md`(不占 C 盘,与「数据跟着项目走」的取向一致)。
- 读取顺序:**全局在前、工作区在后**,后者可覆盖前者。每个目录只读 `.md`,按文件名排序,不递归。
- 两个目录都可以不存在;不存在即不注入,行为与现状**完全一致**。

### 8.2 注入
- 新会话启动时由 core 读入,拼进 system prompt 的固定区段(带分节标题,标明来源文件名)。
- 第一版:**用户手写、模型只读**。模型自己写记忆(去重、污染、用户确认)后置。
- **resume 时重新读取最新文件**(用户可能中途改过),并发一条 `kind:"system"` 提示「记忆已更新」。
  context 中的 system prompt 以本次读取为准,不沿用旧快照。
- 压缩(§7.2)只处理对话部分,**不动记忆区段**。

### 8.3 体积上限
- 记忆总字节(折算 token)上限 = `contextLimit` 的 10%,做成具名常量。
- 超限:按读取顺序累加,**超出的文件整份跳过**,发 `kind:"system"` 提示列出被跳过的文件名;
  **不静默截断文件内容**。
- 没有 `hello.context` 时不设上限,与 §7.2「不得猜测容量」同口径。

### 8.4 GUI 可见性
- 会话信息 / HUD 中列出本会话已加载的记忆文件(**仅文件名与字节数**,不展示内容)。
- 被跳过的文件同样列出并标注原因。
- 信息来源必须是 core 的实际加载结果,GUI 不得自己扫目录推断。

### 8.5 安全
- 记忆文件**原样注入**,不做密钥检测;文档与 GUI 提示都要写明「不要把密钥写进记忆文件」。
- `.ecdi/.gitignore` 默认为 `*`;若团队要共享 `memory/`,由用户自行放行。
- 记忆文件内容进入模型上下文,也会进入 `context.jsonl`;不要把其内容复制进 meta 或摘要。

### 8.6 验收
- 注入的记忆内容与磁盘文件逐字一致;顺序为全局→工作区。
- 超限时有可见提示,被跳过文件名正确,文件内容无任何截断。
- 无 `memory/` 目录时行为不变,6 份夹具基线逐位不变。
- resume 前改动记忆文件,恢复后读到新内容并出现「记忆已更新」提示。
- 压缩触发后,记忆区段仍完整保留。
- GUI 列出的加载文件与 core 实际加载一致(负对照:删除文件后,下一会话不再列出)。

---

## 9. L3 实施完成记录(2026-10-28)

L3 记忆文件系统已按照 §8 定稿标准全量实施并严格验证通过：

1. **Core 记忆引擎模块 (`core/internal/memory`)**：
   - 实现独立模块 `memory.go`，包含 `LoadMemoryFiles(globalDir, wsDir, contextLimit)`；
   - 规则：全局在前、工作区在后；工作区同名文件覆盖全局；大小写不敏感 `.md`，非递归；
   - 预算限制：`MemoryLimitRatio = 0.10`（10% 上限），`BytesPerToken = 4`；超限文件整份跳过，不静默截断；无 `contextLimit` 时不设上限；
   - 单元测试：`memory_test.go` 4 项测试 100% PASS。

2. **Core 链路对接与 System Prompt 注入 (`fake-core` & `ecdi-core`)**：
   - 协议：`hello.memories` 输出本会话加载的记忆文件清单（含 name、source、bytes、tokens、skipped、skipReason）；
   - 注入：在 `buildRequestMessages()` 的 System Prompt 区段以 `## 记忆文件: <name> (来源: <source>)` 逐份注入，不修改对话上下文；
   - 提示：
     - 超限跳过：Core 启动及运行期触发时发送系统 notice `[记忆文件超限已跳过: ...]`；
     - Resume 更新：当会话 resume 重启且存在记忆文件时，发送系统 notice `[记忆已更新]`；
     - 工作区切换：收到客户端 `workspace` 切换确认后，自动重新加载对应工作区的记忆并注入后续轮次；
   - 自动化验证：`tools/verify_core_l3.py` 4 项验证全 PASS。

3. **GUI 检查器记忆卡片与 HUD 呈现**：
   - 单一作者纪律：GUI 严格从 Core 的 `hello.memories` 提取数据，不自行扫描磁盘推测；
   - HUD 会话副标：存在记忆文件时动态追加 `· 记忆 N`（展示成功加载的文件数）；
   - 检查器右栏卡片：新增「记忆」卡片（`inspMemoryCard`），列出文件名与大小/KB；超限文件以黄色高亮标记并显示 `[超限跳过]`，默认无记忆时隐藏；
   - 切换与重建：在换会话、新建会话或重置会话时调用 `RebuildMemoryList()` 动态刷新卡片状态。

4. **全量回归验证**：
   - `python tools/regress.py`：
     - 6 份夹具基线：100% OK（逐字节/逐位等价）；
     - 13 个基准探针：100% OK；
     - 8 个补齐探针（含 `--persist-probe` 7/7 green 等）：100% OK；
     - REQ-03 异步投递：C1~C5 全部 OK；
     - live 探针与 live 设置探针（真 core）：全部 PASS；
     - 协议验证：`validate.py` 0 错误 PASS；
   - `python tools/verify_core_l3.py`：4/4 PASS；
   - `python tools/verify_core_l2.py`：5/5 PASS；
   - `core` 单元测试：`cmd/ecdi-core`、`internal/contextlog`、`internal/memory` 全部 PASS。
