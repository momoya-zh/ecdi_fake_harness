#include "App.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#ifdef DrawText
#undef DrawText
#endif


#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Render/BackendFactory.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Window/Window.h"

#include "Theme.h"
#include "UiKit.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>

namespace {

constexpr int kListRowHeight = 28;   // listlayout 模式的固定行高（DIP）
constexpr int kRowGap = 4;
constexpr int kRowPadX = 10;
constexpr int kHeaderH = 26;         // tool 卡片标题高

// 视觉令牌唯一来源 = Theme.h（2026-10-06 v6：对齐用户高保真模型）
using harness::kDarkInk;
using harness::kDarkMuted;
using harness::kDarkAccent;
using harness::kAccentInk;
using harness::kOkDark;
using harness::kRunDark;
using harness::kErrorDark;
using harness::kWarn;
using harness::kWarnInk;
using harness::kSidebarBg;
using harness::kContentBg;
using harness::kInspBg;
using harness::kPanel2;
using harness::kLine;
using harness::kUserBg;
using harness::kUserBorder;
using harness::kUserInk;
using harness::kToolBg;
using harness::kToolBorder;
using harness::kToolHeaderBg;
using harness::kToolHeaderHover;
using harness::kToolHeaderPressed;
using harness::kToolHeaderBorder;
using harness::kAskBg;
using harness::kAskBorder;
using harness::kAskQInk;
using harness::kDenyInk;
using harness::kDenyHover;
using harness::kCardBg;
using harness::kCardBorder;
using harness::kBtnPrimary;
using harness::kBtnPrimaryHover;
using harness::kBtnPrimaryPressed;
using harness::kBtnPrimaryInk;
using harness::kBtnNeutralBg;
using harness::kBtnNeutralBorder;
using harness::kBtnDenyBg;
using harness::kTrackBg;
using harness::kDarkPillBg;
using harness::kRadiusCard;
using harness::kRadiusTool;
using harness::kRadiusCard2;
using harness::kRadiusBtn;
using harness::kRadiusPill;
using harness::kMonoFamily;

// ══════════════════════════════════════════════════════════════════
// ★ 探针专用高对比配色（2026-10-08）——--probe-contrast
// ──────────────────────────────────────────────────────────────────
// 动机（一次真实教训）：默认暗色主题下做像素探针时，转录画布的亮像素
//   要与**卡片圆角、1px 描边、滚动条、窗口边框**抢阈值 —— 于是反复出现
//   「文本行段 = 0」「x 跨度到 899」这类**仪器噪声**，并据此误判过产品行为。
//   改成「白底 + 黑字 + 零描边」后，测量退化为**平凡问题**：
//   统计暗像素的行段即得文本行数，无需任何阈值调参。
// ★ 默认关闭（probeContrast=false）⇒ 所有既有路径**逐位等价**，不参与交付外观。
namespace {
inline const ECDI::Color kProbeCanvas = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeBg     = ECDI::Color::FromRGBA8(255, 255, 255, 255);
inline const ECDI::Color kProbeInk    = ECDI::Color::FromRGBA8(0, 0, 0, 255);

// 探针态取高对比色，否则取常规令牌（调用点写法统一）
inline ECDI::Color Pick(bool probe, const ECDI::Color& normal, const ECDI::Color& hi) {
    return probe ? hi : normal;
}
}   // namespace

int ContentWidth(const ECDI::ScrollView* scroll) {
    const int w = scroll ? scroll->GetWidth() : 0;
    return w > 80 ? w - 40 : 600;   // 扣除滚动条与边距；未布局前给初值
}

// ★★ 未知 `kind` 行的正文拼法（2026-10-13）——**单一真相源**。
//   为何要抽成函数：该文本在**两处**生成（`CreateBlockRow` 建行、
//   `RouteLastMessage` 的 delta 分支更新），首版在两处各写一份 ⇒ delta 一到
//   就把 `[未知 kind: …]` 前缀擦掉（实测截图可见）。同源即不可能再漂移。
std::string UnknownKindText(const Block& block) {
    std::string shown = block.kind.empty()
                            ? "[未知 kind: (字段缺失)]"
                            : "[未知 kind: " + block.kind + "]";
    shown += "\n";
    shown += block.text;
    if (!block.summary.empty()) shown += "\n(" + block.summary + ")";
    return shown;
}

// 侧栏/状态栏的一行小标签；返回裸指针（树拥有），供 HUD 动态 SetText
ECDI::Label* AddLine(ECDI::Panel& parent, const std::string& text,
                     const ECDI::Color& color, bool heading = false, int width = 170,
                     std::optional<float> fontSize = std::nullopt) {
    auto label = std::make_unique<ECDI::Label>(text);
    label->SetTextColor(color);
    if (fontSize) {
        ECDI::Font font;
        font.size = *fontSize;
        label->SetFont(font);
    }
    label->SetSize(width, heading ? 15 : 17);
    ECDI::Label* raw = label.get();
    parent.AddChild(std::move(label));
    return raw;
}

}   // namespace

// ── HarnessApplication ─────────────────────────────────────────

void HarnessApplication::OnTimer(const ECDI::TimerEvent& event) {
    if (m_owner) {
        if (event.GetTimerId() == HarnessApp::kReplayTimer) {
            m_owner->OnReplayTick();
            return;
        }
        if (event.GetTimerId() == HarnessApp::kCloseTimer) {
            m_owner->OnAutoClose();
            return;
        }
        if (event.GetTimerId() == HarnessApp::kSweepTimer) {
            m_owner->OnSweepTick();
            return;
        }
    }
    ECDI::Application::OnTimer(event);   // 光标闪烁 / 动画 tick
}

// ── 构造 / 运行 ────────────────────────────────────────────────

HarnessApp::HarnessApp(CliOptions options)
    : m_options(std::move(options)) {
    // ── 源选择（P1.5）：--core <exe> ⇒ 子进程真路径；否则夹具回放 ──
    if (!m_options.corePath.empty()) {
        auto child = std::make_unique<ChildSource>();
        std::string error;
        if (!child->Start(m_options.corePath, m_options.coreArgs, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ChildSource: " + error));
            return;   // m_source 为空 ⇒ Run 里报错退出
        }
        m_child = child.get();
        m_source = std::move(child);
        m_writer = std::make_unique<ProtocolWriter>(*m_source);
        m_options.fixtureName = m_options.corePath.filename().generic_string() + " (live)";
        return;
    }

    auto replay = std::make_unique<ReplaySource>();
    std::string error;
    if (!replay->Load(m_options.replayPath, error)) {
        ECDI::Logger::Log(ECDI::LogLevel::Error,
                          ECDI::UTF8ToWide("ReplaySource: " + error));
    }
    m_pendingTruncated = replay->TruncatedError();
    m_replay = replay.get();
    m_source = std::move(replay);
}

HarnessApp::~HarnessApp() = default;

int HarnessApp::Run() {
    if (!m_source) {
        MessageBoxW(nullptr,
                    L"消息源不可用。\n"
                    L"--core 指定的假 core 启动失败，或 --replay 夹具加载失败。\n"
                    L"详见日志。",
                    L"harness_gui", MB_ICONERROR | MB_OK);
        return 4;
    }
    HarnessApplication application;
    m_application = &application;
    application.SetOwner(this);

    // ── 后端选择（Phase 26 批二：--backend gl；默认 GDI 逐位零回归）──
    // ★ D7：显式请求 GL 但初始化失败 ⇒ 明确报错退出，不静默回退 GDI——
    //   否则「性能对照」可能实际跑的是 GDI，读数却记在 GL 头上。
    ECDI::RenderServices services = m_options.useGl
        ? ECDI::CreateGLRenderServices()
        : ECDI::CreateDefaultRenderServices();

    // ── Phase 27 接入：--cull-probe 包一层 PaintProbe（透传 + 逐帧命令计数）──
    // 2026-10-05 升级为探针套件：再包一层 MeasureProbe（文本测量成本）。
    // 包装后 GL 就绪检查走探针 IsReady()（转发内部后端），不改变行为。
    if (m_options.cullProbe) {
        auto probe = std::make_unique<ECDI::PaintProbe>(std::move(services.renderer));
        m_probe = probe.get();
        services.renderer = std::move(probe);

        auto measureProbe = std::make_unique<ECDI::MeasureProbe>(std::move(services.measurer));
        m_measureProbe = measureProbe.get();
        services.measurer = std::move(measureProbe);
    }
    ECDI::RenderingBackend* const requestedBackend = services.renderer.get();

    ECDI::Window& win = application.Create("ECDI harness - " + m_options.fixtureName,
                                           1120, 760, std::move(services));

    if (m_options.useGl) {
        if (requestedBackend == nullptr || !requestedBackend->IsReady()) {
            MessageBoxW(nullptr,
                        L"OpenGL (WGL) backend initialization failed.\n"
                        L"--backend gl was requested; not silently falling back to GDI.\n"
                        L"Check that OpenGL is available, or run without --backend gl.",
                        L"harness_gui", MB_ICONERROR | MB_OK);
            m_application = nullptr;
            return 3;
        }
        m_backendName = "gl";
    }
    m_window = &win;
    // 窗口底色 = 画布色（默认白——resize/缝隙漏白全靠它兜底）
    // ★ --probe-contrast：白底（探针测量用；默认路径逐位不变）
    win.SetBackgroundColor(m_options.probeContrast ? kProbeCanvas : harness::kContentBg);
    BuildUi(win);
    uikit::WarnZeroSized(win.GetRootWidget());   // F2/D9 巡检：漏 SetSize 的 0×0 静默不可见 → 告警

    // P1.5 live：收到 core 的 hello 之后回 hello（协议 §4：seq 0）
    if (m_writer) {
        m_writer->Hello("0.0.1");
    }
    win.Show();
    if (m_promptBar && m_promptBar->input()) {
        win.SetFocusedWidget(m_promptBar->input());   // 打开即可打字（验收 A2）
    }

    win.GetPlatformWindow().StartTimer(kReplayTimer, 33);
    if (m_options.timed) {
        m_timedAnchor = std::chrono::steady_clock::now();   // timed 节奏锚点（协议 §8.2：ts 是唯一真相源）
    }
    if (m_options.autoCloseMs > 0) {
        win.GetPlatformWindow().StartTimer(kCloseTimer,
                                           static_cast<unsigned int>(m_options.autoCloseMs));
    }
    // P1.5：--auto-prompt（跑批/冒烟——启动后立即走一次真实发送路径）
    if (!m_options.autoPrompt.empty() && m_writer) {
        SendPrompt(m_options.autoPrompt);
    }

    const int code = application.Run();

    // P1.5 live：关窗前发 bye（协议 §4：guest 即将关闭，请 core 退出）→ 关 stdin → 收尸
    if (m_writer) {
        m_writer->Bye();
    }
    if (m_child) {
        m_child->Stop();
    }

    WriteSummary();
    m_application = nullptr;
    return code;
}

void HarnessApp::BuildUi(ECDI::Window& window) {
    ECDI::Widget& root = window.GetRootWidget();
    root.SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true));
    BuildTopBar(root);
    BuildMainArea(root);
    BuildBottom(root);
    root.Arrange();
}

