# App.cpp 拆分交接文档（2026-10-25）

> **给接手的人**：这份文档只讲「现在在哪、下一步怎么走、哪些坑已经踩过」。
> 为什么拆、拆成什么样、体检数据，在 [`App-split-plan.md`](App-split-plan.md)，不重复。
> 读法：§0 看现状 → §4/§5 记住硬约束 → §7 直接开干 Stage 3。

---

## 0. 三十秒现状

| 项 | 值 |
|---|---|
| HEAD | `f775a5e`（Stage 3 刀②），工作区**干净** |
| `App.cpp` | 12,269 → **869 行**（-93%），只剩 10 个应用接线函数 |
| `App.h` | 2,009 → **1,477 行**（刀① `CliOptions` 259 行 + 刀② 探针结构体 276 行）；刀③ 未做 |
| 最大的文件 | `ProbeSidebar.cpp` 1233 / `AppSummary.cpp` 1114 / `AppUiMain.cpp` 1089 |
| 四条验收门 | 全绿；门 2 基线已换成 `fp-stage3.json`（286/286，见 §3） |
| Stage 2 | `BuildMainArea` 980 行 → 36 行装配 + 7 个 builder，树序 83 条 / 句柄写入 34 条校验一致 |

**一句话**：产品代码和仪器代码已经物理分开，单函数过大问题已解，头文件瘦身完成两刀（`CliOptions` + 探针结构体已出），**只剩刀③（控件前置声明）**。
**★ 但「目标 ≈900 行」靠刀③ 达不到**：实测剩余体积大半是探针的「为什么必须有」注释块 +
`m_xxxProbeLog` 成员声明（85 行），那属 Stage 4，见 §7.5。

---

## 1. 文件清单（`wc -l` 实测，`gui/src/`）

### 产品代码

| 文件 | 行 | 装什么 |
|---|---|---|
| `App.cpp` | 869 | **只剩接线**：`Run` / 构造析构 / `OnTimer` / 事件转发 / 侧栏显隐 / `HandleGlobalKeyDown` |
| `AppUiMain.cpp` | 1089 | `BuildMainArea`(36) + 7 个分区 builder（侧栏/会话头/更多菜单/设置面板/转录视口/Composer/检查器） |
| `AppUiChrome.cpp` | 509 | `BuildUi` / `RebuildUi` / `BuildTopBar` / `BuildBottom` / `HarnessRootLayout` |
| `AppTranscript.cpp` | 861 | 转录行构建、增量追加 |
| `AppHud.cpp` | 857 | 顶栏 HUD、用量、协议健康 |
| `AppStream.cpp` | 817 | 回放行消费、delta 合并 |
| `AppSession.cpp` | 770 | 会话切换、回放控制 |
| `AppSettings.cpp` | 565 | 设置面板读写、凭据 |
| `AppWorkspace.cpp` | 362 | 工作区列表 |
| `AppHistory.cpp` | 355 | 历史记录 |
| `AppExport.cpp` | 198 | 导出 |

### 仪器（探针，与产品代码物理隔离）

| 文件 | 行 | 装什么 |
|---|---|---|
| `ProbeSidebar.cpp` | 1233 | 侧栏对比探针（最大单文件，**唯一还超 1200 的**） |
| `AppSummary.cpp` | 1114 | `WriteSummary` —— 单函数 1082 行，见 §8 |
| `ProbeChrome.cpp` | 977 | 主题/重建/菜单/尺寸探针 |
| `ProbeTranscript.cpp` | 748 | 转录相关探针 |
| `ProbeFlow.cpp` | 730 | 会话/回放/历史探针 |
| `ProbeSettings.cpp` | 729 | 设置/模型/删除供应商探针 |
| `ProbeSupport.cpp` | 366 | 探针公共支撑 |

### 头文件与新增前置

