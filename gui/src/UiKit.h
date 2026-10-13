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

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"

#include <memory>
#include <string>

namespace uikit {

inline const char* kMonoFamily = "Consolas";

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
    ECDI::Font f;
    f.size = size;
    if (mono) {
        f.family = kMonoFamily;
    }
    label->SetFont(f);
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

} // namespace uikit