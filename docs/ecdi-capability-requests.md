# ECDI_fake_harness · ECDI 能力需求账本

> **定位**：本项目对 ECDI 的**能力需求 / 缺陷登记**的唯一落点。
> 依据项目铁律——**任何对 ECDI 框架的修改都不在本仓库做**，这里只登记需求、只给证据、只判归属（见 `P1-gui-implementation-spec.md` §1.2）。
> 状态：**v2.6**（2026-10-17；v1.0 建立于 2026-10-08）。
> ★ 建立缘由：`P1-gui-implementation-spec.md:243` 与 `P1.5-child-process-spec.md` 都写「登记进账本」，
>   但**该账本此前并不存在**——P1 期的 8 条发现（F1–F8）只散在 `.workbuddy/memory/` 的当日日志里。本文件补齐这一缺失的交付物。

---

## §1 编号与状态

### 1.1 编号规则

| 前缀 | 含义 | 说明 |
|---|---|---|
| `REQ-NN` | **对 ECDI 的能力需求 / 缺陷** | 本账本主线；需 ECDI 侧动作 |
| `TODO-NN` | **harness 自身待办** | 不动 ECDI 就能做，只是本仓库还没做 |

★ **归属必须核实到「基类 / 框架源码」层**：判「ECDI 缺能力」前，**必须**查过 `Widget.h` 等**基类公共头**
  **与 ECDI 源码仓**（`D:\Project\ECDI`，含其自带测试），不能只看派生类头（`TextWidget.h` / `Label.h`）；
  且归属结论须有**只改一个自变量的受控对照**支撑（不是「A 失败 / B 正常」的外推）。
  ★ 教训：**REQ-01 / REQ-03 / REQ-04 三条全因违反此纪律而误判为 ECDI 侧**——`SetEnabled` 就在基类 `Widget.h:150-152`；
    `PostToUi` 早已实现（`Application.h:86`）；`\n` 切硬段就在 `TextWidget.cpp:235-248`。三条现已全部**反转为 harness 侧**。

★ **历史编号不重编**：P1 期的 `F1–F8`、P1.5 的 `R-1–R-3`、P3 诊断的 `D-1–D-3` 是**既有引用锚点**，
本账本在各条目的「原编号」列里**原样保留**它们，不改写、不回收（条 125：条号是易腐坏的引用）。

### 1.2 状态图例

| 状态 | 含义 |
|---|---|
| 🔄 **开放** | 已确认，ECDI 侧未闭合 |
| ✅ **已闭合** | 已被后续 ECDI 阶段兑现（保留条目 = 历史链条） |
| ⏸️ **待裁决** | 归属未定（判 ECDI 缺陷 or harness 用法问题），需拍板 |
| 📋 **已登记** | 已确认，但**按定调推后到 P2 / P3 处理**，本轮只登记 |
| ⚠️ **归属反转** | 曾判 ECDI 侧，复核后**改判 harness 侧**——原判以引用形式保留（条 30 / 61②） |

---

## §2 条目（★ REQ-01/02/03/04 = harness 侧，**均已闭合**；REQ-05 已闭合；**REQ-08 已闭合（2026-10-16，Phase 33）**；**REQ-06 已裁决结案（2026-10-17，③ 记为已知现状）**；**REQ-09 已闭合（2026-10-17，ECDI 当日修复 + 验收 8/8）**）
> ★ 2026-10-13 晚补记：思维链（reasoning）暴露的**协议空白**与一条**候选 ECDI 需求被复核否决**（见 §2 末尾「思维链与未知 kind」小节）。
> ★★ 2026-10-14 更新：该**协议空白已由协议 v2 闭合**（`kind:"reasoning"` 一等公民 + I8 + §7 渲染形态）；
> 同时**首次测得**「折叠省下多少」（收起 vs 可见 = 65.0 vs 139.2 文本命令/帧，**2.14×**）。详见同一小节。

### REQ-01 ✅ 含 `\n` 的短文本丢换行（**归属反转：harness 侧** → **已修复闭合**）

> ★★ **REQ-01 归属反转（2026-10-08 晚，自查发现）**——本条**不是** ECDI 缺陷，
>   **是 harness 的 `needWrap` 只看宽度**。原判「ECDI 的 wrap 扫描未消费 `\n`」**作废**，以引用形式保留于下。
>   ★ 这是本项目**第三条归属反转**，且三条成因相同（**未查基类公共头 / ECDI 源码仓**）。

| 项 | 内容 |
|---|---|
| 原编号 | `D-1`（2026-10-08 P3 诊断） |
| 状态 | ✅ **已闭合**（harness 侧修复落地，2026-10-13——见本文末「三条 harness 侧闭合记录」） |
| 归属 | **harness 侧**（原判「ECDI wrap 未消费 `\n`」已作废，见下） |
| 现象 | `block_start.text` / `delta.text` 里的 `\n` 在某些块里不产生换行 |
| ★ 能力侧事实 | **ECDI 正确消费换行**：`TextWidget.cpp:235-248` 显式「**第一步：显式 `\n` 切硬段**」——注释写明「`\n` 不属任何一行；**段数 = `\n` 数 + 1**」（`A\n` = 2 段，第 2 段空 ⇒ 空行，即 D29-Ⅴ 语义）。空段 ⇒ `cpCount == 0` 的空行（`:256-260`），绘制时跳过、占行高（`:390-392`） |
| ★ 真正的缺口 | harness 的 `needWrap` **只看宽度**（`App.cpp:1115-1116`：`needWrap = r.wrapped \|\| r.label->GetWidth() > maxTextW`）。含 `\n` 但**拼接后不超宽**的块 ⇒ `needWrap == false` ⇒ `SetWordWrap(false)` ⇒ 走 **Phase 28 单条 `DrawText` 路径**（`TextWidget.h:62-63` 明写该路径「布局引擎零接触——C29-5」）⇒ **`\n` 不被消费** |
| 唯一出路 | ★ **harness 侧**：对含 `\n` 的块也开启 wrap（`needWrap` 增加「文本含 `\n`」条件），或改判据为「文本含 `\n` 或超宽」 |

**决定性取证（受控阈值探针，2026-10-08；全部实机 + 像素量化）**

| 探针 | 夹具 | 预测（若真因 = `needWrap` 只看宽度） | 实测 | 判定 |
|---|---|---|---|---|
| **A** | 6 行 × 8 字符（不超宽） | 不开 wrap ⇒ **1 行** | **1 行** | ✅ 现象复现 |
| **B** | 长首行 + 5 短行（超宽 ⇒ 开 wrap） | `\n` 被消费 ⇒ 2 + 5 = **7 行**；若被吞则约 3 行 | **7 行**（2 + 5） | ✅ ★ **
 被消费** |
| **阈值 l20** | 2 行 × 20 字符（不超宽） | 不开 wrap ⇒ **1 行** | **1 行** | ✅ |
| **阈值 l100** | 2 行 × 100 字符（超宽 ⇒ 开 wrap） | 两段各自 wrap ⇒ **4 行**；若 `\n` 被吞则 200 字符 ≈ 3 行 | **4 行** | ✅ ★ **决定性：`\n` 被消费** |
| **C** | 6 行 × 126 字符 | 12 行（**无法区分**两种假设） | 12 行 | ⚠️ 仅能排除「行距缺陷」 |

★ **l100 是铁证**：2 段各 100 字符、每段按宽 wrap 成 2 行 ⇒ 4 行。**若 `\n` 被忽略**，拼成 200 字符只会有约 3 行。
★ 诚实声明保留（条 111 配套）：探针 C 的 12 行**不能区分**两种假设，其价值仅在排除「行距/多行渲染有缺陷」。

**影响面（按真实机制重算：只有「不超宽」的块才丢）**

| 夹具 | 含 `\n` 的块 | 其中会丢换行的块 | 丢失换行数 |
|---|---|---|---|
| `protocol/fixtures/realistic/realistic-session.jsonl` | 85 | **0** | **0** |
| `.scratch/core2.jsonl`（live） | 4 | 0 | 0 |
| `.scratch/revtest.jsonl`（live） | 3 | **1** | 1 |
| `protocol/fixtures/demo-session.jsonl` | 1 | **0** | 0 |
| **合计** | **93** | **1** | **1** |

★ **影响面远小于原判**（原判称 `realistic-session.jsonl` 有 3039 个换行受影响 ⇒ **错**）：
该夹具的块**绝大多数拼接后超宽**（开 wrap）⇒ `\n` 正常消费。按真实机制，**93 个含 `\n` 的块里只有 1 个会丢**。

**原判（已作废，保留历史链条）**

> 原判 ①：「★ 违反 | 本项目协议自己的承诺：`PROTOCOL.md` §7 写 `assistant` = 「纯文本，保留换行」」——★ **该「违反」不成立**：ECDI 确实保留换行，未违反。
> 原判 ②：「归属判据 | …③ 实测换行被吞、wrap 正常 ⇒ **wrap 布局的扫描未把 `\n` 当硬断行**」
>   ——★ **作废理由**：该结论基于「探针 A 丢换行 + 探针 C 正常」的外推，**未做受控阈值对照**；
>   而源码 `TextWidget.cpp:235-248` 明明写了切硬段逻辑，`l100` 探针也证实 `\n` 被消费。真正的变量是 **harness 开不开 wrap**。
> 原判 ③：「影响面 | realistic 3039 个换行」——★ **作废**：未考虑「超宽块仍会开 wrap」，按真实机制仅 1 个块受影响。
---

### REQ-02 ✅ `tool_call` 的 `summary` 被吞（**已修复闭合**）

| 项 | 内容 |
|---|---|
| 原编号 | `F3`（P1，2026-10-03）· `D-2`（P3 复现，2026-10-08） |
| 状态 | ✅ **已闭合**（harness 侧修复落地，2026-10-13） |
| 现象 | 假 core 在 `tool_call` 的 `block_end` 里发 `summary:"已发起"`，但卡片标题只有工具名（`bash` / `todo_write`），summary 丢失 |
| 归属 | ★ **待重新裁决**：根因在 **harness 侧** `App.cpp:764`（`block_end` 分支的条件是 `kind == "tool_result"`）⇒ 初判为 **harness 自身缺陷**，非 ECDI 能力缺失 |
| 取证 | 探针夹具 `probe-titles2.jsonl`：4 种变体里，`tool_call` 的 summary 一律不显示；`tool_result` 的 summary 正常显示（`bash · 42 行` / `todo_write · 失败 · 超时`） |

---

### REQ-03 `PostToUi` **能力已具备**，但 harness 从未走异步投递路径

> ★★ **归属反转（2026-10-08，评审提出并经独立复现）**——本条**不是** ECDI 能力缺失，
>   **是 harness 侧的未使用**。原判「为何是 ECDI 侧议题」作废，以引用形式保留于下方「原判（已作废）」。

| 项 | 内容 |
|---|---|
| 原编号 | `R-2`（P1.5 实施回写） |
| 状态 | 📋 **已登记**（P2 立项时处理）· **归属 = harness 侧** |
| 现象 | 规格四处写 `PostToUi` 是「首个真实消费者」，实际 **harness 零消费**——全库唯一命中是 `App.cpp:624` 的**注释** |
| ★ 能力侧事实 | **能力早已实现**：`Application::PostToUi(std::function<void()>)`（`Application.h:86`，**Phase 23 D-1**）⇒ 转发到 `Win32PlatformApplication::PostToUi`（`:112`），含入队 / 唤醒 / 失败回滚**同临界区**、关闭态拒绝（`m_dispatchClosing`）。**ECDI 侧自带测试消费者**：`ECDI/src/Tests/ApplicationDispatchTests.cpp`（顺序 / 禁止递归消费 / 载荷生命周期等用例） |
| ★ 真正的缺口 | **harness 从未真实走异步投递路径**——`ChildSource` 用「读线程 → 有界队列 → UI 线程 8ms 预算轮询 `NextWithin`」（P1.5 实施定案），全程不经 `PostToUi` |
| 为何仍要处理 | 协议指标③要看的正是「`PostToUi` 突发下的顺序与丢失」；**不真走该路径，harness 就无法为 ECDI 产出这条评估读数**——这是 **P2 的立项理由**，而非「ECDI 缺能力」 |
| 取证 | `grep PostToUi gui/src/` 仅 1 处命中（注释）；`ChildSource::Stop()` 直接在调用线程关句柄；`Application.h:86` 与 `Win32PlatformApplication.cpp:112` 证明能力在位 |

**原判（已作废，保留历史链条）**