| 文件 | 行 | 说明 |
|---|---|---|
| `App.h` | 1477 | 应用组装声明；刀①② 之后不含 `CliOptions` 与探针结构体 |
| `CliOptions.h` | 273 | **Stage 3 刀①**：命令行选项 47 字段（含 26 个探针开关），逐字节从 `App.h` 搬出 |
| `ProbeTypes.h` | 318 | **Stage 3 刀②**：24 个探针读数结构体（命名空间作用域） |
| `AppInternal.h` | 119 | 跨 TU 共享声明：路径工具、几何常量、探针色、`Pick()`、5 个 UI 助手 |
| `UiCommon.cpp` | 127 | `ContentWidth` / `ConversationTitle` / `ConversationSubtitle` / `UnknownKindText` / `ReasoningTitle` / `AddLine` |
| `ThemeUsing.h` | 66 | 52 个主题短名 `using harness::kXxx;` |
| `PathUtil.cpp` | — | `Utf8FromWidePath` / `WideFromUtf8Path` / `PathToUtf8` |

---

## 2. 提交历史（每一步都过了门，按序）

| commit | 做了什么 | 验收 |
|---|---|---|
| `108d1d4` | 方案文档 | — |
| `e245999` | 前置：`ThemeUsing.h` / `AppInternal.h` / `PathUtil.cpp` / `split_verify.py` | 门 1+2（279/279） |
| `ab8c87a` | **1a** 30 个 `Run*Probe` → 6 个 `Probe*.cpp` | 四门全绿 |
| `1d7c6c1` | **1b** `WriteSummary` → `AppSummary.cpp` | 四门全绿 |
| `42b592b` | **1c** 导出/历史/工作区/设置 → 4 文件（47 函数） | 四门全绿 |
| `d2a0863` | **1d** 会话/转录/流式/HUD → 4 文件（59 函数） | 四门全绿 |
| `a8c1792` | **1e** UI 构建 → `AppUiChrome` + `AppUiMain`，Stage 1 收尾 | 四门全绿 + GL 抽测 |
| `2fbe612` | **Stage 2** `BuildMainArea` 切 7 个 builder | 门 1+3+4 + `build_order_check` |
| `b705a7e` | 本文档（交接） | — |
| `ac9b4c9` | **Stage 3 刀①** `CliOptions` → `CliOptions.h` | 门 1+2+3+4（结构体不进函数指纹，天然安全） |
| `f775a5e` | **Stage 3 刀②** 24 个探针结构体 → `ProbeTypes.h` | 门 1+3+4 + 逐行等价 + 3 行 diff（门 2 报 3 条返回类型 MISMATCH，归一后全等） |

> 历史 commit message 与 `docs/P4*.md` 里的**行号引用已全部漂移**，这是接受的副作用
> （见 plan §8），不要去建映射表。定位代码一律用「文件名 + 函数名」。

---

## 3. 验收工具链

全部在 `tools/`，全部机读，不靠人眼。

### 门 1 — 编译

```bash
unset http_proxy https_proxy      # ★ 见 §4，不 unset 必卡
cmake --build gui/build --config Release
```
判据：**0 error，0 新增 warning**。已知既有警告 `Credentials.cpp` C4244，与拆分无关，别顺手修。

### 门 2 — 函数体指纹（纯搬运的唯一硬证明）

```bash
python tools/split_verify.py --save .scratch/split-baseline/fp-stage2.json   # 拆前
python tools/split_verify.py --check .scratch/split-baseline/fp-stage2.json  # 拆后
```
判据：`MISMATCH` / `MISSING` 判红；**新增函数名只提示不判红**（Stage 2 会新增 builder）。
指纹 = 定义首行到配对右花括号的**原文**（含注释、含缩进），所以「只搬不改」连注释都要求一字不动。

> ★ 2026-10-25：基线已换 `fp-stage3.json`（286 个函数，刀② 之后重建）。
> **基线是「当前状态的锚」，不是历史档案**——每落一刀就 `--save` 一次，下次才有可比对象。
> 旧的 `fp-before.json` 是 **Stage 2 之前**存的，用它跑必然报
> `BuildMainArea 980 → 36 MISMATCH` + 7 个 NEW —— 那是 Stage 2 的既定成果，不是回归。
> **别拿旧基线判红，也别去"修"那个 MISMATCH。**

