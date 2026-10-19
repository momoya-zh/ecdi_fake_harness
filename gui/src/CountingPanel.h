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

    // ★★ Step 4（2026-10-19）：**计数续接**。
    //   `RebuildUi` 会拆掉整棵树 —— 本面板也随之析构，新面板从 0 开始数。
    //   若不续接，一次主题切换就会让摘要里的 `paints`/`frameCount` 从 61 掉回 3，
    //   而那正是本项目的评估读数（与 p32 基线逐位对比）⇒ **仪器被实验打坏了**。
    //   故重建前后各取一次，旧面板的读数搬进新面板。
    //   ★ 为什么不做成"永远不重建 CountingPanel"（像 0c 那样只重建转录区）：
    //     整树重建的**目的**就是验证"树可以整个拆了再长出来，状态不丢"；
    //     留一个例外，这条判据就退化成"除了那棵树都行"。
    void SeedCounters(size_t paintCount, std::vector<uint64_t> frameUs) {
        m_paintCount = paintCount;
        m_frameUs = std::move(frameUs);
    }

protected:
    void OnPaint(ECDI::PaintContext& ctx, int x, int y) override;

private:
    size_t m_paintCount = 0;
    std::vector<uint64_t> m_frameUs;
};
