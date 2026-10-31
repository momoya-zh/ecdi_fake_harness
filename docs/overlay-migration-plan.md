# 浮层迁移到 ECDI Phase 42（Window 级浮层根）—— 方案与裁决点（2026-10-30）

> **读者**：接手这件事的人。本文自包含，不需要读对话历史。
> **状态**：**方案待裁决**（§8 三个裁决点）。裁决后按 §9 的顺序实施，每一步可单独回滚。
> **前置事实**：`third_party/ecdi` 已换到 **Phase 42** 前缀（ECDI `84c3e22`，含 38/39/40/41/42）；
> 换装当日 `tools/regress.py` **RESULT: PASS**（六夹具结构基线全中、24 探针无 unexpected-red、
> live 设置探针 16/16、`protocol/validate.py` exit 0）。本文所有「框架侧」结论都对码核过
> `ECDI/src/Window/Window.cpp` 与 `ECDI/src/Application/Application.cpp`。

---

## 0. 结论（先说人话）

四句话：

1. 本仓有 **4 个真浮层**（设置模态 / 预览模态 / 模型下拉 / tooltip），另有 **2 个看着像浮层但不是**
   的东西（「···」菜单是**占槽**控件、`↓ 跳到最新` 是**视口内**按钮）。
2. 框架这一版给的是「**每次打开新建、关闭即销毁**」的浮层（`ShowOverlay` 移交所有权、`HideOverlay`
   帧末销毁），而本仓浮层是「**常驻控件 + `SetVisible`**」。**这是本次迁移唯一的结构性矛盾**，
   三档方案都是绕着它取舍。
3. 三档：**L1 托管挂载**（只吃几何与拓扑，零行为变化）／**L2 全量 `ShowOverlay`**（白拿点外关闭、
   ESC、焦点恢复，代价是重建式 + 一批行为变化）／**L3 分档**（模态走 L1，下拉与 tooltip 走 L2）。
4. **建议 L3**。理由不是保守：模态那两套的句柄被 **5 个探针**读、被保存链写、被 `RebuildUi` 跨主题
   重建，改重建式的风险与收益不成比例；而下拉与 tooltip 本来就是「每次打开都重填内容」，
   迁过去几乎零成本。

---

## 1. 现状事实（逐条可核）

### 1.1 四个真浮层

| # | 站点 | 现状实现 | 为绕开缺口的做法 | 锚点 |
|---|---|---|---|---|
| A | **设置模态** | `settingsMask`（`Button`，只吞点击不做事）+ `settingsPanel`（`Panel`，内含 `ScrollView` + 12 个 `m_hud.settings*` 句柄） | ① `HarnessRootLayout` 逐位复刻 `VerticalLayout(0,true)`，只为给它们开 `Exempt` 豁免；② `BuildUi` 尾部 `m_scroll->RemoveChild(...)` 搬到 root 末尾抢 z 序；③ 几何全靠 `LayoutSettingsOverlay` 手算 | `AppUiChrome.cpp` `BuildUi`／`AppUiMain.cpp` `BuildSettingsSheet`／`AppSettings.cpp` `LayoutSettingsOverlay` |
| B | **预览模态** | `previewMask` + `previewPanel`，与 A 同款 | 与 A 完全同款（同一套 `Exempt` + 转移） | `AppPreview.cpp` `BuildPreviewSheet`／`LayoutPreviewOverlay` |
| C | **模型下拉** | `modelPopup`（`Panel` 340×272：搜索框 + 行列表 + 底注）挂 `m_scroll` | 靠「最后 `AddChild` = 最上层」；位置手算视口右上角；**点外不关闭** | `AppSettings.cpp` `SetModelPopupOpen`／`RebuildModelPopupList` |
| D | **tooltip** | `tooltip`（`Panel`）+ `tooltipText`，挂 `m_scroll` 最后子 | 锚点 `GetAbsolutePosition` 减视口原点 + **y 钳到 `[0, 视口高-h]`**——顶栏按钮在视口上方，负 y 会被 `PushClip` 裁掉 | `AppHud.cpp` `OnTooltipFire`／`ShowTooltipFor`／`HideTooltip` |

### 1.2 两个「像浮层但不是」

