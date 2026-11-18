# Harness GUI 设计系统规范（Fluent 2 × Material 3 混合语义体系）

> **适用范围**：`gui/src/**` 全部桌面界面、主题令牌、组件封装与后续界面原型。
> **文档状态**：v1.0（2026-11-15 基于现有 `Theme.h` / `UiKit.h` / `AppInternal.h` / `docs/variant-c-ecdi-mapping.md` 实盘代码收敛）。
> **核心铁律**：本规范建立在 **ECDI 现有绘图原语** 之上，不假设引擎新增阴影、渐变、模糊材质或任意矢量路径能力；任何新增控件不得破坏 `CountingPanel` 重绘基线与 `regress.py` 探针读数。

---

## 1. 设计定位与三大核心原则

本应用是一款运行在 Windows 桌面端的 **AI Agent 调试与对话工作台**（高信息密度、长文阅读、代码与 diff 审阅、高危权限决策并存）。我们的设计语言以 **Microsoft Fluent 2 的桌面空间骨架与控件语法** 为主干，吸收 **Google Material Design 3（M3）的语义化 Token 建模与成对对比度纪律**，并坚持本仓独有的 **“文档流优先于装饰”** 原则。

1. **文档流优先于装饰（Document-First Transcript）**
   * 对话区（Transcript）本质是一篇可持续滚动、可审阅的技术文档，而不是堆满气泡的社交聊天软件。
   * **只有“我说的话”（User）与右栏/浮层独立信息单元保留卡片边界**；Assistant 正文、Tool 调用/结果、Reasoning 思维链、Ask 权限门主体一律去卡片化平铺，靠排版缩进、图标前缀与色彩语义建立层级。
2. **层级表达靠“明度阶梯 + 1px 描边”（Tonal Surface + Hairline Stroke）**
   * ECDI 无 `box-shadow`、无渐变、无可用的系统模糊材质（`WindowBackdrop.h` 已记录 `DWMWA_SYSTEMBACKDROP_TYPE` / `BLURBEHIND` 本机实测不可靠）。
   * 空间前后关系严格通过 **表面明度差（Surface Elevation）** 与 **1px 细线描边（Stroke）** 共同表达，不靠伪造模糊或重色块分割。
3. **成对语义与双后端像素一致（Paired Contrast & Backend Parity）**
   * 所有背景 Token 必须绑定明确的前景（`On-*`）Token，防止浅色/深色切换时出现文字对比度崩塌（如历史上的白底白字、轨道消失）。
   * 字体（`Noto Sans SC` + `Consolas`）与 14px 矢量几何图标在 GDI 与 GL 两条后端必须走同源渲染路径，不依赖操作系统字体回退或 Emoji 字形。

---

## 2. 表面层级与海拔体系（Surface & Elevation）

借鉴 **M3 Tonal Elevation** 的分层思想与 **Fluent 2 Stroke** 的桌面边界规范，全界面划分为 6 个表面层级（Surface Role）。注意深浅两套主题遵循 **“方向反转律”**：

* **Dark 模式（越靠近用户焦点/前景越亮）**：画布最深，容器、控件、浮层逐级提亮。
* **Light 模式（阅读画布最亮，结构框架微沉）**：正文区保持纯白 `#FFFFFF`，侧栏、顶栏、次级表面逐级微暗（`dL*` 逐级下压），以保证正文区永远是视觉中心；唯有独立浮层（下拉/模态卡）回升至纯白并叠加明确描边。

| 层级角色（语义名） | 职责与适用组件 | Dark 实际值（现有映射） | Light 实际值（现有映射） | 边界规则 |
|---|---|---|---|---|
| **Surface.Canvas** | 主转录画布底色、平铺行底色 | `kContentBg` `(22,24,29)` | `kContentBg` `#ffffff` | 无边框 |
| **Surface.Chrome** | 顶栏、左侧栏、自绘标题栏、底栏 | `kDarkTopBg` / `kSidebarBg` `(30,33,40)`<br>`kCaptionBg` `(38,40,45)` | `kDarkTopBg` / `kSidebarBg` / `kCaptionBg` `#f8f9fb` | 区域间 `1px` `Stroke.Subtle` 分隔 |
| **Surface.Inspector** | 右栏检视区、底部 Composer 外框底 | `kInspBg` `(30,33,40)` | `kInspBg` `#fcfcfd` | `1px` `Stroke.Subtle` |
| **Surface.Container** | 输入框内底、chip 胶囊、中性按钮、进度轨道底 | `kPanel2` `(35,38,47)` | `kPanel2` `#f1f3f7` | `1px` `Stroke.Control` |
| **Surface.Reasoning** | 思维链折叠区（弱化次级内容，比画布仅差半档） | `kReasoningBg` `(28,31,38)`<br>Header `(33,37,45)` | `kReasoningBg` `#f9fafc`<br>Header `#f2f4f8` | 去卡片化后正文平铺；若需区块感则保持低对比 |
| **Surface.Overlay** | 模型下拉、设置/预览模态 Sheet | `kCardBg` / `kInspBg` + 边框 | `#ffffff` / `kInspBg` + 边框 | `1px` `Stroke.Control` 提亮/压深 |
| **Surface.Tooltip** | 悬浮提示框（两主题固定深底浅字，**不随主题翻转**） | `kTooltipBg` `(54,59,71)` | `kTooltipBg` `(54,59,71)` | 无边框，靠与周边 `L*` 差浮起 |

