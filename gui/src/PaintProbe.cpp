#include "PaintProbe.h"

namespace ECDI {

PaintProbe::PaintProbe(std::unique_ptr<RenderingBackend> inner)
    : m_inner(std::move(inner)) {}

PaintProbe::~PaintProbe() = default;

void PaintProbe::Initialize(const PlatformRenderContext& context) {
    m_inner->Initialize(context);
}

bool PaintProbe::IsReady() const {
    return m_inner->IsReady();
}

void PaintProbe::BeginFrame(const Color& background) {
    m_frameStart = std::chrono::steady_clock::now();
    m_inner->BeginFrame(background);
}

void PaintProbe::DrawRect(const Rect& rect, const Color& color) {
    ++m_rects;
    OnCommand();
    m_inner->DrawRect(rect, color);
}

void PaintProbe::DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font) {
    ++m_texts;
    OnCommand();
    m_inner->DrawText(pos, text, color, font);
}

void PaintProbe::DrawLine(const Point& start, const Point& end,
                          float width, const Color& color) {
    ++m_lines;
    OnCommand();
    m_inner->DrawLine(start, end, width, color);
}

void PaintProbe::DrawRoundedRect(const Rect& rect, float cornerRadius,
                                 const Color& color) {
    ++m_roundedRects;
    OnCommand();
    m_inner->DrawRoundedRect(rect, cornerRadius, color);
}

void PaintProbe::DrawImage(const Rect& dest, const Image& image) {
    ++m_images;
    OnCommand();
    m_inner->DrawImage(dest, image);
}

void PaintProbe::PushClip(const Rect& rect) {
    ++m_pushClips;
    OnCommand();
    m_inner->PushClip(rect);
}

void PaintProbe::PopClip() {
    ++m_popClips;
    OnCommand();
    m_inner->PopClip();
}

void PaintProbe::DrawFocusRect(const Rect& rect, float cornerRadius,
                               const Color& color) {
    ++m_focusRects;
    OnCommand();
    m_inner->DrawFocusRect(rect, cornerRadius, color);
}

void PaintProbe::EndFrame() {
    const auto executeUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - m_frameStart)
                               .count();
    m_frameCommands.push_back(m_frameCommandsCount);
    m_frameCommandsCount = 0;
    m_frameExecuteUs.push_back(static_cast<uint64_t>(executeUs > 0 ? executeUs : 0));
    m_inner->EndFrame();
}

size_t PaintProbe::TotalCommands() const noexcept {
    size_t total = 0;
    for (const size_t n : m_frameCommands) {
        total += n;
    }
    return total;
}

size_t PaintProbe::MaxFrameCommands() const noexcept {
    size_t max = 0;
    for (const size_t n : m_frameCommands) {
        max = std::max(max, n);
    }
    return max;
}

size_t PaintProbe::AvgFrameCommands() const noexcept {
    if (m_frameCommands.empty()) {
        return 0;
    }
    return (TotalCommands() + m_frameCommands.size() / 2) / m_frameCommands.size();
}

size_t PaintProbe::LastFrameCommands() const noexcept {
    return m_frameCommands.empty() ? 0 : m_frameCommands.back();
}

uint64_t PaintProbe::TotalExecuteUs() const noexcept {
    uint64_t total = 0;
    for (const uint64_t us : m_frameExecuteUs) {
        total += us;
    }
    return total;
}

uint64_t PaintProbe::MaxExecuteUs() const noexcept {
    uint64_t max = 0;
    for (const uint64_t us : m_frameExecuteUs) {
        max = std::max(max, us);
    }
    return max;
}

uint64_t PaintProbe::AvgExecuteUs() const noexcept {
    if (m_frameExecuteUs.empty()) {
        return 0;
    }
    return (TotalExecuteUs() + m_frameExecuteUs.size() / 2) / m_frameExecuteUs.size();
}

} // namespace ECDI