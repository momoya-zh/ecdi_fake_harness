# ECDI_fake_harness · ECDI 能力需求账本

> **定位**：本项目对 ECDI 的**能力需求 / 缺陷登记**的唯一落点。
> 依据项目铁律——**任何对 ECDI 框架的修改都不在本仓库做**，这里只登记需求、只给证据、只判归属（见 `P1-gui-implementation-spec.md` §1.2）。
> 状态：**v5.24**（2026-11-09；v1.0 建立于 2026-10-08。★ 订正：本行自 v2.9 起就落后于 §6，v2.9/v3.0 均未回写此处，见 §6 v3.1 ⑧）。
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

## §2 条目（★ REQ-01/02/03/04 = harness 侧，**均已闭合**；**REQ-03 于 2026-10-24 以「推 + 拉兜底」落地——`PostToUi` 首个框架外真实消费者**；REQ-05 已闭合；**REQ-08 已闭合（2026-10-16，Phase 33）**；**REQ-06 已裁决结案（2026-10-17，③ 记为已知现状）**；**REQ-09 已闭合（2026-10-17，ECDI 当日修复 + 验收 8/8）**；**REQ-10 ✅ **已闭合（2026-10-24 当日：ECDI Phase 36 全链收口——三批 `762c483`/`e394514`/`e163357` + 五链 497/497 + 五处台账同步；`Font` 表达力；★ 2026-10-24 ECDI 侧核验属实并登记 deferred #70）**；**REQ-11 ✅ 已闭合（2026-10-30 对账补记：ECDI 侧 `#71` 已于 2026-10-25 实施——GDI 链 `DrawText` / `DrawFocusRect` 补 alpha 消费；★ 原「推后」判据**未触发**即被 ECDI 侧主动关闭，如实留痕）**；**REQ-12 已闭合（2026-10-21 当日：ECDI 订正多行 preferred 语义 + 本仓收敛回框架）**；**REQ-13 已闭合（2026-10-23，ECDI 落地 `Window::SetMinimumSize` + 本仓收敛删回弹绕行）**；**REQ-14 ✅ 已闭合（2026-10-27 当日：ECDI Phase 37 `VerticalAlignment`/`TextBoxStyle::verticalAlign`/`GetTextTopInset` 落地 + 本仓 7 框收敛 + `--valign-probe` 7/7 进门；★ 判据走公开 `GetCaretClientGeometry`，padding 代数消去）**；**REQ-15 ✅ 已闭合（2026-10-31 当日：ECDI Phase 38 兑现编辑器内软折行 + 本仓两处开启 SetWordWrap(true) + --grow-probe 三相 12/12 进门闭合）**；**REQ-16 ✅ 已闭合（2026-11-01 当日：ECDI Button::AnimateBackgroundTo 修正快速移出时漏 Cancel 旧动画导致 hover 残留缺陷 + ECDI 侧单测 Widget.ButtonFastHoverLeaveCancelsAnimation 闭合）**）；**REQ-20 🔄 开放（2026-11-07 登记：段内内联样式 styled spans 能力缺口；★ 同日 ECDI 裁决已回：重启条件成立、富文本另立项 = ECDI deferred **#88**，Phase 号待排；块级全套先行见 rich-content-plan §G）**；**★ 2026-11-08 ECDI 侧已交付、本仓待消费**：ECDI `#88` / Phase 44（`008daff` batch2）落地 `TextWidget::SetStyledText(const std::string&, const std::vector<TextSpan>&)` + `SpanStyle` + `TextSpan`（`third_party/ecdi/include/ECDI/Widget/TextSpan.h`）。**★★ 验收口径必须收窄**：`SpanStyle` 只有 `family`/`weight`/`foreground` 三个 optional，头注释明文「严禁包含 size（闸门 3）或**斜体**（闸门 2 / L44-8）」⇒ 本仓能兑现的是**粗体**（`weight=700`）与**行内 code**（`family=Consolas`+`foreground`）两件，**斜体永远不生效**、**字号不可变**。§G 原文「粗体/斜体/行内 code 三件套」须按此改写。链路已核验：`ECDI.lib` 符号表含 `SetStyledText` 与 `ClampAndValidateSpans`，安装头与 ECDI 源树 **102/102 逐字节相同**，重链 35/35 成功、demo 基线 8/19/43 不变。**本仓尚未改任何与 styled spans 相关的代码。** ★★ **2026-11-09 更新**：本仓**已消费**——粗体（`**x**` / `__x__`）与行内 code（`` `x` ``）上线（`ParseInlineRuns` + `--md-probe` 11 个 inline 相 + `ApplyInlineText` 三路接入），**斜体按用户裁定暂不做**（`*x*` 按原文保留），`size` 留位不用；同批登记 **REQ-21**（`SpanStyle` 缺 `background` ⇒ 行内 code 无底色）。详见 `rich-content-plan.md` §H。
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

### REQ-03 ✅ `PostToUi` **能力已具备** → **harness 已走异步投递路径（2026-10-24 闭合）**

> ★★ **归属反转（2026-10-08，评审提出并经独立复现）**——本条**不是** ECDI 能力缺失，
>   **是 harness 侧的未使用**。原判「为何是 ECDI 侧议题」作废，以引用形式保留于下方「原判（已作废）」。
> ★ **闭合（2026-10-24）**：harness 侧落地「推 + 拉兜底」，`PostToUi` 首次有了**框架外的真实消费者**。

| 项 | 内容 |
|---|---|
| 原编号 | `R-2`（P1.5 实施回写） |
| 状态 | ✅ **已闭合（2026-10-24）**——判据与负对照见下方闭合记录。归属 = **harness 侧**（能力从来不是缺口） |
| 现象（登记时） | 规格四处写 `PostToUi` 是「首个真实消费者」，实际 **harness 零消费**——全库唯一命中是 `App.cpp:624` 的**注释** |
| ★ 能力侧事实 | **能力早已实现**：`Application::PostToUi(std::function<void()>)`（`Application.h:86`，**Phase 23 D-1**）⇒ 转发到 `Win32PlatformApplication::PostToUi`（`:112`），含入队 / 唤醒 / 失败回滚**同临界区**、关闭态拒绝（`m_dispatchClosing`）。**ECDI 侧自带测试消费者**：`ECDI/src/Tests/ApplicationDispatchTests.cpp`（顺序 / 禁止递归消费 / 载荷生命周期等用例） |
| ★ 真正的缺口 | **harness 从未真实走异步投递路径**——`ChildSource` 用「读线程 → 有界队列 → UI 线程 8ms 预算轮询 `NextWithin`」（P1.5 实施定案），全程不经 `PostToUi`（**2026-10-24 已改**，见闭合记录） |
| 为何仍要处理 | 协议指标③要看的正是「`PostToUi` 突发下的顺序与丢失」；**不真走该路径，harness 就无法为 ECDI 产出这条评估读数**——这是 **P2 的立项理由**，而非「ECDI 缺能力」 |
| 取证 | `grep PostToUi gui/src/` 仅 1 处命中（注释）；`ChildSource::Stop()` 直接在调用线程关句柄；`Application.h:86` 与 `Win32PlatformApplication.cpp:112` 证明能力在位 |

**原判（已作废，保留历史链条）**

> 原判：「为何是 **ECDI 侧**议题 | 协议指标③要看的正是「`PostToUi` 突发下的顺序与丢失」；不真走异步投递路径就无法验证该能力」。
> ⇒ ★ **作废理由**：把「harness 没走该路径」误读成「该能力待验证/缺失」。
>   能力本身**早已实现且有 ECDI 自测覆盖**（Phase 23 + `ApplicationDispatchTests.cpp`）——缺的是**消费方**，不是**能力**。

**★★ 闭合记录（2026-10-24，harness 侧落地 + 受控 A/B）**

七条设计决定，每条带代价：

| 决定 | 理由（不这么做会怎样） |
|---|---|
| 投递的是**信号**，不是数据 | 行留在 `ChildSource` 队列里，由 UI 线程自取。跨线程搬 `std::string` 就得让回调知道目标 `Session` 是否还活着——而 `Session` 可被删（侧栏 `×` / 归档）；队列跟着会话一起死，天然没有这个问题 |
| 闭包**只捕获 `this`**（HarnessApp），执行时**枚举**会话 | 捕获 `Session*` = 埋 UAF。★ 同理，`Application*` 是**值捕获**（在 UI 线程读一次），不是读成员 `m_application`——读线程读一个由 UI 线程写的裸指针没有同步 |
| 合并：`atomic<bool> m_wakeArmed` | 否则一次突发 = 每行一个 `std::function` 入队 |
| 单回调预算 **256 行 / 4ms 谁先到**，余量 `RearmWake()` 立刻再投 | 不设预算 ⇒ 2000 块一次抽干，冻住一整帧。★ 「再投」不能省：数据可能早已全在队列里，**不会有"下一行"来顺带唤醒** |
| tick **保留**，但预算改 0（非阻塞） | `PostToUi` 有三条失败路径：关闭态返 false、唤醒失败返 false、`bad_alloc` **抛异常**（`Application.h:83` 明写框架不吞）。兜底让最坏退化成「33ms 内取到」，而不是永远不取 |
| 只 drain **活动**会话，但**复位所有** live 源的标志 | `HandleLine` 硬绑 `m_sess`（全仓 587 处引用）⇒ 冻结语义必须保留；不复位则被冻的那个切回来时 arm 恒 true、永不投递，而这个后果**没有任何读数会报** |
| `--legacy-poll` 保留旧路径 | A/B 必须**单一自变量**；也是回滚开关。没有它，「推更快」只是把改动前的读数忘了 |

读数（**同一二进制 / 同一 core / 同一组参数 / 两侧都开入队时间戳**；`--auto-prompt hello`）：

| 世界 | `--speed` | blocks/deltas/messages | blockedTicks | avgBlockedUs | maxBlockedUs | wake / tick | posts | maxCB | p50 | p99 |
|---|---|---|---|---|---|---|---|---|---|---|
| legacy | 8 | 6/415/432 | 319/319 | 11599 | 23890 | 0 / 432 | 0 | 0 | 23500 | 48286 |
| **async** | 8 | **6/415/432** | **0** | **0** | **0** | **431 / 1** | 234 | 6 | 5161 | 27040 |
| legacy | 1 | 6/288/302 | 533/533 | 8787 | 23703 | 0 / 302 | 0 | 0 | 14774 | 48625 |
| **async** | 1 | 6/289/303 | **0** | **0** | **0** | **301 / 2** | 296 | 5 | **49** | 16091 |
| **async + `--burst`** | burst | **6/415/432** | **0** | **0** | **0** | 431 / 1 | 3 | **256** | 10998 | 25334 |

★ `--speed=1` 那一行两侧计数不同是**预期**（25s 窗口内流没跑完，截断点随时序浮动）⇒ 计数等价只在 `--speed=8`（流跑完：15s 与 30s 窗口读数都是 `6/415/432`）上断言。

★★ **意外发现（本条最有价值的读数）**：`NextWithin(…, 8)` 的 8ms **不是实测下界**。`cv.wait_for` 受系统时钟粒度约束 ⇒ 实测 **avg 8.8–11.8ms、max 23.9ms**（约 1.5× 预算）。代码注释里的「每 tick 8ms 预算」是**设计值**，改动前**没有任何读数能发现**这一点——它一直藏在「注释即事实」的默认里。

判据与负对照（入口 `tools/regress.py` §2b；变异脚本 `.scratch/probe_async_negctl.py`）：

| 判据 | 结果 | 负对照（必须红） |
|---|---|---|
| **C4 功能等价** | legacy == async == `6/415/432` | — |
| **C1a** 对照世界**确实**在阻塞 | avg 11599µs | —（这条防「0 vs 0」的空真） |
| **C1b** 推的世界零阻塞 | `blockedTicks = 0` | **M1** 走 `--legacy-poll` ⇒ **RED**（318/318 ticks） |
| **C2** 数据走唤醒通路 ≥95% | 431/432 = **99.8%** | **M2** 去掉 `RearmWake` 的复位 ⇒ **RED**（`wake=1/432`、`posts=1`——只投一次，之后全靠 tick） |
| **C3** 单回调 ≤256 行 | burst 下 **256**（3 次回调 = 256 + 175 + 1） | **M3** 去掉预算 ⇒ **RED（`maxCB = 430`，`posts = 2`）** |
| **C5** 节奏化流下 p50 ≤1ms | **49–771µs**（同口径 legacy 14774–23500µs ⇒ **30–300×**） | — |

★ **M3 的判别力是补出来的，不是自带的**：首版用 `--speed=100` 造积压，`maxCB` 只到 **76**（假 core 一共发 432 行，而"逐行加速"只是把细水长流变快，队列永远厚不起来）⇒ 该变异当时**不可判别**，如实记为 INCONCLUSIVE，**没有去调阈值**。随后给假 core 补 `--burst`（关节奏 + 一轮攒成一次写出），积压真实出现：有预算 **256** / 无预算 **430**，M3 转 **RED**。
★ 这条链本身是个教训：**「判据可证伪」也需要一个能造出边界条件的负载源**——判据写对了不等于它被证明过，中间缺的那块零件通常不在被测对象里，而在**发生器**里。
★ `--burst` 自身的两个坑（都当场踩到并修好）：① 必须 flush 控制面消息（`hello/state/workspace/error/ask/bye`）——第一版只留了 hello/bye/ask 三个显式刷点，漏了 `workspace` ack ⇒ GUI 等初始工作区等到超时，整场只收到 1 条消息；② `--burst` 默认关，既有路径逐字节不变。
★ 三条变异（M1 无变异、M2、M3）跑完均按**字节**还原并校验 sha256（`ChildSource.cpp` `4f0e71ec…` · `App.cpp` `33ed2ca3…`），还原后重建复跑基线回到全绿。

**消费者身份**：ECDI 侧此前只有 `ApplicationDispatchTests.cpp` 这一个**自家测试**消费者；本条落地后 `PostToUi` 第一次有**框架外的真实消费者**，并把 `DrainUiDispatch` 的「批次快照 → 解锁逐项执行 → 逐项查关闭态」路径放进真实突发（每次 ≥431 行、234 次投递）里跑。

**收口验收**：`python tools/regress.py` **PASS**（6 夹具 + 13 探针 + 2b live A/B + `validate.py`）。

★ **遗留（不登记为缺陷）**：队列上限 8192 与「单回调 256 行」的组合决定**背压形状**（生产快于消费时谁先让步）。这条只有接上真 agent（P3）才有真实负载可测——假 core 的 432 行压不出这个形状。

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

### REQ-10 `Font` 表达力不足——**字重、face index、复合族名第二项三者皆不可达**（★★ 本轮「同一视觉效果两链无公共解」级缺陷）

> ★ **触发（2026-10-18）**：用户给出高保真原型 `docs/prototype/harness-hifi-variant-c.html`，要求评估「落地到 ECDI 还缺什么」。
>   原型用 **10 处 `font-weight: 600/650/700`** 建立视觉层级 ⇒ 必须先回答「ECDI 能不能画字重」。
> ★ **归属判据（§1.1 铁律）**：已查 ECDI 源码仓 `D:\Project\ECDI\ECDI\src`，且做了**只改一个自变量**的受控对照
>   （只改 `lfWeight`、或只改 `Font::family` 字符串），**链接真实 `ECDI.lib`** 调用生产代码 `Win32FontSource::ResolveFile`，不是读注释推断。
> ★ 本条**推翻了本仓库自己的两个错误结论**（「全框架单字重、字重根本画不出来」与「Bold 是 style 不是 family」），订正过程见 §6 v2.7。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮取证新立；ECDI 侧**无**既有登记，`roadmap-deferred` #69 只覆盖族名反查，不含字重与 face index） |
| 状态 | ✅ **已闭合（2026-10-24）**——原「开放」备注保留于下：——三条子缺陷同源：**`Font` 无法表达「同族不同 style / 不同 face」**。★★ **2026-10-24 ECDI 侧对码核验：三条全部属实**，并登记 ECDI `roadmap-deferred` **#70**（**登记不立项**——#70 是 ECDI 缺陷队列当时唯一有真实消费者锚点的开放项，Phase 立项与否属需求决断、待拍板；初设必答三问已随登记钉出。★ 勘误：本行初版写「Phase 32 #58 与审计批余量在前」系引用 2026-10-12 旧快照——#58 实已于 2026-10-14 关闭、Phase 32 同日全链收口，见 §6 v3.4）。★★ **2026-10-24 Phase 36 批一~三落地**（ECDI `762c483`/`e394514`/`e163357`，MinGW 497/497；批三交付 lib 指纹 `5b0b1c51…d6ca` 已按 DELIVERY.md 逐项核对入 `third_party/ecdi`）**＋ harness 侧库外验收通过**：GL 生产 `FaceIdFor` 身份矩阵 14/14（T36-4/T36-8 断言库外复现：YaHei UI≠YaHei、ZH 请求回退默认 face、Segoe 600==700≠400、300==400、consola.ttf 直查豁免、NSimSun≠SimSun、SimSun@700==@400）；A2 五族墨迹比双链 10/10 方向 OK——GDI 原仪器读数与 REQ-10 基线**逐位相同**（507→690 1.36× 等 = A6 环境恒定锚），GL 经生产 `FontEngine::Glyph` 1.35–1.72×（批三前该链**完全画不出字重**）；A3 真字面格串宽 9/9 delta=0（SimSun@700 按 C36-4 豁免，合成差 +14px 实测）。harness 回归全绿（六夹具基线逐位、13 探针仅设计红、validate.py exit 0）。读数：ECDI 侧 `.workbuddy/spike/p36/p36h_matrix_readout.txt` + 本仓 `.scratch/req11/p36h_matrix.cpp/.txt`。★★ **✅ 已闭合（2026-10-24，ECDI 侧全链收口完成）**——ECDI 三批 `762c483`/`e394514`/`e163357` + **五链 497/497**（MinGW / Clang / ClangCL / MSVC Debug / MSVC Release）+ 五处台账同步（ECDI deferred **v2.40** #70 关闭 / README 行 / docs 索引 / 详设 v1.3 §9 收口 / 本条本行）；harness 复跑读数即本行上文的库外验收。头部 v3.5 → **v3.6** |
| 归属 | **ECDI 侧**（`Core/Font.h` 只有 `size` + `family` 两个字段，`Font.h:17-18`；GDI 后端硬编码 `lfWeight = FW_NORMAL`，`GDIBackend.cpp:1384`；`FontEngine` 硬编码 face index `0`，`FontEngine.cpp:236`） |
| 现象 ① **无字重** | `Font` 没有 `weight` 字段，且**框架内部两处硬编码 `FW_NORMAL`**（`GDIBackend.cpp:1384` · `GDITextMeasurer.cpp:63`）⇒ **任何调用方都无法请求粗体**。全源码搜 `lfWeight|FW_BOLD|FW_SEMIBOLD|FontWeight` 仅 4 处命中，**全部赋 `FW_NORMAL`**（另 2 处在 `Tests/TextMeasurerTests.cpp:81`/`:492`） |
| 现象 ② **无 face index** | `FontEngine.cpp:236` 是 `FT_New_Face(impl.library, path.c_str(), **0**, &face)` ⇒ TTC 永远只加载 **face 0**。实测 `msyh.ttc` 的 face 0 = `Microsoft YaHei / Regular`，**face 1 = `Microsoft YaHei UI / Regular` 永远拿不到**（`msyhbd.ttc` / `msyhl.ttc` / `simsun.ttc` 同型）⇒ **「UI 版与非 UI 版」在 GL 链上根本无法表达** |
| 现象 ③ **复合族名第二项断链** | 注册表键是复合形式 `Microsoft YaHei & Microsoft YaHei UI (TrueType)` → `msyh.ttc`，而 `FamilyToFileName`（`Win32FontSource.cpp:167-279`）只匹配**首项**（`base.compare(0, familyU8.size()+3, familyU8+" & ") == 0`，强度 1）⇒ 逐键模拟（`.scratch/shot-b/family-sim.py`）读数：`Microsoft YaHei` → `msyh.ttc` ✅ / **`Microsoft YaHei UI` → 无匹配** / **`Microsoft YaHei UI Bold` → 无匹配** ⇒ GL 链 `ResolveFile` 返回**空串**，静默回退 SimSun |
| ★★ 实测读数（**只改 `lfWeight`**，白底黑字同串同 DPI，判据 = luma<128 的像素数） | `Microsoft YaHei` CJK 14px「整理 API 错误处理」507 → 690 = **1.36×** · 同族拉丁 15px「harness」168 → 277 = **1.65×** · `Microsoft YaHei UI` CJK 507 → 690 = **1.36×** · `DengXian` CJK 414 → 533 = **1.29×** · `Segoe UI` 拉丁 135 → 242 = **1.79×** ⇒ **对 harness 真实用到的 5 个族全部 ≥1.29×**，判「`lfWeight` 路线在 GDI 侧单独就够用」 |
| ★★ 实测读数（**只改 `Font::family`**） | 四手段矩阵见下；关键行：`Microsoft YaHei Bold` ⇒ GDI **宋体**（回退，墨迹 0.75× = **变细了**）/ GL `msyhbd.ttc` ✅；`Microsoft YaHei UI Bold` ⇒ GDI ✅ / GL **空串** ❌；`微软雅黑 Bold` ⇒ GDI ✅ / GL **空串** ❌ |
| ★ 覆盖面（四手段矩阵） | `lfWeight=FW_BOLD`：GDI ✅ / GL ❌（无字段、不传 variation 轴）· `family="Microsoft YaHei Bold"`：GDI ❌ / GL ✅ · `family="Microsoft YaHei UI Bold"`：GDI ✅ / GL ❌ · `family="微软雅黑 Bold"`：GDI ✅ / GL ❌ ⇒ **两条链唯一同时可用的是「不加粗」** |
| ★ 根因（**推翻了「Bold 是 style」的旧解释**） | GDI **同时接受族名与完整 face 名**（`Arial Bold` / `Times New Roman Bold Italic` / `Consolas Bold` / `Segoe UI Bold Italic` / `Microsoft YaHei UI Bold` 全部 ✅ 命中）。真正的失败原因是**本地化规范名**：`GetTextFaceW` 返回的是 face 的**规范名**，`Microsoft YaHei` 的规范名是**中文** `微软雅黑` ⇒ 英文完整名 `Microsoft YaHei Bold` 匹配不上而回退宋体，但 **`微软雅黑 Bold` 就能命中**。⇒ 规律：**规范名是中文的族必须用中文完整名；规范名是英文的族（Segoe UI / Arial / YaHei UI）英文名即可** |
| ★ 消费者 | **本仓库**：`gui/src` 全部 `ECDI::Font` 只设 `size`，7 处 `family` 赋值**全是** `kMonoFamily = "Consolas"`（`App.cpp:514`/`:522`/`:747`/`:1246`/`:1266` · `UiKit.h:57`/`:106`）⇒ **中文正文全部 `family` 为空**，而空 family 两链**一致落到 SimSun**（`req11_default.cpp`：GDI 空 `lfFaceName` → `宋体`；GL `ResolveFile("")` → `simsun.ttc`）⇒ **harness 现在的中文正文实际渲染在宋体上，不是微软雅黑**。② **ECDI 自家示例**：`examples/ModelProbe/ModelProbe.cpp:474/779` 用 `"consola.ttf"` 文件名写法 |
| ★ 对我方评估的影响 | **variant-c 的 10 处字重层级（品牌字 700 / 卡片标题 650 / 会话名与按钮 600 / 正文 400）落地后全部塌成一档** ⇒ 只能靠「字号 + 颜色 + 留白」代偿。**这不阻塞功能**（渲染正确、协议读数不受影响），阻塞的是**视觉层级**。★ 反过来说：这也是本项目作为「GUI 载体评估方」的一个**真实负载画像**——一个只有 4 档字重的常规界面，ECDI 目前画不出来 |
| ★ ECDI 侧修法候选 | ⓐ **给 `Font` 加 `weight`（`uint16_t`，默认 400）**：GDI 后端把它送 `lfWeight`（**一行改动**，`GDIBackend.cpp:1384` 与 `GDITextMeasurer.cpp:63` 各一处）；GL 后端按 `weight` 选 face（同族已有 Bold 文件时）或走 FreeType variation 轴 ⇒ **两链同解，且调用方零改动**（条 95⑤）。**倾向此条** · ⓑ 给 `Font` 加 `faceIndex`（解 TTC face 1）——**优先级低**：`Microsoft YaHei UI` 与 `Microsoft YaHei` 的差异只是行高，可用 `size` 微调代偿 · ⓒ 让 `FamilyToFileName` 支持**复合键的第二项**（解 `Microsoft YaHei UI` 断链）——与 REQ-09 同处一个函数。★★ **2026-10-24 ECDI 侧订正：ⓒ 与 ②（face index）耦合，非独立闭合**——ⓒ 单独落地后 `Microsoft YaHei UI` 解析到 `msyh.ttc`，但 `FontEngine.cpp:236` 恒取 face 0 ⇒ 拿到 `Microsoft YaHei`（非 UI 版）＝比 SimSun 回退好、但仍是「**静默给错 face**」的 #69 同类陷阱 ⇒ 真正闭合 = **family → (文件, faceIndex) 二元组**或加载后按 face 名扫描，②③ 应一体设计；「独立可做、零风险」只对「消除 SimSun 回退」成立 |
| ★ 我方本轮处置 | **不改代码**。理由：改 `Font` 是 ECDI 公共头变更，harness 侧无法自行绕过（`family` 路线已实测对 CJK 无解，且会给中英文引入两套族名规则）。★ **过渡方案**（若 ECDI 短期不动）：拉丁文用带字重的族名（`Segoe UI Semibold` / `Segoe UI Bold`，两链都通，实测 1.20×/1.38× 方向一致），中文用**字号 + 颜色压深**代偿，**不尝试伪加粗**（描边模拟会在小字号 CJK 上糊成一团） |
| ★ 取证 | 探针 `.scratch/req11/`：`req11_weight.cpp`（`lfWeight` 400/700 墨迹比，5 个真实用例）· `req11_root.cpp`（23 个族名 × 英文名/本地化名）· `req11_facekind.cpp`（族名 vs 完整 face 名，12 个用例）· `req11_default.cpp`（空 family 双链）· `req11_ink2.cpp`（**链内**墨迹对照，修正了跨链比较的仪器错误）· `family-sim.py`（`FamilyToFileName` 逐键模拟）· `ttc-faces.py` / `ft-index.py`（TTC face 枚举）。对照表 `docs/variant-c-ecdi-mapping.md` §2 |

★ **与 `REQ-09` 的关系**：同源（都在 `Font::family` 的语义上）、**不同问题**——`REQ-09` 是「同一字符串两链解析到不同 face」（**已修**，族名反查），
  本条是「**一个字符串根本表达不了想要的 style/face**」。`REQ-09` 修复后族名写法在两条链都对，但**能表达的仍只有「常规体 + 单一 face」**。

★ **仪器教训（记录，防复发）**：首版探针直接比较 **GDI 墨迹 vs FreeType 墨迹**，得出「Segoe UI 常规体就 0.58×」——
  那是**两个渲染器的抗锯齿差异**，不是字重差异。⇒ 正确判据是**只在链内比较**（同一条链的 base 族 vs variant 族）。跨链绝对墨迹量**不可比**。

---

### REQ-11 `Color.a` 在后端间覆盖面不对等——**GDI 链的 `DrawText` / `DrawFocusRect` 恒丢弃 alpha**

> ★ **触发（2026-10-18）**：评估 variant-c 时查「半透明 / 虚线」能否落地，顺手复核 `Color.h:19` 的注释
>   （原文只写 `DrawRect` / `DrawRoundedRect` 经 `BlendAlphaSolid` 消费）。
> ★ **归属判据**：已读 ECDI 源码仓两个后端**逐原语**的 alpha 处理，并做了**只改 `Color.a`** 的受控对照（复刻 `DrawTextImpl` 的 GDI 路径）。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮新立） |
| 状态 | ✅ **已闭合（2026-10-30 对账补记）**——★ **ECDI 侧 `#71` 已于 2026-10-25 实施并关闭**：GDI 链 `DrawText` / `DrawFocusRect` 补 alpha 消费，与 GL 链对齐 ⇒ **缺陷面消失**（验收 = 探针双读数 + 测试 +3，判据 = 墨迹 gMean 差；判别力已证 = 临时回退三处行为核心恰 4 红）。★ 本条**零消费者 ⇒ 无需本仓验收读数**。★ 留痕：原「推后」判据（等首个半透明文本/焦点框消费者）**始终未触发**，是 ECDI 侧**主动修**的，不是消费者拉动。以下为闭合前的历史判读——**缺陷面零消费者**，不阻塞任何已规划功能；但形态已钉死，将来要用时不必重新取证。★ **2026-10-24 ECDI 侧对码核验属实**（一处行号订正见「覆盖面」行），登记 ECDI `roadmap-deferred` **#71**（推后口径一致：零消费者，触发条件 = 首个半透明文本/焦点框消费者出现）。★ **2026-10-26 订正消费者面**：P4 Phase F 的设置面板遮罩已成为**首个半透明绘制消费者**，但它走的是 `DrawRect` + `BlendAlphaSolid`——即「覆盖面」表里**已支持**的路径，**没碰到缺陷面**。⇒ 缺陷面（`DrawText` / `DrawFocusRect`）仍是零消费者，**触发条件未变**，本条状态不动（★ 该判读在 2026-10-26 成立；**2026-10-30 已被 ECDI `#71` 关闭取代**——见本行开头） |
| 归属 | **ECDI 侧**（后端能力不对等；`Color.h:19` 的注释只描述了 GDI 的现状，未声明这是**双链发散**） |
| 现象 | **同一个 `Color` 在两后端合成结果不同**：GL 链**全部 9 个原语**都消费 `alpha`（`GLRenderer.cpp:731-734` `Shade()` → `glColor4f(r,g,b,**a**)`，混合模式 `:201` `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`，调用点 `:215`/`:246`/`:324`/`:329`/`:379`/`:412`/`:642`）；GDI 链**只有部分**消费 |
| ★★ 实测读数（**只改 `Color.a`**，复刻 `GDIBackend::DrawTextImpl`：`SetBkMode(TRANSPARENT)` + `SetTextColor(ToColorRef(color))`） | 深底 `#16181D` 上画 `kDarkInk (230,233,239)`，取**最亮**字形像素：`a=255` → `#E6E9EF` · `a=128` → **`#E6E9EF`** · `a=64` → **`#E6E9EF`** · `a=16` → **`#E6E9EF`** ⇒ **四档输出逐位相同**，alpha 在光栅化前就被丢弃（`ToColorRef` 只有 RGB 三通道）。若 alpha 生效，`a=128` 应为 `~#7E8186` |
| ★ 覆盖面（逐原语，GDI 侧） | `DrawRect` ✅（`GDIBackend.cpp:649` `if (color.a < 1.0f) BlendAlphaSolid(...)`）· `DrawRoundedRect` ✅（同款，`DrawRoundedRectBlendedAA` `:1492`）· `DrawArc` ✅（经 `RasterizeMask`，`CoverageRaster.cpp:71`/`:83`）· `DrawLine` ⚠️ **分流**：`:842-850` 若 `!m_antiAliasing \|\| (color.a >= 1.0f && IsAxisAlignedAfterRound(...))` → `DrawLineLegacy`（`:878` `CreatePen(PS_SOLID, penWidth, ToColorRef(color))` ⇒ **丢 alpha**），否则 `DrawLineCoverage` ✅ · **`DrawText` ❌ 恒丢**（`SetTextColor(ToColorRef(color))`——★ **2026-10-24 ECDI 侧订正：实为单点 `GDIBackend.cpp:746`**；`:682`/`:714` 系 ECDI Phase 34 量化统一动 `GDIBackend.cpp` 后的行漂移，现为其函数头行，实质判读不变）· **`DrawFocusRect` ❌ 恒丢**（`:1214`/`:1222` `CreatePen(PS_SOLID, 1, ToColorRef(color))`） |
| ★ 已有测试面 | `ECDI/src/Tests/RendererTests.cpp:334-382` `TestDrawRectAlpha`（50% 透明红叠纯蓝底，断言 `\|R-128\|<=3` / `G<=3` / `\|B-127\|<=3`）+ `:214 TestGDIBackendAlphaBlend` ⇒ **只覆盖 `DrawRect`**，`DrawText` / `DrawFocusRect` 的 alpha **从未有用例**（不是回归，是从未覆盖） |
| ★ 消费者 | **2026-10-26 起不是零了，但要分清是哪一类**：① **半透明绘制**——P4 Phase F 的设置面板遮罩（`DrawRect` + `BlendAlphaSolid`，双后端实证，失败退不透明）⇒ 走**已支持**路径，本条缺陷面未被触碰；② **半透明文本 / 焦点框**——仍为零：`gui/src` 其余位置全部 `Color::FromRGBA8(..., 255)`（`Theme.h` 全表 alpha=255）；`variant-c` 全文搜 `rgba\|opacity` 零命中（6 处 `transparent` 都是 `background:transparent` = 不画底，不是半透明）⇒ **本条仍是「将来要用时才疼」的形态，不是当前阻塞项** |
| ★ 附带缺口：**无虚线** | `RenderCommand::DrawLineCommand` **无 dash/style 字段**，GDI 侧两处 `CreatePen` 都硬编码 `PS_SOLID`（`:878`/`:1222`）⇒ 原型里的 `border:1px dashed`（variant-c `.gap` :119、`.sample-label` :130）**没有任何对应能力**。若要支持，最小面 = `DrawLine` 加一个 `dashPattern` 字段，或另立 `DrawDashedRect` |
| ★ ECDI 侧修法候选 | ⓐ **`DrawText` 走 alpha 混合**：先按 `a` 把字形光栅化成 mask 再 `BlendAlphaSolid`（`DrawRoundedRectBlendedAA` `:1492` 已有现成先例，可复用同一套合成路径）· ⓑ **至少补测试**：把 `TestDrawRectAlpha` 的模式扩到 `DrawText` / `DrawFocusRect` / 非轴对齐 `DrawLine`，否则「GDI 丢 alpha」这条会随重构漂移 · ⓒ `DrawLine` 的 `PS_SOLID` 分流是**性能优化**（AA 关或轴对齐时走快路径），但**语义上不该在 a<1 时走快路径**——判据里已有 `color.a >= 1.0f`，实际只在「AA 关 + a<1」时漏，属**边角** |
| ★ 我方本轮处置 | **不规避**（当前无消费者）。★ 但登记在案的理由：这是**继 `REQ-09` 之后第二处「同一输入两后端行为不同」**，形态同型，值得让 ECDI 知道这类发散不是孤例 |
| ★ 取证 | `.scratch/req11/req12_text.cpp`（四档 alpha，取最亮像素）· `.scratch/req11/req12_alpha.cpp`（GDI vs GL 逐原语对照表）。★ **仪器教训**：深底亮字**不能用「最暗像素」当判据**——首版取到的是背景色，四档 alpha 全一样，看似"证实"实为取错对象 |