> **调色约束**：同一条水平线上的相邻表面（如浅色下 `cardBg` 与 `contentBg`）绝对禁止取相同 RGB；每向上一级至少保持肉眼可辨的 `L*` 阶梯（浅色参考 `Theme.h`：`reasoning -1.8` → `card -2.4` → `tool -3.5` → `toolHeader -5.7`）。

---

## 3. 语义化色彩令牌表（Semantic Color Tokens）

为消除 `Theme.h` 中历史命名（如 `darkTopBg` 在浅色下实际是浅灰）带来的心智负担，规范层统一采用 **M3 成对语义命名**，并与现代码符号建立一一对应关系：

### 3.1 品牌与文本对比（Accent & Ink）

| 规范 Token | 语义说明 | Dark (RGBA8) | Light (RGBA8 / Hex) | 现有代码映射 (`Theme.h`) |
|---|---|---|---|---|
| **Color.Accent** | 品牌主强调色、运行中指示、焦点高亮边框 | `(91, 157, 255, 255)` | `(89, 109, 232, 255)` `#596de8` | `kDarkAccent` / `kRunDark` |
| **Color.OnAccent.Text** | 浅底/透明底上的强调文字链接 | `(207, 225, 255, 255)` | `(76, 96, 215, 255)` `#4c60d7` | `kAccentInk` |
| **Color.PrimaryBtn** | 主操作按钮填充（发送 / 确认） | `(91, 157, 255, 255)` | `(76, 96, 215, 255)` `#4c60d7` | `kBtnPrimary` |
| **Color.PrimaryBtn.Hover** | 主操作按钮悬停态 | `(124, 177, 255, 255)` | `(91, 110, 224, 255)` | `kBtnPrimaryHover` |
| **Color.PrimaryBtn.Pressed** | 主操作按钮按下态 | `(74, 138, 240, 255)` | `(63, 82, 196, 255)` | `kBtnPrimaryPressed` |
| **Color.OnPrimaryBtn** | 主操作按钮前景文字（必须满足 ≥ 4.5:1） | `(11, 18, 32, 255)` | `(255, 255, 255, 255)` `#ffffff` | `kBtnPrimaryInk` |
| **Color.OnSurface.Primary** | 正文、标题、输入框文本（Light 下对比度 > 15:1） | `(230, 233, 239, 255)` | `(35, 38, 46, 255)` `#23262e` | `kDarkInk` |
| **Color.OnSurface.Secondary** | 次要信息、时间戳、分组标题、常规图标（Light ≥ 4.5:1） | `(154, 162, 177, 255)` | `(107, 112, 120, 255)` `#6b7078` | `kDarkMuted` |
| **Color.OnSurface.Disabled** | 禁用按钮与不可交互文本 | `(96, 102, 114, 255)` | `(168, 173, 184, 255)` | `kBtnDisabledInk` |

### 3.2 对话与决策语义色（Conversation & Decision）

