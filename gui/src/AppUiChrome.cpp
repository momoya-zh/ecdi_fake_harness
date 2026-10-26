// ============================================================
// UI 外壳：建树入口（BuildUi / RebuildUi）、顶栏、底栏、root 布局
// ------------------------------------------------------------
// 为什么单独一个文件：这里回答的是「窗口由哪几块组成、每块多高」——是**结构**，
//   不是内容。中栏里具体长什么样归 AppUiMain.cpp，摆位归 AppHud.cpp。
//   三者的修改理由完全不同：加一块区域改这里，改一个气泡改 Main，改像素位置改 Hud。
// ★ `HarnessRootLayout` 只在本文件用（BuildUi 装它）：它逐位复刻
//   VerticalLayout(0,true) 的摆位算法，但对 Exempt 注册的浮层完全跳过——
//   原版会把可见浮层当固定尺寸子统计，mask+sheet 一显示主区就被挤成 0×0。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1e）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"
#include "ThemedCaptionBar.h"
#include "UiKit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/Layout.h"   // HarnessRootLayout 基类
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/CaptionBar.h"

#include <algorithm>
#include <memory>
#include <set>   // HarnessRootLayout 的豁免集
#include <string>
#include <utility>
#include <vector>

// 跨 TU 共享符号（定义见 AppInternal.h / UiCommon.cpp）
using harness::internal::AddLine;
using harness::internal::ContentWidth;
using harness::internal::ConversationSubtitle;
using harness::internal::ConversationTitle;
using harness::internal::kCanvasPadX;
using harness::internal::kHeaderH;
using harness::internal::kProbeCanvas;
using harness::internal::kSidebarGroupH;
using harness::internal::kSidebarListW;
using harness::internal::kSidebarRowH;

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
    // ★★ P4 Phase I（2026-10-26）：预览 mask/sheet 同款转移（盖全窗；几何归
    //   LayoutPreviewOverlay——root.Arrange 跳过浮层子，布局与浮层互不干扰）。
    if (m_hud.previewMask && m_hud.previewMask->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.previewMask));
        rootLayoutRaw->Exempt(m_hud.previewMask);
    }
    if (m_hud.previewPanel && m_hud.previewPanel->GetParent() == m_scroll) {
        root.AddChild(m_scroll->RemoveChild(m_hud.previewPanel));
        rootLayoutRaw->Exempt(m_hud.previewPanel);
    }
    // ★★ 2026-10-26（按模式分流）：首帧与重建都在 root.Arrange() 之前把
    //   `m_devMode` 投影到所有开发者专属控件上——在开发者模式下所有控件本身已是
    //   可见的，同值守卫下零额外 Arrange、零额外 Invalidate。
    ApplyDevModeVisibility();
    root.Arrange();

    // ★★ 2026-10-26：输入框高度在这里**由测量**定下，不留手抄常数。
    //   ★ 为什么必须在这个位置（三条都不可换）：
    //     ① 构造期不行——`PromptBar::~` 之前没有窗口 ⇒ `GetPreferredSize()` 退回
    //        当前尺寸（默认 20），量了等于没量；
    //     ② `root.Arrange()` 之前不行——`LayoutChildren` 用 `PromptBar` 的**实际宽度**
    //        摆子控件，宽度要到父布局分完才有值；
    //     ③ `Show()` 之后不行——首帧已经画出去了，用户会看见压扁的那一瞬。
    //   ★ 为什么放在 `BuildUi` 而不是 `Run()` 里建窗之后：换主题走 `RebuildUi` →
    //     `BuildUi` 同一条路，两条路径共用一处入口（`RebuildUi` ⑧ 的调用只负责
    //     **草稿**的多行高度，与这里的单行初值不冲突：先本函数定单行，再恢复草稿长高）。
    //   实测口径：单行 29（行高 17 + padding 6×2），见 `--grow-probe` 的 baseline 相
    //   `inputH=29 / barH=81 / hintY=43`。
    if (m_promptBar) m_promptBar->SyncInputHeight();
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
    //   ★★ 2026-10-21：草稿恢复**必须显式重算高度**。
    //     `SetText` 不走 `RaiseTextChanged`（D7 契约），而 `ComposerBox` 的自动长高
    //     挂在 `OnTextChanged` 上 ⇒ 这里若不同步，一个 3 行的草稿会在重建后
    //     被塞回单行高（垂直压扁、光标跑到框外）——而 `CompareUi` 的
    //     `inputText` 通道**读的是文本，看不见高度**，所以这个缺陷在整树重建
    //     探针里是全绿的。这正是"读文本看不见几何"那条老账。
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
    auto menu = std::make_unique<uikit::IconButton>(
        uikit::Icon::Hamburger, "侧栏显隐（Ctrl+B）",
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
    {
        auto pill = makeChip(std::move(sessChipLabel), 84, true);
        m_hud.sessChipPill = pill.get();
        bar->AddChild(std::move(pill));
    }
    {
        auto modeChipLabel = chipLabel("replay", kDarkMuted, 64);
        m_hud.modeChip = modeChipLabel.get();
        auto pill = makeChip(std::move(modeChipLabel), 64, false);
        m_hud.modeChipPill = pill.get();
        bar->AddChild(std::move(pill));
    }

    auto grow = std::make_unique<ECDI::Panel>();
    grow->SetStretch(1);
    grow->SetSize(10, 10);
    bar->AddChild(std::move(grow));

    // ★ 同为胶囊 ⇒ 同样居中（见上方 `chipLabel` 注释）
    auto rowLabel = uikit::Centered("autosize", 80, 20, kDarkMuted, 11.0f, /*mono=*/false);
    m_hud.rowChip = rowLabel.get();
    {
        auto pill = makeChip(std::move(rowLabel), 80, false);
        m_hud.rowChipPill = pill.get();
        bar->AddChild(std::move(pill));
    }

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
        auto pill = makeChip(std::move(backendLabel), 52, false);
        m_hud.backendChipPill = pill.get();
        bar->AddChild(std::move(pill));
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
        "— ﹀", "模型切换（Ctrl+M）——仅 idle 发出，运行中自动排队",
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

// 状态栏（hi-fi .statusbar）：圆点+状态 | 消息数 | tokens | 重绘（Composer 已收进中栏）
void HarnessApp::BuildBottom(ECDI::Widget& root) {
    auto status = std::make_unique<ECDI::Panel>();
    m_hud.statusBar = status.get();
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
