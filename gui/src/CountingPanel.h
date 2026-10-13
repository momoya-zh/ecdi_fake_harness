#pragma once
// 应用自有的重绘计量点（规格 §4.2）。
// Window::GetRootWidget() 返回 Widget&，应用无法子类化 root（基线 B1），
// 因此作为 root 的唯一子节点铺满客户区——每次窗口重绘 OnPaint 都会被调用。

#include "ECDI/Widget/Panel.h"

#include <chrono>
#include <cstdint>
#include <vector>

class CountingPanel : public ECDI::Panel {
public:
    size_t PaintCount() const { return m_paintCount; }
    const std::vector<uint64_t>& FrameDurationsUs() const { return m_frameUs; }

protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override;

private:
    size_t m_paintCount = 0;
    std::vector<uint64_t> m_frameUs;
};
