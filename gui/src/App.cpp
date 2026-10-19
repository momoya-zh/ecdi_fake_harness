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
constexpr int kSidebarMaxRows = 4;   // 可视高度上限；更多的靠滚动

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
    } else {
        // 回放模式：建一个空会话（无 source ⇒ 不拉取；OnReplayTick 见空 source 直接返回）
        auto session = std::make_unique<Session>();
        session->label = "会话 " + std::to_string(m_sessions.size() + 1);
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
    const size_t rowStride = static_cast<size_t>(kSidebarRowH + kSidebarRowGap);
    int y = 0;
    for (size_t i = 0; i < n; ++i) {
        // ★ `orderOverride` 只给负对照用：按给定次序建行。
        //   ★ 选中态仍按**真实活动会话**判定（`idx == m_activeIndex`），不强制 false——
        //     负对照必须只改**一个**自变量（次序）。若同时把选中态造没了，
        //     比对报出的差异就混着两种原因，分不清探针抓到的是哪个（单变量纪律，
        //     同 REQ-01/03/04 被反转的教训）。
        size_t idx = i;
        if (orderOverride) {
            if (orderOverride->size() != n) break;   // 形状不对就不画（探针会给同长度）
            idx = (*orderOverride)[i];
            if (idx >= n) break;
        }
        const bool active = (idx == m_activeIndex);
        auto row = BuildSessionRow(idx, active);
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += static_cast<int>(rowStride);
    }
    const int totalH = n > 0 ? static_cast<int>(n * rowStride) - kSidebarRowGap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * static_cast<int>(rowStride)
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
        // 字段序：名称 · 选中 · 行内按钮数（×）· 行矩形（几何也算视图的一部分：
        // 「画了几行」和「画在哪」都能被抓出来）。
        char geo[64];
        std::snprintf(geo, sizeof(geo), "%d,%d,%d,%d", row->GetX(), row->GetY(),
                      row->GetWidth(), row->GetHeight());
        out.push_back(name + '\x1f' + selected + '\x1f' + std::to_string(buttons) + '\x1f'
                      + geo);
    }
    return out;
}