| # | 控件 | 为什么**不**迁 |
|---|---|---|
| E | 「···」菜单 `moreMenu` | 它是**占槽**控件（展开时把转录往下推 34px），靠 `SetVisible` + 父容器 `Arrange` 生效（`AppSettings.cpp` `SetMoreMenuOpen`）。迁成浮层会**改变语义**：从「推开内容」变成「盖住内容」。 |
| F | `↓ 跳到最新` `jumpLatest` | `ScrollView` 直接子节点（视口内 FAB），从不越出视口，也不参与全窗 z 序。挂在视口里是**正确**的。 |

### 1.3 框架侧硬边界（决定方案形态，逐条对码核过）

| # | 事实 | 后果 |
|---|---|---|
| ① | `ShowOverlay(std::unique_ptr<Widget>)` **移交所有权**；`HideOverlay` 把控件移进 `m_pendingDestroyOverlays`，**帧末析构**（`Window::PaintFrame` 末尾 clear） | **关闭即失去控件**。常驻句柄模式（`SetVisible`）在 `ShowOverlay` 下不可用 |
| ② | 三个 layer `Panel` 是 **private**，公共面只有 `GetOverlayRoot()` | 想保留所有权，只能**直接 `GetOverlayRoot().AddChild(...)`**（`HitTestWindow` 的 `[C]` 分支明确认这条「低阶原语挂载」路径），但**不注册 entry** ⇒ 无 ESC、无点外关闭、无焦点恢复 |
| ③ | `ShowOverlay` **不设尺寸、不画遮罩** | 遮罩要自己拼（一个 `Button` 或 `Panel`），尺寸仍由调用方 `SetSize` |
| ④ | 锚点跟随只在 `anchorWidget != nullptr` 时生效，每帧在 `PaintFrame` 里跑 `UpdateOverlayPositions`（带同值守卫）；`Below` 放不下就**翻 Above**，X 取**锚点 X**（不居中） | 想保留现有「右对齐视口」的模型下拉位置，就**不能**用 `anchorWidget` |
| ⑤ | **Modal 层命中兜底 `return nullptr`**：只要 `m_modalLayer` 有子节点，点空白**永不落主树** | 模态语义天然成立；代价是「点空白」不会到达底层控件 |
| ⑥ | **Tooltip 层在 `HitTestWindow` 里被显式跳过** | tooltip 天生点穿（现状是把 tooltip 当普通子节点，理论上会截留命中） |
| ⑦ | `ShowOverlay` 会把焦点交给浮层内**第一个可聚焦控件**；`HideOverlay` 会把焦点还给 `previousFocus` | 对模态是双刃：设置面板的首个可聚焦是「提供商列表第一行按钮」⇒ 打开设置会**抢走输入框焦点** |
| ⑧ | `Window::HandleKeyDown` 在 **Tab 之前**拦 ESC 关顶层浮层；`FocusNext` 在模态层活动时把 Tab **锁在模态层** | 模态内的键盘导航语义改变 |

> 注 ①：`Widget::AddChild` 拒绝「已有父节点」的 child，且**静默丢弃**（`Widget.cpp` 三条前置条件补的
> Release 侧分支）。所以任何「搬来搬去」的写法都必须先 `RemoveChild`。

---

## 2. 三档方案

| 档 | 做法 | 得到什么 | 放弃什么 | 行为变化 | 风险 |
|---|---|---|---|---|---|
| **L1 托管挂载** | 控件所有权留在 harness，`GetOverlayRoot().AddChild(std::move(...))`；关闭 = `RemoveChild` 收回 | 越界不被 `PushClip` 裁／不被布局挤压／恒在顶层／`HarnessRootLayout` 与 `Exempt` 直接删掉 | 框架的点外关闭、ESC、焦点恢复、焦点锁 | **零**（除几何基准从「视口坐标」变成「客户区坐标」） | 低。`[C]` 分支是框架写明的挂载口，但**不注册 entry** ⇒ 与 `HasActiveOverlay()` 无关 |
| **L2 全量** | 每次打开 `ShowOverlay` 新建，关闭 `HideOverlay` 销毁 | 上面全部 + 点外关闭 + ESC + 焦点恢复 + 焦点锁 + 锚点翻转跟随 | 常驻句柄（`m_hud.settings*` 之类）——必须每次重建并刷新句柄 | 见 §6（5 条） | 中高。句柄在 5 个探针里被读，重建式改造要同步改探针 |
| **L3 分档（建议）** | 模态 A/B 走 L1，下拉 C 与 tooltip D 走 L2 | 两边的长处都拿到；模态零行为变化，交互语义只加在便宜的地方 | 模态没有点外关闭（现状也没有——mask 就是吞点击的） | 只有 C/D 变（3 条，见 §6） | 中低。两档互不干扰，可分批落地 |

