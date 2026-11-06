// ============================================================
// 中栏与右栏的内容构建：BuildMainArea（三栏装配 + 全部浮层）
// ------------------------------------------------------------
// 为什么单独一个文件：980 行、8 个互不相关的界面区（侧栏列表 / 会话头 / 更多菜单 /
//   设置浮层 / 模型弹层+tooltip+跳转钮+模态遮罩 / 右栏检查器）。它必须和
//   AppUiChrome（窗口由哪几块组成）分开，否则"加一个按钮"要滚过整窗结构。
// ★ 纪律：本文件**只建树、不摆位**。所有 SetSize/SetPosition 都必须是
//   "建树时就该定死的尺寸"（行高、栏宽）；依赖窗口尺寸的几何归 AppHud 的 Relayout。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1e）。函数体逐字节相同。
//   ★ Stage 2 会把本文件按现有 `─` 分节注释切成 6 个 builder——届时唯一要求是
//     「树结构与 m_hud.xxx = ptr.get() 的写入顺序」不变。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "SplitterBar.h"
#include "ThemeUsing.h"
#include "UiKit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/ListLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/RingProgressBar.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/TextBox.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 跨 TU 共享符号（定义见 AppInternal.h / UiCommon.cpp）
using harness::internal::AddLine;
using harness::internal::ApplyCompactScrollBars;
using harness::internal::ContentWidth;
using harness::internal::ConversationSubtitle;
using harness::internal::ConversationTitle;
using harness::internal::kCanvasPadX;
using harness::internal::kHeaderH;
using harness::internal::kInspW;
using harness::internal::kJumpBtnH;
using harness::internal::kJumpBtnW;
using harness::internal::kListRowHeight;
using harness::internal::kModelPopupFootH;
using harness::internal::kModelPopupFootY;
using harness::internal::kModelPopupH;
using harness::internal::kModelPopupListMaxH;
using harness::internal::kModelPopupListY;
using harness::internal::kModelPopupPadX;
using harness::internal::kModelPopupSearchH;
using harness::internal::kModelPopupSearchY;
using harness::internal::kModelPopupTitleH;
using harness::internal::kModelPopupTitleY;
using harness::internal::kModelPopupW;
using harness::internal::kProbeBg;
using harness::internal::kProbeInk;
using harness::internal::kRowGap;
using harness::internal::kRowPadX;
using harness::internal::kSidebarListW;
using harness::internal::kSidebarRowGap;
using harness::internal::kSidebarRowH;
using harness::internal::Pick;

// ★★ P4 E：tooltip 配色。★ 2026-10-30 上提到 `AppInternal.h`——tooltip 迁到浮层根后
//   由 `AppHud.cpp` 现建现挂，配色要跨 TU 共用（这里只留短名）。
using harness::internal::kTooltipInk;

namespace {

// ── 侧栏「段距」占位块（2026-11-03，用户反馈「左侧栏过于紧凑」）────────────
// 为什么需要它：`ECDI::VerticalLayout` 只有**一个** spacing（本侧栏 = `kRowGapSide`），
//   没有 per-child margin ⇒「段与段之间比条目之间更松」在这套契约下只有一条路：
//   往子表里插一个固定高度的透明块，让两侧各自的 spacing 叠上去。
//   底部那个 `spring` 就是同一手法的先例（也是本文件唯一的位置）。
// 有效段距 = kRowGapSide(7) + kSidebarSectionSpacer(10) + kRowGapSide(7) = **24**；
//   条目之间 7 ⇒ 段界/条目 = 3.4:1，眼睛这才分得出「哪几行是一伙的」。
//   ★ 只用两处（「工作区」前、「历史」前）：会话列表按工作区分组，
//     语义上属于「工作区」段内，不该再切一刀（见 BuildSidebar 里那段注释）。
//   ★ 为什么不直接用已存在的 `harness::kGapSection`(18)：它是**净**段距的设计意图，
//     而这里的 18 会被两侧 spacing 再各加 8 ⇒ 实际 34，比 `kGapSection` 的语境
//     （三栏之间）宽出一倍。段距占用的是侧栏自己的竖向预算，取值就该由侧栏定。
// ★ 必须固定高 + stretch 0：`fillCrossAxis` 只拉伸**宽**，高度由本块自己说。
//   透明底 + 无边框——它只占位，不许出一块异色（同 `spring` 那条约法）。
constexpr int kSidebarSectionSpacer = 10;

std::unique_ptr<ECDI::Panel> SidebarSectionSpacer() {
    auto spacer = std::make_unique<ECDI::Panel>();
    spacer->SetSize(1, kSidebarSectionSpacer);   // 宽由 fillCrossAxis 拉伸，1 只是首帧值
    spacer->SetStyle(ECDI::PanelStyleOverride{
        .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});
    return spacer;
}

}  // namespace

// 主区（hi-fi .main）：左栏（新建会话/会话/回放/工作区）+ 中栏（转录 + Composer）+ 右栏检查器