| 规范 Token | 语义说明 | Dark | Light | 现有代码映射 |
|---|---|---|---|---|
| **Color.UserBubble.Bg** | 用户消息气泡填充（淡淡的冷蓝倾向，区分于智能体） | `(38, 52, 73, 255)` | `(238, 240, 255, 255)` | `kUserBg` |
| **Color.UserBubble.Border** | 用户消息气泡描边 | `(58, 80, 110, 255)` | `(216, 221, 250, 255)` | `kUserBorder` |
| **Color.Success** | 完成状态点、已允许审计标记、tool 成功态 | `(63, 191, 127, 255)` | `(36, 125, 89, 255)` | `kOkDark` |
| **Color.Error** | 错误提示、已拒绝标记、失败状态 | `(224, 86, 74, 255)` | `(180, 66, 58, 255)` | `kErrorDark` |
| **Color.Warning** | 未知 `kind` 边框提示、同名模型警告、权限按钮强调 | `(224, 163, 60, 255)` | `(138, 100, 32, 255)` | `kWarn`（前景 `kWarnInk`：深 `(34,26,12)` / 浅白） |
| **Color.Ask.Text** | 权限门（`ask`）问句文字（平铺后全画布唯一暖色文本） | `(245, 232, 208, 255)` | `(74, 60, 28, 255)` | `kAskQInk` |
| **Color.Ask.WarmSurface** | 权限门悬停/按下高亮或回退卡片底色 | `(58, 47, 29, 255)` / 框 `(138, 109, 59, 255)` | `(253, 247, 234, 255)` / 框 `(236, 220, 182, 255)` | `kAskBg` / `kAskBorder` |

### 3.3 描边与边框（Fluent 2 Stroke Tokens）

| 规范 Token | 语义说明 | Dark | Light | 现有代码映射 |
|---|---|---|---|---|
| **Stroke.Subtle** | 分栏线、面板分隔线、默认卡片轮廓 | `(46, 51, 62, 255)` | `(231, 233, 238, 255)` `#e7e9ee` | `kLine` |
| **Stroke.Control** | 输入框静态边框、卡片压白底时的加强描边 | `(46, 51, 62, 255)` | `(224, 227, 233, 255)` | `kInputBorder` / `kCardBorder` |
| **Stroke.Focus** | 控件获焦时的 1px 实线高亮描边（替代点线焦点框） | `Color.Accent` | `Color.Accent` | `kDarkAccent` |
| **Stroke.RingTrack** | 环形进度条背景轨道（不可复用浅色 `kLine`，否则压白卡消失） | `(46, 51, 62, 255)` | `(216, 220, 230, 255)` | `kRingTrack` |

---

## 4. 排版与字阶规范（Typography）

### 4.1 字体族约束（双后端零分叉）
* **UI 与中文正文唯一字族**：`Noto Sans SC`（`Theme::sansFamily`）。禁止置空（会退回 `SimSun`）、禁止使用 `"Source Han Sans CN"`（GDI 实测回落宋体）。
* **代码、工具参数、文件路径、调试指标唯一字族**：`Consolas`（`kMonoFamily`）。
* **纯拉丁特殊字重备用**：仅在全 ASCII 场景允许按需使用 `Segoe UI Semibold`（`SemiFont()`），**严禁对含中文字符串使用**（GL 后端无回退会导致中文空白）。

### 4.2 字重二元分档（Weight Discipline）
利用 `Noto Sans SC` 可变字体的命名实例，全应用严格执行两档核心字重（`SansFont(size, weight)`）：
* **`700`（Bold — 默认 UI 档）**：顶栏品牌、分组标签、会话条目、胶囊 Chip、按钮文字、卡片标题、HUD 指标。
* **`400`（Regular — 显式正文档）**：用户气泡正文、Assistant 长文、思维链正文、`ask` 问句、表格数据、输入框内容（长段落用 700 会导致视觉疲劳）。
* *预留档*：`500`（Medium）可作为未来侧栏选中项或小标题的柔和加粗过渡档。

### 4.3 字阶与行盒硬契约（Type Scale & LineBox）
中文严禁进入 `10–11px` 档（GDI/无 subpixel 下笔画发糊）。所有装文字的容器高度必须 **≥ `uikit::LineBox(size)`**，防止 `Widget::Paint` 自裁剪切掉最后 1px 下伸部：

| 档位令牌 | 字号 (DIP) | `LineBox` 最小行盒 (px) | 默认字重 | 典型用途 |
|---|---|---|---|---|
| **Type.Micro** | `13.0f` (`kFontMicro`) | `17` | `700` | 侧栏分组标题、检视小卡标题、底栏状态、文件路径副标 |
| **Type.Small** | `14.0f` (`kFontSmall`) | `19` | `700` | 会话列表条目、芯片 Chip、元信息、标准按钮、Tooltip |
| **Type.Body** | `15.0f` (`kFontBody`) | `20` | `400` | 对话正文、用户气泡、工具标题、输入框、权限问句 |
| **Type.Title** | `16.0f` (`kFontTitle`) | `21` | `700` | 中栏会话头标题、主按钮强调 |
| **Type.Brand** | `17.0f` (`kFontBrand`) | `22` | `700` | 顶栏品牌标识 `harness` |

