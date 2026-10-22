# variant-c 落地对照表 · 每个视觉效果的 ECDI 实现路径

> **用途**：把 `docs/prototype/harness-hifi-variant-c.html` 里的视觉手法，逐条对到 ECDI 现有能力上，
> 判明「能直接画 / 要换实现 / 真缺能力」三档，并把真缺的部分登记进 `docs/ecdi-capability-requests.md`。
> **依据**：`third_party/ecdi/include/` 公共头 + ECDI 源码仓 `D:\Project\ECDI\ECDI\src`（按账本 §1.1 铁律，已查源码仓）。
> **方法**：所有「缺能力」结论都经**链接真实 `ECDI.lib` 的受控对照**验证，不是读注释推断。
> 探针源码与产物：`.scratch/req11/`（`req11_probe2.cpp` 双链族名对照 · `req11_ink2.cpp` 链内墨迹对照 · `req11_default.cpp` 空 family 对照）。
> **日期**：2026-10-18。**状态**：对照完成，已登记 **REQ-10**（`Font` 表达力，开放）与 **REQ-11**（alpha 双链不对等，推后）。

---

## §0 结论速览

variant-c 共用了 **11 类**视觉手法：

| 档位 | 数量 | 含义 |
|---|---|---|
| ✅ **能直接画** | 5 类 | `PanelStyle` 四件套 / `DrawArc` / `DrawLine` / 字号 / 字族 |
| ⚠️ **要换实现** | 3 类 | 阴影、渐变、拉丁字重（有替代路径） |
| ❌ **真缺能力** | 3 类 | CJK 字重（**两链都做不到**）、复合族名第二项（GL 断）、alpha 合成覆盖面 |

★★ **最重要的一条（推翻了我先前的两个错误结论）**：

1. **字重不是「完全做不到」**——拉丁文（Segoe UI / Consolas）用**族名**就能拿到 4 档字重，
   `Font::family = "Segoe UI Semibold"` 在 GDI 链与 GL 链**都能解析**，实测两链渲染方向一致。
2. **但 CJK 字重是「真的做不到」**，而且不是「缺个 API」这么简单：
   **GDI 链与 GL 链对「中文加粗」的可用手段恰好互补且互斥** ——
   GDI 只认 `lfWeight`（硬编码 `FW_NORMAL`）、GL 只认 `"…Bold"` 族名（GDI 不认这个族名）。
   ⇒ **不存在任何一个 `Font::family` 字符串能同时让两条链画出中文粗体。** 详见 §2.4。

---

## §1 逐项对照

### ✅ 1.1 能直接画

| variant-c 手法 | ECDI 对应 | 证据 |
|---|---|---|
| `border-radius` + `border` + `background` | `PanelStyleOverride{background, cornerRadius, borderWidth, borderColor}` | `Theme/PanelStyle.h:14-19`；harness 已在用 `gui/src/App.cpp:1089-1092`、`gui/src/App.cpp:584-586` |
| 状态圆点 `.dot` / `.s-dot` / `.avatar`（全圆） | `DrawRoundedRect` 半径钳制 `[0, min(w,h)/2]` ⇒ `w==h` + `r=w/2` 即实心圆 | `Render/RenderCommand.h:61`；`gui/src/UiKit.h:114-120` 的 `Dot()` 已在用 |
| `.meter` 进度条 | `ProgressBar` 控件 | `Widget/ProgressBar.h`；`gui/src/App.cpp:486` 已在用 |
| 分隔线 `.divider` / `border-top` | `DrawLine` 或 1px `Panel` | `Render/RenderCommand.h:37-43` |
| 字号层级（10 / 11 / 12 / 13 / 14 / 15px） | `Font::size`（float DIP） | `Core/Font.h:17`；harness 已用 10.5–13.0 六档 |
| 等宽字体（代码块 / 工具体） | `Font::family = "Consolas"` | `Core/Font.h:18`；`gui/src/Theme.h:97` 已在用，两链都解析到 `consola.ttf` |

### ⚠️ 1.2 要换实现

