// ============================================================
// 布局与 HUD：根布局 / 贴底跟随 / 跳转按钮 / tooltip / 右侧检查器
// ------------------------------------------------------------
// 为什么单独一个文件：`Relayout`（184 行）+ `RefreshHud`（350 行）是**唯一的
//   摆位与读数出口**——三棵树（侧栏/中栏/检查器）的尺寸、浮层显隐、贴底跟随、
//   跳转按钮、HUD 文本全在这里结算。它是"改界面就要动"的文件，所以必须
//   和"建界面"（1e 的 builder）分开：一个是摆，一个是建。
// ★ 纪律：本文件**不改树结构**（不 AddChild/RemoveChild，唯一例外是
//   RebuildProjectList——它是 HUD 自己那棵小列表的全部所有者）。
//   ★★ 2026-10-30（浮层迁移）追加一个例外：`OnTooltipFire` / `HideTooltipOverlay`
//   经 `Window::ShowOverlay` / `HideOverlay` 改**浮层树**（不是 root 子树，
//   且唯一入口是 Window 的公共 API）——见该函数头注。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1d）。函数体逐字节相同。
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

#include "ECDI/Platform/PlatformWindow.h"   // 同上：Relayout 里设标题/定时器
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Window/OverlayTypes.h"       // ★ 2026-10-30：tooltip 走浮层根（OverlayOptions）
#include "ECDI/Window/Window.h"             // ★ 同上：ShowOverlay / HideOverlay / GetOverlayRoot
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/ProgressBar.h"
#include "ECDI/Widget/RingProgressBar.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/ScrollView.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// 跨 TU 共享符号（定义见 AppInternal.h / UiCommon.cpp）
using harness::internal::ContentWidth;
using harness::internal::ConversationTitle;
using harness::internal::kAgentGutterW;
using harness::internal::kCanvasPadX;
using harness::internal::kHeaderH;
using harness::internal::kJumpBtnH;
using harness::internal::kJumpBtnW;
using harness::internal::kListRowHeight;
using harness::internal::kPinThreshold;
using harness::internal::kRowGap;
using harness::internal::kRowPadX;
using harness::internal::kProposalLineH;
using harness::internal::kScrollBarThickness;
using harness::internal::kSidebarListW;
using harness::internal::kSidebarMaxRows;
using harness::internal::kTooltipDelayMs;
using harness::internal::kTooltipInk;  // ★ 2026-10-30：tooltip 现建现挂（见 OnTooltipFire）
using harness::internal::kTooltipBg;   // ★ 2026-11-04：底色同样必须是固定字面量（别再借主题令牌）
using harness::internal::kTranscriptWMax;
using harness::internal::kTranscriptWMin;

// ★★ 2026-10-27：版心**可视带宽**的唯一计算口（下面两个函数都读它）。
//   ★ 为什么不用 `ContentWidth(m_scroll)`：它是 `scrollW - 40`，那个 40 是
//     「滚动条 + 右侧内边距」的**单侧**扣除（UiCommon.cpp:31）⇒ 左 16 / 右 24，
//     且不随主题联动（条厚一变，差就变，没有任何断言会红）。
//     判据改成正面的形式：**内容列到左右可视边界的距离相等**。
//   ★★ 恒扣条厚，**不看 `IsVisible()`**。这不是保守取值，是断一个回环：
//        条可见性 ← 内容高（是否需要滚动）
//        内容高   ← 换行结果
//        换行结果 ← 版心宽
//        版心宽   ← 条可见性        （上一版：条可见才扣）
//     ⇒ 同一个窗口尺寸存在**两个自洽解**（条隐藏 / 条可见各一个）。
//       `--resize-probe` 的 roundtrip 相实测抓到：同一尺寸下通栏行 626 → 622
//       （正好一个条厚），"回到原尺寸"却回不到原布局——这正是"布局不确定"。
//     恒扣之后版心宽与条状态无关，回环断开，同一尺寸只有一个解。
//   ★ 代价（诚实写出来）：条**隐藏**时右侧多 4px（`kScrollBarThickness`）的
//     不可见留白。上一版为了省这 4px 选择了回环，而回环的代价是布局不确定；
//     4px 与旧版那 40px 的单侧扣除不是一个量级，取确定性。
int HarnessApp::TranscriptAvailWidth() const {
    if (!m_scroll) return 600;
    return std::max(160, m_scroll->GetWidth() - kScrollBarThickness);
}

// ★★ 2026-10-27：转录列**版心宽**的唯一计算口（声明注释见 App.h）。
//   ★ 模型 = 参考载体的**分级** max-width（2026-11-03）：
//     `min(可用宽, clamp(可用宽 × 72%, kTranscriptWMin, kTranscriptWMax))`。
//       · 窗口窄 ⇒ 占满（不再像上一版那样平白扣掉检视栏的 230px）；
//       · 中段 ⇒ 72% 渐宽；宽屏 ⇒ 钉在硬顶，余量由 `TranscriptCanvasX()` 左右平分。
//   ★ 不变性：可用宽 ≤ 1056（= 760/0.72）时与旧式 `min(可用宽, 760)` **逐位相同**
//     ——原校准点下的既有探针读数因此不动（两常量的来历见 `AppInternal.h`）。
//   ★ 性能含义（必须写明，别让人以为零成本）：检视栏开/关会改变**可用宽**，
//     因而在"版心未顶到硬顶"的窗口里，版心宽会真的变化 ⇒ 行内换行缓存失效 ⇒
//     一次全文档重测。只有窗口宽到「可用宽仍 ≥ 硬顶」时才免疫（侧栏 200 +
//     检视栏收起时 ≈ 1285px 以上的窗口）。这是"内容占满"必须付的账——上一版
//     用"永远扣 230"换零重测，代价是小窗口里永远窄一条，观感上更糟。
int HarnessApp::TranscriptLayoutWidth() const {
    const int avail = TranscriptAvailWidth();
    return std::min(avail, std::clamp(avail * 72 / 100, kTranscriptWMin, kTranscriptWMax));
}

// ★★ 2026-10-27：版心左偏移的唯一计算口（声明注释见 App.h）。
//   ★ 为什么"只放右侧"不行：检视栏收起后视口比版心宽 230，若把余量全丢给右边，
//     正文就贴着侧栏、右侧空一大口——**观感是布局坏了，不是留白**（实测反馈）。
//   ★★ 二改（2026-10-27，探针抓到）：上一版算的是
//       `x = pad + (可用宽 − 版心宽)/2`，而**行宽 = 版心宽 − 2×pad**
//     ⇒ 行在"版心"里右贴 2×pad、左贴 0 ⇒ 实测左 16 / 右 48（差 32 = 2×pad）。
//     注释写着"两侧严格相等"，算出来的不是。这版直接按**行宽**居中：
//       `x = (scrollW − 行宽)/2` ⇒ 左右恒等（含「占满」档：18/18）。
//   ★ 为什么除以 scrollW 而不是可视带宽：条是 4px 的**覆盖层**（贴右缘、不参与
//     行排版），把它算进分母会把整列左推 2px，反而破坏对称。行右边界与条之间
//     始终留有 `pad − 条厚/2` 的空隙（占满档实测 14px），不会压到条上。
int HarnessApp::TranscriptCanvasX() const {
    if (!m_scroll) return kCanvasPadX;
    const int rowW = TranscriptLayoutWidth() - 2 * kCanvasPadX;
    return std::max(0, (m_scroll->GetWidth() - rowW) / 2);
}

void HarnessApp::SyncTurnCopyButtons() {
    if (!m_sess) return;
    const size_t n = m_sess->rows.size();
    size_t i = 0;
    while (i < n) {
        if (m_sess->rows[i].alignRight) {
            ++i;
        }
        size_t turnEnd = i;
        bool turnHasStreamingBlock = false;
        size_t lastEndedAssistantIdx = static_cast<size_t>(-1);
        while (turnEnd < n && !m_sess->rows[turnEnd].alignRight) {
            RowRec& r = m_sess->rows[turnEnd];
            if (!r.blockId.empty()) {
                if (!r.ended) {
                    turnHasStreamingBlock = true;
                } else if (r.copyBtn) {
                    if (const Block* b = m_sess->transcript.FindBlock(r.blockId)) {
                        if (!b->ended) {
                            turnHasStreamingBlock = true;
                        } else if (b->kind == "assistant" && !b->text.empty() &&
                                   !(m_sess->hasAgentsCap && !b->agentId.empty())) {
                            lastEndedAssistantIdx = turnEnd;
                        }
                    }
                }
            }
            ++turnEnd;
        }
        const size_t visibleIdx = turnHasStreamingBlock ? static_cast<size_t>(-1)
                                                        : lastEndedAssistantIdx;
        for (size_t k = i; k < turnEnd; ++k) {
            RowRec& r = m_sess->rows[k];
            if (r.copyBtn) {
                const bool want = (k == visibleIdx);
                if (r.copyBtn->IsVisible() != want) {
                    r.copyBtn->SetVisible(want);
                    r.layoutDirty = true;
                }
            }
        }
        i = turnEnd;
    }
}

