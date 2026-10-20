#include "App.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <objbase.h>

#ifdef DrawText
#undef DrawText
#endif


#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
// ★ 侧栏分组标题的中间省略靠码点换算（`ByteOffsetToCodepointIndex`）：
//   `Label` 自己只做**尾部**裁剪，而路径的区分信息在尾部——详见 `ElideMiddleByCodepoints`。
#include "ECDI/Core/UTF8.h"
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

std::string Utf8FromWidePath(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), size,
                            nullptr, nullptr) != size) return {};
    return out;
}

std::wstring WideFromUtf8Path(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), out.data(), size) != size) return {};
    return out;
}

std::string PathToUtf8(const std::filesystem::path& value) {
    return Utf8FromWidePath(value.native());
}

// ★★ 2026-10-20：路径的**中间**省略（保留头与尾）。
//   为什么必须保留尾部：`ECDI::Label` 对超宽文本是**尾部丢弃**（单行 DrawText 裁剪），
//     而两个工作区的区别几乎总在叶子段（`.../proj` vs `.../other`）——尾部一丢，
//     两个分组标题在屏幕上就是同一行，“按工作区区分”退化成“按前缀区分”。
//   为什么这里**不**测量：本函数在 `RebuildSessionList` 的建树路径上，任何
//     `MeasureText` 都会进 `m_evalMeasureCalls`（评估仪器）⇒ 每次侧栏重建都会
//     污染 measure/paints 读数（既有基线里就有 measureCalls/measureChars）。
//     这里改用**码点预算**（分组标题恒为等宽字体，ASCII 每码点同宽），
//     再由 `--sidebar-probe` 侧的实测（`MeasureText` 后比对可用宽）证明真的放得下；
//     探针跑批不产基线，故那次测量不污染任何既有读数。
constexpr size_t kSidebarGroupTitleCp = 22;   // 22 码点 ≈ 11px Consolas 下 150px 宽

std::string ElideMiddleByCodepoints(const std::string& text, size_t maxCp) {
    if (text.empty()) return text;
    const size_t total = ECDI::ByteOffsetToCodepointIndex(text, text.size());
    if (total <= maxCp) return text;   // 放得下就**原样**返回（零改动即零回归）
    if (maxCp < 4) {
        // 退化：连 `X…Y` 都放不下，只能留尾部（尾部是唯一有信息量的一端）
        return text.substr(ECDI::CodepointIndexToByteOffset(text, total - maxCp));
    }
    // 头 1/4、尾 3/4：尾部信息更密（末级目录 + 叶子段）。
    const size_t headCp = maxCp / 4;
    const size_t tailCp = maxCp - headCp - 1;   // 减 1 = 省略号本身占一个码点
    const std::string head =
        text.substr(0, ECDI::CodepointIndexToByteOffset(text, headCp));
    const std::string tail =
        text.substr(ECDI::CodepointIndexToByteOffset(text, total - tailCp));
    return head + "\xe2\x80\xa6" + tail;   // U+2026 HORIZONTAL ELLIPSIS
}

constexpr int kListRowHeight = 28;   // listlayout 模式的固定行高（DIP）
constexpr int kRowGap = 10;          // 行间距（原型 C 的消息间距 23px ⇒ 折半，紧凑窗口下够用）
constexpr int kRowPadX = 10;
// ★ 画布左右留白（原型 C 的 22px 画布 padding）。此前行从 x=0 起排，
//   气泡/工具卡**贴着中栏边线**——这是"看起来挤"最直接的来源。
//   紧凑窗口（1120）下取 16，既留出呼吸感又不白吃掉横向空间。
constexpr int kCanvasPadX = 16;
constexpr int kHeaderH = 32;         // tool 卡片标题高，接近原型 C 的 39px

// ★★ Step 1（2026-10-19）：侧栏会话列表的几何。
//   ★ 为什么行宽是 158 而不是容器的 172：`ScrollView` 的垂直条一旦出现，
//     viewport 会缩到 `172 - (12 + 2) = 158`（`ScrollView.cpp` 的 `BarFootprint`）。
//     行宽若写成 172，出条的那一刻行会被裁掉 14px（还会多出一条水平条）——
//     于是"会话多到要滚动"这件事本身会改变行的排版。
//   ★ 行高 28 与原型 C 的 navitem 同高；行间距 4（比正文的 10 紧，列表才有"一列"的感觉）。
constexpr int kSidebarListW = 172;
constexpr int kSidebarRowW = 158;
constexpr int kSidebarRowH = 28;
constexpr int kSidebarRowGap = 4;
constexpr int kSidebarNameW = 106;   // 22(dot 右缘) → 128，给右侧 18px 删除钮留 6px 间隙
constexpr int kSidebarMaxRows = 4;
// ★★ 2026-10-20：工作区分组标题行的高度。
//   比会话行（28）矮得多：它是**分隔**，不是条目。若与行同高，列表中“工作区”与
//   “会话”就会长得一样高，用户无法从尺寸上区分两层结构。
constexpr int kSidebarGroupH = 18;   // 标题行高（分隔的高度，不带"条目"语义）

// 视觉令牌唯一来源 = Theme.h（2026-10-06 v6：对齐用户高保真模型）
// ★★ 本 GUI 支持的协议主版本上限（协议 §9；2026-10-14 v2 落地）。
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 `RouteLastMessage`）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
constexpr int kProtocolMaxSupported = 3;

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
    // ★★ 0d：构造第一个会话（源选择逻辑搬进 `CreateSessionFromOptions`，
    //   因为"新建会话"要复用同一段）。
    //   ★ 这里只**创建 + 设活动指针**，不调 `SwitchToSession`——此刻窗口还没建，
    //     `m_content` 为空，SwitchToSession 里的 RebuildTranscriptView 会解引用空指针。
    //     首会话的视图由 `Run()` 里的 `BuildUi` 建；之后 DrainInstant/Timed 增量填行。
    Session* first = CreateSessionFromOptions();
    if (first) {
        m_sess = first;
        m_activeIndex = m_sessions.size() - 1;
    }
    // first 为 nullptr（live 启动失败）⇒ m_sess 保持空 ⇒ Run 里报错退出（return 4）。
}

// ★ 析构保持 default：`m_sessions` 是 `vector<unique_ptr<Session>>`，
//   每个 Session 的 source/writer/rows 都是 RAII ⇒ 逐个自动析构。
//   live 子进程的收尸在 `Run()` 收尾显式做（bye → Stop），不在这里。
HarnessApp::~HarnessApp() = default;

// 按当前 CLI 选项建一个会话，push 进 m_sessions 并返回（**不**激活）。
//   ★ 这段就是原构造函数体：--core ⇒ 拉起子进程（真 live），否则夹具回放。
//   抽成方法是为了让 `NewSession()` 能复用同一套源创建逻辑。
//   ★ 返回 nullptr = live 启动失败（不 push 坏会话）；调用方必须判空。
HarnessApp::Session* HarnessApp::CreateSessionFromOptions() {
    auto session = std::make_unique<Session>();
    Session& s = *session;

    // ── 源选择（P1.5）：--core <exe> ⇒ 子进程真路径；否则夹具回放 ──
    if (!m_options.corePath.empty()) {
        auto child = std::make_unique<ChildSource>();
        std::string error;
        if (!child->Start(m_options.corePath, m_options.coreArgs, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ChildSource: " + error));
            // 源启动失败 ⇒ 不 push（避免留下一个拉不动的会话），如实返回 nullptr
            return nullptr;
        }
        s.child = child.get();
        s.source = std::move(child);
        s.writer = std::make_unique<ProtocolWriter>(*s.source);
        s.label = m_options.corePath.filename().generic_string() + " (live)";
        // ★ Step 5：来源必须是**显式字段**，不能再靠"corePath 空不空"推断——
        //   同一个进程里此后可以同时有 live / 启动 replay / 历史 replay。
        s.originKind = "live";
        s.originPath = m_options.corePath;
        if (m_sessions.empty()) {
            // 首个 live 会话：把 fixtureName 也定下来（窗口标题 / 摘要用）
            m_options.fixtureName = s.label;
        }
    } else {
        auto replay = std::make_unique<ReplaySource>();
        std::string error;
        if (!replay->Load(m_options.replayPath, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ReplaySource: " + error));
        }
        s.pendingTruncated = replay->TruncatedError();
        s.replay = replay.get();
        s.source = std::move(replay);
        s.label = m_options.fixtureName;
        // ★ Step 5：`--replay` 启动的会话与"点开的历史"是两种来源。
        //   区分它们有实际后果：只有后者会在打开时读 sidecar（前者是用户
        //   显式指定的夹具，跑批基线必须可复现，不能被历史状态污染）。
        s.originKind = "replay";
        s.originPath = m_options.replayPath;
    }

    // 会话名：首个用 fixtureName；后续在 NewSession 里改成"会话 N"。
    if (s.label.empty()) s.label = "会话 " + std::to_string(m_sessions.size() + 1);

    m_sessions.push_back(std::move(session));
    return m_sessions.back().get();
}

// ＋ 新建会话的真实行为（0d）。
//   ★ live 模式：再拉起一个 core 子进程 —— 这才是"真的新建一个会话"。
//   ★ 回放模式：没有 core 会对着一个空会话说话，回放夹具是一次性的。硬塞一个
//     假回复就是伪造读数（本仓最守不住的线）。故如实建一个**空会话**并说明情况——
//     "能点、有状态、切得过去"，但绝不假装 core 回了话。
void HarnessApp::NewSession() {
    // 先把当前活动会话的 timed 时钟冻结（否则它被切走后还在按墙上时间"到期"）
    PauseActiveTimedClock();

    const bool live = !m_options.corePath.empty();
    if (live) {
        // 真新建：拉一个新 core。创建失败（nullptr）⇒ 如实提示，活动会话不变。
        Session* created = CreateSessionFromOptions();
        if (!created) {
            AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
            Relayout();
            RequestRepaint();
            ResumeSessionTimedClock(*m_sess);
            return;
        }
        created->label = "会话 " + std::to_string(m_sessions.size());
        // ★★ 2026-10-20（用户裁决 m08553）：继承**活动会话**的工作区。
        //   ★ 必须在 `SwitchToSession(idx)` 之前抓：那之后 `m_sess` 已经指向新会话，
        //     `InheritedWorkspacePath()` 会去读新建会话自己的（空）外键，永远继承不到东西。
        //   ★ 也必须在此刻抓（而不是等 hello 回来时现查）：hello 是异步到达的，
        //     到那时活动会话可能已被用户又切走了——继承口径必须钉在"点新建的那一刻"。
        created->workspaceInheritedPath = InheritedWorkspacePath();
    } else {
        // 回放模式：建一个空会话（无 source ⇒ 不拉取；OnReplayTick 见空 source 直接返回）
        auto session = std::make_unique<Session>();
        session->label = "会话 " + std::to_string(m_sessions.size() + 1);
        // ★ Step 5：空会话是**第四类来源**。它没有 originPath ⇒ 没有 sidecar。
        session->originKind = "new";
        m_sessions.push_back(std::move(session));
    }

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);

    if (live) {
        // ★ 协议 §4：GUI 在收到 core 的 hello 之后回 hello（seq 0）。首会话在 Run() 里发；
        //   运行期新建的会话必须在这里补发，否则新 core 永远等不到握手。
        Session& s = *m_sessions[idx];
        if (s.writer) s.writer->Hello("0.0.1");
        if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    } else {
        // 回放空会话：给一条诚实提示（不伪造 core 回复）
        AddNoticeRow("[新建会话：回放模式无 core，空会话——切回上一个会话看历史]", kDarkMuted);
        Relayout();
        RequestRepaint();
    }
    // ★ Step 1：会话数变了 ⇒ 列表必须重建（新行 + 每行的 `×` 可用性都可能变）。
    //   只置脏：本函数可能由「＋ 新建会话」按钮的点击回调进来（派发栈上，不能拆树）。
    m_sidebarDirty = true;
}

// 切换活动会话（0d）：拆当前视图 → 指向目标会话 → 按目标会话模型重建。
void HarnessApp::SwitchToSession(size_t index) {
    if (index >= m_sessions.size()) return;
    if (index == m_activeIndex && m_sess == m_sessions[index].get()) return;   // 已在该会话

    // ★★ 2026-10-20：临时会话在切走时消失（用户裁决 m09443）。
    //   必须在改 `m_activeIndex` **之前**：此时 `m_sess` 还指着旧会话，
    //   才能判断"旧会话是不是临时的"。
    MaybeDeleteTemporarySession();

    // ① 冻结当前活动会话的 timed 时钟（切走 ⇒ 它不该继续"到期"）
    PauseActiveTimedClock();
    // ①b ★ 在**还指着旧会话**时把视图状态同步进旧会话的模型。
    //    顺序要点：RebuildTranscriptView 内部也会 SyncRowViewState，但那时 m_sess
    //    已指向新会话——同步的是新会话的旧行，旧会话的状态就没人管了。眼下不出事
    //    只是因为 ToggleRow/MarkAnswered 每次点击都已写回模型；把正确性押在
    //    "别处恰好也写了"上是本仓最反对的形状，故在这里显式补上。
    SyncRowViewState();

    // ② 切换活动指针。m_sess 是全部 200+ 处调用点的"当前会话"锚，
    //    改这一个指针，OnReplayTick/RefreshHud/… 立即指向新会话。
    m_activeIndex = index;
    m_sess = m_sessions[index].get();

    // ③ 按新会话的模型重建转录视图（复用 0c 的 RebuildTranscriptView）。
    //    ★ 这正是 0c 存在的全部理由：没有"从模型重建"，切会话就无处安放视图。
    //    传 nullptr ⇒ 按 timeline 正向重建（不是负对照）。
    //    ★ 顺带清掉与旧会话绑定的渲染缓存：环色档位（否则新会话首帧可能不重绘）。
    m_ctxRingLevel = -1;
    RebuildTranscriptView();

    // ④ 恢复新会话的 timed 时钟（补偿它被冻结的墙上时间）
    ResumeSessionTimedClock(*m_sess);

    // ⑤ HUD / 进度 / 使能全部跟着新会话刷新
    // ★ Step 1：活动项变了 ⇒ 侧栏的选中态也变了。同样只置脏、不在这里重建
    //   （本函数可能从行点击的回调里进来，那时拆列表 = 拆掉正在执行回调的那个 Button）。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 1（2026-10-19）：侧栏会话列表 = 第二条「模型 → 视图」通道
//
// 为什么整段可以抽成纯函数：一行会话的全部输入只有 `(label, active, canDelete)`
//   —— 它不读 `m_sess`、不读转录、不读协议状态。所以「建一行」与「列表整体重建」
//   可以分开：前者是纯的（探针能反复调用而不改变任何状态），后者只负责拆+装。
//
// ★ 行根为什么必须是 `Button` 而不是 Panel（这是本项目最贵的一条教训）：
//   `Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），而 `Widget::HitTest` 的返回
//   契约是「目标自身必须 ContainsPoint」⇒ **Panel 行永远点不到**。要"整行可点"，
//   行根只能是 Button（或自建 Widget 子类，但那要重写一遍焦点/按下/悬停视觉）。
//
// ★★ 为什么行内「×」不会连带触发整行的切换（这条依赖框架契约，不是巧合）：
//   ECDI Phase 35 起 `Button::OnMouseButtonDown` 与 `OnMouseButtonUp` 都会
//   `event.SetHandled()`（`Button.cpp:126-181`），而 `Application` 的冒泡循环每次派发后
//   都查 `IsHandled()` 再决定是否继续上溯（`Application.cpp:324/361/390`）。⇒ 点「×」时
//   事件在子按钮处就被消费掉，父行按钮的 `OnMouseButtonUp` 根本不会被调用。
//   `EventConsumptionTests.cpp:817-841` 有这条语义的回归用例（嵌套点击/消费/移除）。
//
// ★★★ 为什么回调里**不能**同步重建列表（UAF，本步唯一的真陷阱）：
//   重建 = `content.RemoveChild(...)` 拆掉全部行，而正在执行 `OnMouseButtonUp` 的那个
//   Button **就在被拆的子树里** ⇒ 回调返回后回到 `RaiseClick()` 的栈帧，`this` 已析构。
//   框架的官方出口是 `Widget::RequestRemove()`（派发中只摘树、延迟析构，`Widget.cpp:231-289`），
//   但它只保护**你调它的那一个节点**，保护不了"我顺手把整棵列表重建了"。
//   ⇒ 本实现的规矩：**点击回调只改模型 + 置 `m_sidebarDirty`，重建留到 tick 里做**
//     （timer 回调不在事件派发栈上，此时拆树是安全的）。这条规矩比"记得用 RequestRemove"
//     更不容易被后人破坏，因为它在结构上就不给同步重建留位置。
// ═══════════════════════════════════════════════════════════════════════════

std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionRow(size_t index, bool active) {
    // ★ 纯函数：只读 m_sessions[index] 与传入的 active，不写任何成员。
    auto row = std::make_unique<ECDI::Button>();
    const Session* session = m_sessions[index].get();
    const bool canDelete = m_sessions.size() > 1;

    // 选中态 = 三种视觉同时变（底色/圆点/文字色），但**只有文字色可读回**：
    //   ECDI 的 Button/Panel 都没有 style getter（`Button::SetStyle` 只写不读），
    //   所以探针要靠 `Label::GetTextColor()` 判"这一行是不是选中项"。
    //   ⇒ 文字色不是纯装饰，它同时承担**读数**职责，改配色时别把它改没了。
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = active ? kUserBg : kPanel2,
        .border = active ? harness::kDarkAccent : kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,   // 行根不带文字，显式给值防依赖零值
        .foreground = active ? kAccentInk : kDarkInk});

    auto dot = uikit::Dot(7, active ? harness::kDarkAccent : kLine);
    dot->SetPosition(8, (kSidebarRowH - 7) / 2);
    row->AddChild(std::move(dot));

    // 名称 = `Session::label`（「会话 N」/ 夹具名 /「xxx (live)」）。
    // ★ 不做截断：文本原样进控件，探针才能拿它跟模型逐字比对（A==B 判据）；
    //   装不下的部分由渲染层的控件矩形裁剪（视觉上是截断，数据上没有）。
    // ★ 宽度随「有没有 ×」变：`×` 占 18 + 右边距 6 ⇒ 有删除钮时名称只能到 104，
    //   没有时（只剩一个会话）可以一直铺到行右边距。写死 104 会让单会话下的
    //   `demo-session.jsonl` 尾部被裁掉一个字符——**探针读文字读不到这件事**
    //   （`GetText()` 返回完整文本，裁剪发生在绘制层），只能靠截图发现。
    const int nameW = canDelete ? 104 : (kSidebarRowW - 22 - 2);
    auto name = uikit::Label(session->label.empty() ? "会话" : session->label,
                             nameW, 16, active ? kAccentInk : kDarkInk,
                             harness::kFontSmall);
    name->SetPosition(22, (kSidebarRowH - 16) / 2);
    // ★ 名称标签禁用 ⇒ 它**不参与命中**（`Widget::HitTest` 的 `!IsEnabled()` 早退，
    //   `Widget.cpp:321`），鼠标落点直接归行根 Button。两个后果都是想要的：
    //   ① 行根的 hover/press 视觉真的会亮（若 Label 抢了命中，`UpdateHoverState`
    //      只通知 Label，祖先收不到 `OnMouseEnter` ⇒ 整行点上去毫无反馈）；
    //   ② 探针/自动化点"行中心"时命中的一定是行根，读数不依赖文本是否恰好被点中。
    //   ★ `IsEnabled` 全库只 gate 命中 / 焦点收集 / 键派发，**无任何绘制消费者**
    //     （`Widget.h:197`）⇒ 禁用不会让文字变灰，视觉零变化。
    name->SetEnabled(false);
    row->AddChild(std::move(name));

    if (canDelete) {
        // ★ `×`(U+00D7) 是 symcand 实测两主题都有字形的少数符号之一
        //   （`✕`(U+2715)/`✖` 在浅色 YaHei 缺字形 ⇒ 会画成空白）。
        auto del = std::make_unique<ECDI::Button>("×");
        del->SetSize(18, 18);
        del->SetPosition(kSidebarRowW - 24, (kSidebarRowH - 18) / 2);
        del->SetFont(harness::SansFont(harness::kFontSmall));
        del->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        // ★ 捕获的是**会话身份**（Session*），不是 index：删除会让下标整体前移，
        //   捕获下标的行会"点了第 2 行却删掉第 3 个会话"。指针由 unique_ptr 持有、
        //   从不搬动 ⇒ 点击时现查下标才是对的（查不到就什么都不做，见下）。
        del->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    DeleteSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(del));
    }

    // 整行点击 = 切到该会话。同样按身份解析下标。
    row->SetOnClick([this, session] { SwitchToSessionByPtr(session); });
    return row;
}

// ★★ 2026-10-20：工作区分组标题行。
//   两种行长得不一样，但**只差在尺寸与颜色**，不需新控件类型：
//   `SnapshotSessionList` 靠“行根是不是 Button”区分两类行（第 5 段 `kind`）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionGroupRow(const std::string& title,
                                                               const std::string& workspaceId) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetSize(kSidebarRowW, kSidebarGroupH);
    // 透明底 + 无边框：标题是分隔，不是按钮。透明度用 alpha=0（与 `×` 子钮同一手法）。
    row->SetStyle(ECDI::PanelStyleOverride{.background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
                                           .cornerRadius = 0.0f,
                                           .borderWidth = 0.0f});

    // 标题文字 = 工作区路径（= core 回显的规范化结果，不是 GUI 自己拼的）。
    //   ★ 用 Mono 字体：路径里的 `l/1/I`、`O/0` 必须一眼可分，否则两个相似路径的
    //     工作区分组在屏幕上就等于同一行（这正是“按工作区区分”要解决的那个问题）。
    //   ★★ 必须**自己**做中间省略（`ElideMiddleByCodepoints`）：`ECDI::Label` 对超宽
    //     文本是**尾部丢弃**——截图实测 `D:/Project/ECDI_fake_harness` 被画成
    //     `D:/Project/ECDI_fake_harn`（少了 3 个字），而两个工作区的差异几乎总在叶子段。
    //     尾部一丢，“按工作区区分”就退化成“按前缀区分”：两个都在 `D:/Project/` 下的
    //     工作区在屏幕上会是同一行。把省略放在中间，尾部至少保留叶子段。
    // ★ 有「＋」按钮时标题宽度让出 22px（按钮 18 + 间隙 4）。
    const int labelW = workspaceId.empty() ? (kSidebarRowW - 8) : (kSidebarRowW - 30);
    // ★★ 省略预算必须跟着 labelW 走：有按钮时可用宽度少了 22px，
    //   若仍用 22 码点（≈132px）而 labelW 只有 128px，绘制层会裁掉尾部。
    const size_t elideCp = workspaceId.empty() ? kSidebarGroupTitleCp : (kSidebarGroupTitleCp - 1);
    const std::string shown = ElideMiddleByCodepoints(title, elideCp);
    auto label = uikit::Label(shown, labelW, kSidebarGroupH - 4, kDarkMuted,
                              harness::kFontMicro, true);
    label->SetPosition(8, 2);
    // 禁用：标题不参与命中（`Panel::ContainsPoint` 已经恒 false，这里只是让
    //   “文字不是交互对象”也成立；无绘制消费者 ⇒ 不变灰）。
    label->SetEnabled(false);
    row->AddChild(std::move(label));

    // ★★ 「＋」按钮：拉起该工作区的临时会话（m09443）。
    //   行根是 Panel（`ContainsPoint` 恒 false）⇒ 只有这个子 Button 能被点到。
    //   回调捕获 workspaceId（稳定字符串），不捕获下标。
    if (!workspaceId.empty()) {
        auto plus = std::make_unique<ECDI::Button>();
        plus->SetText("＋");
        plus->SetSize(18, 18);
        plus->SetPosition(kSidebarRowW - 24, (kSidebarGroupH - 18) / 2);
        plus->SetFont(harness::SansFont(harness::kFontSmall));
        plus->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        plus->SetOnClick([this, workspaceId] {
            NewTemporarySession(workspaceId);
        });
        row->AddChild(std::move(plus));
    }
    return row;
}

std::vector<size_t> HarnessApp::GroupTitlesForDisplay() const {
    std::vector<size_t> order;
    order.reserve(m_workspaces.size());
    // 创建序 = `m_workspaces` 下标序（只追加、不删、不重排）；显示序 = 它的逆序。
    //   `rbegin/rend` 而不是 `std::sort`：排序需要一个键，而这里根本没有键——
    //   唯一的规则就是“后建在上”，那就是逆序本身。
    for (auto it = m_workspaces.rbegin(); it != m_workspaces.rend(); ++it) {
        order.push_back(static_cast<size_t>(m_workspaces.rend() - it - 1));
    }
    return order;
}

void HarnessApp::RebuildSessionList(const std::vector<size_t>* orderOverride) {
    if (!m_hud.sessList) return;
    ECDI::Widget& content = m_hud.sessList->GetContentView();

    // ★ 与 RebuildTranscriptView 同一条纪律：拆干净再装，不做增量比对。
    //   `RemoveChild` 会先 `ForgetSubtree` 注销焦点/捕获/hover 再交回所有权
    //   （`Widget.cpp` RemoveChild），故这里不需要手工清理状态。
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    const size_t n = m_sessions.size();
    // ★★ 2026-10-20：行高不再统一（分组标题 `kSidebarGroupH`=18、会话行 `kSidebarRowH`=28）
    //   ⇒ 位置只能逐行累加。再用「下标 × 步进」会把标题按会话行高排，越往下偏得越多。
    int y = 0;
    int lastGap = 0;
    auto place = [&](std::unique_ptr<ECDI::Widget> row, int h) {
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += h + kSidebarRowGap;
        lastGap = kSidebarRowGap;
    };

    if (m_workspaces.empty()) {
        // ★ 一个工作区都没有 ⇒ **平铺**，与分组功能出现之前逐字节同形。
        //   为什么不画一个“未归属工作区”标题凑数：没有工作区实体时那行标题只是
        //   在给“什么都没有”起名字，纯装饰；何况回放会话**永远不会**有工作区
        //   （`MaybeStartWorkspaceSync` 首行 `!writer` 就 return），给它们编一个
        //   “未归属”分组等于在界面上宣告一个不存在的关系。
        for (size_t i = 0; i < n; ++i) {
            size_t idx = i;
            if (orderOverride) {
                if (orderOverride->size() != n) break;   // 形状不对就不画（探针会给同长度）
                idx = (*orderOverride)[i];
                if (idx >= n) break;
            }
            const bool active = (idx == m_activeIndex);
            place(BuildSessionRow(idx, active), kSidebarRowH);
        }
    } else {
        // ★ 分组：标题按**创建序逆序**（后建在上，用户裁决 m08553），组内会话按
        //   `m_sessions` 序（创建序正序）。两个方向都是固定的，不按“最近使用”重排——
        //   任何动态排序都会让同一个工作区在两次刷新之间换位置，用户会以为它是两个。
        //   ★ 分组模式下 `orderOverride` **不生效**：它表达的是“会话次序”，
        //     而分组结构下会话次序由“属于哪个组”决定。分组模式的负对照改用
        //     “换工作区路径”（见 `RunSidebarProbe` ⑬），只改一个自变量。
        std::vector<bool> shown(n, false);
        for (size_t wi : GroupTitlesForDisplay()) {
            const Workspace& ws = *m_workspaces[wi];
            place(BuildSessionGroupRow(ws.path, ws.id), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (m_sessions[i]->workspaceId != ws.id) continue;
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
                shown[i] = true;
            }
        }
        bool anyOrphan = false;
        for (size_t i = 0; i < n; ++i) {
            if (!shown[i]) { anyOrphan = true; break; }
        }
        if (anyOrphan) {
            // ★ 外键指向一个不存在的工作区也落在这里（`WorkspaceById` 返回 nullptr 时
            //   就是这么处理的，两处口径一致）：宁可显示“未归属”，也不能把它悄悄
            //   塞进第一个工作区——那会把归错桶伪装成正常。
            place(BuildSessionGroupRow("未归属工作区", std::string()), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (shown[i]) continue;
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
            }
        }
    }
    const int totalH = y > 0 ? y - lastGap : 0;
    // 高度上限仍按“4 个会话行”折算成像素：分组标题不算一行（它是分隔，不是条目），
    //   否则两个工作区就会把可见区吃掉一半。
    const int visibleH = std::min(totalH, kSidebarMaxRows * (kSidebarRowH + kSidebarRowGap)
                                              - kSidebarRowGap);
    // ★ 先 SetSize（决定 viewport）再 SetContentExtent（决定要不要出条）：
    //   反过来的话第一帧会按旧 viewport 判一次"不需要滚动条"，出条后行宽被挤。
    m_hud.sessList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.sessList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.sessList->SetContentOffset(0, m_hud.sessList->GetScrollOffsetY());

    // 列表高度是**变**的（会话越多越高，到 4 行封顶）⇒ 侧栏那条 VerticalLayout
    // 必须重排一次，否则下面的「项目」段还停在旧位置。
    if (ECDI::Widget* parent = m_hud.sessList->GetParent()) parent->Arrange();
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 5（2026-10-19）：历史会话 —— 把**磁盘上的文件**变成会话
// ───────────────────────────────────────────────────────────────────────────
// 这一节与上面 Step 1 的会话列表是**两种东西**，别混：
//   · `m_sessions`  = 已经打开的会话（内存里的模型）；
//   · 这里的历史区  = 磁盘上**可打开的文件**（`protocol/fixtures/**/*.jsonl`）。
//   点一行历史 ⇒ 多出一个已打开会话（列表里多一行），两者数量不必相等。
//
// ★ 为什么"打开"必须是**真回放**而不是"读到底、GUI 自己造 Block"：
//   后者会让 GUI 变成第二个转录作者，与 `PROTOCOL.md:244`「GUI 不产生转录块，
//   唯一作者是 core」直接冲突；并且坏文件的读数（`seqGaps` / `unknownKinds` /
//   `protocolIncompatible`）全都在**消费路径**上产生，绕开消费就没有读数了
//   ——历史区恰恰是最该照见坏文件的地方。跳到末尾只是**速度**参数。
//
// ★ 为什么历史区**只列不删**：`protocol/fixtures/` 下每个文件都有 sha256 基线，
//   删除 UI 一旦能碰它们，所有回归基线就都开口子了。
// ═══════════════════════════════════════════════════════════════════════════

std::filesystem::path HarnessApp::HistoryRoot() const {
    namespace fs = std::filesystem;
    // ① 从当前夹具路径往上找：`<ancestor>/protocol/fixtures` 存在就用它。
    //    为什么从夹具路径找而不是从 cwd 找：跑批脚本常常在仓库根跑，cwd 恰好对；
    //    但用户也可能从别处启动、只给一个绝对 `--replay`，此时 cwd 是错的，
    //    而夹具自己的位置永远是对的（它必然在仓库里）。
    std::error_code ec;
    if (!m_options.replayPath.empty()) {
        fs::path p = fs::absolute(m_options.replayPath, ec);
        if (ec) { p = m_options.replayPath; ec.clear(); }
        for (fs::path dir = p.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            const fs::path probe = dir / "protocol" / "fixtures";
            if (fs::is_directory(probe, ec) && !ec) return probe;
            ec.clear();
            if (dir == dir.root_path()) break;
        }
    }
    // ② 退到 cwd 相对路径（开发时最常见的形态）。
    const fs::path cwdProbe = fs::path("protocol") / "fixtures";
    if (fs::is_directory(cwdProbe, ec) && !ec) return fs::absolute(cwdProbe, ec);
    return std::filesystem::path();
}

std::vector<std::filesystem::path> HarnessApp::FindHistoryFiles() const {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const fs::path root = HistoryRoot();
    if (root.empty()) return out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        // ★ 跳过 `__pycache__`：`cot/` 下真实存在（生成器跑过就会有），
        //   它不是夹具、也不该出现在历史里。
        if (p.filename() == "__pycache__") { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (p.extension() != ".jsonl") continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [&root](const fs::path& a, const fs::path& b) {
        return a.lexically_relative(root).generic_string()
             < b.lexically_relative(root).generic_string();
    });
    return out;
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildHistoryRow(const std::filesystem::path& file) {
    // ★ 行根同样是 Button：`Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），
    //   可点的行根只能是 Button（同 `BuildSessionRow` 的理由）。
    auto row = std::make_unique<ECDI::Button>();
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,
        .foreground = kDarkInk});
    // 显示**相对仓库夹具根的路径**（`realistic/realistic-session.jsonl`），
    // 而不是裸文件名：六份夹具里 `cot-session.jsonl` 与 `demo-session.jsonl`
    // 不重名，但真实使用中同名文件很常见，裸名会让人点错。
    std::string shown = file.filename().generic_string();
    const std::filesystem::path root = HistoryRoot();
    if (!root.empty()) {
        const std::filesystem::path rel = file.lexically_relative(root);
        if (!rel.empty()) shown = rel.generic_string();
    }
    auto label = uikit::Label(shown, kSidebarRowW - 16, 16, kDarkInk, harness::kFontSmall);
    label->SetPosition(8, (kSidebarRowH - 16) / 2);
    // 禁用 ⇒ 不参与命中（点行中心一定命中行根），且 `IsEnabled` 无绘制消费者 ⇒ 视觉零变化。
    label->SetEnabled(false);
    row->AddChild(std::move(label));
    // ★ 回调**只置脏**，真打开留到 tick：本按钮此刻正被派发链持有，
    //   而"打开"会 `RemoveChild` 掉整棵侧栏/转录区（同 `m_sidebarDirty` 的 UAF 理由）。
    row->SetOnClick([this, file] {
        m_historyOpenPending = file;
        m_historyDirty = false;   // 打开会连列表一起重建，不必再单独刷一次
    });
    return row;
}

