# P4 · GUI 可用性改造 —— 执行方案（2026-10-25）

> **读者**：接手电梯的 agent。本文自包含，不需要读对话历史。
> **开工前先读 `AGENTS.md`**（若存在）——会话规范优先于本文的假设。
> **上游决定**：原型 `C:\Users\a1367\.qoder-cn\app\chat-session-visualizations\419bb068-2406-4659-84fa-0d8cadfbb78a\visualizations\gui-v4-ux-mockup.html`（可交互，用户已验收「整体上已经没有问题了」）。改造动机：P3.5/P3.6 落地后的真实使用暴露一批可用性短板——上翻后无回底入口、无全局快捷键、空会话死胡同、设置面板整屏遮挡转录（最小窗下还溢出）、转录不可选中复制；经 hi-fi 原型多轮迭代收敛后定案本方案。（原引用「项目记忆 `gui-usability-initiative`」是 Qoder 侧记忆，本仓不存在——2026-10-25 评审时改为内联。）
> **用户点名必做**：Phase A（跳回最新按钮布局）。其余按优先级排。
> **评审定案（2026-10-25）**：~30 个代码/协议锚点已实地核对通过。两处拍板——Phase H 验收走 `hello.context` 回退路径（§10）；Phase F 遮罩半透明、双后端实证、失败退不透明（§8，即 ECDI #71 首个消费者）。两处文案修正——`ShellExecuteW` 并无先例（§11）；上游记忆引用悬空已内联（本节）。

---

## 0. 硬边界（违反 = 返工）

1. **只改 `gui/src/**`**。`protocol/`、`core/`、`evals/`、`docs/ecdi-capability-requests.md` 一律不动——本次是纯 harness 渲染侧改造，**不扩协议**。
2. **不动 ECDI 框架**。`third_party/ecdi/` 只是安装前缀（消费口）；框架源码仓在 `D:\Project\ECDI\ECDI`，**只读头文件当参考**。缺能力（如 tooltip）在 harness 侧用现有原语拼，同时评估是否登记 REQ。
3. **评估读数不得被打坏**：`--out` 摘要的 `paints`/`frameCount` 与基线逐位可比是仓库纪律（见 `CountingPanel.h` 的 SeedCounters 注释、App.cpp:4292 的"仪器污染"教训）。每个 Phase 完成后跑一次基线回放并对比：
   ```bash
   gui/build/Release/harness_gui.exe --replay protocol/fixtures/demo-session.jsonl --instant --out /tmp/p4-baseline-<phase>.json --auto-close 3000
   ```
   若某 Phase 使 paints 明显漂移，**必须**在摘要里记录该 Phase 的名字和漂移量，不许悄悄接受。
4. **提交纪律**：每个 Phase 一个 commit，消息 `feat(p4.x): ...`/`fix(p4.x): ...`；precommit hook（`tools/precommit-check.py`：BOM/行尾/夹具）会拦，别 `--no-verify`。
5. **代码风格**：跟着 `App.cpp` 现有走——`m_` 前缀成员、PascalCase 方法、注释写「为什么」不写「是什么」、中文注释、关键契约带日期。**App.cpp ~0.5MB，永远按行号片段读，不要整读。**

## 1. 构建与自检

```bash
# 前缀已在仓库位置（third_party/ecdi），无需显式传 ECDI_PREFIX
cmake -S gui -B gui/build
cmake --build gui/build --config Release

python protocol/validate.py            # 应全绿（本次不改它，跑一遍证明没误伤）
python tools/regress.py                # 全量回归门（改完每个 Phase 都跑）
```

冒烟（真子进程 + 免参数入口）：
```bash
cd core && go build -o bin/ecdi-core.exe ./cmd/ecdi-core && cd ..
gui/build/Release/harness_gui.exe --core core/bin/ecdi-core.exe --auto-prompt "hi"
```

## 2. 关键代码地图（已实地核对，行号=2026-10-25 的 HEAD `4abbc33`）

