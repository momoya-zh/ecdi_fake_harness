// ============================================================
// MCP 产品面可见性探针（--mcp-probe，hello.mcp，2026-11-06）
// ------------------------------------------------------------
// 为什么需要它：`hello.mcp` 是新增的**展示数据**，既有读数（blocks/deltas/
//   seq）对它完全不可见；截图能证明某一刻看起来还行，但证明不了"每次都对"。
//   本仓对新增 UI 面的纪律是：模型侧一份、视图侧一份，两条独立路径相等。
//
// 判据（`matched` 是它们的合取）：
//   ① 正向夹具：模型侧 modelServices/modelFailed 必须 >0（成功与失败同屏，
//      否则"（K 个失败）"与红行永远走不到）。
//   ② 卡片行数 == 模型推导的期望行数（配置来源/问题/服务/失败/禁用/空态）。
//   ③ 每个服务名与每个失败服务名各在卡片里出现**一次**。
//      ★ 六版新增：远程服务（`transport != "stdio"`）的行必须逐字形如
//        `<name> · <transport> · <N> 工具` —— 只查「行里有 http/sse 字样」
//        会让「所有行都写死 http」的实现假绿。
//   ④ 会话头副标含 `McpStatusText()` 的逐字结果；没有 mcp 字段时不得出现 "MCP"。
//   ⑤ 屏上 `[MCP]` system 行数 == 失败数 + (配置错误 ? 1 : 0)。
//   ⑥ 负向（demo）：没有 mcp 字段 ⇒ 卡片不可见、0 行、副标无 MCP、0 条 MCP 行。
//
// ★ 负对照必须另跑一支（demo 夹具）：正向只证明"有状态时会显示"，
//   "每个会话都预先显示一张 MCP 卡"的实现同样能绿。见 tools/regress.py。
// ============================================================

#include "App.h"
#include "AppInternal.h"

#include "ECDI/Widget/Label.h"

#include <cstdio>
#include <string>

namespace {

// 卡片行文本：所有行都建成 Label（AppMcp.cpp），这里逐个读回。
std::string RowText(const ECDI::Widget* w) {
    const auto* label = dynamic_cast<const ECDI::Label*>(w);
    return label ? label->GetText() : std::string();
}

}  // namespace