void HarnessApp::RebuildHistoryList() {
    if (!m_hud.historyList) return;
    if (!m_historyScanned) {
        m_historyFiles = FindHistoryFiles();
        m_historyScanned = true;
    }
    ECDI::Widget& content = m_hud.historyList->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }
    const size_t n = m_historyFiles.size();
    const size_t rowStride = static_cast<size_t>(kSidebarRowH + kSidebarRowGap);
    int y = 0;
    for (size_t i = 0; i < n; ++i) {
        auto row = BuildHistoryRow(m_historyFiles[i]);
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += static_cast<int>(rowStride);
    }
    const int totalH = n > 0 ? static_cast<int>(n * rowStride) - kSidebarRowGap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * static_cast<int>(rowStride)
                                              - kSidebarRowGap);
    m_hud.historyList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.historyList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.historyList->SetContentOffset(0, m_hud.historyList->GetScrollOffsetY());
    if (m_hud.historyCount) {
        m_hud.historyCount->SetText(std::to_string(n) + " 个历史文件");
    }
    if (ECDI::Widget* parent = m_hud.historyList->GetParent()) parent->Arrange();
}

std::filesystem::path HarnessApp::UiStatePath(const Session& s) const {
    // ★ 与夹具**同目录**、名字加 `.ui.json` 后缀：`demo-session.jsonl` →
    //   `demo-session.jsonl.ui.json`。同目录是为了"夹具搬走，状态跟着走"；
    //   加后缀而不是另建目录，是为了让 `rm *.jsonl` 这种清理动作**不会**顺手删掉状态。
    // ★ 不用 `evals/` 里那种 `<x>.jsonl.json`：那是**评估摘要**（旧产物，来自 `f243579`），
    //   与本状态文件语义完全不同，同名会让两者互相覆盖。
    if (s.originPath.empty()) return std::filesystem::path();
    std::filesystem::path p = s.originPath;
    p += ".ui.json";
    return p;
}

bool HarnessApp::LoadUiState(Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value root;
    std::string error;
    if (!json::Parse(text, root, error)) {
        // 坏状态文件不该让会话打不开：如实记一条日志，按"没有决策记录"继续。
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: " + error + " (" +
                                           path.generic_string() + ")"));
        return false;
    }
    // answers: { "<ask id>": "allow" | "deny" }
    if (const json::Value* answers = root.Find("answers");
        answers && answers->IsObject()) {
        for (const auto& kv : answers->object) {
            if (!kv.second.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Ask) continue;
                if (entry.id != kv.first) continue;
                entry.answered = true;
                entry.answer = kv.second.text;
            }
        }
    }
    // expanded: ["b1", ...]（只记展开的，收起是默认态）
    if (const json::Value* expanded = root.Find("expanded");
        expanded && expanded->IsArray()) {
        for (const json::Value& id : expanded->array) {
            if (!id.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Block) continue;
                if (entry.id == id.text) entry.expanded = true;
            }
        }
    }
    return true;
}

void HarnessApp::SaveUiState(const Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return;
    json::Value root;
    root.type = json::Value::Type::Object;
    root.object["version"] = json::Value();       // 先占位，下面逐步覆盖
    root.object["version"].type = json::Value::Type::Number;
    root.object["version"].number = 1.0;
    root.object["answers"].type = json::Value::Type::Object;
    root.object["expanded"].type = json::Value::Type::Array;
    for (const TimelineEntry& entry : s.timeline) {
        if (entry.kind == TimelineEntry::Kind::Ask && !entry.answer.empty()) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.answer;
            root.object["answers"].object[entry.id] = v;
        } else if (entry.kind == TimelineEntry::Kind::Block && entry.expanded) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.id;
            root.object["expanded"].array.push_back(v);
        }
    }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: 写入失败 " + path.generic_string()));
        return;
    }
    out << root.Pretty(2) << "\n";
}

void HarnessApp::ApplyUiStateToView(Session& s) {
    // ★ 为什么需要这一步（顺序陷阱）：`RebuildTranscriptView` 的第一行是
    //   `SyncRowViewState()`（视图 → 模型）。刚抽干出来的行全是 `expanded=false` /
    //   `answered=false` ⇒ 若只把 sidecar 写进模型就重建，同步会**立刻把刚读进来的
    //   状态覆盖掉**（模型被更旧的视图抹平）。故先把同一批状态也写进视图行。
    for (RowRec& r : s.rows) {
        if (r.timelineIdx >= s.timeline.size()) continue;
        const TimelineEntry& entry = s.timeline[r.timelineIdx];
        if (r.ask && entry.answered) {
            r.answered = true;
            ApplyAnsweredLook(r, entry.answer);   // 同一条视觉路径（Step 3）
        }
        if (r.body && entry.expanded) {
            r.expanded = true;
            r.body->SetExpanded(true);
        }
    }
}

bool HarnessApp::OpenHistorySession(const std::filesystem::path& file) {
    std::string error;
    auto replay = std::make_unique<ReplaySource>();
    if (!replay->Load(file, error)) {
        // ★ 如实报错，不建半好会话（半好会话会让"打不开"看起来像"打开了一个空会话"）。
        AddNoticeRow("[打开失败] " + file.filename().generic_string() + " — " + error,
                     kErrorDark);
        Relayout();
        RequestRepaint();
        return false;
    }

    PauseActiveTimedClock();

    auto session = std::make_unique<Session>();
    Session& s = *session;
    s.pendingTruncated = replay->TruncatedError();
    s.replay = replay.get();
    s.source = std::move(replay);
    s.label = file.filename().generic_string();
    // ★★ 来源必须记下来：这是本步新增的第三类会话（live / 启动时 --replay / 历史）。
    //   没有它，后续两个 tick 里就分不清"这个会话该不该读 sidecar"。
    s.originKind = "replay-history";
    s.originPath = file;
    m_sessions.push_back(std::move(session));
    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);   // 内部会 RebuildTranscriptView（此刻还是空的，无妨）

    // ── 跳到末尾：把源抽干（复用 `DrainInstant` 的消费语义）──
    //   ★ 为什么必须走 `HandleLine`（而不是直接读行数）：坏文件的读数
    //     （seqGaps/unknownKinds/protocolIncompatible）全在消费路径上产生。
    std::string line;
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    m_sess->instantDone = true;   // 已抽干 ⇒ 别让 DrainInstant 再跑一遍
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }

    // ── GUI 侧状态（sidecar）──
    const bool hadState = LoadUiState(*m_sess);
    if (hadState) {
        ApplyUiStateToView(*m_sess);
    } else {
        // ★ 缺失时必须**说出来**，不能静默当作"未回答"：Step 3 修的正是
        //   "历史把授权决定抹平成未回答"这条病，换个位置不能复发。
        bool anyAsk = false;
        for (const TimelineEntry& entry : m_sess->timeline) {
            if (entry.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        if (anyAsk) {
            AddNoticeRow("[此会话没有决策记录：磁盘上没有 " +
                         UiStatePath(*m_sess).filename().generic_string() +
                         "，权限门一律按未回答显示]", kDarkMuted);
        }
    }

    // 抽干后再重建一次：此刻 timeline 才有内容，且 sidecar 已写进模型与视图。
    RebuildTranscriptView();
    Relayout();
    FollowTailIfPinned();
    m_sidebarDirty = true;    // 会话列表要多出一行
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
    return true;
}

std::vector<std::string> HarnessApp::SnapshotSessionList() const {
    std::vector<std::string> out;
    if (!m_hud.sessList) return out;
    const ECDI::Widget& content = m_hud.sessList->GetContentView();
    for (size_t i = 0; i < content.GetChildCount(); ++i) {
        const ECDI::Widget* row = content.GetChildAt(i);
        if (row == nullptr) continue;
        std::string name;
        std::string selected = "0";
        size_t buttons = 0;
        for (size_t c = 0; c < row->GetChildCount(); ++c) {
            const ECDI::Widget* child = row->GetChildAt(c);
            if (dynamic_cast<const ECDI::Button*>(child)) ++buttons;
            if (const auto* lbl = dynamic_cast<const ECDI::Label*>(child)) {
                // 第一个 Label 就是名称（圆点是 Panel、`×` 是 Button，都不满足本分支）
                if (name.empty()) {
                    name = lbl->GetText();
                    selected = (lbl->GetTextColor() == kAccentInk) ? "1" : "0";
                }
            }
        }
        // 字段序：名称 · 选中 · 行内按钮数（×）· 行矩形 · ★行类（kind）。
        // 行矩形也算视图的一部分：「画了几行」和「画在哪」都能被抓出来。
        // ★★ 2026-10-20：`kind` **只能追加在尾部**。若插在中间，所有历史读数会在
        //   字段位上静默串味（旧日志把 geometry 当 kind 读，不报错但全错）。
        //   判据来源 = 行根类型：会话行是 Button（`Panel::ContainsPoint` 恒 false，
        //   可点行的根只能是 Button），分组标题行是 Panel。这就是两类行在视图层的
        //   唯一区别，不靠尺寸（尺寸会随主题变）。
        char geo[64];
        std::snprintf(geo, sizeof(geo), "%d,%d,%d,%d", row->GetX(), row->GetY(),
                      row->GetWidth(), row->GetHeight());
        const char* kind = dynamic_cast<const ECDI::Button*>(row) ? "session" : "group";
        out.push_back(name + '\x1f' + selected + '\x1f' + std::to_string(buttons) + '\x1f'
                      + geo + '\x1f' + kind);
    }
    return out;
}

HarnessApp::SidebarCompare HarnessApp::CompareSessionList(
    const std::vector<std::string>& before, const std::vector<std::string>& after) const {
    static const char* kFieldName[] = {"name", "selected", "buttons", "geometry", "kind"};
    SidebarCompare cmp;
    if (before.size() != after.size()) {
        cmp.matched = false;
        cmp.firstDivergence = "rowCount " + std::to_string(before.size()) + " -> "
                              + std::to_string(after.size());
        return cmp;
    }
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] == after[i]) continue;
        cmp.matched = false;
        // 逐字段定位第一处差异（与转录区 CompareRows 同款可读性：别只报"不一样"）
        auto split = [](const std::string& s) {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : s) {
                if (ch == '\x1f') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(ch);
            }
            parts.push_back(cur);
            return parts;
        };
        const std::vector<std::string> a = split(before[i]);
        const std::vector<std::string> b = split(after[i]);
        std::string detail = "row " + std::to_string(i);
        // 段落数必须与 `kFieldName` 同长：写死 4 会让新增的 `kind` 永远不参与比对，
        //   跟“没加”一样（而且不报错）。
        constexpr size_t kFieldCount = sizeof(kFieldName) / sizeof(kFieldName[0]);
        for (size_t f = 0; f < a.size() && f < b.size() && f < kFieldCount; ++f) {
            if (a[f] == b[f]) continue;
            detail += std::string(" ") + kFieldName[f] + " [" + a[f] + "] -> [" + b[f] + "]";
            break;
        }
        cmp.firstDivergence = detail;
        return cmp;
    }
    return cmp;
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建的比对面。
//   与侧栏/转录两套快照的关系是**聚合**而非替代：这里不重写字段格式，直接复用
//   两个既有量具（`SnapshotSessionList` / `SnapshotRows`），只额外抓三样它们看不见的
//   东西——输入框草稿、侧栏可见性、以及"现在是什么主题"。
//   ★ 为什么必须四路并联：整树重建的风险不是"某一处画错了"，而是"某处状态没被搬过去"。
//     只查侧栏会漏掉转录的展开态；只查转录会漏掉输入框里的草稿；只查控件会漏掉
//     `m_sidebarVisible` 这种只存在于模型里的状态（它没有对应的"控件属性"可读）。
HarnessApp::UiSnapshot HarnessApp::SnapshotUi() const {
    UiSnapshot snap;
    snap.sidebar = SnapshotSessionList();
    snap.transcript = SnapshotRows();
    snap.inputText = (m_promptBar && m_promptBar->input())
                         ? m_promptBar->input()->GetText() : std::string();
    snap.sidebarVisible = (m_hud.sidebar != nullptr) && m_hud.sidebar->IsVisible();
    snap.light = harness::g_themeLight;
    snap.activeIndex = m_activeIndex;
    snap.sessionCount = m_sessions.size();
    // 仪器读数也进快照：便于在失败时看出"是不是重建把计数器搞坏了"。
    snap.paints = m_counting ? m_counting->PaintCount() : 0;
    snap.frames = m_counting ? m_counting->FrameDurationsUs().size() : 0;
    uint64_t total = 0;
    if (m_counting) {
        for (uint64_t us : m_counting->FrameDurationsUs()) total += us;
    }
    snap.frameUsTotal = static_cast<size_t>(total);
    return snap;
}

HarnessApp::SidebarCompare HarnessApp::CompareUi(const UiSnapshot& before,
                                                 const UiSnapshot& after) const {
    // 侧栏：直接复用 `CompareSessionList`（它会逐字段报 name/selected/buttons/geometry）。
    SidebarCompare side = CompareSessionList(before.sidebar, after.sidebar);
    if (!side.matched) {
        side.firstDivergence = "sidebar: " + side.firstDivergence;
        return side;
    }

    // 转录区：直接复用 `CompareRows`（`mismatches` 才是它的判据，`firstDivergence`
    // 由它自己的字段表产出）。★ 不用 blockOnly：整树重建要保住**全部**行。
    const RebuildProbeRec rows = CompareRows("ui", before.transcript, after.transcript, false);
    if (rows.mismatches != 0) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "transcript: " + rows.firstDivergence;
        return cmp;
    }

    // 输入框草稿。★ 这条最容易漏：`PromptBar` 的 TextBox 是**用户输入**，
    //   它不在 `timeline` 里、也不在任何 Snapshot 里，重建时若不显式搬运就会静默丢失。
    if (before.inputText != after.inputText) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "input: [" + before.inputText + "] -> [" + after.inputText + "]";
        return cmp;
    }

    // 状态标记：全部是**只存在于模型/句柄层**的事实，没有任何一个是"画出来的"。
    if (before.sidebarVisible != after.sidebarVisible) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = std::string("state: sidebarVisible [")
                              + (before.sidebarVisible ? "1" : "0") + "] -> ["
                              + (after.sidebarVisible ? "1" : "0") + "]";
        return cmp;
    }
    if (before.activeIndex != after.activeIndex) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "state: activeIndex " + std::to_string(before.activeIndex)
                              + " -> " + std::to_string(after.activeIndex);
        return cmp;
    }
    if (before.sessionCount != after.sessionCount) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "state: sessionCount " + std::to_string(before.sessionCount)
                              + " -> " + std::to_string(after.sessionCount);
        return cmp;
    }
    return SidebarCompare{};
}

bool HarnessApp::SwitchToSessionByPtr(const Session* target) {
    if (target == nullptr) return false;
    for (size_t i = 0; i < m_sessions.size(); ++i) {
        // ★ 只比地址、不解引用：这一行可能在会话刚被删掉、列表还没重建的那一帧里
        //   被点到（时序见 RebuildSessionList 头注的 UAF 段），此时 target 已悬垂，
        //   任何解引用都是 UB；比地址是安全的，查不到就什么都不做。
        if (m_sessions[i].get() == target) {
            SwitchToSession(i);
            return true;
        }
    }
    return false;
}

bool HarnessApp::DeleteSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    // ★ 禁止删到零会话：`m_sess` 变空指针会让全仓 200+ 处 `m_sess->` 解引用崩溃。
    //   "支持零会话"是一个独立的架构决定（每个调用点加判空），不该被一个删除按钮带进来。
    if (m_sessions.size() <= 1) return false;

    const bool deletingActive = (index == m_activeIndex);
    if (deletingActive) {
        // ★★ 顺序不能换：先把活动指针挪到邻居（SwitchToSession 内部会
        //    SyncRowViewState → 重建转录区 → 完成切换），**之后**才 erase。
        //    反过来的话，`SwitchToSession` 会在旧 Session 已被销毁的情况下
        //    去读它的 rows/timeline（那里存的是裸指针）⇒ UAF。
        const size_t neighbor = (index + 1 < m_sessions.size()) ? index + 1 : index - 1;
        SwitchToSession(neighbor);
    }

    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(index));

    // erase 把后面的元素整体前移 ⇒ 活动下标可能要跟着减一。
    // 注意 `m_sess` 本身仍指向**同一个 Session 对象**（unique_ptr 换的是所有权位置，
    // 不是对象地址），故这里只需修正下标；用下标重取一遍指针是防御性写法。
    if (m_activeIndex > index) --m_activeIndex;
    if (m_activeIndex >= m_sessions.size()) m_activeIndex = m_sessions.size() - 1;
    m_sess = m_sessions[m_activeIndex].get();

    // ★ 不在事件派发栈上拆树（见本段头注的 UAF 段）：只置脏，重建交给下一个 tick。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