| 事实 | 位置 |
|---|---|
| `HarnessApplication : public ECDI::Application`，已有 `OnTimer`/`OnWindowResized` override 先例（先做、再调基类，或反之——顺序自己注释清楚） | App.h:291；App.cpp:327,350 |
| 键盘/滚轮是**单收件人+冒泡**派发；`Event::SetHandled()` 已接线（M22），置位可终止 | `KeyModifier.h`（Ctrl=2）、`Event.h:45-61` |
| 转录滚动容器：`m_scroll`（`SetScrollStep(40)`），内容根 `m_content = &m_scroll->GetContentView()` | App.cpp:2607-2616 |
| **`CountingPanel` 是铺满客户区的 root 子节点，后加的 child 天然浮在 ScrollView 之上**——设置面板(560×620)、`···`菜单就是这么浮的；浮层控件一律挂它 | App.cpp:2604(panel), 2616(scroll), `CountingPanel.h` |
| 贴底逻辑现只有一个 60px 阈值内联量 | `FollowTailIfPinned()` App.cpp:9343-9348 |
| 行视图记录 `RowRec{panel,label,headerBtn,body,allowBtn,denyBtn,...}`；文本行全是 `ECDI::Label`（**不可选中**） | App.h:502 |
| ask 已答：`MarkAnswered` 置灰两钮、留选中项 | App.cpp:9060 |
| 设置面板显隐唯一投影路径（隐藏 scroll+composer 让位） | `ApplySettingsVisibility` App.cpp:8283 |
| 模型列表（P3.6 在右栏逐行渲染）+ 排队切换 | `m_modelListDirty` App.cpp:2648,3003；`SendPendingModelSwitchIfIdle` App.cpp:3565；行构建 ~App.cpp:2700,8520 |
| sidecar 持久化**只对 replay-history 会话生效** | App.cpp:1153-1229, 3970 |
| TextBox：`SetReadOnly(true)` = **编辑 no-op 但方向键/选区/Ctrl+A/C/X 全照常**（注释原文如此）；剪贴板经 `GetWindow()->GetPlatformWindow()->SetClipboardText` | `TextBox.h:50-53,391-401` |
| 合成键/点击探针先例（验收要沿用它） | `SynthesizeKey` App.cpp:4261；`SynthesizeClickAt` ~4240 |
| 现有 UI 判据探针族：`--menu-probe` `--enter-probe` `--settings-probe` `--history-probe`… | main.cpp:70-121 |

---

## 3. Phase A（最高优先，用户点名）—— 「跳到最新」按钮

**症状**：GUI 现在只有 `FollowTailIfPinned`（60px 内强吸底），用户上翻后没有任何回底入口；且吸底判断只在增量填行时被动执行。

**设计（对齐原型 F 条）**：
- 按钮浮在**转录视口右下角**：父节点 `CountingPanel`，绝对定位，`y = scroll.GetAbsolutePosition().y + scroll.GetHeight() - 按钮高 - 8`，`x = scroll 右缘 - 按钮宽 - 16`（贴滚动条一侧——原型里用户认可的"跟着滚动条走"语义）。**不要居中。**
- 文案 `↓ 跳到最新`，样式抄菜单条的 Panel+Button 浮层做法（App.cpp:2281 附近）。
- 点击：`m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY())` + `FollowTailIfPinned` 语义自然恢复（offset 回到底部即重新 pinned）。
- 显隐（pinned 状态机）：
  - `pinned = offset >= maxOffsetY - kPinThreshold`，**把 60 抽成 `constexpr int kPinThreshold`**（放 App.cpp 常量区 ~133 附近）。
  - 可见性更新点：① `HarnessApplication::OnMouseWheel` override（先调基类让 scroll 真的滚，再刷新状态——顺序写注释）；② `OnKeyDown` 里 PageUp/PageDown/Home/End 同理；③ 填行后（`FollowTailIfPinned` 调用点顺手 `RefreshJumpButton()`）；④ `OnWindowResized`/`Relayout` 里重算位置。
  - ScrollView **没有滚动回调**——不要为此加定时器每 tick 轮询（污染 paints）；事件侧 + 填行点足够覆盖真实交互。若发现遗漏路径（如拖滚动条滑块），用「tick 里只在 `GetScrollOffsetY()` 与缓存值**不等时**才刷按钮」的脏比较，禁止无条件 Invalidate。
- 流式抢滚动条的既有语义**不改**（pinned 才吸底），本 Phase 只补"看得见、点得回"。

**验收**：新增 `--jump-probe`（仿 `--menu-probe` 形状）：回放 `--instant` 加载完 → 合成滚轮上滚 → 断言按钮可见 + `offset < max-kPinThreshold` → 合成点击 → 断言回底、按钮隐藏 → 读数进 `--out` 摘要。基线 paints 对比照 §0.3。