> 原判：「为何是 **ECDI 侧**议题 | 协议指标③要看的正是「`PostToUi` 突发下的顺序与丢失」；不真走异步投递路径就无法验证该能力」。
> ⇒ ★ **作废理由**：把「harness 没走该路径」误读成「该能力待验证/缺失」。
>   能力本身**早已实现且有 ECDI 自测覆盖**（Phase 23 + `ApplicationDispatchTests.cpp`）——缺的是**消费方**，不是**能力**。

---

### REQ-04 ✅ 按钮使能：**框架能力具备** → **已修复闭合（判据 A5 兑现）**

> ★★ **归属反转（2026-10-08，评审提出并经独立复现）**——本条**不是** ECDI 能力缺失。
>   原判「两条出路」里**出路 ② 不成立**（ECDI 不需要新增能力），只剩**出路 ① = harness 自己补访问器**。

| 项 | 内容 |
|---|---|
| 原编号 | `F5`（P1）· `R-3`（P1.5） |
| 状态 | ✅ **已闭合**（harness 侧修复落地，2026-10-13——**判据 A5 已兑现**；**边界**亦于 2026-10-14 实测闭合，见下） |
| 现象 | 协议 §6 硬要求「发送 / 停止按钮**只由 `state` 驱动**」，但 harness 侧**当前未做到** |
| ★ 能力侧事实 | **框架能力完全具备**：① `Widget::SetEnabled` / `IsEnabled` **自 Phase 3 即在**（`Widget.h:150-152`；`git blame` 确认引入提交 `a11cb9b7`，2026-08-07）；② `HitTest` **已按 `IsEnabled` 门控**（`Widget.cpp:114-128`：`if (!IsEnabled()) return nullptr;`）⇒ 禁用控件不参与命中 |
| ★ 真正的缺口 | harness 的 `PromptBar` **没暴露 `m_send` / `m_stop`**（`PromptBar.h:36-37` 私有且**无访问器**）⇒ `App` 拿不到这两个 `Button*`，无从 `SetEnabled`。全库 `SetEnabled` 仅 2 处（`App.cpp:1017-1018`，权限门按钮） |
| 唯一出路 | ★ **harness 补 `PromptBar` 的按钮访问器**（或 `SetInteractionEnabled(bool)` 一类封装）⇒ 之后按 `state` 驱动即可。**不需要 ECDI 侧任何改动** |

**★ 附带发现（供 ECDI 侧参考，非阻断）**：禁用态**无视觉反馈**——全 ECDI 源码里 `IsEnabled` 只有 1 个消费点（`Widget.cpp:124` 的 `HitTest`），`Button.cpp` 无任何 `Enabled` 相关代码。
但这是**有意的设计决策**（YAGNI），非缺陷：`modelprobe-p1-preliminary-design.md:130` 记「disabled 视觉灰化：**demo 层处理**…**框架不做 disabled 绘制**——YAG」；
`phase13-captionbar-detailed-design.md:695` 亦记「**不做**：…禁用态视觉（禁用时 `HitTest` 已过滤）」。
⇒ harness 若要禁用态「看起来灰」，**须自行 `SetStyle`**（ECDI 侧已有先例：`modelprobe` 的 busy 态自设灰底灰字）。

#### ★★ REQ-04 **边界**取证（2026-10-14）：禁用 ⇒ 点击**不达**（已实测闭合）

> ★ 为何要单独取证：`REQ-04` 此前只有「使能**位**被置对」的证据（`--state-probe` 回读
> `IsEnabled()` + 标题）。而本条的**边界**是「禁用后点击**到不到**」——中间隔着
> `Widget::HitTest` 的 `if (!IsEnabled()) return nullptr;`。**两段是独立代码**，
> 使能位为真**推不出**命中门控生效 ⇒ 必须单独测（条 81b：否定型断言必须自带正对照）。

| 项 | 内容 |
|---|---|
| 状态 | ✅ **已实测闭合**（harness 侧探针 `--hit-probe`，2026-10-14） |
| 取证工具 | `harness_gui --hit-probe`（新增）· 跑批与裁定脚本 `.scratch/probe_hit_gate.py` |
| 静态链条 | `WindowMessageHandler.cpp:148`（`WM_LBUTTONDOWN` → DIP）→ `Application::OnMouseButtonDown`（`Application.cpp:334`）→ `FindTargetWidget`（`Application.cpp:253`）→ `rootWidget.HitTest` → `Widget::HitTest`（`Widget.cpp:185`）门控在 `:188`（可见）/`:195`（使能）⇒ 返回 nullptr 则**不派发**、`Button::OnClick` 不被调 |
| 观测通道 | `HitTest` 返回值 + **`SendPrompt` 被进入的次数**（合成 `MouseButtonDown/UpEvent` 喂给 `Application::OnEvent`）——**不靠像素、不靠 `SendInput` 注入**（注入会抢前台/移光标，且此前实测 hover 色始终不出现，正对照失败） |
| 四组受控对照 | ① `as-is` 落定态+按钮中心（**正对照**）② `off-point` 同态+输入框中心（**负对照**）③ `forced-enabled` 同点+`SetEnabled(true)` ④ `forced-disabled` 同点+`SetEnabled(false)`（**判据**） |
| 实测读数（6 份夹具 × 3 轮，全部 PASS） | ①③ `hitIsSend=true, hitClass=Button, clickReached=true`；④ `hitIsSend=false, clickReached=false`，落点回落到 **RootWidget**（`hitIsRoot=true`, `hitClass=Widget`, `hitConsumes=false`） |
| ★ 关键细节 | 禁用时 `HitTest` **不是**返回 `nullptr`——按钮被跳过，命中**回落**到祖先 `RootWidget`（其 `ContainsPoint` 覆盖整个客户区）。**「命中者 != 发送按钮」与「命中 nullptr」是两回事**，读数里分开记（`hitIsSend` / `hitIsNull` / `hitIsRoot`） |
| 结论 | ECDI 的 `HitTest` 使能门控**按契约生效** ⇒ 本条**不需要 ECDI 任何改动**（与 v1.1 归属反转的结论一致，本次补上此前缺的**边界**证据） |

★ **附：ECDI 自身测试未覆盖此门控**（值得记的框架事实）——ECDI 的 `WidgetTests.cpp:1356/1390`
覆盖的是**键盘**通道的 `IsEnabled` 门控（焦点/派发），`CollapsiblePanelTests.cpp:56` 覆盖的是
**不可见子树**的命中跳过；**「禁用控件不被鼠标命中」这一条没有直接用例**。本探针因此在
harness 侧补上了这份读数（属**评估数据**，不是要求 ECDI 补测试）。

★ **探针自身的已知副作用（条 57③：自己的仪器会污染实验）**：合成点击走真实派发链 ⇒
`Application::OnMouseButtonDown` 会 `SetFocusedWidget`（`Button::CanFocus()==true`）⇒ 输入框失焦
⇒ `TextBox::OnFocusLost` 停掉 caret 闪烁定时器（`TextBox.cpp:215-232`）。探针结束时**已复原焦点**，
但定时器重启带来 **±1~2 的 `paints`/`frames` 相位扰动**（实测 cot 夹具 0/0/−2/−1，随跑批时长变）。
⇒ **探针自身读数不受影响**；但**不要**拿「探针开」的 paints/frames 与「探针关」的做性能对比。

**原判（已作废，保留历史链条）**

> 原判：「两条出路 | ① harness 补 `PromptBar` 访问器（harness 侧可做）；② **ECDI `Button` 提供可绑定 `state` 的使能接口**（ECDI 侧能力）——需拍板走哪条」。
> ⇒ ★ **作废理由**：把「harness 未暴露按钮」误判为「ECDI 缺可绑定 `state` 的使能接口」。
>   实际上 `SetEnabled` 自 Phase 3 即在、`HitTest` 已门控——**出路 ② 系基于未核实的假设**（我只读了 `TextWidget.h` / `Label.h`，未打开基类 `Widget.h`）。

---

### REQ-05 ✅ GDI / GL 的 `LineHeight` 不一致（**真 ECDI 缺陷** → **Phase 31 已修复闭合**）

> ★★ 本条**不是** harness 用法问题——两条链对**同一字体**给出**不同的行高**，属框架内不一致。
>   发现来源：用户观察「**GL 的空比 GDI 长**」，据此做受控复现，**精确成立**。
>   ★ 这也是我本轮**唯一确认的 ECDI 侧缺陷**（REQ-01/03/04 经复核均已反转为 harness 侧）。

| 项 | 内容 |
|---|---|
| 原编号 | —（本轮新确认，用户提示触发） |
| 状态 | ✅ **已闭合**（Phase 31 修复；跨仓库实测确认，见下方「闭合记录」） |
| 修复版本 | ECDI **Phase 31** 批二（行高落地，commit `b2d8fa8`）· 收口 `103a2f0` |
| 现象 | 同一夹具、同一字体，**GDI 行距 = 14px，GL 行距 = 16px**（实测，见下） |
| 根因 | **两条 `LineHeight` 实现用了不同依据**：① GDI 侧 `GDITextMeasurer.cpp:287` 用 `TEXTMETRIC.tmHeight`；② GL 侧 `FontEngine.cpp:401` 用 FreeType 的 `face->size->metrics.height`。二者语义不同 ⇒ 同字体也产生不同行盒 |
| 放大效应 | `TextWidget::BuildTextLayout` 的 `totalHeight = lineHeight × lines.size()`（`TextWidget.cpp:308`）⇒ **行数越多，两后端的总高差越大**（8 行时差 16px） |
| 建议方向（未实现） | 二选一：① 让 FT 侧与 GDI 的 `tmHeight` 语义对齐（如用 `metrics.ascender − descender + 外部行距`）；② 或在 `TextMeasurer` 契约里**钉死**「行高 = 哪种口径」，两条链都按它实现 |

**★ 闭合记录（2026-10-13 回写，跨仓库端到端）**

| 项 | 内容 |
|---|---|
| 修复 | ECDI **Phase 31**：`LineHeight` 口径统一为 `(ascender − descender + lineGap) × fontSizePx / unitsPerEm`——GDI 侧改走 `GetOutlineTextMetricsW` 的 `otmMacAscent − otmMacDescent + otmMacLineGap`，FT 侧 `face->size->metrics.height` 本就是该口径 ⇒ **两侧各自独立算出同一个数** |
| 契约 | `TextMeasurer.h` 已写明「两后端应给出同一个数；允许差异仅**舍入 ≤1 px**（@96dpi），**该容差是契约的一部分**」 |
| Phase 31 复跑读数 | SimSun@14：**GDI 16.00 / FT 16.00，差 0.00** ⇒ 详设明写「**不是**『被允许』，是**差异已消除**」 |
| ★ 本仓库独立复测（2026-10-13，库 `a1f843f`） | `cjk_many`：**GDI 8 带 / 16px，GL 8 带 / 16px**（修复前 GDI 为 1 团 / 14px）· `mix_cjk`：两后端均 4 / 16px · `cjk_short`：两后端均 1 带 |
| 判定 | ✅ **闭合**——两处独立复测（ECDI 侧探针 + 本仓库截图像素测量）**互相印证** |

★ **本条的意义**：这是账本中**第一条完整走完「登记 → ECDI 修复 → 跨仓库验证 → 回写闭合」闭环**的需求。
**决定性取证（高对比探针 + 逐行剖面，2026-10-08）**

★ **方法改进（本轮教训）**：默认暗色主题下，像素探针要与卡片圆角 / 1px 描边 / 滚动条抢阈值，反复产出「行段 = 0」「跨度到 899」这类**仪器噪声**，并曾据此误判产品行为。★ 故本轮为 harness 增加**探针专用**开关 `--probe-contrast`（白底 + 黑字 + 零描边，默认关闭、既有路径逐位等价）⇒ 测量退化为**平凡问题**。

夹具 `probe-cjk-cjk_many.jsonl` = **8 段「测试中文第一段」**（各段均短、含 7 个 `\n`）：

| 后端 | 文本行段数 | 行高 | 行距 | 剖面形态 |
|---|---|---|---|---|
| **GDI**（默认） | 未分离（y100–211 连成一段，h=112） | — | **14 px** | ★ **完美周期 14px** 的密度序列（n 在 9→37→9 循环 8 次）——8 行**紧排至零间隙** |
| **GL** | **8 行** | **12 px** | **16 px** | 各行独立分离，间距均匀 |

★ **判定（两后端各自的证据）**：
- GDI：`hc-gdi-many.png` 的 y 剖面是**严格周期 14px** 的重复图案 ⇒ 确为「8 行 × 14px 行距」的无间隙紧排，**不是**渲染丢失。
- GL：8 段清晰分离、行距恒 16px ⇒ 换行与行距都正常。
- ⇒ **差异 = 行距 14 vs 16**，与 `\n` 消费无关（两者都消费了 `\n`），**根因是 `LineHeight` 口径不同**。