// ★★ Step 2（2026-10-19）：侧栏显隐。
//   用户口径（m00001 第 2 步）：「最便宜的一个。但不要用 CollapsiblePanel 的 200ms
//   动画——中间帧会污染 frameCount/paints。用 SetVisible(false) + 父 Arrange() 瞬时切换」。
void HarnessApp::SetSidebarVisible(bool visible) {
    if (!m_hud.sidebar) return;
    if (m_sidebarVisible == visible && m_hud.sidebar->IsVisible() == visible) return;

    m_sidebarVisible = visible;          // ① 模型侧先落（重建后按它恢复）
    m_hud.sidebar->SetVisible(visible);  // ② 值变化时才 Invalidate（Widget.cpp:427-446）

    // ③ 重排父容器：布局**只在 Arrange 时**跳过隐藏子（C-VIS-1）把它们停泊到负区、
    //    并且不占主轴槽位与间隙 ⇒ 不 Arrange，中栏就吃不到那 200px。
    //    `HorizontalLayout` 里中栏 stretch=1 吃满余宽，故侧栏一藏，中栏立刻变宽 200。
    //    ★ Arrange 本身**不 Invalidate**（Widget.cpp 头注释）——重绘由 ② 的
    //      SetVisible 负责，这里不额外 RequestRepaint，避免虚增 invalidations。
    if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();

    // ④ 中栏变宽 ⇒ 转录行宽变 ⇒ 自动换行必须重算，否则文字仍按旧宽度折行，
    //    留在更宽的气泡里（看上去像"内容变短了"，一类只在视觉上暴露的缺陷）。
    Relayout();
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// 冻结活动会话的 timed 锚点：记下暂停时刻（幂等——已暂停则不动）。
void HarnessApp::PauseActiveTimedClock() {
    if (!m_sess || !m_options.timed) return;
    if (!m_sess->timedPaused) {
        m_sess->timedPaused = true;
        m_sess->timedPausedAt = std::chrono::steady_clock::now();
    }
}

// 恢复某会话的 timed 锚点：把锚点整体后移"被冻结了多久"。
//   ★ 为何需要补偿：elapsedMs = now - anchor。会话被切走期间 anchor 不动而 now 前进，
//     切回来若不补偿，elapsedMs 会凭空多出"离开的那段墙上时间" ⇒ 一次性抽干夹具。
void HarnessApp::ResumeSessionTimedClock(Session& s) {
    if (!m_options.timed) return;
    if (s.timedPaused) {
        const auto pausedFor = std::chrono::steady_clock::now() - s.timedPausedAt;
        s.timedAnchor += pausedFor;
        s.timedPaused = false;
    }
}

int HarnessApp::Run() {
    // ★ 0d：会话创建失败时 m_sess 为**空指针**（构造失败不再 push 坏会话），
    //   故先判空再解引用。
    if (!m_sess || !m_sess->source) {
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
    RefreshHud();  // Before hello, live controls are deliberately gated closed.
    uikit::WarnZeroSized(win.GetRootWidget());   // F2/D9 巡检：漏 SetSize 的 0×0 静默不可见 → 告警

    // P1.5 live：收到 core 的 hello 之后回 hello（协议 §4：seq 0）
    if (m_sess->writer) {
        m_sess->writer->Hello("0.0.1");
    }
    win.Show();
    if (m_promptBar && m_promptBar->input()) {
        win.SetFocusedWidget(m_promptBar->input());   // 打开即可打字（验收 A2）
    }

    win.GetPlatformWindow().StartTimer(kReplayTimer, 33);
    if (m_options.timed) {
        // ★ 0d：timed 锚点逐会话（协议 §8.2：ts 是唯一真相源）。
        //   放在 Session 里，否则新建/切回的会话会拿别人的锚点算 elapsedMs。
        m_sess->timedAnchor = std::chrono::steady_clock::now();
    }
    if (m_options.autoCloseMs > 0) {
        win.GetPlatformWindow().StartTimer(kCloseTimer,
                                           static_cast<unsigned int>(m_options.autoCloseMs));
    }
    MaybeSendAutoPrompt();  // Replay keeps its prior immediate note; live waits for workspace ack.
    // v3 auto-prompt waits for the core-confirmed initial workspace path.
    // ★ 2026-10-20：探针需要更长的安全超时，但**不得写回 `m_options`**——那是 CLI 选项，
    //   改写它等于让 `--auto-close` 在探针下静默失真（写摘要时读到的是被篡改后的值）。
    //   只重设计时器，选项本身不动。
    if (m_options.workspaceProbe && m_options.autoCloseMs < 90000) {
        win.GetPlatformWindow().StartTimer(kCloseTimer, 90000);
    }

    const int code = application.Run();

    // P1.5 live：关窗前发 bye（协议 §4：guest 即将关闭，请 core 退出）→ 关 stdin → 收尸。
    // ★ 0d：必须遍历**所有**会话——多会话下活动指针只指一个，而每个 live 会话
    //   各拉起过一个 core 子进程。只 Bye/Stop 活动会话 ⇒ 其余 core 变僵尸
    //   （这正是 P1.5 验收里"no zombies"那一项要防的）。
    for (auto& session : m_sessions) {
        if (session->writer) session->writer->Bye();
        if (session->child) session->child->Stop();
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
    // ★ Step 1：列表的**首帧**填充。放在 `root.Arrange()` 之前是安全的——
    //   `RebuildSessionList` 自己会把侧栏重排一遍（列表高度会变，下方分区要跟着让位）。
    //   ★ 这里**直接**调而不是置 `m_sidebarDirty`：此刻还没有窗口定时器，
    //     置脏要等第一个 tick 才生效 ⇒ 首帧会是空列表（用户会看到一闪而过的空栏）。
    RebuildSessionList();
    // ★ Step 5：历史区同样要首帧填上（同一理由：此刻还没有定时器，置脏要等一 tick）。
    RebuildHistoryList();
    // ★★ 2026-10-20：「项目」段同样要首帧填上（同理由）。
    RebuildProjectList();
    root.Arrange();
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建——运行期切主题的唯一可行路径。
//   【为什么非拆不可】`Theme.h` 的令牌是引用别名（`inline const Color& kSidebarBg =
//   g_theme.sidebarBg;`），而 ECDI 的 `PanelStyle`/`ButtonStyle` 是**值语义**：
//   `BuildUi` 那一刻把颜色拷进每个控件的 style，之后改 `g_theme` 不会让任何已存在
//   的控件换色（`Theme.h:19-20` 记的正是这条，`Run()` 里那句"必须在 BuildUi 之前"
//   是它的直接后果）。⇒ 要让树换色，只能让树按新令牌重新长一遍。
//   【为什么这是四步里唯一动仪器的】树的子节点之一就是 `CountingPanel`——`paints`
//   与 `frameCount` 的**唯一**来源。拆树会析构它，新面板从 0 开始数 ⇒ 不续接
//   计数器，一次主题切换就把"重绘 22 次"变成"重绘 3 次"，评估基线当场失效。
//   `CountingPanel::SeedCounters` 就是为此存在（它不做例外式的"永不重建"——
//   那会让判据退化成"除了那棵树都行"）。
void HarnessApp::RebuildUi() {
    if (!m_window) return;

    // ① 视图侧的用户状态先写回模型。`RebuildTranscriptView` 内部也会调一次，
    //    但这里动的是**整棵树**（连 `m_scroll` 都要换），必须在拆之前再保一次，
    //    否则行的展开/已答会跟着旧控件一起消失（0c 的 `buildTitle` 就是这类回退）。
    SyncRowViewState();
    m_pendingPromptViews.clear();  // old UI widgets are about to be destroyed; Session models survive.

    // ② 评估计数器快照。**必须在拆树之前**读——旧面板一析构就读不到了。
    const size_t paints = m_counting ? m_counting->PaintCount() : 0;
    std::vector<uint64_t> frameUs;
    if (m_counting) frameUs = m_counting->FrameDurationsUs();

    // ③ 用户输入与焦点。输入文本是**唯一无法从模型重建**的界面状态
    //    （`TimelineEntry` 只描述已发生的转录，不描述还没发出去的草稿）。
    //    ★ 光标位置无公开写入点（`TextBox` 只有 `MoveCaret`/`MoveCaretToEnd`，
    //      没有 `SetCaret(size_t)`）⇒ 只能恢复到"末尾"，这是本步的**已知损失**。
    const std::string draft = (m_promptBar && m_promptBar->input())
                                  ? m_promptBar->input()->GetText()
                                  : std::string();

    // ④ 拆掉 root 的全部子节点。`RemoveChild` 走 `Window::ForgetSubtree`
    //    （焦点/捕获/hover 一并注销）⇒ 拆完窗口处于"无焦点"状态，这正是
    //    `Application::OnTimer` 会在下一个 tick 直接 return 的原因
    //    （定时器只派给焦点控件，`Application.cpp:303-339`）——**所以后面必须重设焦点**，
    //    否则回放 tick 停摆、探针再也跑不动。这是本步的头号结构性风险。
    ECDI::Widget& root = m_window->GetRootWidget();
    while (root.GetChildCount() > 0) {
        root.RemoveChild(root.GetChildAt(0));
    }

    // ⑤ 让 `g_theme` 与 `g_themeLight` 重新对齐（`SwitchTheme` 只是重新赋值，
    //    幂等），然后按新令牌把整棵树建出来。
    harness::SwitchTheme(harness::g_themeLight);
    m_window->SetBackgroundColor(m_options.probeContrast ? kProbeCanvas : harness::kContentBg);
    BuildUi(*m_window);

    // ⑥ 续接计数器。`BuildUi` 刚建出的新面板计数为 0；把 ①② 的快照写回去，
    //    于是"重建"在评估读数上是**透明的**（paints 一位不变）。
    if (m_counting) {
        m_counting->SeedCounters(paints, std::move(frameUs));
    }

    // ⑦ 转录区从模型重建。`BuildUi` 只建了空容器（启动时转录本来就是空的），
    //    运行期重建必须自己把 `timeline` 铺回去——`RebuildTranscriptView` 做的就是
    //    这件事（它同时把滚动位置保持在原处）。
    RebuildTranscriptView();

    // ⑧ 恢复草稿。`SetText` 不触发 `RaiseTextChanged`（`TextBox.cpp:121-141`），
    //    故不会误触发任何发送逻辑；`MoveCaretToEnd` 是唯一可用的光标落点。
    if (m_promptBar && m_promptBar->input()) {
        if (!draft.empty()) {
            m_promptBar->input()->SetText(draft);
            m_promptBar->input()->MoveCaretToEnd();
        }
        // ⑨ 焦点交还输入框：既恢复 caret 闪烁（证明焦点真的回来了），
        //    也让 `Application::OnTimer` 重新有派发目标（否则 tick 停摆）。
        m_window->SetFocusedWidget(m_promptBar->input());
    }

    // ⑩ 侧栏显隐是**模型侧状态**（`m_sidebarVisible`），新树默认全可见 ⇒ 按它恢复。
    if (!m_sidebarVisible && m_hud.sidebar) {
        m_hud.sidebar->SetVisible(false);
        if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();
    }

    // ⑪ 收尾：环进度条的颜色级别缓存要作废（新控件 + 新令牌），HUD 全量刷一次。
    m_ctxRingLevel = -1;
    ++m_uiRebuilds;
    Relayout();
    FollowTailIfPinned();
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

// 顶栏 HUD（hi-fi .titlebar）：≡ | harness | 会话 chip | 后端 chip | … | 行模式 chip | 圆点+状态
void HarnessApp::BuildTopBar(ECDI::Widget& root) {
    auto bar = std::make_unique<ECDI::Panel>();
    bar->SetSize(1000, 36);
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = harness::kDarkTopBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    bar->SetLayout(std::make_unique<ECDI::HorizontalLayout>(10, true, 8));

    // ★★ Step 2（2026-10-19）：`≡` 原本是 `Label`——**点不到**（Label 没有
    //   OnMouseButtonUp，基类 `Widget` 的所有鼠标回调都是空实现，
    //   `Widget.cpp:405-421`）。改成 Button 才有回调。
    //   ★ 视觉必须与原来的 Label 一致：22×20、kDarkMuted、SansFont(14)。
    //     `Label` 基线是"水平左对齐+垂直居中"（`TextWidget::CalculateTextPosition`
    //     的默认），而 Button 的水平位置由 `ButtonStyle.textAlignment` 驱动、主题
    //     缺省是 **Center**（ButtonStyle.h:36 的 C30-3）——所以这里显式写 Left，
    //     否则字形会从 22px 宽的左边缘挪到中间，与改前的截图对不上。
    auto menu = std::make_unique<ECDI::Button>("≡");
    menu->SetTextColor(kDarkMuted);
    // ★ 不调 SetFont：原 `Label("≡")` 用的是**主题默认字体**（TextWidget 的
    //   TextStyle 缺省），Button 的 `ApplyTheme` 同样先走 `TextWidget::ApplyTheme`
    //   ⇒ 不覆盖就与改前同源。这里曾经多写了一行 `SansFont(14.0f)`，截图逐像素
    //   比对时发现字形整体下移 2px、笔画变短（y=14/17/20 → 16/19/22）——
    //   `SansFont` 注入的是 `g_theme.sansFamily`，与主题缺省 family 不是同一个字体。
    menu->SetStyle(ECDI::ButtonStyleOverride{
        .background = harness::kDarkTopBg,   // 与顶栏同色 ⇒ 看起来仍是裸字形
        .border = harness::kDarkTopBg,
        .borderWidth = 0.0f,
        .cornerRadius = 0.0f,
        // ★ hover/pressed 必须显式钉成与 background 同色：不写就落回主题缺省，
        //   鼠标掠过左上角会闪一块与顶栏不同的色，还会触发 200ms 背景动画
        //   （`Button::AnimateBackgroundTo` 只在目标 != 当前时启动 ⇒ 同色即零动画、
        //   零额外重绘；这条直接决定 paints/invalidations 读数是否被污染）。
        .pressedBackground = harness::kDarkTopBg,
        .hoverBackground = harness::kDarkTopBg,
        .textAlignment = ECDI::TextAlignment::Left});
    menu->SetSize(22, 20);
    m_hud.sidebarToggle = menu.get();
    // ★ 点完把焦点还给输入框：`Button::CanFocus() == true` ⇒ 派发链里
    //   `Application::OnMouseButtonDown` 已经 `SetFocusedWidget(target)`，
    //   于是输入框失焦 ⇒ **caret 停闪、边框从聚焦色变回常态色**。
    //   截图逐像素比对抓到的正是这条（展开态 vs 再展开态差 1480 px，全部落在
    //   输入框上下边框那一行 `#e86d59` → `#e9e3e0`）。
    //   `≡` 是**窗体级开关**，不接收文本输入，没有理由拿走键盘焦点。
    menu->SetOnClick([this] {
        SetSidebarVisible(!m_sidebarVisible);
        if (m_window && m_promptBar) {
            m_window->SetFocusedWidget(m_promptBar->input());
        }
    });
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

    // ★★ Step 4 / 0e（2026-10-19）：运行期主题切换控件。
    //   【为什么必须放在顶栏】它是**全局**状态的中枢，不是某个面板的属性；
    //   侧栏是会话的容器、检查器是读数的容器，都不该承载"整个程序长什么样"。
    //   【为什么是 Button】切换的唯一正确路径是"点一下 → 回调 → `RebuildUi`"。
    //   若做成 Label 加别处的热键，就没法用合成点击验证它——`RunThemeProbe` 的
    //   `toggle` 相走的正是 `SynthesizeClickAt` → 真实派发链 → `RaiseClick`。
    //   【为什么这里可以调 SetFont 而 `≡` 不行】`≡` 的 bug 是"用了与主题缺省不同
    //   的 family 导致字形漂移"；本钮显式指定 `SansFont`，两侧（深/浅）都按同一
    //   规则现算，不存在"和谁不一样"的问题。
    auto themeBtn = std::make_unique<ECDI::Button>("主题");
    m_hud.themeToggle = themeBtn.get();
    themeBtn->SetTextColor(kDarkInk);
    themeBtn->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    themeBtn->SetSize(48, 20);
    themeBtn->SetFont(harness::SansFont(harness::kFontSmall));
    themeBtn->SetOnClick([this] {
        // 真翻转在回调里做（只改 `g_theme` 与 `g_themeLight`，不碰任何控件）。
        harness::SwitchTheme(!harness::g_themeLight);
        // ★ **绝不在回调里重建**：这个 lambda 正在 `Button::RaiseClick` 的栈上执行，
        //   而重建会 `root.RemoveChild` 掉**当前这个按钮**——那就是拿正在执行的
        //   对象去析构它自己（与 Step 1 的 `m_sidebarDirty` 完全同族，见其头注）。
        //   ECDI Phase 35 的 `RequestRemove` 延迟析构只保护走它那条路径的调用；
        //   `RemoveChild` 是同步析构，必须由我们保证不在派发栈上调用。
        m_uiRebuildPending = true;
    });
    bar->AddChild(std::move(themeBtn));

    root.AddChild(std::move(bar));
}

// 主区（hi-fi .main）：左栏（新建会话/会话/回放/工作区）+ 中栏（转录 + Composer）+ 右栏检查器
void HarnessApp::BuildMainArea(ECDI::Widget& root) {
    auto main = std::make_unique<ECDI::Panel>();
    main->SetStretch(1);
    main->SetLayout(std::make_unique<ECDI::HorizontalLayout>(0, true, 0));

    // ── 左栏（hi-fi .side）──
    auto side = std::make_unique<ECDI::Panel>();
    m_hud.sidebar = side.get();   // Step 2：显隐入口要能摸到侧栏本体
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
        // ★★ 0d：不再是「仅记录」——真的新建一个会话并切过去。
        //   live 模式拉起新 core；回放模式建空会话并如实提示（不伪造转录）。
        NewSession();
    });
    side->AddChild(std::move(newSess));

    // ★ 2026-10-18 原型 C 形态：分区标签 10px/700 letter-spacing ⇒ 落地为
    //   **11px + muted + 不加空格**（中文不进 10px：§3.3 实测会糊；`"会 话"`
    //   那种手撒空格是伪装 letter-spacing，等宽观感下反而散）。
    //   ★ 分区名 `工作区`/`项目` 直接沿用 C 的措辞（C 左侧第一段就叫「工作区」）。
    AddLine(*side, "工作区", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto path = std::make_unique<ECDI::Label>("等待 core 确认实际路径");
        m_hud.workspacePath = path.get();
        path->SetTextColor(kDarkInk);
        path->SetFont(harness::MonoFont(10.0f));
        path->SetWordWrap(true);
        path->SetSize(172, 34);
        side->AddChild(std::move(path));

        auto status = uikit::Label("未连接", 172, 14, kDarkMuted, harness::kFontMicro);
        m_hud.workspaceStatus = status.get();
        side->AddChild(std::move(status));

        auto pick = std::make_unique<ECDI::Button>("选择文件夹…");
        m_hud.workspacePick = pick.get();
        pick->SetTextColor(kDarkInk);
        pick->SetFont(harness::SansFont(harness::kFontSmall));
        pick->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .hoverBackground = kToolHeaderHover});
        pick->SetSize(172, 26);
        pick->SetOnClick([this] { ChooseWorkspaceFolder(); });
        side->AddChild(std::move(pick));

        // ★★ 2026-10-20（m09704）：目录列表收进「工作区」段（原「项目」段）。
        //   路径只出现一次，目录内容紧跟其后，语义 = 「这个工作区在哪 + 里面有什么」。
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.projectList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }

    // ★★ Step 1（2026-10-19）：会话列表替换掉此前**单行硬编码**的会话条目。
    //   改动前的形态是一行 `uikit::Row` + 两个 Label（id / core 版本），只能显示
    //   活动会话——`m_sessions` 里有 N 个会话时，界面上永远只看得见 1 个。
    //   ⇒ 现在是 `m_sessions → 控件树` 的**纯函数渲染**（`RebuildSessionList`），
    //     点一行切会话、行尾 `×` 删会话、选中态由组件自身可见性表达。
    //   ★ 为什么用 `ScrollView` 而不是直接往 `side` 里塞行：会话数无上限，
    //     而侧栏是定宽定高的列。列表自身滚动比"整个侧栏滚动"正确得多
    //     （后者的「工作区/项目」分区会跟着一起滚走）。
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.sessList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }
    {
        // 计数行：`2 个会话`。这一行是"侧栏到底画了几个"的**文字读数**——
        // 它和列表同源于 m_sessions，任意一侧错位都会立刻显形（探针也读它）。
        // ★ 直接挂在 side 下（不经 Row 包一层）：它是单标签，多一层容器只会
        //   在 VerticalLayout 里多占一个槽位、多一点间距。
        auto cnt = uikit::Label("1 个会话", kSidebarListW, 14, kDarkMuted, harness::kFontMicro);
        m_hud.sessCount = cnt.get();
        side->AddChild(std::move(cnt));
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


    // ── ★★ Step 5：历史会话（磁盘上的夹具 = 可打开的文件）──
    //   ★ 这一段与上面「工作区」里的会话列表是**两种东西**：上面是已打开的会话，
    //     这里是**还没打开的文件**。点一行会多出一个已打开会话。
    //   ★ 只列不删：`protocol/fixtures/` 下每个文件都有 sha256 基线，
    //     删除 UI 一旦能碰它们，所有回归基线就都开口子了。
    AddLine(*side, "历史", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.historyList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        side->AddChild(std::move(list));
    }
    {
        auto cnt = uikit::Label("0 个历史文件", kSidebarListW, 14, kDarkMuted,
                                harness::kFontMicro);
        m_hud.historyCount = cnt.get();
        side->AddChild(std::move(cnt));
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
        auto mkAct = [&head, this](const std::string& text, const std::string& tip,
                                   bool toggleInspector = false) {
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
            if (toggleInspector) m_hud.inspectorToggle = b.get();
            b->SetOnClick([this, tip, toggleInspector] {
                (void)tip;
                if (!toggleInspector || !m_hud.inspector) return;
                m_inspectorVisible = !m_inspectorVisible;
                m_hud.inspector->SetVisible(m_inspectorVisible);
                if (ECDI::Widget* parent = m_hud.inspector->GetParent()) parent->Arrange();
                Relayout();
                RequestRepaint();
            });
            head->AddChild(std::move(b));
        };
        mkAct("↗", "分享");
        mkAct("≡", "检查器", true);
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
        if (m_sess->writer) {
            const bool ok = m_sess->writer->Cancel();
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
    m_hud.inspector = insp.get();
    insp->SetVisible(m_inspectorVisible);
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
    // ★★ Step 1：侧栏列表的**唯一重建点**。
    //   为什么必须在 `if (!m_sess->source) return;` **之前**：回放模式新建的空会话
    //   `source == nullptr`，本函数会在下一行直接 return —— 把重建放在后面就意味着
    //   "新建会话后侧栏永远不刷新"（列表说 1 个、模型已经 2 个）。
    //   为什么放在 tick 里而不是回调里：见 `m_sidebarDirty` 的声明注释（派发栈 UAF）。
    //   ★ timer 回调不在事件派发栈上 ⇒ 此刻 `RemoveChild` 拆树是安全的。
    if (m_sidebarDirty) {
        m_sidebarDirty = false;
        RebuildSessionList();
        // ★★ 2026-10-20：「项目」段与「会话列表」同一条链：活动会话变了
        //   ⇒ 工作区可能变了 ⇒ 目录内容要跟着刷。
        RebuildProjectList();
        RequestRepaint();
    }
    // ★★ Step 4：整树重建的**唯一执行点**（同族的派发栈 UAF 理由见
    //   `m_uiRebuildPending` 的声明注释）。放在 `if (!m_sess->source) return;`
    //   **之前**：空会话（回放模式新建的）也要能换主题。
    //   ★ 顺序上先于侧栏重建之后无所谓——两者不会同时置脏（同一 tick 内至多一个来源），
    //     但若真同时发生，整树重建会把旧列表一起丢掉，故它必须在后。
    if (m_uiRebuildPending) {
        m_uiRebuildPending = false;
        RebuildUi();
        RequestRepaint();
    }
    // ★★ Step 5：历史区在 tick 里重建（行数是扫盘来的，回调里不能扫也不能拆树）。
    if (m_historyDirty) {
        m_historyDirty = false;
        RebuildHistoryList();
        RequestRepaint();
    }
    // ★★ Step 5：打开历史会话（延迟到此：派发栈已退，拆树/抽干都安全）。
    if (!m_historyOpenPending.empty()) {
        const std::filesystem::path file = m_historyOpenPending;
        m_historyOpenPending.clear();
        OpenHistorySession(file);   // 内部会置 m_sidebarDirty + RequestRepaint
    }
    if (!m_sess->source) {
        if (m_options.workspaceProbe && !m_workspaceProbeDone) RunWorkspaceProbe();
        return;   // 源启动失败（Run 已报错退出路径）/ 0d 空会话：无可拉取
    }
    if (m_options.workspaceProbe && !m_sess->source->IsLive()) {
        RunWorkspaceProbe();  // Probe requires a live core; report unavailable instead of hanging on replay.
        return;
    }
    if (m_sess->source->IsLive()) {
        // ── live（子进程）：本 tick 内带超时批量拉取，把时间还给消息循环 ──
        // （v1 定案：UI 线程仍做拉取，但**不无限阻塞**——每 tick 8ms 预算，
        //   否则 WM_PAINT 饿死、窗口全黑；P2 改用 PostToUi 异步投递）
        bool gotAny = false;
        const int budgetMs = 8;
        std::string line;
        while (m_sess->child->NextWithin(line, gotAny ? 0 : budgetMs)) {
            ++m_sess->liveLines;
            HandleLine(line);
            gotAny = true;
        }
        if (gotAny) {
            Relayout();
            FollowTailIfPinned();
            CacheEvalMetrics();
            RefreshHud();
            RequestRepaint();
        } else if (m_sess->child->Eof() && !m_sess->liveEof) {
            m_sess->liveEof = true;   // core 退出（stdin EOF 或 bye 收尾）
            FailPendingOperations("core EOF / 断连");
            m_hudDirty = true;
            RefreshHud();
        }
        if (m_options.workspaceProbe && !m_workspaceProbeDone) RunWorkspaceProbe();
        return;
    }

    if (m_sess->replay->Empty() && !m_sess->waitForAnswer) {
        if (m_sess->replaySettled) {
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
            // ★★ Step 0c 重建探针同理：夹具拉完后本函数从这里 `return`，走不到末尾。
            //   第一版只放末尾 ⇒ 摘要里连 `rebuildProbe` 键都没有（探针一步没跑）。
            if (m_options.rebuildProbe && !m_rebuildProbeDone) {
                RunRebuildProbe();
            }
            // ★★ Step 0d 会话探针：同样放在 settle 分支（回放模式下夹具拉完才跑）。
            if (m_options.sessionProbe && !m_sessionProbeDone) {
                RunSessionProbe();
            }
            // ★★ Step 1 侧栏探针：同上（同步跑完即 Exit，故必须排在其他探针之后，
            //   否则它先 Exit 会让别人一步都跑不了。顺序：reasoning → rebuild → session → sidebar）。
            if (m_options.sidebarProbe && !m_sidebarProbeDone) {
                RunSidebarProbe();
            }
            // ★★ Step 3 答案探针：排在最后（同样同步跑完即 Exit）。
            //   顺序：reasoning → rebuild → session → sidebar → answer。
            if (m_options.answerProbe && !m_answerProbeDone) {
                RunAnswerProbe();
            }
            // ★★ Step 4 主题/整树重建探针：排在最后。
            //   ★ 与前五个不同，它是**跨 tick** 的（toggle 相要点真实按钮，
            //     而重建被延迟到下一个 tick）⇒ 它**不能**和上面的同步探针同 tick 共存：
            //     上面任何一个一旦 Exit，本探针就再也拿不到下一个 tick。
            //     故六条互斥使用（同一进程只开一个 `--*-probe`）。
            if (m_options.themeProbe && !m_themeProbeDone) {
                RunThemeProbe();
            }
            // ★★ Step 5 历史会话探针：排在最后，同为**跨 tick**（点历史行后，
            //   打开被延迟到下一个 tick——派发栈 UAF）。
            //   顺序：reasoning → rebuild → session → sidebar → answer → theme → history。
            //   七条互斥使用。
            if (m_options.historyProbe && !m_historyProbeDone) {
                RunHistoryProbe();
            }
            return;
        }
        m_sess->replaySettled = true;
    }
    if (m_options.timed) {
        DrainTimed();
    } else {
        DrainInstant();
    }
    Relayout();
    FollowTailIfPinned();
    if (m_sess->replaySettled && m_options.scrollSweepStep > 0 && !m_sweepStarted && m_scroll) {
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
    if (m_options.hitProbe && m_sess->replaySettled && !m_hitProbeDone) {
        m_hitProbeDone = true;
        RunHitProbe();
    }
    // ★ 思维链折叠行为取证（协议 v2 §7）：跨 tick 状态机（每次只推进一步，
    //   步间等过 200ms 折叠动画）⇒ 不能像 --hit-probe 那样一次跑完。
    if (m_options.reasoningProbe && m_sess->replaySettled && !m_reasoningProbeDone) {
        RunReasoningProbe();
    }
    // 注：`--rebuild-probe` 的推进在**上面 settle 分支里**（本函数到尾就 `return` 了，
    //   这里再写一遍是死代码；第一版正是写在这里，所以探针一步都没跑）。
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
    if (m_sess->instantDone) return;
    m_sess->instantDone = true;
    std::string line;
    // ★ 协议 §9：版本不兼容 ⇒ **停止消费**（"拒绝连接"的实质）。
    //   只在 `HandleLine` 返回后检查：`Exit()` 只让消息循环退出，**不打断本循环**，
    //   而 `--instant` 会在同一 tick 抽干整个夹具 ⇒ 若不在此处 break，
    //   "拒绝"就只是打印一行提示、随后照常把整条流处理完（实测：protocol:3 的
    //   夹具仍读到 blocks=44）——那不是拒绝，是"边抱怨边照做"。
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);   // 协议 §1.2：截断必须显式报错
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::DrainTimed() {
    if (m_sess->waitForAnswer) return;   // I3：ask 未答前 core 不再发消息
    if (m_sess->protocolIncompatible) return;   // ★ 协议 §9：版本不兼容 ⇒ 不再消费
    // ★ 0d：会话被切走（冻结）期间不拉取——它不在屏上，"到期"没有意义。
    if (m_sess->timedPaused) return;
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  m_sess->timedAnchor)
            .count();
    std::string line;
    while (m_sess->replay->NextDue(elapsedMs, line)) {
        HandleLine(line);
        if (m_sess->waitForAnswer) break;   // ask 已显示，停在 waiting（协议 §8.3）
        if (m_sess->protocolIncompatible) break;   // ★ 协议 §9：版本不兼容 ⇒ 停止消费
    }
    if (m_sess->replay->Empty() && !m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::HandleLine(const std::string& line) {
    std::string notice;
    const bool ok = m_sess->transcript.OnLine(line, notice);
    if (!notice.empty()) AddNoticeRow(notice, kErrorDark);   // I4 / I5 等显式提示
    if (ok) RouteLastMessage();
}

void HarnessApp::RouteLastMessage() {
    const json::Value& msg = m_sess->transcript.LastMessage();
    const std::string t = msg.GetString("t");

    if (t == "block_start") {
        if (const Block* block = m_sess->transcript.FindBlock(msg.GetString("id"))) {
            if (block->kind == "user" && !block->clientId.empty()) {
                const std::string clientId = block->clientId;
                for (auto it = m_pendingPromptViews.begin(); it != m_pendingPromptViews.end(); ++it) {
                    if (it->clientId == clientId) {
                        if (m_content && it->row.panel) m_content->RemoveChild(it->row.panel);
                        m_pendingPromptViews.erase(it);
                        break;
                    }
                }
                auto& pending = m_sess->pendingPrompts;
                pending.erase(std::remove_if(pending.begin(), pending.end(),
                    [&](const PendingPrompt& p) { return p.clientId == clientId; }), pending.end());
            }
            CreateBlockRow(*block);
        }
        return;
    }
    if (t == "delta") {
        const std::string id = msg.GetString("id");
        const auto row = m_sess->blockRows.find(id);
        const Block* block = m_sess->transcript.FindBlock(id);
        if (row != m_sess->blockRows.end() && block && row->second->label) {
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
        const Block* block = m_sess->transcript.FindBlock(id);
        const auto row = m_sess->blockRows.find(id);
        if (row == m_sess->blockRows.end() || !row->second->headerBtn) {
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
            if (const Block* call = m_sess->transcript.FindBlock(block->forId)) {
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
        const std::string clientId = msg.GetString("client_id");
        const std::string message = msg.GetString("message", "core error");
        const json::Value* fatalField = msg.Find("fatal");
        // ★ 协议 §3.8：`fatal` 缺省 = **false**。缺字段不是「致命」——§9 的宽容原则
        //   （「宽容比拒连更安全」）同样适用于这里：一个漏写字段的 core 不该被判死，
        //   真致命的 core 会自己退出，EOF 仍会兜住。
        const bool fatal = fatalField && fatalField->IsBool() && fatalField->boolean;
        const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
            m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                return p.clientId == clientId && p.status == "sending";
            });
        const bool correlated = !clientId.empty() && pending != m_sess->pendingPrompts.end();
        if (correlated) FailPrompt(clientId, message);
        if (fatal) {
            FailPendingOperations(message);
            AddNoticeRow("[fatal error] " + message, kErrorDark);
        } else if (!correlated) {
            AddNoticeRow("[error] " + message, kErrorDark);
        }
        return;
    }
    if (t == "hello") {
        m_sess->sessionId = msg.GetString("session");
        m_sess->coreVer = msg.GetString("core");
        const json::Value* protocol = msg.Find("protocol");
        m_sess->protocolVer = protocol && protocol->IsNumber()
            ? static_cast<int>(protocol->number) : 0;
        const json::Value* caps = msg.Find("caps");
        bool capsValid = caps && caps->IsArray();
        bool hasWorkspace = false;
        std::string joined;
        if (capsValid) {
            for (const auto& cap : caps->array) {
                if (!cap.IsString()) {
                    capsValid = false;
                    break;
                }
                if (!joined.empty()) joined += ", ";
                joined += cap.text;
                if (cap.text == "workspace") hasWorkspace = true;
            }
        }
        m_sess->capsStr = capsValid ? joined : std::string();
        m_sess->workspaceCap = capsValid && hasWorkspace;
        // ★★ 协议 §9：只有 core 声明的主版本**高于**本 GUI 上限才拒连（不做降级猜测）。
        //   缺字段按 v1 宽容；**低于**上限从来不是拒绝理由（§9 无此条）。
        if (m_sess->protocolVer > kProtocolMaxSupported) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "[协议不兼容] core 声明 protocol %d，本 GUI 支持到 %d"
                          " —— 按 §9 拒绝连接（不做降级猜测）",
                          m_sess->protocolVer, kProtocolMaxSupported);
            AddNoticeRow(buf, kErrorDark);
            m_sess->protocolIncompatible = true;
            m_sess->disconnected = true;
            m_hudDirty = true;
            if (m_application) m_application->Exit();   // 不进入渲染路径
            return;
        }
        // ★★ 协议 v3 §3.1：caps 缺 `workspace` **不是**拒连条件，而是「GUI 不得显示
        //   工作区选择器」⇒ 隐藏整段工作区交互，但**连接与发送照常**。
        //   ★ 2026-10-20 修正：旧实现把 `!workspaceCap` 也写进上面的拒连条件，
        //     而两份 hostile 夹具的 hello 恰好是 `protocol:3` + caps 无 workspace
        //     ⇒ 它们一行都没被解析（blocks=0/violations=0），I1/I4/I5 的负对照
        //     全部空转。规范从没有「必须声明 workspace 才能连接」这一条。
        if (!m_sess->workspaceCap) {
            m_sess->workspaceStatus = "core 未声明 workspace 能力";
        }
        if (const json::Value* ctx = msg.Find("context");
            ctx && ctx->IsNumber() && ctx->number > 0) {
            m_sess->contextLimit = static_cast<long long>(ctx->number);
        }
        m_hudDirty = true;
        AddNoticeRow("core " + msg.GetString("core") + " · session " +
                         msg.GetString("session") + " · protocol " +
                         std::to_string(m_sess->protocolVer), kDarkMuted);
        // The v3 core starts in idle and does not emit a redundant initial state line.
        if (m_sess->coreState.empty()) m_sess->coreState = "idle";
        MaybeStartWorkspaceSync();
        return;
    }
    if (t == "state") {
        m_sess->coreState = msg.GetString("value");
        m_hudDirty = true;
        if (m_sess->coreState == "idle") MaybeStartWorkspaceSync();
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "workspace") {
        const std::string requestId = msg.GetString("request_id");
        const json::Value* requestField = msg.Find("request_id");
        if (!requestField || !requestField->IsString() || requestId.empty()) {
            if (!m_sess->pendingWorkspaceId.empty()) {
                const bool initialSync = m_sess->pendingWorkspaceInitialSync;
                m_sess->pendingWorkspaceId.clear();
                m_sess->pendingWorkspaceInitialSync = false;
                m_sess->workspaceStatus = "工作区确认无效：缺少 request_id";
                if (initialSync && !m_sess->workspaceSyncComplete) {
                    m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
                }
                RefreshHud();
            }
            AddNoticeRow("[workspace ack 缺少 request_id]", kErrorDark);
            return;
        }
        if (m_sess->pendingWorkspaceId.empty() || requestId != m_sess->pendingWorkspaceId) {
            AddNoticeRow("[workspace ack 不匹配] " + requestId, kErrorDark);
            return;
        }
        const bool initialSync = m_sess->pendingWorkspaceInitialSync;
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        const json::Value* ok = msg.Find("ok");
        if (!ok || !ok->IsBool()) {
            m_sess->workspaceStatus = "工作区确认无效：ok 必须是 boolean";
        } else if (ok->boolean) {
            const json::Value* path = msg.Find("path");
            if (!path || !path->IsString() || path->text.empty()) {
                m_sess->workspaceStatus = "工作区确认无效：缺少实际路径";
            } else {
                m_sess->workspacePath = path->text;
                m_sess->workspaceSyncComplete = true;
                m_sess->workspaceStatus = "已由 core 确认";
                // ★★ 2026-10-20：确认成功 = 「这个会话现在属于这条路径那个工作区」。
                //   ★ 注册只发生在这里（不在请求时）：工作区的路径定义是 **core 回显的
                //     规范化结果**，不是 GUI 自己发出去的那个字符串。在请求时注册就会
                //     把一个未经确认的路径写进注册表，而“注册表里的路径”正是侧栏
                //     告诉用户「这个会话在哪」的唯一依据——那等于自己给自己发证据。
                //   ★ `AcquireWorkspace` 按路径去重：两个会话被确认到同一目录时共享
                //     同一个工作区实体（侧栏不会出现两个同路径的标题行）。
                //   ★ 归属变了 ⇒ 侧栏的**分组结构**也变了，必须重画（不是只刷 HUD 的
                //     两行文字）。`m_sidebarDirty` 只在 tick 顶部被消费（OnReplayTick），
                //     而本分支正好就在 tick 里（HandleLine ← OnReplayTick），不在派发栈上，
                //     置脏是安全的。
                //     漏了这一行的实测症状：新会话的 ack 到达后侧栏**从不重建**，
                //     于是一个已经确认到路径的会话永远挂在“未归属工作区”下面，
                //     而顶部的工作区块却正确地显示着同一个路径——自相矛盾的界面。
                AssignSessionWorkspace(*m_sess, AcquireWorkspace(path->text));
                m_sidebarDirty = true;
            }
        } else {
            m_sess->workspaceStatus = "切换失败：" + msg.GetString("message", "core 拒绝请求");
        }
        if (initialSync && !m_sess->workspaceSyncComplete) {
            // Initial sync failure leaves the GUI gated until an explicit folder request succeeds.
            m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
        }
        m_hudDirty = true;
        RefreshHud();
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "usage") {
        m_sess->usageTurn = static_cast<int>(msg.GetNumber("turn"));
        m_sess->usageIn = static_cast<long long>(msg.GetNumber("input"));
        m_sess->usageOut = static_cast<long long>(msg.GetNumber("output"));
        m_hudDirty = true;
        return;
    }
    if (t == "bye") {
        FailPendingOperations("core 已发送 bye");
        AddNoticeRow("会话结束 (" + msg.GetString("reason") + ", code " +
                         std::to_string(static_cast<int>(msg.GetNumber("code"))) + ")",
                     kDarkMuted);
        return;
    }
    // state / usage：上移到 HUD，转录不渲染
}

// ── 行构建（规格 §5.2 渲染映射）────────────────────────────────

void HarnessApp::AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                        const std::string& blockId, size_t timelineIdx) {
    RowRec stored = rec;
    stored.panel = row.get();
    // ★★ 0c：行与时间线条目**成对**登记。下标由调用方**显式给出**而不是取
    //   `rows.size()`——重建时若某条目建不出行（块缺失即跳过），两者就会错位，
    //   而"视图状态写回模型"依赖这个下标指对地方（错位 = 把 A 的展开态写到 B 上）。
    stored.timelineIdx = timelineIdx;
    if (m_options.rowModeList) {
        stored.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
    }
    m_sess->rows.push_back(stored);
    RowRec& ref = m_sess->rows.back();
    m_content->AddChild(std::move(row));
    if (!blockId.empty()) m_sess->blockRows[blockId] = &ref;
    MaterializePendingPrompts();  // keep transient rows physically after canonical rows.
    // settle 后新增的行（发送/停止/回答反馈）没有逐 tick 的 Relayout 可蹭——
    // 排一个补重排脉冲，让定时器把它布局出来
    m_relayoutPulse = std::max(m_relayoutPulse, 2);
}

// ★★ 0c（2026-10-19）：行构造拆成**两层**。
//   `BuildBlockRow` = **纯函数**：只吃 `Block` 值 + `m_options`，产出控件树并填 `rec`；
//     不追加时间线、不发协议、不写全局状态 ⇒ **可以被重放任意多次**。
//   `CreateBlockRow` = 增量入口：建行 → 追加时间线条目 → 绑回调 → 探针捕获。
//   为何必须拆：重建视图要能"把同一个块再画一次"，而增量入口带副作用（时间线会翻倍）。
//   为何拆得开：本分支历来只读 `block` 的字段（不读会话状态），故纯化是**零成本**的。
//
//   ★ 重建保真度的一个隐含要求：**标题必须是 `block` 当前值的函数**。
//     增量路径里标题由两段代码写（`block_start` 建行时给初值、`block_end` 到达时改写），
//     重建只有一次机会 ⇒ 那两段必须收敛成同一个函数，否则重建出来的是「初值标题」
//     （工具卡会永远停在「等待结果」，而这正是历史上那个误导 bug 的形态）。
//     故 `ToolTitle()` 收口两处，`ReasoningTitle()` 早已是同款收口。
std::string ToolTitle(const Block& block, const Block* call) {
    std::string base;
    if (block.kind == "tool_call") {
        base = block.name.empty() ? "tool_call" : block.name;
    } else if (!block.ended) {
        base = "等待结果";            // 结果块还没结束：不知道是哪个工具的结论
    } else if (call && !call->name.empty()) {
        base = call->name;            // 结果块：工具名来自 forId 引用的 call 块
    } else {
        base = "tool";
    }
    if (block.ended) {
        // ★ REQ-02（2026-10-13）：`summary` 曾在此被吞——`tool_call` 的 `block_end`
        //   也会带 `summary`（协议 §3.5 允许），两类都必须落地。
        if (!block.summary.empty()) {
            base += " · " + block.summary;
        } else if (!block.ok) {
            base += " · 失败";
        }
    }
    // ★ 2026-10-18：`▸`(U+25B8) 两主题 face 都无字形 ⇒ `›`(U+203A)（同右向尖括号语义）
    return "› " + base;
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildBlockRow(const Block& block, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();

    // ★ 0c：**每一**条块行都登记自己的块 id（此前只有 reasoning/tool 两支登记）。
    //   两个后果，都不是小事：
    //   ① 重建探针靠"有没有 blockId"区分「块行」与「通知行」——chat 气泡没有 id 时
    //      会被当成通知，于是负对照只比到可折叠行（实测 5/19/126/500，而真实错位量
    //      是 0/42/218/1998）⇒ **负对照失去了判别力**，正向全等也就不算证据。
    //   ② 行不知道自己来自哪个块，任何"按块回查"的诊断都只能靠猜。
    rec.blockId = block.id;

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
        return panel;
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
        return panel;
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

        // 标题 = 单一真相源（箭头两态）
        // ★ 0c：箭头取自 `rec.expanded`（重建时先由条目回填）——否则重建出来的行
        //   一律显示收起态箭头，而控件实际是展开的（读数与屏幕当场打架）。
        auto header = std::make_unique<ECDI::Button>(ReasoningTitle(block, rec.expanded));
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
        return panel;
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
        return panel;
    }

    // tool_call / tool_result：可折叠卡片（默认折叠；标题按钮 + CollapsiblePanel 展开体）
    rec.tool = true;
    rec.blockId = block.id;
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kContentBg, kProbeBg),
        .cornerRadius = kRadiusTool,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .borderColor = kCardBorder});

    std::string bodyText;
    if (block.kind == "tool_call") {
        bodyText = block.argsPretty.empty() ? "{}" : block.argsPretty;
    } else {
        bodyText = block.text.empty() ? block.summary : block.text;
    }

    const Block* call = (block.kind == "tool_result")
                            ? m_sess->transcript.FindBlock(block.forId)
                            : nullptr;
    auto header = std::make_unique<ECDI::Button>(ToolTitle(block, call));
    rec.headerBtn = header.get();
    // 结束且失败 ⇒ 标题转错误色（增量路径在 `block_end` 做，重建在这里做，同一判据）
    header->SetTextColor((block.ended && !block.ok)
                             ? kErrorDark
                             : Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
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
    return panel;
}

// 可折叠行的点击绑定（**构建期与重建期共用**）。
//   ★ 0c 的关键变化：展开态**先写回模型**（`timeline[idx].expanded`），再动控件。
//     在此之前展开态只活在 `RowRec` 上 ⇒ 任何重建都会把用户的展开操作静默回退。
void HarnessApp::BindCollapsible(const Block& block, RowRec& stored) {
    if (!stored.headerBtn) return;
    const bool reasoning = stored.reasoning;
    stored.headerBtn->SetOnClick([this, &stored, reasoning] {
        if (m_options.rowModeList) return;   // 固定行高模式：展开体被后续行覆盖，v1 禁用
        ToggleRow(stored, reasoning);
    });
    (void)block;
}

// 展开/收起一行（**点击路径与探针共用的唯一入口**）。
//   ★ 为什么必须抽成函数：0c 的重建探针要构造"用户已经展开过"的状态，而它**不能**
//     合成点击——探针需要在**任意**行上造状态，包括滚动到视口外、HitTest 命不中的行；
//     若探针自己再写一遍"改标志 + 翻箭头 + 重排"，那就是**第二份实现**，测的是它自己。
void HarnessApp::ToggleRow(RowRec& stored, bool reasoning) {
    stored.expanded = !stored.expanded;
    // ★ 视图 → 模型（重建前若来不及扫全表，这一条保证单行状态不丢）
    if (stored.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[stored.timelineIdx].expanded = stored.expanded;
    }
    if (stored.body) stored.body->Toggle();
    // 箭头随展开态翻转（﹀ ⇄ ︿）；文案与 token 副标由**单一真相源**决定，
    // 故回查块（`block_end` 可能已到达 ⇒ 文案应为「已完成」+ tokens）
    if (stored.headerBtn) {
        if (const Block* b = m_sess->transcript.FindBlock(stored.blockId)) {
            if (reasoning) {
                stored.headerBtn->SetText(ReasoningTitle(*b, stored.expanded));
            } else {
                // ★ 修 2026-10-19：此前工具卡这一支传的是 `std::string()`
                //   ⇒ **点一下工具卡，标题就被清空**（卡片从此没有名字）。
                //   与"箭头两态"同一个道理：标题必须由单一真相源重算，不能置空。
                const Block* call =
                    (b->kind == "tool_result") ? m_sess->transcript.FindBlock(b->forId) : nullptr;
                stored.headerBtn->SetText(ToolTitle(*b, call));
            }
        }
    }
    m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画
    // ★★ Step 5：展开态也进 sidecar（同样是"只对历史会话"，理由同 MarkAnswered）。
    if (m_sess && m_sess->originKind == "replay-history") SaveUiState(*m_sess);
    Relayout();             // 立即重排一次（settle 后定时器路径不再经过 Relayout）
    RequestRepaint();
}

void HarnessApp::CreateBlockRow(const Block& block) {
    RowRec rec;
    auto panel = BuildBlockRow(block, rec);

    // ★★ 0c：**先**登记模型条目，**再**交给 `AddRow`——
    //   `AddRow` 用 `rows.size()` 当条目的下标，两者等长同序才成立。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Block;
    entry.id = block.id;
    entry.expanded = false;
    m_sess->timeline.push_back(entry);

    AddRow(std::move(panel), rec, block.id, m_sess->timeline.size() - 1);
    if (m_sess->rows.empty()) return;
    RowRec& stored = m_sess->rows.back();

    // ★ 在**建行时刻**抓流式态标题（`block_start` 刚到、`block_end` 还没来）。
    //   为何必须此刻抓：`--instant` 在同一 tick 抽干夹具 ⇒ 事后回读永远是
    //   「已完成」，`▼ 思考中` 那一态**根本采不到**（首版就栽在这里）。
    if (m_options.reasoningProbe && stored.reasoning && m_reasoningTitleStreaming.empty()) {
        m_reasoningTitleStreaming = stored.headerBtn->GetText();
    }
    // 只有可折叠行需要绑回调（普通气泡/未知 kind 无展开体）
    if (stored.body && stored.headerBtn) BindCollapsible(block, stored);
}

// ── ★★ 0c：模型 → 视图（重建路径）────────────────────────────────────
//
// 为什么需要"重建"而不是就地改：`Theme.h:19-20` 写明令牌是**引用别名**，UI 一旦建好就把
// 当时的颜色值拷进了各自的 PanelStyle/ButtonStyle ⇒ 之后 `SwitchTheme()` 不会重绘。
// 故换主题/换会话的唯一可靠手段是**拆掉重来**；而拆掉重来的前提是模型足够把视图原样
// 再生出来——本组函数就是那条通路，0c 的探针拿它自证（不是靠肉眼）。

// 视图 → 模型：把 `rows` 上的临场状态（展开/已答）写回 `timeline`。
//   ★ 为何要"扫全表"而不是只靠回调里那句写回：重建可能由**别的**路径触发
//     （换主题、切会话、探针），那些路径不经过点击回调。重建前统一同步一次，
//     才能保证"用户看到的"与"模型记下的"在拆树那一刻一致。
void HarnessApp::SyncRowViewState() {
    for (RowRec& r : m_sess->rows) {
        if (r.timelineIdx >= m_sess->timeline.size()) continue;
        TimelineEntry& entry = m_sess->timeline[r.timelineIdx];
        if (r.body) entry.expanded = r.expanded;
        if (r.ask) entry.answered = r.answered;
    }
}

// 把一条时间线条目落成一行（构建期与重建期**共用同一个**分派点）。
//   ★ 重建期**绝不**执行副作用：不重发 answer、不重跑自动应答、不追加通知。
void HarnessApp::MaterializeOne(size_t idx, const TimelineEntry& entry) {
    // ★ idx = 本条目在 `timeline` 中的位置，直接当作新行的 `timelineIdx`：
    //   重建期**不**追加条目、**不**发副作用，行与条目一一对应。
    (void)idx;
    switch (entry.kind) {
        case TimelineEntry::Kind::Block: {
            const Block* block = m_sess->transcript.FindBlock(entry.id);
            if (!block) return;   // 模型里指向一个不存在的块：如实跳过，不编造
            RowRec rec;
            rec.expanded = entry.expanded;   // ★ 先回填状态再建行：标题箭头据此定态
            auto panel = BuildBlockRow(*block, rec);
            AddRow(std::move(panel), rec, block->id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.expanded = entry.expanded;
            if (stored.body && stored.headerBtn) {
                BindCollapsible(*block, stored);
                // ★ 展开态要真的落到控件上（`expanded` 标志只影响行高计算，
                //   折叠体的内容可见性由控件自己管）
                if (entry.expanded) stored.body->SetExpanded(true);
            }
            return;
        }
        case TimelineEntry::Kind::Ask: {
            RowRec rec;
            auto panel = BuildAskRow(entry, rec);
            AddRow(std::move(panel), rec, entry.id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
            stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });
            // ★ 已答状态**静默回填**：只置灰、不重发 answer（否则重建 = 重放一次决策，
            //   在 live 会话里就是往 core 又答了一遍 —— 那是会改协议状态的真副作用）。
            // ★★ Step 3：走**同一条**视觉路径（`ApplyAnsweredLook`），答案取
            //   `entry.answer`。第一版这里只有两行 `SetEnabled(false)` ⇒ 重建出来的
            //   权限门与"刚答完"不同形（配色不还原），而 A==B 比的是文字与位标志，
            //   看不见配色 ⇒ 又一次"比对全等但屏幕不一样"（同 0c 的 buildTitle 缺陷）。
            if (entry.answered) {
                stored.answered = true;
                ApplyAnsweredLook(stored, entry.answer);
            }
            return;
        }
        case TimelineEntry::Kind::Notice: {
            RowRec rec;
            auto panel = BuildNoticeRow(entry.text, entry.color, rec);
            AddRow(std::move(panel), rec, std::string(), idx);
            return;
        }
    }
}

void HarnessApp::MaterializeRows() {
    for (size_t i = 0; i < m_sess->timeline.size(); ++i) {
        MaterializeOne(i, m_sess->timeline[i]);
    }
}

// Level 1 重建：拆掉转录区的全部行控件，再按模型重放一遍。
//   ★ 只碰 `m_content` 的子树 ⇒ `CountingPanel`/HUD/侧栏/检查器指针全部存活，
//     评估读数不中断（整树重建才会打死计数器，那是 Level 2，留给 0e）。
//   ★ 拆之前必须先把视图状态同步进模型，否则等于"照着一份更旧的模型重建"。
void HarnessApp::RebuildTranscriptView(const std::vector<std::string>* orderOverride) {
    SyncRowViewState();
    m_pendingPromptViews.clear();

    // ★ 重建会让内容重新排一遍 ⇒ 不刻意恢复的话视图会跳回顶部。
    //   "换主题/切会话之后回到刚才读到的地方"是**可用性**要求，不是装饰：
    //   转录一长，重排后跳顶等于让人重新找位置。
    const int keepScroll = m_scroll ? m_scroll->GetScrollOffsetY() : 0;

    // ① 先让**指针表**归零，再动控件树：否则 `Relayout()` 会顺着 `rows` 里
    //    已经指向已删控件的裸指针走（悬垂读）。顺序反了就是 UAF，不是风格问题。
    m_sess->rows.clear();
    m_sess->blockRows.clear();
    if (m_content) {
        // `RemoveChild` 返回所有权；逐个摘掉即完成子树销毁（`Widget::RemoveChild`
        // 内部走 `Window::ForgetSubtree`，焦点/捕获/悬停指针一并清掉）
        while (m_content->GetChildCount() > 0) {
            m_content->RemoveChild(m_content->GetChildAt(0));
        }
    }

    // ② 按模型重放（或按调用方给的**错误**次序重放——那正是负对照要的形状）
    if (!orderOverride) {
        MaterializeRows();
    } else {
        // ★ 负对照专用：**只**照块表建块行（不建通知/问句行、不按条目回填状态）
        //   ——这正是"只拿 `Transcript::m_blocks` 重建视图"这个错误实现的样子。
        //   `timelineIdx` 给一个越界哨兵值：它**不**参与视图状态写回
        //   （负对照要复现的是错误实现，不该顺手把状态写坏）。
        const size_t kNoTimeline = static_cast<size_t>(-1);
        for (const std::string& id : *orderOverride) {
            const Block* block = m_sess->transcript.FindBlock(id);
            if (!block) continue;
            RowRec rec;
            auto panel = BuildBlockRow(*block, rec);
            AddRow(std::move(panel), rec, block->id, kNoTimeline);
            if (m_sess->rows.empty()) continue;
            RowRec& stored = m_sess->rows.back();
            if (stored.body && stored.headerBtn) BindCollapsible(*block, stored);
        }
    }
    MaterializePendingPrompts();

    // ★ 重建后必须自己收尾：重排（几何是新的）再把滚动位置放回去。
    //   让调用方记得补这一步是设计要求里最容易漏的一环，故内置。
    Relayout();
    if (m_scroll) m_scroll->SetContentOffset(0, keepScroll);
}

// 权限门行：**纯构造**（重建可重放）。问句与 id 都来自模型条目，不是来自"刚到的消息"
//   ——重建时那条消息早已不存在，若从消息取就等于"重建不出问句"。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildAskRow(const TimelineEntry& entry, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = kAskBg, .cornerRadius = kRadiusTool,
        .borderWidth = 1.0f, .borderColor = kAskBorder});
    rec.ask = true;
    rec.askId = entry.id;   // P1.5：answer 回执要带 id

    auto label = std::make_unique<ECDI::Label>("权限 " + entry.text);
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
    return panel;
}

