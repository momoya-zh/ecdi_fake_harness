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

### C.1 落地记录（2026-11-10/11，文件导入与暂存胶囊）

1. **入口与界面**：PromptBar 增加导入按钮（`+` 胶囊），点击调起 Win32 原生 `GetOpenFileNameW`，支持选择任意文件；已暂存文件在输入框上方渲染为附件胶囊（含文件名、大小、删除按钮 `×`）。
2. **复制落点与防污染**：选中的文件被复制到 `<工作区>/.ecdi/imports/<timestamp>_<filename>`；重名自动加时间戳前缀；`<ws>/.ecdi/.gitignore` 自动写入 `*`，严格保证不污染用户 Git 仓库。
3. **协议零改动与消费闭环**：发送 prompt 时将附件相对路径拼入提示词文本末尾（如 `[附带文件: .ecdi/imports/xxx]`），模型侧通过标准 `read_file` 读取，协议与 journal 完全零改动。
4. **UTF-8 与跨语言支持**：采用 `Utf8FromWidePath` / `WideFromUtf8Path` 与宽字符直接传递，消除 MSVC CP936(GBK) 导致的中文文件名乱码。
5. **验证探针**：`--import-probe` 6 相全绿（`import-stage`、`import-remove`、`import-submit`、`import-oversize`、`import-no-workspace`、`import-utf8-name`）。

## D. 建议顺序
A（核对，半小时级）→ B（原型先行，过目后实施）→ C（实施小）。
理由：B 用户可感知收益最大且有明确的探针冲击面需要设计；C 独立且改动最小；A 可能无事可做。
（★ 2026-11-10 现状：A、B、B+、C 全部完成闭环。）

## E. 待裁决
| # | 问题 | 选项 |
|---|---|---|
| E1 | 表格实现 | B=渲染期解析（主案）/ C2=协议新 kind / 直接上富文本引擎 |
| E2 | 导入落点 | .ecdi/imports/（主案）/ 用户仓库内可见目录 / 只引用路径不复制 |
| E3 | 顺序 | A→B→C（主案）/ 其他 |
| E4 | 多行输入 | 是否先由用户复现具体缺口（主案）|

## F. 裁决记录（2026-10-28）
E1=渲染期解析；E2=复制进 `.ecdi/imports/`；E3=A→B→C；E4=先由用户复现缺口（已确认为 `TextBox` 软折行缺失，登记 `REQ-15` 交 ECDI 侧实现）。

## G. 扩围裁决（2026-11-07）：转录富渲染 = 块级全套先行，内联样式登记 REQ

> 用户定调：「富内容其实应该把转录全做好，目前就只做了表格的转录」。
> 现状核对：`ParseMarkdownSegments` 只有 `Paragraph | Table` 两段种（`gui/src/Transcript.h:156`）；
> 标题 / 代码块 / 列表 / 引用 / 分隔线 / 链接 / 段内粗斜体全部按原文显示。

**范围拆两层，代价不同：**

1. **块级全套（本批做）**：标题（h1–h3）/ 围栏代码块（mono + 底色）/ 有序、无序列表 / 引用块 /
   分隔线 / 链接。应用层全做得了——ECDI `Font` 已有 weight/family/size（REQ-10 已消费），
   每块一个控件、各给各的字体，是表格方案（§B）的同形扩展：
   - 扩 `MarkdownSegmentKind` 段种 + `ParseMarkdownSegments` 纯函数 + 组装分支，解析器仍收在一处、探针可直接调。
   - 协议 / journal / context 依旧零改动（模型侧口径不漂移）；切分点仍落 `block_end`，流式中途整段回落纯文本。
   - 版心仍 `TranscriptLayoutWidth = min(可用宽, 760)`；行高/探针冲击面按 §0.3 清单显式改判据、不静默重基线。
   - **按惯例 HTML 原型先行**（`docs/prototype/`）过目，再动 C++。