// ★★ Stage 2（2026-10-25）：本函数只做**装配顺序**，各分区自成一个 builder。
//   为什么按这个顺序：树结构与 `m_hud.xxx = ptr.get()` 的写入顺序都是契约
//   （句柄必须在 AddChild 之前写——AddChild 会转移所有权）。改顺序 = 改行为。
//   ★ 设置面板必须**先建后挂**：它要当视口浮层，而视口（m_scroll）在后面才建，
//     所以 BuildSettingsSheet 返回所有权，由 BuildTranscriptViewport 挂上去。
void HarnessApp::BuildMainArea(ECDI::Widget& root) {
    auto main = std::make_unique<ECDI::Panel>();
    main->SetStretch(1);
    main->SetLayout(std::make_unique<ECDI::HorizontalLayout>(0, true, 0));

    BuildSidebar(*main);

    // ★ 2026-11-01：用户模式默认 220 / 240（原 240 / 260）。这两个数只做
    //   **分割条双击复位**的目标值；首帧实际宽度来自 `AppConfig` 的记忆值。
    const int defSideW = m_options.HasAnyProbe() ? 200 : 220;
    const int defInspW = m_options.HasAnyProbe() ? kInspW : 240;

    // ★ 左分割条
    auto leftSplitter = std::make_unique<SplitterBar>(
        SplitterBar::Side::Left, m_hud.sidebar,
        /*minW=*/160, /*maxW=*/480, /*defaultW=*/defSideW);
    m_hud.leftSplitter = leftSplitter.get();
    leftSplitter->SetVisible(true);
    leftSplitter->SetOnResize([this, mainPtr = main.get()](int newW) {
        m_sidebarWidth = newW;
        if (mainPtr) mainPtr->Arrange();
        // ★★ 2026-11-03（P0，用户反馈「左侧栏过于紧凑」的根因之一）：
        //   列表里的行是**绝对定位**的子控件，`fillCrossAxis` 只拉 ScrollView 自身，
        //   不碰行 ⇒ 宽度一变就必须重建那一层的行，否则行宽永远停在首次建行时的值。
        //   原先只有 `OnDragEnd` 置脏 ⇒ 拖的过程中行不跟手、松手才"跳"一下。
        //   这里只置标志、不重建：本回调在事件派发栈上，`RemoveChild` 拆树不安全
        //   （同 `m_sidebarDirty` 的 UAF 理由）；真正的重建在 `OnReplayTick` 顶部。
        m_sidebarDirty = true;
        m_historyDirty = true;
        SyncHeadTitlesWidth();
        RequestRepaint();
    });
    leftSplitter->SetOnDragEnd([this](int finalW) {
        m_sidebarWidth = finalW;
        m_sidebarDirty = true;
        m_historyDirty = true;
        Relayout();
        SyncHeadTitlesWidth();
        RequestRepaint();
        if (!m_options.HasAnyProbe()) {
            m_appConfig.sidebarWidth = finalW;
            std::string err;
            m_appConfig.Save(err);
        }
    });
    main->AddChild(std::move(leftSplitter));

    // ── 中栏（转录 + Composer）──
    auto counting = std::make_unique<CountingPanel>();
    m_counting = counting.get();
    m_counting->SetStretch(1);
    m_counting->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true, 0));
    m_counting->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? kProbeBg : kContentBg});

    BuildConversationHead();
    BuildMoreMenu();

    auto settingsSheet = BuildSettingsSheet();
    auto previewSheet = BuildPreviewSheet();
    BuildTranscriptViewport(std::move(settingsSheet), std::move(previewSheet));
    BuildComposer();

    // ★ P3.5：设置面板的显隐投影（中栏三件都建完了，唯一投影路径此刻可用）。
    ApplySettingsVisibility();
    // ★ P4 Phase I：预览 sheet 显隐投影（同上——重建后按模型位恢复）。
    ApplyPreviewVisibility();
    // ★★ P4 H：模型浮窗显隐投影（重建后按模型位恢复——同 ApplySettingsVisibility
    //   的「投影只存在于一条路径」纪律）+ chip 首帧数据。
    SetModelPopupOpen(m_modelPopupOpen);
    UpdateModelChip();
    // ★ P3.6：模型列表首帧/重建后置脏（tick 里统一重建——列表行是动态的）。
    m_modelListDirty = true;
    main->AddChild(std::move(counting));

    // ★ 右分割条
    auto rightSplitter = std::make_unique<SplitterBar>(
        SplitterBar::Side::Right, nullptr,
        /*minW=*/180, /*maxW=*/500, /*defaultW=*/defInspW);
    m_hud.rightSplitter = rightSplitter.get();
    rightSplitter->SetVisible(true);
    rightSplitter->SetOnResize([this, mainPtr = main.get()](int newW) {
        m_inspectorWidth = newW;
        if (mainPtr) mainPtr->Arrange();
        SyncHeadTitlesWidth();
        RequestRepaint();
    });
    rightSplitter->SetOnDragEnd([this](int finalW) {
        m_inspectorWidth = finalW;
        Relayout();
        SyncHeadTitlesWidth();
        RequestRepaint();
        if (!m_options.HasAnyProbe()) {
            m_appConfig.inspectorWidth = finalW;
            std::string err;
            m_appConfig.Save(err);
        }
    });
    main->AddChild(std::move(rightSplitter));

    BuildInspector(*main);
    m_hud.rightSplitter->SetTarget(m_hud.inspector);

    root.AddChild(std::move(main));
}


