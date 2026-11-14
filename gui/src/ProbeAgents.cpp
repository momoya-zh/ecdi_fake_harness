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
#include "ECDI/Widget/ScrollView.h"

// 跨 TU 共享符号（定义见 AppInternal.h）
using harness::internal::kAgentGutterW;
using harness::internal::kAgentIndent;
using harness::internal::kCanvasPadX;
using harness::internal::kRowGap;

void HarnessApp::RunAgentProbe() {
    if (!m_sess) {
        m_probeState.agentProbeDone = true;
        return;
    }

    AgentProbeRec rec;
    rec.phase = "agent-layout";
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
    // ★★ 判据按**契约**写，不按"有没有 agents"写（2026-11-04 订正）：
    //   协议 §3.1 说「支持子代理的 core 必须声明 `caps.agents`；未声明 ⇒ GUI 不得
    //   显示代理卡」。所以正确行为有**两支**：
    //     · 声明了 cap ∧ 有代理 ⇒ 卡显形，行数 = agents + 1（主代理那行）
    //     · 没声明 cap         ⇒ 卡必须**没有**，哪怕转录里真有 agent 消息
    //         （不合规的 core 可以发消息却不声明能力；内容照旧渲染，卡片不得出现）
    //   ★ 为什么必须这么写：若只判"卡跟着 agents 走"，那么**门控整个不存在**的实现
    //     在"有 cap"与"无 agent"两支上也都是绿的——负向用例恒成立等于没有负向用例
    //     （`fixtures/subagent/agent-cap-missing.jsonl` 就是专为这一支造的）。
    const bool cardOk = m_sess->hasAgentsCap && agents > 0
                            ? (rec.cardRows == agents + 1 && rec.cardVisible)
                            : (rec.cardRows == 0 && !rec.cardVisible);

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

    const int baseIndentPx = rec.indentPx;
    m_probeState.agentProbeLog.push_back(std::move(rec));

    // ★ 相位 ②（正对照专享：有 agents cap ∧ 有真实代理块）：
    //   验证连续归属竖线（跨 kRowGap 衔接）与代理卡定位高亮（含折叠自动展开与清理）。
    if (m_sess->hasAgentsCap && !m_sess->transcript.Agents().empty()) {
        std::map<std::string, size_t> counts;
        std::string targetId;
        for (const RowRec& r : m_sess->rows) {
            if (!r.agentTag.empty()) ++counts[r.agentTag];
        }
        for (const auto& kv : counts) {
            if (kv.second >= 2) { targetId = kv.first; break; }
        }
        if (targetId.empty() && !counts.empty()) {
            targetId = counts.begin()->first;
        }
        AgentProbeRec rec2;
        rec2.phase = "agent-gutter-and-focus";
        rec2.rows = m_sess->rows.size();
        rec2.attributedBlocks = m_sess->transcript.AgentBlocks();
        rec2.agentViolations = m_sess->transcript.AgentViolations();
        rec2.unknownTypes = m_sess->transcript.UnknownTypes();
        rec2.cardRows = m_hud.agentList ? m_hud.agentList->GetChildCount() : 0;
        rec2.cardVisible = m_hud.inspAgentCard && m_hud.inspAgentCard->IsVisible();
        const AgentProbeRec& baseRec = m_probeState.agentProbeLog.front();
        rec2.agentLines = baseRec.agentLines;
        rec2.agents = baseRec.agents;
        rec2.agentsEnded = baseRec.agentsEnded;
        rec2.agentsOk = baseRec.agentsOk;

        std::string detail;
        auto fail = [&](const std::string& msg) {
            if (!detail.empty()) detail += "; ";
            detail += msg;
        };
        if (targetId.empty()) {
            fail("no attributed agent rows found");
        } else {
            size_t foldIdx = static_cast<size_t>(-1);
            bool wasExpanded = true;
            for (const RowRec& r : m_sess->rows) {
                if (r.agentTag != targetId) continue;
                if (r.timelineIdx < m_sess->timeline.size()) {
                    for (size_t i = r.timelineIdx + 1; i > 0; --i) {
                        const size_t ti = i - 1;
                        if (m_sess->timeline[ti].kind == TimelineEntry::Kind::Fold) {
                            foldIdx = ti;
                            wasExpanded = m_sess->timeline[ti].expanded;
                            break;
                        }
                    }
                }
                break;
            }
            if (foldIdx != static_cast<size_t>(-1) && wasExpanded) {
                ToggleTurn(foldIdx);  // 先人工折起，实测 FocusAgent 的自动展开链路
            }
            FocusAgent(targetId);

            if (m_focusedAgentId != targetId) {
                fail("m_focusedAgentId did not match " + targetId);
            }
            size_t focusBorders = 0;
            size_t continuousBridges = 0;
            size_t targetVisible = 0;
            const RowRec* firstVis = nullptr;
            for (size_t i = 0; i < m_sess->rows.size(); ++i) {
                const RowRec& r = m_sess->rows[i];
                if (!r.panel) continue;
                if (r.agentTag == targetId) {
                    if (r.foldHidden) fail("agent row stayed foldHidden after FocusAgent");
                    if (!firstVis) firstVis = &r;
                    ++targetVisible;
                    if (r.focusHighlight) ++focusBorders;
                    if (!r.gutter) {
                        fail("target agent row missing gutter");
                    } else if (!m_options.rowModeList) {
                        if (r.gutter->GetParent() != m_content) {
                            fail("gutter parent is not m_content");
                        }
                        if (!r.gutter->IsVisible()) fail("gutter not visible after FocusAgent");
                        if (r.gutter->GetWidth() != kAgentGutterW + 1) {
                            fail("focused gutter width != kAgentGutterW+1");
                        }
                        size_t nextVis = i + 1;
                        while (nextVis < m_sess->rows.size() && m_sess->rows[nextVis].foldHidden) {
                            ++nextVis;
                        }
                        if (nextVis < m_sess->rows.size() && m_sess->rows[nextVis].agentTag == targetId) {
                            ++continuousBridges;
                            if (r.gutter->GetHeight() != r.panel->GetHeight() + kRowGap) {
                                fail("continuous gutter height != rowH+kRowGap");
                            }
                        } else if (r.gutter->GetHeight() != r.panel->GetHeight()) {
                            fail("terminal agent gutter height != rowH");
                        }
                    }
                } else if (r.focusHighlight) {
                    fail("non-target row has focusHighlight");
                }
            }
            if (targetVisible == 0) fail("no visible rows for target agent");
            if (focusBorders != 1) {
                fail("expected focusBorders==1 got " + std::to_string(focusBorders));
            }
            if (!m_options.rowModeList && counts[targetId] >= 2 && continuousBridges == 0) {
                fail("expected >=1 continuous gutter bridge for multi-row agent");
            }
            if (firstVis && !firstVis->focusHighlight) {
                fail("focusHighlight not on first visible row of target agent");
            }
            if (m_scroll && firstVis) {
                const int maxY = m_scroll->GetMaxOffsetY();
                const int wantOff = std::min(std::max(0, firstVis->y - 8), maxY);
                if (m_scroll->GetScrollOffsetY() != wantOff) {
                    fail("scrollOffsetY mismatch after FocusAgent");
                }
            }

            ClearFocusedAgentHighlight();
            if (!m_focusedAgentId.empty()) fail("m_focusedAgentId not cleared");
            for (const RowRec& r : m_sess->rows) {
                if (r.focusHighlight || r.gutterHighlight) {
                    fail("highlight flag remained after ClearFocusedAgentHighlight");
                }
                if (r.gutter && !r.foldHidden && !m_options.rowModeList &&
                    r.gutter->GetWidth() != kAgentGutterW) {
                    fail("gutter width not restored to kAgentGutterW");
                }
            }
            if (foldIdx != static_cast<size_t>(-1) && foldIdx < m_sess->timeline.size() &&
                m_sess->timeline[foldIdx].expanded != wasExpanded) {
                ToggleTurn(foldIdx);
            }
        }
        for (const RowRec& r : m_sess->rows) {
            if (!r.panel) continue;
            if (r.indent > 0) {
                ++rec2.indentRows;
                if (r.gutter) ++rec2.gutterRows;
                const int right = r.panel->GetX() + r.panel->GetWidth();
                rec2.maxRight = std::max(rec2.maxRight, right);
                if (right > colRight) ++rec2.overflowRows;
            } else {
                ++rec2.rootRows;
            }
        }
        rec2.indentPx = baseIndentPx;
        rec2.matched = detail.empty();
        rec2.note = detail;
        m_probeState.agentProbeLog.push_back(std::move(rec2));
    }
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