void HarnessApp::Relayout() {
    SyncTurnCopyButtons();
    // 版心宽 = min(可视带宽, 最大阅读宽)。两种行模式共用同一个数，
    // 免得「list 模式满宽、autosize 版本心宽」这种只在某个模式下露出来的不一致。
    const int layoutW = TranscriptLayoutWidth();
    // 行左边界：按**行宽**居中（`(scrollW − 行宽)/2`），两侧留白恒等。
    const int canvasX0 = TranscriptCanvasX();

    const bool inAgentTab = m_sess && !m_sess->activeAgentTab.empty();
    if (m_options.rowModeList) {
        // listlayout：行高固定、不测量；Arrange 由 ListLayout 完成
        for (RowRec& r : m_sess->rows) {
            r.tabHidden = IsRowHiddenByAgentTab(r);
            if (r.panel) {
                r.panel->SetVisible(inAgentTab ? !r.tabHidden : (!r.tabHidden && !r.foldHidden));
                if (r.panel->GetHeight() != kListRowHeight) {
                    r.panel->SetSize(layoutW, kListRowHeight);
                }
            }
        }
        for (PendingPromptView& view : m_pendingPromptViews) {
            if (view.row.panel) {
                view.row.panel->SetVisible(!inAgentTab);
                if (view.row.panel->GetHeight() != kListRowHeight) {
                    view.row.panel->SetSize(layoutW, kListRowHeight);
                }
            }
        }
        m_content->Arrange();
        m_scroll->UpdateContentExtent();
        SyncInspectorLayout(false);
        return;
    }

    // autosize：每行按内容测量（文本测量成本正是本模式要暴露的指标 ④）
    // ★ 行可用宽 = **版心宽** − 左右画布留白；行本身从 `canvasX` 起排。
    //   ★★ 2026-10-27：这里读的是 `TranscriptLayoutWidth()`（版心，= min(对称可用宽, 760)）
    //     **不是**视口宽。视口宽会随检视栏开合变 ⇒ 行若跟着变，wrapW / maxTextW 就变
    //     ⇒ 换行缓存（C29-10，指纹含宽度）全失效 ⇒ 全文档文本重测（「点详情卡一下、
    //     会话越长越卡」的根因）。版心封顶后，宽窗口里 toggle 是零重测的。
    const int innerW0 = std::max(120, layoutW - 2 * kCanvasPadX);
    int y = 0;
    SyncAgentTaskBanner(canvasX0, innerW0, y);
    // ★ v5：改成**带下标**的遍历，唯一目的是让竖线能看一眼"下一行是不是同一个代理"
    //   （见下方 gutter 高度）。行数 O(n)，多一次比较不改变复杂度。
    for (size_t rowIdx = 0; rowIdx < m_sess->rows.size(); ++rowIdx) {
        RowRec& r = m_sess->rows[rowIdx];
        r.tabHidden = IsRowHiddenByAgentTab(r);
        const bool rowHidden = inAgentTab ? r.tabHidden : (r.tabHidden || r.foldHidden);
        // ★★ 2026-11-06（回合级折叠）：被折叠过程行**不参与排版**，`y` 也不推进。
        //   ★ 为什么必须在这里跳过、而不是只靠 `SetVisible(false)`：`ListLayout`
        //     会跳过不可见子控件（那是 list 模式那半边的全部理由），但 autosize
        //     这条路是**本函数手算 y 的**——它根本不问布局器。不跳过就只是"看不见
        //     但高度还在"，屏上留下一个与折叠内容等高的空洞（回放实测的最初形态）。
        //   ★ 为什么同时还要 `SetVisible(false)`：命中测试/绘制/`Arrange` 在
        //     list 模式由布局器负责，可见性必须由控件自己持有。
        if (rowHidden) {
            if (r.panel) {
                r.panel->SetVisible(false);
                const int hiddenIndent = inAgentTab ? 0 : r.indent;
                const int maxHiddenW = std::max(160, innerW0 - hiddenIndent);
                if (r.panel->GetWidth() > maxHiddenW) {
                    r.panel->SetSize(maxHiddenW, r.panel->GetHeight());
                }
                // ★★ 2026-11-06（订正）：被折叠的行**仍然要横向定位**。
                //   只 `continue` 会让面板停在建行期的默认 x=0 ⇒ 任何"测行的横移"
                //   的读数都会读到一个假值：`--agent-probe` 的 `indentPx` 实测变成
                //   0 − canvasX = **−18**（子代理行被折起来时），于是它的元断言
                //   `indentPx == kAgentIndent` 无故变红，note 里却看不出是折叠引起的。
                //   纵向 y 不参与（不可见、不占位），所以这里只下发横坐标；
                //   `y` 照旧不推进。
                r.panel->SetPosition(canvasX0 + hiddenIndent, y);
            }
            if (r.gutter && !m_options.rowModeList) {
                r.gutter->SetVisible(false);
            }
            continue;
        }
        if (r.panel && !r.panel->IsVisible()) {
            r.panel->SetVisible(true);
        }
        // ★★ 协议 v5（2026-11-04）：子代理行右移一档。**只改宽度与横坐标**——
        //   `y` 的累加、行高的求解路径一个字不动（缩进是一次减法，不是一层新布局）。
        //   ★ 这里刻意用**同名遮蔽**：`innerW0`/`canvasX0` 是整栏的，`innerW`/`canvasX`
        //     是本行的。循环体内所有既有算式因此逐字不变地变成"按本行宽"，
        //     不可能漏改某一处——漏一处就是「面板按版心宽、文字按缩进宽」的错位，
        //     而那种错位只在有子代理的行上出现，既有探针一条都抓不到。
        const int effIndent = inAgentTab ? 0 : r.indent;
        const int innerW = std::max(160, innerW0 - effIndent);
        const int canvasX = canvasX0 + effIndent;
        int rowH = 0;

        // ★★ P0 Step 1 & Step 3: Row-level layout cache + lazy collapsed measurement.
        //   When neither row content nor column width changed, skip all text measurements
        //   and SetSize calls, updating only vertical placement `y` and gutter continuation.
        if (!r.layoutDirty && r.lastLayoutW == innerW) {
            rowH = r.cachedRowH;
            if (r.panel) {
                const bool isChatBubble =
                    (!r.foldBar && !r.tool && !r.reasoning && !r.ask && !r.hasMarkdown);
                const int px = (isChatBubble && r.alignRight)
                                   ? (canvasX + innerW - r.cachedBubbleW)
                                   : canvasX;
                r.panel->SetPosition(px, y);
            }
            if (r.foldBar) {
                y += rowH + kRowGap;
                continue;
            }
        } else if (r.foldBar) {
            if (r.headerBtn) {
                r.headerBtn->SetSize(std::max(80, innerW - 2 * kRowPadX), kHeaderH);
                r.headerBtn->SetPosition(kRowPadX, 2);
            }
            rowH = 2 + kHeaderH + 2;
            if (r.panel) {
                r.panel->SetSize(innerW, rowH);
                r.panel->SetPosition(canvasX, y);
            }
            r.lastLayoutW = innerW;
            r.cachedRowH = rowH;
            r.cachedBubbleW = innerW;
            r.layoutDirty = false;
            y += rowH + kRowGap;
            continue;
        } else {
            int bubbleW = innerW;
            // ★ 协议 v2：思维链卡与工具卡**同一套几何**（通栏 + 折叠体），故共用本分支
            if (r.tool || r.reasoning || r.ask) {
                const int bodyW = innerW - 2 * kRowPadX;
                const int wrapW = std::max(40, bodyW - 20);
                int bodyH = r.bodyH;
                // Collapsed reasoning/tool rows skip text wrap + AutoSize completely during streaming;
                // ToggleRow measures synchronously before calling body->Toggle().
                if (r.label && (r.expanded || r.ask)) {
                    if (r.lastWrapW != wrapW) {
                        r.label->SetSize(wrapW, r.label->GetHeight());
                        const int propContentW = std::max(wrapW, r.proposal.maxCols * 8 + 12);
                        for (ECDI::Label* l : r.proposal.lines) {
                            if (l) l->SetSize(propContentW, kProposalLineH);
                        }
                        if (r.proposal.sv && !r.proposal.lines.empty()) {
                            const int linesH = static_cast<int>(r.proposal.lines.size()) * kProposalLineH;
                            const int barExtra = (propContentW > wrapW) ? (kScrollBarThickness + 2) : 0;
                            r.proposal.sv->SetSize(wrapW, linesH + barExtra);
                            r.proposal.sv->SetContentExtent(propContentW, linesH);
                        }
                        r.lastWrapW = wrapW;
                    }
                    r.label->AutoSize();
                    bodyH = r.label->GetHeight() + 8;
                    if (!r.proposal.lines.empty()) {
                        const int propContentW = std::max(wrapW, r.proposal.maxCols * 8 + 12);
                        const int barExtra = (propContentW > wrapW) ? (kScrollBarThickness + 2) : 0;
                        bodyH += static_cast<int>(r.proposal.lines.size()) * kProposalLineH + barExtra;
                    }
                    r.bodyH = bodyH;
                }
                if (r.headerBtn) {
                    const int headW = r.ask ? std::max(80, innerW - kRowPadX - 168)
                                            : innerW - 2 * kRowPadX;
                    r.headerBtn->SetSize(headW, kHeaderH);
                    r.headerBtn->SetPosition(kRowPadX, 2);
                }
                if (r.body) {
                    if ((r.expanded || r.ask) && (r.lastBodyW != bodyW || r.lastBodyH != bodyH)) {
                        r.lastBodyW = bodyW;
                        r.lastBodyH = bodyH;
                        r.body->SetSize(bodyW, bodyH);
                    }
                    r.body->SetPosition(kRowPadX, 2 + kHeaderH + 2);
                }
                rowH = 2 + kHeaderH + 2 + (r.expanded ? bodyH : 0);
                if (r.ask) {
                    const int btnY = 2 + (kHeaderH - 28) / 2;
                    if (r.allowBtn) {
                        r.allowBtn->SetSize(70, 28);
                        r.allowBtn->SetPosition(innerW - 160, btnY);
                    }
                    if (r.denyBtn) {
                        r.denyBtn->SetSize(70, 28);
                        r.denyBtn->SetPosition(innerW - 82, btnY);
                    }
                    if (r.conclusion && r.conclusion->IsVisible()) {
                        r.conclusion->AutoSize();
                        const int cw = r.conclusion->GetWidth();
                        const int ch = r.conclusion->GetHeight();
                        r.conclusion->SetPosition(innerW - cw - 14, 2 + (kHeaderH - ch) / 2);
                    }
                }
                if (r.panel) {
                    r.panel->SetSize(innerW, rowH);
                    r.panel->SetPosition(canvasX, y);
                }
            } else if (r.hasMarkdown) {
                const int tableAvailW = std::max(40, innerW - 2 * kRowPadX);
                for (RowRec::TableRec& t : r.tables) {
                    if (t.lastAvailW != tableAvailW) {
                        LayoutTableInterior(t, tableAvailW);
                        t.lastAvailW = tableAvailW;
                    }
                }
                int curY = 8;
                RowRec::SegMeta defaultMeta;
                for (size_t si = 0; si < r.segments.size(); ++si) {
                    ECDI::Widget* w = r.segments[si];
                    if (!w) continue;
                    RowRec::SegMeta& meta = (si < r.segMeta.size()) ? r.segMeta[si] : defaultMeta;
                    int gapAfter = 10;
                    switch (meta.kind) {
                        case MarkdownSegmentKind::Heading:
                            gapAfter = (meta.level <= 1) ? 12 : (meta.level == 2 ? 10 : 8);
                            break;
                        case MarkdownSegmentKind::CodeBlock:
                            gapAfter = 12;
                            break;
                        case MarkdownSegmentKind::ListItem:
                            gapAfter = meta.lastOfList ? 10 : 3;
                            break;
                        case MarkdownSegmentKind::Quote:
                            gapAfter = 10;
                            break;
                        case MarkdownSegmentKind::Hr:
                            gapAfter = 12;
                            break;
                        case MarkdownSegmentKind::Table:
                            gapAfter = 12;
                            break;
                        default:
                            gapAfter = 10;
                            break;
                    }

                    if (meta.kind == MarkdownSegmentKind::Hr) {
                        w->SetSize(std::max(40, innerW - 2 * kRowPadX), 1);
                        w->SetPosition(kRowPadX, curY + 4);
                        curY += 1 + 4 + gapAfter;
                    } else if (meta.kind == MarkdownSegmentKind::ListItem) {
                        const int indentStep = 26;
                        const int slotW = 26;
                        int left = kRowPadX + meta.depth * indentStep;
                        if (w->GetChildCount() >= 2) {
                            auto* mk = w->GetChildAt(0);
                            auto* body = dynamic_cast<ECDI::TextWidget*>(w->GetChildAt(1));
                            if (!mk || !body) { w->SetPosition(left, curY); curY += w->GetHeight() + gapAfter; continue; }
                            const int availW = std::max(40, innerW - left - kRowPadX);
                            body->SetWordWrap(true);
                            body->SetSize(std::max(20, availW - slotW), body->GetHeight());
                            body->AutoSize();
                            const int singleLineH = uikit::LineBox(harness::kFontBody);
                            const int bodyH = std::max(singleLineH, body->GetHeight());
                            body->SetSize(body->GetWidth(), bodyH);
                            mk->SetSize(slotW, singleLineH);
                            mk->SetPosition(0, 0);
                            body->SetPosition(slotW, 0);
                            w->SetSize(availW, bodyH);
                            w->SetPosition(left, curY);
                            curY += bodyH + gapAfter;
                        } else {
                            w->SetPosition(left, curY);
                            curY += w->GetHeight() + gapAfter;
                        }
                    } else if (meta.kind == MarkdownSegmentKind::CodeBlock) {
                        if (w->GetChildCount() >= 2) {
                            auto* bar = w->GetChildAt(0);
                            auto* sv = w->GetChildAt(1);
                            const int cardW = std::max(40, innerW - 2 * kRowPadX);
                            const int barH = 28;
                            bar->SetSize(cardW - 2, barH);
                            bar->SetPosition(1, 1);
                            if (bar->GetChildCount() >= 2) {
                                auto* lang = bar->GetChildAt(0);
                                auto* btn = bar->GetChildAt(1);
                                lang->AutoSize();
                                lang->SetPosition(10, (barH - lang->GetHeight()) / 2);
                                btn->SetSize(48, 20);
                                btn->SetPosition(cardW - 2 - 48 - 8, (barH - 20) / 2);
                            }
                            const int viewW = cardW - 2;
                            int contentW = 0;
                            int contentH = 0;
                            if (auto* s = dynamic_cast<ECDI::ScrollView*>(sv)) {
                                const int padX = 12;
                                const int padY = 8;
                                const int minLineH = uikit::LineBox(harness::kFontMicro);
                                auto& cv = s->GetContentView();
                                if (cv.GetChildCount() > 0) {
                                    if (auto* codeWidget = dynamic_cast<ECDI::TextWidget*>(cv.GetChildAt(0))) {
                                        if (!meta.codeMeasured) {
                                            const std::string& raw = codeWidget->GetText();
                                            const ECDI::Font monoFont = harness::MonoFont(harness::kFontMicro);
                                            int maxLineW = 1;
                                            size_t pos = 0;
                                            while (true) {
                                                const size_t nl = raw.find('\n', pos);
                                                const std::string line = (nl == std::string::npos)
                                                    ? raw.substr(pos)
                                                    : raw.substr(pos, nl - pos);
                                                int lw = 1;
                                                if (m_window) {
                                                    lw = static_cast<int>(
                                                        m_window->GetTextMeasurer().MeasureText(monoFont, line).width + 0.999f);
                                                } else {
                                                    lw = static_cast<int>(line.size()) * 7;
                                                }
                                                maxLineW = std::max(maxLineW, lw);
                                                if (nl == std::string::npos) break;
                                                pos = nl + 1;
                                            }
                                            const int textW0 = std::max(1, maxLineW + 2);
                                            if (dynamic_cast<ECDI::Label*>(codeWidget)) {
                                                codeWidget->SetWordWrap(true);
                                            } else {
                                                codeWidget->SetWordWrap(false);
                                            }
                                            codeWidget->SetSize(textW0, minLineH);
                                            codeWidget->AutoSize();
                                            meta.cachedCodeMaxLineW = maxLineW;
                                            meta.cachedCodeTextH = std::max(minLineH, codeWidget->GetHeight());
                                            meta.codeMeasured = true;
                                        }
                                        const int textW = std::max(1, meta.cachedCodeMaxLineW + 2);
                                        const int textH = meta.cachedCodeTextH;
                                        codeWidget->SetPosition(padX, padY);
                                        codeWidget->SetSize(std::max(textW, viewW - 2 * padX), textH);
                                        contentW = textW + 2 * padX;
                                        contentH = textH + 2 * padY;
                                        s->SetContentExtent(contentW, contentH);
                                    }
                                }
                                contentW = static_cast<int>(s->GetContentExtent().width);
                                contentH = static_cast<int>(s->GetContentExtent().height);
                            }
                            const int barFoot = (contentW > viewW) ? (4 + 2) : 0;
                            sv->SetSize(viewW, contentH + barFoot);
                            sv->SetPosition(1, 1 + barH);
                            const int cardH = 1 + barH + contentH + barFoot + 1;
                            w->SetSize(cardW, cardH);
                            w->SetPosition(kRowPadX, curY);
                            curY += cardH + gapAfter;
                        } else {
                            w->SetPosition(kRowPadX, curY);
                            curY += w->GetHeight() + gapAfter;
                        }
                    } else if (meta.kind == MarkdownSegmentKind::Quote) {
                        if (w->GetChildCount() >= 2) {
                            auto* bar = w->GetChildAt(0);
                            auto* body = dynamic_cast<ECDI::TextWidget*>(w->GetChildAt(1));
                            if (!bar || !body) { w->SetPosition(kRowPadX, curY); curY += w->GetHeight() + gapAfter; continue; }
                            const int availW = std::max(40, innerW - 2 * kRowPadX);
                            body->SetWordWrap(true);
                            body->SetSize(std::max(20, availW - 3 - 12), body->GetHeight());
                            body->AutoSize();
                            const int singleLineH = uikit::LineBox(harness::kFontBody);
                            const int h = std::max(singleLineH, body->GetHeight());
                            body->SetSize(body->GetWidth(), h);
                            bar->SetSize(3, h);
                            bar->SetPosition(0, 0);
                            body->SetPosition(3 + 12, 0);
                            w->SetSize(availW, h);
                            w->SetPosition(kRowPadX, curY);
                            curY += h + gapAfter;
                        } else {
                            w->SetPosition(kRowPadX, curY);
                            curY += w->GetHeight() + gapAfter;
                        }
                    } else if (auto* tw = dynamic_cast<ECDI::TextWidget*>(w)) {
                        tw->SetWordWrap(true);
                        tw->SetSize(std::max(40, innerW - 2 * kRowPadX), tw->GetHeight());
                        tw->AutoSize();
                        tw->SetPosition(kRowPadX, curY);
                        curY += tw->GetHeight() + gapAfter;
                    } else if (auto* sv = dynamic_cast<ECDI::ScrollView*>(w)) {
                        const int contentW = static_cast<int>(sv->GetContentExtent().width);
                        const int contentH = static_cast<int>(sv->GetContentExtent().height);
                        const int availW = std::max(40, innerW - 2 * kRowPadX);
                        const int tableW = std::min(availW, contentW);
                        const int barFoot = sv->GetHorizontalScrollBar()
                                                ? sv->GetHorizontalScrollBar()->GetThickness() + 2
                                                : 14;
                        const int barExtraH = (contentW > availW) ? barFoot : 0;
                        const int tableH = contentH + barExtraH;
                        sv->SetSize(tableW, tableH);
                        sv->SetPosition(kRowPadX, curY);
                        curY += tableH + gapAfter;
                    } else {
                        w->SetPosition(kRowPadX, curY);
                        curY += w->GetHeight() + gapAfter;
                    }
                }
                if (r.copyBtn && r.copyBtn->IsVisible()) {
                    r.copyBtn->SetSize(48, 22);
                    r.copyBtn->SetPosition(kRowPadX, curY);
                    curY += 22 + 8;
                }
                rowH = curY + 8;
                if (r.panel) {
                    r.panel->SetSize(innerW, rowH);
                    r.panel->SetPosition(canvasX, y);
                }
            } else if (!r.streamChunks.empty()) {
                const int maxBubbleW = std::max(120, innerW * 78 / 100);
                const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
                int curY = 8;
                int maxUsedW = 0;
                bool anyWrapped = false;
                for (RowRec::StreamChunk& ch : r.streamChunks) {
                    if (!ch.widget) continue;
                    const bool needChunkLayout =
                        ch.dirty ||
                        (ch.wrapped && ch.lastWrapW != maxTextW) ||
                        (!ch.wrapped && ch.cachedW > maxTextW);
                    if (needChunkLayout) {
                        if (!ch.wrapped) {
                            ch.widget->AutoSize();
                        }
                        const bool hasNewline =
                            ch.widget->GetText().find('\n') != std::string::npos;
                        const bool needWrap =
                            ch.wrapped || hasNewline || ch.widget->GetWidth() > maxTextW;
                        if (needWrap != ch.wrapped) {
                            ch.wrapped = needWrap;
                            ch.widget->SetWordWrap(needWrap);
                        }
                        if (ch.wrapped) {
                            if (ch.lastWrapW != maxTextW) {
                                ch.widget->SetSize(maxTextW, ch.widget->GetHeight());
                                ch.lastWrapW = maxTextW;
                            }
                            ch.widget->AutoSize();
                        }
                        ch.cachedW = ch.widget->GetWidth();
                        ch.cachedH = ch.widget->GetHeight();
                        ch.dirty = false;
                    }
                    ch.widget->SetPosition(kRowPadX, curY);
                    curY += ch.cachedH;
                    maxUsedW = std::max(maxUsedW, ch.cachedW);
                    if (ch.wrapped) anyWrapped = true;
                }
                if (r.copyBtn && r.copyBtn->IsVisible()) {
                    r.copyBtn->SetSize(48, 22);
                    r.copyBtn->SetPosition(kRowPadX, curY + 2);
                    rowH = curY + 2 + 22 + 8;
                } else {
                    rowH = curY + 8;
                }
                bubbleW = anyWrapped
                              ? maxBubbleW
                              : std::min(maxBubbleW, maxUsedW + 2 * kRowPadX + 2);
                if (r.copyBtn && r.copyBtn->IsVisible()) {
                    bubbleW = std::max(bubbleW, 48 + 2 * kRowPadX);
                }
                if (r.panel) {
                    r.panel->SetSize(bubbleW, rowH);
                    r.panel->SetPosition(r.alignRight ? canvasX + innerW - bubbleW
                                                      : canvasX,
                                         y);
                }
            } else {
                const int maxBubbleW = std::max(120, innerW * 78 / 100);
                const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
                if (r.label) {
                    if (!r.wrapped) {
                        r.label->AutoSize();   // 单行 extent
                    }
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
                    r.label->SetPosition(kRowPadX, 8);
                }
                if (r.copyBtn && r.copyBtn->IsVisible()) {
                    r.copyBtn->SetSize(48, 22);
                    r.copyBtn->SetPosition(kRowPadX, r.label ? r.label->GetHeight() + 10 : 8);
                    rowH = (r.label ? r.label->GetHeight() + 10 : 0) + 22 + 8;
                } else {
                    rowH = r.label ? r.label->GetHeight() + 16 : 0;
                }
                if (r.label) {
                    bubbleW = r.wrapped
                                  ? maxBubbleW
                                  : std::min(maxBubbleW,
                                             r.label->GetWidth() + 2 * kRowPadX + 2);
                }
                if (r.copyBtn && r.copyBtn->IsVisible()) {
                    bubbleW = std::max(bubbleW, 48 + 2 * kRowPadX);
                }
                if (r.panel) {
                    r.panel->SetSize(bubbleW, rowH);
                    r.panel->SetPosition(r.alignRight ? canvasX + innerW - bubbleW
                                                      : canvasX,
                                         y);
                }
            }
            r.lastLayoutW = innerW;
            r.cachedRowH = rowH;
            r.cachedBubbleW = bubbleW;
            r.layoutDirty = false;
        }
        r.y = y;   // ★ v5：「代理」卡点击定位要用（见 `AppAgents.cpp::FocusAgent`）
        // ★ v5：归属竖线跟着**本行行高**走（建行期给不出高度，测量后才定）。
        //   autosize 模式下竖线挂在画布层（m_content）于 (canvasX, y)，下一可见行
        //   同属该代理时多画 kRowGap 10px 无缝衔接成一条连续竖线；list 模式保留行内挂法。
        if (r.gutter) {
            if (inAgentTab) {
                r.gutter->SetVisible(false);
            } else if (m_options.rowModeList) {
                r.gutter->SetSize(kAgentGutterW, rowH);
                r.gutter->SetPosition(0, 0);
            } else {
                r.gutter->SetVisible(true);
                size_t nextVis = rowIdx + 1;
                while (nextVis < m_sess->rows.size() && m_sess->rows[nextVis].foldHidden) {
                    ++nextVis;
                }
                const bool connectNext =
                    (nextVis < m_sess->rows.size() && !r.agentTag.empty() &&
                     m_sess->rows[nextVis].agentTag == r.agentTag);
                const bool focused = (!m_focusedAgentId.empty() && r.agentTag == m_focusedAgentId);
                const int gw = kAgentGutterW + (focused ? 1 : 0);
                const int gh = rowH + (connectNext ? kRowGap : 0);
                r.gutter->SetSize(gw, gh);
                r.gutter->SetPosition(canvasX, y);
            }
        }
        y += rowH + kRowGap;
    }
    ApplyFocusedAgentHighlight();
    for (PendingPromptView& view : m_pendingPromptViews) {
        RowRec& r = view.row;
        if (!r.panel) continue;
        if (inAgentTab) {
            r.panel->SetVisible(false);
            continue;
        }
        if (!r.panel->IsVisible()) r.panel->SetVisible(true);
        // ★ v5：待确认气泡恒为根行（没有归属），用整栏量——循环内那两个
        //   遮蔽名出不了 main loop，这里必须显式用 `innerW0`。
        const int maxBubbleW = std::max(120, innerW0 * 78 / 100);
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
        r.panel->SetPosition(canvasX0 + innerW0 - bubbleW, y);
        y += rowH + kRowGap;
    }
    // 横向 extent 至少覆盖到**内容列右边界**（= canvasX + 行宽）。★ 它已不再与
    // `ContentWidth()` 同源：后者是 `scrollW − 40` 的单侧扣除。取两者较大：
    // 既保留旧行为的下限，也不裁内容；两者都远小于视口宽，不会引出横向滚动条。
    // ★★ 2026-10-27 二改：右边界用 **canvasX + innerW**，不是 `canvasX + layoutW`。
    //   后者是"版心"（含两侧 pad），比行右边界多出 2×pad ⇒ 占满档实测 extent 704
    //   > 视口 684 ⇒ **恒出一条横向滚动条**（`--resize-probe` 的 maxOffsetX 判据会红）。
    //   extent 的职责只是"别裁内容"，多算的那 32px 既不是内容也不该被滚。
    m_scroll->SetContentExtent(std::max(ContentWidth(m_scroll), canvasX0 + innerW0),
                               std::max(0, y - kRowGap));
    // ★★ P4 Phase A：内容范围变了 ⇒ maxOffset/滚动条可见性都可能变——
    //   「跳到最新」的几何与显隐跟着刷新一次（Relayout 覆盖 resize/侧栏/菜单/
    //   设置切换等全部几何路径）。
    RefreshJumpButton();
    // ★★ P4 Phase C：空状态引导的宽度跟随（resize/侧栏切换后重摆）。
    RefreshEmptyGuide();
    // ★★ P4 Phase F：设置浮层几何跟随（视口尺寸变了，遮罩/sheet 重摆）。
    LayoutSettingsOverlay();
    // ★★ P4 Phase I：预览浮层几何跟随（同一条 Relayout 几何路径）。
    LayoutPreviewOverlay();
    // ★★ 2026-10-30（浮层迁移）：模型下拉现在挂在浮层根上（客户区坐标），
    //   视口位置变了必须跟着重算——否则 resize 后它停在旧坐标（原实现在
    //   scroll 子表里，视口坐标不随窗口变，所以没有这条需求）。
    //   `SetPosition` 有同值守卫 ⇒ 空闲帧零代价（不会置脏、不会多一次重绘）。
    RepositionModelPopup();
    SyncInspectorLayout(false);
}