**只在纯搬运时有效。** 一旦动函数体（Stage 2 那种），门 2 失效，换 `build_order_check.py`。

### 门 3 — 全量回归

```bash
python tools/regress.py        # 无参数，约 6 分钟
```
6 夹具基线 + 13 探针 + REQ-03 live A/B + `protocol/validate.py`。
注意：探针的 `negative` 相**设计上就该红**，单独列出；`core/bin/fake-core.exe` 缺失时 2b 节
报 **SKIP 不是 PASS**——看到 SKIP 要知道那是「没测」。

GL 后端抽测（include 集变了理论上不影响渲染，跑一次就证完）：
```bash
# 与 .scratch/split-baseline/gl-demo.json 比对
gui/build/Release/harness_gui.exe --gl --out .scratch/gl-demo-after.json ...
python tools/summary_diff.py .scratch/split-baseline/gl-demo.json .scratch/gl-demo-after.json
```

### 门 4 — 摘要 diff

```bash
python tools/summary_diff.py .scratch/split-baseline/demo.json .scratch/demo-after.json
```
判据：除 volatile 键外**逐字节相同（含字段顺序）**。
volatile 白名单只有 4 个：`frameCount` / `paints` / `maxPaintUs` / `avgPaintUs`。
**`invalidations` 不在白名单里**——它是稳定锚（demo 恒 8 / cot 恒 20），漂移就是重绘链路被改坏。

基线文件在 `.scratch/split-baseline/`：`demo` / `realistic` / `cot` / `scale` /
`hostile-seq-gap` / `hostile-truncated` / `gl-demo` + `fp-before.json`。

### 门 2 的替代 — 执行顺序校验（Stage 2 类改动）

```bash
python tools/build_order_check.py --before <拆前快照>.cpp --after <拆后>.cpp --entry BuildMainArea
```
把同文件成员调用**原地展开**还原成执行顺序，逐行比对。写代码的顺序无所谓，**执行顺序才是契约**。
校验两条契约：
1. **树序** — `AddChild` / `RemoveChild` 的先后 = 父子关系 = 命中测试顺序 = 绘制 z 序；
2. **句柄写入序** — `m_hud.xxx = ptr.get()` 必须在 `AddChild` **之前**（`AddChild` 会转移所有权，
   反过来写 = 悬垂指针，编译能过，运行必崩）。

### 提交前

```bash
python tools/precommit-check.py     # 查 BOM 漂移、编码
```
新文件一律**无 BOM + LF**（`.gitattributes` 钉 `eol=lf`）。

---

## 4. 环境陷阱（不照做一定卡，且报错信息完全不指向真因）

```bash
unset http_proxy https_proxy
```

本机**同时存在大写和小写两套代理变量**。MSBuild 的 `ProcessStartInfo.EnvironmentVariables`
字典是**大小写不敏感**的 ⇒ 报 `MSB6001 已添加项…关键字"HTTPS_PROXY"`，CL.exe 根本起不来。
报错看起来像 CMake 配置问题，实际是环境变量重复。大写变量保留无妨，只 unset 小写即可。

其他：
- 生成器是 **Visual Studio 18 2026，只支持 Release**（CMakeLists 在配置期和链接期两处拦 Debug）。
- `gui/CMakeLists.txt` 用 `GLOB_RECURSE CONFIGURE_DEPENDS` ⇒ **新增 .cpp 不用改 CMake**。
- 新 TU 常见缺 include：`ECDI::PlatformWindow` 只被前置声明（要 `ECDI/Platform/PlatformWindow.h`）；
  `ECDI::ListLayout`（`ECDI/Layout/ListLayout.h`）；`ECDI::Logger`（`ECDI/Core/Logger.h`）；
  `creds::` 是命名空间不是类（`Credentials.h`）。

---

## 5. 三条规则（都是踩出来的，违反必返工）

### 5.1 函数定义必须留在**列 0**

