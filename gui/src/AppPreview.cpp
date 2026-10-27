// ============================================================
// 文件路径可点 + 只读预览（P4 Phase I，2026-10-26）
// ------------------------------------------------------------
// 规格住在 docs/P4-gui-usability-plan.md §11。两处与 §11 原文的偏差（均经用户确认）：
//   ① 入口：§11 原文「把标题按钮的点击绑到预览」不可行——标题按钮点击已被
//      折叠占用（BindCollapsible → ToggleRow，AppTranscript.cpp）。改为标题旁
//      小按钮「⤢」，仅当标题文本匹配路径形态时创建（AppTranscript.cpp 构建）。
//   ② Esc 关闭（§11 未提，用户拍板加）——HandleGlobalKeyDown 的 Esc 链最前。
// 「在资源管理器中显示」是本仓**首个 shell 调用**（2026-10-25 核实此前无
//   ShellExecuteW 先例）；探针只断言路径/参数构造，不真开 shell（有副作用）。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Platform/PlatformWindow.h"   // SetClipboardText（复制路径按钮）
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollView.h"

using harness::internal::ApplyCompactScrollBars;
using harness::internal::kProbeBg;
using harness::internal::kProbeInk;
using harness::internal::PathToUtf8;
using harness::internal::Pick;
using harness::internal::WideFromUtf8Path;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // ShellExecuteW
#include <shellapi.h>  // ShellExecuteW（windows.h 不自带，LEAN_AND_MEAN 更不会）

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <regex>

namespace {

// §11 起步集（正则放常量）：`[\w./-]+\.(go|cpp|py|md|json|jsonl|html|h)`。
// 实现要点（探针 regex-cases 相逐条钉死）：
//   · 交替序 **长扩展在前**（jsonl 先于 json）——否则 `output.jsonl` 贪婪回溯
//     会截成 `output.json`（首版实测）；
//   · 扩展名后跟 `\b` 词边界——`a.json` 后再跟字母的串不允许借尾巴命中；
//   · 惰性主体 `+?` 让匹配从最早可成功点开始，`https://x/a.md` 一类 URL 会带出
//     `//` 前缀 ⇒ 结果统一剥前导 `/`（见函数尾）。
// 已知边界（v1 接受）：`foo.src/main.go` 一类复合串会整段匹配——后续 IO 校验
//   （workspace 内 + 存在性）会把误报挡成 notice，不产生越权读。
const std::regex& PathPattern() {
    static const std::regex pattern(
        "[\\w./\\-]+?\\.(?:jsonl|html|json|cpp|go|py|md|h)\\b");
    return pattern;
}

// IO 纪律（§11）：字节上限 ~512KB；行数上限 2000（一次性 wrap 布局是已知
// 近似 O(n^1.5)，万行 Label 列表会让预览打开变成一次卡顿——双闸各自出 notice）。
constexpr size_t kPreviewMaxBytes = 512 * 1024;
constexpr size_t kPreviewMaxLines = 2000;

}  // namespace

// ── 判定：文本里抽第一个路径形态匹配 ─────────────────────────────
// 探针与卡片头构建共用这一个函数——「判据只有一份实现」（同 ToggleRow 纪律）。
std::string HarnessApp::PreviewPathFromText(const std::string& text) {
    std::smatch m;
    if (!std::regex_search(text, m, PathPattern())) return {};
    std::string hit = m.str(0);
    // URL 形态（`https://x/a.md`）会带 `//` 前缀：剥掉（真实相对路径不以 / 开头
    // ——workspace 纪律下根相对路径本来就不该是绝对根）。
    size_t pos = 0;
    while (pos < hit.size() && (hit[pos] == '/' || hit[pos] == '\\')) ++pos;
    return hit.substr(pos);
}