void HarnessApp::SyncInspectorLayout(bool force) {
    if (!m_hud.inspector || !m_hud.inspectorSv || !m_hud.inspector->IsVisible()) {
        m_inspectorLayoutDirty = true;
        return;
    }
    ECDI::ScrollView* sv = m_hud.inspectorSv;
    const int availWPre = std::max(1, sv->GetWidth());
    const int availHPre = std::max(1, sv->GetHeight());
    if (!force && !m_inspectorLayoutDirty &&
        availWPre == m_lastInspW && availHPre == m_lastInspH) {
        return;
    }
    m_hud.inspector->Arrange();
    ECDI::Widget& body = sv->GetContentView();
    const int availW = std::max(1, sv->GetWidth());
    const int availH = std::max(1, sv->GetHeight());
    m_lastInspW = availW;
    m_lastInspH = availH;
    m_inspectorLayoutDirty = false;
    constexpr int kBodyPad = harness::kPadFrame - 1;

    auto measureBottom = [&]() -> int {
        int maxBottom = 0;
        const size_t n = body.GetChildCount();
        for (size_t i = 0; i < n; ++i) {
            const ECDI::Widget* child = body.GetChildAt(i);
            if (!child || !child->IsVisible()) continue;
            maxBottom = std::max(maxBottom, child->GetY() + child->GetHeight());
        }
        return (maxBottom > 0) ? (maxBottom + kBodyPad) : 0;
    };

    body.SetSize(availW, availH);
    body.Arrange();
    const int initialH = measureBottom();
    // 垂直条实际占位 = kScrollBarThickness + 2（kBarInset，见 UiCommon.cpp）。
    constexpr int kVBarFootprint = harness::internal::kScrollBarThickness + 2;
    const int finalW = (initialH > availH)
                           ? std::max(1, availW - kVBarFootprint)
                           : availW;
    if (finalW != availW) {
        body.SetSize(finalW, availH);
        body.Arrange();
    }
    const int contentH = measureBottom();
    sv->SetContentExtent(finalW, contentH);
}