---

### REQ-12 多行 `TextBox` 的 `GetPreferredSize` 返回**当前尺寸**——同一框架内与 `TextWidget` 的 wrap 分支自相矛盾

> ★ **触发（2026-10-21）**：用户报告「多行不自动长高」，并据此推断「气泡必然不能设成内容滚动，是不是该给 ECDI 加个新控件」。
> ★ **归属判据**：已读 ECDI 源码仓 `TextBox.cpp` / `TextWidget.cpp` / `Widget.cpp` 与**其自带测试** `TextBoxTests.cpp`，
>   并做了受控对照（同一 `GetPreferredSize` 的两个分支、两种控件）。**结论：不需要新控件**——缺的不是控件，是**一条规则的不一致**。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮新立） |
| 状态 | ✅ **已闭合（2026-10-21，ECDI 侧采纳 ⓐ 订正语义）**——`TextBox::MultilinePreferred` 落地；★ 本仓组合实现已**收敛回框架**（见末行） |
| 归属 | **ECDI 侧**（同一框架内两条 preferred 规则互相矛盾；不是"两类控件的分野"） |
| 现象 | 多行输入框**永远无法**用 `AutoSize()` 随内容长高：`TextBox::GetPreferredSize` 一见到 `\n` 就 `return Widget::GetPreferredSize()` = **当前尺寸**（`TextBox.cpp:166-174`）⇒ `Widget::AutoSize` 比较后判定"同尺寸"⇒ 返回 `false`、**结构性 no-op** |
| ★★ 矛盾所在 | **同一条 `GetPreferredSize` 在 `TextWidget` 的 wrap 分支返回 `{GetWidth(), 行数 × 行高}`**（`TextWidget.cpp:428-436`，需求稿 D29-B）。⇒ 在同一个框架里：**`Label`（开 wrap）能长高，`TextBox`（多行）不能**。用户看到的"气泡会高、输入框不会"正是这条不一致的可见后果，**不是**两类控件应有的区别 |
| ★ 挂账出处（不是"忘了"，是**记过账的**） | `phase29-text-multiline-requirements.md:36`（K4：「多行高度 v1 手工 SetSize，不参与 AutoSize」）· `phase9.8-autosize-detailed-design.md:110`（同一句的原始出处）· `phase9.8-autosize-requirements.md:47/127-128`（「v1 挂账」「多行高度仍手工 SetSize」） |
| ★ 现状的**真实代价** | 调用方若想让多行框长高，只能自己算「行数 × 行高 + padding×2」，即**在应用层复制两条框架规则**（行数 = `\n` 数 + 1，见 `TextBox.cpp:390-403`；行高 = `TextMeasurer::LineHeight`，见 `TextBox.cpp:431-438`）。一旦 Phase 29 的 **O2**（编辑器内 wrap）兑现，「`\n` 数 + 1」会**静默算错**——框高少一行，且**没有任何读数会报** |
| ★ 已有的、本可复用的量 | `GetMaxScrollOffset()` 里**已经在算** `行数 × 行高 − 视口高`（`TextBox.cpp:452-457`）。但它是 `private` 且**带 `max(0, …)`** ⇒ **不能**直接拿来当"内容高"：不溢出时它恒为 0 ⇒ 用它会导致**只会变高、永远缩不回去**（本仓 `--grow-probe` 的 `shrink` 相专门盯这条，见 `.scratch/probe_grow_negctl.py` 的 M5） |
| ★ 修法候选（按我方倾向排序） | ⓐ **订正语义**（API **+0**）：多行分支改为返回 `{GetWidth(), ceil(行数 × 行高 + padding×2)}` ⇒ `AutoSize()` 立刻可用，且与 wrap 分支**同构**。代价：要改 `TextBoxTests.cpp:1182-1190` 那条**锁定旧行为**的断言（`TestTextBoxPreferredMultilineNotParticipating`），并回扫上述六处文档 · ⓑ **加一条只读查询**（API **+1**）：`GetContentHeight()`，语义按「**不受 `max(0,·)` 钳制**」写清，不动 `AutoSize` 语义、零回归 · ⓒ 不做（本仓继续组合实现） |
| ★ 不建议 | **不要为此加新控件**。用户最初的提议（"ECDI 给个新控件"）会把 `TextBox` 一千三百行复制一遍，而真正的缺口只有一条 `if` 分支。同理也不建议把气泡改成滚动容器——**气泡的长高能力本来就是对的**（wrap 的 `Label` 已经能长高），问题只在输入框那一侧 |
| ★ 我方本轮处置 | 走 **ⓒ 组合实现**（用户 2026-10-21 拍板）：`gui/src/PromptBar.cpp` 的 `ComposerBox` 用 **protected 可见性**（`GetWindow()` + `TextWidget::m_style`）算内容高，`PromptBar::SyncInputHeight` 负责 clamp 到 `[单行高, kInputMaxH]` 并重排父布局。**零 ECDI 改动、零私有后门** |
| ★ 取证（本仓） | `--grow-probe` 7 相（`type-newline` / `grow` / `max-clamp` / `send-reset` / `shrink` / `exact-fit` / `baseline`）在 light+dark 双主题全绿；负对照 `.scratch/probe_grow_negctl.py` 5 条变异：M1（摘掉 `OnTextChanged` 上报）⇒ `type-newline` 红、M2（摘掉 `SetText` 后同步）⇒ `send-reset` 红、M4（去掉封顶）⇒ `max-clamp` 红、M5（只增不减）⇒ `send-reset`+`shrink` 红、M3（`ceil`→截断）⇒ **EXPECTED-GREEN**，因为本机实测行高 = **17.0（整数）**，`lines×17+12` 恒为整数 ⇒ `ceil` 是恒等变换。★ 这条如实记为「该变异在本配置下语义等价」，**不计入**判别力证据；行高为非整数的字体/字号下它会红 |


**★★ 闭合记录（2026-10-21，ECDI 采纳 ⓐ）**

ECDI 的裁决理由只有一条，但够重：**这条缺陷的形状是「同一个虚函数对同一个问题给两个答案」。
ⓐ 修的是答案；ⓑ 是再加一个问题。** ⓑ 落地后框架里会同时存在 `GetPreferredSize`（多行仍返回
当前尺寸）与 `GetContentHeight`（返回真话），而下一个消费者先伸手拿的一定是前者——那是所有控件
的统一入口——然后**还是会掉进同一个坑**。不一致没有消失，只是多了一块绕行标志牌。

ECDI 侧落地（`ECDI/src/Widget/TextBox.cpp` 的 `MultilinePreferred`）：

| 契约 | 落地方式 |
|---|---|
| 多行 preferred | `{当前宽, ceil(行数 × 行高 + padding×2)}`——与 wrap 分支 D29-B **同构** |
| **① 取整归属** | `ceil` 做在 `GetPreferredSize` **内部**，不留给 `Widget::AutoSize`（那是向零截断）⇒ 69.6→69 的 exact-fit 滚动条 bug 不会由框架自己复现 |
| **② 无测量器/无窗口** | 退回当前尺寸，与 wrap「无有限宽 ⇒ 退化单行」同构——保住无头测试零污染 |
| **③ 行数口径** | `\n` 数 + 1，与 `RecalculateLines` 同源；O2（编辑器内 wrap）兑现时**只需改这一处真相源** |
| 旧测试 | **不删，翻转成反向断言**（`TextBox.MultilinePreferred`）：装不下 ⇒ preferred > 当前；装得下 ⇒ 报内容高而非视口高；删行 ⇒ 变矮 |

★ **负对照（证明测试不是装饰）**：`.scratch/ecdi_multiline_negctl.py` 四条变异，
每条都要求测试**变红**，跑完按字节还原并校验 sha256：
· M1 改回「返回当前尺寸」（缺陷本体）⇒ RED · M2 `ceil`→截断 ⇒ RED ·
· M3 只增不减（`max(内容高, 当前高)`）⇒ RED · M4 无测量器时**编造 0 高**（而非退回）⇒ RED。
基线 `487 passed / 0 failed`，还原后回到全绿。

★★ **本轮两个「仪器先错」的实例，都靠负对照抓出来，如实记账**：
1. **基线读错了版本**：第一次跑的是 `cmake-build-release-visual-studio/ecdi_tests.exe`（10-10 的陈旧
   产物，372 用例），真实产物在 `Release/` 子目录（487 用例）。⇒ 之后的每一步都以 487 为基线。
2. **M2 一度 STILL-GREEN**：`FakeTextMeasurer::LineHeight` 恒为 **16.0（整数）** ⇒
   `行数×16` 恒为整数 ⇒ `ceil` 是**恒等变换**，取整方向在既有夹具上**根本不可观测**。
   ⇒ 补了 `FractionalLineHeightTextBox`（行高 19.2，2 行 ⇒ 38.4）才让 M2 变红。
   ★ 这与 harness 侧 M3 的结论**同源**：**整数行高会让「取整方向」这个契约隐形**。

**本仓收敛（同日）**：`ComposerBox::ContentHeight` 的手算三行删除，改为
`GetPreferredSize().height`；`LineHeightNow` 删除；`DesiredInputHeight` 不再
`dynamic_cast`、不再自算 padding/行高。★ **`--grow-probe` 七相判据一字未改**，
高度读数与收敛前**逐位相同**（29/46/63/132/29/29/63，light/dark 一致）——
这正是收口的验收方式：行为不变，实现变干净。若哪一相变红，就说明框架的订正与旧行为不等价。


---

### REQ-13 窗口最小尺寸约束 —— **已闭合（ECDI 落地 + 本仓收敛）**

> ★ **触发（2026-10-21）**：用户要求给窗口加最小尺寸约束（UI 打磨清单第 2 项）。查过后确认框架侧无入口，
>   本仓只能在 `OnWindowResized` 里夹，**代价是可见的一次回弹**，故按铁律登记。
> ★ **归属判据**：已读 ECDI 源码仓与安装前缀的公共头，并做全仓（含文档）关键词扫描；
>   判据是**「该能力在框架里存不存在」**，不是「我方用法对不对」——本条属前者。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮新立；`status.txt` 的 MIN-SIZE 条目曾口头称它「an ECDI-side capability request」，但**从未落到本账本**，现补登） |
| 状态 | ✅ **已闭合**（ECDI 落地 `Window::SetMinimumSize`，本仓已收敛：删除 `OnWindowResized` 的 `ClampToMinimum` 绕行、在建窗时登记最小尺寸，2026-10-23） |
| 归属 | **ECDI 侧**（框架无该能力；非 harness 用法问题） |
| 现象 | 窗口可以被拖到任意小。ECDI **不拦**：① `include/ECDI/Window/Window.h` 的公开 API 里**没有任何尺寸约束函数**（`SetChromeMode`/`SetCaptionHeight`/`SetResizeInset`/`SetWindowLayer`/`SetWindowBackdrop`/`SetBackgroundColor`/`SetBounds`/`SetFocusedWidget`/`SetCaptureWidget`/`SetFileDropEnabled` 全表核对过，无名称为 `MinSize`/`MinimumSize`/`SetMinTrack` 者）；② `src/` 全树**不处理 `WM_GETMINMAXINFO`**——全仓（含 `docs/`）该串只出现 **1 次**，且是在 `Window.h:182` 的注释里说明 `SetBounds` 是「请求而非保证」时被顺带提及（原话：「超屏被移回 / 最大化态下受 `WM_GETMINMAXINFO` 影响」）⇒ **框架自己提到过这条消息，却从未拦截它** |
| ★ 为什么这构成缺陷而不是「设计选择」 | 拖拽缩放的最小尺寸是**操作系统在 `WM_GETMINMAXINFO` 里问出来的**，这是 Win32 唯一能在**用户松手之前**拒绝尺寸的钩子。不拦它 ⇒ 系统先改完尺寸、再派发 `WM_SIZE`，框架此时已经是「事后」⇒ **任何调用方都只能事后纠正**，而事后纠正的可见后果就是窗口先缩后弹 |
| ★ 调用方（本仓）现在怎么做 | ★★ **本节所述绕行已于 2026-10-23 删除**（`ClampToMinimum` 整段移除，改由框架拦，见下方闭合记录）；以下按登记时的原文保留，是**历史**。`gui/src/App.cpp` 的 `HarnessApp::OnWindowResized`：⓪ 步 `ClampToMinimum()`，超下限则重发 `SetBounds` 并**直接 return**（不排版），让 `SetBounds` 自己派发的那次事件来排版；第二轮 `want == have` 收敛，**恰好两轮不成环**。下限 `880x460` 是**推导**的：中栏头 `padding 14×2 + 标题 300 + 三钮 28×3 + 间隙 8×4 = 444`，两翼 `侧栏 200 + 检查器 230 = 430` ⇒ 客户区最小宽 `874`（取 880）；垂直固定带 `顶栏 36 + 会话头 62 + composer 92 + 状态栏 26 = 216`，视口至少留 244 ⇒ 最小高 `460` |
| ★ 可见代价（实测，非推测） | 把边框拖到下限以下时窗口会**先缩再弹回**——一次橡皮筋式的回弹，而不是在边框处硬停。这是「事后拒绝」的必然形状，不是实现没写好。消除它**只能**由框架在 `WM_GETMINMAXINFO` 阶段拦下 ⇒ 本条的全部理由 |
| ★ 修法候选 | ⓐ **`Window::SetMinimumSize(int w, int h)`**（或 `SetSizeLimits`）：配置期 API，与 `SetResizeInset` 同组语义；Win32 侧在 `Win32PlatformWindow` 的窗口过程里处理 `WM_GETMINMAXINFO`、填 `ptMinTrackSize`（**注意单位是物理像素**，需按当前 DPI 折算——`Window.h:179-181` 的 `SetBounds` doc 明写入参是「屏幕坐标（DIP）」「与 `Application::Create(width, height)` 同口径」，而 `Win32PlatformWindow.cpp:825` 的 `case WM_SIZE:` 里用的是 `PixelsToDip(LOWORD(lParam), dpi)` ⇒ **框架在 `WM_SIZE` 已经做过一次 px→DIP 折算**，新的 `ptMinTrackSize` 必须走反方向，这里很容易再次吃单位口径的亏）。同时建议在 `WindowResizedEvent` 文档里**声明**「有最小尺寸时 resize 事件不会报出小于下限的尺寸」，否则消费者仍不知道要不要自己再夹一遍 · ⓑ 只在文档里补一句「最小尺寸由调用方在 resize 回调里自行钳制」——**不解决问题**，回弹照旧，只是把代价写明白了 · ⓒ 不做 |
| ★ 不建议 | **不要**在 harness 侧再包一层 `WM_GETMINMAXINFO` 子类化窗口过程。ECDI 刻意不暴露 `HWND`：`include/ECDI/Platform/PlatformWindow.h:20`「Window 不接触 HWND/创建细节」（结构性约束，接口层就不带 Win32 类型）⇒ 绕过去要反查 `GetParent`/`EnumThreadWindows` 之类，**那正是这条边界想防的事**。★ 另注：`include/ECDI/Application/Application.h:78`「不绑定 Window、不暴露 HWND 或任何 Win32 类型（R3 / D1）」这句**出自 `PostToUi` 的 doc**（讨论异步投递为何不携带平台句柄），不是一条全局禁令——引用时不应把它当成窗口层的总原则，**真正撑住本条的是 `PlatformWindow.h:20` 与接口全表零 Win32 类型这两点** |
| ★ 我方本轮处置 | **2026-10-23 已收敛**（详见下方闭合记录）：登记时走 ⓒ 事后钳制（`ClampToMinimum`），框架落地后删除绕行、改在建窗处登记下限；`--resize-probe` 第 6 相由「证明我们推回来了」改写为两相反向取证。 |
| ★ 取证（本仓） | `.scratch/scan_min_size.py` 递减扫：`1120/1000/940/900` 按请求生效，`880` 以下全部回 `880x700`；边界截图 `.scratch/resize-shots/bound-880.png`（三钮齐全）vs `bound-860.png`（第三个 `···` **被裁掉**）；负对照 `.scratch/probe_minsize_negctl.py` 两条变异均 RED（M1 删宽度钳制 ⇒ `root width 640 is below the floor 880 ... middle column 210 < head requirement 444`；M2 闸门默认 false ⇒ 差异串以 `setup: the floor is disabled` 开头）——★ 上列 M1/M2 是**绕行时代的接缝**，`ClampToMinimum`/`m_minSizeEnforced` 已随收敛删除，脚本已于 2026-10-23 重写到新接缝（见闭合记录） |

★ **与 `REQ-12` 的形态对照（值得记）**：`REQ-12` 是「**同一框架两条规则矛盾**」——缺陷在框架内部，修答案即可；
`REQ-13` 是「**框架根本没有这条能力**」——不是矛盾，是缺项，只能在框架侧加 API。
判别方法也因此不同：`REQ-12` 靠**分支对读**（`TextBox` 与 `TextWidget` 的同名虚函数）判定，`REQ-13` 靠**全仓关键词扫描 + 公开 API 全表核对**判定。
★ 另注（**登记时的原文，2026-10-23 已过期**）：本仓对 `REQ-13` 的**绕行是可用且已验证的**，所以它不阻塞任何已规划功能——这一点与 `REQ-11`（当前零消费者）不同：
`REQ-13` 的消费者**现在就在**，只是被迫接受一个可见缺陷。★ 闭合后本仓不再有绕行，"消费者被迫接受缺陷"这半句随之失效（缺陷已由框架堵住）。


**★★ 闭合记录（2026-10-23，ECDI 落地 ⓐ + 本仓收敛）**

ECDI 采纳 ⓐ（`Window::SetMinimumSize(int w, int h)`）的裁决理由只有一条：**「客户区下限 ↔ 整窗尺寸」的换算
是框架规则，不是调用方的**。要换算就得同时知道三件事——当前 DPI、chrome 形态（`Borderless` 时
`WM_NCCALCSIZE` 把非客户区吞成 0，Normal 时有边框标题栏）、以及单位口径（`WM_GETMINMAXINFO` 收**物理像素**，
而框架公开几何 API 全用 **DIP**）。这三件事框架全知道、调用方一个都拿不到（`PlatformWindow.h:20` 的
「不接触 HWND/创建细节」边界）。⇒ 让调用方换算 = 让它在框架外复制一套框架内部规则，正是本条想防的事。
ⓑ（只写文档）不解决回弹，已排除。

ECDI 侧落地（`src/Platform/Win32/Win32PlatformWindow.cpp`，公共 API **+1**）：

| 契约 | 落地方式 |
|---|---|
| 口径 | 入参 = **客户区 DIP**（与 `GetClientSize` 同口径）；逐轴独立，**`<= 0` = 该轴不设下限** |
| **两路拦截** | ① `WM_GETMINMAXINFO` 填 `ptMinTrackSize`（`DipToPixels` 按 `GetDpiForWindow` 折算，`break` 交 `DefWindowProc` 补齐 `ptMaxSize` 等其余字段）——这是**鼠标拖边框**唯一能在松手之前拒绝的钩子 ⇒ 硬停、不回弹 · ② `WM_WINDOWPOSCHANGING` 夹 `cx/cy` ——★ **① 只在带 sizing frame 的窗口上被系统采用**（实测：`probe_minmaxinfo` 覆盖矩阵，Borderless 无框窗口单靠 ① 拦不住 `SetWindowPos`），故编程尺寸请求走 ② |
| 换算归属 | `MinimumTotalSizePx()`：`Borderless` ⇒ 客户区即整窗（差值恒 0，**不能**按样式位换算，否则凭空加出一圈边框）；`Normal` ⇒ `AdjustWindowRectExForDpi(0×0 矩形)` 取边框差值 |
| **运行期有效** | 刻意**不**仿 chrome 四件套的 `m_shown` 闸门——四件套是「一次确定」的状态机，本方法只是一份被读取的配置，没有需要一次确定的东西。★ 附带必要性：**运行期可清零**才能造出「低于下限」的对照世界（见下方 ⑥ 相） |
| 不越权 | 登记/清零下限**不**主动改当前尺寸、不发 `SetWindowPos`「刷新」——`ptMinTrackSize` 每次 `WM_GETMINMAXINFO` 现读。`SetBounds` 的请求语义照旧，本方法**不**替调用方放大窗口 |
| 单元测试 | `WindowChromeTests.cpp` 三个真窗口用例：`WindowMinimumSize.ClampsProgrammaticResize`（`:632`）· `NormalUsesClientArea`（`:678`，整窗 > 客户区下限且客户区**逐位等于** 500×400）· `PerAxisIndependent`（`:703`，`(500,0)` 只夹宽、`(-10,-10)` 两轴皆放行）。★ 三条替身（`AnimationTests` / `ProgressBarTests` / `RingProgressBarTests`）各补一条空实现——纯虚新增的连带改动 |
| 文档 | `docs/usage.md:95` 运行期 API 段新增条目（客户区 DIP / `<=0` 语义 / 夹住在生效之前 / 不替调用方放大窗口） |

★ **负对照（证明测试不是装饰）**：`.scratch/probe_minsize_negctl.py`（**本轮重写到新接缝**）——旧的
两条 needle 指向已删除的 `ClampToMinimum` 与 `m_minSizeEnforced`，若不改写会永远 `SKIP: needle hits=0`，
而**那看起来像一次干净的通过，其实什么都没测**（正是 §6-④「仪器里藏了没写出来的假设」的同族）。现在两条变异：
· **M1** 删建窗处的产品登记 ⇒ `min-size` 相 RED：`root is below the floor -- the minimum size did not hold; ...`
· **M2** 删探针里的 `SetMinimumSize(0, 0)` ⇒ `min-size-off` 相 RED：`setup: with the floor cleared the request did NOT land at 640 -- the probe cannot produce a below-floor world`
每条跑完按字节还原并校验 sha256（当前 `App.cpp` = `f3b3ad2a…5b77`，`crlf=0`）；两条 **PASS**。
ECDI 侧基线：收敛前 `487 passed / 0 failed` → 加三条用例后 `490 passed / 0 failed`。

**本仓收敛（同日）**：
1. **删除** `OnWindowResized` ⓪ 步的 `ClampToMinimum()` 整个函数与其调用（原函数位置留注释说明为何删：
   两处都夹 ⇒ 谁也证明不了自己有用）。`OnWindowResized` 现在**直通** `effW = width, effH = height`。
2. 登记点前移到建窗处：`Run()` 的 `win.SetChromeMode(...) / SetCaptionHeight(...)` 之后、`Show()` **之前**，
   `win.SetMinimumSize(kMinClientW, kMinClientH)`（`880×460`，推导过程见上表「调用方现在怎么做」行——
   那个数字仍是本仓的产品决定，只有** enforcement** 归了框架）。1120×760 本就在下限之上，不改任何既有读数。
3. **`--resize-probe` 第 ⑥ 相改写为两相**（`App.cpp:5556` 起）：`min-size` 请求 `640×360` ⇒ 断言根控件
   **逐位等于** `880×460`（不是 `>=`：落在 800/1000 之类说明拦住它的不是下限本身），并保留中栏 ≥ 444 的
   头部完整性判据；`min-size-off` 先把下限清零 ⇒ 同一请求**必须真的**落成 `640×360`。
   ★ 两条**相反**方向的断言同时成立，才排除「下限不存在」与「下限无处不在」两种退化——
   改前那一相断言的是自己写的 `max(880, x)`，是**同义反复**。
4. 全量回归：`tools/regress.py` **PASS**（6 夹具结构读数逐项等值 + 13 探针全绿 + `protocol/validate.py` exit=0）；
   `--resize-probe` 7/7。★ 其余各相判据一字未改。

★ **如实记一条未自动化覆盖的**：鼠标拖边框的「硬停不回弹」由 `WM_GETMINMAXINFO` 的语义 + `probe_minmaxinfo`
覆盖矩阵保证，但探针只能走编程路径（真 `SetBounds` → 真 `WM_SIZE`），**人手拖拽未做自动化验证**。
  ★ 且已实测确认**这条路在本环境补不了**：`probe_drag2`（`.scratch/probe_minmaxinfo/`）用 `SetCursorPos` + `mouse_event`
  合成输入——点客户区、点右下角边框、按住拖动 20 步——**一个事件都没进窗口过程**（`click_client`/`click_border_br`/`drag_br`
  全部 `events=[]`，且 `foreground=0`：注入输入进不了非前台窗口）。★ 作为对照，程序化 `PostMessage(WM_SYSCOMMAND, SC_SIZE)`
  **能**进（`SYSCOMMAND→ENTERSIZEMOVE→一串 NCHITTEST`），说明窗口本身活着、是**注入通道**不通。⇒ 这条缺口不是「忘了测」，
  是**仪器够不着**；按「没测到 ≠ 没问题」如实挂账，等有真实输入通道再补。


---

### REQ-14 单行 `TextBox` 恒顶部对齐 + 唯一旋钮 `padding` **三耦合** —— 「上下居中」与「文字不撞圆角」在框架里**都没有入口**

> ★ **触发（2026-10-26）**：用户报两件事——① 输入框文字需**上下居中**（观感）；② 最左侧文字**会撞圆角**。
>   查后确认两者在框架侧**都缺入口**：既无垂直对齐字段，也无「文本内缩 ↔ `cornerRadius`」的关联。
>   本仓只能靠「把高度做成恰好一行」把顶部对齐**挤成事实居中**，且必须同时加大 `padding` ⇒ 左右内缩被动变大。
> ★ **归属判据**：已读**基类公共头**（`Widget.h` / `TextWidget.h` / `TextBox.h`）+ **ECDI 源码仓**
>   （`TextBox.cpp` 的 `OnPaint` / `GetTextLeftInset` / `GetTextAreaHeight` / `GetTextAreaWidth`），并对
>   `include/` + `src/` 全树 grep `verticalAlign|VerticalAlign|textAlign|TextAlign` 核对——命中者只有
>   `ButtonStyle::textAlignment` 与 GDI 的 `SetTextAlign`，**TextBox 侧零对齐字段**。
>   判据是**「该能力在框架里存不存在」**，不是「我方用法对不对」——本条属前者（同 `REQ-13` 形态）。

| 项 | 内容 |
|---|---|
| 原编号 | （无——本轮新立。★ 登记前枚举过全集：`REQ-07` 已复核否决、`REQ-12` 是另一件事（多行 `GetPreferredSize` 语义，已闭合）、`REQ-13` 已闭合 ⇒ 编号顺延至 **14**） |
| 状态 | ✅ **已闭合（2026-10-27，ECDI Phase 37 `verticalAlign` 当日落地 + 本仓 7 框收敛，见下方闭合专行）** |
| 归属 | **ECDI 侧**（能力缺项；非 harness 用法问题——ECDI 侧已裁决并按 ⓐ 落地） |
| 现象 | ① 单行 `TextBox` 的文本**恒贴顶**：`textY = fy + inset`（`TextBox.cpp:1338`）、`inset = m_style.padding`（`GetTextLeftInset()`，`:493-498`）——`OnPaint` **没有单行分支**，与多行走同一条顶部对齐路径（多行顶部对齐是对的，单行不是）；② 文本**左内缩只有一个来源**（`textX = fx + inset`，`:1337`），而 `cornerRadius` 只参与**绘制**（`:1281`），与内缩**互不知道** ⇒ 想让文字躲开圆角，只能加大 `padding` |
| ★ 唯一旋钮的**三耦合** | `padding` 一个值同时决定三件事：**X 内缩**（`textX`，`:1337`）· **Y 内缩**（`textY`，`:1338`）· **裁切高**（`PushClip` 用 `GetTextAreaHeight() = H − padding×2`，`:1343` / `:500-503`）。⇒ ① 想只调一个轴**必然动另一个**；② **越界即裁字**：`padding×2 > H − lineH` 时裁切框比行还矮，字形被上下切掉——这是「唯一旋钮」自带的可触发缺陷，不是假想 |
| ★ 与 ECDI 自家契约的关系（**请 ECDI 侧裁决这一行**） | `TextBox.h:79` 写明单行契约：「内容测量 + padding×2（§3.6 验证项：**height = lineH + padding×2 → 自然垂直居中**）」⇒ 框架立场是**由调用方把尺寸做对**，顶部对齐 + 等分 padding 恰好等价于居中。★ 本条的诉求不是「算错了」，而是**这个契约在 UI 里不可用**：输入框高度通常由视觉/布局目标决定（本仓实测 29 / 30 / 26 px），一旦偏离 `lineH + 2×padding` 就**没有任何合法手段**居中。反过来说：`GetPreferredSize()` 给的正是那个「恰好」值 ⇒ **只有采用 preferred 高的调用方才是安全的**，其余一律偏心 |
| ★ 框架内部不一致（旁证） | 同一个 `TextWidget` 基类下：`Button` **垂直恒居中**（`TextWidget::CalculateTextPosition` 的 `offsetY = (H − lineH)/2`，`TextWidget.cpp:361`；`ButtonStyle.h:32` 明写「垂直恒居中 P7 不动」），`TextBox` 却**恒顶部对齐、且连水平对齐都没有**。`TextBox::OnPaint` 的注释（`:1334-1335`）自称「与 8.5.1 单行版 `CalculateTextPosition` **同构**」——**只在 `padding == (H − lineH)/2` 时成立**，那正是上一条契约的前提 |
| ★ 圆角侧的现成先例 | 框架**已经**在**滚动条**上处理过「不要压住边框环与圆角」：`LayoutScrollBar` 四边内缩 `= max(padding, 2)`（`TextBox.h:349-351`）。⇒ 「文本不压圆角」有同构落点，只是文本侧**没有对应处理**；且那个 `2` 是常数、**不是从 `cornerRadius` 推的**（本仓圆角 7–9，`2` 不够） |
| ★ 我方现在的唯一绕行（有代价） | 两式必须**同时**成立：`padding = cornerRadius + 2` **且** `H = lineH + 2×padding`（后者令裁切高恰好 = `lineH`，既居中又不裁）。★ **代价三条**：聊天框 29 → 39（**+10px，吃掉转录区**）、左右内缩 6 → 11；设置三框 30 → 35、内缩 0 → 9；且 `lineH` 随 DPI/字种漂（17 只是 96dpi 的读数）⇒ 高度必须**测量得出**、不能写死（同 `REQ-06` / 纪律 14 的坑） |
| ★ 修法候选 | ⓐ **`TextBoxStyle::verticalAlign`**（`Top`/`Center`/`Bottom`，**显式缺省 `Top`** ⇒ 零行为变更）：单行与「内容不足一屏」时按 `(H − contentH)/2` 放线箱。★ 有现成先例可抄：`Core/TextAlignment.h` + `ButtonStyle::textAlignment`（连 C30-3 的教训一起抄——**不得依赖枚举零值**，缺省必须显式注入） · ⓑ **拆 `padding` → `paddingX` / `paddingY`**（或保留 `padding` 作两轴默认值）：本条第一诉求（躲圆角）**只要 X**，现在却被迫连 Y 一起改 · ⓒ **内缩取 `max(padding, cornerRadius)`**——照 `LayoutScrollBar` 的先例把常数 `2` 换成真半径 · ⓓ 只在文档里把契约写清楚：**不解决**（本仓四处里三处 `padding = 0`，且宽度方向的圆心不会自己变大） |
| ★ 判据（本仓侧可行性，如实） | 「居中」**可编程断言**：单行时 `GetHeight() == GetPreferredSize(...).height` ⇒ 线箱上下内缩相等（`GetPreferredSize` 是公开的）。★ 但「内缩 ≥ 圆角」**当前无法断言**——`GetTextLeftInset()` / `GetTextAreaHeight()` 都在 **protected** 区，公开侧没有 `GetPadding()`。★ 若采纳 ⓐ/ⓑ，建议同批补 `GetPadding()`（或 `GetVerticalAlign()`），本仓 `--grow-probe` 就能把两件事都变成门里的判据 |
| ★ 不阻塞 | 有绕行（见上），但有可见代价：要么框变高 10px、要么更缩进、要么偏心。与 `REQ-13` 登记时同形——**消费者现在就在**（4 处），被迫接受一个可见缺陷 |