// ── 左栏：工作区 / 会话 / 回放 / 历史 / 身份行（hi-fi .side）──
void HarnessApp::BuildSidebar(ECDI::Panel& main) {
    // ── 左栏（hi-fi .side）──
    auto side = std::make_unique<ECDI::Panel>();
    m_hud.sidebar = side.get();   // Step 2：显隐入口要能摸到侧栏本体
    side->SetSize(m_sidebarWidth, 100);
    side->SetStyle(ECDI::PanelStyleOverride{
        .background = kSidebarBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    side->SetLayout(std::make_unique<ECDI::VerticalLayout>(harness::kRowGapSide, true,
                                                           harness::kSidebarPad));

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
    // ★★ 2026-11-03：段前插占位块（见 `SidebarSectionSpacer` 的注释）——原来的
    //   问题是「新建会话」、段标题、条目三者间距完全相同（6px），眼睛读不出分段。
    side->AddChild(SidebarSectionSpacer());
    AddLine(*side, "工作区", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto path = std::make_unique<ECDI::Label>("等待 core 确认实际路径");
        m_hud.workspacePath = path.get();
        path->SetTextColor(kDarkInk);
        path->SetFont(harness::MonoFont(12.0f));
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
    }

    // ★★ Step 1（2026-10-19）：会话列表替换掉此前**单行硬编码**的会话条目。
    //   改动前的形态是一行 `uikit::Row` + 两个 Label（id / core 版本），只能显示
    //   活动会话——`m_sessions` 里有 N 个会话时，界面上永远只看得见 1 个。
    //   ⇒ 现在是 `m_sessions → 控件树` 的**纯函数渲染**（`RebuildSessionList`），
    //     点一行切会话、行尾 `×` 删会话、选中态由组件自身可见性表达。
    //   ★ 为什么用 `ScrollView` 而不是直接往 `side` 里塞行：会话数无上限，
    //     而侧栏是定宽定高的列。列表自身滚动比"整个侧栏滚动"正确得多
    //     （后者的「工作区/项目」分区会跟着一起滚走）。
    // ★ 2026-11-03：这里**故意不加段距占位块**。会话是按工作区分组的
    //   （列表内的标题就是工作区路径），所以它在语义上属于「工作区」段内，
    //   与上面的目录列表不是两段。三处段距全加会多吃 18px 竖向预算
    //   （见 `Theme.h` 的 `kSidebarPad` 那条预算注释），而收益只是"看起来更像两块"。
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.sessList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        ApplyCompactScrollBars(*list);   // ★ 条宽全仓统一（kScrollBarThickness）
        side->AddChild(std::move(list));
    }
    // ★★ 2026-11-03（P2）：「会话计数」与「回放进度计数」合并成**一行元信息**。
    //   改前是三个兄弟控件（计数 17 / 进度行 19 / 细轨 3 + 三处 7px 间距 = 53px），
    //   只表达"一个列表里有几个 + 回放到第几条"这一条信息，竖向却占掉近两行。
    //   合并后 = 一行 19 + 细轨 3 + 一处间距 = 29px，省 **24px**（正好补历史区多出的
    //   那一行，见 `AppHistory.cpp` 的 `kHistoryMaxRows`）。
    //   ★ 两个 Label 仍然是**两个句柄**：它们的文本由两条独立写者维护
    //     （`RefreshHud` 写会话数、回放 tick 写 n/N），合成一个字符串会让两个写者互相覆盖。
    //   ★ 宽度：会话数是定宽（数字最多三位），n/N 是**变长**文本
    //     （`1213 / 1213 条` / `12345 条（live）` / `空会话 · 无 core`）；
    //     所以只给计数一个定宽，n/N 用 `SetStretch(1)` 吃掉余量——行被侧栏
    //     `fillCrossAxis` 拉到栏宽，余量随栏宽变，n/N 自然跟着变，不需要第二个常数。
    {
        auto row = uikit::Row(172, 19, 8);
        m_hud.sessProgressRow = row.get();
        auto cnt = uikit::Label("1 个会话", 68, 19, kDarkMuted, harness::kFontMicro);
        m_hud.sessCount = cnt.get();
        row->AddChild(std::move(cnt));
        auto prog = uikit::Label("0 / 0", 1, 19, kDarkMuted, harness::kFontSmall);
        prog->SetStretch(1);
        m_hud.sessProgress = prog.get();
        row->AddChild(std::move(prog));
        side->AddChild(std::move(row));
    }

    // ★★ 回放进度条（I4 评估钩子）**保留**，但改成 C 那种「细线段」形态：
    //   原型 C 没有这一块，而它是协议 v2 的评估读数 ⇒ 折中方案是**不单独占一个
    //   分区**，改成会话条目下方的一条 3px 细轨 + 右侧计数，视觉上从「一个区块」
    //   退化为「一行元信息」，既保住读数又不破坏 C 的节奏。
    {
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
    side->AddChild(SidebarSectionSpacer());   // ★ 2026-11-03：段前占位（同「工作区」）
    m_hud.historyHeader = AddLine(*side, "历史", kDarkMuted, true, 172, harness::kFontMicro);
    {
        auto list = std::make_unique<ECDI::ScrollView>();
        m_hud.historyList = list.get();
        list->SetSize(kSidebarListW, kSidebarRowH);
        list->SetScrollStep(kSidebarRowH + kSidebarRowGap);
        list->SetScrollBarVisible(true);
        ApplyCompactScrollBars(*list);   // ★ 条宽全仓统一（kScrollBarThickness）
        side->AddChild(std::move(list));
    }
    {
        auto cnt = uikit::Label("0 个历史文件", kSidebarListW, 14, kDarkMuted,
                                harness::kFontMicro);
        m_hud.historyCount = cnt.get();
        side->AddChild(std::move(cnt));
    }

    // ── 侧栏底部身份行（C 的 `.side-bottom`）──
    //   ★★ 2026-11-01：下面这条注释原来的理由是「`margin-top:auto` 在 ECDI 里
    //     没有等价物（布局无弹性余量分配）」——**那条已作废**：`VerticalLayout`
    //     的主轴 stretch 权重就是弹性余量分配（`VerticalLayout.cpp` D2：末位可见
    //     stretch 吃余数，Σ == remaining）。用一个 0 高 Panel 当弹簧即可，
    //     于是身份行**真的**贴在侧栏底部，多出来的高度变成「历史区与身份行之间
    //     的一段留白」，而不是全堆在身份行下面（后者看起来像界面没画完）。
    //   ★ 为什么弹簧不吃 `side` 自己的 stretch：`VerticalLayout` 只给**子控件**
    //     分余量，父容器自己没有 stretch 概念。
    //   ★ 透明底 + 无边框：它只占位，不许出一块异色。
    {
        auto spring = std::make_unique<ECDI::Panel>();
        spring->SetSize(172, 1);      // 首帧高度 1（Arrange 前），之后由 stretch 接管
        spring->SetStretch(1);
        spring->SetStyle(ECDI::PanelStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f});
        side->AddChild(std::move(spring));
    }
    {
        auto sep = std::make_unique<ECDI::Panel>();
        sep->SetSize(172, 1);
        sep->SetStyle(ECDI::PanelStyleOverride{
            .background = kLine, .borderWidth = 0.0f});
        side->AddChild(std::move(sep));
        // ★ 2026-11-01：行高 = 上下 padding(8×2) + 行盒(19) = 35。
        //   原 24 只够旧行盒 16 ⇒ 子标签被行容器的裁剪区切掉 3px（文字底部发虚）。
        auto row = uikit::Row(172, 35, 8, 8);
        row->AddChild(uikit::DotCell(14, kPanel2, uikit::kRowMinH, nullptr));
        auto who = uikit::Label("本地工作区", 100, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(who));
        auto dots = uikit::Label("· · ·", 40, 16, kDarkMuted, harness::kFontSmall);
        row->AddChild(std::move(dots));
        side->AddChild(std::move(row));
    }

    main.AddChild(std::move(side));
}

// ── 中栏会话头：标题 + 元信息 + 三个操作钮（导出 / 检查器 / 更多）──
void HarnessApp::BuildConversationHead() {
    // ── 中栏会话头（原型 C 的 `.conversation-head`，62px）──────────
    // ★ 为什么加：原型 C 中栏顶部有「标题 + 元信息 + 操作」这一带，而现状
    //   ScrollView 直接从第一行开始，**标题只存在于系统标题栏**（用户看不到
    //   自己在聊什么）。C 的 `.head-actions` 是 3 个 iconbtn，但 `✳ ⚙ ◉ ⋯ ⌁ ☷ ✓ ⌂ ✕ ↻ ▦ ⌕`
    //   这一整批图标在两个主题的 face 里**都没有字形**（`.scratch/symcand.py` 实测）
    //   ⇒ 落地为**文字按钮**（诚实降级：不画没有字形的图标）。
    {
        auto head = std::make_unique<ECDI::Panel>();
        head->SetSize(600, 76);
        head->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? kProbeBg : kContentBg,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? kProbeBg : kLine});
        head->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, true, harness::kPadFrame));

        // 标题（拉丁 = 夹具名，可用 Semibold 出口）+ 元信息
        auto titles = std::make_unique<ECDI::Panel>();
        titles->SetSize(300, 44);
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
            h1->SetSize(300, 22);
            titles->AddChild(std::move(h1));
            auto sub = std::make_unique<ECDI::Label>(ConversationSubtitle());
            m_hud.convSub = sub.get();
            sub->SetTextColor(m_options.probeContrast ? kProbeInk : kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontMicro));
            sub->SetSize(m_devMode ? 300 : 480, 18);
            titles->AddChild(std::move(sub));
        }
        titles->SetSize(m_devMode ? 300 : 480, 44);
        head->AddChild(std::move(titles));

        auto grow = std::make_unique<ECDI::Panel>();
        grow->SetStretch(1);
        grow->SetSize(10, 10);
        head->AddChild(std::move(grow));

        // ★★ L2 持久化：只读会话顶部「继续」按钮
        auto contBtn = std::make_unique<ECDI::Button>("⎘ 继续");
        m_hud.continueBtn = contBtn.get();
        contBtn->SetSize(64, 24);
        contBtn->SetFont(harness::SansFont(harness::kFontSmall));
        contBtn->SetTextColor(kBtnPrimaryInk);
        contBtn->SetStyle(ECDI::ButtonStyleOverride{
            .background = kBtnPrimary,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kBtnPrimaryPressed,
            .hoverBackground = kBtnPrimaryHover});
        contBtn->SetOnClick([this] { ContinueCurrentSession(); });
        contBtn->SetVisible(false);
        head->AddChild(std::move(contBtn));

        // 操作钮：C 是 3 个 iconbtn（↗ ☷ ···）——`↗` 两主题都有字形，
        // `☷` 与 `⋯`(U+22EF) 都没有 ⇒ 后两个用 ASCII 文字（诚实降级，不画缺字图标）。
        // ★★ 2026-10-20：动作从「`bool toggleInspector`」改为枚举。
        //   原来一参布尔只能表达「是不是检查器」，于是 `↗`/`···` 只能落到
        //   `(void)tip; return;`（空回调）。加动作就得加枚举——否则第三个布尔参
        //   会变成 `mkAct("···", "更多", false, true)` 那种读不懂的调用。
        enum class HeadAct { Export, Inspector, Menu };
        auto mkAct = [&head, this](uikit::Icon icon, const std::string& tip,
                                   HeadAct act) {
            // ★★ P4 E：tip 参数终于有了去处（此前 `(void)tip` 闲置——ECDI 无
            //   tooltip 控件，mkAct 一直带着它等今天）。
            auto b = std::make_unique<uikit::IconButton>(
                icon, tip,
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
                    break;
                case HeadAct::Menu:
                    ToggleMoreMenu();
                    break;
                case HeadAct::Inspector:
                    ToggleInspector();   // ★ P4 B：唯一入口（原内联逻辑抽出，Ctrl+I 共用）
                    break;
                }
                if (m_window && m_promptBar) {
                    m_window->SetFocusedWidget(m_promptBar->input());
                }
            });
            head->AddChild(std::move(b));
        };
        mkAct(uikit::Icon::Share, "导出转录", HeadAct::Export);
        mkAct(uikit::Icon::PanelRight, "检查器（Ctrl+I）", HeadAct::Inspector);
        mkAct(uikit::Icon::Dots, "更多", HeadAct::Menu);
        m_counting->AddChild(std::move(head));
    }
}