| variant-c 手法 | 用量 | 问题 | 替代实现 |
|---|---|---|---|
| `box-shadow`（`0 18px 60px` 卡片 / `0 3px 12px` 输入框 / `0 0 0 3px` 状态点光晕） | 3 处 | **无阴影原语**。ECDI 全源码搜 `Shadow\|Gradient\|Blur` **仅 2 处命中**，都是 `Window/WindowBackdrop.h:10-11` 的注释——明确记录 Mica/Blur **已判 YAGNI**（"探针 1 证 `DWMWA_SYSTEMBACKDROP_TYPE` 本机不生效、探针 6 证 `BLURBEHIND(3)` 出不了可见白雾"） | ① **描边代偿**：`borderWidth=1` + `borderColor` 略深于底色（harness 现状已这么做）② **底色分层代偿**：面板色比主区略亮/略暗（DSH 深色就是这么做的：输入框 `#2c2c2e` 比主区 `#151517` 亮）③ 状态点光晕直接去掉，改用 `DrawArc` 画 1px 环 |
| `linear-gradient(145deg,#7b8cff,#5267dc)` 品牌标 | 1 处 | **无渐变原语**，`DrawRectCommand` 只有单色 | **纯色 + 圆角**（variant-c 已改成 `#596de8` 单色）。若要渐变感，可用 `DrawImage` 贴一张 27×27 预渲染图（`DrawImageCommand` 支持缩放映射，`RenderCommand.h:72-76`） |
| 拉丁字重 `600/650/700` | 10 处 | `Font` 无 `weight` 字段（`Core/Font.h` 全文只有 `size` / `family`）；ECDI 全源码 `lfWeight` **只在 4 处赋 `FW_NORMAL`** | **用带字重的族名**（`Segoe UI Semibold` / `Segoe UI Bold` / `Segoe UI Light`），harness 侧零改动可用，见 §2.2 |
| **中文**字重 `600/650/700` | 同 10 处 | **两链都做不到**（详见 §2.4） | **改用「字号 + 颜色 + 留白」代偿**，见 §3.1 |

### ❌ 1.3 真缺能力（已登记）

| 缺口 | 影响 | 处置 |
|---|---|---|
| **CJK 加粗在两条链上没有公共解** | 写 `"Microsoft YaHei Bold"` ⇒ GDI 链回退**宋体**（另一个字体）、GL 链正确加粗；写 `"Microsoft YaHei UI Bold"` ⇒ GDI 正确、GL 链回退宋体。**写哪个都错一半** | 已登记 **REQ-10**（建议 ECDI 给 `Font` 增 `weight`） |
| **复合族名的第二项在 GL 链匹配不到** | `Microsoft YaHei UI` / `Microsoft YaHei UI Bold` / `Microsoft YaHei UI Light` 全部 `ResolveFile → 空串` ⇒ 回退默认 face。GDI 链**全部正常** ⇒ 又一处 REQ-09 型双链发散 | 已并入 **REQ-10** 现象③（同一函数，独立可修） |
| **TTC 恒取 face 0** | `Microsoft YaHei UI` 与 `Microsoft YaHei` 的行高差异在 GL 链无法表达 | 已并入 **REQ-10** 现象②（优先级低，可用 `size` 代偿） |
| **`Color.a` 双链覆盖面不对等** | GL 链 9 个原语全消费；GDI 链 `DrawText` / `DrawFocusRect` **恒丢**，`DrawLine` 在「AA 关 + a<1」时丢 | 已登记 **REQ-11**（📋 推后：当前零消费者） |
| **无虚线** | `DrawLineCommand` 无 dash 字段，GDI 侧两处 `CreatePen` 硬编码 `PS_SOLID` ⇒ 原型 `.gap` / `.sample-label` 的 `border:1px dashed` 无对应能力 | 记在 **REQ-11** 的「附带缺口」行 |

---

## §2 ★ 字重：受控对照实测

### 2.1 方法

所有读数来自**链接真实 `ECDI.lib`** 的探针（`.scratch/req11/`），不是模拟：

