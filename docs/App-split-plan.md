# App.cpp 上帝代码拆分方案（2026-10-25）

> **★ 如果你是新开对话接手的**：先读 [`App-split-handover.md`](App-split-handover.md)。
> 那份写「现在在哪、下一步怎么走、哪些坑已踩过」；本文写「为什么这么拆」。
> 当前进度：Stage 1 + Stage 2 已完成并提交（HEAD `2fbe612`），**下一步是 Stage 3**。
>
> **读者**：接手拆分的 agent。本文自包含。
> **结论先行**：App.cpp 12333 行里 **47% 是测试仪器**（探针 4496 行 + 摘要 1083 行），产品代码只有 6207 行。
> 所以拆分的第一刀不该按"功能模块"切，而该先**把仪器和产品代码物理分开**——这是本仓最干净、也最容易验证的一条边界。
> **工作量**：Stage 1（纯搬运，零风险）半天；Stage 2（BuildMainArea 内部分解）半天；Stage 3（头文件瘦身）半天；Stage 4/5 可选。

---

## 0. 现状体检（全部实测，非估计）

### 0.1 规模

| 文件 | 行数 | 说明 |
|---|---|---|
| `gui/src/App.cpp` | **12333** | 145 个 `HarnessApp::` 成员函数 + 2 个匿名命名空间 + 1 个 `HarnessApplication` |
| `gui/src/App.h` | **1995** | 1 个 `CliOptions`（259 行）+ 1 个 `HarnessApplication`（31 行）+ 1 个 `HarnessApp`（1657 行） |
| 其余 26 个文件 | 总计 3865 | 最大 `Theme.h` 471，无一个超 500 行 |

参照：本仓第二大的 `.cpp` 是 `PromptBar.cpp` 376 行。App.cpp 是它的 **33 倍**。

### 0.2 App.cpp 的内部结构

| 行区间 | 内容 | 行数 |
|---|---|---|
| 1–67 | `#include`（含 Windows 壳、ECDI 事件/布局/控件全家桶） | 67 |
| 68–306 | 匿名 ns：路径 UTF-8 转换、`ElideMiddleByCodepoints`、颜色常量、`HarnessRootLayout` | 239 |
| 308–403 | 匿名 ns 续：`ContentWidth` / `ConversationTitle` / `UnknownKindText` / `ReasoningTitle` / `AddLine` | 96 |
| 407–475 | `HarnessApplication` 四个 override（timer / resize / wheel / key） | 69 |
| 479–518 | 构造 / 析构 | 40 |
| 521–12333 | `HarnessApp::` 成员函数 145 个 | 11813 |

### 0.3 成分分析（按职责聚类，145 个函数全覆盖）

| 组 | 函数数 | 行数 | 占比 |
|---|---:|---:|---:|
| **探针 / 仪器**（`Run*Probe` + 合成输入 + 快照比对 + `DigestReplayFile`） | 30 | **4496** | 38.1% |
| **摘要落盘**（`WriteSummary` 单函数） | 1 | **1083** | 9.2% |
| UI 构建（BuildUi/RebuildUi/TopBar/MainArea/Bottom/Relayout） | 6 | 1573 | 13.3% |
| 转录视图（行构建/折叠/空态/答案） | 17 | 790 | 6.7% |
| 会话与侧栏 | 14 | 774 | 6.6% |
| HUD / 刷新 / tooltip / 滚动跟随 | 11 | 548 | 4.6% |
| 设置 / 模型 / 提供商 | 18 | 539 | 4.6% |
| 生命周期与 tick | 5 | 489 | 4.1% |
| 协议流式（Drain/HandleLine/RouteLastMessage/唤醒） | 8 | 484 | 4.1% |
| 发送 / 工作区 / 临时会话 | 18 | 481 | 4.1% |
| 历史会话 + UI 状态持久化 | 9 | 315 | 2.7% |
| 导出 | 3 | 160 | 1.4% |
| 全局快捷键 / 检视栏 | 2 | 54 | 0.5% |
| 计时钟 | 2 | 20 | 0.2% |

**仪器合计 5579 行 = 47.3%。** 这不是"顺手写多了"，是本仓的工作方式：每条需求都要有机读判据（`status.txt` 里每条 done 都挂着"机读证据"）。仪器本身是交付物，不能删——但它不该和产品代码住在一个文件里。

### 0.4 单函数 Top 10

| 行数 | 函数 | 位置 |
|---:|---|---|
| 1083 | `WriteSummary` | 11252 |
| 982 | `BuildMainArea` | 2249 |
| 545 | `RunSidebarProbe` | 5240 |
| 363 | `RunWorkspaceProbe` | 9958 |
| 353 | `RefreshHud` | 10899 |
| 347 | `RunMenuProbe` | 6041 |
| 339 | `RouteLastMessage` | 3781 |
| 321 | `RunResizeProbe` | 6404 |
| 314 | `RunHistoryProbe` | 8434 |
| 299 | `RunSettingsProbe` | 7773 |

### 0.5 App.h 的成分

| 行区间 | 内容 | 行数 |
|---|---|---|
| 47–305 | `struct CliOptions`：47 个字段，**34 个是探针开关** | 259 |
| 307–337 | `class HarnessApplication` | 31 |
| 340–390 | `HarnessApp` public（Run / tick 回调 / SendPrompt / 快捷键 / tooltip） | 51 |
| 391–1230 | `HarnessApp` private 方法声明（含 840 行方法区，其中 1195–1952 是探针声明） | 840 |
| 1227–1332 | 核心数据成员（窗口/控件句柄、会话表、脏标志） | 106 |
| 1333–1952 | **探针记录结构体 + 探针状态成员**（`StateProbeRec`…`DelProvProbeRec`） | 620 |
| 1953–1995 | 评估计数器（paints/frames/measure/invalidations） | 43 |

**头文件里 44% 是仪器**（CliOptions 的 34 个开关 + 620 行探针结构体）。另有 501 处 `m_*Probe*` 成员引用散在 App.cpp 里（其中 28 处是 ECDI 自己的 `m_probe`/`m_measureProbe`）。

