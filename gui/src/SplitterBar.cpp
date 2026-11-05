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

namespace {

// 系统预定义光标由系统共享、可重复加载（ECDI 的 CursorHandleFor 同款前提），
// 故缓存句柄并与 ::GetCursor() 按值比较是安全的。
HCURSOR SizeWeCursor() noexcept {
    static HCURSOR h = ::LoadCursor(nullptr, IDC_SIZEWE);
    return h;
}

HCURSOR ArrowCursor() noexcept {
    static HCURSOR h = ::LoadCursor(nullptr, IDC_ARROW);
    return h;
}

// 分割条只有 3px 宽：GetAbsolutePosition() 与 HitTest 的浮点 / 内容偏移口径
// 只要差 1-2px，几何判据就会整片 miss ⇒ 鼠标明明在分割条上却不显示拉伸光标，
// 3px 热区也就找不到（用户 14:51 反馈「拉不动、不跟手」的直接嫌疑）。
// ⇒ 光标判据留容差；它**只影响提示形态**，实际拖拽命中仍由 ECDI 的 HitTest 决定。
constexpr float kCursorTolerance = 16.0f;

// 客户区坐标 (x, y) 是否落在这个控件的可见矩形（含容差）内。
// ★ 依据：MouseEvent 携带窗口客户区坐标（MouseEvent.h:12, 20-21）；
//   Widget::GetAbsolutePosition() 沿父链累加 GetX()-GetContentOffsetX()
//   （Widget.cpp:785-804），是 HitTest 递归「x - child->x + contentOffset」
//   （Widget.cpp:368-369）的逆变换 ⇒ 同基准，但边界用容差吸收口径差。
bool PointerOverBar(const ECDI::Widget& w, int x, int y) noexcept {
    const ECDI::Point abs = w.GetAbsolutePosition();
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    return fx >= abs.x - kCursorTolerance &&
           fx < abs.x + static_cast<float>(w.GetWidth()) + kCursorTolerance &&
           fy >= abs.y - kCursorTolerance &&
           fy < abs.y + static_cast<float>(w.GetHeight()) + kCursorTolerance;
}

// 只有「当前光标确实是我们设下的 SIZEWE」时才回退成 Arrow：
// 不覆盖 TextBox 的 IBeam 等由 WM_SETCURSOR 正规路径设下的形态。
void RestoreArrowIfSizeWe() noexcept {
    if (::GetCursor() == SizeWeCursor()) {
        ::SetCursor(ArrowCursor());
    }
}

}  // namespace

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
    // ★★ 2026-11-05：光标按「鼠标是否真的落在自己的热区」决定，
    //   而不是按「这条 Move 是否被派发给我」决定。
    //   ECDI 的 capture 契约（Application.cpp:371-387：捕获存在时 hover 冻结、
    //   Move 直派捕获者、跳过 HitTest）意味着控件可能在鼠标早已离开后仍收到 Move；
    //   原来无条件 SetCursor(SIZEWE) 会让光标永久卡在拉伸态（用户实测：
    //   设置 → 点服务商行 → 关闭 → 扫过分割条后不恢复）。
    //   ★ 这是 harness 侧修复，不要求 ECDI 任何改动。
    const bool overMe = PointerOverBar(*this, event.GetMouseX(), event.GetMouseY());
    if (overMe || m_dragging) {
        ::SetCursor(SizeWeCursor());
    } else {
        RestoreArrowIfSizeWe();
    }

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
        // ★ 松手时若指针已不在热区，别把 SIZEWE 留在屏幕上
        //   （拖拽期间 OnMouseLeave 刻意不恢复，这里补上出口）。
        if (!PointerOverBar(*this, event.GetMouseX(), event.GetMouseY())) {
            RestoreArrowIfSizeWe();
        }
        Invalidate();
    }
}

void SplitterBar::OnMouseEnter() {
    m_hovered = true;
    ::SetCursor(SizeWeCursor());
    Invalidate();
}

void SplitterBar::OnMouseLeave() {
    if (!m_dragging) {
        m_hovered = false;
        // ★ 对称恢复：进入时设了 SIZEWE，离开必须还回去。
        //   脱树 / hover 冻结等路径不保证派发 Leave（ECDI HoverTracker 契约 C，
        //   HoverTracker.cpp:86），故 OnMouseMove 里另有几何判据兜底。
        RestoreArrowIfSizeWe();
        Invalidate();
    }
}