// ── 打开预览：IO 纪律全过 ⇒ 读行 + 开面板；任何一步不过 ⇒ m_previewError
//    落因，面板**仍然打开**（体内显示 notice——不静默，探针好断言）。
bool HarnessApp::OpenPreview(const std::string& rawPath) {
    namespace fs = std::filesystem;
    m_previewError.clear();
    m_previewLines.clear();
    m_previewTruncated = false;
    m_previewPath = rawPath;

    // ① 只允许当前会话 workspace（core 确认过的规范化路径，协议 §3.9）之下。
    if (!m_sess || m_sess->workspacePath.empty()) {
        m_previewError = "无工作区：预览只读 core 确认过的路径之下";
        SetPreviewVisible(true);
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(WideFromUtf8Path(m_sess->workspacePath), ec);
    if (ec) {
        m_previewError = "工作区路径无法解析";
        SetPreviewVisible(true);
        return false;
    }
    fs::path target = WideFromUtf8Path(rawPath);
    if (!target.is_absolute()) target = root / target;   // 相对路径按 workspace 根拼接
    const fs::path canon = fs::weakly_canonical(target, ec);
    if (ec) {
        m_previewError = "路径无法解析：" + rawPath;
        SetPreviewVisible(true);
        return false;
    }
    // ② weakly_canonical 必须仍在 workspace 之内（防 `..` 逃逸）。
    std::wstring rootStr = root.wstring();
    while (!rootStr.empty() && (rootStr.back() == L'\\' || rootStr.back() == L'/')) {
        rootStr.pop_back();
    }
    const std::wstring& canonStr = canon.wstring();
    const bool inside =
        canonStr == rootStr ||
        (canonStr.rfind(rootStr, 0) == 0 &&
         (canonStr[rootStr.size()] == L'\\' || canonStr[rootStr.size()] == L'/'));
    if (!inside) {
        m_previewError = "路径越出工作区，拒绝读取";
        SetPreviewVisible(true);
        return false;
    }
    if (!fs::exists(canon, ec) || !fs::is_regular_file(canon, ec)) {
        m_previewError = "文件不存在或不是常规文件";
        SetPreviewVisible(true);
        return false;
    }
    const uintmax_t size = fs::file_size(canon, ec);
    if (ec) {
        m_previewError = "文件大小不可读";
        SetPreviewVisible(true);
        return false;
    }

    // ③ 读入（一次；重建按模型投影，不读盘）。二进制读后按 '\n' 拆行、去 '\r'。
    m_previewTruncated = size > kPreviewMaxBytes;
    std::ifstream f(canon, std::ios::binary);
    if (!f) {
        m_previewError = "文件打开失败";
        SetPreviewVisible(true);
        return false;
    }
    std::string blob(static_cast<size_t>(std::min<uintmax_t>(size, kPreviewMaxBytes)), '\0');
    f.read(blob.data(), static_cast<std::streamsize>(blob.size()));
    blob.resize(static_cast<size_t>(f.gcount()));

    size_t start = 0;
    while (start < blob.size()) {
        if (m_previewLines.size() >= kPreviewMaxLines) {
            m_previewTruncated = true;
            break;
        }
        size_t end = blob.find('\n', start);
        if (end == std::string::npos) end = blob.size();
        std::string line = blob.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        m_previewLines.push_back(std::move(line));
        start = end + 1;
    }
    m_previewPath = PathToUtf8(canon);   // 显示/复制用规范化后的实际路径
    SetPreviewVisible(true);
    RebuildPreviewContent();
    return true;
}

// ── sheet 构建：返回所有权，由 BuildTranscriptViewport 挂 scroll（同设置）。
//    结构 = 头（[只读] 徽标 + 文件名 + ×）/ 体（ScrollView + Consolas 行）/
//    底（在资源管理器中显示 + 复制路径）。体内行由 RebuildPreviewContent 填。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildPreviewSheet() {
    auto panel = std::make_unique<ECDI::Panel>();
    m_hud.previewPanel = panel.get();
    panel->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 14));
    // 尺寸基准值；实际几何由 LayoutPreviewOverlay 按视口钳制（同设置 sheet）。
    panel->SetSize(420, 480);
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
        .cornerRadius = kRadiusBtn,
        .borderWidth = 1.0f,
        .borderColor = Pick(m_options.probeContrast, kProbeBg, kCardBorder)});

    // ── 头：[只读] · 文件名（弹性） · × ──
    auto head = std::make_unique<ECDI::Panel>();
    head->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, true, 0));
    auto badge = std::make_unique<ECDI::Label>("[只读]");
    badge->SetTextColor(Pick(m_options.probeContrast, kProbeInk, kOkDark));
    badge->SetFont(harness::SansFont(12.0f));
    head->AddChild(std::move(badge));
    auto title = std::make_unique<ECDI::Label>("（未打开）");
    m_hud.previewTitle = title.get();
    title->SetTextColor(Pick(m_options.probeContrast, kProbeInk, kDarkInk));
    title->SetFont(harness::SansFont(12.0f));
    title->SetStretch(1);   // 弹性吃掉剩余宽度，× 靠右
    head->AddChild(std::move(title));
    auto close = std::make_unique<ECDI::Button>("×");
    close->SetTextColor(Pick(m_options.probeContrast, kProbeInk, kDarkMuted));
    close->SetFont(harness::SansFont(13.0f));
    close->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kProbeBg, kCardBg),
        .border = Pick(m_options.probeContrast, kProbeBg, kCardBorder),
        .borderWidth = 0.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderPressed),
        .hoverBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderHover)});
    close->SetOnClick([this] { SetPreviewVisible(false); });
    head->AddChild(std::move(close));
    panel->AddChild(std::move(head));

    // ── 体：ScrollView（stretch 吃剩余高）+ 内容视（VerticalLayout 行容器）──
    auto sv = std::make_unique<ECDI::ScrollView>();
    m_hud.previewSv = sv.get();
    sv->SetSize(390, 360);   // 基准尺寸（F2/D9 契约，同设置面板写法）；stretch=1 吃剩余高
    sv->SetStretch(1);
    sv->SetScrollStep(18);
    ApplyCompactScrollBars(*sv);   // ★ 条宽全仓统一（kScrollBarThickness）
    ECDI::Widget* content = &sv->GetContentView();
    m_hud.previewBody = content;
    content->SetLayout(std::make_unique<ECDI::VerticalLayout>(1, true, 2));
    panel->AddChild(std::move(sv));

    // ── 底：在资源管理器中显示 · 复制路径 ──
    auto foot = std::make_unique<ECDI::Panel>();
    foot->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, false, 0));
    auto reveal = std::make_unique<ECDI::Button>("在资源管理器中显示");
    reveal->SetFont(harness::SansFont(12.0f));
    reveal->SetTextColor(Pick(m_options.probeContrast, kProbeInk, kDarkInk));
    reveal->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kProbeBg, kToolHeaderBg),
        .border = Pick(m_options.probeContrast, kProbeBg, kCardBorder),
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderPressed),
        .hoverBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderHover)});
    // ★ 本仓首个 shell 调用：explorer /select 定位文件。失败（返回 ≤32）不弹
    //   窗——把原因写进预览体 notice（与 IO 错误同一条显示路径）。
    reveal->SetOnClick([this] {
        const std::wstring wpath = WideFromUtf8Path(m_previewPath);
        const std::wstring params = L"/select,\"" + wpath + L"\"";
        const INT_PTR rc = reinterpret_cast<INT_PTR>(ShellExecuteW(
            nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL));
        if (rc <= 32) {
            m_previewError = "资源管理器打开失败（代码 " + std::to_string(rc) + "）";
            RebuildPreviewContent();
        }
    });
    foot->AddChild(std::move(reveal));
    auto copy = std::make_unique<ECDI::Button>("复制路径");
    copy->SetFont(harness::SansFont(12.0f));
    copy->SetTextColor(Pick(m_options.probeContrast, kProbeInk, kDarkInk));
    copy->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kProbeBg, kToolHeaderBg),
        .border = Pick(m_options.probeContrast, kProbeBg, kCardBorder),
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderPressed),
        .hoverBackground = Pick(m_options.probeContrast, kProbeBg, kToolHeaderHover)});
    // 复制走 ECDI 口（PlatformWindow.h:76 SetClipboardText），不直调 Win32 剪贴板。
    copy->SetOnClick([this] {
        m_window->GetPlatformWindow().SetClipboardText(m_previewPath);
    });
    foot->AddChild(std::move(copy));
    panel->AddChild(std::move(foot));

    return panel;
}

