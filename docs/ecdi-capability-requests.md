# ECDI_fake_harness · ECDI 能力需求账本

> **定位**：本项目对 ECDI 的**能力需求 / 缺陷登记**的唯一落点。
> 依据项目铁律——**任何对 ECDI 框架的修改都不在本仓库做**，这里只登记需求、只给证据、只判归属（见 `P1-gui-implementation-spec.md` §1.2）。
> 状态：**v1.0 建立并首轮登记**（2026-10-08）。
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

## §2 条目（★ REQ-01/02/03/04 = harness 侧，**均已闭合**；REQ-05 已闭合；REQ-06 待裁决）
> ★ 2026-10-13 晚补记：思维链（reasoning）暴露的**协议空白**与一条**候选 ECDI 需求被复核否决**（见 §2 末尾「思维链与未知 kind」小节）。

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
| 状态 | ✅ **已闭合**（harness 侧修复落地，2026-10-13——**判据 A5 已兑现**） |
| 现象 | 协议 §6 硬要求「发送 / 停止按钮**只由 `state` 驱动**」，但 harness 侧**当前未做到** |
| ★ 能力侧事实 | **框架能力完全具备**：① `Widget::SetEnabled` / `IsEnabled` **自 Phase 3 即在**（`Widget.h:150-152`；`git blame` 确认引入提交 `a11cb9b7`，2026-08-07）；② `HitTest` **已按 `IsEnabled` 门控**（`Widget.cpp:114-128`：`if (!IsEnabled()) return nullptr;`）⇒ 禁用控件不参与命中 |
| ★ 真正的缺口 | harness 的 `PromptBar` **没暴露 `m_send` / `m_stop`**（`PromptBar.h:36-37` 私有且**无访问器**）⇒ `App` 拿不到这两个 `Button*`，无从 `SetEnabled`。全库 `SetEnabled` 仅 2 处（`App.cpp:1017-1018`，权限门按钮） |
| 唯一出路 | ★ **harness 补 `PromptBar` 的按钮访问器**（或 `SetInteractionEnabled(bool)` 一类封装）⇒ 之后按 `state` 驱动即可。**不需要 ECDI 侧任何改动** |

**★ 附带发现（供 ECDI 侧参考，非阻断）**：禁用态**无视觉反馈**——全 ECDI 源码里 `IsEnabled` 只有 1 个消费点（`Widget.cpp:124` 的 `HitTest`），`Button.cpp` 无任何 `Enabled` 相关代码。
但这是**有意的设计决策**（YAGNI），非缺陷：`modelprobe-p1-preliminary-design.md:130` 记「disabled 视觉灰化：**demo 层处理**…**框架不做 disabled 绘制**——YAG」；
`phase13-captionbar-detailed-design.md:695` 亦记「**不做**：…禁用态视觉（禁用时 `HitTest` 已过滤）」。
⇒ harness 若要禁用态「看起来灰」，**须自行 `SetStyle`**（ECDI 侧已有先例：`modelprobe` 的 busy 态自设灰底灰字）。

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

### REQ-06 跨后端基线偏移（拉丁 +1 px · CJK +2 px）——**ECDI 记为既有差异，待裁决**

> ★ 本条**不是**我发现的，而是 **ECDI 在 Phase 31 收口时主动登记**（详设 **O10** / §13.2），
>   并明确判为「**既有（pre-existing），非本 Phase 引入**」；**修复方向留给决策**，未折进 Phase 31。
>   本账本收编它，是为了让「给 ECDI 的需求」这一侧也有对应条目。

| 项 | 内容 |
|---|---|
| 原编号 | ECDI Phase 31 详设 **O10**（§13.2） |
| 状态 | ⏸️ **待裁决**（ECDI 侧已登记根因；修与不修未定） |
| 现象 | 同一字形、同字体/字号/DPI/位置，两后端的**字形栅格包围盒**垂直方向差：**拉丁 +1 px · CJK +2 px** |
| 根因（已定位） | ① **基线差 1 px**：GDI 用 `pos.y + tmAscent`（`tmAscent = 12`），GL 用 `floor(pos.y) + floor(FontEngine::Ascent)`（FT `metrics.ascender = 13`）⇒ GL 低 1 px；② **CJK 再叠 1 px**：字形 bearing 取整差（`测` 的 GDI 字形顶 +0 vs GL +2） |
| 判为 pre-existing 的三条独立理由 | ① GL 基线表达式**本 Phase 未动**；② 它**不查 `LineHeight`** ⇒ 与本 Phase 的改动**正交**；③ **改动前的 GL 截图与新截图逐位相同** |
| 本仓库实测印证 | 同一夹具 `cjk_short`：GDI 首行 y102–115 / GL y104–115 ⇒ **差 2 px**，与 CJK +2 px 的记录一致（且是**首行偏移**，非行距） |
| 待裁决 | ECDI 侧原话：「修正它会**全局改变 GL 文本位置**」⇒ 修与不修需拍板（本轮**未改任何 ECDI 代码**） |

