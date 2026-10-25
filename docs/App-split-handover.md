# App.cpp 拆分交接文档（2026-10-25）

> **给接手的人**：这份文档只讲「现在在哪、下一步怎么走、哪些坑已经踩过」。
> 为什么拆、拆成什么样、体检数据，在 [`App-split-plan.md`](App-split-plan.md)，不重复。
> 读法：§0 看现状 → §4/§5 记住硬约束 → §7 直接开干 Stage 3。

---

## 0. 三十秒现状

| 项 | 值 |
|---|---|
| HEAD | 以 `git log -1` 为准，工作区**干净**。最后一个**功能**提交是 `b167580`（Stage 4 收尾），之后只有文档与工具 |
| `App.cpp` | 12,269 → **869 行**（-93%），只剩 10 个应用接线函数 |
| `App.h` | 2,009 → **1,422 行**（-29%）：刀① -259 / 刀② -276 / 刀③ +11 / Stage 4 收口 -93 + 摘要段声明 +27 |
| 最大的文件 | `ProbeSidebar.cpp` 1416 / `ProbeChrome.cpp` 1148 / `ProbeTranscript.cpp` 989 |
| 四条验收门 | 全绿；门 2 基线 `fp-stage4.json`（**310/310**，见 §3） |
| Stage 2 | `BuildMainArea` 980 行 → 36 行装配 + 7 个 builder，树序 83 条 / 句柄写入 34 条校验一致 |
| Stage 4 | 103 个探针成员 → `ProbeState.h`（**552 处** `m_probeState.*`）；`WriteSummary` 1082→304；派发表 19 行 |

**一句话**：**Stage 1 / 2 / 3 / 4 全部完成**。产品代码与仪器代码物理分开、单函数过大已解
（`WriteSummary` 1082→304、`BuildMainArea` 980→36）、头文件 2009→1422、仪器状态收成一个
具名句柄 `m_probeState`。**Stage 5（组件化）仍未做，且不建议做**（见 §8.2）。

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
| `AppStream.cpp` | 775 | 回放行消费、delta 合并、探针派发表（`kSettleSteps`） |
| `AppSession.cpp` | 770 | 会话切换、回放控制 |
| `AppSettings.cpp` | 565 | 设置面板读写、凭据 |
| `AppWorkspace.cpp` | 362 | 工作区列表 |
| `AppHistory.cpp` | 355 | 历史记录 |
| `AppExport.cpp` | 198 | 导出 |

### 仪器（探针，与产品代码物理隔离）

| 文件 | 行 | 装什么 |
|---|---|---|
| `ProbeSidebar.cpp` | 1233 | 侧栏对比探针（最大单文件，**唯一还超 1200 的**） |
| `AppSummary.cpp` | 336 | `WriteSummary` 304 行 = 摘要格式的**目录**（24 段实现在各 `Probe*.cpp`），见 §8.1 |
| `ProbeChrome.cpp` | 977 | 主题/重建/菜单/尺寸探针 |
| `ProbeTranscript.cpp` | 748 | 转录相关探针 |
| `ProbeFlow.cpp` | 730 | 会话/回放/历史探针 |
| `ProbeSettings.cpp` | 729 | 设置/模型/删除供应商探针 |
| `ProbeSupport.cpp` | 366 | 探针公共支撑 |

### 头文件与新增前置

| 文件 | 行 | 说明 |
|---|---|---|
| `App.h` | 1395 | 应用组装声明；Stage 3/4 之后不含 `CliOptions`、探针结构体、控件头与探针状态 |
| `CliOptions.h` | 273 | **Stage 3 刀①**：命令行选项 47 字段（含 26 个探针开关），逐字节从 `App.h` 搬出 |
| `ProbeTypes.h` | 318 | **Stage 3 刀②**：24 个探针读数结构体（命名空间作用域） |
| `ProbeState.h` | 178 | **Stage 4**：103 个探针状态成员；`HarnessApp` 只留 `ProbeState m_probeState;` |
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
| `160b808` | **Stage 3 刀③** 控件类型改前置声明 | 门 1+2+3+4 + GL 抽测；重编半径实测见 §7.2③ |
| `4528fb7` | **Stage 4** 103 个探针成员 → `ProbeState.h` | 门 1+3+4 + **改名后逐字节全等**（12 个 .cpp） |
| `b167580` | **Stage 4** 收尾：`WriteSummary` 切段 + 派发表 | 门 1+3×2+4 + 24 段逐行等价 + 23 份摘要对账 |
| `84abe0b` | 文档收口 + `tools/rename_verify.py` 入库 | 门 2 基线重建 310/310；只动文档与独立工具，未重编 |

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