// 顶栏 HUD（hi-fi .titlebar）：≡ | harness | 会话 chip | 后端 chip | … | 行模式 chip | 圆点+状态
void HarnessApp::BuildTopBar(ECDI::Widget& root) {
    auto bar = std::make_unique<ECDI::Panel>();
    bar->SetSize(1000, 36);
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kDarkTopBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    bar->SetLayout(std::make_unique<ECDI::HorizontalLayout>(10, true, 8));

    auto menu = std::make_unique<ECDI::Label>("≡");
    menu->SetTextColor(kDarkMuted);
    menu->SetSize(22, 20);
    bar->AddChild(std::move(menu));

    auto title = std::make_unique<ECDI::Label>("harness");
    m_hud.title = title.get();
    title->SetTextColor(kDarkInk);
    {
        ECDI::Font font;
        font.size = 13.0f;
        title->SetFont(font);
    }
    title->SetSize(64, 20);
    bar->AddChild(std::move(title));

    // chip = 药丸 Panel + 内嵌 Label；accent 变体描边用 --accent、字用 #cfe1ff
    auto makeChip = [](std::unique_ptr<ECDI::Label> label, int width, bool accent) {
        auto pill = std::make_unique<ECDI::Panel>();
        pill->SetSize(width, 20);
        pill->SetStyle(ECDI::PanelStyleOverride{
            .background = kDarkPillBg,
            .cornerRadius = kRadiusPill,
            .borderWidth = 1.0f,
            .borderColor = accent ? harness::kDarkAccent : kLine});
        pill->AddChild(std::move(label));
        return pill;
    };
    auto chipLabel = [](const std::string& text, const ECDI::Color& color, int width) {
        auto label = std::make_unique<ECDI::Label>(text);
        label->SetTextColor(color);
        ECDI::Font font;
        font.size = 11.0f;
        font.family = kMonoFamily;
        label->SetFont(font);
        label->SetSize(width, 20);
        return label;
    };

    auto sessChipLabel = chipLabel("s-____", kAccentInk, 84);
    m_hud.sessChip = sessChipLabel.get();
    bar->AddChild(makeChip(std::move(sessChipLabel), 84, true));
    bar->AddChild(makeChip(chipLabel("replay", kDarkMuted, 64), 64, false));

    auto grow = std::make_unique<ECDI::Panel>();
    grow->SetStretch(1);
    grow->SetSize(10, 10);
    bar->AddChild(std::move(grow));

    auto rowLabel = std::make_unique<ECDI::Label>("autosize");
    m_hud.rowChip = rowLabel.get();
    rowLabel->SetTextColor(kDarkMuted);
    {
        ECDI::Font font;
        font.size = 11.0f;
        rowLabel->SetFont(font);
    }
    rowLabel->SetSize(80, 20);   // stretch=0 的子布局不分配主轴尺寸——必须显式给
    bar->AddChild(makeChip(std::move(rowLabel), 80, false));

    bar->AddChild(uikit::DotCell(8, kOkDark, 20, &m_hud.statusDot));

    auto state = std::make_unique<ECDI::Label>("idle");
    m_hud.stateChip = state.get();
    state->SetTextColor(kDarkMuted);
    {
        ECDI::Font font;
        font.size = 12.0f;
        state->SetFont(font);
    }
    state->SetSize(60, 20);
    bar->AddChild(std::move(state));

    root.AddChild(std::move(bar));
}