// ── `···` 菜单条：内联下拉，撑开在会话头与转录之间 ──
void HarnessApp::BuildMoreMenu() {
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
            // ★ 500 = Medium（字重半档，见 Theme.h 字体工厂注释）：换 Noto 后
            //   Bold 观感比雅黑更重，菜单项降半档，仍比正文 400 略强调。
            b->SetFont(harness::SansFont(harness::kFontSmall, 500));
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
        mkMenuItem("复制全部", "复制全部智能体回答到剪贴板", [this] { CopyAllTranscriptText(); });
        // ★★ 2026-10-27（v4 第三步）：预填逻辑上提到 `OpenSettingsPanel()`——
        //   空态引导卡上的「打开设置」按钮也要走同一条预填，否则从那里进面板时
        //   字段全空、模型子列表也是空的（实测截图抓到：只有一个"模型"标签，
        //   下面一片空白）。这条在引入模型子列表之前就存在，只是没人看得出来。
        mkMenuItem("设置", "提供商连接设置（识别名 / BaseURL / API Key / 模型）",
                   [this] { OpenSettingsPanel(); });
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
        {
            auto b = std::make_unique<ECDI::Button>(m_devMode ? "简洁视图" : "开发者视图");
            m_hud.devModeMenuBtn = b.get();
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall, 500));   // 同上：菜单项半档 Medium
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            b->SetSize(96, 24);
            b->SetOnClick([this] {
                SetMoreMenuOpen(false);
                SetDevMode(!m_devMode);
            });
            menu->AddChild(std::move(b));
        }
        m_counting->AddChild(std::move(menu));
    }
}