// ── 体内行重建：按模型态（path/error/lines/truncated）重投影。唯一数据源是
//    模型字段——重建不读盘（OpenPreview 时已读入）。
void HarnessApp::RebuildPreviewContent() {
    if (!m_hud.previewBody) return;
    ECDI::Widget& body = *m_hud.previewBody;
    while (body.GetChildCount() > 0) {
        body.RemoveChild(body.GetChildAt(0));   // 清行先例：AppHud.cpp RebuildProjectList
    }
    if (m_hud.previewTitle) {
        m_hud.previewTitle->SetText(m_previewError.empty()
                                        ? m_previewPath
                                        : m_previewPath + "（未读取）");
    }

    const auto mkRow = [&](const std::string& text, const ECDI::Color& color) {
        auto row = std::make_unique<ECDI::Label>(text);
        row->SetTextColor(color);
        row->SetFont(harness::MonoFont(12.0f));   // §11：等宽（kMonoFamily=Consolas）
        body.AddChild(std::move(row));
    };

    if (!m_previewError.empty()) {
        mkRow("[错误] " + m_previewError, Pick(m_options.probeContrast, kProbeInk, kErrorDark));
    }
    if (m_previewTruncated) {
        mkRow("[已截断] 文件超过预览上限（512KB / 2000 行），只显示前一部分",
              Pick(m_options.probeContrast, kProbeInk, kWarn));
    }
    for (const std::string& line : m_previewLines) {
        mkRow(line, Pick(m_options.probeContrast, kProbeInk, kDarkInk));
    }
    if (m_hud.previewSv) {
        m_hud.previewSv->UpdateContentExtent();   // 包围盒 → 滚动范围（extent=0 教训）
    }
}