★ **四处实测（2026-10-26；尺寸取自本仓常量，偏移量由 `H − 2p − lineH` 直接相减，并与截图互证）**

| 输入框 | H | `padding` | 圆角 | 线箱偏移 `H − 2p − lineH` | 左内缩 vs 圆角 |
|---|---|---|---|---|---|
| 聊天框 `ComposerBox`（`PromptBar.cpp:139`） | 29 | 6 | 9 | **0** ⇒ 事实上居中（**巧合**：29 = 17 + 6×2） | 6 **< 9** ⇒ 临界，最上笔画距弧约 1px |
| 设置 BaseURL / API Key / 模型名（`AppUiMain.cpp:555` 起，`SetSize(500, 30)` 在 `:577`） | 30 | 0 | 7 | **13 全在下方** ⇒ 明显偏上 | **0 < 7** ⇒ 笔画直接压在描边上 |
| 设置挡位数值框（`AppUiMain.cpp:619`） | 26 | 0 | 7 | **9 全在下方** | 0 |
| 模型浮窗搜索框（`AppUiMain.cpp:925`） | 26 | 0 | 0 | **9 全在下方** | 无圆角，仅偏心 |

★ 取证方法：真窗口（`--theme light|dark`）+ GDI 截客户区 → 在控件矩形内做字形极性图 / 墨迹包围盒
（`.scratch/_geom2.py` · `_map.py` · `_analyze3.py`；`.scratch/` 不入库）。`lineH = 17` 来自 `--grow-probe`
baseline（29 = 17+6×2 / 46 = 17×2+12 / 63 = 17×3+12）。
★ **一条自我订正（写下来防复发）**：**墨迹上下间隙差 ≠ 偏心**——17px 线箱内 ascent/descent 本就不对称
（聊天框墨迹 10/7 但线箱偏移 = 0）。判偏心只能看**线箱**位置，即上表第 5 列。用墨迹包围盒判居中会把
「字形不对称」误读成「对齐错误」。

★ **与相邻条目的形态对照**：`REQ-12` = 「同一框架两条规则矛盾」（缺陷在内部，修答案即可）· `REQ-13` = 「框架根本没有这条能力」（纯缺项）·
**`REQ-14` = 两者混合**：`TextBox` 无对齐能力（缺项），而框架用一个**文档化的等价契约**（尺寸做对即居中）把缺项
掩盖成了「设计」。⇒ 请 ECDI 侧**先裁决契约是否可接受**，再谈实现：若认定「调用方必须用 preferred 高」成立，
本条应结案为**已知现状**（同 `REQ-06` ③ 的处理），本仓改为全量改用 preferred 高并接受布局代价。

### ★ REQ-14 闭合专行（2026-10-27，ECDI Phase 37 + 本仓收敛）

> **ECDI 侧裁决 = 采纳 ⓐ，且形态比登记稿更完整**（三批 `074b8aa` / `ac80645` / `6a3807b`，MinGW→MSVC
> 五链 513/513，详设 v1.2 全链收口，deferred #76 关闭）：
> ① **新公共头 `Core/VerticalAlignment.h`**：`Top = 0`（缺省 = Phase 37 前逐位不变）/ `Center = 1`
>   ——与登记稿 ⓐ 的「显式缺省 Top ⇒ 零行为变更」一致；与 C30-3 **不同**（这里缺省与零值相同，
>   显式注入只是纪律），且文档写明前瞻契约：将来改主题缺省为 Center 会立即触发 C30-3。
> ② **`TextBoxStyle::verticalAlign` + override 字段**（置于 `borderColor` 之后规避 C7560 乱序）。
> ③ **`GetTextTopInset()`**（protected，「不扩公开面」；不变量 1 = `Top` ⇒ 恒等于 `GetTextLeftInset()`
>   的**结构性保证**——快路径根本不读行数；不变量 2 = 超屏退化 `max(0,·)` ⇒ 与改前逐位同）。
> ④ **X/Y 单一来源拆分**（`OnPaint` insetX/insetY、`CalculateCaretPosition` △8、
>   `CaretIndexFromPosition` △9，守卫注释明写「禁止合并回一个函数——那正是三耦合的根因 N1」）；
>   **clip 顶不随居中外缩**（`areaTop = fy + insetX`，C37-3）⇒ 居中不会把字推进焦点环。
> ⑤ 登记里请补的 `GetPadding()` **没加**（公开面不变）——但不需要：单行居中时 padding 在代数上
>   消去（`p + (H−2p−lineH)/2 = (H−lineH)/2`），居中判据**不依赖 padding 访问器**即可断言
>   （见下）。「内缩 ≥ 圆角」仍无公开断言口，登记原样保留。

**本仓收敛（同日）**：7 个单行消费者 `SetStyle(.verticalAlign = Center)`——composer（`PromptBar.cpp`
ApplyBorder，焦点两态共用一入口）· 设置 BaseURL/API-key/模型名 · 两个挡位框（`AppUiMain.cpp`）·
模型浮窗搜索框（只覆写对齐一项，其余吃主题缺省）。composer 29 = 17+6×2 本是**巧合居中**，Center 化
把它钉成**契约居中**——将来 padding/行高漂移（DPI/字种）不再依赖 29=17+6×2。多处实测行高读数
**16**（设置框默认字体）而非登记估算的 17——正是登记自己警告的「17 只是 96dpi/聊天字体的读数，
必须测量不得写死」；本仓判据用 `GetCaretClientGeometry().rect.height` 同源推导，未硬编码。

**机读判据 = 新探针 `--valign-probe`（7/7 green，已进门 regress.py EXTRA_PROBES）**：
- **读回仪器 = 公开 API `GetCaretClientGeometry()`**（非探针后门）：`rect.y − 控件绝对 y ==
  GetTextTopInset()`（Phase 37 △8 的 Y 原点，与绘制/点击同源）；`rect.height == GetLineHeight()`。
  绝对坐标相减相消 ⇒ 隐藏浮层里的搜索框同样可读，且读的是**行箱**（登记的自我订正：墨迹包围盒
  不能判居中）。★ `Widget::GetWindow()` 是 protected——探针第一次接线在那里撞了 C2248，
  去掉越权守卫后由探针顶部的 `m_window` 前置守卫兜住。
- **判据**（无需知道 padding）：交付态（Center）读数 == `(H − lineH)/2`；**翻转臂**（SetStyle 只覆写
  `verticalAlign = Top`，读完即翻回）给出**剂量对照**：设置挡位框 26px 读数 **Top=0 → Center=5**，
  恢复臂 == 首测；**composer 恰满盒两臂相等（6 == 6）**——相等本身就是「Top 时也居中」的交付事实，
  两臂差异 = 内容是否超屏的函数，判别力直接可读。
- **回归**：6 夹具逐位、25 探针仅设计红、`--grow-probe` 9/9 且 baseline **逐位不动**
  （inputH=29/barH=81/hintY=43）——composer Center 化在 preferred 精确配高下**几何零扰动**
  （精确配高 ⇒ 余量恒 0；30 行封顶时 `max(0,·)` 退化回 Top）。
- **人验截图**：设置面板四框文本居中可见（URL/模型名/挡位/API Key 掩码点列）——改动前文本贴顶、
  下方空 13px。`.scratch/req14-settings.png` 等 3 张。
- **换库**：MSVC Release lib（SHA256 `a8822fbc…b7fbcb`，11,696,664 B）经 `cmake --install`
  入 `third_party/ecdi`，含新头 `VerticalAlignment.h`；库内符号抽查（`GetTextTopInset` ×3 /
  `VerticalAlignment` ×4）。同日**构建器迁 Ninja**（同一 cl.exe 工具链，静态库 ABI 不变；
  msbuild `--clean-first` 纪律由 Ninja 依赖追踪 + 全量重建替代，全量日志 62 行零警告）。
- **顺手发现（同日闭合，2026-10-27）**：模型浮窗搜索框从未设 `.background`（P4 H 起缺省）⇒ 深色主题下
  白底——与 2026-10-26 修设置框「灰底+描边」同族的一处漏网。**已修**：用户定夺「补背景 + 所有输入框
  深色下白字」。落法（全在 harness 侧，零框架改动）——
  ① 搜索框与设置面板输入框**同款**：`.background=kPanel2 / .border=kLine / .borderColor=kLine /
     .cornerRadius=kRadiusBtn / 1px`（`.borderColor` 必须显式给，缺省透明 ⇒ drawBorder 恒假，
     即 2026-10-26 那条「设了 borderWidth 以为描边在」的同因）；上方 REQ-14 段里「其余全吃主题缺省」
     一句的**缺陷面**由此订正——吃缺省吃的就是白底黑字。
  ② **文字色**：ECDI `TextStyle.foreground` 全局缺省 **Black**（Theme.h 头注 + ButtonStyle.h:13），
     而本仓 5 个输入框（composer · 设置 BaseURL/模型名/API Key · 两挡位框 · 浮窗搜索框）**没有一个**
     调过 `SetTextColor` ⇒ 深底黑字。全部补 `SetTextColor(kDarkInk)`（深色 230,233,239≈白 ·
     浅色 35,38,46 仍深墨）——按令牌纪律不写死白，浅色主题自动不回归。
  ③ composer 底从 `kInputBg`（深色=纯白一块）换 `kPanel2`，与其余输入框同族；`inputBg` 令牌
     自此零消费者 ⇒ 从 Theme.h 删除（引用别名纪律：令牌表只活在主题里的字段不留）。
  **回归**：`tools/regress.py` RESULT: PASS（6 夹具逐位 · 探针仅设计红 · `--valign-probe` 7/7
  读数与颜色无关不受扰动）。**人验截图**：深色 `req14-modelpopup/settings/composer` 同款机位重拍
  （`.scratch/inputfix-*.png`）⇒ 搜索框/挡位框/composer 全部深灰底白字带描边；浅色同法
  （`.scratch/lightfix-*.png`）⇒ 浅灰底深墨字，无回归。

---

### REQ-15 🔄 `TextBox` 不支持按宽度自动软折行（编辑器内 Word Wrap / Phase 29 O2 挂账项）

> ★ **触发（2026-10-28）**：用户核对「富内容三项」§A（多行输入）时明确诉求为**连续输入超宽时自动折行（Soft Wrap）并撑高输入框**，而非仅靠 `Shift+Enter` 插入硬换行 `\n`。
> ★ **归属判据**：已查基类公共头 [`TextWidget.h`](../third_party/ecdi/include/ECDI/Widget/TextWidget.h) 与派生类公共头 [`TextBox.h`](../third_party/ecdi/include/ECDI/Widget/TextBox.h)。虽然基类 `TextWidget` 自 Phase 29 具备 `SetWordWrap(bool)` 与 `BuildTextLayout`（供 `Label` 使用），但 `TextBox` 的行缓存、测量、绘制、光标几何、鼠标点击命中与上下键导航全部绑定在硬换行缓存 `m_lineStarts`（仅扫描 `\n`）与水平跟手滚动 `m_scrollOffsetX` 上，属于 ECDI 侧已挂账的 **Phase 29 O2（编辑器内 wrap）** 能力缺口。

| 项 | 内容 |
|---|---|
| 原编号 | ECDI Phase 29 **O2**（参见 `TextBox.h:77` 与本账本 `REQ-12` 表格第 6/7 行） |
| 状态 | ✅ **已闭合**（2026-10-31 当日：本仓四项全部兑现并验证通过——① 换装 Phase 42 前缀（含 Phase 38）· ② `ComposerBox` 构造与 `PromptBar` 显式开启 `SetWordWrap(true)` · ③ `--grow-probe` 新增 `wrap-grow` / `wrap-shrink` / `wrap-resize` 进门，实测 12/12 全绿（baseline 逐位未动 `inputH=29 / barH=81 / hintY=43`，`wrap-grow` inputH=63, `wrap-shrink` inputH=29，`GetScrollOffsetX` 恒为 0）· ④ 六夹具结构基线保持 100% 命中） |
| 归属 | **ECDI 侧**（`TextBox` 内部行模型 `m_lineStarts` 仅按 `\n` 切分；非 harness 侧漏调 `SetWordWrap(true)`） |
| 现象 | 用户在 `ComposerBox`（`SetSingleLine(false)`）中连续打字或粘贴不含 `\n` 的长段落时，文本到达输入框右边界后**不会折到下一行**，而是触发水平滚动（`m_scrollOffsetX` 跟手右移），输入框高度保持单行 `29px` 不变 |
| ★ 框架头内事实（已核实） | ① `TextBox.h:77-78` 明写：`③ 行数口径 = \n 数 + 1，与 RecalculateLines 同源——编辑器内 wrap（O2）兑现时只需改这一处真相源`；② `TextBox.h:261`：`RecalculateLines()` 注释明写「重算行起始缓存（扫描 `\n`）」；③ `TextBox.h:352`：`m_scrollOffsetX` 注释明写「水平滚动偏移（像素；跟手模式——光标边界推导，clamp ≥ 0，无上限）」 |
| ★ 为何不能在 `TextBox` 上直接调 `SetWordWrap(true)` | `TextBox` 虽继承 `TextWidget::SetWordWrap`，但 override 了 `GetPreferredSize()` 与 `OnPaint()`：① 无 `\n` 时 `TextBox::GetPreferredSize()` 若走基类 wrap 会报多行高，但 `TextBox::OnPaint` / `RecalculateLines` / `CalculateCaretPosition` / `CaretIndexFromPosition` 仍按 `m_lineStarts`（仅 `\n`）单行水平绘制 ⇒ **框变高了、字却还在第一行横滚**（测量与绘制/光标分裂）；② 含 `\n` 时 `MultilinePreferred()` 直接按 `m_lineStarts`（`\n` 数 + 1）算高，完全不读 `m_wordWrap` |
| ★ 为何严禁在 harness 应用层做「自动塞 `\n`」绕行 | 若在 `ComposerBox::OnTextChanged` 按像素宽往 `m_text` 里硬插 `\n`：① **污染发送原文**（用户写的一句话被切成带硬换行的多段发给 LLM / 写入转录与 journal）；② **窗口缩窄/拉宽或删字时无法重排**（硬 `\n` 与用户手敲的 `Shift+Enter` 无法区分）；③ **打碎 Undo/Redo 快照与 IME 组合区间**（`m_compositionStart` 码点偏移错位） |
| ★ ECDI 侧实现要点（供 ECDI 侧参考） | 当 `IsWordWrap() && !IsSingleLine()` 且存在有限文本区宽 `GetTextAreaWidth() > 0` 时：① **视觉行缓存（Visual Lines）**：`RecalculateLines()` 在每个 `\n` 硬段内复用或同构于 `TextWidget::BuildTextLayout`（`FitText` 逐行切分，保留软折行与硬换行边界，注意软折行**不**吞掉行尾空格码点索引，以免光标落在空格处失踪）；② **高度同源**：`MultilinePreferred()`（以及开启 `wordWrap` 时的单硬段分支）统一用 `视觉行数 × LineHeight + padding×2`；③ **光标与导航同源**：`LineIndexFromCodepoint`、`LineRange`、`CalculateCaretPosition`、`CaretIndexFromPosition`、`MoveCaret(Up/Down/Home/End)` 全走同一份视觉行表，且软折行开启时 `m_scrollOffsetX` 恒钉为 `0` |
| ★ 本仓收敛预案（待 ECDI 兑现后） | ① `ComposerBox` 构造时加一行 `SetWordWrap(true)`；② `--grow-probe` 新增 `wrap-grow` / `wrap-shrink` / `wrap-resize` 相：输入一段不含 `\n` 的超宽单行文本 ⇒ 断言 `bar->inputHeight() > singleH` 且 `GetScrollOffsetX() == 0`，删短后缩回 `singleH` |

---

### REQ-16 ✅ 按钮快速进出时背景过渡动画未取消导致 hover 残留

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-01 用户反馈（「鼠标如果经过的特别快的话会出现鼠标已经远离该控件，但是依旧为鼠标悬停在上面的效果」） |
| 状态 | ✅ **已闭合**（2026-11-01 当日：ECDI `Button.cpp:AnimateBackgroundTo` 修正 `target == m_displayedBackground` 分支漏 Cancel 旧动画的缺陷；ECDI 侧新增受控单测 `Widget.ButtonFastHoverLeaveCancelsAnimation` 验证推进 300ms 终态不漂移；harness 重新链接最新 `ECDI.lib` 通过） |
| 归属 | **ECDI 侧**（`Button::AnimateBackgroundTo` 动画状态机退出路径漏取消排队中动画；非 harness 控件派发缺陷） |
| 现象 | 鼠标高速掠过按钮（在动画第一帧未产生有效数值插值前移出）时，按钮在鼠标已经远离后依然永久保持高亮悬停效果 |
| ★ 根因（已核实到源码） | `Button.cpp` 的 `AnimateBackgroundTo(const Color& target)` 开头写了 `if (target == m_displayedBackground) return;`（防空转）。当鼠标极快进出时：① `OnMouseEnter` 提交了指向 `hoverBackground` 的动画，此时 `m_displayedBackground` 仍为 `normalBackground` 初值；② 紧接着的 `OnMouseLeave` 算出的目标为 `normalBackground`，此时 `target == m_displayedBackground` 命中并直接 `return`；③ **直接 return 未调用 `AnimationManager::Cancel(m_backgroundAnimToken)` 取消旧动画**，导致走向 hover 的旧动画在后续 Tick 中继续插值并在 200ms 后把按钮强制变亮停在 hover 态 |
| 修复措施 | 在 `target == m_displayedBackground` 提前返回分支内，前置 `if (window != nullptr) window->GetAnimationManager().Cancel(m_backgroundAnimToken);`，彻底消除旧动画残留 |
| 验证结果 | ECDI 侧单测 `Widget.ButtonFastHoverLeaveCancelsAnimation` 挂载真 Window、模拟连续 Enter+Leave 并以 10 帧 Tick 推进 300ms，验证呈现值恒保持 `normalBackground` 成功；全量回归测试套件通过 |

---

### REQ-17 ✅ IME「无组合直插」在 ECDI 侧没有通路 —— 中文全角标点（，。）

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-04 用户反馈（「我输入框输入不了中文的逗号和句号」） |
| 状态 | ✅ **已闭合**（2026-11-04 当日闭环：ECDI 侧 **#86** 三处修复 → 本仓换库重链 → 回归 PASS → **20:19 用户实测中文标点能打入**） |
| 归属 | **ECDI 侧**（平台消息覆盖缺失）。已按纪律查过 `Widget.h` / `TextWidget.h` / `TextBox.h` **与 ECDI 源码仓全库**（`D:\Project\ECDI`，含其自带测试与设计文档） |
| 现象 | 输入框里打中文逗号 / 句号不上屏；**汉字正常**（走组合上屏）；英文 `,` `.` 正常（走 `WM_CHAR`） |
| ★ 已核实①（源码） | ECDI 全库**零** `WM_IME_CHAR` 引用（`git log -S "WM_IME_CHAR" -- ECDI/src` 全历史 = 空 ⇒ 从来没实现过，不是回归）。`WindowMessageHandler.cpp:276-289` 只把 `WM_CHAR` / `WM_SYSCHAR` 翻成 `CharInputEvent`；`WM_IME_CHAR` 未列 ⇒ 落到 `DefWindowProc`，而它对该消息**不做任何插入**。 |
| ★ 已核实②（源码 + 设计文档） | `TextBox::CommitComposition` 首行 `if (!m_isComposing) return;`（`TextBox.cpp`，8.5.1 模型 B）；设计文档 `docs/dev/phase8.5.1-text-system2.0-detailed-design.md` §F8 把它写成**判据**：「未组合时 CommitComposition → m_text 不变」⇒ 「无组合的结果文本」是**按设计丢弃**的。 |
| ★ 已核实③（源码） | 平台拿到 `GCS_RESULTSTR` 后置 `m_imeResultPendingChars = result.size()`（`Win32PlatformWindow.cpp:1081`），随后系统补发的 `WM_CHAR` 会被**故意吞掉**（8.5.1 双写修复，`HandleMessage` 前置拦截 `:1184`）⇒ ②吞掉之后，**连 `WM_CHAR` 兜底也没有**。 |
| ⇒ 合计结论 | **任何「无组合的 IME 直插」在 ECDI 里都被静默丢失**：没有 `WM_IME_CHAR` 通路、吃不到 `WM_CHAR` 兜底、`CommitComposition` 又按 F8 主动 no-op。三条任缺其一都不会丢字——三条同时在，丢字就必然。 |
| 影响面 | 阻断「真拿它打字」：中文写作离不开标点。属**可用性阻断项**，不是观感项 ⇒ 按 2026-11-04 的新优先级（能投入使用 > 采数据）当日即修，未排期 |
| ⚠️ 原「未定项」→ 已收敛 | 微软拼音对全角标点究竟走 `WM_IME_CHAR` 还是「不带 COMPSTR 的 `RESULTSTR`」——**ECDI #86 把两条都堵了**，故它不再是修复前提；实际走哪条由人工打一次「，」顺带记录。 |
| ✅ 修复措施（ECDI **#86**，三处） | ① `WindowMessageHandler.cpp` 新增 `case WM_IME_CHAR`（与 `WM_CHAR/WM_SYSCHAR` 共用 `ConsumeCodeUnit` → `CharInputEvent`），且**刻意 `return 0` 而非 `nullopt`**：非 Unicode 语义下 `DefWindowProc` 会把它拆成两条 `WM_CHAR`（二次插入），不能依赖「我们是 Unicode 窗口」这个前提。② `TextBox::CommitComposition` 把 F8 判据**收窄**为「**无组合 ∧ 结果为空** ⇒ no-op」；有结果的无组合提交走 `InsertText`（进 Undo / `RaiseTextChanged` / 光标跟随）。③ `Win32PlatformWindow::HandleMessage` 的吞字计数器改为对 `WM_CHAR` **与** `WM_IME_CHAR` 同时递减——只认前者的话，新增通路后重复序列会逃过吞字，「丢字」被换成「双写」更糟。 |
| 验证结果 | ECDI 侧自带测试同批补齐（`EventTests` / `TextBoxTests` / `WidgetTests` +133 行）；本仓换库重链：`third_party/ecdi/lib/ECDI.lib` sha256 `3866a17d…`（11974558 B · 11-04 20:04 构建），harness `808f1903…`（2122752 B · 20:06 重链）；**头文件零漂移**（`diff -rq` 只报安装期生成的 `Core/version.h`）⇒ 纯 .cpp 修复，全量重编不必要（`ninja` 只跑 `[1/2] Linking`）。`tools/regress.py` **RESULT: PASS**（7m47s，39 条 `rc=0` 零失败，指纹 `808f1903`），日志 `.scratch/_regress86.log`。★★ **人工验收通过（2026-11-04 20:19，用户实测）**：「中文标点能打入了」——输入框里中文逗号 / 句号正常上屏。★ 注：**IME 无法自动注入**（注入输入进不了非前台窗口），故这一条只能人工判，机器判据到「库里含 #86 三处 + 回归不红」为止；本条**不再有机器可补的缺口**。 |
| ★ 同批库里的另一个修复（非本条） | ECDI **#85**：最小化 → 还原/最大化后客户区被 DWM 当作帧延伸区做玻璃合成（浅色主题整窗发白、重画不自愈）；修法 = 离开最小化态且 `Borderless` 时 `SWP_FRAMECHANGED` 重算非客户区，且**不动** `{0,0,1,0}` 边距（Phase 32 材质/阴影结论保持原样）。与本条同批入库，这里只记出处，**不另立 REQ**。 |

---

### REQ-18 ✅ `ForgetSubtree` 清捕获登记时未同步释放 OS 捕获 —— 摘树路径的 `::SetCapture` 泄漏（**原立案描述；2026-11-05 结案：归属反转 harness 侧，主诉撤单，见下方「复核订正 4」**）

