// ============================================================
// 回合级折叠探针（--fold-probe，2026-11-06）
// ------------------------------------------------------------
// 需求原话："read_file、grep 这类都是打印出来的……最后回答出来后能把前面的
//   这类都收起来，不然上下翻太费时间了。" ⇒ 回答结束（`state:idle`）时，
//   本回合的过程行（tool_call/tool_result/reasoning/中间 assistant）收成一条摘要，
//   user 消息与最终回答保持可见。
//
// 为什么这个特性需要自己的判据：
//   折叠是**纯视图上的减法**——协议层一个字节都没变。`blocks/deltas/messages/seq`
//   这套读数对它完全不可见（这正是它可以零扰动进回归的原因，也是它必须有专门
//   探针的原因：否则"什么都没做"与"做对了"读数完全一样，都是全绿）。
//
// 判据（`matched` 是它们的合取）：
//   ① `folds >= 1`
//        —— 确实折了。零折叠也能让 ②③ 真空成立（0 == 0），所以这条把真空堵掉。
//   ② `hiddenRows == foldHiddenCount`
//        —— 模型说该隐藏的每一条，视图控件**真的**不可见；一条不多一条不少。
//          这是"模型 ⇔ 视图"的逐项对齐（同 Job/Proposal 两支的手法）。
//   ③ `visibleProcRows == procRows - foldHiddenCount`
//        —— 屏上还能看见的过程行数，恰好等于"过程行总数 − 被隐藏的"。
//          只查 ② 不够：一个"把过程行全藏了（包括摘要条）"的实现也能过 ②。
//   ④ `summaryOk == folds`
//        —— 每条摘要条的文案含「已完成」，且步数 > 0（`· N 步`）。
//   ⑤ `foldRows == folds`
//        —— 屏上摘要条数与模型一致（不多画一条幽灵条）。
//   ⑥ `rowsAfter == rowsBefore`
//        —— **展开一次再折回来**，`rows.size()` 纹丝不动。
//          这条守的是本特性的第一条不变量"折叠不改变条目数"：
//          插入式实现会让它 +1，而那种实现同时会污染 `--rebuild-probe` 的
//          `rebuildTimelineCount`（那支从此对任何插入类缺陷都不再敏感）。
//   ⑦ `expandRestores > 0`
//        —— 展开以后过程行**确实回来了**（不是只会藏、不会放）。
//   ⑧ 负向：`folds == 0` 且 `foldHiddenCount == 0`
//        —— 过程行不足两条的夹具（`subagent`：1 回合 11 块但过程行稀疏）上，
//          一行都不许折。没有这条，"无脑全折"在正臂上是绿的。
//
// 跑批（tools/regress.py）：正臂用 realistic（32 回合，过程行密集），
//   负臂用 subagent（过程行不足）。两臂都要求 `violations == 0`。
// ============================================================

#include "App.h"
#include "AppInternal.h"

#include <cstdio>
#include <string>

// 视图侧：当前**可见**的过程行（模型标 `foldable`、且控件可见）条数。
// ★ 单列一个函数是因为它有两个消费者：探针主体（判据 ③）与往返臂（判据 ⑦）。
//   两处各写一遍遍历，判据 ③ 与 ⑦ 就会在"什么算过程行"上悄悄分叉。
size_t HarnessApp::CountVisibleProcRows() const {
    if (!m_sess) return 0;
    size_t n = 0;
    for (const RowRec& r : m_sess->rows) {
        if (r.foldable && r.panel && r.panel->IsVisible()) ++n;
    }
    return n;
}