// ── 设置面板（模态 sheet 的内容体）：返回所有权，由视口挂载 ──
std::unique_ptr<ECDI::Widget> HarnessApp::BuildSettingsSheet() {
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
        title->SetFont(harness::SansFont(18.0f));
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
        ApplyCompactScrollBars(*sv);   // ★ 条宽全仓统一（kScrollBarThickness）
        ECDI::Widget* content = &sv->GetContentView();
        content->SetLayout(std::make_unique<ECDI::VerticalLayout>(6, false, 4));
        panel->AddChild(std::move(sv));

        auto addLabel = [&content](const std::string& text) {
            auto l = std::make_unique<ECDI::Label>(text);
            l->SetTextColor(kDarkMuted);
            l->SetFont(harness::SansFont(harness::kFontSmall));
            l->SetSize(500, 20);
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
            //   ★★ 2026-10-26 订正：这里原本只给了 `.borderWidth = 1.0f`，
            //     **边框一直没画出来**。`TextBox::OnPaint`（TextBox.cpp:1282）的判据是
            //     `drawBorder = borderWidth > 0 && borderColor.a > 0`——`.border` 是
            //     **焦点点线框**的颜色，不是恒显描边色；恒显描边归 `.borderColor`，
            //     而它的默认值是**透明** ⇒ 这半个 AND 恒假，走的是
            //     `else if (radius > 0)` 分支（圆角生效、描边零）。当时"实测可辨"
            //     验的是灰底那一半，描边那半从来没被看见过——典型「设了参数以为它在」。
            //   ⇒ 补 `.borderColor`；`.border` 保留（聚焦点线框不至于跳回默认黑）。
            //   ★ REQ-14（Phase 37 verticalAlign）：30 高 / padding 0 / lineH 17
            //     ⇒ Top 时行箱贴上沿、下方空 13px；居中走框架入口（REQ-14 闭合）。
            //   ★ 2026-10-27：文字色从未 Set ⇒ ECDI TextStyle 缺省黑（Theme.h 注释
            //     「全局默认 Black」）压在 kPanel2 深灰底上不可读。输入框与 Label
            //     是同一文字族，改吃 kDarkInk（深色≈白字，浅色仍是深墨）。
            (*slot)->SetTextColor(kDarkInk);
            (*slot)->SetStyle(ECDI::TextBoxStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .cornerRadius = harness::kRadiusBtn,
                .borderWidth = 1.0f,
                .borderColor = kLine,
                .verticalAlign = ECDI::VerticalAlignment::Center});
            (*slot)->SetSize(500, 30);
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
            cap->SetSize(500, 20);
            content->AddChild(std::move(cap));
            auto row = std::make_unique<ECDI::Panel>();
            // ★ 26（输入框/挡位钮）+ 6×2 padding ⇒ 38；原 26 会让按钮下沿被裁。
            // ★ 宽 500 而非 520：520 = sv 宽时视口被垂直条吃掉 8px ⇒ 恒出一条
            //   横向滚动条（把 API Key 输入框压掉半行）。留 20px 给它。
            row->SetSize(500, 38);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto box = std::make_unique<ECDI::TextBox>();
            *slot = box.get();
            (*slot)->SetSingleLine(true);
            (*slot)->SetTextColor(kDarkInk);   // ★ 同上游标框：黑字压深灰底不可读（2026-10-27）
            (*slot)->SetStyle(ECDI::TextBoxStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .cornerRadius = harness::kRadiusBtn,
                .borderWidth = 1.0f,
                .borderColor = kLine,   // ★ 同上一处：恒显描边色缺了 ⇒ drawBorder 恒 false
                .verticalAlign = ECDI::VerticalAlignment::Center});   // ★ REQ-14（Phase 37）
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

        // ── ★★ 2026-10-25 修复（实测反馈「上面空一大片」）─────────────
        //   此前这三件**分居两处**：`list` 挂在 content 首位（字段之前）、`listCap`
        //   挂在 panel 底部（字段之后）⇒ ① 空列表时字段上方恒留 108px 纯空白
        //   （首启必现，用户看到的就是它）；② 「点击选择编辑」的标签与它描述的
        //   列表被整片字段隔开，读不出从属关系。
        //   ⇒ 统一收进 content，并按「先选后编」排在字段之前：标签→列表→增删行
        //     紧挨着，选中哪家、下方字段就填哪家。
        auto listCap = std::make_unique<ECDI::Label>(
            "提供商（点击选择编辑；● = 当前连接）");
        listCap->SetTextColor(kDarkMuted);
        listCap->SetFont(harness::SansFont(harness::kFontSmall));
        listCap->SetSize(500, 20);
        content->AddChild(std::move(listCap));
        {
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.settingsProviderList = list.get();
            // 基准高 = 一行提示（空态）；有行时由 RebuildSettingsProviderList
            //   按行数改到 ≤108（3 行满格），不再恒占 108 的空白。
            list->SetSize(500, 26);
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            content->AddChild(std::move(list));
        }
        {
            auto row = std::make_unique<ECDI::Panel>();
            // ★★ 行高必须 ≥ 子控件高 + 2×padding：`HorizontalLayout` 的 padding
            //   是**四边硬 inset**，槽位从 padding 起摆、子控件却保持自身高度 ⇒
            //   行高 26 + padding 6 时可用高只有 26-6=20，26px 按钮的下沿被父容器
            //   裁掉 6px（圆角消失、文字下半截被切——实测截图）。
            row->SetSize(500, 38);   // 26（按钮）+ 6×2
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
                // ★ v4 第三步：新增提供商 = 它名下还没有任何模型 ⇒ 模型编辑目标也清空。
                m_editingModelName.clear();
                // ★ 新增 = 与上一家无关：把「待确认改名」也清掉，否则一次
                //   A→B 的确认会残留到下一次保存（那正是"确认过 A→B 却执行 A→C"）。
                m_pendingRenameFrom.clear();
                m_pendingRenameTo.clear();
                if (m_hud.settingsBase) {
                    if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText("");
                    if (m_hud.settingsProviderName) m_hud.settingsProviderName->SetText("");
                    m_hud.settingsBase->SetText("");
                    m_hud.settingsModel->SetText("");
                    if (m_hud.settingsContext) m_hud.settingsContext->SetText("");
                    if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("");
                    m_hud.settingsKey->SetText("");
                    m_hud.settingsHint->SetText("填写新提供商信息，保存后加入列表");
                }
                RebuildSettingsModelList();   // 编辑目标没了 ⇒ 模型列表回到空态说明
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
                // ★ v4 第三步：编辑目标换人了 ⇒ 模型编辑目标也跟着换成新那家的激活模型，
                //   否则字段区编的还是"刚被删掉那家"的模型名（保存时会被当成新增）。
                m_editingModelName.clear();
                if (const ProviderConfig* now2 = m_appConfig.Find(m_editingProviderId)) {
                    m_editingModelName = now2->ActiveModelName();
                }
                RebuildSettingsProviderList();
                RebuildSettingsModelList();
                m_modelListDirty = true;
                m_hud.settingsHint->SetText("已删除");
            });
            row->AddChild(std::move(b2));
            content->AddChild(std::move(row));   // ★ 与标签/列表同处 content（见上方注释）
        }

        // ── ★★ v4 第三步（2026-10-27）：模型子列表 ─────────────────────────
        //   为什么排在提供商行之后、字段之前：读法仍是「先选后编」——选提供商 →
        //   选这家的哪个模型 → 下面三个字段编它。三块紧挨着，从属关系才看得出来；
        //   插到字段下面的话，用户会先看到"模型名"却不知道它是这家里面的哪一个。
        //   ★ 列表**跟着 m_editingProviderId 走**（RebuildSettingsModelList 里重建），
        //     没选提供商时给一行说明，不留空白（同上方提供商列表的空态纪律）。
        {
            auto cap = std::make_unique<ECDI::Label>(
                "模型（点击选择编辑；● = 该家激活模型；保存后表单这个成为激活）");
            cap->SetTextColor(kDarkMuted);
            cap->SetFont(harness::SansFont(harness::kFontSmall));
            cap->SetSize(500, 20);
            content->AddChild(std::move(cap));
            auto list = std::make_unique<ECDI::Panel>();
            m_hud.settingsModelList = list.get();
            list->SetSize(500, 26);   // 基准高 = 一行提示；有行时按行数改到 ≤108
            list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
            content->AddChild(std::move(list));
        }
        {
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(500, 38);   // 26（按钮）+ 6×2 —— 同上方增删行，padding 是四边硬 inset
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 6));
            auto b1 = std::make_unique<ECDI::Button>("新增模型");
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
                // ★ 清空「正在编辑的模型」⇒ 保存时走"追加"，不是"替换掉某一个"。
                //   三个容量字段也一并清掉：留着上一行的值会让人以为新模型已经配好了。
                m_editingModelName.clear();
                if (m_hud.settingsModel) m_hud.settingsModel->SetText("");
                if (m_hud.settingsContext) m_hud.settingsContext->SetText("");
                if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("");
                if (m_hud.settingsHint) {
                    m_hud.settingsHint->SetText("填写新模型名与容量，保存后加入该提供商");
                }
                RebuildSettingsModelList();
            });
            row->AddChild(std::move(b1));
            auto b2 = std::make_unique<ECDI::Button>("删除模型");
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
            b2->SetOnClick([this] { OnSettingsDeleteModel(); });
            row->AddChild(std::move(b2));
            content->AddChild(std::move(row));
        }

        // ★★ §9.5（2026-10-27）：身份由用户**声明**，两个字段排在最前。
        //   识别名 = 机器键（凭据目标名 / 环境变量片段 / 协议 `model.provider`
        //   都绑它），显示名 = 只给眼睛看的（可重复）。留空识别名 ⇒ 保存时用
        //   BaseURL 的 host 兜底（旧派生行为降级成了"初值"，见 AppConfig.h）。
        addLabel("识别名（唯一机器键：字母数字和 . _ -，≤64；留空用域名）");
        addInput(&m_hud.settingsProviderId, false);
        addLabel("显示名（界面显示，可重复；留空则与识别名相同）");
        addInput(&m_hud.settingsProviderName, false);
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
            // ★★ 2026-10-27（用户要求「按键靠右」）：行宽跟字段列对齐（520），
            //   最前面塞一个 `stretch = 1` 的空面板吃掉剩余空间 ⇒ 两个按钮被顶到右端。
            //   ★ 为什么不用 `SetPosition`：ECDI 布局**幂等重排**，手写坐标每次
            //     Arrange 都会被覆盖（Layout 契约 1）；stretch 是布局自己的分配语义。
            //   ★ padding 取 0（不是原来的 6）：按钮右边缘要跟下面那行右对齐的状态
            //     文字落在同一条线上；行高随之从 42 收到 30（30 = 按钮高，不再垫 6）。
            //     原来那 42/6 的注释讲的是「padding 会裁按钮下沿」，padding=0 时该
            //     约束自然消失（0 + 30 = 30），不是把它删了当没事。
            row->SetSize(520, 30);
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, false, 0));
            auto spacer = std::make_unique<ECDI::Panel>();
            spacer->SetSize(0, 30);       // 宽由 stretch 分配（初值无意义）
            spacer->SetStretch(1);
            row->AddChild(std::move(spacer));
            auto save = std::make_unique<ECDI::Button>("保存并连接");
            m_hud.settingsSave = save.get();
            save->SetTextColor(harness::kBtnPrimaryInk);
            save->SetFont(harness::SansFont(16.0f));
            save->SetStyle(ECDI::ButtonStyleOverride{
                .background = harness::kBtnPrimary,
                .border = harness::kBtnPrimary,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = harness::kBtnPrimaryPressed,
                .hoverBackground = harness::kBtnPrimaryHover});
            save->SetSize(140, 30);
            save->SetOnClick([this] { OnSettingsSave(); });
            row->AddChild(std::move(save));

            auto closeBtn = std::make_unique<ECDI::Button>("关闭");
            closeBtn->SetTextColor(kDarkInk);
            closeBtn->SetFont(harness::SansFont(16.0f));
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

        // ★ 2026-10-27（用户要求「下面的文字也靠右」）：状态行改 `RightLabel`。
        //   它超宽时**贴左裁尾**（不是贴右裁头）——文案纪律「可执行信息放最前」
        //   因此仍然成立（理由写在 UiKit.h 的 RightLabel 上）。
        auto hint = std::make_unique<uikit::RightLabel>("");
        m_hud.settingsHint = hint.get();
        hint->SetTextColor(kDarkMuted);
        hint->SetFont(harness::SansFont(harness::kFontSmall));
        hint->SetSize(520, 18);
        panel->AddChild(std::move(hint));

        // ★★ P4 F：不进 counting（那会被 VerticalLayout 接管成槽位兄弟）——
        //   暂存，scroll 建好后与遮罩一起转移为视口浮层。
        settingsSheetOwned = std::move(panel);
    }
    // ★ v4 第三步：构造期先把模型子列表刷成"空态说明"。两个入口（更多菜单 /
    //   引导卡）都会预填并重建，但**首启自动弹面板**那条路径不预填（first-run 探针
    //   要求字段为空）⇒ 不刷的话那里是一块高 26 的空白带（实测截图：只有"模型"
    //   标签，下面什么都没有，看不出该先选提供商）。
    RebuildSettingsModelList();

    return settingsSheetOwned;
}