- **GDI 链**：照 `ECDI\src\Render\GDIBackend.cpp:1382-1391` 复刻 —— `LOGFONTW{ lfHeight=-14, lfCharSet=DEFAULT_CHARSET, **lfWeight=FW_NORMAL** }` + `CreateFontIndirectW` + `GetTextFaceW`。
- **GL 链**：照 `ECDI\src\Render\FontEngine.cpp:207` + `:236` —— `Win32FontSource::ResolveFile(family)` + `FT_New_Face(path, **0**, &face)`。
- **字重判据**：渲染同一串文本，数**暗像素**（亮度 < 128）。★ **只在同一条链内比较**（variant 族 vs 其 base 族），因为 GDI 与 FreeType 的抗锯齿策略不同，**跨链绝对墨迹量不可比**（常规体就已经 0.58×，那是渲染器差异，不是字重差异——这一条是我第一版探针的仪器错误，已修正）。

### 2.2 拉丁：两链都支持，方向一致

| base → 请求 | GDI 墨迹比 | GL 墨迹比 | 结论 |
|---|---|---|---|
| `Segoe UI` → `Segoe UI Semibold` | **1.20×** | **1.21×** | ✅ 两链一致变粗 |
| `Segoe UI` → `Segoe UI Bold` | **1.38×** | **1.57×** | ✅ 两链一致变粗 |
| `Consolas` → `Consolas Bold` | 1.08× | 1.27× | ✅ 两链都变粗（GDI 幅度小） |

⇒ **`Font::family = "Segoe UI Semibold"` 是安全的**，不需要 ECDI 改动。
可用档位（`GetTextFaceW` 确认解析成功、注册表确认文件）：
`Segoe UI` → `segoeui.ttf` · `Segoe UI Semibold` → `seguisb.ttf` · `Segoe UI Bold` → `segoeuib.ttf` · `Segoe UI Light` → `segoeuil.ttf`。

### 2.3 中文：两链分歧，且分歧方向相反

| 请求的 family | GDI 实际 face | GDI 墨迹比 | GL `ResolveFile` | GL 实际 face | 结论 |
|---|---|---|---|---|---|
| `Microsoft YaHei` | 微软雅黑 ✅ | — | `msyh.ttc` ✅ | Microsoft YaHei / Regular | 一致 |
| `Microsoft YaHei Bold` | **宋体** ❌ | 0.75×（变**细**了） | `msyhbd.ttc` ✅ | Microsoft YaHei / Bold | **GDI 回退到另一个字体** |
| `Microsoft YaHei UI` | Microsoft YaHei UI ✅ | — | **空串** ❌ | SimSun / Regular | **GL 回退** |
| `Microsoft YaHei UI Bold` | Microsoft YaHei UI Bold ✅ | **1.32×** | **空串** ❌ | SimSun / Regular | **GL 回退** |
| `DengXian` | 等线 ✅ | — | `Deng.ttf` ✅ | DengXian / Regular | 一致 |
| `DengXian Bold` | **宋体** ❌ | 0.96× | `Dengb.ttf` ✅ | DengXian / Bold | **GDI 回退到另一个字体** |

★★ **为什么 GDI 认不出 `Microsoft YaHei Bold`（订正：先前写「Bold 是 style、不是 family」是错的）**

我最初把它归因于「GDI 的族模型里 Bold 是 style 而非 family」。**这个解释已被我自己的受控对照推翻**
（探针 `.scratch/req11/req11_facekind.cpp`，只改 `lfFaceName` 一个自变量，`lfWeight` 固定 `FW_NORMAL`）：

| 请求的 `lfFaceName` | `GetTextFaceW` 实际返回 | 判定 |
|---|---|---|
| `Arial Bold` | `Arial Bold` | ✅ 完整 face 名**有效** |
| `Times New Roman Bold Italic` | `Times New Roman Bold Italic` | ✅ |
| `Consolas Bold` | `Consolas Bold` | ✅ |
| `Segoe UI Bold Italic` | `Segoe UI Bold Italic` | ✅ |
| `Microsoft YaHei UI Bold` | `Microsoft YaHei UI Bold` | ✅ |
| `Microsoft YaHei UI Light Italic` | **宋体** | ❌ |
| `Arial Regular` | **宋体** | ❌ |

⇒ **完整 face 名（`<family> <style>`）在 GDI 里是普遍有效的**，「Bold 不是 family」不成立。
**真因是本地化规范名**（`req11_root.cpp`，23 个族名 × 英文名/本地化名两两对照）：

