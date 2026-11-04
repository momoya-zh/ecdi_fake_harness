#include "Transcript.h"

#include <cstdio>

bool Transcript::OnLine(const std::string& jsonLine, std::string& error) {
    std::string parseError;
    if (!json::Parse(jsonLine, m_last, parseError)) {
        ++m_violations;
        error = "[PARSE] line rejected: " + parseError;
        return false;
    }

    const std::string t = m_last.GetString("t");

    // I4：seq 每方向从 0 起严格递增且连续；缺口必须显式提示，不得静默继续。
    // 本行仍被处理（缺口提示 ≠ 丢弃这条消息）。
    const json::Value* seq = m_last.Find("seq");
    if (seq && seq->IsNumber()) {
        const long long current = static_cast<long long>(seq->number);
        if (m_seenSeq && current != m_lastSeq + 1) {
            ++m_seqGaps;
            ++m_violations;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[I4] seq gap %lld -> %lld: messages lost",
                          m_lastSeq, current);
            error = buf;
        }
        m_lastSeq = current;
        m_seenSeq = true;
    }

    if (t == "block_start") {
        const std::string id = m_last.GetString("id");
        if (id.empty() || m_blocks.count(id) != 0) {
            ++m_violations;
            error = "[I1] duplicate block_start: " + id;
            return false;
        }
        Block block;
        block.id = id;
        block.clientId = m_last.GetString("client_id");
        block.kind = m_last.GetString("kind");
        block.name = m_last.GetString("name");
        block.forId = m_last.GetString("for");
        block.text = m_last.GetString("text");
        // ★★ v5 §3.3：子代理归属（可选字段，缺省即根代理）。
        block.agentId = m_last.GetString("agent");
        block.parentId = m_last.GetString("parent");
        if (!block.agentId.empty()) {
            ++m_agentBlocks;
            // I12 的 GUI 侧记账：归属一个没 start 过的代理 ⇒ 计数（渲染照旧）。
            if (m_agents.find(block.agentId) == m_agents.end()) ++m_agentViolations;
        }
        // ★★ 未知 `kind`（2026-10-13）：协议 §3.3 只列了六个值，而 I5 只管信封层的 `t`
        //   ⇒ 未知 `kind` **不被任何不变量覆盖**。此前它静默掉进 App 的工具卡分支、
        //   标题写死「等待结果」⇒ 与「正在等的 tool_result」视觉上无法区分（**误导**）。
        //   此处**只记账 + 显式提示**（不改块的解析结果——I5 的前向兼容精神：
        //   未知内容仍被保留与渲染，只是不再伪装成有语义的形态）。
        //   ★ 思维链（`reasoning` / `thinking` …）正是这条路径的主要真实来源。
        //   ★★ **缺失 `kind` 字段与「未知 kind」同等处置**（2026-10-13 实测补漏）：
        //      首版写成 `!block.kind.empty() && !IsKnownKind(...)`，导致**没有 `kind` 的
        //      block_start 被漏掉**——它照样掉进工具卡分支（探针 r4 实测仍是
        //      「等待结果」卡）。空串同样是「不在六个已知值里」，必须一并走中性路径。
        if (!IsKnownKind(block.kind)) {
            block.unknownKind = true;
            ++m_unknownKinds;
            error = block.kind.empty()
                        ? std::string("[I5-kind] block_start without 'kind' rendered neutrally")
                        : "[I5-kind] unknown block kind rendered neutrally: " + block.kind;
        }
        if (block.kind == "tool_call") {
            const json::Value* args = m_last.Find("args");
            if (args && args->IsObject()) block.argsPretty = args->Pretty(2);
        }
        if (block.kind == "tool_result") {
            // I7：tool_result 必须指向已存在的 tool_call
            const auto ref = m_blocks.find(block.forId);
            if (ref == m_blocks.end() || ref->second.kind != "tool_call") {
                ++m_violations;
                error = "[I7] tool_result 'for' does not name a tool_call: " + block.forId;
            }
        }
        const auto placed = m_blocks.emplace(id, std::move(block));
        // ★ 0a：到达顺序与块表**分开记账**——map 的遍历序是 id 字典序，不是到达序。
        //   取 emplace 实际落位的键（而不是 `id` 变量），让两边**逐字同源**：
        //   若某天 emplace 因故没插入（当前 I1 已在上游拦住），这里也不会多记一条。
        if (placed.second) m_order.push_back(placed.first->first);
        return true;
    }

    if (t == "delta") {
        const std::string id = m_last.GetString("id");
        const auto it = m_blocks.find(id);
        if (it == m_blocks.end() || it->second.ended) {
            ++m_violations;
            error = "[I2] delta outside block lifetime: " + id;
            return false;
        }
        it->second.text += m_last.GetString("text");
        ++m_deltas;
        return true;
    }

    if (t == "block_end") {
        const std::string id = m_last.GetString("id");
        const auto it = m_blocks.find(id);
        if (it == m_blocks.end() || it->second.ended) {
            ++m_violations;
            error = "[I1] block_end without matching start: " + id;
            return false;
        }
        it->second.ok = m_last.GetBool("ok", false);
        it->second.summary = m_last.GetString("summary");
        // ★ 协议 v2 §3.5：可选的 token 数（`reasoning` 标题副标用）。
        //   缺字段 ⇒ GetNumber 返回 fallback 0 ⇒ 保持 0 ⇒ 标题**不显示**副标。
        //   ★ **绝不用字符数兜底**——那是编造读数（本项目纪律：不把没测到的写成测到了）。
        it->second.tokens = static_cast<long long>(m_last.GetNumber("tokens", 0.0));
        it->second.ended = true;
        return true;
    }

    // ★★ v5 §3.12：子代理生命周期（控制面，不是转录块）。
    //   start 建条目、end 补结果；两次 start 或 end 无 start 都记 I12 一次，
    //   但**消息照常吸收**（前向兼容 > 当场判死）。
    if (t == "agent") {
        const std::string id = m_last.GetString("id");
        const std::string phase = m_last.GetString("phase");
        if (id.empty() || (phase != "start" && phase != "end")) {
            ++m_agentViolations;
            error = "[I12] malformed agent message (id/phase)";
            return true;   // 已消费，不进 unknownTypes
        }
        AgentInfo& a = m_agents[id];
        a.id = id;
        if (phase == "start") {
            if (a.started) ++m_agentViolations;
            a.started = true;
            a.name = m_last.GetString("name");
            a.parentCall = m_last.GetString("parent_call");
            a.task = m_last.GetString("task");
            a.model = m_last.GetString("model");
            a.depth = static_cast<int>(m_last.GetNumber("depth", 1.0));
        } else {
            if (!a.started) ++m_agentViolations;
            if (a.ended) ++m_agentViolations;
            a.ended = true;
            a.ok = m_last.GetBool("ok", false);
            a.summary = m_last.GetString("summary");
            a.turns = static_cast<int>(m_last.GetNumber("turns", 0.0));
            a.inTokens = static_cast<long long>(m_last.GetNumber("input", 0.0));
            a.outTokens = static_cast<long long>(m_last.GetNumber("output", 0.0));
            a.elapsedMs = static_cast<long long>(m_last.GetNumber("elapsedMs", 0.0));
        }
        return true;
    }

    // I5：未知 t 忽略并记录（不得崩溃）
    static const char* kKnown[] = {"hello", "state", "block_start", "delta", "block_end",
                                   "ask", "usage", "error", "workspace", "bye", "model", "agent"};
    bool known = false;
    for (const char* k : kKnown) known = known || (t == k);
    if (!known) {
        ++m_unknownTypes;
        error = "[I5] unknown message type ignored: " + t;
    }
    return true;
}

