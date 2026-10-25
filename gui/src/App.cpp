#include "App.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// ★ 2026-10-25（Stage 1c）：`ShlObj.h` / `objbase.h` / `commdlg.h` / `Credentials.h`
//   随导出（AppExport.cpp）与选目录（AppWorkspace.cpp）、凭据（AppSettings.cpp）
//   一起搬走了。本文件不再碰任何 Windows 壳对话框或 COM 初始化——
//   若将来又需要，请连同调用点一起看，别在这里无脑加回来。
#include <ctime>   // ★ P4 D：MarkAnswered 的 answerAt（本地 HH:MM）

#ifdef DrawText
#undef DrawText
#endif


#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Platform/PlatformWindow.h"
// ★ 2026-10-25（Stage 1d）：随代码搬走的 include 已清掉——
//   `UTF8.h`（ElideMiddleByCodepoints → AppSession）、`BackendFactory.h`（建后端在 main.cpp）、
//   `MouseButton*.h`（合成鼠标事件 → ProbeSupport）、`ScrollBar.h`（grow-probe 读滚动条 → ProbeFlow）。
//   ★ 判据是机读的：`grep -c "\b符号\b" App.cpp` 只剩 include 行本身。
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"   // OnMouseWheel 的形参类型
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"   // OnKeyDown / HandleGlobalKeyDown 的形参类型
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/Layout.h"   // ★ P4 F：HarnessRootLayout 基类
#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Layout/VerticalLayout.h"

#include <set>   // ★ P4 F：HarnessRootLayout 的豁免集
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"

#include "Theme.h"
#include "ThemeUsing.h"    // ★ 2026-10-25：主题令牌短名收口（原文件级 52 条 using）
#include "AppInternal.h"   // ★ 2026-10-25：跨 TU 共享的路径转换与几何常量
#include "UiKit.h"
#include "ThemedCaptionBar.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

// ★★ 2026-10-25：路径转换三兄弟搬到 `PathUtil.cpp`（`harness::internal`）。
//   为什么必须搬：它们原本是这个 TU 的**内部链接** helper，而拆分后
//   Probe*.cpp 也要转路径（RunWorkspaceProbe / RunMenuProbe / RunHistoryProbe）。
//   这里只留短名 ⇒ App.cpp 里 200+ 个调用点一个字都不用改。
using harness::internal::Utf8FromWidePath;
using harness::internal::WideFromUtf8Path;
using harness::internal::PathToUtf8;


// ★ 2026-10-25（Stage 1d）：转录行几何 / 「跳到最新」按钮 / tooltip 延时
//   整组定义移到 `AppInternal.h`（AppTranscript.cpp 建行、AppHud.cpp 摆位都要读
//   同一组数），此处只留短名。kTooltipInk 仍在本文件（只有 BuildMainArea 用它）。
using harness::internal::kListRowHeight;
using harness::internal::kRowGap;
using harness::internal::kRowPadX;
using harness::internal::kCanvasPadX;
using harness::internal::kHeaderH;
using harness::internal::kJumpBtnW;
using harness::internal::kJumpBtnH;
using harness::internal::kTooltipDelayMs;

// ★★ P4 Phase A（2026-10-25）：转录贴底判定阈值。此前是 FollowTailIfPinned 里的
//   内联 60；「跳到最新」按钮的显隐与贴底跟随共用同一条语义（离开底部多少算
//   "翻上去了"），抽成常量避免两处各写一个数日后漂移。
//   ★ 2026-10-25：定义移到 `AppInternal.h`（探针 TU 也要用同一条阈值），此处只留短名。
using harness::internal::kPinThreshold;

// ★ 2026-10-25（Stage 1d）：UI 文本/测量 helper 与探针高对比色短名。
using harness::internal::ContentWidth;
using harness::internal::ConversationTitle;
using harness::internal::ConversationSubtitle;
using harness::internal::UnknownKindText;
using harness::internal::ReasoningTitle;
using harness::internal::Pick;
using harness::internal::kProbeBg;
using harness::internal::kProbeInk;

// ★★ P4 E：tooltip 配色（深底浅字——两主题通用，不进 g_theme：它是浮层
//   系统的固定视觉，不是随主题走的表面色）。
//   ★ 2026-10-25（Stage 1d）：仍然留在这里——只有 BuildMainArea 建 tooltip 时用，
//     搬去 AppHud 反而要反向依赖；等 1e 拆 BuildMainArea 时再一起走。
const ECDI::Color kTooltipInk = ECDI::Color::FromRGBA8(240, 241, 245);

// ★★ Step 1（2026-10-19）：侧栏会话列表的几何。
//   ★ 为什么行宽是 158 而不是容器的 172：`ScrollView` 的垂直条一旦出现，
//     viewport 会缩到 `172 - (12 + 2) = 158`（`ScrollView.cpp` 的 `BarFootprint`）。
//     行宽若写成 172，出条的那一刻行会被裁掉 14px（还会多出一条水平条）——
//     于是"会话多到要滚动"这件事本身会改变行的排版。
//   ★ 行高 28 与原型 C 的 navitem 同高；行间距 4（比正文的 10 紧，列表才有"一列"的感觉）。
//   ★ 2026-10-25（Stage 1c）：整组定义移到 `AppInternal.h`（AppHistory.cpp 建历史
//     列表行要用同一组数），此处只留短名。
using harness::internal::kSidebarListW;
using harness::internal::kSidebarRowW;
// ★ 2026-10-25：行高与标题行高移到 `AppInternal.h`（--sidebar-probe 要量这两条），此处留短名。
using harness::internal::kSidebarRowH;
using harness::internal::kSidebarRowGap;
using harness::internal::kSidebarNameW;   // 22(dot 右缘) → 128，给右侧 18px 删除钮留 6px 间隙
using harness::internal::kSidebarMaxRows;
// ★★ 2026-10-20：工作区分组标题行的高度。
//   比会话行（28）矮得多：它是**分隔**，不是条目。若与行同高，列表中“工作区”与
//   “会话”就会长得一样高，用户无法从尺寸上区分两层结构。
// ★ 2026-10-25：定义移到 `AppInternal.h`（探针与产品侧共用同一条行高）。
using harness::internal::kSidebarGroupH;   // 标题行高（分隔的高度，不带"条目"语义）

// 视觉令牌唯一来源 = Theme.h（2026-10-06 v6：对齐用户高保真模型）
// ★★ 本 GUI 支持的协议主版本上限（协议 §9；2026-10-14 v2 落地）。
//   core 声明的 `hello.protocol` **高于**此值 ⇒ 拒绝连接（见 `RouteLastMessage`）。
//   ★ 改这一处 = 声明支持新的主版本——与 `protocol/PROTOCOL.md` §9 的表格**必须同步**。
//   ★ 2026-10-25：定义移到 `AppInternal.h`（AppSummary.cpp 也要读它），此处只留短名。
using harness::internal::kProtocolMaxSupported;

// ★ 2026-10-25：主题令牌短名（52 条 using）已收口到 `ThemeUsing.h`——
//   拆分后每个新 TU 都要这组短名，复制 5 遍迟早漂移。见该头文件的说明。

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
// ★ 2026-10-25（Stage 1d）：kProbeBg / kProbeInk / Pick 上提 `AppInternal.h`
//   （转录与 HUD 两个 TU 都要用）。kProbeCanvas 只在这里用（Run/RebuildUi 设窗口底色）。

// ★★ P4 F（2026-10-25）：root 专用布局——逐位复刻 VerticalLayout(0, true) 的
//   摆位算法（spacing=0 / fillCrossAxis / 隐藏子 C-VIS 停泊），但对 **Exempt 注册
//   的浮层**（设置遮罩/sheet）完全跳过：不进统计、不停泊、不摆位（几何归
//   LayoutSettingsOverlay）。
//   ★ 为什么必须换：原 VerticalLayout 把**可见的**浮层当固定尺寸子统计——
//   mask(整窗高)+sheet 一显示，主区就被挤没了（实测 counting 被挤到 0×0）。
//   ★ 隐藏子停泊语义保留（C-VIS-4）：非豁免的隐藏子照旧停 (−w,−h)。
class HarnessRootLayout : public ECDI::Layout {
public:
    void Exempt(ECDI::Widget* w) { m_exempt.insert(w); }