## 4. Phase B —— 全局快捷键

在 `HarnessApplication` 加 `OnKeyDown` override：**先判全局组合，命中即 `event.SetHandled()` 并 return（不再 `Application::OnKeyDown`，否则焦点控件还会吃一遍）；未命中原样转基类**。经 `SetOwner` 转发回 `HarnessApp::HandleGlobalKey(KeyCode, KeyModifier)`。

| 键 | 行为 | 落点 |
|---|---|---|
| `Ctrl+B` | 侧栏显隐 | `SetSidebarVisible` App.cpp:1504 |
| `Ctrl+I` | 检视栏显隐 | `m_hud.inspector->SetVisible`（~App.cpp:2245 的投影路径） |
| `Ctrl+,` | 开/关设置 | `SetSettingsVisible` App.cpp:8293 |
| `Ctrl+M` | 开模型浮窗（Phase H） | 浮窗 toggle |
| `Esc` | 按 设置 > 浮窗 > 菜单 优先级逐层关 | 各自 SetVisible(false) |
| `Ctrl+N` | 新建会话 | 复用侧栏 `+` 的既有入口 |

注意：键盘单收件人 ⇒ 焦点在 TextBox（composer 输入框）里时 `Ctrl+B` 等仍会被 TextBox 吃掉。**必须实测**（`SynthesizeKey` + `SetFocusedWidget` 两态对照）；若被吃，策略=全局键在 Application 层先于焦点控件判（`Application::OnKeyDown` 是派发者，override 里先拦截本来就能覆盖），写注释说明这条语义。Enter/Shift+Enter 已有（`PromptBar.cpp:113`），不动。

**验收**：`--key-probe`：合成 Ctrl+B/Esc/Ctrl+,，回读 `m_sidebarVisible`/`m_settingsVisible` 模型位 + 控件 IsVisible，判 A==B。

## 5. Phase C —— 空状态

转录为空（rows 数 0 且无 pending 气泡）时在 `m_content` 渲染一块引导 Panel：一句话 + 2~3 个建议指令 Button（点击=`SendPrompt`；无 core 时按钮改「打开设置」走 `SetSettingsVisible(true)`）。任何 `block_start`/notice 到达即移除。首启（`options.firstRun`）且关了设置后不再留空面板——这块空状态正好补上 App.cpp:2586 记录过的死胡同。**回放/harness 场景不弹**（`--replay` 加载即有内容，天然不触发）。

**验收**：新建空会话（`Ctrl+N`/侧栏 +）→ IsVisible 判据；`--instant` 回放 → 判据为不出现。

## 6. Phase D —— ask 留痕（审计性）

`MarkAnswered`（App.cpp:9060）现状=置灰。改为：两钮隐藏，行内渲染一条 `● 已允许`/`● 已拒绝`（绿/红）+ 命令原文（块文本已有）+ 时刻；`RowRec::answer` 字段已存结论，重建路径（`SnapshotRows`/sidecar）自动兼容——**别动模型侧**，纯视图投影。不做「改主意」（协议 ask 一次决，没有重发 answer 的语义）。

**验收**：`--answer-probe`（已有，main.cpp:95）扩一条判据：已答行含结论标记文本。

## 7. Phase E —— 悬浮 tooltip 浮层（ECDI 无 Tooltip 原语 ⇒ harness 拼）

单例浮层 `Panel+Label` 挂 `CountingPanel`（最顶层 child）。给符号按钮（`☰/···/↗/⚙/⇥/×`）挂 `OnMouseEnter/OnMouseLeave`（`Widget.h:251-255`，`HoverTracker.h` 有现成机制）：enter 延迟 ~400ms 显示于按钮下缘（`GetAbsolutePosition()` 取坐标——纪律：不手算父链，App.cpp:4274 注释），leave/点击即隐藏。文案表放 Theme.h 或 App.cpp 常量区，集中维护。
登记评估：harness 拼一个 tooltip ≈ N 行；**考虑在 `docs/ecdi-capability-requests.md` 提 REQ（框架级 Tooltip）**——但先查 ECDI 源码仓确认真没有（铁律：判缺能力前查基类公共头+源码仓），本 Phase 里只做"查+登记"，不实现框架侧。

## 8. Phase F —— 设置面板改模态浮层（修溢出 + 焦点劫持）

