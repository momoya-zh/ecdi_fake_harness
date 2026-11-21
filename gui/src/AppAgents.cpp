// ============================================================
// 右栏「代理」卡（协议 v5，2026-11-04）
// ------------------------------------------------------------
// 为什么单独一个文件：这张卡是**产品面**的（用户看「现在谁在干活」），
//   与 `ProbeAgents.cpp` 的探针读数读的是同一份模型，但目的大不相同——
//   探针要证明「渲染没退步」，这张卡要回答「谁在跑、跑到哪」。合成一个
//   文件会让两种用途互相污染（历史教训：探针字段被拿去做产品状态 ⇒
//   关掉探针产品就瞎了）。
//
// ★ 设计约束（三条）：
//   ① **不动侧栏会话列表**：子代理不是会话，塞进 `m_sessions` 会撞
//      「同时只有一个活动会话」这条硬不变量。故走检视栏卡片这条既有先例
//      （「模型」卡）。
//   ② **默认隐藏**（同「记忆」卡）：没有委派时不占纵向空间。检视栏是
//      固定列高的排布，凭空多 130px 会让下面几张卡被挤出可视区。
//   ③ **行序 = 屏幕序**：按每个代理**第一个块行的 y** 排序，不按 id 字典序
//      （`a10 < a2` 是字典序的必然结果，而屏幕上下关系才是用户看到的）。
//
// ★ 点击定位：每行是可点按钮 ⇒ 滚到该代理的第一行。行的 y 由 `Relayout`
//   写进 `RowRec::y`（布局结果，不是推算值）。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"
#include "UiKit.h"

#include "ECDI/Core/TextAlignment.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/ScrollBarStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/ScrollView.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kAgentRowGap = 4;   // 卡内行距（与「模型」「记忆」卡一致）

// 毫秒 → 人类可读。★ 不显示 0ms：子代理还没结束时 `elapsedMs` 就是 0，
//   把它印成 "0ms" 等于给出一个假读数（本仓纪律：没测到的不要写成测到了）。
std::string ElapsedText(long long ms) {
    char buf[32];
    if (ms <= 0) return "—";
    if (ms < 1000) {
        std::snprintf(buf, sizeof(buf), "%lldms", ms);
    } else if (ms < 60000) {
        std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(ms) / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%lldm%02llds", ms / 60000, (ms / 1000) % 60);
    }
    return buf;
}

}  // namespace

// ── 建卡（由 `BuildInspector` 调用）────────────────────────────────────
void HarnessApp::BuildAgentCard(ECDI::Widget& insp, int rowH) {
    auto card = std::make_unique<ECDI::Panel>();
    m_hud.inspAgentCard = card.get();
    card->SetVisible(false);   // 有代理时才显形（见 `RebuildAgentList`）
    // 算式：pad12 + 标题 17 + 间距 8 + 1 行 + pad12（行数在重建时才定）
    card->SetSize(200, 49 + rowH);
    card->SetStyle(ECDI::PanelStyleOverride{
        .background = kContentBg, .cornerRadius = kRadiusCard2,
        .borderWidth = 1.0f, .borderColor = kCardBorder});
    card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, true, 12));
    card->AddChild(uikit::Label("代理", 176, 14, kDarkMuted, harness::kFontMicro));

    auto list = std::make_unique<ECDI::Panel>();
    m_hud.agentList = list.get();
    // ★ 176 = 卡宽 200 − 左右各 12 内边距：列表容器与行按钮都必须收在版心内，
    //   且开启 fillCrossAxis 让卡片与行随右栏拖拽自适应伸展。
    list->SetSize(176, rowH);
    list->SetLayout(std::make_unique<ECDI::VerticalLayout>(kAgentRowGap, true, 0));
    card->AddChild(std::move(list));

    insp.AddChild(std::move(card));
}

