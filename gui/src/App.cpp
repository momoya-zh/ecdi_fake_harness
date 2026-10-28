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
// ★ 2026-10-25（Stage 1d/1e）：随代码搬走的 include 已清掉——
//   `UTF8.h`（ElideMiddleByCodepoints → AppSession）、`BackendFactory.h`（建后端在 main.cpp）、
//   `MouseButton*.h`（合成鼠标事件 → ProbeSupport）、`ScrollBar.h`（grow-probe 读滚动条 → ProbeFlow）、
//   四个 `Layout/*.h`（HarnessRootLayout/BuildUi/BuildMainArea → AppUiChrome / AppUiMain）、
//   `ThemedCaptionBar.h`（建标题栏 → AppUiChrome）、`<set>`（root 布局的豁免集 → AppUiChrome）。
//   ★ 判据是机读的：`grep -c "\b符号\b" App.cpp` 只剩 include 行本身。
//   ★ 本文件现在只剩「应用接线」：Run / 构造 / 事件转发。若某天又想在这里建控件，
//     先想想该不该去 AppUiChrome / AppUiMain。
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"   // OnMouseWheel 的形参类型
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"   // OnKeyDown / HandleGlobalKeyDown 的形参类型
#include "ECDI/EventSystem/Window/TimerEvent.h"

#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/CaptionBar.h"

#include "Theme.h"
#include "ThemeUsing.h"    // ★ 2026-10-25：主题令牌短名收口（原文件级 52 条 using）
#include "AppInternal.h"   // ★ 2026-10-25：跨 TU 共享的路径转换与几何常量
#include "UiKit.h"

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
using harness::internal::kProbeCanvas;

// ★ 2026-10-25（Stage 1e）：kTooltipInk 随 BuildMainArea 搬到 AppUiMain.cpp。

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
// ★ 2026-10-25（Stage 1d/1e）：kProbeCanvas / kProbeBg / kProbeInk / Pick
//   全部上提 `AppInternal.h`（探针、Run、AppUiChrome 共用同一组值）。


// ★ 2026-10-25（Stage 1d）：ContentWidth / ConversationTitle / ConversationSubtitle /
//   UnknownKindText / ReasoningTitle 搬到 `UiCommon.cpp`（`harness::internal`）。
//   它们现在被 AppTranscript.cpp 与 AppHud.cpp 同时使用，留在匿名命名空间里
//   等于把「单一真相源」拆成三份。短名见文件顶部的 using 区。



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
    // ★★ 2026-10-26（按模式分流）：无参启动且未挂任何探针/未传 `--dev` ⇒ 进入用户模式。
    //   · 开发者模式（`m_devMode == true`）：`--replay` / `--core` / 任一 `--*-probe` / `--dev`，
    //     保持既有树、布局与读数口径 100% 不变；
    //   · 用户模式（`m_devMode == false`）：默认收起右侧检视栏（`m_inspectorVisible = false`），
    //     隐藏评估胶囊、底栏、渲染探针卡与测试夹具历史区。
    m_devMode = m_options.devMode || !m_options.firstRun || m_options.HasAnyProbe();
    if (!m_devMode) {
        m_inspectorVisible = false;
    }
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
    // ★★ 2026-10-27：侧栏一藏，中栏变宽 200 ⇒ titles 可以吃更多（上限 480）。
    SyncHeadTitlesWidth();
    RequestRepaint();
}

// ★★ 2026-10-27：检视栏可见性的**唯一落地口**（Ctrl+I 与中栏头「详情」按钮、
//   `SetDevMode` 的模式切换共用）。
//   ★ 为什么还要收成一个函数（只有一个控件要切了）：`m_inspectorVisible` 有三条
//     写路径，而「模型态翻了吗」与「控件跟着翻了吗」必须永远同进同退——分散写
//     就会出现「模型说收起了、屏幕还在」这类只在视觉上暴露的缺陷（`--sidebar-probe`
//     的 `model still says visible` 判据专门盯这一条）。
//   ★ 注意本函数**不负责宽度**：中栏控件会随检视栏收起而变宽（视口该变），
//     排版宽另由 `TranscriptLayoutWidth()` 单独算。两件事分开管，是因为
//     「视口满宽」和「版心按 max-width 封顶」在视觉上必须同时成立。
//   ★★ 2026-10-27 更正：上一版这里写的是"版心不变（永远扣掉 230）"——那条模型
//     已经被 max-width 取代。现在的真实语义：**视口变宽 ⇒ 版心跟着变宽，直到
//     `kMaxTranscriptW` 封顶**；只有窗口宽到"扣掉 230 之后仍 ≥ 封顶"才免疫。别
//     再按"零重测"理解它（见 `TranscriptLayoutWidth` 的性能注记）。
void HarnessApp::ApplyInspectorVisibility() {
    if (m_hud.inspector) m_hud.inspector->SetVisible(m_inspectorVisible);
}