> ★ 2026-10-25：基线已换 `fp-stage4.json`（**310** 个函数，Stage 4 两刀都落完后重建）。
> **基线是「当前状态的锚」，不是历史档案**——每落一刀就 `--save` 一次，下次才有可比对象。
> 旧的 `fp-before.json` 是 **Stage 2 之前**存的，用它跑必然报
> `BuildMainArea 980 → 36 MISMATCH` + 7 个 NEW —— 那是 Stage 2 的既定成果，不是回归。
> **别拿旧基线判红，也别去"修"那个 MISMATCH。**

### 门 2 的第二种替代 — 纯改名（Stage 4 那种）

函数体里全是改名字符串时，指纹必然变，且「变了」这个信号**没有信息量**。
替代判据是**逐文件正向映射后全等**：

```bash
# 映射文件：一行「旧名 新名」，# 开头为注释
python tools/rename_verify.py --map .scratch/stage4-rename-map.txt --ref af7e505
python tools/rename_verify.py --map m.txt --ref HEAD gui/src/App.cpp    # 也可只查单文件
```

输出 `RENAME-EQ` / `UNCHANGED`（未碰到，跳过）为绿，`DIFF` 判红并打印前 20 行 unified diff。
全等 ⇒ 除映射命中的标识符外一个字符都没动，**覆盖面比函数指纹更广**（成员声明、
初始化列表、注释、缩进全在范围内）。

工具自带两条前置检查，不满足就判红——因为那时「逐字节全等」这个证明**不成立**：
1. 旧名不得出现在**字符串字面量**里（字符串内容不会被映射，那里不等就是真的没改）。
   `--allow-literal` 可放行确属巧合的情况。字面量扫描会跳过反斜杠转义，
   `"a\\"` 后面的名字判在串**外**（曾用朴素引号计数把正常代码报成「名字在字符串里」）。
2. 映射不得链式（新名又当旧名），否则一次替换的结果依赖匹配顺序。

映射怎么来：`git show <ref>:gui/src/App.h` 里把成员声明逐行抽出来，套上「新容器 + 去前缀」
即可（Stage 4 的 103 行映射就是这么生成的，见 §8.1a）。**别手抄**，抄错一个名字工具会
安静地把它当「没改」放过。

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
`hostile-seq-gap` / `hostile-truncated` / `gl-demo` + **`fp-stage4.json`（现行指纹基线）**。
`fp-before` / `fp-stage2` / `fp-stage3` 只作历史存档，**不要拿来判红**（见 §3 门 2 的注）。

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

## 7. Stage 3：头文件瘦身（✅ 三刀全部完成）

`App.h` 2009 行 → **1488 行**（刀① -259 / 刀② -276 / 刀③ +11）；Stage 4 再收到 **1395**。

> ★ **「约 1100 行是仪器声明」这个估算偏高，别再拿它当目标依据**：把探针的 struct body
> 全部搬走只砍了 276 行。剩下的大头是每条探针上方的「为什么必须有」注释块 +
> `m_xxxProbeLog` 成员声明（85 行含探针成员）+ `Run*Probe()` 声明。
> 成员声明部分后来由 **Stage 4 收走（-93 行）**；注释块**故意留下**（见 §8.1）。
> **「目标 ≈900 行」始终没达到**，最终落点是 **1395**。

### 7.1 实测行号地图（**已过期，仅存档**）

> ⚠️ 下表是 **Stage 3 开工前**（App.h 2009 行）的行号。刀①②③ 之后全部漂移，
> 按仓库约定**不要建映射表**，定位一律用「文件名 + 函数名」。

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

**③ 成员指针类型前置声明** —— ✅ **已完成（`160b808`）**：摘掉 7 个 `ECDI/Widget/*.h`，
改成 8 条前置声明；9 个 `.cpp` 各自补 include。**App.h 1477 → 1488（+11 行）**。
- 依据（改前实测）：`ECDI::Label*` / `Button*` / `Panel*` / `ScrollView*` / `TextBox*`
  共 72 处**全是指针**；仅两处引用形参（`BuildSidebar` / `BuildInspector`）；App.h 无内联
  函数体（只有 2 处单行）⇒ 全部可降级。`ECDI::Widget` 不用声明，`Application.h` 已声明。