// ── 重建列表（每收到一条 `agent` 生命周期消息调一次）──────────────────
void HarnessApp::RebuildAgentList() {
    if (!m_hud.agentList || !m_hud.inspAgentCard) return;

    while (m_hud.agentList->GetChildCount() > 0) {
        m_hud.agentList->RemoveChild(m_hud.agentList->GetChildAt(0));
    }

    Session* s = m_sess;
    // ★★ 门控两件都要满足：① core 声明了 `caps.agents`（协议 §3.1 的**契约**要求）；
    //   ② 转录里真的有子代理。只有 ② 是"巧合成立"——不合规 core 可以发 agent
    //   消息却不声明能力，那时显示卡片就是 GUI 违约。
    //   ★ 被门控掉的只是**卡片**：块本身照旧按归属缩进渲染（内容是 content，
    //     I5 前向兼容那一条管的是"不认识的东西不要崩"，与能力门控是两回事）。
    const bool hasAgents = s && s->hasAgentsCap && !s->transcript.Agents().empty();
    if (m_hud.inspAgentCard->IsVisible() != hasAgents) {
        m_hud.inspAgentCard->SetVisible(hasAgents);
        SyncInspectorLayout();
    }
    if (!hasAgents) return;

    // 行高：卡片自身布局用的 22（与检视栏其它卡同高）——按钮文字靠
    //   `textAlignment = Left` 左对齐，不需要额外的行内分栏。
    constexpr int kRowH = 22;

    // 屏幕序：按每个代理第一个归属块行的 y 升序。
    auto firstRowY = [s](const std::string& id) {
        for (const RowRec& r : s->rows) {
            if (r.agentTag == id) return r.y;
        }
        return 1 << 30;   // 还没建行（刚 start、块还没到）⇒ 排最后
    };
    std::vector<const AgentInfo*> order;
    order.reserve(s->transcript.Agents().size());
    for (const auto& kv : s->transcript.Agents()) order.push_back(&kv.second);
    std::sort(order.begin(), order.end(),
              [&firstRowY](const AgentInfo* a, const AgentInfo* b) {
                  const int ya = firstRowY(a->id), yb = firstRowY(b->id);
                  if (ya != yb) return ya < yb;
                  return a->id < b->id;   // 同 y（都还没建行）⇒ 稳定序
              });

    auto makeRow = [this, kRowH](const std::string& text, const ECDI::Color& ink,
                                 const std::string& agentId) {
        auto btn = std::make_unique<ECDI::Button>(text);
        // ★ 176：见 BuildAgentCard 里列表容器的算式。行按钮带**不透明**底
        //   （kContentBg），超宽一点都会把卡片描边压在底下——176 收在版心内。
        btn->SetSize(176, kRowH);
        btn->SetFont(harness::SansFont(12.0f));
        btn->SetTextColor(ink);
        // ★ 背景 = 卡片底色 ⇒ 看起来是"一行字"，不是一颗按钮；但点击面仍在
        //   （`hoverBackground` 用 `kPanel2` 给一档极轻的反馈，避免"点了没反应"）。
        btn->SetStyle(ECDI::ButtonStyleOverride{
            .background = kContentBg,
            .border = kContentBg,
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            // ★ 顺序照 `ButtonStyleOverride` 的**声明序**（C7560）：
            //   pressedBackground 在 hoverBackground 之前，写反会直接编译报错。
            .pressedBackground = kPanel2,
            .hoverBackground = kPanel2,
            .textAlignment = ECDI::TextAlignment::Left});
        // ★ 只读点：捕获 id，不捕获 AgentInfo*（map 节点在后续 start 插入时
        //   仍有效，但按 id 查找更不容易被将来的容器替换打穿）。
        btn->SetOnClick([this, agentId] { FocusAgent(agentId); });
        m_hud.agentList->AddChild(std::move(btn));
    };

    // ① 主代理行：永远是第 0 行，它没有 start/end（协议 §3.12 只管子代理）。
    {
        const std::string st = s->coreState.empty() ? "idle" : s->coreState;
        std::string word = "就绪";
        if (st == "running") word = "思考中";
        else if (st == "waiting") word = "等待授权";
        makeRow("主代理 · " + word, kDarkInk, std::string());
    }

    // ② 子代理行：名字 + 状态 + 轮数 + 耗时。
    for (const AgentInfo* a : order) {
        std::string name = a->name.empty() ? a->id : a->name;
        // 名字里出现过的 id 也带上：同名两个子代理（"检索员" ×2）在卡上
        //   分不开，而它们的块在转录里是分开的两段。
        std::string text;
        ECDI::Color ink = kDarkMuted;
        if (!a->ended) {
            text = "▸ " + name + " · 进行中";
            ink = kWarn;
        } else if (a->ok) {
            text = "✓ " + name;
            ink = kOkDark;
        } else {
            text = "✗ " + name;
            ink = kErrorDark;
        }
        if (a->ended) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "  %d轮 %s", a->turns,
                          ElapsedText(a->elapsedMs).c_str());
            text += buf;
        }
        makeRow(text, ink, a->id);
    }

    const int rows = static_cast<int>(s->transcript.Agents().size()) + 1;   // +1 = 主代理行
    const int listH = rows * kRowH + (rows - 1) * kAgentRowGap;
    // 算式同建卡处：pad12 + 标题 17 + 间距 8 + 列表 + pad12；保留当前拉伸后的卡宽
    const int cardW = std::max(176, m_hud.inspAgentCard->GetWidth());
    m_hud.inspAgentCard->SetSize(cardW, 49 + listH);
    m_hud.agentList->SetSize(std::max(152, cardW - 24), listH);
    m_hud.inspAgentCard->Arrange();
    SyncInspectorLayout();
    RequestRepaint();
}