搬进 `namespace` 时**不要缩进函数体**。本仓风格本来就是成员定义和匿名 ns 自由函数都顶格写。
缩进一改，门 2 的 sha256 就变，`279/279` 立刻变成 `279 MISMATCH` —— 而代码其实完全等价。
这是「机读证明纯搬运」的前提。

### 5.2 默认实参**只写在定义处**

```cpp
// AppInternal.h —— 故意不带默认值
ECDI::Label* AddLine(ECDI::Panel& parent, const std::string& text,
                     const ECDI::Color& color, bool heading, int width,
                     std::optional<float> fontSize);
```
两个原因叠加：① C++ 规定默认实参只能出现一次；② 门 2 要求定义行逐字节可比，
签名里多一个 `= false` 就算「变了」。所有调用点显式传满参数。

### 5.3 区间删除必须**一趟逆序**

Stage 1e 出过一次真实事故：先删 144–205，再用**原始**行号删 206/214 ——
第一次删除后整体上移 62 行，那两个下标落进了 `OnKeyDown` 里，**静默删掉两行**。
`门 2 抓到：OnKeyDown 18 → 16 MISMATCH`。

正确做法：**先把所有区间算完，再按行号从大到小一趟删完**。永远不要两趟。

---

## 6. `invalidations` 抖动协议

`--auto-close 40000` 下它可能 ±1（尾 tick 相位）。**遇到差异先别改基线**：

1. 拆分后的二进制连跑 3 次，看是否稳定在某个值 v；
2. 若稳定，用 HEAD（拆分前）建 `git worktree` 编一份，同样连跑 3 次；
3. 两边同值 ⇒ 基线抖动，改基线并在此处记一笔；不同值 ⇒ **真回归，回滚该 commit**。

1b 就是这么查的：基线记 9，拆分后恒 8，worktree 建的原版也恒 8 ⇒ 基线错，改成 8。
同一次实验还看到 `paints` 在 112/93/95 之间跳，反过来证明了 volatile 白名单是对的。

---

## 7. Stage 3：头文件瘦身（刀①② 已完成，只剩刀③）

`App.h` 2009 行 → **1477 行**（刀① -259 / 刀② -276）。

> ★ **「约 1100 行是仪器声明」这个估算偏高，别再拿它当目标依据**：把探针的 struct body
> 全部搬走只砍了 276 行。剩下的大头是每条探针上方的「为什么必须有」注释块 +
> `m_xxxProbeLog` 成员声明 + `Run*Probe()` 声明 —— 那些属 Stage 4（§8），不在 Stage 3。
> **所以「目标 ≈900 行」靠刀①②③ 达不到，1477 → 约 1150 就是 Stage 3 的真实天花板。**

### 7.1 实测行号地图

| 位置 | 内容 |
|---|---|
| 1–46 | includes + 三条「必须完整类型」的注释（**别动**，见 7.3） |
| **47–305** | `struct CliOptions` —— **47 字段，其中 26 个是探针开关** |
| 307–337 | `class HarnessApplication`（27 行） |
| 339–2009 | `class HarnessApp`（1670 行） |
| ├ 393 | `struct Hud` |
| ├ 587 / 618 / 668 / 675 / 709 / 715 / 1029 | `TimelineEntry` / `RowRec` / `PendingPrompt` / `PendingPromptView` / `Workspace` / `Session` / `SidebarCompare` |
| └ **1347–1967** | **24 个探针结构体**（22 个 `*ProbeRec` + `UiSnapshot` 1567 + `ReplayDigest` 1630） |

`CliOptions` 字段类型分布：33 `bool` / 8 `std::string` / 4 `path` / 2 `int`。
26 个探针开关是：`cullProbe` `settingsProbe` `jumpProbe` `keyProbe` `emptyProbe` `modelProbe`
`selectProbe` `delProvProbe` `probeContrast` `stateProbe` `hitProbe` `reasoningProbe` `orderProbe`
`rebuildProbe` `sessionProbe` `sidebarProbe` `answerProbe` `themeProbe` `historyProbe`
`workspaceProbe` `menuProbe` `resizeProbe` `enterProbe` `growProbe` `hudProbe` `asyncProbe`。
（`selectableRows` 是产品开关，别归进探针。）