void HarnessApp::FollowTailIfPinned() {
    if (!m_scroll) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const bool pinned = m_scroll->GetScrollOffsetY() >= maxY - kPinThreshold;
    if (pinned) m_scroll->SetContentOffset(0, maxY);
    // ★★ P4 Phase A：填行/重排后顺手刷新「跳到最新」——本函数的调用点天然
    //   覆盖所有内容增长路径（tick 填行 / Relayout / 切会话重建），无需另铺。
    RefreshJumpButton();
}

// ★★ P4 Phase A（2026-10-25）：刷新「跳到最新」按钮的几何与显隐（唯一刷新口）。
//   ★ 调用时点：滚轮/导航键后（HarnessApplication override）、填行后
//     （FollowTailIfPinned 尾部）、重排后（Relayout 尾部）、tick 脏检查
//     （DirtyRefreshJumpButton——拖滚动条滑块没有事件回调，靠定时器节拍兜底）。
//   ★ 全部是只读查询 + 几何 setter + 同值守卫的 SetVisible（M9：同值不
//     Invalidate）⇒ 高频调用零重绘代价，不打扰 CountingPanel 读数。
void HarnessApp::RefreshJumpButton() {
    if (!m_hud.jumpLatest || !m_scroll) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const int offset = m_scroll->GetScrollOffsetY();
    m_jumpLastOffsetY = offset;   // 脏检查基准同步更新
    // 几何：视口右下角、贴滚动条一侧（原型 F 条）。条厚不猜——垂直条可见时按
    // 其实际宽度让位，条隐藏/显示切换时几何自然跟随。
    // ★ 2026-10-27：条厚 12→4（`kScrollBarThickness`），旧注释里的
    //   「BarFootprint=14」是 12+2 的观测值，已作废；现在真实占位 = 4+2 = 6。
    //   这里仍按 `GetWidth() + 6` 让位：`GetWidth()` 是条控件宽（=thickness），
    //   再加 6 是**视觉间隙**，不属于占位账。
    int barW = 0;
    if (ECDI::ScrollBar* bar = m_scroll->GetVerticalScrollBar()) {
        if (bar->IsVisible()) barW = bar->GetWidth() + 6;
    }
    m_hud.jumpLatest->SetSize(kJumpBtnW, kJumpBtnH);
    // ★★ 2026-10-27（订正）：横向回到**贴视口右下角**，不再跟版心。
    //   它是浮层按钮（FAB），贴视口角才是正确语义；跟版心走会让它在检视栏收起时
    //   悬在内容右侧 230px 处——既不贴边也不贴内容，两头不靠。
    m_hud.jumpLatest->SetPosition(m_scroll->GetWidth() - barW - kJumpBtnW - 16,
                                  m_scroll->GetHeight() - kJumpBtnH - 10);
    const bool pinned = offset >= maxY - kPinThreshold;
    // 显隐 = 离开贴底带 ∧ 有可滚内容 ∧ 转录可见（设置面板占槽时 scroll 不可见）。
    m_hud.jumpLatest->SetVisible(!pinned && maxY > 0 && m_scroll->IsVisible());
}

// ★★ P4 Phase A：tick 脏检查——offset 与缓存不等才真正刷新（禁止无条件刷新/
//   Invalidate，CountingPanel 读数纪律）。
void HarnessApp::DirtyRefreshJumpButton() {
    if (!m_scroll || !m_hud.jumpLatest) return;
    if (m_scroll->GetScrollOffsetY() != m_jumpLastOffsetY) RefreshJumpButton();
}

// ── ★★ P4 Phase E（2026-10-25）：tooltip 单例浮层 ─────────────────────
// ECDI 无 Tooltip 原语（源码仓核实：TrayIcon szTip 是唯一同名物）——harness 拼：
// TipButton（UiKit）回调 enter/leave，这里管单例浮层 + 400ms 延迟定时器。
// ★★ 2026-10-30（浮层迁移，docs/overlay-migration-plan.md §4）：**改走
//   `Window::ShowOverlay` + Tooltip 层**，不再挂 `m_scroll`。三件事一起解决：
//     ① **不再被视口裁剪**——原来 tooltip 挂在转录视口里，而顶栏/中栏头的按钮
//        在视口**上方** ⇒ 算出来的 y 是负数 ⇒ 被 `PushClip` 裁掉，只能靠
//        `clamp(y, 0, ...)` 把提示强行贴到视口顶端（S5 那条绕行，已删）。
//        现在坐标是**客户区坐标**，锚点在哪就在哪。
//     ② **点穿**——Tooltip 层在 `Window::HitTestWindow` 里被显式跳过，
//        提示框永远不会截留鼠标（挂普通子树时它是会参与命中的）。
//     ③ **单例由销毁保证**——`HideOverlay` 帧末析构，`m_hud.tooltip` 随之置空
//        ⇒ "同时存在两条 tooltip"在结构上不可能。
//   ★ 为什么**不用** `anchorWidget`（框架的锚点跟随）：它会覆盖位置为
//     `anchor.y + anchor.h`（丢掉这里刻意的 +6px 间距），X 也改成锚点左缘。
//     位置在本函数手算更贴原观感；代价是窗口 resize 时不自动跟随——tooltip
//     只在悬停期间存在，鼠标一动就重算，这个代价可以接受。
void HarnessApp::ShowTooltipFor(const ECDI::Widget* anchor, const std::string& text) {
    if (!anchor || !m_window) return;
    m_tooltipAnchor = anchor;
    m_tooltipPending = text;
    m_window->GetPlatformWindow().StartTimer(kTooltipTimer, kTooltipDelayMs);
}

// 关掉**当前显示中的** tooltip（若有）。句柄必须立刻置空：`HideOverlay` 只是
//   把控件移进延迟销毁队列，帧末才真的析构——期间任何读取都是悬空指针。
void HarnessApp::HideTooltipOverlay() {
    if (m_window && m_hud.tooltip) {
        m_window->HideOverlay(m_hud.tooltip);
    }
    m_hud.tooltip = nullptr;
    m_hud.tooltipText = nullptr;
}

void HarnessApp::HideTooltip() {
    if (m_window) m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    m_tooltipAnchor = nullptr;
    m_tooltipPending.clear();
    HideTooltipOverlay();
}

void HarnessApp::OnTooltipFire() {
    if (!m_window || !m_tooltipAnchor) {
        return;
    }
    m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    // 同一时刻只有一条：上一条若还在（锚点快速切换），先关掉。
    HideTooltipOverlay();

    const int cw = m_window->GetRootWidget().GetWidth();
    const int ch = m_window->GetRootWidget().GetHeight();
    // ★ 按 UTF-8 码点列宽（ASCII=1，多字节=2）估算宽度：此前用 `size() * 7` 按字节乘，
    //   导致 3 字节中文字符被算成 21px（14px 字号实宽 ~14px），长中文提示框右侧空出一大截；
    //   纯列宽算术同时保证零 MeasureText 调用，不扰动 `--cull-probe` 的 measureCalls 读数。
    int tipCols = 0;
    for (size_t i = 0; i < m_tooltipPending.size();) {
        const unsigned char c = static_cast<unsigned char>(m_tooltipPending[i]);
        const size_t len = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
        tipCols += (len == 1) ? 1 : 2;
        i += len;
    }
    const int w = std::clamp<int>(tipCols * 7 + 20, 48, std::max(48, cw));
    const int h = 26;
    // ★ 锚点坐标 = 客户区绝对坐标（浮层根的坐标系与它同源，不再需要减视口原点）。
    const ECDI::Point a = m_tooltipAnchor->GetAbsolutePosition();
    const int x = std::clamp(static_cast<int>(a.x), 0, std::max(0, cw - w));
    const int y = std::clamp(static_cast<int>(a.y) + m_tooltipAnchor->GetHeight() + 6,
                             0, std::max(0, ch - h));

    auto tip = std::make_unique<uikit::TipPanel>();
    tip->SetSize(w, h);
    tip->SetPosition(x, y);
    // ★★ 2026-11-04 修：`.background` 曾是 `kDarkInk`——那是**文字色令牌**，
    //   深色下是 (230,233,239) 近白 ⇒ 与写死的 `kTooltipInk` 组成「近白底 + 近白字」
    //   （实测 1.08:1，dL* 2.9，字溶解）。底色与文字必须同为**固定字面量**，
    //   理由见 `AppInternal.h` 的 `kTooltipBg` 注释（含取值依据与三条备选否决）。
    tip->SetStyle(ECDI::PanelStyleOverride{
        .background = kTooltipBg,
        .cornerRadius = kRadiusBtn,
        .borderWidth = 0.0f});
    auto txt = std::make_unique<ECDI::Label>();
    txt->SetTextColor(kTooltipInk);
    txt->SetFont(harness::SansFont(harness::kFontSmall));
    const int lineH = uikit::LineBox(harness::kFontSmall);
    txt->SetSize(w - 16, lineH);
    txt->SetPosition(8, (h - lineH) / 2);
    txt->SetText(m_tooltipPending);
    m_hud.tooltipText = txt.get();
    tip->AddChild(std::move(txt));

    ECDI::OverlayOptions options;
    options.layer = ECDI::OverlayLayer::Tooltip;
    // 纯视觉层：不参与点外关闭（Tooltip 层本来就不接命中），ESC 归 harness 的
    //   逐层关逻辑（`HandleGlobalKeyDown`），框架别抢。
    options.lightDismiss = ECDI::LightDismissPolicy::None;
    options.closeOnEsc = false;
    m_hud.tooltip = tip.get();
    if (!m_window->ShowOverlay(std::move(tip), options)) {
        m_hud.tooltip = nullptr;
        m_hud.tooltipText = nullptr;
    }
}

void HarnessApp::RequestRepaint() {
    ++m_invalidations;
    if (m_counting) m_counting->Invalidate();
}