// ── 定位高亮（点击右栏代理卡跳转时同步标亮首行边框与整段归属竖线）────
void HarnessApp::ApplyFocusedAgentHighlight() {
    if (!m_sess) return;
    if (m_focusedAgentId.empty()) {
        // Fast path: if no row currently has highlight active, skip O(N) SetStyle scan.
        bool anyHighlighted = false;
        for (const RowRec& r : m_sess->rows) {
            if (r.gutterHighlight || r.focusHighlight) {
                anyHighlighted = true;
                break;
            }
        }
        if (!anyHighlighted) return;
    }
    bool firstVisibleMarked = false;
    const ECDI::Color normalGutter =
        harness::internal::Pick(m_options.probeContrast, kLine, harness::internal::kProbeInk);
    for (RowRec& r : m_sess->rows) {
        const bool match = !m_focusedAgentId.empty() && r.agentTag == m_focusedAgentId;
        if (r.gutter && r.gutterHighlight != match) {
            r.gutterHighlight = match;
            r.gutter->SetStyle(ECDI::PanelStyleOverride{
                .background = match ? kDarkAccent : normalGutter,
                .cornerRadius = 1.0f});
        }
        bool wantPanel = false;
        if (match && !r.foldHidden && r.panel && !firstVisibleMarked) {
            firstVisibleMarked = true;
            wantPanel = true;
        }
        if (auto* p = dynamic_cast<ECDI::Panel*>(r.panel)) {
            if (wantPanel && !r.focusHighlight) {
                r.focusHighlight = true;
                p->SetStyle(ECDI::PanelStyleOverride{
                    .borderWidth = 1.5f,
                    .borderColor = kDarkAccent});
            } else if (!wantPanel && r.focusHighlight) {
                r.focusHighlight = false;
                p->SetStyle(ECDI::PanelStyleOverride{
                    .borderWidth = r.baseBorderW,
                    .borderColor = r.baseBorderColor});
            }
        }
    }
}

void HarnessApp::ClearFocusedAgentHighlight() {
    if (m_focusedAgentId.empty() && m_focusedAgentPulse == 0) return;
    m_focusedAgentId.clear();
    m_focusedAgentPulse = 0;
    ApplyFocusedAgentHighlight();
    Relayout();
    RequestRepaint();
}

// ── 定位（点右栏「代理」卡上某一行：直接切换到对应子代理或主对话标签页）────
void HarnessApp::FocusAgent(const std::string& agentId) {
    if (!m_sess) return;
    SelectAgentTab(agentId);
}

bool HarnessApp::IsRowHiddenByAgentTab(const RowRec& r) const {
    if (!m_sess) return false;
    if (!m_sess->activeAgentTab.empty()) {
        return r.agentTag != m_sess->activeAgentTab;
    }
    // 主对话视图（activeAgentTab 为空）：只要声明了 caps.agents，子代理的内部操作过程
    // 只在对应子代理标签页内展示，不在主对话里平铺混排。
    if (m_sess->hasAgentsCap && !r.agentTag.empty()) {
        return true;
    }
    return false;
}

// ── 活跃代理数（顶栏胶囊用）────────────────────────────────────────────
size_t HarnessApp::ActiveAgentCount() const {
    if (!m_sess) return 0;
    size_t n = 0;
    for (const auto& kv : m_sess->transcript.Agents()) {
        if (kv.second.started && !kv.second.ended) ++n;
    }
    return n;
}

size_t HarnessApp::ActiveJobCount() const {
    if (!m_sess) return 0;
    size_t n = 0;
    for (const auto& kv : m_sess->jobsByCall) {
        if (kv.second.status == "running") ++n;
    }
    return n;
}

// ── 中栏子代理动态选项卡条（默认隐藏，有子代理且 caps.agents 时显形）────────