---

## 3. L1 怎么落地（模态 A / B）

1. **建树位置**：`BuildSettingsSheet()` / `BuildPreviewSheet()` 已经在返回 `unique_ptr`，只是被
   `BuildTranscriptViewport` 收进去挂 `m_scroll`。改为：`BuildMainArea` 建完两份 sheet 后**不交给视口**，
   存进 `HarnessApp` 的新成员（`m_settingsSheet` / `m_previewSheet`，`unique_ptr`，harness 持有）。
   ⇒ `BuildTranscriptViewport` 的两个参数退化为零（或删掉这两个形参）。
2. **挂载/收回**：新增 `MountSettingsOverlay()` / `UnmountSettingsOverlay()`：
   - 挂：`m_window->GetOverlayRoot().AddChild(std::move(m_settingsSheet))`（连同遮罩；遮罩也可由 harness
     持有、一起挂，顺序 = 先遮罩后 sheet ⇒ 绘制序与命中序都对）。
   - 收：`overlayRoot.RemoveChild(...)` 拿回 `unique_ptr` 存回成员。**先收再销毁窗口**（`RebuildUi` 前）。
3. **显隐**：`ApplySettingsVisibility()` 从「`SetVisible` 两个控件」改成「挂/收 + `SetVisible`」——
   模型侧状态 `m_settingsVisible` 不变，探针读 `settingsPanel->IsVisible()` 仍然成立（挂着且可见）。
4. **几何**：`LayoutSettingsOverlay()` / `LayoutPreviewOverlay()` 的算式**逐字不变**——它们读的是
   `root.GetWidth()/GetHeight()`（= 客户区），与 `overlayRoot` 同基准。**唯一要改的是注释**里
   「浮层挂 m_scroll」那几句。
5. **删掉**：`HarnessRootLayout` 整个类（改回 `VerticalLayout(0, true)`）、`Exempt` 机制、
   `BuildUi` 尾部那 18 行转移代码、`#include <set>` 与 `ECDI/Layout/Layout.h`。
6. **`RebuildUi`**：overlay 树**不在** `root` 下 ⇒ 现有「`while (root.GetChildCount()) RemoveChild`」
   不会碰它们。所以重建前必须**先收**（`UnmountSettingsOverlay` / `UnmountPreviewOverlay`），
   重建后按 `m_settingsVisible` / `m_previewOpen` 重新挂。漏了这步的症状 = 切主题后设置面板
   **还是旧配色**（浮层里的控件不吃新令牌），而 `--theme-probe` 的判据只看「面板还在不在」，
   会**全绿**——这是本方案最需要补判据的一处。

## 4. L2 怎么落地（下拉 C / tooltip D）

**C 模型下拉**

- `SetModelPopupOpen(true)`：`BuildModelPopup()`（把现在 `BuildTranscriptViewport` 里那段 popup 构建
  抽成函数，返回 `unique_ptr`）→ `ShowOverlay(std::move(pop), opts)`：
  `layer = Normal`、`lightDismiss = PassThrough`、`exemptWidget = m_hud.modelChip`（点 chip 自身
  走既有 toggle，不要被点外关闭抢先）、**`anchorWidget = nullptr`**（见 §1.3 ④：用锚点会把 X 钉在
  chip 左缘，丢掉现在「右对齐视口」的观感；位置仍按 `m_scroll` 宽手算，只是在客户区坐标里算）。
- `SetModelPopupOpen(false)`：`HideOverlay(nullptr)` + **把 `m_hud.modelPopup/modelPopupList/modelSearch/
  modelPopupFoot` 全部置 nullptr**（关闭即销毁，悬空句柄是 UAF）。
- 行点击回调里先 `SetModelPopupOpen(false)` 再 `RequestModelSwitch(...)`：销毁发生在**帧末**，
  回调栈安全（这正是 `m_pendingDestroyOverlays` 的存在理由）。