// 主区（hi-fi .main）：左栏（新建会话/会话/回放/工作区）+ 中栏（转录 + Composer）+ 右栏检查器
void HarnessApp::BuildMainArea(ECDI::Widget& root) {
    auto main = std::make_unique<ECDI::Panel>();
    main->SetStretch(1);
    main->SetLayout(std::make_unique<ECDI::HorizontalLayout>(0, true, 0));

    // ── 左栏（hi-fi .side）──
    auto side = std::make_unique<ECDI::Panel>();
    side->SetSize(200, 100);
    side->SetStyle(ECDI::PanelStyleOverride{
        .background = kSidebarBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    side->SetLayout(std::make_unique<ECDI::VerticalLayout>(12, false, 10));

    auto newSess = std::make_unique<ECDI::Button>("＋ 新建会话");
    newSess->SetTextColor(kAccentInk);
    newSess->SetStyle(ECDI::ButtonStyleOverride{
        .background = kUserBg,            // --accent-soft 与 user 气泡同值 #263449
        .border = harness::kDarkAccent,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn});
    newSess->SetSize(180, 30);
    newSess->SetOnClick([this] {
        AddNoticeRow("[新建会话 仅记录]", kDarkMuted);
        Relayout();
        RequestRepaint();
    });
    side->AddChild(std::move(newSess));

    AddLine(*side, "会 话", kDarkMuted, true, 170, 11.0f);
    {
        auto row = uikit::Row(180, 28, 8, 8);
        row->SetStyle(ECDI::PanelStyleOverride{
            .background = kPanel2, .cornerRadius = 6.0f});
        row->AddChild(uikit::Dot(7, harness::kDarkAccent));
        auto name = uikit::Label("—", 60, 16, kDarkInk);
        m_hud.sessSession = name.get();
        row->AddChild(std::move(name));
        auto meta = uikit::Label("—", 72, 16, kDarkMuted, 10.5f);
        m_hud.sessCore = meta.get();
        row->AddChild(std::move(meta));
        side->AddChild(std::move(row));
    }

    AddLine(*side, "回 放", kDarkMuted, true, 170, 11.0f);
    {
        auto prog = std::make_unique<ECDI::Label>("0 / 0");
        m_hud.sessProgress = prog.get();
        prog->SetTextColor(kDarkInk);
        { ECDI::Font f; f.size = 12.0f; prog->SetFont(f); }
        prog->SetSize(170, 16);
        side->AddChild(std::move(prog));
        auto bar = std::make_unique<ECDI::ProgressBar>();
        m_replayBar = bar.get();
        bar->SetSize(180, 5);
        bar->SetStyle(ECDI::ProgressBarStyleOverride{
            .trackColor = kTrackBg, .fillColor = harness::kDarkAccent,
            .cornerRadius = 2.0f});
        side->AddChild(std::move(bar));
    }

    AddLine(*side, "工 作 区", kDarkMuted, true, 170, 11.0f);
    {
        // 夹具路径链 → 伪树（dir ▾ / 文件 ·；真实数据 = replayPath 的父目录链）
        std::filesystem::path p = m_options.replayPath;
        const std::string file = p.filename().generic_string();
        p = p.parent_path();
        std::vector<std::string> dirs;
        for (int i = 0; i < 2 && !p.empty(); ++i) {   // 最多两级父目录
            const std::string name = p.filename().generic_string();
            if (!name.empty()) dirs.push_back(name);
            p = p.parent_path();
        }
        int indent = 0;
        for (auto it = dirs.rbegin(); it != dirs.rend(); ++it) {
            auto lbl = std::make_unique<ECDI::Label>("▾ " + *it + "/");
            lbl->SetTextColor(kDarkInk);
            ECDI::Font f; f.size = 12.0f; f.family = kMonoFamily;
            lbl->SetFont(f);
            lbl->SetSize(180 - indent * 12, 16);
            side->AddChild(std::move(lbl));
            ++indent;
        }
        auto fileLbl = std::make_unique<ECDI::Label>("· " + file);
        fileLbl->SetTextColor(kDarkMuted);
        ECDI::Font ff; ff.size = 12.0f; ff.family = kMonoFamily;
        fileLbl->SetFont(ff);
        fileLbl->SetSize(180 - indent * 12, 16);
        side->AddChild(std::move(fileLbl));
    }

    main->AddChild(std::move(side));

    // ── 中栏（转录 + Composer）──
    auto counting = std::make_unique<CountingPanel>();
    m_counting = counting.get();
    m_counting->SetStretch(1);
    m_counting->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true, 0));
    m_counting->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? kProbeBg : kContentBg});

    auto scroll = std::make_unique<ECDI::ScrollView>();
    m_scroll = scroll.get();
    m_scroll->SetStretch(1);
    m_scroll->SetScrollStep(40);
    m_content = &m_scroll->GetContentView();
    if (m_options.rowModeList) {
        // listlayout 模式：固定行高、不测量（规格 §5.3 的 B 组）
        m_content->SetLayout(std::make_unique<ECDI::ListLayout>(kListRowHeight, 2));
    }
    m_counting->AddChild(std::move(scroll));

    // Composer 收进中栏（hi-fi .composer 在 .mid 内）
    auto composer = std::make_unique<PromptBar>();
    m_promptBar = composer.get();
    composer->SetSize(600, 56);
    composer->SetOnSend([this](const std::string& text) {
        SendPrompt(text);
        if (m_promptBar && m_promptBar->input()) {
            m_promptBar->input()->SetText("");   // 已发出/记录，输入框清空（聊天惯例）
        }
    });
    composer->SetOnStop([this](const std::string&) {
        if (m_writer) {
            const bool ok = m_writer->Cancel();
            AddNoticeRow(ok ? "[cancel → core]" : "[cancel 发送失败]",
                         ok ? kDarkMuted : kErrorDark);
        } else {
            AddNoticeRow("[cancel 仅记录]", kDarkMuted);
        }
        Relayout();
        RequestRepaint();
    });
    counting->AddChild(std::move(composer));
    main->AddChild(std::move(counting));

    // ── 右栏检查器（hi-fi .insp：卡片化）──
    auto insp = std::make_unique<ECDI::Panel>();
    insp->SetSize(230, 100);
    insp->SetStyle(ECDI::PanelStyleOverride{
        .background = kInspBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    insp->SetLayout(std::make_unique<ECDI::VerticalLayout>(14, false, 12));

    auto makeCard = [](int h) {
        auto card = std::make_unique<ECDI::Panel>();
        card->SetSize(200, h);
        card->SetStyle(ECDI::PanelStyleOverride{
            .background = kPanel2, .cornerRadius = kRadiusCard2,
            .borderWidth = 1.0f, .borderColor = kLine});
        return card;
    };
    auto kvRow = [](ECDI::Panel& parent, const std::string& k,
                    ECDI::Label** vOut, int kw = 70) {
        auto row = uikit::Row(178, 16, 10);
        row->AddChild(uikit::Label(k, kw, 16, kDarkMuted));
        auto vl = uikit::Label("—", 90, 16, kDarkInk, 11.5f, /*mono=*/true);
        *vOut = vl.get();
        row->AddChild(std::move(vl));
        parent.AddChild(std::move(row));
    };

    AddLine(*insp, "渲染探针", kDarkMuted, true, 200, 11.0f);
    {
        auto card = makeCard(92);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        kvRow(*card, "重绘", &m_hud.evalPaints);
        kvRow(*card, "命令/帧", &m_hud.probeCmds);
        kvRow(*card, "执行", &m_hud.probeExec);
        insp->AddChild(std::move(card));
    }

    AddLine(*insp, "用量", kDarkMuted, true, 200, 11.0f);
    {
        auto card = makeCard(70);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 10));
        auto urow = [&card](const std::string& k, ECDI::Label** vOut,
                            ECDI::ProgressBar** barOut) {
            auto row = uikit::Row(178, 16, 8);
            row->AddChild(uikit::Label(k, 40, 16, kDarkMuted));
            auto bar = std::make_unique<ECDI::ProgressBar>();
            *barOut = bar.get();
            bar->SetSize(70, 5);
            bar->SetStyle(ECDI::ProgressBarStyleOverride{
                .trackColor = kTrackBg, .fillColor = harness::kDarkAccent,
                .cornerRadius = 2.0f});
            row->AddChild(std::move(bar));
            auto vl = uikit::Label("—", 52, 16, kDarkInk, 11.0f, /*mono=*/true);
            *vOut = vl.get();
            row->AddChild(std::move(vl));
            card->AddChild(std::move(row));
        };
        urow("输入", &m_hud.usageInV, &m_usageBarIn);
        urow("输出", &m_hud.usageOutV, &m_usageBarOut);
        insp->AddChild(std::move(card));
    }

    AddLine(*insp, "协议健康", kDarkMuted, true, 200, 11.0f);
    {
        auto card = makeCard(70);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        // ★ 第三行（2026-10-13）：未知 kind 单独一行。
        //   为何不并进 healthViol：实测把「未知kind N」追加到同一 label 后**被裁掉**
        //   （该 label 宽 150，`违规 N · 未知 N · 未知kind N` 放不下，截图可见「未知k」）。
        //   单独一行既避免裁切，也让「协议未规定的事」有一处固定的可见落点。
        auto hrow = [&card](ECDI::Label** vOut, const char* initial = "—") {
            auto row = uikit::Row(178, 16, 8);
            row->AddChild(uikit::Dot(8, kOkDark));
            auto vl = uikit::Label(initial, 150, 16, kDarkInk);
            *vOut = vl.get();
            row->AddChild(std::move(vl));
            card->AddChild(std::move(row));
        };
        hrow(&m_hud.healthSeq);
        hrow(&m_hud.healthViol);
        hrow(&m_hud.healthKind, "未知 kind · 0");
        insp->AddChild(std::move(card));
    }

    AddLine(*insp, "权限历史", kDarkMuted, true, 200, 11.0f);
    {
        auto card = makeCard(70);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        for (size_t i = 0; i < 3; ++i) {
            auto row = uikit::Row(178, 14, 8);
            auto chip = uikit::Label("", 42, 14, kDarkMuted, 10.5f, /*mono=*/true);
            m_hud.histChip[i] = chip.get();
            row->AddChild(std::move(chip));
            auto what = uikit::Label("", 122, 14, kDarkMuted, 11.0f);
            m_hud.histWhat[i] = what.get();
            row->AddChild(std::move(what));
            card->AddChild(std::move(row));
        }
        insp->AddChild(std::move(card));
    }

    main->AddChild(std::move(insp));

    root.AddChild(std::move(main));
}