// ── ★★ 2026-10-30（浮层迁移）：模型下拉浮窗的**建**（挂/收归 `SetModelPopupOpen`）──
// 为什么单独提成函数：它从「建树期建一次、之后只 SetVisible」改成了
//   「每次打开现建、关闭即销毁」（`Window::ShowOverlay` 的语义：所有权移交 +
//   `HideOverlay` 帧末析构）⇒ 必须能反复调用，且每次调用都要重设 `m_hud` 句柄。
// ★ 尺寸/坐标仍在 `SetModelPopupOpen` 里定（那里才知道视口多宽）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildModelPopup() {
    auto pop = std::make_unique<ECDI::Panel>();
    m_hud.modelPopup = pop.get();
    pop->SetStyle(ECDI::PanelStyleOverride{
        .background = kCardBg,
        .cornerRadius = kRadiusBtn,
        .borderWidth = 1.0f,
        .borderColor = kCardBorder});
    // ★ 2026-11-01（用户反馈「功能不知所以」）：标题行把「这是干什么的」写在脸上。
    //   此前打开只有一个空搜索框——列表因 `list` 尺寸 0×0 被整棵剔除，用户看到的
    //   是一块「不知道干嘛的空白面板」。标题不是装饰：即便列表修好，一列模型名 +
    //   一个空搜索框仍然不自明（它不像菜单，像一张表）。
    auto popTitle = std::make_unique<ECDI::Label>("切换模型");
    popTitle->SetTextColor(kDarkInk);
    popTitle->SetFont(harness::SansFont(harness::kFontSmall));
    popTitle->SetSize(kModelPopupW - 2 * kModelPopupPadX, kModelPopupTitleH);
    popTitle->SetPosition(kModelPopupPadX, kModelPopupTitleY);
    pop->AddChild(std::move(popTitle));
    auto search = std::make_unique<ECDI::TextBox>();
    m_hud.modelSearch = search.get();
    search->SetSingleLine(true);
    // ★ REQ-14（Phase 37 verticalAlign）：26 高 / padding 0 ⇒ Top 时行箱贴上沿、
    //   下方空 9px ⇒ Center。
    // ★★ 2026-10-27 修（台账「待定夺」落定）：REQ-14 落地时这里「只覆写对齐一项，
    //   其余全吃主题缺省」——吃缺省吃的正是缺陷：ECDI TextBoxStyle 缺省 background=白
    //   ⇒ 深色主题下一块白底（P4 H 起漏网）。⇒ 与设置面板输入框同款：kPanel2 底 +
    //   kLine 描边（borderColor 必须显式给，缺省透明 ⇒ drawBorder 恒假）+
    //   kDarkInk 文字（缺省黑，深底不可读）。
    search->SetTextColor(kDarkInk);
    search->SetStyle(ECDI::TextBoxStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .cornerRadius = kRadiusBtn,
        .borderWidth = 1.0f,
        .borderColor = kLine,
        .verticalAlign = ECDI::VerticalAlignment::Center});
    search->SetSize(kModelPopupW - 2 * kModelPopupPadX, kModelPopupSearchH);
    search->SetPosition(kModelPopupPadX, kModelPopupSearchY);
    search->SetOnTextChanged([this](const std::string&) { RebuildModelPopupList(); });
    pop->AddChild(std::move(search));
    // ★★ 2026-11-01 修（用户反馈「点开一片空白」+「功能不知所以」）：两条缺陷叠在
    //   同一个控件上——
    //   ① **`SetSize` 缺失** ⇒ 0×0 ⇒ `Widget::Paint` 的第一条 `IsRectVisible(self)`
    //      （Widget.cpp:605）直接 return ⇒ 整棵列表子树零绘制。行**建了**、一个都没画，
    //      所以探针（只看开合与挂载）一直绿，屏幕却空着——这正是「结构绿了、视觉没绿」。
    //   ② **两个排布者抢同一个容器**：`VerticalLayout` 的 `Arrange` 会把每个子项改写成
    //      `(padding, y累进)`（VerticalLayout.cpp:99），把 `RebuildModelPopupList` 里
    //      手工算的 `SetPosition(0, y)` 全部覆盖。留手工那套——它和 `kModelPopupListMaxH`
    //      的溢出收敛（`hidden` 计数 + 底注「另有 N 项未显示」）是配套的，布局不知道这件事。
    auto list = std::make_unique<ECDI::Panel>();
    m_hud.modelPopupList = list.get();
    list->SetSize(kModelPopupW - 2 * kModelPopupPadX, kModelPopupListMaxH);
    list->SetPosition(kModelPopupPadX, kModelPopupListY);
    pop->AddChild(std::move(list));
    auto foot = std::make_unique<ECDI::Label>(
        "仅 idle 发出 · 运行中自动排队（本轮输出完生效）");
    m_hud.modelPopupFoot = foot.get();
    foot->SetTextColor(kDarkMuted);
    foot->SetFont(harness::SansFont(harness::kFontMicro));
    foot->SetSize(kModelPopupW - 2 * kModelPopupPadX, kModelPopupFootH);
    foot->SetPosition(kModelPopupPadX, kModelPopupFootY);
    pop->AddChild(std::move(foot));
    pop->SetSize(kModelPopupW, kModelPopupH);
    return pop;
}

