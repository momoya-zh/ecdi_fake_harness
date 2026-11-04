// ============================================================
// 浮层拓扑取证（`--overlay-probe`）
// ------------------------------------------------------------
// 【为什么需要这个探针】
//   2026-10-30 的浮层迁移把本仓 6 处绕行换成了 ECDI Phase 42 的窗口级浮层根
//   （`Window::GetOverlayRoot()` / `ShowOverlay` / `HideOverlay`）。迁移之后，
//   本仓多了一条**结构性不变量**：
//       「模态/tooltip/下拉都必须挂在浮层根下，且整树重建不得让新旧两套同屏。」
//   而在这之前，**没有任何探针读过 `GetOverlayRoot()`**——旧判据只问
//   「设置面板还在不在」（`--theme-probe` 的 toggle 相）与「可见性对不对」
//   （`--flow-probe`）。「在」与「只有一套在」是两件事：两套同屏时前者全绿。
//   这正是本仓反复栽的那类坑——**读数看得见的量，不是要守的量**。
//
// 【核心读数：`overlayRootChildren`】
//   浮层根**恒有 3 个孩子**（Normal / Modal / Tooltip 三个层，Window 构造期建好，
//   `Window.cpp` 的 `m_overlayRoot->AddChild(normal/modal/tooltip)`）。托管挂载的
//   遮罩与 sheet 是**直接**挂到浮层根上的（走 `HitTestWindow` 的 `[C]` 分支），
//   ⇒ 期望值 = 3 + 已挂载数。这条恒等式能同时抓住两类缺陷：
//     ① 旧的一套没收（`RebuildUi` 忘了 `Unmount*`）⇒ 多 2；
//     ② `ShowOverlay` 重复注册 ⇒ 某个层里多一个孩子。
//   ★ 「已挂载数」必须**按 `GetParent()` 实测**，不能按「托管指针非空」推断——
//     后者在"已经 AddChild 过但指针还没置空"的中间态会撒谎。
//
// 【时序前提：为什么可以是同步探针】
//   `Window::HideOverlay` 是**立刻**从层里 `RemoveChild` 并移入 `m_pendingDestroyOverlays`
//   （只有**析构**延后到帧末）。所以"孩子数对不对"当场就能读，不必等下一帧。
//   唯一需要跨帧的是「主题切换 → 重建」，但 `RebuildUi()` 本就可以同步调
//   （`--theme-probe` 的 rebuild 相正是这么做的：本函数跑在 tick 回调里，不在派发栈上）。
//
// 【判据逐条，每条都配了一条能让它变红的负对照】
//   ① closed        全关后浮层根只剩三个层；且托管挂载**不**让 `HasActiveOverlay()` 为真。
//      ★ 负对照在 ⑧：`ShowOverlay` 出来的下拉**必须**让 `HasActiveOverlay()` 为真。
//        若这条永远是 false，说明读数本身是瞎的（① 就变成了空真）。
//   ② mount         遮罩与 sheet 的父节点都是浮层根；遮罩盖满客户区；sheet 水平居中。
//      ★ 居中这一条是"版心式算式"的直检：`LayoutSettingsOverlay` 用
//        `(rw - w) / 2`，若哪天有人改成贴左，这里立刻红。
//   ③ modal-blank   点遮罩空白处**不关闭**（用户 2026-10-30 裁决：保持现状语义），
//      且这一击**到不了**顶栏——拿主题钮当探针：`g_themeLight` 不许翻。
//      ★ 这是遮罩「真的挡住」的唯一机读证据。改前遮罩挂在 `m_scroll` 里，
//        点转录区之外的地方会直接命中顶栏按钮（那一版就是这样点到主题钮的）。
//   ④ rebuild       整树重建后 sheet/遮罩**换新令牌**（指针变），而浮层根孩子数**不变**。
//      ★ 这是本探针的主判据：指针变 + 孩子数不变 ⇒ 旧的被收回、新的被挂上，
//        恰好一套。两个读数缺一不可——只看指针会放过"旧的还在"，
//        只看孩子数会放过"根本没重建"。
//   ⑤ theme         切主题 + 重建后，sheet 内文字色跟着变。
//      ★ 这条守的是「重建真的用了新令牌」。`Theme.h` 的令牌是引用别名，颜色在建树时
//        就被拷进 `PanelStyle`；若重建路径没走到浮层，面板会留着旧配色而
//        「面板还在不在」类判据全绿（`RebuildUi` 第 ④ 步的注释记的正是这条）。
//   ⑥ tip           tooltip 的祖先链上有浮层根、且**不在** root 子树里；
//      关掉后句柄立即为空（`HideOverlay` 帧末才析构 ⇒ 句柄必须自己置空）。
//   ⑥b tip-palette  tooltip 的**配色**：两个主题下都必须「深底浅字」（WCAG ≥ 4.5），
//      且两主题的 (底,字) 逐位相同（= 浮层固定视觉，不随主题走）。
//      ★★ 2026-11-04 补（用户报告「深色模式下浮窗文本没改色」）。此前 ⑥ 只问结构，
//        于是 `.background = kDarkInk`（主题令牌）配字面量字色 ⇒ 深色下近白压近白
//        (1.08:1)、字已溶解，而「挂在哪 / 点得穿 / 单例」三条全绿。
//      ★ 两个量具：`TipPanel::Background()` 读控件**实际要画**的底色（`Panel` 无
//        公开 getter ⇒ 派生读 protected `m_style`，`CountingPanel` 同手法）；
//        `GetTextColor()` 读字色。**不比常量**——常量从来没错，错在调用点。
//      ★ 负对照：同一相里用「当年的错误组合」算一次对比度，要求它必须 < 4.5
//        （元断言）。量具坏了会在这里红，而不是在下面假绿。
//   ⑦ tip-through   把 tooltip **盖在主题钮上**再点它：点击必须**穿过**提示框落到钮上。
//      ★ `HitTestWindow` 显式跳过 Tooltip 层。这条判据的判别力来自
//        「`Button::ConsumesMouseInput()==true` 而 `Panel` 不覆盖（默认 false）」——
//        若提示框截留了输入，命中者会是 Panel，钮的回调根本不执行，主题不翻。
//   ⑧ popup         下拉挂在浮层根下、`HasActiveOverlay()` 为真；点外面（Light Dismiss
//      `PassThrough`）必须关掉。
//   ★ 本探针**不**覆盖：ESC 关浮层（托管挂载的模态没注册 entry ⇒ 按设计没有 ESC；
//     下拉的 ESC 属于框架行为，ECDI 自己的 `OverlayTests.cpp` 已覆盖）。
// ============================================================


