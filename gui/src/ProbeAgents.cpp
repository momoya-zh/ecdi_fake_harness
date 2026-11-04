// ============================================================
// 子代理探针（--agent-probe，协议 v5）
// ------------------------------------------------------------
// 判据形态：**模型侧 ⇔ 视图侧**逐条对齐 + 几何不变量 + 生命周期读数。
//
// 为什么缩进需要自己的判据：它错了在既有读数里**完全不可见**——
//   `blocks` / `deltas` / `seqGaps` 与"这一行画在哪儿、多宽"毫无关系。
//   而截图只能证明"某一刻看起来还行"（本仓老教训：看起来正常不算判据）。
//
// 四条判据（`matched` 是它们的合取，跑批脚本还要额外看正/负对照两支）：
//   ① `indentRows == attributedBlocks`
//        模型里有多少带 `agent` 的块，屏上就该有多少缩进行。**两侧独立读出**——
//        只读一侧是自说自话（视图说有 7 行缩进，模型说只有 3 个归属块，也得绿）。
//   ② `gutterRows == indentRows`
//        缩进行必须**有自己的竖线控件**，而不是只把 x 挪了 24px。
//   ③ `overflowRows == 0`
//        缩进行的右边界不得越过根列的右边界。这条是「忘了同步缩宽度」的直检：
//        只挪 x 不减宽 ⇒ 每一行正好溢出 24px，肉眼几乎看不出（右边是空白画布），
//        但文本会被裁在视口外。
//   ④ `indentPx == kAgentIndent`
//        横移是**测出来的**（`panel->GetX() − 画布左边界`），不是把常数抄一遍。
//
// 负对照（跑批脚本跑另一支）：无 `agent` 的夹具上 `indentRows == 0` 且 `agents == 0`。
//   没有这一支，"恒 0" 与 "恒 24×N" 的实现都会绿。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ECDI/Widget/Panel.h"

// 跨 TU 共享符号（定义见 AppInternal.h）
using harness::internal::kAgentIndent;
using harness::internal::kCanvasPadX;