- `RebuildModelPopupList()` 现在被 `SetOnTextChanged` 调——句柄在打开期间有效，无需改。
- `--menu-probe` 读 `m_hud.modelPopup->IsVisible()` 与 `modelPopupList` 行数：**关闭后句柄为 nullptr**，
  两处读取都要按 `m_hud.modelPopup && ...` 判空（现状已有 `&&`，逐个复核一遍即可）。

**D tooltip**

- `OnTooltipFire()`：`BuildTooltip(text)` → `ShowOverlay(std::move(tip), opts)`：
  `layer = Tooltip`（命中自动跳过 ⇒ 点穿）、`closeOnEsc = false`（ESC 归 harness 的逐层关逻辑）、
  `lightDismiss = None`（不参与点外关闭）。
- 位置：**不用 `anchorWidget`**。现状的 `+6px` 间距与「按文字估算宽 + 钳进视口」都是有用行为，
  锚点模式会覆盖成 `anchor.y + anchor.h`（丢 6px）且 X 取锚点左缘。改成客户区坐标手算，
  **y 的下钳从 `0` 放开**（浮层不再受视口裁剪，这正是 S5 那条绕行的目的）——但仍要钳到客户区。
- `HideTooltip()`：`HideOverlay(nullptr)` + 句柄置 nullptr；`m_tooltipAnchor` 可能已脱树，
  现在的注释说「靠 leave 先于析构兜住」，迁移后**多一层保护**：`ShowOverlay` 的 `UpdateOverlayPositions`
  有锚点脱树守卫，但只有传了 `anchorWidget` 才生效 ⇒ 手算路径要自己查 `GetParent()`。

## 5. 迁移后可以删掉什么

| 删 | 位置 | 为什么可以删 |
|---|---|---|
| `HarnessRootLayout` 整个类（≈55 行） | `AppUiChrome.cpp` | 浮层不再是 `root` 的子节点，`VerticalLayout(0,true)` 恢复可用 |
| `Exempt` 注册 4 处 + 转移挂载 18 行 | `BuildUi` | 同上 |
| `#include <set>` / `ECDI/Layout/Layout.h` | `AppUiChrome.cpp` | 只服务上面两处 |
| 「最后 `AddChild` = 最上层」的 z 序依赖（4 处注释 + 依赖） | `AppUiMain.cpp` | 层序由框架保证 |
| tooltip 的 `y = clamp(..., 0, ...)` 下钳 | `AppHud.cpp` `OnTooltipFire` | 浮层不再被视口裁剪 |

## 6. 行为变化清单（只有 L2 站点才有）

| # | 变化 | 站点 | 影响面 | 接受度 |
|---|---|---|---|---|
| 1 | **点浮层外关闭**（`PassThrough`：先关，再把这次点击派发给底层） | C | 现状点外面**什么都不发生** | 建议接受（下拉的标准语义） |
| 2 | 点 chip 自身**不**触发点外关闭（`exemptWidget`） | C | 保持现有 toggle | 必须 |
| 3 | tooltip 不再截留命中（Tooltip 层被 `HitTestWindow` 跳过） | D | 现状理论上会截留（tooltip 在鼠标下方 6px，实际几乎不触发） | 接受 |
| 4 | 打开下拉时焦点移到**搜索框**（首个可聚焦） | C | 现状焦点留在原处；这里**是好事**（可以直接打字过滤） | 接受 |
| 5 | 关闭下拉后焦点回到打开前控件 | C | 现状不动 | 接受 |

> 若模态 A/B 也走 L2，额外 4 条：点遮罩关闭（现状 mask 是吞点击的 `Button`）、Tab 锁在面板内、
> 打开时焦点被「提供商列表第一行」抢走（caret 停闪）、关闭后焦点恢复到打开前控件。
> 其中「焦点被抢」是**负向**变化，也是建议模态走 L1 的主要理由之一。

## 7. 探针与基线影响

