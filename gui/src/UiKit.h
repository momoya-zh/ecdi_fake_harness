#pragma once
// UiKit —— harness 侧人体工学补丁层（骑在 ECDI 布局契约之上）。
// ────────────────────────────────────────────────────────────
// 契约背景（ECDI F2/D9 冻结）：布局只管摆位不管尺寸，stretch=0 的子
// "主轴保持当前尺寸" ⇒ 漏写 SetSize 的子 = 0×0 **静默不可见**。
// ECDI 短期不补"主轴填充"类便利模式（2026-10-06 用户口径：框架在修 bug + Phase 28），
// 因此 harness 在这里集中补上"尺寸这半边"：
//   * Row/VStack/Label/Dot —— 尺寸与创建强制成对，漏不掉；
//   * WarnZeroSized —— 布局完成后巡检，把"静默不可见"降级为日志告警。
// 约定：新 UI 一律经本头创建容器行；绕过者需在注释里说明理由。

#include "Theme.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
// ★ P4 E：TipButton 需要 Button 完整类型 + 合成鼠标事件（override 形参）。
#include "ECDI/Widget/Button.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/Render/PaintContext.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace uikit {

// ★ 2026-10-18：族名统一走 Theme.h 的字体工厂（中文正文随主题变、等宽恒 Consolas）。
//   本头不再自己定义字族常量 —— 单一真相源在 Theme.h。
inline const char* kMonoFamily = harness::kMonoFamily;

// 行容器：HorizontalLayout + 强制成对尺寸。
// fillCross 默认 false——行内子控件保留各自高度（圆点是圆、进度条是细条）；
// 需要"行内等高"的场景显式传 true。
inline std::unique_ptr<ECDI::Panel> Row(int w, int h, int spacing = 8,
                                        int padding = 0, bool fillCross = false) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetSize(w, h);
    row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(spacing, fillCross, padding));
    return row;
}

// 列容器：VerticalLayout + 强制成对尺寸（fillCross=false——子控件自管宽）。
inline std::unique_ptr<ECDI::Panel> VStack(int w, int h, int spacing = 6,
                                           int padding = 10) {
    auto v = std::make_unique<ECDI::Panel>();
    v->SetSize(w, h);
    v->SetLayout(std::make_unique<ECDI::VerticalLayout>(spacing, false, padding));
    return v;
}

// 定尺寸标签。AutoSize 之外的一切标签都该走这里（漏尺寸 = 0×0 不可见）。
inline std::unique_ptr<ECDI::Label> Label(const std::string& text, int w, int h,
                                          const ECDI::Color& color,
                                          float size = 12.0f, bool mono = false) {
    auto label = std::make_unique<ECDI::Label>(text);
    label->SetTextColor(color);
    label->SetFont(mono ? harness::MonoFont(size) : harness::SansFont(size));
    label->SetSize(w, h);
    return label;
}

// ── 水平居中标签（2026-10-16 用户反馈：胶囊内文字贴左）────────────────
// 根因（**已读 ECDI 源码核实**，不是从"有 override"推断的）：
//   · `ECDI::Label` 不 override `CalculateTextPosition`（`Label.h` 只声明 `OnPaint`）
//     ⇒ 走 `TextWidget` 基类默认 = **水平左对齐 + 垂直居中**（P7 定案）。
//   · **全库只有 `Button`** override 它做水平对齐，且已由 `ButtonStyle::textAlignment`
//     三态驱动（`Button.cpp:110-124`，默认 Center）。
//   · `CheckBox` / `Radio` 虽也 override，但那是**垂直**对齐到各自的 16px 指示器，
//     水平**显式放弃**（`CheckBox.cpp:56` 的 `(void)textWidth;`）
//     —— 别把这两个误当成"框架有水平居中能力"的先例。
// ★ 本派生用的正是 ECDI 预留的扩展点：`TextWidget.h:74-78` 明写
//   `CalculateTextPosition` 是「对齐策略虚方法，**不写死**」，且 `textWidth`
//   参数的注释就是「供派生类使用（**水平居中需要**；基类默认左对齐不用）」
//   ⇒ harness 侧派生即用，无需向 ECDI 提需求。
// ★ 只在**胶囊**这类定宽容器里用；转录正文/普通标签保持左对齐（阅读性更好）。
class CenteredLabel : public ECDI::Label {
public:
    using ECDI::Label::Label;

protected:
    // 水平居中 + 垂直居中。照 `Button.cpp:117` 的算法（`(GetWidth() - textWidth)/2`）。
    // ★ `textWidth` 是**文本实测宽**、不是控件宽（`TextWidget.h:75`）⇒ 相减即居中偏移。
    ECDI::Point CalculateTextPosition(int x, int y, float textWidth,
                                      float lineHeight) const override {
        const float w = static_cast<float>(GetWidth());
        const float h = static_cast<float>(GetHeight());
        float offsetX = (w - textWidth) / 2.0f;
        if (offsetX < 0.0f) offsetX = 0.0f;   // 文字比容器宽 ⇒ 贴左，不溢出（诚实降级）
        const float offsetY = (h - lineHeight) / 2.0f;
        return ECDI::Point{static_cast<float>(x) + offsetX,
                           static_cast<float>(y) + offsetY};
    }
};