// 状态栏（hi-fi .statusbar）：圆点+状态 | 消息数 | tokens | 重绘（Composer 已收进中栏）
void HarnessApp::BuildBottom(ECDI::Widget& root) {
    auto status = std::make_unique<ECDI::Panel>();
    status->SetSize(1000, 26);
    status->SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kDarkTopBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    status->SetLayout(std::make_unique<ECDI::HorizontalLayout>(14, true, 6));

    status->AddChild(uikit::DotCell(7, kOkDark, 14, &m_hud.sbDot));

    auto stState = std::make_unique<ECDI::Label>("idle");
    m_hud.stState = stState.get();
    stState->SetTextColor(kDarkMuted);
    stState->SetStretch(1);
    stState->SetSize(200, 16);
    status->AddChild(std::move(stState));

    auto monoLine = [](const std::string& text, int width) {
        auto lbl = std::make_unique<ECDI::Label>(text);
        lbl->SetTextColor(kDarkMuted);
        ECDI::Font f; f.size = 11.0f; f.family = kMonoFamily;
        lbl->SetFont(f);
        lbl->SetSize(width, 16);
        return lbl;
    };
    m_hud.stMsgs = AddLine(*status, "0 / 0 msg", kDarkMuted, false, 120, 11.0f);
    m_hud.stTokens = AddLine(*status, "tokens —", kDarkMuted, false, 150, 11.0f);
    m_hud.stPaint = AddLine(*status, "paints —", kDarkMuted, false, 210, 11.0f);
    root.AddChild(std::move(status));
}

// ── 回放拉取（~30Hz 单定时器驱动，规格 §6.1）───────────────────

void HarnessApp::OnReplayTick() {
    if (!m_source) return;   // 源启动失败（Run 已报错退出路径）
    if (m_source->IsLive()) {
        // ── live（子进程）：本 tick 内带超时批量拉取，把时间还给消息循环 ──
        // （v1 定案：UI 线程仍做拉取，但**不无限阻塞**——每 tick 8ms 预算，
        //   否则 WM_PAINT 饿死、窗口全黑；P2 改用 PostToUi 异步投递）
        bool gotAny = false;
        const int budgetMs = 8;
        std::string line;
        while (m_child->NextWithin(line, gotAny ? 0 : budgetMs)) {
            ++m_liveLines;
            HandleLine(line);
            gotAny = true;
        }
        if (gotAny) {
            Relayout();
            FollowTailIfPinned();
            CacheEvalMetrics();
            RefreshHud();
            RequestRepaint();
        } else if (m_child->Eof() && !m_liveEof) {
            m_liveEof = true;   // core 退出（stdin EOF 或 bye 收尾）
            m_hudDirty = true;
            RefreshHud();
        }
        return;
    }

    if (m_replay->Empty() && !m_waitForAnswer) {
        if (m_replaySettled) {
            if (m_relayoutPulse > 0) {   // 折叠/展开动画、settle 后新增行：几何持续跟随
                --m_relayoutPulse;
                Relayout();
                FollowTailIfPinned();
                RequestRepaint();
            }
            if (m_hudDirty) {   // 已拉完：只在有新读数时刷一次 HUD，不再制造重绘
                CacheEvalMetrics();
                RefreshHud();
            }
            return;
        }
        m_replaySettled = true;
    }
    if (m_options.timed) {
        DrainTimed();
    } else {
        DrainInstant();
    }
    Relayout();
    FollowTailIfPinned();
    if (m_replaySettled && m_options.scrollSweepStep > 0 && !m_sweepStarted && m_scroll) {
        // --scroll-sweep：等夹具拉完且尾随完成后，从顶部开始逐 tick 扫描到底
        m_sweepStarted = true;
        m_scroll->SetContentOffset(0, 0);
        if (m_window) {
            m_window->GetPlatformWindow().StartTimer(kSweepTimer, 33);
        }
    }
    CacheEvalMetrics();
    RefreshHud();
    RequestRepaint();
}

void HarnessApp::OnAutoClose() {
    if (m_application) m_application->Exit();
}

// --scroll-sweep 步进：每次前进 step px；扫到底即退出（摘要由 Run() 返回后落盘）
void HarnessApp::OnSweepTick() {
    if (!m_scroll || m_sweepDone) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const int y = m_scroll->GetScrollOffsetY() + m_options.scrollSweepStep;
    if (y >= maxY) {
        m_scroll->SetContentOffset(0, maxY);
        m_sweepDone = true;
    } else {
        m_scroll->SetContentOffset(0, y);
    }
    CacheEvalMetrics();
    RefreshHud();
    RequestRepaint();
    if (m_sweepDone && m_application) {
        m_application->Exit();
    }
}

void HarnessApp::DrainInstant() {
    if (m_instantDone) return;
    m_instantDone = true;
    std::string line;
    while (m_source->Next(line)) HandleLine(line);
    if (!m_pendingTruncated.empty()) {
        AddNoticeRow(m_pendingTruncated, kErrorDark);   // 协议 §1.2：截断必须显式报错
        m_pendingTruncated.clear();
    }
}

void HarnessApp::DrainTimed() {
    if (m_waitForAnswer) return;   // I3：ask 未答前 core 不再发消息
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  m_timedAnchor)
            .count();
    std::string line;
    while (m_replay->NextDue(elapsedMs, line)) {
        HandleLine(line);
        if (m_waitForAnswer) break;   // ask 已显示，停在 waiting（协议 §8.3）
    }
    if (m_replay->Empty() && !m_pendingTruncated.empty()) {
        AddNoticeRow(m_pendingTruncated, kErrorDark);
        m_pendingTruncated.clear();
    }
}

void HarnessApp::HandleLine(const std::string& line) {
    std::string notice;
    const bool ok = m_transcript.OnLine(line, notice);
    if (!notice.empty()) AddNoticeRow(notice, kErrorDark);   // I4 / I5 等显式提示
    if (ok) RouteLastMessage();
}