void HarnessApp::CreateAskRow(const json::Value& msg) {
    // ★★ 0c：问句进模型。此前它只存在于这条 json 消息里 ⇒ 任何重建都会**
    //    把权限门变成一问句都没有的空卡**（按钮还在，但不知道在问什么）。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Ask;
    entry.id = msg.GetString("id");
    entry.text = msg.GetString("question");
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildAskRow(m_sess->timeline.back(), rec);
    AddRow(std::move(panel), rec, entry.id, m_sess->timeline.size() - 1);

    RowRec& stored = m_sess->rows.back();
    stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
    stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });

    if (!m_options.autoAnswer.empty()) {
        MarkAnswered(stored, m_options.autoAnswer);   // 无人值守：立即回答（协议 §8.3）
    } else if (m_options.timed) {
        m_sess->waitForAnswer = true;   // timed 回放停在 waiting，直到用户点选
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
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
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
    for (RowRec& r : m_sess->rows) {
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

// ── ★★ Step 0c：重建探针（--rebuild-probe）──────────────────────────────
//
// 断言：**照模型重建出来的视图 == 增量构建出来的视图**（逐行、逐字段）。
//   为什么必须机读：少一条通知、错一位次序、丢一个展开态，屏幕上看都像"差不多"。
//   故本探针把每行压成一条可比较的字符串，两侧用**同一个**量具比。
//
// 判别力（0a 的教训）：demo 夹具上「到达序」与「字典序」恰好一致 ⇒ 只用 demo
//   验不出顺序错。故本探针**自带负对照**：再按字典序重建一次，必须报出
//   cot 42 / realistic 218 / scale 1998 / demo 0 处次序错。负对照不报错 = 探针瞎了。

std::vector<std::string> HarnessApp::SnapshotRows() const {
    std::vector<std::string> out;
    out.reserve(m_sess->rows.size());
    for (const RowRec& r : m_sess->rows) {
        // 行种类：A=权限门 / B=块行 / N=通知行
        //   ★ 判据必须只用 `RowRec` 上**已经存在**的信息，不能再回头问模型
        //     ——否则重建前后两侧会拿到同一份答案，比对就恒等。
        const char* kind = nullptr;
        const std::string* id = nullptr;
        if (r.ask) {
            kind = "A";
            id = &r.askId;
        } else if (r.tool || r.reasoning || !r.blockId.empty()) {
            kind = "B";
            id = &r.blockId;
        } else {
            kind = "N";
        }
        std::string line;
        line += kind;
        line += '\x1f';
        if (id) line += *id;
        line += '\x1f';
        line += r.expanded ? "1" : "0";
        line += '\x1f';
        line += r.answered ? "1" : "0";
        line += '\x1f';
        line += r.headerBtn ? r.headerBtn->GetText() : std::string();
        line += '\x1f';
        if (r.label) line += r.label->GetText();
        line += '\x1f';
        // ★★ Step 3：`answer` 段放**最后**——既有字段的编号一位不动，
        //   `CompareRows` 的 `kFieldName[]` 只需在尾部追加一个名字；
        //   放在中间会让 "expanded/answered/header/label" 全部错位，
        //   历史读数与解读脚本会静默串味。
        line += r.answer;
        out.push_back(std::move(line));
    }
    return out;
}

HarnessApp::RebuildProbeRec HarnessApp::CompareRows(const char* phase,
                                                    const std::vector<std::string>& before,
                                                    const std::vector<std::string>& after,
                                                    bool blockOnly) const {
    RebuildProbeRec rec;
    rec.phase = phase;

    // ① 「只比块行」时两侧都先做**同一次**投影。
    //   否则"少了通知行"会被读成"次序错"——两种损失混在一个数里就没法归因。
    auto project = [](const std::vector<std::string>& rows) {
        std::vector<std::string> out;
        for (const std::string& r : rows) {
            if (!r.empty() && r[0] == 'B') out.push_back(r);
        }
        return out;
    };
    const std::vector<std::string> b = blockOnly ? project(before) : before;
    const std::vector<std::string> a = blockOnly ? project(after) : after;

    rec.beforeCount = b.size();
    rec.rowCount = a.size();
    rec.timelineCount = m_sess->timeline.size();
    // 滚动位置：重建后由 `RebuildTranscriptView` 自己恢复，这里读**恢复之后**的值，
    // 与摘要里的 `rebuildScrollBefore` 对读即可判"有没有跳回顶部"。
    rec.scrollOffset = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
    rec.maxScrollOffset = m_scroll ? m_scroll->GetMaxOffsetY() : 0;

    // 身份 = 前两段（种类 + 标识）。身份不同 ⇒ 次序/条目错，单独计数。
    auto identity = [](const std::string& s) {
        const size_t p1 = s.find('\x1f');
        if (p1 == std::string::npos) return s;
        const size_t p2 = s.find('\x1f', p1 + 1);
        if (p2 == std::string::npos) return s;
        return s.substr(0, p2);
    };
    // ★ 分歧必须报到**字段**一级：负对照的首个分歧常是 `B/b1 -> B/b1`（身份相同、
    //   展开态不同），只印身份就是一条**假线索**——排查的人会以为次序没错。
    //   字段名按 `SnapshotRows` 的拼装次序编号。
    static const char* kFieldName[] = {"kind", "id", "expanded", "answered", "header", "label", "answer"};
    auto firstDiffField = [](const std::string& x, const std::string& y) {
        std::vector<std::string> fx;
        std::vector<std::string> fy;
        auto split = [](const std::string& s, std::vector<std::string>& out) {
            size_t start = 0;
            while (true) {
                const size_t p = s.find('\x1f', start);
                if (p == std::string::npos) {
                    out.push_back(s.substr(start));
                    return;
                }
                out.push_back(s.substr(start, p - start));
                start = p + 1;
            }
        };
        split(x, fx);
        split(y, fy);
        for (size_t i = 0; i < std::max(fx.size(), fy.size()); ++i) {
            const std::string& vx = i < fx.size() ? fx[i] : std::string();
            const std::string& vy = i < fy.size() ? fy[i] : std::string();
            if (vx == vy) continue;
            std::string field = i < sizeof(kFieldName) / sizeof(kFieldName[0]) ? kFieldName[i]
                                                                             : "field" + std::to_string(i);
            auto clip = [](const std::string& v) {
                return v.size() > 28 ? v.substr(0, 28) + "…" : v;
            };
            return field + " [" + clip(vx) + "] -> [" + clip(vy) + "]";
        }
        return std::string("(fields equal)");
    };
    auto show = [&](const std::string& s) {
        std::string t = identity(s);
        for (char& c : t) {
            if (c == '\x1f') c = '/';
        }
        return t;
    };

    const size_t n = std::max(b.size(), a.size());
    for (size_t i = 0; i < n; ++i) {
        const bool hasB = i < b.size();
        const bool hasA = i < a.size();
        if (!hasB || !hasA) {
            ++rec.mismatches;   // 长度差也计入（少一行 / 多一行）
            if (rec.firstDivergence.empty()) {
                rec.firstDivergence = "idx=" + std::to_string(i) + " len(" +
                                      std::to_string(b.size()) + " vs " +
                                      std::to_string(a.size()) + ")";
            }
            continue;
        }
        if (b[i] == a[i]) continue;
        ++rec.mismatches;
        if (identity(b[i]) != identity(a[i])) ++rec.orderMismatches;
        if (rec.firstDivergence.empty()) {
            rec.firstDivergence = "idx=" + std::to_string(i) + " " + show(b[i]) + " -> " +
                                  show(a[i]) + " @" + firstDiffField(b[i], a[i]);
        }
    }
    return rec;
}

void HarnessApp::RunRebuildProbe() {
    if (!m_window || !m_sess->replay) return;
    if (m_options.rowModeList) {
        // 固定行高模式下"行高随展开变化"这件事本就不成立，探针无从比较 ⇒ 如实不开跑
        m_rebuildProbeDone = true;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_rebuildProbeStep > 0 && now < m_rebuildProbeNextAt) return;
    const auto settle = std::chrono::milliseconds(400);   // > 200ms 折叠动画

    switch (m_rebuildProbeStep) {
        case 0: {
            // ① 造**必有**区分力的前置条件：真的展开若干可折叠行、真的答掉若干权限门。
            //   ★ 走 `ToggleRow` / `MarkAnswered`（用户点下去走的**同一条**路径），
            //     不直接改标志：否则探针测的是"我照着我写的方式再写一遍对不对"。
            //   ★ 样本数封顶：每次 ToggleRow 都会 Relayout（O(行数)），scale 夹具 2000 行
            //     若全展开就是 O(n²)。4 个样本已足够判"状态保不保得住"。
            const int kMaxExpandSamples = 4;
            const int kMaxAnswerSamples = 2;
            // ★ 顺手把「展开/收起一次之后标题是否还完整」也留成读数。
            //   为什么必须单独记：0c 里修的那个 bug（工具卡点一下标题被写成空串，
            //   因为回调给非 reasoning 分支传的是 `std::string()`）**不会**让重建比对
            //   失败——重建前后标题都是空的，A == B 照样成立。丢失发生在"点击那一刻"，
            //   所以只有把点击前后的标题都记下来才看得见。
            //   ★★ 取样必须**按种类凑齐**：只取"前 4 个可折叠行"在 cot 上全是思维链行
            //      （夹具里 12 reasoning / 4 tool_call / 4 tool_result，思维链全在前面），
            //      于是刚修的工具卡分支**一个样本都没有**——读数 0 看着像"没问题"，
            //      实际是"没测"。先取工具卡，再补思维链。
            const auto sampleOne = [&](RowRec& r) {
                const std::string beforeT = r.headerBtn->GetText();
                ToggleRow(r, r.reasoning);
                const std::string afterT = r.headerBtn->GetText();
                if (m_rebuildTitleSample.empty()) {
                    m_rebuildTitleSample = beforeT;
                    m_rebuildTitleAfter = afterT;
                }
                if (afterT.empty() && !beforeT.empty()) ++m_rebuildTitleEmptied;
                ++m_rebuildExpandedSamples;
            };
            for (RowRec& r : m_sess->rows) {   // 第一轮：工具卡（钉住那个 bug）
                if (m_rebuildExpandedSamples >= 2) break;
                if (r.tool && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {   // 第二轮：思维链卡
                if (m_rebuildExpandedSamples >= kMaxExpandSamples) break;
                if (r.reasoning && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {   // 兜底：别的可折叠行
                if (m_rebuildExpandedSamples >= kMaxExpandSamples) break;
                if (!r.tool && !r.reasoning && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {
                if (m_rebuildAnsweredSamples >= kMaxAnswerSamples) break;
                if (r.ask && !r.answered) {
                    MarkAnswered(r, "allow");
                    ++m_rebuildAnsweredSamples;
                }
            }
            // 模型里记下的通知条数（"通知进模型"这件事的承重性证据：
            //   若模型不记通知，重建就会**静默**少这么多行）
            m_rebuildNoticeCount = 0;
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Notice) ++m_rebuildNoticeCount;
            }
            // ★ 滚动这一项也要**先造出判别力**：夹具抽干后视图停在顶部
            //   （offset=0，而 scale 的 maxOffset 有 10 万），此时"重建后有没有
            //   跳回顶部"这一问恒真——0 == 0。先把视图真的滚到中间再重建，
            //   读数才有意义（否则又是一次"看着全等其实没测"）。
            if (m_scroll && m_scroll->GetMaxOffsetY() > 200) {
                m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY() / 3);
            }
            m_rebuildScrollBefore = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
            m_rebuildProbeBefore = SnapshotRows();
            m_rebuildProbeStep = 1;
            m_rebuildProbeNextAt = now + settle;
            return;
        }
        case 1: {
            // ② forward：照 `timeline` 重建 ⇒ 判据 A == B（必须全等）
            RebuildTranscriptView();
            m_rebuildProbeLog.push_back(
                CompareRows("forward", m_rebuildProbeBefore, SnapshotRows(), false));
            m_rebuildProbeStep = 2;
            m_rebuildProbeNextAt = now + settle;
            return;
        }
        case 2: {
            // ③ 幂等：**再**重建一次，仍须与 A 全等。
            //   这一项专抓"状态只落到控件、没落回模型"：第一次重建屏幕上看着对，
            //   第二次就会塌回收起态，而单看 ② 是看不出来的（② 的 B 是第一次的产物）。
            RebuildTranscriptView();
            m_rebuildProbeLog.push_back(
                CompareRows("forward-again", m_rebuildProbeBefore, SnapshotRows(), false));
            m_rebuildProbeStep = 3;
            m_rebuildProbeNextAt = now + settle;
            return;
        }
        case 3: {
            // ④ 负对照：**只**按块表、按 id 字典序重建（= 只拿 `Transcript::m_blocks`
            //   遍历这种错误实现的等价物；`std::map` 迭代序就是 `std::less<std::string>`
            //   序，故这里 `std::sort` 出来的次序与之逐元素相同）。
            //   预期：orderMismatches = 字典序错位量（cot 42 / realistic 218 /
            //   scale 1998 / demo 0）。为 0 ⇒ 探针没有判别力，② 的全等不算证据。
            std::vector<std::string> lex = m_sess->transcript.Order();
            std::sort(lex.begin(), lex.end());
            RebuildTranscriptView(&lex);
            m_rebuildProbeLog.push_back(
                CompareRows("lexicographic", m_rebuildProbeBefore, SnapshotRows(), true));
            m_rebuildProbeStep = 4;
            m_rebuildProbeNextAt = now + settle;
            return;
        }
        case 4: {
            m_rebuildProbeDone = true;
            if (m_application) m_application->Exit();
            return;
        }
        default:
            m_rebuildProbeDone = true;
            return;
    }
}

// ★★ Step 0d：多会话探针（--session-probe）。同步跑完——三个读数（会话数/活动下标/
//   行数）都是同步值，不像重建那样要等折叠动画沉降，故不需要跨 tick 状态机。
void HarnessApp::RunSessionProbe() {
    if (!m_sess) { m_sessionProbeDone = true; return; }
    // ★ 只在回放模式跑（契约见 App.h 声明处）：live 下 NewSession 会真拉一个 core
    //   子进程，探针不该在测量里制造真进程。目前调用点只在 replay settled 分支，
    //   已间接保证；这里再显式挡一道，防止将来有人从别处调用它。
    if (!m_sess->replay) { m_sessionProbeDone = true; return; }

    auto snap = [&](const char* phase) {
        SessionProbeRec rec;
        rec.phase = phase;
        rec.sessionCount = m_sessions.size();
        rec.activeIndex = m_activeIndex;
        rec.rowCount = m_sess->rows.size();
        // 块行数（不含通知/问句行）——负对照的关键：新会话的块行数必须为 0。
        rec.blockRowCount = 0;
        for (const RowRec& r : m_sess->rows) {
            if (r.tool || r.reasoning || (!r.blockId.empty() && !r.ask)) ++rec.blockRowCount;
        }
        m_sessionProbeLog.push_back(std::move(rec));
    };

    snap("baseline");                 // ① 起点：当前会话数 / 行数
    NewSession();                     // ② 真的新建一个会话（回放模式 ⇒ 空会话）
    snap("new");                      //    读：会话数应 +1；新会话块行数应为 0
    SwitchToSession(0);               // ③ 切回会话 0
    snap("back");                     //    读：活动会话应回到 0；行数应回到 baseline
    m_sessionProbeDone = true;
    if (m_application) m_application->Exit();
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 1：侧栏列表的 A==B 探针（--sidebar-probe）
//
// 为什么侧栏需要**自己**一套判据：0c 的 `SnapshotRows` 只看 `m_sess->rows`
//   —— 那是转录区。侧栏错了它一个字都读不到。而侧栏恰恰是"视图与模型的对应
//   关系"最容易分叉的地方：列表说 2 个会话、点下去切到第 1 个，转录区完全正常。
//
// 六条读数，每条都配了**能被推翻的形态**：
//   ① baseline      起点快照（模型会话数 / 列表行数 / 选中项）
//   ② rebuild       原地重建一次 ⇒ 必须与 baseline 逐行相同（matched=true）
//   ③ rebuild-again 再重建一次 ⇒ 幂等（防"重建时顺手改了状态"）
//   ④ negative      **负对照**：喂逆序下标重建 ⇒ 必须报**不匹配**。
//                   这一条是整个探针的"灵敏度证明"——若它也 matched，
//                   说明比对函数根本没在看次序，②③ 的 matched 就一文不值。
//                   （同 0a 的字典序教训：没有负对照的正对照是自证）
//   ⑤ new           新建一个会话并重建 ⇒ 列表行数 +1、会话数 +1
//   ⑥ deleted       删掉那个新会话 ⇒ 列表行数回到 baseline、
//                   **活动会话的转录行数一位都不变**（负对照：删别人不能动我）
// ═══════════════════════════════════════════════════════════════════════════
void HarnessApp::RunSidebarProbe() {
    if (!m_sess) { m_sidebarProbeDone = true; return; }
    // ★ 只在回放模式：live 下 NewSession 会真拉 core 子进程，探针不该制造真进程。
    if (!m_sess->replay) { m_sidebarProbeDone = true; return; }
    if (!m_hud.sessList) { m_sidebarProbeDone = true; return; }

    auto snap = [&](const char* phase) {
        SidebarProbeRec rec;
        rec.phase = phase;
        rec.sessionCount = m_sessions.size();
        rec.activeIndex = m_activeIndex;
        rec.transcriptRows = m_sess->rows.size();
        // 控件侧：列表内容视图里的直接子节点数（= 画出来的行数）
        const ECDI::Widget& content = m_hud.sessList->GetContentView();
        rec.rowCount = content.GetChildCount();
        // 选中项：行内第一个 Label 的文字色 == kAccentInk 即视为选中
        //   （ECDI 的 Button/Panel 都没有 style getter，文字色是**唯一**可读回的
        //     选中标记 —— 见 `BuildSessionRow` 里那段注释，别把它当纯装饰改掉）
        rec.selectedRows = 0;
        rec.selectedIndex = static_cast<size_t>(-1);
        for (size_t i = 0; i < content.GetChildCount(); ++i) {
            const ECDI::Widget* row = content.GetChildAt(i);
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl == nullptr) continue;
                if (lbl->GetTextColor() == kAccentInk) {
                    ++rec.selectedRows;
                    rec.selectedIndex = i;
                }
                break;   // 只看行内第一个 Label（名称），圆点是 Panel、`×` 是 Button
            }
        }
        // ★ Step 2：布局层读数。只看 `IsVisible()` 会被"隐藏了但没重排、
        //   中栏没吃到那 200px"骗过（0c 的"重建了但没重排"同族缺陷）。
        rec.sidebarVisible = m_hud.sidebar ? m_hud.sidebar->IsVisible() : false;
        rec.midWidth = m_counting ? m_counting->GetWidth() : 0;
        m_sidebarProbeLog.push_back(std::move(rec));
    };

    // ★ 焦点守卫：合成点击会把焦点交给被点的 Button（`Application::OnMouseButtonDown`
    //   第一件事就是 `SetFocusedWidget`），焦点框与 caret 闪烁都会多画几帧 ⇒
    //   paints/frames 读数被这份探针污染。收尾必须还回去（同 `RunHitProbe`）。
    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // ★★ 点击臂：**走真实派发链**点列表行/`×`，而不是直接调 SwitchToSession/DeleteSessionAt。
    //   理由（这是 Step 1 之前完全空缺的那一类判据）：前面几相只证明"重建画得对"，
    //   却没有任何一条读数证明**点上去真的会切、会删**——画对了但回调没接上，
    //   在读数上与前几相完全同形。0c 在转录区正是栽在这里（buildTitle 重建完
    //   标题空了，而当时没有任何判据看得见）。
    auto rowAt = [&](size_t i) -> ECDI::Widget* {
        ECDI::Widget& content = m_hud.sessList->GetContentView();
        return i < content.GetChildCount() ? content.GetChildAt(i) : nullptr;
    };
    auto clickCentreOf = [&](ECDI::Widget* w) {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + w->GetWidth() / 2,
                          static_cast<int>(abs.y) + w->GetHeight() / 2);
        // 派发返回 ⇒ 已离开事件栈 ⇒ 此时拆树安全（回调里只置了 `m_sidebarDirty`）
        if (m_sidebarDirty) { m_sidebarDirty = false; RebuildSessionList(); }
        return true;
    };
    auto deleteButtonOf = [&](size_t i) -> ECDI::Button* {
        ECDI::Widget* row = rowAt(i);
        if (!row) return nullptr;
        for (size_t c = 0; c < row->GetChildCount(); ++c) {
            if (auto* b = dynamic_cast<ECDI::Button*>(row->GetChildAt(c))) return b;
        }
        return nullptr;
    };
    // 把"这一相该看到什么"写成显式期望，失败时给出**哪一条没满足**（别只报 false）
    auto expect = [&](const char* phase,
                      std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        SidebarProbeRec& rec = m_sidebarProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    // ① 起点
    RebuildSessionList();
    m_sidebarDirty = false;
    snap("baseline");
    const std::vector<std::string> baseList = SnapshotSessionList();
    const size_t baseRows = m_sess->rows.size();

    // ② 原地重建 ⇒ A==B
    RebuildSessionList();
    {
        const SidebarCompare cmp = CompareSessionList(baseList, SnapshotSessionList());
        snap("rebuild");
        m_sidebarProbeLog.back().matched = cmp.matched;
        m_sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
    }

    // ③ 再重建一次 ⇒ 幂等（与 ② 的快照比，而不是与 ① 比：
    //   若 ② 已经把状态改坏了，③ 与 ① 比会同时报两处，定位反而更难）
    {
        const std::vector<std::string> afterRebuild = SnapshotSessionList();
        RebuildSessionList();
        const SidebarCompare cmp = CompareSessionList(afterRebuild, SnapshotSessionList());
        snap("rebuild-again");
        m_sidebarProbeLog.back().matched = cmp.matched;
        m_sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
    }

    // ④ 新建会话 ⇒ 列表行数 +1、会话数 +1。
    //   ★ 顺序说明：负对照（⑤）需要**两个以上**会话才有自由度，所以新建排在它前面。
    //     demo 夹具默认只有 1 个会话，若把负对照放在新建之前，它就永远是
    //     "逆序 == 正序"的退化情形——那条读数会写着 matched=true，读者会误以为
    //     负对照通过了，而实际上它根本没被检验过。
    NewSession();
    RebuildSessionList();   // 探针不在事件派发栈上，直接建是安全的
    snap("new");
    const size_t emptyRows = m_sess->rows.size();   // 空会话的转录行数（= 1，那条 notice）

    // ⑥ ★★ 点击臂：Step 1 之前这一类判据**完全空缺**——前面几相只证明"重建画得对"，
    //   没有任何一条读数证明"点上去真的会切"。而"画对了但回调没接上"在读数上
    //   与前几相完全同形（0c 在转录区正是这样骗过了当时的判据）。
    //   判据三件必须同时成立：活动下标变了 ∧ 选中行跟着变 ∧ **转录行数跟着换**。
    //   只查下标会被"切了但转录没重建"骗过；只查转录会被"两会话行数恰好相同"骗过。
    auto selectedIndex = [&]() -> long long {
        ECDI::Widget& content = m_hud.sessList->GetContentView();
        for (size_t i = 0; i < content.GetChildCount(); ++i) {
            const ECDI::Widget* row = content.GetChildAt(i);
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl == nullptr) continue;
                if (lbl->GetTextColor() == kAccentInk) return static_cast<long long>(i);
                break;
            }
        }
        return -1;
    };

    // ⑤ 点第 2 行（空会话）→ 点第 1 行（原会话）：两次都必须真的换过去
    const bool clickedAway = clickCentreOf(rowAt(1));
    expect("click-empty", {{clickedAway, "click did not dispatch"},
                           {m_activeIndex == 1, "activeIndex!=1"},
                           {selectedIndex() == 1, "selected row != 1"},
                           {m_sess->rows.size() == emptyRows, "transcript did not follow"}});
    const bool clickedBack = clickCentreOf(rowAt(0));
    expect("click-back", {{clickedBack, "click did not dispatch"},
                          {m_activeIndex == 0, "activeIndex!=0"},
                          {selectedIndex() == 0, "selected row != 0"},
                          {m_sess->rows.size() == baseRows, "transcript did not follow back"}});
    // ★ 负对照（点击臂专用）：点**行内 `×` 之外**的空白处必须切行，而点 `×` 必须**不**切行。
    //   后者的读法见 ⑦（删除臂）——若 `×` 的点击漏到父行，`SwitchToSessionByPtr` 会因为
    //   目标已被 erase 而静默 no-op（地址比对兜住了），故此处不重复断言，靠 ⑦ 的形状判。

    // ⑥ ★ 负对照：逆序重建。判据是 **matched == false**（探针必须能看见次序错）。
    //   ★ 只改**次序**这一个自变量：选中态仍按真实活动会话判定（`orderOverride`
    //     不再强制 false），否则差异里混进了"选中态被造没了"，就分不清探针抓到的
    //     是次序错还是选中错——单变量纪律（同 REQ-01/03/04 的教训）。
    if (m_sessions.size() >= 2) {
        // ★ 快照必须在**紧邻**逆序重建之前抓：点击臂刚把活动会话从 1 号切回 0 号，
        //   若沿用 `new` 相那份快照，选中态会天然差一格，比对报出的差异就与"次序"
        //   无关了（第一版正是这么写错的，读数里出现
        //   `restore-after-negative failed: row 0 selected [0] -> [1]` —— 错的是快照时机，
        //   不是实现）。
        const std::vector<std::string> twoList = SnapshotSessionList();
        std::vector<size_t> rev;
        for (size_t i = m_sessions.size(); i > 0; --i) rev.push_back(i - 1);
        RebuildSessionList(&rev);
        const SidebarCompare cmp = CompareSessionList(twoList, SnapshotSessionList());
        snap("negative");
        m_sidebarProbeLog.back().matched = cmp.matched;
        m_sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
        // 复原：负对照只借用一次列表，别把逆序留在屏上（⑦ 的读数会受它影响）
        RebuildSessionList();
        const SidebarCompare restored = CompareSessionList(twoList, SnapshotSessionList());
        if (!restored.matched) {
            // 复原失败 = 重建不是纯函数（第一次的差异被留在了状态里）⇒ 必须显式暴露
            m_sidebarProbeLog.back().firstDivergence =
                "restore-after-negative failed: " + restored.firstDivergence;
            m_sidebarProbeLog.back().matched = true;   // 仍记负对照通过，但差异里写明复原失败
        }
    } else {
        // 理论上到不了这里（④ 之后至少 2 个会话）；留一条诚实读数而不是静默跳过。
        snap("negative");
        m_sidebarProbeLog.back().matched = true;
        m_sidebarProbeLog.back().firstDivergence =
            "(no degrees of freedom: fewer than 2 sessions)";
    }

    // ⑦ ★★ 删除臂 A：点**非活动**会话的 `×`。
    //   用户口径（ECDI 实施方案第 ④ 条）：「删非活动时保留当前转录与活动会话不变，
    //   只调整列表和活动下标」。⇒ 判据是**四条同时成立**，其中 transcriptRows
    //   不变是真正的负对照：删错了会话（把活动那个删了）会让转录行数变。
    //   且这一臂同时验证了「嵌套 `×` 的点击不会漏到父行」：若漏了，父行会先
    //   `SwitchToSession` 切到被删的会话，随后 erase 让它变成空转录 ⇒ transcriptRows 会掉。
    {
        // 当前状态：0=原会话（活动，有转录）1=新建空会话。删 1 号（非活动）。
        ECDI::Button* delBtn = deleteButtonOf(1);
        const bool clicked = delBtn && clickCentreOf(delBtn);
        expect("delete-inactive", {{clicked, "no delete button on row 1"},
                                   {m_sessions.size() == 1, "sessionCount != 1"},
                                   {m_hud.sessList->GetContentView().GetChildCount() == 1,
                                    "rowCount != 1"},
                                   {m_activeIndex == 0, "activeIndex != 0"},
                                   {m_sess->rows.size() == baseRows,
                                    "active transcript changed by deleting inactive"}});
    }

    // ⑧ ★★ 删除臂 B：点**活动**会话的 `×` ⇒ 必须先切邻居再删，且列表/活动下标都收敛。
    //   这条是顺序错误的唯一直检：若实现先 erase 再 SwitchToSession，
    //   切换时读的是已析构的 Session（UAF，`rows` 里全是裸指针）。
    NewSession();
    RebuildSessionList();
    {
        const bool activeIsNew = (m_activeIndex == 1 && m_sessions.size() == 2);
        ECDI::Button* delBtn = deleteButtonOf(1);
        const bool clicked = delBtn && clickCentreOf(delBtn);
        expect("delete-active", {{activeIsNew, "setup: new session not active"},
                                 {clicked, "no delete button on row 1"},
                                 {m_sessions.size() == 1, "sessionCount != 1"},
                                 {m_hud.sessList->GetContentView().GetChildCount() == 1,
                                  "rowCount != 1"},
                                 {m_activeIndex == 0, "did not fall back to neighbour"},
                                 {m_sess->rows.size() == baseRows,
                                  "transcript did not come back to survivor"}});
    }

    // ⑨ ★ 最后一条：只有一个会话时**不允许删**（`DeleteSessionAt` 返回 false）。
    //   「删到零会话」会让 `m_sess == nullptr`，而全仓 200+ 处调用点假定非空。
    //   判据 = 点了也不掉行（不是"按钮不存在"——这里造的是"按钮在但删不动"，
    //   因为 canDelete 是按 size>1 判的，1 个会话时 `×` 根本不会被建出来。
    //   ⇒ 诚实的读法是：1 个会话时行内没有按钮，故 buttons == 0 且点不到。
    {
        ECDI::Widget* row = rowAt(0);
        const bool noButton = (row != nullptr && row->GetChildCount() == 2);
        expect("single-session", {{noButton, "row still offers a delete button"},
                                  {m_sessions.size() == 1, "sessionCount != 1"},
                                  {m_sess != nullptr, "active session became null"}});
    }

    // ⑩ ★★ Step 2：侧栏收起/展开臂。
    //   ★ 这一臂必须**点顶栏 `≡`** 而不是直接调 `SetSidebarVisible`：Step 2 之前
    //     `≡` 是个 `Label`（点不到），改成 Button 之后"回调接上了没有"才第一次
    //     成为可检验的命题——直接调函数会把这条判据整个绕过去。
    //   ★ 判据四条同时成立，缺一条就会被某种错误实现骗过：
    //     · sidebarVisible 翻转 —— 显隐真的改了；
    //     · **中栏宽度 +200/-200** —— 布局真的重排了（只改可见性不 Arrange 会在这里露馅，
    //       而 IsVisible() 仍然是对的 ⇒ 光看可见性不够）；
    //     · 侧栏宽 200 —— 常量没被折叠过程改掉（它不是 CollapsiblePanel，不该有中间尺寸）；
    //     · 列表行数不变 —— 收起/展开**不能**动会话模型（这条是负对照：
    //       若实现走成"收起 = 清空列表"，行数会掉到 0，而其余三条依然可能成立）。
    if (m_hud.sidebar && m_hud.sidebarToggle && m_counting) {
        const int expandedMid = m_counting->GetWidth();
        const int sideWidth = m_hud.sidebar->GetWidth();
        const size_t rowsBefore = m_hud.sessList->GetContentView().GetChildCount();

        // 收起：点顶栏 `≡`
        const bool clickedCollapse = clickCentreOf(m_hud.sidebarToggle);
        expect("collapse", {{clickedCollapse, "toggle click did not dispatch"},
                            {!m_sidebarVisible, "model still says visible"},
                            {!m_hud.sidebar->IsVisible(), "sidebar still visible"},
                            {m_counting->GetWidth() == expandedMid + sideWidth,
                             "mid column did not reclaim the sidebar width"},
                            {m_hud.sidebar->GetWidth() == sideWidth, "sidebar width changed"},
                            {m_hud.sessList->GetContentView().GetChildCount() == rowsBefore,
                             "collapse disturbed the session list"}});

        // 展开：再点一次，必须**回到原宽度**（不是"变宽了"就行——那可能是重复加宽）
        const bool clickedExpand = clickCentreOf(m_hud.sidebarToggle);
        expect("expand", {{clickedExpand, "toggle click did not dispatch"},
                          {m_sidebarVisible, "model still says hidden"},
                          {m_hud.sidebar->IsVisible(), "sidebar still hidden"},
                          {m_counting->GetWidth() == expandedMid,
                           "mid column did not return to its original width"},
                          {m_hud.sessList->GetContentView().GetChildCount() == rowsBefore,
                           "expand disturbed the session list"}});
    }

    // ⑪ ★★ 2026-10-20：检查器藏/显臂。
    //   这块在 GPT 完成前是**零判据**的：`≡` 的回调只改一个布尔 + `SetVisible`，
    //   一旦父容器 `Arrange()` 没跑，屏幕不对而所有既有探针全绿。
    //   形态照抄 ⑩：以**中栏宽度**为读据（检查器 230px，藏起来中栏必须宽出正好 230）。
    //   ★ 同样必须点真钮：直接调 `SetVisible` 会把“回调接上了没有”绕过去。
    //   ★ 第三条判据是负对照：藏检查器**不能**动转录行数——若实现走成“重搭整棵树”，
    //     中栏变宽仍然成立，但行数会变。
    if (m_hud.inspector && m_hud.inspectorToggle && m_counting) {
        const int midBefore = m_counting->GetWidth();
        const int inspWidth = m_hud.inspector->GetWidth();
        const size_t txBefore = m_sess->rows.size();

        const bool clickedHide = clickCentreOf(m_hud.inspectorToggle);
        expect("inspector-hide", {{clickedHide, "inspector toggle did not dispatch"},
                                  {!m_inspectorVisible, "model still says visible"},
                                  {!m_hud.inspector->IsVisible(), "inspector still visible"},
                                  {m_counting->GetWidth() == midBefore + inspWidth,
                                   "mid column did not reclaim the inspector width"},
                                  {m_sess->rows.size() == txBefore,
                                   "hiding the inspector disturbed the transcript"}});

        const bool clickedShow = clickCentreOf(m_hud.inspectorToggle);
        expect("inspector-show", {{clickedShow, "inspector toggle did not dispatch"},
                                   {m_inspectorVisible, "model still says hidden"},
                                   {m_hud.inspector->IsVisible(), "inspector still hidden"},
                                   {m_counting->GetWidth() == midBefore,
                                    "mid column did not return to its original width"},
                                   {m_sess->rows.size() == txBefore,
                                    "showing the inspector disturbed the transcript"}});
    }

    // ⑫ ★★ 2026-10-20：工作区分组臂。
    //   ★ 为何必须在**回放**里造工作区：回放会话 `writer == nullptr` ⇒
    //     `MaybeStartWorkspaceSync` 首行就 return ⇒ 回放会话永远拿不到真实 ack。
    //     但分组渲染本身与 core 无关——它读的是模型侧的外键。所以这里用**真实入口**
    //     （`AcquireWorkspace` + `AssignSessionWorkspace`，与 workspace ack 里那两行
    //     是同一对函数）把模型推到「两个工作区各挂一个会话」，再验视图。
    //     造模型用真函数而不是手写 id 字符串：否则测的是探针自己的常量。
    //   ★ 第一条判据先验**前一个状态**（没工作区时平铺）：否则后面的分组断言
    //     证明不了“平铺是回退项”，只能证明“分组能用”。
    //   ★ 标题序（后建在上）是用户裁决 m08553 的唯一可机检形式：它不是“最近
    //     使用”也不是“字母序”，只能与**创建序逆序**逐字相等。
    {
        const size_t flatRows = m_hud.sessList->GetContentView().GetChildCount();
        const size_t flatSessions = m_sessions.size();

        NewSession();                 // 回放空会话：不拉子进程
        RebuildSessionList();
        // ★★ 探针用的两个路径**故意很长，且只在叶子段不同**——
        //   这正是截图抓到的缺陷形态：两个工作区只差最后一段，而 `Label` 是**尾弃**，
        //   尾部一丢屏幕上一模一样。用短路径（如 `D:/probe/workspace-b`）就根本
        //   触发不到裁剪 ⇒ 这条判据会变成永真的空转。
        Workspace* wsA = AcquireWorkspace("D:/probe/very-long-parent-directory/workspace-alpha");
        Workspace* wsB = AcquireWorkspace("D:/probe/very-long-parent-directory/workspace-beta");
        AssignSessionWorkspace(*m_sessions[0], wsA);
        AssignSessionWorkspace(*m_sessions[1], wsB);
        RebuildSessionList();

        auto firstLabel = [&](size_t i) -> const ECDI::Label* {
            ECDI::Widget* row = rowAt(i);
            if (!row) return nullptr;
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl != nullptr) return lbl;
            }
            return nullptr;
        };
        auto rowTitle = [&](size_t i) -> std::string {
            const ECDI::Label* lbl = firstLabel(i);
            return lbl ? lbl->GetText() : std::string();
        };
        auto endsWith = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() &&
                   s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        // 行类判据 = 行根类型（会话行必须是 Button：`Panel::ContainsPoint` 恒 false）
        auto rowIsSession = [&](size_t i) {
            return dynamic_cast<const ECDI::Button*>(rowAt(i)) != nullptr;
        };

        const bool twoWorkspaces = (m_workspaces.size() == 2 && wsA && wsB);
        expect("grouping", {{flatRows == flatSessions,
                             "flat fallback was not drawn before any workspace existed"},
                            {twoWorkspaces, "setup: workspaces were not created"},
                            {m_hud.sessList->GetContentView().GetChildCount() == 4,
                             "rowCount != 4 (2 titles + 2 sessions)"},
                            {!rowIsSession(0) && rowIsSession(1) && !rowIsSession(2) &&
                                 rowIsSession(3),
                             "row kinds are not title/session/title/session"},
                            // ★ 判据从“整串相等”改成“尾部相等”：标题现在带中间省略，
                            //   整串比会随预算常量变脆；尾叶子段才是用户靠它区分的东西。
                            {endsWith(rowTitle(0), "workspace-beta"),
                             "later-created workspace is not on top (or its tail was clipped)"},
                            {endsWith(rowTitle(2), "workspace-alpha"),
                             "earlier-created workspace is not below (or its tail was clipped)"},
                            {rowTitle(0) != rowTitle(2),
                             "the two groups render as the same text"},
                            {rowAt(0) && rowAt(1) && rowAt(0)->GetHeight() == kSidebarGroupH &&
                                 rowAt(1)->GetHeight() == kSidebarRowH,
                             "title and session rows are not visually distinguishable"},
                            {selectedIndex() == 1, "grouping lost the active-session marker"},
                            // ★★ m09443：标题行现在有「＋」按钮（不是「×」）。
                            //   旧判据 `deleteButtonOf(0) == nullptr` 会因「＋」的存在而翻红。
                            //   新判据：标题行的按钮（如果有）文字必须是「＋」，不能是「×」。
                            {deleteButtonOf(0) == nullptr ||
                                 deleteButtonOf(0)->GetText() == "＋",
                             "a group title offered a delete button instead of ＋"}});

        // ⑭ ★★ 2026-10-20（截图抓到的那一类）：分组标题必须真的**放得下**。
        //   为何必须量宽而不是只比字符串：裁剪发生在**绘制层**（`Label` → 单行 DrawText），
        //   `GetText()` 读回来的仍是完整串 ⇒ 一条只读模型的判据会全绿而屏幕是错的。
        //   这是 0c `buildTitle` 缺陷的同族：**比对全等，屏幕不一样**。
        //   判据三条缺一条都不成立：
        //     ① 实测宽 ≤ 可用宽（否则右侧会被无声切掉）；
        //     ② 尾部真的保留了（否则“区别”正好被切掉，分组退化成按前缀区分）；
        //     ③ 负对照：**未省略的原串**实测必须超宽——否则说明这些路径根本不需要省略，
        //        ① 就是恒真的空转（“没裁剪”不是因为我们做对了，而是因为没触发）。
        {
            const ECDI::Label* title0 = firstLabel(0);
            // 非 const：`MeasureText` 是虚函数且非 const（测量器可能内部缓存）。
            ECDI::TextMeasurer& measurer = m_window->GetTextMeasurer();
            const ECDI::Font font = harness::MonoFont(harness::kFontMicro);
            const std::string shown = rowTitle(0);
            const float shownW =
                shown.empty() ? 0.0f : measurer.MeasureText(font, shown).width;
            const float availW = title0 ? static_cast<float>(title0->GetWidth()) : 0.0f;
            const float rawW = wsB ? measurer.MeasureText(font, wsB->path).width : 0.0f;
            expect("group-title-fit",
                   {{title0 != nullptr, "row 0 has no title label"},
                    {shownW <= availW,
                     "group title is wider than its label (paint layer would clip the tail)"},
                    {endsWith(shown, "workspace-beta"),
                     "the distinguishing tail did not survive elision"},
                    {shown.size() < (wsB ? wsB->path.size() : 0u),
                     "title was not actually elided"},
                    {rawW > availW,
                     "negative control: the raw path fits, so this phase proves nothing"}});
        }

        // ⑬ ★ 分组臂的负对照：只改**一个自变量**（wsB 的路径），
        //   快照必须在 `row 0 name` 上失配，且只在那里。
        //   若分组标题不读 `Workspace::path`（比如读了会话标签），这条会静默变成
        //   “改了什么都没变”——而所有其它相依然全绿。
        const std::vector<std::string> beforeRename = SnapshotSessionList();
        const std::string savedPath = wsB ? wsB->path : std::string();
        if (wsB) wsB->path = "D:/probe/workspace-b-renamed";   // probe-only corruption
        RebuildSessionList();
        {
            const SidebarCompare cmp =
                CompareSessionList(beforeRename, SnapshotSessionList());
            snap("group-negative");
            m_sidebarProbeLog.back().matched =
                !cmp.matched && cmp.firstDivergence.rfind("row 0 name", 0) == 0;
            m_sidebarProbeLog.back().firstDivergence =
                cmp.matched ? "(group negative control saw no difference)"
                            : cmp.firstDivergence;
        }
        // 复原（同 ⑥ 的纪律：负对照不能把状态留在屏上），并验复原真的回到原样
        if (wsB) wsB->path = savedPath;
        RebuildSessionList();
        const SidebarCompare restoredGroup =
            CompareSessionList(beforeRename, SnapshotSessionList());
        if (!restoredGroup.matched) {
            m_sidebarProbeLog.back().firstDivergence =
                "restore-after-group-negative failed: " + restoredGroup.firstDivergence;
        }
    }

    // ★ 焦点复原：合成点击把焦点交给了被点的 Button，留下的焦点框与 caret 闪烁
    //   会多画几帧 ⇒ paints/frames 读数被污染（同 RunHitProbe 的收尾）。
    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_sidebarDirty = false;

    m_sidebarProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ Step 3：`answer` 进模型的取证。
//
// 这条探针要回答的问题只有一个：**"当时允许还是拒绝"这件事，重建之后还在不在屏上？**
//   · 改之前：模型只存 `answered`（bool）⇒ 重建后两个钮都灰，读者无从判断结论。
//     A==B 探针也抓不到——它比的是文字与位标志，而第一版的 `MaterializeOne` 连位标志
//     都回填了（`stored.answered = true`），于是"丢结论"在读数上是**空的**。
//     这是 0c `buildTitle` 缺陷的同族：**比对全等，屏幕不一样**。
//
// 判据设计（为什么每条都不可省）：
//   ① 模型侧 `answer` 逐字等于点击的 choice —— 结论真的进了模型；
//   ② 屏幕侧 `answerOnScreen` 从**两个钮的文字色**反推 —— 结论真的到了用户眼前，
//      且这条读数**不查模型**（否则与 ① 同义反复，同 SnapshotRows 的纪律）；
//   ③ forward：重建前后行快照 A==B（含新增的 answer 段）；
//   ④ negative：把模型里的答案改成另一个值再重建，快照比对必须**不**相等，
//      且 firstDivergence 必须指向 `answer` 字段 —— 证明 ③ 的相等不是"两边都没读"；
//   ⑤ 复原：把答案改回去，必须重新相等 —— 证明差异来自那一个自变量（单变量纪律）；
//   ⑥ 未答的权限门：answer 为空、两钮**都还能点** —— 负对照，证明"置灰"只发生在答过之后。
void HarnessApp::RunAnswerProbe() {
    if (!m_sess) { m_answerProbeDone = true; return; }
    // ★ 只在回放模式：live 下点击「允许」会真的 `writer->Answer()` 发给 core，
    //   探针不该在测量里制造真副作用（仪器污染实验）。
    if (!m_sess->replay) { m_answerProbeDone = true; return; }

    // 找第一个权限门：**记 timelineIdx 而不是 RowRec***，因为重建会清空 rows，
    // 拿着的引用会立刻悬垂。timelineIdx 是模型侧坐标，重建不动它。
    size_t askTimeline = static_cast<size_t>(-1);
    for (const RowRec& r : m_sess->rows) {
        if (r.ask) { askTimeline = r.timelineIdx; break; }
    }
    if (askTimeline == static_cast<size_t>(-1) || askTimeline >= m_sess->timeline.size()) {
        // 夹具里没有 ask ⇒ 诚实记一条"没有可测的权限门"，别静默跳过
        AnswerProbeRec rec;
        rec.phase = "no-ask";
        rec.firstDivergence = "(fixture has no ask row)";
        rec.matched = false;
        m_answerProbeLog.push_back(std::move(rec));
        m_answerProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }

    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // ★★ 视口陷阱（第一版只有 demo 通过，就栽在这里）：夹具里的 ask 可以有很多个
    //   （demo 1 / cot 2 / realistic 11 / scale 62），而转录重放完默认停在**底部**
    //   ⇒ 第一个 ask 行通常在视口**之上**。
    //   而 `Widget::HitTest` 对 ScrollView 这一类做了 `ClipsChildren() && !ContainsRect`
    //   ⇒ 落在裁剪区外的坐标在**子树里**找不到目标，于是一路回落到兄弟节点
    //   （滚动区下方的 composer / 空白）。合成点击**不报错、也没有任何变化**，
    //   读数看上去就是"回调没接上"——一个纯粹由探针位置造成的假缺陷。
    //   demo 的 1 个 ask 恰好落在视口内，所以首轮只有它通过：**单夹具通过不等于判据成立**。
    auto bringIntoView = [&]() {
        if (!m_scroll) return;
        for (RowRec& r : m_sess->rows) {
            if (!r.ask || r.timelineIdx != askTimeline || !r.panel) continue;
            const int y = r.panel->GetY();
            const int h = r.panel->GetHeight();
            const int viewH = m_scroll->GetHeight();
            const int cur = m_scroll->GetScrollOffsetY();
            if (y < cur + 8) {
                m_scroll->SetContentOffset(0, std::max(0, y - 8));
            } else if (y + h > cur + viewH - 8) {
                m_scroll->SetContentOffset(0, std::max(0, y + h - viewH + 8));
            }
            return;
        }
    };
    bringIntoView();

    auto askRow = [&]() -> RowRec* {
        for (RowRec& r : m_sess->rows) {
            if (r.ask && r.timelineIdx == askTimeline) return &r;
        }
        return nullptr;
    };
    auto askRowCount = [&]() {
        size_t n = 0;
        for (const RowRec& r : m_sess->rows) if (r.ask) ++n;
        return n;
    };
    auto answeredCount = [&]() {
        size_t n = 0;
        for (const RowRec& r : m_sess->rows) if (r.ask && r.answered) ++n;
        return n;
    };
    auto modelAnswers = [&]() {
        size_t n = 0;
        for (const auto& e : m_sess->timeline) if (!e.answer.empty()) ++n;
        return n;
    };
    auto modelAnswer = [&]() { return m_sess->timeline[askTimeline].answer; };
    // ★ 屏幕侧独立读数：从文字色反推（ECDI 无 style getter，这是唯一出口）
    auto screenAnswer = [&]() -> std::string {
        const RowRec* r = askRow();
        if (!r || !r->allowBtn || !r->denyBtn) return "unknown";
        const ECDI::Color a = r->allowBtn->GetTextColor();
        const ECDI::Color d = r->denyBtn->GetTextColor();
        const bool allowNormal = (a == kWarnInk);
        const bool denyNormal = (d == kDenyInk);
        const bool allowGray = (a == harness::kBtnDisabledInk);
        const bool denyGray = (d == harness::kBtnDisabledInk);
        if (allowNormal && denyGray) return "allow";
        if (denyNormal && allowGray) return "deny";
        if (allowNormal && denyNormal) return "";       // 未答：两钮都常态
        return "unknown";                               // 两个都灰 = 答了但不认识的 choice
    };
    auto buttonsDisabled = [&]() {
        const RowRec* r = askRow();
        if (!r || !r->allowBtn || !r->denyBtn) return false;
        return !r->allowBtn->IsEnabled() && !r->denyBtn->IsEnabled();
    };

    auto snap = [&](const char* phase) {
        AnswerProbeRec rec;
        rec.phase = phase;
        rec.askRows = askRowCount();
        rec.answeredRows = answeredCount();
        rec.modelAnswers = modelAnswers();
        rec.answerModel = modelAnswer();
        rec.answerOnScreen = screenAnswer();
        rec.buttonsDisabled = buttonsDisabled();
        m_answerProbeLog.push_back(std::move(rec));
    };
    auto expect = [&](const char* phase, std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        AnswerProbeRec& rec = m_answerProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };
    auto clickCentreOf = [&](ECDI::Widget* w) {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + w->GetWidth() / 2,
                          static_cast<int>(abs.y) + w->GetHeight() / 2);
        return true;
    };

    // ① 未答态（负对照⑥）：答案为空、两钮都还能点。
    snap("baseline");
    {
        AnswerProbeRec& rec = m_answerProbeLog.back();
        const RowRec* r = askRow();
        const bool bothEnabled = r && r->allowBtn && r->denyBtn &&
                                 r->allowBtn->IsEnabled() && r->denyBtn->IsEnabled();
        rec.matched = (rec.askRows >= 1) && (rec.answerModel.empty()) &&
                      (rec.answerOnScreen.empty()) && bothEnabled;
        if (!rec.matched) {
            rec.firstDivergence = "baseline should be unanswered with both buttons enabled";
        }
    }

    // ② 点「允许」（真实派发链）⇒ 结论进模型 + 上屏 + 两钮置灰
    {
        clickCentreOf(askRow() ? askRow()->allowBtn : nullptr);
        expect("answered", {{askRow() != nullptr, "ask row vanished"},
                            {modelAnswer() == "allow", "model answer != allow"},
                            {screenAnswer() == "allow", "screen does not show allow"},
                            {buttonsDisabled(), "buttons still enabled after answering"}});
    }

    // ③ 再点一次同一个钮 ⇒ 必须**零变化**（禁用控件已退出命中；不得重复记账）
    {
        const size_t rowsBefore = m_sess->rows.size();
        const size_t noticesBefore = m_sess->timeline.size();
        clickCentreOf(askRow() ? askRow()->allowBtn : nullptr);
        expect("reclick", {{modelAnswer() == "allow", "answer changed on second click"},
                           {screenAnswer() == "allow", "screen changed on second click"},
                           {m_sess->rows.size() == rowsBefore, "second click added a row"},
                           {m_sess->timeline.size() == noticesBefore, "second click added an entry"}});
    }

    // ④ forward：重建 ⇒ 行快照 A==B，且结论仍在（这条是本步的核心判据）
    std::vector<std::string> answeredRows;
    {
        answeredRows = SnapshotRows();
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("forward", answeredRows, SnapshotRows(), false);
        const bool conditionsOk = (cmp.mismatches == 0) && modelAnswer() == "allow" &&
                                  screenAnswer() == "allow" && buttonsDisabled();
        expect("forward", {{conditionsOk, "forward rebuild lost the answer or changed rows"}});
        // ★ 差异文本只在失败时写：无条件覆盖会把 `expect` 记下的"哪一条没满足"
        //   换成一句与条件无关的比对摘要，排查的人反而看不到断言的失败原因。
        if (!conditionsOk && m_answerProbeLog.back().firstDivergence.empty()) {
            m_answerProbeLog.back().firstDivergence = cmp.firstDivergence;
        }
    }

    // ⑤ negative：把模型里的答案改成 `deny` 再重建 ⇒ 比对必须**不**相等，
    //    且首个分歧必须指向 `answer` 字段（否则 ④ 的相等可能只是"两边都没读这段"）。
    //   ★ 负对照的"通过"= 看见了差异，故这里 `matched` 记**false**是正确的读数，
    //     不能把它当失败——说明文字写进 `firstDivergence` 前缀。
    {
        m_sess->timeline[askTimeline].answer = "deny";
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("negative", answeredRows, SnapshotRows(), false);
        snap("negative");   // ★ 先 push 再取引用：push_back 可能重分配，先取引用会悬垂
        AnswerProbeRec& rec = m_answerProbeLog.back();
        const bool sawDifference = (cmp.mismatches != 0);
        // ★ 用 `find("answer [")` 而不是 `rfind("answer", 0)`：后者只在**位置 0** 匹配，
        //   而差异文本形如 `idx=7 A/q1 -> A/q1 @answer [allow] -> [deny]`，
        //   字段名永远不在开头 ⇒ 会把"差异正确指向 answer"误判成"没指向"。
        //   （第一版就是这个 bug：四个夹具的 negative 相全部报
        //    `divergence not in the answer field: ... @answer [...]`——差异明明就在 answer 上。）
        const bool pointedAtAnswer = cmp.firstDivergence.find("answer [") != std::string::npos;
        const bool screenFollowed = (screenAnswer() == "deny");
        rec.matched = false;   // 负对照的期望就是"不匹配"
        if (sawDifference && pointedAtAnswer && screenFollowed) {
            rec.firstDivergence = "ok(expected divergence): " + cmp.firstDivergence;
        } else if (!sawDifference) {
            rec.firstDivergence = "negative control saw no difference -- snapshot ignores answer";
        } else if (!pointedAtAnswer) {
            rec.firstDivergence = "divergence not in the answer field: " + cmp.firstDivergence;
        } else {
            rec.firstDivergence += "screen did not follow the mutated answer";
        }
    }

    // ⑥ 复原：答案改回 `allow` ⇒ 必须重新与 forward 快照相等（单变量纪律）
    {
        m_sess->timeline[askTimeline].answer = "allow";
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("restored", answeredRows, SnapshotRows(), false);
        const bool conditionsOk = (cmp.mismatches == 0) && modelAnswer() == "allow" &&
                                  screenAnswer() == "allow" && buttonsDisabled();
        expect("restored", {{conditionsOk, "restore did not return to the forward state"}});
        if (!conditionsOk && m_answerProbeLog.back().firstDivergence.empty()) {
            m_answerProbeLog.back().firstDivergence = cmp.firstDivergence;
        }
    }

    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_answerProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建 + 运行期主题切换的取证。
//   【与前面五个探针的形状差异】那些是"一步做完"的（同一个 tick 里造前置、重建、比对），
//   因为局部重建是同步的。本探针不行：
//     ① 主题切换的正确入口是**点顶栏那个 Button**，而回调只置 `m_uiRebuildPending`
//        （派发栈上不能拆树）⇒ 重建要等到**下一个 tick 的开头**才发生；
//     ② 要证明"重建真的发生了"，必须读 `m_uiRebuilds` **在重建之后**的值。
//   ⇒ 故写成跨 tick 状态机（`settle` 与 `RunRebuildProbe` 同款 400ms：足够让
//     重建落地 + 一帧画完 + HUD 刷完）。
//   【判据的骨架】所有 rebuild 类相都比 `m_themeUiBefore`（baseline 四路快照）：
//     · `matched == true`  —— 侧栏/转录/输入框/状态标记四路一处不差；
//     · `paints` **恰好等于** baseline —— 这是"仪器没被实验打坏"的唯一读数；
//     · `frames`/`frameUsTotal` 恰好相等 —— `SeedCounters` 真的把历史搬回来了。
//   【负对照】`negative` 相把 `m_sidebarVisible` 单独改掉再重建：四路比对应**报出**
//     `state: sidebarVisible [1] -> [0]`。它证明 ①②③ 的相等不是"量具瞎了"。
void HarnessApp::RunThemeProbe() {
    if (!m_sess) { m_themeProbeDone = true; return; }
    // ★ 只在回放模式：live 下重建会... 其实也安全（child 是 Session 的，不受控件树影响），
    //   但 live 的转录在持续增长 ⇒ 两次快照之间必然不同，`matched` 会假失败。
    //   探针测的是"重建保状态"，不是"重建和流式数据赛跑"，故限定回放。
    if (!m_sess->replay) { m_themeProbeDone = true; return; }
    if (!m_hud.themeToggle) { m_themeProbeDone = true; return; }

    if (m_themeProbeStep > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_themeProbeNextAt) return;
    }
    const auto settle = std::chrono::milliseconds(400);

    auto snap = [&](const char* phase) {
        ThemeProbeRec rec;
        rec.phase = phase;
        rec.light = harness::g_themeLight;
        rec.uiRebuilds = m_uiRebuilds;
        rec.paints = m_counting ? m_counting->PaintCount() : 0;
        rec.frames = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        rec.sidebarRows = m_hud.sessList
                              ? static_cast<int>(m_hud.sessList->GetContentView().GetChildCount())
                              : 0;
        rec.transcriptRows = static_cast<int>(m_sess->rows.size());
        rec.inputText = (m_promptBar && m_promptBar->input())
                            ? m_promptBar->input()->GetText() : std::string();
        rec.sidebarVisible = (m_hud.sidebar != nullptr) && m_hud.sidebar->IsVisible();
        rec.activeIndex = m_activeIndex;
        rec.sessionCount = m_sessions.size();
        m_themeProbeLog.push_back(std::move(rec));
    };
    // 记一条并把四路比对结果写进去（`paints` 等三项仪器读数单独判）
    auto snapCompare = [&](const char* phase, size_t paintsBefore, size_t framesBefore,
                           size_t frameUsBefore) {
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_themeUiBefore, after);
        snap(phase);
        ThemeProbeRec& rec = m_themeProbeLog.back();
        std::string bad = cmp.matched ? std::string() : cmp.firstDivergence;
        // ★ 仪器读数：重建期间不该有任何重绘 ⇒ 必须**恰好**相等。
        //   变大/变小都是缺陷，故不用 ">=" 之类的宽容判据。
        if (after.paints != paintsBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "paints " + std::to_string(paintsBefore) + " -> "
                   + std::to_string(after.paints);
        }
        if (after.frames != framesBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "frameCount " + std::to_string(framesBefore) + " -> "
                   + std::to_string(after.frames);
        }
        if (after.frameUsTotal != frameUsBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "frameUsTotal " + std::to_string(frameUsBefore) + " -> "
                   + std::to_string(after.frameUsTotal);
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        rec.paints = after.paints;
        rec.frames = after.frames;
        rec.frameUsTotal = after.frameUsTotal;
    };

    switch (m_themeProbeStep) {
    case 0: {
        // ── ① baseline：抓四路快照 + 仪器读数，然后**同步**做一次重建 ──
        //   ★ 这里可以同步重建：本函数跑在 `OnReplayTick` 里（timer 回调，不在派发栈上）。
        //     而 `toggle` 相不行——那一相必须走真实点击，故分了两条路径。
        m_themeUiBefore = SnapshotUi();
        m_themePaintsBefore = m_themeUiBefore.paints;
        m_themeFramesBefore = m_themeUiBefore.frames;
        snap("baseline");
        {
            ThemeProbeRec& rec = m_themeProbeLog.back();
            // baseline 的判据是"前置条件成立"：必须有 ≥1 个会话行、≥1 个转录行，
            // 否则"重建后都一样"只说明两边都是空的（探针是瞎的）。
            rec.matched = (rec.sidebarRows >= 1) && (rec.transcriptRows >= 1);
            if (!rec.matched) {
                rec.firstDivergence = "baseline has nothing to preserve (sidebar/transcript empty)";
            }
        }
        // 造判别力：给输入框塞一段草稿（这是唯一无法从模型重建的状态，
        // 若探针不主动造它，"草稿被搬过去了"永远是空真）。
        if (m_promptBar && m_promptBar->input()) {
            m_promptBar->input()->SetText("draft-not-sent");
            m_promptBar->input()->MoveCaretToEnd();
            // ★ 塞完草稿要重抓快照：`m_themeUiBefore` 必须包含这段草稿，
            //   否则重建后"草稿还在"这条就成了拿空串对比空串。
            m_themeUiBefore = SnapshotUi();
            m_themePaintsBefore = m_themeUiBefore.paints;
            m_themeFramesBefore = m_themeUiBefore.frames;
        }
        RebuildUi();
        // ★ 重建后立刻比对（同一 tick，未经过任何重绘）
        snapCompare("rebuild", m_themePaintsBefore, m_themeFramesBefore,
                    m_themeUiBefore.frameUsTotal);
        // ★ 额外：`uiRebuilds` 必须真的 +1（这是"重建发生了"的机读证据；
        //   若忘了 ++，后面的 toggle 相还会通过，但"重建"其实一次都没跑）。
        if (m_themeProbeLog.back().uiRebuilds != 1) {
            m_themeProbeLog.back().matched = false;
            m_themeProbeLog.back().firstDivergence +=
                "; uiRebuilds expected 1, got " + std::to_string(m_themeProbeLog.back().uiRebuilds);
        }
        m_themeProbeStep = 1;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 1: {
        // ── ② rebuild-again：再同步重建一次 ⇒ 幂等（第二次也得全等） ──
        const size_t p = m_counting ? m_counting->PaintCount() : 0;
        const size_t f = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        uint64_t t = 0;
        if (m_counting) for (uint64_t us : m_counting->FrameDurationsUs()) t += us;
        RebuildUi();
        snapCompare("rebuild-again", p, f, static_cast<size_t>(t));
        m_themeProbeStep = 2;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 2: {
        // ── ③ toggle：**真实点击**顶栏「主题」钮 ⇒ 下一个 tick 才会重建 ──
        //   ★ 这一相是本步的命题本身：运行期换主题。
        //   ★ 用 `SynthesizeClickAt` 走真实派发链（`Application::OnEvent`），
        //     不是直接调 `RebuildUi`——否则"按钮接上了没有"这件事没被验证
        //     （0c 的 `buildTitle` 正是这样骗过判据的：画对了，回调没接）。
        const ECDI::Point abs = m_hud.themeToggle->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.themeToggle->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.themeToggle->GetHeight() / 2);
        m_themeProbeStep = 3;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 3: {
        // ── ④ 读数：重建必须真的发生（uiRebuilds 2→3）且真的换色 ──
        snap("toggle");
        {
            ThemeProbeRec& rec = m_themeProbeLog.back();
            std::string bad;
            // 注意：`m_themeUiBefore.light` 是深色时的值；翻转后应相反
            if (rec.light == m_themeUiBefore.light) bad += "theme did not flip";
            if (rec.uiRebuilds != 3) {
                if (!bad.empty()) bad += "; ";
                bad += "uiRebuilds expected 3, got " + std::to_string(rec.uiRebuilds);
            }
            // ★ 主题真的换了 ⇒ 侧栏/转录的四路快照**结构**仍必须相同
            //   （换的是颜色，不是内容。`SnapshotSessionList`/`SnapshotRows` 都只记
            //   文字与几何、不记颜色，故此处相等是正确判据；颜色由 `light` 字段证明）。
            const UiSnapshot after = SnapshotUi();
            const SidebarCompare cmp = CompareUi(m_themeUiBefore, after);
            if (!cmp.matched) {
                if (!bad.empty()) bad += "; ";
                bad += cmp.firstDivergence;
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
            rec.sidebarRows = static_cast<int>(after.sidebar.size());
            rec.transcriptRows = static_cast<int>(after.transcript.size());
            rec.inputText = after.inputText;
        }
        m_themeProbeStep = 4;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 4: {
        // ── ⑤ toggle-back：再点一次 ⇒ 回到深色，且状态一致 ──
        const ECDI::Point abs = m_hud.themeToggle->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.themeToggle->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.themeToggle->GetHeight() / 2);
        m_themeProbeStep = 5;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 5: {
        snap("toggle-back");
        {
            ThemeProbeRec& rec = m_themeProbeLog.back();
            std::string bad;
            if (rec.light != m_themeUiBefore.light) bad += "theme did not flip back";
            if (rec.uiRebuilds != 4) {
                if (!bad.empty()) bad += "; ";
                bad += "uiRebuilds expected 4, got " + std::to_string(rec.uiRebuilds);
            }
            const UiSnapshot after = SnapshotUi();
            const SidebarCompare cmp = CompareUi(m_themeUiBefore, after);
            if (!cmp.matched) {
                if (!bad.empty()) bad += "; ";
                bad += cmp.firstDivergence;
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
        }
        m_themeProbeStep = 6;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 6: {
        // ── ⑥ negative：单独改 `m_sidebarVisible`（不改任何控件）再重建 ──
        //   ★ 这是"比对面能看见模型侧状态"的唯一证据：若 `CompareUi` 只看控件，
        //     这条差异会被漏掉（新树默认全可见，重建后侧栏又是可见的 ⇒ 控件本身
        //     看不出"模型里说要收起"）。故断言**必须**报出 `state: sidebarVisible`。
        const bool savedSidebar = m_sidebarVisible;
        m_sidebarVisible = false;
        // 不清脏标志重建：`RebuildUi` 的 ⑩ 会用 `m_sidebarVisible` 恢复成隐藏
        const size_t p = m_counting ? m_counting->PaintCount() : 0;
        const size_t f = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        uint64_t t = 0;
        if (m_counting) for (uint64_t us : m_counting->FrameDurationsUs()) t += us;
        RebuildUi();
        // ★ 现在侧栏是**隐藏**的 ⇒ 与 baseline（可见）比对必须不等，
        //   且分歧必须指向 sidebarVisible。
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_themeUiBefore, after);
        snap("negative");
        ThemeProbeRec& rec = m_themeProbeLog.back();
        rec.paints = p;   // 仪器读数对本相无意义（此处只测模型状态是否进比对）
        rec.frames = f;
        rec.frameUsTotal = static_cast<size_t>(t);
        const bool sawDifference = !cmp.matched;
        const bool pointedAtSidebar =
            cmp.firstDivergence.find("sidebarVisible") != std::string::npos;
        // ★ 负对照的"通过"= 看见了差异 ⇒ `matched` 记 false 是**正确读数**。
        rec.matched = false;
        if (sawDifference && pointedAtSidebar) {
            rec.firstDivergence = "ok(expected divergence): " + cmp.firstDivergence;
        } else if (!sawDifference) {
            rec.firstDivergence = "negative control saw no difference -- snapshot ignores state";
        } else {
            rec.firstDivergence = "divergence not in sidebarVisible: " + cmp.firstDivergence;
        }
        // 复原为原状态（单变量纪律：negative 之后必须能回到 baseline）
        m_sidebarVisible = savedSidebar;
        if (m_hud.sidebar) m_hud.sidebar->SetVisible(savedSidebar);
        if (m_hud.sidebar) {
            if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();
        }
        m_themeProbeStep = 7;
        m_themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 7: {
        // ── ⑦ restored：确认回到 baseline（含侧栏可见性、主题、仪器读数） ──
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_themeUiBefore, after);
        snap("restored");
        ThemeProbeRec& rec = m_themeProbeLog.back();
        std::string bad = cmp.matched ? std::string() : cmp.firstDivergence;
        if (after.light != m_themeUiBefore.light) {
            if (!bad.empty()) bad += "; ";
            bad += "theme not restored";
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        m_themeProbeStep = 8;
        m_themeProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    default:
        m_themeProbeDone = true;
        return;
    }
}

// ★★ Step 5：打开历史会话的取证。
//
// 这条探针要回答三个问题，每个都必须能被反驳：
//   ① **列表是真的从磁盘扫出来的吗？** —— 磁盘文件数 == 屏幕行数（探针自己扫一遍，
//      不只信 UI）。负对照：两者不等就必须报出来。
//   ② **"打开"是真的走回放消费吗？** —— 打开后的 blocks/deltas/messages 必须与
//      **独立**再喂一遍同一文件（`DigestReplayFile`）完全相等，且 `replay->Empty()`
//      为真（真的跳到末尾了）。若实现改成"一次读到底、GUI 自己造 Block"，
//      坏文件那几份（hostile/）的读数会立刻不等，且 seqGaps 变成 0。
//   ③ **GUI 侧状态能存能读回来吗？** —— 答一次 ⇒ sidecar 落盘；重新打开同一文件 ⇒
//      模型里 answer 非空、屏幕上那个钮仍是常态色。**缺失时**则必须如实提示
//      「此会话没有决策记录」，而不是静默当成"从未回答过"（Step 3 修的就是这条病）。
void HarnessApp::RunHistoryProbe() {
    if (!m_sess) { m_historyProbeDone = true; return; }
    if (!m_sess->replay) { m_historyProbeDone = true; return; }
    if (!m_hud.historyList) { m_historyProbeDone = true; return; }
    if (m_historyProbeStep > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_historyProbeNextAt) return;
    }
    const auto settle = std::chrono::milliseconds(300);

    auto rowsOnScreen = [&]() -> size_t {
        return m_hud.historyList->GetContentView().GetChildCount();
    };
    auto snap = [&](const char* phase) {
        HistoryProbeRec rec;
        rec.phase = phase;
        rec.filesOnDisk = FindHistoryFiles().size();
        rec.rowsOnScreen = rowsOnScreen();
        if (m_sess) {
            rec.openedLabel = m_sess->label;
            rec.originKind = m_sess->originKind;
            rec.blocks = m_sess->transcript.Blocks();
            rec.deltas = m_sess->transcript.Deltas();
            rec.messages = m_sess->replay ? m_sess->replay->Total() : 0;
            rec.atEnd = m_sess->replay ? m_sess->replay->Empty() : false;
            for (const RowRec& r : m_sess->rows) {
                if (!r.ask) continue;
                ++rec.askRows;
                if (r.answered) ++rec.answeredRows;
            }
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) ++rec.modelAnswers;
            }
        }
        m_historyProbeLog.push_back(std::move(rec));
    };
    auto finish = [&](const char* phase, std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        HistoryProbeRec& rec = m_historyProbeLog.back();
        std::string bad;
        for (const auto& [ok, what] : conds) {
            if (ok) continue;
            if (!bad.empty()) bad += "; ";
            bad += what;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    switch (m_historyProbeStep) {
    case 0: {
        // ── ① list：磁盘 == 屏幕，且至少有一个可点的目标 ──
        RebuildHistoryList();   // 直接重建，保证计数是最新的
        const size_t onDisk = FindHistoryFiles().size();
        // 目标文件：**优先当前 --replay 的那个文件**——这样每个夹具测的都是自己，
        //   且 `list` 相与 `cleanup` 相的活动会话读数可以直接当对照
        //   （cot 打开 cot、scale 打开 scale，判别力比"永远打开 demo"强得多）。
        //   ★ 若当前会话不是文件来源（live / 空会话），退到 demo（它有权限门，
        //     sidecar 三态才有意义），再退到第一个。
        m_historyProbeFile.clear();
        const std::filesystem::path currentFile =
            (m_sess->originPath.empty() || !m_sess->originPath.has_extension())
                ? std::filesystem::path()
                : m_sess->originPath;
        if (!currentFile.empty()) {
            // ★ 必须规范化再比：`--replay` 给的是相对路径（`protocol/fixtures/...`），
            //   而 `FindHistoryFiles` 返回的是绝对路径。字典序比较会永远不等。
            std::error_code ec;
            const std::filesystem::path want = std::filesystem::weakly_canonical(currentFile, ec);
            for (const std::filesystem::path& f : m_historyFiles) {
                std::error_code ec2;
                if (std::filesystem::weakly_canonical(f, ec2) == want) {
                    m_historyProbeFile = f;
                    break;
                }
            }
        }
        if (m_historyProbeFile.empty()) {
            for (const std::filesystem::path& f : m_historyFiles) {
                if (f.filename() == "demo-session.jsonl") { m_historyProbeFile = f; break; }
            }
        }
        if (m_historyProbeFile.empty() && !m_historyFiles.empty()) {
            m_historyProbeFile = m_historyFiles.front();
        }
        m_historyProbeDigest = DigestReplayFile(m_historyProbeFile);
        const bool existed = !m_historyProbeFile.empty() &&
            std::filesystem::is_regular_file(m_historyProbeFile.generic_string() + ".ui.json");
        finish("list", {{onDisk > 0, "no fixture found on disk"},
                        {rowsOnScreen() == onDisk, "row count != file count on disk"},
                        {!m_historyProbeFile.empty(), "no target file chosen"},
                        {m_historyProbeDigest.loaded && m_historyProbeDigest.blocks > 0,
                         "digest of target file is empty"}});
        m_historyProbeLog.back().sidecarExisted = existed;
        m_historyProbeStep = 1;
        m_historyProbeNextAt = std::chrono::steady_clock::now();   // 立刻进下一步
        return;
    }
    case 1: {
        // ── ② 点真实的历史行（走真实派发链，不是直接调 OpenHistorySession）──
        size_t targetRow = 0;
        for (size_t i = 0; i < m_historyFiles.size(); ++i) {
            if (m_historyFiles[i] == m_historyProbeFile) { targetRow = i; break; }
        }
        ECDI::Widget* row = m_hud.historyList->GetContentView().GetChildAt(targetRow);
        if (!row) {
            finish("open", {{false, "history row widget missing"}});
            m_historyProbeStep = 9;
            m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
            return;
        }
        // ★★ 视口陷阱（Step 3 的同族，这里换了个容器）：历史区最多显示 4 行，
        //   第 5、6 个文件（realistic / scale）在裁剪区外。`Widget::HitTest` 对
        //   `ClipsChildren()` 的子树有 `!ContainsRect` 早退 ⇒ 落在裁剪区外的坐标
        //   在子树里找不到目标、**静默回落到别处**：点击不报错、也没有任何变化，
        //   读数伪装成"打开逻辑坏了"。故先把目标行滚进视口。
        {
            const int y = row->GetY();
            const int h = row->GetHeight();
            const int viewH = m_hud.historyList->GetHeight();
            const int cur = m_hud.historyList->GetScrollOffsetY();
            if (y < cur || y + h > cur + viewH) {
                m_hud.historyList->SetContentOffset(0, std::max(0, y));
            }
        }
        const ECDI::Point abs = row->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + row->GetWidth() / 2,
                          static_cast<int>(abs.y) + row->GetHeight() / 2);
        m_historyProbeStep = 2;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 2: {
        // ── ③ open：确实是"历史"来源、真的跳到末尾、内容与独立回放一致 ──
        const ReplayDigest& d = m_historyProbeDigest;
        finish("open", {
            {m_sess->originKind == "replay-history", "originKind is not replay-history"},
            {m_sess->label == m_historyProbeFile.filename().generic_string(),
             "opened session label != file name"},
            {m_sess->replay && m_sess->replay->Empty(), "replay not drained to the end"},
            {m_sess->transcript.Blocks() == d.blocks, "blocks != independent digest"},
            {m_sess->transcript.Deltas() == d.deltas, "deltas != independent digest"},
            {(m_sess->replay ? m_sess->replay->Total() : 0) == d.messages,
             "messages != independent digest"},
        });
        m_historyProbeStep = 3;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 3: {
        // ── ④ sidecar-missing：新夹具没有状态文件 ⇒ 权限门必须**未答**，
        //      且必须有一条如实的提示（不能假装"从未问过"）。
        const bool noAnswersYet = [&] {
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) return false;
            }
            return true;
        }();
        const bool noticeSaid = [&] {
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind != TimelineEntry::Kind::Notice) continue;
                if (e.text.find("没有决策记录") != std::string::npos) return true;
            }
            return false;
        }();
        bool anyAsk = false;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        std::vector<std::pair<bool, const char*>> conds;
        conds.push_back({noAnswersYet, "model already has answers before any click"});
        if (anyAsk) {
            conds.push_back({noticeSaid, "missing sidecar was not announced"});
        }
        finish("sidecar-missing", conds);
        m_historyProbeLog.back().sidecarExisted = false;
        m_historyProbeStep = 4;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 4: {
        // ── ⑤ sidecar-write：答一次 ⇒ 状态必须落盘，且内容里真有这个 ask id ──
        ECDI::Button* allow = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (r.ask && r.allowBtn) { allow = r.allowBtn; break; }
        }
        if (!allow) {
            // 目标夹具没有权限门 ⇒ 这一相与下一相无意义，如实记下而不是静默跳过。
            finish("sidecar-write", {{false, "(target fixture has no ask row)"}});
            m_historyProbeStep = 7;
            m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
            return;
        }
        // ★★ 同一个视口陷阱的第三次出场：历史会话是**直接拉到末尾**打开的，
        //   第一个权限门通常远在视口之上（cot 2 个 ask、realistic 11 个、
        //   scale 62 个；只有 demo 的 1 个恰好在屏幕附近）。点击落在 ScrollView
        //   裁剪区外时 `HitTest` 早退、**零变化零报错**，读数会显示
        //   "sidecar 没写"——但真正没发生的是"点击"。先把它滚进来。
        for (RowRec& r : m_sess->rows) {
            if (!r.ask || !r.panel) continue;
            const int y = r.panel->GetY();
            const int h = r.panel->GetHeight();
            const int viewH = m_scroll ? m_scroll->GetHeight() : 0;
            const int cur = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
            if (m_scroll && (y < cur || y + h > cur + viewH)) {
                m_scroll->SetContentOffset(0, std::max(0, y - 8));
            }
            break;
        }
        const ECDI::Point abs = allow->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + allow->GetWidth() / 2,
                          static_cast<int>(abs.y) + allow->GetHeight() / 2);
        m_historyProbeStep = 5;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 5: {
        const std::filesystem::path statePath = UiStatePath(*m_sess);
        const bool written = !statePath.empty() &&
                             std::filesystem::is_regular_file(statePath);
        bool hasAnswer = false;
        if (written) {
            std::ifstream in(statePath, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
            hasAnswer = text.find("\"allow\"") != std::string::npos;
        }
        finish("sidecar-write", {{written, "sidecar file was not written"},
                                 {hasAnswer, "sidecar does not contain the answer"},
                                 {m_sess->originKind == "replay-history",
                                  "write happened for a non-history session"}});
        m_historyProbeLog.back().sidecarWritten = written;
        m_historyProbeStep = 6;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 6: {
        // ── ⑥ sidecar-roundtrip：重新打开同一文件 ⇒ 结论必须回来 ──
        //   ★ 直接调 `OpenHistorySession`（此刻不在派发栈上，安全），
        //     而不是再点一次行：点行这一相已在 ② 验过，这里测的是**读回**。
        OpenHistorySession(m_historyProbeFile);
        m_historyProbeStep = 7;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 7: {
        size_t modelAnswers = 0;
        size_t screenAnswered = 0;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) ++modelAnswers;
        }
        for (const RowRec& r : m_sess->rows) {
            if (r.ask && r.answered) ++screenAnswered;
        }
        std::vector<std::pair<bool, const char*>> conds;
        // 若目标夹具根本没有 ask 行（⑤ 已记过），此相不作断言（如实写进差异里）。
        bool anyAsk = false;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        if (!anyAsk) {
            conds.push_back({false, "(target fixture has no ask row)"});
        } else {
            conds.push_back({modelAnswers >= 1, "answer was not restored into the model"});
            conds.push_back({screenAnswered >= 1, "answer was not restored onto the screen"});
        }
        finish("sidecar-roundtrip", conds);
        // ── 收尾：把探针造出来的副作用清干净（工件目录不留脏文件/脏会话）──
        //   ★ 路径必须**直接从被打开的文件**算：此刻 `m_sess` 还是历史会话，
        //     但下面会把它删掉，删完 `m_sess` 就指回原会话（originPath 是
        //     `--replay` 那个文件）⇒ 用 `UiStatePath(*m_sess)` 会算错路径、
        //     把用户的 sidecar 留下来、还删不掉自己的。
        const std::filesystem::path statePath =
            m_historyProbeFile.empty()
                ? std::filesystem::path()
                : std::filesystem::path(m_historyProbeFile.generic_string() + ".ui.json");
        const bool created = !m_historyProbeLog.front().sidecarExisted;
        if (created && !statePath.empty()) {
            std::error_code ec;
            std::filesystem::remove(statePath, ec);
            m_historyProbeLog.back().sidecarRemoved = !ec;
        }
        // 删掉探针新建的会话（从尾部删，`DeleteSessionAt` 会先切邻居再 erase）
        while (m_sessions.size() > 1) {
            DeleteSessionAt(m_sessions.size() - 1);
        }
        m_sidebarDirty = true;
        m_historyProbeStep = 8;
        m_historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 8: {
        // ── ⑦ 收尾读数：工作区真的干净了（会话数回到 1、sidecar 不再存在）──
        const bool cleanSessions = m_sessions.size() == 1;
        bool cleanSidecar = true;
        if (!m_historyProbeFile.empty()) {
            cleanSidecar = !std::filesystem::is_regular_file(
                m_historyProbeFile.generic_string() + ".ui.json");
        }
        const bool created = !m_historyProbeLog.front().sidecarExisted;
        finish("cleanup", {{cleanSessions, "sessions left behind by the probe"},
                           {cleanSidecar || !created, "sidecar left behind by the probe"}});
        m_historyProbeStep = 9;
        m_historyProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    default:
        m_historyProbeDone = true;
        return;
    }
}

// 直接回放某个文件到末尾，返回读数（**不**动活动会话、不建行、不碰 UI）。
HarnessApp::ReplayDigest HarnessApp::DigestReplayFile(
    const std::filesystem::path& file) const {
    ReplayDigest d;
    if (file.empty()) return d;
    ReplaySource source;
    std::string error;
    if (!source.Load(file, error)) return d;
    d.loaded = true;
    d.messages = source.Total();   // 与摘要里 `messages` 同口径：夹具总行数
    Transcript transcript;
    std::string line;
    std::string lineError;
    while (source.Next(line)) {
        transcript.OnLine(line, lineError);
        lineError.clear();
    }
    d.blocks = transcript.Blocks();
    d.deltas = transcript.Deltas();
    return d;
}

// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
void HarnessApp::SendPrompt(const std::string& text) {
    // ★ --hit-probe：探针合成点击时只计数、零副作用，保持旧读数。
    if (m_hitProbeActive) {
        ++m_hitProbeSendCalls;
        return;
    }
    if (m_sess->writer) {
        (void)PostPrompt(text);
        return;
    }
    AddNoticeRow("[prompt 仅记录] " + text, kDarkMuted);   // 回放：只记录
    Relayout();
    RequestRepaint();
}

std::string HarnessApp::PostPrompt(const std::string& text, bool allowBusyForProbe) {
    if (!m_sess || !m_sess->writer || m_sess->disconnected ||
        m_sess->protocolIncompatible || m_sess->protocolVer != 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        !m_sess->pendingWorkspaceId.empty()) {
        return {};
    }
    if (allowBusyForProbe) {
        if (m_sess->coreState != "running" || HasUnconfirmedPrompt()) return {};
    } else if (m_sess->coreState != "idle" || HasUnconfirmedPrompt()) {
        return {};
    }

    const std::string clientId = "p" + std::to_string(m_sess->nextClientId++);
    // ★★ m09443：发了 prompt ⇒ 临时会话转正。
    m_sess->temporary = false;
    PendingPrompt pending;
    pending.clientId = clientId;
    pending.text = text;
    m_sess->pendingPrompts.push_back(std::move(pending));

    // Materialize and paint the optimistic row before touching the reverse-channel pipe.
    MaterializePendingPrompts();
    Relayout();
    FollowTailIfPinned();
    RefreshHud();
    RequestRepaint();

    if (!m_sess->writer->Prompt(text, clientId)) {
        FailPendingOperations("prompt 写入失败，连接未确认");
    }
    return clientId;
}

bool HarnessApp::HasUnconfirmedPrompt() const {
    if (!m_sess) return false;
    return std::any_of(m_sess->pendingPrompts.begin(), m_sess->pendingPrompts.end(),
                       [](const PendingPrompt& p) { return p.status == "sending"; });
}

void HarnessApp::FailPrompt(const std::string& clientId, const std::string& reason) {
    if (!m_sess || clientId.empty()) return;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.clientId != clientId || pending.status != "sending") continue;
        pending.status = "failed";
        pending.failure = reason.empty() ? "core 未确认" : reason;
        MaterializePendingPrompts();
        Relayout();
        FollowTailIfPinned();
        RefreshHud();
        RequestRepaint();
        return;
    }
}

void HarnessApp::FailPendingOperations(const std::string& reason) {
    if (!m_sess) return;
    m_sess->disconnected = true;
    const std::string detail = reason.empty() ? "连接已断开" : reason;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.status == "sending") {
            pending.status = "failed";
            pending.failure = detail;
        }
    }
    if (!m_sess->pendingWorkspaceId.empty()) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区未确认：" + detail;
    }
    MaterializePendingPrompts();
    Relayout();
    RefreshHud();
    RequestRepaint();
}

bool HarnessApp::RequestWorkspace(const std::string& path, bool initialSync) {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer != 3 ||
        !m_sess->workspaceCap || m_sess->protocolIncompatible || m_sess->disconnected ||
        path.empty() || m_sess->coreState != "idle" ||
        !m_sess->pendingWorkspaceId.empty()) {
        return false;
    }
    if (initialSync && m_sess->workspaceSyncStarted) return false;
    if (initialSync) m_sess->workspaceSyncStarted = true;

    const std::string requestId = "w" + std::to_string(m_sess->nextWorkspaceRequestId++);
    m_sess->pendingWorkspaceId = requestId;
    m_sess->pendingWorkspaceInitialSync = initialSync;
    m_sess->workspaceStatus = "等待 core 确认…";
    RefreshHud();
    if (!m_sess->writer->Workspace(requestId, path)) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区请求发送失败";
        FailPendingOperations("workspace 写入失败，连接未确认");
        return false;
    }
    return true;
}

