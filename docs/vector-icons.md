# 用 ECDI 图元手绘矢量图标（harness 侧 playbook）

> 面向：在 `ECDI_fake_harness/gui/` 里加图标、或接手 `--menu-probe` / `--model-probe` 相关 UI 的人（含 agent）。
> 铁律：**不改 ECDI 本体**。所有手法都在 `gui/src/` 内完成。
> 依据时点：2026-10-26，`third_party/ecdi` 快照（含 `DrawArc`，Phase 33）。

---

## 0. 一句话结论

ECDI **没有** `DrawPath`（画不了任意 SVG 贝塞尔），但**有** `DrawLine` / `DrawArc` / `DrawRoundedRect` 三个够用的图元 + **有** `DrawImage`（位图路线也通）。
`Counter`、`Minimize`、`Close` 这类 10–14px 界面图标，**全部可以用几何原语画出来**，且 GDI / GL 两后端**逐像素一致**、永不缺字。

现成先例：`gui/src/ThemedCaptionBar.h` 的 `ThemedCaptionButton`（窗口右上四个按钮）。
⚠️ **但别照抄它的继承关系**——它是 `public ECDI::Widget`（`:67`），**不调基类 `OnPaint`**，hover/pressed 自己管。原因见 §5.1。

---

## 1. 能力边界（先认清，别猜）

| 想做的事 | 能不能 | 依据 |
|---|---|---|
| 画直线 / 圆 / 圆角矩形 | ✅ | `DrawLine` / `DrawArc` / `DrawRoundedRect` |
| 画任意曲线（SVG path / 贝塞尔） | ❌ | 无 `DrawPath` 原语 |
| 贴 PNG / 位图 | ✅ | `PaintContext::DrawImage(Rect, const Image&)`；GDI 与 GL **都已实现**（`GLRenderer.cpp:459`） |
| 用 Unicode 符号当图标（`⚙` `☷` `⋯`） | ⚠️ **不可靠** | GDI **无字体回退**：全局锁 Microsoft YaHei，雅黑没有的字形直接画方框 |
| 换字体绕过缺字 | ❌ | 换 family 会让度量漂移（`AppUiChrome.cpp:341` 记录的 ≡ 字形下移 bug） |

> ⚠️ 常见误传两条，别再传播：① 「ECDI 没有位图原语」——**假的**，`DrawImage` 在。② 「原语是 7 个含 `DrawBorder`」——**`DrawBorder` 全仓不存在**，真实面是 `PaintContext.h` 里那 10 个方法。

### 为什么仍然推荐矢量而不是 PNG
1. **零资源**：不用打包 PNG、不用管 DPI 倍图、不用 `ImageDecoder` 初始化。
2. **主题可变**：颜色是运行期参数。PNG 想跟随深浅主题得备两套。
3. **逐像素可复现**：GDI 与 GL 走同一条 `RenderCommand` 序列，图标不会两链不一样（`DrawImage` 则会引入采样差异）。

---

## 2. 图元速查（`ECDI/Render/PaintContext.h`）

```cpp
void DrawRect(const Rect& r, const Color& c);                                   // :34
void DrawText(const Point& pos, const std::string& s, const Color& c,
              const Font& font = Font());                                       // :38
void DrawLine(const Point& a, const Point& b, float width, const Color& c);     // :43
void DrawArc(const Point& center, float radius,
             float startAngleDeg, float sweepAngleDeg,                          // :51
             float width, const Color& c);
void DrawRoundedRect(const Rect& r, float cornerRadius, const Color& c);        // :56
void DrawImage(const Rect& dest, const Image& img);                             // :61
void PushClip(const Rect& r);  void PopClip();                                  // :64 / :67
bool IsRectVisible(const Rect& r) const;                                        // :73
void DrawFocusRect(const Rect& r, float cornerRadius, const Color& c);          // :76
Size MeasureText(const Font&, const std::string&);  float LineHeight(const Font&);
```

`DrawArc` 的角度约定（`:47-49`）：**`0°` = 3 点钟方向，正角 = 顺时针**，单位是度，`|sweep| > 360` 由后端钳制。框架**不做**角度归一化，所以传负数会如你所愿地反向。

`OnPaint(PaintContext& ctx, int x, int y)` 的 `x/y` 是**本控件自己的绘制原点**（已经算好父链偏移），矩形一律用
`Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() }` 构造——照抄 `Button.cpp:280-298` 的写法，别自己去 `GetAbsolutePosition()`。

---

## 3. 图标几何规格（14px 视觉框）