> **墨迹居中修正律（`kTextInkDy`）**：14px 档位中文墨迹中线比行盒中线偏低约 `2px`，凡与单行文字并排的圆点（`DotCell`）或小 Spinner（`SpinnerButton`），必须在行盒垂直居中基础上 **下移 `kTextInkDy = 2px`**。

---

## 5. 空间结构、尺度与圆角（Spatial & Shape System）

### 5.1 桌面三栏空间骨架
* **顶天立地外壳**：自绘 `ThemedCaptionBar`（高 `kDefaultHeight`，右侧 `min|max|close` 按钮固定 `46px` 宽）+ 顶栏 + 三栏主区 + 底栏（仅开发者模式显示）。
* **可调节三栏**：左栏默认 `220px`（`min 160 / max 480`）+ 左 `SplitterBar` + 中栏（自适应伸展）+ 右 `SplitterBar` + 右栏默认 `240px`（`min 180 / max 500`）。
* **中栏分级 Max-Width 阅读版心**：
  * 可用宽 `< 1056px`：按约 `72%` 渐缩至最小铺满（保留 `kCanvasPadX = 16px` 左右画布边距）；
  * `1056px ~ 1500px`：下限校准锚 `kTranscriptWMin = 760px` 起渐宽；
  * 宽屏超宽：硬顶锁定 `kTranscriptWMax = 1080px`（约一行 55 汉字），多余宽度左右平分居中。

### 5.2 间距、密度与滚动条尺度
* **容器内边距**：中/右栏统一 `kPadFrame = 14px`，左侧栏独立 `kSidebarPad = 16px`（防止左侧拥挤，同时保护竖向预算）。
* **列表与段落节奏**：
  * 侧栏分组标题高 `kSidebarGroupH = 24px`、组前段距 `kSidebarGroupGap = 10px`、分区占位 `10px`；
  * 侧栏列表行高 `kSidebarRowH = 34px`、条目间距 `kRowGapSide = 7px`（注意 `700px` 客户区高是侧栏底部不溢出的底线）；
  * 转录行间距 `kRowGap = 10px`，气泡内边距 `kPadCard = 10px`，子代理归属缩进 `kAgentIndent = 24px` + 左侧 `3px` 归属竖线（`kAgentGutterW = 3`）。
* **全仓统一紧凑滚动条**：所有 `ScrollView` 与多行 `TextBox` 强制使用 `kScrollBarThickness = 4px`，禁止混用框架默认的 `12px` 滚动条。

### 5.3 Fluent 2 尺度圆角表（Shape Scale）
坚持桌面工具的“克制圆角”，不引入 M3 的大半径（`16–28dp`）或全员胶囊化：

| 规范圆角 | 数值 | 现有常量 (`Theme.h`) | 适用对象 |
|---|---|---|---|
| **Radius.Sharp** | `0.0f` | — | 全宽工具条、平铺的 Tool/Reasoning/Ask 行背景、窗口边缘 |
| **Radius.Micro** | `4.0f` | — | 文件附件胶囊、内嵌徽章、小状态块 |
| **Radius.Control** | `7.0f` | `kRadiusBtn` | 常规按钮、Tooltip 浮层、停止/导入按钮 |
| **Radius.Surface** | `8.0f` | `kRadiusCard2` | 右栏检视小卡、侧栏会话选中行、下拉浮窗 |
| **Radius.Input** | `9.0f` | `kRadiusInput` | Composer 输入外框卡片 |
| **Radius.Overlay** | `10.0f` | `kRadiusTool` / `kRadiusPill` | 模态 Sheet、圆角 Pill、权限门回退卡 |
| **Radius.Bubble** | `12.0f` | `kRadiusCard` | 用户消息气泡、未知 `kind` 提示卡 |
| **Radius.Full** | `d / 2.0f` | — | 状态圆点（`Dot`）、圆形头像/计数点 |

---

## 6. 矢量图标与动态反馈规范（Iconography & Motion）

### 6.1 手绘几何图标契约（`UiKit.h`）
* **统一规格**：以控件中心为原点、`14×14px` 视觉框、`1.0f` 线宽，仅使用 `DrawLine` / `DrawArc` / `DrawRoundedRect`。
* **配色响应**：图标前景色跟随按钮状态——常态 `Color.OnSurface.Secondary`（`kDarkMuted`）、悬停 `Color.OnSurface.Primary`（`kDarkInk`）、禁用 `Color.OnSurface.Disabled`（`kBtnDisabledInk`）。
* **已有标准图标集**：`Hamburger`、`PanelLeft`、`PanelRight`、`Share`、`Dots`、`Sun`、`Moon`、`ChevronDown`、`Plus`、`FileBadge`。禁止再往 UI 中插入 Unicode 符号（如 `⚙`、`⋯`、`📄`）作图标。