> 立案：2026-11-05 14:2x，用户报告「鼠标偶尔变成左右拉伸样式，离开拉伸区后仍不恢复」。
> ★ 用户 14:30 裁定「你登记，然后我去 ECDI 修」⇒ **本仓只登记，不改 ECDI、不改本仓代码**。
>
> ★★ **2026-11-05 14:35 复核订正（原判不改写，增量记录）**：用户实测「卡住后点一下左键
> **不自愈**，会一直保持那个状态」。★ 该结果**不否证**下面的源码链，但把下方「点一下是否自愈」
> 当作「10 秒零代码验证」是**判据设计错误**——它是**单向判据**：`WM_SETCURSOR` 只在鼠标移动 /
> 窗口激活时发（ECDI `Win32PlatformWindow.cpp:1154-1174`），一次 Down+Up 虽会经
> `Application.cpp:455/508` 跨边界走到 `SetMouseCapture(false) → ::ReleaseCapture()`，
> 却**不会重设 `::SetCursor` 的结果** ⇒ 「恢复」可证实捕获泄漏，「不恢复」**不能证伪**。
> ⇒ **「OS 捕获泄漏 ⇒ 光标卡住」这一因果链仍属待验证假说**，源码链只是确定了「摘树时框架登记
> 被清、Up 的释放被幂等短路」这个**代码事实**。★★ **建议暂缓 ECDI 侧修复**：先补机读判据
> （本仓只读探针 `::GetCapture()` / `::GetCursor()`），或做「移出窗口后点击是否被吞」的行为对照；
> 在机制坐实前改 `ForgetSubtree`，有重蹈 `REQ-01/03/04/07` 归属误判的风险。
>
> ★★ **2026-11-05 14:38 复核订正 2**：用户补充「**在 UI 界面外点一下再回去就恢复了**」。
> ⇒ 关键自变量从「点没点」变成「**有没有移出窗口再回来**」，出现两个竞争解释：
> **H1（ECDI 侧 OS 捕获残留）**：窗口内点击仍在同一捕获窗口内，Down/Up 不足以打破；
> 窗口外点击被系统 / 别的窗口夺走捕获（`WM_CAPTURECHANGED` → `OnCaptureLost`）⇒ 恢复。
> **H2（纯 harness 侧，无需 ECDI 缺陷）**：`WM_SETCURSOR` 只在鼠标移动 / 窗口激活 / 重新进入
> 时发；移出窗口触发 `WM_MOUSELEAVE`（`Win32PlatformWindow.cpp:1143-1150`），移回时第一次
> `WM_SETCURSOR` 把光标设回 Arrow，而新位置上 `SplitterBar` 不再收到 `OnMouseMove` ⇒ 恢复；
> 窗口内点击若不伴随鼠标移动，则不产生重查 ⇒ 不恢复。
> ★ **H2 能同时解释两条观测**（窗口内点不恢复 / 移出再回来恢复），且只需 harness 侧
> 「一次性 `SetCursor` 无恢复」这一个**已知**缺陷 ⇒ 按奥卡姆剃刀**本轮倾向 H2**，
> 即 **REQ-18 很可能是第 5 次归属误判**（前四次 = `REQ-01/03/04/07`）。源码链仍是客观代码
> 事实，但它**是否导致本现象**未坐实。
> ★★ **判别实验 X（零代码、30 秒，请用户做）**：卡住后**全程不点击**，只把鼠标移出窗口到
> 桌面、停一下、再移回窗口内。恢复 ⇒ H2 成立、ECDI 无缺陷、本条改判 harness 侧（或撤单）；
> 不恢复且必须窗口外点一下 ⇒ H1 成立、维持 ECDI 侧。辅助 **Y**：窗口外那一下，目标窗口 /
> 桌面的**首次点击是否被吞**（被吞 = OS 捕获实锤）。
> ⇒ **状态改 ⏸️ 待裁决；明确暂缓 ECDI 侧修复**。原判（v5.8 / v5.9）逐条保留，不改写。
>
> ★★ **2026-11-05 14:41 复核订正 3（实验 X 结果）**：用户回报「**不点击、只移出窗口再回来，
> 依旧是左右拉的样子**」⇒ **X = 不恢复**，方向指向 H1（重新进入窗口并没有触发光标重查）。
> ★ 但**仍不判 ECDI**：① 混淆变量未排除（移回路径是否又扫过分割条、窗口外那一下首次点击
> 是否被吞）；② 按 `Application.cpp:455/508`，窗口内一次完整 Down+Up 本应跨边界走到
> `ReleaseCapture()`，与「窗口内点一下不自愈」有一处不吻合。⇒ **行为判据已用尽**，
> 再靠肉眼二分就是第三次过度推断。
> ★★ **下一步改为受控对照（只改一个自变量，且不碰 ECDI）**：先修本仓 `SplitterBar` 这个
> **两条假说都承认存在**的缺陷（`OnMouseLeave` 恢复 Arrow / `SetCursor` 按状态决定），
> 再按原路径复现一次：症状消失 ⇒ 归属 harness 侧、REQ-18 降级或撤单、ECDI 不用动；
> 仍卡 ⇒ H1 实锤，再去 ECDI 改 `ForgetSubtree`（或先加只读探针 `::GetCapture()` 取读数）。
> 状态**保持 ⏸️ 待裁决**，不来回翻转。
>
> ★★★ **2026-11-05 15:05 结案（复核订正 4）：归属反转 → harness 侧，REQ-18 撤单，ECDI 不用动**
> 用户按 v5.15 判据复验，回报「**两者都好了**」：a 离开分割条后光标恢复箭头；
> b 拖动跟手、分割线不再常亮蓝色。原稳定复现路径（设置 → 点服务商行（不改配置） →
> 关闭 → 扫分割条 → 拖分割条）**全绿**。⇒ 状态 ⏸️ 待裁决 → **✅ 闭合（归属反转）**。
>
> **两阶段受控对照（每步只加一个自变量，ECDI 全程零改动）**：
>
> | 阶段 | 唯一的自变量 | 观测 |
> |---|---|---|
> | S0 基线 | — | 光标恒 `SIZEWE`；拖拽仅在该操作序列后退化（蓝线常亮、必须极慢才跟手） |
> | S1 | 修 `SplitterBar.cpp`：光标按几何位置决定 + `OnMouseLeave` 对称恢复（v5.12/v5.13） | 光标症状**消失**；拖拽症状**仍在** |
> | S2 | 再把 `AppSettings.cpp` 的列表自我重建挪到下一 tick（`App.h`/`AppStream.cpp`，v5.15） | **两症状全消** |
>
> ⇒ 仅改 harness 侧即完整消除全部症状，**无需 ECDI 侧假说**。按 §1.1「先证伪不需要改对方的
> 解释」与「只改一个自变量」纪律：v5.8 判「ECDI 侧 OS 捕获泄漏」**不成立为已确认缺陷**，
> 这是继 `REQ-01/03/04/07` 之后的**第 5 次归属误判**（v5.10 的预判应验）。
>
> **诚实的边界（跳过必须说出来）**：
> ① **H1/H2 机制二分仍未完成**：实验 X（移出再回来不恢复）指向 H1，但未排除「移回路径
>    又扫过分割条」「窗口外首次点击是否被吞」两个混淆变量；本结案**不宣称 H2 成立**。
>    能确定的是「去除 harness 侧触发点后症状全消」，不是「ECDI 内部到底发生了什么」。
> ② `::GetCapture()` 的**物理读数自始至终没测**；ECDI 三处源码（`Window.cpp:721-725` /
>    `Application.cpp:508` / `Window.cpp:617-626`）仍是**客观代码事实**，但只能记为
>    **潜在隐患观察项**，不叫已确认缺陷、不占新 REQ 号、不跟进（按 11-04「评估降权、
>    能投入使用优先」）。若 ECDI 侧日后想自查，建议挂 `MouseCaptureTests.cpp` 单测断言
>    `::GetCapture() == nullptr`（修复前须红）。
> ③ **S1 / S2 两个改动各自是否必要，未做反向对照**（没试「只做 S2、回退 S1」）。按同一条
>    定调，不为凑读数去回退已工作的代码；两处**都保留**。
> ④ 本轮**未加机读判据**（`--cursor-probe` 未授权、未做），验收凭据是用户在
>    `gui/cmake-build-release-visual-studio/harness_gui.exe` 上的**一次人工复验**。
>
> ★ **对本仓的处置**：`REQ-18` 主诉撤单；下表所有原判断言（归属 / 合计结论 / 建议修法 /
> 验收判据 / 待办）均为 **v5.8 立案时的历史记录**，按本仓「历史不改写」惯例**逐条保留、
> 整体由本结案段取代**，不再执行。★ **用户不必去 ECDI 改 `ForgetSubtree`**，亦不必换库重链。
> 附带观察 `CursorShape` 缺 `SizeWE` 仍只是能力缺口、有绕行，依旧不单独立 REQ。

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-05 用户反馈（光标卡在 `IDC_SIZEWE`）；14:27 补充精确复现路径 |
| 状态 | ✅ **闭合（2026-11-05 15:05，归属反转为 harness 侧）**——两阶段受控对照（S1 修 `SplitterBar` / S2 延迟重建）后两症状全消，**ECDI 零改动**；主诉撤单，见上方「复核订正 4」。原 ⏸️ 待裁决轨迹见 §6 v5.10–v5.15 |
| 归属 | **ECDI 侧**（`Window::ForgetSubtree` 与 `Window::SetCaptureWidget` 幂等契约冲突 ⇒ **OS 级鼠标捕获泄漏**）。已按 §1.1 纪律查 `Widget.h` 基类公共头 **与 ECDI 源码仓**（`D:\Project\ECDI`，取证时点 HEAD `daed47c`），并核对其自带 `src/Tests/MouseCaptureTests.cpp`（审计 #60 / C3 同族）。★★ **2026-11-05 15:05 结案改判：归属 harness 侧**（本行仅保留 v5.8 立案时原判，不再执行；见上方「复核订正 4」） |
| 现象 | 鼠标扫过左右分割条后变成 `IDC_SIZEWE`，移开后**不恢复**；稳定复现路径 = 点「设置」→ 在服务商列表**点任意一行**（云知声 / DeepSeek 均可，**不改任何配置**）→ 直接「关闭」→ 扫过分割条 |
| ★ 受控对照（只改一个自变量） | ① 设置 → **直接关闭** → 扫分割条 ⇒ **正常恢复**；② 同上但中间**点一下服务商行** ⇒ **稳定卡住**。自变量只有「点击回调里是否发生列表自我重建」，其余（浮层显隐、鼠标路径、主题、服务商是谁、有无配置改动）逐位相同。用户 14:27 明确「什么配置都不改就会稳定触发」 |
| ★ 已核实①（本仓触发点） | `gui/src/AppSettings.cpp:1102-1149`：服务商行 `Button::SetOnClick` 回调**无条件**执行 `:1147` `RebuildSettingsProviderList()` + `:1148` `RebuildSettingsModelList()`；而 `:1073-1075` 的重建首步就是 `while (GetChildCount()>0) RemoveChild(GetChildAt(0))` ⇒ **把当前正被点击的 Button 自己摘树销毁**（发生在 Down 与 Up 之间） |
| ★ 已核实②（ECDI 摘树路径） | `ECDI/src/Window/Window.cpp:721-725`：`ForgetSubtree` 在捕获目标落在被摘子树内时只做 `m_captureWidget = nullptr`——**只清框架登记，不调 `m_platformWindow->SetMouseCapture(false)`**；OS 侧 Down 时建立的 `::SetCapture(m_hwnd)`（`Win32PlatformWindow.cpp:1415`）仍挂在窗口上 |
| ★ 已核实③（释放被幂等短路） | `ECDI/src/Application/Application.cpp:508` Up 末尾调 `window.SetCaptureWidget(nullptr)`，注释还写着「摘树路径已由 `ForgetSubtree` 清空（幂等）」；但 `Window.cpp:617-626` 的幂等判据是 `wasCaptured = (m_captureWidget != nullptr)`——此时已被 ② 清成 `nullptr` ⇒ `willCapture == wasCaptured == false` ⇒ **提前 return，`SetMouseCapture(false)` 永不调用**，`::ReleaseCapture()` 不发生 |
| ⇒ 合计结论 | 「摘树清登记」与「Up 靠登记跨边界释放」是**同一个状态位的两半**：前者把它提前清零，后者就再也跨不过边界 ⇒ **只要控件在 Down/Up 之间摘树，OS 捕获必然残留**。ECDI `Win32PlatformWindow.cpp:1114` 自述「唯一自愈出口 = 用户下一次任意点击」与本现象一致 |
| ★ 证据强度（诚实标注） | 上述**源码链条是确定的**（三处源码 + 两组受控对照）；但「`::GetCapture()` 在关闭后仍等于本窗口 HWND」这一**物理读数尚未实测** ⇒ 本条判为「源码级闭环的已确认缺陷」，**不冒充已有机读证据**。待补：ECDI 侧单测断言 `::GetCapture()`，或本仓 `--cursor-probe`；另请用户试「卡住后在空白处点一下左键是否自愈」（若是，即 `Application.cpp:1114` 预言的自愈出口） |
| 影响面 | 不只是光标：OS 捕获残留会让本窗口吞掉客户区外的鼠标消息、hover/命中路径异常；本仓之所以**先**看见光标，是因为 `SplitterBar` 用了「一次性 `SetCursor`、无恢复」的裸 Win32 调用绕开了 ECDI 的 `WM_SETCURSOR` 逐次重查契约（`Win32PlatformWindow.cpp:1154-1174` / `Widget.h:315-319`），它是**显影剂**，不是根因 |
| 建议修法（**不替 ECDI 裁决**） | **案 A（本仓倾向）**：`Window::ForgetSubtree` ② 清捕获登记时同步 `m_platformWindow->SetMouseCapture(false)`。平台层已具备安全性：`Win32PlatformWindow.cpp:1420-1437` 的 `SetMouseCapture(false)` 自带 `GetCapture() == m_hwnd` 守卫与 `ReleasingGuard`（抑制 `::ReleaseCapture` 同步投递 `WM_CAPTURECHANGED` 造成的伪失捕上报），不会误伤别人的捕获。**案 B**：让 `SetCaptureWidget` 的幂等判据独立于可被摘树清空的 `m_captureWidget`（另记 OS 捕获状态位），使 Up 的 `nullptr` 调用仍能跨边界释放。两案都须守住 `OnCaptureLost`（`Window.cpp:642-651`）「只清登记、不回调平台」与 #59 契约 C「脱树不补发 Up / Leave」；另 `ForgetSubtree` 是 `noexcept`，新调用的异常规格由 ECDI 侧评估 |
| 本仓侧放大器（**不属本条**） | `gui/src/SplitterBar.cpp:48/107` 裸 `::SetCursor(IDC_SIZEWE)`、`:111-116` `OnMouseLeave` 不恢复 ⇒ 本仓自行收敛（补对称恢复 / 收敛到契约），**不在 ECDI 侧修**。附带观察：`CursorShape`（`ECDI/include/ECDI/Core/CursorShape.h:13-16`）只有 `Arrow`/`IBeam`，今天没有合规通路自述「左右拉伸」——是能力缺口而非缺陷，本轮**不单独立 REQ**，留一行指针。★ **2026-11-05 15:1x 增量订正**：用户定调「没有现成链路，让使用者直接操作 Win32 了」⇒ 已立 **REQ-19**（本行「不单独立 REQ」按历史不改写保留，但**结论已被取代**） |
| 验收判据 | ① **ECDI 侧单测**（建议挂 `MouseCaptureTests.cpp`）：Down 建立捕获 → 在 Up 之前 `RemoveChild` 摘掉该控件 → 派发 Up → 断言 `::GetCapture() == nullptr`；修复前必须**红**。② 本仓换 `ECDI.lib` 重链后，按上述 C 路径人工复现：设置 → 点服务商行 → 关闭 → 扫分割条 ⇒ 光标必须恢复。③ 负对照仍在：不点服务商行的分支不得回归。④ 若本仓加 `--cursor-probe`，其负对照（点行→关闭）修复前须读到 `GetCapture()==m_hwnd`，否则判据空转 |
| 待办 / 开放问题 | ① 用户去 ECDI 修，修完回传库与 commit；② 本仓是否同批做 `--cursor-probe` / `SplitterBar` 收敛（**当前未授权，未做**）；③ 其它在事件回调内自我重建的调用点（`AppSettings.cpp:360/1336-1337`、`AppUiMain.cpp:894-895`、`AppStream.cpp:209`）是否同族，待本仓自查 |

---

### REQ-19 🔄 应用侧「无合规通路」两例 —— 光标形态值域（**19-A**）/ shell 打开文件（**19-B**），**记作能力缺口、不是缺陷**

> 立案：2026-11-05 15:1x，用户观察「harness 那自己修好了，不过这也看出一个问题，**我们没有现成链路，
> 让使用者直接操作 Win32 了**」。
> ⇒ 本条把 REQ-18 里那句「`CursorShape` 缺 `SizeWE` 是能力缺口、有绕行、不单独立 REQ」**升级为独立条目**：
> 缺口本身不新鲜，新鲜的是**代价被量化了**——为绕开它，本仓写了 44 行辅助代码 + 5 个调用点，
> 并且**亲手造出了 REQ-18 那条「光标卡住」**（该条最终判定 harness 侧、ECDI 零改动，根因就是这个绕行）。
>
> ★ **2026-11-05 15:18 用户裁定**：① 光标缺口取 **案 A**（`CursorShape` 只追加值域，**不**补通用逃生舱）；
> ② `AppPreview` 的 `ShellExecuteW` **并入本条**（19-B），记作**能力缺口**，**不另立 REQ-20**。

#### 19-A 光标形态值域（原 REQ-18「附带观察」升级）

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-05 用户观察（REQ-18 收口时暴露）；原为 REQ-18 条目内的一行「附带观察」，本轮升级为独立 REQ |
| 状态 | ✅ **19-A 已闭合（2026-11-05 16:1x）· 19-B 仍开放**。★ **2026-11-05 15:5x 增量**：**19-A 的 ECDI 侧已落地**——ECDI `#87` / commit `1142738`（`CursorShape` 追加 7 值 + `CursorHandleFor` 补 `IDC_*` + `Platform87.CursorShapeMapping`），**ClangCL 588/588 全绿**且**判别力已证**（回退一条映射 ⇒ 恰该用例红，587 passed / 1 failed）。★★ **2026-11-05 16:1x 闭合（19-A）**：新 `ECDI.lib` 到位（11975450 B · `c5208346…`，旧库备份 `.scratch/ECDI.lib.prev-20261105`）+ 已装头同步 `CursorShape.h`（唯一漂移，另加安装期 `Core/version.h`）；本仓收敛 = `SplitterBar` **删 66 行绕行**（几何容差判据 / 句柄缓存 / `::GetCursor()` 值比较 / 三处出口恢复 / `<Windows.h>`），只留一行 `GetCursor()` override；重链后 `tools/regress.py` **RESULT: PASS**（11m54s，全部探针 rc=0）。**19-B 仍开放**（`AppPreview.cpp` 的 `ShellExecuteW` 未动，等 ECDI 侧裁决） |
| 归属 | **ECDI 侧**（`CursorShape` **值域** + `CursorHandleFor` **映射**两处）。已按 §1.1 查 `Widget.h` 基类公共头、`CursorShape.h`、`Win32PlatformWindow.cpp`、`TextBox.h` 先例，以及 ECDI 自带 `src/Tests/PlatformQuickfixTests.cpp`（取证时点 HEAD `daed47c`） |
| 缺口（一句话） | **自述通路是完整的，表达力是残缺的**：`Widget::GetCursor()` → `Window::OnQueryCursor`（`Window.cpp:657` 取命中控件形态）→ `WM_SETCURSOR`（`Win32PlatformWindow.cpp:1153-1174`）→ `::SetCursor`，四段全在；但枚举只有两个值（`CursorShape.h:13-16`），`CursorHandleFor`（`:44-60`）除 `IBeam` 外一律回落 `IDC_ARROW` ⇒ **「左右拉伸」这个词在框架里不存在** |
| ★ 代价（本条存在的理由） | 本仓 `gui/src/SplitterBar.cpp` 为绕行付出：**匿名命名空间 44 行**（`:19-62`——句柄缓存 ×2、几何判据 `PointerOverBar` 含 16px 容差、`RestoreArrowIfSizeWe`）+ **5 处调用点**（`:100/104/160-161/169/179`）+ `#include <Windows.h>`。★★ 其中最贵的是 `if (::GetCursor() == SizeWeCursor())` 这一行：应用必须**逆推「当前光标是不是我设的」**才能不覆盖 `TextBox` 的 `IBeam`——而「谁拥有光标」这份知识框架本来就有（它每次 `WM_SETCURSOR` 都重查一遍） |
| ★★ 结构性后果 | 裸 `::SetCursor` 是**一次性写、无 owner、无恢复** ⇒ 应用被迫**手写光标状态机**（进入设 / 离开还 / 松手还 / 几何兜底），框架 `WM_SETCURSOR` 的**逐次重查**（`Widget.h:315-319` 明写「每次重查」）因此被完全弃用 ⇒ 从「框架每帧自愈」退化成「谁最后写谁赢」。**REQ-18 的光标卡死就是这条绕行的直接产物**（绕行自伤，不是框架缺陷） |
| ★ 补齐后本仓能删什么 | `SplitterBar` 只需一行 override：`CursorShape GetCursor() const noexcept override { return CursorShape::SizeWE; }`（与 `TextBox.h:38` 的 `IBeam` 完全同款）。`PointerOverBar` / `RestoreArrowIfSizeWe` / 三处出口恢复 / `#include <Windows.h>` **全部作废**；16px 容差那套 hack 也不需要——命中由 `HitTestWindow` 决定，3px 条宽不是应用该操心的事 |
| 建议修法（**案 A 已于 15:18 由用户裁定采纳**） | **案 A（采纳）**：`CursorShape` **只追加末尾、不重排**（头文件 `:10` 自己写了这条约定）——取值集合见下一行；`CursorHandleFor` 补 `IDC_*` 映射（未知值仍回落 `IDC_ARROW`，保持今天的安全缺省）。**案 B（已否决，记录理由）**：通用逃生舱 `CursorShape::PlatformDefined` + host 钩子 ⇒ 会把平台头重新泄回应用、破坏跨平台抽象；光标本就是 OS 概念，枚举补全已够覆盖，不必为单个消费者开洞 |
| ★ 取值边界（2026-11-05 15:21 用户补：「应该同时也缺了上下箭头」） | **主张**：`SizeWE`（今天就有消费者）+ `SizeNS`（水平分割条；本仓今天没有，但同一个 `SplitterBar` 加 `Side::Top/Bottom` 立刻需要）——两者是**同一族、同一次补齐**，不要只补一半。**同族可选**：`SizeNWSE`/`SizeNESW`（角部）、`SizeAll`（可拖面板）、`Hand`（可点链接/路径）、`Cross`（精确取点）——本仓**当前无消费者，不主张**，纳不纳入由 ECDI 裁。**明确不收**：`Wait` / `AppStarting`——那是**模态忙闲态**，不是「悬停在这个控件上」的自述，应由应用级 API 承担，混进本枚举会把「逐次重查」的语义搞坏 |
| ★ 建议补丁（ECDI 侧，可直接落地） | **①** `include/ECDI/Core/CursorShape.h` 只追加末尾（`:10` 的约定）：`SizeWE=2` / `SizeNS=3` / `SizeNWSE=4` / `SizeNESW=5` / `SizeAll=6` / `Hand=7` / `Cross=8`。**②** `Win32PlatformWindow.cpp` 的 `CursorHandleFor` 每个新值一行 `return ::LoadCursorW(nullptr, IDC_SIZEWE);`（`IDC_SIZENS` / `IDC_SIZENWSE` / `IDC_SIZENESW` / `IDC_SIZEALL` / `IDC_HAND` / `IDC_CROSS`），缺省仍回落 `IDC_ARROW`。**③** 测试：见下方验收判据 ①（★ **不能直测 `CursorHandleFor`**——它在 `Win32PlatformWindow.cpp` 的**匿名 namespace** 里，这正是本条原判据要改的原因） |
| ★ 边界（不夸大） | 这是**表达力缺口**，不是错误行为——今天所有路径自洽（未知形态一律 Arrow），且有绕行。按 2026-11-04「不为采数据挡功能」定调，**不排期**、不阻断任何功能 |
| ★ 纪律产出（本条真正的教训，两例共用） | 「应用里出现 `#include <Windows.h>` + 裸 Win32 调用」= **一条潜在 REQ 的信号**，不是实现细节。本仓全量盘点后只剩 2 处：① **19-A**（`SplitterBar.cpp` 光标）；② **19-B**（`AppPreview.cpp:39-40` `ShellExecuteW`，框架无「用系统默认程序打开 / 定位文件」能力）——15:18 用户裁定**并入本条**，不另立 REQ-20 |
| 验收判据 | ① ECDI 侧单测（挂 `PlatformQuickfixTests.cpp`，**复用 ②-b 装置**）：Host 的 `OnQueryCursor` 返回 `SizeWE` ⇒ 真窗口发 `WM_SETCURSOR(HTCLIENT)` ⇒ 断言 `::GetCursor() == ::LoadCursorW(nullptr, IDC_SIZEWE)`，用例尾恢复箭头（②-b 同款卫生）。★ **原判据（直测 `CursorHandleFor`）不可行，已订正**——它是 `Win32PlatformWindow.cpp` 匿名 namespace 内的静态函数，测试拿不到；★ **判别力**：修复前该用例必红（`::GetCursor()` 落在 `IDC_ARROW`），这正是它该有的样子。② 本仓收敛后 `SplitterBar.cpp` **不再出现 `#include <Windows.h>` / `::SetCursor` / `::GetCursor`**（可 `grep` 判定）；③ 负对照：`TextBox` 仍 `IBeam`、普通控件仍 `Arrow`（`PlatformQuickfixTests.cpp:198-202` 既有两条不得回归）；④ 人工：扫过分割条出现左右箭头、离开恢复、拖拽跟手（REQ-18 结案时那组判据原样复用） |
| 待办 / 开放问题（19-A） | ① ECDI 补值后本仓才做收敛（**当前不预做**——REQ-18 已证明「先改本仓再等框架」会多走一轮）；② 取值集合按上方「取值边界」行：**主张 `SizeWE` + `SizeNS`**，其余同族可选、`Wait`/`AppStarting` 明确不收——最终由 ECDI 裁，本仓不预设；③ 案 B 已否决 ⇒ 验收判据 ②（`SplitterBar.cpp` 无 Win32）恒成立，不需重写 |

#### 19-B shell「在文件管理器中定位 / 打开」无能力面 —— 应用只能裸 `ShellExecuteW`

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-05 15:18 用户裁定「并入 REQ-19，记作能力缺口」（**不另立 REQ-20**） |
| 状态 | 🔄 **开放**（能力缺口；本仓有绕行、**不阻断使用**） |
| 归属 | **ECDI 侧（能力面缺失，非错误行为）**。已按 §1.1 核实：ECDI 全库（`include/` + `src/`）**零** `ShellExecute` 引用（只用到 `shellapi.h` 的 `Shell_NotifyIconW` / `SHGetFileInfoW` / 拖放族）；而平台层「把系统能力包成方法」**已是既有做法**——`PlatformWindow.h:72/76` 的 `GetClipboardText` / `SetClipboardText` ⇒ 本条要的是**沿用同一范式再加一个方法**，不是新范式 |
| ★ 最干净的对照（同一文件、20 行之内） | `gui/src/AppPreview.cpp` 预览面板 foot 栏两个**相邻**按钮：**「复制路径」**（`:263-265`）走 `m_window->GetPlatformWindow().SetClipboardText(...)` —— **零 Win32**；**「在资源管理器中显示」**（`:241-250`）只能 `ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/select,\"…\"", nullptr, SW_SHOWNORMAL)`，并为它付 `#include <windows.h>` + `<shellapi.h>`（`:39-40`）。★ 同一个栏、同一个文件、同一个用途族，一个走框架一个走 OS ⇒ 归属无争议 |
| 代价 | ① 应用要懂 **`ShellExecuteW` 的返回值域**（`<=32` 判失败、`SE_ERR_*` 语义）才能写错误分支；② `explorer.exe /select` 是 Windows 专有 ⇒ 换平台即失效，而框架其它地方都做了跨平台收敛；③ 探针只能断言路径/参数构造、**不能真开 shell**（有副作用）⇒ 该路径天然缺机读覆盖（`AppPreview.cpp:9-10` 已自述） |
| 建议修法（**案 A 风格，最小**） | `PlatformWindow` 增一个**窄**方法，例如 `bool RevealInFileManager(const std::string& path)`（或更通用的 `bool OpenExternal(const std::string& target)`），失败返回 false，**错误语义留给调用方**决定是否提示；Win32 实现内部 `ShellExecuteW` + `<=32` 判定，其它平台各自实现（`explorer` / `xdg-open` / `open`）。★ **不要求**做通用 shell 抽象层（YAGNI，与 19-A 否决案 B 同一个理由） |
| 边界（不夸大） | 能力缺口、非缺陷：今天行为正确（能定位文件、失败有 notice），只是实现越过了框架。按 11-04「不为采数据挡功能」定调**不排期** |
| 验收判据 | ① ECDI 侧单测：对 `RevealInFileManager` 做**参数构造**断言或注入 stub（**不真开 shell**）；② 本仓收敛后 `AppPreview.cpp` **不再出现 `#include <windows.h>` / `<shellapi.h>` / `ShellExecuteW`**（可 `grep` 判定）；③ 负对照：相邻「复制路径」路径不回归；④ 人工：点按钮真能定位到文件，失败分支 notice 仍显示 |
| 待办 / 开放问题（19-B） | ① 命名与粒度（`RevealInFileManager` 专名 vs `OpenExternal` 通用）留给 ECDI 裁决；② 本仓收敛在 ECDI 落地后做，**当前不预做**；③ ★ 若 ECDI 判定「拉起外部程序」超出框架边界（安全考量：应用不该能拉起任意 exe），则本条**转为不登记**、本仓保留绕行——这一分歧**由对方给结论，本仓不预设** |

---

### REQ-20 🔄 段内内联样式（styled spans：粗体 / 斜体 / 行内 code）—— 转录富渲染碰到「一控件一套字体」边界

> 立案：2026-11-07，用户裁定「富内容应该把转录全做好，目前只做了表格」。范围拆两层：
> **块级全套**（标题 / 围栏代码块 / 列表 / 引用 / 分隔线 / 链接）应用层做得了、先行（`rich-content-plan.md` §G）；
> **段内内联样式**是框架能力边界，按铁律登记交 ECDI，不在应用层拆控件绕行。

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-07 富内容二期扩围裁决（`docs/rich-content-plan.md` §G） |
| 状态 | ✅ **已闭合（2026-11-09，本仓已消费）**（能力缺口，ECDI 侧已交付：`#88` / Phase 44 落地 `TextWidget::SetStyledText` + `SpanStyle`，Phase 45 追加 `italic` / `size`。★ **2026-11-07 ECDI 裁决**：重启条件成立、富文本另立项 = ECDI `docs/dev/roadmap-deferred.md` **#88**（v2.81）。★★ 本仓消费口径（用户 2026-11-09 裁定）：**粗体 + 行内 code 上线，斜体暂不做、`size` 留位**；残留维度缺口另立 **REQ-21**） |
| 归属 | **ECDI 侧（能力缺口，非缺陷）**。已按 §1.1 核实到源码仓：安装头 + ECDI 源仓（`D:\Project\ECDI`）全库 grep `RichText/TextRun/InlineStyle/TextSpan/StyleRun/SetRangeFont` **零命中**；`TextWidget.h:23`「Phase 9：持有 TextStyle（m_style——文字视觉唯一来源）」⇒ 一控件一套样式；`Font.h:16-20` 已有 weight（REQ-10 已消费）但粒度 = 整控件 |
| 缺口（一句话） | markdown 段内粗体 / 斜体 / 行内 code 需要「同一文本流内混排样式」，而框架文字视觉按控件单值（TextStyle/Font）——「这半句加粗」在框架里不存在 |
| ★ ECDI 侧既有裁决（勿重开，先对账） | `phase28-long-text-cost-requirements.md:39/72`：富文本（styled spans）= 非目标、另立项；`phase36-font-expressiveness-requirements.md:64`：富文本 → **#53 渲染表现力群**、单消费者不立项；`roadmap-deferred.md:196`（#53）：重启条件 = 第二消费者出现。★ 本仓转录渲染已是真实消费者（表格渲染已闭合、块级全套启动）——**是否构成重启条件由 ECDI 裁，本仓不预设**。★★ **2026-11-07 裁决已回（ECDI 侧 v2.81 / #88）**：用户澄清口径本意——「等第二消费者」防的是**内部 demo 自嗨**（当时完全没有独立开库消费的项目）；本仓 = 真实外部消费工程 ⇒ **重启条件成立**；**富文本另立项、不并 #53 群**（体量不匹配，沿 phase28「另立项」口径），#53 群其余项维持不动。（★ 路径订正：三份文档实在 ECDI 仓 `docs/dev/` 下；`roadmap-deferred.md` #53 实在 `:196`，核验属实） |
| ★ 代价（为何不在应用层绕行） | 绕行 = 把内联样式拆成多个 Label 拼接 ⇒ ① 打破「一 block 一 TextBox」的选中/复制假设（`--select-probe` / `--selectable-rows` 2026-11-07 刚铺完 tool 体）；② 跨控件折行要自管（框架 wrap 按控件），CJK 混排是重灾区；③ rebuild/hit/answer/grow/cull/select 探针冲击面比表格大且属静默重基线。同 REQ-15 教训：不在应用层塞 `\n` 式绕行 |
| 建议修法 | `TextWidget`/`Label` 的 styled spans 能力（文本 + 逐段 Font/颜色覆盖，测量/折行/命中统一在框架内）；API 形状由 ECDI 裁，本仓不预设。`TextBox` 侧若开需兼顾选中/复制通路 |
| 边界（不夸大） | 能力缺口、非缺陷：今天行为自洽（内联标记按原文显示、内容不丢）。按 11-04「不为采数据挡功能」定调不挡块级功能；块级全套不受影响、先行。（★ 2026-11-07 裁决更新：原「不排期」→ **已另立项**，Phase 号与顺位待 ECDI 排，见状态行） |
| 验收判据 | ① ECDI 侧单测：同一 Label 内粗体/常规/mono 三段混排，测量与折行正确、逐段样式生效（GDI/GL 两链一致）；② 判别力：回退任一段样式 ⇒ 恰该用例红；③ 本仓消费后：转录段内粗体/斜体/行内 code 真渲染，选中/复制仍取块全文（口径消费时定）；④ 负对照：既有单字体路径（全部现行探针）不回归 |
| 待办 / 开放问题 | ① ✅ **已裁（2026-11-07）**：富文本**另立项**（ECDI deferred #88），不并 #53 群；② ✅ **本仓消费已落地（2026-11-09）**：粗体 + 行内 code 上线，斜体暂不做、`size` 留位，见 `rich-content-plan.md` §H；③ 块级全套独立推进，见 `rich-content-plan.md` §G |

---

### REQ-21 🔄 `SpanStyle` 缺 `background` —— 行内 code 画不出底色块

> 立案：2026-11-09（本仓消费 Phase 44/45 的 styled spans 时暴露）。同批用户裁定
> 「background 问题先登记」⇒ 行内 code **不等它**，以「等宽 + 前景色」先上线。