HarnessApp::SidebarCompare HarnessApp::CompareSessionList(
    const std::vector<std::string>& before, const std::vector<std::string>& after) const {
    static const char* kFieldName[] = {"name", "selected", "buttons", "geometry"};
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
        for (size_t f = 0; f < a.size() && f < b.size() && f < 4; ++f) {
            if (a[f] == b[f]) continue;
            detail += std::string(" ") + kFieldName[f] + " [" + a[f] + "] -> [" + b[f] + "]";
            break;
        }
        cmp.firstDivergence = detail;
        return cmp;
    }
    return cmp;
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
    // P1.5：--auto-prompt（跑批/冒烟——启动后立即走一次真实发送路径）
    if (!m_options.autoPrompt.empty() && m_sess->writer) {
        SendPrompt(m_options.autoPrompt);
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
        RequestRepaint();
    }
    if (!m_sess->source) return;   // 源启动失败（Run 已报错退出路径）/ 0d 空会话：无可拉取
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
            m_hudDirty = true;
            RefreshHud();
        }
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
        AddNoticeRow("[error] " + msg.GetString("message"), kErrorDark);
        return;
    }
    if (t == "hello") {
        // 捕获给 HUD 左栏（转录里仍留一行 core 信息，与原型一致）
        m_sess->sessionId = msg.GetString("session");
        m_sess->coreVer = msg.GetString("core");
        m_sess->protocolVer = static_cast<int>(msg.GetNumber("protocol"));
        // ★★ 协议 §9（v2 落地，2026-10-14）：core 声明的版本**高于**本 GUI 支持的上限
        //   ⇒ **拒绝连接并显示原因，不做降级猜测**。
        //   ★ 为何现在才实现：该规则自 v1 就写在文档里，但**从未被实现**——
        //     在只有 v1 的世界里它恒不触发，属"写在纸上但没人执行"的条款。
        //     v2 出现的这一刻它才真正开始起作用（也正是它存在的理由）。
        //   ★ 缺字段 ⇒ 按 v1 处理（宽容优于拒连：缺字段不是"更高的版本"）。
        if (m_sess->protocolVer > kProtocolMaxSupported) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "[协议不兼容] core 声明 protocol %d，本 GUI 支持到 %d"
                          " —— 按 §9 拒绝连接（不做降级猜测）",
                          m_sess->protocolVer, kProtocolMaxSupported);
            AddNoticeRow(buf, kErrorDark);
            m_sess->protocolIncompatible = true;
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
            m_sess->capsStr = joined;
        }
        // ★ 协议 v2 §3.1：上下文容量（百分比的分母）。缺字段 ⇒ 保持 0 ⇒ 显示 `—`。
        //   ★ **不猜容量**：分母未知时百分比无定义，猜一个比不显示更糟（编造读数）。
        if (const json::Value* ctx = msg.Find("context");
            ctx && ctx->IsNumber() && ctx->number > 0) {
            m_sess->contextLimit = static_cast<long long>(ctx->number);
        }
        m_hudDirty = true;
        AddNoticeRow("core " + msg.GetString("core") + " · session " +
                         msg.GetString("session") + " · protocol " +
                         std::to_string(m_sess->protocolVer),
                     kDarkMuted);
        return;
    }
    if (t == "state") {
        m_sess->coreState = msg.GetString("value");
        m_hudDirty = true;
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
            if (entry.answered) {
                stored.answered = true;
                stored.allowBtn->SetEnabled(false);
                stored.denyBtn->SetEnabled(false);
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
        if (r.headerBtn) line += r.headerBtn->GetText();
        line += '\x1f';
        if (r.label) line += r.label->GetText();
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
    static const char* kFieldName[] = {"kind", "id", "expanded", "answered", "header", "label"};
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

    // ★ 焦点复原：合成点击把焦点交给了被点的 Button，留下的焦点框与 caret 闪烁
    //   会多画几帧 ⇒ paints/frames 读数被污染（同 RunHitProbe 的收尾）。
    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_sidebarDirty = false;

    m_sidebarProbeDone = true;
    if (m_application) m_application->Exit();
}

// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
void HarnessApp::SendPrompt(const std::string& text) {
    // ★ --hit-probe：探针合成点击时**只计数、零副作用**——
    //   否则每跑一次探针就会往转录里塞一行 "[prompt 仅记录] …"，污染摘要读数。
    if (m_hitProbeActive) {
        ++m_hitProbeSendCalls;
        return;
    }
    if (m_sess->writer) {
        // live：真发 prompt（协议 §6：仅 idle 合法——按钮使能已由 state 驱动）
        const bool ok = m_sess->writer->Prompt(text);
        AddNoticeRow(ok ? "[prompt → core] " + text : "[prompt 发送失败] " + text,
                     ok ? kDarkMuted : kErrorDark);
    } else {
        AddNoticeRow("[prompt 仅记录] " + text, kDarkMuted);   // 回放：只记录
    }
    Relayout();   // settle 后立即反馈，不等下一个 tick
    RequestRepaint();
}

void HarnessApp::MarkAnswered(RowRec& rec, const std::string& choice) {
    if (rec.answered) return;
    rec.answered = true;
    // ★ 0c：已答进模型（重建时回填成"置灰"而不是"可再答一次"）
    if (rec.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[rec.timelineIdx].answered = true;
    }
    if (rec.allowBtn) rec.allowBtn->SetEnabled(false);
    if (rec.denyBtn) rec.denyBtn->SetEnabled(false);
    m_sess->waitForAnswer = false;
    if (m_sess->writer) {
        // live：真发 answer（协议 §4：需 id + choice；I3 的解除信号）
        if (!rec.askId.empty()) {
            m_sess->writer->Answer(rec.askId, choice);
        }
    }
    m_sess->askHist.emplace_back(choice, rec.label ? rec.label->GetText() : std::string());
    if (m_sess->askHist.size() > 16) m_sess->askHist.pop_front();
    m_hudDirty = true;
    AddNoticeRow("[answer] " + choice, kDarkMuted);
    RequestRepaint();
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
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.rowCount, r.sessionCount, r.activeIndex,
                          r.selectedRows, r.selectedIndex, r.transcriptRows,
                          r.matched ? "true" : "false",
                          esc(r.firstDivergence).c_str());
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