| 请求 | 实际返回 | 说明 |
|---|---|---|
| `Microsoft YaHei` | `微软雅黑` | 该 face 的**规范名是中文** |
| `Microsoft YaHei Bold` | **宋体** | 英文名匹配不上中文规范名 ⇒ 回退 |
| `微软雅黑 Bold` | `微软雅黑 Bold` | ✅ 用**中文完整名**就能拿到 |
| `DengXian` | `等线` | 规范名中文 |
| `DengXian Bold` | **宋体** | ❌ |
| `等线 Bold` | `等线 Bold` | ✅ |
| `Microsoft YaHei UI` / `…UI Bold` | 同名 | ✅ 该族规范名**本来就是英文** |
| `微软雅黑 UI` | **宋体** | ❌ 反过来也不行 |

⇒ 规律：**规范名是中文的族，必须用中文完整名；规范名是英文的族（Segoe UI / Arial / YaHei UI），英文完整名即可。**
★ 但 `微软雅黑 Bold` 在 GL 链 `ResolveFile` 返回**空串**（`Win32FontSource.h:25-31` 明写不支持中文族名）
⇒ **即使知道了正确写法，仍然没有两侧都通的字符串** —— §2.4 的结论不变，只是根因换了。

### 2.4 ★★ 结论：CJK 加粗没有公共解

| 手段 | GDI 链 | GL 链 |
|---|---|---|
| `lfWeight = FW_BOLD` | ✅ 有效（`req11_weight.cpp`：YaHei CJK 14px 墨迹 507→690 = **1.36×**；拉丁 15px 168→277 = **1.65×**；DengXian 1.29×；Segoe UI 1.79×） | ❌ `Font` 没有 weight 字段，且 `FT_New_Face` 不传 variation 轴 |
| `family = "Microsoft YaHei Bold"` | ❌ 回退宋体（规范名是中文） | ✅ 得到 `msyhbd.ttc` 的 Bold face |
| `family = "Microsoft YaHei UI Bold"` | ✅ 有效 | ❌ `ResolveFile` 返回空串 |
| `family = "微软雅黑 Bold"`（本地化名） | ✅ 有效 | ❌ 空串（头文件已声明不做中文族名） |

⇒ **两条链唯一同时可用的是「不加粗」。** 任何 `Font::family` 字符串都无法让两条链同时画出中文粗体。
这是**真能力缺口**，不是 harness 用法问题 ⇒ 已登记 REQ-10。

★★ **但落地建议要改**（这一条先前写得不完整）：既然 `lfWeight` 在 GDI 侧对**所有真实用例**都有效（≥1.29×），
而 GL 侧缺的是 `Font` 上的一个字段，那么**正确的需求形态是「给 `Font` 加 `weight`」而不是「换族名」**：
- 加 `weight` ⇒ GDI 后端消费它（`lfWeight`），GL 后端按 face 名或 FreeType variation 轴消费，**两链同解**；
- 换族名 ⇒ 只有 GDI 通、GL 断，且**中英文要写两套规则**（拉丁用 `Segoe UI Semibold`、中文只能用 `Microsoft YaHei` 不加粗）。
⇒ 族名路线只能当**过渡**（拉丁文现在就能用），CJK 字重必须走 `Font::weight`。

### 2.5 附带发现：TTC 永远只加载 face 0

`FontEngine.cpp:236` 是 `FT_New_Face(impl.library, path.c_str(), **0**, &face)` —— face index 写死 0。
实测各 TTC 的 face 0：

| 文件 | face 0 | face 1（**永远拿不到**） |
|---|---|---|
| `msyh.ttc` | Microsoft YaHei / Regular | **Microsoft YaHei UI / Regular** |
| `msyhbd.ttc` | Microsoft YaHei / Bold | **Microsoft YaHei UI / Bold** |
| `msyhl.ttc` | Microsoft YaHei Light / Regular | **Microsoft YaHei UI Light / Regular** |
| `simsun.ttc` | SimSun / Regular | **NSimSun / Regular** |

⇒ 即使 `ResolveFile("Microsoft YaHei UI")` 修好了（返回 `msyh.ttc`），**GL 链拿到的仍是非 UI 的那个 face**。
**`Microsoft YaHei UI` 与 `Microsoft YaHei` 的区别（UI 版行高更紧）在 GL 链上根本无法表达**，
除非 `FontEngine` 支持 face index。这一条已并入 REQ-10 现象②。

---