### 0.6 具体痛点（不是"不好看"，是"改不动"）

1. **探针与产品代码交错在同一个函数里**。`OnReplayTick`（3373–3629）里有两段探针派发链：settle 分支内 12 个 `if (m_options.xProbe && !m_xProbeDone) RunXProbe();`（3505–3590），函数尾又有 4 个（3611–3625）。改 tick 逻辑时无法判断"这行是产品行为还是仪器"。
2. **`BuildMainArea` 982 行里塞了 8 个不相关的界面区**：左栏（2255）、历史段（2371）、侧栏底（2392）、中栏头（2420）、`···` 菜单（2519）、设置浮层（2621–2910，289 行）、跳最新按钮（2910）、模态遮罩（2937）、模型浮窗（2962）、tooltip（2996）、右栏检查器（3054）。改右栏要滚过 800 行无关代码。
3. **`WriteSummary` 1083 行是 20 段 `if (m_options.xProbe) { json += ... }` 首尾相接**。加一个探针字段要在 1000 行里找位置，而它本该和探针本身住在一起。
4. **`main.cpp` 为了拿 `CliOptions` 吃掉整条 ECDI 控件 include 链**（`App.h` 里有 Button/Label/ScrollView/ScrollBar/TextBox/Window/CaptionBar…）。
5. **单文件 = 单冲突点**。两个人同时改 GUI 任何位置，必在 App.cpp 撞车。

### 0.7 一个诚实的反例：编译时间不是理由

实测一次 `App.cpp` 重编 + 链接约 **11 秒**（15:42:47 改文件 → 15:42:58 出 exe）。MSVC 对 12k 行没意见。**别拿编译速度当拆分理由**，它不是瓶颈。

但有个反向的坑必须先说清：现在 `App.h` 只被 **2 个 TU**（App.cpp / main.cpp）包含；拆成 20 个 .cpp 后，任何一次 `App.h` 改动会重编 20 个 TU。**所以 Stage 1 之后必须紧接着 Stage 3**（把 `CliOptions` / 探针结构体从 App.h 挪出去），否则拆完反而更慢。这是本方案里唯一有先后强制关系的两步。

---

## 1. 拆分原则（违反 = 返工）

1. **只搬不改**。Stage 1/2/3 全程禁止顺手改逻辑、改注释、改命名、格式化。函数体必须**逐字节**搬走。唯一允许的编辑是：`#include` 增删、`HarnessApp::` 限定符保留（成员定义放哪个 TU 都合法）。
2. **一步一 commit，一步一验证**。提交前缀 `refactor(gui): ...`。precommit hook（BOM/行尾/夹具）会拦，别 `--no-verify`。
3. **新文件：无 BOM、LF、UTF-8**。与现有 26 个文件一致（`tools/precommit-check.py` 会查 BOM 漂移）。
4. **不动** `protocol/` `core/` `evals/` `third_party/`；不动 ECDI 框架。
5. **探针保持成员身份**，不许改成自由函数。它们要访问私有成员（`m_scroll` / `m_hud` / `m_sess` / `m_application`），改成自由函数就得开 friend / accessor，噪声远大于收益。C++ 允许成员函数定义在任意 TU，这就够了。
6. **单文件上限 1200 行**（软约束）。超了就在同一 Stage 内再切一刀。
7. **匿名命名空间的 helper 要跟着搬家**，不许复制。见 §5 风险表。

---

## 2. 目标布局

全部**平铺在 `gui/src/`**（不建子目录）：`target_include_directories` 只加了 `src`，子目录会让 `#include "App.h"` 的相对解析变复杂；而 `CMakeLists.txt` 用的是 `GLOB_RECURSE CONFIGURE_DEPENDS`（第 38 行），**新增 .cpp 无需改 CMake**。

### 2.1 产品代码

| 文件 | 内容 | 预计行数 | 来源（当前行号） |
|---|---|---:|---|
| `App.cpp` | include + 匿名 ns + `HarnessApplication` + ctor/dtor + `Run` + tick 骨架（`OnReplayTick`/`OnAutoClose`/`OnSweepTick`/`OnWindowResized`） | ≈970 | 1–518、1759–1885、3373–3732 |
| `AppUiChrome.cpp` | `BuildUi` `RebuildUi` `BuildTopBar` `BuildBottom` | ≈450 | 1887–2050、3231–3275 |
| `AppUiMain.cpp` | `BuildMainArea`（Stage 2 后只剩组合 + 浮层投影）+ `Relayout` | 1167 → ≈340 | 2249–3231、10519–10704 |
| `AppUiSidebar.cpp` | `BuildSidebar` `BuildHistorySection` `BuildSidebarFooter`（Stage 2 产物） | ≈250 | 2255–2412 |
| `AppUiCenter.cpp` | `BuildConversationHead` `BuildMoreMenu` `BuildComposer`（Stage 2 产物） | ≈300 | 2420–2621 |
| `AppUiOverlay.cpp` | `BuildSettingsOverlay` `BuildJumpButton` `BuildModalMask` `BuildModelPopup` `BuildTooltip`（Stage 2 产物） | ≈450 | 2621–3054 |
| `AppUiInspector.cpp` | `BuildInspector`（Stage 2 产物） | ≈180 | 3054–3231 |
| `AppSession.cpp` | 会话创建/切换/侧栏列表构建与比对/归档删除 | ≈780 | 521–1102、1557–1692 |
| `AppHistory.cpp` | 历史扫描/打开 + UI 状态持久化 | ≈320 | 1081–1194、1325–1557 |
| `AppStream.cpp` | `HandleLine` `RouteLastMessage` `Drain*` 唤醒与 EOF | ≈480 | 3275–3373、3734–3781、3781–4120 |
| `AppTranscript.cpp` | 行构建（block/ask/notice）、折叠、重建、空态、答案 | ≈790 | 4120–4712、10321–10519 |
| `AppWorkspace.cpp` | 发送/失败、工作区获取与同步、临时会话、项目列表 | ≈480 | 8770–9155、9912–9958 |
| `AppExport.cpp` | 转录导出（文本 + 落盘 + 对话框） | ≈160 | 9213–9373 |
| `AppSettings.cpp` | 设置面板保存、模型列表、提供商、配置/凭据解析 | ≈540 | 9373–9958 |
| `AppHud.cpp` | `RefreshHud` `HudTouch/Unbuilt` `CacheEvalMetrics` `RequestRepaint` tooltip 状态机、滚动跟随、跳最新 | ≈550 | 10414–10899、10704–10791 |
| `AppSummary.cpp` | `WriteSummary`（Stage 4 后拆成 core + 每探针一段） | 1083 → ≈120 | 11252–12333 |

