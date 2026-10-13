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
        block.kind = m_last.GetString("kind");
        block.name = m_last.GetString("name");
        block.forId = m_last.GetString("for");
        block.text = m_last.GetString("text");
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
        m_blocks.emplace(id, std::move(block));
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
        it->second.ended = true;
        return true;
    }

    // I5：未知 t 忽略并记录（不得崩溃）
    static const char* kKnown[] = {"hello", "state", "block_start", "delta", "block_end",
                                   "ask", "usage", "error", "bye"};
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
