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
// ★ --hit-probe（REQ-04 边界）：合成鼠标事件走**真实派发链**
//   （Application::OnEvent → OnMouseButtonDown/Up → FindTargetWidget → HitTest → Bubbling）
//   ——不注入系统输入、不动光标、不抢前台，故对用户零干扰（对比 SendInput 路线）。
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"

#include "Theme.h"
#include "UiKit.h"
#include "ThemedCaptionBar.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <utility>

namespace {

constexpr int kListRowHeight = 28;   // listlayout 模式的固定行高（DIP）
constexpr int kRowGap = 10;          // 行间距（原型 C 的消息间距 23px ⇒ 折半，紧凑窗口下够用）
constexpr int kRowPadX = 10;
// ★ 画布左右留白（原型 C 的 22px 画布 padding）。此前行从 x=0 起排，
//   气泡/工具卡**贴着中栏边线**——这是"看起来挤"最直接的来源。
//   紧凑窗口（1120）下取 16，既留出呼吸感又不白吃掉横向空间。
constexpr int kCanvasPadX = 16;
constexpr int kHeaderH = 32;         // tool 卡片标题高，接近原型 C 的 39px

// 视觉令牌唯一来源 = Theme.h（2026-10-06 v6：对齐用户高保真模型）
// ★★ 本 GUI 支持的协议主版本上限（协议 §9；2026-10-14 v2 落地）。
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 `RouteLastMessage`）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
constexpr int kProtocolMaxSupported = 2;

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
using harness::kReasoningBg;
using harness::kReasoningBorder;
using harness::kReasoningHeaderBg;
using harness::kReasoningHover;
using harness::kReasoningInk;
using harness::kAskBg;
using harness::kAskBorder;
using harness::kAskQInk;
using harness::kDenyInk;
using harness::kDenyHover;
using harness::kAllowHover;
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
using harness::kRingTrack;
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

// ★★ 中栏会话头的标题/副标（2026-10-18，原型 C 的 `.conversation-head`）——
//   **单一真相源**：两处生成（建头、`RefreshHud` 里 hello/会话 id 到达后更新），
//   各写一份必然漂移（同 `UnknownKindText` 首版两处各写一份的教训）。
//   ★ 标题用**夹具文件名**（拉丁，可用 Semibold 出口加重）而非写死「harness 会话」——
//     用户要的是「知道自己在聊什么」，而系统标题栏在深色密排版里不构成答案。
//   ★ 副标三要素 = 协议版本 + 模式 + 工作区名（C 的 `在 ECDI_fake_harness · 今天 14:32`
//     在落地时换成真实来源，不写死时间：本 GUI 的时间轴由回放 `ts` 驱动，
//     墙上时钟与协议时间是两个量纲）。
std::string ConversationTitle() {
    return "harness 会话";
}

std::string ConversationSubtitle() {
    return "协议 v2 · 回放模式";
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

// ★★ 思维链卡的标题拼法（协议 v2 §7，2026-10-14）——**单一真相源**。
//   为何要抽成函数：该文本在**三处**生成（建行、`block_end` 到达、点击展开/收起），
//   三处各写一份必然漂移（本项目已踩过同类坑：`UnknownKindText` 首版两处各写一份，
//   delta 一到就把前缀擦掉）。同源即不可能再漂移。
//
//   ★ 箭头字形（2026-10-14，**用户指定**）：用户嫌 `▼`/`▲` 难看，并要求
//     「类似于 `<` 尖括号这种……**向下的尖括号和向上的尖括号**的样子，但是要**钝角**，
//     大概 **120°** 的样子」，随后**直接给出字形**：**`﹀`(U+FE40) / `︿`(U+FE3F)**。
//
//   ★★ **我此前两次都测错了角度（根因 = 量具公式符号反了）**——记此以免重犯：
//     首版公式写成 `acos((k²-1)/(k²+1))`，**正确应为 `acos((1-k²)/(1+k²))`**
//     ⇒ 每个读数都被**镜像**成 `180° - 真值`。后果：
//       · 我说「`▼` 本来就是 118°、角度已达标」——**错**，它其实是 **62°（锐角）**；
//       · 我据此换上的 `∨`/`∧`，我说 127.7°/131.4°——**错**，其实是 **53.1°/49.1°（锐角）**
//         ⇒ 用户要**钝角**，我给的却是**锐角**，方向完全反了。
//     ⇒ 修正后实测（`.scratch/probe_chevron.py`，**先校准再测量**）：
//         `﹀` **134.5°** / `︿` **135.7°**（钝角、细笔画、上下仅差 **1.2°**）⇒ **采用**。
//     ★★ **教训（本轮最该记的一条）**：**测量函数本身是仪器，未校准的仪器会给出自信的错数**。
//       修正版先喂**解析式已知角度**的合成 V 形做校准（60/90/110/120/130/150，
//       误差 ≤1.4°），通过后才测真实字形。**不校准就不报数。**
//
//   ★ 字形可用性是**实测**的，不是按名字猜的（同一次取证）：`⌄`(U+2304) / `⯆`(U+2BC6) /
//     `〈`(U+2329) 等在当前字体**缺字**；缺字会被 Windows **静默字体链接**到别的 face
//     ⇒ 字重/基线错位，**比原样更难看**。⇒ 选字形必须实测存在性。
std::string ReasoningTitle(const Block& block, bool expanded) {
    // ★ 字节序列**由 Python 反查得到**（`chr(cp).encode('utf-8')`），**不靠心算**：
    //   U+FE40 ﹀ = EF B9 80 · U+FE3F ︿ = EF B8 BF
    //   （我第一版凭记忆写成 EF B8 80 / EF B9 80 —— **错了**，那是 U+FE00/U+FE40 的错配。
    //     教训：多字节字面量一律程序化生成/核对，别手写——同条 64「字面量不得凭阅读记忆重打」。）
    std::string out = expanded ? "\xEF\xB8\xBF "      // ︿ U+FE3F（展开）
                               : "\xEF\xB9\x80 ";     // ﹀ U+FE40（收起）
    out += block.ended ? "\xE5\xB7\xB2\xE5\xAE\x8C\xE6\x88\x90"      // 已完成
                       : "\xE6\x80\x9D\xE8\x80\x83\xE4\xB8\xAD";     // 思考中
    if (block.tokens > 0) {
        out += " \xC2\xB7 " + std::to_string(block.tokens) + " tokens";
    }
    return out;
}

// 侧栏/状态栏的一行小标签；返回裸指针（树拥有），供 HUD 动态 SetText
ECDI::Label* AddLine(ECDI::Panel& parent, const std::string& text,
                     const ECDI::Color& color, bool heading = false, int width = 170,
                     std::optional<float> fontSize = std::nullopt) {
    auto label = std::make_unique<ECDI::Label>(text);
    label->SetTextColor(color);
    // ★ 2026-10-18 v7：族名走主题（浅色 = 雅黑，深色 = 空/SimSun 基线）。
    //   原先只在传了 fontSize 时才 SetFont ⇒ 不传的那些落到空 family = SimSun，
    //   与显式指定的控件**同一界面两种中文字形**。
    //   默认值取 `ECDI::Font::size` 的出厂值 14.0f（原先不调 SetFont 时就是这个数）。
    label->SetFont(harness::SansFont(fontSize.value_or(14.0f)));
    // ★ 行高随字号走（2026-10-16 用户反馈「小节标题字太小」）：
    //   标题 11f→13f 后，13px 的行盒在原来的 15px 里会被**裁切**
    //   （`Font::size` 是 DIP 高度，`Font.h:17`）。标题行高 15→18 留出行距。
    //   正文行高 17 不变（其字号未动）。
    label->SetSize(width, heading ? 18 : 17);
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

    const std::string windowTitle = "ECDI harness - " + m_options.fixtureName;
    ECDI::Window& win = application.Create(windowTitle, 1120, 760, std::move(services));
    // 使用 ECDI CaptionBar（参照 examples/ModelProbe）：逻辑命中区和实体栏高保持一致，
    // 必须在 Show()/Application::Run() 之前配置窗口 chrome。
    win.SetChromeMode(ECDI::ChromeMode::Borderless);
    win.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);

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
    // ★ 2026-10-18 v7：主题必须在 **BuildUi 之前** 定下来——令牌是引用别名，
    //   UI 建好时颜色值已被拷进各控件的 PanelStyle/ButtonStyle，之后再切不会重绘。
    harness::SwitchTheme(m_options.lightTheme);
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

    // CaptionBar 是 root 的直接子项：保持贴边全宽，空白区可拖动，按钮由 harness 自绘
    // （ECDI 原生 CaptionBar 把颜色写死，跟不了主题 —— 见 ThemedCaptionBar.h 头注）。
    auto caption = std::make_unique<harness::ThemedCaptionBar>(
        window, "ECDI harness - " + m_options.fixtureName);
    caption->SetSize(1120, ECDI::CaptionBar::kDefaultHeight);
    root.AddChild(std::move(caption));

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
        // ★ 2026-10-18 原型 C：品牌字 15px/700。**"harness" 是纯拉丁** ⇒ 这是
        //   `SemiFont`（`Segoe UI Semibold`）唯一合法的用处——中文串绝不能用
        //   （GDI 回落宋体 / GL 解析 msyhbd.ttc ⇒ 两链不同 face，见 Theme.h）。
        title->SetFont(harness::SemiFont(harness::kFontBrand));
    }
    title->SetSize(72, 20);
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
    // ★ 用 `uikit::Centered`（`CenteredLabel`）而非裸 `Label`（2026-10-16 用户反馈
    //   「s-cot-12 / replay 这几个在胶囊内的字位置不对，贴左了，应该居中」）。
    //   根因：`ECDI::Label` **不** override `CalculateTextPosition` ⇒ 基类默认**左对齐**
    //   （`Label.h` 只声明 `OnPaint`；全库只有 `Button` 做水平对齐）。
    //   胶囊是定宽容器 ⇒ 文字必须居中才不显偏。
    auto chipLabel = [](const std::string& text, const ECDI::Color& color, int width) {
        return uikit::Centered(text, width, 20, color, 11.0f, /*mono=*/true);
    };