void HarnessApp::RunFoldProbe() {
    if (!m_sess) {
        m_probeState.foldProbeDone = true;
        return;
    }

    FoldProbeRec rec;

    // ── 模型侧 ────────────────────────────────────────────────
    // 数 `timeline` 上的三个量：摘要条数、过程行数、被隐藏数。
    for (size_t i = 0; i < m_sess->timeline.size(); ++i) {
        const TimelineEntry& e = m_sess->timeline[i];
        if (e.kind == TimelineEntry::Kind::Fold) ++rec.folds;
        if (e.foldable) ++rec.procRows;
        if (e.foldHidden) ++rec.foldHiddenCount;
    }

    // ── 视图侧 ────────────────────────────────────────────────
    // ★ 只读 `RowRec` 的字段与控件的 `IsVisible()`，**不回查模型**——
    //   回查会让 A == B 变成同义反复（`SnapshotRows` 的同一纪律）。
    for (const RowRec& r : m_sess->rows) {
        const bool visible = r.panel ? r.panel->IsVisible() : false;
        if (r.foldHidden) {
            // 模型说隐藏 ⇒ 控件必须真的不可见。若可见，这条不计入 → 判据 ② 红。
            if (!visible) ++rec.hiddenRows;
        }
        if (r.foldBar) {
            ++rec.foldRows;
            if (r.headerBtn) {
                const std::string t = r.headerBtn->GetText();
                if (t.find("已完成") != std::string::npos &&
                    t.find("\xE6\xAD\xA5") != std::string::npos) {   // "步"
                    ++rec.summaryOk;
                }
            }
        }
        // 可见过程行：模型标了 `foldable`、且控件可见（被折起来的那些不算）。
        if (r.foldable && visible) ++rec.visibleProcRows;
    }

    rec.rowsBefore = m_sess->rows.size();

    // ── 往返：展开 → 再折叠 ────────────────────────────────────
    // ★ 只对**第一条**摘要条做往返：多折几次会把耗时拉长，而判据是"行数不变 +
    //   过程行能回来"，做一次就够证明，做 N 次不多证明任何东西。
    size_t foldIdx = static_cast<size_t>(-1);
    for (size_t i = 0; i < m_sess->timeline.size(); ++i) {
        if (m_sess->timeline[i].kind == TimelineEntry::Kind::Fold) {
            foldIdx = i;
            break;
        }
    }
    if (foldIdx != static_cast<size_t>(-1)) {
        // 记下这条回合在展开后应当可见的过程行数（判据 ⑦ 的期望值）。
        ToggleTurn(foldIdx);   // 展开
        const size_t expandedProc = CountVisibleProcRows();
        ToggleTurn(foldIdx);   // 再折叠
        rec.expandRestores = expandedProc;
        rec.rowsAfter = m_sess->rows.size();
    } else {
        rec.rowsAfter = rec.rowsBefore;
    }

    // ── 判定 ──────────────────────────────────────────────────
    if (rec.folds == 0) {
        // 负臂：不该折的夹具上，"没折"才是对的。
        rec.matched = (rec.foldHiddenCount == 0 && rec.foldRows == 0);
        if (!rec.matched) {
            char note[192];
            std::snprintf(note, sizeof(note),
                          "negative: foldHidden=%zu foldRows=%zu (expected 0/0)",
                          rec.foldHiddenCount, rec.foldRows);
            rec.note = note;
        }
    } else {
        rec.matched = rec.folds >= 1 &&
                      rec.hiddenRows == rec.foldHiddenCount &&
                      rec.visibleProcRows == rec.procRows - rec.foldHiddenCount &&
                      rec.summaryOk == rec.folds &&
                      rec.foldRows == rec.folds &&
                      rec.rowsAfter == rec.rowsBefore &&
                      rec.expandRestores > 0;
        if (!rec.matched) {
            char note[384];
            std::snprintf(note, sizeof(note),
                          "folds=%zu proc=%zu hidden=%zu/%zu visibleProc=%zu "
                          "summary=%zu foldRows=%zu rows=%zu->%zu restore=%zu",
                          rec.folds, rec.procRows, rec.hiddenRows, rec.foldHiddenCount,
                          rec.visibleProcRows, rec.summaryOk, rec.foldRows,
                          rec.rowsBefore, rec.rowsAfter, rec.expandRestores);
            rec.note = note;
        }
    }

    m_probeState.foldProbeLog.push_back(std::move(rec));
    m_probeState.foldProbeDone = true;
}

void HarnessApp::AppendFoldProbeSummary(std::string& json) {
    json += "  \"foldProbe\": [";
    for (size_t i = 0; i < m_probeState.foldProbeLog.size(); ++i) {
        const FoldProbeRec& r = m_probeState.foldProbeLog[i];
        json += (i ? ", " : "");
        char b[768];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"folds\": %zu, \"procRows\": %zu, "
                      "\"foldHiddenCount\": %zu, \"hiddenRows\": %zu, "
                      "\"visibleProcRows\": %zu, \"foldRows\": %zu, \"summaryOk\": %zu, "
                      "\"rowsBefore\": %zu, \"rowsAfter\": %zu, \"expandRestores\": %zu, "
                      "\"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.folds, r.procRows, r.foldHiddenCount,
                      r.hiddenRows, r.visibleProcRows, r.foldRows, r.summaryOk,
                      r.rowsBefore, r.rowsAfter, r.expandRestores,
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}