### 2.2 仪器（与产品代码物理隔离，这是第一刀）

**1a 已落地（2026-10-25），实际行数如下**（`wc -l` 实测，含 include 区与文件头）：

| 文件 | 内容 | 实际行数 |
|---|---|---:|
| `ProbeSupport.cpp` | `SynthesizeClickAt` `SynthesizeKey` `WidgetKindName` `SnapshotRows` `SnapshotUi` `CompareUi` `CompareRows` `DigestReplayFile` `ProbeResizeClient` | 366 |
| `ProbeTranscript.cpp` | `RunHitProbe` `RunReasoningProbe` `RunRebuildProbe` `RunAnswerProbe` `RunSelectProbe` `RunDelProvProbe` | 748 |
| `ProbeSidebar.cpp` | `RunSidebarProbe` `RunSessionProbe` `RunHistoryProbe` `RunThemeProbe` | 1233 |
| `ProbeChrome.cpp` | `RunMenuProbe` `RunResizeProbe` `RunHudProbe` `RunEmptyProbe` `RunModelProbe` | 977 |
| `ProbeFlow.cpp` | `RunEnterProbe` `RunGrowProbe` `RunJumpProbe` `RunKeyProbe`（纯 UI 交互） | 730 |
| `ProbeSettings.cpp` | `RunSettingsProbe` `RunWorkspaceProbe`（唯一碰磁盘/环境/真子进程的两个） | 729 |

> **与初版的差异**：原计划 `ProbeFlow.cpp` 装 6 个函数（≈1330 行，超 1200 上限），
> 实际执行时按上限规则再切一刀，拆出 `ProbeSettings.cpp`。切分依据不是行数，是**副作用面**：
> Enter/Grow/Jump/Key 四个是纯 UI 交互（合成事件 + 回读控件位），
> Settings/Workspace 两个会写配置、读凭据库、设 env、拉真子进程——混在一起改一个要滚过另一个的全部仪器代码。
>
> `ProbeSidebar.cpp` 1233 行，比 1200 的软上限高 2.7%，**刻意不拆**：
> 四个函数共用同一套「拆列表→重建→逐行比对」机制，拆开会把一套仪器劈成两半，
> 收益（33 行）远小于成本（两个文件都要重复 include 与 helper 短名）。

### 2.3 头文件

| 文件 | 内容 | 预计行数 |
|---|---|---:|
| `CliOptions.h` | `struct CliOptions`（只 include `<filesystem>` `<string>`） | 259 |
| `ProbeTypes.h` | 20 个 `*ProbeRec` 结构体，**从类内移到命名空间作用域**（`HarnessApp::StateProbeRec` → `StateProbeRec`） | ≈600 |
| `ProbeState.h` | （Stage 4）`struct ProbeState` 收口全部 `m_*Probe*` 成员 | ≈90 |
| `AppInternal.h` | （Stage 1 必需）跨 TU 共享的匿名 ns helper 声明：`ContentWidth` `PathToUtf8` `Pick` `HarnessRootLayout` | ≈60 |
| `App.h` | 只剩 `HarnessApplication` + `HarnessApp` 类声明 | 1995 → ≈900 |

---

## 3. 分阶段执行计划

### Stage 0 — 基线（必须先做）

```bash
# 1. 工作区当前有未提交改动（delprov 探针，App.cpp +154 行）
git status --short          # M gui/src/App.cpp App.h AppConfig.cpp main.cpp
#    → 先提交或 stash。在脏工作区上搬运，回滚时无法区分"搬运错"和"本来就没提交"
#    ★ 2026-10-25 评审注：delprov 已提交（5524ae7），本方案 §0 的行号快照比该
#    commit 早——执行时一切行号以当场 `git log -1` 的 HEAD 为准重新定位
#    （门 2 的 before 指纹本来就是现场抓取的，不受影响）。

# 2. 构建 + 存基线
cmake -S gui -B gui/build && cmake --build gui/build --config Release
python tools/regress.py     # 必须全绿（13 探针 + 6 夹具 + REQ-03 A/B）

# 3. 存"拆前"摘要快照（6 夹具 × 固定参数）
mkdir -p .scratch/split-baseline
for f in demo realistic cot scale; do
  gui/build/Release/harness_gui.exe --replay protocol/fixtures/... --instant --theme light \
    --auto-close 40000 --out .scratch/split-baseline/$f.json
done
```

### Stage 1 — App.cpp 纯文件切分（零风险，机械搬运）

按 §2.1 / §2.2 的清单，把 145 个成员函数按名字搬到目标文件。**每个新文件自带最小 include 集**（不要复制 App.cpp 的 67 行 include 大礼包——只留真正用到的）。

顺序（从最独立的开始，每步都能单独编译通过）：

1. `Probe*.cpp`（5 个文件，4496 行，38%）——最独立，探针之间几乎不互相调用，只调产品函数。**先切这刀**：立刻让 App.cpp 从 12333 降到 7800。
2. `AppSummary.cpp`（1083 行）——单函数，无依赖，切完 App.cpp → 6700。
3. `AppExport.cpp` / `AppHistory.cpp` / `AppWorkspace.cpp` / `AppSettings.cpp`——中低耦合。
4. `AppSession.cpp` / `AppTranscript.cpp` / `AppStream.cpp` / `AppHud.cpp`——核心耦合，最后切。
5. `AppUiChrome.cpp` / `AppUiMain.cpp`——最后，因为 Stage 2 还要动它。