| 探针 | 相数 | 读什么 | 迁移影响 |
|---|---|---|---|
| `--settings-probe` | 17 | `settingsPanel` 可见性/几何 ≥400×300、`settingsMask` 可见、`settingsHint` 文本、合成点击「保存」 | L1：**零改动**（挂载位置变，可见性与几何不变）；若走 L2 则要重写（句柄每次新建） |
| `--preview-probe` | 8 | `previewPanel/previewMask` | 同上 |
| `--menu-probe` | 7 | `modelPopup` 可见性、`modelPopupList` 行数、点行切换 | L2：关闭后句柄为 nullptr ⇒ 判空复核 |
| `--flow-probe` | — | `settingsPanel` / `modelPopup` 可见性（ProbeFlow） | L2 同上 |
| `--resize-probe` | 7 | 窗口 resize 后浮层几何跟随 | 两档都要复跑（几何算式不变，基准变） |
| `--theme-probe` | 7 | 整树重建后浮层还在不在 | **L1 必须补判据**（见 §3 第 6 条：只查「还在」会漏「还是旧配色」） |
| （tooltip 无探针） | — | — | 现状就没有；迁移后建议补一条 `--tip-probe`，或至少一张截图对照 |

### 7.1 实际落地：新增 `--overlay-probe`（2026-10-30）

上表的两个缺口（tooltip 无探针、`--theme-probe` 只问「还在不在」）由**同一个**新探针补掉
——它们守的是同一件事：**浮层到底挂在哪儿、是不是只有一套**。合并成一个探针的理由是
「一次搭台、九个相」，而不是两个各跑一遍 `RebuildUi` 的昂贵机器。

**核心读数 `overlayRootChildren`**：浮层根恒有 3 个孩子（Normal/Modal/Tooltip 三层，
`Window` 构造期建好），托管挂载的遮罩与 sheet 是**直接**挂上去的（走 `HitTestWindow` 的
`[C]` 分支）⇒ 期望 `3 + mountedManaged`。这条恒等式同时抓两类缺陷：

- 旧的一套没收（`RebuildUi` 忘了 `Unmount*`）⇒ 多 2；
- `ShowOverlay` 重复注册 ⇒ 某个层里多一个。

**九相**（`RunOverlayProbe`，同步跑完即 Exit——`HideOverlay` **立刻**从层里 `RemoveChild`，
只有**析构**延后到帧末，所以孩子数当场可读）：

| 相 | 判据 | 判别力从哪来 |
|---|---|---|
| `closed` | 全关后浮层根只剩三层；托管挂载**不**让 `HasActiveOverlay()` 为真 | 负对照在 `popup` 相：注册式浮层**必须**让它为真，否则本相是空真 |
| `mount` | 遮罩/sheet 的父节点都是浮层根；遮罩盖满客户区；sheet 水平居中 | 居中直检 `(rw-w)/2`；遮罩尺寸直检「点不到顶栏」 |
| `modal-blank` | 点遮罩空白处**不关闭**，且这一击**到不了**顶栏 | 拿主题钮当探针：`SwitchTheme()` 是同步的，穿过去就翻 |
| `rebuild` | 重建后换新令牌（指针变）且孩子数**不变** | 只看指针会放过「旧的还在」，只看孩子数会放过「没重建」 |
| `theme` | 切主题+重建后 `settingsHint` 的文字色跟着变 | 令牌是引用别名、颜色建树时拷进 StyleField ⇒ 没重建到浮层就留着旧色 |
| `tip` | tooltip 祖先链有浮层根、**不在** root 子树里 | 挂在 root 子树里会被视口裁剪（迁移前正是靠 clamp 硬贴顶端） |
| `tip-through` | 把提示框**盖在主题钮上**再点，主题必须翻 | `Button::ConsumesMouseInput()==true` 而 `Panel` 默认 false ⇒ 截留则回调不执行 |
| `popup` | 下拉在浮层根下、`HasActiveOverlay()` 为真、**没**直接挂到根上 | 最后一条把「托管挂载」与「注册式浮层」两种挂法分开钉住 |
| `popup-dismiss` | 点外面（`PassThrough`）必须关掉，且句柄立刻为空 | `HideOverlay` 帧末才析构 ⇒ 句柄不置空就是悬空读 |

**不覆盖**（诚实缺口，写在文件头）：ESC 关浮层。托管挂载的模态**按设计**没注册 entry
⇒ 没有 ESC；下拉的 ESC 属框架行为，ECDI 自己的 `OverlayTests.cpp` 已覆盖。