// ── ★★ 2026-10-20：工作区注册表（模型侧实现）────────────────────────────
// 三条不变量都在这里维护（设计注见 `App.h` 的 `struct Workspace`）：
//   ① **只追加**：`m_workspaces` 的下标即创建序，永不删除、永不重排 ⇒
//      「创建序固定」这条用户裁决不需要额外排序字段，下标本身就是证据；
//   ② **按路径去重**：同一目录的第二个会话复用同一个实体（否则侧栏会画出
//      两个同路径的标题行，而用户看到的会是"我的会话莫名其妙分成两组"）；
//   ③ **只由 core 确认过的路径创建**：`AcquireWorkspace` 的唯一调用点在 workspace
//      ack 的 `ok:true` 分支 ⇒ 注册表里不存在"GUI 自己猜的路径"。
HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

const HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::WorkspaceByPath(const std::string& path) {
    if (path.empty()) return nullptr;
    // ★ 字符串完全相等，不用 `std::filesystem::equivalent()`：入参已经是 core 回显的
    //   规范化路径（§3.9：绝对、`/` 分隔、无尾随 `/`、无 `.`/`..`）。用 equivalent
    //   会把 `D:/a` 与 `d:/A` 判成同一处——那是替 core 改写它自己的规范化结果。
    for (const auto& ws : m_workspaces) {
        if (ws && ws->path == path) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::AcquireWorkspace(const std::string& path) {
    if (path.empty()) return nullptr;
    if (Workspace* existing = WorkspaceByPath(path)) return existing;
    auto ws = std::make_unique<Workspace>();
    // ★ ID 形如 `w1`/`w2`…，与协议侧的 `request_id`（`w1`/`w2`…）**同名但不同域**：
    //   前者是 GUI 模型里的工作区实体，后者是单次请求的关联键。两者都由自增计数器
    //   产生、都从 1 开始，所以一个会话的 `pendingWorkspaceId` 常与它即将获得的
    //   `workspaceId` 字形相似——这不是缺陷（探针读的是 `Workspace::path`），
    //   但**别在代码里把两者互相当作判据**。
    ws->id = "w" + std::to_string(m_nextWorkspaceId++);
    ws->path = path;
    ws->requestedPath = path;
    m_workspaces.push_back(std::move(ws));
    return m_workspaces.back().get();
}

void HarnessApp::AssignSessionWorkspace(Session& session, Workspace* workspace) {
    session.workspaceId = workspace ? workspace->id : std::string();
}

std::string HarnessApp::InheritedWorkspacePath() const {
    if (!m_sess) return {};
    const Workspace* ws = WorkspaceById(m_sess->workspaceId);
    if (!ws) return {};
    // ★ 只读 `Workspace::path`（已确认路径），不读 `Session::workspacePath`：
    //   后者在"已发请求、等 ack"的窗口里可能还留着旧值，而在"确认失败"之后
    //   会保留失败前的值——两种情况都不是"这个会话现在在哪"的可靠答案。
    return ws->path;
}

// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：临时会话（用户裁决 m09443）
// ───────────────────────────────────────────────────────────────────────
// 从工作区标题行的「＋」按钮拉起的会话是**临时**的：它还没有执行过任何
// 任务（没发过 prompt）。一旦用户切走（`SwitchToSession` 到另一个会话），
// 临时会话就被删除——因为它的存在理由（"在这个工作区开一个空会话"）
// 已经被放弃。一旦发了 prompt（`PostPrompt` 成功），它转为正式会话。
// ═══════════════════════════════════════════════════════════════════════

void HarnessApp::NewTemporarySession(const std::string& workspaceId) {
    // 只允许 live 模式（有 core 才能"执行任务"）。
    if (m_options.corePath.empty()) return;
    const Workspace* ws = WorkspaceById(workspaceId);
    if (!ws) return;

    Session* created = CreateSessionFromOptions();
    if (!created) {
        AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }
    created->label = "会话 " + std::to_string(m_sessions.size());
    created->temporary = true;
    // 外键在创建时就设好——路径已经确认过了，工作区实体已经存在，
    // 不需要等 ack（与 `MaybeStartWorkspaceSync` 的继承口径不同：
    // 那里是"还没确认"，这里是"已经确认"）。
    created->workspaceId = workspaceId;
    created->workspacePath = ws->path;
    created->workspaceSyncComplete = true;
    created->workspaceStatus = "已由 core 确认";

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);
    Session& s = *m_sessions[idx];
    if (s.writer) s.writer->Hello("0.0.1");
    if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    m_sidebarDirty = true;
}

void HarnessApp::MaybeDeleteTemporarySession() {
    if (!m_sess || !m_sess->temporary) return;
    // ★ 只删"没发过 prompt"的临时会话：发过 prompt 的已经转正（`PostPrompt`
    //   里把 `temporary` 清掉了）。
    //   ★ 用 `m_activeIndex` 定位：此时 `m_sess` 还指着旧会话（`SwitchToSession`
    //     在改 `m_activeIndex` 之前调用本函数）。
    const size_t idx = m_activeIndex;
    if (idx >= m_sessions.size()) return;
    // 与 `DeleteSessionAt` 同一条纪律：禁删到零会话。
    if (m_sessions.size() <= 1) return;
    // 删除前不需要先切邻居——`SwitchToSession` 马上就会改 `m_activeIndex`。
    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(idx));
    // 修正活动下标（与 `DeleteSessionAt` 同逻辑）。
    if (m_activeIndex > idx) --m_activeIndex;
    if (m_activeIndex >= m_sessions.size()) m_activeIndex = m_sessions.size() - 1;
    m_sess = m_sessions[m_activeIndex].get();
    m_sidebarDirty = true;
    m_hudDirty = true;
}

// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容
// ───────────────────────────────────────────────────────────────────────
// 数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
// 回放会话没有工作区 ⇒ 此段显示「无工作区」。
// ★ 只列一级（不递归）：侧栏宽度 172px，递归树会立刻溢出；且用户要的是
//   「这个工作区里有什么」，不是文件浏览器。
// ═══════════════════════════════════════════════════════════════════════

void HarnessApp::RebuildProjectList() {
    if (!m_hud.projectList) return;
    ECDI::Widget& content = m_hud.projectList->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    // 数据源：活动会话的已确认工作区路径。
    std::string wsPath;
    if (m_sess && !m_sess->workspacePath.empty()) {
        wsPath = m_sess->workspacePath;
    }

    int y = 0;
    const int rowH = 16;
    const int gap = 2;

    if (wsPath.empty()) {
        // 回放会话 / 无工作区：诚实显示。
        auto lbl = uikit::Label("无工作区", kSidebarListW, rowH, kDarkMuted,
                                harness::kFontMicro);
        lbl->SetPosition(0, y);
        content.AddChild(std::move(lbl));
        y += rowH + gap;
    } else {
        // 列一级目录内容。
        std::error_code ec;
        std::vector<std::string> dirs;
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(
                 std::filesystem::path(wsPath), ec)) {
            const std::string name = entry.path().filename().generic_string();
            if (name.empty()) continue;
            if (entry.is_directory(ec)) {
                dirs.push_back(name + "/");
            } else {
                files.push_back(name);
            }
            ec.clear();
        }
        // 目录在前、文件在后，各自按名称排序（确定性）。
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        for (const auto& d : dirs) {
            auto lbl = uikit::Label("▽ " + d, kSidebarListW, rowH, kDarkInk,
                                    harness::kFontMicro, true);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
        for (const auto& f : files) {
            auto lbl = uikit::Label("· " + f, kSidebarListW, rowH, kDarkMuted,
                                    harness::kFontMicro, true);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
        if (dirs.empty() && files.empty()) {
            auto lbl = uikit::Label("（空目录）", kSidebarListW, rowH, kDarkMuted,
                                    harness::kFontMicro);
            lbl->SetPosition(0, y);
            content.AddChild(std::move(lbl));
            y += rowH + gap;
        }
    }

    const int totalH = y > 0 ? y - gap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * (rowH + gap) - gap);
    m_hud.projectList->SetSize(kSidebarListW, std::max(visibleH, rowH));
    m_hud.projectList->SetContentExtent(kSidebarListW, std::max(totalH, rowH));
    m_hud.projectList->SetContentOffset(0, m_hud.projectList->GetScrollOffsetY());


    if (ECDI::Widget* parent = m_hud.projectList->GetParent()) parent->Arrange();
}