★ **对 `REQ-01` 的补充**：GDI 下 8 行紧排至零间隙，**视觉上即「文字糊在一起」**——
这意味着 REQ-01 的「短文本丢换行」在 GDI 上还**叠加**了本条，「不可读」的观感由此而来。两条需分别修。
---

### REQ-06 跨后端基线偏移——**已裁决结案（2026-10-17，③「记为已知现状」）**；★ 原登记读数「拉丁 +1 · CJK +2」经 112 格复测**订正**

> ★ 本条**不是**我发现的，而是 **ECDI 在 Phase 31 收口时主动登记**（详设 **O10** / §13.2），
>   并明确判为「**既有（pre-existing），非本 Phase 引入**」；**修复方向留给决策**，未折进 Phase 31。
>   本账本收编它，是为了让「给 ECDI 的需求」这一侧也有对应条目。

| 项 | 内容 |
|---|---|
| 原编号 | ECDI Phase 31 详设 **O10**（§13.2） |
| 状态 | ✅ **已裁决结案（2026-10-17）**——ECDI 侧走 O10 候选 **③「记为已知现状」**（用户拍板）；本账本同步结案。**★ 但结案的前提是「不存在既等价又不改默认观感的修法」，与在案理由不同**，见下「复测订正」 |
| 现象（原登记，**保留为历史引用**） | 同一字形、同字体/字号/DPI/位置，两后端的**字形栅格包围盒**垂直方向差：**拉丁 +1 px · CJK +2 px** ← ★ 该读数出自 **SimSun@14 单点**，非类别规律（订正见下） |
| 根因（已定位） | ① **基线差 1 px**：GDI 用 `pos.y + tmAscent`（`tmAscent = 12`），GL 用 `floor(pos.y) + floor(FontEngine::Ascent)`（FT `metrics.ascender = 13`）⇒ GL 低 1 px；② **CJK 再叠 1 px**：字形 bearing 取整差（`测` 的 GDI 字形顶 +0 vs GL +2） |
| 判为 pre-existing 的三条独立理由 | ① GL 基线表达式**本 Phase 未动**；② 它**不查 `LineHeight`** ⇒ 与本 Phase 的改动**正交**；③ **改动前的 GL 截图与新截图逐位相同** |
| 本仓库实测印证 | 同一夹具 `cjk_short`：GDI 首行 y102–115 / GL y104–115 ⇒ **差 2 px**，与 CJK +2 px 的记录一致（且是**首行偏移**，非行距） |
| 待裁决（原表述，保留引用） | ECDI 侧原话：「修正它会**全局改变 GL 文本位置**」⇒ 修与不修需拍板（本轮**未改任何 ECDI 代码**）　★★ **2026-10-17 复测：此话方向反了**——统一到 `round(hhea·px/upem)` 时 **GL 只动 ≤1 px，GDI 最大动 −13 px**（疼的是默认可见链） |

★ **与 `REQ-05` 的关系**：两条**独立**——`REQ-05` 是**行距**（14 vs 16，已闭合）；本条是**基线/包围盒**（±1–2 px，仍开放）。
  不要把本条的残留误读成「`REQ-05` 没修好」。

★ **2026-10-17 裁决前置取证（14 字体 × 8 字号 = 112 格，DPI 96）**——装置与读数：
  ECDI 侧 `.workbuddy/spike/p36-baseline/`（本仓库副本 `.scratch/p36-baseline/`）；
  ECDI 侧结论落点 = **Phase 31 详设 §13.6（v1.5）**；roadmap-deferred **#69 / v2.31**；desktopnest-roadmap **v1.25**。

| 项 | 在案（2026-10-13 收编时） | 112 格实测 | 判定 |
|---|---|---|---|
| 拉丁 +1 px | SimSun@14 的 `H`/`g` 各 +1 | 默认字体确为 {0, +1}；跨字体 **GL−GDI 基线差 ∈ [−13, +3]** | ⚠️ **单样本外推** |
| CJK +2 px | `测`@14 = +2 | 同字体 `测` 在 12/16/20/24/32/48/64px = **+1/+1/+1/0/0/+1/0** | ⚠️ **字号现象，非类别规律**（14px = `Font::size` 默认值，恰是我方夹具采样点） |
| 80/112 格 | — | Δ ∈ {0, +1} | 常态是「不差」或「差 1」 |
| 符号方向 | 隐含「GL 恒低 1 px」 | Calibri **−13** / Consolas −11 / Courier New **+3** / YaHei **+2** ⇒ **翻转** | ★ 「统一上移 N px」**结构上不可行** |
| 包围盒合成式 | 「基线差 + bearing 取整」（未给方向） | 顶差 = **基线差 − bearing 差**（`bearingY` 向上为正）；默认字体 **23/24 格 ≤ ±1**，唯一超差 = `测`@14 | ★ 本仓库 `REQ-05` 之外的 T31-8 ±1 判据**原判过严**，现订正 |
| FT ascent 来源 | 未测 | `face->ascender == hhea.Ascender` **112/112**、`round(FT) == ceil(hhea·px/upem)` **110/112** | ★ **GL 侧事实上就是 hhea 口径** ⇒ 与 `REQ-05` 已统一的行高口径**同源不同维** |
| GDI `tmAscent` 可复算性 | 未测 | `== round(hhea)` 62 · `== round(usWin)` 85 · `== otmMacAscent` 69（均 /112） | ★ **无一式可普遍复算** ⇒ ECDI 候选①「FT 无对应量」**成立**，GL **无法**精确追平 |

★ **量具校准（条 76，三件全过才下结论）**：`FontEngine::Ascent` 与直读 FT **0/112 失配**；px=14 的 GDI `tmA/tmD/tmH` 与 Phase 31 归档读数（`p31-lineheight/readout_px14.txt` · `p31-glbbox/cjk_diag.txt`）**0/10 字体不符**；包围盒合成锚 `测`@14 = 基线 +1 / bearing −1 / 顶 +2，与 `bbox_readout.txt` 逐项吻合。
★ **一处判据错误自我纠正（值得记）**：首版拿 **GDI `tmAscent` 与 FT `ascender` 数值相等**判「两侧同一 face」——**错**，两个不同量具的读数会偶然相同（GDI Consolas tmA=13 恰等于 FT 侧 **回退后 SimSun** 的 ascent=13.00）。改判据 = **串宽**（同单位同语义，DPI 96 下 DIP↔px 1:1）+ 正对照（同 face 两侧同宽：真 Consolas 152 / SimSun 133）。
★ **未测（条 105 边界，登记不伪造）**：跨 DPI；CFF 字体（本机样本全 TrueType + `head` bit3）；像素级逐字号包围盒（T31-8 那种 `GetPixel`/`glReadPixels` 形态）；非中文语言环境字体集。

| 项 | 内容 |
|---|---|
| ★ **结案理由（与在案理由不同）** | 不是「GL 代价太大」，而是 **① GDI 侧不可复算 ⇒ GL 追不平 ② 两侧统一到共同源 ⇒ 代价落在默认 GDI 链（最大 −13 px，违反条 95⑤）** ⇒ **不存在既等价又不改默认观感的修法** |
| ★ **本仓库的读数的连带订正** | 本轮取证同时暴露 `REQ-09`（`Font::family` 双链语义矛盾）⇒ **我方既有 GDI↔GL 跨后端对比读数中，凡设置过 `family` 的行，两后端渲染的并非同一 face** ⇒ 那些对比的「同一内容」前提不成立，须按 `REQ-09` 标注解读（**未追改历史 evals**） |

---

### REQ-08 环形进度条（圆弧原语缺失）——**本项目成为 `DrawArc` 的首个真实消费者**

> ★★ **触发（2026-10-14）**：用户要求「加一个上下文已占用的百分比显示，**可以是圆环的**，
>   也可以是一个百分比条，**圆环 ECDI 应该还没有实现**」。
>   ★ 按项目铁律，判「ECDI 缺能力」**必须先查基类公共头 + ECDI 源码仓**（前 4 次归属反转全栽在这上面）
>   ⇒ **本轮先查了**，结论：**用户猜对了**。

| 项 | 内容 |
|---|---|
| 状态 | ✅ **闭合（2026-10-16）**——ECDI **Phase 33** 落地 `DrawArc`（框架首个曲线原语，D5 裁决**纯虚**，`RenderingBackend.h:110`）与其首个控件消费者 `RingProgressBar`；harness 以**首个真实消费者**身份兑现（右栏「用量」卡上下文行 = 44px 环），机读证据 `commandMix.arc = 350 > 0` |
| 需求形态 | **环形**（圆环）进度条——按角度画部分环，用于「上下文占用百分比」 |
| ★ 能力侧事实（**已查源码仓**） | ① **绘制原语只有 6 个**：`DrawRect` / `DrawText` / `DrawLine` / `DrawRoundedRect` / `DrawImage` / `DrawFocusRect`（`Render/RenderingBackend.h:72-115`）；② **GL 侧同样 6 个**（`GLRenderer.cpp:208-496`）；③ **命令 variant 全集亦无**（`Render/RenderCommand.h:93-96`）⇒ 两后端 + 命令层**三处一致地没有圆弧** |
| ★ 但「实心圆/圆环」**能画** | `DrawRoundedRect` 的半径契约是 `[0, min(w,h)/2]` 钳制（`RenderingBackend.h:93`；`GDIBackend.cpp:117`）⇒ 传 `w==h` + 半径 `w/2` 即**实心圆**；两个同心实心圆叠出**圆环**。⇒ 缺的不是「圆」，是「**按角度的弧**」 |
| ★★ **ECDI 自己早已登记为挂账项** | 不是我"发现"的：`roadmap-deferred.md:5` 明确列出 **`DrawArc`** 为挂账项；`phase9.6-progressbar-requirements.md:27` 写明「**indeterminate（不确定进度的循环动画 / 转圈）不在 v0.1 范围——它需要 `DrawArc` / `PushTransform`（渲染能力挂账）**」；`phase24-line-antialiasing-requirements.md:120` 的 **N1** 记「曲线/圆弧抗锯齿 = **K12 原语不存在**」 |
| ⇒ **本条的真正价值** | **本项目是 `DrawArc` 的「首个真实消费者」**——ECDI 的挂账理由是「**无消费者不动**」（`phase9.6-animation-requirements.md:36`「DrawArc / PushTransform 继续记账：**无消费者不动**」）。现在消费者出现了（上下文占用环 / 将来的 spinner），**挂账的前提变了** |
| 本轮处置 | **不阻塞**：改用**百分比条 + 百分比文本**（`ProgressBar` 现有能力即可兑现，已实测）。环形**待 ECDI 裁决** |
| ★ **闭合（2026-10-16）** | ECDI **Phase 33** 交付 `DrawArc` + `RingProgressBar`（静态库接入 `third_party/ecdi`，2026-10-16 19:12）。harness 兑现：右栏「用量」卡上下文行由 16px 条改为 **44px 环**（`gui/src/App.cpp` 构建处 + `RefreshHud` 更新处）。**两个实现期发现**：① 轨道色**不能**沿用 `kTrackBg`——它与卡片底色 `kPanel2` **同值**（`Theme.h:82`），环会看不见整圆 ⇒ 改用 `kLine`；② `RingProgressBar::SetStyle` 的头**未**声明「覆盖后 Invalidate」（对比 `Panel.h:39` 明确写了）⇒ harness 只在**颜色档位翻转**时补一次 `RequestRepaint()`（每 tick 补会刷爆 `invalidations`，污染评估读数——条 57③）。**机读证据**：`commandMix.arc = 350 > 0`（`.scratch/ring-verify.json` / `center-verify.json`）；`invalidations` 仅 +1（17 vs 基线 16）。**用户视觉确认**：环与数字正确 |
| ★ 附带闭合（同日） | 顶栏胶囊文字贴左：`ECDI::Label` **不** override `CalculateTextPosition` ⇒ 基类默认左对齐；**全库只有 `Button`** override 做水平对齐（`Button.cpp:110-124`，Phase 30 △6 三态）。harness 用 ECDI 预留扩展点（`TextWidget.h:74-78`「对齐策略虚方法，不写死」）派生 `uikit::CenteredLabel` 兑现，未向 ECDI 提需求 |
| 若 ECDI 要做，最小可用面 | 一个 `DrawArc(center, radius, startAngle, sweepAngle, width, color)`（或 `DrawRing`）即可覆盖：环形进度 / spinner / 仪表盘。★ **不必**一次做全（椭圆/贝塞尔等仍可挂账） |
| 反面做法（**不建议**，但须记录） | 应用侧用 `DrawRoundedRect` 拼大量小段逼近弧——**是 workaround 不是能力**：段数决定平滑度、命令数随精度爆炸（与评估指标 ① 直接冲突），且抗锯齿无从谈起 |