统一约定：以**控件中心为原点**、视觉框 **14×14**、线宽 **1.0f**、笔画颜色 = ink。坐标写成相对量，控件多大都居中。

| 图标 | 画法（相对中心） |
|---|---|
| `Hamburger` 侧栏 | 3× `DrawLine`：`y=-4/0/+4`，`x∈[-6,6]` |
| `PanelLeft` 检视栏（左） | `DrawRoundedRect((-6,-4.5,12,9), r=2)` + `DrawLine(x=-2.4, y∈[-4.5,4.5])` |
| `PanelRight` 检视栏（右） | 同上，竖线 `x=+2.4` |
| `Share` 导出 | 开口向上的方框 `(-6,-1)→(-6,5)→(6,5)→(6,-1)` + 竖线 `(0,2)→(0,-5)` + 箭头 `(-3,-2)→(0,-5)→(3,-2)` |
| `Dots` 更多 | 3× `DrawRoundedRect`，边长 `2.7`、`r=1.35`、圆心 `x=-4.6/0/+4.6`（左上 = 圆心 - 1.35） |
| `Sun` 太阳 | `DrawArc(center, 3.1, 0, 360, 1.0)` + 8 条辐条：`r∈[4.7,6.2]`，方向 `0/45/…/315°` |
| `Moon` 月亮 | `DrawArc(center, 4.4, 60, 240, 1.0)`——**最省的近似**：一段 240° 开口弧。真月牙要两段弧 + 填充遮罩，14px 下看不出差别，不值得 |
| `ChevronDown` 下拉 | 2× `DrawLine`：`(-4,-1.5)→(0,2.5)→(4,-1.5)` |

两个实操细节：
- **端点不是圆头**：三条横线各自定长，别指望端点粘成矩形。
- **1px 线可能发虚**：Phase 24 给 `DrawLine` 加了覆盖栅格 AA，125% 缩放下 1.0f 线宽会摊到 1.25 设备像素。**先用整数坐标试**；若目视发灰，把坐标整体 +0.5f（不要改线宽）。这条必须目视一次，别照抄结论。

---

## 4. 参考实现（可整段抄进 `gui/src/UiKit.h`）