    auto sessChipLabel = chipLabel("s-____", kAccentInk, 84);
    m_hud.sessChip = sessChipLabel.get();
    bar->AddChild(makeChip(std::move(sessChipLabel), 84, true));
    bar->AddChild(makeChip(chipLabel("replay", kDarkMuted, 64), 64, false));

    auto grow = std::make_unique<ECDI::Panel>();
    grow->SetStretch(1);
    grow->SetSize(10, 10);
    bar->AddChild(std::move(grow));

    // ★ 同为胶囊 ⇒ 同样居中（见上方 `chipLabel` 注释）
    auto rowLabel = uikit::Centered("autosize", 80, 20, kDarkMuted, 11.0f, /*mono=*/false);
    m_hud.rowChip = rowLabel.get();
    bar->AddChild(makeChip(std::move(rowLabel), 80, false));

    bar->AddChild(uikit::DotCell(8, kOkDark, 20, &m_hud.statusDot));

    auto state = std::make_unique<ECDI::Label>("idle");
    m_hud.stateChip = state.get();
    state->SetTextColor(kDarkMuted);
    {
        state->SetFont(harness::SansFont(12.0f));
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
    side->SetLayout(std::make_unique<ECDI::VerticalLayout>(harness::kRowGapSide, false, harness::kPadFrame));

    auto newSess = std::make_unique<ECDI::Button>("＋ 新建会话");
    newSess->SetTextColor(kAccentInk);
    newSess->SetStyle(ECDI::ButtonStyleOverride{
        .background = kUserBg,            // --accent-soft 与 user 气泡同值 #263449
        .border = harness::kDarkAccent,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn});
    newSess->SetSize(172, 32);
    newSess->SetFont(harness::SansFont(harness::kFontBody));
    newSess->SetOnClick([this] {
        AddNoticeRow("[新建会话 仅记录]", kDarkMuted);
        Relayout();
        RequestRepaint();
    });
    side->AddChild(std::move(newSess));

    // ★ 2026-10-18 原型 C 形态：分区标签 10px/700 letter-spacing ⇒ 落地为
    //   **11px + muted + 不加空格**（中文不进 10px：§3.3 实测会糊；`"会 话"`
    //   那种手撒空格是伪装 letter-spacing，等宽观感下反而散）。
    //   ★ 分区名 `工作区`/`项目` 直接沿用 C 的措辞（C 左侧第一段就叫「工作区」）。
    AddLine(*side, "工作区", kDarkMuted, true, 172, harness::kFontMicro);
    {
        // 会话行 = navitem.selected 的等价物（C 的 `.navitem.selected`）
        auto row = uikit::Row(172, 28, 8, 8);
        row->SetStyle(ECDI::PanelStyleOverride{
            .background = kPanel2, .cornerRadius = 8.0f});
        row->AddChild(uikit::Dot(7, harness::kDarkAccent));
        auto name = uikit::Label("—", 60, 16, kDarkInk, harness::kFontSmall);
        m_hud.sessSession = name.get();
        row->AddChild(std::move(name));
        // meta 显示 core 版本（C 的 session 行右侧是相对时间，但本 GUI 的
        // 「会话」只有一条：回放夹具 ⇒ 显示 core 版本比显示假时间更诚实）
        auto meta = uikit::Label("—", 72, 16, kDarkMuted, harness::kFontSmall);
        m_hud.sessCore = meta.get();
        row->AddChild(std::move(meta));
        side->AddChild(std::move(row));
    }

    // ★★ 回放进度条（I4 评估钩子）**保留**，但改成 C 那种「细线段」形态：
    //   原型 C 没有这一块，而它是协议 v2 的评估读数 ⇒ 折中方案是**不单独占一个
    //   分区**，改成会话条目下方的一条 3px 细轨 + 右侧计数，视觉上从「一个区块」
    //   退化为「一行元信息」，既保住读数又不破坏 C 的节奏。
    {
        auto row = uikit::Row(172, 16, 8);
        auto prog = std::make_unique<ECDI::Label>("0 / 0");
        m_hud.sessProgress = prog.get();
        prog->SetTextColor(kDarkMuted);
        prog->SetFont(harness::SansFont(harness::kFontSmall));
        prog->SetSize(172, 16);
        row->AddChild(std::move(prog));
        side->AddChild(std::move(row));
        auto bar = std::make_unique<ECDI::ProgressBar>();
        m_replayBar = bar.get();
        bar->SetSize(172, 3);
        bar->SetStyle(ECDI::ProgressBarStyleOverride{
            .trackColor = kTrackBg, .fillColor = harness::kDarkAccent,
            .cornerRadius = 1.5f});
        side->AddChild(std::move(bar));
    }

    // ── 项目 / 工作区文件（C 的「项目」段）──
    AddLine(*side, "项目", kDarkMuted, true, 172, harness::kFontMicro);
    {
        // 夹具路径链 → 伪树（dir `▽` / 文件 `·`；真实数据 = replayPath 的父目录链）
        // ★ 2026-10-18：`▾`(U+25BE) / `▸`(U+25B8) 在两个主题的 face 里**都没有字形**
        //   （`.scratch/symcand.py` 两轮实测：GDI 靠字体链接偷画、GL 直接空白）
        //   ⇒ 换成 `▽`(U+25BD) / `›`(U+203A)，两主题都真有。
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
            auto lbl = std::make_unique<ECDI::Label>("▽ " + *it + "/");
            lbl->SetTextColor(kDarkInk);
            ECDI::Font f = harness::MonoFont(12.0f);
            lbl->SetFont(f);
            lbl->SetSize(172 - indent * 12, 16);
            side->AddChild(std::move(lbl));
            ++indent;
        }
        auto fileLbl = std::make_unique<ECDI::Label>("· " + file);
        fileLbl->SetTextColor(kDarkMuted);
        ECDI::Font ff = harness::MonoFont(12.0f);
        fileLbl->SetFont(ff);
        fileLbl->SetSize(172 - indent * 12, 16);
        side->AddChild(std::move(fileLbl));
    }

    // ── 侧栏底部身份行（C 的 `.side-bottom`）──
    //   `margin-top:auto` 在 ECDI 里没有等价物（布局无弹性余量分配）⇒ 用
    //   一根 1px 分隔线 + 紧跟的身份行近似「贴在底部的一行」。
    {
        auto sep = std::make_unique<ECDI::Panel>();
        sep->SetSize(172, 1);
        sep->SetStyle(ECDI::PanelStyleOverride{
            .background = kLine, .borderWidth = 0.0f});
        side->AddChild(std::move(sep));
        auto row = uikit::Row(172, 24, 8, 8);
        row->AddChild(uikit::Dot(14, kPanel2));
        auto who = uikit::Label("本地工作区", 100, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(who));
        auto dots = uikit::Label("· · ·", 40, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(dots));
        side->AddChild(std::move(row));
    }

    main->AddChild(std::move(side));