### 7.2 三刀，按这个顺序

**① `CliOptions.h`** —— ✅ **已完成（`ac9b4c9`）**：47–305 整段搬出，`App.h` 只 `#include "CliOptions.h"`。
- 依赖只有 `<string>` `<filesystem>`，干净。
- 搬法：**脚本切片不手抄**（`.scratch/cut1_clioptions.py`），struct 体 sha256 前后同为
  `42056bb5e22d8725` ⇒ 259 行连同注释逐字节一致。
- `main.cpp` 已显式加 `#include "CliOptions.h"`，不再靠传递包含。
- ★ **原计划里「收益最大的一刀」这句话不成立，别照抄**：`main.cpp` 仍要
  `HarnessApp app(...)` 因而仍包 `App.h`，19 个 TU 一个没少。本刀的真实收益是
  `App.h` 少 259 行 + `CliOptions` 的 47 个字段不必先拿到 1670 行的类定义才能读。
  **重编收益要等刀③（控件前置声明）才可能兑现。**
- 门 2 对本刀天然安全：结构体不进函数指纹，指纹数仍是 286 且逐项不变。

**② `ProbeTypes.h`** —— ✅ **已完成（`f775a5e`）**：24 个结构体搬到命名空间作用域，`App.h` 1752 → 1477。
- 这是唯一有真实机械风险的一步：类内 `struct X` 提到外面后，类内引用同名可见不用改，
  但**类外**的 `HarnessApp::X` 引用必须改。实测外部引用只有 **5 处**，其中 2 处是
  `SidebarCompare`（不在本刀范围），真正要改的只有 3 处，全在 `ProbeSupport.cpp`：
  `SnapshotUi` / `CompareRows` / `DigestReplayFile` 的**返回类型**。
- 搬法：脚本切片（`.scratch/cut2_probetypes.py`），一趟逆序删除（§5.3）。
- **门 2 会报 3 条 MISMATCH，那是预期的**：改的是列 0 函数定义行的返回类型限定符。
  判据换成「归一化后 sha256 全等」——把 `HarnessApp::(UiSnapshot|RebuildProbeRec|ReplayDigest)`
  换成裸名再比，三个函数体全等；`git diff ProbeSupport.cpp` 也恰好只有 3 行。
- 另两条判据：逐行 lstrip 全等（只允许少 4 空格缩进）+ 门 4 摘要 `IDENTICAL`
  （摘要键序 = 字段声明序，字段搬乱必红）。
- ★ **没有分 4 批搬**：原文「一次只搬 5~6 个」是按「结构体很大」估的，实测这 24 个
  body 合计只有 276 行、平均 11.5 行纯数据。分批只缩小出错半径，而逐行等价证明
  + 一次全量回归给出的是更强的证据（也省下 3 次 6 分钟）。
  **如果下次要搬的是带成员函数/带相互引用的类型，就回到分批。**

**③ 成员指针类型前置声明** —— `App.h` 里 `ECDI::Label*` / `Panel*` / `ScrollView*` / `Button*` 等
改成前置声明，把 `#include "ECDI/Widget/*.h"` 从 `App.h` 移到需要的 `.cpp`。
- 需要先确认哪些成员函数是**内联在头文件里**的（内联函数体用到完整类型就不能只前置声明）。
- 量：`App.h` 现在 include 了 8 个 `ECDI/Widget/*.h` + `ECDI/Application/Application.h`。

### 7.3 已知约束：三个头**不能**降级成前置声明

`App.h` 18–28 行的注释已经写明，别去「优化」：

| 头 | 为什么必须完整 |
|---|---|
| `ECDI/EventSystem/Window/WindowResizedEvent.h` | `OnWindowResized` 的 `override` 要在派生类里比对基类签名，且 `event.GetWidth()` 要有定义 |
| `ECDI/EventSystem/Input/KeyBoard/KeyCode.h` | `SynthesizeKey(ECDI::KeyCode, ...)` **按值传参**，枚举无法前置声明 |
| `ECDI/EventSystem/Input/KeyBoard/KeyModifier.h` | 同上，`SynthesizeKey` 第三参 + `HandleGlobalKeyDown` 形参 |

