#include "Transcript.h"

#include "Iso8601.h"

// ★★ 2026-11-09（富内容 §H）：行内解析要按**框架同一份** UTF-8 步进口径走
//   （`DecodeCodepointAt` 的「非法前导按 1 字节跳」与 `CodepointIndexToByteOffset`
//   是同一份 `SequenceLength`）。自己写一份会让 `startCp` 在畸形输入上与 ECDI 漂移。
#include "ECDI/Core/UTF8.h"

#include <cstdint>
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
        // ★★ 2026-11-06：块的时刻（回合折叠的耗时副标）。
        //   ★ 用信封上的 `ts`——它是 core 的作者时刻，正是"这个回合花了多久"要的口径。
        //     拿 GUI 收到行的墙上时间会让回放/`--speed` 改变读数（那是评估量具，不是事实）。
        //   ★ 解析失败/缺字段 ⇒ 保持 0 ⇒ 该块不参与耗时计算（见字段注释）。
        {
            int64_t ts = 0;
            if (harness::iso8601::ParseIso8601Ms(m_last.GetString("ts"), ts)) {
                block.tsMs = static_cast<long long>(ts);
            }
        }
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
    // ★ v6（2026-11-05）：job 是**已知**控制面类型——它不进 blocks，但绝不能
    //   被计成 unknownTypes（那会在 I5 通知里把正常 v6 流报成"未知类型"）。
    static const char* kKnown[] = {"hello", "state", "block_start", "delta", "block_end",
                                   "ask", "usage", "error", "workspace", "bye", "model", "agent",
                                   "job"};
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

// ── 块级全套的**行分类器**（2026-11-08，富内容 B+ / rich-content-plan §G）──────
//   ★ 为什么拆成「一次分类 + 状态机装配」两步：块级的每个构造都由**行首**触发，
//     而误判一行（比如把正文里的 `---` 当成分隔线、把缩进代码当成列表项）不会报错，
//     只会静默渲染成别的样子。分类器单独可测 ⇒ 误判能在探针里逐行取证，
//     不必靠「看截图觉得不对」。
//   ★ 纪律：**识别不了的一律回落 Plain**。解析器宁可少渲染，不可错切分——
//     因为切分点落在 block_end，错一次就是整段结论换了样子。

// 缩进几个空格算一级嵌套。2 是 CommonMark 对 `-` 列表项内容缩进的常用口径，
// 也贴合本仓转录里模型实际输出的 `<2 空格><- >` 形态（realistic 夹具实测）。
static constexpr int kListIndentSpaces = 2;

enum class LineKind {
    Plain,
    Fence,        // ``` 或 ~~~（开与合都是它，由状态机区分）
    Heading,
    ListItem,
    Quote,
    Hr,
    Blank
};

struct LineTag {
    LineKind kind = LineKind::Plain;
    int headingLevel = 0;
    int headingIndent = 0;      // `#` 前的空格数（≥4 ⇒ 不是标题，是缩进代码）
    bool listOrdered = false;
    int listMarker = 0;
    int listDepth = 0;
    std::string fenceMarker;    // "```" 或 "~~~"（开围栏时记下，闭合必须同字符且长度 ≥ 原长）
    std::string fenceInfo;      // 开围栏的信息串（trim 后）
};

static bool IsAllOf(const std::string& s, char c) {
    if (s.empty()) return false;
    for (char ch : s) {
        if (ch != c) return false;
    }
    return true;
}