```cpp
// ── 矢量图标按钮 ────────────────────────────────────────────────
// 为什么继承 TipButton 而不是 Button：tooltip 的 enter/leave 回调只有 TipButton 有。
// 为什么继承 Button 系而不是像 ThemedCaptionButton 那样纯 Widget：
//   按钮的圆角底 / hover 120ms 色过渡 / pressed 态 / 点击消费 全在 Button 里实现了，
//   纯 Widget 派生要把这些重写一遍（ThemedCaptionButton 那么做是因为系统 CaptionButton
//   是 final 且无样式 API，它必须整体替换——**不是**通用范式）。
enum class Icon { None, Hamburger, PanelLeft, PanelRight, Share, Dots, Sun, Moon, ChevronDown };

class IconButton : public TipButton {
public:
    using EnterFn = std::function<void(TipButton*)>;
    using LeaveFn = std::function<void(TipButton*)>;

    IconButton(Icon icon, std::string tip, EnterFn onEnter, LeaveFn onLeave)
        : TipButton("", std::move(tip), std::move(onEnter), std::move(onLeave)),
          m_icon(icon) {}

    void SetIcon(Icon icon) {
        if (icon == m_icon) return;      // 同值守卫：否则每帧 Invalidate 自激励
        m_icon = icon;
        Invalidate();
    }
    Icon GetIcon() const noexcept { return m_icon; }

    // ★ 文本模式仍然可用：SetText 非空 ⇒ §5.4 的"文本优先"分支生效。
protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override {
        TipButton::OnPaint(ctx, x, y);   // 底色 / 圆角 / 焦点框 / 文本 全归基类
        if (m_icon == Icon::None || !GetText().empty()) return;
        DrawGlyph(ctx, x, y, m_hovered ? m_inkHot : m_ink);
    }

    // ★ Button 的 m_pressed / m_hovered 是 private ⇒ 图标色只能自己记
    void OnMouseEnter() override { m_hovered = true;  TipButton::OnMouseEnter(); }
    void OnMouseLeave() override { m_hovered = false; TipButton::OnMouseLeave(); }

private:
    static constexpr float kBox = 7.0f;  // 半框 = 14/2

    void DrawGlyph(ECDI::PaintContext& ctx, int x, int y, const ECDI::Color& ink) const {
        const float cx = static_cast<float>(x) + GetWidth()  * 0.5f;
        const float cy = static_cast<float>(y) + GetHeight() * 0.5f;
        const ECDI::Point c{ cx, cy };
        auto pt = [&](float dx, float dy) { return ECDI::Point{ cx + dx, cy + dy }; };
        auto line = [&](float x1, float y1, float x2, float y2) {
            ctx.DrawLine(pt(x1, y1), pt(x2, y2), 1.0f, ink);
        };
        switch (m_icon) {
        case Icon::Hamburger:
            line(-6, -4, 6, -4); line(-6, 0, 6, 0); line(-6, 4, 6, 4);
            break;
        case Icon::PanelLeft:
        case Icon::PanelRight: {
            const float sx = (m_icon == Icon::PanelLeft) ? -2.4f : 2.4f;
            ctx.DrawRoundedRect(ECDI::Rect{ cx - 6, cy - 4.5f, 12, 9 }, 2.0f, ink);
            line(sx, -4.5f, sx, 4.5f);
            break;
        }
        case Icon::Share:
            line(-6, -1, -6, 5); line(-6, 5, 6, 5); line(6, 5, 6, -1);
            line(0, 2, 0, -5);   line(-3, -2, 0, -5); line(0, -5, 3, -2);
            break;
        case Icon::Dots:
            for (float dx : { -4.6f, 0.0f, 4.6f }) {
                ctx.DrawRoundedRect(ECDI::Rect{ cx + dx - 1.35f, cy - 1.35f, 2.7f, 2.7f },
                                    1.35f, ink);
            }
            break;
        case Icon::Sun:
            ctx.DrawArc(c, 3.1f, 0.0f, 360.0f, 1.0f, ink);
            for (int i = 0; i < 8; ++i) {
                const float rad = static_cast<float>(i) * 45.0f * 3.14159265f / 180.0f;
                const float sn = std::sin(rad), cs = std::cos(rad);
                line(cs * 4.7f, sn * 4.7f, cs * 6.2f, sn * 6.2f);
            }
            break;
        case Icon::Moon:
            ctx.DrawArc(c, 4.4f, 60.0f, 240.0f, 1.0f, ink);
            break;
        case Icon::ChevronDown:
            line(-4, -1.5f, 0, 2.5f); line(0, 2.5f, 4, -1.5f);
            break;
        case Icon::None:
            break;
        }
    }

    Icon m_icon;
    bool m_hovered = false;
    ECDI::Color m_ink    = harness::kDarkMuted;   // 常态
    ECDI::Color m_inkHot = harness::kDarkInk;     // hover
};
```

用法（与 `TipButton` 完全一致，只多一个 `Icon`）：

```cpp
auto btn = std::make_unique<uikit::IconButton>(
    uikit::Icon::PanelRight, "检视栏显隐（Ctrl+I）",
    [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
    [this](uikit::TipButton*) { HideTooltip(); });
btn->SetStyle(ECDI::ButtonStyleOverride{
    .background      = kPanel2,
    .border          = kLine,
    .borderWidth     = 1.0f,
    .cornerRadius    = kRadiusBtn,
    .pressedBackground = kToolHeaderPressed,
    .hoverBackground   = kToolHeaderHover});
btn->SetSize(28, 24);
btn->SetOnClick([this] { ToggleInspector(); });
```

> `ButtonStyleOverride` 的**成员顺序必须照声明序**写（`ButtonStyle.h`：background / border / borderWidth / cornerRadius / pressedBackground / hoverBackground / textAlignment / foreground）——MSVC 对乱序 designated init 报 **C7560**。

---

## 5. 六个坑（每一条都踩过或读码确认过）

### 5.1 别把 `ThemedCaptionButton` 当通用范式
它在 `:67` 是 `public ECDI::Widget`，`:79` 起自己画背景、自己管 hover/pressed、`:122` 起自己做 inside 判定。
它这么写是因为**系统 `CaptionButton` 是 `final` 且无样式 API**（`ThemedCaptionBar.h:10-11`），必须整体替换，顺手连 `OnPaint` 也不调基类（`:204-212`）。
通用图标按钮没这个约束，**继承 `Button` 系能省掉 60 行**，还白拿 hover 色过渡和点击消费。

### 5.2 焦点：点图标会动焦点，两条路都得自己收尾
`Application.cpp:438` 是：

```cpp
window.SetFocusedWidget(target->CanFocus() ? target : nullptr);
```

- 派生 `Button`（`CanFocus()` 默认 true）⇒ 焦点**转移到按钮**，输入框 caret 停闪、边框退成常态色。
- 改成 `CanFocus() → false` ⇒ 焦点被**清空**（不是保留），输入框**照样**失焦。