- ★ 顺手清掉一处**死 include**：`ScrollBar.h` 当初为 `RefreshJumpButton` 而包，而那个函数
  已在 Stage 1d 搬进 `AppHud.cpp` —— App.h 里 `ECDI::ScrollBar` 出现 **0 次**。
- ★★ **重编半径实测（touch 各控件头，数重编的 .cpp）：收益高度集中在冷门头**

| 头 | 改前 | 改后 | 变化 |
|---|---|---|---|
| `Button` | 20 | 20 | **±0**（用得太广，本来几乎每个 TU 都要） |
| `Label` | 21 | 21 | **±0**（同上） |
| `ScrollView` | 20 | 15 | -25% |
| `CollapsiblePanel` | 19 | 4 | -79% |
| `ProgressBar` | 19 | 2 | -89% |
| `RingProgressBar` | 19 | 2 | -89% |

  改前口径：任一控件头都被 `App.h` 传递包含 ⇒ 19 个 App.h TU ∪ 直接包含者。
  **结论：这刀是重编刀、不是瘦身刀**（行数净增 11）。真正省下的是冷门控件的改动成本；
  最常用的 `Button`/`Label` 一点没省，别再指望从这里挤出编译时间。

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

### 7.5 Stage 3 总账

| 刀 | commit | App.h | 判据 |
|---|---|---|---|
| 起点 | — | 2009 | — |
| ① `CliOptions.h` | `ac9b4c9` | 1752 | 结构体 sha256 前后同 `42056bb5e22d8725`；门 2 逐项不变 |
| ② `ProbeTypes.h` | `f775a5e` | 1477 | 逐行 lstrip 全等 + `git diff` 恰好 3 行 + 门 4 摘要全等 |
| ③ 前置声明 | `160b808` | **1488** | 门 2 286/286 + 门 4 + GL 抽测全等；重编半径见 §7.2③ |

**净结果：2009 → 1488（-26%）。原目标 900 达不到**，原因见 §7 顶部：
剩下的大头是探针注释块与 `m_*ProbeLog` 成员声明（85 行含探针成员）+ `Run*Probe()` 声明，
要动它们得做 Stage 4（501 处引用），那不属于 Stage 3。
**Stage 4 后来又砍了 93 行（1488 → 1395）**，但砍的是成员声明；探针注释块仍在 App.h
——那是**故意留的**（注释讲判据，跟着 `Run*Probe()` 走），所以 1395 就是这条线的终点。

**Stage 3 的三个真实结论**（三条都和原文档的预期不符，别照抄原文）：
1. 刀① 不产生重编收益（`main.cpp` 仍要构造 `HarnessApp` 因而仍包 App.h）。
2. 刀② 只砍 276 行，不是「约 1100 行仪器声明」。
3. 刀③ 是重编刀且行数**净增 11**；收益只在冷门控件头（`ProgressBar` -89%），
   常用头（`Button`/`Label`）**零收益**。

---

## 8. Stage 4（✅ 已完成）/ Stage 5（不建议做）

### 8.1a Stage 4：仪器状态收口（`4528fb7`）

**做了什么**：103 个探针状态成员从 `HarnessApp` 收进 `ProbeState.h`，类里只留
`ProbeState m_probeState;`。569 处引用变成 `m_probeState.xxx`（12 个 `.cpp` + `App.h`）。
`App.h` 1488 → **1395**（后因摘要段声明 +27，最终 **1422**）。

Stage 4 共三件（方案文档 §Stage 4 原文的三条）：**8.1a 状态收口 / 8.1b 摘要切段 / 8.1c 派发表**。

**范围判据**（写在 `ProbeState.h` 头注，三条，逐条 grep 核实）：

| 规则 | 内容 |
|---|---|
| A | 名字含 `Probe` 的成员（**`m_measureProbe` 除外**——ECDI 的测量对象，属产品侧 HUD 读数链） |
| B | `m_ap*` 家族（REQ-03 异步投递读数，名字没带 `Probe`） |
| C | 名字不带 `Probe`、但**读写点全在探针门控内**的草稿：`m_reasoningTitleStreaming` / rebuild 七个快照字段 / theme 三个 before 快照 / `m_hudAudit` / `m_hudTouched` |