static std::string TrimCopy(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

// 分类一行。★ 顺序有意为之：围栏最先（围栏内的一切都由状态机接管，不再二次分类）；
// 然后分隔线（`***` 与 `___` 不会被误认成列表/强调）；再标题；最后列表/引用。
static LineTag ClassifyLine(const std::string& line) {
    LineTag tag;
    if (line.find_first_not_of(" \t\r") == std::string::npos) {
        tag.kind = LineKind::Blank;
        return tag;
    }

    // 前导缩进（空格；tab 按 4 计——与 GFM 一致）
    int indent = 0;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        indent += (line[i] == '\t') ? 4 : 1;
        ++i;
    }
    const std::string body = line.substr(i);

    // ① 围栏：``` / ~~~，长度 ≥ 3
    {
        size_t n = 0;
        while (n < body.size() && (body[n] == '`' || body[n] == '~')) ++n;
        if (n >= 3 && (IsAllOf(body.substr(0, n), '`') || IsAllOf(body.substr(0, n), '~'))) {
            tag.kind = LineKind::Fence;
            tag.fenceMarker = body.substr(0, n);
            tag.fenceInfo = TrimCopy(body.substr(n));
            return tag;
        }
    }

    // ② 分隔线：≥3 个 `-` / `*` / `_`（中间允许空格），且整行只有这一种字符
    //    ★ 必须整行判：`- - -` 是分隔线，而 `- 列表项` 不是。
    {
        std::string squeezed;
        for (char c : body) {
            if (c != ' ' && c != '\t') squeezed += c;
        }
        if (squeezed.size() >= 3 &&
            (IsAllOf(squeezed, '-') || IsAllOf(squeezed, '*') || IsAllOf(squeezed, '_'))) {
            tag.kind = LineKind::Hr;
            return tag;
        }
    }

    // ③ 标题：1–6 个 `#` + 至少一个空格；`#` 前缩进 ≥4 视为缩进代码，不是标题
    {
        size_t h = 0;
        while (h < body.size() && body[h] == '#') ++h;
        if (h >= 1 && h <= 6 && h < body.size() && (body[h] == ' ' || body[h] == '\t') &&
            indent < 4) {
            tag.kind = LineKind::Heading;
            tag.headingLevel = static_cast<int>(h);
            tag.headingIndent = indent;
            return tag;
        }
    }

    // ④ 列表项：`-` / `*` / `+` 或 `1.` / `1)`，**后面必须跟空格**；
    //    `---` 已在 ② 拦掉、`#` 已在 ③ 拦掉，这里不会互相吃。
    {
        if (body.size() >= 2 && (body[0] == '-' || body[0] == '*' || body[0] == '+') &&
            (body[1] == ' ' || body[1] == '\t')) {
            tag.kind = LineKind::ListItem;
            tag.listOrdered = false;
            tag.listDepth = indent / kListIndentSpaces;
            return tag;
        }
        size_t d = 0;
        while (d < body.size() && body[d] >= '0' && body[d] <= '9') ++d;
        if (d >= 1 && d + 1 < body.size() &&
            (body[d] == '.' || body[d] == ')') && (body[d + 1] == ' ' || body[d + 1] == '\t')) {
            tag.kind = LineKind::ListItem;
            tag.listOrdered = true;
            int num = 0;
            for (size_t k = 0; k < d; ++k) {
                num = num * 10 + (body[k] - '0');
                if (num > 999999) break;   // 防溢出；超长序号按截断值记
            }
            tag.listMarker = num;
            tag.listDepth = indent / kListIndentSpaces;
            return tag;
        }
    }

    // ⑤ 引用块：`>` 后跟空格或行尾
    if (!body.empty() && body[0] == '>') {
        if (body.size() == 1 || body[1] == ' ' || body[1] == '\t') {
            tag.kind = LineKind::Quote;
            return tag;
        }
    }

    tag.kind = LineKind::Plain;
    return tag;
}

// 剥掉列表项的行首标记，返回正文（保留标记后的内容，去掉一个空白）
static std::string StripListMarker(const std::string& line, const LineTag& tag) {
    int indent = 0;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        indent += (line[i] == '\t') ? 4 : 1;
        ++i;
    }
    const std::string body = line.substr(i);
    size_t skip = 0;
    if (tag.listOrdered) {
        while (skip < body.size() && body[skip] >= '0' && body[skip] <= '9') ++skip;
        if (skip < body.size()) ++skip;   // '.' 或 ')'
    } else {
        skip = 1;                          // '-', '*', '+'
    }
    if (skip < body.size() && (body[skip] == ' ' || body[skip] == '\t')) ++skip;
    return body.substr(skip);
}