## §3 落地时真正会疼的三件事

### 3.1 中文的字重层级只能靠「字号 + 颜色 + 留白」拼

variant-c 的视觉层级是 `700 品牌字` > `650 卡片标题` > `600 会话名/按钮` > `400 正文`。
ECDI 上中文**只有一档**（Regular），所以落地映射：

| variant-c | 落地（中文） | 落地（拉丁） |
|---|---|---|
| `700` 品牌字 15px | `Microsoft YaHei UI` 15px + 颜色压到 `#1a1d24` | `Segoe UI Bold` 15px |
| `650` 卡片标题 11px | `Microsoft YaHei UI` 11px + 颜色 `#23262e`（**不加粗**，11px 加粗会糊） | `Segoe UI Semibold` 11px |
| `600` 会话名/按钮 12–13px | `Microsoft YaHei UI` 12–13px + 颜色压深一档 | `Segoe UI Semibold` 12–13px |
| `400` 正文 13px | `Microsoft YaHei UI` 13px | `Segoe UI` 13px |

★ 但注意 §2.3：`Microsoft YaHei UI` 在 **GL 链解析不到**（回退 SimSun）。
⇒ **落地时中文一律写 `Microsoft YaHei`（不带 UI）**，这是当前唯一两链都通的写法；
UI 与非 UI 的行高差异用 `Font::size` 微调代偿。

### 3.2 阴影全部去掉后，层次只能靠底色和描边

variant-c 的三层浮起（`.app` 卡片 / `.composer` / `.card`）落地后**只剩描边**。
DSH 深色主题已经验证过这条路（输入框比主区亮）。⇒ 落地建议：
- `.app` 外框：`border 1px`（已有）
- `.composer`：底色比主区**略暗** + 描边（代替 `box-shadow:0 3px 12px`）
- `.card`：`#ffffff` + 描边 `#eaecf0`（已有），去掉 `box-shadow` 后无损失

### 3.3 `DrawText` 透明底 + 小字号 CJK 会糊

`DrawTextCommand.color` 注释明确「背景透明，由控件先画」（`RenderCommand.h:30`）。
CJK 在 10–11px 且无 subpixel 时边缘发灰。variant-c 里 `.subtle` 11px、`.card-title` 11px、`.activity` 10px 都是这个档位。
⇒ **10–11px 的 CJK 要么提字号，要么换 ASCII 标签**。不阻塞，但影响观感。

---

## §4 与账本的衔接

| 编号 | 内容 | 状态 |
|---|---|---|
| （无编号） | **阴影 / 渐变** | **不提**——ECDI 已明确判 YAGNI（`WindowBackdrop.h:10-11`），且描边 + 提亮底色代偿可用（§3.2） |
| **REQ-10** | **`Font` 表达力不足**：① 无字重 ② 无 face index ③ 复合族名第二项在 GL 链断 | 🔄 **开放**（账本已登记，2026-10-18） |
| **REQ-11** | **`Color.a` 双链覆盖面不对等**（GDI 的 `DrawText`/`DrawFocusRect` 恒丢）+ 无虚线 | 📋 **已登记（推后）**——当前零消费者 |

★ 按账本 §1.1 纪律，上述结论均已有**只改一个自变量的受控对照**支撑（探针在 `.scratch/req11/`），
已按 §5.2 维护规则写入 `docs/ecdi-capability-requests.md`（v2.7）。

---

## §5 ★ 落地实测：variant-c 的浅色形态已在真 GUI 上跑通

前面 §1–§4 都是**静态推演 + 头文件/源码取证**。本节是**真跑出来的结果**：
`gui/src/Theme.h` 从「编译期常量」改成「运行期可切换双令牌」后，
`harness_gui.exe --theme light` 已能渲染出 variant-c 的浅色形态。

### 5.1 实现路径（关键决策）

**没有**把 ~200 处按名引用的令牌改成 `theme().ink` 成员访问——那要动每一处、容易漏。
改用**引用别名**：

```cpp
struct Theme { ECDI::Color darkTopBg; /* …53 个颜色字段… */ const char* sansFamily; };
inline Theme g_theme = MakeDark();
inline const ECDI::Color& kDarkTopBg = g_theme.darkTopBg;   // 绑定地址不变
inline void SwitchTheme(bool light) { g_theme = light ? MakeLight() : MakeDark(); }
```