#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"
#include "UiKit.h"          // ★ 2026-11-04：uikit::TipPanel（读 tooltip 实际底色）

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>            // ★ 2026-11-04：std::pow（WCAG 相对亮度）
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ECDI/Core/Color.h"
#include "ECDI/Widget/Button.h"    // themeToggle / modelChip / settingsMask 的完整类型
#include "ECDI/Widget/Label.h"     // settingsHint（`GetTextColor`）
#include "ECDI/Widget/Panel.h"     // settingsPanel / tooltip / modelPopup
#include "ECDI/Widget/Widget.h"
#include "ECDI/Window/OverlayTypes.h"
#include "ECDI/Window/Window.h"

namespace {

// 从 `w` 沿父链上溯，看能否走到 `target`（`w` 本身算在内）。
//   ★ 用固定步数上限而不是 while(w)：父链成环是框架级 bug，探针不该被它挂死。
bool ReachesAncestor(const ECDI::Widget* w, const ECDI::Widget* target) {
    for (int i = 0; i < 16 && w != nullptr; ++i) {
        if (w == target) return true;
        w = w->GetParent();
    }
    return false;
}

// ── ★★ 2026-11-04：配色读数的两个量具 ────────────────────────────────────
//   ★ 为什么放在**探针**里而不是 `Theme.h`：这是**测量**，不是设计令牌。
//     一旦把 `ContrastRatio` 挪进共享层，就会有人拿它去"自动挑颜色"——
//     那会把视觉裁决交给公式，本仓不要那个（`Theme.h` 的配色全是人定的）。
std::string RgbHex(const ECDI::Color& c) {
    // 8-bit 直觉口径（与 `Color::FromRGBA8` 对称）。钳制在这里做：`Color` 契约是
    //   「构造不 Clamp」，但 **hex 显示**必须先落到 [0,255]，否则 `%02x` 溢出。
    auto to8 = [](float v) {
        const int n = static_cast<int>(v * 255.0f + 0.5f);
        return n < 0 ? 0 : (n > 255 ? 255 : n);
    };
    char b[8];
    std::snprintf(b, sizeof(b), "#%02x%02x%02x", to8(c.r), to8(c.g), to8(c.b));
    return b;
}

// WCAG 2.x 相对亮度 + 对比度。
//   ★ 为什么用它而不是「分量差的平方和」一类：本仓的**表面分离度**判据确实是
//     CIE L* 偏移（记忆条 3），但那是「两个**表面**要不要区分得开」的口径；
//     这里是「**字压在这块面上读不读得出**」——那正是 WCAG 对比度定义的问题域，
//     阈值 4.5 也有公开依据（AA 正文），不是随手拍的数。
//   ★ 它不是 L*：`L*` 判「浮层浮不浮起来」（本缺陷的选值依据，见 `kTooltipBg`），
//     两者**各管一件事**，不要合并成一条判据。
double RelativeLuminance(const ECDI::Color& c) {
    auto lin = [](float v) {
        const double s = (v < 0.0f) ? 0.0 : (v > 1.0f ? 1.0 : static_cast<double>(v));
        return (s <= 0.04045) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

double ContrastRatio(const ECDI::Color& a, const ECDI::Color& b) {
    const double la = RelativeLuminance(a);
    const double lb = RelativeLuminance(b);
    const double hi = (std::max)(la, lb);
    const double lo = (std::min)(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

}  // namespace

void HarnessApp::RunOverlayProbe() {
    if (!m_window) { m_probeState.overlayProbeDone = true; return; }
    // ★ 只在回放模式：④⑤ 要调 `RebuildUi()`，而 live 下转录在持续增长 ⇒
    //   "重建后状态一致"这类比对必然假失败（同 `RunThemeProbe` 的理由）。
    if (!m_sess || !m_sess->replay) { m_probeState.overlayProbeDone = true; return; }
    if (!m_hud.settingsPanel || !m_hud.settingsMask || !m_hud.modelChip || !m_hud.themeToggle) {
        m_probeState.overlayProbeDone = true;
        return;
    }

    ECDI::Widget& root = m_window->GetRootWidget();
    ECDI::Widget& overlayRoot = m_window->GetOverlayRoot();
    const int clientW = root.GetWidth();
    const int clientH = root.GetHeight();

    // 浮层根的直接孩子里，除三个层之外的都算「托管挂载」。
    //   ★ 层数写成常量 3 是**结构事实**（`Window` 构造期建好三个层），不是魔数：
    //     若 ECDI 改了这个数字，① 会立刻红——那正是我们想要的提醒。
    constexpr int kLayerCount = 3;
    auto mountedManaged = [&]() {
        int n = 0;
        if (m_hud.settingsPanel && m_hud.settingsPanel->GetParent() == &overlayRoot) ++n;
        if (m_hud.settingsMask && m_hud.settingsMask->GetParent() == &overlayRoot) ++n;
        return n;
    };
    auto overlayNodes = [&]() { return static_cast<int>(overlayRoot.GetChildCount()); };

    auto snap = [&](const char* phase) {
        OverlayProbeRec rec;
        rec.phase = phase;
        rec.overlayRootChildren = overlayNodes();
        rec.mountedManaged = mountedManaged();
        rec.settingsOnOverlay =
            (m_hud.settingsPanel != nullptr) && (m_hud.settingsPanel->GetParent() == &overlayRoot);
        rec.maskOnOverlay =
            (m_hud.settingsMask != nullptr) && (m_hud.settingsMask->GetParent() == &overlayRoot);
        rec.tipOnOverlay = (m_hud.tooltip != nullptr) &&
                           ReachesAncestor(m_hud.tooltip, &overlayRoot) &&
                           !ReachesAncestor(m_hud.tooltip, &root);
        rec.popupOnOverlay = (m_hud.modelPopup != nullptr) &&
                             ReachesAncestor(m_hud.modelPopup, &overlayRoot) &&
                             !ReachesAncestor(m_hud.modelPopup, &root);
        rec.settingsVisible = m_settingsVisible;
        rec.popupOpen = m_modelPopupOpen;
        rec.tipAlive = (m_hud.tooltip != nullptr);
        rec.anyRegistered = m_window->HasActiveOverlay();
        // ★★ 2026-11-04：tooltip 配色读数。两半都必须读**控件实际用于绘制的字段**：
        //   · 底色走 `uikit::TipPanel::Background()`（= `Panel::m_style.background.value`，
        //     `Panel::OnPaint` 取的就是它）；
        //   · 字色走 `TextWidget::GetTextColor()`（公开；`SetTextColor` 内部转发到
        //     同一个 `TextStyle::foreground`，所以这条读的就是绘制值，不是副本）。
        //   ★ 不用 `dynamic_cast` 到 `TipPanel` 失败就静默跳过：那会让判据退化成
        //     空真（控件类型换了、断言全绿）。读不到就是缺陷，照实记。
        if (m_hud.tooltip) {
            const ECDI::Color* bg = nullptr;
            if (auto* tp = dynamic_cast<const uikit::TipPanel*>(m_hud.tooltip)) {
                bg = &tp->Background();
            }
            if (m_hud.tooltipText) {
                rec.tipInk = RgbHex(m_hud.tooltipText->GetTextColor());
            }
            if (bg) {
                rec.tipBg = RgbHex(*bg);
                if (m_hud.tooltipText) {
                    rec.tipContrast = ContrastRatio(*bg, m_hud.tooltipText->GetTextColor());
                }
            }
        }
        rec.sheetX = m_hud.settingsPanel ? static_cast<int>(m_hud.settingsPanel->GetX()) : 0;
        rec.sheetW = m_hud.settingsPanel ? static_cast<int>(m_hud.settingsPanel->GetWidth()) : 0;
        rec.clientW = clientW;
        // ★ 2026-11-01：几何读数（-1 = 控件不存在，与真实的 0 区分）。
        if (m_hud.modelPopup) {
            const ECDI::Point p = m_hud.modelPopup->GetAbsolutePosition();
            rec.popupX = static_cast<int>(p.x);
            rec.popupY = static_cast<int>(p.y);
            rec.popupW = m_hud.modelPopup->GetWidth();
            rec.popupH = m_hud.modelPopup->GetHeight();
        }
        if (m_hud.modelChip) {
            const ECDI::Point c = m_hud.modelChip->GetAbsolutePosition();
            rec.chipX = static_cast<int>(c.x);
            rec.chipY = static_cast<int>(c.y);
            rec.chipW = m_hud.modelChip->GetWidth();
            rec.chipH = m_hud.modelChip->GetHeight();
        }
        m_probeState.overlayProbeLog.push_back(std::move(rec));
    };
    auto expect = [&](const char* phase,
                      std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        OverlayProbeRec& rec = m_probeState.overlayProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;

        // ★ 临时诊断（`HARNESS_PROBE_TRACE=<file>`）：崩溃时定位停在哪个相位。
        //   探针摘要只在 Exit 时写一次 ⇒ 崩了连"跑到哪"都不知道。
        if (const char* tracePath = std::getenv("HARNESS_PROBE_TRACE")) {
            if (std::FILE* f = std::fopen(tracePath, "a")) {
                std::fprintf(f, "%s\n", phase);
                std::fclose(f);
            }
        }
    };

    // 焦点守卫（同 `RunMenuProbe` / `RunSidebarProbe` 的收尾纪律）：合成点击会把焦点
    //   交给被点的 Button ⇒ caret 闪烁重绘多画几帧 ⇒ paints/frames 被这份探针污染。
    //   ★★ 2026-10-31：**不在开头捕获焦点指针**。本探针有三处 `RebuildUi()`
    //     （`rebuild` / `theme` / `popup-rebuild`），它会把 root 整棵子树拆掉重建
    //     ⇒ 开头捕获的裸指针到收尾时已经悬垂。收尾改为「现取」，见函数末尾。
    auto clickCentreOf = [&](ECDI::Widget* w) -> bool {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + static_cast<int>(w->GetWidth()) / 2,
                          static_cast<int>(abs.y) + static_cast<int>(w->GetHeight()) / 2);
        return true;
    };

    // ── ⓪ 清场：本探针要求从"全关"出发（启动时可能因首启自动弹了设置） ──
    //   ★ 收尾也要回到这里（单变量纪律），否则 ⑧ 的 `HasActiveOverlay()` 读数
    //     会带上前面几相的残留。
    SetModelPopupOpen(false);
    HideTooltip();
    SetSettingsVisible(false);

    // ── ① closed：托管挂载不注册，浮层根只剩三个层 ──────────────────────
    //   ★ 期望值用 `3 + mountedManaged` 而不是硬编码 3：`ApplySettingsVisibility`
    //     关闭时**只 SetVisible(false)、不收回**（收回会清焦点 ⇒ tick 停摆），
    //     所以关掉之后遮罩/sheet 仍挂在浮层根上。这条恒等式对"挂着但不可见"
    //     是宽容的，对"多挂了一套"是不宽容的——正是我们要的分辨率。
    expect("closed", {{overlayNodes() == kLayerCount + mountedManaged(),
                       "overlay root has extra nodes while everything is closed"},
                      {!m_window->HasActiveOverlay(),
                       "a managed mount must not register as an active overlay"},
                      {!m_settingsVisible, "settings still visible after close"}});

    // ── ② mount：遮罩与 sheet 都挂到浮层根上，遮罩盖满、sheet 居中 ────────
    SetSettingsVisible(true);
    expect("mount", {{m_hud.settingsPanel->GetParent() == &overlayRoot,
                      "settings sheet is not a child of the overlay root"},
                     {m_hud.settingsMask->GetParent() == &overlayRoot,
                      "settings mask is not a child of the overlay root"},
                     {overlayNodes() == kLayerCount + 2,
                      "expected exactly one mask + one sheet on the overlay root"},
                     {m_settingsVisible, "settings model says hidden right after opening"},
                     // 遮罩必须盖满客户区——否则顶栏/底栏的按钮还能点到（③ 直检）
                     {static_cast<int>(m_hud.settingsMask->GetWidth()) == clientW &&
                          static_cast<int>(m_hud.settingsMask->GetHeight()) == clientH,
                      "settings mask does not cover the whole client area"}});
    {
        // 居中：`(clientW - sheetW) / 2`。单独判，失败时把三个数写进差异里。
        OverlayProbeRec& rec = m_probeState.overlayProbeLog.back();
        const int want = (clientW - rec.sheetW) / 2;
        if (rec.sheetX != want) {
            rec.matched = false;
            if (!rec.firstDivergence.empty()) rec.firstDivergence += "; ";
            rec.firstDivergence += "settings sheet not centred [x=" + std::to_string(rec.sheetX) +
                                   " want=" + std::to_string(want) +
                                   " w=" + std::to_string(rec.sheetW) + "]";
        }
    }

    // ── ③ modal-blank：点遮罩空白处不关闭，且这一击到不了顶栏 ─────────────
    //   ★ 判别力来自「主题钮在遮罩**下面**」：合成点击走真实 `HitTestWindow`，
    //     若遮罩没盖住/没挡住，命中者就是主题钮，它的回调会同步翻 `g_themeLight`。
    //     ★ 主题钮的回调只置 `m_uiRebuildPending`（重建在下一 tick），但
    //       `SwitchTheme()` 是**同步**的 ⇒ 这个读数不需要等下一帧。
    {
        const bool lightBefore = harness::g_themeLight;
        const ECDI::Point t = m_hud.themeToggle->GetAbsolutePosition();
        const int tx = static_cast<int>(t.x) + static_cast<int>(m_hud.themeToggle->GetWidth()) / 2;
        const int ty = static_cast<int>(t.y) + static_cast<int>(m_hud.themeToggle->GetHeight()) / 2;
        SynthesizeClickAt(tx, ty);
        const bool lightAfter = harness::g_themeLight;
        // 万一真的穿过去了（判据要红），把主题翻回来，别把状态留给后面的相。
        if (lightAfter != lightBefore) {
            harness::SwitchTheme(lightBefore);
            m_uiRebuildPending = false;
        }
        expect("modal-blank", {{m_settingsVisible,
                                "clicking the mask closed the settings panel (decision: it must stay)"},
                               {lightAfter == lightBefore,
                                "the click reached the top bar through the modal mask"},
                               {overlayNodes() == kLayerCount + 2,
                                "clicking the mask changed the overlay root children"}});
    }

    // ── ④ rebuild：整树重建后换新令牌，孩子数不变 ────────────────────────
    //   ★ 主判据。指针变 + 孩子数不变 = 旧的被收回、新的被挂上，恰好一套。
    //     两个读数缺一不可：只看指针会放过"旧的还在"，只看孩子数会放过"没重建"。
    {
        ECDI::Widget* const oldSheet = m_hud.settingsPanel;
        ECDI::Widget* const oldMask = m_hud.settingsMask;
        const int before = overlayNodes();
        RebuildUi();
        expect("rebuild", {{m_hud.settingsPanel != oldSheet,
                            "the settings sheet widget was not rebuilt (stale theme tokens)"},
                           {m_hud.settingsMask != oldMask,
                            "the settings mask widget was not rebuilt"},
                           {m_hud.settingsPanel->GetParent() == &overlayRoot,
                            "rebuilt sheet is not on the overlay root"},
                           {overlayNodes() == before,
                            "overlay root gained or lost children across RebuildUi "
                            "(the old overlay was probably never unmounted)"},
                           {m_settingsVisible,
                            "settings did not survive the rebuild"}});
    }

    // ── ⑤ theme：切主题 + 重建后，浮层内的文字色跟着变 ────────────────────
    //   ★ 守的是「重建真的用了新令牌」。`Theme.h` 的令牌是引用别名，颜色在建树时
    //     就拷进了各控件的 StyleField；若重建没走到浮层，面板会留着旧配色，
    //     而"面板还在不在"类判据全绿——这条正是那种假绿的解药。
    {
        const bool lightBefore = harness::g_themeLight;
        // `kDarkMuted`：深色 (154,162,177) / 浅色 (107,112,120) ⇒ 两套必然不同。
        const ECDI::Color inkBefore = m_hud.settingsHint->GetTextColor();
        harness::SwitchTheme(!lightBefore);
        RebuildUi();
        const ECDI::Color inkAfter = m_hud.settingsHint->GetTextColor();
        // 复原主题（不重建：本进程马上 Exit，且摘要只读 `m_probeState`）
        harness::SwitchTheme(lightBefore);
        m_uiRebuildPending = false;
        expect("theme", {{inkAfter != inkBefore,
                          "settings sheet kept the old ink after a theme switch "
                          "(the rebuild never reached the overlay)"},
                         {overlayNodes() == kLayerCount + 2,
                          "theme rebuild left a stale overlay on the root"},
                         {m_hud.settingsPanel->GetParent() == &overlayRoot,
                          "themed sheet is not on the overlay root"}});
    }

    // ── ⑥ tip：tooltip 在浮层根下、不在 root 子树里；关掉后句柄立即为空 ────
    {
        // ★ 先关设置：⑥⑦ 都在测"提示框与底下的控件"，而遮罩盖满客户区 ⇒
        //   留着它，⑦ 的点击会命中遮罩而不是主题钮，那一相就成了空真。
        //   （关闭**不收回**，所以遮罩/sheet 仍挂在浮层根上——`overlayNodes` 不变。）
        SetSettingsVisible(false);
        ShowTooltipFor(m_hud.themeToggle, "overlay-probe");
        OnTooltipFire();
        expect("tip", {{m_hud.tooltip != nullptr, "tooltip was not created"},
                       {m_hud.tooltip && ReachesAncestor(m_hud.tooltip, &overlayRoot),
                        "tooltip is not on the overlay root"},
                       {m_hud.tooltip && !ReachesAncestor(m_hud.tooltip, &root),
                        "tooltip is inside the root subtree (it would be clipped by the viewport)"},
                       {m_window->HasActiveOverlay(ECDI::OverlayLayer::Tooltip),
                        "tooltip layer does not report an active overlay"}});
    }

    // ── ⑥b tip-palette：**配色**在两个主题下都必须「深底浅字」 ────────────
    //   ★★ 2026-11-04（用户报告「深色模式下浮窗文本没改色」之后补）。此前 ⑥ 只有
    //     结构断言，于是这个缺陷躲了 10 天：深色下底色 `kDarkInk` = (230,233,239)
    //     近白、字 `kTooltipInk` = (240,241,245) ⇒ 对比 **1.08:1**，字已溶解，
    //     而「挂在哪 / 点得穿 / 单例」三条全绿。
    //   ★ 为什么必须**两个主题各测一次**：病灶是「底借了主题令牌、字是字面量」，
    //     单测一个主题会漏——浅色下同一份代码读出 13.40:1（完全正常）。
    //     在浅色跑，这条判据对原缺陷**是绿的**，那就等于没写。
    //   ★ 为什么不用「比对 kTooltipBg 常量」：常量本身从来没错，错在**调用点**
    //     把 `.background` 写成了 `kDarkInk`。只比对常量 = 恒真断言。
    //     故这里读的是控件实际要画的字段（`TipPanel::Background` / `GetTextColor`）。
    {
        const bool lightBefore = harness::g_themeLight;
        // 只切令牌、**不重建**：tooltip 每次 `OnTooltipFire` 现建现挂 ⇒ 它天然
        //   会用"当下"的令牌。这正是缺陷的复现路径——若底/字仍借主题令牌，
        //   两个主题读出的值必然不同。
        auto paletteIn = [&](bool light, ECDI::Color& bg, ECDI::Color& ink, double& cr) {
            harness::SwitchTheme(light);
            m_uiRebuildPending = false;   // 切令牌不排队重建（本相不需要，见上）
            ShowTooltipFor(m_hud.themeToggle, "overlay-probe");
            OnTooltipFire();
            bg = ECDI::Color::Black();
            ink = ECDI::Color::White();
            cr = 0.0;
            if (auto* tp = dynamic_cast<uikit::TipPanel*>(m_hud.tooltip)) {
                bg = tp->Background();
            }
            if (m_hud.tooltipText) {
                ink = m_hud.tooltipText->GetTextColor();
                cr = ContrastRatio(bg, ink);
            }
        };

        ECDI::Color darkBg, darkInk, lightBg, lightInk;
        double darkCr = 0.0, lightCr = 0.0;
        paletteIn(false, darkBg, darkInk, darkCr);
        // ★ 负对照的原料：缺陷版底色 = 深色主题的 `darkInk` 令牌（原代码就写了它）。
        //   ★ 必须在**深色**档下取，取完再切浅色——否则拿到的是浅色值，负对照失效。
        const ECDI::Color buggyBg = harness::g_theme.darkInk;
        const ECDI::Color buggyInk = darkInk;
        const double buggyCr = ContrastRatio(buggyBg, buggyInk);
        paletteIn(true, lightBg, lightInk, lightCr);

        // 复原主题 + 重新竖一条提示框（⑦ 要用，且它必须是"原始主题"下的那条）
        harness::SwitchTheme(lightBefore);
        m_uiRebuildPending = false;
        ShowTooltipFor(m_hud.themeToggle, "overlay-probe");
        OnTooltipFire();

        // ★ 元断言：量具本身有分辨力。这条把「读数坏了」与「配色坏了」分开——
        //   若 `ContrastRatio` 被改成恒返回 10，本相会在这里红，而不是在下面假绿。
        //   同时它也是这个缺陷的**回归见证**：当年的组合必须判为不可读。
        const bool meterHasPower = buggyCr < 4.5;
        // ★ 两个主题下 (底,字) 逐位相同 ⇒ 配色不随主题走（设计裁决）。
        const bool invariant = (RgbHex(darkBg) == RgbHex(lightBg)) &&
                               (RgbHex(darkInk) == RgbHex(lightInk));

        expect("tip-palette",
               {{meterHasPower,
                 "the contrast meter has no discriminating power "
                 "(the known-bad pair was not judged unreadable)"},
                // 两个主题各判一次：4.5 = WCAG AA 正文下限
                {darkCr >= 4.5, "tooltip is unreadable in the DARK theme (bg/ink too close)"},
                {lightCr >= 4.5, "tooltip is unreadable in the LIGHT theme (bg/ink too close)"},
                // ★ 设计裁决：tooltip 是浮层系统的**固定视觉**，不随主题走。
                //   这条守的是「别再把底色接回主题令牌」——病灶的根。
                {invariant,
                 "tooltip palette follows the theme "
                 "(the background was wired to a theme token again)"}});

        // 回写读数（`expect` 已 push 本相记录，这里取到的才是它）。
        //   ★ 记**深色**那一档：那是缺陷被报告的场景，也是本相的主场。
        OverlayProbeRec& rec = m_probeState.overlayProbeLog.back();
        rec.tipBg = RgbHex(darkBg);
        rec.tipInk = RgbHex(darkInk);
        rec.tipContrast = darkCr;
        rec.tipInvariant = invariant;
        // 失败时把读数写进差异串（对比度是连续量，红的时候要知道差多少）
        if (!rec.matched) {
            rec.firstDivergence += " [dark " + RgbHex(darkBg) + " on " + RgbHex(darkInk) +
                                   " cr=" + std::to_string(static_cast<int>(darkCr * 100)) + "%" +
                                   " light " + RgbHex(lightBg) + " on " + RgbHex(lightInk) +
                                   " cr=" + std::to_string(static_cast<int>(lightCr * 100)) + "%" +
                                   " buggy cr=" + std::to_string(static_cast<int>(buggyCr * 100)) +
                                   "%]";
        }
    }

    // ── ⑦ tip-through：把提示框盖在主题钮上，点击必须穿过它 ────────────────
    //   ★ 判别力：`Button::ConsumesMouseInput()==true`，而 `Panel` 不覆盖（默认 false）。
    //     若提示框截留了输入，命中者会是 Panel，钮的回调不执行 ⇒ 主题不翻 ⇒ 本相红。
    {
        const bool lightBefore = harness::g_themeLight;
        const ECDI::Point t = m_hud.themeToggle->GetAbsolutePosition();
        const int tw = static_cast<int>(m_hud.themeToggle->GetWidth());
        const int th = static_cast<int>(m_hud.themeToggle->GetHeight());
        bool positioned = false;
        if (m_hud.tooltip) {
            // 把提示框搬到钮上（探针自己动它，只为造这一相的前提）
            m_hud.tooltip->SetPosition(static_cast<int>(t.x) - 2, static_cast<int>(t.y) - 2);
            m_hud.tooltip->SetSize(tw + 4, th + 4);
            positioned = true;
        }
        SynthesizeClickAt(static_cast<int>(t.x) + tw / 2, static_cast<int>(t.y) + th / 2);
        const bool lightAfter = harness::g_themeLight;
        if (lightAfter != lightBefore) {
            harness::SwitchTheme(lightBefore);
            m_uiRebuildPending = false;
        }
        expect("tip-through", {{positioned, "no tooltip to place over the anchor button"},
                               {lightAfter != lightBefore,
                                "the click did not pass through the tooltip "
                                "(the tooltip layer swallowed it)"}});
    }

    // ── ⑧ popup：下拉在浮层根下且注册；点外面（PassThrough）必须关掉 ────────
    {
        HideTooltip();          // 先收提示框：⑧ 的 `HasActiveOverlay()` 只该看到下拉
        SetModelPopupOpen(true);
        const bool openOk = m_modelPopupOpen && m_hud.modelPopup != nullptr;
        // ★★ 2026-11-01（用户反馈「模型胶囊点开的位置不对」）：位置判据。
        //   此前本探针对位置**一条断言都没有**——只记「开没开」「挂在哪」，于是弹窗
        //   能被定位到转录视口正中（实测横向偏左 ~170px、纵向偏下 ~110px）而 10/10 全绿。
        //   三条判据 + 一条元断言：
        //     元断言（尺寸 == 规格）先把「几何根本没算」与「算错了」分开——
        //     否则 0==0 也能让对齐判据通过。
        //     ① 右缘对齐 chip 右缘（±1，容浮点取整）
        //     ② 顶边不高于 chip 下沿（不许压住顶栏）
        //     ③ chip 下沿到弹窗顶的间隙 ≤ 规格值 +1
        int popL = 0, popT = 0, popW = 0, popH = 0;
        int chipL = 0, chipT = 0, chipW = 0, chipH = 0;
        if (m_hud.modelPopup) {
            const ECDI::Point p = m_hud.modelPopup->GetAbsolutePosition();
            popL = static_cast<int>(p.x);
            popT = static_cast<int>(p.y);
            popW = m_hud.modelPopup->GetWidth();
            popH = m_hud.modelPopup->GetHeight();
        }
        if (m_hud.modelChip) {
            const ECDI::Point c = m_hud.modelChip->GetAbsolutePosition();
            chipL = static_cast<int>(c.x);
            chipT = static_cast<int>(c.y);
            chipW = m_hud.modelChip->GetWidth();
            chipH = m_hud.modelChip->GetHeight();
        }
        const int popR = popL + popW;
        const int chipR = chipL + chipW;
        const int chipB = chipT + chipH;
        expect("popup", {{openOk, "model popup did not open"},
                         {m_hud.modelPopup && ReachesAncestor(m_hud.modelPopup, &overlayRoot),
                          "model popup is not on the overlay root"},
                         {m_hud.modelPopup && !ReachesAncestor(m_hud.modelPopup, &root),
                          "model popup is inside the root subtree"},
                         // ★ 元断言：几何真的算过（尺寸 == 规格），才谈得上对齐。
                         {popW == harness::internal::kModelPopupW &&
                              popH == harness::internal::kModelPopupH,
                          "popup size != spec (geometry never laid out?)"},
                         {std::abs(popR - chipR) <= 1,
                          "popup right edge is not aligned with the model chip's right edge"},
                         {popT >= chipB,
                          "popup top overlaps the top bar (it must hang below the chip)"},
                         {popT - chipB <= harness::internal::kModelPopupDropGap + 1,
                          "popup hangs too far below the model chip"},
                         // ★ 负对照：注册式浮层**必须**让这个读数为真——否则 ① 里
                         //   "托管挂载不注册"那条就成了空真（读数恒 false 时它也通过）。
                         {m_window->HasActiveOverlay(),
                          "ShowOverlay did not register an active overlay "
                          "(so the closed-phase reading proves nothing)"},
                         // ★ 注册式浮层进的是**层**，不是浮层根的直接孩子——
                         //   这一条把「托管挂载」与「注册式浮层」两种挂法分开钉住。
                         //   若哪天有人把下拉改成直接挂浮层根，它会立刻红（并顺带
                         //   说明"为什么它没有 ESC / 点外关闭"）。
                         {overlayNodes() == kLayerCount + mountedManaged(),
                          "the popup was attached directly to the overlay root "
                          "instead of into the normal layer"}});
    }
    {
        // ── ⑧b popup-rebuild：整树重建时下拉必须被收回、并按新令牌重建 ──────
        //   ★ 2026-10-31（审计整改批 A 复核新增）。守的是 `RebuildUi` 的 ⓪b：
        //     下拉挂在浮层根上（Normal 层），拆 root 子树碰不到它 ⇒ 不显式收回时
        //     旧下拉留在屏幕上（旧令牌），且 `exemptWidget` 指向的 chip 已被析构
        //     ⇒ 框架条目里留下悬垂指针（下一次点外 = UAF 读）。
        //   ★ 判据三件套：模型位还在（没被吞）· 控件换了（真重建）· 孩子数不变
        //     （旧的确实被收回，没多出一套）。
        ECDI::Widget* const oldPopup = m_hud.modelPopup;
        RebuildUi();
        //   ★ 为什么**不用** `overlayNodes()` 判「旧的没留下」：下拉进的是 Normal **层**，
        //     不是浮层根的直接孩子 ⇒ 孩子数对它完全无感（多一套也读 3）。
        //     `popup` 相那条 `kLayerCount + mountedManaged()` 只证明"没有直接挂浮层根"。
        //   ★ 判「旧的真被摘掉」只能看它的父指针：`HideOverlay` 立刻摘树、帧末才析构，
        //     而本相与 `RebuildUi()` 之间没有帧泵 ⇒ 指针仍然有效（拆完父指针为 nullptr）。
        //     ★ 反过来若 ⓪b 缺失，`BuildUi` 末尾的投影会因「位真、句柄也非空」直接
        //     幂等返回 ⇒ `m_hud.modelPopup == oldPopup`，第二条判据立刻红。
        expect("popup-rebuild",
               {{m_modelPopupOpen,
                 "the model bit was swallowed by the rebuild "
                 "(HideOverlay fires onClosed; the bit must be restored before BuildUi)"},
                {m_hud.modelPopup != nullptr && m_hud.modelPopup != oldPopup,
                 "the model popup widget was not rebuilt (it would keep the old theme tokens)"},
                {m_hud.modelPopup && ReachesAncestor(m_hud.modelPopup, &overlayRoot),
                 "rebuilt popup is not on the overlay root"},
                {oldPopup == nullptr || oldPopup->GetParent() == nullptr,
                 "the old popup is still attached after RebuildUi "
                 "(two popups on screen, the stale one keeps the old tokens)"},
                {overlayNodes() == kLayerCount + mountedManaged(),
                 "overlay root children no longer match [3 layers + mounted managed overlays]"},
                {m_window->HasActiveOverlay(ECDI::OverlayLayer::Normal),
                 "rebuilt popup is not registered (it would lose light dismiss)"}});
    }
    {
        // 点外面。★ 坐标选客户区左上角 (2,2)：顶栏最左侧是纯显示 Label，
        //   点它不会触发任何回调（也不会拖窗——合成事件不经过平台层）。
        SynthesizeClickAt(2, 2);
        expect("popup-dismiss", {{!m_modelPopupOpen,
                                  "clicking outside did not close the model popup "
                                  "(LightDismiss PassThrough)"},
                                 {m_hud.modelPopup == nullptr,
                                  "popup handle not cleared after light dismiss "
                                  "(HideOverlay destroys at frame end)"},
                                 {!m_window->HasActiveOverlay(),
                                  "overlay still registered after light dismiss"}});
    }

    // ── 收尾：回到 ⓪ 的全关状态，还回焦点（探针不许把状态留在屏上） ──────
    SetModelPopupOpen(false);
    HideTooltip();
    SetSettingsVisible(false);
    //   ★★ 2026-10-31（实测抓到的崩溃：40 次跑 21 次 0xC0000005，全部停在
    //     `popup-dismiss` 之后）：焦点**必须现取**，不能用在开头捕获的指针。
    //     本探针中途调了三次 `RebuildUi()`，root 子树整棵换过 ⇒ 那个指针早已悬垂，
    //     `SetFocusedWidget(悬垂)` 是一次 UAF 写（内部还要 `Invalidate()`，即虚调用）。
    //     `m_promptBar` 是重建后的新控件、`input()` 一定活着，而它也正好是
    //     `RebuildUi` 自己交还焦点的同一个目标（`AppUiChrome.cpp` 第 ⑨ 步）。
    if (m_window) {
        m_window->SetFocusedWidget((m_promptBar && m_promptBar->input())
                                       ? static_cast<ECDI::Widget*>(m_promptBar->input())
                                       : nullptr);
    }

    m_probeState.overlayProbeDone = true;
    if (m_application) m_application->Exit();
}

// ── 摘要段 ────────────────────────────────────────────────────────────────
void HarnessApp::AppendOverlayProbeSummary(std::string& json) {
    json += "  \"overlayProbe\": [";
    // 本文件私有的转义（与其它探针同族：各自作用域内一份，不跨 TU 共享——
    //   三份既有转义 lambda 的**行为不同**，合并会改输出，见 Stage 4 教训②）。
    //   ★ 差异串里**不许出现双引号**：`firstDivergence` 走这里，而这里转义了引号，
    //     但历史上有探针用了不转义的 `escS` 把整份 JSON 弄坏过 ⇒ 文案纪律：
    //     用方括号，别用引号。
    auto escO = [](const std::string& in) {
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
    for (size_t i = 0; i < m_probeState.overlayProbeLog.size(); ++i) {
        const OverlayProbeRec& r = m_probeState.overlayProbeLog[i];
        json += (i ? ", " : "");
        char b[1024];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"overlayRootChildren\": %d, "
                      "\"mountedManaged\": %d, \"settingsOnOverlay\": %s, "
                      "\"maskOnOverlay\": %s, \"tipOnOverlay\": %s, \"popupOnOverlay\": %s, "
                      "\"settingsVisible\": %s, \"popupOpen\": %s, \"tipAlive\": %s, "
                      "\"anyRegistered\": %s, \"sheetX\": %d, \"sheetW\": %d, "
                      "\"clientW\": %d, "
                      "\"popupX\": %d, \"popupY\": %d, \"popupW\": %d, \"popupH\": %d, "
                      "\"chipX\": %d, \"chipY\": %d, \"chipW\": %d, \"chipH\": %d, "
                      // ★ 2026-11-04：tooltip 配色读数（空串 = 该相没有提示框）
                      "\"tipBg\": \"%s\", \"tipInk\": \"%s\", \"tipContrast\": %.2f, "
                      "\"tipInvariant\": %s, "
                      "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                      r.phase.c_str(), r.overlayRootChildren, r.mountedManaged,
                      r.settingsOnOverlay ? "true" : "false",
                      r.maskOnOverlay ? "true" : "false",
                      r.tipOnOverlay ? "true" : "false",
                      r.popupOnOverlay ? "true" : "false",
                      r.settingsVisible ? "true" : "false",
                      r.popupOpen ? "true" : "false",
                      r.tipAlive ? "true" : "false",
                      r.anyRegistered ? "true" : "false",
                      r.sheetX, r.sheetW, r.clientW,
                      r.popupX, r.popupY, r.popupW, r.popupH,
                      r.chipX, r.chipY, r.chipW, r.chipH,
                      escO(r.tipBg).c_str(), escO(r.tipInk).c_str(), r.tipContrast,
                      r.tipInvariant ? "true" : "false",
                      r.matched ? "true" : "false", escO(r.firstDivergence).c_str());
        json += b;
    }
    json += "],\n";
}