现状：560×620 固定、替换 scroll+composer（`ApplySettingsVisibility` App.cpp:8283）；最小窗口（880×460，App.h:1603）下 620 高溢出且不可滚。
改：
1. 半透明遮罩 Panel（`Color` 带 alpha；铺满 root）+ 居中 sheet。★ alpha 语义已核实（2026-10-25，`Color.h:19`）：`DrawRect`/`DrawRoundedRect` 路径经 `BlendAlphaSolid` 消费 alpha ⇒ Panel 背景半透明理论可行；但 GDI 链 `DrawText`/`DrawFocusRect` 恒丢弃、窗口清屏路径不消费。**本遮罩将成为 ECDI #71/REQ-11 登记时的「首个半透明消费者」触发条件**（原登记：零消费者、本仓不规避）。定案：遮罩走半透明，`--backend gdi|gl` 双后端目视实证渲染正确；任一后端不出半透明效果 ⇒ 退化为不透明深色遮罩，**不阻塞 Phase F**，并把实测现象登记为 #71 的首批消费者证据（正反结果都是本仓的评估数据）。
2. sheet 尺寸 `min(620, rootW-80) × min(560, rootH-80)`，**内容装进 ScrollView**（照 App.cpp:2055 侧栏列表的用法）。
3. `ApplySettingsVisibility` 改为只投影遮罩+sheet 显隐，**不再隐藏 scroll/composer**（流式输出在设置后面继续可见——用户验收过原型这条）。三件套（menu/settings/peek）的互斥投影收敛成这一个函数，保持"唯一投影路径"纪律（App.cpp:8284 的注释别删）。
4. Esc 关（Phase B 已接）；`firstRun` 自动弹出逻辑（main.cpp:136 → App.cpp:362）保留。

**验收**：`--settings-probe`（已有）判据迁移：开设置 ⇒ 遮罩可见 **且** `m_scroll->IsVisible()==true`（行为变化点，写进探针断言）。

## 9. Phase G —— 转录可选中/可复制（★ 先做风险尖峰，再全量换）

**机会**：`TextBox::SetReadOnly(true)` 的注释明写"方向键/选区/Ctrl+A/C 照常"⇒ ECDI **可能不需要新能力**就能给转录选区+复制（原判断"大概率撞 ECDI 缺口"要修正；无论结论正反，都写进账本叙事——这是评估数据）。
**风险（先尖峰，别直接铺开）**：
- Label→TextBox 只读行改变测量/绘制路径：`MeasureProbe` 计数、C29-10 布局缓存、断行逻辑（`wrapped/lastWrapW`，App.cpp:9300 一带）都要重走；caret 闪烁 timer 在只读+失焦态是否还跑（App.cpp:4292 的教训：**焦点态会改 paints**）。
- 尖峰做法：**只做 assistant 正文行**一类，`--row-mode` 之外加开关 `--selectable-rows`（默认 off），跑基线回放对比 paints/frameCount/measure 读数并记录。若读数爆炸（例如每个只读行都挂上闪烁 timer），先解决或退回，**不许带病铺开**。
- 铺开顺序：assistant → user → system/error → tool/reasoning 展开体（折叠卡标题行保持 Label）。
**低成本先行**（尖峰失败也不丢人）：行 hover「复制」小按钮，`GetPlatformWindow()->SetClipboardText(utf8)`——`PlatformWindow.h:76` 现成。

**验收**：只读行 SetText 后合成 Ctrl+A/Ctrl+C（`SynthesizeKey`，焦点在行上/不在行上两态），回读 `GetClipboardText()` 断言等于块全文；基线读数对比写入 commit message。

## 10. Phase H —— 模型切换浮窗 + 用量常驻（对齐 P3.6 现状与原型）

P3.6 已把模型列表放右栏（App.cpp:2700 起，行由 `RebuildModelList` 重建、`m_modelListDirty` 驱动，App.cpp:2648/3003）。按已定案改：
1. **顶栏 chip**：`提供商 › 模型 ▾`（数据即 `m_sess->providerId`/模型名，App.cpp:3670 回执处已有）。点击开合浮窗；`Ctrl+M` 同。
2. **浮窗**：Panel+ScrollView 挂 CountingPanel（同 Phase A 的浮层纪律），行=提供商分组标题 + 「模型 · context」行；**同名跨提供商标黄 + 「同名×N」**；顶部一个搜索 TextBox 过滤；底部注明「仅 idle 发出 · 运行中自动排队」（排队逻辑 `SendPendingModelSwitchIfIdle` 已存在，App.cpp:3565，**不动**）。
3. **右栏**：删列表，留「当前 提供商/模型/context/maxOutput」只读卡。
4. **用量**：`usage.context`（v4 已下发）在顶栏 chip 显示 `9%` 一类占比 + 右栏细 ProgressBar（`ProgressBar.h` 现成）。缺分母显示 `—`（协议 §3.7 定案：不猜）。