// ★★ P4 Phase B（2026-10-25）：检视栏显隐的唯一入口（中栏头 `≡` 按钮与 Ctrl+I
//   共用）。抽出前逻辑内联在按钮回调里——全局快捷键没有可复用的路径，两份
//   实现迟早漂移（SetSidebarVisible 的"唯一入口"纪律）。
void HarnessApp::ToggleInspector() {
    if (!m_hud.inspector) return;
    m_inspectorVisible = !m_inspectorVisible;
    // ★★ 2026-10-27：中栏视口随检视栏显隐变宽/窄 230（这是**该**变的：滚动条、
    //   中栏边界必须贴到窗口右边缘，见实测反馈「收起后边框停在原位」）。
    //   ★ 版心（行宽）随之走 max-width 模型：**未封顶时真的会变**（wrapW 变 ⇒
    //     换行缓存失效 ⇒ 一次重测），封顶后一位不变。上一版注释说"扣回那 230
    //     ⇒ 零重测"，那是"永远扣 230"旧模型的结论，已作废——别拿它当性能承诺。
    //   ★ 顺序仍是先 `Arrange` 再 `Relayout`：宽度由布局分配，必须在父容器重排之后
    //     才能读到新的 `m_scroll` 宽（版心正是读它算出来的）。
    ApplyInspectorVisibility();
    if (ECDI::Widget* parent = m_hud.inspector->GetParent()) parent->Arrange();
    Relayout();
    // ★★ 2026-10-27 修复（实测反馈「点详情后那三个键没了」）：
    //   检视栏出现 ⇒ 中栏**窄 230**，而会话头的 `titles` 原来是固定 480、三钮各 44
    //   ⇒ 总需 ≈ 700 > 中栏 570 ⇒ 后两钮被排到父边界外（布局不压缩、Panel 不裁子），
    //   屏幕上就是"消失"。修法两半，缺一不可：
    //     ① `SyncHeadTitlesWidth()` 让 titles 吃剩余宽（上限仍 480，宽窗口零变化）；
    //     ② 本函数要**真的调用**它——原实现在这里既不置 `m_hudDirty` 也不刷 HUD，
    //        与对称的 `SetSidebarVisible`（置脏 + RefreshHud + 重算）不一致，
    //        正是这处不对称让宽度一直停在旧值。
    //   ★ 顺序：`parent->Arrange()` 之后才有新的中栏宽可读。
    SyncHeadTitlesWidth();
    RequestRepaint();
}

