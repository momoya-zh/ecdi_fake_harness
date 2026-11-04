// ============================================================
// 提案卡探针（--proposal-probe，协议 v5 §3.6 可选 detail）
// ------------------------------------------------------------
// 判据形态：**模型侧 ⇔ 视图侧**逐条对齐（与 --agent-probe 同一手法）。
//
// 为什么提案卡需要自己的判据：ask 门从"文本问句"升级为"结构化提案"后，
//   审阅质量是全新 UI 面——diff 行数、配色行、键值行在既有读数里**完全不可见**
//   （`blocks`/`deltas` 与"折叠体里画了几行"毫无关系），截图只能证明某一刻
//   看起来还行。回归门必须有机读证据。
//
// 判据（`matched` 是它们的合取；跑批脚本另跑负对照支）：
//   ① `detailRows == modelDetailRows`
//        模型里有多少带 detail 的 ask，屏上就该有多少提案卡。**两条独立计算
//        路径**——视图数"建了提案体的行"，模型重读 timeline 条目。
//   ② `diffRows == modelDiffRows`（且 plus/minus 逐项相等）
//        提案行不是"随便画了几行"：diff 文本重切行计数必须与建行期统计一致。
//        少了这条，"每卡固定画 3 行"的实现照样绿。
//   ③ `emptyDetailRows == 0`
//        有 detail 却一行都没建出来 = 空卡。回落义务（§3.6）只在 detail
//        **缺失**时走，detail 在场却空渲染是 bug。
//   ④ `askRows >= 1 && detailRows >= 1`
//        正向夹具必须真有带提案的权限门——否则判据在空转录上恒真。
//
// 负对照（跑批脚本跑另一支，见 tools/regress.py）：无 `detail` 的夹具
//   （demo）上 `detailRows == 0 && diffRows == 0` 且 `askRows >= 1`
//   ——回落路径完好，v4 行为逐位保留。没有这一支，"每个 ask 都画提案体"
//   的实现也会绿。
// ============================================================

#include "App.h"
#include "AppInternal.h"

#include <cstdio>
#include <string>
#include <vector>

void HarnessApp::RunProposalProbe() {
    if (!m_sess) {
        m_probeState.proposalProbeDone = true;
        return;
    }

    ProposalProbeRec rec;

    // 模型侧（独立重算：不读 RowRec，直接重切 timeline 条目的 detail）
    for (const TimelineEntry& e : m_sess->timeline) {
        if (e.kind != TimelineEntry::Kind::Ask) continue;
        if (!e.askDetail.has) continue;
        ++rec.modelDetailRows;
        size_t start = 0;
        const std::string& diff = e.askDetail.diff;
        while (start <= diff.size()) {
            const size_t nl = diff.find('\n', start);
            const std::string line =
                diff.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            if (!line.empty()) {
                ++rec.modelDiffRows;
                if (line[0] == '+') ++rec.modelPlus;
                if (line[0] == '-') ++rec.modelMinus;
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    // 视图侧（建行期统计）
    for (const RowRec& r : m_sess->rows) {
        if (!r.ask) continue;
        ++rec.askRows;
        if (r.proposal.lines.empty()) continue;
        ++rec.detailRows;
        rec.diffRows += r.proposal.diffLines;
        rec.plusRows += r.proposal.plus;
        rec.minusRows += r.proposal.minus;
        rec.kvRows += r.proposal.kvLines;
    }
    // ③ 有 detail 却没建出提案行（视图 ask 行与模型条目一一对应，交叉核对）
    rec.emptyDetailRows = rec.modelDetailRows > rec.detailRows
                              ? rec.modelDetailRows - rec.detailRows
                              : 0;

    // 元断言（见文件头 ①②③④）
    rec.matched = rec.detailRows == rec.modelDetailRows &&
                  rec.diffRows == rec.modelDiffRows &&
                  rec.plusRows == rec.modelPlus &&
                  rec.minusRows == rec.modelMinus &&
                  rec.emptyDetailRows == 0 &&
                  rec.askRows >= 1 && rec.detailRows >= 1;

    if (!rec.matched) {
        char note[256];
        std::snprintf(note, sizeof(note),
                      "askRows=%llu detailRows=%llu modelDetail=%llu diffRows=%llu modelDiff=%llu "
                      "plus=%llu/%llu minus=%llu/%llu empty=%llu",
                      static_cast<unsigned long long>(rec.askRows),
                      static_cast<unsigned long long>(rec.detailRows),
                      static_cast<unsigned long long>(rec.modelDetailRows),
                      static_cast<unsigned long long>(rec.diffRows),
                      static_cast<unsigned long long>(rec.modelDiffRows),
                      static_cast<unsigned long long>(rec.plusRows),
                      static_cast<unsigned long long>(rec.modelPlus),
                      static_cast<unsigned long long>(rec.minusRows),
                      static_cast<unsigned long long>(rec.modelMinus),
                      static_cast<unsigned long long>(rec.emptyDetailRows));
        rec.note = note;
    }

    m_probeState.proposalProbeLog.push_back(std::move(rec));
    m_probeState.proposalProbeDone = true;
}

void HarnessApp::AppendProposalProbeSummary(std::string& json) {
    // 机读证据：一行一相（本探针一相，数组形态与其它探针一致）。
    //   判据（跑批脚本执行，见文件头四条）+ 负对照（无 detail 夹具 ⇒ 全 0）。
    json += "  \"proposalProbe\": [";
    for (size_t i = 0; i < m_probeState.proposalProbeLog.size(); ++i) {
        const ProposalProbeRec& r = m_probeState.proposalProbeLog[i];
        json += (i ? ", " : "");
        char b[768];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"askRows\": %zu, \"detailRows\": %zu, "
                      "\"diffRows\": %zu, \"plusRows\": %zu, \"minusRows\": %zu, "
                      "\"kvRows\": %zu, "
                      "\"modelDetailRows\": %zu, \"modelDiffRows\": %zu, "
                      "\"modelPlus\": %zu, \"modelMinus\": %zu, "
                      "\"emptyDetailRows\": %zu, \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.askRows, r.detailRows, r.diffRows,
                      r.plusRows, r.minusRows, r.kvRows, r.modelDetailRows, r.modelDiffRows,
                      r.modelPlus, r.modelMinus, r.emptyDetailRows,
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}