⇒ `SwitchTheme()` 只改 `g_theme` 的**内容**，**调用点零改动**。
★ 硬约束：**只允许在 `BuildUi()` 之前调**——令牌是引用，UI 建好就把颜色值拷进了各自
`PanelStyle`/`ButtonStyle`，之后再切不会重绘。唯一调用点 `gui/src/App.cpp:311`。

### 5.2 逐项兑现情况

| variant-c 的手法 | 落地做法 | 结果 |
|---|---|---|
| 浅色画布 / 侧栏 / 卡片三层 | `contentBg #ffffff` · `sidebarBg/inspBg #f8f9fb` · `panel2 #f1f3f7` | ✅ |
| 圆角 + 描边卡片 | `PanelStyle.cornerRadius/borderWidth/borderColor` | ✅ 直接用 |
| 品牌蓝按钮 | `kBtnPrimary #4c60d7` + 白字（5.30:1） | ✅ |
| 琥珀 ask 权限门 | `kAskBg #fdf7ea` / `kAskBorder #ecdcb6` / `kAskQInk #4a3c1c`（10.06:1） | ✅ |
| 10 处 `font-weight:600/700` | **画不出来**（ECDI 单字重）⇒ 改靠字号/颜色/留白（§3.1） | ⚠️ 降级 |
| 3 处 `box-shadow` | **无原语** ⇒ 改靠底色 + 1px 描边（§3.2） | ⚠️ 降级 |
| 品牌标 `linear-gradient` | **无原语** ⇒ 去掉渐变，纯色 | ⚠️ 降级 |
| `clamp()` 字号 / `@media` 三档折叠 | 无 CSS 引擎 ⇒ 窗口 `AutoSize` + 固定栅格 | ⚠️ 换实现 |

### 5.3 ★ 落地过程暴露的四个真问题（都不是静态推演能发现的）

**① 助手气泡在浅色下「消失」** —— 最严重的一个。
`MakeLight()` 首版把 `cardBg` 与 `contentBg` 都设成 `#ffffff`，**同值** ⇒ 助手消息只剩
1px 描边，视觉上退化成「裸文本」。深色从来不存在此问题（`kCardBg (30,33,40)` vs
`kContentBg (22,24,29)`）。
⇒ 根因是**两套主题的层次方向相反**：深色「越重要越亮」，浅色「越重要越暗」。
浅色必须按白画布的 **L\* 偏移**重新定一套 ramp，不能照抄深色的相对关系。

**② 思维链卡几乎不可见** —— 同源问题。首版 `reasoningBg #fbfbfc` 只有 dL\* −1.3，
**且比助手卡还暗**（层级颠倒）。

**③ 环形进度条轨道消失**。
`App.cpp:667` 的轨道色复用 `kLine`（注释还写着「不用 kTrackBg，否则看不见整圆」），
但 `kLine #e7e9ee` 压在白卡 `#f1f3f7` 上只有 **1.09:1** ⇒ 整圈轨道没了。深色同一对是 1.19:1。
⇒ 新增独立令牌 `kRingTrack`（浅色 `#d8dce6`，1.24:1）。

**④ 输入框常态显示蓝色虚线焦点框**。
ECDI `TextBox::OnPaint` 有**两套**边框（`TextBox.cpp:1229-1265`）：
① `borderWidth/borderColor` 实心圆角环；② `HasFocus() && ShowFocusRect()` 的
`DrawFocusRect` **点线框**（颜色取 `TextBoxStyle.border`，默认品牌蓝）。
本项目输入框**启动即获焦**（`App.cpp:325`，验收 A2「打开即可打字」）⇒ ②**永远**盖在①上。
⇒ harness 侧派生 `ComposerBox`：关掉②（`SetShowFocusRect(false)`），
改让①的颜色表达焦点（失焦 `kInputBorder` / 获焦 `kDarkAccent`）。
★ **不是**直接 `SetShowFocusRect(false)` 了事——那样焦点完全不可见，是拿可访问性换观感。

### 5.4 落地后的对比度实测（浅色，WCAG）

