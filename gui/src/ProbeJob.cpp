// ============================================================
// 后台作业卡探针（--job-probe，协议 v6 §3.13 `t:"job"`）
// ------------------------------------------------------------
// 判据形态：**模型侧 ⇔ 视图侧**逐项对齐（与 --proposal-probe 同一手法）。
//
// 为什么后台作业需要自己的判据：job 是**控制面**消息——它不建转录块，
//   只改「发起它的那张工具卡」的标题与折叠体。既有读数（blocks/deltas/
//   seq）对它**完全不可见**，截图也只能证明某一刻看起来还行。
//
// 判据（`matched` 是它们的合取）：
//   ① `modelJobs >= 1 && viewRows == modelJobs`
//        模型里有几个作业，屏上就该有几张挂对了 call 的工具卡。
//   ② `titleMatched == modelJobs`
//        每张卡的标题必须同时含 job id 与状态文案（运行中/已终止/…）。
//   ③ `tailMatched == modelJobs`
//        每张卡的正文必须含模型 tail 的**最后一行非空内容**——只对标题
//        不对正文，"卡片只有标题"的实现照样绿。
//   ④ `bodyMatched == modelJobs`
//        正文含 start.command ⇒ 卡确实挂在**发起该命令**的那张工具卡上，
//        而不是随便找了一行贴上去。
//   ⑤ `staleRows == 0`
//        负向对照：没有 job 消息的夹具（demo）上，任何工具行都不得出现
//        「后台」字样——否则「每张 run_command 卡都预先贴后台」也会绿。
//
// 负对照另跑一支（tools/regress.py）：`modelJobs==0 && viewRows==0 &&
//   staleRows==0` 才算通过。
// ============================================================

#include "App.h"
#include "AppInternal.h"

#include <cstdio>
#include <string>

namespace {

// TailLastLine：取模型 tail 的最后一行非空内容（去 \r；过长截 40 字符）。
//   用「最后一行」而不是第一行：ring 窗口丢弃的是头部，尾部才一定还在。
std::string TailLastLine(const std::string& tail) {
    size_t end = tail.size();
    while (end > 0 && (tail[end - 1] == '\n' || tail[end - 1] == '\r')) --end;
    if (end == 0) return {};
    size_t start = tail.rfind('\n', end - 1);
    start = (start == std::string::npos) ? 0 : start + 1;
    std::string line = tail.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() > 40) line = line.substr(0, 40);
    return line;
}

}  // namespace

void HarnessApp::RunJobProbe() {
    if (!m_sess) {
        m_probeState.jobProbeDone = true;
        return;
    }

    JobProbeRec rec;
    rec.modelJobs = m_sess->jobsByCall.size();

    for (const auto& kv : m_sess->jobsByCall) {
        const std::string& callId = kv.first;
        const JobView& v = kv.second;

        RowRec* row = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (r.blockId == callId) {
                row = &r;
                break;
            }
        }
        if (!row || !row->headerBtn || !row->label) continue;
        ++rec.viewRows;

        const std::string title = row->headerBtn->GetText();
        if (title.find(v.id) != std::string::npos &&
            title.find(harness::internal::JobStatusLabel(v.status)) != std::string::npos) {
            ++rec.titleMatched;
        }

        const std::string body = row->label->GetText();
        if (!v.command.empty() && body.find(v.command) != std::string::npos) {
            ++rec.bodyMatched;
        }
        const std::string lastLine = TailLastLine(v.tail);
        if (!lastLine.empty() && body.find(lastLine) != std::string::npos) {
            ++rec.tailMatched;
        }
    }

    // 负向：没有任何 job 时，任何工具行的标题/正文都不得出现「后台」。
    if (rec.modelJobs == 0) {
        for (const RowRec& r : m_sess->rows) {
            if (!r.blockId.empty() && !r.ask &&
                (r.headerBtn && r.headerBtn->GetText().find("后台") != std::string::npos ||
                 r.label && r.label->GetText().find("── 后台 ") != std::string::npos)) {
                ++rec.staleRows;
            }
        }
        rec.matched = (rec.viewRows == 0 && rec.staleRows == 0);
        if (!rec.matched) {
            char note[160];
            std::snprintf(note, sizeof(note), "negative: viewRows=%zu staleRows=%zu",
                          rec.viewRows, rec.staleRows);
            rec.note = note;
        }
    } else {
        rec.matched = rec.viewRows == rec.modelJobs &&
                      rec.titleMatched == rec.modelJobs &&
                      rec.bodyMatched == rec.modelJobs &&
                      rec.tailMatched == rec.modelJobs &&
                      rec.staleRows == 0;
        if (!rec.matched) {
            char note[256];
            std::snprintf(note, sizeof(note),
                          "modelJobs=%zu viewRows=%zu title=%zu body=%zu tail=%zu stale=%zu",
                          rec.modelJobs, rec.viewRows, rec.titleMatched, rec.bodyMatched,
                          rec.tailMatched, rec.staleRows);
            rec.note = note;
        }
    }

    m_probeState.jobProbeLog.push_back(std::move(rec));
    m_probeState.jobProbeDone = true;
}

void HarnessApp::AppendJobProbeSummary(std::string& json) {
    json += "  \"jobProbe\": [";
    for (size_t i = 0; i < m_probeState.jobProbeLog.size(); ++i) {
        const JobProbeRec& r = m_probeState.jobProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"modelJobs\": %zu, \"viewRows\": %zu, "
                      "\"titleMatched\": %zu, \"bodyMatched\": %zu, \"tailMatched\": %zu, "
                      "\"staleRows\": %zu, \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.modelJobs, r.viewRows, r.titleMatched,
                      r.bodyMatched, r.tailMatched, r.staleRows,
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}