void HarnessApp::RouteLastMessage() {
    const json::Value& msg = m_transcript.LastMessage();
    const std::string t = msg.GetString("t");

    if (t == "block_start") {
        if (const Block* block = m_transcript.FindBlock(msg.GetString("id"))) {
            CreateBlockRow(*block);
        }
        return;
    }
    if (t == "delta") {
        const std::string id = msg.GetString("id");
        const auto row = m_blockRows.find(id);
        const Block* block = m_transcript.FindBlock(id);
        if (row != m_blockRows.end() && block && row->second->label) {
            // ★ 未知 kind 的行有**前缀**（`[未知 kind: xxx]`）——不能整段覆盖，
            //   否则流式 delta 一到就把前缀擦掉（2026-10-13 实测：截图里 r2 的
            //   `[未知 kind: thinking]` 前缀正是这样丢的）。此处保持与
            //   `CreateBlockRow` 同一拼法。
            if (block->unknownKind) {
                row->second->label->SetText(UnknownKindText(*block));
            } else {
                row->second->label->SetText(block->text);   // 合并：只追加到文本缓冲，不逐字符刷新
            }
        }
        return;
    }
    if (t == "block_end") {
        const std::string id = msg.GetString("id");
        const Block* block = m_transcript.FindBlock(id);
        const auto row = m_blockRows.find(id);
        if (row == m_blockRows.end() || !row->second->headerBtn) {
            return;
        }
        // ★ REQ-02（2026-10-13）：`tool_call` 的 `summary` 此前被吞——原条件只认
        //   `tool_result`，而假 core 在 **`tool_call` 的 `block_end`** 里就发了
        //   `summary:"已发起"`（协议 §3.5 允许），于是那条摘要无处落地。
        //   修正为两类都更新标题：`tool_call` 用自身 name，`tool_result` 用 forId 引用的工具名。
        const bool isResult = (block && block->kind == "tool_result");
        const bool isCall = (block && block->kind == "tool_call");
        if (isResult) {
            // hi-fi：结果行标题 = 工具名（来自 forId 引用的 call 块）+ ok/失败摘要
            std::string title = "▸ ";
            if (const Block* call = m_transcript.FindBlock(block->forId)) {
                title += call->name.empty() ? "tool" : call->name;
            } else {
                title += "tool";
            }
            if (!block->summary.empty()) {
                title += block->ok ? " · " : " · 失败 · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
        } else if (isCall) {
            // 工具调用：标题 = 工具名（+ 可选摘要）——此前 summary 在此被丢弃
            std::string title = "▸ " + (block->name.empty() ? "tool_call" : block->name);
            if (!block->summary.empty()) {
                title += block->ok ? " · " : " · 失败 · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
        }
        return;
    }
    if (t == "ask") {
        CreateAskRow(msg);
        return;
    }
    if (t == "error") {
        AddNoticeRow("[error] " + msg.GetString("message"), kErrorDark);
        return;
    }
    if (t == "hello") {
        // 捕获给 HUD 左栏（转录里仍留一行 core 信息，与原型一致）
        m_sessionId = msg.GetString("session");
        m_coreVer = msg.GetString("core");
        m_protocolVer = static_cast<int>(msg.GetNumber("protocol"));
        if (const json::Value* caps = msg.Find("caps"); caps && caps->IsArray()) {
            std::string joined;
            for (const auto& cap : caps->array) {
                if (!joined.empty()) joined += ", ";
                joined += cap.text;
            }
            m_capsStr = joined;
        }
        m_hudDirty = true;
        AddNoticeRow("core " + msg.GetString("core") + " · session " +
                         msg.GetString("session") + " · protocol " +
                         std::to_string(m_protocolVer),
                     kDarkMuted);
        return;
    }
    if (t == "state") {
        m_coreState = msg.GetString("value");
        m_hudDirty = true;
        return;
    }
    if (t == "usage") {
        m_usageTurn = static_cast<int>(msg.GetNumber("turn"));
        m_usageIn = static_cast<long long>(msg.GetNumber("input"));
        m_usageOut = static_cast<long long>(msg.GetNumber("output"));
        m_hudDirty = true;
        return;
    }
    if (t == "bye") {
        AddNoticeRow("会话结束 (" + msg.GetString("reason") + ", code " +
                         std::to_string(static_cast<int>(msg.GetNumber("code"))) + ")",
                     kDarkMuted);
        return;
    }
    // state / usage：上移到 HUD，转录不渲染
}

// ── 行构建（规格 §5.2 渲染映射）────────────────────────────────

void HarnessApp::AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                        const std::string& blockId) {
    RowRec stored = rec;
    stored.panel = row.get();
    if (m_options.rowModeList) {
        stored.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
    }
    m_rows.push_back(stored);
    RowRec& ref = m_rows.back();
    m_content->AddChild(std::move(row));
    if (!blockId.empty()) m_blockRows[blockId] = &ref;
    // settle 后新增的行（发送/停止/回答反馈）没有逐 tick 的 Relayout 可蹭——
    // 排一个补重排脉冲，让定时器把它布局出来
    m_relayoutPulse = std::max(m_relayoutPulse, 2);
}

void HarnessApp::CreateBlockRow(const Block& block) {
    auto panel = std::make_unique<ECDI::Panel>();
    RowRec rec;

    if (block.kind == "user") {
        rec.alignRight = true;
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kUserBorder});
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        panel->AddChild(std::move(label));
        AddRow(std::move(panel), rec, block.id);
        return;
    }

    if (block.kind == "assistant" || block.kind == "system" || block.kind == "error") {
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kCardBorder});
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        if (block.kind == "system") label->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
        if (block.kind == "error") label->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
        panel->AddChild(std::move(label));
        AddRow(std::move(panel), rec, block.id);
        return;
    }

    // ★★ 未知 `kind` 的**中性落点**（2026-10-13）。
    //   问题（实测取证，见 `.scratch/kind-probe.png` / `cot-findings.md`）：
    //     §3.3 只列六个 kind，而 I5 只管信封层的 `t` ⇒ 未知 `kind` **不被任何不变量覆盖**。
    //     此前的三分支写法让它们**静默掉进工具卡分支**，而该分支的 else 把标题写死成
    //     「等待结果」——那正是 `tool_result` 在 `block_end` 到达**前**的占位标题
    //     ⇒ **思维链块与「正在等的工具结果」视觉上无法区分**（不是难看，是**误导**）。
    //   ★ 归属 **harness 侧**（ECDI 只是忠实渲染调用方给的控件树）。
    //   ★ 处置 = **中性渲染**：弱化色 + 明写 `[未知 kind: xxx]` + 普通气泡（不折叠、
    //     不占工具卡的位置）。理由：既保住 I5 的前向兼容精神（内容仍被渲染、不被丢弃），
    //     又不让它**伪装成有语义的形态**。
    //   ★ 思维链（`reasoning` / `thinking` …）正是这条路径的主要真实来源——
    //     给它一个**一等公民的位置**需要协议 v2（`kind:"reasoning"`），
    //     在那之前，中性渲染是「不误导」的最小正确行为。
    if (block.unknownKind) {
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kWarn});   // 琥珀描边：视觉上明确「这不是普通助手文本」
        // 前缀显式标出不认识的 kind；正文仍按原样展示（不丢内容）
        std::string shown = UnknownKindText(block);
        auto label = std::make_unique<ECDI::Label>(shown);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kWarn, kProbeInk));
        panel->AddChild(std::move(label));
        AddRow(std::move(panel), rec, block.id);
        return;
    }

    // tool_call / tool_result：可折叠卡片（默认折叠；标题按钮 + CollapsiblePanel 展开体）
    rec.tool = true;
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kToolBg, kProbeBg),
        .cornerRadius = kRadiusTool,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .borderColor = kToolBorder});

    std::string title;
    std::string bodyText;
    if (block.kind == "tool_call") {
        title = block.name.empty() ? "tool_call" : block.name;
        bodyText = block.argsPretty.empty() ? "{}" : block.argsPretty;
    } else {
        title = "等待结果";
        bodyText = block.text.empty() ? block.summary : block.text;
    }

    auto header = std::make_unique<ECDI::Button>("▸ " + title);
    rec.headerBtn = header.get();
    header->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
    {
        ECDI::Font f; f.size = 12.0f; f.family = kMonoFamily;
        header->SetFont(f);
    }
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kToolHeaderBg, kProbeBg),
        .border = kToolHeaderBorder,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover,
        .textAlignment = ECDI::TextAlignment::Left});   // Phase 30 △3：卡头贴左（hi-fi .tool>.h）
    panel->AddChild(std::move(header));
    auto body = std::make_unique<ECDI::CollapsiblePanel>();
    rec.body = body.get();
    rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
    auto label = std::make_unique<ECDI::Label>(bodyText);
    rec.label = label.get();
    label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
    label->SetWordWrap(true);   // Phase 29：工具体按 bodyW 断行（JSON/日志长串）
    {
        ECDI::Font f; f.size = 12.0f; f.family = kMonoFamily;
        label->SetFont(f);
    }
    // Phase 30 批二：CollapsiblePanel 收起态钉住内容可见性（#51④）——
    // 收起溢绘已由框架修复，应用侧 SetVisible workaround 退役。
    rec.body->GetContent()->AddChild(std::move(label));
    panel->AddChild(std::move(body));

    AddRow(std::move(panel), rec, block.id);

    // deque：push_back 不失效引用——回调捕获行记录指针是安全的
    RowRec& stored = m_rows.back();
    stored.headerBtn->SetOnClick([this, &stored] {
        if (m_options.rowModeList) return;   // 固定行高模式：展开体会被后续行覆盖，v1 禁用
        stored.expanded = !stored.expanded;
        if (stored.body) stored.body->Toggle();
        m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画，行高/后续行位置持续跟随
        Relayout();             // 立即重排一次（settle 后定时器路径不再经过 Relayout）
        RequestRepaint();
    });
}

void HarnessApp::CreateAskRow(const json::Value& msg) {
    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = kAskBg, .cornerRadius = kRadiusTool,
        .borderWidth = 1.0f, .borderColor = kAskBorder});
    RowRec rec;
    rec.ask = true;
    rec.askId = msg.GetString("id");   // P1.5：answer 回执要带 id

    auto label = std::make_unique<ECDI::Label>("权限 " + msg.GetString("question"));
    rec.label = label.get();
    label->SetTextColor(kAskQInk);
    {
        ECDI::Font f; f.size = 13.0f;
        label->SetFont(f);
    }
    panel->AddChild(std::move(label));

    auto allow = std::make_unique<ECDI::Button>("允许");
    rec.allowBtn = allow.get();
    allow->SetTextColor(kWarnInk);
    {
        ECDI::Font f; f.size = 12.0f;
        allow->SetFont(f);
    }
    allow->SetStyle(ECDI::ButtonStyleOverride{
        .background = kWarn,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kWarn,
        .hoverBackground = ECDI::Color::FromRGBA8(240, 183, 92, 255)});
    panel->AddChild(std::move(allow));
    auto deny = std::make_unique<ECDI::Button>("拒绝");
    rec.denyBtn = deny.get();
    deny->SetTextColor(kDenyInk);
    {
        ECDI::Font f; f.size = 12.0f;
        deny->SetFont(f);
    }
    deny->SetStyle(ECDI::ButtonStyleOverride{
        .background = kBtnDenyBg,
        .border = kAskBorder,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .hoverBackground = kDenyHover});
    panel->AddChild(std::move(deny));

    AddRow(std::move(panel), rec, msg.GetString("id"));
    RowRec& stored = m_rows.back();
    stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
    stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });

    if (!m_options.autoAnswer.empty()) {
        MarkAnswered(stored, m_options.autoAnswer);   // 无人值守：立即回答（协议 §8.3）
    } else if (m_options.timed) {
        m_waitForAnswer = true;   // timed 回放停在 waiting，直到用户点选
    }
}

// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
void HarnessApp::SendPrompt(const std::string& text) {
    if (m_writer) {
        // live：真发 prompt（协议 §6：仅 idle 合法——按钮使能已由 state 驱动）
        const bool ok = m_writer->Prompt(text);
        AddNoticeRow(ok ? "[prompt → core] " + text : "[prompt 发送失败] " + text,
                     ok ? kDarkMuted : kErrorDark);
    } else {
        AddNoticeRow("[prompt 仅记录] " + text, kDarkMuted);   // 回放：只记录
    }
    Relayout();   // settle 后立即反馈，不等下一个 tick
    RequestRepaint();
}

void HarnessApp::MarkAnswered(RowRec& rec, const std::string& choice) {    if (rec.answered) return;
    rec.answered = true;
    if (rec.allowBtn) rec.allowBtn->SetEnabled(false);
    if (rec.denyBtn) rec.denyBtn->SetEnabled(false);
    m_waitForAnswer = false;
    if (m_writer) {
        // live：真发 answer（协议 §4：需 id + choice；I3 的解除信号）
        if (!rec.askId.empty()) {
            m_writer->Answer(rec.askId, choice);
        }
    }
    m_askHist.emplace_back(choice, rec.label ? rec.label->GetText() : std::string());
    if (m_askHist.size() > 16) m_askHist.pop_front();
    m_hudDirty = true;
    AddNoticeRow("[answer] " + choice, kDarkMuted);
    RequestRepaint();
}

void HarnessApp::AddNoticeRow(const std::string& text, const ECDI::Color& color) {
    auto panel = std::make_unique<ECDI::Panel>();
    RowRec rec;
    auto label = std::make_unique<ECDI::Label>(text);
    rec.label = label.get();
    label->SetTextColor(color);
    panel->AddChild(std::move(label));
    AddRow(std::move(panel), rec, std::string());
}

// ── 布局与重绘（规格 §5.3 / §6.2）──────────────────────────────

void HarnessApp::Relayout() {
    const int contentW = ContentWidth(m_scroll);

    if (m_options.rowModeList) {
        // listlayout：行高固定、不测量；Arrange 由 ListLayout 完成
        for (RowRec& r : m_rows) {
            if (r.panel->GetHeight() != kListRowHeight) {
                r.panel->SetSize(contentW, kListRowHeight);
            }
        }
        m_content->Arrange();
        m_scroll->UpdateContentExtent();
        return;
    }

    // autosize：每行按内容测量（文本测量成本正是本模式要暴露的指标 ④）
    int y = 0;
    for (RowRec& r : m_rows) {
        int rowH = 0;
        if (r.tool) {
            const int bodyW = contentW - 2 * kRowPadX;
            const int wrapW = std::max(40, bodyW - 20);
            int bodyH = 0;
            if (r.label) {
                // Phase 29 wrap：工具体按 bodyW 宽断行；★ 宽度不变不 SetSize——
                // 无条件 SetSize 会打穿 C29-10 wrap 布局缓存（指纹含宽度），
                // 逐帧重建 = 4.8s 测量成本事故（2026-10-06 实测）
                if (r.lastWrapW != wrapW) {
                    r.label->SetSize(wrapW, r.label->GetHeight());
                    r.lastWrapW = wrapW;
                }
                // ★★ A-2 已复核**否决**（2026-10-13 晚）：此处**保持**无条件 `AutoSize()`。
                //   我曾判定「折叠卡的长文本被每帧重测」并改成「按需重测」，
                //   **实测推翻了它**——把 `chars` 分解为 `chars = a(一次性) + b(每帧)·frames`
                //   （`.scratch/probe_a2_decompose.py`，同长度下多档 `--auto-close` 拟合）：
                //     body=4000 ⇒ a = 123,081 · **b = 504**
                //     body=40   ⇒ a =     138 · **b = 502**
                //   ⇒ **每帧成本与文本长度无关**（b 之比 **1.00×**）；长文本的代价是
                //     **一次性**的布局建立（ECDI 的 C29-10 五元组指纹缓存**按文档生效**）。
                //   ⇒ 先前那个「8.80×」是**同时长下两个不同长度**的对比，差值全在 a，
                //     被我误读成了逐帧浪费 ⇒ **原判收回**（条 30/40）。
                //   ⇒ 「按需重测」那版实测**零收益**（读数完全一致），故**不保留**——
                //     留一个不解决问题的状态变量只会带来「失效纪律」的腐化风险。
                //   ★ 一次性成本是**必需的**：`CollapsiblePanel` 的收起态 `SetSize`
                //     **定义展开基准**（其头注释 76-78 行），`bodyH` 必须有值。
                r.label->AutoSize();
                bodyH = r.label->GetHeight() + 8;
            }
            r.bodyH = bodyH;
            if (r.headerBtn) {
                r.headerBtn->SetSize(contentW - 2 * kRowPadX, kHeaderH);
                r.headerBtn->SetPosition(kRowPadX, 2);
            }
            if (r.body) {
                // 只在基准尺寸变化时下发：收起态 SetSize 定义展开基准，
                // 展开态 SetSize 会直接钉住呈现值——动画期间重入会把动画打死
                if (r.lastBodyW != bodyW || r.lastBodyH != bodyH) {
                    r.lastBodyW = bodyW;
                    r.lastBodyH = bodyH;
                    r.body->SetSize(bodyW, bodyH);
                }
                r.body->SetPosition(kRowPadX, 2 + kHeaderH + 2);
            }
            rowH = 2 + kHeaderH + 2 + (r.expanded ? bodyH : 0);
        } else {
            const int maxBubbleW = std::max(120, contentW * 78 / 100);
            const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
            if (r.label) {
                if (r.ask) {
                    r.label->AutoSize();   // 权限问句保持单行
                    if (r.allowBtn) {
                        r.allowBtn->SetSize(70, 28);
                        r.allowBtn->SetPosition(contentW - 160, 5);
                    }
                    if (r.denyBtn) {
                        r.denyBtn->SetSize(70, 28);
                        r.denyBtn->SetPosition(contentW - 82, 5);
                    }
                } else {
                    // 聊天气泡（Phase 29 wrap）：单行放得下 → 气泡收缩；
                    // 超上限 → 按上限宽断行（高度 = 行数×行高，布局驱动）
                    if (!r.wrapped) {
                        r.label->AutoSize();   // 单行 extent
                    }
                    // ★ REQ-01（2026-10-13）：含**显式 `\n`** 的文本也必须走 wrap 路径。
                    //   非 wrap ⇒ `TextWidget` 走 Phase 28 的「单条 DrawText、布局引擎零接触」
                    //   路径（`TextWidget.h:62-63`）⇒ **`\n` 不被消费**，整段被拼成一行，
                    //   再由宽度重新断行（断点与原意不符）。ECDI 侧本身正确（`TextWidget.cpp:235-248`
                    //   显式切硬段、段数 = `\n` 数 + 1），此前误判为 ECDI 缺陷，实为本行只看了宽度。
                    const bool hasNewline =
                        r.label->GetText().find('\n') != std::string::npos;
                    const bool needWrap =
                        r.wrapped || hasNewline || r.label->GetWidth() > maxTextW;
                    if (needWrap != r.wrapped) {
                        r.wrapped = needWrap;
                        r.label->SetWordWrap(needWrap);
                    }
                    if (r.wrapped) {
                        if (r.lastWrapW != maxTextW) {
                            r.label->SetSize(maxTextW, r.label->GetHeight());
                            r.lastWrapW = maxTextW;
                        }
                        r.label->AutoSize();   // 稳定期命中 wrap 布局缓存，不逐帧重建
                    }
                }
                r.label->SetPosition(kRowPadX, 5);
            }
            rowH = std::max(r.label ? r.label->GetHeight() + 10 : 0, r.ask ? 38 : 0);
        }
        if (r.tool || r.ask) {
            // 工具卡 / 权限门：通栏（hi-fi align-self: stretch）
            r.panel->SetSize(contentW, rowH);
            r.panel->SetPosition(0, y);
        } else {
            // 聊天气泡（hi-fi .msg max-width:78%）：user 右对齐、assistant 左对齐
            const int maxBubbleW = std::max(120, contentW * 78 / 100);
            int bubbleW = contentW;
            if (r.label) {
                bubbleW = r.wrapped
                              ? maxBubbleW
                              : std::min(maxBubbleW,
                                         r.label->GetWidth() + 2 * kRowPadX + 2);
            }
            r.panel->SetSize(bubbleW, rowH);
            r.panel->SetPosition(r.alignRight ? contentW - bubbleW : 0, y);
        }
        y += rowH + kRowGap;
    }
    m_scroll->SetContentExtent(contentW, std::max(0, y - kRowGap));
}