void HarnessApp::BuildAgentTabBar() {
    if (!m_counting) return;
    auto bar = std::make_unique<ECDI::Panel>();
    m_hud.agentTabBar = bar.get();
    bar->SetVisible(false);
    bar->SetSize(600, 36);
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? harness::internal::kProbeBg : kPanel2,
        .borderWidth = 0.0f});
    bar->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, true, 0));

    auto sv = std::make_unique<ECDI::ScrollView>();
    m_hud.agentTabScroll = sv.get();
    sv->SetStretch(1);
    sv->SetSize(600, 35);
    sv->SetScrollStep(32);
    if (auto* hb = sv->GetHorizontalScrollBar()) {
        hb->SetStyle(ECDI::ScrollBarStyleOverride{
            .thickness = harness::internal::kScrollBarThickness});
    }
    if (auto* vb = sv->GetVerticalScrollBar()) {
        vb->SetStyle(ECDI::ScrollBarStyleOverride{
            .thickness = harness::internal::kScrollBarThickness});
    }
    sv->SetContentExtent(1, 28);
    bar->AddChild(std::move(sv));

    auto sep = std::make_unique<ECDI::Panel>();
    sep->SetSize(600, 1);
    sep->SetStyle(ECDI::PanelStyleOverride{
        .background = m_options.probeContrast ? harness::internal::kProbeBg : kLine,
        .borderWidth = 0.0f});
    bar->AddChild(std::move(sep));

    m_agentTabBtns.clear();
    m_counting->AddChild(std::move(bar));
}