**基线漂移**：`paints` / `frameCount` / `invalidations` 可能变（浮层进出 `root` 子表的次数变了，
而 `CountingPanel` 只数自己的重绘）。**必须实测再记账，不得静默重采**——与 REQ-15 ④ 同一条纪律。

## 8. 待裁决点

- **D1 迁移深度**：L3 分档（建议）／全 L2／全 L1。
- **D2 模态的交互语义**：模态 A/B 要不要点外关闭？现状 mask 是「吞点击不关闭」，改成点外关闭是
  **用户可感知的行为变更**（有人靠 mask 防误触）。
- **D3 基线**：允许 `paints/invalidations` 漂移并在账本显式记账，还是要求逐位不变（后者会限制方案）。

## 9. 实施顺序（每步可单独回滚）

| 步 | 内容 | 回滚点 | 门 | 状态 |
|---|---|---|---|---|
| 1 | 只搬**预览模态** B 到 L1（一个站点，验证整条链路） | `git revert` 单文件 | 编译 + `--preview-probe` 8/8 + regress | ✅ 8/8 |
| 2 | 设置模态 A 到 L1 + 删 `HarnessRootLayout`/`Exempt`/转移代码 | 同上 | 编译 + `--settings-probe` 17/17 + `--theme-probe` + regress | ✅ 12/17（余 4 相 designed-red 由 §2e live 探针真跑 16/16） |
| 3 | 补 `RebuildUi` 的「先收后挂」与「切主题后浮层配色」判据 | 同上 | 新判据 + `--theme-probe` | ✅ 先收后挂已落地；配色判据落在新探针的 `theme` 相 |
| 4 | tooltip D 到 L2 | 同上 | 截图对照（无探针）+ regress 读数 | ✅ 改由 `--overlay-probe` 的 `tip` / `tip-through` 两相守 |
| 5 | 模型下拉 C 到 L2 | 同上 | `--menu-probe` 7/7 + `--flow-probe` + regress | ✅ `--menu-probe` 7/7 |
| 6 | 基线重采 + 账本记账（`docs/ecdi-capability-requests.md` 新增条目） | — | 六夹具读数逐项对比表 | ✅ 六夹具结构基线全中、无需漂移记账 |
| 7 | **新增 `--overlay-probe`**（迁移本身没有判据，这是补口） | 单文件 | 9/9 + 负对照 4 红 | ✅ 见 §7.1 |

## 10. 验证计划

- 每步：`cmake --build gui/build`（Ninja/Release，需手工导出 MSVC 环境）+ 相关探针 + `tools/regress.py`。
- 每步收尾：GL 后端抽测（`--backend gl`）——浮层绘制走两条链，半透明遮罩是 #71 的首个消费者。
- 全部完成后：六夹具 `paints/frameCount/invalidations` 与迁移前逐项对比，写进账本（漂移要能解释）。

---

## 11. 修订记录

- v1.0（2026-10-30）：首版。基于 Phase 42 前缀已换装、regress 全绿的事实。
- v1.1（2026-10-30）：实施收尾。步 1–5 落地；新增 §7.1（`--overlay-probe`，九相 9/9 +
  负对照 4 红）；补两处**迁移引入的**缺陷（见下）。
- v1.2（2026-10-30）：记录新探针首跑抓到的缺陷。
- v1.3（2026-10-31）：换装 ECDI 审计整改批 A 的新前缀，补 §11.2（六项机检 + `exemptWidget`
  这第 5 个非拥有指针 + 两条 P3 观察），新增 `--overlay-probe` 的 `popup-rebuild` 相。
- v1.4（2026-10-31）：补 §11.3——新相上线前先抓到了探针**自己**的悬垂指针
  （跨三次 `RebuildUi` 持有焦点裸指针，40 次跑 21 次崩），已修为「用时现取」。
- v1.5（2026-10-31）：换装 ECDI 审计整改批 B 的 B1（`ownerOverlay` / `entry.widget` 注销与级联关闭修复），补 §11.4。ECDI.lib 换新（11,973,470 B，SHA256 `d8a6ab31…`，102 个公共头零漂移）；编译重链，`--overlay-probe` 10/10 保持全绿。

### 11.1 迁移引入 / 暴露的两个缺陷（都已修）

**① 点外关闭绕过模型位 ⇒ 悬空指针**（`--overlay-probe` 首跑 `popup-dismiss` 红）