void HarnessApp::MaybeStartWorkspaceSync() {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer != 3 ||
        !m_sess->workspaceCap || m_sess->workspaceSyncStarted ||
        m_sess->disconnected || m_sess->coreState != "idle") {
        return;
    }
    std::error_code ec;
    // ★★ 2026-10-20（用户裁决 m08553）：新会话**继承活动会话的工作区**。
    //   改动前这里恒用 `std::filesystem::current_path()`，即 GUI 进程的 cwd；
    //   而 `ChildSource::Start` 的 `CreateProcessW` 又不传 `lpCurrentDirectory`
    //   （子进程也继承 GUI cwd）⇒ 用户在会话 A 里把工作区切到 `D:/Project` 之后
    //   点「＋ 新建会话」，会话 B 会**静默**回到启动 GUI 的那个目录。屏幕上看不出
    //   这件事（两行会话长得一样、工作区段显示的是活动会话的值），只有对照
    //   core 的 stderr 才能发现——所以这条不是"体验优化"，是**纠错**。
    std::string initialPath = m_sess->workspaceInheritedPath;
    if (initialPath.empty()) {
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (ec) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "无法读取启动工作目录：" + ec.message();
            RefreshHud();
            return;
        }
        initialPath = PathToUtf8(cwd);
    }
    // ★ 继承路径只消费一次：若这次请求被 core 拒绝，**不**自动改用 cwd 重试。
    //   被拒不等于"路径不存在"（core 可能因自己的原因拒），而擅自换一个目录重问
    //   会让屏幕上的路径与用户以为的那个不同——那正是 §3.9 要避免的"GUI 猜"。
    m_sess->workspaceInheritedPath.clear();
    if (!RequestWorkspace(initialPath, true)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "启动工作区请求未能发送";
            RefreshHud();
        }
    }
}

void HarnessApp::MaybeSendAutoPrompt() {
    if (!m_sess || m_options.autoPrompt.empty() || m_sess->autoPromptSent ||
        m_options.workspaceProbe) {
        return;
    }
    if (!m_sess->writer) {
        m_sess->autoPromptSent = true;
        SendPrompt(m_options.autoPrompt);
        return;
    }
    if (m_sess->protocolVer != 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty() ||
        m_sess->disconnected) {
        return;
    }
    m_sess->autoPromptSent = true;
    (void)PostPrompt(m_options.autoPrompt);
}

void HarnessApp::ChooseWorkspaceFolder() {
    if (!m_sess || !m_sess->writer || !m_sess->workspaceCap ||
        m_sess->protocolVer != 3 || m_sess->disconnected ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        return;
    }
    BROWSEINFOW browse{};
    browse.lpszTitle = L"选择工作区文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST item = SHBrowseForFolderW(&browse);
    if (!item) return;
    wchar_t selected[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(item, selected) != FALSE;
    CoTaskMemFree(item);
    if (!ok) {
        m_sess->workspaceStatus = "无法读取所选文件夹";
        RefreshHud();
        return;
    }
    const std::string path = Utf8FromWidePath(selected);
    if (path.empty() || !RequestWorkspace(path, false)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceStatus = "工作区请求未能发送";
            RefreshHud();
        }
    }
}

void HarnessApp::MaterializePendingPrompts() {
    if (!m_content || !m_sess) return;
    // Transient rows are intentionally rebuilt rather than inserted into canonical row/timeline state.
    for (const PendingPromptView& view : m_pendingPromptViews) {
        if (view.row.panel) m_content->RemoveChild(view.row.panel);
    }
    m_pendingPromptViews.clear();

    for (const PendingPrompt& pending : m_sess->pendingPrompts) {
        auto panel = std::make_unique<ECDI::Panel>();
        RowRec row;
        row.alignRight = true;
        row.label = nullptr;
        std::string display;
        // ★★ 协议 §7（原文）：待确认气泡「与已确认 `user` 气泡**同位置同形状**，只多一个
        //   弱化后缀 `· 发送中`」；失败态「后缀改 `· 未确认`，描边转为错误色」。
        //   ★ 2026-10-20 修正：此前是**换行**追加「未确认 · 等待 core 接受」/「未确认：<原因>」，
        //     那会改变气泡高度与形状（违反「同形状」），文案也不是规范里的那两个后缀；
        //     listlayout 又自创了 `[未确认] ` / `[失败] ` 前缀。现在一律只用同行后缀。
        display = pending.text;
        display += pending.status == "sending" ? " · 发送中" : " · 未确认";
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            // ★ 协议 §7：失败态「描边转为错误色」；发送中仍用 user 气泡的常规描边
            //   （“同形状”也包括边框，只有颜色在失败时换）。
            .borderColor = pending.status == "sending" ? kUserBorder : kErrorDark});
        auto label = std::make_unique<ECDI::Label>(display);
        row.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody));
        panel->AddChild(std::move(label));
        row.panel = panel.get();
        if (m_options.rowModeList) row.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
        m_content->AddChild(std::move(panel));
        PendingPromptView view;
        view.clientId = pending.clientId;
        view.row = row;
        m_pendingPromptViews.push_back(std::move(view));
    }
}

void HarnessApp::RunWorkspaceProbe() {
    if (!m_options.workspaceProbe || m_workspaceProbeDone || !m_sess) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_workspaceProbeDeadline == std::chrono::steady_clock::time_point{}) {
        m_workspaceProbeDeadline = now + std::chrono::seconds(80);
    }
    auto sendEnabled = [&]() {
        return m_promptBar && m_promptBar->sendButton() && m_promptBar->sendButton()->IsEnabled();
    };
    auto canonicalCount = [&](const std::string& clientId) {
        size_t count = 0;
        for (const TimelineEntry& entry : m_sess->timeline) {
            if (entry.kind != TimelineEntry::Kind::Block) continue;
            const Block* block = m_sess->transcript.FindBlock(entry.id);
            if (block && block->kind == "user" && block->clientId == clientId) ++count;
        }
        return count;
    };
    auto append = [&](const std::string& phase, const std::string& clientId,
                      size_t matches, bool matched, bool expectedMismatch) {
        WorkspaceProbeRec rec;
        rec.phase = phase;
        rec.clientId = clientId;
        rec.path = m_sess->workspacePath;
        rec.pendingId = m_sess->pendingWorkspaceId;
        rec.canonicalMatches = matches;
        rec.sendEnabled = sendEnabled();
        rec.matched = matched;
        rec.expectedMismatch = expectedMismatch;
        rec.bubbleText = m_workspaceProbeBubbleText;
        rec.bubbleShapeOk = m_workspaceProbeBubbleShapeOk;
        m_workspaceProbeLog.push_back(std::move(rec));
    };
    auto stopFailed = [&](const std::string& phase, const std::string& clientId, size_t matches) {
        append(phase, clientId, matches, false, false);
        m_workspaceProbeDone = true;
        if (m_application) m_application->Exit();
    };
    auto equivalent = [](const std::string& path, const std::filesystem::path& expected) {
        if (path.empty()) return false;
        std::error_code ec;
        const bool same = std::filesystem::equivalent(std::filesystem::path(WideFromUtf8Path(path)), expected, ec);
        return same && !ec;
    };
    // ★★ 协议 §7 判据：待确认气泡「与已确认 `user` 气泡同位置同形状，只多一个弱化后缀」。
    //   “同形状”的可测形式 = **去掉后缀必须逐字等于正文**，且**不含换行**——旧实现的
    //   `\n未确认 · 等待 core 接受` 正是靠换行改变气泡高度，违的也是这一条。
    //   ★ 读的是控件（`row.label->GetText()`），不是模型：模型里从来不存后缀，
    //     只对模型下断言等于自证（与 Step 3 的 ReadBack 纪律同族）。
    //   ★ 描边颜色读不回来（ECDI::Panel 无 style getter），所以那一半只能靠截图取证；
    //     这里只钉住文案与形状。
    auto bubbleProbe = [&](const std::string& clientId) {
        m_workspaceProbeBubbleText.clear();
        m_workspaceProbeBubbleShapeOk = false;
        const auto view = std::find_if(m_pendingPromptViews.begin(), m_pendingPromptViews.end(),
            [&](const PendingPromptView& v) { return v.clientId == clientId; });
        if (view == m_pendingPromptViews.end() || !view->row.label) return;
        const std::string text = view->row.label->GetText();
        m_workspaceProbeBubbleText = text;
        const auto model = std::find_if(m_sess->pendingPrompts.begin(), m_sess->pendingPrompts.end(),
            [&](const PendingPrompt& p) { return p.clientId == clientId; });
        if (model == m_sess->pendingPrompts.end()) return;
        const std::string suffix = model->status == "sending" ? " \u00b7 \u53d1\u9001\u4e2d"
                                                             : " \u00b7 \u672a\u786e\u8ba4";
        m_workspaceProbeBubbleShapeOk =
            text.find('\n') == std::string::npos &&
            text.size() >= suffix.size() &&
            text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            text == model->text + suffix;
    };

    if (now >= m_workspaceProbeDeadline) {
        stopFailed("timeout", m_workspaceProbeRejectedId, 0);
        return;
    }
    if (!m_sess->writer) {
        stopFailed("unavailable", {}, 0);
        return;
    }
    if (m_sess->protocolIncompatible ||
        (m_sess->disconnected && m_workspaceProbeStep != 11 && m_workspaceProbeStep != 12)) {
        stopFailed("disconnected", {}, 0);
        return;
    }

    switch (m_workspaceProbeStep) {
        case 0: {
            if (!m_sess->workspaceSyncComplete || !m_sess->pendingWorkspaceId.empty()) return;
            std::error_code ec;
            const std::filesystem::path cwd = std::filesystem::current_path(ec);
            if (ec) {
                stopFailed("baseline", {}, 0);
                return;
            }
            m_workspaceProbeOriginalPath = m_sess->workspacePath;
            const bool baselineOk = equivalent(m_sess->workspacePath, cwd);
            append("baseline", {}, 0, baselineOk, false);
            if (!baselineOk) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            std::filesystem::path target = cwd.parent_path();
            if (target.empty() || target == cwd) target = std::filesystem::temp_directory_path(ec);
            if (ec || target.empty() || target == cwd ||
                !std::filesystem::is_directory(target, ec) || ec) {
                stopFailed("pick-ok", {}, 0);
                return;
            }
            m_workspaceProbeTargetPath = PathToUtf8(target);
            const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
            if (ec || temp.empty()) {
                stopFailed("pick-fail", {}, 0);
                return;
            }
            m_workspaceProbeFailPath = PathToUtf8(
                temp / ("dsh-v3-workspace-missing-" + std::to_string(now.time_since_epoch().count())));
            m_workspaceProbeStep = 1;
            return;
        }
        case 1: {
            const bool sent = RequestWorkspace(m_workspaceProbeTargetPath, false);
            m_workspaceProbeWorkspaceId = m_sess->pendingWorkspaceId;
            const bool locked = sent && !m_workspaceProbeWorkspaceId.empty() && !sendEnabled() &&
                                m_sess->workspacePath == m_workspaceProbeOriginalPath;
            append("pending-lock", {}, 0, locked, false);
            if (!locked) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 2;
            return;
        }
        case 2: {
            if (!m_sess->pendingWorkspaceId.empty()) return;
            const bool ok = m_sess->workspaceStatus == "已由 core 确认" &&
                            equivalent(m_sess->workspacePath,
                                       std::filesystem::path(WideFromUtf8Path(m_workspaceProbeTargetPath)));
            append("pick-ok", {}, 0, ok, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 3;
            return;
        }
        case 3: {
            if (!RequestWorkspace(m_workspaceProbeFailPath, false)) {
                stopFailed("pick-fail", {}, 0);
                return;
            }
            m_workspaceProbeWorkspaceId = m_sess->pendingWorkspaceId;
            m_workspaceProbeStep = 4;
            return;
        }
        case 4: {
            if (!m_sess->pendingWorkspaceId.empty()) return;
            const bool pathPreserved = equivalent(m_sess->workspacePath,
                                                  std::filesystem::path(WideFromUtf8Path(m_workspaceProbeTargetPath)));
            const bool failedAck = m_sess->workspaceStatus.find("切换失败：") == 0;
            const bool ok = pathPreserved && failedAck && sendEnabled();
            append("pick-fail", {}, 0, ok, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 5;
            return;
        }
        case 5: {
            m_workspaceProbeAcceptedId = PostPrompt("workspace probe accepted bubble");
            if (m_workspaceProbeAcceptedId.empty()) {
                stopFailed("bubble-flying", {}, 0);
                return;
            }
            const size_t matches = canonicalCount(m_workspaceProbeAcceptedId);
            const bool pending = std::any_of(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_workspaceProbeAcceptedId && p.status == "sending";
                });
            const auto transientView = std::find_if(m_pendingPromptViews.begin(),
                m_pendingPromptViews.end(), [&](const PendingPromptView& view) {
                    return view.clientId == m_workspaceProbeAcceptedId;
                });
            const bool rendered = transientView != m_pendingPromptViews.end() &&
                                  transientView->row.panel && transientView->row.label;
            bubbleProbe(m_workspaceProbeAcceptedId);
            const bool ok = pending && rendered && matches == 0 && !sendEnabled() &&
                            m_workspaceProbeBubbleShapeOk;
            append("bubble-flying", m_workspaceProbeAcceptedId, matches, ok, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 6;
            return;
        }
        case 6: {
            const size_t matches = canonicalCount(m_workspaceProbeAcceptedId);
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_workspaceProbeAcceptedId;
                });
            if (pending != m_sess->pendingPrompts.end() && pending->status == "failed") {
                stopFailed("bubble-confirmed", m_workspaceProbeAcceptedId, matches);
                return;
            }
            if (matches == 0 || pending != m_sess->pendingPrompts.end()) return;
            const bool ok = matches == 1;
            // 已确认后临时气泡应当**已不存在**：这里如实记为 shapeOk=false / 空文案。
            bubbleProbe(m_workspaceProbeAcceptedId);
            append("bubble-confirmed", m_workspaceProbeAcceptedId, matches, ok, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 7;
            return;
        }
        case 7: {
            if (m_sess->coreState != "running") return;
            m_workspaceProbeRejectedId = PostPrompt("workspace probe correlated rejection", true);
            if (m_workspaceProbeRejectedId.empty()) {
                stopFailed("bubble-failed", {}, 0);
                return;
            }
            m_workspaceProbeStep = 8;
            return;
        }
        case 8: {
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_workspaceProbeRejectedId;
                });
            const size_t matches = canonicalCount(m_workspaceProbeRejectedId);
            if (pending == m_sess->pendingPrompts.end() || pending->status == "sending") {
                if (matches == 0) return;
                stopFailed("bubble-failed", m_workspaceProbeRejectedId, matches);
                return;
            }
            const bool ok = pending->status == "failed" && matches == 0;
            bubbleProbe(m_workspaceProbeRejectedId);
            const bool shaped = m_workspaceProbeBubbleShapeOk;
            append("bubble-failed", m_workspaceProbeRejectedId, matches, ok && shaped, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 9;
            return;
        }
        case 9: {
            // Start a real turn so the following forced prompt is queued while core is busy.
            m_workspaceProbeAcceptedId = PostPrompt("workspace probe disconnect trigger");
            if (m_workspaceProbeAcceptedId.empty()) {
                stopFailed("disconnect-trigger", {}, 0);
                return;
            }
            m_workspaceProbeStep = 10;
            return;
        }
        case 10: {
            if (m_sess->coreState != "running" || canonicalCount(m_workspaceProbeAcceptedId) != 1) return;
            m_workspaceProbeRejectedId = PostPrompt("workspace probe disconnect before ack", true);
            if (m_workspaceProbeRejectedId.empty() || !m_sess->child) {
                stopFailed("disconnect-unconfirmed", m_workspaceProbeRejectedId, 0);
                return;
            }
            const size_t matches = canonicalCount(m_workspaceProbeRejectedId);
            const bool pending = std::any_of(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_workspaceProbeRejectedId && p.status == "sending";
                });
            if (!pending || matches != 0) {
                stopFailed("disconnect-unconfirmed", m_workspaceProbeRejectedId, matches);
                return;
            }
            m_workspaceProbeStep = 11;
            m_sess->child->Stop(0);  // deterministic EOF before the busy core can answer the queued prompt.
            return;
        }
        case 11: {
            if (!m_sess->disconnected) return;
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_workspaceProbeRejectedId;
                });
            const auto view = std::find_if(m_pendingPromptViews.begin(),
                m_pendingPromptViews.end(), [&](const PendingPromptView& item) {
                    return item.clientId == m_workspaceProbeRejectedId;
                });
            const size_t matches = canonicalCount(m_workspaceProbeRejectedId);
            const bool failedView = view != m_pendingPromptViews.end() &&
                                    view->row.panel && view->row.label;
            const bool ok = pending != m_sess->pendingPrompts.end() &&
                            pending->status == "failed" && matches == 0 && failedView &&
                            !sendEnabled();
            // ★ 这一相用的是**新的** clientId（step 10 重新赋了值），所以必须重新读
            //   控件；不重读就会把上一相 p2 的文案当成 p4 的读数（实测过）。
            bubbleProbe(m_workspaceProbeRejectedId);
            const bool shaped = m_workspaceProbeBubbleShapeOk;
            append("disconnect-unconfirmed", m_workspaceProbeRejectedId, matches, ok && shaped, false);
            if (!ok) {
                m_workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_workspaceProbeStep = 12;
            return;
        }
        case 12: {
            const std::string before = m_sess->workspacePath;
            const std::filesystem::path expected =
                std::filesystem::path(WideFromUtf8Path(m_workspaceProbeTargetPath));
            m_sess->workspacePath = m_workspaceProbeOriginalPath;  // probe-only local corruption
            RefreshHud();
            const bool expectedMismatch = equivalent(before, expected) &&
                                          !equivalent(m_sess->workspacePath, expected);
            // ★★ §7 判据的负对照：光有正相“匹配”证明不了什么——一个永远返回
            //   `true` 的判据也会匹配。把气泡文案改成旧实现那种**带换行**的形态
            //   （`\n未确认 · …`），判据必须翻红，否则前面的 `bubbleShapeOk`
            //   根本没有区分能力。判完立即恢复原文案。
            bool shapeNegControl = false;
            {
                const auto view = std::find_if(m_pendingPromptViews.begin(), m_pendingPromptViews.end(),
                    [&](const PendingPromptView& v) { return v.clientId == m_workspaceProbeRejectedId; });
                if (view != m_pendingPromptViews.end() && view->row.label) {
                    const std::string saved = view->row.label->GetText();
                    view->row.label->SetText(saved + "\n\u672a\u786e\u8ba4 \u00b7 \u7b49\u5f85 core \u63a5\u53d7");
                    bubbleProbe(m_workspaceProbeRejectedId);
                    shapeNegControl = !m_workspaceProbeBubbleShapeOk;
                    view->row.label->SetText(saved);
                    // ★ 不把 shapeOk 改回 true：日志里应当留下「负对照确实看见了
                    //   换行」这个读数，而不是抹掉证据好看。
                }
            }
            append("negative", {}, 0, false, expectedMismatch && shapeNegControl);
            m_workspaceProbeDone = true;
            if (m_application) m_application->Exit();
            return;
        }
        default:
            stopFailed("invalid-step", {}, 0);
            return;
    }
}

void HarnessApp::MarkAnswered(RowRec& rec, const std::string& choice) {
    if (rec.answered) return;
    rec.answered = true;
    // ★ 0c：已答进模型（重建时回填成"置灰"而不是"可再答一次"）
    // ★★ Step 3：**答的是什么**也进模型。0c 只存了 `answered`（bool），于是任何
    //   重建之后"当时允许还是拒绝"就丢了——两个钮都置灰，读者无从判断。
    //   权限门是**授权决定**，不是一个"已处理"标记：丢掉结论等于把历史抹成空白。
    if (rec.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[rec.timelineIdx].answered = true;
        m_sess->timeline[rec.timelineIdx].answer = choice;
    }
    rec.answer = choice;
    // ★ 视觉走**同一条**路径（见 `ApplyAnsweredLook` 的注释：首答与重建必须同形，
    //   否则"刚答完"和"重建后"长得不同，而 A==B 看不见配色差异）。
    ApplyAnsweredLook(rec, choice);
    m_sess->waitForAnswer = false;
    if (m_sess->writer) {
        // live：真发 answer（协议 §4：需 id + choice；I3 的解除信号）
        if (!rec.askId.empty()) {
            m_sess->writer->Answer(rec.askId, choice);
        }
    }
    m_sess->askHist.emplace_back(choice, rec.label ? rec.label->GetText() : std::string());
    if (m_sess->askHist.size() > 16) m_sess->askHist.pop_front();
    // ★★ Step 5：把结论落到 sidecar（**只有历史打开的会话**才写——启动时 `--replay`
    //   的会话是跑批基线，写它等于让"再跑一次"与"第一次跑"不同）。
    if (m_sess->originKind == "replay-history") SaveUiState(*m_sess);
    m_hudDirty = true;
    AddNoticeRow("[answer] " + choice, kDarkMuted);
    RequestRepaint();
}

// ★★ Step 3：已答权限门的视觉。**首答与重建共用**（这是本函数存在的唯一理由）。
//
// 判据设计（为什么是"选中者留色、另一个灰化"而不是加一行回显文字）：
//   · ECDI 的 Button **不画 disabled 态**（契约：灰化是调用方责任，先例
//     `PromptBar::ApplySendStyle`）⇒ 若不自己处置，两个钮置灰后**像素完全不变**，
//     屏幕上根本看不出"答过了"，更看不出"答的是什么"。
//   · 加回显 Label 要动 `Relayout` 的 ask 分支（多一个子控件、多一处定位），
//     而本步的契约是"只补模型、不改布局"——那会把一次数据修复变成一次布局改动。
//   · 因此复用**已有**的两个钮：结论落在"哪个钮还是常态色"上。
//     信息量相同（allow/deny/未答 三态可分），零新增控件，零布局改动。
//
// 可读回：ECDI 无 style getter，`GetTextColor()` 是唯一能问出"这个钮是什么色"的接口
//   ⇒ 探针据此独立于模型判断"屏幕上是哪个答案"（同侧栏选中态的判据手法）。
void HarnessApp::ApplyAnsweredLook(RowRec& rec, const std::string& choice) {
    // 两个钮都**不可再点**（`SetEnabled(false)` 同时退出命中——`Widget::HitTest`
    // 对禁用控件早退，故这里不需要额外的点击守卫）。
    if (rec.allowBtn) rec.allowBtn->SetEnabled(false);
    if (rec.denyBtn) rec.denyBtn->SetEnabled(false);

    const bool allowPicked = (choice == "allow");
    const bool denyPicked = (choice == "deny");
    // 选中的那个留常态色；另一个进"已弃"外观。未知 choice（协议外的值）⇒ 两个都灰，
    // 如实呈现"答了但答案不认识"，不编造一个看起来正常的状态。
    auto paint = [](ECDI::Button* b, bool keepNormal, bool isAllow) {
        if (!b) return;
        const ECDI::Color fg = keepNormal ? (isAllow ? kWarnInk : kDenyInk) : harness::kBtnDisabledInk;
        const ECDI::Color bg = keepNormal ? (isAllow ? kWarn : kBtnDenyBg) : harness::kBtnDisabledBg;
        const ECDI::Color bd = keepNormal ? kAskBorder : harness::kBtnDisabledLine;
        b->SetTextColor(fg);
        // ⚠ 事实核对（2026-10-19，读 Button.cpp:243+ 后确认）：`Button::OnPaint` 的填充
        //   **只**来自 `m_displayedBackground`，它**从不**用 ButtonStyle.border 画自身边框
        //   ——border/borderWidth 唯一的消费者是焦点框（`HasFocus() && ShowFocusRect()` 时
        //   传给 DrawFocusRect）。故这里设的 bd 既不画边、也不改底色，只是给"刚被点过的
        //   那个钮"的焦点环定色（答完之后这个钮仍持焦点，直到焦点被别处拿走）。
        //   留着它是因为删掉会让焦点环落到主题缺省色、与权限门的配色脱节；但**别指望
        //   靠它加一圈边框**——要边框得外层包一个 Panel（ScrollView 那条 D13 同理）。
        b->SetStyle(ECDI::ButtonStyleOverride{
            .background = bg,
            .border = bd,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = bg,
            .hoverBackground = bg});   // 禁用后不会再 hover，但钉死可防"状态回退时闪色"
    };
    paint(rec.allowBtn, allowPicked, true);
    paint(rec.denyBtn, denyPicked, false);
    // 记下视图侧结论（`SnapshotRows` 只读 RowRec，不回查模型——见字段注释）
    rec.answer = choice;
}

// 通知行：**纯构造**（重建可重放）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildNoticeRow(const std::string& text,
                                                        const ECDI::Color& color, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    auto label = std::make_unique<ECDI::Label>(text);
    rec.label = label.get();
    label->SetTextColor(color);
    label->SetFont(harness::SansFont(harness::kFontSmall));   // 统一中文 face（原先落 SimSun）
    panel->AddChild(std::move(label));
    return panel;
}

void HarnessApp::AddNoticeRow(const std::string& text, const ECDI::Color& color) {
    // ★★ 0c：通知进模型。此前它**只**是控件 —— 而通知恰好承载全部协议健康信息
    //   （I4/I5 缺口、截断报错、协议 §9 拒连、hello/bye 摘要、发送/回答回执）。
    //   ⇒ 任何重建都会把"这个会话被截断过""协议版本不兼容被拒"这类信息**静默抹掉**，
    //     屏幕看起来只是"少了几行灰字"，而丢掉的是**这次运行是否可信**的判据。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Notice;
    entry.text = text;
    entry.color = color;
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildNoticeRow(text, color, rec);
    AddRow(std::move(panel), rec, std::string(), m_sess->timeline.size() - 1);
}

// ── 布局与重绘（规格 §5.3 / §6.2）──────────────────────────────

