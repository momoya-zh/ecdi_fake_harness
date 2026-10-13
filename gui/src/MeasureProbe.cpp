#include "MeasureProbe.h"

namespace ECDI {

namespace {

uint64_t ElapsedUs(std::chrono::steady_clock::time_point start) {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    return static_cast<uint64_t>(us > 0 ? us : 0);
}

}   // namespace

MeasureProbe::MeasureProbe(std::unique_ptr<TextMeasurer> inner)
    : m_inner(std::move(inner)) {}

MeasureProbe::~MeasureProbe() = default;

void MeasureProbe::Initialize(const PlatformRenderContext& context) {
    m_inner->Initialize(context);
}

Size MeasureProbe::MeasureText(const Font& font, const std::string& text) {
    const auto start = std::chrono::steady_clock::now();
    const Size size = m_inner->MeasureText(font, text);
    const uint64_t us = ElapsedUs(start);
    ++m_measureCalls;
    m_totalChars += text.size();
    m_totalUs += us;
    m_maxUs = std::max(m_maxUs, us);
    return size;
}

float MeasureProbe::LineHeight(const Font& font) {
    const auto start = std::chrono::steady_clock::now();
    const float height = m_inner->LineHeight(font);
    m_totalUs += ElapsedUs(start);
    ++m_lineHeightCalls;
    return height;
}

} // namespace ECDI