void HarnessApp::FollowTailIfPinned() {
    if (!m_scroll) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const bool pinned = m_scroll->GetScrollOffsetY() >= maxY - 60;   // 用户上翻 > 60px 则不跟随
    if (pinned) m_scroll->SetContentOffset(0, maxY);
}

void HarnessApp::RequestRepaint() {
    ++m_invalidations;
    if (m_counting) m_counting->Invalidate();
}

void HarnessApp::CacheEvalMetrics() {
    if (!m_counting) return;
    m_evalPaints = m_counting->PaintCount();
    const std::vector<uint64_t>& frames = m_counting->FrameDurationsUs();
    m_evalFrames = frames.size();
    m_evalMaxUs = 0;
    m_evalSumUs = 0;
    for (const uint64_t us : frames) {
        m_evalMaxUs = std::max(m_evalMaxUs, us);
        m_evalSumUs += us;
    }
    m_evalLastUs = frames.empty() ? 0 : frames.back();

    if (m_probe) {
        m_evalProbeFrames = m_probe->FrameCount();
        m_evalProbeCommands = m_probe->TotalCommands();
        m_evalProbeMax = m_probe->MaxFrameCommands();
        m_evalProbeAvg = m_probe->AvgFrameCommands();
        m_evalMaxExecUs = m_probe->MaxExecuteUs();
        m_evalAvgExecUs = m_probe->AvgExecuteUs();
    }
    if (m_measureProbe) {
        m_evalMeasureCalls = m_measureProbe->MeasureCalls();
        m_evalMeasureTotalUs = m_measureProbe->TotalUs();
        m_evalMeasureMaxUs = m_measureProbe->MaxUs();
    }
}

// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。
void HarnessApp::RefreshHud() {
    m_hudDirty = false;
    if (!m_hud.title) return;

    m_hud.title->SetText("harness");
    if (m_hud.rowChip) {
        m_hud.rowChip->SetText(m_options.rowModeList ? "listlayout" : "autosize");
    }
    if (m_hud.sessChip) {
        m_hud.sessChip->SetText(m_sessionId.empty() ? "s-____" : m_sessionId);
    }
    if (m_hud.backendChip) {
        m_hud.backendChip->SetText(m_backendName + " ▾");
    }
    const std::string stateText =
        m_coreState.empty() ? std::string("idle") : m_coreState;
    const bool active = (m_coreState == "running" || m_coreState == "waiting");
    // ★ REQ-04（2026-10-13）：按 `state` 驱动发送/停止的使能（协议 §6 硬要求）。
    //   判据 A5「idle 时发送可点/停止灰；running 时相反」在此兑现。
    //   ★ 只在**回放/live 都成立**的语义下驱动：默认空 state = idle ⇒ 发送可用。
    //   `active` 已含 running + waiting（waiting = ask 未答，同样不可再发）。
    if (m_promptBar) {
        m_promptBar->SetStateDrivenEnabled(!active);
    }
    // ★★ REQ-04 直接回读探针（--state-probe，2026-10-14）
    //   · `SetEnabled` 不内建 Invalidate（Widget.h:185-187）⇒ 使能翻转**没有**绘制侧读数
    //     ⇒ 用**只读回读**替代：把按钮的真实 IsEnabled() 与 GetText() 取出来记档。
    //   · 只在「与上次不同」时记一行 ⇒ 摘要是一份状态翻转轨迹，可逐行核对协议 §6。
    //   · 放在 SetStateDrivenEnabled **之后**——读到的才是本次驱动后的结果。
    if (m_options.stateProbe && m_promptBar) {
        ECDI::Button* send = m_promptBar->sendButton();
        ECDI::Button* stop = m_promptBar->stopButton();
        if (send && stop) {
            const bool sendEnabled = send->IsEnabled();
            const bool stopEnabled = stop->IsEnabled();
            const std::string sendTitle = send->GetText();
            const bool changed = !m_stateProbeLastValid ||
                                 sendEnabled != m_stateProbeLastSend ||
                                 stopEnabled != m_stateProbeLastStop;
            if (changed) {
                StateProbeRec rec;
                rec.coreState = m_coreState.empty() ? "idle" : m_coreState;
                rec.sendEnabled = sendEnabled;
                rec.stopEnabled = stopEnabled;
                rec.sendTitle = sendTitle;
                m_stateProbeLog.push_back(std::move(rec));
                m_stateProbeLastSend = sendEnabled;
                m_stateProbeLastStop = stopEnabled;
                m_stateProbeLastValid = true;
            }
        }
    }
    if (m_hud.stateChip) {
        m_hud.stateChip->SetText(stateText);
        m_hud.stateChip->SetTextColor(kDarkMuted);
    }
    if (m_hud.statusDot) {
        m_hud.statusDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 4.0f});
    }
    if (m_hud.sbDot) {
        m_hud.sbDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 3.5f});
    }

    if (m_hud.sessSession) {
        m_hud.sessSession->SetText(m_sessionId.empty() ? "—" : m_sessionId);
    }
    if (m_hud.sessCore) {
        m_hud.sessCore->SetText(m_coreVer.empty() ? "—" : m_coreVer);
    }
    if (m_hud.sessProgress) {
        char buf[64];
        if (m_replay) {
            std::snprintf(buf, sizeof(buf), "%zu / %zu 条", m_replay->Consumed(),
                          m_replay->Total());
        } else {
            std::snprintf(buf, sizeof(buf), "%zu 条（live）", m_liveLines);
        }
        m_hud.sessProgress->SetText(buf);
    }
    if (m_replayBar) {
        if (m_replay) {
            const size_t total = m_replay->Total();
            m_replayBar->SetProgress(total > 0
                                         ? static_cast<float>(m_replay->Consumed()) /
                                               static_cast<float>(total)
                                         : 0.0f);
        } else {
            m_replayBar->SetProgress(0.0f);   // live：总量未知，进度条不表达
        }
    }

    if (m_hud.healthSeq) {
        const size_t gaps = m_transcript.SeqGaps();
        if (gaps == 0) {
            m_hud.healthSeq->SetText("seq 连续 · 0 缺口");
            m_hud.healthSeq->SetTextColor(kDarkInk);
        } else {
            m_hud.healthSeq->SetText("seq 缺口 " + std::to_string(gaps));
            m_hud.healthSeq->SetTextColor(kErrorDark);
        }
    }
    if (m_hud.healthViol) {
        m_hud.healthViol->SetText("违规 " + std::to_string(m_transcript.Violations()) +
                                  " · 未知 " + std::to_string(m_transcript.UnknownTypes()));
    }
    // ★ 未知 kind 单列一行（2026-10-13）：它与「未知 t」管辖**不同的判别符**，
    //   合并会同时导致①语义误读（把「协议空白」读成「I5 已覆盖」）
    //   ②文本裁切（并进上行实测被裁，见 BuildInspector 注释）。
    if (m_hud.healthKind) {
        const size_t uk = m_transcript.UnknownKinds();
        if (uk == 0) {
            m_hud.healthKind->SetText("未知 kind · 0");
            m_hud.healthKind->SetTextColor(kDarkMuted);
        } else {
            // 协议 §3.3 没有这些值 ⇒ 用琥珀色（与 seq 缺口的红、正常的灰区分）
            m_hud.healthKind->SetText("未知 kind · " + std::to_string(uk) + " ⚠");
            m_hud.healthKind->SetTextColor(kWarn);
        }
    }
    if (m_hud.evalPaints) {
        m_hud.evalPaints->SetText(std::to_string(m_evalPaints) + " 次 · 请求 " +
                                  std::to_string(m_invalidations));
    }
    if (m_hud.probeCmds) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeCmds->SetText("avg " + std::to_string(m_evalProbeAvg));
        } else {
            m_hud.probeCmds->SetText("—");
        }
    }
    if (m_hud.probeExec) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeExec->SetText("avg " +
                                     std::to_string(static_cast<unsigned long long>(m_evalAvgExecUs)) +
                                     " us");
        } else {
            m_hud.probeExec->SetText("—");
        }
    }
    if (m_hud.usageInV) {
        m_hud.usageInV->SetText(m_usageIn < 0 ? "—"
                                              : std::to_string(m_usageIn));
    }
    if (m_hud.usageOutV) {
        m_hud.usageOutV->SetText(m_usageOut < 0 ? "—"
                                                : std::to_string(m_usageOut));
    }
    if (m_usageBarIn && m_usageBarOut) {
        const long long peak = std::max(m_usageIn, m_usageOut);
        const float inFrac = (m_usageIn >= 0 && peak > 0)
                                 ? static_cast<float>(m_usageIn) / static_cast<float>(peak)
                                 : 0.0f;
        const float outFrac = (m_usageOut >= 0 && peak > 0)
                                  ? static_cast<float>(m_usageOut) / static_cast<float>(peak)
                                  : 0.0f;
        m_usageBarIn->SetProgress(inFrac);
        m_usageBarOut->SetProgress(outFrac);
    }
    for (size_t i = 0; i < 3; ++i) {
        if (m_hud.histChip[i] && m_hud.histWhat[i]) {
            const size_t n = m_askHist.size();
            if (i < n) {
                const auto& [choice, what] = m_askHist[n - 1 - i];
                m_hud.histChip[i]->SetText(choice);
                m_hud.histChip[i]->SetTextColor(choice == "allow" ? kOkDark : kErrorDark);
                m_hud.histWhat[i]->SetText(what);
            } else {
                m_hud.histChip[i]->SetText("");
                m_hud.histWhat[i]->SetText("");
            }
        }
    }

    if (m_hud.stState) {
        m_hud.stState->SetText(stateText + " · protocol " +
                               std::to_string(m_protocolVer));
    }
    if (m_hud.stMsgs) {
        if (m_replay) {
            m_hud.stMsgs->SetText(std::to_string(m_replay->Consumed()) + " / " +
                                  std::to_string(m_replay->Total()) + " msg");
        } else {
            m_hud.stMsgs->SetText(std::to_string(m_liveLines) + " msg（live）");
        }
    }
    if (m_hud.stTokens) {
        if (m_usageIn < 0) {
            m_hud.stTokens->SetText("tokens —");
        } else {
            m_hud.stTokens->SetText("tokens " + std::to_string(m_usageIn) + " / " +
                                    std::to_string(m_usageOut));
        }
    }
    if (m_hud.stPaint) {
        m_hud.stPaint->SetText("paints " + std::to_string(m_evalPaints) + " · 上次 " +
                               std::to_string(m_evalLastUs) + " us");
    }
}