    // ── 中栏（转录 + Composer）──
    auto counting = std::make_unique<CountingPanel>();
    m_counting = counting.get();
    m_counting->SetStretch(1);
    m_counting->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true, 0));
    m_counting->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? kProbeBg : kContentBg});

    // ── 中栏会话头（原型 C 的 `.conversation-head`，62px）──────────
    // ★ 为什么加：原型 C 中栏顶部有「标题 + 元信息 + 操作」这一带，而现状
    //   ScrollView 直接从第一行开始，**标题只存在于系统标题栏**（用户看不到
    //   自己在聊什么）。C 的 `.head-actions` 是 3 个 iconbtn，但 `✳ ⚙ ◉ ⋯ ⌁ ☷ ✓ ⌂ ✕ ↻ ▦ ⌕`
    //   这一整批图标在两个主题的 face 里**都没有字形**（`.scratch/symcand.py` 实测）
    //   ⇒ 落地为**文字按钮**（诚实降级：不画没有字形的图标）。
    {
        auto head = std::make_unique<ECDI::Panel>();
        head->SetSize(600, 62);
        head->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kContentBg,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kLine});
        head->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, true, harness::kPadFrame));

        // 标题（拉丁 = 夹具名，可用 Semibold 出口）+ 元信息
        auto titles = std::make_unique<ECDI::Panel>();
        titles->SetSize(300, 36);
        titles->SetLayout(std::make_unique<ECDI::VerticalLayout>(2, false, 0));
        {
            auto h1 = std::make_unique<ECDI::Label>(ConversationTitle());
            m_hud.convTitle = h1.get();
            h1->SetTextColor(m_options.probeContrast ? kProbeInk : kDarkInk);
            // ★ 加重走 `SemiFont`（`Segoe UI Semibold`）——这是**拉丁专用**出口：
            //   `Microsoft YaHei Bold` 在 GDI 回落宋体、GL 解析到 msyhbd.ttc，
            //   两链不同 face ⇒ 中文串绝不能用。标题目前是「harness 会话」混排串，
            //   **故意不用 SemiFont**（见 Theme.h 里那条硬约束的注释）。
            h1->SetFont(harness::SansFont(harness::kFontTitle));
            h1->SetSize(300, 18);
            titles->AddChild(std::move(h1));
            auto sub = std::make_unique<ECDI::Label>(ConversationSubtitle());
            m_hud.convSub = sub.get();
            sub->SetTextColor(m_options.probeContrast ? kProbeInk : kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontMicro));
            sub->SetSize(300, 14);
            titles->AddChild(std::move(sub));
        }
        head->AddChild(std::move(titles));

        auto grow = std::make_unique<ECDI::Panel>();
        grow->SetStretch(1);
        grow->SetSize(10, 10);
        head->AddChild(std::move(grow));

        // 操作钮：C 是 3 个 iconbtn（↗ ☷ ···）——`↗` 两主题都有字形，
        // `☷` 与 `⋯`(U+22EF) 都没有 ⇒ 后两个用 ASCII 文字（诚实降级，不画缺字图标）。
        auto mkAct = [&head](const std::string& text, const std::string& tip) {
            auto b = std::make_unique<ECDI::Button>(text);
            b->SetTextColor(kDarkMuted);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            // ★ 指示符顺序必须照抄 `ButtonStyleOverride` 的成员声明顺序
            //   （`ECDI/Theme/ButtonStyle.h:44-51`：background / border / borderWidth /
            //   cornerRadius / pressedBackground / hoverBackground / textAlignment /
            //   foreground）。MSVC 对乱序 designated init 报 C7560，且 GCC/Clang 的
            //   C++20 扩展也不保证顺序 ⇒ 一律按声明序写。
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(28, 24);
            b->SetOnClick([tip] {
                // ★ 这三个钮在原型 C 里也是装饰（inspector 显隐 + 分享 + 更多），
                //   而右栏承载评估读数、**不能藏**（README 第一目的）⇒ 点了只提示，
                //   不改变布局。tip 仅作提示文案用。
                (void)tip;
            });
            head->AddChild(std::move(b));
        };
        mkAct("↗", "分享");
        mkAct("≡", "检查器");
        mkAct("···", "更多");
        counting->AddChild(std::move(head));
    }

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
    composer->SetSize(600, 92);
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
    insp->SetLayout(std::make_unique<ECDI::VerticalLayout>(14, false, harness::kPadFrame));

    auto makeCard = [](int h) {
        auto card = std::make_unique<ECDI::Panel>();
        card->SetSize(200, h);
        card->SetStyle(ECDI::PanelStyleOverride{
            .background = kContentBg, .cornerRadius = kRadiusCard2,
            .borderWidth = 1.0f, .borderColor = kCardBorder});
        return card;
    };
    auto cardTitle = [](ECDI::Panel& card, const char* title) {
        card.AddChild(uikit::Label(title, 178, 14, kDarkMuted, harness::kFontMicro));
    };
    auto kvRow = [](ECDI::Panel& parent, const std::string& k,
                    ECDI::Label** vOut, int kw = 56) {
        auto row = uikit::Row(178, 16, 10);
        row->AddChild(uikit::Label(k, kw, 16, kDarkMuted));
        auto vl = uikit::Label("—", 110, 16, kDarkInk, 10.5f, /*mono=*/true);
        *vOut = vl.get();
        row->AddChild(std::move(vl));
        parent.AddChild(std::move(row));
    };

    // 卡片白底和内置标题对齐原型 C 的信息层级；统计项仍保留 harness 专用读数。
    {
        auto card = makeCard(108);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "渲染探针");
        kvRow(*card, "重绘", &m_hud.evalPaints);
        kvRow(*card, "命令/帧", &m_hud.probeCmds);
        kvRow(*card, "执行", &m_hud.probeExec);
        insp->AddChild(std::move(card));
    }

    {
        // 标题收进白卡，与原型 C 的卡片层级一致。上下文环仍保留为评估读数。
        //   算式：48（环行）+ 8（间距）+ 16（输入）+ 8 + 16（输出）+ 2×10（卡片内边距）= 124。
        auto card = makeCard(140);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 10));
        cardTitle(*card, "用量");
        // ★★ 协议 v2（2026-10-14）上下文占用百分比——**用户要求**。
        //   ★ 为什么必须有 `hello.context`：百分比**需要分母**，而 v1 协议里
        //     **没有任何容量字段** ⇒ 此前两根条只能拿 `max(input, output)` 当分母，
        //     那是**拿输入跟输出比**（两个不同量纲的东西相除）⇒ 读数无意义。
        //     现分母 = `hello.context`，缺字段 ⇒ 显示 `—`（**不猜**）。
        //   ★★ 2026-10-16：**环形兑现**。此前这里是「百分比条」，理由是 ECDI 后端
        //     只有 6 个绘制原语、**没有圆弧**（当时实测为真，并据此登记账本 `REQ-08`）。
        //     ECDI **Phase 33** 落地了框架首个曲线原语 `DrawArc`（纯虚，见
        //     `RenderingBackend.h:92-112` 的完整契约）与其首个消费者 `RingProgressBar`
        //     ⇒ 本条**挂账前提（"无消费者不动"）改变**，本项目作为首个真实消费者兑现它。
        //   ★ 轨道色**不用 `kTrackBg`**：它与卡片底色 `kPanel2` **同值**（`Theme.h:82`），
        //     5px 细条上无所谓（只看得见已完成段），但**环会因此看不见整圆**、
        //     只剩一段悬空的弧 ⇒ 无法判断"还剩多少"。故轨道改用 `kLine`（亮一档）。
        //   ★ 环内**不叠百分比文字**（用户裁决）：`RingProgressBar` 是纯视觉控件
        //     （`RingProgressBarStyle.h:10` 明写无 TextStyle），叠字要靠应用侧手算居中，
        //     而 ECDI 布局无居中能力 ⇒ 数值仍由右侧 `ctxPct` 标签承担。
        auto urow = [&card](const std::string& k, ECDI::Label** vOut,
                            ECDI::ProgressBar** barOut, const ECDI::Color& fill) {
            auto row = uikit::Row(178, 16, 8);
            row->AddChild(uikit::Label(k, 40, 16, kDarkMuted));
            auto bar = std::make_unique<ECDI::ProgressBar>();
            *barOut = bar.get();
            bar->SetSize(70, 5);
            bar->SetStyle(ECDI::ProgressBarStyleOverride{
                .trackColor = kTrackBg, .fillColor = fill,
                .cornerRadius = 2.0f});
            row->AddChild(std::move(bar));
            auto vl = uikit::Label("—", 52, 16, kDarkInk, 11.0f, /*mono=*/true);
            *vOut = vl.get();
            row->AddChild(std::move(vl));
            card->AddChild(std::move(row));
        };
        // 上下文行置顶：它是**决策相关**读数（"还能聊多久"），输入/输出只是明细。
        // ★ 本行**不走 `urow`**：环要 44px 见方，而 `HorizontalLayout` 的
        //   `fillCrossAxis=false` 是**顶对齐**（`HorizontalLayout.h:15`；`UiKit.h:74`
        //   明写「ECDI 布局无居中」）⇒ 16px 的文字会贴到环顶。故左右各套一个
        //   **48px cell** 手动垂直居中（沿用 `UiKit.h:76-89` `DotCell` 的既有手法）。
        {
            auto row = uikit::Row(178, 48, 8);
            auto cell = [](std::unique_ptr<ECDI::Widget> inner) {
                auto c = std::make_unique<ECDI::Panel>();   // 无 layout ⇒ 保留子控件 SetPosition
                c->SetSize(inner->GetWidth(), 48);
                inner->SetPosition(0, (48 - inner->GetHeight()) / 2);
                c->AddChild(std::move(inner));
                return c;
            };
            row->AddChild(cell(uikit::Label("上下文", 40, 16, kDarkMuted)));

            auto ring = std::make_unique<ECDI::RingProgressBar>();
            m_ctxRing = ring.get();
            ring->SetSize(44, 44);            // min(w,h) 作直径（RingProgressBar.h:16）
            ring->SetStyle(ECDI::RingProgressBarStyleOverride{
                .trackColor = kRingTrack,     // ★ 独立令牌：kLine 浅色下压白卡只有 1.09:1，整圈会消失
                .fillColor = harness::kOkDark,
                .thickness = 5.0f});
            row->AddChild(cell(std::move(ring)));

            auto vl = uikit::Label("—", 52, 16, kDarkInk, 11.0f, /*mono=*/true);
            m_hud.ctxPct = vl.get();
            row->AddChild(cell(std::move(vl)));
            card->AddChild(std::move(row));
        }
        urow("输入", &m_hud.usageInV, &m_usageBarIn, harness::kDarkAccent);
        urow("输出", &m_hud.usageOutV, &m_usageBarOut, harness::kDarkAccent);
        insp->AddChild(std::move(card));
    }

    {
        auto card = makeCard(100);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "协议健康");
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

    {
        auto card = makeCard(92);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 10));
        cardTitle(*card, "权限历史");
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
        ECDI::Font f = harness::MonoFont(11.0f);
        lbl->SetFont(f);
        lbl->SetSize(width, 16);
        return lbl;
    };
    m_hud.stMsgs = AddLine(*status, "0 / 0 msg", kDarkMuted, false, 120, harness::kFontMicro);
    m_hud.stTokens = AddLine(*status, "tokens —", kDarkMuted, false, 150, harness::kFontMicro);
    m_hud.stPaint = AddLine(*status, "paints —", kDarkMuted, false, 210, harness::kFontMicro);
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
            // ★★ 思维链折叠探针必须**在这里**推进：夹具拉完后本函数会从上面 `return`，
            //   走不到末尾那段。第一版把调用只放在末尾 ⇒ 状态机永远停在 step 0
            //   （只有 settle 那一 tick 采到 default，之后再不前进）。
            if (m_options.reasoningProbe && !m_reasoningProbeDone) {
                RunReasoningProbe();
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
    // ★★ --hit-probe（REQ-04 边界）：夹具拉完 + `Relayout()` 之后**跑一次**。
    //   位置理由：此刻几何已定（按钮位置/尺寸是探针坐标的来源），
    //   且只跑一次（否则 30Hz 每 tick 都合成点击，读数被自身重复污染）。
    if (m_options.hitProbe && m_replaySettled && !m_hitProbeDone) {
        m_hitProbeDone = true;
        RunHitProbe();
    }
    // ★ 思维链折叠行为取证（协议 v2 §7）：跨 tick 状态机（每次只推进一步，
    //   步间等过 200ms 折叠动画）⇒ 不能像 --hit-probe 那样一次跑完。
    if (m_options.reasoningProbe && m_replaySettled && !m_reasoningProbeDone) {
        RunReasoningProbe();
    }
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
    // ★ 协议 §9：版本不兼容 ⇒ **停止消费**（"拒绝连接"的实质）。
    //   只在 `HandleLine` 返回后检查：`Exit()` 只让消息循环退出，**不打断本循环**，
    //   而 `--instant` 会在同一 tick 抽干整个夹具 ⇒ 若不在此处 break，
    //   "拒绝"就只是打印一行提示、随后照常把整条流处理完（实测：protocol:3 的
    //   夹具仍读到 blocks=44）——那不是拒绝，是"边抱怨边照做"。
    while (m_source->Next(line)) {
        HandleLine(line);
        if (m_protocolIncompatible) break;
    }
    if (!m_pendingTruncated.empty()) {
        AddNoticeRow(m_pendingTruncated, kErrorDark);   // 协议 §1.2：截断必须显式报错
        m_pendingTruncated.clear();
    }
}