**Stage 1 实际结束时**（2026-10-25）：App.cpp **871 行**，最大文件 `ProbeSidebar.cpp` 1233 行，
中位数 ≈700。（原估 ≈970 / 1330 是按函数名粗算的，实测略有出入。）

#### 1a 已完成（2026-10-25）——实测结果

| 项 | 结果 |
|---|---|
| App.cpp | **12269 → 7790 行**（切出 30 个函数 + 5 个新 TU） |
| 新文件 | ProbeSupport 366 / ProbeTranscript 748 / ProbeSidebar 1233 / ProbeChrome 977 / ProbeFlow 730 / ProbeSettings 729 |
| 门 1 编译 | 零 error；唯一警告在 `Credentials.cpp`（**本次未触碰的文件**，是既有警告——此前一直是增量构建，从未重编过它） |
| 门 2 指纹 | **PASS**：279 个函数一致 / 0 变 / 0 丢 / 0 新增（含 30 个搬家函数，函数体逐字节相同） |
| 门 3 回归 | **PASS**：6 夹具 + 13 探针 + REQ-03 A/B + validate.py |
| 门 3 GL 抽测 | `backend=gl`、8/19/43、退出码 0 |
| 门 4 摘要 | 6 夹具 + GL 全部 IDENTICAL（见下方 invalidations 的例外说明） |

**执行中遇到的三个真问题（后续 1b–1e 会再遇到，先记下）**：
1. **`ECDI::PlatformWindow` 是不完整类型**。探针要 `m_window->GetPlatformWindow().StartTimer(...)`，
   而 `Window.h` 只前置声明 `PlatformWindow`。App.cpp 原先靠 `Platform.h` 间接带入，
   新 TU 必须显式 `#include "ECDI/Platform/PlatformWindow.h"`。
2. **跨 TU 符号**：实测探针只依赖 5 个非成员符号——`PathToUtf8`(5 处) `WideFromUtf8Path`(4 处)
   `kPinThreshold`(3 处) `kSidebarRowH`/`kSidebarGroupH`(各 1 处)。已按 §5 的预案上提到
   `AppInternal.h`，各 TU 加 `using harness::internal::xxx;`。**其余 45 个匿名 ns 符号探针一个都不用**
   （`ContentWidth`/`Pick`/`HarnessRootLayout` 那几处命中都在产品函数或注释里，不在探针体内）。
3. **`ThemeUsing.h` 是必需品，不是洁癖**：6 个新 TU 全部要用主题令牌短名，
   若各抄 52 条 `using`，加一个令牌就要改 7 处。§5 里 Zcode 提的那条风险由此消解。

#### 1b 已完成（2026-10-25）——`WriteSummary` 独立成 `AppSummary.cpp`

| 项 | 结果 |
|---|---|
| App.cpp | 7790 → **6709 行** |
| 新文件 | `AppSummary.cpp` 1110 行（1082 行函数 + include/文件头） |
| 门 2 | PASS：279 一致 / 0 变 / 0 丢 |
| 门 3 | PASS（regress 全绿 + GL 抽测） |
| 门 4 | 7 份摘要（6 夹具 + GL）**逐字节相同，含字段顺序** |

**1b 的额外依赖（1c 之后还会遇到，都属"常量/工具上提"这一类）**：
- `kProtocolMaxSupported` 也是跨 TU 的（`RouteLastMessage` 判版本 + 摘要里记上限）⇒ 与 1a 的三个几何常量同处理，进 `AppInternal.h`。
- `ECDI::Logger::Log` 需要显式 `#include "ECDI/Core/Logger.h"`（App.cpp 原先靠别的头间接带入）。

★ 门 4 在 1b 用的是**逐字节 + 字段顺序**比对（不是只比 key）：`WriteSummary` 就是产生这份
摘要的函数，字段顺序是 84 份 evals 基线的一部分。比对方法：两边都剔除 4 个 volatile 键后
按原顺序 re-dump 再比字符串——顺序变了就会被抓到。

#### 1c 已完成（2026-10-25）——产品代码第一刀：导出/历史/工作区/设置

| 项 | 结果 |
|---|---|
| App.cpp | 6709 → **5404 行**（再切出 47 个函数，累计 -56%） |
| 新文件 | `AppExport.cpp` 198 / `AppHistory.cpp` 355 / `AppWorkspace.cpp` 362 / `AppSettings.cpp` 565 |
| 门 1 | 零 error、零新增 warning |
| 门 2 | PASS：279 一致 / 0 变 / 0 丢 |
| 门 3 | PASS（regress 全绿 + GL 抽测 `backend=gl` 8/19/43） |
| 门 4 | 7 份摘要 IDENTICAL |

**分组依据不是"功能名词"，是"副作用面"**——这条在 1a 切 ProbeFlow 时立的规矩，1c 又用了一次：

- `AppWorkspace.cpp` 里全是**状态迁移 + 消息投递**，一个控件都不建；建行/建控件的那两个
  （`RebuildProjectList` / `MaterializePendingPrompts`）**故意留在 App.cpp 没搬**，它们属 1d 的
  HUD / 转录。判据：搬走之后这个文件的依赖从 7 个内部符号降到 **0 个**。
- `AppHistory.cpp` 把"历史 .jsonl 扫描"和"UI 状态持久化"合成一个文件，理由是它们**共享
  state 目录的路径规则**（`HistoryRoot` / `UiStatePath`）——拆成两个文件等于把同一套路径拼法
  写两遍。
- `AppSettings.cpp` 是唯一读环境变量、写配置文件、碰凭据库的路径，所以它独立带
  `Credentials.h` / `AppConfig.h` / `String.h`（`ECDI::UTF8ToWide`）。

**1c 的额外依赖（1d/1e 还会遇到）**：
- 侧栏几何整组（`kSidebarListW` / `kSidebarRowW` / `kSidebarRowGap` / `kSidebarNameW` /
  `kSidebarMaxRows`）上提 `AppInternal.h`——历史列表建行要用，1e 的侧栏 builder 也要用。
- `ECDI::UTF8ToWide` 要显式 `#include "ECDI/Core/String.h"`；`creds::` 要 `Credentials.h`。