const Block* Transcript::FindBlock(const std::string& id) const {
    const auto it = m_blocks.find(id);
    return it == m_blocks.end() ? nullptr : &it->second;
}

void Transcript::FinalizeIncompleteBlocks() {
    for (auto& kv : m_blocks) {
        if (!kv.second.ended) {
            kv.second.ended = true;
        }
    }
}

size_t Transcript::MaxClientIdIndex() const {
    size_t maxIdx = 0;
    for (const auto& kv : m_blocks) {
        const std::string& cid = kv.second.clientId;
        if (cid.size() > 1 && cid[0] == 'p') {
            size_t val = 0;
            bool ok = true;
            for (size_t i = 1; i < cid.size(); ++i) {
                if (cid[i] < '0' || cid[i] > '9') {
                    ok = false;
                    break;
                }
                val = val * 10 + static_cast<size_t>(cid[i] - '0');
            }
            if (ok && val > maxIdx) maxIdx = val;
        }
    }
    return maxIdx;
}

// ★★ §3.3 的已知 kind（2026-10-13 收进单一判据；2026-10-14 v2 加 `reasoning`）。
//   为何要独立成函数：App 的渲染分支与 Transcript 的记账**必须同源**——
//   若两处各写一份列表，加一个 kind 时漏改一处 ⇒ 又会出现「渲染成一个样、
//   记账成另一个样」的静默不一致。
//   ★ `reasoning` = 思维链（协议 v2 §3.3 新增）。**它不属于「未知 kind」**——
//     §5 的 I8 只管**未知**值，`reasoning` 有专门的渲染形态（§7：默认收起）。
//     把两者混为一谈会让「未知 kind 要中性渲染」这条通用规则失去意义。
bool Transcript::IsKnownKind(const std::string& kind) {
    return kind == "user" || kind == "assistant" || kind == "reasoning" ||
           kind == "tool_call" || kind == "tool_result" || kind == "system" ||
           kind == "error";
}