**验收**：回放 `protocol/fixtures/cot/cot-session.jsonl`（`hello.context`=128000）断言分母上屏——**走回退路径**。★ 定案（2026-10-25）：全部夹具的 `usage` 行均无 `context` 字段，而 §0.1 禁改 `protocol/` ⇒ `usage.context` 按轮覆盖路径在回放下**不可测**，仅真 core live 可验；不为此加夹具。合成点浮窗行 → 断言排队不立发（state==running 时 `model` 未写出）、idle 后写出（`ProtocolWriter` 侧已有队列，只验 GUI）。

## 11. Phase I —— 文件路径可点 + 只读预览（三层渲染的后两层）

- **识别**：tool_call/tool_result 文本与 assistant 正文里的路径形态（`[\\w./-]+\\.(go|cpp|py|md|json|jsonl|html|h)` 起步，正则放常量）渲染成可点击段。ECDI Label 无富文本 ⇒ 务实做法：tool 卡片头部（`ToolTitle`，App.cpp:3722）已经是「名 · 摘要」，摘要若匹配路径形态就把标题按钮的点击绑到预览；正文级富文本**本期不做**（依赖 Phase G 的行渲染换代，避免双重构）。
- **预览面板**：右侧浮层 Panel（挂 CountingPanel，宽 `min(420, rootW-80)`），头=「只读」徽标+文件名+×，体=`ScrollView` + 等宽 `Label` 行列表（`Monospace` 字体已有），改动行不做特判（v1 只给全文）；底=「在资源管理器中显示」（**净新代码**——2026-10-25 核实 gui/src **无** `ShellExecuteW`/shell 先例，`Credentials.cpp` 走的是凭据 API 不是 shell；用 `ShellExecuteW` 打 `explorer.exe /select,<path>` 一类实现，新写即注释说明）+「复制路径」（Phase G 的剪贴板口）。
- **IO 纪律**：读文件只允许当前会话 `workspace` 响应回的实际路径（规范化绝对路径，协议 §3.9 已有语义）**之下**——拼接后必须校验 `weakly_canonical` 仍在 workspace 内（防 `..` 逃逸）；文件上限 ~512KB，超了显示截断 notice（与 transcript 截断先例同措辞习惯）。**绝不**因为 UI 上出现路径就去读任意盘路径。
- 检视栏「本会话改动文件」聚合卡依赖协议里没有的结构化信息，**本期不做**（等真实数据形态再说，投机抽象禁止）。

## 12. 建议时间线（一晚，按额度收口）

| 时段 | 内容 |
|---|---|
| 开工 | §1 构建基线 + 跑 `regress.py` 存档 |
| A → B → C → D | 四块都是小刀，各带探针，逐个 commit |
| F → H | 中刀：F 改投影路径要小心三件套互斥；H 复用 P3.6 队列 |
| E | 独立小刀，可插任意空隙 |
| G | **尖峰必做**；铺开量力——宁可只完成 assistant 行 + hover 复制按钮 |
| I | 今晚可弃项；做则严守 §11 的 IO 纪律 |
| 收工 | `regress.py` + 全部夹具回放 + 基线 diff 写进 docs/ledger 叙事或直接 commit message |

**中途编译不过/回归红 = 停在最后一个绿 commit 上报，不许带着红灯继续。**

## 13. 明确不做

- 协议/夹具/core 任何改动；`validate.py` 判据改动
- ECDI 框架侧实现（tooltip、SelectableLabel 之类）——只允许查源码+登记 REQ
- 拖拽调宽（sidebar/inspector 硬编码宽度本次不动）、面板折叠状态持久化扩展到 live 会话（sidecar 现语义别扩）
- 内嵌编辑器、改动行高亮、diff 结构化渲染（v5 议题）
- 「改主意」重发 answer（协议一次决）