**顺手做的减法（这是"搬走"的一部分，不是顺手改）**：App.cpp 顶部
`ShlObj.h` / `objbase.h` / `commdlg.h` / `Credentials.h` 四个 include 随代码一起搬走了。
判据是机读的：`grep -E "SHGet|CoTaskMem|BROWSEINFO|OPENFILENAME|creds::"` 在 App.cpp 里已归零。
现在 App.cpp **不再碰任何 Windows 壳对话框或 COM 初始化**——这条边界以前是混的。

#### 1d 已完成（2026-10-25）——会话/转录/流式/HUD，App.cpp 只剩 UI 构建

| 项 | 结果 |
|---|---|
| App.cpp | 5400 → **2346 行**（再切出 59 个函数，累计 -81%） |
| 新文件 | `AppSession.cpp` 770 / `AppTranscript.cpp` 861 / `AppStream.cpp` 817 / `AppHud.cpp` 857 |
| 前置 | `UiCommon.cpp` 96 行（5 个共享 helper 从匿名 ns 搬出） |
| 门 1 | 零 error、零新增 warning |
| 门 2 | PASS：279 一致 / 0 变 / 0 丢 |
| 门 3 | PASS（regress 全绿 + GL 抽测 `backend=gl` 8/19/43） |
| 门 4 | 7 份摘要 IDENTICAL |

**四个文件的边界是"改它的理由"**，不是功能分类：

- `AppSession` —— 会话有生命周期（core 子进程 + 写入端 + 工作区归属 + 行集合都挂在它上面）。
- `AppTranscript` —— 只管"长什么样"，行宽一律 `ContentWidth(m_scroll)`，不许自己算。
- `AppStream` —— 只管"收到什么"：解包 / 唤醒 / tick / 断流判定。`RouteLastMessage` 里不许直接摆控件。
- `AppHud` —— 只管"摆在哪儿"：`Relayout`(184) + `RefreshHud`(350) 是三棵树的唯一摆位与读数出口。

**这一步做了一次"函数上提"，值得单独记**：`Pick` / `ContentWidth` / `ConversationTitle` /
`ConversationSubtitle` / `UnknownKindText` / `ReasoningTitle` 六个原本是 App.cpp 匿名命名空间的
内部链接 helper，被转录与 HUD 同时需要 ⇒ 新建 `UiCommon.cpp`（声明进 `AppInternal.h`）。
其中 `Pick` 破例写成**头内 inline**：一行谓词、AppTranscript 里 22 处调用点全在建行热路径上，
为它多一个 TU 外调不值。头文件里"不许 inline 实现"那条针对的是有体量的函数。

★ 门 2 在这里的用法值得记：搬函数到 `namespace harness::internal` 时，**定义行保持列 0**
（不缩进），函数体逐字节不动 ⇒ 指纹仍然全等。这是"纯搬运"能被机器证明的关键细节。

**顺手做的减法**：`UTF8.h` / `BackendFactory.h` / `MouseButton*.h` / `ScrollBar.h` 五个 include
随代码搬走（`grep -c` 只剩 include 行本身）。App.cpp 现在只剩 UI 构建 + `Run` + 应用接线。

#### 1e 已完成（2026-10-25）——Stage 1 收尾：App.cpp 只剩应用接线

| 项 | 结果 |
|---|---|
| App.cpp | 2346 → **871 行**（累计 12269 → 871，**-93%**） |
| 新文件 | `AppUiChrome.cpp` 509 / `AppUiMain.cpp` 1048（`BuildMainArea` 980 行整体搬） |
| App.cpp 剩余 | 10 个函数：`Run`(127) / 构造 / 4 个 `HarnessApplication::*` / `HandleGlobalKeyDown` / `OnWindowResized` / `SetSidebarVisible` / `ToggleInspector` |
| 门 1 | 零 error、零新增 warning |
| 门 2 | PASS：279 一致 / 0 变 / 0 丢 |
| 门 3 | PASS（regress 全绿 + GL 抽测 `backend=gl` 8/19/43） |
| 门 4 | 7 份摘要 IDENTICAL |

**Stage 1 到此结束**：`App.cpp` 从 12269 行降到 871 行，全仓最大文件是 `ProbeSidebar.cpp` 1233 行
（`AppUiMain.cpp` 1048 次之），中位数 ≈700。

**1e 遇到的两个真问题（值得记，Stage 2 会再碰）**：

1. **默认实参只能写一次**。`AddLine` 原本把默认值写在定义处（App.cpp 里只有定义）。
   上提到 `UiCommon.cpp` + 声明进 `AppInternal.h` 之后，两边都带默认值 ⇒ C2572 重定义。
   解法：**默认值只留在定义处**，头文件声明故意不带。理由不只是 C++ 规则——门 2 要求
   定义行逐字节可比，签名里少一个 `= false` 就算"变了"。所有调用点本来就传满 6 个参数。
2. **一次算好所有搬运区间，最后统一倒序删除**。第一版脚本先删 `HarnessRootLayout` 块
   （144–205 行），再用**原始行号**删 206/214 两处命名空间大括号 ⇒ 索引整体上移 62 行，
   把 `OnKeyDown` 里的两行削掉了。门 2 立刻报 `OnKeyDown 18 → 16 MISMATCH`——
   这是本轮唯一一次门 2 抓到真事故，而它抓的正是"搬运时削掉了代码"这类最容易漏的问题。
   教训：**区间删除必须一次性倒序执行**，不能分两趟。

**include 减法**：四个 `Layout/*.h` + `ThemedCaptionBar.h` + `<set>` 随代码搬走。
App.cpp 的注释里写清了"若某天又想在这里建控件，先想想该不该去 AppUiChrome / AppUiMain"。

### Stage 2 — `BuildMainArea` 内部拆解（唯一要动函数体的一步）✅ 已完成

**实测结果（2026-10-25）**：980 行 → **7 个 builder + 36 行编排**。文件 1048 → 1090 行。