// ── 转录视口：ScrollView + 浮层（跳转钮 / 遮罩 / 设置 sheet / 模型浮窗 / tooltip）──
void HarnessApp::BuildTranscriptViewport(std::unique_ptr<ECDI::Widget> settingsSheet,
                                         std::unique_ptr<ECDI::Widget> previewSheet) {
    auto scroll = std::make_unique<ECDI::ScrollView>();
    m_scroll = scroll.get();
    m_scroll->SetStretch(1);
    m_scroll->SetScrollStep(40);
    // ★ 必须在 `SetContentExtent`/首次布局**之前**设：条厚参与 viewport 计算
    //   （`thickness + kBarInset`），运行期再改会与已算出的 maxOffset 脱钩。
    ApplyCompactScrollBars(*m_scroll);
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
    // ★★ 2026-10-30（浮层迁移，docs/overlay-migration-plan.md §3）：设置/预览的遮罩与
    //   sheet **不再挂 m_scroll**，也不再由 BuildUi 搬到 root 末尾——所有权交给 harness
    //   （`m_settings*Owned` / `m_preview*Owned`），首次显示时挂 `Window::GetOverlayRoot()`。
    //   于是「盖全窗 + 不被视口 PushClip 裁 + 不被布局统计」三件事都由浮层根保证，
    //   本函数只负责**建**，不负责挂。
    //   ★ 挂载序仍是「先遮罩后 sheet」（`Widget::Paint` 正序 / `HitTest` 逆序）——
    //     与原先在 scroll 子表里的相对次序逐位相同，视觉与命中关系不变。
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
    m_settingsMaskOwned = std::move(mask);
    if (settingsSheet) {
        m_hud.settingsPanel->SetVisible(false);   // 显隐统一走 ApplySettingsVisibility
        m_settingsSheetOwned = std::move(settingsSheet);
    }
    // ★★ P4 Phase I（2026-10-26）：文件预览 sheet——与设置同款（同一条迁移）。
    {
        auto pmask = std::make_unique<ECDI::Button>();
        m_hud.previewMask = pmask.get();
        pmask->SetVisible(false);
        pmask->SetOnClick([] {});   // 吞点击（同 settings mask；关闭走 Esc/×）
        pmask->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(15, 17, 23, 150),
            .cornerRadius = 0.0f,
            .pressedBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150),
            .hoverBackground = ECDI::Color::FromRGBA8(15, 17, 23, 150)});
        m_previewMaskOwned = std::move(pmask);
        if (previewSheet) {
            m_hud.previewPanel->SetVisible(false);   // 显隐统一走 ApplyPreviewVisibility
            m_previewSheetOwned = std::move(previewSheet);
        }
    }
    // ★★ P4 Phase H（2026-10-25）：模型下拉浮窗。
    // ★★ 2026-10-30（浮层迁移）：**建树期不再创建它**——下拉改成每次打开时
    //   由 `BuildModelPopup()` 新建、经 `Window::ShowOverlay` 挂 Normal 层
    //   （关闭即销毁），见 `AppSettings.cpp` 的 `SetModelPopupOpen`。
    //   于是「恒在内容之上 + 点外关闭 + 打开时焦点进搜索框」由框架给，
    //   而「最后 AddChild = 最上层」那条 z 序依赖彻底消失。
    // ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层。
    // ★★ 2026-10-30（浮层迁移）：**建树期不再创建它**——tooltip 改成每次显示时
    //   新建并经 `Window::ShowOverlay` 挂 Tooltip 层（关闭即销毁），见
    //   `AppHud.cpp` 的 `OnTooltipFire`。原来那句「隐藏态常驻」的常驻控件没了，
    //   换来的是「不被视口裁剪 + 点穿 + 单例由销毁保证」。
    m_counting->AddChild(std::move(scroll));
}