// ★★ P4 Phase B（2026-10-25）：全局快捷键分派（HarnessApplication::OnKeyDown
//   在转基类**之前**调用；命中返回 true ⇒ 事件到此为止，不派给焦点控件）。
//   ★ 刻意不拦 Ctrl+A/C/X/V/Z（TextBox 编辑语义）与 Enter（PromptBar 发送）。
bool HarnessApp::HandleGlobalKeyDown(const ECDI::KeyDownEvent& event) {
    const ECDI::KeyCode key = event.GetKeyCode();
    if (key == ECDI::KeyCode::Escape) {
        // 逐层关：预览（P4 I）> 设置 > 模型浮窗（P4 H）> ···菜单。
        // 无层可关 ⇒ 返回 false 让事件照常派发（Esc 不悬空拦截）。
        if (m_previewOpen) {
            SetPreviewVisible(false);
            return true;
        }
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
    if (event.IsShiftDown() && key == ECDI::KeyCode::D) {
        SetDevMode(!m_devMode);
        return true;
    }
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

    // ★★ 2026-10-28：启动发现并恢复已知工作区下的持久化会话（L1，仅在无 CLI 指定会话时生效）
    //   ★★ 隐患修复（同日）：判据**不能**用 `m_options.corePath` 空不空——无参启动走到这里时，
    //     `TryAutoConnectFromConfig()` 早把自动解析的 core 路径填进去了（AppSettings.cpp:245），
    //     于是该判据恒真 ⇒ 整段恢复被跳过，而且**一声不响**：磁盘上 8 个会话
    //     （`.ecdi/sessions/*.meta.json` + `*.jsonl` 齐全）一条都不进侧栏。
    //     修法 = 读解析期钉死的 `sourceFromCli`（main.cpp），运行期不再从路径反推来源。
    //     ★ 判据本体收在 `ShouldAutoRestoreSessions()`（CliOptions.h）——纯函数才能被
    //       `--persist-probe` 的 startup-gate 相对照；写在 Run() 深处的局部布尔
    //       任何探针都摸不到，这正是本轮缺陷藏了一整轮的原因。
    const bool cliSessionRequested = !ShouldAutoRestoreSessions(m_options);
    Session* initialSess = m_sess;
    if (!cliSessionRequested) {
        if (initialSess) {
            // 防止在恢复过程连续切会话时被 MaybeDeleteTemporarySession 误删
            initialSess->temporary = false;
        }
        DiscoverAndRestoreJournalSessions();
        if (!m_sessions.empty()) {
            if (initialSess && initialSess->timeline.empty() && m_sessions.size() > 1) {
                auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                                       [initialSess](const std::unique_ptr<Session>& p) { return p.get() == initialSess; });
                if (it != m_sessions.end()) {
                    m_sessions.erase(it);
                }
            } else if (initialSess && m_sessions.size() == 1) {
                initialSess->temporary = true;
            }
            SwitchToSession(m_sessions.size() - 1);
        }
    } else {
        // ★★ 2026-10-28：跳过分流必须**说出来**——"命令行指定了来源"是正当理由，但用户
        //   看不见"磁盘上还有 N 个会话没被恢复"，这种沉默正是本轮缺陷的现场。
        //   ★★ 为什么用 Logger 而**不是** `AddNoticeRow`（试过，撤回）：
        //     走 else 分支的**只有**带 `--replay`/`--core` 的场景，而这恰好是全部探针与
        //     回归基线的入口（`tools/regress.py` 的基线跑不带任何 probe flag）。
        //     往转录里插一行 notice ⇒ `rows`/`blocks` 全 +1 ⇒ 25 个探针的既有读数被改写；
        //     而"用户模式"（`!firstRun && !HasAnyProbe()`）根本到不了这里（那时
        //     `sourceFromCli` 恒 false）⇒ 对真实用户它反而永远不可见。
        //     两头不讨好 ⇒ 改用日志：机器可读、零 UI 扰动、零摘要字段变化。
        if (!m_options.HasAnyProbe()) {
            size_t skippedSessions = 0;
            for (const auto& wsPath : m_appConfig.knownWorkspaces) {
                if (wsPath.empty()) continue;
                std::string scanErr;
                skippedSessions += SessionStore::ScanSessionMetas(wsPath, scanErr).size();
            }
            if (skippedSessions > 0) {
                ECDI::Logger::Log(ECDI::LogLevel::Warning,
                                  ECDI::UTF8ToWide("[startup] skipped auto-restore of " +
                                                   std::to_string(skippedSessions) +
                                                   " persisted session(s): CLI source was given"));
            }
        }
    }

    // P1.5 live：收到 core 的 hello 之后回 hello（协议 §4：seq 0）
    if (m_sess && m_sess->writer) {
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
        if (session->journalPromoted) {
            SyncSessionMeta(*session);
            if (session->journal) session->journal->Close();
        }
        if (session->writer) session->writer->Bye();
        if (session->child) session->child->Stop();
    }

    WriteSummary();
    m_application = nullptr;
    return code;
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
    // ★★ 2026-10-27：窗口变窄 ⇒ 中栏变窄 ⇒ 会话头三钮可能被挤出可视区
    //   （实测 1000 宽 + 详情开只剩「导出」半截）。同值守卫 ⇒ 宽度没变时零成本，
    //   故放在高频的 resize 链上安全（不动 HUD 读数、不额外 Invalidate）。
    SyncHeadTitlesWidth();
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
//     探针走 `m_probeState.menuProbeExportPath` 直接给它一个路径，绕开对话框。
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
//   ★ 开销：默认 `m_probeState.hudAudit == false` ⇒ `HudTouch` 只做一次 bool 判断就返回，
//     不分配、不记录。只有探针打开它之后才登记。


// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。

// ── 退出摘要（规格 §6.3）───────────────────────────────────────

