#pragma once
// harness 自绘标题栏 —— 让 ECDI 的系统 CaptionBar 跟随 `--theme dark|light` 切色。
//
// ── 为什么要有这个头 ────────────────────────────────────────────
// ECDI 的 CaptionBar / CaptionButton 把所有颜色写死在实现里：
//   `CaptionBar.cpp:31`   背景 `Color::FromRGBA8(38, 40, 45, 255)`
//   `CaptionBar.cpp:37`   标题 `Color::FromRGBA8(235, 236, 240, 235)`（注释原文：
//                         「DefaultTheme 的文本色是黑色（不可读于深色标题栏），
//                           而本阶段不提供标题样式 API」）
//   `CaptionButton.cpp:14-27` glyph / hover / pressed / close 红，同批硬编码
//   （`class CaptionButton final : public Widget`，连派生口子都没有）。
// 框架侧 `Theme` 是 9 个纯虚 `Get*Style()` 的抽象基类，**没有 caption 令牌**，
// CaptionBar 构造期也不走主题通路 ⇒ 两种主题只能由应用侧覆盖。
//
// ── 覆盖手法（不碰框架本体，铁律）────────────────────────────────
// `ECDI::CaptionBar : public ECDI::Panel`，其 `PanelStyle m_style` 是 protected，
// 故派生类可直接改背景；`m_title` / `m_minButton` / `m_maxButton` / `m_closeButton`
// 是 CaptionBar 的 **private**，派生类拿不到，只能：
//   * 标题 —— `GetChildAt(0)` 拿到那个 `ECDI::Label`（AddChild 顺序 label,min,max,close），
//     调 `SetTextColor` / `SetFont`；
//   * 三个按钮 —— 从树上 `RemoveChild` 取回 unique_ptr 后**释放**，换上自绘按钮。
// ⚠️ 一旦释放基类按钮，`CaptionBar::SetSize`（→RelayoutChildren）与
//   `CaptionBar::OnPaint`（→m_maxButton->SetGlyph）都会踩野指针 ⇒
//   本类的两个 override **都不调基类 CaptionBar 的实现**：
//     SetSize → `ECDI::Widget::SetSize`（纯几何赋值）+ 自己 Relayout
//     OnPaint → `ECDI::Panel::OnPaint`（画背景）+ 自己同步 max glyph
//
// ── 视觉契约（与原生逐点对齐，只换颜色来源）─────────────────────
// * 布局常量与 `CaptionBar.cpp:19-22` 同值：kButtonWidth 46 / kTitleLeftPad 12 /
//   kTitleRightGap 8；按钮靠右序 min|max|close（X = w-138 / w-92 / w-46）。
// * 按钮几何照抄 `CaptionButton.cpp:94-114`：glyph 10×10 居中，1px 线宽；
//   minimize=1 横线 / maximize=4 线方框 / restore=6 线错位双方框 / close=2 对角线。
// * 常态按钮**不画背景**（露出标题栏底色），只有 hover / pressed 才画；
//   半透明底靠 `GDIBackend.cpp:620-651 DrawRect` 的 `BlendAlphaSolid` 合成
//   （REQ-11 只影响 DrawText/DrawFocusRect，不影响 DrawRect）。
// * 输入模型照抄 `CaptionButton.cpp:44-92`：Down 置 pressed + Invalidate；Up 清 pressed +
//   Invalidate，仅当 Up 时鼠标仍在本按钮矩形内（`GetAbsolutePosition()` + 事件坐标判据）
//   才触发回调；Enter/Leave 翻 hover。事件参数是 const 引用，标 handled 用
//   `Event::SetHandled() const noexcept`（Event.h:61，mutable m_handled）。
// * `ConsumesMouseInput() → true` 必须保留：Win32 平台层在 caption 高度内命中
//   「消费鼠标的控件」才不返回 HTCAPTION（`Win32PlatformWindow.cpp:649-729`），
//   否则点按钮会变成拖动窗口。
// * 拖拽：标题 Label 不消费鼠标 ⇒ 点标题文字仍可移动窗口（与原生一致）。

// * `ECDI::Window` 引用自己持一份：基类 `m_window` 是 private，派生类不可见，
//   而 OnPaint（同步 max glyph）与 ToggleMaximizeRestore 都要用。

#include "ECDI/Window/CaptionBar.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/WindowState.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Widget/TextWidget.h"
#include "ECDI/EventSystem/Input/Mouse/MouseEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/Core/Rect.h"
#include "Theme.h"