// ── Composer（输入条）──
void HarnessApp::BuildComposer() {
    // Composer 收进中栏（hi-fi .composer 在 .mid 内）
    auto composer = std::make_unique<PromptBar>();
    m_promptBar = composer.get();
    composer->SetSize(600, 92);
    composer->SetOnSend([this](const std::string& text) {
        if (!SendPrompt(text)) return;
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
    composer->SetOnContinue([this] {
        ContinueCurrentSession();
    });
    m_counting->AddChild(std::move(composer));
}

// ── 右栏检查器：五张白卡（渲染探针 / 模型 / 用量 / 协议健康 / 权限历史）──
void HarnessApp::BuildInspector(ECDI::Panel& main) {
    // ── 右栏检查器（hi-fi .insp：卡片化）──
    auto insp = std::make_unique<ECDI::Panel>();
    m_hud.inspector = insp.get();
    insp->SetVisible(m_inspectorVisible);
    insp->SetSize(m_inspectorWidth, 100);
    insp->SetStyle(ECDI::PanelStyleOverride{
        .background = kInspBg,
        .borderWidth = 1.0f, .borderColor = kLine});
    // ★ 2026-11-02 疏松（用户实测「右侧栏过于紧凑」）：卡片间距 14→16；
    //   卡内边距 10→12、卡内行距 6→8；行盒统一 kInspRowH=22
    //   （原 16/14 经 uikit::Row 兜底实为 19，行间净距仅 6px）。
    //   六张卡的硬编码高已按新几何逐张重算（算式见各卡注释）。
    insp->SetLayout(std::make_unique<ECDI::VerticalLayout>(16, true, harness::kPadFrame));
    constexpr int kInspRowH = 22;

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
    auto kvRow = [kInspRowH](ECDI::Panel& parent, const std::string& k,
                             ECDI::Label** vOut, int kw = 56) {
        auto row = uikit::Row(178, kInspRowH, 10);
        row->AddChild(uikit::Label(k, kw, kInspRowH, kDarkMuted));
        auto vl = uikit::Label("—", 110, kInspRowH, kDarkInk, 12.5f, /*mono=*/true);
        *vOut = vl.get();
        row->AddChild(std::move(vl));
        parent.AddChild(std::move(row));
    };

    // 卡片白底和内置标题对齐原型 C 的信息层级；统计项仍保留 harness 专用读数。
    {
        // ★ 2026-11-02：pad12 + 标题17 + 3×(8+22) + pad12 = 131 → 132
        auto card = makeCard(132);
        m_hud.inspProbeCard = card.get();
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
        cardTitle(*card, "渲染探针");
        kvRow(*card, "重绘", &m_hud.evalPaints);
        kvRow(*card, "命令/帧", &m_hud.probeCmds);
        kvRow(*card, "执行", &m_hud.probeExec);
        insp->AddChild(std::move(card));
    }

    {
        // 标题收进白卡，与原型 C 的卡片层级一致。上下文环仍保留为评估读数。
        //   算式：48（环行）+ 8（间距）+ 16（输入）+ 8 + 16（输出）+ 2×10（卡片内边距）= 124。
        // ★ 2026-11-02：pad12 + 标题17 + 8 + 列表110 + pad12 = 159 → 160（列表恒定 110）
        auto card = makeCard(160);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
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
        // ★ 2026-11-02：pad12 + 标题17 + 8 + 环行48 + 8 + 22 + 8 + 22 + pad12 = 157 → 158
        auto card = makeCard(158);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
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
        auto urow = [&card, kInspRowH](const std::string& k, ECDI::Label** vOut,
                                       ECDI::ProgressBar** barOut, const ECDI::Color& fill) {
            auto row = uikit::Row(178, kInspRowH, 8);
            row->AddChild(uikit::Label(k, 40, kInspRowH, kDarkMuted));
            auto bar = std::make_unique<ECDI::ProgressBar>();
            *barOut = bar.get();
            bar->SetSize(70, 5);
            bar->SetStyle(ECDI::ProgressBarStyleOverride{
                .trackColor = kTrackBg, .fillColor = fill,
                .cornerRadius = 2.0f});
            // ★ 2026-11-02：行盒抬高到 22 后，5px 细条在 fillCross=false 的行里
            //   会顶对齐贴到文字上方——套一个等高 cell 手动垂直居中（同环行手法）。
            auto barCell = std::make_unique<ECDI::Panel>();
            barCell->SetSize(70, kInspRowH);
            bar->SetPosition(0, (kInspRowH - 5) / 2);
            barCell->AddChild(std::move(bar));
            row->AddChild(std::move(barCell));
            auto vl = uikit::Label("—", 52, kInspRowH, kDarkInk, 13.0f, /*mono=*/true);
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
            // ★ 2026-11-03：换 Noto 后「上下文」三字 @14px 实需 42px，原标签格 40px
            //   把「文」右缘硬切（Label 单行裁剪；雅黑墨迹恰收在界内故此前未露）。
            //   修法**不是**加宽标签格了事——HorizontalLayout 定距 8 会把环推到
            //   x=52，破坏环与下方输入/输出两根条左缘的同列对齐（x=48）。故本行
            //   改**无布局手工摆位**：三格 x 逐像素保持 H 布局旧值（0/48/100 =
            //   40+8 / +44+8），只把标签格自身加宽 40→48（吃掉右侧 8px 死空隙：
            //   文字左对齐位置不动、墨迹间隙 ~6px 不变）⇒「文」完整、环与条的
            //   列对齐、百分比位置全部不变。
            auto row = std::make_unique<ECDI::Panel>();
            row->SetSize(178, 48);
            auto cell = [](std::unique_ptr<ECDI::Widget> inner, int x) {
                auto c = std::make_unique<ECDI::Panel>();   // 无 layout ⇒ 保留子控件 SetPosition
                c->SetPosition(x, 0);
                c->SetSize(inner->GetWidth(), 48);
                inner->SetPosition(0, (48 - inner->GetHeight()) / 2);
                c->AddChild(std::move(inner));
                return c;
            };
            row->AddChild(cell(uikit::Label("上下文", 48, 16, kDarkMuted), 0));

            auto ring = std::make_unique<ECDI::RingProgressBar>();
            m_ctxRing = ring.get();
            ring->SetSize(44, 44);            // min(w,h) 作直径（RingProgressBar.h:16）
            ring->SetStyle(ECDI::RingProgressBarStyleOverride{
                .trackColor = kRingTrack,     // ★ 独立令牌：kLine 浅色下压白卡只有 1.09:1，整圈会消失
                .fillColor = harness::kOkDark,
                .thickness = 5.0f});
            row->AddChild(cell(std::move(ring), 48));

            auto vl = uikit::Label("—", 52, 18, kDarkInk, 13.0f, /*mono=*/true);
            m_hud.ctxPct = vl.get();
            row->AddChild(cell(std::move(vl), 100));
            card->AddChild(std::move(row));
        }
        urow("输入", &m_hud.usageInV, &m_usageBarIn, harness::kDarkAccent);
        urow("输出", &m_hud.usageOutV, &m_usageBarOut, harness::kDarkAccent);
        insp->AddChild(std::move(card));
    }

    // ★★ 协议 v5（2026-11-04）：右栏「代理」卡。**就这一行**——实现全在
    //   `AppAgents.cpp`（见方案 §约束：本文件只允许这一处变动）。
    //   ★ 位置：紧跟「用量」之后，**不是**方案原文说的"BuildInspector 末尾"。
    //     改位置的理由是实测出来的：检视栏是固定列高、**不滚动**（`VerticalLayout`
    //     直接排在 `insp` 上），加在末尾时整张卡落在窗口下边界之下——截图实证
    //     只剩「代理」标题与「主代理」一行露头，两个子代理行全在视野外
    //     （`.scratch/agents_inspector.png`，1920×960 窗口）。而探针读的是
    //     `cardRows`/`cardVisible`，**看不见"被挤出可视区"这件事**——它是
    //     "跑过 ≠ 判过"的又一例，只能靠看图发现。
    //   ★ 放在这里的代价：开发者模式下它后面还有「协议健康」「权限历史」两张
    //     诊断卡，窗口高度不足时被挤下去的是它们（用户模式下「渲染探针」与
    //     「协议健康」默认隐藏，一屏放得下）。检视栏该不该可滚动是另一个议题，
    //     已登记为待办，不在本轮顺手改。
    BuildAgentCard(*insp, kInspRowH);

    // ★★ 2026-11-06：右栏「MCP」卡（hello.mcp，协议 §3.1 可选字段）。
    //   数据只来自 core；默认隐藏，有状态时由 RebuildMcpCard 显形。
    BuildMcpCard(*insp, kInspRowH);

    {
        // ── ★★ L3（2026-10-28）：记忆卡片（docs/session-persistence-decisions.md §8）──
        // ★ 2026-11-02：pad12 + 标题17 + 8 + 列表50 + pad12 = 99 → 100
        auto card = makeCard(100);
        m_hud.inspMemoryCard = card.get();
        card->SetVisible(false); // 默认隐藏，有记忆文件时展示
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
        cardTitle(*card, "记忆");
        auto list = std::make_unique<ECDI::Panel>();
        m_hud.memoryList = list.get();
        list->SetSize(190, 50);
        list->SetLayout(std::make_unique<ECDI::VerticalLayout>(4, false, 0));
        card->AddChild(std::move(list));
        insp->AddChild(std::move(card));
    }

    {
        // ★ 2026-11-02：pad12 + 标题17 + 3×(8+22) + pad12 = 131 → 132
        auto card = makeCard(132);
        m_hud.inspHealthCard = card.get();
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
        cardTitle(*card, "协议健康");
        // ★ 第三行（2026-10-13）：未知 kind 单独一行。
        //   为何不并进 healthViol：实测把「未知kind N」追加到同一 label 后**被裁掉**
        //   （该 label 宽 150，`违规 N · 未知 N · 未知kind N` 放不下，截图可见「未知k」）。
        //   单独一行既避免裁切，也让「协议未规定的事」有一处固定的可见落点。
        auto hrow = [&card, kInspRowH](ECDI::Label** vOut, const char* initial = "—") {
            auto row = uikit::Row(178, kInspRowH, 8);
            // ★ 2026-11-01：点必须按**行盒**在格内居中——`Dot` 直接塞进
            //   fillCross=false 的行里是顶对齐的（点中心 4 vs 文字中心 9.5，差 4px，
            //   用户实测反馈「协议健康下面三个点和文字没对齐」）。
            //   2026-11-02：cell 高随行盒抬到 22，居中基准不变。
            row->AddChild(uikit::DotCell(8, kOkDark, kInspRowH, nullptr));
            auto vl = uikit::Label(initial, 150, kInspRowH, kDarkInk);
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
        // ★ 2026-11-02：pad12 + 标题17 + 3×(8+22) + pad12 = 131 → 132
        auto card = makeCard(132);
        card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
        cardTitle(*card, "权限历史");
        for (size_t i = 0; i < 3; ++i) {
            auto row = uikit::Row(178, kInspRowH, 8);
            auto chip = uikit::Label("", 42, kInspRowH, kDarkMuted, 12.5f, /*mono=*/true);
            m_hud.histChip[i] = chip.get();
            row->AddChild(std::move(chip));
            auto what = uikit::Label("", 122, kInspRowH, kDarkMuted, 13.0f);
            m_hud.histWhat[i] = what.get();
            row->AddChild(std::move(what));
            card->AddChild(std::move(row));
        }
        insp->AddChild(std::move(card));
    }

    main.AddChild(std::move(insp));
}