★ **归属纪律**：本条**不是**「harness 用法问题」（不像 REQ-01/03/04）——`DrawArc` 确实**不存在**，
且**ECDI 自己也是这么登记的**。⇒ 这是本项目**第 5 条**候选 ECDI 需求，但**第 1 条**
「先查了源码仓、且 ECDI 已自认挂账」的条目（前 4 条全部反转为 harness 侧）。

---

### REQ-09 `Font::family` 双链语义矛盾——**不存在两侧都对的单一写法**（★★ 本轮唯一「同一字符串在两个后端渲染不同字体」级缺陷）

> ★ **本条不是 ECDI 隐瞒的缺陷**：Phase 26 详设 §9 **L1** 明确登记过「两侧语义不完全对等」，属**已登记的延迟设计**。
>   本轮的贡献 = 把它**测成了可判定的形状**（双向失效 + 两个真实消费者）， ⇒ 按条 91 五态表，**「出现登记中的消费者」触发条件达成**。
> ★ **归属判据（条 1.1 铁律）**：已查 ECDI 源码仓 `D:\Project\ECDI\ECDI\src`，且做了**只改一个自变量（family 写法）**的受控对照 ⇒ 不是「只看头文件的推断」。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮取证新立；ECDI 侧对应 **`roadmap-deferred` #69**） |
| 状态 | ✅ **已闭合（2026-10-17，当日修复 + 验收）**——ECDI 侧 `Win32FontSource::ResolveFile` 补**族名反查**（注册表；禁子串匹配；**文件名直查保留且优先**）⇒ 同一 `Font::family` 两条链落到同一张 face。**ECDI 侧 `roadmap-deferred` #69 已标已修复**；回归 `ecdi_tests` **465/465** |
| 归属 | **ECDI 侧**（`Win32FontSource::ResolveFile` 把 `family` **当文件名**直查，`Win32FontSource.cpp:113`；GDI 链把同一串送 `LOGFONTW.lfFaceName` = **族名**语义，`GDIBackend.cpp:1362` / `GDITextMeasurer.cpp:71`） |
| 现象 | **同一个 `Font::family` 字符串，两条链解析到两个不同 face**：族名 ⇒ GDI 命中、**GL 静默回退默认 face（SimSun）**；文件名 ⇒ GL 命中、**GDI 回退宋体** |
| ★ 实测读数（px=14 / DPI 96，可比量 = **串宽**） | `"Consolas"` ⇒ GDI 152（真 Consolas）/ GL **133**（与 `"simsun.ttc"` 逐格相同 = 回退）· `"consola.ttf"` ⇒ GDI **133**（= 宋体）/ GL 152 · 正对照：同一 face 两侧同宽 152 vs 133 |
| ★ 覆盖面 | 常见族名 **5/5 在 GL 链回退**（`Courier New` / `Arial` / `Segoe UI` / `Microsoft YaHei` / `Cascadia Mono`，读数全 = SimSun）；同串在 GDI 链 **5/5 解析到真实 face**（`GetTextFaceW` 实测） |
| ★ 两个真实消费者 | ① **本仓库**：`gui/src/Theme.h:97` 与 `gui/src/UiKit.h:26` 的 `kMonoFamily = "Consolas"` ⇒ **GL 运行时等宽从未生效**（日志/ID 列被排成比例字体，换行宽度与 GDI 不同）；② **ECDI 自家示例**：`examples/ModelProbe/ModelProbe.cpp:474/779` 按 Phase 26 §11.7 勘误写成 `"consola.ttf"` ⇒ 在**默认 GDI 链**上这两行现在显示宋体（★ 即「B 的修法只对 GL 一侧有效」） |
| ★ 对我方评估的影响 | **所有设置过 `family` 的 GDI↔GL 跨后端对比读数，其「同一内容」前提不成立** ⇒ 历史 `evals/*.json` **不追改**（条 61②），但解读须带此标注；`REQ-06` 的基线差反而是小头　★★ **2026-10-17 修复后**：新取的 GL 读数与 GDI 已同 face ⇒ **今后对比可用**；**历史 `evals/*.json` 仍不追改**（条 61②），但解读须带「GL 侧当时等宽失效」的标注——★ 其中凡与**等宽行**相关的读数（日志/ID/JSON 列）差异会被高估 |
| ★ ECDI 侧修法候选 | ⓐ `Win32FontSource` 补**族名→文件名反查**（`EnumFontFamiliesExW` + 结果缓存，**零公共 API 增量**，沿 Phase 26 `familyCache` 先例）· ⓑ `RenderServices` 注入按族名的 `FontSource`（动装配）· ⓒ 契约显式禁止族名（则本仓库须改名，且 ②/两侧不可兼得仍在）⇒ **倾向 ⓐ**：一处改动即让两条链同解，且**消费者零改动**（条 95⑤） |
| ★ 我方本轮处置 | **不改代码**（`kMonoFamily` 保留族名并加注释说明两侧差异）。★ 理由：**改成 `"consola.ttf"` 只会把失效从 GL 侧搬到 GDI 侧**——单字符串喂不了两条链，属**契约层矛盾，非用法问题**（区别于 `REQ-01/03/04/07` 的四次归属反转）　★★ **2026-10-17 更新：该判断已被 ECDI 的修复取代** —— 修复落地后**族名写法在两条链都对**，故我方 `kMonoFamily = "Consolas"` **无需改动即恢复等宽**（GL 侧现在真的拿到 Consolas 而非回退 SimSun）。★ 仍保留此行的历史判断，因为它正确记录了「选文件名只会把失效搬到 GDI 侧」这一当时的边界 |
| ★ 取证 | ECDI 侧探针 `t36_family_probe.cpp` / `t36_single_family.cpp`（只改一个自变量 + 正对照），读数 `family_readout.txt`；本仓库副本 `.scratch/p36-baseline/`；结论落点 = Phase 31 详设 **§13.6** ④ + roadmap-deferred **#69** |

★ **与 `REQ-06` 的关系**：同源取证、**不同问题**——`REQ-06` 是「同一 face 上两后端的基线差」（已结案为已知现状）；
  本条是「**两后端拿到的根本不是同一个 face**」。后者会污染前者的读数，故本条**优先级高于** `REQ-06`。

---

### ★ 三条 harness 侧闭合记录（2026-10-13 落地）

> 三条均为**纯 harness 侧**修复（不需要 ECDI 任何改动）；已实现、已重建、已实机复测。
> 改动文件：`gui/src/App.cpp` · `App.h` · `main.cpp` · `PromptBar.h` · `PromptBar.cpp` · `Theme.h`。

| 条目 | 修复内容 | 实机复测证据 |
|---|---|---|
| `REQ-01` | `needWrap` 增加「**文本含 `\n`**」条件（`App.cpp`）——此前只看宽度 ⇒ 含 `\n` 但不超宽的块走 Phase 28「单条 DrawText、布局引擎零接触」路径 ⇒ `\n` 不被消费 | 夹具 8 段 CJK：**GDI 8 带 / 16px**（修复前 **1 团 / `\n` 被吞**）· GL 同为 8 带 / 16px ⇒ 两后端逐项一致 |
| `REQ-02` | `block_end` 分支放宽到 **`tool_call`**（原条件只认 `tool_result` ⇒ tool_call 的 `summary` 无处落地） | `probe-titles2`：`read_file · 已发起` / `edit_file · 已发起` / `bash · 已发起`（修复前只有工具名）；`tool_result` 侧 `bash · 42 行` / `todo_write · 失败 · 超时` **未回归** |
| `REQ-04` | `PromptBar` 暴露 `sendButton()` / `stopButton()` + 新增 `SetStateDrivenEnabled(bool)`；`App::RefreshHud` 按 `state` 驱动（`active = running \|\| waiting`） | **判据 A5 兑现**：`running` 态发送**灰化**、停止可点；`idle` 态发送**蓝色实心可点**、停止灰化（两张截图对照） |

★ **`REQ-04` 的附带结论**：ECDI **有意不做** disabled 绘制（YAGNI——`modelprobe-p1-preliminary-design.md:130`「disabled 视觉灰化：demo 层处理」）
⇒ 「变灰」是**调用方责任**。本次在 `Theme.h` 增 3 个禁用态令牌（`kBtnDisabledBg/Ink/Line`），`PromptBar` 显式切换 ⇒ **仍不需要 ECDI 改动**。

★ **本轮的 harness 侧自伤（已修）**：`edit` 工具会按**无 BOM** 重写文件 ⇒ `App.cpp` / `App.h` / `main.cpp` / `PromptBar.h` 一度全部丢失 BOM（4 个文件**缺 BOM 且含中文** = **已确认缺陷**，MSVC 按 CP936 读会触发 C4819）。已按条 70 判据补齐（`new == BOM + old`，行数不变），全目录复查缺 BOM 数 = **0**。

### 思维链与未知 `kind`（2026-10-13 晚新增）

> 触发：用户提出「harness 这种 agent 用**开源模型**时是会传**思维链**的」。
> 取证：`.scratch/cot-findings.md`（现象）· `.scratch/D-attribution-conclusion.md`（归属）·
> 探针夹具 `.scratch/probe-kind.jsonl` · 截图 `.scratch/kind-probe.png`。

#### ① 协议空白：§3.3 没有思维链的位置 —— ✅ **已由协议 v2 闭合（2026-10-14）**

> ★★ **用户定调**（2026-10-14）：「思考链的那个 reasoning？那必然要作为正式存在的啊，
> 不过默认应当不渲染，显示『思考中』这几个字，加一个向下的不带柄的箭头，点击展开后箭头变为朝上的」。

| 项 | 内容 |
|---|---|
| 现象 | §3.3 的 `kind` 枚举 = `user` / `assistant` / `tool_call` / `tool_result` / `system` / `error` **六个值**，**没有思维链** |
| 严重性 | 未知 `kind` **不被任何不变量覆盖**——I5 只管信封层的 `t`（子判别符不归它管）⇒ 实测 `unknownTypes=0`、`validate.py` **退出码 0**。一份满屏思维链的夹具会**全绿通过** |
| 性质 | **协议空白**（不是 ECDI 缺陷，也不是单纯 harness 缺陷）⇒ 与协议 §8.5 自己的纪律冲突：「一个恒成立的负向用例，与没有这个用例无法区分」 |
| **处置（v2 已落地）** | 协议升 **v1 → v2**：① §3.3 增 `kind:"reasoning"`（**一等公民**）；② §5 增不变量 **I8**（未知 `kind` 必须中性渲染 + 显式提示）；③ §7 增渲染形态 = **默认收起**，标题 `▼ 思考中`，点击展开箭头翻 `▲`（再点回 `▼`）；④ §9 版本表 + GUI 侧**真正实现**「声明版本高于上限 ⇒ 拒绝连接」（该条款自 v1 就在文档里，**从未实现过**，v2 才让它开始起作用） |

★ **为什么升主版本**：新增 `kind` 本身是「只增」（旧 GUI 按 I5 忽略，不崩不丢），但 **I8 是
一条新义务**——它约束「遇到不认识的东西**必须**怎么做」，属**契约面扩大**，非纯数据追加。
★ **v1 GUI 的向后表现（已实测）**：把 `reasoning` 当未知 kind ⇒ 中性可见气泡，**不崩、不丢内容**，只是**不折叠**。
★ **I8 与 `reasoning` 的边界**：`reasoning` **不在** I8 管辖内（它是已知值，有专门形态）；
I8 管的是 `thinking` / `analysis` / 拼错的名字 / **缺失字段**——实测 `--kind unknown` 仍 `unknownKinds=8`。

##### 折叠省下多少：**首次测得**（此前"测不出来"）

> 首版此处写「折叠省多少**测不出来**」——当时 harness 对 reasoning **没有折叠能力**
> （未知 kind 一律中性渲染成可见气泡，正是为了不伪装成工具卡）⇒ **不存在可比的折叠态**。
> v2 落地后折叠态真实存在，该读数**现在可以测了**。