#include <algorithm>
#include <functional>
#include <memory>

namespace harness {

/// 自绘标题按钮 —— 行为对齐 ECDI CaptionButton，颜色读 Theme.h 令牌。
/// ECDI 的 `CaptionButton` 是 final 且无样式 API，只能整体替换。
class ThemedCaptionButton : public ECDI::Widget {
public:
    enum class Glyph { Minimize, Maximize, Restore, Close };

    ThemedCaptionButton(Glyph glyph, std::function<void()> onClick)
        : m_glyph(glyph), m_onClick(std::move(onClick)) {}

    void SetGlyph(Glyph glyph) noexcept { m_glyph = glyph; }

    bool ConsumesMouseInput() const noexcept override { return true; }

protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override {
        const bool isClose = (m_glyph == Glyph::Close);
        const ECDI::Rect self{ (float)x, (float)y,
                              (float)GetWidth(), (float)GetHeight() };
        if (m_pressed) {
            ctx.DrawRect(self, isClose ? kCaptionClosePressed : kCaptionPressed);
        } else if (m_hovered) {
            ctx.DrawRect(self, isClose ? kCaptionCloseHover : kCaptionHover);
        }

        const ECDI::Color pen = (m_hovered && isClose)
            ? ECDI::Color::FromRGBA8(255, 255, 255, 255)
            : (m_hovered ? kCaptionGlyphHot : kCaptionGlyph);
        const float cx = x + GetWidth() * 0.5f;
        const float cy = y + GetHeight() * 0.5f;
        const float gx = cx - 5.0f, gy = cy - 5.0f;
        const float gx2 = gx + 10.0f, gy2 = gy + 10.0f;
        const ECDI::Point lt{ gx, gy }, rt{ gx2, gy }, lb{ gx, gy2 }, rb{ gx2, gy2 };
        switch (m_glyph) {
        case Glyph::Minimize:
            ctx.DrawLine(lt, rt, 1.0f, pen);
            break;
        case Glyph::Maximize:
            ctx.DrawLine(lt, rt, 1.0f, pen);
            ctx.DrawLine(rt, rb, 1.0f, pen);
            ctx.DrawLine(rb, lb, 1.0f, pen);
            ctx.DrawLine(lb, lt, 1.0f, pen);
            break;
        case Glyph::Restore:
            ctx.DrawLine({ gx + 3.0f, gy }, rt, 1.0f, pen);
            ctx.DrawLine(rt, { gx2, gy + 7.0f }, 1.0f, pen);
            ctx.DrawLine({ gx, gy + 3.0f }, { gx + 7.0f, gy + 3.0f }, 1.0f, pen);
            ctx.DrawLine({ gx, gy + 3.0f }, lb, 1.0f, pen);
            ctx.DrawLine(lb, { gx + 7.0f, gy2 }, 1.0f, pen);
            ctx.DrawLine({ gx + 7.0f, gy + 3.0f }, { gx + 7.0f, gy2 }, 1.0f, pen);
            break;
        case Glyph::Close:
            ctx.DrawLine(lt, rb, 1.0f, pen);
            ctx.DrawLine(rt, lb, 1.0f, pen);
            break;
        }
    }

    void OnMouseButtonDown(const ECDI::MouseButtonDownEvent& event) override {
        m_pressed = true;
        Invalidate();
        event.SetHandled();
    }

    void OnMouseButtonUp(const ECDI::MouseButtonUpEvent& event) override {
        m_pressed = false;
        Invalidate();
        const ECDI::Point abs = GetAbsolutePosition();
        const int mx = event.GetMouseX(), my = event.GetMouseY();
        const bool inside = mx >= abs.x && mx < abs.x + GetWidth() &&
                            my >= abs.y && my < abs.y + GetHeight();
        if (inside && m_onClick) {
            m_onClick();
        }
        event.SetHandled();
    }

    void OnMouseEnter() override {
        m_hovered = true;
        Invalidate();
    }