// 剥掉引用块的 `>` 前缀（以及紧随的一个空白）
static std::string StripQuoteMarker(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && line[i] == '>') {
        ++i;
        if (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    }
    return line.substr(i);
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

        // ★★ 块级全套（2026-11-08）：先做表格探测（保持二期既有优先级不动——
        //   表格在 GFM 里也是块，且它与标题/列表不冲突，先判不影响任何既有夹具）。
        bool isTableStart = false;
        std::vector<std::string> headers;
        std::vector<MarkdownAlign> aligns;

        if (line.find('|') != std::string::npos && (i + 1 < lines.size())) {
            headers = SplitPipeRowCells(line);
            if (!headers.empty() && TryParseDelimiterLine(lines[i + 1], headers.size(), aligns)) {
                isTableStart = true;
            }
        }

        // 围栏代码块必须先判：围栏**内部**的 `|` 不能当表格起始。
        const LineTag tag = ClassifyLine(line);
        if (!isTableStart && tag.kind == LineKind::Fence) {
            flushParagraph();
            ++i;
            // 围栏内原文逐行收集（保留空行与缩进）；遇到同字符、长度 ≥ 原长的闭合围栏才收束。
            std::string code;
            bool closed = false;
            while (i < lines.size()) {
                const LineTag inner = ClassifyLine(lines[i]);
                if (inner.kind == LineKind::Fence &&
                    inner.fenceMarker[0] == tag.fenceMarker[0] &&
                    inner.fenceMarker.size() >= tag.fenceMarker.size() &&
                    inner.fenceInfo.empty()) {
                    closed = true;
                    ++i;
                    break;
                }
                if (!code.empty()) code += "\n";
                code += lines[i];
                ++i;
            }
            // ★ 未闭合围栏（模型流式输出中途 / 手写夹具截断）：整段按原文进 Paragraph，
            //   不静默丢弃、也不假装是代码块。判据在 --md-probe 的 "fence-unclosed" 相。
            if (!closed) {
                MarkdownSegment seg;
                seg.kind = MarkdownSegmentKind::Paragraph;
                seg.text = tag.fenceMarker + tag.fenceInfo + (code.empty() ? "" : "\n" + code);
                segments.push_back(std::move(seg));
                continue;
            }
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::CodeBlock;
            seg.text = code;
            seg.lang = tag.fenceInfo;
            segments.push_back(std::move(seg));
            continue;
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
            continue;
        }

        // 标题 / 分隔线：单行独立成段
        if (tag.kind == LineKind::Heading) {
            flushParagraph();
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::Heading;
            seg.level = tag.headingLevel;
            seg.text = TrimCopy(line.substr(static_cast<size_t>(tag.headingIndent) +
                                            static_cast<size_t>(tag.headingLevel)));
            segments.push_back(std::move(seg));
            ++i;
            continue;
        }
        if (tag.kind == LineKind::Hr) {
            flushParagraph();
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::Hr;
            segments.push_back(std::move(seg));
            ++i;
            continue;
        }

        // 列表项：**逐项**成段（不合并成一个 List 段）——
        //   ★ 理由：实现上「一个控件一套字体」，而列表项将来若要点选/复制也要逐项命中；
        //     合并成一个多行文本会把悬挂缩进（marker 与正文分栏）做成第二套排版。
        //     逐项成段 ⇒ 每项一个控件，marker 与正文各自摆位，relayout 天然对齐。
        if (tag.kind == LineKind::ListItem) {
            flushParagraph();
            // 续行（缩进更深的缩进正文，非列表项）并入本项
            std::string itemText = StripListMarker(line, tag);
            ++i;
            while (i < lines.size()) {
                const LineTag cont = ClassifyLine(lines[i]);
                if (cont.kind == LineKind::Blank) break;
                if (cont.kind != LineKind::Plain) break;
                // 只有**缩进**的普通行才算续行；顶格普通行结束本项
                if (lines[i].empty() || (lines[i][0] != ' ' && lines[i][0] != '\t')) break;
                itemText += "\n";
                itemText += TrimCopy(lines[i]);
                ++i;
            }
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::ListItem;
            seg.text = itemText;
            seg.ordered = tag.listOrdered;
            seg.marker = tag.listMarker;
            seg.depth = tag.listDepth;
            segments.push_back(std::move(seg));
            continue;
        }

        // 引用块：连续 `>` 行合并成一段（中间的空 `>` 也保留，作为段内换行）
        if (tag.kind == LineKind::Quote) {
            flushParagraph();
            std::string quoted = StripQuoteMarker(line);
            ++i;
            while (i < lines.size()) {
                const LineTag cont = ClassifyLine(lines[i]);
                if (cont.kind != LineKind::Quote) break;
                quoted += "\n";
                quoted += StripQuoteMarker(lines[i]);
                ++i;
            }
            MarkdownSegment seg;
            seg.kind = MarkdownSegmentKind::Quote;
            seg.text = quoted;
            segments.push_back(std::move(seg));
            continue;
        }

        // 普通行 / 空行 → 累积进段落
        if (!pendingParagraph.empty()) {
            pendingParagraph += "\n";
        }
        pendingParagraph += line;
        ++i;
    }

    flushParagraph();

    // ★★ 基线不变量（2026-11-08 重写时保留并**扩大**）：
    //   若整段文本没有任何**块级构造**产出的段（即所有段都是 Paragraph），
    //   则回退成「单个 Paragraph，内容逐字等于原 text」。
    //   ★ 为什么必须保留这条：7 份既有夹具的渲染路径依赖「无构造 ⇒ 原文直出」，
    //     一旦解析器把普通文本切成多段，行高/居中/复制的既有基线全部漂移。
    //     原判据只看 Table，扩到块级后改为「除 Paragraph 外一个都不许有」。
    bool hasBlockConstruct = false;
    for (const auto& s : segments) {
        if (s.kind != MarkdownSegmentKind::Paragraph) {
            hasBlockConstruct = true;
            break;
        }
    }
    if (!hasBlockConstruct) {
        segments.clear();
        MarkdownSegment s;
        s.kind = MarkdownSegmentKind::Paragraph;
        s.text = text;
        segments.push_back(std::move(s));
    }

    return segments;
}