| 新函数 | 行数 | 内容 |
|---|---:|---|
| `BuildMainArea` | **36** | 只剩装配顺序 + 三处显隐投影 |
| `BuildSidebar` | 158 | 工作区 / 会话列表 / 回放进度 / 历史 / 身份行 |
| `BuildConversationHead` | 100 | 标题 + 元信息 + 三个操作钮（导出/检查器/更多） |
| `BuildMoreMenu` | 103 | `···` 内联下拉（导出/设置/归档/清空视图） |
| `BuildSettingsSheet` | 283 | 设置面板内容体（**返回所有权**，见下） |
| `BuildTranscriptViewport` | 117 | ScrollView + 五个浮层（跳转钮/遮罩/设置 sheet/模型浮窗/tooltip） |
| `BuildComposer` | 28 | 输入条 |
| `BuildInspector` | 175 | 五张白卡（渲染探针/模型/用量/协议健康/权限历史） |

**与原计划的 6 个不同：拆成了 7 个。** 原因是设置面板**必须先建后挂**——它要当视口浮层，
而视口（`m_scroll`）在它之后才建。原计划把"设置面板"和"模型浮窗/tooltip/遮罩"合进一个
`BuildSettingsOverlay`，但那四者的挂载点在视口内，只有面板本体是在视口之前建的。
硬合会让"返回值"跨两段传递，比拆开更绕。`BuildSettingsSheet` 返回 `unique_ptr` 正好表达
「建好了但还没挂」这个中间态。

**改了什么（全量清单，只有这四处）**：
1. `counting->AddChild(...)` → `m_counting->AddChild(...)`（局部句柄 → 成员句柄，同一对象）
2. `main->AddChild(...)` → `main.AddChild(...)`（`main` 成了引用参数）
3. `settingsSheetOwned` → 函数返回值 / 入参（原代码里它就是"暂存"语义）
4. 每个分区的注释块随分区一起搬（`// ──` 分节注释是切分依据，也是给读者的地图）

**其余一字未动，含缩进**——所以 diff 里能一眼看出只有上面这几处。

#### 门 2 在 Stage 2 失效，替代判据是什么

`BuildMainArea` 的函数体真的变了，门 2 必然报 `变了 1 / 新增 7`——这不是失败，是这个阶段
的正常指纹。所以必须换一条**能证明"行为没变"**的判据：

**`tools/build_order_check.py`（本轮新增）**。做法：把新文件里对同文件成员函数的调用
**在调用点原地展开**，重建出**执行顺序**，再与拆前的线性函数逐行比对。文件里的书写顺序
不算数（编排函数排在最前，与执行顺序无关），只有执行顺序算数。

```
  python tools/build_order_check.py --before .scratch/AppUiMain.before.cpp \
         --after gui/src/AppUiMain.cpp --entry BuildMainArea
  → 树结构（AddChild/RemoveChild）  一致（83 条，逐行相同）
  → 句柄写入（m_hud.* = ....get()） 一致（34 条，逐行相同）
```

两条契约各自对应一类事故：
- **树结构顺序** = 父子关系 = 命中测试顺序 = 绘制层序。顺序错了，浮层就压不住、点击会穿透。
- **句柄写入顺序**：`m_hud.xxx = ptr.get()` 必须在 `AddChild` **之前**——AddChild 转移所有权，
  反过来写就是悬空指针。这条比树结构更隐蔽（编译不报错，运行时才炸）。

### Stage 3 — 头文件瘦身（紧接 Stage 1，解决"改头文件重编 20 TU"）

1. `CliOptions` → `CliOptions.h`（259 行搬走）。`main.cpp` 改成只 include 它 + `App.h`，不再吃控件链。
2. 20 个 `*ProbeRec` → `ProbeTypes.h`，从类内移到命名空间作用域。类内声明改成 `std::vector<StateProbeRec>` 等（类型名少一层 `HarnessApp::` 前缀，只有 App.cpp 内部引用，无外部消费者）。
3. `App.h` 里把能前置声明的控件类型改前置声明（`ECDI::Button` `ECDI::Label` `ECDI::ScrollView` `ECDI::TextBox` `ECDI::ProgressBar` `ECDI::RingProgressBar` `ECDI::Panel`），指针成员不需要完整类型。**注意**：`App.h` 现在因为 override 签名需要 `WindowResizedEvent`/`KeyCode`/`KeyModifier` 的完整定义（第 18–28 行注释写明了原因），这几个不能动。

**Stage 3 后**：App.h 1995 → ≈900，且 App.cpp 侧不再因改一个探针开关而重编全部。

### Stage 4 — 仪器收口（可选，收益中，diff 噪声大）

1. `ProbeState.h`：把 501 处 `m_xxxProbe*` 成员收进 `struct ProbeState`，`HarnessApp` 里只留 `ProbeState p;`。
   机械替换：`sed -E 's/\bm_(state|hit|reasoning|rebuild|session|sidebar|answer|theme|history|menu|resize|hud|settings|enter|grow|jump|key|empty|model|select|delProv|workspace|ap|eval)Probe/m_probe_\1/g'` 后再整体前缀成 `p.`。
   **安全网是编译器**：漏改一处 = 编译错误，不是静默行为变化。但要排除 28 处 `m_probe`/`m_measureProbe`（ECDI 的 `PaintProbe*`，不是探针状态）。
2. `OnReplayTick` 的两段派发链合成一张表：`{&CliOptions::xProbe, &ProbeState::xDone, &HarnessApp::RunXProbe}`，一个循环替代 16 个 `if`。
3. `WriteSummary` 按探针切段：每段 `if (m_options.xProbe) { json += ... }` 变成 `AppendXProbeSummary(json)`，实现搬到对应 `Probe*.cpp`。**摘要字段顺序必须逐字节不变**（有基线依赖）。

### Stage 5 — 组件化（不建议现在做）

把 `HarnessApp` 拆成 `TranscriptView` / `SessionStore` / `SettingsController` / `HudPresenter` 四个对象。**当前不建议**：探针需要穿透访问所有这些对象的内部状态，拆完会引入大量 friend / accessor，且 145 个函数里 30 个是探针——先做 Stage 1–4 已经能拿到 80% 的可读性收益。等仪器真的独立成模块（Stage 4 完成）之后再评估。

---

## 4. 每阶段的验收门（四条，全部机读）

### 门 1：编译