2. **段内内联样式（粗体 / 斜体 / 行内 code）= REQ-20，交 ECDI**：框架一控件一套字体
   （`TextWidget.h:23` TextStyle 唯一来源；安装头 + ECDI 源仓 grep styled-span 类 API 零命中），
   已按铁律登记 `ecdi-capability-requests.md` **REQ-20**（对账 ECDI #53 群 / 富文本另立项既有裁决，
   不排期、不阻断块级）。**不在应用层拆控件绕行**——会打破「一 block 一 TextBox」选中/复制假设。

**顺序更新**：A ✅ → B ✅ → **B+ ✅（块级全套与内联样式）** → **C ✅（文件导入与暂存胶囊，已于 2026-11-10 闭环）**。全部就绪。

### G.1 批 1 落地记录（2026-11-08）：解析器 + 纯函数判据

**已完成（只动解析器与探针，未动渲染、未动协议/journal/夹具）：**

| 项 | 落点 | 证据 |
|---|---|---|
| 段种扩到 7 种 | `MarkdownSegmentKind{Paragraph,Table,Heading,CodeBlock,ListItem,Quote,Hr}`（`Transcript.h:156`） | `--md-probe` 8/8 PASS |
| 行状态机 | `ClassifyLine` / `StripListMarker` / `StripQuoteMarker`（`Transcript.cpp` 匿 namespace），主循环按围栏→表格→标题→分隔线→列表→引用→段落的优先级装配 | 同上 |
| 基线不变量**扩大** | 原判据「无 Table ⇒ 单 Paragraph 等于原文」→「**除 Paragraph 外一个段都没有** ⇒ 单 Paragraph 等于原文」（`Transcript.cpp` 尾） | 7 份夹具 blocks/deltas/messages 逐字不变（见下） |
| 新判据 | `--md-probe` 8 相：heading / fence / fence-unclosed / list / quote / hr / no-construct-identity / mixed-order | 全量 `tools/regress.py` = **PASS** |

**基线证据**（批 1 后实测，与批 1 前逐字相同）：

```
demo-session         blocks=8     deltas=19   messages=43    viol=0
realistic-session    blocks=220   deltas=717  messages=1213  viol=0
cot-session          blocks=44    deltas=934  messages=1044  viol=0
scale-transcript-2000 blocks=2000 deltas=4590 messages=8906  viol=0
```

**两处与 §G 原定口径的偏离（须记，否则后续验收对不上）：**

1. **链接本批不解析**：用户 2026-11-08 裁定「等 REQ-19-B 落地再做链接」。§G 原文把链接列进块级全套，
   实际按裁决**移出本批**——`[x](y)` 按原文进 Paragraph，不解析、不假装支持。
   加进段种却点不开，比不渲染更糟。
2. **列表逐项成段，不合并成一个 List 段**：每个 `- xxx` 产出一条 `ListItem` 段（带 `ordered/marker/depth`）。
   理由是「一控件一套字体」+ 悬挂缩进要靠 marker 与正文分栏摆位；合并成多行文本会把悬挂缩进做成第二套排版。
   `--md-probe` 的 `list` 相断言「5 项 / 3 有序 / 序号原文 1-2-3 不重排 / depth=1 恰一处」。

**REQ-20 已由 ECDI 侧交付，但**验收口径收窄**：`SpanStyle` 只有 `family/weight/foreground`
三个 optional，头注释明文「严禁包含 size（闸门 3）或斜体（闸门 2 / L44-8）」。
⇒ 内联样式实际只能兑现**粗体 + 行内 code** 两件，**斜体永远不会生效**（不是没做，是框架裁决不做）。
§G 原文「粗体 / 斜体 / 行内 code 三件套」须按此改写，否则将来照文档验收斜体会撞空。

