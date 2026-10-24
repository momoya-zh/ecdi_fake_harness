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

    // I5：未知 t 忽略并记录（不得崩溃）
    static const char* kKnown[] = {"hello", "state", "block_start", "delta", "block_end",
                                   "ask", "usage", "error", "workspace", "bye", "model"};
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