**刻意不搬**（读写点在产品路径上，别顺手挪进来）：`m_measureProbe`、`m_uiRebuilds`
（`RebuildUi` 无条件自增，探针只是读它）、`m_jumpLastOffsetY`、`m_tooltipAnchor`、
`m_tooltipPending`、`m_evalPaints` / `m_evalFrames` / `m_evalMaxUs` / `m_evalSumUs` /
`m_evalLastUs` / `m_invalidations` 等**产品**读数。

**判据（★ 门 2 在这一步失效，替代品比指纹更强）**

31 个函数报 `MISMATCH` —— 函数体里全是改名字符串，指纹必然变。替代判据是
`.scratch/stage4_verify.py`：**取 HEAD 版每个 `.cpp`，套同一个映射，与工作区文件逐字节比**。
12 个 `.cpp` 全部全等 ⇒ 除 `m_X` → `m_probeState.X` 之外**没有改动任何一个字符**
（含注释、缩进、空行）。这比「函数体 sha256 相同」覆盖得更广（连函数外的代码也覆盖了）。

配套的两条安全检查（脚本里写死，改不通过就退出）：
1. 成员名**不得出现在字符串字面量里** —— 否则改名会改掉摘要的键或日志文本（引号计数要正确处理 `\"`）；
2. 成员声明必须单行且名字只出现一次 —— 否则切片会切错。

**额外验证：23 份摘要逐字节对账**（13 探针 + 6 夹具 + 4 异步，改前存 `.scratch/stage4-before/`）
⇒ 18 份 `IDENTICAL`，5 份差异**全部**落在 `paints` / `invalidations` / `latencyP99Us`。
按 §6 抖动协议各跑 3 次：async 相 `invalidations` = 253/246/210、`paints` = 164/136/125
（±20% 抖动），而功能计数 `blocks/deltas/messages` 三跑恒为 `6/415/432` ⇒ 判定为时序抖动。

**★ 已知的「不彻底」**：`m_measureProbe` 与 `m_eval*` 读数留在产品侧。若将来要把它们也收进去，
先回答一个问题：**`WriteSummary` 里那些键是产品契约还是仪器读数？** 前者不该跟着探针走
（84 份 evals 基线依赖它们）。

### 8.1b `WriteSummary` 按探针切段（同 `b167580`）

`WriteSummary` **1082 → 304 行**，`AppSummary.cpp` **1114 → 336 行**。

27 个 `if (m_options.xProbe) { json += ... }` 块里，**≥6 行的 24 段**搬成
`AppendXProbeSummary(json)`，实现在对应 `Probe*.cpp`：

| 目标文件 | 段数 |
|---|---|
| `ProbeTranscript.cpp` | 7（hit / reasoning / order / rebuild / answer / select / delProv） |
| `ProbeChrome.cpp` | 5（menu / empty / model / hud / resize） |
| `ProbeSidebar.cpp` | 4（session / sidebar / theme / history） |
| `ProbeFlow.cpp` | 4（jump / key / enter / grow） |
| `ProbeSettings.cpp` | 2（settings / workspace） |
| `AppHud.cpp` | 1（state —— 被动日志，由 `RefreshHud` 填） |
| `ProbeSupport.cpp` | 1（async —— REQ-03） |

**关键设计：守卫留在 `WriteSummary`。** 它现在是一份**摘要格式的目录**：哪个键在哪个
`--xxx-probe` 下、按什么顺序输出，一眼看完；字段顺序（= 84 份 evals 的契约）就在这一处。
把守卫也搬走就等于把顺序分散到 24 个文件里。

3 行以下的小块（`reasoningStreamingTitle`）**留在原地**——搬走反而更难读。

**★ 一个看着像重复代码、实则不能合并的坑**：`WriteSummary` 里有 **11 份转义 lambda**，
实测是**三种行为**：

| 变体 | 差异 | 位置 |
|---|---|---|
| 完整转义（9 份） | `"` → `\"`，且 `c < 0x20` → `\u00xx` | 多数探针段 |
| 不转义引号（1 份，`escS`） | `case '"': out += "\""` | settings 相 |
| 不转义控制字符（1 份，`escH2`） | `default: out += char(c)` | hud 相 |