`Application::OnMouseButtonDown` 的 Light Dismiss 分支**直接**调 `Window::HideOverlay(nullptr)`，
完全绕过 `SetModelPopupOpen(false)` ⇒ `m_modelPopupOpen` 留在 true，而 `m_hud.modelPopup`
指向的控件已进 `m_pendingDestroyOverlays`（帧末析构）。**下一次 `Relayout()` →
`RepositionModelPopup()` 就是一次悬空读**，而滚动/尺寸变化都会走到那里。
修法：用框架给的 `OverlayOptions::onClosed`（摘树之后、析构之前同步回调）清模型位与四个句柄。

> 这条在迁移前不存在：旧实现下拉是常驻控件，「关闭」只是 `SetVisible(false)`，没有所有权移交。

**② 模态开着时 Normal 层的下拉会先于遮罩被命中**

`Window::HitTestWindow` 的探测序是 `[A]` Modal 层 → `[B]` Normal 层 → `[C]` 浮层根直接子节点
（托管挂载走 `[C]`）。迁移前遮罩与下拉同在 `root` 子树里，靠「最后 `AddChild` = 最上层」定序，
遮罩后挂 ⇒ 盖住下拉；迁移后两者分居两棵树，顺序反了过来。
修法：开设置/预览时**合掉**模型下拉（与 menu/preview 同族互斥），把"同时存在"变成结构上不可能，
不依赖框架的探测顺序。

### 11.2 审计整改批 A 复核（2026-10-31）

ECDI 侧按 `audit/remediation-plan.md` 落完 **批 A**（A1/A2 浮层悬垂注销 · C1 线段数值安全带 ·
D1/D2 角掩码与 GL clamp · F1 文档订正），本仓换装并复核。

**换装读数**：`cmake --install` 前缀 ⇒ `lib/ECDI.lib` SHA256 `4832ff38…`、11,972,244 B
（与 `cmake-build-release-visual-studio` 产物逐字节相同）；102 个公共头**集合不变**，
内容漂移**仅 `Window.h` 一处**（`OverlayEntry::stale`，private 嵌套结构）。

**六项机检**（对码实测，不引用方案里的行号）：`ForgetSubtree` 体内 `erase`/`HideOverlay`
**代码行 0**（各 1 处仅在注释）；`entry.stale` 判定（`:1205`）**先于**
`anchorWidget == nullptr` 的 `continue`（`:1210`）；`kMaxArcPixels` 复用 `BackendContract.h:131`
未新开常量；GL 侧空掩码降级在 `GLRenderer.cpp:811`；`doc_lint` 41 条全为存量 `L01`，
改动文件 BOM 状态与 HEAD 逐字节一致（零新增）。

**★ 新发现：`exemptWidget` 是同族的第 5 个非拥有指针，批 A 未覆盖**

方案 §2.1 列的 4 类（`anchorWidget` / `previousFocus` / `ownerOverlay` / `entry.widget`）
漏了 `OverlayOptions::exemptWidget`——它**会被解引用**：
`Application::OnMouseButtonDown` 的 `IsWithinSubtree(hit, topOverlay->options.exemptWidget)`
（`Application.cpp:428`）沿父链上溯，指针悬垂即 UAF 读。危险度与 A1 同级：
它指向的同样是**应用侧自有控件**（本仓是 `m_hud.modelChip`）。

本仓受此影响的确切路径：`RebuildUi` 拆 `root` 子树 ⇒ `modelChip` 析构，而下拉挂在浮层根上、
拆树碰不到 ⇒ 条目里留下悬垂 `exemptWidget`，下一次点外就是 UAF 读。
**已自保**：`RebuildUi` ⓪b 先 `SetModelPopupOpen(false)`（模型位先存后还，避免 `onClosed`
把位清掉导致「切主题吞下拉」），并新增 `--overlay-probe` 的 `popup-rebuild` 相钉住
（模型位在 / 控件换了 / 孩子数不变 / 仍注册）。**已回报 ECDI，建议并入批 B。**

**两条 P3 观察（不阻塞，报给 ECDI）**