| 项 | 内容 |
|---|---|
| 原编号 | 2026-11-09 富内容 §H 消费时发现（`docs/rich-content-plan.md` §H） |
| 状态 | 🔄 **开放**（能力缺口；**不阻断**行内 code——本仓已用等宽 + 前景色兑现，底色属「观感维度」缺失） |
| 归属 | **ECDI 侧（能力缺口，非缺陷）**。已按 §1.1 核实到安装头：`third_party/ecdi/include/ECDI/Widget/TextSpan.h:15-23` 的 `SpanStyle` 只有 `family` / `weight` / `foreground` / `italic` / `size` **五个 optional，没有 background**（Phase 45 `eb2106a` 后逐字复核） |
| 缺口（一句话） | markdown 行内 code（`` `x` ``）的标准观感是「等宽 + 浅底色块」，而 span 只能覆盖字族/字重/斜体/字号/前景色——**底色块在框架里没有表达方式** |
| 消费者证据 | 本仓 2026-11-09 落地的 `ParseInlineRuns`（`gui/src/Transcript.cpp`）产出的 code span；渲染侧 `HarnessApp::ApplyInlineText`（`AppTranscript.cpp`）；判据 `--md-probe` 的 `inline-code` / `inline-mixed` / `inline-code-literal` 三相 |
| 建议修法 | `SpanStyle::background`（`std::optional<Color>`）；绘制侧在文字之前按 run 画底色矩形（`DrawRect` + `PushClip` 既有原语足够，**不需要新原语**）。API 形状由 ECDI 裁，本仓不预设 |
| 边界（不夸大） | 能力缺口、非缺陷：今天行为自洽（行内 code = 等宽 + 强调色，可读性上已能区分）。★ 无底色**不是错误渲染**，只是与本仓视觉稿有差距 ⇒ 按 11-04「不为采数据挡功能」不挡上线 |
| 验收判据 | ① ECDI 侧单测：同一 Label 内两种 background 的 run，底色矩形与文字 run 边界对齐（含软折行跨行、跨 DPI）；② 判别力：回退 background ⇒ 恰该用例红；③ 本仓消费后：行内 code 有底色且**选中高亮仍可读**（这是本条的真正难点——底色与选区高亮叠加）；④ 负对照：无 background 的 span 绘制逐字节不变（`a=1` 路径零变化，沿 #71 红线） |
| 待办 / 开放问题 | ① 是否与 REQ-20 合并为「styled spans 表现力」一条 —— **不合并**：REQ-20 已闭合（能力已落地），本条是**落地后暴露的维度缺口**，分开记才不会让 REQ-20 的闭合记录留下假完备；② 本仓是否等它再上线 —— **不等**（用户 2026-11-09 裁定「先登记」）；③ 若将来做，需先定「底色与选区高亮的叠加顺序」，否则两种实现都自洽但视觉不同 |

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
| ★ `REQ-11` | `Color.a` 在 GDI 链的 `DrawText` / `DrawFocusRect` 恒被丢弃（后端能力不对等） | ✅ **ECDI `#71` 实施（2026-10-25）**：GDI 链补 alpha 消费、与 GL 链对齐 ⇒ **两链全原语消费**；本条零消费者 ⇒ 无需本仓验收读数（原「推后」判据未触发即关闭，是 ECDI 侧主动修） |
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
| ★ `TODO-01` | **双主题令牌化**：`Theme.h` 从「编译期常量」改成「运行期可切换」，让 `harness-hifi-variant-c.html` 的浅色形态能在**真 GUI** 上验证，而不是只停在 HTML 原型 | ✅ **已落地**（2026-10-18）：`struct Theme` 53 色 + `sansFamily`，`MakeDark()`/`MakeLight()`，`SwitchTheme()` + **引用别名**（`inline const ECDI::Color& kX = g_theme.x`）⇒ 全仓 ~200 处按名引用**零改动**；`--theme dark\|light` CLI。★ 硬约束：只能在 `BuildUi()` 之前调（颜色值在 BuildUi 时被拷进各 `PanelStyle`）。回归双主题各 5 用例全 PASS。详见 `docs/variant-c-ecdi-mapping.md` §5 |
| ★ `TODO-04` | **自绘标题栏随主题切色**：系统 `CaptionBar` 的底色/标题/三钮颜色在 ECDI 里全部写死（`CaptionBar.cpp:31/37` · `CaptionButton.cpp:14-27`），框架无 caption 样式令牌 ⇒ 深色主题下外观正常，`--theme light` 下标题栏仍是深灰（未验证，本轮才发现） | ✅ **已落地**（2026-10-18）：应用侧派生 `gui/src/ThemedCaptionBar.h`（`ThemedCaptionBar : ECDI::CaptionBar` + `ThemedCaptionButton : ECDI::Widget`），`Theme.h` 增 8 个 caption 令牌（`captionBg/captionInk/captionGlyph/captionGlyphHot/captionHover/captionPressed/captionCloseHover/captionClosePressed`）+ 8 个 `kCaption*` 别名；`App.cpp:388` 构造点换成派生类。**ECDI 一行未改**（铁律）。深色令牌与原生逐值相同 ⇒ 回归基线不变。★ 关键约束：派生类把基类三个按钮 `RemoveChild` 释放后，**绝不能**再调 `CaptionBar::SetSize`（→`RelayoutChildren` 解引用）或 `CaptionBar::OnPaint`（→`m_maxButton->SetGlyph`）⇒ 两个 override 都限定到 `ECDI::Widget` / `ECDI::Panel` 基类实现 |
| ★ `TODO-05` | **`--theme light` 下未实际跑通全链验证**：本轮新增 caption 令牌后需按 §4 既有回归口径复跑，避免「只编译过、没实机看过」 | ✅ **已复跑并截图**（2026-10-18 晚）：10 组（2 主题 × demo/scale/realistic × gdi/gl）全部 `rc=0` 且 `seqGaps=0 / violations=0`（demo 8/19/43 · scale 2000/4590/8906 · realistic 220/717/1213，两主题逐项一致）⇒ 「换主题只改颜色令牌」这条结论对 caption 同样成立。截图 `.scratch/caption-theme/caption-*-top.png`：caption 底色浅 `#f8f9fb` / 深 `#26282d`，标题字 `#23262e` / `#ebecf0`，close glyph `#696f7b` / `#cfcfd5` ⇒ 三处颜色全部随主题切换。回归驱动 `.scratch/caption_theme_regress.py`、截图 `.scratch/caption_theme_shots.py`、读数 `.scratch/caption-theme-regress.log` / `caption-theme-shots.log` |
| ★ `TODO-02` | **字体工厂**：全仓 `ECDI::Font` 构造散落 18 处、`family` 只有 `Consolas` 一个值，中文正文全部落到 SimSun；且 `AddLine()` 只在传了字号时才 `SetFont` ⇒ 同一界面里两种中文字形混排 | ✅ **已落地**（2026-10-18）：`harness::SansFont(float)` / `MonoFont(float)` 单一入口；`AddLine()` 改为无条件 `SetFont`（默认 `14.0f` = `Font::size` 出厂值）。★ **2026-10-22 订正**：原写「深色刻意保留 `sansFamily = ""`（SimSun）作回归基线，浅色才用雅黑」——该决定已被**推翻并撤回**：两主题现在**都**用 `Microsoft YaHei`（`Theme.h` 的 `MakeDark()` 里 `t.sansFamily = "Microsoft YaHei"`）。撤回理由见下条 `FONT-UNIFIED` 与 §6 的 v3.0 |
| ★ `TODO-03` | **输入框焦点视觉**：ECDI 默认 `DrawFocusRect` 点线框，本项目输入框**启动即获焦**（验收 A2）⇒ 常态就是一圈蓝色虚线，与 variant-c 的实线描边形态冲突 | ✅ **已落地**（2026-10-18）：派生 `ComposerBox` 关掉点线框（`SetShowFocusRect(false)`），改让 `borderColor` 表达焦点（失焦 `kInputBorder` / 获焦 `kDarkAccent`）。★ **不是**直接关掉焦点框——那样焦点完全不可见，是拿可访问性换观感 |
| ★ `FONT-UNIFIED` | **两主题字族统一**（用户报「中间上面三个按键深浅主题样子不同」，收窄为「three dots 暗色每个 dot 的间隔比浅色大」）：`gui/src/Theme.h:184` 深色 `sansFamily = ""` ⇒ ECDI 默认 face = **SimSun**，`Theme.h:281` 浅色 `"Microsoft YaHei"`，而 `SansFont()` 读 `g_theme.sansFamily` ⇒ **同一串字符被两个 face 光栅化** | ✅ **已落地**（2026-10-22）：`MakeDark()` 里 `t.sansFamily = "Microsoft YaHei"`（改一行）。★ 实测比用户报告更严重：浅色把 `·`(U+00B7) 渲成**一条 9px 实心横杠**（三点粘连），深色是**三个独立 2px 点、中心距 12.0px**；浅色 `↗`/`≡` 另带 ClearType 彩边（15 种墨色 vs 3 种）⇒ 根因是**字族**不是间距。修后 `dump_dots3.py` 两主题 run 数/pitch/墨色数完全一致（`SAME=True`）。代价：深色 paints 变（demo 22→23 · realistic 17→19 · cot 21→22 · scale 21→23），**此前深色 paints/frames 不可直接对比**；六夹具 × 两主题的结构读数（blocks/deltas/messages）全不变。★ 这是本仓第一次有意改变深色字形基线，`TODO-02` 里「深色刻意保留 SimSun 作回归基线」一句已随之作废 |
| ★ `TODO-06` | **回归门 race 段错误（红门不可信项）**：`--workspace-probe`（活 core）确定复现段错误/零输出；`--theme-probe`、`--delprov-probe` 间歇段错误且**崩溃点逐轮漂移**（同轮 theme 红、换轮 delprov 红；加一条 fprintf 改时序即 6/6 全过 ⇒ 海森 bug） | 🔄 **立案 2026-10-31**：干净构建核实为 **HEAD 自带**（b0dd54e 上 theme 同样段错误、ws 同样零输出），与表格修复 f1cfa07 无关。疑与 overlay/定时器悬垂指针族同源（`RebuildUi` 开头 ⓪/⓪b 两条自保注释、审计整改批 A §11.2 未覆盖的 `exemptWidget` 第 5 例）。★ **纪律：本条未闭合前，回归红相先对照本条与 `.scratch/_regress_precommit.log` 族证据，再归因新改动**；★ **2026-11-01 实测订正（v4.6/v4.7，以此为准，取代本条此前所有补强与订正）**：rc 已实测——当前 HEAD 二进制（sha256 `cf4771dd`，01:12 构建）上 answer / theme / history / rebuild 四门 **外加 sidebar / table / hit 三门，全部 `rc=0xC0000005`**，而后三门的 json 已经落盘 ⇒ 「**绿门也在崩**」由推测变实测；连**不带任何探针的普通回放**同样 `rc=0xC0000005`（json 已落盘）⇒ 这不是「某几门红」，是**每次运行都在退出时崩**。崩点已反汇编定位（无 cdb，用 `.scratch/_dbg_lite.py` + `.scratch/_pe_sym.py`）：**F1** = `+0x2410` 处调 `VCRUNTIME140!memcpy` 读 NULL（std::string 扩容路径，`--answer-probe` **5/5 复现、0.22s**）；**F2** = `+0xA533` 的 `mov -0x8(%rax)`（std::string 析构/释放路径，size>0xF 而 buffer==0 = 对象已被复用或二次析构）。⇒ 症状收敛为 **std::string 悬垂 / 二次析构一族**，「堆布局敏感」假说不再需要。详见 §6 v4.6。★★ **v4.7 补**：A/B 证明该崩溃**只在部分构建上复现**（`/Zi`+`/DEBUG` 一加就消失、仅加 `/MAP` 也不崩）⇒「确定性」只对构建 A（sha `cf4771dd`）成立，本质仍是布局/时序敏感；结论一律要带二进制指纹。★ 以下 v4.5 段落保留为过程记录：★ 2026-11-01 订正（**v4.5，撤回 v4.4 ④ 的「堆布局敏感实锤」**）：padded-HEAD 补丁未留痕（`gui/src/App.h:448` `struct RowRec` 现无 pad/reserve 字段、`git status` 空、`git stash list` 空），「干净 HEAD 4/4 绿」与「padded-HEAD 8/8 复现」**均无对应日志**（v4.4 ④ 所引四份文件全为 10-31，且 `_regress_tablefix2.log` 的 theme 是 `3/7 UNEXPECTED-RED`、`_regress_precommit.log` 另有 `--delprov-probe` 红、`_wp2.err` 0 字节、`_tp.err` 是临时 `[tp]` 插桩轨迹）⇒ 该对照降为**假说**。改立两条可复核证据：① 11-01 01:17 轮（01:12 构建 = post-fix HEAD）五门空产物而其余 23 门 json 齐备 ⇒ 仍崩在落盘前（**非 f1cfa07 引入**）；② `_regress_precommit.log`（22:57 四门绿）与 `_regress_t1t2.log`（00:42 四门 + §2c `legacy` + ws 共 6 红）**之间零提交** ⇒ 同源红相漂移。★ 判据含义订正：`FAILED TO PRODUCE OUTPUT` = **崩在 json 落盘前**（`tools/regress.py:209`，`subprocess.run` 的 rc 未采集），**不是**「跑完再崩」。取证 `.scratch/_regress_precommit.log` / `_regress_t1t2.log` / `_reg--{answer,theme,history,rebuild,workspace}-probe.json`（五缺）/ `.scratch/crash.dmp`（有效 MDMP，待解） |
| ★ `TODO-07` | **`RowRec` 非拥有控件指针的失效规则（`TODO-06` 的排雷入口）**：`panel / label / headerBtn / body / allowBtn / denyBtn / conclusion` 只在「建出这一行的那一代控件树」里有效，`RebuildTranscriptView` 拆树即全失效；此前**没有任何强制约定**（`blockRows` 的裸指针靠「deque + 与 `rows.clear()` 成对清空」保命，注释只写在 `App.h:541`） | 🔄 **2026-11-01 立案 · 第一步已落地**：规则三条写进 `App.h` 的 `RowRec` 头注释（不跨重建持有 / 跨重建用 `timelineIdx·askId·blockId` / 快照点有绊线）；`SnapshotRows` 增加**活控件集校验**——拼行前把 `label`/`headerBtn` 与当刻从 `m_content` 递归收集的真实控件集比对，不命中就写 `<dead:字段名>` 并 `fprintf` 一行 `[snapshot] DEAD row ref`（**判据格式未动**）。★ 实测（`gui/build` 当前构建）：answer / theme / rebuild / history / sidebar 五门 **deadRefs=0**、读数与 10-31 基线逐项一致 ⇒ 本构建上「控件指针已死」未发生。★ **已知覆盖缺口**：绊线只查「指针不在活树里」，**查不到「控件活着但内部 std::string 已坏」**——而 `TODO-06` 现场（`_Ptr==0` 且 `_Mysize==15`）更像后者，故绊线阴性**不构成结论**。★★ **2026-11-01 第二步已落地**：`blockRows` 由 `std::map<std::string, RowRec*>` 改成 **`std::map<std::string, size_t>`（rows 下标）**，取行统一走新增的 `FindBlockRow()`（越界即 `nullptr`）；`AppStream.cpp` 两处消费点（`delta` / `block_end`）改成先判空再解引用，`row->second->x` 全部收敛为 `row->x`（15 处）；`AddRow` 改写 `rows.size()-1`。★ 依据：`rows` 全仓只有 `push_back` / `clear`（无 erase/pop）⇒ 下标在两次重建之间稳定，两张表仍成对清空。★ 实测（sha `9ce5a655`）：八门 `rc=0`、读数与基线逐项一致（answer 6/1 · theme 7/1 · rebuild 3/0 · history 6/0 · sidebar 23/1 · table 6/0 · grow 12/0 · hit 4/0）。★ 下一步：`TODO-06` 的现场（控件活着但内部 string 已坏）仍无绊线覆盖，等一个**留档的复现二进制** |

---

## §5 载体与维护

### 5.1 同一事实的其它载体（订正时须一并回扫）

| 载体 | 登记内容 |
|---|---|
| `docs/ecdi-capability-requests.md`（本文件） | **唯一账本**：能力需求与归属判定 |
| `.workbuddy/memory/MEMORY.md` | 项目长期记忆：定位、结构、现状、数据卫生 |
| `.workbuddy/memory/YYYY-MM-DD.md` | 当日日志：取证过程与原始读数 |
| `docs/P1-gui-implementation-spec.md` §9 / `docs/P1.5-child-process-spec.md` §7–§8 | 各阶段自己的「已知局限」与实施回写 |
| `docs/variant-c-ecdi-mapping.md` | 原型的**逐手法落地对照表**（`REQ-10` / `REQ-11` 的取证来源与实现建议） |
| ECDI 侧 `docs/phase31-window-geometry-and-line-height-detailed-design.md` **§13.6** | `REQ-06`/`REQ-09` 的**取证结论落点**（112 格读数 + 仪器校准） |
| ECDI 侧 `docs/roadmap-deferred.md` **#69** · `docs/phase26-freetype-gl-detailed-design.md` **§9 L1** | `REQ-09` 的 ECDI 侧登记处（同一事实的对方载体）；★ 该条**只覆盖族名反查**，**不含**字重 / face index / 复合族名第二项；★ **2026-10-24 起 `REQ-10` 已有登记处 = ECDI `docs/roadmap-deferred.md` #70**（对码核验 + ⓒ 耦合订正的对方载体），`REQ-11` = 同表 **#71** |
| `evals/*.json` | 机器可读读数（评估证据） |

★ **纪律**（条 117）：「某需求已闭合」这类事实会被**多份文档各自登记** ⇒ 订正时先列载体清单，再按关键词全库回扫，不只在「本次改过的文件」里搜。

### 5.2 维护规则

1. **新条目**：取 `max(现有编号) + 1`，并在「原编号」列留空或填来源（条 61⑥：新增编号前先枚举全集）。
2. **闭合条目**：**不删除**，在 §3 表里给闭合依据（哪个 ECDI Phase / 哪行代码）。
3. **取证要求**：凡判「已确认」，必须附**可复现**的证据（探针夹具路径 / 读数文件 / 源码行号）——禁用「看起来」「应该是」。
4. **归属必须有判据**：判 ECDI 侧 vs harness 侧，要写清**为什么**（本次 REQ-01 的判据链 = 解析层正常 + ECDI 契约已含空行 + wrap 正常）。

---

## §6 修订记录（新在前）
- **v5.24（2026-11-09）★ REQ-20 本仓消费落地 + 登记 REQ-21（`SpanStyle` 缺 background）**
  ① **REQ-20 状态 → 已闭合（本仓消费）**：ECDI `#88` / Phase 44 交付 `TextWidget::SetStyledText`，
     Phase 45（`eb2106a`）追加 `italic` / `size` 两个维度 ⇒ 11-08 记下的「斜体永远不生效、字号不可变」
     **口径已过期**，本次逐字复核后订正（凡带「Phase 44 时点」的结论都必须重读一遍头文件，见下③）。
  ② 本仓消费（用户 2026-11-09 拍板）：**粗体 + 行内 code 上线**；**斜体暂不做**（`*x*` 按原文保留，
     不解析也不去标记）；**`size` 留位不用**（保版心行高稳定）。实现 = `ParseInlineRuns`（纯函数、
     三遍式：扫描 → 配对 → 输出）+ `ApplyInlineText`（三路正文入口）+ `--md-probe` 11 个 inline 相
     （含 `inline-cp-cjk` 码点索引专项与两相渲染侧判据）。落点见 `rich-content-plan.md` §H。
  ③ **★ 新登记 REQ-21**：`SpanStyle` 只有 family/weight/foreground/italic/size，**无 background**
     ⇒ 行内 code 画不出底色块。用户裁定「先登记」，行内 code 不等它。与 REQ-20 **不合并**
     （REQ-20 是能力落地、REQ-21 是落地后暴露的维度缺口；合记会让闭合记录假完备）。
  ④ 头部 v5.23 → **v5.24**；§2 头部摘要同步（REQ-20 摘要行末补 11-09 消费事实）。
- **v5.23（2026-11-07）★ REQ-20 裁决已回：重启条件成立、富文本另立项（ECDI deferred #88）**
  ① ECDI 侧用户澄清「第二消费者」口径本意：当时**完全没有独立开库消费的项目**、需求全来自内部测试 demo，
     闸门防的是内部自嗨；本仓 = 真实外部消费工程 ⇒ 其转录富渲染需求本身就是闸门等待的信号，重启条件成立。
  ② 用户拍板 **富文本另立项、不并 #53 群**（体量不匹配：逐段样式 × 测量/折行/命中 × 两链 × 选中复制；
     沿 phase28「另立项」口径）；#53 群其余项维持不动。ECDI 侧落账 = `roadmap-deferred.md` **v2.81 / #88**（待做区）。
  ③ 本仓回写：REQ-20 状态行 / 既有裁决行 / 边界行 / 待办①（已裁）+ §2 头部摘要；Phase 号与顺位待 ECDI 排，
     本仓消费仍不预做、块级全套不受影响先行。
  ④ 顺带订正：REQ-20 所引三份 ECDI 文档实在 `docs/dev/` 下（原引路径缺前缀）；归属核验结论本身全部属实
     （代码/公共头 grep 零命中，仅 phase7 需求稿一处文档性提及 RichText）。
  ⑤ 头部 v5.22 → **v5.23**。
- **v5.22（2026-11-07）★ 登记 REQ-20：段内内联样式（styled spans）能力缺口**
  ① 用户裁定「富内容应该把转录全做好」⇒ 范围拆两层：**块级全套先行**（应用层，`rich-content-plan.md` §G）；
     **内联粗体/斜体/行内 code** 碰到「一控件一套字体」边界，按铁律登记交 ECDI。
  ② 归属核实到源码仓（§1.1）：安装头 + `D:\Project\ECDI` 全库 grep `RichText/TextRun/InlineStyle/TextSpan/StyleRun/SetRangeFont`
     零命中；`TextWidget.h:23` 文字视觉唯一来源 = 控件级 TextStyle。
  ③ 对账 ECDI 既有裁决：富文本 = phase28/29/36/38 一贯非目标、归 **#53 渲染表现力群**（单消费者不立项，
     重启条件 = 第二消费者）——本仓是否构成重启条件由 ECDI 裁，本仓不预设、不排期。
  ④ 头部 v5.21 → **v5.22**。
- **v5.21（2026-11-05）★★ REQ-19-A 闭合：换新库 + 本仓 `SplitterBar` 收敛 + `regress` PASS**
  ① 用户 15:57 交付新 `ECDI.lib`（**11975450 B · `c5208346…`**；旧 `3866a17d…` / 11974558 B 已备份
     `.scratch/ECDI.lib.prev-20261105`），并同步安装头 `Core/CursorShape.h`。
  ② 安装面核对：`third_party/ecdi/include` 与 ECDI 源码头 `diff -rq` **唯一漂移 = `Core/CursorShape.h`**
     （即本次改动本身）+ 安装期生成的 `Core/version.h` ⇒ 纯值域追加，其余公共头零变化。
  ③ **本仓收敛**：`SplitterBar.cpp` **182 → 116 行**——删掉匿名命名空间 44 行（几何容差判据 /
     句柄缓存 / `::GetCursor()` 值比较 / `RestoreArrowIfSizeWe`）、5 处调用点、`<Windows.h>`；
     `SplitterBar.h` 加一行 `GetCursor()` override 返回 `CursorShape::SizeWE`（与 `TextBox.h:38` 同款）。
     ⇒ 光标交回框架：每条 `WM_SETCURSOR` 按命中控件重查，鼠标离开自动换回，**应用侧零状态机**。
  ④ 重链 `cmake --build gui/build`（Ninja，34 步）后 `tools/regress.py` **RESULT: PASS**（11m54s，
     全部探针 `rc=0`）。
  ⑤ **19-A ✅ 闭合**；**19-B 仍开放**（`AppPreview.cpp` 的 `ShellExecuteW` 未动，等 ECDI 侧裁决是否
     提供 `RevealInFileManager`/`OpenExternal`）。
  ⑥ 头部 v5.20 → **v5.21**。
- **v5.20（2026-11-05）★ REQ-19-A 的 ECDI 侧已落地（ECDI #87 / `1142738`），588/588 + 判别力已证**
  ① ECDI 侧三处改动全部落地：`CursorShape` 追加 `SizeWE=2`…`Cross=8`（只追加末尾）、
     `CursorHandleFor` 每值一行 `IDC_*`、`Platform87.CursorShapeMapping` 用例。
  ② **验证**：ClangCL Debug **588/588 全绿**（用户实跑确认）；★ **判别力已证**——把 `SizeWE`
     的映射临时改回 `IDC_ARROW` 后重跑，**恰该用例红（587 passed / 1 failed）**，其余全绿，
     说明它不是恒绿的空转用例。回退已复原。
  ③ ★ 落地时**顺手订正了 ECDI 侧台账**（`roadmap-deferred.md` v2.80 / `#87` + README 用例数
     587→588）；并修掉该文件里一处**重复 BOM**（`docs/dev/roadmap-deferred.md` 曾以 `BOM BOM` 开头）。
  ④ ★ **踩坑记录（给后来者）**：`cmake-build-debug-mingw` 目录在本机跑全量会在
     `ImageDecode.Png1x1White` 崩（`vector<unsigned char>::operator[]` 越界断言）——**与本条无关**
     （改动不碰解码），同仓 ClangCL 链同用例是绿的 ⇒ 该构建目录的解码配置/产物有问题，别拿它当判据。
  ⑤ 本条**仍开放**：本仓 `SplitterBar` 的 44 行绕行要等**新 `ECDI.lib` 交付**后才删得掉
     （已装的头里还没有 `SizeWE`，现在改必然编译不过）。
  ⑥ 头部 v5.19 → **v5.20**。
- **v5.19（2026-11-05）★ REQ-19-A 补「上下箭头」与可落地补丁；★ 订正一条不可执行的验收判据**
  ① 用户 15:21 补「**目前看来应该同时也缺了上下箭头**」⇒ 新增「取值边界」行：**主张 `SizeWE` + `SizeNS`
     同批补齐**（同一个 `SplitterBar` 加 `Side::Top/Bottom` 立刻需要；只补一半 = 留第二次改动）；
     `SizeNWSE`/`SizeNESW`/`SizeAll`/`Hand`/`Cross` 同族可选、本仓**无消费者不主张**；
     **`Wait`/`AppStarting` 明确不收**——模态忙闲态 ≠ 「悬停自述」，混进枚举会破坏「逐次重查」语义。
  ② 新增「建议补丁」行（ECDI 侧可直接落地）：`CursorShape` 追加 `SizeWE=2 … Cross=8` →
     `CursorHandleFor` 每值一行 `return ::LoadCursorW(nullptr, IDC_*)`（缺省仍 `IDC_ARROW`）→ 测试。
  ③ ★★ **订正验收判据 ①（自伤）**：原写「断言 `CursorHandleFor(SizeWE)` 等于 `::LoadCursor(...)`」
     **根本不可执行**——该函数在 `Win32PlatformWindow.cpp` 的**匿名 namespace** 内，测试取不到。
     改为复用既有 ②-b 装置（真窗口 + `WM_SETCURSOR(HTCLIENT)` + 断言 `::GetCursor() == ::LoadCursorW(IDC_SIZEWE)`），
     并补「修复前必红」的判别力说明。★ 教训入账：**判据写在账本里就必须能跑**，否则等于账本自己
     制造了一个「恒成立的负向用例」（与协议 §8.5 同一条纪律）。
  ④ 头部 v5.18 → **v5.19**。
- **v5.18（2026-11-05）★ REQ-19 裁定：光标取案 A；`ShellExecuteW` 并入 19-B 记作能力缺口**
  ① 用户 15:18 裁「**案A吧，并入REQ19记作能力缺口**」⇒ 19-A 修法锁定为「`CursorShape` **只追加末尾**」，
     `CursorHandleFor` 补 `IDC_*` + `PlatformQuickfixTests` 加断言；**案 B（通用逃生舱）记为已否决**
     （会把平台头泄回应用；光标本就是 OS 概念，枚举补全即够）。
  ② **19-B 立案（并入，不另立 REQ-20）**：`AppPreview.cpp:241-250`「在资源管理器中显示」只能裸
     `ShellExecuteW` + `<windows.h>`/`<shellapi.h>`；★ 而**同一 foot 栏相邻**的「复制路径」
     （`:263-265`）走 `PlatformWindow::SetClipboardText` ⇒ **同一文件 20 行内的一对一对照**，
     归属无争议。
  ③ ECDI 侧核实（§1.1）：全库**零** `ShellExecute` 引用；平台层「把系统能力包成方法」已有范式先例
     ——`PlatformWindow.h:72/76` 的 `GetClipboardText`/`SetClipboardText` ⇒ 要的是沿用同一范式，不是新范式。
  ④ 建议 `PlatformWindow` 增**窄**方法（`RevealInFileManager` 或 `OpenExternal`），失败返回 false；
     **不做通用 shell 抽象**（YAGNI，与 19-A 否决案 B 同理由）。★ 若 ECDI 认为「拉起外部程序」超出
     框架边界（安全考量），本条**转不登记**、本仓保留绕行——**由对方裁决，本仓不预设**。
  ⑤ 两例统一标「**能力缺口、非缺陷、不排期**」；本仓收敛**一律等 ECDI 落地后做**（当前不预做，
     REQ-18 已证明「先改本仓再等框架」会多走一轮）。本轮只改**账本一个文件**。
  ⑥ 头部 v5.17 → **v5.18**。
- **v5.17（2026-11-05）★ REQ-19 立案：`CursorShape` 值域缺第三种形态 ⇒ 应用只能裸调 Win32（ECDI 侧，开放）**
  ① 用户 15:1x 观察（REQ-18 收口后）：「harness 那自己修好了，不过这也看出一个问题，
     **我们没有现成链路，让使用者直接操作 Win32 了**」。
  ② 缺口定位：自述通路四段完整（`GetCursor` → `OnQueryCursor` → `WM_SETCURSOR` → `::SetCursor`），
     **残的是值域**——`CursorShape.h:13-16` 只有 `Arrow`/`IBeam`，`Win32PlatformWindow.cpp:44-60`
     除 `IBeam` 外一律回落 `IDC_ARROW` ⇒ 「左右拉伸」在框架里不存在。
  ③ 代价量化（本条立案理由）：本仓 `SplitterBar.cpp` 绕行为此付出**匿名命名空间 44 行 + 5 个调用点 +
     `#include <Windows.h>`**；其中最贵的是用 `::GetCursor()` 值比较去逆推「光标是不是我设的」——
     而框架本来就知道（每次 `WM_SETCURSOR` 重查）。★ 裸 `::SetCursor` 无 owner ⇒ 应用被迫手写光标
     状态机，**REQ-18 的光标卡死正是这条绕行的产物**（该条已判 harness 侧、ECDI 零改动）。
  ④ 建议案 A（只追加末尾，与 `TextBox.h:38` 同款 override 即可替代全部绕行）/ 案 B（通用逃生舱，
     不作首选，会把平台头泄回应用）。**不替 ECDI 裁决**；按 11-04 定调**不排期**、不阻断功能。
  ⑤ ★ 纪律产出：「应用出现 `#include <Windows.h>` + 裸 Win32 调用」= 潜在 REQ 的**信号**。
     本仓全量盘点后仅剩 2 处：本条（`SplitterBar.cpp` 光标）与 `AppPreview.cpp:39-40` `ShellExecuteW`
     （框架无「打开外部 URL」能力）——后者**是否并入待裁**，不擅自扩大范围。
  ⑥ REQ-18 条目内原「附带观察…不单独立 REQ」按历史不改写保留，但已加**增量订正**指向本条
     （结论被取代）。本轮只改**账本一个文件**，未改代码 / ECDI / 探针。
  ⑦ 头部 v5.16 → **v5.17**。
- **v5.16（2026-11-05）★★ REQ-18 结案：归属反转 harness 侧，撤单，ECDI 不用动**
  ① 用户 15:05 回报 v5.15 判据「**两者都好了**」：a 光标离开分割条恢复箭头；
     b 拖动跟手、蓝线不再常亮。原稳定复现路径全绿。
  ② 两阶段受控对照（每步只加一个自变量、ECDI 全程零改动）：S0 基线两症状齐发 →
     S1 修 `SplitterBar.cpp`（几何判据 + `OnMouseLeave` 对称恢复，v5.12/v5.13）⇒ 光标愈、
     拖拽仍退化 → S2 把 `AppSettings.cpp` 的自我重建挪到下一 tick（v5.15 三文件）⇒
     **两症状全消**。⇒ 不需 ECDI 侧假说即可解释并修复，v5.8 的「OS 捕获泄漏」判为
     **第 5 次归属误判**（前四次 `REQ-01/03/04/07`；v5.10 的预判应验）。
  ③ 状态 ⏸️ 待裁决 → **✅ 闭合（归属反转）**；标题状态符 🔄 → ✅、条目顶部加
     「复核订正 4 / 结案」、状态行与归属行加时点订正，**下表原判逐条保留不改写**
     （历史不改写惯例），整体由结案段取代。★ **用户不必去 ECDI 改 `ForgetSubtree`**，
     亦不必换库重链。
  ④ 诚实边界：H1/H2 机制二分仍未完成（实验 X 的两个混淆变量未排除，不宣称 H2 成立）；
     `::GetCapture()` 物理读数自始至终未测 ⇒ ECDI 三处源码只记为**潜在隐患观察项**，
     不叫缺陷、不占新 REQ、不跟进；S1/S2 各自必要性未做反向对照，两处都保留；
     未加 `--cursor-probe`，凭据是用户一次人工复验。
  ⑤ 本轮只改**账本 + 今日日志**，未改任何代码 / ECDI / 探针；工作区五个 M
     （账本 + 四个源码文件）保持，**未 commit**（commit ≠ push，等指令）。
  ⑥ 头部 v5.15 → **v5.16**。
- **v5.15（2026-11-05）★ 延迟重建落地（用户授权三文件），待复验**
  ① 用户 15:00 授权「改这三个文件」。改动：`AppSettings.cpp:1147-1148` 的 `OnClick` 内两行
     重建 → 只置 `m_settingsListDirty = true`；`App.h` 新增该标志；
     `AppStream.cpp::OnReplayTick` 新增消费块，在 tick 里调
     `RebuildSettingsProviderList()` + `RebuildSettingsModelList()`。
  ② 安全性核实：`kReplayTimer` 在窗口初始化时**无条件**启动（`App.cpp:688`，33ms），
     消费块位于 `OnReplayTick` 的 `if (!m_sess->source) return;` 之前 ⇒ 空会话也能消费；
     字段区 `SetText` 仍即时生效，只有「[编辑中]」标记最多迟一帧。
     ⇒ Down/Up 完整走完才摘树，焦点 / 捕获 / hover 三条登记不再被中途清空。
  ③ 校验：三文件 BOM 与 HEAD 一致（`AppStream.cpp` 工作区 CRLF 是既有本地漂移，
     `.gitattributes eol=lf` 提交时归一化，HEAD 为 LF）、`git diff --check` 干净、
     三文件 diff 仅 4 / 11 / 9 行、precommit 三门绿；编译仍由用户在 CLion / VS 完成
     （Bash 下 MSVC 缺 `INCLUDE`，见 v5.12）。
  ④ **待复验**：设置 → 点服务商行 → 关闭 → 扫分割条 → 拖分割条：
     a 离开后光标恢复；b 拖动跟手、蓝线不常亮。两者都好 ⇒ 触发点坐实为「回调内摘树」，
     **归属 harness 侧，REQ-18 降级或撤单，ECDI 不用动**；仍异常 ⇒ 才去 ECDI 改
     `ForgetSubtree` 或先加只读探针 `::GetCapture()`。
  ⑤ 状态仍 ⏸️ 待裁决；头部 v5.14 → **v5.15**；未 commit。