// 居中标签工厂（尺寸与创建强制成对，同 `Label`）。
inline std::unique_ptr<CenteredLabel> Centered(const std::string& text, int w, int h,
                                               const ECDI::Color& color,
                                               float size = 12.0f, bool mono = false) {
    auto label = std::make_unique<CenteredLabel>(text);
    label->SetTextColor(color);
    label->SetFont(mono ? harness::MonoFont(size) : harness::SansFont(size));
    label->SetSize(w, h);
    return label;
}

// ── 右对齐标签（2026-10-27 用户要求：设置面板底部状态行靠右）──────────────
// 与 `CenteredLabel` 同一个扩展点（`TextWidget::CalculateTextPosition`），
// 只把偏移换成 `GetWidth() - textWidth`。
// ★ 超宽时的降级方向很关键：**贴左、裁尾部**（offsetX 钳 0），而不是贴右裁开头。
//   状态行的文案纪律是「可执行信息放最前」（§8.5.6 的变量名、§9.5 的
//   「再点保存确认」）⇒ 真被裁时必须裁掉后缀，不能把开头裁掉。
//   也就是说：常态右对齐，极端长文案自动退回原来的左对齐行为。
class RightLabel : public ECDI::Label {
public:
    using ECDI::Label::Label;

protected:
    ECDI::Point CalculateTextPosition(int x, int y, float textWidth,
                                      float lineHeight) const override {
        const float w = static_cast<float>(GetWidth());
        const float h = static_cast<float>(GetHeight());
        float offsetX = w - textWidth;
        if (offsetX < 0.0f) offsetX = 0.0f;   // 文字超宽 ⇒ 贴左（保住"可执行信息在前"）
        const float offsetY = (h - lineHeight) / 2.0f;
        return ECDI::Point{static_cast<float>(x) + offsetX,
                           static_cast<float>(y) + offsetY};
    }
};

// 右对齐标签工厂（尺寸与创建强制成对，同 `Label`）。
inline std::unique_ptr<RightLabel> Right(const std::string& text, int w, int h,
                                         const ECDI::Color& color,
                                         float size = 12.0f, bool mono = false) {
    auto label = std::make_unique<RightLabel>(text);
    label->SetTextColor(color);
    label->SetFont(mono ? harness::MonoFont(size) : harness::SansFont(size));
    label->SetSize(w, h);
    return label;
}

// 圆点（状态点/列表点）——Panel + 全圆角。
inline std::unique_ptr<ECDI::Panel> Dot(int d, const ECDI::Color& color) {
    auto dot = std::make_unique<ECDI::Panel>();
    dot->SetSize(d, d);
    dot->SetStyle(ECDI::PanelStyleOverride{
        .background = color, .cornerRadius = d / 2.0f});
    return dot;
}

// 垂直居中圆点单元格：放进 fillCross=true 的行里，单元格被拉伸到行高（= cellH），
// 圆点在格内手动居中——ECDI 布局无居中，直接放圆点会被跨轴拉伸成竖胶囊。
// dotOut 供 HUD 运行时翻色（SetStyle）。
inline std::unique_ptr<ECDI::Panel> DotCell(int d, const ECDI::Color& color, int cellH,
                                            ECDI::Panel** dotOut, int cellW = 0) {
    const int w = (cellW == 0 ? d : cellW);
    auto cell = std::make_unique<ECDI::Panel>();
    cell->SetSize(w, cellH);
    auto dot = std::make_unique<ECDI::Panel>();
    dot->SetSize(d, d);
    dot->SetStyle(ECDI::PanelStyleOverride{
        .background = color, .cornerRadius = d / 2.0f});
    if (dotOut) *dotOut = dot.get();
    dot->SetPosition((w - d) / 2, (cellH - d) / 2);
    cell->AddChild(std::move(dot));
    return cell;
}