void HarnessApp::Relayout() {
    const int contentW = ContentWidth(m_scroll);

    if (m_options.rowModeList) {
        // listlayout：行高固定、不测量；Arrange 由 ListLayout 完成
        for (RowRec& r : m_sess->rows) {
            if (r.panel->GetHeight() != kListRowHeight) {
                r.panel->SetSize(contentW, kListRowHeight);
            }
        }
        for (PendingPromptView& view : m_pendingPromptViews) {
            if (view.row.panel && view.row.panel->GetHeight() != kListRowHeight) {
                view.row.panel->SetSize(contentW, kListRowHeight);
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
    for (RowRec& r : m_sess->rows) {
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
    for (PendingPromptView& view : m_pendingPromptViews) {
        RowRec& r = view.row;
        if (!r.panel) continue;
        const int maxBubbleW = std::max(120, innerW * 78 / 100);
        const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
        if (r.label) {
            if (!r.wrapped) r.label->AutoSize();
            const bool hasNewline = r.label->GetText().find('\n') != std::string::npos;
            const bool needWrap = r.wrapped || hasNewline || r.label->GetWidth() > maxTextW;
            if (needWrap != r.wrapped) {
                r.wrapped = needWrap;
                r.label->SetWordWrap(needWrap);
            }
            if (r.wrapped) {
                if (r.lastWrapW != maxTextW) {
                    r.label->SetSize(maxTextW, r.label->GetHeight());
                    r.lastWrapW = maxTextW;
                }
                r.label->AutoSize();
            }
            r.label->SetPosition(kRowPadX, 8);
        }
        const int rowH = r.label ? r.label->GetHeight() + 16 : 0;
        const int bubbleW = r.label
            ? (r.wrapped ? maxBubbleW : std::min(maxBubbleW, r.label->GetWidth() + 2 * kRowPadX + 2))
            : maxBubbleW;
        r.panel->SetSize(bubbleW, rowH);
        r.panel->SetPosition(kCanvasPadX + innerW - bubbleW, y);
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
        m_hud.sessChip->SetText(m_sess->sessionId.empty() ? "s-____" : m_sess->sessionId);
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
    // ★ 0d：标题改成反映**活动会话**（多会话后，标题不该恒是首会话的名字）。
    if (m_hud.convTitle) {
        m_hud.convTitle->SetText(m_sess->label.empty() ? ConversationTitle() : m_sess->label);
    }
    if (m_hud.convSub) {
        std::string sub = "core " + (m_sess->coreVer.empty() ? std::string("—") : m_sess->coreVer);
        if (!m_sess->sessionId.empty()) sub += " · " + m_sess->sessionId;
        sub += " · protocol " + std::to_string(m_sess->protocolVer);
        // ★ 0d：会话计数——让用户看见"现在有几个会话、我在第几个"。
        sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
               std::to_string(m_sessions.size());
        m_hud.convSub->SetText(sub);
    }
    const std::string stateText =
        m_sess->coreState.empty() ? std::string("idle") : m_sess->coreState;
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
    const bool live = m_sess->writer != nullptr;
    // ★★ 2026-10-20：`liveReady` **不再**包含 `workspaceCap`——协议 §3.1 只要求
    //   「未声明 workspace 的 core，GUI 不得显示工作区选择器」，而不是「不能对话」。
    //   旧写法把 workspace 能力当成 live 就绪的前提，导致一份合法的不支持工作区的
    //   core 连一句 prompt 都发不出去（发送钮永久置灰）。
    const bool liveReady = live && m_sess->protocolVer == 3 &&
                           !m_sess->protocolIncompatible && !m_sess->disconnected &&
                           !m_sess->liveEof;
    // 只有「core 声明了 workspace 且初始同步未完成」才是阻塞条件；无此能力时直接放行。
    const bool workspaceReady = !m_sess->workspaceCap || m_sess->workspaceSyncComplete;
    const bool sendEnabled = live
        ? (liveReady && workspaceReady && m_sess->coreState == "idle" &&
           m_sess->pendingWorkspaceId.empty() && !HasUnconfirmedPrompt())
        : !active;
    const bool stopEnabled = live ? (liveReady && active) : active;
    if (m_promptBar) m_promptBar->SetButtonStates(sendEnabled, stopEnabled);
    const bool pickerEnabled = m_sess->workspaceCap && liveReady &&
                               m_sess->coreState == "idle" &&
                               m_sess->pendingWorkspaceId.empty();
    // ★★ 协议 §3.1：对一份**真实的 core**，它未声明 workspace ⇒ GUI「不得显示工作区
    //   选择器」。⇒ 隐藏路径与选择钮，只留一行诚实的状态说明（“core 未声明 workspace 能力”）。
    //   ★ 只在 live 下藏：回放/空会话根本没有 core，沿用既有外观（零回归风险）。
    //   ★ 用控件自己的 IsVisible() 当“上次已应用值”，不新增模型字段——重建后新建的
    //     控件默认可见，这个判据会自己纠回来；换成成员记录反而会在重建后陈旧。
    const bool hideWorkspace = live && !m_sess->workspaceCap;
    bool workspaceVisChanged = false;
    auto applyWorkspaceVis = [&](ECDI::Widget* w) {
        if (!w) return;
        if (w->IsVisible() == hideWorkspace) {
            w->SetVisible(!hideWorkspace);
            workspaceVisChanged = true;
        }
    };
    applyWorkspaceVis(m_hud.workspacePath);
    applyWorkspaceVis(m_hud.workspacePick);
    applyWorkspaceVis(m_hud.projectList);
    if (workspaceVisChanged) {
        // VerticalLayout 只在 Arrange 时给隐藏子不占槽（C-VIS-1）⇒ 不重排就会留个空洞。
        if (ECDI::Widget* p = m_hud.workspaceStatus ? m_hud.workspaceStatus->GetParent()
                                                    : nullptr) {
            p->Arrange();
        }
    }
    if (m_hud.workspacePath) {
        m_hud.workspacePath->SetText(m_sess->workspacePath.empty()
            ? "core 实际工作区：尚未确认" : "core 实际工作区：" + m_sess->workspacePath);
    }
    if (m_hud.workspaceStatus) m_hud.workspaceStatus->SetText(m_sess->workspaceStatus);
    if (m_hud.workspacePick) {
        m_hud.workspacePick->SetEnabled(pickerEnabled);
        m_hud.workspacePick->SetTextColor(pickerEnabled ? kDarkInk : kDarkMuted);
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
                rec.coreState = m_sess->coreState.empty() ? "idle" : m_sess->coreState;
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

    // ★★ Step 1：侧栏会话的"活动会话"读数**搬到了列表行里**（每行自带 id/label/选中态）。
    //   `sessSession` / `sessCore` 两个槽位因此**不再赋值**——它们原本服务于
    //   "全局只有一行会话条目"的形态。字段仍留在 `Hud` 里（删字段是独立清理，
    //   见 App.h 里的注释），故这里的两处 `if` 永远不成立（死代码但无害）。
    if (m_hud.sessSession) {
        m_hud.sessSession->SetText(m_sess->sessionId.empty() ? "—" : m_sess->sessionId);
    }
    if (m_hud.sessCore) {
        m_hud.sessCore->SetText(m_sess->coreVer.empty() ? "—" : m_sess->coreVer);
    }
    if (m_hud.sessCount) {
        // 计数与列表同源：都从 m_sessions.size() 出发 ⇒ 这一行是列表的**文字对照**。
        // 两者不一致（列表少画/多画了一行）时，人和探针都读得出来。
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%zu 个会话", m_sessions.size());
        m_hud.sessCount->SetText(buf);
    }
    if (m_hud.sessProgress) {
        char buf[64];
        if (m_sess->replay) {
            std::snprintf(buf, sizeof(buf), "%zu / %zu 条", m_sess->replay->Consumed(),
                          m_sess->replay->Total());
        } else if (m_sess->source) {
            std::snprintf(buf, sizeof(buf), "%zu 条（live）", m_sess->liveLines);
        } else {
            // ★ 无 source 且无 replay = 回放模式下新建的空会话（0d 起就有这种会话）。
            //   上一版把它归进 else 支，于是侧栏写着「0 条（live）」——**谎报来源**。
            //   这个错在 0d 就存在，但那时侧栏只画活动会话、用户几乎见不到空会话；
            //   Step 1 把 N 个会话都摆到屏上之后，它变成每点一次「新建」就出现一次。
            std::snprintf(buf, sizeof(buf), "%s", "空会话 · 无 core");
        }
        m_hud.sessProgress->SetText(buf);
    }
    if (m_replayBar) {
        if (m_sess->replay) {
            const size_t total = m_sess->replay->Total();
            m_replayBar->SetProgress(total > 0
                                         ? static_cast<float>(m_sess->replay->Consumed()) /
                                               static_cast<float>(total)
                                         : 0.0f);
        } else {
            m_replayBar->SetProgress(0.0f);   // live：总量未知，进度条不表达
        }
    }

    if (m_hud.healthSeq) {
        const size_t gaps = m_sess->transcript.SeqGaps();
        if (gaps == 0) {
            m_hud.healthSeq->SetText("seq 连续 · 0 缺口");
            m_hud.healthSeq->SetTextColor(kDarkInk);
        } else {
            m_hud.healthSeq->SetText("seq 缺口 " + std::to_string(gaps));
            m_hud.healthSeq->SetTextColor(kErrorDark);
        }
    }
    if (m_hud.healthViol) {
        m_hud.healthViol->SetText("违规 " + std::to_string(m_sess->transcript.Violations()) +
                                  " · 未知 " + std::to_string(m_sess->transcript.UnknownTypes()));
    }
    // ★ 未知 kind 单列一行（2026-10-13）：它与「未知 t」管辖**不同的判别符**，
    //   合并会同时导致①语义误读（把「协议空白」读成「I5 已覆盖」）
    //   ②文本裁切（并进上行实测被裁，见 BuildInspector 注释）。
    if (m_hud.healthKind) {
        const size_t uk = m_sess->transcript.UnknownKinds();
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
        m_hud.usageInV->SetText(m_sess->usageIn < 0 ? "—"
                                              : std::to_string(m_sess->usageIn));
    }
    if (m_hud.usageOutV) {
        m_hud.usageOutV->SetText(m_sess->usageOut < 0 ? "—"
                                                : std::to_string(m_sess->usageOut));
    }
    // ★★ 上下文占用百分比（协议 v2 §3.1，2026-10-14）——**用户要求**。
    //   ★ 分母 = `hello.context`（**唯一来源**）；**不再**用 `max(input, output)`。
    //     旧写法是**拿输入跟输出比**（两个不同量纲的东西相除）⇒ 读数无意义，
    //     现已被"真正的分母"取代。这是本行**修掉的一个既有缺陷**，不只是新增。
    //   ★ 分子 = `usage.input`（= 本轮送进模型的上下文规模）。**不含 output**：
    //     output 是本轮新生成的，尚未回到下一轮的上下文里。
    //   ★ 三者任一未知 ⇒ 显示 `—` 且条归零（**不猜**；分母未知时百分比无定义）。
    const bool ctxKnown = (m_sess->contextLimit > 0 && m_sess->usageIn >= 0);
    if (m_hud.ctxPct) {
        if (!ctxKnown) {
            m_hud.ctxPct->SetText("—");
            m_hud.ctxPct->SetTextColor(kDarkMuted);
        } else {
            // 四舍五入到整数百分比；占用可能 >100%（core 低估容量）⇒ 如实显示，不钳
            const long long pct =
                (m_sess->usageIn * 100 + m_sess->contextLimit / 2) / m_sess->contextLimit;
            m_hud.ctxPct->SetText(std::to_string(pct) + "%");
            // 配色：<70% 常规 · 70–90% 琥珀（该收尾了）· >90% 红（快溢出）
            m_hud.ctxPct->SetTextColor(pct >= 90 ? kErrorDark
                                                 : (pct >= 70 ? kWarn : kOkDark));
        }
    }
    if (m_ctxRing) {
        const float frac = ctxKnown
                               ? static_cast<float>(m_sess->usageIn) /
                                     static_cast<float>(m_sess->contextLimit)
                               : 0.0f;
        // 环本身钳到 [0,1]（SetProgress 契约）；文本仍如实显示 >100%
        m_ctxRing->SetProgress(frac > 1.0f ? 1.0f : frac);
        // ★ 环色与右侧数字**同源**（同一组阈值），避免「环是绿的、字是红的」自相矛盾。
        //   阈值沿用既有口径：<70 常规绿 · 70–90 琥珀 · >90 红；分母未知 ⇒ 回退绿（环归零）。
        const long long pctForColor =
            ctxKnown ? (m_sess->usageIn * 100 + m_sess->contextLimit / 2) / m_sess->contextLimit : 0;
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
        const long long denom = (m_sess->contextLimit > 0)
                                    ? m_sess->contextLimit
                                    : std::max(m_sess->usageIn, m_sess->usageOut);
        const float inFrac = (m_sess->usageIn >= 0 && denom > 0)
                                 ? static_cast<float>(m_sess->usageIn) / static_cast<float>(denom)
                                 : 0.0f;
        const float outFrac = (m_sess->usageOut >= 0 && denom > 0)
                                  ? static_cast<float>(m_sess->usageOut) / static_cast<float>(denom)
                                  : 0.0f;
        m_usageBarIn->SetProgress(inFrac > 1.0f ? 1.0f : inFrac);
        m_usageBarOut->SetProgress(outFrac > 1.0f ? 1.0f : outFrac);
    }
    // 权限历史：无记录时给一行占位（否则是一张 70px 高的空盒子——`v8-light-insp.png` 可见）。
    // ★ 为何不 `SetVisible(false)` 把卡片藏掉：`Widget::SetVisible` **故意不内建 Invalidate**
    //   （`Widget.h:160-170` 明写「静默不刷新」），harness 还得自己补重绘；而「协议允许问、
    //   但本次一次都没问」本身是**有信息量的评估读数**（ask 门是 I3 的可见载体），
    //   藏掉不如显式说「暂无」。复用现有 3 行控件 ⇒ 零新增控件、零布局改动。
    const bool noHist = m_sess->askHist.empty();
    for (size_t i = 0; i < 3; ++i) {
        if (m_hud.histChip[i] && m_hud.histWhat[i]) {
            const size_t n = m_sess->askHist.size();
            if (i < n) {
                const auto& [choice, what] = m_sess->askHist[n - 1 - i];
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
                               std::to_string(m_sess->protocolVer));
    }
    if (m_hud.stMsgs) {
        if (m_sess->replay) {
            m_hud.stMsgs->SetText(std::to_string(m_sess->replay->Consumed()) + " / " +
                                  std::to_string(m_sess->replay->Total()) + " msg");
        } else {
            m_hud.stMsgs->SetText(std::to_string(m_sess->liveLines) + " msg（live）");
        }
    }
    if (m_hud.stTokens) {
        if (m_sess->usageIn < 0) {
            m_hud.stTokens->SetText("tokens —");
        } else {
            m_hud.stTokens->SetText("tokens " + std::to_string(m_sess->usageIn) + " / " +
                                    std::to_string(m_sess->usageOut));
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
    // ★★ 0d：多会话时摘要只记**退出时刻的活动会话**——这件事必须显式写出来。
    //   实测教训（2026-10-19）：窗口还开着的时候点了「＋ 新建会话」，活动会话切到
    //   空会话，落盘的就是 blocks=0/deltas=0/messages=0 —— 看起来与"跑失败了"
    //   **完全一样**（与 protocolIncompatible 那条注释同族：空摘要不得伪装成功摘要）。
    //   ★ 只在 sessionCount > 1 时写这三个字段：单会话运行的摘要**逐字节不变**，
    //     所有既有基线（p32 起）仍可比——这是本仓的铁律，不能为解释一个交互场景破例。
    if (m_sessions.size() > 1) {
        std::snprintf(buf, sizeof(buf), "  \"sessionCount\": %zu,\n", m_sessions.size());
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"activeSessionIndex\": %zu,\n", m_activeIndex);
        json += buf;
        json += "  \"activeSessionLabel\": \"" + m_sess->label + "\",\n";
    }
    std::snprintf(buf, sizeof(buf), "  \"blocks\": %zu,\n", m_sess->transcript.Blocks());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"deltas\": %zu,\n", m_sess->transcript.Deltas());
    json += buf;
    // ★ 注意：这是**夹具总行数**（文件里有多少行），不是"本次消费了多少行"。
    //   被 §9 拒绝时它仍是全量（文件没变），故判读「是否真的拒绝」要看
    //   `blocks`（消费后才产生）与 `protocolIncompatible`，不能看本字段。
    std::snprintf(buf, sizeof(buf), "  \"messages\": %zu,\n",
                  m_sess->replay ? m_sess->replay->Total() : m_sess->liveLines);
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
    if (m_options.orderProbe && !m_sess->transcript.Order().empty()) {
        // ★★ Step 0a 机读证据（2026-10-19）：块**到达顺序**原文。
        //   为什么必须是数组而不是一个「顺序对不对」的布尔：探针若自己也算一遍
        //   `sorted(...)` 再比，比对逻辑就是**被测对象自己**——错的地方会一起错。
        //   故这里只如实吐出 `Order()` 的内容，判据留给跑批脚本对着夹具独立算。
        //   读法：本数组必须与夹具里 `block_start` 的出现次序**逐元素相同**。
        json += "  \"blockOrder\": [";
        const std::vector<std::string>& order = m_sess->transcript.Order();
        for (size_t i = 0; i < order.size(); ++i) {
            json += (i ? ", \"" : "\"");
            json += order[i];
            json += "\"";
        }
        json += "],\n";
    }
    if (m_options.rebuildProbe && !m_rebuildProbeLog.empty()) {
        // ★★ Step 0c 机读证据（2026-10-19）：照模型重建视图的逐字段比对。
        //   判据（跑批脚本执行，不由本函数自评）：
        //     · forward / forward-again 的 mismatches 必须 == 0（视图重放无损）
        //     · lexicographic 的 orderMismatches 必须 == 字典序错位量
        //       （cot 42 / realistic 218 / scale 1998 / demo 0）——负对照，
        //       为 0 说明探针看不出顺序错，于是上面那条"全等"没有意义
        //     · rebuildNoticeCount > 0 证明"通知进模型"是承重的（否则重建少这么多行）
        //     · expanded/answeredSamples > 0 证明"状态被保住"这条断言不是空断言
        json += "  \"rebuildProbe\": [";
        // ★ `firstDivergence` 现在会带上**行文本**（标题/正文），而转录里的文字是
        //   JSON 里来的、可能含引号 ⇒ 必须转义，否则整个摘要不是一个合法 JSON，
        //   跑批脚本读它会**整份**失败（比少一个字段严重得多）。
        auto esc = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char e[8];
                            std::snprintf(e, sizeof(e), "\\u%04x", c);
                            out += e;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_rebuildProbeLog.size(); ++i) {
            const RebuildProbeRec& r = m_rebuildProbeLog[i];
            json += (i ? ", " : "");
            char b[768];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"rowCount\": %zu, \"beforeCount\": %zu, "
                          "\"timelineCount\": %zu, \"mismatches\": %zu, "
                          "\"orderMismatches\": %zu, \"firstDivergence\": \"%s\", "
                          "\"scrollOffset\": %d, \"maxScrollOffset\": %d}",
                          r.phase.c_str(), r.rowCount, r.beforeCount, r.timelineCount,
                          r.mismatches, r.orderMismatches, esc(r.firstDivergence).c_str(),
                          r.scrollOffset, r.maxScrollOffset);
            json += b;
        }
        json += "],\n";
        std::snprintf(buf, sizeof(buf), "  \"rebuildNoticeCount\": %zu,\n", m_rebuildNoticeCount);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"rebuildTimelineCount\": %zu,\n",
                      m_sess->timeline.size());
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"rebuildExpandedSamples\": %d,\n",
                      m_rebuildExpandedSamples);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"rebuildAnsweredSamples\": %d,\n",
                      m_rebuildAnsweredSamples);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"rebuildScrollBefore\": %d,\n",
                      m_rebuildScrollBefore);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"rebuildRowCount\": %zu,\n", m_sess->rows.size());
        json += buf;
        // ★ 点击一次后标题是否还完整（0c 修的工具卡标题被清空那个 bug 的专属读数）
        std::snprintf(buf, sizeof(buf), "  \"rebuildTitleEmptied\": %d,\n",
                      m_rebuildTitleEmptied);
        json += buf;
        json += "  \"rebuildTitleSample\": \"" + esc(m_rebuildTitleSample) + "\",\n";
        json += "  \"rebuildTitleAfter\": \"" + esc(m_rebuildTitleAfter) + "\",\n";
    }
    if (m_options.sessionProbe && !m_sessionProbeLog.empty()) {
        // ★★ Step 0d 机读证据（2026-10-19）：新建/切换会话的行为轨迹。
        //   判据（跑批脚本执行）：
        //     · new.sessionCount == baseline.sessionCount + 1（真的多了一个会话）
        //     · new.blockRowCount == 0（**负对照**：新会话不含旧转录的块行——
        //       若它非零，说明新会话抄了旧会话的模型，"新建"是假的）
        //     · back.activeIndex == 0 ∧ back.rowCount == baseline.rowCount（切回无损）
        json += "  \"sessionProbe\": [";
        for (size_t i = 0; i < m_sessionProbeLog.size(); ++i) {
            const SessionProbeRec& r = m_sessionProbeLog[i];
            json += (i ? ", " : "");
            char b[256];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"sessionCount\": %zu, \"activeIndex\": %zu, "
                          "\"rowCount\": %zu, \"blockRowCount\": %zu}",
                          r.phase.c_str(), r.sessionCount, r.activeIndex,
                          r.rowCount, r.blockRowCount);
            json += b;
        }
        json += "],\n";
    }
    if (m_options.sidebarProbe && !m_sidebarProbeLog.empty()) {
        // ★★ Step 1 机读证据（2026-10-19）：侧栏列表的 A==B 轨迹。
        //   判据（跑批脚本执行）：
        //     · rebuild.matched == true ∧ rebuild-again.matched == true
        //       （原地重建两次都无损 ⇒ 列表是模型的纯函数）
        //     · negative.matched == **false**（负对照：逆序必须被抓出来。
        //       若它为 true，说明比对函数没在看次序，上面两条 matched 不作数）
        //     · new.rowCount == baseline.rowCount + 1 ∧ new.sessionCount == +1
        //     · deleted.rowCount == baseline.rowCount ∧ deleted.transcriptRows == baseline.transcriptRows
        //       （删掉会话后列表回到原状，且**活动会话的转录一位没变**）
        json += "  \"sidebarProbe\": [";
        // ★ 自带一份转义（与 rebuildProbe 段那个 `esc` 同款，但**不能复用**：
        //   它是那段 `if` 块内的局部 lambda，作用域到 `],\n` 就结束了）。
        //   会话 label 含夹具文件名，可能是任意 UTF-8；`firstDivergence` 里还会拼
        //   行文本 ⇒ 不转义就会让整份摘要不再是合法 JSON（跑批脚本读它会整份失败）。
        auto esc = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char e[8];
                            std::snprintf(e, sizeof(e), "\\u%04x", c);
                            out += e;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_sidebarProbeLog.size(); ++i) {
            const SidebarProbeRec& r = m_sidebarProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"rowCount\": %zu, \"sessionCount\": %zu, "
                          "\"activeIndex\": %zu, \"selectedRows\": %zu, "
                          "\"selectedIndex\": %zu, \"transcriptRows\": %zu, "
                          "\"sidebarVisible\": %s, \"midWidth\": %d, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.rowCount, r.sessionCount, r.activeIndex,
                          r.selectedRows, r.selectedIndex, r.transcriptRows,
                          r.sidebarVisible ? "true" : "false", r.midWidth,
                          r.matched ? "true" : "false",
                          esc(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ Step 3 机读证据（2026-10-19）：ask 的**答案内容**是否进模型、是否上屏。
    //   判据（跑批脚本执行）：
    //     · baseline.matched == true（未答：answer 空、两钮都可点 —— 负对照⑥）
    //     · answered.answerModel 与 answered.answerOnScreen 都为 "allow"
    //       （模型与屏幕**各自独立**读出同一个结论，不是同义反复）
    //     · answered.buttonsDisabled == true
    //     · reclick.matched == true（再点同一个钮零变化）
    //     · forward.matched == true 且 forward.answerOnScreen == "allow"
    //       （重建后结论仍在屏上 —— 这就是本步要修的缺陷的直检）
    //     · negative.matched == **false**（负对照：改答案后重建必须报出差异）
    //     · restored.matched == true（改回去必须重新相等 —— 单变量纪律）
    if (m_options.answerProbe && !m_answerProbeLog.empty()) {
        json += "  \"answerProbe\": [";
        // ★ 自带一份转义（作用域同 sidebarProbe 段：不能复用别段的局部 lambda）
        auto escA = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char e[8];
                            std::snprintf(e, sizeof(e), "\\u%04x", c);
                            out += e;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_answerProbeLog.size(); ++i) {
            const AnswerProbeRec& r = m_answerProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"askRows\": %zu, \"answeredRows\": %zu, "
                          "\"modelAnswers\": %zu, \"answerModel\": \"%s\", "
                          "\"answerOnScreen\": \"%s\", \"buttonsDisabled\": %s, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.askRows, r.answeredRows, r.modelAnswers,
                          escA(r.answerModel).c_str(), escA(r.answerOnScreen).c_str(),
                          r.buttonsDisabled ? "true" : "false",
                          r.matched ? "true" : "false",
                          escA(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ Step 4 机读证据（2026-10-19）：整树重建 + 运行期主题切换。
    //   判据（跑批脚本执行）：
    //     · baseline.matched == true（前置成立：侧栏/转录都非空，否则"全等"是空真）
    //     · rebuild.matched == true 且 rebuild.paints == baseline.paints
    //       （**恰好相等**：重建期间不该有任何重绘；变小=计数器被清零、变大=偷重绘）
    //     · rebuild-again.matched == true（幂等）
    //     · toggle.matched == true 且 toggle.light != baseline.light
    //       （主题真的翻了，且四路内容一处不差 —— 换的是颜色不是内容）
    //     · toggle-back.matched == true
    //     · negative.matched == **false** 且 firstDivergence 含 "sidebarVisible"
    //       （负对照：模型侧状态确实进了比对面）
    //     · restored.matched == true（单变量纪律）
    //     · uiRebuilds 单调 1→2→3→4→5
    if (m_options.themeProbe && !m_themeProbeLog.empty()) {
        // ★ 顶层两个标量：这一次进程最终停在什么主题、整树重建过几次。
        //   只在探针开启时写 ⇒ 默认摘要**逐字节不变**（本仓铁律）。
        json += std::string("  \"theme\": \"") + (harness::g_themeLight ? "light" : "dark")
                + "\",\n";
        json += "  \"uiRebuilds\": " + std::to_string(m_uiRebuilds) + ",\n";
        json += "  \"themeProbe\": [";
        // ★ 第三份转义 lambda（前两份都在各自 if 块作用域内，不能复用）
        auto escT = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char e[8];
                            std::snprintf(e, sizeof(e), "\\u%04x", c);
                            out += e;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_themeProbeLog.size(); ++i) {
            const ThemeProbeRec& r = m_themeProbeLog[i];
            json += (i ? ", " : "");
            char b[768];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"light\": %s, \"uiRebuilds\": %zu, "
                          "\"paints\": %zu, \"frames\": %zu, \"frameUsTotal\": %zu, "
                          "\"sidebarRows\": %d, \"transcriptRows\": %d, "
                          "\"inputText\": \"%s\", \"sidebarVisible\": %s, "
                          "\"activeIndex\": %zu, \"sessionCount\": %zu, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.light ? "true" : "false", r.uiRebuilds,
                          r.paints, r.frames, r.frameUsTotal, r.sidebarRows, r.transcriptRows,
                          escT(r.inputText).c_str(), r.sidebarVisible ? "true" : "false",
                          r.activeIndex, r.sessionCount,
                          r.matched ? "true" : "false", escT(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }

    // ★★ Step 5：历史会话探针。同样**只在开启时写**（默认摘要逐字节不变）。
    if (m_options.historyProbe && !m_historyProbeLog.empty()) {
        json += "  \"historyProbe\": [";
        // 第四份转义 lambda（前三份都在各自 if 块作用域内）
        auto escH = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char e[8];
                            std::snprintf(e, sizeof(e), "\\u%04x", c);
                            out += e;
                        } else {
                            out += static_cast<char>(c);
                        }
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_historyProbeLog.size(); ++i) {
            const HistoryProbeRec& r = m_historyProbeLog[i];
            json += (i ? ", " : "");
            char b[1024];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"filesOnDisk\": %zu, \"rowsOnScreen\": %zu, "
                          "\"openedLabel\": \"%s\", \"originKind\": \"%s\", "
                          "\"blocks\": %zu, \"deltas\": %zu, \"messages\": %zu, "
                          "\"atEnd\": %s, \"askRows\": %zu, \"answeredRows\": %zu, "
                          "\"modelAnswers\": %zu, \"sidecarExisted\": %s, "
                          "\"sidecarWritten\": %s, \"sidecarRemoved\": %s, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.filesOnDisk, r.rowsOnScreen,
                          escH(r.openedLabel).c_str(), escH(r.originKind).c_str(),
                          r.blocks, r.deltas, r.messages,
                          r.atEnd ? "true" : "false", r.askRows, r.answeredRows,
                          r.modelAnswers,
                          r.sidecarExisted ? "true" : "false",
                          r.sidecarWritten ? "true" : "false",
                          r.sidecarRemoved ? "true" : "false",
                          r.matched ? "true" : "false", escH(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }

    std::snprintf(buf, sizeof(buf), "  \"seqGaps\": %zu,\n", m_sess->transcript.SeqGaps());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"violations\": %zu,\n", m_sess->transcript.Violations());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"unknownTypes\": %zu,\n", m_sess->transcript.UnknownTypes());
    json += buf;
    // ★ 未知 block_start.kind（2026-10-13）：与 unknownTypes **并列而非合并**——
    //   前者是信封层 `t`（I5 明文覆盖），后者是 `kind`（协议 v2 起由 I8 覆盖）。
    //   ★ v2（2026-10-14）：`reasoning` 是**已知** kind ⇒ 不再计入本项
    //     （cot 夹具的读数由 12 变 0，正是这条生效的证据）。
    std::snprintf(buf, sizeof(buf), "  \"unknownKinds\": %zu,\n", m_sess->transcript.UnknownKinds());
    json += buf;
    // ★ 协议 §9（v2）：如实记下 core 声明的版本与本 GUI 的上限，
    //   以及**是否因版本不兼容而拒绝连接**——否则一次被拒的运行会留下
    //   一份"看起来跑完了"的空摘要。
    std::snprintf(buf, sizeof(buf), "  \"protocolDeclared\": %d,\n", m_sess->protocolVer);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolMaxSupported\": %d,\n",
                  kProtocolMaxSupported);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolIncompatible\": %s,\n",
                  m_sess->protocolIncompatible ? "true" : "false");
    json += buf;
    // ★ 协议 v2 §3.1：上下文容量的读数（0 = core 未声明 ⇒ 百分比显示 `—`）
    std::snprintf(buf, sizeof(buf), "  \"contextLimit\": %lld,\n",
                  static_cast<long long>(m_sess->contextLimit));
    json += buf;
    if (m_options.workspaceProbe) {
        json::Value records;
        records.type = json::Value::Type::Array;
        auto putString = [](json::Value& object, const char* key, const std::string& value) {
            json::Value field;
            field.type = json::Value::Type::String;
            field.text = value;
            object.object[key] = std::move(field);
        };
        size_t expectedMismatches = 0;
        bool checksPassed = true;
        for (const WorkspaceProbeRec& rec : m_workspaceProbeLog) {
            json::Value item;
            item.type = json::Value::Type::Object;
            putString(item, "phase", rec.phase);
            putString(item, "clientId", rec.clientId);
            putString(item, "path", rec.path);
            putString(item, "pendingId", rec.pendingId);
            json::Value count;
            count.type = json::Value::Type::Number;
            count.number = static_cast<double>(rec.canonicalMatches);
            item.object["canonicalMatches"] = std::move(count);
            json::Value send;
            send.type = json::Value::Type::Bool;
            send.boolean = rec.sendEnabled;
            item.object["sendEnabled"] = std::move(send);
            json::Value matched;
            matched.type = json::Value::Type::Bool;
            matched.boolean = rec.matched;
            item.object["matched"] = std::move(matched);
            json::Value expected;
            expected.type = json::Value::Type::Bool;
            expected.boolean = rec.expectedMismatch;
            // ★ §7 判据的机器读数：控件上的实际文案 + 形状判据。
            putString(item, "bubbleText", rec.bubbleText);
            {
                json::Value shape;
                shape.type = json::Value::Type::Bool;
                shape.boolean = rec.bubbleShapeOk;
                item.object["bubbleShapeOk"] = std::move(shape);
            }
            item.object["expectedMismatch"] = std::move(expected);
            records.array.push_back(std::move(item));
            if (rec.expectedMismatch) {
                ++expectedMismatches;
                checksPassed = checksPassed && !rec.matched && rec.phase == "negative";
            } else {
                checksPassed = checksPassed && rec.matched;
            }
        }
        const bool probePassed = m_workspaceProbeDone && m_workspaceProbeLog.size() == 9 &&
                                 expectedMismatches == 1 && checksPassed;
        json += "  \"workspaceProbe\": " + records.Dump() + ",\n";
        json += std::string("  \"workspaceProbePassed\": ") +
                (probePassed ? "true,\n" : "false,\n");
    }
    // ★ 上下文占用读数（分子/分母都如实输出，便于复核百分比是否算对）
    std::snprintf(buf, sizeof(buf), "  \"usageInput\": %lld\n",
                  static_cast<long long>(m_sess->usageIn));
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