| 组合 | 比值 |
|---|---|
| 正文 `#23262e` / 白 | **15.13** |
| 次要 `#6b7078` / 白 | **4.98** |
| 次要 / 面板 `#f8f9fb` | 4.73 |
| 正文 / 用户气泡 `#eef0ff` | 13.36 |
| ask 问句 `#4a3c1c` / `#fdf7ea` | 10.06 |
| 白 / 品牌蓝 `#4c60d7` | 5.30 |
| 白 / 琥珀按钮 `#8a6420` | 5.35 |
| ok `#247d59` / 面板 | 4.80 |

⇒ 全部 ≥4.5。原型 variant-b 的对应值是 8.20 / 2.68 / 2.83，落地版**整体高一档**。

### 5.5 表面层次：L\* 偏移才是正确指标，不是 WCAG 比值

判断「卡片与画布分不分得开」不能用 WCAG 对比度——它是为**文字**设计的，
在 1.0–1.2 区间几乎没有分辨率。正确指标是 CIE **L\* 偏移**。

深色主题（画布 `#16181d`）的实际设计语言，与浅色落地版的反向镜像：

| 表面 | 深色 dL\* | 浅色 dL\* |
|---|---|---|
| 思维链 | +3.49 | −1.75 |
| 消息卡 | +4.47 | −2.42 |
| 工具卡 | +5.88 | −3.51 |
| 工具卡头 | +7.88 | −5.69 |
| 环形轨道 | +12.95 | −12.27 |

★ 浅色的偏移量约为深色的一半，**不是错**：白画布 L\*=100 已到顶，向下可用的
L\* 空间只有 100；而深色画布 L\*=8.24，向上有 92 的余量。**同样的感知分离度，
浅色需要的数值更小**。这与用户此前的判断一致：浅色不该按深色的数值标准苛求。

### 5.6 回归

`--theme light` 与默认深色**两轮全 PASS**，5 个用例读数逐项一致
（demo 8/19/43 · scale 2000/4590/8906 · realistic 220/717/1213，
全部 `seqGaps=0 / violations=0 / unknownKinds=0`）⇒ **换主题只改颜色，不动协议读数**。

### 5.7 仍未兑现的（诚实边界）

- **字重层级**：ECDI 单字重，variant-c 的 10 处 600/700 全部降级为字号+颜色（§3.1）。
- **阴影**：无原语，全部降级为描边+底色（§3.2）。
- **中文加粗无公共解**：`sansFamily = "Microsoft YaHei"` 本身**两链都能解析**
  （GDI → `微软雅黑`；GL `ResolveFile` → `msyh.ttc`），所以常规体中文没问题。
  但**加粗**这一档两链没有交集：`Microsoft YaHei Bold` 在 GDI 回退宋体、在 GL 正常；
  `Microsoft YaHei UI Bold` 反之。⇒ REQ-10 已登记，当前不追求中文粗体。
- **中文字形只在 `sansFamily` 非空时改善**：~~深色主题刻意保留 `sansFamily = ""`
  （落到 SimSun）作为**回归基线**，浅色才用雅黑。两主题字形不同是**有意为之**，
  不是 bug——若要让深色也换雅黑，行高会 +19%（`SimSun 16 → YaHei 19` @14px），
  需要连带调所有硬编码行高。~~

  ★ **2026-10-22 订正：上面这条已作废，两主题现在都渲染在雅黑上。**
  作废的**原因不是审美变了，而是「两主题字形不同」这件事被证明本身就是缺陷**：
  同一串 `·`（U+00B7）在浅色下被光栅化成**一条 9px 实心横杠**、在深色下是
  **三个独立 2px 点（中心距 12.0px）**——`···` 按钮在两主题下不是同一个字形，
  用户报的是「三点间隔不同」，实际是三点粘连。根因就在这一条要维护的
  「两套字族」上：`Theme.h` 深色 `sansFamily = ""`（ECDI 默认 face = SimSun）、
  浅色 `"Microsoft YaHei"`，而 `SansFont()` 读的就是 `g_theme.sansFamily`。
  ⇒ 判定：**让同一个控件在两主题下长得不一样，不是「有意为之」，是可预期的缺陷源**。
  代价照付：深色行高 SimSun 16 → YaHei 19（+19%），paints 读数随之变化
  （demo 22→23、realistic 17→19、cot 21→22、scale 21→23），
  **今天之前的深色 paints/frames 读数不可直接对比**；结构读数
  （blocks/deltas/messages）六夹具 × 两主题全不变。这是本仓**第一次有意改变深色字形基线**。