// ── 思考中 spinner（2026-11-04，用户裁决 A+C）──────────────────────────────
// 只做三件事：① 按行状态开/停；② 每 `kSpinStepTicks` 个 tick 走一格；③ 需要时补一次重绘。
// ★ 为什么状态由**行**决定而不是由事件决定：`reasoning` 行的 `ended` 在
//   `block_end` 到达时翻 true（AppStream），重建时由模型回填（AppTranscript）
//   ⇒ "块结束了没有"本来就只有一个真相源，这里读它即可。若在事件回调里
//   再维护一个"正在思考"布尔值，等于给同一条命途加第二份状态，迟早分叉。
// ★ 为什么探针模式下整段关闭：`paints`/`frameCount`/`invalidations` 是与
//   p32/夹具基线**逐位对比**的评估读数；动画会让它们随"思考时长"漂移。
//   本仓已有同款门控先例（`!m_options.HasAnyProbe()` 决定是否写配置/扫盘）。
//   跑批（tools/regress.py）全部经由探针 ⇒ 基线零扰动；`--instant` 回放下
//   reasoning 在同一个 tick 内就 `ended`，本来也不会转。
void HarnessApp::AdvanceThinkingSpinners() {
    if (!m_sess || m_options.HasAnyProbe()) return;

    bool anySpinning = false;
    bool needsPaint = false;
    for (RowRec& r : m_sess->rows) {
        if (!r.spinner) continue;
        bool want = false;
        if (r.reasoning) {
            want = !r.ended;
        } else if (r.tool) {
            bool jobRunning = false;
            if (!r.blockId.empty()) {
                auto jit = m_sess->jobsByCall.find(r.blockId);
                if (jit != m_sess->jobsByCall.end() && jit->second.status == "running") {
                    jobRunning = true;
                }
            }
            want = !r.ended || jobRunning;
        }
        if (r.spinner->IsSpinning() != want) {
            r.spinner->SetSpinning(want);
            needsPaint = true;   // 开/停各补一帧：停也要把弧擦掉
        }
        if (want) anySpinning = true;
    }
    // ★ 只在"真正走格"的 tick 补重绘：每 tick 都补等于把 33ms 变成整窗重绘，
    //   与本类注释里那条"仪器污染"（条 57③）是同一类错。
    if (anySpinning && m_spinTick == 0) {
        for (RowRec& r : m_sess->rows) {
            if (r.spinner && r.spinner->IsSpinning()) r.spinner->Advance();
        }
        needsPaint = true;
    }
    // ★ 取模而不是 `++`：计数器永不超过 kSpinStepTicks ⇒ 不会在 INT_MAX 处
    //   溢出成负数（负数的 `%` 恒不为 0，动画会停摆数秒后才被下一次翻转救回）。
    m_spinTick = (m_spinTick + 1) % kSpinStepTicks;
    if (needsPaint) RequestRepaint();
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
        m_probeState.evalProbeFrames = m_probe->FrameCount();
        m_probeState.evalProbeCommands = m_probe->TotalCommands();
        m_probeState.evalProbeMax = m_probe->MaxFrameCommands();
        m_probeState.evalProbeAvg = m_probe->AvgFrameCommands();
        m_evalMaxExecUs = m_probe->MaxExecuteUs();
        m_evalAvgExecUs = m_probe->AvgExecuteUs();
    }
    if (m_measureProbe) {
        // ★ 修法 D（2026-10-26）：侧栏标题省略的惰性校准也走这条测量链（仅 +2 次），
        //   从读数里减掉，保证 measureCalls 与修法 D 之前同口径。
        m_evalMeasureCalls = m_measureProbe->MeasureCalls() - m_elideMeasureOverhead;
        m_evalMeasureTotalUs = m_measureProbe->TotalUs();
        m_evalMeasureMaxUs = m_measureProbe->MaxUs();
    }
}

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
void HarnessApp::ApplyDevModeVisibility() {
    auto setVis = [](ECDI::Widget* w, bool vis, bool& parentDirty) {
        if (!w) return;
        if (w->IsVisible() != vis) {
            w->SetVisible(vis);
            parentDirty = true;
        }
    };

    // ① 顶栏：会话 id / 来源模式 / 行布局 / 渲染后端 四个评估胶囊仅在开发者模式显示。
    bool topDirty = false;
    setVis(m_hud.sessChipPill, m_devMode, topDirty);
    setVis(m_hud.modeChipPill, m_devMode, topDirty);
    setVis(m_hud.rowChipPill, m_devMode, topDirty);
    setVis(m_hud.backendChipPill, m_devMode, topDirty);
    if (topDirty && m_hud.sessChipPill) {
        if (ECDI::Widget* bar = m_hud.sessChipPill->GetParent()) bar->Arrange();
    }

    // ② 底栏（26px 评估状态栏）：用户模式隐藏，把垂直空间让给对话视口。
    bool rootDirty = false;
    setVis(m_hud.statusBar, m_devMode, rootDirty);
    if (rootDirty && m_hud.statusBar) {
        if (ECDI::Widget* root = m_hud.statusBar->GetParent()) root->Arrange();
    }

    // ③ 侧栏：回放进度条与测试夹具「历史」区仅在开发者模式显示；
    //   用户模式下若尚未连接真实工作区，把多余的 `workspaceStatus`（「等待 core 确认」）收起。
    bool sideDirty = false;
    // ★★ 2026-11-03（P2）：计数行合并后，这里隐的是**进度文字**而不是整行——
    //   那一行现在同时装着「N 个会话」（用户模式也要显示）。行容器 `sessProgressRow`
    //   于是变成**恒可见**的骨架，只有 `n/N` 与细轨还是开发者专属读数。
    setVis(m_hud.sessProgress, m_devMode, sideDirty);
    setVis(m_replayBar, m_devMode, sideDirty);
    setVis(m_hud.historyHeader, m_devMode, sideDirty);
    setVis(m_hud.historyList, m_devMode, sideDirty);
    setVis(m_hud.historyCount, m_devMode, sideDirty);
    const bool showWsStatus = m_devMode || (m_sess && !m_sess->workspacePath.empty());
    setVis(m_hud.workspaceStatus, showWsStatus, sideDirty);
    if (sideDirty && m_hud.sidebar) {
        m_hud.sidebar->Arrange();
    }

    // ④ 右侧检视栏：「渲染探针」卡仅在开发者模式显示；「协议健康」卡在开发者模式
    //   或出现真实协议异常（缺口 / 违规 / 未知类型 / 未知 kind）时显示。
    bool inspDirty = false;
    setVis(m_hud.inspProbeCard, m_devMode, inspDirty);
    const bool hasHealthIssue = m_sess && (m_sess->transcript.SeqGaps() > 0 ||
                                           m_sess->transcript.Violations() > 0 ||
                                           m_sess->transcript.UnknownTypes() > 0 ||
                                           m_sess->transcript.UnknownKinds() > 0);
    setVis(m_hud.inspHealthCard, m_devMode || hasHealthIssue, inspDirty);
    if (inspDirty) {
        SyncInspectorLayout();
    }

    // ⑤ 中栏头部操作钮与 ··· 菜单项：用户模式下给更直观的中文标签。
    if (m_hud.devModeMenuBtn) {
        m_hud.devModeMenuBtn->SetText(m_devMode ? "简洁视图" : "开发者视图");
    }
    bool headDirty = false;
    auto setActBtn = [&](ECDI::Button* b, const char* text, int w) {
        if (!b) return;
        if (b->GetText() != text) b->SetText(text);
        if (b->GetWidth() != w) {
            b->SetSize(w, 24);
            headDirty = true;
        }
    };
    if (m_devMode) {
        setActBtn(m_hud.exportToggle, "", 28);
        setActBtn(m_hud.inspectorToggle, "", 28);
        setActBtn(m_hud.moreToggle, "", 28);
    } else {
        setActBtn(m_hud.exportToggle, "导出", 44);
        setActBtn(m_hud.inspectorToggle, "详情", 44);
        setActBtn(m_hud.moreToggle, "更多", 44);
    }
    if (headDirty && m_hud.exportToggle) {
        if (ECDI::Widget* head = m_hud.exportToggle->GetParent()) head->Arrange();
    }
    // ★★ 2026-10-27：titles 宽度不再是常数 300/480 —— 固定值会把三钮挤出中栏
    //   （实测 1000 宽 + 详情开只剩「导出」半截）。收口到唯一计算口。
    //   ★ 必须排在 `setActBtn` **之后**：预留量按 btnW 现值算（用户模式 44 / 开发 28），
    //     先换钮宽再算，才不会差一个钮宽。
    SyncHeadTitlesWidth();
}

// ★★ 2026-10-27：会话头 titles 宽的**唯一**计算口（病因与取舍见 App.h 声明注释）。
//   为什么是「吃剩余」而不是「按文本量」：本函数会被 resize / 侧栏显隐 / 检视栏显隐 /
//   模式切换四条路径调用，任何 `MeasureText` 都会进 `m_evalMeasureCalls`（评估仪器，
//   与 AppSession.cpp:55-63 「修法 D」同一条纪律）；而三钮占的费用是纯算术 ⇒ 稳态
//   零测量、零 SetSize（同值守卫），只在宽度真变时动几何。
void HarnessApp::SyncHeadTitlesWidth() {
    if (!m_hud.exportToggle) return;
    ECDI::Widget* head = m_hud.exportToggle->GetParent();
    if (!head) return;

    // 预留 = 左右 padding + 三钮 + **四道**间距 + grow 的最小宽。
    //   间距数：`head` 有 5 个可见子（titles / grow / 三钮）⇒ 4 道（AppUiMain.cpp:295 起）。
    //   grow 是 `SetStretch(1)` 的填充块，但 HorizontalLayout 算「会不会溢出」时
    //   仍用它的 10px 占位 ⇒ 必须留出来，否则到头来还是差 10px 把末钮挤出去。
    const int btnW = m_devMode ? 28 : 44;
    const int cap = m_devMode ? 300 : 480;
    const int contExtra = (m_hud.continueBtn && m_hud.continueBtn->IsVisible())
                              ? (m_hud.continueBtn->GetWidth() + 8)
                              : 0;
    const int reserved = 2 * harness::kPadFrame + btnW * 3 + 8 * 4 + 10 + contExtra;

    int tw = head->GetWidth() - reserved;
    if (tw > cap) tw = cap;
    const int minTw = std::max(48, std::min(120, head->GetWidth() - reserved));
    if (tw < minTw) tw = minTw;   // 兜底：宁可标题被裁，也不让三钮/继续按钮消失

    bool dirty = false;
    if (m_hud.convTitle && m_hud.convTitle->GetWidth() != tw) {
        m_hud.convTitle->SetSize(tw, 22);
        dirty = true;
    }
    if (m_hud.convSub && m_hud.convSub->GetWidth() != tw) {
        m_hud.convSub->SetSize(tw, 18);
        dirty = true;
    }
    ECDI::Widget* titles = m_hud.convSub ? m_hud.convSub->GetParent() : nullptr;
    if (titles && titles->GetWidth() != tw) {
        titles->SetSize(tw, 44);
        dirty = true;
    }
    if (!dirty) return;
    if (titles) titles->Arrange();   // 子宽变了 → 竖排先归位
    head->Arrange();                 // 再让 HorizontalLayout 按新 titles 宽排三钮
}

void HarnessApp::SyncResponsiveColumns(bool allowListRebuild) {
    if (!m_window) return;
    ECDI::Widget& root = m_window->GetRootWidget();
    const int rootW = root.GetWidth();
    const int rootH = root.GetHeight();
    if (rootW <= 0 || rootH <= 0) return;

    // 1) 模型胶囊自适应内容宽，窄窗上限 130，宽窗上限 220。
    if (m_hud.modelChip) {
        UpdateModelChip();
    }

    // 2) 左右栏宽度按中栏保底宽（444px）响应式让位，绝不覆写 m_sidebarWidth / m_inspectorWidth。
    constexpr int kMinMidW = 444;
    constexpr int kMinSideW = 150;
    constexpr int kMinInspW = 180;
    const bool sideVis = m_hud.sidebar && m_hud.sidebar->IsVisible();
    const bool inspVis = m_hud.inspector && m_hud.inspector->IsVisible();
    const int splitW =
        ((m_hud.leftSplitter && m_hud.leftSplitter->IsVisible()) ? m_hud.leftSplitter->GetWidth() : 0) +
        ((m_hud.rightSplitter && m_hud.rightSplitter->IsVisible()) ? m_hud.rightSplitter->GetWidth() : 0);

    int effSideW = sideVis ? std::max(kMinSideW, m_sidebarWidth) : 0;
    int effInspW = inspVis ? std::max(kMinInspW, m_inspectorWidth) : 0;
    const int availForSides = std::max(0, rootW - splitW - kMinMidW);
    if (sideVis && inspVis) {
        if (effSideW + effInspW > availForSides) {
            const int excess = (effSideW + effInspW) - availForSides;
            const int sideSlack = std::max(0, effSideW - kMinSideW);
            const int inspSlack = std::max(0, effInspW - kMinInspW);
            const int totalSlack = sideSlack + inspSlack;
            if (totalSlack > 0) {
                int cutSide = std::min(sideSlack, (excess * sideSlack + totalSlack / 2) / totalSlack);
                int cutInsp = std::min(inspSlack, excess - cutSide);
                int rem = excess - cutSide - cutInsp;
                if (rem > 0 && sideSlack > cutSide) {
                    const int extra = std::min(rem, sideSlack - cutSide);
                    cutSide += extra;
                    rem -= extra;
                }
                if (rem > 0 && inspSlack > cutInsp) {
                    cutInsp += std::min(rem, inspSlack - cutInsp);
                }
                effSideW -= cutSide;
                effInspW -= cutInsp;
            }
            if (effSideW + effInspW > availForSides) {
                const int sum = effSideW + effInspW;
                if (sum > 0) {
                    effSideW = std::max(0, (availForSides * effSideW) / sum);
                    effInspW = std::max(0, availForSides - effSideW);
                }
            }
        }
    } else if (sideVis && effSideW > availForSides) {
        effSideW = std::max(0, availForSides);
    } else if (inspVis && effInspW > availForSides) {
        effInspW = std::max(0, availForSides);
    }

    bool mainDirty = false;
    bool sideLayoutDirty = false;
    bool inspDirty = false;
    if (sideVis && m_hud.sidebar->GetWidth() != effSideW) {
        m_hud.sidebar->SetSize(effSideW, m_hud.sidebar->GetHeight());
        mainDirty = true;
        sideLayoutDirty = true;
    }
    if (inspVis && m_hud.inspector->GetWidth() != effInspW) {
        m_hud.inspector->SetSize(effInspW, m_hud.inspector->GetHeight());
        mainDirty = true;
        inspDirty = true;
    }

    // 3) 短窗口（rootH < 540，如 880×460）收紧侧栏竖向间距与内边距，恢复 ≥540 后还原。
    const bool wantCompactVert = (rootH < 540);
    if (m_hud.sidebar && m_sidebarCompactVert != wantCompactVert) {
        m_sidebarCompactVert = wantCompactVert;
        if (wantCompactVert) {
            m_hud.sidebar->SetLayout(std::make_unique<ECDI::VerticalLayout>(2, true, 6));
        } else {
            m_hud.sidebar->SetLayout(std::make_unique<ECDI::VerticalLayout>(
                harness::kRowGapSide, true, harness::kSidebarPad));
        }
        m_hud.sidebar->Arrange();
        sideLayoutDirty = true;
    }

    if (mainDirty) {
        if (ECDI::Widget* main = m_hud.sidebar ? m_hud.sidebar->GetParent()
                                               : (m_hud.inspector ? m_hud.inspector->GetParent() : nullptr)) {
            main->Arrange();
        }
    }
    if (inspDirty) {
        SyncInspectorLayout();
    }
    if (sideLayoutDirty) {
        if (allowListRebuild && m_sess) {
            RebuildSessionList();
            RebuildHistoryList();
            if (m_hud.sidebar) m_hud.sidebar->Arrange();
        } else {
            m_sidebarDirty = true;
            m_historyDirty = true;
        }
    }
}