- **v5.14（2026-11-05）★ 复验 2：光标提示已正常，拖拽仅在「那次操作之后」退化**
  ① 用户 14:58 复验：a) 分割条上左右箭头**正常显示**（16px 容差生效）；
     b) 分割线**正常高亮**（`m_dragging` 生效），但**必须非常慢地移动鼠标才跟得上**；
     且只在「设置 → 点服务商行 → 关闭」**之后**出现，**操作前完全正常**，并会一直保持蓝色。
  ② ⇒ v5.13 的「3px 热区没提示 ⇒ 按不准」**不是主因**（a 已正常而 b 仍异常），该推断作废，
     原记录保留不改写；容差本身无副作用，暂留。
  ③ ★ 新结论：拖拽代码逐字未改 ⇒ 不是本轮修复引入的回归。「一直是蓝的」说明
     `m_dragging` / `m_hovered` 粘住；「必须非常慢才跟上」说明每条 Move 都在跑完整的
     `SetSize → Arrange → m_sidebarDirty → 侧栏重建` 重链 ⇒ **同一个操作序列污染了全局
     鼠标状态**，光标卡住只是它的症状之一。SplitterBar 修复只治了显示，没治状态。
  ④ ★ **排除一条假说**：`HarnessApplication::OnTimer`（`App.cpp:147-172`）对
     `kReplayTimer` 在 override 里无条件分流到 `OnReplayTick`，**不依赖焦点**；
     只有基类 `Application::OnTimer`（ECDI `Application.cpp:311-329`）的光标 / 动画 tick
     走焦点链 ⇒ 「`RemoveChild` 清焦点 ⇒ tick 停摆 ⇒ 侧栏不重建」**不成立**，别往这条查。
  ⑤ ★★ **下一步（等用户原子授权）**：把 `AppSettings.cpp:1147-1148` 的列表自我重建从
     `OnClick` 回调挪到下一 tick（`App.h` 新增 `m_settingsListDirty` +
     `AppStream.cpp::OnReplayTick` 消费，复用 `m_modelListDirty` 同族通道），
     让 Down/Up 完整走完再摘树。这同时是 H1/H2 的判别：**若「操作后拖动也正常」⇒
     触发点就是「回调内摘树」，归属回到 harness 侧，ECDI 不用改**。
  ⑥ 状态仍 ⏸️ 待裁决；本轮**未改任何代码**（涉及新文件，需另行授权）。头部 v5.13 → **v5.14**。
- **v5.13（2026-11-05）★ 复现结果：光标已恢复，但拖拽「不跟手」——补光标判据容差**
  ① 用户 14:51 反馈：新构建（其实际运行的是 `gui/cmake-build-release-visual-studio/harness_gui.exe`，
     14:48:47 晚于源码 14:48:06，确认含本轮改动）下**光标卡住已消失**，但左右栏拉不动 / 不跟手。
  ② ★ **拖拽代码逐字未改**（`OnMouseButtonDown` 的 `m_dragging/SetCaptureWidget`、
     `OnMouseMove` 的 `delta → SetSize → m_onResize` 全部保持原样）⇒ 不是尺寸链回归。
     最可能的原因是**光标提示消失**：`PointerOverBar` 用 `GetAbsolutePosition()` 重算的
     **3px** 矩形与 ECDI HitTest 的命中口径差 1-2px 即整片 miss ⇒ 分割条上恒为 Arrow，
     3px 热区没有视觉提示，用户按不准 ⇒ 观感「拉不动」。
  ③ 处置：`PointerOverBar` 加 `kCursorTolerance = 16px`（**只影响提示形态**，
     实际拖拽命中仍由 ECDI HitTest 决定）；`OnMouseMove` / 拖拽逻辑不变。
  ④ **待用户复验两条**：a) 鼠标停在分割条上是否重新显示左右箭头；b) 按住拖动时
     分割线是否高亮（`m_dragging` 生效的可见证据）且宽度是否连续变化。
     a 是 b 否 ⇒ 另查 `m_sidebarDirty` 的 tick 重建链（`AppUiMain.cpp:141-147`）或捕获残留；
     a 否 ⇒ 几何口径仍不对，改用「由 hover 状态驱动 + 宽松兜底」。
  ⑤ 状态**仍 ⏸️ 待裁决**：光标症状已除，但 H1/H2 的机制二分尚未完成。
     头部 v5.12 → **v5.13**；本轮代码改动仍只 `gui/src/SplitterBar.cpp`。
- **v5.12（2026-11-05）★ REQ-18 受控对照已就位：本仓 `SplitterBar` 修复落地（待复现）**
  ① 用户 14:44 授权「改 SplitterBar」；本轮代码改动**只** `gui/src/SplitterBar.cpp`（+62/−3）。
  ② 修法：光标不再按「这条 Move 是否派发给我」决定，改为按「**事件坐标是否真落在自身热区**」
     决定（`PointerOverBar`：MouseEvent 客户区坐标 ↔ `Widget::GetAbsolutePosition()` 同基准，
     依据 `Widget.cpp:368-369` 的 HitTest 坐标变换与 `:785-804` 的逆变换）；
     `OnMouseLeave` / 拖拽松手离开热区时 `RestoreArrowIfSizeWe()` 只把「确实是我们设下的
     SIZEWE」改回 Arrow，**不覆盖 TextBox 的 IBeam** 等 `WM_SETCURSOR` 正规路径的形态。
  ③ 这是**两条假说都承认存在**的那个 harness 侧缺陷 ⇒ 只改这一个自变量，**ECDI 零改动**。
  ④ 验证状态：`git diff --check` 干净、BOM / LF 与 HEAD 一致（漂移 0）、precommit 三门绿；
     ★ **增量编译未跑成**：Bash 下 `cmake --build gui/build --target harness_gui` 报
     `C1083 无法打开包括文件 "algorithm" / "map"`，且**未改的 `Transcript.cpp` 同样失败**
     ⇒ 是 MSVC 环境未初始化（缺 `INCLUDE`），**不是本改动的语法错误**；按本仓惯例交给用户在
     VS / CLion 编译。静态核对已做：`GetAbsolutePosition/GetWidth/GetHeight` 均 const noexcept、
     `MouseButtonUpEvent → MouseButtonEvent → MouseEvent` 有 `GetMouseX/Y`、
     `LoadCursor/GetCursor/SetCursor` 由 Windows.h 提供。
  ⑤ **待用户复现**（设置 → 点服务商行 → 关闭 → 扫分割条）：症状消失 ⇒ 归属 harness 侧、
     REQ-18 降级或撤单、ECDI 不用动；仍卡 ⇒ H1 实锤，再去 ECDI 改 `ForgetSubtree`
     （或先加只读探针 `::GetCapture()`）。状态**保持 ⏸️ 待裁决**，不提前改判。
     头部 v5.11 → **v5.12**。
- **v5.11（2026-11-05）★ REQ-18 复核订正 3：实验 X = 不恢复，行为判据用尽，改走本仓受控对照**
  ① 用户 14:41 回报：不点击、只移出窗口再回来，**依旧是 `IDC_SIZEWE`** ⇒ 14:38 的判别实验 X
     结果 = 不恢复，方向指向 **H1**（ECDI 捕获残留），即重新进入窗口并没有触发光标重查。
  ② ★ **仍不判 ECDI、状态保持 ⏸️ 待裁决**：混淆变量未排除（移回路径是否又扫过分割条、
     窗口外首次点击是否被吞）；且按 `Application.cpp:455/508`，窗口内一次 Down+Up 本应
     跨边界走到 `ReleaseCapture()`，与「窗口内点一下不自愈」有一处不吻合。
  ③ ★★ **收口决定**：连续三次行为判据都不足以二分，不再靠肉眼推断。改为**只改一个自变量**
     的受控对照 —— 先修本仓 `SplitterBar`（两条假说都承认它「一次性 `SetCursor`、无恢复」
     是真缺陷），**不碰 ECDI**，再按原路径复现：症状消失 ⇒ 归属 harness 侧、REQ-18 降级或
     撤单；仍卡 ⇒ H1 实锤，再去 ECDI 改 `ForgetSubtree`（或先加只读探针读 `::GetCapture()`）。
  ④ 本轮仍**只改本账本一个文件**；未改 ECDI、未改本仓代码 / 探针（代码改动等用户授权）。
     头部 v5.10 → **v5.11**；状态不来回翻转，保持 ⏸️。
- **v5.10（2026-11-05）★ REQ-18 复核订正 2：状态改 ⏸️ 待裁决，可能是第 5 次归属反转**
  ① 用户 14:38 补充「**在 UI 界面外点一下再回去就恢复了**」⇒ 关键自变量从「点没点」变成
     「**有没有移出窗口再回来**」。
  ② 两个竞争解释：**H1** = ECDI OS 捕获残留，窗口外点击被系统夺走捕获（`WM_CAPTURECHANGED`
     → `OnCaptureLost`）才打破；**H2** = 纯 harness 侧，`WM_SETCURSOR` 在鼠标重新进入窗口时
     把光标设回 Arrow（`Win32PlatformWindow.cpp:1143-1150, 1154-1174`），窗口内点击不伴随
     移动则不重查。H2 同时解释两条观测且只需一个已知缺陷（`SplitterBar` 一次性 `SetCursor`
     无恢复）⇒ **按奥卡姆剃刀本轮倾向 H2**。
  ③ ★ 若 H2 坐实，本条即**第 5 次归属误判**（前四次 `REQ-01/03/04/07`，成因均为「未先证伪
     harness 侧解释就判 ECDI」）。三处源码仍是客观代码事实，但**与本现象的因果链未坐实**。
  ④ **判别实验 X（零代码）**：卡住后全程不点击，只把鼠标移出窗口到桌面、停一下、再移回；
     恢复 ⇒ H2、本条改判 harness 侧或撤单；不恢复且必须窗口外点一下 ⇒ H1、维持 ECDI 侧。
     辅助 Y：窗口外首次点击是否被吞（被吞 = OS 捕获实锤）。
  ⑤ **状态 🔄 → ⏸️ 待裁决**，条目顶部加醒目复核标注 2；**明确暂缓 ECDI 侧修复**。
     v5.8 / v5.9 原判逐条保留，不改写。头部 v5.9 → **v5.10**。
  ⑥ 本轮仍**只改本账本一个文件**；未改 ECDI、未改本仓代码 / 探针。
- **v5.9（2026-11-05）★ REQ-18 复核订正：点击自愈判据是单向的，机制降级为待验证假说**
  ① 用户 14:35 实测：光标卡住后点一下左键**不自愈**、会一直保持那个状态。
  ② 复盘：v5.8 条目里「点一下是否自愈（若是即 `Application.cpp:1114` 预言的自愈出口）」被写成
     双向验证，实为**单向判据**——`WM_SETCURSOR` 只在鼠标移动 / 窗口激活时发
     （`Win32PlatformWindow.cpp:1154-1174`），点击本身**不重设 `::SetCursor` 的结果**；
     而一次完整 Down+Up 按源码会跨边界释放 OS 捕获（`Application.cpp:455/508` +
     `Window.cpp:617-626`），但「释放捕获」≠「重设光标」⇒ 不恢复既可能因为没移动鼠标，
     也可能因为另有状态在每次 Move 重设 SIZEWE，两种解释都还没排除。
  ③ 结论：三处源码事实与两组受控对照**仍成立**；「OS 捕获泄漏 ⇒ 光标卡住」这一**因果链降级为
     待验证假说**。条目内已加醒目标注；状态暂留 🔄，但**建议暂缓 ECDI 侧改动**，避免在机制
     坐实前重蹈 `REQ-01/03/04/07` 的归属误判。
  ④ 下一步二选一：a) 零代码行为对照 = 卡住后把鼠标移出本窗口、点别的窗口看第一次点击是否被吞
     （OS 捕获的直接后果），再移回本窗口明显移动一段看是否恢复；b) 授权本仓加**只读**探针
     `::GetCapture()` / `::GetCursor()`，把机制变成读数（推荐，一锤定音）。
  ⑤ 本轮仍**只改本账本一个文件**；未改 ECDI、未改本仓代码 / 探针。头部 v5.8 → **v5.9**。
- **v5.8（2026-11-05）★ REQ-18 立案：`ForgetSubtree` 摘树路径泄漏 OS 鼠标捕获（ECDI 侧，开放）**
  ① 用户 14:0x 报告光标卡在 `IDC_SIZEWE`；14:27 补充精确路径：设置 → 点服务商行（**不改配置**）→ 关闭 ⇒ 稳定触发；设置 → 直接关闭 ⇒ 正常（**只改一个自变量**）。
  ② 本仓触发点 `AppSettings.cpp:1102-1149` 的 `OnClick` 在回调内 `RebuildSettingsProviderList()`（`:1073-1075` 摘光子节点）⇒ 被点击的 Button 在 Down/Up 之间摘树。
  ③ ECDI 侧三处源码闭环：`Window.cpp:721-725` 只清 `m_captureWidget`、不调 `SetMouseCapture(false)`；`Application.cpp:508` Up 末尾的 `SetCaptureWidget(nullptr)` 被 `Window.cpp:617-626` 的幂等判据短路；`Win32PlatformWindow.cpp:1415` 的 `::SetCapture` 因此永不配对释放。
  ④ 归属判 **ECDI 侧**；已按 §1.1 查 `Widget.h` 基类公共头 + ECDI 源码仓（取证时点 HEAD `daed47c`）+ `MouseCaptureTests.cpp`。★ **证据强度如实标注**：源码链确定，`::GetCapture()` 物理读数**未实测**，不冒充机读证据。
  ⑤ 建议案 A = `ForgetSubtree` 清捕获时同步 `SetMouseCapture(false)`（平台层已有 `GetCapture()==m_hwnd` 守卫 + `ReleasingGuard`）；案 B = 幂等判据改用独立 OS 状态位。**不替 ECDI 裁决**，两案取舍与 `noexcept` 异常规格评估都留给对方。
  ⑥ **用户裁定「你登记，然后我去 ECDI 修」** ⇒ 本轮**只改本账本一个文件**；未改 ECDI、未改本仓任何代码 / 探针 / 其它文档载体。REQ-18 保持 🔄 开放，待换库重链 + 人工复现后回写闭合。
  ⑦ 附带观察（`CursorShape` 缺 `SizeWE`）**不单独立 REQ**，只在 REQ-18 条目内留一行指针（能力缺口、有绕行；按 2026-11-04 定调不为采数据挡功能）。
  ⑧ 头部 v5.7 → **v5.8**。
- **v5.7（2026-11-04）★ REQ-17 闭合：IME 无组合直插通路补齐（中文标点丢字）**
  ① 用户 20:04 交付带修复的 `ECDI.lib`（11974558 B）；本仓换库备份旧库 `.scratch/ECDI.lib.prev-20261104`（11974328 B）。
  ② ECDI **#86** 三处：翻译器新增 `WM_IME_CHAR`（`return 0` 而非 `nullopt`，堵非 Unicode 语义的二次拆包）· `CommitComposition` 的 F8 判据收窄为「无组合 ∧ 空结果」· 吞字计数器对 `WM_CHAR` 与 `WM_IME_CHAR` 同时递减（否则丢字变双写）。
  ③ 安装面：头文件**零漂移**（`diff -rq` 只报安装期生成的 `Core/version.h`）⇒ 纯 .cpp 修复，不需要全量重编；`ninja` 只跑了 `[1/2] Linking`。
  ④ 产物：harness `808f1903…`（2122752 B · 20:06，前值 `e0f4952e…` / 2122240 B）。
  ⑤ 同批库还含 ECDI **#85**（最小化→还原后 DWM 帧延伸区发白），与本条无关，只记出处不另立 REQ。
  ⑥ **人工验收通过（20:19）**：用户在输入框实测「中文标点能打入了」⇒ 本条从立案到闭合 **当日完成**，含换库、重链、39 门回归与上屏实测。
  ⑦ 头部 v5.5 → **v5.7**（v5.6 = 本条立案，同一日内先立案后闭合）。
- **v5.6（2026-11-04）★ REQ-17 立案：IME 无组合直插在 ECDI 侧没有通路（中文标点丢字）**
  ① 用户反馈「输入框输入不了中文的逗号和句号」，汉字正常。
  ② 三处源码核实：ECDI 全库无 `WM_IME_CHAR`（`git log -S` 全历史为空）；`CommitComposition` 无组合即 no-op 且被写成 F8 判据；`GCS_RESULTSTR` 之后 `WM_CHAR` 被故意吞。
  ③ 归属判 **ECDI 侧**（平台消息覆盖缺失），首选补丁 = `WindowMessageHandler.cpp` 加 `case WM_IME_CHAR`，**不动 F8**。
  ④ 未定为「标点走 `WM_IME_CHAR` 还是不带 COMPSTR 的 `RESULTSTR`」，判别实验已写入条目（改完打一次「，」即可二分）。
  ⑤ 本条**不排期**：可用性阻断项，按 2026-11-04 定调（能投入使用优先于采数据）即修。
  ⑥ 头部 v5.5 → **v5.6**。
- **v5.5（2026-11-01）★ REQ-16 闭合：按钮快速进出时背景过渡动画未取消导致 hover 残留**
  ① **问题复现**：用户反馈「鼠标如果经过的特别快的话会出现鼠标已经远离该控件，但是依旧为鼠标悬停在上面的效果」。
  ② **根因定位**：`ECDI\src\Widget\Button.cpp` 的 `Button::AnimateBackgroundTo` 中，开头有 `if (target == m_displayedBackground) return;`。当鼠标高速划过时，进入时提交了指向 hover 的 200ms 过渡动画（此时 `m_displayedBackground` 仍为 normal 初值）；在首帧插值前移出时，目标还原 normal，导致 `target == m_displayedBackground` 触发提前 return，**漏调用了 `AnimationManager::Cancel(m_backgroundAnimToken)`**。旧动画在后续 Tick 中继续运行，在 200ms 后强行把按钮刷成 hover 高亮态且不再恢复。
  ③ **ECDI 侧修复**：在提前返回分支中补上 `if (window != nullptr) window->GetAnimationManager().Cancel(m_backgroundAnimToken);`。
  ④ **测试闭合**：ECDI 侧新增受控真窗口单测 `Widget.ButtonFastHoverLeaveCancelsAnimation`（进入后未 Tick 立即离开，随后逐帧推进 300ms 验证呈现值恒保持 normal 不变），测试通过；本仓重新链接最新 `ECDI.lib`，全量回归套件绿。
  ⑤ 头部 v5.4 → **v5.5**。
- **v5.4（2026-11-01）★ 模型行左对齐 + 4 处滚动条文字债收口**
  ① **左对齐**（用户裁决）：`RebuildModelPopupList` 的模型行补 `.textAlignment = ECDI::TextAlignment::Left`。★ 必须**显式**写：`ButtonStyle::textAlignment` 的主题缺省是 **Center**（`ButtonStyle.h:32` / C30-3），不写就居中——居中在「工具条按钮」上是对的，在「可选列表」上像标题。**不得依赖枚举零值 Left**（缺省恒由主题注入 Center，`VerticalAlignment.h:9` 专门拿这条当反例）。写法与 `AppHistory` / `AppSession` 行根一致。
  ② **文字债 4 处全部收口**：`AppInternal.h`（`ApplyCompactScrollBars` 头注）、`UiCommon.cpp`（同函数实现注）、`AppTranscript.cpp`（`kTableBarThickness`）、本账本 v4.4 ②（行内划删除线 + 指向本条）。统一改法 = **结论不变、理由换轨**：从「躲上游 `BarThickness()` 只读竖条的缺陷」改成「**设计裁决**：全仓条宽统一」。同时钉住一条**没有变**的：`ApplyCompactScrollBars` 末尾的 `SetSize` 重排**仍必需**，且新头文件反而把它写明了（改厚度不触发重排；`SetContentExtent` 在 extent 未变时提前返回救不了）。
  ③ **实测**（`sha 4ba59bdf`，`tools/regress.py` 7m43s）：`RESULT: PASS`，全 33 条 `rc=0`；`--overlay-probe` 10/10、`--valign-probe` 7/7、`--table-probe` 6/6、`--grow-probe` 12/12（后三门都吃模型浮窗/文本框，故一并看）。人验：模型行文字起点与标题/分组左缘对齐。
  ④ 头部 v5.3 → **v5.4**。
- **v5.3（2026-11-01）★ 模型下拉三缺陷闭合：位置错位 / 列表整块空白 / 「不知所以」**（用户实测反馈，非探针发现）
  ① **现象**（用户报告 + 我实机复现）：点顶栏「云知声 › ?」胶囊，弹窗出现在**中栏转录区中间偏右**（改前实测弹窗 `x 532..872 / y 142..274`，而胶囊在 `x 847..1044 / y 8..30` ⇒ 横向偏左 ~172px、纵向偏下 ~112px），且**框里只有一个空搜索框和一行灰底注，没有任何模型行**。
  ② **根因 A（位置）**：`RepositionModelPopup` 锚的是 `m_scroll`——**转录视口**，不是 chip。原注释写「要『贴视口右上角』」，话没说错，错在把「视口」当成了正文视口。改：锚 `m_hud.modelChip` 的**绝对几何**，右缘对齐 + 下沿 6px（弹窗 340 宽、chip 200 宽 ⇒ 必须右对齐，左对齐会向右突出出界），并按客户区 `std::clamp` 夹取（窄窗口下防左缘进负区、矮窗口下防底注被顶出）。用绝对坐标而非父容器内坐标：chip 在顶栏 `HorizontalLayout` 里随时会因重排移动。
  ③ **根因 B（空列表）**：`BuildModelPopup` 里 `modelPopupList` **从未 `SetSize`** ⇒ 0×0 ⇒ `Widget::Paint` 第一条 `if (!ctx.IsRectVisible(self)) return;`（`Widget.cpp:605`）**整棵列表子树零绘制**。行都建出来了、一个都没画 ⇒ 这就是「探针全绿而屏幕空着」的标准形状（本探针此前只看开合与挂载）。
     同时**删掉**该容器上的 `VerticalLayout`：它的 `Arrange` 会把每个子项改写成 `(padding, y累进)`（`VerticalLayout.cpp:99`），把 `RebuildModelPopupList` 手工算的 `SetPosition(0,y)` 全部覆盖。**同一个容器不许有两个排布者**；留手工那套——y 游标与溢出收敛（`hidden` 计数 + 底注「另有 N 项未显示」）是配套的，布局不知道这件事。
  ④ **根因 C（不自明）**：加标题行「切换模型」。即便列表修好，「一列模型名 + 一个空搜索框」仍不像菜单、像一张表。
  ⑤ **几何收进单一真相源**：`AppInternal.h` 新增 `kModelPopup*` 一族（W340 / H300 / PadX10 / TitleY10 / SearchY32 / ListY64 / ListMaxH210 / FootY276 / DropGap6）。此前 `BuildModelPopup`（AppUiMain.cpp）与 `RebuildModelPopupList`（AppSettings.cpp）**各写一份数字**——正是「标题一加就错位」的漂移面。弹窗 272→300 是**给标题让位**：列表可用高必须仍是 210，否则「能装几个模型」会随标题加不加而变（视觉改动偷渡成行为改动）。
  ⑥ ★★ **探针补强（本条最该记住的部分）**：`--overlay-probe` 此前**对位置一条断言都没有**——只记「开没开」「挂在哪」，于是弹窗能被扔到正文中间而 10/10 全绿。新增 `OverlayProbeRec::popupX/Y/W/H` 与 `chipX/Y/W/H`（绝对坐标；**-1 = 控件不存在**，与真实的 0 区分）+ `popup` 相四条判据：**元断言**（尺寸 == 规格——先把「几何根本没算」与「算错了」分开，否则 0==0 也能让对齐判据通过）+ 右缘对齐 ±1 + 顶边不高于 chip 下沿 + 间隙 ≤ 规格+1。
  ⑦ **实测**：`--overlay-probe` 10/10 绿，读数 `popup (714,66 340×300)` / `chip (854,40 200×20)` ⇒ 右缘同为 1054、顶边 66 = 60+6，逐项符合规格。全量回归 `RESULT: PASS`（7m35s，`sha ea3f4548`，33 条全 `rc=0`）。
  ⑧ **人验**：截图确认弹窗挂在胶囊正下方、右缘对齐；标题 / 搜索框 / 分组「云知声」/ 模型行「u2-flash · 200K」/ 底注全部可见。
  ⑨ ★ **测量教训（自省）**：我中途用 PrintWindow 位图目测出「弹窗只有 132/152 高、宽 294」，据此怀疑「弹窗被压缩」——**全部是错的**（PrintWindow 位图有缩放偏差，且弹窗底色与正文底色同值、边界判不准）。真实值只有探针读数能定。⇒ **几何结论只认结构化读数，不认像素目测**；此条与项目纪律 2（先证明量具、再谈读数）同源。
  ⑩ 遗留观察（当时未动，**v5.4 ① 已处理**）：模型行文字是**居中**的（`ButtonStyle.textAlignment` 缺省 Center），列表项通常左对齐。
  ⑪ 头部 v5.2 → **v5.3**。
- **v5.2（2026-11-01）★ ECDI 新库换入（Phase 43 / #84 per-axis scrollbar thickness）+ 4 处文字债**
  ① **换入两件（不是一件）**：`third_party/ecdi/lib/ECDI.lib`（`d9fcdce7`，11,974,214 B，来自 `ECDI/cmake-build-release-visual-studio` 的 11-01 14:58 构建）+ **`include/ECDI/Widget/ScrollView.h`**。只换 lib 会留下「头里写着旧 API、库里没有旧符号」的半截状态。逐文件 diff 102 个头：**只有这一个**有差异（安装前缀在 10-30 之后未再同步），其余全等；现已全同步。
  ② **库差异 = 单一提交** `daed47c fix(scrollview): per-axis scrollbar thickness (#84 / Phase 43)`：`BarThickness()` **删除且不留别名** → `VerticalBarThickness()` / `HorizontalBarThickness()`（「名字不带轴」正是缺陷根因，留别名等于让补全继续推荐它）；修复 S1/S4/S5/S6（`NeedsVerticalBar` / `ViewportHeight` 扣错轴、`ApplyLayout` 用一个 `t` 设两条 ⇒ 横条 `thickness` 被静默丢弃）。
  ③ **本仓受影响面 = 零代码改动**：`BarThickness()` 全仓**零调用**（只有 3 处注释 + 本账本引用）；编译 18/18 通过、链接成功、`ECDI.lib` 内已确认新符号。★ **同厚度路径位相同** ⇒ 全仓「两条都设 4px」的几何与绘制**零变化**（回归读数逐项吻合可证）。
  ④ ✅ **4 处文字债（v5.4 ② 已收口）**：`AppInternal.h` / `UiCommon.cpp` / `AppTranscript.cpp` / 本账本 v4.4 ② —— 表述都是「因为 `BarThickness()` 只读竖条、只薄一条会留幻影占位，所以两条必须同设」。新库下**理由已消失**，但结论「两条同设 4px」仍成立——它是**设计裁决**（全仓统一，4 与 12 混排比 4 本身更扎眼），不是绕行。另：`ApplyCompactScrollBars` 末尾的 `SetSize` 重排**仍必需**（新头文件明说改厚度不触发重排，且 `SetContentExtent` 在 extent 未变时提前返回救不了）。
  ⑤ 头部 v5.1 → **v5.2**。
- **v5.1（2026-11-01）★ 改动后首次**全量**回归：功能全绿（含此前红门 `--workspace-probe` 转绿）+ 我的台架补丁自己炸了一次**
  ① **全量结果**（`sha 9ce5a655`，`tools/regress.py`，耗时 7m46s）：夹具 6/6 OK；13 门 OK；补齐 12 门 OK；读数判据 7 条 OK；REQ-03 A/B 8 条 OK；**`--workspace-probe 8/9 green OK`**（10-31 是 `FAILED TO PRODUCE OUTPUT`）；live 设置探针 16/16；协议 `exit=0`。
  ② ★ **`--workspace-probe` 与 `--delprov-probe` 在本构建上都转绿**：进一步印证 `TODO-06` 是**布局/构建敏感**——同一份源码、只改构建开关，红相集合就换了一副样子。**不要再拿「某轮红了」当归因证据**，必须先看 `BINARY:` 那行的 sha256。
  ③ ★★ **教训（自嘲一条）**：v4.6 加的 §4 汇总表第一版写成 `rc={rc:>10s}`，而 `RUNLOG` 存的是**原始 int** ⇒ 第一次全量跑到最后一张表时 `ValueError`，`exit=1` 且**没有 RESULT 行**——前面所有门其实全绿。⇒ **台架补丁必须真跑一遍全量，`py_compile` 不算验证**；这也正是我一直在账本里要求的「0 条要配阳性对照」的同一条纪律，只是这次犯在自己身上。
  ④ 修复：`rc_str(rc)` 后再格式化（`regress.py` §4）；并用 3 门端到端复跑验证输出形态正确。
  ⑤ 头部 v5.0 → **v5.1**。
- **v5.0（2026-11-01）★ `TODO-07` 第二步：`blockRows` 裸指针 → 索引句柄（消掉全仓唯一「跨容器操作仍存活」的指针）**
  ① **改动**：`Session::blockRows` 类型 `std::map<std::string, RowRec*>` → `std::map<std::string, size_t>`（`rows` 下标）；新增唯一读取入口 `HarnessApp::FindBlockRow(id)`（`AppTranscript.cpp`），越界返回 `nullptr`；`AddRow` 写 `rows.size()-1`；`AppStream.cpp` 的 `delta` 与 `block_end` 两个消费点改为 `RowRec* const row = FindBlockRow(id); if (!row) return;`，`row->second->x` 15 处收敛为 `row->x`。
  ② **为什么下标而不是别的**：`rows` 全仓只有 `push_back` 与 `clear`（实测 grep 无 erase/pop_back/pop_front）⇒ 下标在「两次重建之间」稳定；`blockRows` 与 `rows` 仍成对清空。下标 + 越界检查把「行表被重建」从**静默悬垂**变成**一次显式判空**。
  ③ **同时作废一条隐式不变量**：`App.h` 原文写「`blockRows` 里是指向 `rows` 元素的裸指针，靠 deque push_back 不失效」——这条依赖从此不再需要（注释已改写，并注明「仍然成立的那半条 = Session 不得被搬动/拷贝」）。
  ④ **实测**（sha `9ce5a655`）：八门 `rc=0`、`deadRefs=0`、读数与 10-31 基线逐项一致：answer 6 相红 1 · theme 7 红 1 · rebuild 3 红 0 · history 6 红 0 · sidebar 23 红 1 · table 6 红 0 · grow 12 红 0 · hit 4 红 0。
  ⑤ **编译坑（记一笔）**：成员函数的返回类型若含**嵌套类型**，类外定义必须写 `HarnessApp::RowRec* HarnessApp::FindBlockRow(...)`——只写 `RowRec*` 会报 C2143/C2761（一次过不去）。
  ⑥ **仍未闭合**：`TODO-06` 的现场形态是「控件活着而它内部 `std::string` 已坏」，本条绊线覆盖不到；且能稳定复现的二进制已被覆盖、未留档 ⇒ 下一步等复现靶，不再盲改。
  ⑦ 头部 v4.9 → **v5.0**。