    void OnMouseLeave() override {
        m_hovered = false;
        m_pressed = false;
        Invalidate();
    }

private:
    Glyph m_glyph;
    std::function<void()> m_onClick;
    bool m_hovered = false;
    bool m_pressed = false;
};

/// 自绘标题栏 —— 构造签名与 ECDI::CaptionBar 一致，可直接替换 `App.cpp:388` 的构造点。
class ThemedCaptionBar : public ECDI::CaptionBar {
public:
    ThemedCaptionBar(ECDI::Window& window, const std::string& title = std::string())
        : ECDI::CaptionBar(window, title), m_window(window) {
        // 基类构造已 AddChild(label, min, max, close) 并 RelayoutChildren()；
        // 这里把三个原生按钮摘掉换成自绘按钮，并给标题上主题色。
        // ★ 逆序摘除：GetChildCount() 在循环里会变小，索引从尾部往前扫才稳。
        for (size_t i = GetChildCount(); i-- > 1;) {
            RemoveChild(GetChildAt(i));   // unique_ptr 出作用域即释放 ⇒ 基类指针同步失效
        }
        auto* label = dynamic_cast<ECDI::TextWidget*>(GetChildAt(0));
        if (label) {
            label->SetTextColor(kCaptionInk);
            label->SetFont(SansFont(kFontBody));
        }

        auto minBtn = std::make_unique<ThemedCaptionButton>(
            ThemedCaptionButton::Glyph::Minimize, [&window] { window.Minimize(); });
        auto maxBtn = std::make_unique<ThemedCaptionButton>(
            ThemedCaptionButton::Glyph::Maximize, [this, &window] { ToggleMaximizeRestore(); });
        auto closeBtn = std::make_unique<ThemedCaptionButton>(
            ThemedCaptionButton::Glyph::Close, [&window] { window.RequestClose(); });
        m_minButton = minBtn.get();
        m_maxButton = maxBtn.get();
        m_closeButton = closeBtn.get();
        AddChild(std::move(minBtn));
        AddChild(std::move(maxBtn));
        AddChild(std::move(closeBtn));

        SetStyle(ECDI::PanelStyleOverride{ .background = kCaptionBg });
        Relayout();
        // ★ `PanelStyle::background` 的 StyleField 已被上面这次 SetStyle 置值 ⇒
        //   主题只需在构造前由 `harness::SwitchTheme()` 定好（App.cpp:338 已经这么做了），
        //   不存在「运行期切主题重绘 caption」的路径——与全仓其它控件同一约束。
    }

    void SetSize(int w, int h) override {
        // ★ 不调 `ECDI::CaptionBar::SetSize`：它的 RelayoutChildren() 会解引用
        //   已被释放的基类按钮 ⇒ 野指针。Widget 基类实现就是纯几何赋值。
        ECDI::Widget::SetSize(w, h);
        Relayout();
    }

protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override {
        // ★ 不调 `ECDI::CaptionBar::OnPaint`：它会在每帧解引用 m_maxButton。
        if (m_window.GetWindowState() == ECDI::WindowState::maximized) {
            m_maxButton->SetGlyph(ThemedCaptionButton::Glyph::Restore);
        } else {
            m_maxButton->SetGlyph(ThemedCaptionButton::Glyph::Maximize);
        }
        ECDI::Panel::OnPaint(ctx, x, y);
    }

private:
    void Relayout() {
        // 与 CaptionBar.cpp:86-114 同几何：closeX=w-46 / maxX=w-92 / minX=w-138，
        // 标题 SetPosition(12,0) + titleW = max(0, minX-12-8)。
        const int w = GetWidth();
        const int h = GetHeight();
        const int closeX = w - 46;
        const int maxX = w - 92;
        const int minX = w - 138;
        m_closeButton->SetPosition(closeX, 0);
        m_closeButton->SetSize(46, h);
        m_maxButton->SetPosition(maxX, 0);
        m_maxButton->SetSize(46, h);
        m_minButton->SetPosition(minX, 0);
        m_minButton->SetSize(46, h);
        if (auto* label = dynamic_cast<ECDI::TextWidget*>(GetChildAt(0))) {
            const int titleW = (std::max)(0, minX - 12 - 8);
            label->SetPosition(12, 0);
            label->SetSize(titleW, h);
        }
    }

    void ToggleMaximizeRestore() {
        if (m_window.GetWindowState() == ECDI::WindowState::maximized) {
            m_window.Restore();
        } else {
            m_window.Maximize();
        }
    }

    ECDI::Window& m_window;   // 本类自己的副本（基类 m_window 是 private，不可见）；
                              // 初始化列表里从构造参数拷来 —— 引用成员只能这么绑。
    ThemedCaptionButton* m_minButton = nullptr;
    ThemedCaptionButton* m_maxButton = nullptr;
    ThemedCaptionButton* m_closeButton = nullptr;
};

} // namespace harness