// ── 退出摘要（规格 §6.3）───────────────────────────────────────

void HarnessApp::WriteSummary() {
    if (m_summaryWritten) return;
    m_summaryWritten = true;

    // 退出时刻全量刷新缓存：settle 后 CacheEvalMetrics 不再随 tick 执行，
    // 若不在此补刷，摘要里的 paints/probe 读数会冻结在 settle 时刻（undercount
    // 光标闪烁 / HUD 刷新产生的后续帧）。此时窗口与探针仍存活（application 栈对象未析构）。
    CacheEvalMetrics();

    char buf[512];
    std::string json = "{\n";
    json += "  \"fixture\": \"" + m_options.fixtureName + "\",\n";
    json += std::string("  \"mode\": \"") + (m_options.timed ? "timed" : "instant") + "\",\n";
    json += std::string("  \"rowMode\": \"") + (m_options.rowModeList ? "listlayout" : "autosize") +
            "\",\n";
    json += "  \"ecdiCommit\": \"" + m_options.ecdiCommit + "\",\n";
    json += "  \"backend\": \"" + m_backendName + "\",\n";
    std::snprintf(buf, sizeof(buf), "  \"blocks\": %zu,\n", m_transcript.Blocks());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"deltas\": %zu,\n", m_transcript.Deltas());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"messages\": %zu,\n",
                  m_replay ? m_replay->Total() : m_liveLines);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"invalidations\": %zu,\n", m_invalidations);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"paints\": %zu,\n", m_evalPaints);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"frameCount\": %zu,\n", m_evalFrames);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"maxPaintUs\": %llu,\n",
                  static_cast<unsigned long long>(m_evalMaxUs));
    json += buf;
    const uint64_t avg = m_evalFrames > 0 ? m_evalSumUs / m_evalFrames : 0;
    std::snprintf(buf, sizeof(buf), "  \"avgPaintUs\": %llu,\n",
                  static_cast<unsigned long long>(avg));
    json += buf;
    if (m_options.cullProbe && m_probe) {
        // Phase 27 探针读数：实际到达后端的绘制命令（视口剔除后）+ 执行端帧时
        std::snprintf(buf, sizeof(buf), "  \"probeFrames\": %zu,\n", m_evalProbeFrames);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"paintCommands\": %zu,\n", m_evalProbeCommands);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"maxFrameCommands\": %zu,\n", m_evalProbeMax);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"avgFrameCommands\": %zu,\n", m_evalProbeAvg);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"maxExecuteUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMaxExecUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"avgExecuteUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalAvgExecUs));
        json += buf;
        json += "  \"commandMix\": {\"rect\": " + std::to_string(m_probe->RectCommands()) +
                ", \"text\": " + std::to_string(m_probe->TextCommands()) +
                ", \"line\": " + std::to_string(m_probe->LineCommands()) +
                ", \"roundedRect\": " + std::to_string(m_probe->RoundedRectCommands()) +
                ", \"image\": " + std::to_string(m_probe->ImageCommands()) +
                ", \"pushClip\": " + std::to_string(m_probe->PushClipCommands()) +
                ", \"popClip\": " + std::to_string(m_probe->PopClipCommands()) +
                ", \"focusRect\": " + std::to_string(m_probe->FocusRectCommands()) + "},\n";
    }
    if (m_options.cullProbe && m_measureProbe) {
        // 测量链读数：调用次数 / 累计墙钟 / 单次峰值 / 累计字符（us/char 归一化用）
        std::snprintf(buf, sizeof(buf), "  \"measureCalls\": %zu,\n", m_evalMeasureCalls);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureTotalUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMeasureTotalUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureMaxUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMeasureMaxUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureChars\": %zu,\n",
                      m_measureProbe->TotalChars());
        json += buf;
    }
    if (m_options.stateProbe && !m_stateProbeLog.empty()) {
        // ★★ REQ-04 机读证据（2026-10-14）：状态翻转轨迹。
        //   每条 = {core state, 发送使能, 停止使能, 发送标题}——**只在变化时**记，
        //   故可逐行核对协议 §6「发送/停止只由 state 驱动」。
        //   ★ 标题文本来自 `Button::GetText()` **直接回读**（不靠像素/截图）。
        json += "  \"stateProbe\": [";
        for (size_t i = 0; i < m_stateProbeLog.size(); ++i) {
            const StateProbeRec& r = m_stateProbeLog[i];
            json += (i ? ", " : "");
            json += "{\"state\": \"" + r.coreState + "\", \"sendEnabled\": " +
                    (r.sendEnabled ? "true" : "false") + ", \"stopEnabled\": " +
                    (r.stopEnabled ? "true" : "false") + ", \"sendTitle\": \"" +
                    r.sendTitle + "\"}";
        }
        json += "],\n";
    }
    std::snprintf(buf, sizeof(buf), "  \"seqGaps\": %zu,\n", m_transcript.SeqGaps());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"violations\": %zu,\n", m_transcript.Violations());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"unknownTypes\": %zu,\n", m_transcript.UnknownTypes());
    json += buf;
    // ★ 未知 block_start.kind（2026-10-13）：与 unknownTypes **并列而非合并**——
    //   前者是信封层 `t`（I5 明文覆盖），后者是 `kind`（协议未规定，属**空白**）。
    std::snprintf(buf, sizeof(buf), "  \"unknownKinds\": %zu\n", m_transcript.UnknownKinds());
    json += buf;
    json += "}\n";

    if (!m_options.outPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_options.outPath.parent_path(), ec);
        std::ofstream out(m_options.outPath, std::ios::binary);
        if (out) {
            out << json;
        } else {
            ECDI::Logger::Log(ECDI::LogLevel::Warning,
                              L"WriteSummary: cannot open output file");
        }
    }
}