// 巡检：布局完成后调用一次。命中"可见但 0×0"即 Log Warning（带树路径）——
// 把 F2/D9 契约下的静默失败变成可发现的告警。
inline void WarnZeroSized(const ECDI::Widget& w, const std::string& path = "root") {
    if (w.IsVisible() && (w.GetWidth() == 0 || w.GetHeight() == 0)) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("UiKit: 可见控件 0 尺寸 @ " + path));
    }
    const size_t n = w.GetChildCount();
    for (size_t i = 0; i < n; ++i) {
        WarnZeroSized(*w.GetChildAt(i), path + "/" + std::to_string(i));
    }
}

// ★★ P4 Phase E（2026-10-25）：带 tooltip 的按钮。
//   ECDI 无 Tooltip 原语（源码仓核实：只有 TrayIcon 的 szTip），OnMouseEnter/
//   OnMouseLeave 是 protected virtual ⇒ 挂提示只能子类化。enter/leave/点击经
//   std::function 回调宿主（App 的单例浮层 + 延迟定时器），UiKit 不依赖 App。
//   ★ 框架侧 REQ（组件级 Tooltip）已评估待登记——harness 拼 ≈ 80 行，其中
//   大半在管"单例浮层 + 定时器"，那正是框架该收走的部分。
class TipButton : public ECDI::Button {
public:
    TipButton(const std::string& text, std::string tip,
              std::function<void(TipButton*)> onEnter,
              std::function<void(TipButton*)> onLeave)
        : ECDI::Button(text), m_tip(std::move(tip)),
          m_onEnter(std::move(onEnter)), m_onLeave(std::move(onLeave)) {}

    const std::string& tip() const noexcept { return m_tip; }

protected:
    void OnMouseEnter() override {
        ECDI::Button::OnMouseEnter();
        if (m_onEnter) m_onEnter(this);
    }
    void OnMouseLeave() override {
        ECDI::Button::OnMouseLeave();
        if (m_onLeave) m_onLeave(this);
    }
    // 点击即隐藏（提示的使命到点击为止；进入按下态后提示会挡住反馈）。
    void OnMouseButtonDown(const ECDI::MouseButtonDownEvent& event) override {
        if (m_onLeave) m_onLeave(this);
        ECDI::Button::OnMouseButtonDown(event);
    }

private:
    std::string m_tip;
    std::function<void(TipButton*)> m_onEnter;
    std::function<void(TipButton*)> m_onLeave;
};

// ── 矢量图标按钮（docs/vector-icons.md §4）────────────────────────────────────
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
            const float sx = (m_icon == Icon::PanelLeft) ? -2.0f : 2.0f;
            line(-6, -4.5f, 6, -4.5f);
            line(6, -4.5f, 6, 4.5f);
            line(6, 4.5f, -6, 4.5f);
            line(-6, 4.5f, -6, -4.5f);
            line(sx, -4.5f, sx, 4.5f);
            break;
        }
        case Icon::Share:
            line(-5.5f, -0.5f, -5.5f, 4.5f);
            line(-5.5f, 4.5f, 5.5f, 4.5f);
            line(5.5f, 4.5f, 5.5f, -0.5f);
            line(0, 2, 0, -5);
            line(-3, -2, 0, -5);
            line(0, -5, 3, -2);
            break;
        case Icon::Dots:
            for (float dx : { -4.5f, 0.0f, 4.5f }) {
                ctx.DrawRoundedRect(ECDI::Rect{ cx + dx - 1.3f, cy - 1.3f, 2.6f, 2.6f },
                                    1.3f, ink);
            }
            break;
        case Icon::Sun:
            ctx.DrawArc(c, 3.0f, 0.0f, 360.0f, 1.0f, ink);
            for (int i = 0; i < 8; ++i) {
                const float rad = static_cast<float>(i) * 45.0f * 3.14159265f / 180.0f;
                const float sn = std::sin(rad), cs = std::cos(rad);
                line(cs * 4.6f, sn * 4.6f, cs * 6.2f, sn * 6.2f);
            }
            break;
        case Icon::Moon:
            ctx.DrawArc(c, 4.5f, 45.0f, 270.0f, 1.0f, ink);
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

} // namespace uikit