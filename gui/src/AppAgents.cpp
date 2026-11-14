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
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollView.h"

#include <algorithm>
#include <cstdio>
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
    card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
    card->AddChild(uikit::Label("代理", 178, 14, kDarkMuted, harness::kFontMicro));

    auto list = std::make_unique<ECDI::Panel>();
    m_hud.agentList = list.get();
    // ★ 176 = 卡宽 200 − 左右各 12 内边距：列表容器与行按钮都必须收在版心内。
    //   此前 190 ⇒ 右缘 12+190=202，越过卡片右边界（~x=199），不透明底把
    //   1px 描边盖掉一截（实测「代理卡选项遮住右边框」）。
    list->SetSize(176, rowH);
    list->SetLayout(std::make_unique<ECDI::VerticalLayout>(kAgentRowGap, false, 0));
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
    // 算式同建卡处：pad12 + 标题 17 + 间距 8 + 列表 + pad12
    m_hud.inspAgentCard->SetSize(200, 49 + listH);
    m_hud.agentList->SetSize(176, listH);
    m_hud.inspAgentCard->Arrange();
    SyncInspectorLayout();
    RequestRepaint();
}

// ── 定位高亮（点击右栏代理卡跳转时同步标亮首行边框与整段归属竖线）────
void HarnessApp::ApplyFocusedAgentHighlight() {
    if (!m_sess) return;
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

// ── 定位（点卡上某一行）──────────────────────────────────────────────
void HarnessApp::FocusAgent(const std::string& agentId) {
    if (agentId.empty() || !m_scroll || !m_sess) return;
    auto findAgentRow = [&]() -> const RowRec* {
        for (const RowRec& r : m_sess->rows) {
            if (r.agentTag == agentId) return &r;
        }
        return nullptr;
    };
    const RowRec* first = findAgentRow();
    // 没找到 = 这个代理的块还没建（start 刚到、首个块还没到）⇒ 什么都不做。
    if (!first) return;
    // 若目标行所在回合处于折叠态，先展开该回合再定位。
    if (first->foldHidden && first->timelineIdx < m_sess->timeline.size()) {
        for (size_t i = first->timelineIdx + 1; i > 0; --i) {
            const size_t ti = i - 1;
            if (m_sess->timeline[ti].kind == TimelineEntry::Kind::Fold) {
                if (!m_sess->timeline[ti].expanded) {
                    ToggleTurn(ti);
                }
                break;
            }
        }
        first = findAgentRow();
        if (!first) return;
    }
    m_focusedAgentId = agentId;
    m_focusedAgentPulse = 60;
    ApplyFocusedAgentHighlight();
    Relayout();
    first = findAgentRow();
    if (!first) return;
    // 留 8px 余量：目标行贴着视口上沿时读不出"它是谁的下一层"。
    const int scrollY = std::min(std::max(0, first->y - 8), m_scroll->GetMaxOffsetY());
    m_scroll->SetContentOffset(0, scrollY);
    RefreshJumpButton();
    RequestRepaint();
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