> ★★ **2026-11-09 订正（此段结论已过期，保留作历史）**：Phase 45（ECDI `eb2106a`）已在
> `SpanStyle` 上追加 `italic` 与 `size` 两个维度，头注释也同步改写（不再是「严禁包含」）。
> ⇒ 「斜体永远不会生效 / 字号不可变」**只在 Phase 44 时点成立**。本仓的实际范围由用户
> 2026-11-09 重新裁定：**斜体暂不纳入、`size` 留位不用**（见 §H.1）——这是**产品选择**，
> 不再是框架限制。★ 教训：凡以「某 Phase 时点的头文件」为依据写下的能力边界，消费前必须
> 重读一遍安装头（本次若照旧文验收，会把「能做的」当成「做不了的」）。
ECDI 侧链路已核对可用（`TextWidget::SetStyledText` 在 `ECDI.lib` 符号表内；安装头与 ECDI 源树 102/102 逐字节相同）。

### G.2 批 2 落地记录（2026-11-08）：渲染 + 判据推广 + 夹具

| 项 | 落点 | 证据 |
|---|---|---|
| **判据收成一处** | 新增 `HasMarkdownBlocks(segs)`（`Transcript.cpp`）＝「存在任一非 Paragraph 段」。原先两处各写一份「找 Table」（`AppTranscript.cpp:261` 重建期 / `AppStream.cpp:581` 流式结束期）全部改为调它 | 漏改的形态从「静默不渲染」变成「编译不过」 |
| `hasTable` → `hasMarkdown` | `RowRec::hasMarkdown` 为正名；`hasTable()` 保留为**只读别名函数**（9 个既有引用点含 `--table-probe` 2 处回读无需同时改判据） | 写路径唯一，不会双份状态漂移 |
| 段元数据 | `RowRec::SegMeta{kind, level, depth, ordered, marker, lastOfList}`，与 `segments` **严格同序同长** | `render-blocks` 相断言 `segMeta.size()==segments.size()` |
| 装配分支 | `BuildAssistantMarkdownWidgets` 扩到 7 段种；新增 `BuildCodeBlockWidget` / `BuildListItemWidget` / `BuildQuoteWidget` / `BuildHrWidget`（`AppTranscript.cpp`） | `render-blocks` PASS |
| 段间距按种 | `Relayout` 段循环改为读 `segMeta` 取间距：h1 12 / h2 10 / h3 8 / 代码块 12 / 列表项间 3 末项 10 / 引用 10 / hr 12 / 表格 12 | 原为写死 +10/+12，做不出层级 |
| 代码块横滚 | `ScrollView` + `BarStyleOverride{.thickness=4}` + 内容 `GetContentView().AddChild` + `SetContentExtent`（照表格先例） | 用户裁决：横滚 + 复制钮，不软折行 |
| 专用夹具 | `protocol/fixtures/markdown/blocks-session.jsonl`（53 行 / 5 块 / 39 delta），生成器 `gen_blocks_fixture.py` 确定性可复现（sha256 `20fdda1a…`） | 既有夹具 **一律未动** |

**批 2 后基线复验（逐字相同）**：demo 8/19/43、realistic 220/717/1213、cot 44/934/1044、scale 2000/4590/8906，violations 全 0。

**§0.3 冲击面实测结论（逐条核对，无静默重基线）**：

| 探针 | 结果 | 说明 |
|---|---|---|
| `--select-probe` | 3/3 PASS | 「一 block 一 TextBox」假设**未被打破**——块级行内每个段仍各是一个 TextBox，但选中/复制相判的是一致性而非「只有一个」 |
| `--rebuild-probe` | 3/3（negative 为 designed-red） | 行数与内容未漂移 |
| `--grow-probe` | 12/12 PASS | 行高逻辑未受影响 |
| `--answer-probe` | 5 PASS + negative(designed-red) | 点击合成未受影响 |
| `--table-probe` | 6/6 PASS | 表格线完全没有回归 |
| `--md-probe` | **10/10 PASS** | 新增 |