// ★★ 2026-11-08（富内容 B+）：复合渲染判据的唯一实现（声明见 Transcript.h）。
bool HasMarkdownBlocks(const std::vector<MarkdownSegment>& segs) {
    for (const auto& s : segs) {
        if (s.kind != MarkdownSegmentKind::Paragraph) return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// 段内内联样式解析器（富内容 §H，2026-11-09）
// ═══════════════════════════════════════════════════════════════════════
// ★★ 为什么是「扫描 → 配对 → 输出」三遍，而不是「区间查找 + 字符串替换」：
//   1. **码点索引是硬约束**。`TextSpan.startCp / cpCount` 是 UTF-8 解码后的字符位，
//      不是字节偏移。替换式实现（删掉 `**` 再找位置）几乎必然按字节算偏移——
//      纯 ASCII 段落测不出来，中文一进来就漂。逐码点步进 ⇒ cp 索引是扫描的自然
//      产物，不需要事后换算（也就不存在「换算口径与 ECDI 不一致」这条风险）。
//      ★ 步进量直接调 `ECDI::DecodeCodepointAt`——**不自己写 SequenceLength**，
//        「非法前导按 1 字节跳」这条规则在框架里已经有一份真相。
//   2. **未闭合必须回落原文**。流式中途 `**粗` 每天都在发生：它必须显示成带星号的
//      原文，而不是把星号吃掉、把后半段全加粗。区间查找式实现得为「找不到闭合」
//      写回滚；先配对再把未匹配标记降级为字面，则天然满足，不需要回滚。
//   3. **嵌套要扁平化**。ECDI 的 span 契约是「有序、无重叠」，而
//      `**粗体里的 `code`**` 语法上是嵌套的。逐码点取「当前有效标记栈」的样式位、
//      再把**样式相同的连续码点**合并成一个 run ⇒ 嵌套自然摊平成
//      `[粗][粗+code][粗]` 三个不重叠区间，没有专门的扁平化代码。
//
// ★ 规则全集（每条都对应 `--md-probe` 的一个 inline-* 相；改规则必须同时改相）：
//   R1 标记集：`**` / `__`（粗体）、`` ` ``（行内 code）、`\`（转义 `\*` `\_` `` \` `` `\\`）
//   R2 斜体**不解析**：单 `*` / 单 `_` 一律字面（用户 2026-11-09 裁定「斜体先不纳入」）。
//      故 `*x*` 显示为 `*x*`，不去标记——「不解析」与「假装支持但无效果」是两件事。
//   R3 flanking（只对粗体）：开启标记右侧必须非空白；关闭标记左侧必须非空白。
//      ⇒ `2 ** 3` 里的 `**` 不被吞（右侧是空格 ⇒ 不是开启）。
//   R4 intraword（只对 `__`）：开启时左侧不得是 ASCII 字母数字；关闭时右侧同。
//      ⇒ `snake_case_name` 不被吃掉。`**` 无此限制（GFM 亦如此）。
//   R5 code span 内**不解析**任何其它标记（`` `**x**` `` 里的 `**` 是字面）。
//   R6 未闭合 ⇒ 该标记按字面输出（`markText` 原样写回），不产生任何样式。
//   R7 配对必须**同种且栈顶**：`**a `b** c` ` 里那个 `**` 不是关闭（栈顶是 code）⇒ 字面。
//   R8 空内容不成样式：`` ` ` `` / `****` ⇒ 标记两侧全是字面。
//   R9 行内 code 的族与色由调用方给（`InlineCodeStyle`）——解析器不知道主题。
namespace {

enum class InlineMark { Bold, Code };

// 扫描期 token：Text 携带原文片段与码点数；Open/Close 携带原文标记字节
struct InlineToken {
    enum class Kind { Text, Open, Close } kind = Kind::Text;
    InlineMark  mark = InlineMark::Bold;   // Open/Close 有效
    std::string bytes;                     // Text：原文片段（可含多字节序列）
    std::string markText;                  // Open/Close：原文标记字节（`**` / `__` / `` ` ``）
    size_t      cpCount = 0;               // Text：该片段码点数
    bool        literal = false;           // 遍 1 结论：这个标记不成立 ⇒ 按字面输出
};

bool IsAsciiAlnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsBlankByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool IsEscapableByte(unsigned char c) {
    return c == '*' || c == '_' || c == '`' || c == '\\';
}

} // namespace

InlineRuns ParseInlineRuns(const std::string& text, const InlineCodeStyle& codeStyle) {
    const size_t n = text.size();

    // ── 遍 0：扫描成 token 流（连续字面字符合并成一个 Text token）────────
    std::vector<InlineToken> tokens;
    auto appendText = [&tokens](const char* p, size_t len, size_t cps) {
        if (len == 0) return;
        if (!tokens.empty() && tokens.back().kind == InlineToken::Kind::Text) {
            tokens.back().bytes.append(p, len);
            tokens.back().cpCount += cps;
            return;
        }
        InlineToken t;
        t.kind = InlineToken::Kind::Text;
        t.bytes.assign(p, len);
        t.cpCount = cps;
        tokens.push_back(std::move(t));
    };

    bool inCode = false;   // R5：code span 内的标记一律字面
    int  boldDepth = 0;    // 扫描期粗体深度（决定 `**` 是开还是关；遍 1 仍会严格复核）
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        // R1：反斜杠转义 —— 反斜杠自身不保留，被转义的字符按字面输出
        if (c == '\\' && i + 1 < n && IsEscapableByte(static_cast<unsigned char>(text[i + 1]))) {
            appendText(text.c_str() + i + 1, 1, 1);   // 可转义集合全是 ASCII ⇒ 1 字节 = 1 码点
            i += 2;
            continue;
        }

        // 行内 code 标记（反引号不套 flanking：它在正文里几乎只作标记，误判风险远小于 `*`）
        if (c == '`') {
            InlineToken t;
            t.mark = InlineMark::Code;
            t.markText.assign(1, '`');
            t.kind = inCode ? InlineToken::Kind::Close : InlineToken::Kind::Open;
            inCode = !inCode;
            tokens.push_back(std::move(t));
            ++i;
            continue;
        }

        // 粗体标记 `**` / `__`（单 `*` / 单 `_` 是斜体 ⇒ R2 不解析，落下面的普通字节分支）
        const bool isBoldMarker = ((c == '*' || c == '_') && i + 1 < n &&
                                   static_cast<unsigned char>(text[i + 1]) == c);
        if (isBoldMarker && !inCode) {
            const size_t after = i + 2;
            const bool hasRight = after < n;
            const bool rightBlank = hasRight && IsBlankByte(static_cast<unsigned char>(text[after]));
            const bool hasLeft = i > 0;
            const bool leftBlank = hasLeft && IsBlankByte(static_cast<unsigned char>(text[i - 1]));

            bool canOpen = hasRight && !rightBlank;    // R3
            bool canClose = hasLeft && !leftBlank;     // R3
            if (c == '_') {                            // R4 intraword
                if (hasLeft && IsAsciiAlnum(static_cast<unsigned char>(text[i - 1]))) canOpen = false;
                if (hasRight && IsAsciiAlnum(static_cast<unsigned char>(text[after]))) canClose = false;
            }

            // R7：先判关闭（配平优先），再判开启——`**a**b**` 中间那个必须关掉前一段
            if (canClose && boldDepth > 0) {
                InlineToken t;
                t.mark = InlineMark::Bold;
                t.kind = InlineToken::Kind::Close;
                t.markText = text.substr(i, 2);
                --boldDepth;
                tokens.push_back(std::move(t));
                i += 2;
                continue;
            }
            if (canOpen) {
                InlineToken t;
                t.mark = InlineMark::Bold;
                t.kind = InlineToken::Kind::Open;
                t.markText = text.substr(i, 2);
                ++boldDepth;
                tokens.push_back(std::move(t));
                i += 2;
                continue;
            }
            appendText(text.c_str() + i, 2, 2);        // 两个 ASCII 字符 = 2 码点
            i += 2;
            continue;
        }

        // 普通码点：★ 步进量走 ECDI 的解码口径（非法前导按 1 字节跳，与框架单源）
        // ★ 这里只要步进量，解码值本身不用——`DecodeCodepointAt` 的 out 参数无条件被写，
        //   仍传一个局部变量是为了不自己复制一份前导字节判定（那就会变成第二份口径）。
        char32_t decoded = 0;
        size_t len = ECDI::DecodeCodepointAt(text.c_str() + i, n - i, decoded);
        if (len == 0 || len > n - i) len = n - i;      // 末段截断：只复制实有字节（cp 仍计 1）
        appendText(text.c_str() + i, len, 1);
        i += len;
    }

    // ── 遍 1：配对校验（R6/R7/R8 在这里落地）───────────────────────────
    std::vector<size_t> openStack;
    for (size_t k = 0; k < tokens.size(); ++k) {
        InlineToken& t = tokens[k];
        if (t.kind == InlineToken::Kind::Open) {
            openStack.push_back(k);
            continue;
        }
        if (t.kind != InlineToken::Kind::Close) continue;

        // R7：只与**栈顶同种**配对；跨栈（`**a `b** c` `）⇒ 本标记是字面
        if (openStack.empty() || tokens[openStack.back()].mark != t.mark) {
            t.literal = true;
            continue;
        }
        const size_t openIdx = openStack.back();
        openStack.pop_back();
        // R8：两标记相邻 ⇒ 零内容 ⇒ 两边都当字面（`****` / `` `` ``）
        if (k == openIdx + 1) {
            tokens[openIdx].literal = true;
            t.literal = true;
            continue;
        }
    }
    for (size_t k : openStack) tokens[k].literal = true;   // R6：未闭合的开启标记 ⇒ 字面

    // ── 遍 2：输出干净文本 + 生成 spans（同样式连续码点合并成一个 run）────
    InlineRuns runs;
    runs.text.reserve(text.size());

    std::vector<InlineMark> live;
    size_t cp = 0;
    size_t runStart = 0;
    ECDI::SpanStyle runStyle;
    bool runValid = false;

    auto styleOfLive = [&]() {
        ECDI::SpanStyle st;
        bool bold = false, code = false;
        for (InlineMark m : live) {
            if (m == InlineMark::Bold) bold = true;
            else code = true;
        }
        if (bold) st.weight = static_cast<std::uint16_t>(700);
        if (code) {
            st.family = codeStyle.family;
            st.foreground = codeStyle.color;
        }
        return st;
    };
    // ★★ 无覆盖的 run **不产出 span**。这里少一个判断就会毁掉整条基线：
    //   纯文本段落会得到一条「五个维度全 nullopt」的 span ⇒ `HasInlineStyles` 恒真
    //   ⇒ 渲染侧永远走 `SetStyledText`，「无构造 ⇒ 原文直出」当场失效
    //   （探针只显示成「spans.size() 比期望多」，症状离病因很远）。
    auto isPlainStyle = [](const ECDI::SpanStyle& st) {
        return !st.family.has_value() && !st.weight.has_value() && !st.foreground.has_value() &&
               !st.italic.has_value() && !st.size.has_value();
    };
    auto flushRun = [&](size_t endCp) {
        if (runValid && endCp > runStart && !isPlainStyle(runStyle)) {
            runs.spans.push_back(ECDI::TextSpan{runStart, endCp - runStart, runStyle});
        }
        runValid = false;
    };
    auto beginChar = [&]() {                       // 写字符前调用：样式变了就先收束上一 run
        ECDI::SpanStyle st = styleOfLive();
        if (runValid && !(runStyle == st)) flushRun(cp);
        if (!runValid) {
            runStart = cp;
            runStyle = st;
            runValid = true;
        }
    };

    for (const InlineToken& t : tokens) {
        if (t.kind == InlineToken::Kind::Text) {
            if (t.cpCount == 0) continue;
            beginChar();
            runs.text.append(t.bytes);
            cp += t.cpCount;
            continue;
        }
        if (t.literal) {                           // R6：标记不成立 ⇒ 原样写回
            beginChar();
            runs.text.append(t.markText);
            cp += t.markText.size();               // 标记字节全是 ASCII ⇒ 字节数 = 码点数
            continue;
        }
        if (t.kind == InlineToken::Kind::Open) {
            flushRun(cp);                          // 样式即将变化，收束
            live.push_back(t.mark);
        } else {
            flushRun(cp);
            if (!live.empty()) live.pop_back();
        }
    }
    flushRun(cp);

    return runs;
}

// ★★ 与 `HasMarkdownBlocks` 同一条纪律：判据唯一实现，渲染侧只调这里。
bool HasInlineStyles(const InlineRuns& runs) {
    return !runs.spans.empty();
}