| 项 | 内容 |
|---|---|
| 受控性 | `--kind` **只改 kind 字段**，正文**逐字节相同**（实测 612 条 text 载荷全等）⇒ 单变量对比 |
| 判据 | `commandMix.text / probeFrames`（**帧数无关**） |
| 实测 | `reasoning`（收起）**65.0** vs `unknown`（可见）**141.0** vs `assistant`（可见）**139.2** ⇒ 收起**少画约一半**文本命令（**2.14×**） |
| 结论 | 折叠**确实省下绘制量**（这是评估指标 ① 的直接读数）；★ 但**测量成本不省**——A-2 已证收起态仍布局测量一次（C29-10 缓存使其不逐帧重建） |
| ★ 自我修正 | 首版断言「三种 kind 的 commandMix 极差仅 145 ⇒ 无系统性差异」——**该断言已失效**（v2 后极差 4147）。原因：当时三种 kind 都是**可见**气泡，差别只在颜色令牌；现在 reasoning 收起 ⇒ 绘制量真的变了 |

#### ② ★★ 候选 ECDI 需求被复核**否决**（第 4 次归属反转）

| 项 | 内容 |
|---|---|
| 候选条目 | 曾拟记 `REQ-07`：**ECDI 的 `CollapsiblePanel` 收起后，其内容仍被逐帧测量** ⇒ 请框架提供「跳过不可见内容布局/测量」的能力 |
| 复核结论 | ❌ **不成立，不登记**。归属 = **harness 侧** |
| 判据（ECDI 侧**已有**正确契约） | `ECDI/src/Layout/VerticalLayout.cpp:37` `if (!child->IsVisible()) continue;`（注释明写 `// C-VIS-1：隐藏子不进任何统计`）· `HorizontalLayout.cpp:37,69` · `GridLayout.cpp:28` · `ListLayout.cpp:23` **四处同款** |
| 判据（harness 侧失效点） | `gui/src/App.cpp:1123` `r.label->AutoSize();` **无条件执行**（不看 `r.expanded`、不看内容 `IsVisible()`）；且工具卡 `panel` **未 `SetLayout`**（子节点手动摆位）⇒ 没有任何 Layout 会替它跳过 |
| 实测 | 受控 4 档剂量（body 40/400/4000/8000 字符，**全部默认折叠**）：`measureChars` = 15,796 / 19,875 / 139,043 / 454,946 ⇒ **1.00× / 1.26× / 8.80× / 28.80×**，帧数基本持平 ⇒ 测量确未被跳过 |
| ★ 方法教训 | 这是本项目**第 4 次**「候选 ECDI 缺陷 → 复核后判 harness 侧」（前三次 = `REQ-01`/`REQ-03`/`REQ-04`），**成因同一**：没先查 ECDI 源码仓。本轮**先做了这一步**，故没有给 ECDI 记一条不存在的需求 |

★ **旁证（值得记的框架事实）**：`Widget::ArrangeInternal()`（`Widget.cpp:272-286`）**自身不查 `IsVisible`**——它无条件递归子节点；跳过与否**完全由各 Layout 决定**。而 `Paint`（`Widget.cpp:363`）与 `HitTest`（`Widget.cpp:188`）**都有**门控。⇒ 「Arrange 不门控」是**有意分工**，不是疏漏；判断这类问题时**不能只看 `ArrangeInternal`**。

#### ③ 已落地的 harness 侧处置（`A-1`）

| 改动 | 位置 |
|---|---|
| 未知 `kind`（含**缺失字段**）不再落工具卡 ⇒ **中性气泡**（琥珀描边 + 明写 `[未知 kind: xxx]`，正文原样保留不丢内容） | `App.cpp` `CreateBlockRow` 新增分支 |
| 前缀文本**单一真相源**（建行与 delta 更新同源——首版两处各写一份，delta 一到就擦掉前缀，实测踩中） | `App.cpp` `UnknownKindText()` |
| `Transcript::UnknownKinds()` 计数 + `Block::unknownKind` 标记；已知 kind 判据收进 `Transcript::IsKnownKind()`（与渲染分支同源） | `Transcript.h` / `Transcript.cpp` |
| 摘要新增 `unknownKinds` 字段（与 `unknownTypes` **并列不合并**：前者是 `kind`，后者是 `t`） | `App.cpp` `WriteSummary` |
| 右栏「协议健康」**独立一行**显示未知 kind（并进上行实测被裁切） | `App.cpp` `BuildInspector` / `RefreshHud` |
| 可执行规格增检查：未知 `kind` 记 note（**不判违规**——I5 前向兼容精神：忽略并记录，不是拒绝） | `protocol/validate.py` |

★ **实测复验**：探针 7 块（1 正常 assistant + 4 未知 kind + 2 真工具块）⇒ 四个未知块**全部中性渲染**（截图可辨），真工具卡 `read_file · 已发起` / `read_file · 42 行` 不受影响；摘要 `unknownKinds=4`、`unknownTypes=0`；`validate.py` 对全库 5 份夹具仍 **exit 0**，对探针夹具给出 note。

★ **首版两个 bug（自测发现并修）**：① `!block.kind.empty() && !IsKnownKind(...)` 漏掉**缺失 `kind`** 的块（它照样掉工具卡）⇒ 改为 `!IsKnownKind(block.kind)`；② `validate.py` 的 `sorted(...)` 在字段缺失时对 `None` 排序 ⇒ `TypeError` 崩溃，已加 `is not None` 守卫。

---

## §3 已闭合条目（保留历史链条）

★ 依条 30 / 条 61②：**历史记录不改写**——这些条目曾真实存在，闭合后在原地标注结论，不删除。

| 原编号 | 当时的发现 | 闭合依据 |
|---|---|---|
| `F1` | autosize 对照组可能不成立：`TextWidget::GetPreferredSize` 是单行测量，行高恒定 | ✅ **wrap 路径已闭合**：`TextWidget.h:58` 记「`wrap == true` 且有有限宽 ⇒ **布局驱动** `{GetWidth(), totalHeight}`」（Phase 29 批二）。非 wrap 下单行语义**仍如此，但那是设计**（Phase 28 原路径） |
| `F2` | 多行文本高度测量缺失：tool 展开体 `bodyH = 单行高 + 8` ⇒ 必然裁切 | ✅ **已闭合**：`App.cpp:1077` 现为 `bodyH = r.label->GetHeight() + 8`，而 wrap 下 `GetHeight()` 即 `totalHeight` |
| `F4` | I7 的 `for` 引用关系不可见（规格 §5.2 要求，实现无渲染） | ✅ **已闭合**：`App.cpp:768` 用 `FindBlock(block->forId)` 取工具名渲染进标题（`bash · 42 行`）——**超出**协议 §7 的「标题 = ok/summary」要求 |
| `F6` | `state` / `usage` 完全不渲染（`usage.turn/input/output` 数据被丢弃） | ✅ **已闭合**：`App.cpp:1289-1306` 渲染用量进度条 + `tokens N / M`（`App.cpp:1335-1340`） |
| ★ `REQ-01` | 含 `\n` 的短文本丢换行（曾误判为 ECDI 缺陷） | ✅ **harness 侧修复**（2026-10-13）：`needWrap` 增加「含 `\n`」条件；复测 GDI/GL 均 8 带 / 16px |
| ★ `REQ-02` | `tool_call` 的 `summary` 被吞（P1 期 F3 复现） | ✅ **harness 侧修复**（2026-10-13）：`block_end` 分支放宽到 `tool_call`；`read_file · 已发起` 已显示 |
| ★ `REQ-04` | 按钮使能缺 API（曾误判为 ECDI 侧） | ✅ **harness 侧修复**（2026-10-13）：`PromptBar` 暴露按钮 + `SetStateDrivenEnabled`；**判据 A5 兑现** |
| ★ `REQ-05` | GDI / GL 的 `LineHeight` 不一致（GDI 14px vs GL 16px）——本项目**首条闭环需求** | ✅ **Phase 31 修复**：口径统一为 `(ascender − descender + lineGap)`；SimSun@14 两侧均 **16.00**；本仓库独立复测两后端均 **8 带 / 16px**（详见 `REQ-05` 的「闭合记录」） |

---

## §4 harness 自身待办（不动 ECDI）

| 原编号 | 待办 | 现状 |
|---|---|---|
| `F7` | `truncated` 夹具实际**以 LF 结尾** ⇒ `ReplaySource` 的「缺尾 LF」分支**零覆盖**（协议 §8.5 自己警告的形态） | 🔄 **仍成立**（2026-10-08 复核：末字节 = `0x0A`）⇒ 需补一份**真缺尾 LF** 的夹具，或改现有夹具 |
| `F8` | evals 数据卫生（3 项） | 🔄 **仍成立且扩到 5 项**，见 `MEMORY.md`「待澄清的数据卫生问题」 |
| ★ `A-1` | **未知 `kind` 的中性渲染 + 记账**（`reasoning`/`thinking`/缺失 `kind` 曾静默长成「等待结果」工具卡，与未完成的 `tool_result` 无法区分） | ✅ **已落地并实机复验**（2026-10-13）：`App.cpp` 中性分支 + `Transcript::UnknownKinds()` + 摘要 `unknownKinds` 字段 + 右栏独立行 + `validate.py` 记 note。探针 `probe-kind.jsonl` 四个块全部中性渲染，`unknownKinds=4` |
| ★ `A-2` | **折叠态仍在全量测量**（`App.cpp:1123` 无条件 `AutoSize()`）：折叠卡里的长文本每帧照样测量 ⇒ 为看不见的内容付全部成本 | ❌ **已复核否决（2026-10-13 晚）** —— **原判收回**：那条「8.80×」是**同时长下两个不同长度**的对比，差值**全在一次性布局建立成本**上，不是逐帧浪费。实测分解 `chars = a + b·frames`：body=4000 ⇒ a=123,081 / **b=504**；body=40 ⇒ a=138 / **b=502** ⇒ **每帧成本与文本长度无关（b 之比 1.00×）**，ECDI 的 C29-10 缓存**按文档生效**。⇒ 修法（按需重测）实测**零收益**，**已完整回退**（代码骨架与 HEAD 逐行相同）。取证 `.scratch/A2-recheck-conclusion.md`（2026-10-13 受控 4 档剂量实测：body 40→4000 字符，`measureChars` 15,796→139,043，**8.80×**）⇒ 属 harness 侧，修法见 `.scratch/D-attribution-conclusion.md` §五 |
| ★ `C-1` | **思维链评估夹具**（`fixtures/cot/`）：开源推理模型的 CoT 是「长 + 流式 + 本应折叠」的独立负载画像，既有夹具都不覆盖 | ✅ **已落地**（2026-10-13）：生成器 `gen_cot.py`（`--turns/--thinking-chars/--kind/--emit-empty`）+ 入库基线 `cot-session.jsonl`（44 块 / 934 deltas / 1044 消息 / 172,868 B · sha256 `2b8cb42f…` 可复现）+ PROTOCOL §8.4 登记。读数见 `.scratch/C-cot-eval-conclusion.md` |

---

## §5 载体与维护

### 5.1 同一事实的其它载体（订正时须一并回扫）

| 载体 | 登记内容 |
|---|---|
| `docs/ecdi-capability-requests.md`（本文件） | **唯一账本**：能力需求与归属判定 |
| `.workbuddy/memory/MEMORY.md` | 项目长期记忆：定位、结构、现状、数据卫生 |
| `.workbuddy/memory/YYYY-MM-DD.md` | 当日日志：取证过程与原始读数 |
| `docs/P1-gui-implementation-spec.md` §9 / `docs/P1.5-child-process-spec.md` §7–§8 | 各阶段自己的「已知局限」与实施回写 |
| ECDI 侧 `docs/phase31-window-geometry-and-line-height-detailed-design.md` **§13.6** | `REQ-06`/`REQ-09` 的**取证结论落点**（112 格读数 + 仪器校准） |
| ECDI 侧 `docs/roadmap-deferred.md` **#69** · `docs/phase26-freetype-gl-detailed-design.md` **§9 L1** | `REQ-09` 的 ECDI 侧登记处（同一事实的对方载体） |
| `evals/*.json` | 机器可读读数（评估证据） |

★ **纪律**（条 117）：「某需求已闭合」这类事实会被**多份文档各自登记** ⇒ 订正时先列载体清单，再按关键词全库回扫，不只在「本次改过的文件」里搜。

### 5.2 维护规则

1. **新条目**：取 `max(现有编号) + 1`，并在「原编号」列留空或填来源（条 61⑥：新增编号前先枚举全集）。
2. **闭合条目**：**不删除**，在 §3 表里给闭合依据（哪个 ECDI Phase / 哪行代码）。
3. **取证要求**：凡判「已确认」，必须附**可复现**的证据（探针夹具路径 / 读数文件 / 源码行号）——禁用「看起来」「应该是」。
4. **归属必须有判据**：判 ECDI 侧 vs harness 侧，要写清**为什么**（本次 REQ-01 的判据链 = 解析层正常 + ECDI 契约已含空行 + wrap 正常）。

