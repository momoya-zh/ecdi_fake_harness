#pragma once
// 渲染后端计数探针（Phase 27 harness 接入；2026-10-05 升级为计时探针）。
// 包装真实后端（GDI/GL），逐帧统计：
//   1) 实际到达后端的绘制命令数（Draw* + PushClip/PopClip）——视口剔除后 = 每帧真实构建量；
//   2) 命令分类计数（rect/text/line/…）——看命令构成（转录场景 text 应占大头）；
//   3) 执行端墙钟（BeginFrame..EndFrame）——GDI vs GL 后端 A/B 的核心读数。
// 用法：--cull-probe 时把 services.renderer 包一层（App::Run），退出摘要输出读数。

#include "ECDI/Render/RenderingBackend.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

namespace ECDI {

class PaintProbe : public RenderingBackend {
public:
    explicit PaintProbe(std::unique_ptr<RenderingBackend> inner);
    ~PaintProbe() override;

    // ── 透传（全部转发内部后端，不改变行为）──────────────
    void Initialize(const PlatformRenderContext& context) override;
    bool IsReady() const override;
    void BeginFrame(const Color& background) override;
    void DrawRect(const Rect& rect, const Color& color) override;
    void DrawText(const Point& pos, const std::string& text,
                  const Color& color, const Font& font) override;
    void DrawLine(const Point& start, const Point& end,
                  float width, const Color& color) override;
    void DrawArc(const Point& center, float radius,
                 float startAngleDeg, float sweepAngleDeg,
                 float width, const Color& color) override;
    void DrawRoundedRect(const Rect& rect, float cornerRadius,
                         const Color& color) override;
    void DrawImage(const Rect& dest, const Image& image) override;
    void PushClip(const Rect& rect) override;
    void PopClip() override;
    void DrawFocusRect(const Rect& rect, float cornerRadius,
                       const Color& color) override;
    void EndFrame() override;

    // ── 读数（帧 = BeginFrame..EndFrame 完整周期）─────────
    [[nodiscard]] size_t FrameCount() const noexcept { return m_frameCommands.size(); }
    [[nodiscard]] size_t TotalCommands() const noexcept;
    [[nodiscard]] size_t MaxFrameCommands() const noexcept;
    [[nodiscard]] size_t AvgFrameCommands() const noexcept;
    [[nodiscard]] size_t LastFrameCommands() const noexcept;

    // 执行端墙钟（us；BeginFrame 进 → EndFrame 出，含全部命令执行 + 呈现）
    [[nodiscard]] uint64_t TotalExecuteUs() const noexcept;
    [[nodiscard]] uint64_t MaxExecuteUs() const noexcept;
    [[nodiscard]] uint64_t AvgExecuteUs() const noexcept;

    // 命令分类（生命周期累计——转录场景 text 占比是富文本需求的直接证据）
    [[nodiscard]] size_t RectCommands() const noexcept { return m_rects; }
    [[nodiscard]] size_t TextCommands() const noexcept { return m_texts; }
    [[nodiscard]] size_t LineCommands() const noexcept { return m_lines; }
    [[nodiscard]] size_t ArcCommands() const noexcept { return m_arcs; }   // Phase 33：DrawArc（曲线原语）
    [[nodiscard]] size_t RoundedRectCommands() const noexcept { return m_roundedRects; }
    [[nodiscard]] size_t ImageCommands() const noexcept { return m_images; }
    [[nodiscard]] size_t PushClipCommands() const noexcept { return m_pushClips; }
    [[nodiscard]] size_t PopClipCommands() const noexcept { return m_popClips; }
    [[nodiscard]] size_t FocusRectCommands() const noexcept { return m_focusRects; }

private:
    void OnCommand() noexcept { ++m_frameCommandsCount; }

    std::unique_ptr<RenderingBackend> m_inner;
    size_t m_frameCommandsCount = 0;      // 当前帧累计
    std::vector<size_t> m_frameCommands;   // 每帧命令数
    std::chrono::steady_clock::time_point m_frameStart;
    std::vector<uint64_t> m_frameExecuteUs;   // 每帧执行墙钟 us

    // 命令分类（生命周期累计）
    size_t m_rects = 0;
    size_t m_texts = 0;
    size_t m_lines = 0;
    size_t m_arcs = 0;                     // Phase 33：DrawArc 分类计数
    size_t m_roundedRects = 0;
    size_t m_images = 0;
    size_t m_pushClips = 0;
    size_t m_popClips = 0;
    size_t m_focusRects = 0;
};

} // namespace ECDI