// ============================================================
// 右栏「MCP」卡 + 会话头副标（hello.mcp，2026-11-06）
// ------------------------------------------------------------
// 为什么要有这一层：MCP 配置与启动完全发生在 core 里，GUI 此前只能看到
//   `caps` 里有没有 "mcp"——用户写了 mcp.json 界面上没有任何确认，启动失败
//   也只有 stderr。产品面必须回答两个问题：
//     ① 你读的是我那个配置吗？（source / config / 服务数 / 工具数）
//     ② 谁没起来、为什么？（failed，具体原因由 core 的 system 块承载）
//
// ★ 数据来源只有 `hello.mcp`（协议 §3.1 可选字段）：GUI **不**扫 mcp.json、
//   不推断服务名。core 是唯一权威（与记忆卡 §8.4 同一纪律）。
// ★ 默认隐藏：没有 MCP 状态时不占纵向空间。副标在**两种模式**都拼一段
//   `MCP N 个服务 / M 个工具`，用户模式（产品面默认视图）也能看见。
// ============================================================

#include "App.h"
#include "ThemeUsing.h"
#include "UiKit.h"

#include "ECDI/Theme/PanelStyle.h"

#include <cstdio>
#include <string>
#include <utility>

namespace {

constexpr int kMcpRowH = 22;    // 与「代理」「模型」卡同高
constexpr int kMcpRowGap = 4;   // 卡内行距（同上）

// 配置来源 → 人话。未知来源原样显示（core 将来加级别时不会变哑）。
std::string SourceLabel(const std::string& source) {
    if (source == "workspace") return "工作区";
    if (source == "explicit") return "--mcp-config";
    if (source == "global") return "全局";
    return source;
}

}  // namespace

// ── 副标文本（AppHud.cpp 两种模式共用）────────────────────────────────
//
// ★ 返回空串 = 本次会话没有 MCP 状态（没配/旧 core）⇒ 副标逐字不变。
//   失败数追加在括号里，让"N 个服务 / M 个工具"与"还有 K 个没起来"同屏可读。
std::string HarnessApp::McpStatusText() const {
    if (!m_sess || !m_sess->mcp.has) return {};
    const McpStatus& m = m_sess->mcp;
    if (!m.problem.empty()) return "MCP 配置错误";

    int tools = 0;
    for (const auto& s : m.services) tools += s.tools;

    char buf[128];
    std::string out;
    if (m.services.empty()) {
        out = "MCP 0 个服务";
    } else {
        std::snprintf(buf, sizeof(buf), "MCP %d 个服务 / %d 个工具",
                      static_cast<int>(m.services.size()), tools);
        out = buf;
    }
    if (!m.failed.empty()) {
        std::snprintf(buf, sizeof(buf), "（%d 个失败）", static_cast<int>(m.failed.size()));
        out += buf;
    } else if (m.disabled > 0) {
        std::snprintf(buf, sizeof(buf), "（%d 个禁用）", m.disabled);
        out += buf;
    }
    return out;
}

// ── 建卡（由 `BuildInspector` 在「代理」卡之后调用）──────────────────
void HarnessApp::BuildMcpCard(ECDI::Widget& insp, int rowH) {
    auto card = std::make_unique<ECDI::Panel>();
    m_hud.inspMcpCard = card.get();
    card->SetVisible(false);   // 有 hello.mcp 时才显形（见 RebuildMcpCard）
    // 算式同「代理」卡：pad12 + 标题 17 + 间距 8 + 列表 + pad12
    card->SetSize(200, 49 + rowH);
    card->SetStyle(ECDI::PanelStyleOverride{
        .background = kContentBg, .cornerRadius = kRadiusCard2,
        .borderWidth = 1.0f, .borderColor = kCardBorder});
    card->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 12));
    card->AddChild(uikit::Label("MCP", 178, 14, kDarkMuted, harness::kFontMicro));

    auto list = std::make_unique<ECDI::Panel>();
    m_hud.mcpList = list.get();
    list->SetSize(190, rowH);
    list->SetLayout(std::make_unique<ECDI::VerticalLayout>(kMcpRowGap, false, 0));
    card->AddChild(std::move(list));

    insp.AddChild(std::move(card));
}

// ── 重建（hello 到达时一次；会话切换走整树重建，不在这里追）──────────
void HarnessApp::RebuildMcpCard() {
    if (!m_hud.mcpList || !m_hud.inspMcpCard) return;

    while (m_hud.mcpList->GetChildCount() > 0) {
        m_hud.mcpList->RemoveChild(m_hud.mcpList->GetChildAt(0));
    }

    Session* s = m_sess;
    const bool show = s && s->mcp.has;
    if (m_hud.inspMcpCard->IsVisible() != show) {
        m_hud.inspMcpCard->SetVisible(show);
        SyncInspectorLayout();
    }
    if (!show) return;

    auto addRow = [this](const std::string& text, const ECDI::Color& ink) {
        m_hud.mcpList->AddChild(uikit::Label(text, 178, kMcpRowH, ink));
    };

    const McpStatus& m = s->mcp;
    // ① 配置来源：先回答"读的是哪一级"，再看服务。
    if (!m.source.empty()) {
        addRow("配置 · " + SourceLabel(m.source), kDarkMuted);
    }
    // ② 配置层错误（解析失败 / 显式路径不存在）：卡片只放结论，全文在 system 块。
    if (!m.problem.empty()) {
        addRow("✗ 配置错误", kErrorDark);
    }
    // ③ 每个启动成功的服务：名字 · [传输 ·] 工具数。
    //   ★ stdio 是老路径，行文案保持不变；远程服务把 http/sse 标出来，用户才
    //     分得清"我配的是本地进程还是远端端点"。
    for (const auto& svc : m.services) {
        char buf[200];
        if (!svc.transport.empty() && svc.transport != "stdio") {
            std::snprintf(buf, sizeof(buf), "%s · %s · %d 工具",
                          svc.name.c_str(), svc.transport.c_str(), svc.tools);
        } else {
            std::snprintf(buf, sizeof(buf), "%s · %d 工具", svc.name.c_str(), svc.tools);
        }
        addRow(buf, kDarkInk);
    }
    // ④ 启动失败的服务：红色 + 结论；原因在 core 的 system 块（不在这里编）。
    for (const auto& f : m.failed) {
        addRow("✗ " + f.name + " · 启动失败", kErrorDark);
    }
    if (m.disabled > 0) {
        addRow(std::to_string(m.disabled) + " 个服务已禁用", kDarkMuted);
    }
    // ⑤ 读到了配置但一个可用项都没有：明确说"无可用服务"，不留白卡。
    if (m.services.empty() && m.failed.empty() && m.problem.empty()) {
        addRow("（无可用服务）", kDarkMuted);
    }

    const int rows = static_cast<int>(m_hud.mcpList->GetChildCount());
    const int listH = rows * kMcpRowH + (rows > 0 ? (rows - 1) * kMcpRowGap : 0);
    m_hud.inspMcpCard->SetSize(200, 49 + listH);
    m_hud.mcpList->SetSize(190, listH);
    m_hud.inspMcpCard->Arrange();
    SyncInspectorLayout();
    RequestRepaint();
}