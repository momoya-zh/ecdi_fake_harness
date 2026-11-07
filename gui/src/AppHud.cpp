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
                if (const Block* b = m_sess->transcript.FindBlock(r.blockId)) {
                    if (!b->ended || !r.ended) {
                        turnHasStreamingBlock = true;
                    } else if (b->kind == "assistant" && !b->text.empty() && r.copyBtn) {
                        lastEndedAssistantIdx = turnEnd;
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
                r.copyBtn->SetVisible(k == visibleIdx);
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

    if (m_options.rowModeList) {
        // listlayout：行高固定、不测量；Arrange 由 ListLayout 完成
        for (RowRec& r : m_sess->rows) {
            if (r.panel->GetHeight() != kListRowHeight) {
                r.panel->SetSize(layoutW, kListRowHeight);
            }
        }
        for (PendingPromptView& view : m_pendingPromptViews) {
            if (view.row.panel && view.row.panel->GetHeight() != kListRowHeight) {
                view.row.panel->SetSize(layoutW, kListRowHeight);
            }
        }
        m_content->Arrange();
        m_scroll->UpdateContentExtent();
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
    // ★ v5：改成**带下标**的遍历，唯一目的是让竖线能看一眼"下一行是不是同一个代理"
    //   （见下方 gutter 高度）。行数 O(n)，多一次比较不改变复杂度。
    for (size_t rowIdx = 0; rowIdx < m_sess->rows.size(); ++rowIdx) {
        RowRec& r = m_sess->rows[rowIdx];
        // ★★ 2026-11-06（回合级折叠）：被折叠过程行**不参与排版**，`y` 也不推进。
        //   ★ 为什么必须在这里跳过、而不是只靠 `SetVisible(false)`：`ListLayout`
        //     会跳过不可见子控件（那是 list 模式那半边的全部理由），但 autosize
        //     这条路是**本函数手算 y 的**——它根本不问布局器。不跳过就只是"看不见
        //     但高度还在"，屏上留下一个与折叠内容等高的空洞（回放实测的最初形态）。
        //   ★ 为什么同时还要 `SetVisible(false)`：命中测试/绘制/`Arrange` 在
        //     list 模式由布局器负责，可见性必须由控件自己持有。
        if (r.foldHidden) {
            if (r.panel) {
                r.panel->SetVisible(false);
                // ★★ 2026-11-06（订正）：被折叠的行**仍然要横向定位**。
                //   只 `continue` 会让面板停在建行期的默认 x=0 ⇒ 任何"测行的横移"
                //   的读数都会读到一个假值：`--agent-probe` 的 `indentPx` 实测变成
                //   0 − canvasX = **−18**（子代理行被折起来时），于是它的元断言
                //   `indentPx == kAgentIndent` 无故变红，note 里却看不出是折叠引起的。
                //   纵向 y 不参与（不可见、不占位），所以这里只下发横坐标；
                //   `y` 照旧不推进。
                r.panel->SetPosition(canvasX0 + r.indent, y);
            }
            continue;
        }
        // ★★ 协议 v5（2026-11-04）：子代理行右移一档。**只改宽度与横坐标**——
        //   `y` 的累加、行高的求解路径一个字不动（缩进是一次减法，不是一层新布局）。
        //   ★ 这里刻意用**同名遮蔽**：`innerW0`/`canvasX0` 是整栏的，`innerW`/`canvasX`
        //     是本行的。循环体内所有既有算式因此逐字不变地变成"按本行宽"，
        //     不可能漏改某一处——漏一处就是「面板按版心宽、文字按缩进宽」的错位，
        //     而那种错位只在有子代理的行上出现，既有探针一条都抓不到。
        const int innerW = std::max(160, innerW0 - r.indent);
        const int canvasX = canvasX0 + r.indent;
        int rowH = 0;
        // ★★ 2026-11-06：回合摘要条 = 单行通栏标题，无折叠体、无换行。
        //   高度与工具行"收起态"取同一个数（kHeaderH + 上下各 2），
        //   因为它在同一个槽位上与那条工具行互斥出现。
        if (r.foldBar) {
            if (r.headerBtn) {
                r.headerBtn->SetSize(std::max(80, innerW - 2 * kRowPadX), kHeaderH);
                r.headerBtn->SetPosition(kRowPadX, 2);
            }
            rowH = 2 + kHeaderH + 2;
            if (r.panel) {
                r.panel->SetSize(innerW, rowH);
                r.panel->SetPosition(canvasX, y);
            }
            y += rowH + kRowGap;
            continue;
        }
        // ★ 协议 v2：思维链卡与工具卡**同一套几何**（通栏 + 折叠体），故共用本分支
        if (r.tool || r.reasoning || r.ask) {
            const int bodyW = innerW - 2 * kRowPadX;
            const int wrapW = std::max(40, bodyW - 20);
            int bodyH = 0;
            if (r.label) {
                // Phase 29 wrap：工具体按 bodyW 宽断行；★ 宽度不变不 SetSize——
                // 无条件 SetSize 会打穿 C29-10 wrap 布局缓存（指纹含宽度），
                // 逐帧重建 = 4.8s 测量成本事故（2026-10-06 实测）
                if (r.lastWrapW != wrapW) {
                    r.label->SetSize(wrapW, r.label->GetHeight());
                    // ★★ 协议 v5（2026-11-05）：提案行宽随行宽统一下发。高度定死
                    //   kProposalLineH（mono 不折行，宽=裁剪界），只有宽度随行变。
                    for (ECDI::Label* l : r.proposal.lines) {
                        if (l) l->SetSize(wrapW, kProposalLineH);
                    }
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
                // ★★ 协议 v5（2026-11-05）：提案行是**定高乘法**——行数在建行期
                //   定死进 `rec.proposal.lines`（内容不随宽度变），这里零测量。
                //   问句行 AutoSize 之后接 N 行 kProposalLineH，公式的其余部分不动。
                bodyH += static_cast<int>(r.proposal.lines.size()) * kProposalLineH;
            }
            r.bodyH = bodyH;
            if (r.headerBtn) {
                // ★★ 2026-10-27：权限门的 header 必须给右侧两钮**让位**（按钮不进折叠体，
                //   见 BuildAskRow 的注释）。168 = 按钮区 160（`innerW-160` 起到右边缘）
                //   + 8 间隙；不留这一段，摘要文字会被不透明按钮盖掉一截。
                const int headW = r.ask ? std::max(80, innerW - kRowPadX - 168)
                                        : innerW - 2 * kRowPadX;
                r.headerBtn->SetSize(headW, kHeaderH);
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
            // ★★ 2026-10-27：权限门的两钮与已答结论锚在 **header 行**（垂直居中）。
            //   · 按钮**不随折叠体走**：折叠体一收，按钮必须还在（`--answer-probe`
            //     用控件绝对中心点合成点击，按钮一隐藏它就必红；真用户也会找不到）。
            //   · 结论沿用 P4 D 的右对齐位，纵坐标从固定的 10 换成 header 行中线——
            //     `kHeaderH=32`、结论高 16 ⇒ 仍是 10，与改造前逐像素一致。
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
        } else if (r.hasTable) {
            // 富内容二期：复合 assistant 行的高度由下方 segments 累加计算，不走单气泡 wrap
        } else {
            const int maxBubbleW = std::max(120, innerW * 78 / 100);
            const int maxTextW = std::max(40, maxBubbleW - 2 * kRowPadX - 2);
            if (r.label) {
                // ★★ 2026-10-27：权限门已并入上面的折叠分支（`r.ask` 走 header + 折叠体，
                //   两钮与结论锚在 header 行）⇒ 原来这里的 `if (r.ask) {...}` 整支删除。
                //   本块现在只剩真正的聊天气泡。
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
                r.label->SetPosition(kRowPadX, 8);
            }
            if (r.copyBtn && r.copyBtn->IsVisible()) {
                r.copyBtn->SetSize(48, 22);
                r.copyBtn->SetPosition(kRowPadX, r.label ? r.label->GetHeight() + 10 : 8);
                rowH = (r.label ? r.label->GetHeight() + 10 : 0) + 22 + 8;
            } else {
                rowH = r.label ? r.label->GetHeight() + 16 : 0;
            }
        }
        if (r.tool || r.reasoning || r.ask) {
            // 工具卡 / 思维链卡 / 权限门：通栏（hi-fi align-self: stretch）
            r.panel->SetSize(innerW, rowH);
            r.panel->SetPosition(canvasX, y);
        } else if (r.hasTable) {
            // ★★ 富内容二期：含有 Markdown 表格的复合 assistant 行
            //   T1+T2（2026-11-01）：表格内部布局先按版心宽定稿（折行/行高/列线），
            //   再走下面的 ScrollView 尺寸下发；宽不变不重排（同 lastWrapW 纪律）
            const int tableAvailW = std::max(40, innerW - 2 * kRowPadX);
            for (RowRec::TableRec& t : r.tables) {
                if (t.lastAvailW != tableAvailW) {
                    LayoutTableInterior(t, tableAvailW);
                    t.lastAvailW = tableAvailW;
                }
            }
            int curY = 8;
            for (ECDI::Widget* w : r.segments) {
                if (!w) continue;
                if (auto* tw = dynamic_cast<ECDI::TextWidget*>(w)) {
                    tw->SetWordWrap(true);
                    tw->SetSize(std::max(40, innerW - 2 * kRowPadX), tw->GetHeight());
                    tw->AutoSize();
                    tw->SetPosition(kRowPadX, curY);
                    curY += tw->GetHeight() + 10;
                } else if (auto* sv = dynamic_cast<ECDI::ScrollView*>(w)) {
                    const int contentW = static_cast<int>(sv->GetContentExtent().width);
                    const int contentH = static_cast<int>(sv->GetContentExtent().height);
                    const int availW = std::max(40, innerW - 2 * kRowPadX);
                    const int tableW = std::min(availW, contentW);
                    // +2 = ScrollView 条四边内缩（与上游 BarFootprint 同式）；表格横条已薄化 ⇒ 不能写死 14
                    const int barFoot = sv->GetHorizontalScrollBar()
                                            ? sv->GetHorizontalScrollBar()->GetThickness() + 2
                                            : 14;
                    const int barExtraH = (contentW > availW) ? barFoot : 0;
                    const int tableH = contentH + barExtraH;
                    sv->SetSize(tableW, tableH);
                    sv->SetPosition(kRowPadX, curY);
                    curY += tableH + 12;
                } else {
                    w->SetPosition(kRowPadX, curY);
                    curY += w->GetHeight() + 10;
                }
            }
            if (r.copyBtn && r.copyBtn->IsVisible()) {
                r.copyBtn->SetSize(48, 22);
                r.copyBtn->SetPosition(kRowPadX, curY);
                curY += 22 + 8;
            }
            rowH = curY + 8;
            r.panel->SetSize(innerW, rowH);
            r.panel->SetPosition(canvasX, y);
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
            if (r.copyBtn && r.copyBtn->IsVisible()) {
                bubbleW = std::max(bubbleW, 48 + 2 * kRowPadX);
            }
            r.panel->SetSize(bubbleW, rowH);
            r.panel->SetPosition(r.alignRight ? canvasX + innerW - bubbleW
                                              : canvasX,
                                 y);
        }
        r.y = y;   // ★ v5：「代理」卡点击定位要用（见 `AppAgents.cpp::FocusAgent`）
        // ★ v5：归属竖线跟着**本行行高**走（建行期给不出高度，测量后才定）。
        //   x 恒 0：行已整体右移，竖线因此落在"根行左边界 + 24"处，读作子层级。
        if (r.gutter) {
            // ★★ 2026-11-04（实测记录，别再来一遍）：竖线**只能按行分段**。
            //   试过「下一行同属该代理时把高度补上 kRowGap(10) 让两段在行距里
            //   对接」，结果**一点效果都没有**——像素读数仍是「段长 36、间隔 10」。
            //   根因：`Widget::Paint` 基类管线为**每个控件**发 `PushClip`
            //   （`ECDI/src/Widget/Panel.cpp:45` 的注释明写），子控件超出父矩形
            //   的部分一律不可见。故那 10px 是**死代码**，已撤。
            //   ⇒ 想要一条连续的归属轨道，必须把竖线**提到画布层**（作为内容容器
            //     的子控件按绝对 y 摆放），不能挂在行里。那会改动建行路径
            //     （`CreateBlockRow`）与本函数"不改树结构"的既有纪律，
            //     故列为待办而**不是**本轮顺手做。
            r.gutter->SetSize(kAgentGutterW, rowH);
            r.gutter->SetPosition(0, 0);
        }
        y += rowH + kRowGap;
    }
    for (PendingPromptView& view : m_pendingPromptViews) {
        RowRec& r = view.row;
        if (!r.panel) continue;
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
    const int w = std::clamp<int>(static_cast<int>(m_tooltipPending.size()) * 7 + 20,
                                  48, std::max(48, cw));
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
    txt->SetSize(w - 16, 16);
    txt->SetPosition(8, 5);
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
        const bool want = r.reasoning && !r.ended;
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
    if (inspDirty && m_hud.inspector) {
        m_hud.inspector->Arrange();
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
    const int reserved = 2 * harness::kPadFrame + btnW * 3 + 8 * 4 + 10;

    int tw = head->GetWidth() - reserved;
    if (tw > cap) tw = cap;
    if (tw < 120) tw = 120;   // 兜底：宁可标题被裁，也不让三钮消失

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

void HarnessApp::SetDevMode(bool dev) {
    if (m_devMode == dev) return;
    m_devMode = dev;
    // ★★ 缺口 5 闭合：按目标模式恢复其独立记忆的检视栏偏好，而非无条件覆盖成 dev。
    m_inspectorVisible = dev ? m_devInspectorVisible : m_userInspectorVisible;
    // ★★ 2026-10-27：显隐改走唯一落地口（模型态与控件永远同进同退）。
    //   排版宽不放这里管——那是 `TranscriptLayoutWidth()` 的账，Relayout 会跟着算。
    ApplyInspectorVisibility();
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

// HUD 刷新：每 tick 至多一次；SetText 同值仍会请求重绘，
// 因此靠 m_hudDirty + settled 判定避免 30Hz 空转。
void HarnessApp::RefreshHud() {
    m_hudDirty = false;
    if (!m_hud.title) return;

    m_hud.title->SetText("harness");
    if (HudTouch("rowChip"), m_hud.rowChip) {
        m_hud.rowChip->SetText(m_options.rowModeList ? "listlayout" : "autosize");
    }
    if (HudTouch("sessChip"), m_hud.sessChip) {
        m_hud.sessChip->SetText(m_sess->sessionId.empty() ? "s-____" : m_sess->sessionId);
    }
    if (HudTouch("modeChip"), m_hud.modeChip) {
        // ★★ 2026-10-26（订正）：判据是**会话自述的 `originKind`**，不是指针推断。
        //   ★ 上一版写的是 `if (live) ... else if (replay != nullptr) "replay"`——
        //     而 `App.h` 的 `originKind` 注释（Step 5 就写下的）明确说过
        //     **`replay != nullptr` 不能当来源判据**：历史会话 `s.replay` 也非空
        //     （`AppHistory.cpp` 里 `s.replay = replay.get()` 与
        //     `s.originKind = "replay-history"` 是同时赋的），于是从磁盘点开的会话
        //     会被报成 "replay"，恰好把这一版想修掉的"来源恒显示 replay"换个理由重现。
        //   ⇒ 四类来源一对一投影，`originKind` 是唯一真相。
        //     "replay-history" 缩成 "history"（胶囊只有 64px，7 码点装得下）。
        const std::string& origin = m_sess->originKind;
        const char* modeText = "empty";
        if (origin == "live") {
            modeText = "live";
        } else if (origin == "replay") {
            modeText = "replay";
        } else if (origin == "replay-history") {
            modeText = "history";
        }
        m_hud.modeChip->SetText(modeText);
    }
    if (HudTouch("backendChip"), m_hud.backendChip) {
        // ★★ 2026-10-21：**不再拼 `▽`**。后端在 `Create` 时定死，运行期没有切换
        //   路径（全仓唯一赋值处是 GL 就绪检查之后）⇒ 箭头会承诺一个不存在的菜单。
        //   原写法 `m_backendName + " ▽"` 是"看着能点、其实不能"的标本。
        m_hud.backendChip->SetText(m_backendName);
    }
    // 中栏会话头（2026-10-18，原型 C）：副标随会话 id / core 版本到达而更新。
    // ★ 2026-10-18 订正：此前只拼 `协议 v2 · 回放模式 · <sessionId>`，漏掉
    //   `hello.core_version` 与 `protocol`（截图上副标是
    //   `core 0.1.0 · session s-0001 · protocol 1` —— 与规格 §9 的
    //   `version/protocol` 呈现位不符，看着像没接上 hello）。
    //   ★ 只更新**会变的部分**（模式与工作区是启动即定的），标题保持建头时的值。
    // ★ 0d：标题改成反映**活动会话**（多会话后，标题不该恒是首会话的名字）。
    if (HudTouch("convTitle"), m_hud.convTitle) {
        m_hud.convTitle->SetText(m_sess->label.empty() ? ConversationTitle() : m_sess->label);
    }
    if (HudTouch("convSub"), m_hud.convSub) {
        if (m_devMode) {
            std::string sub = "core " + (m_sess->coreVer.empty() ? std::string("—") : m_sess->coreVer);
            if (!m_sess->sessionId.empty()) sub += " · " + m_sess->sessionId;
            sub += " · protocol " + std::to_string(m_sess->protocolVer);
            if (!m_sess->memories.empty()) {
                sub += " · 记忆 " + std::to_string(m_sess->memories.size());
            }
            // ★ 2026-11-06：MCP 启动期状态（产品面可见性）。没有 MCP 状态时
            //   McpStatusText() 返回空串 ⇒ 副标逐字不变。
            if (std::string mcpText = McpStatusText(); !mcpText.empty()) {
                sub += " · " + mcpText;
            }
            // ★ 0d：会话计数——让用户看见"现在有几个会话、我在第几个"。
            sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
                   std::to_string(m_sessions.size());
            m_hud.convSub->SetText(sub);
        } else {
            std::string sub;
            if (!m_sess->workspacePath.empty()) {
                sub = "工作区：" + m_sess->workspacePath;
            } else if (!m_sess->providerId.empty()) {
                // ★ §9.5：显示名（空回落 id）。副标题是给眼睛看的，不是给协议看的。
                sub = ProviderDisplayLabel(m_sess->providerId) + " › " +
                      (m_sess->modelName.empty() ? "?" : m_sess->modelName);
            } else {
                sub = "未连接模型 — 点击右上角模型胶囊或按 Ctrl+, 配置";
            }
            if (!m_sess->memories.empty()) {
                size_t loaded = 0;
                for (const auto& m : m_sess->memories) {
                    if (!m.skipped) loaded++;
                }
                sub += " · 记忆 " + std::to_string(loaded);
            }
            // ★ 2026-11-06：MCP 状态在**用户模式**也要可见（产品面默认视图）。
            //   与开发者分支共用 McpStatusText()，两处不各拼一份文案。
            if (std::string mcpText = McpStatusText(); !mcpText.empty()) {
                sub += " · " + mcpText;
            }
            if (m_sessions.size() > 1) {
                sub += " · 会话 " + std::to_string(m_activeIndex + 1) + "/" +
                       std::to_string(m_sessions.size());
            }
            m_hud.convSub->SetText(sub);
        }
    }
    const std::string stateText =
        m_sess->coreState.empty() ? std::string("idle") : m_sess->coreState;
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
    const bool live = m_sess->writer != nullptr;
    // ★★ 2026-10-20：`liveReady` **不再**包含 `workspaceCap`——协议 §3.1 只要求
    //   「未声明 workspace 的 core，GUI 不得显示工作区选择器」，而不是「不能对话」。
    //   旧写法把 workspace 能力当成 live 就绪的前提，导致一份合法的不支持工作区的
    //   core 连一句 prompt 都发不出去（发送钮永久置灰）。
    // ★★ 2026-10-27 修：`== 3` 是协议 v4 升级后**最后一处没迁移**的版本守卫
    //   （同族缺陷 2026-10-26 在 `ChooseWorkspaceFolder` 里修过，注释见
    //   AppWorkspace.cpp:332「`protocolVer != 3` 是 P3.6 升 v4 后没更新的拦路守卫」，
    //   当时只改了这一处，HUD 这处漏了）。v4 core 下 `liveReady` 恒假 ⇒
    //   `pickerEnabled` 与 `sendEnabled` 一起恒假 ⇒ 无参数启动（默认连真 core，
    //   `protocolDeclared: 4`）时「选择文件夹…」和「发送」**都是死钮**——
    //   禁用钮在 ECDI 里吞掉点击（`--hit-probe` 已证：forced-disabled ⇒ 点击不送达），
    //   所以症状是「点了没反应」而不是「点了报错」。
    //   ⇒ 口径与其余五处一致：workspace/prompt 语义 v3 引入、v4 保留 ⇒ `>= 3`。
    const bool liveReady = live && m_sess->protocolVer >= 3 &&
                           !m_sess->protocolIncompatible && !m_sess->disconnected &&
                           !m_sess->liveEof;
    // 只有「core 声明了 workspace 且初始同步未完成」才是阻塞条件；无此能力时直接放行。
    const bool workspaceReady = !m_sess->workspaceCap || m_sess->workspaceSyncComplete;
    const bool sendEnabled = live
        ? (liveReady && workspaceReady && m_sess->coreState == "idle" &&
           m_sess->pendingWorkspaceId.empty() && !HasUnconfirmedPrompt())
        : !active;
    const bool stopEnabled = live ? (liveReady && active) : active;
    const bool isJournalReadOnly = (m_sess && m_sess->originKind == "journal");
    if (m_hud.continueBtn) {
        m_hud.continueBtn->SetVisible(isJournalReadOnly);
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
    if (workspaceVisChanged) {
        // VerticalLayout 只在 Arrange 时给隐藏子不占槽（C-VIS-1）⇒ 不重排就会留个空洞。
        if (ECDI::Widget* p = m_hud.workspaceStatus ? m_hud.workspaceStatus->GetParent()
                                                    : nullptr) {
            p->Arrange();
        }
    }
    if (HudTouch("workspacePath"), m_hud.workspacePath) {
        if (m_devMode) {
            m_hud.workspacePath->SetFont(harness::MonoFont(12.0f));
            // ★★ 2026-11-03（P2）：空路径不再写「尚未确认」——下一行
            //   `workspaceStatus` 此刻正是「等待 core 确认（…）」，两行说同一句话，
            //   读起来像界面在结巴。这里只留一个「未确立」的占位，把"为什么还没有"
            //   交给状态行（它同时还要承载「core 未声明 workspace 能力」「切换失败：…」
            //   这类**非**同义的诊断，所以不能反过来把它并进这一行）。
            m_hud.workspacePath->SetText(m_sess->workspacePath.empty()
                ? "core 实际工作区：—" : "core 实际工作区：" + m_sess->workspacePath);
        } else {
            m_hud.workspacePath->SetFont(harness::SansFont(harness::kFontMicro));
            m_hud.workspacePath->SetText(m_sess->workspacePath.empty()
                ? "尚未选择工作区目录" : m_sess->workspacePath);
        }
    }
    if (HudTouch("workspaceStatus"), m_hud.workspaceStatus) {
        if (m_devMode) {
            m_hud.workspaceStatus->SetText(m_sess->workspaceStatus);
        } else {
            const bool hasPath = !m_sess->workspacePath.empty();
            if (m_hud.workspaceStatus->IsVisible() != hasPath) {
                m_hud.workspaceStatus->SetVisible(hasPath);
                if (m_hud.sidebar) m_hud.sidebar->Arrange();
            }
            const bool synced = hasPath && m_sess->workspaceStatus.find("已确认") == 0;
            m_hud.workspaceStatus->SetText(synced ? "已就绪" : m_sess->workspaceStatus);
        }
    }
    if (HudTouch("workspacePick"), m_hud.workspacePick) {
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
        ECDI::Button* pick = m_hud.workspacePick;   // ★ 2026-11-07：picker 同点回读
        if (send && stop) {
            const bool sendEnabled = send->IsEnabled();
            const bool stopEnabled = stop->IsEnabled();
            const std::string sendTitle = send->GetText();
            const bool pickerEnabledNow = pick ? pick->IsEnabled() : false;
            const bool pickerVisibleNow = pick ? pick->IsVisible() : false;
            // ★ picker 使能也参与「变化才记」：它比 send 先翻（hello 解析出 caps
            //   即翻，不必等 workspace 同步完成）——不记的话，「picker 已就绪而
            //   send 未就绪」的中间态在轨迹里不存在，2d-b 臂就少了定位分歧的那行。
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
        // ★★ 2026-11-07（2d-b 第四臂）：model-cap 拒绝侧 live 断言。
        //   条件：live（writer 在场——与 RequestModelSwitch 第一道门同一判据，回放
        //   不触发）+ hello 已解析 + caps 缺 "model" + idle + 尚未试过。经
        //   RequestModelSwitch 试一次切换——那是设置面板按钮的**同一入口**（单入口
        //   纪律），拒绝必须留下通知行且不落 pending/queue。此前整条 live 语料里
        //   「拒绝分支」结构性不可见（真 core 恒声明 model），与 2026-11-06 的
        //   「选择文件夹死钮」同族缺口。
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
        // ★★ 协议 v5（2026-11-04）：并行时带上**活跃代理数**。
        //   ★ 为什么放顶栏而不是只放「代理」卡：卡可以收起（`m_inspectorVisible`
        //     = false），收起后这是唯一能看出"还有几路在跑"的地方——而"现在
        //     是谁在干活"正是产品目的本身，不该依赖某张卡是否展开。
        //   ★ 只在 >0 时附加：0 是常态，常态不该多占字（胶囊宽 104，见
        //     `AppUiChrome.cpp` 的 `stateChip->SetSize`——那一处与这里是一对，
        //     改格式必须同批复核宽度）。
        if (size_t activeAgents = ActiveAgentCount(); activeAgents > 0) {
            chipText += " " + std::to_string(activeAgents) + "代理";
        }
        m_hud.stateChip->SetText(chipText);
        m_hud.stateChip->SetTextColor(kDarkMuted);
    }
    if (HudTouch("statusDot"), m_hud.statusDot) {
        m_hud.statusDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 4.0f});
    }
    if (HudTouch("sbDot"), m_hud.sbDot) {
        m_hud.sbDot->SetStyle(ECDI::PanelStyleOverride{
            .background = active ? kOkDark : kDarkMuted, .cornerRadius = 3.5f});
    }

    // ★★ Step 1：侧栏会话的"活动会话"读数**搬到了列表行里**（每行自带 id/label/选中态）。
    //   `sessSession` / `sessCore` 两个槽位因此**不再赋值**。
    //   ★★ 2026-10-21：这两个槽位与它们的 `if` 分支**已删除**。原注释写的是
    //     "字段仍留在 Hud 里（删字段是独立清理），故这里的两处 if 永远不成立
    //     （死代码但无害）"——"无害"这个判断错了：留着它们会让**下一个读代码的人
    //     以为侧栏有一行会话 id/meta**，而那一行早就不存在了。更重要的是，
    //     它们是"**读了但永远不写**"这一类缺陷的样板，而这类缺陷刚好可以机检
    //     （见 `--hud-probe`）。修一个样板、留一个样板是自相矛盾的。
    if (HudTouch("sessCount"), m_hud.sessCount) {
        // 计数与列表同源：都从 m_sessions.size() 出发 ⇒ 这一行是列表的**文字对照**。
        // 两者不一致（列表少画/多画了一行）时，人和探针都读得出来。
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%zu 个会话", m_sessions.size());
        m_hud.sessCount->SetText(buf);
    }
    if (HudTouch("sessProgress"), m_hud.sessProgress) {
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

    if (HudTouch("healthSeq"), m_hud.healthSeq) {
        const size_t gaps = m_sess->transcript.SeqGaps();
        if (gaps == 0) {
            m_hud.healthSeq->SetText("seq 连续 · 0 缺口");
            m_hud.healthSeq->SetTextColor(kDarkInk);
        } else {
            m_hud.healthSeq->SetText("seq 缺口 " + std::to_string(gaps));
            m_hud.healthSeq->SetTextColor(kErrorDark);
        }
    }
    if (HudTouch("healthViol"), m_hud.healthViol) {
        m_hud.healthViol->SetText("违规 " + std::to_string(m_sess->transcript.Violations()) +
                                  " · 未知 " + std::to_string(m_sess->transcript.UnknownTypes()));
    }
    // ★ 未知 kind 单列一行（2026-10-13）：它与「未知 t」管辖**不同的判别符**，
    //   合并会同时导致①语义误读（把「协议空白」读成「I5 已覆盖」）
    //   ②文本裁切（并进上行实测被裁，见 BuildInspector 注释）。
    if (HudTouch("healthKind"), m_hud.healthKind) {
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
    // 用户模式下若出现真实协议健康异常，自动浮现「协议健康」卡片（不静默吞错）。
    if (!m_devMode && m_hud.inspHealthCard) {
        const bool hasIssue = (m_sess->transcript.SeqGaps() > 0 ||
                               m_sess->transcript.Violations() > 0 ||
                               m_sess->transcript.UnknownTypes() > 0 ||
                               m_sess->transcript.UnknownKinds() > 0);
        if (m_hud.inspHealthCard->IsVisible() != hasIssue) {
            m_hud.inspHealthCard->SetVisible(hasIssue);
            if (m_hud.inspector) m_hud.inspector->Arrange();
        }
    }
    if (HudTouch("evalPaints"), m_hud.evalPaints) {
        m_hud.evalPaints->SetText(std::to_string(m_evalPaints) + " 次 · 请求 " +
                                  std::to_string(m_invalidations));
    }
    if (HudTouch("probeCmds"), m_hud.probeCmds) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeCmds->SetText("avg " + std::to_string(m_probeState.evalProbeAvg));
        } else {
            m_hud.probeCmds->SetText("—");
        }
    }
    if (HudTouch("probeExec"), m_hud.probeExec) {
        if (m_options.cullProbe && m_probe) {
            m_hud.probeExec->SetText("avg " +
                                     std::to_string(static_cast<unsigned long long>(m_evalAvgExecUs)) +
                                     " us");
        } else {
            m_hud.probeExec->SetText("—");
        }
    }
    if (HudTouch("usageInV"), m_hud.usageInV) {
        m_hud.usageInV->SetText(m_sess->usageIn < 0 ? "—"
                                              : std::to_string(m_sess->usageIn));
    }
    if (HudTouch("usageOutV"), m_hud.usageOutV) {
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
    // ★★ P4 H 修复（P3.6 遗留接线）：分母改 **ctxCapacity**（v4 逐轮覆盖：
    // hello.context ⇒ usage.context ⇒ model ack 逐级写它）——此前只写不读，
    // 百分比/环一直用 v2 的 contextLimit，usage.context 覆盖从未生效。
    // ★ P4 H：顶栏模型 chip 的常驻数据同源刷新。
    UpdateModelChip();
    const bool ctxKnown = (m_sess->ctxCapacity > 0 && m_sess->usageIn >= 0);
    if (HudTouch("ctxPct"), m_hud.ctxPct) {
        if (!ctxKnown) {
            m_hud.ctxPct->SetText("—");
            m_hud.ctxPct->SetTextColor(kDarkMuted);
        } else {
            // 四舍五入到整数百分比；占用可能 >100%（core 低估容量）⇒ 如实显示，不钳
            const long long pct =
                (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity;
            m_hud.ctxPct->SetText(std::to_string(pct) + "%");
            // 配色：<70% 常规 · 70–90% 琥珀（该收尾了）· >90% 红（快溢出）
            m_hud.ctxPct->SetTextColor(pct >= 90 ? kErrorDark
                                                 : (pct >= 70 ? kWarn : kOkDark));
        }
    }
    if (m_ctxRing) {
        const float frac = ctxKnown
                               ? static_cast<float>(m_sess->usageIn) /
                                     static_cast<float>(m_sess->ctxCapacity)
                               : 0.0f;
        // 环本身钳到 [0,1]（SetProgress 契约）；文本仍如实显示 >100%
        m_ctxRing->SetProgress(frac > 1.0f ? 1.0f : frac);
        // ★ 环色与右侧数字**同源**（同一组阈值），避免「环是绿的、字是红的」自相矛盾。
        //   阈值沿用既有口径：<70 常规绿 · 70–90 琥珀 · >90 红；分母未知 ⇒ 回退绿（环归零）。
        const long long pctForColor =
            ctxKnown ? (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity : 0;
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
            if (m_hud.inspector) m_hud.inspector->Arrange();
        }
    }
    if (!hasMemories) return;

    for (const auto& m : s->memories) {
        auto row = std::make_unique<ECDI::Panel>();
        row->SetSize(180, 18);
        auto nameLbl = std::make_unique<ECDI::Label>(m.name);
        nameLbl->SetFont(harness::SansFont(harness::kFontMicro));
        nameLbl->SetSize(110, 16);
        nameLbl->SetPosition(0, 1);
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
        metaLbl->SetSize(65, 16);
        metaLbl->SetPosition(112, 1);
        metaLbl->SetTextColor(m.skipped ? kWarn : kDarkMuted);
        row->AddChild(std::move(metaLbl));

        m_hud.memoryList->AddChild(std::move(row));
    }

    const int totalH = 28 + static_cast<int>(s->memories.size()) * 20;
    if (m_hud.inspMemoryCard) {
        m_hud.inspMemoryCard->SetSize(200, std::max(60, totalH));
        m_hud.memoryList->SetSize(190, static_cast<int>(s->memories.size()) * 20);
        m_hud.inspMemoryCard->Arrange();
    }
    if (m_hud.inspector) {
        m_hud.inspector->Arrange();
    }
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