### 7.4 为什么 Stage 3 必须紧接 Stage 1

拆分前 `App.h` 只被 2 个 TU 包含，改它重编 2 个文件；现在被 **20+ 个 TU** 包含，
改一行私有成员就重编 20+。**拆分把「改头文件」的代价放大了 10 倍**，Stage 3 是还这笔债。
（顺带说明：编译时间从来不是拆分的理由，见 plan §0.7——这是拆分带来的**新**成本。）

### 7.5 刀③ 的量（接手前先算清，别指望它砍行数）

刀③ 的收益**不是行数，是重编半径**：`App.h` 现在 include 了 8 个 `ECDI/Widget/*.h`
+ `ECDI/Application/Application.h`，其中 7 个只用来写 `ECDI::Label*` / `Panel*` / `ScrollView*`
这类**指针成员**（共 72 处）——指针成员只需前置声明。

- 好消息：`App.h` 里几乎没有内联函数体（只有 2 处单行内联），所以**不存在**
  「内联函数用到完整类型因而不能降级」的阻碍。
- 坏消息：`App.h` 会被 **19 个 TU** 包含；把 include 摘掉后，凡是在 `.cpp` 里
  `new ECDI::Button(...)` / 调 `->SetText(...)` 的地方都得自己补 include。
  已实测各头被哪些文件用到（`grep -rl "ECDI/Widget/X.h"`）：
  `Button` 9 处 / `Label` 13 / `ScrollView` 9 / `ScrollBar` 7 / `TextBox` 9 /
  `CollapsiblePanel` 2 / `ProgressBar` 与 `RingProgressBar` **只有 `App.h` 用**。
- ⇒ 顺序建议：**先摘只有 App.h 用的两个**（`ProgressBar` / `RingProgressBar`，
  前置声明即可，零扩散），验证编译通过再动其它 6 个。
- 预期行数变化 ≈ -10（几个 include + 注释），**不是几百行**。别把它当瘦身刀，它是重编刀。

---

## 8. Stage 4 / 5：先别做

**Stage 4（仪器收口）**：`m_*Probe*` 成员有 501 处引用，散在 `HarnessApp` 里。
收益是「探针状态集中」，但 diff 噪声极大（几乎每个产品函数都要改），
且必须在 Stage 3 之后做才有意义。可选，不阻塞。

**Stage 5（组件化）**：**明确不建议现在做**。探针需要**贯穿式访问**内部状态
（`SnapshotUi` 要比对 rebuild 前后的四路快照），组件化会把私有成员封起来，
探针只能靠友元或额外接口，等于用新耦合换旧耦合。除非决定「砍掉探针」，否则不做。

---

## 9. 接手冒烟清单（先跑这个，确认环境没坏）

```bash
cd /d/Project/ECDI_fake_harness
git status --short                     # 应为空
unset http_proxy https_proxy
cmake --build gui/build --config Release        # 门 1，约 11s 增量
python tools/split_verify.py --check .scratch/split-baseline/fp-stage3.json   # 门 2，应 286/286
python tools/regress.py                          # 门 3，约 6 分钟
```

四条都对得上，再动 Stage 3。**任何一条不对，先查环境（§4），别怀疑代码。**

---

## 10. 文档索引

| 文件 | 讲什么 |
|---|---|
| `docs/App-split-plan.md` | 体检数据、拆分原则、目标布局、分阶段计划、风险表、各阶段实测结果 |
| `docs/App-split-handover.md` | 本文档：现状、工具链、坑、下一步 |
| `tools/split_verify.py` | 门 2，文件头注释写了设计取舍 |
| `tools/summary_diff.py` | 门 4，文件头注释写了 volatile 键的依据 |
| `tools/build_order_check.py` | Stage 2 类改动的替代判据，两条契约在文件头 |
| `tools/regress.py` | 门 3，判据说明在文件头 |
