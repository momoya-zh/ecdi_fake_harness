#pragma once
// 文本测量探针（Phase 27 harness 接入，2026-10-05）。
// 包装真实测量器（GDITextMeasurer / FreeTypeTextMeasurer），统计：
//   1) MeasureText / LineHeight 调用次数与累计墙钟——「逐帧文本测量成本」的量具
//      （autosize 行模式的成本正是这个；Phase 26 测量缓存收益也由它兑现成读数）；
//   2) 累计测量字符数——us/char 归一化用。
// 用法：--cull-probe 时把 services.measurer 包一层（App::Run），退出摘要输出读数。

#include "ECDI/Render/TextMeasurer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace ECDI {

class MeasureProbe : public TextMeasurer {
public:
    explicit MeasureProbe(std::unique_ptr<TextMeasurer> inner);
    ~MeasureProbe() override;

    // ── 透传 ──────────────────────────────────────────────
    void Initialize(const PlatformRenderContext& context) override;
    Size MeasureText(const Font& font, const std::string& text) override;
    float LineHeight(const Font& font) override;

    // ── 读数 ──────────────────────────────────────────────
    [[nodiscard]] size_t MeasureCalls() const noexcept { return m_measureCalls; }
    [[nodiscard]] size_t LineHeightCalls() const noexcept { return m_lineHeightCalls; }
    [[nodiscard]] size_t TotalChars() const noexcept { return m_totalChars; }
    [[nodiscard]] uint64_t TotalUs() const noexcept { return m_totalUs; }
    [[nodiscard]] uint64_t MaxUs() const noexcept { return m_maxUs; }

private:
    std::unique_ptr<TextMeasurer> m_inner;
    size_t m_measureCalls = 0;
    size_t m_lineHeightCalls = 0;
    size_t m_totalChars = 0;
    uint64_t m_totalUs = 0;
    uint64_t m_maxUs = 0;
};

} // namespace ECDI