⇒ §0.3 预警的五个探针**没有一个需要改判据**。原因是块级只在「含块级构造」的块上生效，而既有夹具里这类块的数量是 0——所以基线天然不动。这不是巧合，是「解析器在无构造时逐字回落原文」这条不变量的直接结果。

### G.3 仍然开放

1. ✅ **`REQ-20` 内联样式**——**2026-11-09/10 本仓已消费**（粗体 + 行内 code + 斜体 + 粗斜体均已上线；
   `size` 留位），见 **§H**。
2. **链接**——等 `REQ-19-B`（OpenExternal）。`[x](y)` 当前按原文显示。
3. ✅ **C 项（文件导入与暂存胶囊）**——**2026-11-10/11 已闭环**：见 **§C.1**。
4. **`REQ-21`（新）**——`SpanStyle` 缺 `background` ⇒ 行内 code 无底色块。已登记、不等它。

---

## H. 段内内联样式（富内容 §H，2026-11-09）

> 触发：ECDI Phase 44/45 交付 `TextWidget::SetStyledText` + `SpanStyle`（`#88`）后，本仓开始消费。
> 用户同批拍板：**粗体 + 行内 code 上线；斜体暂不做；`size` 留位；`background` 缺口先登记（REQ-21）**。

### H.1 范围与验收口径（★ 先钉死，否则验收会撞空）

| 构造 | 语法 | 兑现方式 | 状态 |
|---|---|---|---|
| 粗体 | `**x**` / `__x__` | `SpanStyle::weight = 700` | ✅ 上线 |
| 行内 code | `` `x` `` | `family = "Consolas"` + `foreground = 强调蓝` | ✅ 上线（**无底色**，见 REQ-21） |
| 斜体 | `*x*` / `_x_` | `SpanStyle::italic = true`（Phase 45） | ✅ 上线（含 `***x***` / `___x___` 粗斜体） |
| 字号覆盖 | `SpanStyle::size` | —— | ⏸️ **留位不用**（保版心行高稳定） |
| 删除线 / 链接 | `~~x~~` / `[x](y)` | —— | ❌ 不做（无 span 维度 / 等 REQ-19-B） |

### H.2 实现落点

| 项 | 落点 | 说明 |
|---|---|---|
| 解析器 | `ParseInlineRuns(text, InlineCodeStyle)`（`Transcript.cpp`） | **纯函数、三遍式**：扫描 token → 配对校验 → 输出 clean text + spans。规则 R1–R9 全写在实现处注释里 |
| 码点口径 | `ECDI::DecodeCodepointAt` | ★ **不自己写前导字节判定**——`SequenceLength` 在框架里已有一份真相，复制一份就会出现「cp 索引与 ECDI 漂移」这类最难查的错 |
| 判据 | `HasInlineStyles(runs)`（`Transcript.cpp`） | 与 `HasMarkdownBlocks` 同一条纪律：唯一实现，渲染侧只调 |
| 渲染接入 | `HarnessApp::ApplyInlineText`（`AppTranscript.cpp`） | **唯一入口**，三路正文共用：复合渲染的段控件（`makeText`）/ 无块级构造的只读 `TextBox` / `Label` |
| 流式升级 | `AppStream.cpp` 的 `block_end` 分支 | 只有行内样式（无块级构造）的块，流式期仍是原文；块结束时**就地** `ApplyInlineText` + `Relayout`（不重建整行——行内只换 text/spans，重建会冲掉滚动与选中） |
| 判据 | `--md-probe` 相 11–28 · `--table-probe` 相 1–7 | `inline-bold` · `inline-code` · `inline-mixed`（嵌套扁平化）· `inline-unclosed` · `inline-escape` · `inline-negative`（flanking/intraword/零内容三合一负臂）· `inline-cp-cjk`（★ 码点索引专项）· `inline-code-literal` · `inline-identity`（基线）· `render-inline` · `render-inline-negative` · `inline-italic` · `inline-bold-italic` · `inline-italic-negative` · `render-inline-italic` · `render-list-inline` · `render-selectable-rich`（`--selectable-rows` 下列表/引用只读 `TextBox`）· `render-codeblock-extent`（代码块实测高度与长行横滚 extent）· `table-inline-cells`（表头/表体行内样式与去标记自然列宽） |