- **v4.9（2026-11-01）★ 立 `TODO-07` 并落地第一步：`RowRec` 控件指针失效规则 + `SnapshotRows` 存活校验**
  ① 规则三条写进 `gui/src/App.h` 的 `RowRec` 头注释（不跨重建持有；跨重建用 id；快照点有绊线），并点明 `blockRows` 那条隐式不变量（deque + 成对清空）**动容器前必须先读**。
  ② `gui/src/ProbeSupport.cpp`：`SnapshotRows` 拼行前从 `m_content` 递归收集「当刻活动控件集」，逐字段校验 `label`/`headerBtn`；失效则写 `<dead:字段名>` + stderr 一行 `[snapshot] DEAD row ref`。**判据格式与字段数一字未动**（仍是 `\x1f` 分隔的七段）。
  ③ 实测（`gui/build`）：answer / theme / rebuild / history / sidebar 五门 `deadRefs=0`，读数与 10-31 基线逐项一致（answer 6 相红 1 · theme 7 红 1 · rebuild 3 红 0 · history 6 红 0 · sidebar 23 红 1）。
  ④ **覆盖缺口（必须记）**：绊线查的是「指针不在活树里」，查不到「控件活着而它内部 `std::string` 已坏」——`TODO-06` 的现场正是后者形态 ⇒ **本步是装绊线，不是修复**，`TODO-06` 仍开。
  ⑤ 下一步（待授权）：`blockRows` 的裸指针改**索引句柄**（把仓库里唯一「跨容器操作仍存活」的指针消掉）；若再拿到稳定复现的二进制，**先 `cp` 留档再动**。
  ⑥ 头部 v4.8 → **v4.9**。
- **v4.8（2026-11-01）★ ASAN 独立构建上线 + 六门**阴性**（阳性对照成立）：判据要改成结构性的**
  ① **构建开关**：`gui/CMakeLists.txt` 新增 `option(HARNESS_ASAN ... OFF)`，`-DHARNESS_ASAN=ON` 时给本目标加 `/fsanitize=address /Zi` + 链接 `/fsanitize=address /DEBUG`。**独立 build 目录**（`gui/build-asan`），不污染门用的 `gui/build`；读数（paints/frames/时延）与发布构建不可比，故**永不进门**。
  ② **两个必踩的坑（已写进 CMakeLists 注释）**：
     · **`LNK2038` × 132 条**：`ECDI.lib` 是非 ASAN 构建，其 STL 注解标记 = 0；ASAN TU = 1 ⇒ 链接器直接拒链。解法 = 本目标加 `_DISABLE_STL_ANNOTATION`（两侧都成 0）。代价 = 丢掉「容器未用容量的毒化」，**堆红区/隔离区不受影响**。
     · **`0xC0000135`（DLL 未找到）**：`clang_rt.asan_dynamic-x86_64.dll` 在 MSVC `bin\Hostx64\x64`，不在 exe 旁 ⇒ 运行前必须把它加进 `PATH`，否则进程起不来、且 stderr 为空（看起来像"跑过了"）。
  ③ **阳性对照（`0 条` 必须有牙）**：`ASAN_OPTIONS=verbosity=1` 时 stderr 打出 `AddressSanitizer: libc interceptors initialized` + shadow 映射 + `redzone=16` + `Init done` ⇒ 运行时**确实在跑**，"clean" 不是"没加载"。
  ④ **结果**：`--answer / --theme / --history / --rebuild / --sidebar / --table` **六门全部 `rc=0`、stderr 0 字节、无报告**。
  ⑤ **解读（只说能说的）**：ASAN 抓的是「已 free 仍在用 / 越界」；它**看不见**「对象已析构但内存没还（或已被复用/清零）」这类 —— 而这恰好是本条的现场特征（std::string 落到 `_Ptr==0 且 _Mysize==15` 的不可能态，`_Ptr` 是被清零而不是指向已释放块）。⇒ 本条更可能是 **use-after-destroy / 陈旧对象读**，不是典型 heap UAF；同时 ASAN 的布局本身也可能避开触发条件（A/B 已证布局一变就不崩）。
  ⑥ **因此判据要换**：不能再以「ASAN 报不报」为准（它在本条上必然阴性），而应立**结构性判据**——给 `RowRec::label / headerBtn` 这类**非拥有控件指针**定义明确的失效规则（重建即失效 / 换 id 查表），并在 `SnapshotRows` 前后加断言。这是修复阶段的入口，也是 `TODO-06` 真正闭合的条件。
  ⑦ 工具四件套已齐：`_dbg_lite.py`（即时调试器）、`_pe_sym.py`（.pdata 符号化）、`gui/build/harness_gui.map`（RVA→函数名）、`gui/build-asan`（ASAN）。
  ⑧ 头部 v4.7 → **v4.8**。
- **v4.7（2026-11-01）★ `TODO-06` A/B：崩溃**只在部分构建上复现**——「确定性」必须限定到二进制；调试构建会把缺陷吓跑**
  ① **A/B 实测**（同一份源码 `b6589bb`，只差构建开关；`--answer-probe` + demo 夹具 + `--instant`）：
     | 构建 | 开关 | exe sha256 | 大小 | 结果 |
     |---|---|---|---|---|
     | **A** | 无（01:12 原构建） | `cf4771dd` | 2074112 | **5/5 崩 `0xC0000005`** |
     | **B** | `/Zi` + `/DEBUG` | `a513de90` | 3266048 | **6/6 通过 `rc=0`** |
     | **C** | 仅 `/MAP` | `ffbc4796` | 2074112 | **4/4 通过 `rc=0`** |
     ⇒ 「确定性」只对**构建 A** 成立；v4.6 ② 的「七门全崩」同样是构建 A 的读数。**加调试信息就让缺陷消失**（B）⇒ 它与代码/堆布局强相关：v4.5 ⑤ 撤回的是**无证据的 padded 实验**，不是「布局敏感」这个方向本身——方向反被证实。
  ② **连 C 也不崩**（C 与 A 大小同为 2074112，仅 `UiCommon.cpp` 重编译导致 sha 不同）：未深究差异来源，但已足够证明**同源码 ≠ 同行为**⇒ 任何全绿/全红结论必须带**二进制指纹**（即 v4.6 ① 给 `regress.py` 加的那一行）。
  ③ **符号化结论仍有效**（取自构建 A 的致命现场）：`memcpy(dst=堆, src=NULL, n=15)`，位于 `std::string::_Reallocate_grow_by`（`ExceptionInformation=[读, 0]`）；第二处 `_Tidy_deallocate` 读 `[ptr-8]`（ptr=0 而 size>0xF）。两处都要求 std::string 处于「指针为空但长度非零」的**不可能态** ⇒ **悬垂 / 被复用的 std::string 对象**。
  ④ **调用链**（构建 A，`.map` 符号化 + 栈扫描）：`Win32PlatformWindow::HandleMessage` → `HarnessApp::OnReplayTick` → `RunAnswerProbe` → `SnapshotRows` → `line += …`（`ProbeSupport.cpp::SnapshotRows` 用 `r.label->GetText()` / `r.headerBtn->GetText()` / `r.answer` 拼行）。四门中 answer / theme / rebuild 都过 `SnapshotRows`；history 不过（走 `RebuildHistoryList`）却命中第二处 ⇒ **同族、不同路径**。
  ⑤ **归因倾向（未定论）**：焦点在 `m_sess->rows` 与 `RowRec::label / headerBtn` 的**生命周期**——是「重建换了控件但 rows 留着旧指针」，还是容器本身被重建后引用悬垂。`RunAnswerProbe` 自己的注释已写明「重建会清空 rows，拿着的引用会立刻悬垂」⇒ 这族风险**被识别过一处**，是否有第二处未清点。
  ⑥ **下一步**：①**`/fsanitize=address` 独立构建**——ASAN 的判据不依赖布局，能在「缺陷消失」的构建里照样报 UAF；②给 `SnapshotRows` 的指针加「重建后失效」断言；③`/MAPINFO:LINES` 在本机 link 19.51 上 `LNK1117` 链不过（已记进 `gui/CMakeLists.txt` 注释），源行只能走 PDB，而 PDB 又会掩盖缺陷 ⇒ 暂不追行号。归属（ECDI 侧 vs `gui/src` 侧）仍**未定**，但首次有了可指认的宿主函数。
  ⑦ 头部 v4.6 → **v4.7**。
- **v4.6（2026-11-01）★ `TODO-06` 从「race 假说」转为「确定性缺陷」：rc 已采集、崩点已定位**
  ① **台架补丁**（`tools/regress.py`，**只加读数、判据未动**）：每轮开头打 `BINARY: <path> sha256:<8> size mtime`；每个子进程落「门 / rc / json」三元组，`FAILED TO PRODUCE OUTPUT` 后附 `rc=0x…`；末尾新增 §4 汇总表。改动点 = 新增 `rc_str()` / `exe_fingerprint()` / `record()` 三个函数 + 五处 `subprocess.run(...)` 取回返回值（fixtures / 13 门 / EXTRA_PROBES / live A·B 四跑 / workspace）。
  ② **实测**（当前 HEAD 二进制 sha256 `cf4771dd`、01:12 构建，`--instant --theme light` + demo 夹具）：sidebar / answer / theme / history / rebuild / table / hit **七门全是 `rc=0xC0000005`**。json 落盘：answer / theme / rebuild = **MISSING**（0.22s–2.3s 早崩），sidebar / table / hit = **yes**（写盘之后崩）。
  ③ **不带任何探针的普通回放同样 `rc=0xC0000005`**，且 json 已落盘 ⇒ 缺陷与探针无关，是**退出路径必崩**；「门绿」仅是「json 已落盘」的产物 —— v4.5 ① 的「绿门也可能崩过」由推测变实测。
  ④ **崩点定位**（`.scratch/_dbg_lite.py` = ctypes Debug API 即时调试器，`.scratch/_pe_sym.py` = `.pdata` 函数边界 + RIP 相对引用的字符串，构成「无符号表也能认函数」的够用符号化）：
     · **F1** `harness_gui.exe+0x2410` → `call` 导入跳板 `0x179EC8` → IAT `0x185AD0` = **`VCRUNTIME140!memcpy`**，`ExceptionInformation=[读, 0x0]` ⇒ **memcpy 读 NULL**。所在函数 `0x22EE..0x243F` = **`std::string` 扩容**（`operator new` + 两处 memcpy + 尾写 `\0` + 分配头校验 `int 0x29`）。命中 answer / theme / rebuild。
     · **F2** `harness_gui.exe+0xA533`：`mov -0x8(%rax),%r8` 读 `0xFFFFFFFFFFFFFFF8` ⇒ buffer 指针为 **0** 而 `size>0xF`，即 **std::string 落在「堆缓冲但指针为空」的不可能态**（二次析构，或对象内存已被复用）。命中 history（2.3s）与**所有正常退出**。
     ⇒ F1/F2 同族：**悬垂 / 二次析构的 std::string**。方向与「overlay / 定时器悬垂指针族」一致，但**尚未落到具体对象**。
  ⑤ **撤回 v4.5 ④ 证据 B**：`--answer-probe` **5/5 确定性复现（0.22s）** ⇒ 「22:57 绿 / 00:42 红 = 同源间歇」不成立——「零提交」≠「零改动」，那 105 分钟里工作树一直在改（表格视觉迭代）。padded-HEAD 假说随之**彻底作废**（不再需要堆布局解释）。但「**HEAD 自带、非 f1cfa07 引入**」仍成立：在当前 HEAD 二进制的稳定复现即是证明。
  ⑥ **工具侧教训**：首版调试器对「首轮 AV」回 `DBG_CONTINUE` ⇒ 程序 SEH 重试同一条指令 ⇒ 同一地址重发 400 次；正确是 `DBG_EXCEPTION_NOT_HANDLED` 交还程序，只有断点（`0x80000003`）才 `DBG_CONTINUE`。另：`ExceptionInformation[0]` 是读写标志、`[1]` 才是访问地址，读 NULL 时 `[1]==0`。
  ⑦ **下一步（判归属的前置）**：F1/F2 的调用者要落到源行，需符号。两条路：(a) 给 `gui` 目标加 `/MAP`（或 `/Zi`+`/DEBUG` 产 PDB）——一行 link option，一次配置永久可查；(b) 手写 x64 unwind 走 `.pdata` 帧链（成本高、且只到函数边界）。倾向 (a)。**归属未定前不新开 Phase**：调用链若落在 `ECDI::Widget` / `Window` 析构或 overlay / timer 摘链 ⇒ 按 ECDI Phase 处理；若在 `gui/src` 业务侧 ⇒ 记 `TODO-07` 小修。
  ⑧ 头部 v4.5 → **v4.6**。
- **v4.5（2026-11-01）★ `TODO-06` 证据订正：撤回「堆布局敏感实锤」，换入两条可复核证据**（纯文档订正，零代码改动）：
  ① **判据含义订正（本次最要紧的一条）**：`tools/regress.py:209-210` 的判据是 `out.exists()`，而 `200-203` 的 `subprocess.run(...)` 返回值**未被赋值** ⇒ 子进程 rc 从采集环节就被丢弃，全程无人读。故 `FAILED TO PRODUCE OUTPUT` 的语义 = **探针没跑到收尾 `AppendSummary`、json 未落盘就崩了**——**不是**「跑完再崩」。反过来说，**绿的门也可能崩过**（json 已落盘 ⇒ 判据看不见），只是这一点同样无 rc 可证，不得当结论用。v4.4 ④ 把「json 落盘后崩」写成 `TODO-06` 的「签名」属越界外推，撤回。
  ② **归档红相订正（v4.4 ④ 所引四份文件内容与结论不符）**：`_regress_precommit.log`（10-31 22:57）四门**确**全绿（answer 5/6 · theme 6/7 · history 6/6 · rebuild 3/3），但**同轮 `--delprov-probe` 亦 `FAILED TO PRODUCE OUTPUT`**，落款 `RESULT: 2 FAILURE(S)`（另一红为 ws）；`_regress_tablefix2.log`（10-31 22:38）四门里 **theme 是 `3/7 UNEXPECTED-RED`**，落款 `RESULT: 2 FAILURE(S)`（theme + ws）⇒ 「红的只有 ws」不成立；`_tp.err`（214B）是临时 `[tp] case N / RebuildUi enter` **fprintf 插桩轨迹**，不是崩溃证据；`_wp2.err` **0 字节**，等于没引用。⇒ 「干净 HEAD 4/4 绿」「padded-HEAD 8/8 复现」两份对照**均无对应日志**；「8/8」的 8 在存档里也无出处（`_regress_t1t2.log` 四门各红一次 = 4）。padded 补丁未留痕：`gui/src/App.h:448` `struct RowRec` 现无 pad/reserve 字段，`git status` 干净、`git stash list` 空。
  ③ **换入的主证 A（post-fix HEAD 上五门仍空产物）**：11-01 01:17–01:19 那轮（01:12 构建）**没留日志**，但产物齐全可判：`.scratch/_reg--*.json` 共 23 个探针齐备，**独缺 `answer / theme / history / rebuild / workspace` 五个**。`regress.py` 无 argv 过滤（全量按序跑、每门先 `unlink` 再跑、`main()` 前无选择开关）⇒ 缺 = 该门崩在落盘前；且其后的 `session`(01:17) / `hit`(01:17) 照常产出 ⇒ 是**逐门判决后的失败**，不是中途被中断。⇒ 「**HEAD 自带、非 f1cfa07 引入**」此条由本条独立支撑（不依赖无存档的干净 HEAD 对照）。
  ④ **换入的主证 B（同源红相漂移 = race 的直接签名）**：`_regress_precommit.log`(22:57 四门绿) 与 `_regress_t1t2.log`(00:42 四门 + §2c `legacy` + ws，`RESULT: 6 FAILURE(S)`) 之间**无任何代码提交**（`412c4b7` / `b6589bb` 均 01:26 才落）。三份日志的红相集合 `{delprov, ws}` → `{theme, ws}` → `{answer, theme, history, rebuild, legacy, ws}` 逐轮不同 ⇒ 漂移本身就是证据，且**不依赖**任何未留痕实验。
  ⑤ **降级与保留**：`padded-HEAD ⇒ 堆布局敏感` 由「实锤」降为**假说**（合理但不可复现）；「疑与 overlay/定时器悬垂指针族同源」同为**假说**——`--workspace-probe`（live core）与 §2c `legacy` 同样过 `RebuildUi`，而 `--delprov-probe` 22:57 红、00:42 绿 ⇒ 族归属未定，需一次定向插桩（`RebuildUi` 前后各一条 fprintf + 全门 rc）才能收窄。v4.3 ② 的「b0dd54e 干净构建同样复现」也无日志存档，仅记录在案（结论由 ③ 承接）。★ **栈仍可取**：`.scratch/crash.dmp`（10-31 17:44，头 `4D 44 4D 50` = 有效 minidump）尚未解析；`_cdb_answer.log` 只证明 cdb 未装（`'cdb' 不是内部或外部命令`，vcvars 已初始化）⇒ 是「调试器没走通」，不是「无栈可解」。
  ⑥ **机制欠账（「红门不可信」的根因）**：`regress.py` 每门须落盘三元组「门 / rc / json 是否存在」，并在每轮开头记运行期 `exe` 指纹（sha256 + mtime）——否则「绿」无法区分「真绿」与「崩在判决之后」，「A 轮绿 B 轮红」也无法排除换二进制。此三项补齐前，红门纪律只能靠人读产物 + 对 `_reg--*.json` 做存在性核对。
  ⑦ 头部 v4.4 → **v4.5**。
- **v4.4（2026-11-01）★ 富内容二期第二轮：表格 T1+T2 落地（折行 / 自适应行高 / 全高列线 / 细横条）+ `TODO-06` 证据补强**：
  ① **本仓动作**：`RowRec::TableRec` 携带可重跑的表格内部元数据，`LayoutTableInterior` 于 Relayout 期按版心宽定稿——列宽注水分配（短列钉自然宽、真长列分余宽、保底 48 后仍超版心才交横滚）、wrap 折行高（挂窗态 `AutoSize` = 宽钉死高求解）、行高自适应、全高 1px 列线；表格 ScrollView 双条 `ScrollBarStyleOverride{.thickness=4}`（默认 12 的 1/3，用户裁决）；
  ② **上游判据**：~~`ScrollView::BarThickness()`（上游 ScrollView.cpp:193-197）读**竖条**厚度却供双轴占位（`BarFootprint`）用 ⇒ 只薄横条会留 8px 幻影占位、反逼出一条竖条，故双条同薄~~ **★ 2026-11-01 订正（见 v5.2 ④）：上游缺陷已由 ECDI `#84` / Phase 43（`daed47c`）修复——`BarThickness()` 删除，改为 `VerticalBarThickness()` / `HorizontalBarThickness()` 两轴各取各的，同厚度路径位相同。本仓「双条同薄」结论不变、理由改为【设计裁决：全仓条宽统一】；三处源码注释（`AppInternal.h` / `UiCommon.cpp` / `AppTranscript.cpp`）已同步。**；`AppHud` 的 `barExtraH` 改读活厚度（`GetThickness()+2`），不再写死 14；
  ③ **人验**：双主题截图样张（`.scratch/_table-wrap-visual.jsonl` / `_table-wide-visual.jsonl`）——常规表贴合版心**不出横条**；16 列宽表横条 4px 细、无竖条、列线跨折行连续、单行对齐未回归；`--table-probe` 6/6 绿（★ v4.5 订正：该门判据只看 json 是否落盘、rc 未采集 ⇒ **不能**据此断言「退出时段错误」；原写「json 落盘后崩为 `TODO-06` 签名」已撤回）；
  ④ **`TODO-06` 证据补强**：~~T1+T2 构建上 answer/theme/history/rebuild 8/8 段错误 vs 干净 HEAD 4/4 绿 ⇒ 于干净 HEAD 的 `RowRec` 加 24 字节填充（padded-HEAD 对照）同样 8/8 复现 ⇒ 堆布局敏感的本条 race 实锤、非新改动引入~~ ⇒ **★ 2026-11-01 v4.5 全条撤回**：所引四份取证件内容与本条结论不符（详见 v4.5 ②），padded 补丁亦无留痕；改由 v4.5 ③④ 两条可复核证据承担「HEAD 自带、非 f1cfa07 引入」与「race 间歇」两个判断。红门纪律不变；
  ⑤ 头部 v4.3 → **v4.4**。
- **v4.3（2026-10-31）★ 立案 `TODO-06`：回归门 race 段错误（红门不可信项）**：
  ① **现象**：`--workspace-probe`（活 core）确定复现段错误/零输出；`--theme-probe`、`--delprov-probe` 间歇段错误且崩溃点逐轮漂移（加一条 fprintf 改时序即全过 ⇒ 海森 bug）；
  ② **归因核对**：干净构建（b0dd54e）同样复现 ⇒ **HEAD 自带**，与同日表格修复 f1cfa07 无关；疑与 overlay/定时器悬垂指针族同源（`RebuildUi` ⓪/⓪b 自保注释、审计整改批 A §11.2 的 `exemptWidget` 第 5 例）；
  ③ **纪律**：本条未闭合前，回归红相先对照本条证据再归因新改动；同日表格「有框无字」缺陷另由 f1cfa07 闭合（建表期 Label 无测量器 ⇒ 改 `Window::GetTextMeasurer()` 实测设尺寸）。
  头部 v4.2 → **v4.3**。
- **v4.2（2026-10-31）★ `REQ-15` ✅ 闭合：输入框 Word Wrap 消费落地 + `--grow-probe` 12/12 进门**：
  ① **本仓动作**：`ComposerBox` 构造函数与 `PromptBar` 显式开启 `SetWordWrap(true)`，正式消费 ECDI Phase 38 的视觉行缓存机制；
  ② **探针扩充**：`--grow-probe` 新增 `wrap-grow`（单行超宽软折行长高且 `GetScrollOffsetX()==0`）/ `wrap-shrink`（删减恢复单行高）/ `wrap-resize`（窗口宽度变化驱动折行自适应），实测 12/12 全绿，既有 9 相（含 baseline `inputH=29 / barH=81 / hintY=43`）读数逐位未动；
  ③ **回归通过**：六夹具结构基线保持 100% 命中，闭合本条。
  头部 v4.1 → **v4.2**。
- **v4.1（2026-10-30）★ 对账两笔：`REQ-11` 闭合（ECDI `#71`，2026-10-25）· `REQ-15` ECDI 侧已兑现（Phase 38，2026-10-29）**：
  ① **触发**：ECDI 侧 Phase 41 收口后按本账逐条对码（`#79` 迁区同一轮），发现两条状态落后于 ECDI 实际进度。
  ② **`REQ-11` → ✅ 闭合**：ECDI `roadmap-deferred` **`#71` 已于 2026-10-25 实施并关闭**——GDI 链 `DrawText` / `DrawFocusRect` 补 alpha 消费，与 GL 链对齐 ⇒ **两链全原语消费**。验收 = 探针双读数（`.workbuddy/spike/p71/`）+ 测试 +3（`Renderer.TextAlphaConsumed` / `FocusRectAlphaConsumed` / `LineAlphaAAOffConsumed`，判据 = 墨迹 gMean 差），判别力已证（临时回退三处行为核心 ⇒ 恰 4 红 + 1 绿）。本条**零消费者 ⇒ 无需本仓验收读数**。★ 留痕纪律：原「推后」判据（触发条件 = 首个半透明文本/焦点框消费者）**始终未触发**，是 ECDI 侧**主动修**的 ⇒ 不得记成「被消费者逼出来的」；且 ECDI 侧已声明**半透明文本仍为灰度近似**（不是逐字形 alpha 合成），本条闭合不等于「半透明文本观感已达标」。
  ③ **`REQ-15` → 仍 🔄，但语义变为「待本仓消费」**：ECDI **Phase 38（2026-10-29，五批 531/531 × 四构建链）** 兑现编辑器内软折行，`TextBox.h:79` 行数口径改为「视觉行数 = `m_lines.size()`」。本仓收敛动作（**未做**）：换前缀 → `ComposerBox::SetWordWrap(true)` → `--grow-probe` 三相加 → 基线重采。★ 闭合判据写进条内「状态」行，四项缺一不算闭合。
  ④ ★★ **换前缀的已知风险（必须显式记账，不得静默重基线）**：ECDI Phase 39–41 依次改了 ① UTF-8 码点扫描改单趟推进 ② 折行支改偏移表 ③ **帧级布局 / 重绘失效结算管线**——③ 让几何 setter 恒发 `Invalidate`、`AddChild` / `RemoveChild` 恒发重绘、**帧首多一次布局结算** ⇒ 本仓 `paints` / `frameCount` 基线**可能漂移**。按 `P4-gui-usability-plan.md` §0.3 的纪律：漂移必须写进 `--out` 摘要并说明成因，不许悄悄重基线。
  ⑤ 头部 v4.0 → **v4.1**。
- **v4.0（2026-10-28）★ 新立 `REQ-15`：`TextBox` 不支持按宽度自动软折行（Phase 29 O2 编辑器内 wrap）**：
  ① **触发**：核对「富内容三项」§A（多行输入）时，用户确认诉求为「连续打字到右边界自动折行并撑高输入框」，而非仅 `Shift+Enter` 硬换行。
  ② **归属 = ECDI 侧（Phase 29 O2 挂账项）**：`TextBox.h:77-78` 明写 `行数口径 = \n 数 + 1，与 RecalculateLines 同源——编辑器内 wrap（O2）兑现时只需改这一处真相源`；`RecalculateLines`（`:261`）只扫 `\n`，超宽走 `m_scrollOffsetX`（`:352`）水平跟手滚动。
  ③ **不绕行理由**：在 `TextBox` 上直接调基类 `SetWordWrap(true)` 会导致测量与绘制/光标分裂；在 harness 应用层往文本塞 `\n` 会污染发送载荷、破坏缩放重排与 Undo/IME 区间。故按纪律登记 `REQ-15`，待 ECDI 侧将 `m_lineStarts` 升级为视觉行缓存后，本仓 `ComposerBox` 仅需加一行 `SetWordWrap(true)` 并补 `--grow-probe` 软折行相。
  头部 v3.9 → **v4.0**。
- **v3.9（2026-10-27）★ `REQ-14` 闭合：ECDI Phase 37 `verticalAlign` 当日落地 + 本仓 7 框收敛**：
  ① ECDI 三批 `074b8aa`/`ac80645`/`6a3807b`（五链 513/513，deferred #76 关闭）采纳登记稿 ⓐ——
     `VerticalAlignment{Top=0 缺省逐位不变, Center=1 内容不超屏才生效}` + `TextBoxStyle::verticalAlign`
     + `GetTextTopInset()`（protected；Top 快路径不读行数 = A1 结构性保证）+ X/Y 单一来源拆分
     （OnPaint/光标/点击定位 △7/△8/△9）+ clip 顶不随居中外缩（C37-3）。
  ② 本仓 7 个单行框 `Center` 化（composer / 设置×5 / 模型搜索）；composer 巧合居中钉成契约居中，
     `--grow-probe` baseline 逐位不动（29/81/43）= 几何零扰动。
  ③ 新探针 `--valign-probe` 7/7 进门：判据走公开 `GetCaretClientGeometry()`（行箱 Y 原点同源），
     单行居中 padding 代数消去 ⇒ 判据 `(H−lineH)/2` 不依赖 padding 访问器；翻转臂 Top=0 → Center=5
     给出剂量对照，恢复臂 == 首测；composer 恰满盒两臂相等（6==6）即「Top 时也居中」的交付事实。
     实测设置框行高 16（≠ 登记估算 17）——「行高必须测量」的自我警告兑现。
  ④ 换库 MSVC Release lib `a8822fbc…`（11,696,664 B）+ 同日构建器迁 Ninja（同 cl.exe，ABI 不变）。
  ⑤ 顺手发现（同日修）：模型浮窗搜索框无 `.background` ⇒ 深色主题白底（P4 H 漏网）；
     连带「所有输入框文字色吃 ECDI 缺省黑」一并闭合，见上方收敛段。
  头部 v3.8 → **v3.9**。
- **v3.8（2026-10-26）★ 新立 `REQ-14`：单行 `TextBox` 无垂直对齐（`padding` 三耦合 + 圆角无感知）**：
  ① **触发**：用户报「输入框文字需上下居中」+「最左侧文字撞圆角」⇒ 查证两者在框架侧**都没有入口**。
  ② **归属 = ECDI 侧（能力缺项）**，判据是「能力存不存在」：基类公共头 + ECDI 源码仓全树 grep
     `verticalAlign|textAlign` 零命中（只有 `ButtonStyle::textAlignment` 与 GDI `SetTextAlign`）；
     `TextBox::OnPaint` 无单行分支，`textY = fy + padding` 恒顶部对齐（`TextBox.cpp:1338`）。
  ③ ★ **本条刻意写成「请先裁决契约」而不是直接判缺陷**：`TextBox.h:79` 已文档化
     「height = lineH + padding×2 → 自然垂直居中」⇒ 框架可以主张「调用方把尺寸做对就行」。
     两种结论都已写进条目（可接受 ⇒ 结案为已知现状，同 `REQ-06` ③；不可接受 ⇒ 走 ⓐ`verticalAlign`
     / ⓑ`paddingX`+`paddingY`）。★ 这一条是对 `REQ-01/03/04/07` 四次误判的预防——**不要替对方
     决定它的设计意图**。
  ④ **实测四处**（聊天框 29/6/圆角9 · 设置三框 30 或 26/padding 0/圆角 7 · 搜索框 26/0/0），
     偏移量用 `H − 2p − lineH` 判，**不用墨迹包围盒**（本轮自我订正：17px 线箱的 ascent/descent
     本就不对称，聊天框墨迹 10/7 但线箱偏移 = 0）。
  ⑤ ★ **不新增代码改动**：本条只登记；本仓的绕行（`padding = radius+2` 且 `H = lineH + 2×padding`）
     与是否实施，等本条裁决后再定——绕行有可见代价（聊天框 +10px 吃掉转录区），不宜先改后议。
  ⑥ 头部 v3.7 → **v3.8**

- **v3.7（2026-10-26）★ `REQ-11` 消费者面订正 + 探针归门（评估结论的可复现性）**：
  ① **消费者面**：P4 Phase F 的设置面板遮罩是**首个半透明绘制消费者**（`DrawRect` + `BlendAlphaSolid`，
     属「覆盖面」表里已支持的路径，双后端实证、失败退不透明）⇒ 原文「当前零消费者」不再准确，已就地订正。
     ★ **但缺陷面（`DrawText` / `DrawFocusRect` 恒丢 alpha）仍是零消费者**，触发条件
     （首个半透明文本/焦点框消费者出现）**未变**，`REQ-11` 状态仍是 📋 推后。
     —— 这里刻意不把「有消费者了」写成「该修了」：新消费者走的正是**已经对**的那条路。
  ② **不新增 REQ**：P4（A–H）与 App.cpp 拆分（Stage 1–4）都是 harness 侧改造，`protocol/` / `core/` /
     `evals/` 零改动，未发现新的 ECDI 侧能力缺口。
  ③ **探针覆盖 13/25 → 25/25**（`tools/regress.py`），并把「不绿」拆成三类：
     `designed-red`（结构上不可达）/ `skipped-*`（探针自述不适用）/ `UNEXPECTED`（真回归），
     `unavailable` 不算跳过。记账本的理由：本仓对 ECDI 的评估结论**全部由探针读数支撑**，
     探针没进门 = 结论不可复现；而 11 个探针此前正是「有实现、有判据、无门」。
  ④ 头部 v3.6 → **v3.7**

- **v3.6（2026-10-24）★ `REQ-10` 闭合翻转：ECDI 侧 Phase 36 全链收口完成**：
  ① **ECDI 侧收口提交 = `b1a32b5`**（deferred **v2.40** #70 关闭 + README Phase 36 行收口语 + docs/README 三行 + 详设 **v1.3** §9 收口记录）。
  ② **全链数字 = 五链 497/497**（MinGW / Clang / ClangCL / MSVC Debug / MSVC Release；MSVC 双配置与 ClangCL 本日重建补齐批三 T36-7）。
  ③ 本条「状态」行由 🔄 开放翻转为 ✅ 闭合（读数即上文的库外验收 14/14 · 10/10 · 9/9）；**待做区清空动作完成**——ECDI 缺陷队列现存仅 #71（推后）。
  ④ 头部 v3.5 → **v3.6**


