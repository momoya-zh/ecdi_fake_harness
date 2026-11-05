# run_command 后台长任务（③）方案

日期：2026-11-05。前置：L1 沙盒（`ed85206`，`docs/l1-sandbox-plan.md`）。
状态：**已拍板 v6 口径**（用户 2026-11-05 选「v6 + caps.jobs」），本文件随实现更新读数。

## 要解决的问题

`run_command` 现在**阻塞一轮对话**：默认 60 秒、上限 300 秒，长构建 / 测试 / dev server
要么超时被杀，要么把整轮卡死；模型没有 job_id、没有输出回读、没有取消入口。
L1 沙盒已经能整树清退进程，缺的是「把作业的生命周期从一次工具调用里拿出来」。

## 范围（最小闭环）

1. `run_command` 增 `background`（bool，缺省 false）。false 时行为逐位不变。
2. 后台启动后**立即返回** `job_id` + 起始状态，模型可以继续下一步。
3. 新工具：
   - `job_output(job_id, wait_seconds?)`：取增量输出（每作业一个读游标），可等 0–30s；
   - `job_kill(job_id)`：整树终止 + 返回尾部输出；
   - `job_list()`：列出本会话作业（id/状态/时长/输出量/命令首行）。
4. 作业随**会话**存活：单轮取消不杀后台作业；会话结束（`bye`/EOF/core 退出）全杀。
5. 后台命令仍过 ask 门；提案卡 `detail` 增 `background:true`。

**不做**：PTY / 交互式 stdin；跨进程重启恢复；作业优先级/配额调度；GBK 控制台解码
（既有问题，另批）。

## 协议：v6（新增 `t:"job"`）

新增 `t` 类型 ⇒ 主版本 6（§9 规则）。`hello.caps` 增 `"jobs"`；GUI 上限
`kProtocolMaxSupported` 同步 6。

```json
{"t":"job","seq":12,"ts":"...","id":"j1","phase":"start","command":"npm run dev","cwd":"D:/ws","call":"b7"}
{"t":"job","seq":31,"ts":"...","id":"j1","phase":"output","text":"ready in 312ms\n","bytes":312,"dropped":0}
{"t":"job","seq":45,"ts":"...","id":"j1","phase":"end","status":"exited","exitCode":0,"durationMs":8123}
```

- `id`：`j1` 起，一份流内唯一；**首条**必须是 `phase:"start"`。
- `start`：`command`（完整不截断）、`cwd`、`call`（发起它的 `tool_call` 块 id，必须已存在）。
- `output`：`text` 为**新增**文本（非累计）；`bytes` = 累计产出字节；`dropped` = 因环形缓冲
  溢出而丢弃的累计字节（GUI 显式提示，不静默）。
- `end`：`status` ∈ `exited|failed|killed|timeout`；`exitCode`（killed 时为 -1）；
  `durationMs`。
- **I14**：`job.id` 唯一；`start` 先于该 id 的任何 `output`/`end`；`end` 恰好一次；
  `end` 之后不得再有该 id 的任何消息；`phase` 必须是三个已知值；`start` 的 `call`
  必须指向已 `block_start` 的 `tool_call`。
- **I3 不开口子**：ask 未答期间不得发任何消息（`usage` 除外）⇒ core 在 ask 挂起时
  **缓冲** job 事件，答完再放。

## core 实现

- `jobHub`（新文件 `jobs.go`）：`map[string]*job` + mutex；并发上限 4；单作业墙钟上限 30 分钟。
- 作业执行复用 L1：`sandbox` 暴露「启动并保留作业句柄」的原语；Windows 保留
  `Job Object` 句柄（`KILL_ON_JOB_CLOSE` 兜底），非 Windows 保留进程组 pid。
- 输出：`io.Writer` 汇入 256KB 环形尾窗 + 累计游标；每 200ms 或 ≥8KB 合并发一条
  `output`（避免刷屏）；读游标落后于窗口时回 `dropped`。
- 生命周期：`start` 在工具返回前发；`end` 在 wait goroutine 里发；`shutdown` 在
  会话退出时全杀（不再发事件）。
- 工具层：`run_command` 分支 `background`；`job_output`/`job_kill`/`job_list` 在
  `executeTool` 分发；三者**不再过 ask**（作业是用户已批准命令的延续，`job_kill`
  只作用于本会话自己的作业）。