void HarnessApp::DrainTimed() {
    if (m_waitForAnswer) return;   // I3：ask 未答前 core 不再发消息
    if (m_protocolIncompatible) return;   // ★ 协议 §9：版本不兼容 ⇒ 不再消费
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  m_timedAnchor)
            .count();
    std::string line;
    while (m_replay->NextDue(elapsedMs, line)) {
        HandleLine(line);
        if (m_waitForAnswer) break;   // ask 已显示，停在 waiting（协议 §8.3）
        if (m_protocolIncompatible) break;   // ★ 协议 §9：版本不兼容 ⇒ 停止消费
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
        // ★ 协议 v2 §7：思维链块结束时标题改为「**已完成**」（用户裁决）。
        //   ★ **箭头不变**——箭头只由展开态决定，与是否结束无关。
        //     （我一度连箭头一起换成 `▶`，被截图证明违背用户明确给出的规格，已撤回。）
        //   ★ token 副标也在此刻出现（`tokens` 是 `block_end` 才有的字段）。
        //   ★ 展开态也要更新文案：用户看到的是「已完成」+ `︿`。
        //     （2026-10-18 订正：此处原文写 `▲`，但思维链标题的箭头单一真相源是
        //      `ReasoningTitle()`，用的正是用户指定的钝角 `﹀`/`︿`，不是 `▲`。）
        if (row->second->reasoning && block) {
            row->second->ended = true;
            if (row->second->headerBtn) {
                row->second->headerBtn->SetText(
                    ReasoningTitle(*block, row->second->expanded));
            }
            RequestRepaint();
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
            // ★ 2026-10-18：`▸`(U+25B8) 在两个主题的 face 里**没有字形**
            //   （`.scratch/symcand.py` 两轮实测：GDI 靠字体链接偷画、GL 直接空白）
            //   ⇒ 换成 `›`(U+203A)，同为右向尖括号，两主题都真有。
            std::string title = "› ";
            if (const Block* call = m_transcript.FindBlock(block->forId)) {
                title += call->name.empty() ? "tool" : call->name;
            } else {
                title += "tool";
            }
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
        } else if (isCall) {
            // 工具调用：标题 = 工具名（+ 可选摘要）——此前 summary 在此被丢弃
            // ★ 2026-10-18：`▸` → `›`（字形可用性实测，见上方 isResult 分支注释）
            std::string title = "› " + (block->name.empty() ? "tool_call" : block->name);
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
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
        // ★★ 协议 §9（v2 落地，2026-10-14）：core 声明的版本**高于**本 GUI 支持的上限
        //   ⇒ **拒绝连接并显示原因，不做降级猜测**。
        //   ★ 为何现在才实现：该规则自 v1 就写在文档里，但**从未被实现**——
        //     在只有 v1 的世界里它恒不触发，属"写在纸上但没人执行"的条款。
        //     v2 出现的这一刻它才真正开始起作用（也正是它存在的理由）。
        //   ★ 缺字段 ⇒ 按 v1 处理（宽容优于拒连：缺字段不是"更高的版本"）。
        if (m_protocolVer > kProtocolMaxSupported) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "[协议不兼容] core 声明 protocol %d，本 GUI 支持到 %d"
                          " —— 按 §9 拒绝连接（不做降级猜测）",
                          m_protocolVer, kProtocolMaxSupported);
            AddNoticeRow(buf, kErrorDark);
            m_protocolIncompatible = true;
            m_hudDirty = true;
            if (m_application) m_application->Exit();   // 不进入渲染路径
            return;
        }
        if (const json::Value* caps = msg.Find("caps"); caps && caps->IsArray()) {
            std::string joined;
            for (const auto& cap : caps->array) {
                if (!joined.empty()) joined += ", ";
                joined += cap.text;
            }
            m_capsStr = joined;
        }
        // ★ 协议 v2 §3.1：上下文容量（百分比的分母）。缺字段 ⇒ 保持 0 ⇒ 显示 `—`。
        //   ★ **不猜容量**：分母未知时百分比无定义，猜一个比不显示更糟（编造读数）。
        if (const json::Value* ctx = msg.Find("context");
            ctx && ctx->IsNumber() && ctx->number > 0) {
            m_contextLimit = static_cast<long long>(ctx->number);
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
        // ★ 正文必须走主题字体工厂：此前不 SetFont ⇒ family 为空 ⇒ 落 SimSun，
        //   而侧栏/标题走雅黑 ⇒ **同一界面两种中文字形**（浅色下尤其割裂）。
        label->SetFont(harness::SansFont(harness::kFontBody));
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
        label->SetFont(harness::SansFont(harness::kFontBody));   // 同 user 分支：统一中文 face
        if (block.kind == "system") label->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
        if (block.kind == "error") label->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
        panel->AddChild(std::move(label));
        AddRow(std::move(panel), rec, block.id);
        return;
    }

    // ★★ 思维链卡（协议 v2 §7，2026-10-14）——**默认收起**，点击标题展开，箭头翻转。
    //   ★ 为何必须有这条**显式分支**：`reasoning` 一旦进入 `IsKnownKind`，
    //     `block.unknownKind` 就是 **false** ⇒ 它会**直接掉到本函数末尾的工具卡分支**，
    //     而那个分支的 else 把标题写死成「等待结果」——**正是 2026-10-13 修掉的那个 bug
    //     换个方式复活**（思维链又一次伪装成"正在等的工具结果"）。加 kind 必须同时加分支。
    //   ★ 复用 `CollapsiblePanel`（与工具卡同机制，不新造控件）：
    //     折叠动画、内容可见性、展开基准全由框架既有能力承担。
    if (block.kind == "reasoning") {
        rec.reasoning = true;
        rec.ended = block.ended;
        rec.blockId = block.id;
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kReasoningBg, kProbeBg),
            .cornerRadius = kRadiusTool,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kReasoningBorder});

        // 标题 = 单一真相源（箭头两态）；默认收起 ⇒ ▼
        auto header = std::make_unique<ECDI::Button>(ReasoningTitle(block, false));
        rec.headerBtn = header.get();
        header->SetTextColor(Pick(m_options.probeContrast, kReasoningInk, kProbeInk));
        {
            ECDI::Font f = harness::SansFont(12.0f);
            header->SetFont(f);
        }
        header->SetStyle(ECDI::ButtonStyleOverride{
            .background = Pick(m_options.probeContrast, kReasoningHeaderBg, kProbeBg),
            .border = kReasoningBorder,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kReasoningHover,
            .textAlignment = ECDI::TextAlignment::Left});
        panel->AddChild(std::move(header));

        auto body = std::make_unique<ECDI::CollapsiblePanel>();
        rec.body = body.get();
        rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        // 展开后是**用户主动要读**的内容 ⇒ 用常规 ink（不再弱化）
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetWordWrap(true);   // 思维链很长，必须按宽断行
        {
            ECDI::Font f = harness::SansFont(12.0f);
            label->SetFont(f);
        }
        rec.body->GetContent()->AddChild(std::move(label));
        panel->AddChild(std::move(body));

        AddRow(std::move(panel), rec, block.id);

        RowRec& stored = m_rows.back();
        // ★ 在**建行时刻**抓流式态标题（`block_start` 刚到、`block_end` 还没来）。
        //   为何必须此刻抓：`--instant` 在同一 tick 抽干夹具 ⇒ 事后回读永远是
        //   「已完成」，`▼ 思考中` 那一态**根本采不到**（首版就栽在这里）。
        if (m_options.reasoningProbe && m_reasoningTitleStreaming.empty()) {
            m_reasoningTitleStreaming = stored.headerBtn->GetText();
        }
        stored.headerBtn->SetOnClick([this, &stored] {
            if (m_options.rowModeList) return;   // 固定行高模式：展开体被后续行覆盖，v1 禁用
            stored.expanded = !stored.expanded;
            if (stored.body) stored.body->Toggle();
            // 箭头随展开态翻转（▼ ⇄ ▲）；文案与 token 副标由**单一真相源**决定，
            // 故回查块（`block_end` 可能已到达 ⇒ 文案应为「已完成」+ tokens）
            if (stored.headerBtn) {
                if (const Block* b = m_transcript.FindBlock(stored.blockId)) {
                    stored.headerBtn->SetText(ReasoningTitle(*b, stored.expanded));
                }
            }
            m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画
            Relayout();
            RequestRepaint();
        });
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
        label->SetFont(harness::SansFont(harness::kFontBody));   // 统一中文 face
        panel->AddChild(std::move(label));
        AddRow(std::move(panel), rec, block.id);
        return;
    }

    // tool_call / tool_result：可折叠卡片（默认折叠；标题按钮 + CollapsiblePanel 展开体）
    rec.tool = true;
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kContentBg, kProbeBg),
        .cornerRadius = kRadiusTool,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .borderColor = kCardBorder});

    std::string title;
    std::string bodyText;
    if (block.kind == "tool_call") {
        title = block.name.empty() ? "tool_call" : block.name;
        bodyText = block.argsPretty.empty() ? "{}" : block.argsPretty;
    } else {
        title = "等待结果";
        bodyText = block.text.empty() ? block.summary : block.text;
    }

    // ★ 2026-10-18：`▸`(U+25B8) 两主题 face 都无字形 ⇒ `›`(U+203A)（同右向尖括号语义）
    auto header = std::make_unique<ECDI::Button>("› " + title);
    rec.headerBtn = header.get();
    header->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    {
        ECDI::Font f = harness::SansFont(12.0f);
        header->SetFont(f);
    }
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = Pick(m_options.probeContrast, kContentBg, kProbeBg),
        .border = kCardBorder,
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
        ECDI::Font f = harness::MonoFont(12.0f);
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
        ECDI::Font f = harness::SansFont(13.0f);
        label->SetFont(f);
    }
    panel->AddChild(std::move(label));

    auto allow = std::make_unique<ECDI::Button>("允许");
    rec.allowBtn = allow.get();
    allow->SetTextColor(kWarnInk);
    {
        ECDI::Font f = harness::SansFont(12.0f);
        allow->SetFont(f);
    }
    allow->SetStyle(ECDI::ButtonStyleOverride{
        .background = kWarn,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kWarn,
        .hoverBackground = kAllowHover});
    panel->AddChild(std::move(allow));
    auto deny = std::make_unique<ECDI::Button>("拒绝");
    rec.denyBtn = deny.get();
    deny->SetTextColor(kDenyInk);
    {
        ECDI::Font f = harness::SansFont(12.0f);
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

// ── ★★ REQ-04 **边界**探针（--hit-probe，2026-10-14）────────────────────
// 目的：把「按钮禁用 ⇒ 点击不达」变成**机读证据**，且**不注入系统输入**。
//
// 为什么不能用 `--state-probe` 代替：那一条读的是 `IsEnabled()`（使能**位**）。
// REQ-04 的边界是「禁用后点击**到不到**」——中间隔着 `Widget::HitTest` 的
// `if (!IsEnabled()) return nullptr;`（`Widget.cpp:195`）。使能位对 ⇒ 命中门控
// **仍可能**不生效（两段独立代码）⇒ 必须单独取证，不能从前者推出后者。
//
// 为什么不用 `SendInput` 注入：注入会移动真实光标、抢前台（用户正在看视频时
// 被抢屏一次）。而**合成 `MouseButtonDown/UpEvent` 直接喂给 `Application::OnEvent`**
// 走的是**完全同一条**内部派发链（`OnMouseButtonDown` → `FindTargetWidget` →
// `rootWidget.HitTest` → 冒泡 → `Button::OnMouseButtonUp` → `RaiseClick`），
// 只是**跳过了 Win32 消息翻译层**（`WindowMessageHandler` 的坐标折算）。
// 本探针要证的正是**命中门控**（在 Application/Widget 层），故这层跳过不影响判据；
// 且坐标本就用 DIP、与翻译层输出同口径。
//
// 四组**受控对照**（同一个点、只改 enabled —— 单变量）：
//   ① as-is            落定后的真实状态（**正对照**：enabled 时必须命中且点击达）
//   ② off-point        同状态、点移到按钮**外侧**（**负对照**：证明坐标确实在起作用）
//   ③ forced-enabled   显式 `SetEnabled(true)`
//   ④ forced-disabled  显式 `SetEnabled(false)`（判据：命中者不再是它 ∧ 点击不达）
// ★ 没有 ①② 这两个对照，「④ 点击不达」无法区分「门控生效」与「探针根本没工作」（条 81b）。

std::string HarnessApp::WidgetKindName(const ECDI::Widget* w) {
    if (w == nullptr) return "(null)";
    // 只区分本探针用得到的几类——为的是让读数**自解释**（"命中者是谁"）
    // ★ 顺序敏感：具体类在前（Button 是 TextWidget 子类）。
    if (dynamic_cast<const ECDI::Button*>(w)) return "Button";
    if (dynamic_cast<const ECDI::TextBox*>(w)) return "TextBox";
    if (dynamic_cast<const ECDI::Label*>(w)) return "Label";
    if (dynamic_cast<const ECDI::Panel*>(w)) return "Panel";
    return "Widget";
}

void HarnessApp::SynthesizeClickAt(int x, int y) {
    // ★ 走**真实入口** `Application::OnEvent`——与系统消息到达后的路径完全同源
    //   （`Window::OnEvent` → `Application::OnEvent` → 具名 handler）。
    //   `m_application` 是 Run() 栈上的 HarnessApplication（Application 子类）。
    if (!m_window || !m_application) return;
    ECDI::MouseButtonDownEvent down(m_window, x, y, ECDI::MouseButton::Left);
    m_application->OnEvent(down);
    ECDI::MouseButtonUpEvent up(m_window, x, y, ECDI::MouseButton::Left);
    m_application->OnEvent(up);
}

void HarnessApp::RunHitProbe() {
    if (!m_window || !m_promptBar) return;
    ECDI::Button* send = m_promptBar->sendButton();
    if (!send) return;

    // 坐标一律用 ECDI 的 `GetAbsolutePosition()`（父链累加，含内容偏移）——
    // ★ 不手算父链、不硬编码偏移（手算必漏某一层；同「不硬编码 DPI/边框」纪律）。
    auto centerOf = [](ECDI::Widget* w, int& x, int& y) {
        const ECDI::Point abs = w->GetAbsolutePosition();
        x = static_cast<int>(abs.x) + w->GetWidth() / 2;
        y = static_cast<int>(abs.y) + w->GetHeight() / 2;
    };
    int cx = 0, cy = 0;
    centerOf(send, cx, cy);

    // ② 负对照点：**输入框中心**——同一时刻、同一段代码路径、且**必命中一个活控件**
    //   （比「移到窗口外」强：窗口外命中 nullptr 无法区分「门控生效」与「探针失效」）。
    int nx = cx, ny = cy;
    ECDI::TextBox* input = m_promptBar->input();
    if (input) centerOf(input, nx, ny);

    m_hitProbeActive = true;   // 此后 SendPrompt 只计数、零副作用

    // ★ 保存焦点并在探针结束后**复原**：合成的点击会走 `Application::OnMouseButtonDown`
    //   里的 `SetFocusedWidget(...)`，把焦点从输入框挪到按钮（`Button::CanFocus() == true`）
    //   ⇒ `TextBox::OnFocusLost` 会 `StopTimer(kCaretBlinkTimer)` ⇒ **光标闪烁重绘消失**。
    //   实测后果：探针开启时 paints 由 17 掉到 15（确定复现，5/5 次）。这不是本探针要测的
    //   量，属**仪器污染实验**（条 57③）⇒ 必须复原，否则「探针关闭 = 零行为变化」不成立。
    ECDI::Widget* const focusBefore = m_window->GetFocusedWidget();

    auto scene = [&](const char* name, int x, int y) {
        HitProbeRec rec;
        rec.scene = name;
        rec.px = x;
        rec.py = y;
        rec.sendEnabled = send->IsEnabled();
        ECDI::Widget* hit = m_window->GetRootWidget().HitTest(x, y);
        rec.hitIsNull = (hit == nullptr);
        rec.hitIsSend = (hit == send);
        // ★ 禁用时 `HitTest` **不是**返回 nullptr——按钮被跳过，落点回落到祖先
        //   （RootWidget，其 `ContainsPoint` 覆盖整个客户区）⇒ 必须**如实记录落点**，
        //   否则「命中者 != 发送按钮」这句话在读数里看不出"落到了哪"。
        rec.hitIsRoot = (hit == &m_window->GetRootWidget());
        rec.hitConsumes = (hit != nullptr && hit->ConsumesMouseInput());
        rec.hitClass = WidgetKindName(hit);
        // 点击是否达：先取计数 → 合成 Down+Up → 看 SendPrompt 有没有被进入
        const int before = m_hitProbeSendCalls;
        SynthesizeClickAt(x, y);
        rec.clickReached = (m_hitProbeSendCalls > before);
        m_hitProbeLog.push_back(std::move(rec));
    };

    // ① **正对照**：真实状态 + 按钮中心 ⇒ 应「命中即它 ∧ 点击达」
    //    （这一条同时是「探针本身有效」的证明——没有它，④ 的「不达」不可解读，条 81b）
    scene("as-is", cx, cy);
    // ② **负对照**：同一状态 + 输入框中心 ⇒ 必命中 TextBox、但**不得**达 SendPrompt
    scene("off-point", nx, ny);
    // ③④ **单变量对照**：同一点、只翻 enabled
    send->SetEnabled(true);
    scene("forced-enabled", cx, cy);
    send->SetEnabled(false);
    scene("forced-disabled", cx, cy);
    // ★ 复原：把使能位还给 state 驱动的结果（探针不得留下状态）。
    //   下一次 RefreshHud 会按 state 重新驱动，此处只是不留"半路"状态。
    const bool active = (m_coreState == "running" || m_coreState == "waiting");
    send->SetEnabled(!active);
    if (m_promptBar->stopButton()) m_promptBar->stopButton()->SetEnabled(active);

    // ★ 焦点复原（见上：不清干净会改变 caret 闪烁重绘 ⇒ 污染 paints/frames 读数）
    m_window->SetFocusedWidget(focusBefore);

    m_hitProbeActive = false;
}

// ── ★★ 协议 v2 §7 思维链折叠行为的机读取证（--reasoning-probe）────────────
// 用户口径是**渲染行为**：默认不渲染正文、点标题展开、箭头由朝下变朝上。
// 截图只能证明"某一刻像素长这样"，**不能**证明"点一下真的翻了、再点一下真的回去了"
// ——后者才是"可交互"的定义。故沿用 `--hit-probe` 的同一套纪律：
//   · 合成点击走**真实派发链**（不注入系统输入、不动光标）；
//   · 观测 = 标题文本（箭头+两态文本）、`IsExpanded()`、内容容器 `IsVisible()`、行高。
//
// ★★ 为什么必须是**跨 tick 状态机**（第一版在一个 tick 里做完，读数错了两处）：
//   ① 折叠动画 200ms，而 `SetContentVisible(false)` 在动画的 **onFinished** 里
//      （`CollapsiblePanel.cpp:110-115`）⇒ 点完**立刻**读会读到"内容仍可见"，
//      把正确实现误判成"没折叠"。这是**动画中间态**，不是终态。
//   ② 标题两态（`思考中`/`思考过程`）取决于 `block_end` 是否到达；`--instant` 在同一
//      tick 抽干整个夹具 ⇒ 事后再读**永远**是 `思考过程`，`思考中` 那一态**根本采不到**。
//   ⇒ 分步推进：每次 tick 只做一步，步间等待 > 动画时长；两态标题在**真实事件时刻**抓。
void HarnessApp::RunReasoningProbe() {
    if (!m_window) return;

    RowRec* target = nullptr;
    for (RowRec& r : m_rows) {
        if (r.reasoning && r.headerBtn && r.body) {
            target = &r;
            break;
        }
    }
    if (!target) { m_reasoningProbeDone = true; return; }

    // 步进节流：给折叠动画（200ms）留足时间，确保读到的是**终态**
    const auto now = std::chrono::steady_clock::now();
    if (m_reasoningProbeStep > 0 && now < m_reasoningProbeNextAt) return;
    const auto settle = std::chrono::milliseconds(400);   // > 200ms 动画

    auto snapshot = [&](const char* phase) {
        ReasoningProbeRec rec;
        rec.phase = phase;
        rec.headerText = target->headerBtn->GetText();
        rec.expanded = target->body->IsExpanded();
        rec.contentVisible = target->body->GetContent()->IsVisible();
        rec.panelH = target->panel->GetHeight();
        m_reasoningProbeLog.push_back(std::move(rec));
    };

    switch (m_reasoningProbeStep) {
        case 0:
            // ① 默认态：必须**收起**且正文**不可见**（用户口径「默认应当不渲染」）
            snapshot("default");
            m_reasoningProbeStep = 1;
            m_reasoningProbeNextAt = now + settle;
            return;
        case 1: {
            // ② 点标题中心一次 —— 走真实 HitTest + 派发链（与 --hit-probe 同源）
            const ECDI::Point abs = target->headerBtn->GetAbsolutePosition();
            const int cx = static_cast<int>(abs.x) + target->headerBtn->GetWidth() / 2;
            const int cy = static_cast<int>(abs.y) + target->headerBtn->GetHeight() / 2;
            SynthesizeClickAt(cx, cy);
            m_reasoningProbeStep = 2;
            m_reasoningProbeNextAt = now + settle;
            return;
        }
        case 2:
            // ③ 展开终态：必须 expanded ∧ 内容可见 ∧ 箭头为 ▲
            Relayout();   // 动画结束后补一次布局，让行高落到终态
            snapshot("after-click-1");
            m_reasoningProbeStep = 3;
            m_reasoningProbeNextAt = now + settle;
            return;
        case 3: {
            // ④ 再点一次：必须**回到收起**（箭头必须翻回去，不能单向）
            const ECDI::Point abs = target->headerBtn->GetAbsolutePosition();
            const int cx = static_cast<int>(abs.x) + target->headerBtn->GetWidth() / 2;
            const int cy = static_cast<int>(abs.y) + target->headerBtn->GetHeight() / 2;
            SynthesizeClickAt(cx, cy);
            m_reasoningProbeStep = 4;
            m_reasoningProbeNextAt = now + settle;
            return;
        }
        case 4:
            Relayout();
            snapshot("after-click-2");
            m_reasoningProbeStep = 5;
            m_reasoningProbeDone = true;   // 全部采完 ⇒ 可以退出
            if (m_application) m_application->Exit();
            return;
        default:
            m_reasoningProbeDone = true;
            return;
    }
}

// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
void HarnessApp::SendPrompt(const std::string& text) {
    // ★ --hit-probe：探针合成点击时**只计数、零副作用**——
    //   否则每跑一次探针就会往转录里塞一行 "[prompt 仅记录] …"，污染摘要读数。
    if (m_hitProbeActive) {
        ++m_hitProbeSendCalls;
        return;
    }
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
    label->SetFont(harness::SansFont(harness::kFontSmall));   // 统一中文 face（原先落 SimSun）
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
    // ★ 行可用宽 = 内容宽 − 左右画布留白；行本身从 kCanvasPadX 起排。
    const int innerW = std::max(120, contentW - 2 * kCanvasPadX);
    int y = 0;
    for (RowRec& r : m_rows) {
        int rowH = 0;
        // ★ 协议 v2：思维链卡与工具卡**同一套几何**（通栏 + 折叠体），故共用本分支
        if (r.tool || r.reasoning) {
            const int bodyW = innerW - 2 * kRowPadX;
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
                r.headerBtn->SetSize(innerW - 2 * kRowPadX, kHeaderH);
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
            const int maxBubbleW = std::max(120, innerW * 78 / 100);
            const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
            if (r.label) {
                if (r.ask) {
                    r.label->AutoSize();   // 权限问句保持单行
                    if (r.allowBtn) {
                        r.allowBtn->SetSize(70, 28);
                        r.allowBtn->SetPosition(innerW - 160, 5);
                    }
                    if (r.denyBtn) {
                        r.denyBtn->SetSize(70, 28);
                        r.denyBtn->SetPosition(innerW - 82, 5);
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
                r.label->SetPosition(kRowPadX, 8);
            }
            rowH = std::max(r.label ? r.label->GetHeight() + 16 : 0, r.ask ? 38 : 0);
        }
        if (r.tool || r.reasoning || r.ask) {
            // 工具卡 / 思维链卡 / 权限门：通栏（hi-fi align-self: stretch）
            r.panel->SetSize(innerW, rowH);
            r.panel->SetPosition(kCanvasPadX, y);
        } else {
            // 聊天气泡（hi-fi .msg max-width:78%）：user 右对齐、assistant 左对齐
            const int maxBubbleW = std::max(120, innerW * 78 / 100);
            int bubbleW = innerW;
            if (r.label) {
                bubbleW = r.wrapped
                              ? maxBubbleW
                              : std::min(maxBubbleW,
                                         r.label->GetWidth() + 2 * kRowPadX + 2);
            }
            r.panel->SetSize(bubbleW, rowH);
            r.panel->SetPosition(r.alignRight ? kCanvasPadX + innerW - bubbleW
                                              : kCanvasPadX,
                                 y);
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
        // ★ 2026-10-18：`▾`(U+25BE) 两主题 face 都无字形 ⇒ `▽`(U+25BD) 空心下三角
        m_hud.backendChip->SetText(m_backendName + " ▽");
    }
    // 中栏会话头（2026-10-18，原型 C）：副标随会话 id / core 版本到达而更新。
    // ★ 2026-10-18 订正：此前只拼 `协议 v2 · 回放模式 · <sessionId>`，漏掉
    //   `hello.core_version` 与 `protocol`（截图上副标是
    //   `core 0.1.0 · session s-0001 · protocol 1` —— 与规格 §9 的
    //   `version/protocol` 呈现位不符，看着像没接上 hello）。
    //   ★ 只更新**会变的部分**（模式与工作区是启动即定的），标题保持建头时的值。
    if (m_hud.convSub) {
        std::string sub = "core " + (m_coreVer.empty() ? std::string("—") : m_coreVer);
        if (!m_sessionId.empty()) sub += " · " + m_sessionId;
        sub += " · protocol " + std::to_string(m_protocolVer);
        m_hud.convSub->SetText(sub);
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
            // ★ 2026-10-18：`⚠`(U+26A0) 两主题 face 都无字形 ⇒ `△`(U+25B3) 空心三角
            //   （本身就是警示语义，且 `▲` 同族已在用），配琥珀色。
            m_hud.healthKind->SetText("未知 kind · " + std::to_string(uk) + " △");
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
    // ★★ 上下文占用百分比（协议 v2 §3.1，2026-10-14）——**用户要求**。
    //   ★ 分母 = `hello.context`（**唯一来源**）；**不再**用 `max(input, output)`。
    //     旧写法是**拿输入跟输出比**（两个不同量纲的东西相除）⇒ 读数无意义，
    //     现已被"真正的分母"取代。这是本行**修掉的一个既有缺陷**，不只是新增。
    //   ★ 分子 = `usage.input`（= 本轮送进模型的上下文规模）。**不含 output**：
    //     output 是本轮新生成的，尚未回到下一轮的上下文里。
    //   ★ 三者任一未知 ⇒ 显示 `—` 且条归零（**不猜**；分母未知时百分比无定义）。
    const bool ctxKnown = (m_contextLimit > 0 && m_usageIn >= 0);
    if (m_hud.ctxPct) {
        if (!ctxKnown) {
            m_hud.ctxPct->SetText("—");
            m_hud.ctxPct->SetTextColor(kDarkMuted);
        } else {
            // 四舍五入到整数百分比；占用可能 >100%（core 低估容量）⇒ 如实显示，不钳
            const long long pct =
                (m_usageIn * 100 + m_contextLimit / 2) / m_contextLimit;
            m_hud.ctxPct->SetText(std::to_string(pct) + "%");
            // 配色：<70% 常规 · 70–90% 琥珀（该收尾了）· >90% 红（快溢出）
            m_hud.ctxPct->SetTextColor(pct >= 90 ? kErrorDark
                                                 : (pct >= 70 ? kWarn : kOkDark));
        }
    }
    if (m_ctxRing) {
        const float frac = ctxKnown
                               ? static_cast<float>(m_usageIn) /
                                     static_cast<float>(m_contextLimit)
                               : 0.0f;
        // 环本身钳到 [0,1]（SetProgress 契约）；文本仍如实显示 >100%
        m_ctxRing->SetProgress(frac > 1.0f ? 1.0f : frac);
        // ★ 环色与右侧数字**同源**（同一组阈值），避免「环是绿的、字是红的」自相矛盾。
        //   阈值沿用既有口径：<70 常规绿 · 70–90 琥珀 · >90 红；分母未知 ⇒ 回退绿（环归零）。
        const long long pctForColor =
            ctxKnown ? (m_usageIn * 100 + m_contextLimit / 2) / m_contextLimit : 0;
        const int level = pctForColor >= 90 ? 2 : (pctForColor >= 70 ? 1 : 0);
        // ★★ 只在**档位翻转**时 SetStyle + 补重绘——不能每 tick 都做：
        //   `RingProgressBar::SetStyle` 的头**未**声明「覆盖后 Invalidate」
        //   （对比 `Panel.h:39` 明确写了），实现又在 lib 里 ⇒ 不能假定它内建重绘；
        //   但每 tick 补一次 `RequestRepaint()` 会把 `invalidations`/`paints`
        //   凭空刷高（cot instant 20s ≈ 600 ticks ⇒ 16 变 ~616），而这两个数正是
        //   本项目的**评估读数**（与 p32 基线逐位对比）⇒ 那是「自己的仪器污染实验」
        //   （条 57③）。档位翻转全场至多 2 次 ⇒ 污染可忽略且可解释。
        if (level != m_ctxRingLevel) {
            m_ctxRingLevel = level;
            m_ctxRing->SetStyle(ECDI::RingProgressBarStyleOverride{
                .fillColor = level == 2 ? kErrorDark
                                        : (level == 1 ? kWarn : harness::kOkDark)});
            RequestRepaint();   // 颜色变了 ⇒ 确保这一帧被重画
        }
    }
    if (m_usageBarIn && m_usageBarOut) {
        // ★ 输入/输出两根条改为**以上下文容量为共同分母**（原来拿二者峰值互比，
        //   两个不同量纲相除 ⇒ 无意义）。容量未知时退化为二者峰值互比，
        //   仅作"相对大小"提示——**不**声称是占用率。
        const long long denom = (m_contextLimit > 0)
                                    ? m_contextLimit
                                    : std::max(m_usageIn, m_usageOut);
        const float inFrac = (m_usageIn >= 0 && denom > 0)
                                 ? static_cast<float>(m_usageIn) / static_cast<float>(denom)
                                 : 0.0f;
        const float outFrac = (m_usageOut >= 0 && denom > 0)
                                  ? static_cast<float>(m_usageOut) / static_cast<float>(denom)
                                  : 0.0f;
        m_usageBarIn->SetProgress(inFrac > 1.0f ? 1.0f : inFrac);
        m_usageBarOut->SetProgress(outFrac > 1.0f ? 1.0f : outFrac);
    }
    // 权限历史：无记录时给一行占位（否则是一张 70px 高的空盒子——`v8-light-insp.png` 可见）。
    // ★ 为何不 `SetVisible(false)` 把卡片藏掉：`Widget::SetVisible` **故意不内建 Invalidate**
    //   （`Widget.h:160-170` 明写「静默不刷新」），harness 还得自己补重绘；而「协议允许问、
    //   但本次一次都没问」本身是**有信息量的评估读数**（ask 门是 I3 的可见载体），
    //   藏掉不如显式说「暂无」。复用现有 3 行控件 ⇒ 零新增控件、零布局改动。
    const bool noHist = m_askHist.empty();
    for (size_t i = 0; i < 3; ++i) {
        if (m_hud.histChip[i] && m_hud.histWhat[i]) {
            const size_t n = m_askHist.size();
            if (i < n) {
                const auto& [choice, what] = m_askHist[n - 1 - i];
                m_hud.histChip[i]->SetText(choice);
                m_hud.histChip[i]->SetTextColor(choice == "allow" ? kOkDark : kErrorDark);
                m_hud.histWhat[i]->SetText(what);
            } else {
                m_hud.histChip[i]->SetText(noHist && i == 0 ? "—" : "");
                m_hud.histChip[i]->SetTextColor(kDarkMuted);
                m_hud.histWhat[i]->SetText(noHist && i == 0 ? "暂无权限请求" : "");
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
    // ★ 注意：这是**夹具总行数**（文件里有多少行），不是"本次消费了多少行"。
    //   被 §9 拒绝时它仍是全量（文件没变），故判读「是否真的拒绝」要看
    //   `blocks`（消费后才产生）与 `protocolIncompatible`，不能看本字段。
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
                ", \"arc\": " + std::to_string(m_probe->ArcCommands()) +
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
    if (m_options.hitProbe && !m_hitProbeLog.empty()) {
        // ★★ REQ-04 **边界**机读证据（2026-10-14）：命中门控受控对照。
        //   读法：`hitIsSend` = HitTest 是否返回发送按钮本身；`clickReached` = 合成
        //   Down+Up 后 SendPrompt 是否被进入。判据 = 正对照全 true ∧ forced-disabled 全 false。
        json += "  \"hitProbe\": [";
        for (size_t i = 0; i < m_hitProbeLog.size(); ++i) {
            const HitProbeRec& r = m_hitProbeLog[i];
            json += (i ? ", " : "");
            char b[320];
            std::snprintf(b, sizeof(b),
                          "{\"scene\": \"%s\", \"px\": %d, \"py\": %d, "
                          "\"sendEnabled\": %s, \"hitIsSend\": %s, \"hitIsNull\": %s, "
                          "\"hitIsRoot\": %s, \"hitConsumes\": %s, "
                          "\"hitClass\": \"%s\", \"clickReached\": %s}",
                          r.scene.c_str(), r.px, r.py,
                          r.sendEnabled ? "true" : "false",
                          r.hitIsSend ? "true" : "false",
                          r.hitIsNull ? "true" : "false",
                          r.hitIsRoot ? "true" : "false",
                          r.hitConsumes ? "true" : "false",
                          r.hitClass.c_str(),
                          r.clickReached ? "true" : "false");
            json += b;
        }
        json += "],\n";
    }
    if (m_options.reasoningProbe && !m_reasoningProbeLog.empty()) {
        // ★★ 协议 v2 §7 机读证据（2026-10-14）：思维链折叠**行为**轨迹。
        //   判据 = default 必须 expanded=false ∧ contentVisible=false（默认不渲染正文）；
        //          after-click-1 必须 expanded=true（点开了）；
        //          after-click-2 必须回到 false（可逆，不是单向）。
        //   箭头不单列字段——它在 `headerText` 里（`﹀` 收起 / `︿` 展开），
        //   与用户可见文本**逐字相同**，避免"读数说箭头对了、屏幕上是另一个字"。
        json += "  \"reasoningProbe\": [";
        for (size_t i = 0; i < m_reasoningProbeLog.size(); ++i) {
            const ReasoningProbeRec& r = m_reasoningProbeLog[i];
            json += (i ? ", " : "");
            char b[256];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"headerText\": \"%s\", "
                          "\"expanded\": %s, \"contentVisible\": %s, \"panelH\": %d}",
                          r.phase.c_str(), r.headerText.c_str(),
                          r.expanded ? "true" : "false",
                          r.contentVisible ? "true" : "false", r.panelH);
            json += b;
        }
        json += "],\n";
    }
    // ★ 流式态标题（**建行时刻**抓的，见 App.h 注释：事后回读永远是终态）
    if (m_options.reasoningProbe && !m_reasoningTitleStreaming.empty()) {
        json += "  \"reasoningStreamingTitle\": \"" + m_reasoningTitleStreaming + "\",\n";
    }
    if (m_options.orderProbe && !m_transcript.Order().empty()) {
        // ★★ Step 0a 机读证据（2026-10-19）：块**到达顺序**原文。
        //   为什么必须是数组而不是一个「顺序对不对」的布尔：探针若自己也算一遍
        //   `sorted(...)` 再比，比对逻辑就是**被测对象自己**——错的地方会一起错。
        //   故这里只如实吐出 `Order()` 的内容，判据留给跑批脚本对着夹具独立算。
        //   读法：本数组必须与夹具里 `block_start` 的出现次序**逐元素相同**。
        json += "  \"blockOrder\": [";
        const std::vector<std::string>& order = m_transcript.Order();
        for (size_t i = 0; i < order.size(); ++i) {
            json += (i ? ", \"" : "\"");
            json += order[i];
            json += "\"";
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
    //   前者是信封层 `t`（I5 明文覆盖），后者是 `kind`（协议 v2 起由 I8 覆盖）。
    //   ★ v2（2026-10-14）：`reasoning` 是**已知** kind ⇒ 不再计入本项
    //     （cot 夹具的读数由 12 变 0，正是这条生效的证据）。
    std::snprintf(buf, sizeof(buf), "  \"unknownKinds\": %zu,\n", m_transcript.UnknownKinds());
    json += buf;
    // ★ 协议 §9（v2）：如实记下 core 声明的版本与本 GUI 的上限，
    //   以及**是否因版本不兼容而拒绝连接**——否则一次被拒的运行会留下
    //   一份"看起来跑完了"的空摘要。
    std::snprintf(buf, sizeof(buf), "  \"protocolDeclared\": %d,\n", m_protocolVer);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolMaxSupported\": %d,\n",
                  kProtocolMaxSupported);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolIncompatible\": %s,\n",
                  m_protocolIncompatible ? "true" : "false");
    json += buf;
    // ★ 协议 v2 §3.1：上下文容量的读数（0 = core 未声明 ⇒ 百分比显示 `—`）
    std::snprintf(buf, sizeof(buf), "  \"contextLimit\": %lld,\n",
                  static_cast<long long>(m_contextLimit));
    json += buf;
    // ★ 上下文占用读数（分子/分母都如实输出，便于复核百分比是否算对）
    std::snprintf(buf, sizeof(buf), "  \"usageInput\": %lld\n",
                  static_cast<long long>(m_usageIn));
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
