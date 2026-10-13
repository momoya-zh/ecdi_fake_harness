#pragma once
#include "MessageSource.h"

// JSONL 夹具回放源（规格 §3 / 协议 §8）。
// instant：Next 一次吐完全部行；timed：NextDue 只在行 ts 到期时吐出。
// 协议 §1.2：读到不完整的最后一行（EOF 截断）必须报错，不得静默丢弃 → TruncatedError()。

#include <filesystem>
#include <string>
#include <vector>

class ReplaySource : public MessageSource {
public:
    // 读入夹具并预解析每行 ts 偏移；失败返回 false。
    bool Load(const std::filesystem::path& path, std::string& error);

    // MessageSource：无视节奏取下一行（instant 语义）
    bool Next(std::string& line) override;

    // timed 专用：仅当该行 ts 相对夹具首行的偏移 <= elapsedMs 时消费并返回 true
    bool NextDue(double elapsedMs, std::string& line);

    bool Empty() const { return m_index >= m_lines.size(); }
    size_t Total() const { return m_lines.size(); }
    size_t Consumed() const { return m_index; }

    // 最后一行不完整（无结尾 LF）的原因描述；空 = 无截断
    const std::string& TruncatedError() const { return m_truncatedError; }

private:
    std::vector<std::string> m_lines;
    std::vector<double> m_offsetsMs;   // 每行 ts 相对首行的毫秒偏移
    size_t m_index = 0;
    std::string m_truncatedError;
};
