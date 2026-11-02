#include "SplitterBar.h"

#include "Theme.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Window/Window.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>

SplitterBar::SplitterBar(Side side, ECDI::Widget* target, int minWidth, int maxWidth, int defaultWidth)
    : m_side(side),
      m_target(target),
      m_minWidth(minWidth),
      m_maxWidth(maxWidth),
      m_defaultWidth(defaultWidth) {
    // 默认给 3px 宽的拖拽热区，高度由父容器 fillCrossAxis 撑满
    SetSize(3, 100);
}

void SplitterBar::OnPaint(ECDI::PaintContext& ctx, int x, int y) {
    const int w = GetWidth();
    const int h = GetHeight();
    if (w <= 0 || h <= 0) return;

    // 底色与相邻侧栏对齐，避免穿透
    const ECDI::Color& bg = (m_side == Side::Left) ? harness::kSidebarBg : harness::kInspBg;
    ctx.DrawRect(ECDI::Rect(static_cast<float>(x), static_cast<float>(y),
                            static_cast<float>(w), static_cast<float>(h)), bg);

    // 绘制中心细分割线：常态为 kLine，悬停或拖拽时高亮为 kDarkAccent
    const int lineX = x + w / 2;
    const ECDI::Color& lineColor = (m_dragging || m_hovered) ? harness::kDarkAccent : harness::kLine;
    ctx.DrawLine(ECDI::Point(static_cast<float>(lineX), static_cast<float>(y)),
                 ECDI::Point(static_cast<float>(lineX), static_cast<float>(y + h)),
                 1.0f, lineColor);
}

void SplitterBar::OnMouseMove(const ECDI::MouseMoveEvent& event) {
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));

    if (m_dragging && m_target) {
        if (!m_target->IsVisible()) {
            m_target->SetVisible(true);
        }
        const int delta = event.GetMouseX() - m_startX;
        int newWidth = (m_side == Side::Left) ? (m_startWidth + delta) : (m_startWidth - delta);
        newWidth = std::clamp(newWidth, m_minWidth, m_maxWidth);
        if (newWidth != m_target->GetWidth()) {
            m_target->SetSize(newWidth, m_target->GetHeight());
            if (m_onResize) {
                m_onResize(newWidth);
            }
        }
    }
}

void SplitterBar::OnMouseButtonDown(const ECDI::MouseButtonDownEvent& event) {
    if (event.GetButton() == ECDI::MouseButton::Left) {
        if (event.IsDoubleClick() && m_target) {
            // 双击快速复位到默认宽度
            m_target->SetSize(m_defaultWidth, m_target->GetHeight());
            if (m_onResize) m_onResize(m_defaultWidth);
            if (m_onDragEnd) m_onDragEnd(m_defaultWidth);
            m_dragging = false;
            if (event.GetWindow()) {
                event.GetWindow()->SetCaptureWidget(nullptr);
            }
            Invalidate();
            return;
        }

        m_dragging = true;
        m_startX = event.GetMouseX();
        m_startWidth = m_target ? m_target->GetWidth() : 0;
        // ★ 核心：使用 ECDI Window 级别的 SetCaptureWidget，确保鼠标快速移动到任何位置事件都不会脱手丢失
        if (event.GetWindow()) {
            event.GetWindow()->SetCaptureWidget(this);
        }
        Invalidate();
    }
}

void SplitterBar::OnMouseButtonUp(const ECDI::MouseButtonUpEvent& event) {
    if (event.GetButton() == ECDI::MouseButton::Left && m_dragging) {
        m_dragging = false;
        if (event.GetWindow()) {
            event.GetWindow()->SetCaptureWidget(nullptr);
        }
        if (m_onDragEnd && m_target) {
            m_onDragEnd(m_target->GetWidth());
        }
        Invalidate();
    }
}

void SplitterBar::OnMouseEnter() {
    m_hovered = true;
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
    Invalidate();
}

void SplitterBar::OnMouseLeave() {
    if (!m_dragging) {
        m_hovered = false;
        Invalidate();
    }
}