- `GDIBackend::DrawRoundedRect` 为做空掩码降级把 `m_cornerMaskCache.Get(R)` 提到了
  `R == 0 || !m_antiAliasing` **之前**：① 违反 `CornerMaskCache::Get` 头文件写明的
  「调用方保证 `radius > 0`」；② AA 关闭时也开始生成并缓存掩码（改前完全不生成）。
  建议顺序反过来：先判 `R == 0 || !AA`，再取掩码判 `Empty()`。
- A1/A2 新增的两条用例在 **Release** 下判别力有限：`RemoveChild` 在释放**之前**已把
  `m_parent` 置空（`Widget.cpp:191`）⇒ 旧代码的「锚点脱树守卫」照样会关掉浮层，
  观测结果与修复后一致；只有 Debug 堆填充（0xDD）才会让它真的崩。
  这两条本质是「不崩」判据，不是「无悬垂」判据——想钉死后者需要条目侧可读的探针。

### 11.3 `--overlay-probe` 自身的悬垂指针（2026-10-31，已修）

**症状**：探针偶发 `0xC0000005`，且摘要文件根本写不出来（摘要只在 `Exit` 后写一次）。
复现率随机器状态剧烈波动：同一二进制 30 次跑 1 次崩、另一轮 40 次跑 **21 次**崩。

**定位手段**：在 `expect()` 里加一条 `HARNESS_PROBE_TRACE=<file>` 门控的相位落盘
（每次 `expect` 追加一行相位名）。崩溃的那 21 次**全部**停在 `popup-dismiss` 之后 ⇒
范围锁进收尾段那四行。

**根因**：收尾的 `SetFocusedWidget(focusBefore)` 用的是探针**开头**捕获的裸指针
（`m_window->GetFocusedWidget()`）。而本探针中途调了三次 `RebuildUi()`
（`rebuild` / `theme` / `popup-rebuild`），它把 root 整棵子树拆掉重建 ⇒
该指针在收尾时早已悬垂，`SetFocusedWidget(悬垂)` 是一次 **UAF 写**
（内部还会 `Invalidate()`，即虚调用）。这也解释了复现率为何不稳定——取决于
那块堆内存有没有被复用。

**修法**：焦点**现取**——收尾时改用 `m_promptBar->input()`（重建后的新控件，
且正是 `RebuildUi` 第 ⑨ 步交还焦点的同一个目标），不再跨 `RebuildUi` 持有裸指针。

**验证**：修复前 40 次跑 21 次崩；修复后 15 次跑 **0 次**崩、`popup-rebuild` 全绿。
`popup-rebuild` 的**负对照**（注释掉 `RebuildUi` 的 ⓪b）报两条红：
`the model popup widget was not rebuilt` + `the old popup is still attached after RebuildUi`
⇒ 该缺陷真实存在（重建后屏上是两套下拉，旧的那套留旧令牌），判据有判别力。

> ★ 教训（本仓第 N 次同族）：**任何跨 `RebuildUi()` 持有的裸控件指针都是定时炸弹**。
> `RebuildUi` 的契约是「整棵树换新」，它没有、也不可能替你更新外部缓存。
> 探针/仪器里凡是要在收尾用的控件，一律**用时现取**，不要提前捕获。

### 11.4 换装 ECDI 审计整改批 B 的 B1（2026-10-31）

**换装读数**：
- `third_party/ecdi/lib/ECDI.lib` 替换自 `D:\Project\ECDI\cmake-build-release-visual-studio\ECDI.lib`；
- 体积 `11,973,470 B`（较批 A 前缀增加 1,058 B）；
- SHA256：`D8A6AB31CE05DDD8C352F11F578F2589757D01208806BA8A1FBB4DA28794643C`；
- 102 个公共头文件比对：**哈希 100% 一致，零 API/ABI 变更**。

**影响评估与验证**：
- B1 落地了 `ownerOverlay` 析构时的级联关闭（stale 传播）以及 `entry.widget` 越权析构时的安全清退（避免 `targetLayer->RemoveChild` 产生 UAF）；
- 本仓在 `RebuildUi` ⓪b 的自有保护逻辑保持生效，与框架防御形成互补；
- 重新链接 `gui/build` 后实测相关探针：
  - `--overlay-probe`：**10/10 green**；
  - `--menu-probe`：**7/7 green**；
  - `--preview-probe`：**8/8 green**。
- 零破坏性变化，换装平滑。
