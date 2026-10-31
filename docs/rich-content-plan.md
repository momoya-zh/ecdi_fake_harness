# 富内容三项：多行输入 / 文件导入 / 表格渲染 —— 方案（2026-10-28）

> 背景：会话持久化（L1~L3）已闭环（见 [session-persistence-decisions.md](session-persistence-decisions.md)）。
> 本档覆盖三项新诉求；沙箱三档权限模式已另行后置，不在本档范围。

## 0. 现状事实（三路调查，均给锚点）

### 0.1 输入框多行 —— 疑似「已经存在」
- Composer 编辑框本就是多行 TextBox（`SetSingleLine(false)`，PromptBar.cpp:183）；
- **Enter 发送、Shift+Enter 插换行**已落地（ComposerBox::OnKeyDown，PromptBar.cpp:118-140）；
- 自动长高 + 封顶 [46,260] + 超限内部滚动（DesiredInputHeight/SyncInputHeight，PromptBar.cpp:377-419）；
- 多行粘贴由 ECDI 框架承担并触发变高（PromptBar.cpp:110 注释列明粘贴在六个 RaiseTextChanged 调用点内）；
- 发送链路 `\n` 走 JSON 转义，JSONL 每行仍单行，协议不破（Json.cpp:228-235）；
- core 原样入 user 块与模型消息（session.go:462-470），转录 user 气泡按换行渲染。
- 探针已有五相（--enter-probe：enter-sends / shift-enter-newline / empty / whitespace / unfocused）
  与九相（--grow-probe 含 shrink / exact-fit 陷阱相）。

⇒ 「多行不支持」若属实，缺口必在以上环节之外；**先复现再动手**，见 §A。

### 0.2 文件相关面
- 协议入站全集 `hello/prompt/cancel/answer/workspace/model/bye`（PROTOCOL §4），**无附件概念**；
  出站 kind 全集 `user/assistant/reasoning/tool_call/tool_result/system/error`（§3.3，孪单 validate.py:51）。
- core 只读三件工具（read_file/list_dir/grep）以 `s.cwd` 为根，`resolveInWorkspace` 拒根外与 `..` 逃逸
  （tools.go:96-123）⇒ **工作区外的文件模型读不到**。
- ask 门形状：`block_start(tool_call)` → `ask{id,kind:"permission",options:[allow/deny],default:"deny"}`
  → `answer{id,choice}` → 执行（session.go:561-640）。
- GUI 有壳对话框先例：`GetSaveFileNameW`（AppExport.cpp:176）、`SHBrowseForFolderW`（AppWorkspace.cpp:377）；
  **无任何拖放**（DragAcceptFiles/IDropTarget 零命中）。

### 0.3 表格渲染落点
- assistant/system/error 块 = 一个 Panel 挂**单个**文本控件；流式 delta 走
  `row->label->SetText(block->text)` **单控件整段覆盖**假设（AppStream.cpp:489-497）。
- 版心三口径唯一：`TranscriptLayoutWidth = min(可用宽, 760)`，行内容宽 `layoutW - 2*pad`（AppHud.cpp:156-222）。
- 行高规则 `rowH = label->GetHeight() + 16`、wrap 判决在 AppHud.cpp:317/332。
- 恢复/journal/live **同一条渲染管道**（OnLine→RouteLastMessage→CreateBlockRow）⇒ 一处实现全覆盖。
- 框架有 `GridLayout` 但为定列定格，gui/src 零使用，不适配两维测量 ⇒ 表格需自排。
- 读数会受「一 block 多控件」冲击的探针：--rebuild-probe（rows.size）、--hit/--answer-probe
  （按控件中心合成点击）、--grow-probe（行高）、--cull-probe（measureChars 仪器）、--select-probe
  （一 block 一 TextBox 假设）。