---

## §6 ★ 系统标题栏（CaptionBar）随主题切色（2026-10-18 补记）

§5 落地的是 **HUD 顶栏**（`BuildTopBar()` 的 36px 原型 C 顶栏），**不是** Windows 系统标题栏。
窗口还是 `ChromeMode::Borderless`，最上面那条 32px 的 `ECDI::CaptionBar` 一直沿用 ECDI 的硬编码深色
（`CaptionBar.cpp:31` 底色 `#26282d` + `:37` 标题 `#ebecf0`）⇒ **浅色主题下它是唯一一条深色带**，
`--theme light` 看起来「上面糊了一条」。原型 variant-c 的 `.titlebar`（`docs/prototype/harness-hifi-variant-c.html:22`，
54px + `border-bottom: 1px solid var(--line)`）在真 GUI 里由两条共同承担：32px 系统 CaptionBar + 36px HUD 顶栏。

### 6.1 为什么不能在框架侧修

`CaptionBar` / `CaptionButton` 的颜色**全部硬编码**在 ECDI 源码里，且 `CaptionButton` 是
`class CaptionButton final`、无样式 API；`Theme` 抽象基类的 9 个 `Get*Style()` 里**没有 caption 令牌**
⇒ 按项目铁律（不改 ECDI），只能由 harness 侧派生覆盖。落地 = `gui/src/ThemedCaptionBar.h`
（`ThemedCaptionBar : ECDI::CaptionBar` + 自绘 `ThemedCaptionButton : ECDI::Widget`），
`gui/src/Theme.h` 增 8 个 caption 令牌，`App.cpp:388` 构造点换成派生类。账本登记 `TODO-04`。

### 6.2 落地后像素读数（截图 `.scratch/caption-theme/caption-*-top.png`）

| 量 | 浅色 `--theme light` | 深色（默认） |
|---|---|---|
| caption 底色 | `#f8f9fb`（248,249,251） | `#26282d`（38,40,45） |
| 标题字色 | `#23262e`（抗锯齿边缘） | `#ebecf0`（235,236,240） |
| close glyph | `#696f7b`（105,111,123） | `#cfcfd5`（207,209,213） |
| min / max / close 墨迹量（9×9 窗口） | 1 横线 / 4 线方框 / 2 对角线 | 同左，逐格一致 |

⇒ **底色、标题字、三钮 glyph 全部随主题切换**；三钮几何与原生逐格一致（ASCII 墨迹图人工核对过）。
深色令牌与 ECDI 原生**逐值相同** ⇒ 深色回归基线不变。

### 6.3 两个非显然的坑

**① 浅色 hover 必须是「黑」预混，不是白。** ECDI 原生 hover/pressed 是白 @26 / 白 @45
（`kHoverBg` / `kPressedBg`）——在深色底上是「提亮」，照搬到浅色 `#f8f9fb` 上等于**无变化**
（白叠白）。浅色令牌改成 `(0,0,0,26)` / `(0,0,0,45)`，与深色方向相反，同 §5.3① 的
「两套主题层次方向相反」是同一个根因。半透明合成本身可用：`Color.a<1` 时 `DrawRect` 走
`BlendAlphaSolid`（`GDIBackend.cpp:649`），REQ-11 只影响 `DrawText`/`DrawFocusRect`。

**② 派生类释放基类按钮后，绝不能调 `CaptionBar::SetSize` / `CaptionBar::OnPaint`。**
`CaptionBar.cpp:78-84` 的 `SetSize` 会调私有的 `RelayoutChildren()`（解引用 `m_minButton` 等），
`:116-129` 的 `OnPaint` 会调 `m_maxButton->SetGlyph(...)`。ThemedCaptionBar 在构造里把三个原生
按钮 `RemoveChild` 掉了 ⇒ 这两个 override 必须限定到 `ECDI::Widget::SetSize` / `ECDI::Panel::OnPaint`
基类实现，并在自己的 `OnPaint` 里按 `GetWindowState()` 同步 max glyph。否则是 use-after-free
（不是「可能」，是必然——`SetSize` 每次布局 Arrange 都会被调）。