## GUI（v6）实现

- `--job-probe` 正向夹具 + 负对照；job 行渲染规格进 §7：
  标题 `⏵ 后台 j1 · <command 首行> · 运行中/完成 0/失败/已终止/超时`，正文为尾部
  输出（mono，最多 200 行，超出提示 `… 前 N 行已折叠`）；`dropped>0` 时标题带
  `· 丢弃 N 字节`。
- 重建（`--rebuild-probe`）必须从 journal 的 `job` 消息重建同样的行。

## 验收

- core 单测：并发上限、环形窗口/游标、kill 整树、会话退出全杀、30 分钟上限（注入时钟）、
  ask 挂起期间零 job 消息（I3）。
- E2E：脚本 core 起后台 → `job_output` 两读 → `job_kill` → `end` 顺序 + 退出码。
- `validate.py`：新夹具（正/负各一）过 I14；旧夹具在 v6 下照旧。
- `protocol/PROTOCOL.md`：§3.13 / §5 I14 / §7 渲染 / §9 版本表 / §10 变更。
- GUI：`--job-probe` 正/负；`regress.py` 全量 PASS；precommit 0 漂移。

## 风险与未决

- 作业输出仍按 UTF-8 解释（GBK 控制台是已知老问题）。
- 30 分钟 / 256KB / 并发 4 是先验默认值；需要时再开 flag（`--job-*`）。
- v6 让 v5 GUI 拒连；本仓 GUI 同步升级，README 兼容矩阵一并记录。
## 实现状态（2026-11-05，已落地）

### 实际形态（与上面设计的差异，逐条说明）

- **卡片挂点**：作业卡**不单独建行**，而是挂在发起它的 `run_command` tool_call 行上。
  原因：GUI 的重建是「按 timeline 重放」（`MaterializeRows`），新行类型要连带改
  timeline/probe/布局；而作业天然属于那次调用，挂同一张卡更贴近语义。
  代价：标题是后缀形态 `› run_command · 后台 j1 · 运行中/已终止`，不是原设计的
  `⏵ 后台 j1 · <cmd>`。探针判据同时检查标题含 `j1`+状态、正文含命令与尾部输出。
- **折叠体**：正文 = 工具参数 + `── 后台 j1 · 状态 · 输出 N ──` + 尾部输出（GUI 侧
  再截 64KB）。默认折叠，与工具卡一致；展开/收起走既有 `BindCollapsible`。
- **单轮取消不杀作业**：`shutdown` 只在 bye / stdin EOF 时调用（bye 之前先发完 end，
  保证 journal 里 start 必有 end）。
- **超时**：后台作业不吃 `run_command` 的 60s 工具超时；作业自身 30 分钟墙钟上限
  （`jobMaxAge`）。`timeout_seconds` 只影响前台路径。
- **30 分钟上限**：由 `time.AfterFunc` 实现，**未**做注入时钟的单测（真实等待不可行）；
  终止路径与 kill 共用 `terminate()`，已有单测覆盖。

### 读数（本机，2026-11-05）

- `go test ./...`：**全绿**；`go test -race -run 'TestJobHub|TestLoopE2E_BackgroundJob'` 全绿。
- `protocol/validate.py`：**17 份夹具全绿**（v6 新增 jobs 正向 + I14 hostile）。
- `--job-probe` 正向（jobs 夹具）：
  `modelJobs=1 viewRows=1 titleMatched=1 bodyMatched=1 tailMatched=1 staleRows=0 matched=true`，
  `unknownTypes=0`（job 已登记为已知类型）。
- `--job-probe` 负向（demo）：`modelJobs=0 viewRows=0 staleRows=0 matched=true`。
- `--rebuild-probe`（jobs 夹具）：forward / forward-again `mismatches=0 orderMismatches=0`；
  lexicographic 负对照按设计红（10 处，证明判据有区分力）。
- `--job-probe` 已进 `tools/regress.py`（EXTRA_PROBES + 负对照一支）。

### 未做 / 下一步

- 作业卡**没有**独立行（见上）；若以后要独立行，需新增 TimelineEntry::Kind 并同步
  parity/session 探针的 row-kind 统计。
- 输出编码仍按 UTF-8 解释（GBK 控制台输出会显示为替换字符），与前台命令同一已知限制。
- 并发 4 / 256KB / 30min 仍是先验默认值，未开 CLI flag。