// ── Markdown 管道表格纯函数解析器实现（富内容二期）─────────────────────
namespace {

bool ParseDelimiterCell(const std::string& cell, MarkdownAlign& align) {
    size_t s = cell.find_first_not_of(" \t\r");
    size_t e = cell.find_last_not_of(" \t\r");
    if (s == std::string::npos) return false;
    std::string trimmed = cell.substr(s, e - s + 1);
    if (trimmed.empty()) return false;

    bool leftColon = (trimmed.front() == ':');
    bool rightColon = (trimmed.back() == ':');

    size_t startDash = leftColon ? 1 : 0;
    size_t endDash = rightColon ? trimmed.size() - 1 : trimmed.size();
    if (startDash >= endDash) return false; // 缺少至少一个 '-'

    for (size_t i = startDash; i < endDash; ++i) {
        if (trimmed[i] != '-') return false;
    }

    if (leftColon && rightColon) {
        align = MarkdownAlign::Center;
    } else if (rightColon) {
        align = MarkdownAlign::Right;
    } else {
        align = MarkdownAlign::Left;
    }
    return true;
}

std::vector<std::string> SplitPipeRowCells(const std::string& line) {
    std::vector<std::string> cells;
    std::string current;
    bool escaped = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '|') {
            cells.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    cells.push_back(current);

    size_t firstNonSpace = line.find_first_not_of(" \t\r");
    if (firstNonSpace != std::string::npos && line[firstNonSpace] == '|') {
        if (!cells.empty()) cells.erase(cells.begin());
    }

    size_t lastNonSpace = line.find_last_not_of(" \t\r");
    if (lastNonSpace != std::string::npos && line[lastNonSpace] == '|') {
        bool trailingEscaped = false;
        size_t p = lastNonSpace;
        while (p > 0 && line[p - 1] == '\\') {
            trailingEscaped = !trailingEscaped;
            --p;
        }
        if (!trailingEscaped && !cells.empty()) {
            cells.pop_back();
        }
    }

    for (auto& cell : cells) {
        size_t s = cell.find_first_not_of(" \t\r");
        size_t e = cell.find_last_not_of(" \t\r");
        if (s == std::string::npos) {
            cell.clear();
        } else {
            cell = cell.substr(s, e - s + 1);
        }
    }
    return cells;
}

bool TryParseDelimiterLine(const std::string& line, size_t expectedCols, std::vector<MarkdownAlign>& aligns) {
    if (line.find('|') == std::string::npos && expectedCols > 1) return false;
    auto cells = SplitPipeRowCells(line);
    if (cells.size() != expectedCols || cells.empty()) return false;

    std::vector<MarkdownAlign> parsedAligns;
    parsedAligns.reserve(cells.size());
    for (const auto& c : cells) {
        MarkdownAlign a = MarkdownAlign::Left;
        if (!ParseDelimiterCell(c, a)) return false;
        parsedAligns.push_back(a);
    }
    aligns = std::move(parsedAligns);
    return true;
}

} // namespace