★ **与 `REQ-05` 的关系**：两条**独立**——`REQ-05` 是**行距**（14 vs 16，已闭合）；本条是**基线/包围盒**（±1–2 px，仍开放）。
  不要把本条的残留误读成「`REQ-05` 没修好」。
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

#### ① 协议空白：§3.3 没有思维链的位置（**待协议 v2**）

| 项 | 内容 |
|---|---|
| 现象 | §3.3 的 `kind` 枚举 = `user` / `assistant` / `tool_call` / `tool_result` / `system` / `error` **六个值**，**没有思维链** |
| 严重性 | 未知 `kind` **不被任何不变量覆盖**——I5 只管信封层的 `t`（子判别符不归它管）⇒ 实测 `unknownTypes=0`、`validate.py` **退出码 0**。一份满屏思维链的夹具会**全绿通过** |
| 性质 | **协议空白**（不是 ECDI 缺陷，也不是单纯 harness 缺陷）⇒ 与协议 §8.5 自己的纪律冲突：「一个恒成立的负向用例，与没有这个用例无法区分」 |
| 建议 | 协议 **v2** 增 `kind:"reasoning"`（附带 `tokens` 可选字段）**并补一条 I8**：未知 `kind` 必须以**中性形态**渲染（不得伪装成工具卡/权限门等有语义的形态）+ 显式提示。★ 渲染形态（默认折叠？显 token 数？）建议先用评估夹具取读数再定 |

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
| ★ `A-2` | **折叠态仍在全量测量**（`App.cpp:1123` 无条件 `AutoSize()`）：折叠卡里的长文本每帧照样测量 ⇒ 为看不见的内容付全部成本 | 🔄 **仍成立**（2026-10-13 受控 4 档剂量实测：body 40→4000 字符，`measureChars` 15,796→139,043，**8.80×**）⇒ 属 harness 侧，修法见 `.scratch/D-attribution-conclusion.md` §五 |

---

## §5 载体与维护

### 5.1 同一事实的其它载体（订正时须一并回扫）

| 载体 | 登记内容 |
|---|---|
| `docs/ecdi-capability-requests.md`（本文件） | **唯一账本**：能力需求与归属判定 |
| `.workbuddy/memory/MEMORY.md` | 项目长期记忆：定位、结构、现状、数据卫生 |
| `.workbuddy/memory/YYYY-MM-DD.md` | 当日日志：取证过程与原始读数 |
| `docs/P1-gui-implementation-spec.md` §9 / `docs/P1.5-child-process-spec.md` §7–§8 | 各阶段自己的「已知局限」与实施回写 |
| `evals/*.json` | 机器可读读数（评估证据） |

★ **纪律**（条 117）：「某需求已闭合」这类事实会被**多份文档各自登记** ⇒ 订正时先列载体清单，再按关键词全库回扫，不只在「本次改过的文件」里搜。

### 5.2 维护规则

1. **新条目**：取 `max(现有编号) + 1`，并在「原编号」列留空或填来源（条 61⑥：新增编号前先枚举全集）。
2. **闭合条目**：**不删除**，在 §3 表里给闭合依据（哪个 ECDI Phase / 哪行代码）。
3. **取证要求**：凡判「已确认」，必须附**可复现**的证据（探针夹具路径 / 读数文件 / 源码行号）——禁用「看起来」「应该是」。
4. **归属必须有判据**：判 ECDI 侧 vs harness 侧，要写清**为什么**（本次 REQ-01 的判据链 = 解析层正常 + ECDI 契约已含空行 + wrap 正常）。

---

## §6 修订记录（新在前）

- **v1.6（2026-10-13 晚）思维链 / 未知 `kind` 专项 + 一条候选 ECDI 需求被否决**：
  ① 登记**协议空白**——§3.3 的 `kind` 无思维链位置，且未知 `kind` 不被任何不变量覆盖（实测 `unknownTypes=0` + `validate.py` exit 0，满屏思维链可全绿）⇒ 建议协议 v2 增 `kind:"reasoning"` 并补 **I8**（未知 `kind` 必须中性渲染 + 显式提示）。
  ② ★★ **候选 `REQ-07` 复核否决**：曾拟记「ECDI 折叠件收起后仍测量」为框架需求；查 ECDI 源码仓发现其 Layout **四处**均有 `C-VIS-1` 跳过不可见子节点的契约（`VerticalLayout.cpp:37` 等）⇒ 归属 **harness 侧**（`App.cpp:1123` 无条件 `AutoSize()`）。**不登记为 ECDI 需求**。这是本项目**第 4 次**归属反转，成因同前三次（未先查 ECDI 源码仓）。
  ③ 落地 harness 侧处置（`A-1`）：未知 `kind` 中性渲染 + `UnknownKinds()` 记账 + 摘要 `unknownKinds` + 右栏独立行 + `validate.py` 记 note；实机复验通过，全库 5 份夹具仍 exit 0。
  ④ 另记 harness 待办 `A-2`：折叠态仍全量测量（受控 4 档实测 8.80×），修法已设计未实施。
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