---

## §6 修订记录（新在前）

- **v2.6（2026-10-17 下午）★ `REQ-09` 当日修复并验收（ECDI 侧）+ 一条日志日期漂移的自我纠正**：
  ① **`REQ-09` 闭合**：ECDI 落地 `Win32FontSource::ResolveFile` 的**族名反查**——查注册表
  `HKLM\...\Fonts`，**三级匹配强度**（精确 / `"<family> "` 风格变体前缀 / `"<family> & "` 复合头），
  ★ **禁子串匹配**（实测 `Song` 会命中无关的 `STSong`），★ **文件名直查保留且优先**
  ⇒ 既有调用点与 T26-6 case 3 **逐位不变**（条 95⑤「消费者零改动」）。
  ② **验收（`t69_accept.cpp`，8 族名）**：Consolas / Courier New / Arial / Segoe UI / SimSun /
  Microsoft YaHei / Cascadia Mono / Tahoma ⇒ **8/8 两链同 face**（`consola.ttf` / `cour.ttf` /
  `arial.ttf` / `segoeui.ttf` / `simsun.ttc` / `msyh.ttc` / `CascadiaMono.ttf` / `tahoma.ttf`），
  串宽差 **≤2 px**（= `REQ-06` 那条已知光栅差，非解析错误）。对照：`"consola.ttf"` 仍解析；
  未知文件名/未知族名仍返回空（**无静默降级**）；`"Consolas Bold"` ⇒ `consolab.ttf`。
  **回归 `ecdi_tests` 465/465**、exit 0、`-D_DEBUG` 257 处（断言启用），与改动前基线同数。
  ③ ★ **取证期两处仪器/判据自我纠正**（值得单记）：ⓐ 首版探针把 `RegEnumValueW` 的 `REG_SZ`
  当**窄串**读 ⇒ 每个文件名只回**一个字符**（`consola.ttf` ⇒ `c`）——W 系列 API 的 `REG_SZ` 是 **UTF-16**；
  ⓑ 验收判据改两次：「FT 读数 ≠ 默认 face」对 **SimSun 不成立**（它的真实 face 本就是默认 face）、
  「串宽严格相等」**过严**（两链 advance 取整方式不同）⇒ 改为 **≤2 px**。
  ④ **未闭合的边界（登记不伪造，条 105）**：无**样式选择**（族名恒得 regular）；无 **CJK 族名**
  （`宋体` 无单名注册表键，需读 OpenType name 表）；无**字体回退**（N6）。
  ⑤ ★★ **一条日志日期漂移的自我纠正（条 7）**：跨 DPI 矩阵**实际执行于 2026-10-17 11:00–11:11**
  （证据 = `.scratch/dpi-matrix.json` mtime），但 v2.4 条目与当日日志误记为「2026-10-16 晚」——
  根因 = 会话跨过午夜后**继续写在 `2026-10-16.md`**（条 7 明写此坑，本轮又踩）。
  ⇒ 已新建 `2026-10-17.md`，并在 **v2.4 条目 / `status.txt` / 日志 §九** 三处订正日期；
  ★ 读数本身无误（仅日期标注错），历史措辞**保留加注、不改写**（条 61②）。
  ⑥ 取证：ECDI 侧 `t69_accept.cpp`；本仓库副本 `.scratch/p36-baseline/`。

- **v2.5（2026-10-17）★★ `REQ-06` 裁决结案 + 在案读数订正 + 新立 `REQ-09`（比 `REQ-06` 严重）**：
  ① **触发**：`REQ-06` 自 2026-10-13 收编后一直「待裁决」。按项目铁律（条 1.1 + 前四次归属反转的教训），
    **裁决前先查 ECDI 源码仓并实测**，不采信在案登记的数字。装置 = ECDI 侧库外探针 4 支（零框架改动，沿 p31 先例），
    读数 112 格（14 字体 × 8 字号，DPI 96），本仓库副本 `.scratch/p36-baseline/`。
  ② ★★ **`REQ-06` 裁决 = ③「记为已知现状」（用户拍板）**，但**结案理由与在案理由不同**：在案「修正它会全局改变 GL 文本位置」
    经实测**方向反了**——两侧统一到 `round(hhea·px/upem)` 时 **GL 只动 ≤1 px，GDI 最大动 −13 px**（疼的是默认可见链 ⇒ 违反条 95⑤）；
    而 GL 精确追平 GDI **做不到**（GDI `tmAscent` 无一式可复算：hhea 62 / usWin 85 / `otmMacAscent` 69，均 /112）。
    ⇒ 真结案依据 = **不存在既等价又不改默认观感的修法**。
  ③ ★★ **在案读数订正（第 5 次「先查再判」在本仓库兑现，但这次订正的是 ECDI 登记的数字）**：
    「拉丁 **+1** · CJK **+2**」= **SimSun@14 单点**；112 格实测 **GL−GDI ∈ [−13, +3] 且符号跨字体翻转**
    （Calibri −13 / Consolas −11 / Courier New **+3** / YaHei +2），80/112 格 ∈ {0,+1}。
    「CJK +2」是**字号现象不是类别规律**：同字体 `测` 在 16/24/32px 顶差 = **+1/0/0**——14px 恰是 `Font::size` 默认值、
    也是我方夹具采样点 ⇒ **采样点选中了最大差的那一档**（条 76「绝对阈值隐含量具已校准」的镜像：**单点采样隐含「该点代表类别」**）。
  ④ ★ **T31-8 的 ±1 判据原判过严**：包围盒顶差 = **基线差 − bearing 差**（合成方向此前未写明），默认字体上 **23/24 格 ≤ ±1**，
    唯一超差 = `测`@14 ⇒ 不是「CJK 类别不成立」。根因补全：`face->ascender == hhea` **112/112** ⇒ **GL 侧本就是 hhea 口径**，
    而 `REQ-05` 已把**行高**统一到 hhea ⇒ **同一 B-2 决策没覆盖基线维度**，这才是跨后端残差的结构性来源。
  ⑤ ★★ **新立 `REQ-09`（本轮真正的发现，优先级高于 `REQ-06`）**：`Font::family` 在 GDI 链 = **族名**、在 FreeType 链 = **文件名**
    ⇒ **不存在两侧都对的单一写法**（族名 ⇒ GL 静默回退 SimSun；文件名 ⇒ GDI 回退宋体；常见族名 5/5 在 GL 链回退，均实测）。
    **两个真实消费者**：我方 `kMonoFamily="Consolas"` ⇒ **GL 等宽行从未生效**；ECDI 自家 `ModelProbe.cpp:474/779` 按 Phase 26
    §11.7 B 改成文件名 ⇒ 在**默认 GDI 链**显示宋体（⇒ **B 的修法只对 GL 一侧有效**）。ECDI 侧登记 = **roadmap-deferred #69**，
    定性 = Phase 26 §9 **L1** 已登记的延迟设计，**触发条件已达成**（条 91 五态表），**不是新需求**。
    ★ **连带影响我方历史读数**：凡设置过 `family` 的 GDI↔GL 跨后端对比，「同一内容」前提不成立 ⇒ 历史 `evals/*.json` **不追改**，
    但解读须带标注（`REQ-06` 那条「基线差」在此之下反而是小头）。
  ⑥ ★ **一次判据自我纠正（条 76 量具族）**：首版拿「GDI `tmAscent` == FT `ascender` 数值相等」判两侧同一 face——**错**，
    两个不同量具的读数会偶然相同（GDI Consolas tmA=13 恰等 FT 侧**回退后 SimSun** 的 13.00）⇒ 改判据为**串宽**（同单位同语义）
    + 正对照（同 face 两侧同宽 152 / 133）。★ 本轮另有两支探针的**输出被控制台代码页吞掉**（GBK 打不出 `⇒`），已改判：
    探针读数一律落文件，不在 stdout 上做人眼判读（同条 105「能复制/落文件才算」）。
  ⑦ ★ **归属纪律自查**：本条**不是**第 5 次归属反转——已核 ECDI 源码行号 + 单变量对照 + 正对照，且**我方代码零改动**
    （改 `kMonoFamily` 只会把失效从 GL 侧搬到 GDI 侧）。★ 与 `REQ-08` 同为「已核源码仓」的正例。
  ⑧ 取证：ECDI 侧 `.workbuddy/spike/p36-baseline/`（`t36_ascent_probe` / `t36_bearing` / `t36_family_probe` / `t36_single_family`
    + `tables_probe.py` / `band.py` / `tahoma_edge.py`，读数 `ascent_readout.txt` / `bearing_readout.txt` / `family_readout.txt`）·
    本仓库副本 `.scratch/p36-baseline/`。结论落点：Phase 31 详设 **§13.6（v1.5）** · roadmap-deferred **#69/v2.31** ·
    phase26 详设 **§9 L1 追记（v1.8）** · desktopnest-roadmap **v1.25**（条 118 的旁支载体）。
  ⑨ **未测（登记不伪造，条 105）**：跨 DPI 的基线差（我方 2026-10-17 DPI 矩阵测的是**帧时/命令数**，不是像素几何量）·
    CFF 字体 · 像素级逐字号包围盒 · 非中文环境字体集 · 其余 84 格样本外推风险。

- **v2.4（2026-10-17 上午）★ 跨 DPI 矩阵落地（评估指标①/④的 DPI 维度）+ 一条预测自我否证**：
  ① **补上 2026-10-14 遗留的「跨 DPI 重跑」未测项**。前提勘察（`.scratch/probe_dpi_premise3.py`）：
  本机**不需要改系统缩放**——副屏物理 125%（phys/DIP=1.2500，两条独立路径交叉验证），把窗口
  搬过去即触发真实 `WM_DPICHANGED`（`Win32PlatformWindow.cpp:858-884`：更新翻译器→采纳建议
  矩形→让 `WM_SIZE` 走既有链；建议矩形等比缩放 ⇒ **DIP 尺寸不变** ⇒ 两档布局宽度可比）。
  矩阵 = 3 夹具（demo/cot/scale-2000）× GDI/GL × 100%/125% = 12 点，`--timed --auto-answer allow`
  + 25s 窗口（★ 不用 `--instant`：它首个 tick 就抽干，搬窗时布局已在 100% 定型）。
  **每点回读 `GetDpiForWindow` 验前提：12/12 全过**（100% 档 ==96、125% 档 ==120），两档消费量
  对齐（demo 7/16=7/16 · cot 4/88=4/88 · scale-gdi 502/1193=502/1193 · scale-gl ±1 截断抖动）。
  ② **读数 A · DIP 不变契约成立**：`cmds/frame` 125/100 = **0.992–1.002×（均值 0.9991）**
  ⇒ 同一内容在两档 DPI 下发出的绘制命令数完全相同，布局/命令生成全在 DIP 空间（Phase 20 R3
  架构约束的直接读数）。
  ③ **读数 B · 帧时按线性 DPI 走，不按面积**：`avgExecuteUs` 125/100 = **1.19–1.35×（均值 1.26×）**，
  贴着 DPI 比 1.25 而非面积比 1.5625。评估指标①的 DPI 维度答案。
  ④ **读数 C · 测量成本无系统性爆炸，但两链敏感度不同**：`us/char` GDI 侧 1.06–1.31×（一致偏大）、
  GL 侧 0.85–1.16×（噪声内、`cot-gl 0.83×` 反常偏低）。⇒ 记作**倾向**而非结论（单轮不足以定
  「GDI 对 DPI 更敏感」）。
  ⑤ **读数 D · 同 DPI 下 GDI vs GL 帧时比 2.07–2.85×（100%）/ 2.30–2.54×（125%）⇒ 跨 DPI 稳定，
  DPI 不放大两后端差距**。★ 诚实边界：这不是 `REQ-06` 的基线偏移本身（那是像素几何量，需像素
  探针）——本矩阵答的是帧时/命令/测量成本三项（用户选定的判据）。
  ⑥ ★★ **一条预测自我否证（判据在构造上就错，同 v2.2 帧数判据同族）**：我在操作单里写
  「`chars/call` 在 125% ≈ 100% 的 1.25×」——**错**：`measureChars` 数的是**字符数**，DPI 不改变
  字符数；布局宽度与字号都是 DIP ⇒ 换行位置基本不变 ⇒ `chars/call` 本该近似不变。该看的量是
  **`us/char`**（每字符耗时）。⇒ 教训：**写判据前先问「这个量的单位里含不含 DPI」**——字符数不含，
  耗时会含。（与 A-2「分母混性质」、v2.2「同名不同性质」同族：判据用错量。）
  ⑦ **附带收获**：REQ-08 的环在**两后端 × 两档 DPI 全部 6 组**里 `arc/frame = 2.000` 整
  （轨道弧+进度弧恰 2 条 `DrawArc`/帧），**GL 后端同样在画** ⇒ D5「纯虚防静默分歧」的裁决
  按预期兑现。
  ⑧ 取证：`.scratch/dpi_matrix.py`（矩阵跑批）· `.scratch/dpi-matrix.json`（12 点机读全量）·
  `.scratch/dpi-premise3.log`（前提勘察）· 操作单见 **`2026-10-17.md` §一**（该跑批实际执行于 10-17；原记于 10-16 日志 §九，已按条 7 移入当日文件）。