void HarnessApp::RunAgentProbe() {
    if (!m_sess) {
        m_probeState.agentProbeDone = true;
        return;
    }

    AgentProbeRec rec;
    rec.rows = m_sess->rows.size();

    // 画布几何：整栏左边界与整栏内宽（与 Relayout 同源，各算一次而不是抄数）
    const int canvasX = TranscriptCanvasX();
    const int innerW = std::max(120, TranscriptLayoutWidth() - 2 * kCanvasPadX);
    const int colRight = canvasX + innerW;

    std::map<std::string, size_t> perAgentRows;
    bool sawIndent = false;
    for (const RowRec& r : m_sess->rows) {
        if (!r.panel) continue;
        if (r.indent > 0) {
            ++rec.indentRows;
            if (r.gutter) ++rec.gutterRows;
            if (!r.agentTag.empty()) ++perAgentRows[r.agentTag];
            // ① 横移是测出来的：行左边界 − 画布左边界
            const int dx = r.panel->GetX() - canvasX;
            if (!sawIndent) {
                rec.indentPx = dx;
                sawIndent = true;
            } else {
                rec.indentPx = std::min(rec.indentPx, dx);
            }
            // ② 右边界不得越过根列
            const int right = r.panel->GetX() + r.panel->GetWidth();
            rec.maxRight = std::max(rec.maxRight, right);
            if (right > colRight) ++rec.overflowRows;
        } else {
            ++rec.rootRows;
        }
    }

    // 模型侧（独立读出）
    rec.attributedBlocks = m_sess->transcript.AgentBlocks();
    rec.agentViolations = m_sess->transcript.AgentViolations();
    rec.unknownTypes = m_sess->transcript.UnknownTypes();

    for (const auto& kv : m_sess->transcript.Agents()) {
        const AgentInfo& a = kv.second;
        ++rec.agents;
        if (a.ended) ++rec.agentsEnded;
        if (a.ended && a.ok) ++rec.agentsOk;
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s|%s|%s|%s|%d轮|%llu行|%lldms|%s",
                      a.id.c_str(),
                      a.name.empty() ? "-" : a.name.c_str(),
                      a.started ? (a.ended ? "end" : "run") : "nostart",
                      a.ok ? "ok" : "fail",
                      a.turns,
                      static_cast<unsigned long long>(perAgentRows[a.id]),
                      a.elapsedMs,
                      a.parentCall.empty() ? "-" : a.parentCall.c_str());
        rec.agentLines.push_back(buf);
    }

    // ★ ⑤ 右栏「代理」卡（2026-11-04 同轮补）：行数 = 子代理数 + 1（主代理行），
    //   且显隐与「有没有代理」严格同步。少了这条，卡片可以整块不工作而探针全绿。
    rec.cardRows = m_hud.agentList ? m_hud.agentList->GetChildCount() : 0;
    rec.cardVisible = m_hud.inspAgentCard && m_hud.inspAgentCard->IsVisible();
    const size_t agents = m_sess->transcript.Agents().size();
    const bool cardOk = rec.cardRows == agents + (agents > 0 ? 1 : 0) &&
                        rec.cardVisible == (agents > 0);

    // 元断言（见文件头 ①②③④⑤）
    rec.matched = rec.indentRows == rec.attributedBlocks &&
                  rec.gutterRows == rec.indentRows &&
                  rec.overflowRows == 0 &&
                  (rec.indentRows == 0 || rec.indentPx == kAgentIndent) &&
                  cardOk;

    if (!rec.matched) {
        char note[256];
        std::snprintf(note, sizeof(note),
                      "indentRows=%llu attributedBlocks=%llu gutter=%llu overflow=%llu indentPx=%d "
                      "cardRows=%llu cardVisible=%d agents=%llu",
                      static_cast<unsigned long long>(rec.indentRows),
                      static_cast<unsigned long long>(rec.attributedBlocks),
                      static_cast<unsigned long long>(rec.gutterRows),
                      static_cast<unsigned long long>(rec.overflowRows),
                      rec.indentPx,
                      static_cast<unsigned long long>(rec.cardRows),
                      rec.cardVisible ? 1 : 0,
                      static_cast<unsigned long long>(agents));
        rec.note = note;
    }

    m_probeState.agentProbeLog.push_back(std::move(rec));
    m_probeState.agentProbeDone = true;
}

void HarnessApp::AppendAgentProbeSummary(std::string& json) {
    // 机读证据：一行一个相位（本探针只有一相，保留数组形态与其它探针一致）。
    //   判据（跑批脚本执行，见文件头四条）+ 负对照（无 agent 的夹具 ⇒ 两个 0）。
    json += "  \"agentProbe\": [";
    auto esc = [](const std::string& in) {
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
    for (size_t i = 0; i < m_probeState.agentProbeLog.size(); ++i) {
        const AgentProbeRec& r = m_probeState.agentProbeLog[i];
        json += (i ? ", " : "");
        char b[1024];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"rows\": %zu, \"indentRows\": %zu, \"rootRows\": %zu, "
                      "\"gutterRows\": %zu, \"attributedBlocks\": %zu, "
                      "\"indentPx\": %d, \"overflowRows\": %zu, \"maxRight\": %d, "
                      "\"agents\": %zu, \"agentsEnded\": %zu, \"agentsOk\": %zu, "
                      "\"agentViolations\": %zu, \"unknownTypes\": %zu, "
                      "\"cardRows\": %zu, \"cardVisible\": %s, "
                      "\"matched\": %s, \"note\": \"%s\", \"agentList\": [",
                      r.phase.c_str(), r.rows, r.indentRows, r.rootRows, r.gutterRows,
                      r.attributedBlocks, r.indentPx, r.overflowRows, r.maxRight,
                      r.agents, r.agentsEnded, r.agentsOk, r.agentViolations,
                      r.unknownTypes, r.cardRows, r.cardVisible ? "true" : "false",
                      r.matched ? "true" : "false", esc(r.note).c_str());
        json += b;
        for (size_t k = 0; k < r.agentLines.size(); ++k) {
            json += (k ? ", " : "");
            json += "\"" + esc(r.agentLines[k]) + "\"";
        }
        json += "]}";
    }
    json += "],\n";
}