std::vector<MarkdownSegment> ParseMarkdownSegments(const std::string& text) {
    std::vector<std::string> lines;
    {
        std::string current;
        for (char c : text) {
            if (c == '\n') {
                if (!current.empty() && current.back() == '\r') current.pop_back();
                lines.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty() && current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }

    std::vector<MarkdownSegment> segments;
    std::string pendingParagraph;

    auto flushParagraph = [&]() {
        while (!pendingParagraph.empty() && (pendingParagraph.front() == '\n' || pendingParagraph.front() == '\r')) {
            pendingParagraph.erase(pendingParagraph.begin());
        }
        while (!pendingParagraph.empty() && (pendingParagraph.back() == '\n' || pendingParagraph.back() == '\r')) {
            pendingParagraph.pop_back();
        }
        if (!pendingParagraph.empty()) {
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::Paragraph;
            seg.text = pendingParagraph;
            segments.push_back(std::move(seg));
            pendingParagraph.clear();
        }
    };

    size_t i = 0;
    while (i < lines.size()) {
        const std::string& line = lines[i];

        bool isTableStart = false;
        std::vector<std::string> headers;
        std::vector<MarkdownAlign> aligns;

        if (line.find('|') != std::string::npos && (i + 1 < lines.size())) {
            headers = SplitPipeRowCells(line);
            if (!headers.empty() && TryParseDelimiterLine(lines[i + 1], headers.size(), aligns)) {
                isTableStart = true;
            }
        }

        if (isTableStart) {
            flushParagraph();

            MarkdownTable table;
            table.headers = std::move(headers);
            table.alignments = std::move(aligns);

            size_t j = i + 2;
            while (j < lines.size()) {
                const std::string& rowLine = lines[j];
                if (rowLine.empty() || rowLine.find_first_not_of(" \t\r") == std::string::npos ||
                    rowLine.find('|') == std::string::npos) {
                    break;
                }
                auto rowCells = SplitPipeRowCells(rowLine);
                if (rowCells.empty()) break;

                if (rowCells.size() < table.headers.size()) {
                    rowCells.resize(table.headers.size(), "");
                } else if (rowCells.size() > table.headers.size()) {
                    rowCells.resize(table.headers.size());
                }
                table.rows.push_back(std::move(rowCells));
                ++j;
            }

            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::Table;
            seg.table = std::move(table);
            segments.push_back(std::move(seg));

            i = j;
        } else {
            if (!pendingParagraph.empty()) {
                pendingParagraph += "\n";
            }
            pendingParagraph += line;
            ++i;
        }
    }

    flushParagraph();

    bool hasTable = false;
    for (const auto& s : segments) {
        if (s.kind == MarkdownSegmentKind::Table) {
            hasTable = true;
            break;
        }
    }
    if (!hasTable) {
        segments.clear();
        MarkdownSegment s;
        s.kind = MarkdownSegmentKind::Paragraph;
        s.text = text;
        segments.push_back(std::move(s));
    }

    return segments;
}