- **v2.3（2026-10-16）★ Phase 33 接入：`REQ-08` 闭合 + 胶囊居中 + 一条我报错的自我纠正**：
  ① **`REQ-08` 闭合**：ECDI Phase 33 落地 `DrawArc`（框架首个曲线原语，D5 = 纯虚）+
  `RingProgressBar`（其首个控件消费者），静态库接入 `third_party/ecdi`。harness 以**首个真实
  消费者**身份兑现：右栏「用量」卡上下文行 16px 条 → **44px 环**（轨道 `kLine`、线宽 5、
  环色与数字同阈值三档）。机读证据 `commandMix.arc = 350 > 0`；`invalidations` 仅 +1。
  ② **两个实现期发现**（都记进 §3 REQ-08 表）：轨道色不能用 `kTrackBg`（与卡片底色 `kPanel2`
  **同值** ⇒ 环看不见整圆）；`RingProgressBar::SetStyle` 未声明内建重绘 ⇒ 只在颜色档位翻转时
  补 `RequestRepaint()`（每 tick 补会把 `invalidations` 从 16 刷到 ~600，污染评估读数）。
  ③ **顶栏胶囊文字居中**：`Label` 不 override `CalculateTextPosition` ⇒ 左对齐；用 ECDI 预留
  扩展点派生 `uikit::CenteredLabel`（算法照 `Button.cpp:117`），未向 ECDI 提需求。
  ④ ★★ **一条我报错的自我纠正（用户勘误）**：我曾写「`CheckBox`/`Radio` 也 override 做水平居中」
  ——**错**。读 ECDI 源码仓核实：两者的 override 第一行是 `(void)textWidth;`（**显式放弃水平对齐**），
  只为把文本**垂直**对齐到 16px 指示器；**全库只有 `Button`** 做水平对齐，且已由
  `ButtonStyle::textAlignment` 三态驱动（Phase 30 △6），我引的 `Button.h:57`「写死居中」是**过时注释**。
  根因 = 只读了 `third_party/ecdi/include` 的头就下结论，没读 `D:\Project\ECDI\ECDI\src` 的实现
  ——**本项目第 6 次栽在「判 ECDI 能力前没查源码仓」这根钉子上**（前 5 次是归属反转）。
  ⑤ **BOM 回归修复**：`App.h`/`App.cpp`/`PaintProbe.h`/`PaintProbe.cpp`/`UiKit.h` 五个文件工作区
  丢 BOM（HEAD 里都有；`UiKit.h` 含 825 个非 ASCII 字符 ⇒ 按条 69 判据属已确认缺陷）。已修回。

- **v2.2（2026-10-14 晚·二）★ `--timed` 帧时序读数落地 + 一条判据自我否证**：
  ① **补上 2026-10-14 遗留的「cot 夹具 `--timed` 帧时序」未测项**（此前 `--instant`
  在同一 tick 抽干夹具 ⇒ 中间态不可观测，A-2 想控帧数也正是因此不得而入）。
  实测 6 个观测窗口（15/30/60/90/120/150s）+ 3 组 ask 对照，三条主读数：
  · **节奏忠实**：`--timed` 只消费到 `ts` 到期的行——`consumed/ts-due` = **0.934–0.960**
    （spread 仅 **1.03×**），且**不可能由「一个 tick 抽干」产生**（该退化形态下
    每档 consumption 恒等于 978，实测从 91 增到 699）。协议 §8.2「ts 是唯一节奏源」被证实。
  · **帧率恒定 ~29.3–30.4 Hz**（墙上秒数 × 30Hz，与窗口长度线性），无 burst、无漏拍。
  · ★★ **帧成本由「已累积内容」驱动，而非由「delta 到达率」驱动**：
    `cmds/frame` 288 → 360（**1.25×**），而 `deltas/frame` **下降** 0.193 → 0.151。
    ⇒ 这是 `--timed` **才能**给出的读数（`--instant` 把 934 个 delta 灌进同一个 tick，
    两个窗口的「每帧 delta 数」根本没有区分意义）。
  ② **协议 §8.3 的 ask 暂停被证实并量化**：受控单变量（只差 `--auto-answer`）——
  60s 前后**完全等价**（301 == 301，证明该开关本身不改变任何东西，负对照成立）；
  **90s 档不能判定**（夹具在 80.55s 与 150s 之间恰好都是 383 个事件 ⇒ 冻结与不冻结落在同一个数上，如实判 INVALID）；
  **120s 档决定性**：不答冻结在 **383**，答后推进到 **524**（+141）。
  ③ ★★ **一条判据自我否证（与 v2.1 的量具镜像同族，方向相反）**：我首版判据写
  「timed frames **<** instant frames」——**方向错了**：`--timed` 在整段窗口内持续 tick，
  `--instant` 一个 tick 抽干 ⇒ **帧数在两个模式下不是同一个性质的量，不能比大小**。
  该判据让首轮跑批**在 6 档全跑完后才早退**（12 次运行的帧数读数作废为判据依据）。
  ★ 修法 = 换成**消费侧**读数（`blocks + deltas`）才成立。**教训**：与 A-2「拿同时长两档比」
  同源——**判据采用哪个量之前，先问这个量在两个对照组里是否是同一个性质**。
  ④ **取证产物**：`.scratch/probe_timed.py`（原始跑批）· `.scratch/probe_askctl.py`（ask 对照）·
  `.scratch/recheck_pace.py`（独立复核，含两条反证）· `.scratch/verdict_timed.py`（终判）·
  机读版 `.scratch/timed-verdict.json`。
  **不复测项（显式登记，不假装测过）**：跨 DPI 重跑；第二个 ask（154.17s，所有窗口
  ≤150s 均未到达，其行为由第一个 ask **推断而非实测**）；per-delta 可见延迟（探针只报帧聚合）。
- **v1.8（2026-10-13 晚·三）★ `A-2` 复核否决 —— 原判收回**：
  曾登记「折叠卡里的长文本**每帧**都被测量 ⇒ 为用户看不见的内容付全量成本」，并写出修法。
  **实测推翻**：把 `chars` 分解为 `a(一次性) + b(每帧)·frames`（固定夹具、只改 `--auto-close`
  时长 ⇒ 改帧数，多档线性拟合）⇒ body=4000 的 **b=504**、body=40 的 **b=502**，
  **每帧成本与文本长度无关（1.00×）**；长文本的代价全在 `a`（一次性布局建立，891.9×）。
  ⇒ **ECDI 的 C29-10 五元组指纹缓存按文档生效**，先前「缓存失效」的判断作废。
  ⇒ 修法（`Relayout` 按需重测 + `RowRec::bodyHDirty`）**已完整回退**——实测零收益，
    且留一个不解决问题的失效位会成为日后腐化的入口。
  ★ **三层错误**（详 `.scratch/A2-recheck-conclusion.md` §三）：① 把「同时长下两个长度」的差
    当成逐帧浪费；② 用 `chars/call` 归一化，把**一次性**成本摊到了调用数上（分母混了两种性质）；
    ③ 首次想控制帧数时用「追加静止事件」，而 `--instant` 同一 tick 抽干夹具 ⇒ 帧数恒 54。
  ★ **残留观察（未定性、不登记）**：`a` 随长度近似 **O(n^1.5)**（`a/n` 从 3.45 升到 30.77），
    来源疑为 `BuildTextLayout` 逐行二分 + `FitText` 默认体的子串测量（其头注释自陈「非生产性能路径」）。
    **不登记为 REQ**（条 40）：这是一次性成本、当前无真实负载证明它构成问题；
    待 `fixtures/cot/` 给出长文本真实体量后再判。
- **v1.7（2026-10-13 晚·二）建议 C 落地：思维链评估夹具 + 三条附读数**：
  新增 `protocol/fixtures/cot/`（生成器 + 入库基线，确定性可复现），PROTOCOL §8.4 登记、
  §7 补「未知 `kind` 的中性渲染」一行。读数的**两条自我修正**（重要，均已记档）：
  ① 我原打算把「空体 vs 2600 字」当**折叠对照**，**截图否证**——`reasoning` 走中性路径是**可见气泡**（不是折叠卡），故该组测的是**正文长度**；根因是 **harness 对 thinking 块没有折叠能力**（A-1 有意为之：不让它伪装成工具卡）⇒「折叠省多少」**今天测不出来**，须等渲染形态定案。`--folded` 已改名 `--emit-empty`。
  ② 我原写「三种 kind 渲染路径不同」，**命令计数否证**——极差仅 145 条 text（噪声内），三者都是文本气泡，差别只在颜色令牌与前缀文本，**不在渲染规模**⇒ 该组只作对照，唯一可用判据是 `unknownKinds`（reasoning/unknown = 8 · assistant = 0）。
  ★ 主读数（**帧数无关**判据 `chars/call`，`--thinking-chars` 400/1200/2600/5200）：
  **53.58 → 131.94 → 272.39 → 512.33**（单调，5200 档是 400 档的 **9.6×**），`avgExecuteUs` 7,355 → 14,014 μs（**+90%**）⇒ 思维链长度线性推高每帧测量成本，直接兑现评估指标 ①/④。
- **v2.1（2026-10-14）用户裁决落地 + 上下文百分比 + 新增 `REQ-08`**：
  ① **用户裁决「块结束显示已完成」** ⇒ reasoning 标题随 `block_end` 从 `思考中` 变 `已完成`，
  **箭头不变**（仍是 `▼`；箭头只由展开态决定）。★ 这**修正了我上一轮的越权**——我曾把已结束做成
  `▶ 思考过程`（连箭头一起换），截图证明那违背用户明确给出的规格；用户裁决给出了正确解法
  （**只改文案**），失真顾虑以合规方式解决。
  ② **用户同意 `reasoning` 带 token 字段** ⇒ 协议 §3.5 增可选 `block_end.tokens`，标题副标
  `▼ 已完成 · 1306 tokens`。★ 放 `block_end` 是协议决定：token 数只有内容产完才知道。
  ★ **缺字段不显示**，且**不得**用字符数冒充（那是编造读数）。
  ③ **上下文占用百分比**（用户要求）⇒ 协议 §3.1 增可选 `hello.context`（**分母**），
  右栏新增「上下文」行（百分比条 + `21%` 文本，按占用率变色：<70 常规 / 70–90 琥珀 / >90 红）。
  ★ **修掉一个既有缺陷**：原两根用量条的分母是 `max(input, output)`——**拿输入跟输出比**，
  两个不同量纲相除 ⇒ **读数无意义**；现分母改为 `hello.context`。
  ★ **缺 `context` ⇒ 显示 `—`**（不猜；分母未知则百分比无定义）——已实测该降级路径。
  ④ ★★ **新增 `REQ-08`（环形进度条 / 圆弧原语缺失）**：用户猜「圆环 ECDI 应该还没实现」——
  **查源码仓后确认猜对**（6 个绘制原语无圆弧；GL 侧同样；命令 variant 亦无）。
  ★ 但**「环」能画**（`DrawRoundedRect` 半径钳到 `min(w,h)/2` ⇒ 同心圆叠出环），缺的是**按角度的弧**。
  ★★ **真正的发现**：**ECDI 自己早已把 `DrawArc` 登记为挂账项**，理由是「**无消费者不动**」
  （`phase9.6-animation-requirements.md:36`；`phase9.6-progressbar-requirements.md:27` 明写
  "indeterminate/转圈需要 DrawArc"）⇒ **本项目成了 `DrawArc` 的首个真实消费者，挂账前提变了**。
  本轮**不阻塞**：改用百分比条兑现需求。★ 这是第 5 条候选需求，但**第 1 条「先查源码仓且
  ECDI 自认挂账」**的条目（前 4 条全部反转为 harness 侧）。
  ⑤ ★★ **箭头字形（用户两轮口径，最终由用户直接指定）**：用户嫌 `▼`/`▲` 难看，先要求
  「类似 `<` 尖括号……**钝角**、大概 **120°**」，随后**直接给出字形 `﹀`(U+FE40) / `︿`(U+FE3F)**。
  ★★ **我此前两次把角度测反了，根因 = 量具公式符号错误**：首版写成
  `acos((k²-1)/(k²+1))`，**正确应为 `acos((1-k²)/(1+k²))`** ⇒ 每个读数被**镜像**为 `180°-真值`。
  后果：① 我断言「`▼` 本来就是 118°、角度已达标」——**错**，它其实是 **62.0°（锐角）**；
  ② 我据此换上的 `∨`/`∧`，我说 127.7°/131.4°——**错**，其实是 **53.1°/49.1°（锐角）**
  ⇒ 用户要**钝角**、我给了**锐角**，**方向完全反了**。
  ★★ **教训（本项目最该记的一条）**：**测量函数本身是仪器，未校准的仪器会给出自信的错数**。
  修正版**先喂解析式已知角度**的合成 V 形做校准（60/90/110/120/130/150，误差 ≤1.4°），
  通过后才测真实字形 ⇒ `﹀` **134.5°** / `︿` **135.7°**（钝角、细笔画、上下仅差 **1.2°**）。
  ★ 附带：多字节字面量**不得凭记忆手写**——我第一版把 `﹀` 写成 `EF B8 80`（实为 `EF B9 80`）
  ⇒ 一律用 `chr(cp).encode('utf-8')` **程序化反查**（同条 64「字面量不得凭阅读记忆重打」）。