// ── 投影：BuildUi 首帧与 RebuildUi 共用这一条路径（起始幻影教训）。
void HarnessApp::ApplyPreviewVisibility() {
    if (!m_hud.previewPanel) return;
    m_hud.previewPanel->SetVisible(m_previewOpen);
    if (m_hud.previewMask) m_hud.previewMask->SetVisible(m_previewOpen);
    // 三件套互斥（menu/settings/preview）：开预览合其余浮层——同一时刻只该有
    // 一层模态；反向（开 settings/model 时合预览）在各自的 Set 里做。
    if (m_previewOpen && m_moreMenuOpen) SetMoreMenuOpen(false);
    if (m_previewOpen && m_settingsVisible) SetSettingsVisible(false);
    LayoutPreviewOverlay();
}

// ── 贴右几何：宽 clamp(rw-80, 280..420)（§11 min(420, rootW-80) 加下限），
//    高 clamp(rh-80, 240..560)，右缘留 16px。浮层无父布局 ⇒ SetSize 后自己
//    Arrange，再 UpdateContentExtent（extent=0 教训，同 LayoutSettingsOverlay）。
void HarnessApp::LayoutPreviewOverlay() {
    if (!m_hud.previewPanel) return;
    ECDI::Widget& root = m_window->GetRootWidget();
    const int rw = root.GetWidth();
    const int rh = root.GetHeight();
    if (m_hud.previewMask) {
        m_hud.previewMask->SetSize(rw, rh);
        m_hud.previewMask->SetPosition(0, 0);
    }
    const int w = std::clamp(rw - 80, 280, 420);
    const int h = std::clamp(rh - 80, 240, 560);
    m_hud.previewPanel->SetSize(w, h);
    m_hud.previewPanel->SetPosition(rw - w - 16, (rh - h) / 2);
    m_hud.previewPanel->Arrange();
    if (m_hud.previewSv) {
        m_hud.previewSv->UpdateContentExtent();
    }
}

void HarnessApp::SetPreviewVisible(bool visible) {
    if (m_previewOpen == visible &&
        (!m_hud.previewPanel || m_hud.previewPanel->IsVisible() == visible)) {
        return;   // 幂等（同 SetSettingsVisible）
    }
    m_previewOpen = visible;   // ① 模型侧先落
    ApplyPreviewVisibility();  // ② 再投影
    if (visible) RebuildPreviewContent();   // 重建后句柄换了 ⇒ 投影后重填体内行
    Relayout();
    RequestRepaint();
}