void HarnessApp::RefreshAgentTabStyles() {
    if (!m_sess) return;
    const bool waitingRoot = (m_sess->coreState == "waiting");
    const ECDI::Color inactiveBg =
        m_options.probeContrast ? harness::internal::kProbeBg : kContentBg;
    for (const AgentTabBtn& item : m_agentTabBtns) {
        if (!item.btn) continue;
        const bool active = (m_sess->activeAgentTab == item.agentId);
        ECDI::Color ink = kDarkMuted;
        if (item.agentId.empty()) {
            const std::string wantText = (!active && waitingRoot) ? "主对话 · 待授权" : "主对话";
            if (item.btn->GetText() != wantText) item.btn->SetText(wantText);
            ink = active ? kAccentInk : (waitingRoot ? kWarn : kDarkMuted);
        } else if (active) {
            ink = kAccentInk;
        } else if (!item.ended) {
            ink = kWarn;
        } else if (item.ok) {
            ink = kOkDark;
        } else {
            ink = kErrorDark;
        }
        item.btn->SetTextColor(ink);
        if (active) {
            item.btn->SetStyle(ECDI::ButtonStyleOverride{
                .background = kUserBg,
                .border = kDarkAccent,
                .borderWidth = 1.0f,
                .cornerRadius = 6.0f,
                .pressedBackground = kUserBg,
                .hoverBackground = kUserBg});
        } else {
            item.btn->SetStyle(ECDI::ButtonStyleOverride{
                .background = inactiveBg,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = 6.0f,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
        }
    }
}

void HarnessApp::RebuildAgentTabBar() {
    if (!m_hud.agentTabBar || !m_hud.agentTabScroll) return;

    m_agentTabBtns.clear();
    ECDI::Widget& content = m_hud.agentTabScroll->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    Session* s = m_sess;
    const bool showTabs = s && s->hasAgentsCap && !s->transcript.Agents().empty();
    if (!showTabs) {
        const bool clearedActive = (s && !s->activeAgentTab.empty());
        if (s) s->activeAgentTab.clear();
        const bool wasVis = m_hud.agentTabBar->IsVisible();
        if (wasVis) {
            m_hud.agentTabBar->SetVisible(false);
            if (ECDI::Widget* p = m_hud.agentTabBar->GetParent()) p->Arrange();
        }
        if (clearedActive) {
            ApplyFoldVisibility();
        }
        if (wasVis || clearedActive) {
            Relayout();
            RequestRepaint();
        }
        return;
    }

    if (!s->activeAgentTab.empty() &&
        s->transcript.Agents().count(s->activeAgentTab) == 0) {
        s->activeAgentTab.clear();
    }

    std::vector<const AgentInfo*> order;
    order.reserve(s->transcript.Agents().size());
    std::map<std::string, int> nameCount;
    for (const auto& kv : s->transcript.Agents()) {
        order.push_back(&kv.second);
        const std::string base = kv.second.name.empty() ? kv.second.id : kv.second.name;
        ++nameCount[base];
    }
    std::sort(order.begin(), order.end(),
              [](const AgentInfo* a, const AgentInfo* b) {
                  if (a->order != b->order) return a->order < b->order;
                  return a->id < b->id;
              });

    const ECDI::Font tabFont = harness::SansFont(harness::kFontSmall);
    auto measureTabW = [&](const std::string& label, int minW) -> int {
        int textW = static_cast<int>(label.size()) * 7;
        if (m_window) {
            textW = static_cast<int>(
                m_window->GetTextMeasurer().MeasureText(tabFont, label).width + 0.999f);
        }
        return std::clamp(textW + 22, minW, 240);
    };

    int curX = 12;
    constexpr int kTabY = 3;
    constexpr int kTabH = 25;
    constexpr int kTabGap = 6;

    auto addTab = [&](const std::string& agentId, const std::string& label,
                      bool ended, bool ok, int minW) {
        auto btn = std::make_unique<ECDI::Button>(label);
        const int w = measureTabW(label, minW);
        btn->SetSize(w, kTabH);
        btn->SetPosition(curX, kTabY);
        btn->SetFont(tabFont);
        btn->SetOnClick([this, agentId] { SelectAgentTab(agentId); });
        m_agentTabBtns.push_back(AgentTabBtn{agentId, btn.get(), ended, ok});
        content.AddChild(std::move(btn));
        curX += w + kTabGap;
    };

    const bool rootWaiting = (s->coreState == "waiting" && !s->activeAgentTab.empty());
    addTab(std::string(), rootWaiting ? "主对话 · 待授权" : "主对话", true, true, 76);

    for (const AgentInfo* a : order) {
        const std::string base = a->name.empty() ? a->id : a->name;
        const std::string dispName =
            (nameCount[base] > 1 && !a->name.empty()) ? (base + " (" + a->id + ")") : base;
        std::string label;
        if (!a->ended) {
            label = "▸ " + dispName + " · 进行中";
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %s · %d轮",
                          a->ok ? "✓" : "✗", dispName.c_str(), a->turns);
            label = buf;
        }
        addTab(a->id, label, a->ended, a->ok, 88);
    }

    m_hud.agentTabScroll->SetContentExtent(std::max(1, curX + 6), 28);
    RefreshAgentTabStyles();

    const bool becameVisible = !m_hud.agentTabBar->IsVisible();
    if (becameVisible) {
        m_hud.agentTabBar->SetVisible(true);
        if (ECDI::Widget* p = m_hud.agentTabBar->GetParent()) p->Arrange();
        Relayout();
    }
    RequestRepaint();
}

void HarnessApp::SelectAgentTab(const std::string& agentId) {
    if (!m_sess) return;
    if (!agentId.empty() && m_sess->transcript.Agents().count(agentId) == 0) return;
    if (m_sess->activeAgentTab == agentId) return;

    if (m_sess->activeAgentTab.empty() && !agentId.empty() && m_scroll) {
        m_sess->rootScrollY = m_scroll->GetScrollOffsetY();
    }
    m_sess->activeAgentTab = agentId;
    if (!agentId.empty()) {
        m_focusedAgentId.clear();
        m_focusedAgentPulse = 0;
        ApplyFocusedAgentHighlight();
    }
    m_renderedAgentBannerId.clear();
    RefreshAgentTabStyles();
    ApplyFoldVisibility();
    Relayout();
    if (m_scroll) {
        if (!agentId.empty()) {
            m_scroll->SetContentOffset(0, 0);
        } else {
            m_scroll->SetContentOffset(0, std::min(m_sess->rootScrollY, m_scroll->GetMaxOffsetY()));
        }
    }
    RefreshJumpButton();
    RequestRepaint();
}

void HarnessApp::SyncAgentTaskBanner(int canvasX, int innerW, int& y) {
    if (!m_content || !m_sess || m_options.rowModeList) return;
    if (m_sess->activeAgentTab.empty()) {
        if (m_hud.agentTaskBanner) {
            m_content->RemoveChild(m_hud.agentTaskBanner);
            m_hud.agentTaskBanner = nullptr;
            m_hud.agentTaskTitle = nullptr;
            m_hud.agentTaskBody = nullptr;
        }
        m_renderedAgentBannerId.clear();
        return;
    }

    const auto it = m_sess->transcript.Agents().find(m_sess->activeAgentTab);
    if (it == m_sess->transcript.Agents().end()) {
        m_sess->activeAgentTab.clear();
        if (m_hud.agentTaskBanner) {
            m_content->RemoveChild(m_hud.agentTaskBanner);
            m_hud.agentTaskBanner = nullptr;
            m_hud.agentTaskTitle = nullptr;
            m_hud.agentTaskBody = nullptr;
        }
        m_renderedAgentBannerId.clear();
        return;
    }
    const AgentInfo& ag = it->second;

    bool hasRows = false;
    for (const RowRec& r : m_sess->rows) {
        if (r.agentTag == ag.id) {
            hasRows = true;
            break;
        }
    }

    if (!m_hud.agentTaskBanner) {
        auto banner = std::make_unique<ECDI::Panel>();
        banner->SetStyle(ECDI::PanelStyleOverride{
            .background = m_options.probeContrast ? harness::internal::kProbeBg : kPanel2,
            .cornerRadius = kRadiusCard2,
            .borderWidth = 1.0f,
            .borderColor = m_options.probeContrast ? harness::internal::kProbeInk : kLine});

        auto title = std::make_unique<ECDI::Label>("");
        title->SetFont(harness::SansFont(harness::kFontSmall));
        m_hud.agentTaskTitle = title.get();
        banner->AddChild(std::move(title));

        auto body = std::make_unique<ECDI::Label>("");
        body->SetFont(harness::SansFont(harness::kFontBody, 400));
        body->SetTextColor(m_options.probeContrast ? harness::internal::kProbeInk : kDarkInk);
        m_hud.agentTaskBody = body.get();
        banner->AddChild(std::move(body));

        m_hud.agentTaskBanner = banner.get();
        m_content->AddChild(std::move(banner));
        m_renderedAgentBannerId.clear();
    }

    if (m_renderedAgentBannerId != ag.id || m_renderedAgentBannerEnded != ag.ended ||
        m_renderedAgentBannerHasRows != hasRows) {
        const std::string dispName = ag.name.empty() ? ag.id : ag.name;
        std::string titleText = "子代理 · " + dispName + " (" + ag.id + ")";
        ECDI::Color titleColor = kWarn;
        if (!ag.ended) {
            titleText += " · 进行中";
            titleColor = kWarn;
        } else {
            char buf[96];
            std::snprintf(buf, sizeof(buf), " · %s · %d轮 · %s",
                          ag.ok ? "已完成" : "未完成", ag.turns,
                          ElapsedText(ag.elapsedMs).c_str());
            titleText += buf;
            titleColor = ag.ok ? kOkDark : kErrorDark;
        }
        if (m_options.probeContrast) titleColor = harness::internal::kProbeInk;
        if (m_hud.agentTaskTitle) {
            m_hud.agentTaskTitle->SetText(titleText);
            m_hud.agentTaskTitle->SetTextColor(titleColor);
        }
        std::string bodyText = "任务：" + (ag.task.empty() ? "（无任务描述）" : ag.task);
        if (!hasRows && ag.ended && !ag.summary.empty()) {
            bodyText += "\n结果：" + ag.summary;
        }
        if (m_hud.agentTaskBody) {
            m_hud.agentTaskBody->SetText(bodyText);
        }
        m_renderedAgentBannerId = ag.id;
        m_renderedAgentBannerEnded = ag.ended;
        m_renderedAgentBannerHasRows = hasRows;
    }

    const int padX = harness::internal::kRowPadX;
    const int textW = std::max(40, innerW - 2 * padX);
    int curY = 8;
    if (m_hud.agentTaskTitle) {
        m_hud.agentTaskTitle->SetWordWrap(true);
        m_hud.agentTaskTitle->SetSize(textW, 18);
        m_hud.agentTaskTitle->AutoSize();
        m_hud.agentTaskTitle->SetPosition(padX, curY);
        curY += m_hud.agentTaskTitle->GetHeight() + 4;
    }
    if (m_hud.agentTaskBody) {
        m_hud.agentTaskBody->SetWordWrap(true);
        m_hud.agentTaskBody->SetSize(textW, 18);
        m_hud.agentTaskBody->AutoSize();
        m_hud.agentTaskBody->SetPosition(padX, curY);
        curY += m_hud.agentTaskBody->GetHeight() + 8;
    }
    m_hud.agentTaskBanner->SetSize(innerW, curY);
    m_hud.agentTaskBanner->SetPosition(canvasX, y);
    y += curY + harness::internal::kRowGap;
}