### H.3 为什么解析器是「三遍式」而不是「区间查找 + 字符串替换」

1. **码点索引是硬约束**：`TextSpan.startCp/cpCount` 是 UTF-8 解码后的字符位。删掉 `**` 再找位置的实现
   几乎必然按字节算偏移 —— 纯 ASCII 相全绿，中文段落一进来就漂。逐码点步进让 cp 索引成为扫描的
   自然产物（`inline-cp-cjk` 就是为这条写的相：emoji 4 字节 + 中文 3 字节）。
2. **未闭合必须回落原文**：流式中途 `**粗` 每天都在发生。区间查找式为「找不到闭合」要写回滚；
   先配对再把未匹配标记降级为字面，天然满足（`inline-unclosed`）。
3. **嵌套要扁平化**：ECDI 的 span 契约是「有序、无重叠」，而 `**粗 `code` 尾**` 语法上是嵌套的。
   逐码点取「当前有效标记栈」的样式位、再把**样式相同的连续码点**合并 ⇒ 自然摊平成
   `[粗][粗+code][粗]` 三个不重叠区间，没有专门的扁平化代码（`inline-mixed`）。

### H.4 与既有基线的关系

- **无构造 ⇒ 逐字节旧行为**：`ParseInlineRuns` 无标记时 `text` 逐字等于原文、`spans` 空 ⇒
  `ApplyInlineText` 落回 `SetText`。7 份既有夹具的 blocks/deltas/messages 与渲染路径**不变**
  （与块级批 2 同一条不变量，`inline-identity` + `render-inline-negative` 两相守它）。
- **`user` 气泡不接**：用户输入不按 markdown 渲染（保持「我说的」原文）。
- **tool 体 / ask 体不接**：本批只做 assistant 正文链路（含 `system` / `error` 同分支）。

### H.5 开放问题（本批**不**解决，如实留痕）

1. ★ **两种复制口径不一致**（本批引入）：轮次「复制」按钮走 `GetTurnCopyText`，取的是 `block->text`
   **markdown 原文**（含 `**`）；而 Ctrl+A/C 选中复制走 TextBox，拿到的是**干净文本**（无标记）。
   ⇒ 同一段内容两条路给出不同结果。哪个才对是产品决定（粘到文档里通常想要原文，粘到聊天框想要干净），
   **不擅自统一**。`--copy-probe` / `--select-probe` 各自自洽 ⇒ 探针不会红，但用户能感觉到差异。
2. ★ **TextBox 富文本 × 软折行未在 ECDI 单测覆盖**：Phase 44 的 `Test44TextBoxReadOnlySupport` 只测了
   定尺寸绘制 + 选区 + 明文复制，**没有开 wrap**；而本仓 assistant 正文（`--selectable-rows`）是
   **开软折行的只读 TextBox**。⇒ 「行内 code 正好跨折行点」这一情形只能靠本仓实测，
   `--md-probe` 的渲染相不覆盖它（那一相只读 `GetText()`）。**待用户编译后目视确认**。
3. **行内 code 无底色**——`REQ-21`，等 ECDI。
4. **`size` 留位不用**——保版心行高稳定（斜体已于 2026-11-10 消费上线）。
5. **表格单元格 / 引用块 / 列表项 / 代码块（断点补齐已关闭）**：引用块与列表项正文已接通 `ApplyInlineText` 及 `--selectable-rows` 只读 `TextBox`；表头与表格单元格已接通 `ApplyInlineText` 并按去标记文本测算自然列宽（保留 `Label` 以维持对齐与斑马线绘制）；代码块保持原文不解析行内语法，且已在 `Relayout` 补齐真实换行测量与 `ScrollView::SetContentExtent` 更新。