    void Arrange(ECDI::Widget& parent) override {
        const size_t count = parent.GetChildCount();
        int fixedTotal = 0;
        int totalStretch = 0;
        size_t visibleCount = 0;
        size_t visibleStretchCount = 0;
        for (size_t i = 0; i < count; ++i) {
            const ECDI::Widget* child = parent.GetChildAt(i);
            if (!child->IsVisible() || m_exempt.count(child) > 0) continue;
            ++visibleCount;
            if (child->GetStretch() > 0) {
                totalStretch += child->GetStretch();
                ++visibleStretchCount;
            } else {
                fixedTotal += child->GetHeight();
            }
        }
        const int remaining =
            (std::max)(0, parent.GetHeight() - fixedTotal);
        const int cross = (std::max)(0, parent.GetWidth());
        int y = 0;
        int allocated = 0;
        size_t stretchSeen = 0;
        for (size_t i = 0; i < count; ++i) {
            ECDI::Widget* child = parent.GetChildAt(i);
            if (m_exempt.count(child) > 0) continue;   // 浮层：视而不见
            if (!child->IsVisible()) {
                const int parkW = child->GetWidth();
                const int parkH = child->GetHeight();
                child->SetPosition(-parkW, -parkH);   // C-VIS-4 停泊
                continue;
            }
            if (child->GetStretch() > 0) {
                ++stretchSeen;
                int height = (stretchSeen == visibleStretchCount)
                                 ? remaining - allocated
                                 : static_cast<int>(static_cast<long long>(remaining) *
                                                    child->GetStretch() / totalStretch);
                allocated += height;
                child->SetSize(cross, height);
            } else {
                child->SetSize(cross, child->GetHeight());
            }
            child->SetPosition(0, y);
            y += child->GetHeight();
        }
    }

private:
    std::set<const ECDI::Widget*> m_exempt;
};
}   // namespace

// ★ 2026-10-25（Stage 1d）：ContentWidth / ConversationTitle / ConversationSubtitle /
//   UnknownKindText / ReasoningTitle 搬到 `UiCommon.cpp`（`harness::internal`）。
//   它们现在被 AppTranscript.cpp 与 AppHud.cpp 同时使用，留在匿名命名空间里
//   等于把「单一真相源」拆成三份。短名见文件顶部的 using 区。

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
        // ★★ P4 Phase A：任何定时器节拍先做一次「跳到最新」脏检查（int 比较，
        //   offset 没变就返回）。拖滚动条滑块没有事件回调，靠 caret 闪烁定时器
        //   的节拍兜底是唯一覆盖口——故放在定时器 id 分流**之前**。
        m_owner->DirtyRefreshJumpButton();
        if (event.GetTimerId() == HarnessApp::kTooltipTimer) {
            m_owner->OnTooltipFire();   // ★ P4 E：400ms 到点，显示 tooltip
            return;
        }
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

// ★★ 2026-10-21：窗口拉伸 → 走我们自己的重排链。
//   ★ 顺序不可换：**先**让基类同步 `RootWidget` 尺寸（基类 `OnWindowResized` 干的就是
//     这一件事），**再**重排。反过来的话，`Relayout()` 会拿 `m_scroll` 的**旧宽度**
//     去测每一行 ⇒ 重排白跑一次，下一帧才发现宽度已经变了。
//   ★ 这里**不**重建树、**不**重建转录视图：尺寸不属于模型，`Relayout()` 已经覆盖。
void HarnessApplication::OnWindowResized(const ECDI::WindowResizedEvent& event) {
    ECDI::Application::OnWindowResized(event);   // ① root 尺寸同步（必须在重排之前）
    if (m_owner) m_owner->OnWindowResized(event.GetWidth(), event.GetHeight());
}

// ★★ P4 Phase A（2026-10-25）：滚轮 → 基类先把事件派给 ScrollView（滚动真的
//   发生），**再**刷新「跳到最新」按钮。顺序不可换：反了的话 RefreshJumpButton
//   读到的是滚动前的 offset，按钮显隐滞后一拍。
void HarnessApplication::OnMouseWheel(const ECDI::MouseWheelEvent& event) {
    ECDI::Application::OnMouseWheel(event);
    if (m_owner) m_owner->RefreshJumpButton();
}

// ★★ P4 Phase A：键盘单收件人（Application::OnKeyDown 只派给焦点控件），
//   导航键可能经 TextBox 的滚动（多行输入）间接改变转录状态——这里在派发
//   **之后**补一次按钮刷新。转录本身暂无键盘滚动路径（焦点在输入框时
//   PageUp/Home 等全被 TextBox 吃掉）。
// ★★ P4 Phase B：**之前**先判全局快捷键（顺序 = 拦截 → 未命中转基类派发 →
//   派发后补按钮刷新）。命中即 SetHandled 并 return——不调基类，焦点控件
//   （含输入框）不会再见到这个键，Ctrl+B 在打字中途也成立。
void HarnessApplication::OnKeyDown(const ECDI::KeyDownEvent& event) {
    if (m_owner && m_owner->HandleGlobalKeyDown(event)) {
        event.SetHandled();
        return;
    }
    ECDI::Application::OnKeyDown(event);
    if (!m_owner) return;
    switch (event.GetKeyCode()) {
        case ECDI::KeyCode::PageUp:
        case ECDI::KeyCode::PageDown:
        case ECDI::KeyCode::Home:
        case ECDI::KeyCode::End:
            m_owner->RefreshJumpButton();
            break;
        default:
            break;
    }
}

// ── 构造 / 运行 ────────────────────────────────────────────────

