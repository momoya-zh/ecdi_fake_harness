# ECDI_fake_harness · ECDI 能力需求账本

> **定位**：本项目对 ECDI 的**能力需求 / 缺陷登记**的唯一落点。
> 依据项目铁律——**任何对 ECDI 框架的修改都不在本仓库做**，这里只登记需求、只给证据、只判归属（见 `P1-gui-implementation-spec.md` §1.2）。
> 状态：**v3.7**（2026-10-26；v1.0 建立于 2026-10-08。★ 订正：本行自 v2.9 起就落后于 §6，v2.9/v3.0 均未回写此处，见 §6 v3.1 ⑧）。
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

## §2 条目（★ REQ-01/02/03/04 = harness 侧，**均已闭合**；**REQ-03 于 2026-10-24 以「推 + 拉兜底」落地——`PostToUi` 首个框架外真实消费者**；REQ-05 已闭合；**REQ-08 已闭合（2026-10-16，Phase 33）**；**REQ-06 已裁决结案（2026-10-17，③ 记为已知现状）**；**REQ-09 已闭合（2026-10-17，ECDI 当日修复 + 验收 8/8）**；**REQ-10 ✅ **已闭合（2026-10-24 当日：ECDI Phase 36 全链收口——三批 `762c483`/`e394514`/`e163357` + 五链 497/497 + 五处台账同步；`Font` 表达力；★ 2026-10-24 ECDI 侧核验属实并登记 deferred #70）**；**REQ-11 已登记推后（2026-10-18 新立，alpha 双链不对等；★ 2026-10-24 ECDI 侧核验属实并登记 deferred #71）**；**REQ-12 已闭合（2026-10-21 当日：ECDI 订正多行 preferred 语义 + 本仓收敛回框架）**；**REQ-13 已闭合（2026-10-23，ECDI 落地 `Window::SetMinimumSize` + 本仓收敛删回弹绕行）**）
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
| 状态 | 📋 **已登记（推后）**——**缺陷面零消费者**，不阻塞任何已规划功能；但形态已钉死，将来要用时不必重新取证。★ **2026-10-24 ECDI 侧对码核验属实**（一处行号订正见「覆盖面」行），登记 ECDI `roadmap-deferred` **#71**（推后口径一致：零消费者，触发条件 = 首个半透明文本/焦点框消费者出现）。★ **2026-10-26 订正消费者面**：P4 Phase F 的设置面板遮罩已成为**首个半透明绘制消费者**，但它走的是 `DrawRect` + `BlendAlphaSolid`——即「覆盖面」表里**已支持**的路径，**没碰到缺陷面**。⇒ 缺陷面（`DrawText` / `DrawFocusRect`）仍是零消费者，**触发条件未变**，本条状态不动 |
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
| ★ `TODO-01` | **双主题令牌化**：`Theme.h` 从「编译期常量」改成「运行期可切换」，让 `harness-hifi-variant-c.html` 的浅色形态能在**真 GUI** 上验证，而不是只停在 HTML 原型 | ✅ **已落地**（2026-10-18）：`struct Theme` 53 色 + `sansFamily`，`MakeDark()`/`MakeLight()`，`SwitchTheme()` + **引用别名**（`inline const ECDI::Color& kX = g_theme.x`）⇒ 全仓 ~200 处按名引用**零改动**；`--theme dark\|light` CLI。★ 硬约束：只能在 `BuildUi()` 之前调（颜色值在 BuildUi 时被拷进各 `PanelStyle`）。回归双主题各 5 用例全 PASS。详见 `docs/variant-c-ecdi-mapping.md` §5 |
| ★ `TODO-04` | **自绘标题栏随主题切色**：系统 `CaptionBar` 的底色/标题/三钮颜色在 ECDI 里全部写死（`CaptionBar.cpp:31/37` · `CaptionButton.cpp:14-27`），框架无 caption 样式令牌 ⇒ 深色主题下外观正常，`--theme light` 下标题栏仍是深灰（未验证，本轮才发现） | ✅ **已落地**（2026-10-18）：应用侧派生 `gui/src/ThemedCaptionBar.h`（`ThemedCaptionBar : ECDI::CaptionBar` + `ThemedCaptionButton : ECDI::Widget`），`Theme.h` 增 8 个 caption 令牌（`captionBg/captionInk/captionGlyph/captionGlyphHot/captionHover/captionPressed/captionCloseHover/captionClosePressed`）+ 8 个 `kCaption*` 别名；`App.cpp:388` 构造点换成派生类。**ECDI 一行未改**（铁律）。深色令牌与原生逐值相同 ⇒ 回归基线不变。★ 关键约束：派生类把基类三个按钮 `RemoveChild` 释放后，**绝不能**再调 `CaptionBar::SetSize`（→`RelayoutChildren` 解引用）或 `CaptionBar::OnPaint`（→`m_maxButton->SetGlyph`）⇒ 两个 override 都限定到 `ECDI::Widget` / `ECDI::Panel` 基类实现 |
| ★ `TODO-05` | **`--theme light` 下未实际跑通全链验证**：本轮新增 caption 令牌后需按 §4 既有回归口径复跑，避免「只编译过、没实机看过」 | ✅ **已复跑并截图**（2026-10-18 晚）：10 组（2 主题 × demo/scale/realistic × gdi/gl）全部 `rc=0` 且 `seqGaps=0 / violations=0`（demo 8/19/43 · scale 2000/4590/8906 · realistic 220/717/1213，两主题逐项一致）⇒ 「换主题只改颜色令牌」这条结论对 caption 同样成立。截图 `.scratch/caption-theme/caption-*-top.png`：caption 底色浅 `#f8f9fb` / 深 `#26282d`，标题字 `#23262e` / `#ebecf0`，close glyph `#696f7b` / `#cfcfd5` ⇒ 三处颜色全部随主题切换。回归驱动 `.scratch/caption_theme_regress.py`、截图 `.scratch/caption_theme_shots.py`、读数 `.scratch/caption-theme-regress.log` / `caption-theme-shots.log` |
| ★ `TODO-02` | **字体工厂**：全仓 `ECDI::Font` 构造散落 18 处、`family` 只有 `Consolas` 一个值，中文正文全部落到 SimSun；且 `AddLine()` 只在传了字号时才 `SetFont` ⇒ 同一界面里两种中文字形混排 | ✅ **已落地**（2026-10-18）：`harness::SansFont(float)` / `MonoFont(float)` 单一入口；`AddLine()` 改为无条件 `SetFont`（默认 `14.0f` = `Font::size` 出厂值）。★ **2026-10-22 订正**：原写「深色刻意保留 `sansFamily = ""`（SimSun）作回归基线，浅色才用雅黑」——该决定已被**推翻并撤回**：两主题现在**都**用 `Microsoft YaHei`（`Theme.h` 的 `MakeDark()` 里 `t.sansFamily = "Microsoft YaHei"`）。撤回理由见下条 `FONT-UNIFIED` 与 §6 的 v3.0 |
| ★ `TODO-03` | **输入框焦点视觉**：ECDI 默认 `DrawFocusRect` 点线框，本项目输入框**启动即获焦**（验收 A2）⇒ 常态就是一圈蓝色虚线，与 variant-c 的实线描边形态冲突 | ✅ **已落地**（2026-10-18）：派生 `ComposerBox` 关掉点线框（`SetShowFocusRect(false)`），改让 `borderColor` 表达焦点（失焦 `kInputBorder` / 获焦 `kDarkAccent`）。★ **不是**直接关掉焦点框——那样焦点完全不可见，是拿可访问性换观感 |
| ★ `FONT-UNIFIED` | **两主题字族统一**（用户报「中间上面三个按键深浅主题样子不同」，收窄为「three dots 暗色每个 dot 的间隔比浅色大」）：`gui/src/Theme.h:184` 深色 `sansFamily = ""` ⇒ ECDI 默认 face = **SimSun**，`Theme.h:281` 浅色 `"Microsoft YaHei"`，而 `SansFont()` 读 `g_theme.sansFamily` ⇒ **同一串字符被两个 face 光栅化** | ✅ **已落地**（2026-10-22）：`MakeDark()` 里 `t.sansFamily = "Microsoft YaHei"`（改一行）。★ 实测比用户报告更严重：浅色把 `·`(U+00B7) 渲成**一条 9px 实心横杠**（三点粘连），深色是**三个独立 2px 点、中心距 12.0px**；浅色 `↗`/`≡` 另带 ClearType 彩边（15 种墨色 vs 3 种）⇒ 根因是**字族**不是间距。修后 `dump_dots3.py` 两主题 run 数/pitch/墨色数完全一致（`SAME=True`）。代价：深色 paints 变（demo 22→23 · realistic 17→19 · cot 21→22 · scale 21→23），**此前深色 paints/frames 不可直接对比**；六夹具 × 两主题的结构读数（blocks/deltas/messages）全不变。★ 这是本仓第一次有意改变深色字形基线，`TODO-02` 里「深色刻意保留 SimSun 作回归基线」一句已随之作废 |

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