void HarnessApp::RunMcpProbe() {
    if (!m_sess) {
        m_probeState.mcpProbeDone = true;
        return;
    }

    McpProbeRec rec;
    const McpStatus& m = m_sess->mcp;
    rec.modelHas = m.has;
    rec.modelServices = m.services.size();
    rec.modelFailed = m.failed.size();
    for (const auto& svc : m.services) {
        rec.modelTools += static_cast<size_t>(svc.tools);
        if (!svc.transport.empty() && svc.transport != "stdio") ++rec.modelRemote;
    }

    // 视图侧：卡片
    rec.cardVisible = m_hud.inspMcpCard && m_hud.inspMcpCard->IsVisible();
    if (m_hud.mcpList) {
        rec.cardRows = static_cast<size_t>(m_hud.mcpList->GetChildCount());
        for (size_t i = 0; i < m_hud.mcpList->GetChildCount(); ++i) {
            const std::string text = RowText(m_hud.mcpList->GetChildAt(i));
            // 用**行首前缀**而不是子串：`fs` 与 `fs2` 这类服务名互为子串时，
            // 子串匹配会让"两个服务各画一行"的判据假绿。
            for (const auto& svc : m.services) {
                if (!svc.name.empty() && text.rfind(svc.name + " · ", 0) == 0) {
                    ++rec.serviceRows;
                    // ★ 六版：远程服务的行里必须**逐字**含它自己的 transport。
                    //   只查「行里有没有 http/sse 字样」会让「所有行都写死 http」
                    //   的实现假绿——所以按服务逐个比对。
                    const bool remote = !svc.transport.empty() && svc.transport != "stdio";
                    if (remote &&
                        text.rfind(svc.name + " · " + svc.transport + " · ", 0) == 0) {
                        ++rec.transportRows;
                    }
                    break;
                }
            }
            for (const auto& f : m.failed) {
                if (!f.name.empty() && text.rfind("✗ " + f.name + " · 启动失败", 0) == 0) {
                    ++rec.failureRows;
                    break;
                }
            }
        }
    }

    // 视图侧：副标 + 失败 system 行
    const std::string sub = m_hud.convSub ? m_hud.convSub->GetText() : std::string();
    rec.subtitleHasMcp = sub.find("MCP") != std::string::npos;
    const std::string expected = McpStatusText();
    rec.subtitleMatched = m.has ? (!expected.empty() && sub.find(expected) != std::string::npos)
                                : (sub.find("MCP") == std::string::npos);
    for (const RowRec& r : m_sess->rows) {
        if (r.label && r.label->GetText().find("[MCP]") != std::string::npos) {
            ++rec.renderedMcpNotices;
        }
    }

    // 期望行数：与 AppMcp.cpp::RebuildMcpCard 的分支一一对应，但**从模型重推**。
    size_t expectedRows = 0;
    if (!m.source.empty()) ++expectedRows;
    if (!m.problem.empty()) ++expectedRows;
    expectedRows += m.services.size() + m.failed.size();
    if (m.disabled > 0) ++expectedRows;
    if (m.services.empty() && m.failed.empty() && m.problem.empty()) ++expectedRows;
    const size_t expectedNotices = m.failed.size() + (m.problem.empty() ? 0 : 1);

    if (m.has) {
        rec.matched = rec.modelServices > 0 && rec.modelFailed > 0 &&
                      rec.cardVisible && rec.cardRows == expectedRows &&
                      rec.serviceRows == rec.modelServices &&
                      rec.transportRows == rec.modelRemote &&
                      rec.failureRows == rec.modelFailed &&
                      rec.subtitleHasMcp && rec.subtitleMatched &&
                      rec.renderedMcpNotices == expectedNotices;
    } else {
        rec.matched = !rec.cardVisible && rec.cardRows == 0 &&
                      !rec.subtitleHasMcp && rec.subtitleMatched &&
                      rec.renderedMcpNotices == 0;
    }

    if (!rec.matched) {
        char note[480];
        std::snprintf(note, sizeof(note),
                      "has=%d services=%zu failed=%zu tools=%zu card=%d rows=%zu/%zu "
                      "svcRows=%zu remoteRows=%zu/%zu failRows=%zu notices=%zu/%zu "
                      "subHasMcp=%d subMatched=%d",
                      rec.modelHas ? 1 : 0, rec.modelServices, rec.modelFailed, rec.modelTools,
                      rec.cardVisible ? 1 : 0, rec.cardRows, expectedRows,
                      rec.serviceRows, rec.transportRows, rec.modelRemote,
                      rec.failureRows, rec.renderedMcpNotices, expectedNotices,
                      rec.subtitleHasMcp ? 1 : 0, rec.subtitleMatched ? 1 : 0);
        rec.note = note;
    }

    m_probeState.mcpProbeLog.push_back(std::move(rec));
    m_probeState.mcpProbeDone = true;
}

void HarnessApp::AppendMcpProbeSummary(std::string& json) {
    json += "  \"mcpProbe\": [";
    for (size_t i = 0; i < m_probeState.mcpProbeLog.size(); ++i) {
        const McpProbeRec& r = m_probeState.mcpProbeLog[i];
        json += (i ? ", " : "");
        char b[896];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"modelHas\": %s, \"modelServices\": %zu, "
                      "\"modelFailed\": %zu, \"modelTools\": %zu, \"modelRemote\": %zu, "
                      "\"cardVisible\": %s, \"cardRows\": %zu, \"serviceRows\": %zu, "
                      "\"transportRows\": %zu, \"failureRows\": %zu, "
                      "\"renderedMcpNotices\": %zu, \"subtitleHasMcp\": %s, "
                      "\"subtitleMatched\": %s, \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.modelHas ? "true" : "false", r.modelServices,
                      r.modelFailed, r.modelTools, r.modelRemote,
                      r.cardVisible ? "true" : "false",
                      r.cardRows, r.serviceRows, r.transportRows, r.failureRows,
                      r.renderedMcpNotices,
                      r.subtitleHasMcp ? "true" : "false",
                      r.subtitleMatched ? "true" : "false",
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}