## A. 多行输入：先核对，不动手（已核实 → 登记 `REQ-15`）
1. 让用户复现具体场景（打字？粘贴？超长？某主题下？）。
2. 备一组**纯核对**增量（新相进 --enter/--grow 或独立快照），只证明现状，零产品代码改动。
3. 证实有缺口才立项；缺口形状由复现决定，本文档不预设修法。
4. **2026-10-28 核实结论**：用户诉求为「连续输入超宽时**自动软折行（Word Wrap）**并撑高输入框」。查 [`TextBox.h:77-78`](../third_party/ecdi/include/ECDI/Widget/TextBox.h#L77-L78) 确认 `TextBox` 当前行数口径严格等于 `\n 数 + 1`（`RecalculateLines` 仅扫 `\n`，超宽走 `m_scrollOffsetX` 横向滚动），属 ECDI Phase 29 **O2（编辑器内 wrap）** 挂账项。已按本仓铁律在 [ecdi-capability-requests.md](ecdi-capability-requests.md) 正式登记为 **`REQ-15`**，不在应用层做塞 `\n` 绕行；**已于 2026-10-31 消费闭合**（`ComposerBox` 开启 `SetWordWrap(true)` + `--grow-probe` 补三相达到 12/12 全绿，闭合 `REQ-15`）。

## B. 表格渲染：渲染期解析，协议与 journal 零改动

**主案**：GUI 在 assistant 块文本上识别 markdown 管道表格，解析成网格控件排布。
- 协议/存储不动：journal 逐字节不变、context 里仍是 markdown 原文（模型侧口径不漂移）、
  旧夹具基线不动、无版本 bump。
- 解析器收在 Transcript.cpp 一处：`ParseMarkdownSegments(text) → [Paragraph|Table]` 纯函数，
  可被探针直接调用（判据本体是纯函数——startup-gate 的同款教训）。
- 切分点必须落在 `block_end`，流式中途整段回落纯文本（本仓不要静默半态）。
- 表格吃版心 `layoutW`；列两维测量排布。
- 探针：新增 `--table-probe`（夹具内含表格样例）；§0.3 冲击清单逐项核对，
  **预期** rows/点击落点等读数变化需显式改判据并记录，不静默重基线。
- 按惯例先出 HTML 原型（docs/prototype/）过目，再动 C++。

**备选 C2**：协议加 `kind:"table"`，core 作唯一作者——转录结构化、机器可判，
但动协议 + I5/KNOWN_KINDS 孪单 + fake-core + 夹具 + journal 字节全改，代价大。
**富文本引擎**：仅当内联样式（行内 code/粗体）也上桌面时才谈；表格本身不需要它。

## C. 文件导入：staged 进工作区，协议不动

**主案**：GUI 把用户选中的文件复制进工作区，再发一条普通 prompt 告知相对路径，
模型用现有 read_file 读（ask 门照常生效）。协议零改动。
- 入口：PromptBar 加「导入文件…」按钮（仿 GetSaveFileNameW 先例；模态循环对探针的死锁风险
  按 AppExport 既有规避）；拖放（OLE IDropTarget）列为后置可选项。
- 落点默认 `<工作区>/.ecdi/imports/`（.gitignore 屏蔽、不污染用户仓库）；重名加时间戳前缀；
  路径进 prompt 文本 ⇒ 进转录与 context，可审计。
- 体积上限 + 超限**可见提示**，不静默截断。
- 边界裁决：根外直读**不做**——与后置的沙箱硬界（core 执行时强制工作区根）同向；
  将来若开「批准单文件临时可读边界」，走沙箱档一起裁。

**备选 C1**：协议新入站 `attachment`——动 KNOWN_TYPES/wire/ProtocolWriter/processLine/§4 全套，
且文件内容不进 context 的话与 staged 等价，不进的话又漂移；后置。

## D. 建议顺序
A（核对，半小时级）→ B（原型先行，过目后实施）→ C（实施小）。
理由：B 用户可感知收益最大且有明确的探针冲击面需要设计；C 独立且改动最小；A 可能无事可做。

## E. 待裁决
| # | 问题 | 选项 |
|---|---|---|
| E1 | 表格实现 | B=渲染期解析（主案）/ C2=协议新 kind / 直接上富文本引擎 |
| E2 | 导入落点 | .ecdi/imports/（主案）/ 用户仓库内可见目录 / 只引用路径不复制 |
| E3 | 顺序 | A→B→C（主案）/ 其他 |
| E4 | 多行输入 | 是否先由用户复现具体缺口（主案）|

## F. 裁决记录（2026-10-28）
E1=渲染期解析；E2=复制进 `.ecdi/imports/`；E3=A→B→C；E4=先由用户复现缺口（已确认为 `TextBox` 软折行缺失，登记 `REQ-15` 交 ECDI 侧实现）。