```bash
cmake --build gui/build --config Release
```
零 error 零新增 warning。

### 门 2：函数体指纹全等（**这是纯搬运的唯一硬证明**）

写一个 `tools/split_verify.py`：对拆分前后的源码树，按函数名抓取函数体（从 `名字(` 到配对的花括号），算 sha256，输出两张表：

```
before: App.cpp:WriteSummary       sha=ab12...  1083 行
after : AppSummary.cpp:WriteSummary sha=ab12...  1083 行   ← 一致
```

判据：**函数名集合相同，且每个函数体 sha256 相同**。任何一处 `MISMATCH` 都说明搬运时改到了代码（含注释）。这一步比人眼 review 可靠得多，而且能证明"我没有顺手改东西"。

> 注意：Stage 2 会故意改变 `BuildMainArea` 的形态，所以门 2 在 Stage 2 只对**除 `BuildMainArea` 及其 6 个新函数之外**的函数生效；Stage 2 的等价性靠门 3 + 门 4。

### 门 3：全量回归

```bash
python tools/regress.py     # RESULT: PASS
```
13 个探针 + 6 夹具结构基线 + REQ-03 live A/B + `protocol/validate.py`。

**加一跑 GL 后端抽测**（2026-10-25 评审补充）：

```bash
gui/build/Release/harness_gui.exe --replay protocol/fixtures/demo-session.jsonl \
  --instant --theme light --backend gl --auto-close 20000 --out .scratch/_reg_gl.json
```

判据三条：① `backend == "gl"`（真的走了 GL，不是静默回退 GDI）；② `blocks/deltas/messages == 8/19/43`（与 GDI 同夹具一致）；③ 进程正常退出、无崩溃。

为什么值得多跑这一跑：`include` 集变化**理论上**不该影响渲染，但"理论上"在这仓不值钱——搬文件会改 TU 边界，而 `Theme.h` 的 `inline const Color&` 别名、`Render/BackendFactory` 的静态选择都跟 TU 有关。跑一次 20 秒，就能把"渲染路径没被搬运碰过"变成事实而不是假设。

### 门 4：摘要 diff

对 Stage 0 存的 6 份基线摘要，逐 key 比对。**排除 volatile 键**（`frameCount` `paints` `maxPaintUs` `avgPaintUs`——它们随 caret 闪烁 timer 相位浮动，同二进制 3 跑可差 ±2~4，见 2026-10-25 实测记录）。其余键必须**完全相等**，包括探针数组里每个 `matched` 与 `note`。
**⚠ `invalidations` 不在排除清单里——它是稳定锚**（demo 恒 8 / cot 恒 20，整个 P4 期间逐位不变；P4.C 曾因多打两次 RequestRepaint 被它抓过 8→10）。纯搬运若让它漂移，说明 RequestRepaint/投影链路被改坏——这正是门 4 要抓的事故。（2026-10-25 评审修正：初版把 invalidations 误列为 volatile。）

**1a 实测补一条（2026-10-25，别当成噪声忽略）**：`--auto-close 40000` 这种长窗口下，
`invalidations` 会**偶发 ±1**。本次基线抓到 demo=9，而拆分后连跑 4 次全是 8；
为判定归属，另建 HEAD（拆分前）的 worktree 编了一份二进制连跑 3 次——**也全是 8**。
⇒ 基线那次 9 是尾 tick 抖动（40 秒里 caret 闪烁定时器的最后一拍落在退出边界上），**不是回归**。

判定协议（遇到 invalidations 不一致时按这个走，不许直接改基线）：

```bash
# 1. 拆分后的二进制连跑 3 次，看是否稳定
# 2. 若稳定在某个值 v，用 HEAD（拆分前）建 worktree 编一份，同样连跑 3 次
git worktree add .scratch/pre-split <拆分前的 commit>
cd .scratch/pre-split && cmake -S gui -B gui/build -DECDI_PREFIX=<repo>/third_party/ecdi
cmake --build gui/build --config Release
# 3. 两边同值 ⇒ 基线抖动，改基线并在此处记一笔；不同值 ⇒ 真回归，回滚该 commit
```

★ 顺带确认了 volatile 清单的正确性：同一次实验里拆分前 `paints` 跑出 **112 / 93 / 95**，
拆分后 94——三跑差 ±19，`paints`/`frameCount`/`maxPaintUs`/`avgPaintUs` 确实不能当判据。

---

## 5. 风险与规避