⇒ 唯一正解：**在点击回调里把焦点还回去**（现成先例 `AppUiChrome.cpp:362`）：

```cpp
btn->SetOnClick([this] {
    ToggleInspector();
    if (m_window) m_window->SetFocusedWidget(m_promptBar->input());   // ★ 别省
});
```

### 5.3 `m_pressed` / `m_hovered` 是 `Button` 的 private
派生类**读不到**，所以图标颜色不能"跟随按下态"——除非你 override `OnMouseEnter/Leave`（如 §4）自己记一份。**背景色**不用管，`Button` 的三态过渡（120ms）已经覆盖。

### 5.4 文本模式与图标模式共存
用户模式下中栏三钮是中文文本（`导出` / `详情` / `更多`，44px），开发模式才是图标（28px）——切换逻辑在 `AppHud.cpp:517-530` 的 `setActBtn`。

- 推荐写法：**`GetText()` 非空则走文本**（§4 的 `OnPaint` 已按此实现），`setActBtn` 把开发模式写成 `SetText("")` + `SetSize(28,24)`。
- 反过来（文本优先 + 图标兜底）会让用户模式看不到中文，是错的。
- ⚠️ **`setActBtn` 的类型是 `ECDI::Button*`**，IconButton 能直接塞进去，不用改签名。

### 5.5 两个探针会被视觉改动打中
| 位置 | 判据 | 影响 |
|---|---|---|
| `ProbeChrome.cpp:921` | chip 文本必须含 `﹀`(U+FE40) 或 `▾`(U+25BE) | 想把 chip 的箭头改成**画**出来的 chevron ⇒ 文本里就没有这个字符了 ⇒ **这条判据必须一起改**（改成查 `GetIcon() == Icon::ChevronDown`，或保留字符不动） |
| `AppHud.cpp:517-530` | `setActBtn` 比 `GetText()` 决定是否 `SetText`/`SetSize` | 图标化后这里传 `""`，否则图标永远被文本模式盖住 |

好消息：`ProbeChrome` / `ProbeSidebar` 里那些叫 `baseline` 的相是**逻辑快照**（行数/尺寸/文本），**不是像素基线**——纯视觉改动不会打爆它们。

### 5.6 点击必须消费，`ConsumesMouseInput` 必须保留
- `Button` 系的 `OnMouseButtonDown/Up` 已经 `event.SetHandled()`（`Button.cpp:173-177`，Phase 35：「Down 总是消费、Up 总是消费」）。纯 `Widget` 派生要自己写，且 `ConsumesMouseInput() → true` 不能漏——否则标题栏高度内的按钮会被平台层判成 `HTCAPTION`，点按钮变成拖窗口（`ThemedCaptionBar.h:40-42`）。
- 回调里**别重建 UI**：`Button::RaiseClick` 的栈上，`RemoveChild` 会同步析构正在执行的对象。要重建就置标志位，下一帧做（`AppUiChrome.cpp:512-520` 的 `m_uiRebuildPending`）。

---

## 6. 验收怎么做（别只看"感觉对了"）

1. **形状**：把 `IconButton` 塞进任何一个现成工具栏，`SetIcon` 逐个切一遍，人眼过一遍——14px 的图标只有人眼能判断"像不像"。
2. **两后端一致**：同一份代码跑一次默认（GDI）和 `--gl`，对比同一位置的图标。差异只允许出现在文字上（`N1` 允许 1px），**图标必须完全一致**——不一致说明有后端没实现某个图元。
3. **不发虚**：125% 缩放下截图放大 4× 看 1px 线是否有灰边。有就按 §3 做 0.5 偏移。
4. **交互**：点一下确认 ① 回调真的触发 ② 输入框 caret 还在闪（§5.2）③ 没有多余重绘（`paints`/`invalidations` 读数不该涨——hover 色过渡是 `Button` 的既有动画，别在上面再叠 `Invalidate`）。

---

## 7. 什么时候该换 `DrawImage`

以下情况**别硬画**，走位图：
- 需要真正的曲线（插图、品牌 logo、非几何图形）。
- 需要渐变/多色填充（图元只有纯色）。
- 需要形状内部镂空（图元只描边 + 填充，没有布尔运算）。

路线：`ECDI/Decode/ImageDecoder.h` 载入 → `ctx.DrawImage(dest, img)`。注意：位图会引入**两后端的采样差异**，且要自己管 DPI 倍图；能画矢量就别用。
