#include "CountingPanel.h"

void CountingPanel::OnPaint(ECDI::PaintContext& ctx, int x, int y) {
    const auto start = std::chrono::steady_clock::now();
    ECDI::Panel::OnPaint(ctx, x, y);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    ++m_paintCount;
    m_frameUs.push_back(static_cast<uint64_t>(us));
}
