// ============================================================
// 布局与 HUD：根布局 / 贴底跟随 / 跳转按钮 / tooltip / 右侧检查器
// ------------------------------------------------------------
// 为什么单独一个文件：`Relayout`（184 行）+ `RefreshHud`（350 行）是**唯一的
//   摆位与读数出口**——三棵树（侧栏/中栏/检查器）的尺寸、浮层显隐、贴底跟随、
//   跳转按钮、HUD 文本全在这里结算。它是"改界面就要动"的文件，所以必须
//   和"建界面"（1e 的 builder）分开：一个是摆，一个是建。
// ★ 纪律：本文件**不改树结构**（不 AddChild/RemoveChild，唯一例外是
//   RebuildProjectList——它是 HUD 自己那棵小列表的全部所有者）。
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
using harness::internal::kCanvasPadX;
using harness::internal::kHeaderH;
using harness::internal::kJumpBtnH;
using harness::internal::kJumpBtnW;
using harness::internal::kListRowHeight;
using harness::internal::kPinThreshold;
using harness::internal::kRowGap;
using harness::internal::kRowPadX;
using harness::internal::kSidebarListW;
using harness::internal::kSidebarMaxRows;
using harness::internal::kTooltipDelayMs;

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
                    // ★★ P4 D：已答结论行占两钮原来的位置（右对齐、垂直居中）。
                    if (r.conclusion && r.conclusion->IsVisible()) {
                        r.conclusion->AutoSize();
                        const int cw = r.conclusion->GetWidth();
                        r.conclusion->SetPosition(innerW - cw - 14, 10);
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
    // ★★ P4 Phase A：内容范围变了 ⇒ maxOffset/滚动条可见性都可能变——
    //   「跳到最新」的几何与显隐跟着刷新一次（Relayout 覆盖 resize/侧栏/菜单/
    //   设置切换等全部几何路径）。
    RefreshJumpButton();
    // ★★ P4 Phase C：空状态引导的宽度跟随（resize/侧栏切换后重摆）。
    RefreshEmptyGuide();
    // ★★ P4 Phase F：设置浮层几何跟随（视口尺寸变了，遮罩/sheet 重摆）。
    LayoutSettingsOverlay();
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
    // 其实际宽度让位（kSidebarListW 注释里的 BarFootprint=14 是侧栏的观测值，
    // 这里直接读控件，条隐藏/显示切换时几何自然跟随）。
    int barW = 0;
    if (ECDI::ScrollBar* bar = m_scroll->GetVerticalScrollBar()) {
        if (bar->IsVisible()) barW = bar->GetWidth() + 6;
    }
    m_hud.jumpLatest->SetSize(kJumpBtnW, kJumpBtnH);
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
// ★ 定位纪律：锚点坐标一律 GetAbsolutePosition（不手算父链，App.cpp:4274 同款）；
//   顶栏/中栏头按钮在视口上方 ⇒ y 钳到 0（浮层挂 m_scroll，负 y 会被视口裁剪）。
void HarnessApp::ShowTooltipFor(const ECDI::Widget* anchor, const std::string& text) {
    if (!anchor || !m_window) return;
    m_tooltipAnchor = anchor;
    m_tooltipPending = text;
    m_window->GetPlatformWindow().StartTimer(kTooltipTimer, kTooltipDelayMs);
}

void HarnessApp::HideTooltip() {
    if (m_window) m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    m_tooltipAnchor = nullptr;
    m_tooltipPending.clear();
    if (m_hud.tooltip) m_hud.tooltip->SetVisible(false);
}

void HarnessApp::OnTooltipFire() {
    if (!m_hud.tooltip || !m_hud.tooltipText || !m_tooltipAnchor || !m_scroll) {
        return;
    }
    if (m_window) m_window->GetPlatformWindow().StopTimer(kTooltipTimer);
    // 定时器期间锚点可能已脱树（整树重建）——IsInTree 没有公开口，用宽度/父链
    // 兜底：GetParent 为空的根除外……直接按绝对坐标定位，锚点已析构的情况由
    // HideTooltip（leave 先于析构）与重建路径（BuildUi 后浮层隐藏）兜住。
    const ECDI::Point a = m_tooltipAnchor->GetAbsolutePosition();
    const ECDI::Point s = m_scroll->GetAbsolutePosition();
    m_hud.tooltipText->SetText(m_tooltipPending);
    const int w = std::clamp<int>(static_cast<int>(m_tooltipPending.size()) * 7 + 20,
                                  48, m_scroll->GetWidth());
    const int h = 26;
    int x = static_cast<int>(a.x) - static_cast<int>(s.x);
    int y = static_cast<int>(a.y) + m_tooltipAnchor->GetHeight() + 6 -
            static_cast<int>(s.y);
    x = std::clamp(x, 0, std::max(0, m_scroll->GetWidth() - w));
    y = std::clamp(y, 0, std::max(0, m_scroll->GetHeight() - h));
    m_hud.tooltipText->SetSize(w - 16, 16);
    m_hud.tooltip->SetSize(w, h);
    m_hud.tooltip->SetPosition(x, y);
    m_hud.tooltip->SetVisible(true);
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
        m_probeState.evalProbeFrames = m_probe->FrameCount();
        m_probeState.evalProbeCommands = m_probe->TotalCommands();
        m_probeState.evalProbeMax = m_probe->MaxFrameCommands();
        m_probeState.evalProbeAvg = m_probe->AvgFrameCommands();
        m_evalMaxExecUs = m_probe->MaxExecuteUs();
        m_evalAvgExecUs = m_probe->AvgExecuteUs();
    }
    if (m_measureProbe) {
        m_evalMeasureCalls = m_measureProbe->MeasureCalls();
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
        if (n == "projectList") return h.projectList != nullptr;
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
    if (HudTouch("workspacePath"), m_hud.workspacePath) {
        m_hud.workspacePath->SetText(m_sess->workspacePath.empty()
            ? "core 实际工作区：尚未确认" : "core 实际工作区：" + m_sess->workspacePath);
    }
    if (HudTouch("workspaceStatus"), m_hud.workspaceStatus) m_hud.workspaceStatus->SetText(m_sess->workspaceStatus);
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
        if (send && stop) {
            const bool sendEnabled = send->IsEnabled();
            const bool stopEnabled = stop->IsEnabled();
            const std::string sendTitle = send->GetText();
            const bool changed = !m_probeState.stateProbeLastValid ||
                                 sendEnabled != m_probeState.stateProbeLastSend ||
                                 stopEnabled != m_probeState.stateProbeLastStop;
            if (changed) {
                StateProbeRec rec;
                rec.coreState = m_sess->coreState.empty() ? "idle" : m_sess->coreState;
                rec.sendEnabled = sendEnabled;
                rec.stopEnabled = stopEnabled;
                rec.sendTitle = sendTitle;
                m_probeState.stateProbeLog.push_back(std::move(rec));
                m_probeState.stateProbeLastSend = sendEnabled;
                m_probeState.stateProbeLastStop = stopEnabled;
                m_probeState.stateProbeLastValid = true;
            }
        }
    }
    if (HudTouch("stateChip"), m_hud.stateChip) {
        m_hud.stateChip->SetText(stateText);
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
                r.sendTitle + "\"}";
    }
    json += "],\n";
}