| 风险 | 具体表现 | 规避 |
|---|---|---|
| **匿名 ns helper 跨 TU** | `ContentWidth` `PathToUtf8` `Utf8FromWidePath` `Pick` `HarnessRootLayout` 被探针代码用到（实测：`ContentWidth` 1 处、`PathToUtf8` 5 处、`Utf8FromWidePath` 2 处、`Pick` 2 处、`HarnessRootLayout` 1 处） | Stage 1 第一步就建 `AppInternal.h`，把这些 helper 的**定义**搬到 `UiCommon.cpp` / `PathUtil.cpp`，声明放头文件，包 `namespace harness::internal`。**不许**在每个新 .cpp 里复制一份（会 ODR 撞车或行为分叉） |
| **外部链接符号重复** | `ToolTitle`（4147）现在是**外部链接**的自由函数，定义在 App.cpp 里、无头声明。若搬进 `AppTranscript.cpp` 而别的 TU 也定义同名函数 → 链接期重复定义 | 搬的时候顺手包进 `namespace { }`（这正是它该有的链接性），或放进 `harness::internal` |
| **静态初始化顺序** | `Theme.h` 的 `inline const Color&` 引用别名绑到 `g_theme`；跨 TU 后若某文件在静态期读它 → 未定义顺序 | 现状已避开（都只在函数内用）。搬运时**不要**新造文件作用域的 `const Color&` 别名 |
| **`App.h` 被 20 个 TU 包含** | Stage 1 后改一次头文件重编 20 个 TU，比现在（2 个）慢 | 所以 Stage 3 必须紧跟 Stage 1；且 `App.h` 只放类声明，不放实现 |
| **构建系统** | `GLOB_RECURSE CONFIGURE_DEPENDS` 已就绪，新增 .cpp 自动入库 | 无需改 `CMakeLists.txt`。但新增文件后**必须**让 CMake 重新配置（构建时会自动，`CONFIGURE_DEPENDS` 负责）——首次构建若报"找不到符号"，先 `cmake -S gui -B gui/build` 再编 |
| **编码/行尾** | precommit hook 查 BOM 漂移 | 新文件无 BOM + LF（仓库 129 个文件全是 LF，`.gitattributes` 钉了 `eol=lf`） |
| **探针的仪器污染** | 探针跑批会改 paints 相位（App.h:116 有实测记录） | 拆分期间**不新增**任何探针调用；`RefreshHud` 里的只读查询保持原样 |
| **回滚成本** | 一次搬 145 个函数，出错难定位 | 按 §3 Stage 1 的 5 个子步骤提交（探针 → 摘要 → 外围 → 核心 → UI），每步一 commit。任何一步门 2 红了就 `git revert` 那一个 commit |
| **`using harness::` 声明链（2026-10-25 评审补充）** | App.cpp 文件级有 20+ 个 `using harness::kDarkInk/kPanel2/kLine/...` 颜色别名——探针/设置/UI 代码里**裸用**这些短名。搬到新 TU 后全部编译错 | 每个新 .cpp 在 include 区补同一组 `using harness::xxx;`（只补用到的）。这是「只搬不改」原则下唯一合法的成批新增行；别改成 `harness::` 前缀（那就是改函数体了，门 2 会红） |
| **文件级 `static`/匿名函数要跟随（评审补充）** | `NowHm()`（MarkAnswered 用，static）、`kPinThreshold`/`kJumpBtnW`/`kTooltipInk` 等匿名 ns 常量（jump/key/select 探针用）——跨 TU 不可见 | 搬目标函数时，其依赖的 static/匿名符号要么同文件跟随、要么进 `AppInternal.h` 共享。搬运前先 grep 一遍函数体里的非成员符号 |
| **BuildMainArea 浮层段共享局部 `scroll`（评审补充，Stage 2）** | jump 按钮/模态遮罩/设置 sheet/模型浮窗/tooltip 五段全部 `AddChild` 到同一个局部 `scroll` 变量（z 序 = 挂载顺序，探针有判据）；`settingsSheetOwned` 也是跨段交接 | 6 个 builder 的签名要显式传 `ECDI::ScrollView& scroll`（或浮层段统一由组合段挂载）；root 转移段（mask/sheet 从 scroll 挪 root + `Exempt` 注册）在 `BuildUi` 尾部、不在 BuildMainArea——别一起搬 |

---

## 6. 明确不做的事

1. **不删探针代码**。它们是 `evals/` 84 份基线 + `tools/regress.py` 的判据来源，是交付物。
2. **不改逻辑**。拆分期间发现的 bug 单独记，拆完再修（否则无法区分"搬运引入"和"修 bug 引入"）。
3. **不重命名**（`m_` 前缀、PascalCase、`Run*Probe` 一律保持）。Stage 4 的 `p.` 前缀是唯一例外，且有编译器兜底。
4. **不建子目录**（`src/app/` `src/probe/`）。`#include "App.h"` 的相对解析会变复杂，而平铺 + 前缀命名已经足够表达归属。
5. **不动 `BuildMainArea` 的控件创建顺序**。控件树顺序 = 绘制顺序 = 命中测试顺序，任何顺序变化都会改探针读数（尤其 `--menu-probe` 的 z 序判据）。

---

## 7. 建议的执行顺序与交付

| 阶段 | 内容 | 门 | 产出 |
|---|---|---|---|
| 0 | 提交未提交改动 + 基线 | regress 全绿 + 6 份摘要快照 | 干净的起点 |
| 1a | 5 个 `Probe*.cpp` | 门 1+2+3+4 | App.cpp 12333 → 7800 |
| 1b | `AppSummary.cpp` | 同上 | → 6700 |
| 1c | 导出/历史/工作区/设置 | 同上 | → 5404 ✅ |
| 1d | 会话/转录/流式/HUD | 同上 | → 2346 ✅ |
| 1e | `AppUiChrome.cpp` / `AppUiMain.cpp` | 同上 | App.cpp 871 ✅ |
| 2 | `BuildMainArea` 7 分解 | 门 1+3+4+树序 | 最大文件 ≤1330 ✅ |
| 3 | `CliOptions.h` / `ProbeTypes.h` / 前置声明 | 门 1+3 | App.h ≈900 |
| 4 | `ProbeState` + 派发表 + 摘要分段（可选） | 门 1+2+3+4 | App.h ≈700，摘要与探针同址 |
| 5 | 组件化（不建议现在） | — | — |

**做完 1a–1e + 2 + 3**，收益已经到位：没有任何文件超过 1400 行，仪器与产品代码物理分离，改一个探针不用碰产品文件，改头文件不用重编全部。

---

## 8. 已知副作用（接受，不处理）

**行号引用会全部漂移。** 本文 §0/§2/§3 里的所有行号、以及 `docs/P4-gui-usability-plan.md` 的"关键代码地图"、历史 commit message 里引用的 `App.cpp:NNNN`，在 Stage 1 落地后**全部失效**——文件被切开，行号不再有任何意义。

**处理方式：记一笔，不留映射表。**

理由：映射表本身会立刻过期（Stage 2 又切一次 `BuildMainArea`，Stage 3 又改头文件），维护成本高于收益。而且这些引用本来就只有两种用途——① 定位代码，② 佐证"当时确实读了这段"。前者靠函数名 grep 更快（本仓的函数名都是唯一的、语义化的，`grep -n "HarnessApp::WriteSummary"` 一秒定位），后者是历史记录，本来就该冻结在它写下时的那一刻。

⇒ **今后写代码引用一律用「文件名 + 函数名」，不再写行号**（如 `AppSummary.cpp: WriteSummary`，而不是 `App.cpp:11252`）。本文档作为拆分前的快照保留行号，不再更新。

---

**下一步**：Stage 0 已执行（见 §3 与 commit 记录），从 1a 开始。