### 6.2 微交互与状态文法（Fluent 2 Grammar）
* **Focus（焦点表达）**：关闭 ECDI 默认的点线焦点框（`SetShowFocusRect(false)`），改用 **`1px` `Stroke.Focus`（`kDarkAccent`）实线外框变色**（已在 `ComposerBox` 落地），兼顾无障碍可辨识度与界面干净度。
* **Hover / Pressed（反馈克制原则）**：
  * **功能按钮 / 标题栏按钮**：保留悬停与按下底色变化（Light 用黑色低透明叠加，Dark 用白色低透明或明度 +4/+5/+6 阶梯）。
  * **平铺文档行（Tool / Reasoning）**：**刻意取消整行通栏 hover 变色**——点击的真实反馈是折叠体立刻展开/收起，通栏闪烁灰底反而破坏阅读连续性。
  * **高危决策行（Ask 权限门）**：**保留同色系暖色 hover/pressed**（`kAskBorder`），避免从暖色跳成冷灰产生色相冲突，同时强化“此处需要决策”的交互提示。
* **Loading（加载指示）**：使用 `SpinnerButton` 的 `r=5.5f` 单弧（`90°` 进度弧 + `25% alpha` 全圆轨道），绑定主 Tick 做 **12 步离散旋转（≈100ms/步，1.2s/圈）**，避免独立 60fps 定时器引发全窗重绘风暴。
* **浮层（Overlay）与 Esc 关闭链**：设置模态框、文件预览抽屉、模型下拉、Tooltip 统一挂载至 `Window::GetOverlayRoot()`，不参与主区 `VerticalLayout` 排版，按 `预览 > 设置 > 模型浮窗 > 更多菜单` 优先级响应 `Esc` 关闭。

---

## 7. 明确“不采纳”边界（Guardrails）

为了防止后续演进偏离项目定位与引擎约束，明确以下 **Anti-Patterns（禁止事项）**：

1. **不做 M3 风格的大圆角胶囊化与水波纹（Ripple）**：高密度桌面工具中，过大的留白和全员胶囊按钮会降低信息吞吐并显得移动端化；水波纹裁剪动画在无 GPU 局部重绘的环境下性价比极低。
2. **不强求 Fluent 2 的 Mica / Acrylic 透明材质**：已验证当前宿主环境系统模糊 API 不可靠，且半透明叠底会破坏代码与长文本的阅读对比度。
3. **不搞“半透明/半吊子卡片”**：去卡片化的组件（Tool / Reasoning / Ask）就彻底回归画布底色，不要留一个比背景深 2% 的灰框——既没有卡片边界感，又污染文档流。
4. **不在代码里散写魔法数字与 RGBA 字面量**：所有颜色进 `Theme.h`（除两主题通用固定浮层如 `kTooltipBg` 外），所有像素预算须有实测依据或由 `LineBox` / `GetPreferredSize` 推导。

---

## 8. 建议的渐进落地路径（不破坏现有基线）

本规范可直接作为新功能的默认准绳，如要对现有代码做收敛，建议分三步低风险推进：

1. **Step 1 · Token 别名层对齐（零视觉改动）**：在 [Theme.h](file:///d:/Project/ECDI_fake_harness/gui/src/Theme.h) 保留现有 `k*` 引用别名不变，新增语义分组注释或 `Surface` / `Stroke` / `OnSurface` 语义别名，让新代码不再直接引用 `kDarkTopBg` 这类带历史色彩的名称。
2. **Step 2 · Fluent 2 侧栏选中态增强（极小改动）**：参考 [AppInternal.h](file:///d:/Project/ECDI_fake_harness/gui/src/AppInternal.h#L94) 中子代理行已有的 `kAgentGutterW = 3px` 竖条模式，为侧栏当前活跃会话与导航选中项左侧补充一条 `3px` 宽、带圆角的 `Color.Accent` 指示竖条，强化 Fluent 2 桌面导航质感。
3. **Step 3 · 对比度自检化**：利用 `MakeDark()` 与 `MakeLight()` 的编译/运行期表，在测试探针或校验脚本中增加背景/前景配对的 WCAG 最低对比度断言（如正文 ≥ 4.5:1、UI 次要信息 ≥ 4.5:1），防止日后调色回退。
