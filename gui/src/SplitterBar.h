#pragma once

#include "ECDI/Widget/Widget.h"
#include <functional>

/// @brief 可拖拽分割条（用于左右侧栏与中栏之间的宽度拉伸调整）
class SplitterBar : public ECDI::Widget {
public:
    enum class Side {
        Left,   // 关联左侧栏（向右拖变宽，向左拖收缩）
        Right   // 关联右侧栏（向左拖变宽，向右拖收缩）
    };

    SplitterBar(Side side, ECDI::Widget* target, int minWidth, int maxWidth, int defaultWidth);
    ~SplitterBar() override = default;

    void SetTarget(ECDI::Widget* target) { m_target = target; }
    void SetRange(int minW, int maxW) { m_minWidth = minW; m_maxWidth = maxW; }
    void SetDefaultWidth(int defW) { m_defaultWidth = defW; }

    /// @brief 拖拽中尺寸变动回调（用于触发父级 Arrange 与 Relayout）
    void SetOnResize(std::function<void(int newWidth)> cb) { m_onResize = std::move(cb); }

    /// @brief 拖拽结束松手回调（用于配置持久化保存）
    void SetOnDragEnd(std::function<void(int finalWidth)> cb) { m_onDragEnd = std::move(cb); }

    [[nodiscard]] bool ConsumesMouseInput() const noexcept override { return true; }

    /// @brief 悬停形态自述（★ ECDI #87 / REQ-19-A）：左右拉伸光标交给框架设。
    /// @details 平台层每条 `WM_SETCURSOR` 都按命中控件重查一遍 ⇒ 鼠标离开后光标
    ///          自动换回目标控件的形态，本控件**不需要**任何设/恢复逻辑。
    ///          命中由 `HitTestWindow` 决定，故也不需要几何判据（历史上那 44 行
    ///          绕行——几何容差 + `::GetCursor()` 值比较 + 三处出口恢复——已随
    ///          `CursorShape` 补齐值域整块删除）。
    [[nodiscard]] ECDI::CursorShape GetCursor() const noexcept override {
        return ECDI::CursorShape::SizeWE;
    }

protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override;
    void OnMouseMove(const ECDI::MouseMoveEvent& event) override;
    void OnMouseButtonDown(const ECDI::MouseButtonDownEvent& event) override;
    void OnMouseButtonUp(const ECDI::MouseButtonUpEvent& event) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;

private:
    Side m_side;
    ECDI::Widget* m_target = nullptr;
    int m_minWidth = 100;
    int m_maxWidth = 600;
    int m_defaultWidth = 240;

    bool m_hovered = false;
    bool m_dragging = false;
    int m_startX = 0;
    int m_startWidth = 0;

    std::function<void(int)> m_onResize;
    std::function<void(int)> m_onDragEnd;
};