- **v2.0（2026-10-14）★ 协议升 v2：思维链成为一等公民 + 新不变量 I8**（用户定调）：
  ① §3.3 增 `kind:"reasoning"`；② §5 增 **I8**（未知 `kind` 必须中性渲染 + 显式提示，不得伪装成有语义的形态）；
  ③ §7 增渲染形态 = **默认收起**、标题 `▼ 思考中`、点击展开箭头翻 `▲`（再点回 `▼`）——**逐字依用户口径**；
  ④ §9 版本表 + GUI 侧**首次真正实现**「声明版本高于上限 ⇒ 拒绝连接」（该条款自 v1 就在文档里但**从未实现**，v2 才让它起作用；实测 `protocol:3` 夹具 ⇒ `blocks=0`、`protocolIncompatible=true`）。
  ⑤ 新增 `--reasoning-probe`：**合成点击走真实派发链**取证折叠**行为**（默认收起 ∧ 正文不可见 → 点击展开 ∧ 箭头朝上 → 再点收起），跨 tick 状态机（避开 200ms 折叠动画的中间态）。
  ⑥ ★ **首次测得「折叠省下多少」**：受控 A/B（`--kind` 只改 kind、正文 612 条载荷逐字节相同）⇒ 收起 **65.0** vs 可见 **141.0/139.2** 文本命令/帧（**2.14×**）。此前"测不出来"是因为当时没有折叠能力。
  ⑦ ★ **一条自我修正**：首版断言「三种 kind 的 commandMix 无系统性差异（极差 145）」**已失效**（v2 后极差 4147）——当时三者都是可见气泡，现在 reasoning 收起，绘制量真的变了。
  ⑧ ★★ **一条实施期自伤（已撤回，值得单记）**：我一度把标题做成**三态**（`▼ 思考中` / `▶ 思考过程` / `▲`），理由是「已结束还写'思考中'会失真」。**截图暴露代价**：绝大多数块都已结束 ⇒ 屏幕几乎全是 `▶`，而用户要的是**向下**箭头 ⇒ **实际呈现违背了明确给出的规格**。⇒ 纪律：**用户给了可见形态的具体规格时，不要擅自替换成"更好"的形态**；有异议**提出来问**。该失真顾虑仍成立，但列为**待裁决**，不擅自改规格。
- **v1.9（2026-10-14）`REQ-04` 边界取证闭合 + 新探针 `--hit-probe`**：
  ① 此前 `REQ-04` 只有「使能**位**被置对」的证据（`--state-probe`）；本条**边界**（禁用 ⇒ 点击**不达**）**未证**。新增 `--hit-probe`：**合成 `MouseButtonDown/UpEvent` 喂给 `Application::OnEvent`**（走真实派发链，**不注入系统输入** ⇒ 不动光标、不抢前台），在**同一点**上做四组受控对照（`as-is` / `off-point` / `forced-enabled` / `forced-disabled`），观测通道 = `HitTest` 返回值 + `SendPrompt` 进入次数。
  ② 实测（6 份夹具 × 3 轮，**全部 PASS**）：`forced-enabled` ⇒ `hitIsSend=true ∧ clickReached=true`（正对照成立）；`forced-disabled` ⇒ 两者皆 false，落点**回落 RootWidget**（`hitIsRoot=true`）。裁定脚本 `.scratch/probe_hit_gate.py`（带正/负对照，多数轮裁定，条 75/81b）。
  ③ ★ 记下一条易混事实：禁用时 `HitTest` **不返回 `nullptr`**，而是**跳过按钮、回落到祖先 `RootWidget`** ⇒ 「命中者不是按钮」与「命中 nullptr」是两回事，读数分列 `hitIsSend`/`hitIsNull`/`hitIsRoot`。
  ④ ★ 另记：ECDI 自身测试**未覆盖**鼠标通道的 `IsEnabled` 门控（`WidgetTests.cpp:1356/1390` 是键盘通道；`CollapsiblePanelTests.cpp:56` 是不可见子树）——本探针在 harness 侧补上这份读数（属评估数据，非要求 ECDI 补测试）。
  ⑤ 诚实边界：探针因合成点击会挪焦点（`Button::CanFocus()==true`）⇒ `TextBox` caret 闪烁定时器重启，`paints`/`frames` 有 **±1~2 相位扰动**（已复原焦点，但相位不还原）⇒ 探针自身读数不受影响，但**不可**用探针开启时的 paints/frames 做性能对比。
- **v1.8（2026-10-13 晚）`A-2` 复核否决**：`A-2`（折叠态仍全量测量）**撤回**——把 `chars` 分解为 `a(一次性) + b(每帧)·frames` 后实测 `b` 与文本长度**无关**（body 4000 vs 40 的 b = 504 vs 502，**1.00×**）⇒ 长文本成本全在**一次性**布局建立，ECDI 的 C29-10 缓存**按文档生效**，**不需要**框架提供「跳过折叠态测量」的能力。`Relayout` 保持无条件 `AutoSize()`（代码骨架与 HEAD 逐行一致，仅增注释）。
- **v1.7（2026-10-13 晚）`C-1` 落地**：思维链评估夹具 `protocol/fixtures/cot/`（生成器 + 基线，12 轮 / 44 块 / 934 delta，可逐字节复现）。
- **v1.6（2026-10-13 晚）思维链 / 未知 `kind` 专项 + 一条候选 ECDI 需求被否决**：
  ① 登记**协议空白**——§3.3 的 `kind` 无思维链位置，且未知 `kind` 不被任何不变量覆盖（实测 `unknownTypes=0` + `validate.py` exit 0，满屏思维链可全绿）⇒ 建议协议 v2 增 `kind:"reasoning"` 并补 **I8**（未知 `kind` 必须中性渲染 + 显式提示）。
  ② ★★ **候选 `REQ-07` 复核否决**：曾拟记「ECDI 折叠件收起后仍测量」为框架需求；查 ECDI 源码仓发现其 Layout **四处**均有 `C-VIS-1` 跳过不可见子节点的契约（`VerticalLayout.cpp:37` 等）⇒ 归属 **harness 侧**（`App.cpp:1123` 无条件 `AutoSize()`）。**不登记为 ECDI 需求**。这是本项目**第 4 次**归属反转，成因同前三次（未先查 ECDI 源码仓）。
  ③ 落地 harness 侧处置（`A-1`）：未知 `kind` 中性渲染 + `UnknownKinds()` 记账 + 摘要 `unknownKinds` + 右栏独立行 + `validate.py` 记 note；实机复验通过，全库 5 份夹具仍 exit 0。
  ④ 另记 harness 待办 `A-2`：折叠态仍全量测量（受控 4 档实测 8.80×），修法已设计未实施。★ **该条已于 v1.8 复核否决**（分解为一次性/每帧两分量后，每帧成本与文本长度无关）。
- **v1.5（2026-10-13）`REQ-01` / `REQ-02` / `REQ-04` 三条 harness 侧修复落地并闭合**：`needWrap` 认 `\n` · `block_end` 认 `tool_call` · `PromptBar` 暴露按钮 + `state` 驱动使能（**判据 A5 兑现**）。三处均实机复测取证。
  ★ `REQ-04` 附带：ECDI 有意不做 disabled 绘制（YAGNI）⇒ 灰化由应用侧自备（`Theme.h` +3 禁用态令牌）——**仍不需要 ECDI 改动**。
  ★ 本轮自伤并修复：`edit` 工具按无 BOM 重写 ⇒ 4 个源文件一度缺 BOM（含中文 = 已确认缺陷），已按条 70 补齐。
- **v1.4（2026-10-13）`REQ-05` 回写闭合 + 新增 `REQ-06`**：ECDI **Phase 31** 已修行高口径，跨仓库实测确认（SimSun@14 两侧均 16.00；本仓库复测两后端均 8 带 / 16px）⇒ `REQ-05` **✅ 已闭合**（账本**首条走完「登记→修复→跨仓库验证→回写」闭环**的需求）。
  ★ 同时收编 ECDI 在 Phase 31 主动登记的 **O10**（跨后端基线偏移：拉丁 +1 / CJK +2 px）为 **`REQ-06`（⏸️ 待裁决）**——ECDI 判其为 **pre-existing**，修复方向留决策。
  ★ 库指纹核对：安装前缀 `a1f843f`（2026-10-13 20:17），头文件哈希与 ECDI 源**一致**；harness 已重建（exe 20:21）。
- **v1.3（2026-10-08 晚）新增 `REQ-05`（GDI/GL 行高不一致——真 ECDI 缺陷）**：由用户观察「GL 的空比 GDI 长」触发，受控复现精确成立（GDI 14px vs GL 16px）。
  根因 = 两条 `LineHeight` 依据不同（GDI `tmHeight` vs FT `metrics.height`）。★ 本轮**唯一**确认的 ECDI 侧缺陷（REQ-01/03/04 均反转为 harness 侧）。
  ★ 方法改进：为 harness 增 `--probe-contrast`（探针专用白底黑字，默认关闭）——此前的暗色主题测量反复产生仪器噪声并导致误判。
- **v1.2（2026-10-08 晚）`REQ-01` 归属反转（自查发现）**：由「ECDI wrap 未消费 `\n`」改判为「**harness 的 `needWrap` 只看宽度**」。
  铁证：受控阈值探针 **l100**（2 段 × 100 字符、超宽）⇒ **4 行**（两段各 wrap 成 2 行）；若 `\n` 被忽略则 200 字符仅约 3 行。源码侧 `TextWidget.cpp:235-248` 显式「第一步：显式 `\n` 切硬段」。
  ★ **影响面同步大幅下修**：由「realistic 3039 个换行」改为「**93 个含 `\n` 块中仅 1 个受影响**」——原判未考虑「超宽块仍会开 wrap」。
  ★★ **至此三条 ECDI 侧判定（REQ-01/03/04）全部反转为 harness 侧，成因同一个：未查基类公共头与 ECDI 源码仓。**
- **v1.1（2026-10-08）归属反转（评审提出 + 独立复现）**：`REQ-03`（`PostToUi`）与 `REQ-04`（按钮使能）**均由「ECDI 缺能力」改判为「harness 未使用 / 未暴露」**。
  证据：`Application.h:86`（Phase 23 能力在位，ECDI 自带 `ApplicationDispatchTests.cpp`）· `Widget.h:150-152`（`SetEnabled` 自 Phase 3 即在，`git blame` = `a11cb9b7`）· `Widget.cpp:114-128`（`HitTest` 已门控）。
  ★ **原判以引用形式保留**（条 30 / 61②）；并记入方法论教训：**判「ECDI 缺能力」前必须查基类公共头与 ECDI 源码仓**。
- **v1.0（2026-10-08）建立并首轮登记**：补齐此前缺失的账本交付物。收编 P1 的 `F1–F8`（4 条闭合、2 条转 harness 待办）、P1.5 的 `R-2`/`R-3`、P3 诊断的 `D-1`/`D-2` 为 `REQ-01`–`REQ-04`，并保留全部历史编号。