- **v3.5（2026-10-24）假 core 补 `--burst` ⇒ M3 转 RED（`REQ-03` 的判据链补齐）**：
  ① **动机**：只测边界不给造边界的手段 ⇒ `C3`（单回调 ≤256 行）在 `--speed=8` 下**恒真而未被触碰**（实测 `maxCB` 只有 6–8），M3 变异**不可判别**（v3.3 已如实记为 INCONCLUSIVE，未调阈值）。
  ② **旋钮**：`--burst` = 关掉 `sleep()` + 一轮输出攒进 256KB `bufio`、轮末一次写出（`handlePrompt` 用 `defer flush` 覆盖**所有** return 路径，含 cancel 的两条早退）；控制面消息（`hello`/`state`/`workspace`/`error`/`ask`/`bye`）在 `emit()` 里**立即刷**。
  ③ ★ **踩坑（当场修好）**：首版只留 hello/bye/ask 三个显式刷点，**漏了 `workspace` ack** ⇒ ack 烂在缓冲里，GUI 等初始工作区路径等到超时，整场只收到 **1 条**消息（`blocks=0`）。教训：**"我列了三个必刷点"不等于"我列全了"**——控制面消息应当在 `emit()` 里按类型统一处理，而不是靠调用点记得刷。
  ④ **读数**：burst 下 `maxCB` **256**（有预算；3 次回调 = 256 + 175 + 1）/ **430**（无预算）⇒ M3 **RED**；内容不变（`6/415/432`，C4b），`seqGaps=0`。
  ⑤ **回归门加 2 条**：`C3b` burst 下 `maxCB` 恰好 256（证明预算真的被顶到，不是空真）、`C4b` burst 不改变内容；`tools/regress.py` §2b 由 3 跑变 **4 跑**。头部 v3.4 → **v3.5**（v3.4 已被同日勘误条目占用）。
- **v3.4（2026-10-24）★ 勘误：v3.2 里的一处旧快照引用 + ECDI 清理轮通报**：
  ① **勘误**：v3.2 ③ 与 REQ-10「状态」行初版写「Phase 32 #58 与审计批余量在前 / 不插队 Phase 32 #58」——系引用
  ECDI **2026-10-12 的旧快照**（审计批一时的状态）；**#58 实已于 2026-10-14 关闭、Phase 32 同日全链收口**
  （ECDI `roadmap-deferred.md` §8 v2.30 / #55 行）。状态行已就地订正；v3.2 ③ 按条 61② 保留不改写。
  ② **通报**：ECDI 同日清理轮 = `roadmap-deferred.md` **v2.33**（#46 行兑现 Phase 34 收口 · doc_lint L13 规模锚点
  465→**490** / docs 183→**188** 续订 + phase34 详设 L04 断表修复 · **L01 缺 BOM 350 处裁决 = 维持现状**[/utf-8
  已全覆盖编译目标，见 ECDI v2.33-④]）。`REQ-10` 立项与否仍待需求决断，ECDI 队列无其它开放缺陷。头部 v3.3 → **v3.4**

- **v3.3（2026-10-24）★ `REQ-03` 闭合：`PostToUi` 首个**框架外**真实消费者（推 + 拉兜底）**：
  ① **设计七条**（投递信号而非数据 · 闭包只捕获 `this` 并执行时枚举会话 · `atomic` 合并唤醒 · 单回调 256 行/4ms 预算且余量 re-arm · tick 保留但预算 0 作兜底 · 冻结语义 + 复位**所有** live 源 · `--legacy-poll` 作单一自变量与回滚）——每条的理由写在 §2 闭合记录里，不在这里重复。
  ② **读数**（同二进制 / 同 core / 同参数 / 两侧都开入队戳）：legacy `avgBlockedUs` **11599** → async **0**；wake 占比 **99.8%**（431/432）；`--speed=1` 下 p50 **49µs vs 14774µs**（30–300×）；`--speed=8` 两侧计数逐项相同 `6/415/432`，且 15s 与 30s 窗口读数一致 ⇒ **流确实跑完**（否则比计数是假的）。
  ③ ★★ **意外发现**：`NextWithin(…, 8)` 的 8ms **不是实测下界**——`cv.wait_for` 受系统时钟粒度约束，实测 **avg 8.8–11.8ms / max 23.9ms**。此前「每 tick 8ms 预算」只是注释里的**设计值**，没有任何读数能发现这件事。
  ④ **判据 C1–C5 进回归门**（`tools/regress.py` §2b，3 跑 live）。负对照：**M1**（`--legacy-poll`）⇒ C1b RED；**M2**（去掉 `RearmWake` 复位）⇒ C2 RED（`wake=1/432`）；**M3**（去预算）⇒ **INCONCLUSIVE**——假 core 一共只 432 行、队列从未积压到 >256，如实记为「本配置下不可判别」，**没有为了让判据变绿去调阈值**。
  ⑤ `--async-probe` 默认关 ⇒ 摘要逐字节不变；新开关 `--legacy-poll` 逐字复原旧路径（A/B 与回滚共用）。头部 v3.2 → **v3.3**。
- **v3.2（2026-10-24）★ `REQ-10` / `REQ-11` ECDI 侧核验收敛：对码全部属实 + 两处订正回写 + ECDI 登记 #70/#71**：
  ① **核验结论**：ECDI 侧逐条对码——`REQ-10` 三条子缺陷**全部属实**（`Font.h:17-18` 仅 `size`+`family`；生产侧 `FW_NORMAL` 恰两处 = `GDIBackend.cpp:1384`/`GDITextMeasurer.cpp:63`，含测试全源码 4 处命中＝与本仓读数一致；`FontEngine.cpp:236` 恒 face 0；`FamilyToFileName` 三档匹配强度[精确 / `<family>␣` / `<family>␣&␣`]对复合键第二项逐档推演全不中）；`REQ-11` **逐行号属实**（GL 七个 `Shade` 调用点 :215/:246/:324/:329/:379/:412/:642 + `glBlendFunc` :201/:504；GDI `DrawRect`:649 / `DrawRoundedRectBlendedAA`:1492 / `DrawLine` 分流 :843-850[判据推演确认仅「AA 关 + a<1」边角漏] / `DrawFocusRect`:1222 / `ToColorRef` RGB-only :671；测试面仅 DrawRect 系两条 =「从未覆盖」判断成立；ECDI 库内同样零 (0,1) 消费者——`DefaultTheme` 仅 a=0「不画」语义与 a=255 两类）。
  ② ★★ **两处订正回写本账本**：(a) `REQ-10` 修法候选 ⓒ **与 ②（face index）耦合，非独立闭合**（已改「修法候选」行）——ⓒ 单独落地解析到对的文件但 `FT_New_Face` 恒取 face 0 ⇒ 拿到 `Microsoft YaHei` 非 UI 版，仍是「静默给错 face」的 #69 同类陷阱；真正闭合形态 = **family → (文件, faceIndex) 二元组**，②③ 一体设计；(b) `REQ-11` 的 `SetTextColor` **实为单点 `GDIBackend.cpp:746`**（`:682`/`:714` 系 ECDI Phase 34 量化统一动该文件后的行漂移，实质判读不变）。
  ③ ★ **ECDI 侧落点**：`docs/roadmap-deferred.md` **v2.32** 新立 **#70**（登记不立项——缺陷队列顺位 ①，Phase 立项时机待拍板，不插队 Phase 32 #58；初设必答三问已钉：600/650「最近 face」两链语义 · `weight` 与族名内嵌样式优先级 · 静态 face 选择与 variable font variation 轴边界）+ **#71**（推后，触发条件已写明）+ **`Color.h` alpha 注释改写**（声明「消费方分后端」双链发散并补列 `DrawArc`/覆盖度路径——原注释只列 DrawRect/DrawRoundedRect，两个方向都不完整；**不写「祝福缺陷」的 alpha 测试**，与 `REQ-06`/O10「记为已知现状」处置同理）。⇒ §2 状态行与 §5.1 载体表已同步。
  ④ ★ **诚实边界**：ECDI 侧**未独立复跑**墨迹比读数（1.29–1.79× / 5 族为本仓探针侧读数，ECDI 仅对码）——已如实记入 #70「未测边界」。本仓**不改代码**：`REQ-10` 过渡方案维持（拉丁用带字重族名 / 中文用字号+颜色压深代偿），`REQ-11` 维持不规避。
  ⑤ ECDI 侧提交 = `e913b8d`（deferred v2.32 + `Color.h` 注释；doc_lint 对码后零新增缺陷）。头部 v3.1 → **v3.2**

- **v3.2（2026-10-24）★ Phase 36 批三交付接收 + `REQ-10` harness 侧库外验收通过**：批三 lib（ECDI `e163357`，SHA256 `5b0b1c51…d6ca`，11,658,056 bytes）按 ECDI 侧 DELIVERY.md 固定指纹逐项核对入 `third_party/ecdi`，harness_gui Release 重建零警告；新探针 `.scratch/req11/p36h_matrix.cpp`（链接新库）产出：GL 生产 `FaceIdFor` 身份矩阵 14/14、GDI 映射矩阵按设计、A2 五族墨迹比双链 10/10 方向 OK（GDI 读数与 REQ-10 基线逐位相同 = A6 环境恒定锚；GL 经生产 `FontEngine::Glyph` 1.35–1.72×）、A3 真字面格串宽 9/9 delta=0（SimSun@700 按 C36-4 豁免）。六夹具回归逐位 + 13 探针绿（仅设计红）+ validate.py exit 0。仪器教训：A3 首版全格共用一条 CJK+拉丁混排串，无 CJK 覆盖的族（Consolas/Segoe UI）读 0.70–0.75×——量到的是 GDI 字体链接 vs GL 无回退（N6），不是 face 保真；已按 face 覆盖改逐格选串。`REQ-10` 状态翻转待 ECDI 侧 Phase 36 全链收口。
- **v3.1（2026-10-23）★ `REQ-13` 闭合：ECDI 落地 `Window::SetMinimumSize` + 本仓删绕行收敛**：
  ① **采纳 ⓐ**（公共 API **+1**），裁决理由是**换算归属**：客户区下限 → 整窗尺寸要同时知道 DPI、chrome 形态、
  单位口径（`WM_GETMINMAXINFO` 收**物理像素**、框架公开几何 API 全用 **DIP**），这三件事框架全知道、
  调用方一个都拿不到（`PlatformWindow.h:20` 的「不接触 HWND」边界）。⇒ 让调用方换算 = 在框架外复制框架内部规则。
  ② ★★ **一条只在实测里才有的道理：一个钩子不够。** `ptMinTrackSize` **只在带 sizing frame 的窗口上被系统采用**
  （`probe_minmaxinfo` 覆盖矩阵实测：Borderless 无框窗口单靠 `WM_GETMINMAXINFO` **拦不住** `SetWindowPos`），
  而本仓的产品形态正是 Borderless ⇒ 落地是**两路**：`WM_GETMINMAXINFO`（鼠标拖边框唯一能在松手之前拒绝的钩子 ⇒
  硬停不回弹）+ `WM_WINDOWPOSCHANGING`（编程尺寸请求）。⇒ 若照手册抄「填 `ptMinTrackSize` 即可」，
  会交付一个**在自己的产品形态下不起作用**的能力——本条的立项证据是「框架从没拦过」，闭合证据却是「拦两次」。
  ③ ★ **第 ⑥ 相从同义反复改成两条相反断言**：改前那一相断言的是自己写的 `max(880, x) >= 880`，
  **读三行 C++ 就能证明，什么也没证明**。现在 `min-size` 请求 640×360 ⇒ 断言根控件**逐位等于** 880×460
  （不是 `>=`——落在 800/1000 说明拦住它的不是下限本身），紧接着 `min-size-off` 把下限清零 ⇒ 同一请求
  **必须真的**落成 640×360。★ 两条相反方向的断言同时成立，才排除「下限不存在」与「下限无处不在」两种退化。
  ④ ★ **仪器自伤（同族第四次）**：`probe_minsize_negctl.py` 的两条旧 needle 指向**已删除的** `ClampToMinimum` /
  `m_minSizeEnforced` ⇒ 会永远输出 `SKIP: needle hits=0`，而**那看起来像一次干净的通过**。已重写到新接缝：
  M1 删建窗处的产品登记 ⇒ `min-size` RED · M2 删探针里的清零 ⇒ `min-size-off` RED；两条 PASS，跑完按字节还原 + sha256 复核。
  ⑤ **ECDI 侧测试 487 → 490**（`WindowChromeTests.cpp` 三个真窗口用例：夹住编程请求 / `Normal` 用客户区口径 /
  逐轴独立含负值放行）。★ 新增的是 `PlatformWindow` 的**纯虚**函数 ⇒ `AnimationTests`/`ProgressBarTests`/
  `RingProgressBarTests` 三处替身各补一条空实现——纯虚扩张的连带成本，登记在此以免日后误判为无关改动。
  ⑥ ★ **如实记一条没有自动化覆盖的**：鼠标拖边框的「硬停不回弹」由 `WM_GETMINMAXINFO` 语义 + 覆盖矩阵保证，
  但探针只能走编程路径（真 `SetBounds` → 真 `WM_SIZE`），**人手拖拽未做自动化验证**。
  ★ 并实测确认这条路**在本环境补不了**：`probe_drag2` 的 `SetCursorPos`+`mouse_event` 合成输入一个事件都进不了窗口过程
  （`events=[]`、`foreground=0`），而程序化 `PostMessage(SC_SIZE)` 能进 ⇒ 是**注入通道**不通、不是忘了测（详见该条闭合记录）。
  ⑦ **回归**：`tools/regress.py` 全量 **PASS**（6 夹具结构读数逐项等值 + 13 探针全绿 + `protocol/validate.py` exit=0），
  `--resize-probe` 7/7；**其余各相判据一字未改**——这是收口的验收方式：行为不变，实现变干净。
  ⑧ **本文件头部 `状态` 行长期落后**：写作 **v2.8（2026-10-18）**，而 §6 早已记下 v2.9 / v3.0 ⇒ 本次一并订正为 v3.1。
     原 `status.txt` 第 57 行末句「`REQ-13` remains open on the ECDI side as filed」在写下时是**真的**，现已闭合，订正见 `status.txt` 第 58 行。

- **v3.0（2026-10-22）★ UI 打磨七项落地 + 新立 `REQ-13` + 撤回「深色保留 SimSun 作回归基线」这一决定**：
  ① **七项**（用户按清单逐项点名，全部已落地并各带探针与负对照）：**空回调**（中栏头 `↗`/`···` 从
  `(void)tip; return;` 变成真动作，`HeadAct` 枚举取代单 `bool toggleInspector`）·**字体统一**（见 ③）·
  **窗口可拉伸**（此前全仓零处 `WindowResized` 处理，行宽是 `Relayout()` 量出来的，`Resize` 之后不重量）·
  **最小尺寸**（新立 `REQ-13`）·**Enter 发送 / Shift+Enter 换行**（改前两个键**都是死的**，
  `SetSingleLine(true)` 让 `TextBox` 的 `InsertText("\n")` 永不执行）·**HUD 审计**
  （`backendChip`/`sessSession`/`sessCore` 三个字段**读而不写**）·**多行自动长高**（`REQ-12`）。
  ② ★ **一条共性，比七项本身更值得记**：七项里有**四项**在改前是「控制看起来在、其实什么也不做」——
  空回调、backendChip（`RefreshHud` 每 tick 求值一个恒假的 `if`）、Enter 键（提示行写「点击发送提交」
  是对缺陷的**如实描述**）、resize（行宽不跟随，右侧一片白）。它们的共同形状是**静默**：
  没有崩溃、没有日志、探针全绿。⇒ 判据因此从「有没有报错」改成「**这个东西建过没有 / 动过没有**」，
  `--hud-probe` 的 `HudTouch` 就是这条判据的机械形式（`m_hudAudit` 默认 false ⇒ 产品路径零开销）。
  ③ ★★ **撤回一条自己写下的决定**：`TODO-02` 里「深色刻意保留 `sansFamily = ""`（SimSun）作**回归基线**，
  浅色才用雅黑；两主题字形不同是**有意为之**」——**作废**。两主题现在都用 `Microsoft YaHei`。
  撤回理由不是审美：用户报「three dots 暗色每个 dot 的间隔比浅色大」，实测比报告更严重——
  浅色把 `·`(U+00B7) 渲成**一条 9px 实心横杠**（三点粘连），深色是**三个独立 2px 点、中心距 12.0px**；
  浅色 `↗`/`≡` 另带 ClearType 彩边（15 种墨色 vs 3 种）。根因正是**两套字族**。
  ⇒ 判定：**让同一个控件在两主题下长得不一样，不是「有意为之」，是可预期的缺陷源**；
  「回归基线」这个词在这里被用错了——基线是**为了让改动可被观测**，不是**为了让两张图不一样**。
  代价照付：深色 paints 变（demo 22→23 · realistic 17→19 · cot 21→22 · scale 21→23），
  **今天之前的深色 paints/frames 不可直接对比**；结构读数（blocks/deltas/messages）六夹具 × 两主题全不变。
  这是本仓**第一次有意改变深色字形基线**。
  ④ ★★ **本轮最重要的方法论收获：仪器错了三次，每次都是「自动检测器里藏了没写出来的假设」**（都已记入 `status.txt`）：
  - 裁切框锚在扫描右缘、正好落在画布边 ⇒ 把最后一个按钮**切掉一半**，然后"发现"按钮少了一个；
  - 判按钮底色用「行上第二常见色」，换字体后计数跌破硬阈值，**静默退化**成页面底色（量出 86px 的"按钮"）；
  - 用「颜色种类最多的行」定位字形行，两主题各挑到一行不同位置的行。
  ⇒ 最后能用的办法是**不检测**：坐标取自探针自己的读数，再打 ASCII 位图人工看。
  ★ 同族还有两例：`export-text` 首版数「以 `[` 开头的行数」立刻红（9 vs 8，错的是仪器——`[ask]` 行也以 `[` 开头）；
  `enter-sends` 首版数 `kind=="user"` 的块、断言发送相 +1 立刻红（1→1），
  因为回放模式下 `SendPrompt` 走 `AddNoticeRow("[prompt 仅记录] …")`、**永不产生 user 块**。
  **若相信推理而不跑，会去「修」一段本来正确的代码。**
  ⑤ ★ **探针反过来抓到我自己的布置错**：加最小尺寸后 `shrink` 相变红，因为它请求 `base−320=800`、
  被下限正确拒绝——判据没错、**测试布置错了**。改成 `base−240=880`（正好下限），
  把「低于下限」留给专门的 `min-size` 相：**两个不同命题不能挤在一相**。
  ⑥ **新立 `REQ-13`**（窗口无最小尺寸约束）：详见该条。★ 它的取证方法与 `REQ-12` **不同**——
  `REQ-12` 靠**分支对读**（同一虚函数两条规则打架），`REQ-13` 靠**全仓关键词扫描 + 公开 API 全表核对**
  （`WM_GETMINMAXINFO` 在 ECDI 含 `docs/` 全树只出现 1 次，且是注释里顺带提及，从未被拦截）。
  ⑦ **一条流程纪律**（第三次同族）：批量改源文件时**不要用行数组写入器**（`[IO.File]::WriteAllLines` /
  `Path.write_text`）。本轮用它插 22 处埋点，把 `gui/src/App.cpp` 整体写成 CRLF（8990 行），
  `probe_hud_negctl.py` 当场拒绝运行（`REFUSING: App.cpp is not LF-only`）——
  **那个前置检查正是为此存在**。已按字节归一 + sha256 复核。
  ⑧ **`REQ-12` 收口后的清理**：负对照三条 needle 因收敛而失配（`SKIP needle hits=0`），
  两个脚手架函数（`ContentHeight`/`LineHeightNow`）与一个只为它们的 `#include` 已删，
  头注释从「手抄三个量」改写为「内容高问框架，本类只管 clamp」（原注释还引用了一个**已不存在**的 `kInputMinH`）。
  `--grow-probe` 删死物前后读数**逐位相同**（29/46/63/132/29/29/63）。
- **v2.9（2026-10-18 晚）★ 系统标题栏随主题切色（`TODO-04` 落地 + `TODO-05` 复跑闭合）**：
  ① **触发**：§5 落地的是 HUD 顶栏（36px），窗口最上面那条 32px `ECDI::CaptionBar` 仍用 ECDI 硬编码深色
  （`CaptionBar.cpp:31` 底色 `#26282d` + `:37` 标题 `#ebecf0`）⇒ `--theme light` 下「上面糊了一条深色带」。
  ② **归属判据**：`CaptionButton` 是 `final` 且无样式 API，`Theme` 的 9 个 `Get*Style()` 里没有 caption 令牌
  ⇒ 框架侧无入口 ⇒ 按铁律只在本仓派生：新增 `gui/src/ThemedCaptionBar.h`，`Theme.h` 增 8 个 caption 令牌，`App.cpp:388` 换构造点。
  ③ ★ **两个非显然的坑**（详见 `docs/variant-c-ecdi-mapping.md` §6.3）：
  - **浅色 hover 必须用「黑」预混**（`(0,0,0,26)`/`(0,0,0,45)`）：照抄原生白 @26/@45 在白底上等于无变化。
    半透明合成本身可用（`DrawRect` 走 `BlendAlphaSolid`，`GDIBackend.cpp:649`；REQ-11 只影响 `DrawText`/`DrawFocusRect`）。
  - **释放基类按钮后绝不能调 `CaptionBar::SetSize` / `CaptionBar::OnPaint`**：前者调私有 `RelayoutChildren()`
    解引用 `m_minButton` 等，后者调 `m_maxButton->SetGlyph()`；而 `SetSize` 每次布局 `Arrange` 都会被调 ⇒ 必然 use-after-free。
    两个 override 分别限定到 `ECDI::Widget::SetSize` / `ECDI::Panel::OnPaint` 基类实现。
  ④ **验证**：`TODO-05` 复跑 10 组（2 主题 × 3 夹具 × gdi/gl）全 PASS，截图取证三处颜色全部随主题切换。
  ⑤ ★ **一条仪器教训**：旧 `regress.py` 的 `sleep + p.terminate()` 时序在本机已不成立——提前杀进程走不到
  优雅收尾路径，`--out` 永远不落盘（首轮 10 组全 `out=False`）。改成 `p.wait(timeout=...)` 等 `--auto-close`
  自行退出才拿到读数。★ 判据：**不要用 sleep 当同步**，要用进程自己的退出信号。
- **v2.8（2026-10-18）★ 双主题在真 GUI 上落地，新立 `TODO-01/02/03`（均为 harness 侧，已闭合）+ 落地暴露的四个真问题**：
  ① **触发**：`harness-hifi-variant-c.html` 的浅色形态若只停在 HTML 原型，就无法回答「这套视觉在真 GUI 上成不成立」。
  ⇒ 把 `gui/src/Theme.h` 从编译期常量改成运行期双令牌（`TODO-01`），并加 `--theme dark|light`。
  ★ 手法关键：**不改成员访问**（全仓 ~200 处按名引用会漏改），改**引用别名** `inline const ECDI::Color& kX = g_theme.x`
  ⇒ 调用点**零改动**。硬约束：只能在 `BuildUi()` 之前调——颜色值在 BuildUi 时被拷进各 `PanelStyle`。
  ② ★★ **落地才暴露的四个真问题**（静态推演一个都发现不了）：
  - **助手气泡在浅色下「消失」**：首版 `cardBg` 与 `contentBg` 都设 `#ffffff`，**同值** ⇒ 助手消息只剩 1px 描边，
    退化成「裸文本」。深色无此问题（`(30,33,40)` vs `(22,24,29)`）。
    ⇒ 根因是**两套主题的层次方向相反**：深色「越重要越亮」，浅色「越重要越暗」，照抄相对关系必然出错。
  - **思维链卡几乎不可见**：同源，首版 `reasoningBg #fbfbfc` 只有 dL\* −1.3，且**比助手卡还暗**（层级颠倒）。
  - **环形进度条轨道消失**：轨道色复用 `kLine`，`#e7e9ee` 压白卡 `#f1f3f7` 只有 **1.09:1**（深色同一对 1.19:1）
    ⇒ 新增独立令牌 `kRingTrack`（浅色 `#d8dce6`，1.24:1）。
  - **输入框常态蓝色虚线焦点框**：ECDI `TextBox::OnPaint` 有**两套**边框（`TextBox.cpp:1229-1265`），
    ① `borderWidth/borderColor` 实心圆角环 ② `HasFocus() && ShowFocusRect()` 的 `DrawFocusRect` 点线框。
    本项目输入框**启动即获焦**（`App.cpp:325`，验收 A2）⇒ ②**永远**盖在①上 ⇒ `TODO-03` 派生 `ComposerBox` 换掉。
    ★ **不是**直接 `SetShowFocusRect(false)` 了事——那样焦点完全不可见，是拿可访问性换观感。
  ③ ★★ **指标教训（本轮最重要的方法论）**：判「表面分不分得开」**不能用 WCAG 比值**——它是为**文字**
  设计的，在 1.0–1.2 区间几乎没有分辨率。必须用 **CIE L\* 偏移**：
  | 表面 | 深色 dL\* | 浅色 dL\* |
  |---|---|---|
  | 思维链 | +3.49 | −1.75 |
  | 消息卡 | +4.47 | −2.42 |
  | 工具卡 | +5.88 | −3.51 |
  | 工具卡头 | +7.88 | −5.69 |
  | 环形轨道 | +12.95 | −12.27 |
  ★ 浅色偏移量约为深色**一半**，**不是错**：白画布 L\*=100 已到顶，向下只有 100 的空间；
  深色画布 L\*=8.24，向上有 92 的余量 ⇒ **同样的感知分离度，浅色需要的数值更小**。
  ④ **WCAG 4.5 的适用边界（用户反驳后修正）**：用户指出浅色低对比度问题没那么严重、真正需要对比度的是深色。
  采样 DSH 自身两套主题后：DSH 浅色正文 **18.90** / 次要 **3.71**；DSH 深色正文 **17.45** / 次要 **8.54**。
  ⇒ 用户方向对（深色才是高对比刚需，被数据证实）；但「浅色跟 DSH 差不多」这个事实判断**不成立**
  （variant-b 正文只有 8.20，差一倍多）。
  ⇒ **正确结论**：4.5 是**正文**门槛，拿它卡次要文字/装饰信息是误用；只拉正文、次要提到 ~4.5 即可。
  ⑤ **回归**：`--theme light` 与默认深色**两轮全 PASS**，5 用例读数逐项一致
  （demo 8/19/43 · scale 2000/4590/8906 · realistic 220/717/1213，全部 `seqGaps=0/violations=0/unknownKinds=0`）
  ⇒ 换主题只改颜色令牌，不动解析/布局。
  ⑥ **浅色对比度实测**（全部 ≥4.5）：正文 `#23262e`/白 **15.13** · 次要 `#6b7078`/白 **4.98** ·
  次要/面板 4.73 · 正文/userBg 13.36 · ask 问句 10.06 · 白/品牌蓝 5.30 · 白/琥珀按钮 5.35 · ok/面板 4.80。
  ⑦ **仍未兑现（诚实边界）**：字重层级（`REQ-10`）· 阴影/渐变（无原语）·
  **CJK 加粗两链无公共解** · **中文字形只在 `sansFamily` 非空时改善**（深色刻意保留 SimSun 作回归基线；
  换雅黑行高 +19%：SimSun 16 → YaHei 19 @14px，需连带调所有硬编码行高）。
  ★ **2026-10-22 前向指针（不改写上文，只标注）**：本段 ⑦ 里「深色刻意保留 SimSun 作回归基线」一句**已作废**，
  两主题现均为 `Microsoft YaHei`；作废理由见本文件 §6 的 `v3.0` ③ 与 `FONT-UNIFIED` 行。历史记录保留原貌，
  以免读者以为「当时的判断本来就不存在」。
- **v2.7（2026-10-18）★ 新立 `REQ-10`（`Font` 表达力）+ `REQ-11`（alpha 双链不对等）+ 推翻本仓库自己的两个错误结论**：
  ① **触发**：用户给出高保真原型 `docs/prototype/harness-hifi-variant-c.html`，要求评估落地缺口。
  原型用 10 处 `font-weight: 600/650/700` 建立层级 ⇒ 必须先回答「ECDI 能不能画字重」。
  ② ★★ **推翻结论 A（「全框架单字重，字重根本画不出来」）**：这是我只读了 `Font.h`（只有 `size`/`family`）
  和 GDI 后端硬编码 `FW_NORMAL` 就下的判断，**没有做受控对照**。实测：
  - **GDI 侧 `lfWeight` 对 harness 真实用到的 5 个族全部有效**（`req11_weight.cpp`，只改 `lfWeight`）：
    `Microsoft YaHei` CJK 14px 墨迹 507→690 = **1.36×** · 同族拉丁 15px 168→277 = **1.65×** ·
    `Microsoft YaHei UI` 1.36× · `DengXian` 1.29× · `Segoe UI` 1.79× ⇒ **≥1.29× 全部有效**。
  - **拉丁文用族名就能拿到字重，且两链方向一致**：`Segoe UI` → `Segoe UI Semibold` = GDI **1.20×** / GL **1.21×**；
    → `Segoe UI Bold` = GDI **1.38×** / GL **1.57×**。
  ⇒ 正确结论：**缺的不是「字重能力」，是 `Font` 上的一个 `weight` 字段**（GDI 侧改两行即可消费）。
  ③ ★★ **推翻结论 B（「GDI 里 Bold 是 style 不是 family」）**：`req11_facekind.cpp` 实测
  **完整 face 名在 GDI 里普遍有效**（`Arial Bold` / `Times New Roman Bold Italic` / `Consolas Bold` /
  `Segoe UI Bold Italic` / `Microsoft YaHei UI Bold` 全部命中）。真因是**本地化规范名**：
  `GetTextFaceW` 返回 face 的规范名，`Microsoft YaHei` 的规范名是**中文** `微软雅黑`
  ⇒ 英文完整名 `Microsoft YaHei Bold` 匹配不上而回退宋体，但 **`微软雅黑 Bold` 能命中**
  （`req11_root.cpp`，23 个族名 × 英文名/本地化名两两对照）。
  ⇒ 规律：**规范名是中文的族必须用中文完整名；规范名是英文的族英文名即可**。
  ★ 但这仍救不了 CJK 字重——`微软雅黑 Bold` 在 GL 链 `ResolveFile` 返回**空串**（头文件明写不支持中文族名）。
  ④ ★ **仪器教训（记录，防复发）**：首版 `req11_ink.cpp` 直接比较 **GDI 墨迹 vs FreeType 墨迹**，
  得出「Segoe UI 常规体就 0.58×」——那是**两个渲染器的抗锯齿差异**，不是字重差异。
  ⇒ 正确判据是**只在链内比较**（同链 base 族 vs variant 族）；跨链绝对墨迹量**不可比**。
  同类错误在 REQ-11 的取证里又出现一次：深底亮字**用「最暗像素」当判据**，取到的是背景色，
  四档 alpha 全一样，看似"证实"实为取错对象 ⇒ 改取**最亮**像素才看出真结果。
  ⑤ **`REQ-10` 的三条子缺陷同源**（`Font` 无法表达「同族不同 style / 不同 face」）：
  无字重（`Font.h:17-18` + `GDIBackend.cpp:1384` 硬编码 `FW_NORMAL`）·
  无 face index（`FontEngine.cpp:236` `FT_New_Face(..., **0**, ...)`，TTC 永远只加载 face 0）·
  复合族名第二项断链（`FamilyToFileName` 只匹配 `"<family> & "` 的**首项**，`Win32FontSource.cpp:167-279`）。
  ⑥ **`REQ-11` 标 📋 推后**：`gui/src` 全部 `Color::FromRGBA8(..., 255)`、variant-c 全文 `rgba|opacity` **零命中**
  ⇒ 当前**零消费者**，不阻塞任何已规划功能。但形态已钉死（GDI 的 `DrawText`/`DrawFocusRect` **恒丢** alpha，
  `DrawLine` 在「AA 关 + a<1」时丢；GL 链 9 个原语全消费）⇒ 将来要用时不必重新取证。
  ⑦ **附带发现**：`gui/src` 全部 `ECDI::Font` 只设 `size`，7 处 `family` 赋值**全是** `kMonoFamily`
  ⇒ **中文正文 `family` 为空 ⇒ 两链一致落到 SimSun**（`req11_default.cpp` 验证）
  ⇒ **harness 现在的中文正文实际渲染在宋体上，不是微软雅黑**。这是一条**纯 harness 侧**待办（换族名即可），
  但受 ③ 约束：可用的族名只有 `Microsoft YaHei`（两链都通），`Microsoft YaHei UI` 在 GL 链断。
  ⑧ 取证：探针 `.scratch/req11/`（`req11_weight.cpp` · `req11_root.cpp` · `req11_facekind.cpp` ·
  `req11_default.cpp` · `req11_ink2.cpp` · `req12_text.cpp` · `req12_alpha.cpp` · `family-sim.py` · `ttc-faces.py`）；
  对照表 `docs/variant-c-ecdi-mapping.md`（同日订正 §2.3 与 §2.4 两处错误解释）。

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