void HarnessApp::SetDevMode(bool dev) {
    if (m_devMode == dev) return;
    m_devMode = dev;
    // ★★ 缺口 5 闭合：按目标模式恢复其独立记忆的检视栏偏好，而非无条件覆盖成 dev。
    m_inspectorVisible = dev ? m_devInspectorVisible : m_userInspectorVisible;
    // ★★ 2026-10-27：显隐改走唯一落地口（模型态与控件永远同进同退）。
    //   排版宽不放这里管——那是 `TranscriptLayoutWidth()` 的账，Relayout 会跟着算。
    ApplyInspectorVisibility();
    SyncResponsiveColumns();
    if (m_hud.inspector) {
        if (ECDI::Widget* parent = m_hud.inspector->GetParent()) parent->Arrange();
    }
    ApplyDevModeVisibility();
    // ★★ 2026-11-03（P0）：隐显一变，列表的行宽就可能失效——必须重算。
    //   机制：`VerticalLayout` **跳过不可见子**（C-VIS-1），所以「历史」段在用户模式下
    //   根本没被 Arrange 过，`historyList->GetWidth()` 一直停在 `SetSize` 的 172；
    //   行宽是从它算出来的（`AppHistory.cpp` 的 `curRowW`）⇒ 切回开发者模式时
    //   列表被拉到栏宽、行却仍是 172−12，同一栏里再出现"行比视口窄一截"。
    //   ★ 只置标志、不在这里重建：本函数可能在派发栈上（顶栏菜单回调），
    //     拆树留给 `OnReplayTick` 顶部（同 `m_sidebarDirty` 的 UAF 纪律）。
    m_historyDirty = true;
    m_sidebarDirty = true;
    Relayout();
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
}

void HarnessApp::HudTouch(const char* name) {
    if (!m_probeState.hudAudit) return;
    m_probeState.hudTouched.emplace_back(name);
}

std::vector<std::string> HarnessApp::HudUnbuilt() const {
    // ★ "这个字段建过没有"直接看**指针本身**，不另维护"已建"表——
    //   指针非空 ⇔ 建树时赋过值，这是唯一可靠且不会漂移的判据。
    auto nonNull = [this](const std::string& n) -> bool {
        const Hud& h = m_hud;
        if (n == "title") return h.title != nullptr;
        if (n == "rowChip") return h.rowChip != nullptr;
        if (n == "backendChip") return h.backendChip != nullptr;
        if (n == "sessChip") return h.sessChip != nullptr;
        if (n == "modeChip") return h.modeChip != nullptr;
        if (n == "statusDot") return h.statusDot != nullptr;
        if (n == "sbDot") return h.sbDot != nullptr;
        if (n == "stateChip") return h.stateChip != nullptr;
        if (n == "sessProgress") return h.sessProgress != nullptr;
        if (n == "convTitle") return h.convTitle != nullptr;
        if (n == "convSub") return h.convSub != nullptr;
        if (n == "workspacePath") return h.workspacePath != nullptr;
        if (n == "workspaceStatus") return h.workspaceStatus != nullptr;
        if (n == "workspacePick") return h.workspacePick != nullptr;
        if (n == "historyList") return h.historyList != nullptr;
        if (n == "historyCount") return h.historyCount != nullptr;
        if (n == "sessList") return h.sessList != nullptr;
        if (n == "sessCount") return h.sessCount != nullptr;
        if (n == "healthSeq") return h.healthSeq != nullptr;
        if (n == "healthViol") return h.healthViol != nullptr;
        if (n == "healthKind") return h.healthKind != nullptr;
        if (n == "evalPaints") return h.evalPaints != nullptr;
        if (n == "probeCmds") return h.probeCmds != nullptr;
        if (n == "probeExec") return h.probeExec != nullptr;
        if (n == "usageInV") return h.usageInV != nullptr;
        if (n == "usageOutV") return h.usageOutV != nullptr;
        if (n == "ctxPct") return h.ctxPct != nullptr;
        if (n == "histChip") return h.histChip[0] != nullptr;
        if (n == "histWhat") return h.histWhat[0] != nullptr;
        if (n == "inspector") return h.inspector != nullptr;
        if (n == "inspectorToggle") return h.inspectorToggle != nullptr;
        if (n == "inspMemoryCard") return h.inspMemoryCard != nullptr;
        if (n == "memoryList") return h.memoryList != nullptr;
        if (n == "inspMcpCard") return h.inspMcpCard != nullptr;
        if (n == "mcpList") return h.mcpList != nullptr;
        if (n == "exportToggle") return h.exportToggle != nullptr;
        if (n == "moreToggle") return h.moreToggle != nullptr;
        if (n == "moreMenu") return h.moreMenu != nullptr;
        // ★ 不在表里 ⇒ 按**未建**处理（fail-closed：宁可报红，不可静默通过）。
        //   这条很关键：将来新增一个 Hud 字段却忘了加进这张表，判据会立刻红，
        //   而不是默默漏检——"表会漏项"是这套机制唯一的真实弱点，所以要让它
        //   **漏了就吵**。
        return false;
    };
    std::vector<std::string> out;
    std::vector<std::string> seen;
    for (const std::string& n : m_probeState.hudTouched) {
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        if (!nonNull(n)) out.push_back(n);
    }
    return out;
}

void HarnessApp::ResetHudCaches() {
    m_lastInspW = -1;
    m_lastInspH = -1;
    m_inspectorLayoutDirty = true;
    m_lastModelChipMaxW = -1;
    m_hudCachedTitle.clear();
    m_hudCachedRowChip.clear();
    m_hudCachedSessChip.clear();
    m_hudCachedModeChip.clear();
    m_hudCachedBackendChip.clear();
    m_hudCachedConvTitle.clear();
    m_hudCachedConvSub.clear();
    m_hudCachedWsPath.clear();
    m_hudCachedWsStatus.clear();
    m_hudCachedWsFontDev = -1;
    m_hudCachedWsPickEnabled = -1;
    m_hudCachedStateChip.clear();
    m_hudCachedStatusDotState = -1;
    m_hudCachedSessCount.clear();
    m_hudCachedSessProgress.clear();
    m_hudCachedReplayProg = -1.0f;
    m_hudCachedHealthSeq.clear();
    m_hudCachedHealthSeqGap = -1;
    m_hudCachedHealthViol.clear();
    m_hudCachedHealthKind.clear();
    m_hudCachedHealthKindNz = -1;
    m_hudCachedEvalPaints.clear();
    m_hudCachedProbeCmds.clear();
    m_hudCachedProbeExec.clear();
    m_hudCachedUsageIn.clear();
    m_hudCachedUsageOut.clear();
    m_hudCachedCtxPct.clear();
    m_hudCachedCtxPctLevel = -1;
    m_hudCachedCtxFrac = -1.0f;
    m_hudCachedInFrac = -1.0f;
    m_hudCachedOutFrac = -1.0f;
    for (int i = 0; i < 3; ++i) {
        m_hudCachedHistChip[i].clear();
        m_hudCachedHistWhat[i].clear();
    }
    m_hudCachedStState.clear();
    m_hudCachedStMsgs.clear();
    m_hudCachedStTokens.clear();
    m_hudCachedStPaint.clear();
}

// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。
void HarnessApp::RefreshHud() {
    m_hudDirty = false;
    if (!m_hud.title) return;

    if (m_hudCachedTitle != "harness") {
        m_hudCachedTitle = "harness";
        m_hud.title->SetText(m_hudCachedTitle);
    }
    if (HudTouch("rowChip"), m_hud.rowChip) {
        const std::string want = m_options.rowModeList ? "listlayout" : "autosize";
        if (m_hudCachedRowChip != want) {
            m_hudCachedRowChip = want;
            m_hud.rowChip->SetText(want);
        }
    }
    if (HudTouch("sessChip"), m_hud.sessChip) {
        const std::string want = m_sess->sessionId.empty() ? "s-____" : m_sess->sessionId;
        if (m_hudCachedSessChip != want) {
            m_hudCachedSessChip = want;
            m_hud.sessChip->SetText(want);
        }
    }
    if (HudTouch("modeChip"), m_hud.modeChip) {
        const std::string& origin = m_sess->originKind;
        const char* modeText = "empty";
        if (origin == "live") {
            modeText = "live";
        } else if (origin == "replay") {
            modeText = "replay";
        } else if (origin == "replay-history") {
            modeText = "history";
        }
        if (m_hudCachedModeChip != modeText) {
            m_hudCachedModeChip = modeText;
            m_hud.modeChip->SetText(modeText);
        }
    }
    if (HudTouch("backendChip"), m_hud.backendChip) {
        if (m_hudCachedBackendChip != m_backendName) {
            m_hudCachedBackendChip = m_backendName;
            m_hud.backendChip->SetText(m_backendName);
        }
    }
    if (HudTouch("convTitle"), m_hud.convTitle) {
        const std::string want = m_sess->label.empty() ? ConversationTitle() : m_sess->label;
        if (m_hudCachedConvTitle != want) {
            m_hudCachedConvTitle = want;
            m_hud.convTitle->SetText(want);
        }
    }
    if (HudTouch("convSub"), m_hud.convSub) {
        std::string sub;
        if (m_devMode) {
            sub = "core " + (m_sess->coreVer.empty() ? std::string("—") : m_sess->coreVer);
            if (!m_sess->sessionId.empty()) sub += " · " + m_sess->sessionId;
            sub += " · protocol " + std::to_string(m_sess->protocolVer);
            if (!m_sess->memories.empty()) {
                sub += " · 记忆 " + std::to_string(m_sess->memories.size());
            }
            if (std::string mcpText = McpStatusText(); !mcpText.empty()) {
                sub += " · " + mcpText;
            }
            sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
                   std::to_string(m_sessions.size());
        } else {
            if (!m_sess->workspacePath.empty()) {
                sub = "工作区：" + m_sess->workspacePath;
            } else if (!m_sess->providerId.empty()) {
                sub = ProviderDisplayLabel(m_sess->providerId) + " › " +
                      (m_sess->modelName.empty() ? "?" : m_sess->modelName);
            } else {
                sub = "未连接模型 — 点击输入框左下角模型胶囊或按 Ctrl+, 配置";
            }
            if (!m_sess->memories.empty()) {
                size_t loaded = 0;
                for (const auto& m : m_sess->memories) {
                    if (!m.skipped) loaded++;
                }
                sub += " · 记忆 " + std::to_string(loaded);
            }
            if (std::string mcpText = McpStatusText(); !mcpText.empty()) {
                sub += " · " + mcpText;
            }
            if (m_sessions.size() > 1) {
                sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
                       std::to_string(m_sessions.size());
            }
        }
        if (m_hudCachedConvSub != sub) {
            m_hudCachedConvSub = sub;
            m_hud.convSub->SetText(sub);
        }
    }
    const std::string stateText =
        m_sess->coreState.empty() ? std::string("idle") : m_sess->coreState;
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
    const bool live = m_sess->writer != nullptr;
    const bool liveReady = live && m_sess->protocolVer >= 3 &&
                           !m_sess->protocolIncompatible && !m_sess->disconnected &&
                           !m_sess->liveEof;
    const bool workspaceReady = !m_sess->workspaceCap || m_sess->workspaceSyncComplete;
    const bool sendEnabled = live
        ? (liveReady && workspaceReady && m_sess->coreState == "idle" &&
           m_sess->pendingWorkspaceId.empty() && !HasUnconfirmedPrompt())
        : !active;
    const bool stopEnabled = live ? (liveReady && active) : active;
    const bool isJournalReadOnly = (m_sess && m_sess->originKind == "journal");
    if (m_hud.continueBtn) {
        const bool wasVis = m_hud.continueBtn->IsVisible();
        m_hud.continueBtn->SetVisible(isJournalReadOnly);
        if (wasVis != isJournalReadOnly) {
            if (ECDI::Widget* head = m_hud.continueBtn->GetParent()) head->Arrange();
            SyncHeadTitlesWidth();
        }
    }
    if (m_promptBar) {
        m_promptBar->SetReadOnlyMode(isJournalReadOnly);
        if (isJournalReadOnly) {
            m_promptBar->SetButtonStates(false, false);
            m_promptBar->SetHint("只读会话不可输入，点「继续」开启新对话", kWarn);
        } else {
            m_promptBar->SetButtonStates(sendEnabled, stopEnabled);
            if (m_devMode) {
                m_promptBar->SetHint("Enter 发送 · Shift+Enter 换行", kDarkMuted);
            } else if (stateText == "waiting" || m_sess->waitForAnswer) {
                m_promptBar->SetHint("● 请先在上方对话中点击「允许」或「拒绝」", kWarn);
            } else if (stateText == "running") {
                m_promptBar->SetHint("● 正在生成回复… 可点击右侧「停止」中断", kDarkMuted);
            } else {
                m_promptBar->SetHint("Enter 发送 · Shift+Enter 换行 · Ctrl+Shift+D 切换视图", kDarkMuted);
            }
        }
    }
    const bool pickerEnabled = m_sess->workspaceCap && liveReady &&
                               m_sess->coreState == "idle" &&
                               m_sess->pendingWorkspaceId.empty();
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
    if (workspaceVisChanged) {
        if (ECDI::Widget* p = m_hud.workspaceStatus ? m_hud.workspaceStatus->GetParent()
                                                    : nullptr) {
            p->Arrange();
        }
    }
    if (HudTouch("workspacePath"), m_hud.workspacePath) {
        const int wantDev = m_devMode ? 1 : 0;
        if (m_hudCachedWsFontDev != wantDev) {
            m_hudCachedWsFontDev = wantDev;
            m_hud.workspacePath->SetFont(
                m_devMode ? harness::MonoFont(12.0f) : harness::SansFont(harness::kFontMicro));
        }
        std::string wantPath;
        if (m_devMode) {
            wantPath = m_sess->workspacePath.empty()
                           ? "core 实际工作区：—"
                           : "core 实际工作区：" + m_sess->workspacePath;
        } else {
            wantPath = m_sess->workspacePath.empty()
                           ? "尚未选择工作区目录"
                           : m_sess->workspacePath;
        }
        if (m_hudCachedWsPath != wantPath) {
            m_hudCachedWsPath = wantPath;
            m_hud.workspacePath->SetText(wantPath);
        }
    }
    if (HudTouch("workspaceStatus"), m_hud.workspaceStatus) {
        std::string wantStatus;
        if (m_devMode) {
            wantStatus = m_sess->workspaceStatus;
        } else {
            const bool hasPath = !m_sess->workspacePath.empty();
            if (m_hud.workspaceStatus->IsVisible() != hasPath) {
                m_hud.workspaceStatus->SetVisible(hasPath);
                if (m_hud.sidebar) m_hud.sidebar->Arrange();
            }
            const bool synced = hasPath && m_sess->workspaceStatus.find("已确认") == 0;
            wantStatus = synced ? "已就绪" : m_sess->workspaceStatus;
        }
        if (m_hudCachedWsStatus != wantStatus) {
            m_hudCachedWsStatus = wantStatus;
            m_hud.workspaceStatus->SetText(wantStatus);
        }
    }
    if (HudTouch("workspacePick"), m_hud.workspacePick) {
        const int wantPick = pickerEnabled ? 1 : 0;
        if (m_hudCachedWsPickEnabled != wantPick) {
            m_hudCachedWsPickEnabled = wantPick;
            m_hud.workspacePick->SetEnabled(pickerEnabled);
            m_hud.workspacePick->SetTextColor(pickerEnabled ? kDarkInk : kDarkMuted);
        }
    }
    if (m_options.stateProbe && m_promptBar) {
        ECDI::Button* send = m_promptBar->sendButton();
        ECDI::Button* stop = m_promptBar->stopButton();
        ECDI::Button* pick = m_hud.workspacePick;
        if (send && stop) {
            const bool sendEnabled = send->IsEnabled();
            const bool stopEnabled = stop->IsEnabled();
            const std::string sendTitle = send->GetText();
            const bool pickerEnabledNow = pick ? pick->IsEnabled() : false;
            const bool pickerVisibleNow = pick ? pick->IsVisible() : false;
            const bool changed = !m_probeState.stateProbeLastValid ||
                                 sendEnabled != m_probeState.stateProbeLastSend ||
                                 stopEnabled != m_probeState.stateProbeLastStop ||
                                 pickerEnabledNow != m_probeState.stateProbeLastPicker;
            if (changed) {
                StateProbeRec rec;
                rec.coreState = m_sess->coreState.empty() ? "idle" : m_sess->coreState;
                rec.sendEnabled = sendEnabled;
                rec.stopEnabled = stopEnabled;
                rec.sendTitle = sendTitle;
                rec.pickerEnabled = pickerEnabledNow;
                rec.pickerVisible = pickerVisibleNow;
                rec.workspaceCap = m_sess->workspaceCap;
                rec.protocolVer = m_sess->protocolVer;
                rec.modelCap = m_sess->hasModelCap;
                m_probeState.stateProbeLog.push_back(std::move(rec));
                m_probeState.stateProbeLastSend = sendEnabled;
                m_probeState.stateProbeLastStop = stopEnabled;
                m_probeState.stateProbeLastPicker = pickerEnabledNow;
                m_probeState.stateProbeLastValid = true;
            }
        }
        if (m_sess->writer && !m_sess->hasModelCap && m_sess->protocolVer > 0 &&
            m_sess->coreState == "idle" && !m_probeState.capRefusalTried) {
            m_probeState.capRefusalTried = true;
            m_probeState.capRefusalModelCap = m_sess->hasModelCap;
            RequestModelSwitch("caprefusal-probe", "refusal-target");
            bool notice = false;
            for (const RowRec& r : m_sess->rows) {
                if (r.label &&
                    r.label->GetText().find("未声明 model 能力") != std::string::npos) {
                    notice = true;
                    break;
                }
            }
            m_probeState.capRefusalNotice = notice;
            m_probeState.capRefusalNoPending =
                m_sess->pendingModelId.empty() && m_sess->queuedModelProvider.empty();
        }
    }
    if (HudTouch("stateChip"), m_hud.stateChip) {
        std::string chipText = stateText;
        if (!m_devMode) {
            const char* friendly = "就绪";
            if (stateText == "running") friendly = "思考中…";
            else if (stateText == "waiting") friendly = "等待授权";
            chipText = friendly;
        }
        if (size_t activeAgents = ActiveAgentCount(); activeAgents > 0) {
            chipText += " " + std::to_string(activeAgents) + "代理";
        }
        if (size_t activeJobs = ActiveJobCount(); activeJobs > 0) {
            chipText += " " + std::to_string(activeJobs) + "后台";
        }
        if (m_hudCachedStateChip != chipText) {
            m_hudCachedStateChip = chipText;
            m_hud.stateChip->SetText(chipText);
            if (m_hud.stateChip->GetTextColor() != kDarkMuted) {
                m_hud.stateChip->SetTextColor(kDarkMuted);
            }
        }
    }
    const int dotState = active ? 1 : 0;
    const bool dotChanged = (m_hudCachedStatusDotState != dotState);
    if (HudTouch("statusDot"), m_hud.statusDot) {
        if (dotChanged) {
            m_hud.statusDot->SetStyle(ECDI::PanelStyleOverride{
                .background = active ? kOkDark : kDarkMuted, .cornerRadius = 4.0f});
        }
    }
    if (HudTouch("sbDot"), m_hud.sbDot) {
        if (dotChanged) {
            m_hud.sbDot->SetStyle(ECDI::PanelStyleOverride{
                .background = active ? kOkDark : kDarkMuted, .cornerRadius = 3.5f});
        }
    }
    if (dotChanged) {
        m_hudCachedStatusDotState = dotState;
    }

    if (HudTouch("sessCount"), m_hud.sessCount) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%zu 个会话", m_sessions.size());
        if (m_hudCachedSessCount != buf) {
            m_hudCachedSessCount = buf;
            m_hud.sessCount->SetText(m_hudCachedSessCount);
        }
    }
    if (HudTouch("sessProgress"), m_hud.sessProgress) {
        char buf[64];
        if (m_sess->replay) {
            std::snprintf(buf, sizeof(buf), "%zu / %zu 条", m_sess->replay->Consumed(),
                          m_sess->replay->Total());
        } else if (m_sess->source) {
            std::snprintf(buf, sizeof(buf), "%zu 条（live）", m_sess->liveLines);
        } else {
            std::snprintf(buf, sizeof(buf), "%s", "空会话 · 无 core");
        }
        if (m_hudCachedSessProgress != buf) {
            m_hudCachedSessProgress = buf;
            m_hud.sessProgress->SetText(m_hudCachedSessProgress);
        }
    }
    if (m_replayBar) {
        float wantProg = 0.0f;
        if (m_sess->replay) {
            const size_t total = m_sess->replay->Total();
            wantProg = total > 0
                           ? static_cast<float>(m_sess->replay->Consumed()) /
                                 static_cast<float>(total)
                           : 0.0f;
        }
        if (wantProg != m_hudCachedReplayProg) {
            m_hudCachedReplayProg = wantProg;
            m_replayBar->SetProgress(wantProg);
        }
    }

    if (HudTouch("healthSeq"), m_hud.healthSeq) {
        const size_t gaps = m_sess->transcript.SeqGaps();
        const int wantGapFlag = (gaps == 0) ? 0 : 1;
        const std::string wantText =
            (gaps == 0) ? std::string("seq 连续 · 0 缺口")
                        : ("seq 缺口 " + std::to_string(gaps));
        if (m_hudCachedHealthSeq != wantText) {
            m_hudCachedHealthSeq = wantText;
            m_hud.healthSeq->SetText(wantText);
        }
        if (m_hudCachedHealthSeqGap != wantGapFlag) {
            m_hudCachedHealthSeqGap = wantGapFlag;
            m_hud.healthSeq->SetTextColor(gaps == 0 ? kDarkInk : kErrorDark);
        }
    }
    if (HudTouch("healthViol"), m_hud.healthViol) {
        const std::string want = "违规 " + std::to_string(m_sess->transcript.Violations()) +
                                 " · 未知 " + std::to_string(m_sess->transcript.UnknownTypes());
        if (m_hudCachedHealthViol != want) {
            m_hudCachedHealthViol = want;
            m_hud.healthViol->SetText(want);
        }
    }
    if (HudTouch("healthKind"), m_hud.healthKind) {
        const size_t uk = m_sess->transcript.UnknownKinds();
        const int wantUkFlag = (uk == 0) ? 0 : 1;
        const std::string wantText =
            (uk == 0) ? std::string("未知 kind · 0")
                      : ("未知 kind · " + std::to_string(uk) + " △");
        if (m_hudCachedHealthKind != wantText) {
            m_hudCachedHealthKind = wantText;
            m_hud.healthKind->SetText(wantText);
        }
        if (m_hudCachedHealthKindNz != wantUkFlag) {
            m_hudCachedHealthKindNz = wantUkFlag;
            m_hud.healthKind->SetTextColor(uk == 0 ? kDarkMuted : kWarn);
        }
    }
    if (!m_devMode && m_hud.inspHealthCard) {
        const bool hasIssue = (m_sess->transcript.SeqGaps() > 0 ||
                               m_sess->transcript.Violations() > 0 ||
                               m_sess->transcript.UnknownTypes() > 0 ||
                               m_sess->transcript.UnknownKinds() > 0);
        if (m_hud.inspHealthCard->IsVisible() != hasIssue) {
            m_hud.inspHealthCard->SetVisible(hasIssue);
            SyncInspectorLayout();
        }
    }
    if (HudTouch("evalPaints"), m_hud.evalPaints) {
        const std::string want = std::to_string(m_evalPaints) + " 次 · 请求 " +
                                 std::to_string(m_invalidations);
        if (m_hudCachedEvalPaints != want) {
            m_hudCachedEvalPaints = want;
            m_hud.evalPaints->SetText(want);
        }
    }
    if (HudTouch("probeCmds"), m_hud.probeCmds) {
        const std::string want = (m_options.cullProbe && m_probe)
                                     ? ("avg " + std::to_string(m_probeState.evalProbeAvg))
                                     : std::string("—");
        if (m_hudCachedProbeCmds != want) {
            m_hudCachedProbeCmds = want;
            m_hud.probeCmds->SetText(want);
        }
    }
    if (HudTouch("probeExec"), m_hud.probeExec) {
        const std::string want =
            (m_options.cullProbe && m_probe)
                ? ("avg " + std::to_string(static_cast<unsigned long long>(m_evalAvgExecUs)) +
                   " us")
                : std::string("—");
        if (m_hudCachedProbeExec != want) {
            m_hudCachedProbeExec = want;
            m_hud.probeExec->SetText(want);
        }
    }
    if (HudTouch("usageInV"), m_hud.usageInV) {
        const std::string want =
            m_sess->usageIn < 0 ? std::string("—") : std::to_string(m_sess->usageIn);
        if (m_hudCachedUsageIn != want) {
            m_hudCachedUsageIn = want;
            m_hud.usageInV->SetText(want);
        }
    }
    if (HudTouch("usageOutV"), m_hud.usageOutV) {
        const std::string want =
            m_sess->usageOut < 0 ? std::string("—") : std::to_string(m_sess->usageOut);
        if (m_hudCachedUsageOut != want) {
            m_hudCachedUsageOut = want;
            m_hud.usageOutV->SetText(want);
        }
    }
    UpdateModelChip();
    const bool ctxKnown = (m_sess->ctxCapacity > 0 && m_sess->usageIn >= 0);
    if (HudTouch("ctxPct"), m_hud.ctxPct) {
        if (!ctxKnown) {
            if (m_hudCachedCtxPct != "—") {
                m_hudCachedCtxPct = "—";
                m_hud.ctxPct->SetText("—");
            }
            if (m_hudCachedCtxPctLevel != 0) {
                m_hudCachedCtxPctLevel = 0;
                m_hud.ctxPct->SetTextColor(kDarkMuted);
            }
        } else {
            const long long pct =
                (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity;
            const std::string wantPct = std::to_string(pct) + "%";
            const int wantLevel = pct >= 90 ? 3 : (pct >= 70 ? 2 : 1);
            if (m_hudCachedCtxPct != wantPct) {
                m_hudCachedCtxPct = wantPct;
                m_hud.ctxPct->SetText(wantPct);
            }
            if (m_hudCachedCtxPctLevel != wantLevel) {
                m_hudCachedCtxPctLevel = wantLevel;
                m_hud.ctxPct->SetTextColor(pct >= 90 ? kErrorDark
                                                     : (pct >= 70 ? kWarn : kOkDark));
            }
        }
    }
    if (m_ctxRing) {
        const float rawFrac = ctxKnown
                                  ? static_cast<float>(m_sess->usageIn) /
                                        static_cast<float>(m_sess->ctxCapacity)
                                  : 0.0f;
        const float clampedFrac = rawFrac > 1.0f ? 1.0f : rawFrac;
        if (clampedFrac != m_hudCachedCtxFrac) {
            m_hudCachedCtxFrac = clampedFrac;
            m_ctxRing->SetProgress(clampedFrac);
        }
        const long long pctForColor =
            ctxKnown ? (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity : 0;
        const int level = pctForColor >= 90 ? 2 : (pctForColor >= 70 ? 1 : 0);
        if (level != m_ctxRingLevel) {
            m_ctxRingLevel = level;
            m_ctxRing->SetStyle(ECDI::RingProgressBarStyleOverride{
                .fillColor = level == 2 ? kErrorDark
                                        : (level == 1 ? kWarn : harness::kOkDark)});
            RequestRepaint();
        }
    }
    if (m_usageBarIn && m_usageBarOut) {
        const long long denom = (m_sess->contextLimit > 0)
                                    ? m_sess->contextLimit
                                    : std::max(m_sess->usageIn, m_sess->usageOut);
        const float inRaw = (m_sess->usageIn >= 0 && denom > 0)
                                ? static_cast<float>(m_sess->usageIn) / static_cast<float>(denom)
                                : 0.0f;
        const float outRaw = (m_sess->usageOut >= 0 && denom > 0)
                                 ? static_cast<float>(m_sess->usageOut) / static_cast<float>(denom)
                                 : 0.0f;
        const float inFrac = inRaw > 1.0f ? 1.0f : inRaw;
        const float outFrac = outRaw > 1.0f ? 1.0f : outRaw;
        if (inFrac != m_hudCachedInFrac) {
            m_hudCachedInFrac = inFrac;
            m_usageBarIn->SetProgress(inFrac);
        }
        if (outFrac != m_hudCachedOutFrac) {
            m_hudCachedOutFrac = outFrac;
            m_usageBarOut->SetProgress(outFrac);
        }
    }
    const bool noHist = m_sess->askHist.empty();
    for (size_t i = 0; i < 3; ++i) {
        if (m_hud.histChip[i] && m_hud.histWhat[i]) {
            const size_t n = m_sess->askHist.size();
            std::string wantChip;
            std::string wantWhat;
            ECDI::Color wantColor = kDarkMuted;
            if (i < n) {
                const auto& [choice, what] = m_sess->askHist[n - 1 - i];
                wantChip = choice;
                wantWhat = what;
                wantColor = (choice == "allow") ? kOkDark : kErrorDark;
            } else {
                wantChip = (noHist && i == 0) ? "—" : "";
                wantWhat = (noHist && i == 0) ? "暂无权限请求" : "";
            }
            if (m_hudCachedHistChip[i] != wantChip) {
                m_hudCachedHistChip[i] = wantChip;
                m_hud.histChip[i]->SetText(wantChip);
            }
            if (m_hud.histChip[i]->GetTextColor() != wantColor) {
                m_hud.histChip[i]->SetTextColor(wantColor);
            }
            if (m_hudCachedHistWhat[i] != wantWhat) {
                m_hudCachedHistWhat[i] = wantWhat;
                m_hud.histWhat[i]->SetText(wantWhat);
            }
        }
    }

    if (m_hud.stState) {
        const std::string want = stateText + " · protocol " +
                                 std::to_string(m_sess->protocolVer);
        if (m_hudCachedStState != want) {
            m_hudCachedStState = want;
            m_hud.stState->SetText(want);
        }
    }
    if (m_hud.stMsgs) {
        std::string want;
        if (m_sess->replay) {
            want = std::to_string(m_sess->replay->Consumed()) + " / " +
                   std::to_string(m_sess->replay->Total()) + " msg";
        } else {
            want = std::to_string(m_sess->liveLines) + " msg（live）";
        }
        if (m_hudCachedStMsgs != want) {
            m_hudCachedStMsgs = want;
            m_hud.stMsgs->SetText(want);
        }
    }
    if (m_hud.stTokens) {
        std::string want;
        if (m_sess->usageIn < 0) {
            want = "tokens —";
        } else {
            want = "tokens " + std::to_string(m_sess->usageIn) + " / " +
                   std::to_string(m_sess->usageOut);
        }
        if (m_hudCachedStTokens != want) {
            m_hudCachedStTokens = want;
            m_hud.stTokens->SetText(want);
        }
    }
    if (m_hud.stPaint) {
        const std::string want = "paints " + std::to_string(m_evalPaints) + " · 上次 " +
                                 std::to_string(m_evalLastUs) + " us";
        if (m_hudCachedStPaint != want) {
            m_hudCachedStPaint = want;
            m_hud.stPaint->SetText(want);
        }
    }
}

// ── L3 记忆文件卡片重建 ──────────────────────────────────────────
void HarnessApp::RebuildMemoryList() {
    if (!m_hud.memoryList) return;
    while (m_hud.memoryList->GetChildCount() > 0) {
        m_hud.memoryList->RemoveChild(m_hud.memoryList->GetChildAt(0));
    }
    const Session* s = m_sess;
    const bool hasMemories = s && !s->memories.empty();
    if (m_hud.inspMemoryCard) {
        if (m_hud.inspMemoryCard->IsVisible() != hasMemories) {
            m_hud.inspMemoryCard->SetVisible(hasMemories);
            SyncInspectorLayout();
        }
    }
    if (!hasMemories) return;

    for (const auto& m : s->memories) {
        auto row = std::make_unique<ECDI::Panel>();
        row->SetSize(176, 18);
        row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(4, false, 0));
        auto nameLbl = std::make_unique<ECDI::Label>(m.name);
        nameLbl->SetFont(harness::SansFont(harness::kFontMicro));
        nameLbl->SetSize(107, 18);
        nameLbl->SetStretch(1);
        if (m.skipped) {
            nameLbl->SetTextColor(kWarn);
        } else {
            nameLbl->SetTextColor(kDarkInk);
        }
        row->AddChild(std::move(nameLbl));

        std::string metaStr;
        if (m.skipped) {
            metaStr = "[超限跳过]";
        } else {
            if (m.bytes >= 1024) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(m.bytes) / 1024.0);
                metaStr = buf;
            } else {
                metaStr = std::to_string(m.bytes) + " B";
            }
        }
        auto metaLbl = std::make_unique<ECDI::Label>(metaStr);
        metaLbl->SetFont(harness::MonoFont(12.0f));
        metaLbl->SetSize(65, 18);
        metaLbl->SetTextColor(m.skipped ? kWarn : kDarkMuted);
        row->AddChild(std::move(metaLbl));

        m_hud.memoryList->AddChild(std::move(row));
    }

    const int nMem = static_cast<int>(s->memories.size());
    const int listH = nMem * 18 + (nMem > 0 ? (nMem - 1) * 4 : 0);
    if (m_hud.inspMemoryCard) {
        const int cardW = std::max(176, m_hud.inspMemoryCard->GetWidth());
        m_hud.inspMemoryCard->SetSize(cardW, 49 + listH);
        m_hud.memoryList->SetSize(std::max(152, cardW - 24), listH);
        m_hud.inspMemoryCard->Arrange();
    }
    SyncInspectorLayout();
    RequestRepaint();
}

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendStateProbeSummary(std::string& json) {
    // ★★ REQ-04 机读证据（2026-10-14）：状态翻转轨迹。
    //   每条 = {core state, 发送使能, 停止使能, 发送标题}——**只在变化时**记，
    //   故可逐行核对协议 §6「发送/停止只由 state 驱动」。
    //   ★ 标题文本来自 `Button::GetText()` **直接回读**（不靠像素/截图）。
    json += "  \"stateProbe\": [";
    for (size_t i = 0; i < m_probeState.stateProbeLog.size(); ++i) {
        const StateProbeRec& r = m_probeState.stateProbeLog[i];
        json += (i ? ", " : "");
        json += "{\"state\": \"" + r.coreState + "\", \"sendEnabled\": " +
                (r.sendEnabled ? "true" : "false") + ", \"stopEnabled\": " +
                (r.stopEnabled ? "true" : "false") + ", \"sendTitle\": \"" +
                r.sendTitle + "\", \"pickerEnabled\": " +
                (r.pickerEnabled ? "true" : "false") + ", \"pickerVisible\": " +
                (r.pickerVisible ? "true" : "false") + ", \"workspaceCap\": " +
                (r.workspaceCap ? "true" : "false") + ", \"modelCap\": " +
                (r.modelCap ? "true" : "false") + ", \"protocolVer\": " +
                std::to_string(r.protocolVer) + "}";
    }
    json += "],\n";
    // ★★ 2026-11-07（2d-b 第四臂）：model-cap 拒绝侧 live 断言（只在试过时输出）。
    if (m_probeState.capRefusalTried) {
        json += "  \"stateProbeCapRefusal\": {\"tried\": true, \"modelCap\": " +
                std::string(m_probeState.capRefusalModelCap ? "true" : "false") +
                ", \"notice\": " +
                std::string(m_probeState.capRefusalNotice ? "true" : "false") +
                ", \"noPending\": " +
                std::string(m_probeState.capRefusalNoPending ? "true" : "false") + "},\n";
    }
}