HarnessApp::HarnessApp(CliOptions options)
    : m_options(std::move(options)) {
    // ★★ P4 H：配置**无条件**先加载一次——此前只在 firstRun 直连路径里 Load，
    //   回放/--replay 模式的 providers 恒空（模型浮窗/右栏只读卡全 "—"）。
    //   失败只记日志：文件不存在 = 干净首启（TryAutoConnectFromConfig 语义不变）。
    {
        std::string cfgError;
        if (!m_appConfig.Load(cfgError)) {
            ECDI::Logger::Log(ECDI::LogLevel::Warning,
                              ECDI::UTF8ToWide("AppConfig: " + cfgError));
        }
    }
    // ★★ P3.5（规格 §8.5 D7）：无参启动的解析在**建会话之前**——
    //   配置 + 凭据都可用 ⇒ 填 corePath/coreArgs/m_coreApiKey，下面照常建 live 会话；
    //   否则 ⇒ 首启：设置面板打开（会话为空壳，保存成功后再建 live 会话）。
    if (m_options.firstRun) {
        if (!TryAutoConnectFromConfig()) {
            m_settingsVisible = true;   // 首启引导（或配置失效——hint 会说明原因）
        }
    }
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

// ＋ 新建会话的真实行为（0d）。
//   ★ live 模式：再拉起一个 core 子进程 —— 这才是"真的新建一个会话"。
//   ★ 回放模式：没有 core 会对着一个空会话说话，回放夹具是一次性的。硬塞一个
//     假回复就是伪造读数（本仓最守不住的线）。故如实建一个**空会话**并说明情况——
//     "能点、有状态、切得过去"，但绝不假装 core 回了话。

// 切换活动会话（0d）：拆当前视图 → 指向目标会话 → 按目标会话模型重建。

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


// ★★ 2026-10-20：工作区分组标题行。
//   两种行长得不一样，但**只差在尺寸与颜色**，不需新控件类型：
//   `SnapshotSessionList` 靠“行根是不是 Button”区分两类行（第 5 段 `kind`）。


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


// ★★ 2026-10-20：归档 / 恢复（m00001 第 6 步残留）。
//   归档 = 「不活动但可打开且不占资源」。会话留在 m_sessions 原位（下标不动），
//   只是从侧栏主列表隐藏、移到「归档」区。


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

// ★★ P4 Phase B（2026-10-25）：检视栏显隐的唯一入口（中栏头 `≡` 按钮与 Ctrl+I
//   共用）。抽出前逻辑内联在按钮回调里——全局快捷键没有可复用的路径，两份
//   实现迟早漂移（SetSidebarVisible 的"唯一入口"纪律）。
void HarnessApp::ToggleInspector() {
    if (!m_hud.inspector) return;
    m_inspectorVisible = !m_inspectorVisible;
    m_hud.inspector->SetVisible(m_inspectorVisible);
    if (ECDI::Widget* parent = m_hud.inspector->GetParent()) parent->Arrange();
    Relayout();
    RequestRepaint();
}

// ★★ P4 Phase B（2026-10-25）：全局快捷键分派（HarnessApplication::OnKeyDown
//   在转基类**之前**调用；命中返回 true ⇒ 事件到此为止，不派给焦点控件）。
//   ★ 刻意不拦 Ctrl+A/C/X/V/Z（TextBox 编辑语义）与 Enter（PromptBar 发送）。
bool HarnessApp::HandleGlobalKeyDown(const ECDI::KeyDownEvent& event) {
    const ECDI::KeyCode key = event.GetKeyCode();
    if (key == ECDI::KeyCode::Escape) {
        // 逐层关：设置 > 模型浮窗（P4 H）> ···菜单。
        // 无层可关 ⇒ 返回 false 让事件照常派发（Esc 不悬空拦截）。
        if (m_settingsVisible) {
            SetSettingsVisible(false);
            return true;
        }
        if (m_modelPopupOpen) {
            SetModelPopupOpen(false);
            return true;
        }
        if (m_moreMenuOpen) {
            SetMoreMenuOpen(false);
            return true;
        }
        return false;
    }
    if (!event.IsCtrlDown()) return false;
    switch (key) {
        case ECDI::KeyCode::B:
            SetSidebarVisible(!m_sidebarVisible);
            return true;
        case ECDI::KeyCode::I:
            ToggleInspector();
            return true;
        case ECDI::KeyCode::Comma:
            SetSettingsVisible(!m_settingsVisible);
            return true;
        case ECDI::KeyCode::N:
            NewSession();
            return true;
        case ECDI::KeyCode::M:   // ★ P4 H：模型浮窗开合（同顶栏 chip 点击）
            SetModelPopupOpen(!m_modelPopupOpen);
            return true;
        default:
            return false;
    }
}

// 冻结活动会话的 timed 锚点：记下暂停时刻（幂等——已暂停则不动）。

// 恢复某会话的 timed 锚点：把锚点整体后移"被冻结了多久"。
//   ★ 为何需要补偿：elapsedMs = now - anchor。会话被切走期间 anchor 不动而 now 前进，
//     切回来若不补偿，elapsedMs 会凭空多出"离开的那段墙上时间" ⇒ 一次性抽干夹具。

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
    // ★★ 2026-10-23（ECDI REQ-13 落地）：窗口下限交给**框架**，不再由本仓在
    //   `OnWindowResized` 里夹。判据与理由见 `App.h` 的 `kMinClientW` 注释。
    //   ★ 为什么必须在 `Show()` 之前登记：下限本身运行期改也有效（框架契约），
    //     但在建窗时一次登记好，才能让**第一帧**就落在合规尺寸上——否则窗口会
    //     先以 1120×760 之外的任何尺寸出现一瞬。1120×760 本身就在下限之上，
    //     故此处登记不改变任何既有读数。
    win.SetMinimumSize(kMinClientW, kMinClientH);

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

    // ★★ REQ-03：装唤醒**必须在这里**——UI 树已经建好（`BuildUi` + `Show` 之后），
    //   否则第一个唤醒回调里的 `HandleLine` 会去碰还不存在的控件行。
    //   此刻消息循环尚未进入，投递只会在队列里等着，天然没有竞态。
    InstallWakeHandlers();
    // ★ 对照世界（`--legacy-poll`）也要开戳：否则「推更快」只剩结构论证，
    //   没有同口径读数可对。`InstallWakeHandler` 里已经给默认世界开过（幂等）。
    if (m_options.asyncProbe) {
        for (auto& session : m_sessions) {
            if (session->child) session->child->EnableLineStamps(true);
        }
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
    // ★★ P4 F 修复（实测反馈）：root 布局换 HarnessRootLayout——逐位复刻
    //   VerticalLayout(0,true) 但跳过 Exempt 注册的全窗浮层（设置遮罩/sheet，
    //   它们挂 root 盖全窗）。原布局把**可见的**浮层当固定尺寸子统计
    //   （mask 整窗高 + sheet 560 高），一开设置主区就被挤没了。浮层几何
    //   归 LayoutSettingsOverlay。BuildUi 尾部转移挂载后注册豁免。
    auto rootLayout = std::make_unique<HarnessRootLayout>();
    HarnessRootLayout* const rootLayoutRaw = rootLayout.get();
    root.SetLayout(std::move(rootLayout));

    // CaptionBar 是 root 的直接子项：保持贴边全宽，空白区可拖动，按钮由 harness 自绘
    // （ECDI 原生 CaptionBar 把颜色写死，跟不了主题 —— 见 ThemedCaptionBar.h 头注）。
    auto caption = std::make_unique<harness::ThemedCaptionBar>(
        window, "ECDI harness - " + m_options.fixtureName);
    m_caption = caption.get();
    // ★★ 2026-10-21：宽度不再写死 1120 —— 取当前客户区宽度。
    //   写死的后果：窗口一拉伸，标题栏（它自己画最小化/最大化/关闭三个钮）就停在
    //   1120 宽的位置，右侧留一条同色空白、三个钮不在窗口右上角。
    //   ★ 这里读的是**构造时**的真实宽度；运行期变化由 `OnWindowResized` 接手。
    caption->SetSize(std::max(320, window.GetRootWidget().GetWidth()),
                     ECDI::CaptionBar::kDefaultHeight);
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
    // ★★ P4 F 修复（实测反馈「变色应该是整个变色」）：遮罩 + sheet 转移到
    //   **root 末尾**（最后子 = 最上层，盖过 caption/counting 全窗——含顶栏/
    //   侧栏/检查器；此前挂 m_scroll 只盖转录视口）。转移必须在 root.Arrange()
    //   **之前**：Arrange 递归驱动子树，挂好后 mask/sheet 尺寸交给
    //   LayoutSettingsOverlay（Relayout 尾部）摆。
    if (m_hud.settingsMask && m_hud.settingsMask->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.settingsMask));
        rootLayoutRaw->Exempt(m_hud.settingsMask);   // 布局视而不见
    }
    if (m_hud.settingsPanel && m_hud.settingsPanel->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.settingsPanel));
        rootLayoutRaw->Exempt(m_hud.settingsPanel);
    }
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
        // ★★ 2026-10-21：草稿恢复**必须显式重算高度**。
        //   `SetText` 不走 `RaiseTextChanged`（D7 契约），而 `ComposerBox` 的自动长高
        //   挂在 `OnTextChanged` 上 ⇒ 这里若不同步，一个 3 行的草稿会在重建后
        //   被塞回单行高（垂直压扁、光标跑到框外）——而 `CompareUi` 的
        //   `inputText` 通道**读的是文本，看不见高度**，所以这个缺陷在整树重建
        //   探针里是全绿的。这正是"读文本看不见几何"那条老账。
        m_promptBar->SyncInputHeight();
        // ⑨ 焦点交还输入框：既恢复 caret 闪烁（证明焦点真的回来了），
        //    也让 `Application::OnTimer` 重新有派发目标（否则 tick 停摆）。
        m_window->SetFocusedWidget(m_promptBar->input());
    }

    // ⑩ 侧栏显隐是**模型侧状态**（`m_sidebarVisible`），新树默认全可见 ⇒ 按它恢复。
    if (!m_sidebarVisible && m_hud.sidebar) {
        m_hud.sidebar->SetVisible(false);
        if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();
    }
    // ⑩c P3.5：设置面板显隐同为模型侧状态 ⇒ 走唯一的投影路径对齐。
    ApplySettingsVisibility();
    // ★★ 2026-10-20⑩b / 2026-10-21 订正：`···` 菜单条的显隐同样是模型侧状态。
    //   ★ 原文写的是「新树的菜单条是**可见**的（`BuildUi` 建完还没投影）」——
    //     那句在 2026-10-21 之后**已经不成立**：`BuildMainArea` 现在建完就按
    //     `m_moreMenuOpen` 投影了（缺陷经过见那一段的注释）。留着旧说法会让人以为
    //     这里在补一件 `BuildUi` 没做的事，而实际上两处做的是同一件事。
    //   ★ 之所以**仍然保留**这一步：`BuildUi` 那句只在**建树那一刻**读模型，
    //     而这里是整树重建收尾前的最后一道对齐；代价是一次赋值 + 一次 Arrange，
    //     且 `SetVisible` 同值不 Invalidate（`Widget.cpp:427-446`）。
    //   ★ 仍然直接打 `SetVisible` 而不调 `SetMoreMenuOpen`：后者带同值守卫
    //     （`m_moreMenuOpen == open` 直接返回），这里要的是「把控件状态对齐到模型」，
    //     两者不是一回事；且它还会 Relayout/Repaint，此处收尾会统一做。
    if (m_hud.moreMenu) {
        m_hud.moreMenu->SetVisible(m_moreMenuOpen);
        if (ECDI::Widget* parent = m_hud.moreMenu->GetParent()) parent->Arrange();
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
    auto menu = std::make_unique<uikit::TipButton>(
        "≡", "侧栏显隐（Ctrl+B）",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
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

    // ★★ 2026-10-21：后端读数胶囊（`gdi` / `gl`）。
    //
    //   【改前是什么样】`Hud::backendChip` **只被读过、从没被写过**：`RefreshHud`
    //     里有 `if (m_hud.backendChip) { ...SetText(m_backendName + " ▽") }`，
    //     而建树处**根本没有这个控件** ⇒ 那个分支永远不成立，屏幕上从来没有它。
    //     也就是说：一个"看起来存在的读数"实际上不存在，而代码里读得若有其事。
    //     这与 `sessSession` / `sessCore` 是同一类（读了但永不写），
    //     故一并处理，并且把这一类做成了可机检的判据（`--hud-probe`）。
    //
    //   【为什么**不**画 `▽`】原代码拼的是 `m_backendName + " ▽"`，`▽` 是个
    //     "可以点开选"的暗示。但后端**在 `Create` 时就定死了**（`--gl` /
    //     `--backend`，运行期没有任何切换路径：全仓只有一处给 `m_backendName`
    //     赋值，就在 GL 就绪检查之后），切后端要重建整个 `PlatformWindow`。
    //     ⇒ 画一个下拉箭头等于承诺一个不存在的菜单——这正是本步要清掉的那类
    //     "看着能点、其实不能"。所以这里**只显示名字**，不带箭头。
    //   ★ 也不做成 Button：它没有动作可做。做成按钮就是把人引到一条死路上。
    //   ★ **必须**把句柄存进 `Hud`：否则 `RefreshHud` 里那个 `if` 又变成死分支
    //     ——那正是它改前的状态。建树时赋值是这条链唯一的写入点。
    {
        auto backendLabel = chipLabel(m_backendName, kDarkMuted, 52);
        m_hud.backendChip = backendLabel.get();
        bar->AddChild(makeChip(std::move(backendLabel), 52, false));
    }

    bar->AddChild(uikit::DotCell(8, kOkDark, 20, &m_hud.statusDot));

    auto state = std::make_unique<ECDI::Label>("idle");
    m_hud.stateChip = state.get();
    state->SetTextColor(kDarkMuted);
    {
        state->SetFont(harness::SansFont(12.0f));
    }
    state->SetSize(60, 20);
    bar->AddChild(std::move(state));

    // ★★ P4 Phase H（2026-10-25）：顶栏模型 chip（`提供商 › 模型 · N% ▾`）。
    //   数据常驻（RefreshHud 里 UpdateModelChip）；点击开下拉浮窗（Ctrl+M 同）。
    auto modelChip = std::make_unique<uikit::TipButton>(
        "— ▾", "模型切换（Ctrl+M）——仅 idle 发出，运行中自动排队",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    m_hud.modelChip = modelChip.get();
    modelChip->SetTextColor(kDarkInk);
    modelChip->SetFont(harness::SansFont(harness::kFontSmall));
    modelChip->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = harness::kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    modelChip->SetSize(200, 22);
    modelChip->SetOnClick([this] { SetModelPopupOpen(!m_modelPopupOpen); });
    bar->AddChild(std::move(modelChip));

    // ★★ Step 4 / 0e（2026-10-19）：运行期主题切换控件。
    //   【为什么必须放在顶栏】它是**全局**状态的中枢，不是某个面板的属性；
    //   侧栏是会话的容器、检查器是读数的容器，都不该承载"整个程序长什么样"。
    //   【为什么是 Button】切换的唯一正确路径是"点一下 → 回调 → `RebuildUi`"。
    //   若做成 Label 加别处的热键，就没法用合成点击验证它——`RunThemeProbe` 的
    //   `toggle` 相走的正是 `SynthesizeClickAt` → 真实派发链 → `RaiseClick`。
    //   【为什么这里可以调 SetFont 而 `≡` 不行】`≡` 的 bug 是"用了与主题缺省不同
    //   的 family 导致字形漂移"；本钮显式指定 `SansFont`，两侧（深/浅）都按同一
    //   规则现算，不存在"和谁不一样"的问题。
    auto themeBtn = std::make_unique<uikit::TipButton>(
        "主题", "深色 / 浅色切换",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
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
        // ★★ 2026-10-20：动作从「`bool toggleInspector`」改为枚举。
        //   原来一参布尔只能表达「是不是检查器」，于是 `↗`/`···` 只能落到
        //   `(void)tip; return;`（空回调）。加动作就得加枚举——否则第三个布尔参
        //   会变成 `mkAct("···", "更多", false, true)` 那种读不懂的调用。
        enum class HeadAct { Export, Inspector, Menu };
        auto mkAct = [&head, this](const std::string& text, const std::string& tip,
                                   HeadAct act) {
            // ★★ P4 E：tip 参数终于有了去处（此前 `(void)tip` 闲置——ECDI 无
            //   tooltip 控件，mkAct 一直带着它等今天）。
            auto b = std::make_unique<uikit::TipButton>(
                text, tip,
                [this](uikit::TipButton* tb) { ShowTooltipFor(tb, tb->tip()); },
                [this](uikit::TipButton*) { HideTooltip(); });
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
            if (act == HeadAct::Inspector) m_hud.inspectorToggle = b.get();
            else if (act == HeadAct::Export) m_hud.exportToggle = b.get();
            else m_hud.moreToggle = b.get();
            b->SetOnClick([this, tip, act] {
                (void)tip;   // 提示文字目前无处显示（ECDI 无 tooltip 控件）；保留参数为将来
                switch (act) {
                case HeadAct::Export:
                    (void)ExportTranscript();
                    return;
                case HeadAct::Menu:
                    ToggleMoreMenu();
                    return;
                case HeadAct::Inspector:
                    break;
                }
                ToggleInspector();   // ★ P4 B：唯一入口（原内联逻辑抽出，Ctrl+I 共用）
            });
            head->AddChild(std::move(b));
        };
        mkAct("↗", "导出转录", HeadAct::Export);
        mkAct("≡", "检查器", HeadAct::Inspector);
        mkAct("···", "更多", HeadAct::Menu);
        counting->AddChild(std::move(head));
    }

    // ── ★★ 2026-10-20：`···` 菜单条（内联下拉）───────────
    // 位置：`head` 与 `scroll` **之间**——点 `···` 后在头部正下方撑开一行，
    //   下方转录区自然下移。为什么不做真浮层见 `Hud::moreMenu` 的注释（ECDI
    //   没有菜单控件也没有跳过布局机制，浮层要跟三条 Arrange 路径手工对齐）。
    // ★★ 【2026-10-21 实测缺陷】建树时必须**按模型值**投影显隐，不能留默认可见。
    //   第一次落地时这里只写了 `SetSize`，把「默认隐藏」当成注释里的意图；
    //   而 `Widget` 的缺省状态是**可见**（`m_state.visible` 初值）⇒ 启动第一帧
    //   菜条就撑在头下面。截图证据 `.scratch/mid-shots/before-open-mid.png`：
    //   四个菜单项（导出/归档/清空视图）在**谁也没点 `···`** 时就挂在屏上，
    //   转录区被顶下去 34px。`RebuildUi` ⑩b 里那条投影**只救重建路径**，
    //   救不了首帧（`BuildUi` 跑完就直接 `Arrange` 了）。
    //   ⇒ 唯一正确的写法是建完立刻投影一次模型值：`m_moreMenuOpen` 的初值是 false，
    //     于是首帧是关的；而「菜单开着时换主题」走的也是同一句（重建后 ⑩b 再投影）。
    //   ★ 这里打 `SetVisible` 而不是 `SetMoreMenuOpen`：后者带同值守卫
    //     （`m_moreMenuOpen == open` 就返回），首帧两条都是 false ⇒ 会直接返回、
    //     **一句 SetVisible 都不发**，缺陷原样留下。守卫防的是「重复动作」，
    //     而这里要的是「把控件状态对齐到模型」，两者不是一回事。
    {
        auto menu = std::make_unique<ECDI::Panel>();
        m_hud.moreMenu = menu.get();
        menu->SetVisible(m_moreMenuOpen);   // ★ 见上：缺这一句 = 首帧露出 34px 空洞
        menu->SetSize(600, 34);
        menu->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kPanel2,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kLine});
        menu->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 8));

        // 三件都是**真实动作**（本步的目的：不再有空回调）：
        //   导出 = 同 `↗`；归档 = 同侧栏的「↓」；清空视图 = 重建视图（不碰模型）。
        auto mkMenuItem = [&menu, this](const std::string& label, const char* tip,
                                        std::function<void()> action) {
            auto b = std::make_unique<ECDI::Button>(label);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(84, 24);
            (void)tip;
            b->SetOnClick([this, action = std::move(action)] {
                // ★ 先合菜单再做事：动作本身可能弹对话框（导出）/ 改活动会话（归档），
                //   菜单留在原处会让人以为下一步还能点。
                SetMoreMenuOpen(false);
                action();
            });
            menu->AddChild(std::move(b));
        };
        mkMenuItem("导出", "导出转录到文件", [this] { (void)ExportTranscript(); });
        mkMenuItem("设置", "提供商连接设置（BaseURL / API Key / 模型）", [this] {
            // 打开时预填非秘密字段（BaseURL/模型来自配置）；key 字段**永不回填**
            // ——凭据库里有值时 hint 说明"留空保持不变"。
            if (m_hud.settingsBase) {
                const ProviderConfig* edit =
                    m_appConfig.Find(m_editingProviderId);
                if (!edit) edit = m_appConfig.DefaultProvider();
                if (edit) {
                    m_editingProviderId = edit->id;
                    m_hud.settingsBase->SetText(edit->baseUrl);
                    m_hud.settingsModel->SetText(edit->model);
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(
                            edit->context > 0 ? std::to_string(edit->context) : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(
                            edit->maxOutput > 0 ? std::to_string(edit->maxOutput) : "");
                    }
                    RebuildSettingsProviderList();
                }
                if (m_providerKeys.empty()) {
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText("填写提供商连接信息，保存后连接");
                } else {
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText(
                        "已保存的密钥不回显——留空则保持不变");
                }
            }
            SetSettingsVisible(true);
        });
        mkMenuItem("归档", "归档当前会话", [this] {
            if (!m_sess) return;
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == m_sess) {
                    (void)ArchiveSessionAt(i);
                    return;
                }
            }
        });
        // ★ 「清空视图」= 从模型重建视图（`RebuildTranscriptView`）。
        //   ★ 不写成「清空 m_sess->timeline」：那会**真的删掉转录模型**——
        //     而 `timeline` 是历史会话/重建/导出的唯一来源，删了就找不回来。
        //     这里要的是「视图错乱了就重画一遍」，模型的作者不是这个按钮。
        mkMenuItem("清空视图", "从模型重画视图", [this] { RebuildTranscriptView(); });
        counting->AddChild(std::move(menu));
    }

    // ── ★★ P3.5：设置面板（★★ P4 F 起改模态浮层：遮罩+居中 sheet 挂 m_scroll；
    //   显隐见 ApplySettingsVisibility——转录/composer 不再腾槽）──
    //   显隐是模型侧状态 `m_settingsVisible`（重建后恢复，同 m_sidebarVisible 纪律）。
    std::unique_ptr<ECDI::Widget> settingsSheetOwned;   // 暂存——scroll 建好后转移挂载
    {
        auto panel = std::make_unique<ECDI::Panel>();
        m_hud.settingsPanel = panel.get();
        panel->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 14));
        // ★ P3.6 观感：卡片底色 + 描边，与周边（kSidebarBg）明显区隔；
        //   尺寸由 LayoutSettingsOverlay 按视口钳制（初始值仅为基准）。
        panel->SetSize(560, 620);
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kCardBg,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kCardBorder});

        auto title = std::make_unique<ECDI::Label>("连接设置");
        title->SetTextColor(kDarkInk);
        title->SetFont(harness::SansFont(16.0f));
        title->SetSize(520, 26);
        panel->AddChild(std::move(title));

        // ★★ P4 F：字段区收进 ScrollView——sheet 高被视口钳制（vh-80）时字段
        //   可滚（修最小窗 880×460 下原 620 固定高的溢出）。底部保存行/hint
        //   留在 panel（固定底栏，不随字段滚走）。
        auto sv = std::make_unique<ECDI::ScrollView>();
        m_hud.settingsSv = sv.get();   // ★ P4 F 修复：LayoutSettingsOverlay 要补 extent
        sv->SetSize(520, 400);   // 基准尺寸（F2/D9 契约）；stretch=1 吃剩余高
        sv->SetStretch(1);
        sv->SetScrollStep(28);
        ECDI::Widget* content = &sv->GetContentView();
        content->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 4));
        panel->AddChild(std::move(sv));

        auto addLabel = [&content](const std::string& text) {
            auto l = std::make_unique<ECDI::Label>(text);
            l->SetTextColor(kDarkMuted);
            l->SetFont(harness::SansFont(harness::kFontSmall));
            l->SetSize(520, 20);
            content->AddChild(std::move(l));
        };
        auto addInput = [this, &content](ECDI::TextBox** slot, bool password) {
            auto box = std::make_unique<ECDI::TextBox>();
            *slot = box.get();
            (*slot)->SetSingleLine(true);
            if (password) {
                (*slot)->SetEchoMode(ECDI::TextBox::EchoMode::Password);
            }
            // ★★ P4 修复（实测反馈）：白卡上无边框的输入框与背景融为一体、
            //   几乎不可辨——灰底 + 描边（与挡位钮同款对比），实测可辨。
            (*slot)->SetStyle(ECDI::TextBoxStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .cornerRadius = harness::kRadiusBtn,
                .borderWidth = 1.0f});
            (*slot)->SetSize(520, 30);
            content->AddChild(std::move(box));
        };
        auto mkSmallButton = [&content](const char* text, int w, int h,
                                      std::function<void()> fn) {
            auto b = std::make_unique<ECDI::Button>(text);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(w, h);
            b->SetOnClick(std::move(fn));
            content->AddChild(std::move(b));
        };
        auto addPresetRow = [this, &content, &mkSmallButton](
                                const std::string& label, ECDI::TextBox** slot,
                                std::initializer_list<const char*> presets) {
            auto cap = std::make_unique<ECDI::Label>(label);
            cap->SetTextColor(kDarkMuted);
            cap->SetFont(harness::SansFont(harness::kFontSmall));
            cap->SetSize(520, 20);
            content->AddChild(std::move(cap));
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 26);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto box = std::make_unique<ECDI::TextBox>();
            *slot = box.get();
            (*slot)->SetSingleLine(true);
            (*slot)->SetSize(120, 26);
            row->AddChild(std::move(box));
            for (const char* p : presets) {
                auto b = std::make_unique<ECDI::Button>(p);
                b->SetTextColor(kDarkInk);
                b->SetFont(harness::SansFont(harness::kFontSmall));
                b->SetStyle(ECDI::ButtonStyleOverride{
                    .background = kPanel2,
                    .border = kLine,
                    .borderWidth = 1.0f,
                    .cornerRadius = kRadiusBtn,
                    .pressedBackground = kToolHeaderPressed,
                    .hoverBackground = kToolHeaderHover});
                b->SetSize(56, 26);
                ECDI::TextBox* target = *slot;
                b->SetOnClick([this, target, text = std::string(p)] {
                    if (target) target->SetText(text);
                });
                row->AddChild(std::move(b));
            }
            content->AddChild(std::move(row));
        };

        auto listCap = std::make_unique<ECDI::Label>(
            "提供商（点击选择编辑；● = 当前连接）");
        listCap->SetTextColor(kDarkMuted);
        listCap->SetFont(harness::SansFont(harness::kFontSmall));
        listCap->SetSize(520, 20);
        panel->AddChild(std::move(listCap));
        {
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.settingsProviderList = list.get();
            list->SetSize(520, 108);
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            content->AddChild(std::move(list));
        }
        {
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 26);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto b1 = std::make_unique<ECDI::Button>("新增提供商");
            b1->SetTextColor(kDarkInk);
            b1->SetFont(harness::SansFont(harness::kFontSmall));
            b1->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b1->SetSize(110, 26);
            b1->SetOnClick([this] {
                m_editingProviderId.clear();
                if (m_hud.settingsBase) {
                    m_hud.settingsBase->SetText("");
                    m_hud.settingsModel->SetText("");
                    if (m_hud.settingsContext) m_hud.settingsContext->SetText("");
                    if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("");
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText("填写新提供商信息，保存后加入列表");
                }
            });
            row->AddChild(std::move(b1));
            auto b2 = std::make_unique<ECDI::Button>("删除所选");
            b2->SetTextColor(kDarkInk);
            b2->SetFont(harness::SansFont(harness::kFontSmall));
            b2->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b2->SetSize(110, 26);
            b2->SetOnClick([this] {
                const ProviderConfig* cur = m_appConfig.Find(m_editingProviderId);
                if (!cur) {
                    m_hud.settingsHint->SetText("先在列表中选择要删除的提供商");
                    return;
                }
                // ★★ P4 修复（实测反馈：点删除未响应后崩溃）：
                //   ① 原按 id 过滤=「同名全家一次删光」——id 由 BaseURL host 派生，
                //     同端点多份配置 id 相同（实测用户配置里有 8 份同 id）⇒ 全删后
                //     providers[0] 在**空向量上越界**（UB ⇒ 未响应/崩溃）。改「删
                //     所选的**一个实例**」（第一个匹配位）——同 id 多份时逐次删。
                //   ② 删后为空的守卫用**删除结果**判断（原 size()<=1 判的是删前，
                //     8→0 的场景它拦不住）。
                bool removed = false;
                for (auto it = m_appConfig.providers.begin();
                     it != m_appConfig.providers.end(); ++it) {
                    if (it->id == m_editingProviderId) {
                        m_appConfig.providers.erase(it);
                        removed = true;
                        break;
                    }
                }
                if (!removed || m_appConfig.providers.empty()) {
                    m_hud.settingsHint->SetText("至少保留一个提供商");
                    std::string reloadError;
                    m_appConfig.Load(reloadError);   // 放弃本次内存改动，回配置文件态
                    return;
                }
                if (m_appConfig.defaultProviderId == m_editingProviderId) {
                    m_appConfig.defaultProviderId = m_appConfig.providers.front().id;
                }
                std::string saveError;
                if (!m_appConfig.Save(saveError)) {
                    m_hud.settingsHint->SetText("配置写入失败：" + saveError);
                    return;
                }
                m_editingProviderId = m_appConfig.defaultProviderId;
                RebuildSettingsProviderList();
                m_modelListDirty = true;
                m_hud.settingsHint->SetText("已删除");
            });
            row->AddChild(std::move(b2));
            panel->AddChild(std::move(row));
        }

        // ★ 无 placeholder：TextBox 没有 Placeholder API（ENTER-SEND 起登记的
        //   已知缺口），字段含义由上方 Label 承担——不去发明第二套提示机制。
        addLabel("Base URL（OpenAI 兼容端点根，如 https://api.deepseek.com/v1）");
        addInput(&m_hud.settingsBase, false);
        addLabel("模型名（如 deepseek-flash / glm-5.2 / u2-flash）");
        addInput(&m_hud.settingsModel, false);
        addPresetRow("上下文容量挡位（token）", &m_hud.settingsContext,
                     {"200000", "500000", "1000000"});
        addPresetRow("输出上限挡位（max_tokens）", &m_hud.settingsMaxOut,
                     {"4096", "8192", "16384", "32768", "65536"});
        addLabel("API Key（存入 Windows 凭据管理器；不进配置文件、不经协议）");
        addInput(&m_hud.settingsKey, true);

        {
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(520, 30);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto save = std::make_unique<ECDI::Button>("保存并连接");
            m_hud.settingsSave = save.get();
            save->SetTextColor(kDarkInk);
            save->SetFont(harness::SansFont(14.0f));
            save->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            save->SetSize(140, 30);
            save->SetOnClick([this] { OnSettingsSave(); });
            row->AddChild(std::move(save));

            auto closeBtn = std::make_unique<ECDI::Button>("关闭");
            closeBtn->SetTextColor(kDarkInk);
            closeBtn->SetFont(harness::SansFont(14.0f));
            closeBtn->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            closeBtn->SetSize(140, 30);
            closeBtn->SetOnClick([this] {
                SetSettingsVisible(false);
                bool anyLive = false;
                for (const auto& sp : m_sessions) {
                    if (sp->originKind == "live") { anyLive = true; break; }
                }
                if (!anyLive && m_sess) {
                    AddNoticeRow("[未连接提供商：用 ··· 菜单的「设置」填写连接信息]",
                                 kDarkMuted);
                    Relayout();
                    RequestRepaint();
                }
            });
            row->AddChild(std::move(closeBtn));
            panel->AddChild(std::move(row));
        }

        auto hint = std::make_unique<ECDI::Label>("");
        m_hud.settingsHint = hint.get();
        hint->SetTextColor(kDarkMuted);
        hint->SetFont(harness::SansFont(harness::kFontSmall));
        hint->SetSize(520, 18);
        panel->AddChild(std::move(hint));

        // ★★ P4 F：不进 counting（那会被 VerticalLayout 接管成槽位兄弟）——
        //   暂存，scroll 建好后与遮罩一起转移为视口浮层。
        settingsSheetOwned = std::move(panel);
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
    // ★★ P4 Phase A（2026-10-25）：「↓ 跳到最新」按钮——浮在视口右下角。
    //   ★ 挂法依据 ScrollView.h 的树结构注释：「装饰与交互子节点必须待在偏移层
    //     之外」——滚动条就是这么挂的（直接子节点、固定视口、不随内容滚）。
    //     ScrollView 不接管 Arrange、ApplyLayout 只定位条与内容节点 ⇒ 没有任何
    //     布局会动它；HitTest 逆序遍历子节点 ⇒ 后加的它在最上层、优先命中。
    //   ★ 几何/显隐的唯一刷新口 = RefreshJumpButton（贴底态 = 隐藏）。
    auto jump = std::make_unique<ECDI::Button>("↓ 跳到最新");
    m_hud.jumpLatest = jump.get();
    jump->SetVisible(false);   // 初始不可见；加载/重排后由 RefreshJumpButton 按贴底态刷新
    jump->SetTextColor(kDarkInk);
    jump->SetFont(harness::SansFont(harness::kFontSmall));
    jump->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = harness::kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    jump->SetSize(kJumpBtnW, kJumpBtnH);
    jump->SetOnClick([this] {
        // 点击 = 显式回底：offset 到底即自然恢复 pinned（跟随语义不变，本按钮只补
        // "看得见、点得回"）。RefreshJumpButton 会随即把它藏起来。
        if (!m_scroll) return;
        m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY());
        RefreshJumpButton();
    });
    scroll->AddChild(std::move(jump));   // ScrollView 的最后子节点 = 最上层
    // ★★ P4 F（2026-10-25）：设置 = 模态浮层（半透明遮罩 + 居中 sheet），与
    //   jump 按钮同款浮层口（ScrollView 直接子节点：无布局接管、固定视口、
    //   HitTest 逆序后加者在上）。子序 [.., jump, mask, sheet] ⇒ 设置打开时
    //   sheet 最先命中、mask 次之（jump 被盖住点不到——模态语义），转录在
    //   半透明遮罩后继续渲染（用户验收过原型这条）。
    //   ★★ 修复（实测反馈「变色应该是整个变色」）：mask+sheet 此后由 BuildUi
    //   尾部转移到 **root 末尾**（盖全窗，含顶栏/侧栏/检查器）——此处先挂
    //   scroll 只是建树期的临时归属。
    auto mask = std::make_unique<ECDI::Button>();
    m_hud.settingsMask = mask.get();
    mask->SetVisible(false);
    mask->SetOnClick([] {});   // 点击被 Button 消费但不做事——防误触，关闭走 Esc/Ctrl+,
    mask->SetStyle(ECDI::ButtonStyleOverride{
        // ★ ECDI #71/REQ-11 首个半透明消费者（评审定案）：DrawRect 路径经
        //   BlendAlphaSolid 消费 alpha；双后端（--backend gdi|gl）实证渲染，
        //   任一后端不出半透明 ⇒ 退不透明深色（不阻塞本 Phase，现象登记 #71）。
        .background = ECDI::Color::FromRGBA8(15, 17, 23, 150),
        .cornerRadius = 0.0f,
        .pressedBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150),
        .hoverBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150)});
    scroll->AddChild(std::move(mask));
    if (settingsSheetOwned) {
        m_hud.settingsPanel->SetVisible(false);   // 显隐统一走 ApplySettingsVisibility
        scroll->AddChild(std::move(settingsSheetOwned));
    }
    // ★★ P4 Phase H（2026-10-25）：模型下拉浮窗——顶部下拉（视口坐标系内
    //   右上角），z 序在 mask/sheet 之上、tooltip 之下。搜索框 + 行列表 + 底注。
    {
        auto pop = std::make_unique<ECDI::Panel>();
        m_hud.modelPopup = pop.get();
        pop->SetVisible(false);
        pop->SetStyle(ECDI::PanelStyleOverride{
            .background = kCardBg,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 1.0f,
            .borderColor = kCardBorder});
        auto search = std::make_unique<ECDI::TextBox>();
        m_hud.modelSearch = search.get();
        search->SetSingleLine(true);
        search->SetSize(320, 26);
        search->SetPosition(10, 8);
        search->SetOnTextChanged([this](const std::string&) { RebuildModelPopupList(); });
        pop->AddChild(std::move(search));
        auto list = std::make_unique<ECDI::Panel>();
        m_hud.modelPopupList = list.get();
        list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
        list->SetPosition(10, 40);
        pop->AddChild(std::move(list));
        auto foot = std::make_unique<ECDI::Label>(
            "仅 idle 发出 · 运行中自动排队（本轮输出完生效）");
        m_hud.modelPopupFoot = foot.get();
        foot->SetTextColor(kDarkMuted);
        foot->SetFont(harness::SansFont(harness::kFontMicro));
        foot->SetSize(320, 16);
        foot->SetPosition(10, 250);
        pop->AddChild(std::move(foot));
        pop->SetSize(340, 272);
        scroll->AddChild(std::move(pop));
    }
    // ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层——最后子 = 最上层（盖过
    //   jump/遮罩/sheet/模型浮窗）。隐藏态常驻，TipButton enter 起 400ms 定时器后显示。
    {
        auto tip = std::make_unique<ECDI::Panel>();
        m_hud.tooltip = tip.get();
        tip->SetVisible(false);
        tip->SetStyle(ECDI::PanelStyleOverride{
            .background = kDarkInk,
            .cornerRadius = kRadiusBtn,
            .borderWidth = 0.0f});
        auto txt = std::make_unique<ECDI::Label>();
        m_hud.tooltipText = txt.get();
        txt->SetTextColor(kTooltipInk);
        txt->SetFont(harness::SansFont(harness::kFontSmall));
        txt->SetSize(400, 16);
        txt->SetPosition(8, 5);
        tip->AddChild(std::move(txt));
        scroll->AddChild(std::move(tip));
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
            // ★★ 2026-10-21：清空是 `SetText` 路径 ⇒ 自动长高不会自己知道。
            //   不同步的症状很难看：发出一条 3 行的消息后，框里已经空了，
            //   但它仍占 3 行高、提示行与按钮被推到下面 —— 看起来像"卡住了"。
            m_promptBar->SyncInputHeight();
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

    // ★ P3.5：设置面板的显隐投影（中栏三件都建完了，唯一投影路径此刻可用）。
    ApplySettingsVisibility();
    // ★★ P4 H：模型浮窗显隐投影（重建后按模型位恢复——同 ApplySettingsVisibility
    //   的「投影只存在于一条路径」纪律）+ chip 首帧数据。
    SetModelPopupOpen(m_modelPopupOpen);
    UpdateModelChip();
    // ★ P3.6：模型列表首帧/重建后置脏（tick 里统一重建——列表行是动态的）。
    m_modelListDirty = true;
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
        cardTitle(*card, "模型");
        {
            // ★ P3.6：提供商切换列表（行由 RebuildModelList 重建；
            //   点击 = RequestModelSwitch，running 时排队——I11 语义）。
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.modelList = list.get();
            list->SetSize(190, 110);
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            card->AddChild(std::move(list));
        }
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

// ── ★★ REQ-03（2026-10-24）：把「UI 线程轮询」换成「读线程投递唤醒」────────
//
// 为什么要改：`NextWithin(line, 8)` 在**没有数据**时会把 UI 线程挂在条件变量上
// 整整 8ms，而 tick 周期是 33ms ⇒ 约 24% 的 UI 线程时间花在**等数据**上；
// 同一段阻塞期间 WM_PAINT 出不来 ⇒ 帧延迟最多 +8ms（打字/滚动的手感就丢在这）。
// 送入延迟则是 `0..33ms`(等 tick) + `0..8ms`(等队列)。
//
// ★ 关键的决定：投递的是**信号**，不是数据。行仍留在 `ChildSource` 的队列里，
//   由 UI 线程自己取。理由不是"实现简单"，而是**所有权**：
//   `Session` 是可被删除的（侧栏 × / 归档），把行搬进一个跨线程回调，
//   就得让那个回调知道目标会话是否还活着；而队列跟着 `Session` 一起死，
//   天然没有这个问题。⇒ 闭包只捕获 `this`（HarnessApp），执行时**枚举**会话。


// ── 回放拉取（~30Hz 单定时器驱动，规格 §6.1）───────────────────


// --scroll-sweep 步进：每次前进 step px；扫到底即退出（摘要由 Run() 返回后落盘）

// ★★ 2026-10-23（ECDI REQ-13）：**本函数已删除**。
//
//   它曾经是本仓在没有框架能力时的下限绕行 —— `max(kMin, 请求值)` 之后再由
//   `OnWindowResized` 用 `SetBounds` 把窗口推回去，代价是拖拽过程中的一次回弹。
//   ECDI 现在有 `Window::SetMinimumSize`（`WM_GETMINMAXINFO` + `WM_WINDOWPOSCHANGING`，
//   在应用尺寸**之前**生效），建窗时登记一次即可，回弹不存在。
//
//   ★ 为什么删干净而不是留作"双保险"：两处都夹 ⇒ 任何一侧坏掉都测不出来
//     （`max()` 会把框架的失效掩盖成"我们的逻辑在工作"）。负对照的做法见
//     `RunResizeProbe` ⑥：**把框架的下限清零**，看同一个请求会不会真的落下去。

// ★★ 2026-10-21（用户要求「先把 UI 完善」第 1 项）：窗口可拉伸。
//
// 【改前是什么样】`App.cpp` 用 `Create(..., 1120, 760)` 建窗，`caption` 也是
//   `SetSize(1120, ...)` 定宽，全仓**零处**响应窗口尺寸变化。实测
//   （`.scratch/probe_resize_shots.py`，真 `SetWindowPos` 走真 WM_SIZE）：
//     · 拉到 1400x900 —— 转录行**仍是 1120 时的宽度**，右侧空出一大片白；
//     · 压到 800x560 —— 行**比视口还宽**，出现横向滚动条，文字在窗口右缘被截断。
//   两种症状同一个原因：行的宽度是 `Relayout()` 按 `ContentWidth(m_scroll)` **量**出来的，
//   而没人再调过它。ECDI 自己只同步 `RootWidget` 尺寸，它不知道我们的行是测量出来的。
//
// 【为什么不重建视图】尺寸不属于模型。`RebuildTranscriptView` 会清空行表、重放
//   整个 timeline、重算滚动位置——那是给「模型变了」用的。这里只需要重测宽度。
//   代价差得很远：重建会把每一行（含折叠体）重新测量一遍，而 `Relayout` 只碰宽度
//   真正变了的行（`lastWrapW` / `lastBodyW` 有同值守卫）。
//
// 【顺序为什么是这五步】
//   ① caption 改宽 —— 它内部按 `GetWidth()` 排三个系统钮，不改宽三个钮就停在原地；
//   ② `root.Arrange()` —— `VerticalLayout(fillCrossAxis=true)` 把新宽度下发到三个栏；
//      漏了这一步，下面的 `Relayout` 会拿**旧**的 `m_scroll->GetWidth()` 去测；
//   ③ `Relayout()` —— 按新内容宽重测每一行、重算滚动范围；
//   ④ `FollowTailIfPinned()` —— 用户在底部时保持贴底，否则一拉伸就被甩上去；
//   ⑤ `RequestRepaint()` —— 几何变了必须重画（`Arrange` 本身不 Invalidate）。
void HarnessApp::OnWindowResized(int width, int height) {
    if (!m_window) return;

    // ★★ 2026-10-23（ECDI REQ-13）：这里**不再**夹下限。
    //
    // 【改前】此处在最前面做一次 `ClampToMinimum`，把低于 880×460 的尺寸用
    //   `SetBounds` 推回下限。那是**框架没有能力**时的绕行：原生拖拽时系统不拦，
    //   只能在我们自己的 resize 回调里补救。
    // 【当时的代价（原文保留）】拖边框的过程中窗口会先缩到目标大小、再被弹回下限,
    //   表现为一次回弹。
    // 【现在】下限由框架在 `WM_GETMINMAXINFO`（ptMinTrackSize）+ `WM_WINDOWPOSCHANGING`
    //   两条路径上**在应用尺寸之前**落实 ⇒ 这个回调只会收到**已合规**的尺寸，
    //   回弹在源头上不存在。绕行删掉，不是因为"它现在无害"，而是因为**前提没了**：
    //   保留它会让"下限由谁负责"永远说不清（两处都在夹 ⇒ 谁也证明不了自己有用）。
    // 【怎么证明框架那侧真的在拦】`--resize-probe` 的 min-size 相**反向**取证：
    //   临时把下限清零，同一个 640×360 必须**真的**落成 640×360（对照世界），
    //   再把下限登记回去、重发同一请求 ⇒ 落在 >= 下限。见 `RunResizeProbe` ⑥。
    const int effW = width;
    const int effH = height;

    // ① 标题栏跟着改宽（它创建时是定宽的）
    if (m_caption) {
        const int w = std::max(320, effW);
        if (m_caption->GetWidth() != w) m_caption->SetSize(w, m_caption->GetHeight());
    }

    // ② root 重排。★ 基类已经把 root 的尺寸设好了，这里只是让布局把新宽度用起来。
    //   ★ 钳制路径下 root 可能仍是上一次的尺寸（`SetBounds` 的事件还没来），
    //     故显式对齐一次，保证"排版用的尺寸 == 生效的尺寸"。
    ECDI::Widget& root = m_window->GetRootWidget();
    if (root.GetWidth() != effW || root.GetHeight() != effH) {
        root.SetSize(effW, effH);
    }
    root.Arrange();

    // ③ 重测每一行（内容宽变了 ⇒ 折行位置变了）
    //   ★ 空会话（回放模式下新建的）`rows` 为空，`Relayout` 内部对空表安全；
    //     但仍要先确认 `m_content` 在——`BuildUi` 之前不该走到这里。
    if (!m_content || !m_sess) return;
    if (m_promptBar) {
        m_promptBar->SyncInputHeight();
    }
    Relayout();

    // ④ 贴底保持 + ⑤ 重画
    FollowTailIfPinned();
    RequestRepaint();
}


// ── 行构建（规格 §5.2 渲染映射）────────────────────────────────


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


// 可折叠行的点击绑定（**构建期与重建期共用**）。
//   ★ 0c 的关键变化：展开态**先写回模型**（`timeline[idx].expanded`），再动控件。
//     在此之前展开态只活在 `RowRec` 上 ⇒ 任何重建都会把用户的展开操作静默回退。

// 展开/收起一行（**点击路径与探针共用的唯一入口**）。
//   ★ 为什么必须抽成函数：0c 的重建探针要构造"用户已经展开过"的状态，而它**不能**
//     合成点击——探针需要在**任意**行上造状态，包括滚动到视口外、HitTest 命不中的行；
//     若探针自己再写一遍"改标志 + 翻箭头 + 重排"，那就是**第二份实现**，测的是它自己。


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

// 把一条时间线条目落成一行（构建期与重建期**共用同一个**分派点）。
//   ★ 重建期**绝不**执行副作用：不重发 answer、不重跑自动应答、不追加通知。


// Level 1 重建：拆掉转录区的全部行控件，再按模型重放一遍。
//   ★ 只碰 `m_content` 的子树 ⇒ `CountingPanel`/HUD/侧栏/检查器指针全部存活，
//     评估读数不中断（整树重建才会打死计数器，那是 Level 2，留给 0e）。
//   ★ 拆之前必须先把视图状态同步进模型，否则等于"照着一份更旧的模型重建"。

// 权限门行：**纯构造**（重建可重放）。问句与 id 都来自模型条目，不是来自"刚到的消息"
//   ——重建时那条消息早已不存在，若从消息取就等于"重建不出问句"。


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


// ── ★★ Step 0c：重建探针（--rebuild-probe）──────────────────────────────
//
// 断言：**照模型重建出来的视图 == 增量构建出来的视图**（逐行、逐字段）。
//   为什么必须机读：少一条通知、错一位次序、丢一个展开态，屏幕上看都像"差不多"。
//   故本探针把每行压成一条可比较的字符串，两侧用**同一个**量具比。
//
// 判别力（0a 的教训）：demo 夹具上「到达序」与「字典序」恰好一致 ⇒ 只用 demo
//   验不出顺序错。故本探针**自带负对照**：再按字典序重建一次，必须报出
//   cot 42 / realistic 218 / scale 1998 / demo 0 处次序错。负对照不报错 = 探针瞎了。


// P1.5：发送路径（发送按钮与 --auto-prompt 共用）


// ── ★★ 2026-10-20：工作区注册表（模型侧实现）────────────────────────────
// 三条不变量都在这里维护（设计注见 `App.h` 的 `struct Workspace`）：
//   ① **只追加**：`m_workspaces` 的下标即创建序，永不删除、永不重排 ⇒
//      「创建序固定」这条用户裁决不需要额外排序字段，下标本身就是证据；
//   ② **按路径去重**：同一目录的第二个会话复用同一个实体（否则侧栏会画出
//      两个同路径的标题行，而用户看到的会是"我的会话莫名其妙分成两组"）；
//   ③ **只由 core 确认过的路径创建**：`AcquireWorkspace` 的唯一调用点在 workspace
//      ack 的 `ok:true` 分支 ⇒ 注册表里不存在"GUI 自己猜的路径"。


// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：临时会话（用户裁决 m09443）
// ───────────────────────────────────────────────────────────────────────
// 从工作区标题行的「＋」按钮拉起的会话是**临时**的：它还没有执行过任何
// 任务（没发过 prompt）。一旦用户切走（`SwitchToSession` 到另一个会话），
// 临时会话就被删除——因为它的存在理由（"在这个工作区开一个空会话"）
// 已经被放弃。一旦发了 prompt（`PostPrompt` 成功），它转为正式会话。
// ═══════════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-10-20：「项目」段 = 当前工作区的真实目录内容
// ───────────────────────────────────────────────────────────────────────
// 数据源 = 活动会话的 `workspacePath`（core 确认过的规范化路径）。
// 回放会话没有工作区 ⇒ 此段显示「无工作区」。
// ★ 只列一级（不递归）：侧栏宽度 172px，递归树会立刻溢出；且用户要的是
//   「这个工作区里有什么」，不是文件浏览器。
// ═══════════════════════════════════════════════════════════════════════


// ── ★★ 2026-10-20：中栏头 `↗`（导出）/ `···`（菜单）─────────────

// ★★ 2026-10-21：导出拆成**三段纯函数**，因为原来它是一整个「弹对话框 + 写文件」。
//   不拆的话，导出这条路径**无法被探针覆盖**：`GetSaveFileNameW` 是模态死循环，
//   合成点击一旦点上去，探针就卡在对话框里，整个 `--menu-probe` 会挂死。
//   拆开之后各自有判据：
//     · `BuildTranscriptExportText()` —— 纯函数，返回**将要写出的文本**。
//       判据：行数/内容与模型对得上、通知行真的在里面（负对照：块表里没有它们）。
//     · `WriteTranscriptExport()` —— 只负责落盘，返回 false 时给错误串。
//     · `ExportTranscript()` —— 只剩「选路径 + 调用上面两个 + 说一声」。
//   ★ 仍然**没被覆盖**的是那个对话框本身（`GetSaveFileNameW` 的调用点）。
//     探针走 `m_menuProbeExportPath` 直接给它一个路径，绕开对话框。
//     这是**诚实的缺口**，写在这里而不是假装探针覆盖了保存对话框。

// 把活动会话的转录渲染成文本。**只读**：不碰模型、不碰协议、不改 seq。
// ★ 导出源是 `timeline`（视图看到的那一份），**不是** `Transcript::m_blocks`。
//   两者不等价：`m_blocks` 只有协议块，而 `timeline` 还含通知行（`[error]` /
//   `[协议不兼容]` / I4 缺口告警）与问句行。导出后者才叫「把屏幕存下来」；
//   导出前者会把「这次运行可不可信」的全部读数默默丢掉。


// ── ★★ P3.5：设置面板 / 无参启动 / 凭据注入（规格 §8.5 D4–D7）────────


// ★★ P4 F：遮罩 + 居中 sheet 的几何。★ 修复（实测反馈）：**遮罩盖全窗**
//   （mask+sheet 挂 root 末尾，画过 caption/counting 全部；此前挂 m_scroll
//   只盖转录视口）——sheet 尺寸改按 root 算。浮层无父布局 ⇒ SetSize 后必须
//   自己 Arrange（ArrangeInternal 递归驱动整棵子树布局——字段区由此摆位），
//   **再 UpdateContentExtent**（字段包围盒 → content extent；漏了它 extent=0，
//   字段区不显示——首版实测反馈的根因）。
//   ★ root.Arrange()（HarnessRootLayout）**跳过浮层子**——浮层几何完全归
//   本函数管，布局与浮层互不干扰。


// ── ★★ P3.6（协议 v4）：模型热切换 ───────────────────────────────


// ── ★★ P4 Phase H（2026-10-25）：顶栏 chip + 模型下拉浮窗 ──────────────


// ★★ P4 Phase D：本地时刻 "HH:MM"（answerAt 用——审计信息，分钟粒度够用）。


// ★★ Step 3：已答权限门的视觉。**首答与重建共用**（这是本函数存在的唯一理由）。
//
// ★★ P4 Phase D（2026-10-25）改版：两钮**隐藏** + 行内结论标记
//   （`● 已允许 HH:MM` 绿 / `● 已拒绝 HH:MM` 红）。
//   · 原「选中者留色、另一个灰化」只把结论编码在配色里（审计事实读不出来），
//     且 ECDI Button 不画 disabled 态——两个死按钮留在行里占位却不可点。
//   · 结论标记是文本 ⇒ 探针可从 `conclusion->GetText()` **独立于模型**读回
//     （原手法读两钮文字色，钮一隐藏即失效——本版判据反而更直接）。
//   · `when` 为空 = 旧 sidecar 重建（无时刻记录）——如实省略时刻段，不编造。
//   · 未答/重建未答：conclusion 隐藏、两钮可见可点（幂等）。

// 通知行：**纯构造**（重建可重放）。

// ── ★★ P4 Phase C（2026-10-25）：空状态引导 ──────────────────────────
// 修的死胡同：首启（firstRun）关掉设置面板后，转录区一行都没有、屏幕全空。
// 空转录 = 「引导卡」的展示条件：一句话 + 出口（live 会话给建议指令、无 core
// 给「打开设置」）。任何行（含 notice）/pending 气泡到达即拆——回放/Ctrl+N 的
// 空会话带 notice，天然不触发（方案 §5 的「回放/harness 场景不弹」由此成立）。


// ── 布局与重绘（规格 §5.3 / §6.2）──────────────────────────────


// ★★ P4 Phase A（2026-10-25）：刷新「跳到最新」按钮的几何与显隐（唯一刷新口）。
//   ★ 调用时点：滚轮/导航键后（HarnessApplication override）、填行后
//     （FollowTailIfPinned 尾部）、重排后（Relayout 尾部）、tick 脏检查
//     （DirtyRefreshJumpButton——拖滚动条滑块没有事件回调，靠定时器节拍兜底）。
//   ★ 全部是只读查询 + 几何 setter + 同值守卫的 SetVisible（M9：同值不
//     Invalidate）⇒ 高频调用零重绘代价，不打扰 CountingPanel 读数。

// ★★ P4 Phase A：tick 脏检查——offset 与缓存不等才真正刷新（禁止无条件刷新/
//   Invalidate，CountingPanel 读数纪律）。

// ── ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层 ─────────────────────
// ECDI 无 Tooltip 原语（源码仓核实：TrayIcon szTip 是唯一同名物）——harness 拼：
// TipButton（UiKit）回调 enter/leave，这里管单例浮层 + 400ms 延迟定时器。
// ★ 定位纪律：锚点坐标一律 GetAbsolutePosition（不手算父链，App.cpp:4274 同款）；
//   顶栏/中栏头按钮在视口上方 ⇒ y 钳到 0（浮层挂 m_scroll，负 y 会被视口裁剪）。


// ★★ 2026-10-21：HUD 覆盖自检（理由见 `App.h` 里 `HudTouch` 的声明注释）。
//
//   这一类缺陷的形状：**读了但从不写**。`RefreshHud` 里写着
//   `if (m_hud.X) { m_hud.X->SetText(...) }`，字面上像"有这个控件就更新它"，
//   而如果建树时从来没赋值，那个 `if` 就永远是假分支——代码看着对，
//   屏幕上是空的，**所有既有读数都看不见它**。
//   已发现三个实例：`backendChip`（屏幕上从来没有）、
//   `sessSession` / `sessCore`（Step 1 之后不再建）。前两个已删、
//   `backendChip` 已补上真实实现——但"修三个实例"不是修这一类，所以这里把它
//   变成一条会自己变红的判据。
//
//   ★ 开销：默认 `m_hudAudit == false` ⇒ `HudTouch` 只做一次 bool 判断就返回，
//     不分配、不记录。只有探针打开它之后才登记。


// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。

// ── 退出摘要（规格 §6.3）───────────────────────────────────────

