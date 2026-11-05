# ask 提案卡（detail）方案与落地记录

日期：2026-11-05。前批：`docs/subagent-v5-plan.md`（协议 v5 子代理，B1–B4 已闭合）。

## 用户需求

「距离常规可用的 agent 程序还差不少，我正在想怎么让它可用。」可用性差距评估（2026-11-05 与用户对齐）排序：
① **ask 门审阅质量**（最高杠杆）② run_command 的 OS 级隔离（L1：Job Object + Safer token）③ 后台长任务
④ MCP host。**本批做 ①**：ask 门从「截断的纯文本问句」升级为「结构化提案卡」——它同时是 ② 的底座
（沙盒残余风险明示也要靠这张卡）。用户拍板：先做 ①。

**病因（实测代码）**：`edit_file` 的问句把新旧串各截 60 字符、`run_command` 截 160 字符——用户审不了
他在允许什么。提示注入场景下（agent 读进来的文件内容操纵它跑命令）这道门是最后一道人工防线。

## 决策与取舍

1. **diff 由 core 算，不进 GUI**。当前磁盘内容只有 core 知道；2MB 内容不该为审阅进 GUI 进程；
   GUI 没有 diff 库，为它写一个等于同一逻辑维护两份。落点 `core/cmd/ecdi-core/diff.go`。
2. **只加可选字段，不加 kind、不加不变量**。`ask.detail` 是渲染数据（与 `block_start.args` 同地位），
   损坏的最坏后果 = 回落问句文本（已有路径）——不造恒难触发的机器检查（validate 不查，文档写明）。
   §9「同一主版本内只增不改」允许，v5 行增补。
3. **edit_file 的 diff = 替换片段**（old_string→new_string），不是整文件 diff——那正是提议的改动本体；
   整文件上下文在 tool_call 的 args 卡里，不重复搬运。write_file 的 diff 旧侧 = **批准前**的磁盘内容
   （新文件 = `--- /dev/null` 全 `+` 行；读不了 ⇒ 不带 diff，不编假数据）。
4. **展开体默认展开**：审批时刻必须看得见将要发生的事。无 detail 的 ask 与 v4 逐位一致
   （77 条既有 ask 行零扰动——回归基线靠它）。
5. **行 = 定高 mono Label、不折行**：diff 的缩进就是语义，折行毁对齐；长行靠控件裁剪。
   行高 `kProposalLineH=18`，行数建行期定死 ⇒ Relayout 的 bodyH 是**纯乘法，零测量**（A-2 教训的应用）。
6. **规模策略（诚实标注）**：公共前/后缀 O(n) 整表修剪后收回 context 3 行；中段超 1500 行/侧
   ⇒ 不做 LCS（O(n·m) 会把审批卡死），粗展示两侧头部 + 截断标注；产出超 400 行截断标注。
   所有截断显式——静默截断会让审阅者拿着半份 diff 批一个没看全的提案。

## 落地记录（2026-11-05）

| 批 | 范围 | 状态 |
|---|---|---|
| 协议 | `PROTOCOL.md` §3.6 增可选 `detail`（字段表 + core/GUI 义务 + 「不做成不变量」的明示）、§7 增「ask 提案卡」渲染规格、§9 v5 行增补；夹具 `fixtures/proposal/write-edit-run-asks.jsonl`（15 份全绿） | ✅ 本批 |
| core | `diff.go`（unifiedDiff + askDetail 四工具分支）+ `diff_test.go` 六案 + `askQuestion` 增 detail 参数（runToolCall/prepareDelegation 两处接线）+ E2E `TestLoopE2E_AskDetailProposal`（三工具 detail 断言） | ✅ 本批 |
| gui | `App.h` AskDetail 进 TimelineEntry（重建同构）、`AppendProposalLines`（mono 逐行配色）、Relayout bodyH 乘法 + 宽度门闸、`ProbeProposal.cpp`（--proposal-probe 模型⇔视图两路对齐）、regress 正/负对照接线 | ✅ 本批 |
| 文档 | README（探针 33、要点、能做什么）、P3 规格 §4.3 增补、本文件 | ✅ 本批 |

## 探针读数（.scratch 实测）

- 正向（proposal 夹具）：`askRows=3 detailRows=3 diffRows=24 plus=18 minus=3 kv=4 matched=true`
  ——模型侧重切与视图侧建行统计逐项相等。
- 负对照（demo 夹具）：`askRows=1 detailRows=0 diffRows=0 kvRows=0`——回落路径完好。
- ★ 判据分叉教训（同 cap-gate 一族）：探针 `matched` 含 `detailRows>=1`（正向臂专用），
  **负对照不读 matched**，读具体读数——否则负对照恒红或恒绿二选一。

## 已声明未做 / 另批

- **L1 沙盒**：✅ 已落地（2026-11-05，`docs/l1-sandbox-plan.md`）。复核补了 fail-closed、
  挂起→挂接→恢复、提案卡两行 note、非 Windows 诚实边界。
- diff 长行**不折行**（裁剪保对齐）：若目视验收不可接受，另批改横向滚动。
- `task` 提案行的任务全文按 200 码点截断（完整任务在 args 卡与问句里）。
- fake-core **不动**（基线恒 protocol 3）；detail 只出现在真 core 与新夹具。