⇒ **别把它们合并成一个 `JsonEscape`**，那会改输出。lambda 随块体原样搬走。

**判据**：24 段块体与 HEAD 原文**逐行比对**（只允许差缩进 + 注入的 `char buf[512];`）
—— 24/24 相同；再叠加 23 份摘要逐字节对账（见下）。

### 8.1c `OnReplayTick` 探针派发表（同 `b167580`）

settle 分支里 19 个同构 if（`if (m_options.x && !m_probeState.xDone) RunX();`）
→ `kSettleSteps[]` 表 + 一个循环，`AppStream.cpp` 817 → 775 行：

```cpp
struct ProbeStep {
    bool CliOptions::*enabled;   // 哪个 --xxx-probe 开关
    bool ProbeState::*done;      // 对应的「已跑过」标志
    void (HarnessApp::*run)();   // 跑它
};
static constexpr ProbeStep kSettleSteps[] = { ... 19 行 ... };
for (const ProbeStep& step : kSettleSteps) {
    if (m_options.*step.enabled && !(m_probeState.*step.done)) (this->*step.run)();
}
```

**行序 = 契约**，按两组排：

- **同步组（16 条）**：跑完即 `Exit()`，所以这些探针**互斥使用**（同一进程只开一个）。
- **跨 tick 组（3 条：settings / theme / history）**：靠后续 tick 推进，**必须排在同步组之后**
  —— 前面任何一个同步探针 Exit，它们就永远等不到下一拍。

加一个探针现在只改一行。生成脚本会断言「表的行序 == 原 if 的书写顺序」，改错顺序直接报错。

★ **新增一个探针要碰 5 处，漏一处不会报错、只会静默不跑**：

| # | 位置 | 改什么 |
|---|---|---|
| 1 | `CliOptions.h` | 加 `bool xProbe = false;` 开关 |
| 2 | `ProbeState.h` | 加状态成员（日志 vector / `xDone` 标志） |
| 3 | `AppStream.cpp` `kSettleSteps[]` | **加一行**（同步组还是跨 tick 组，见上面的行序契约） |
| 4 | `App.h` | 加 `void AppendXProbeSummary(std::string& json);` 声明 |
| 5 | 对应 `Probe*.cpp` | 写 `RunXProbe()` + `AppendXProbeSummary()` |

**不要**在 settle 分支里新写一个 `if` —— 那会让「表就是全部探针」这个不变量失效，
下一个人数表的行数会数少。

### 8.1d Stage 4 的三条判据（汇总）

| 判据 | 覆盖 | 结果 |
|---|---|---|
| 改名后逐字节全等（`tools/rename_verify.py`） | 12 个 `.cpp` 的全部内容 | 12/12 全等 |
| 24 段块体逐行等价 | `WriteSummary` 切出的每一段 | 24/24 相同 |
| 23 份摘要逐字节对账 | 13 探针 + 6 夹具 + 4 异步 | 18 份 IDENTICAL，5 份只差时序派生量 |

外加门 1（0 error 0 warning）与门 3（regress **跑两次**：切段后一次、派发表后一次，
各自单独过关——不拿上一次的绿顶）。

### 8.2 Stage 5（组件化）：**明确不建议现在做**

探针需要**贯穿式访问**内部状态（`SnapshotUi` 要比对 rebuild 前后的四路快照，
`CompareRows` 要直接读 `m_sess->rows`）。组件化会把私有成员封起来，探针只能靠友元或
额外接口，等于**用新耦合换旧耦合**。除非先决定「砍掉探针」，否则不做。

> Stage 4 之后这条更清楚了：探针状态已经收成一个句柄，`m_probeState.*` 就是全部接触面。
> 真要组件化，只需给组件开一个 `ProbeState&` 的友元通道——但那仍然是把组件的内部暴露给探针，
> 并没有真的解耦。

---

## 9. 接手冒烟清单（先跑这个，确认环境没坏）

```bash
cd /d/Project/ECDI_fake_harness
git status --short                     # 应为空
unset http_proxy https_proxy
cmake --build gui/build --config Release        # 门 1，约 11s 增量
python tools/split_verify.py --check .scratch/split-baseline/fp-stage4.json   # 门 2，应 286/286
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
