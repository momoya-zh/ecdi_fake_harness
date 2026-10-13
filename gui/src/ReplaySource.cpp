#include "ReplaySource.h"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace {

// "YYYY-MM-DDTHH:MM:SS.mmmZ" → 自任意纪元的毫秒数（只用于求差，纪元选择任意）。
// days_from_civil（Howard Hinnant 算法）——不依赖本地时区 API。
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool ParseIso8601Ms(const std::string& text, int64_t& outMs) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%3dZ",
                    &y, &mo, &d, &h, &mi, &s, &ms) != 7) {
        return false;
    }
    const int64_t days = DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    outMs = ((days * 86400 + h * 3600 + mi * 60 + s) * 1000) + ms;
    return true;
}

// 从原始行里提取 "ts":"..." 的值（不做完整 JSON 解析——ts 只需要相对偏移）
bool ExtractTs(const std::string& line, int64_t& outMs) {
    const std::string key = "\"ts\":\"";
    const size_t k = line.find(key);
    if (k == std::string::npos) return false;
    const size_t start = k + key.size();
    const size_t end = line.find('"', start);
    if (end == std::string::npos) return false;
    return ParseIso8601Ms(line.substr(start, end - start), outMs);
}

}   // namespace

bool ReplaySource::Load(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open fixture: " + path.generic_string();
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());

    // 协议 §1.1：jsonl 无 BOM / LF。GUI 侧宽容处理编码偏离但保留痕迹：
    // BOM 跳过（validate.py 会把它判为 ENC 违规，这里不重复拦截）；
    // 行尾 \r 去掉（否则 JSON 行解析必失败，错误信息反而失真）。
    if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);

    int64_t firstTs = 0;
    bool firstSet = false;
    std::string line;
    std::istringstream stream(raw);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        int64_t ts = 0;
        if (ExtractTs(line, ts)) {
            if (!firstSet) {
                firstTs = ts;
                firstSet = true;
            }
            m_offsetsMs.push_back(static_cast<double>(ts - firstTs));
        } else {
            // 无 ts（协议要求 core→gui 必填；缺失视为立即到期）
            m_offsetsMs.push_back(0.0);
        }
        m_lines.push_back(std::move(line));
    }
    // 协议 §1.2：EOF 截断的最后一行必须显式报错
    if (!raw.empty() && raw.back() != '\n' && !m_lines.empty()) {
        m_truncatedError = "final line truncated (missing LF) - dropped per protocol 1.2";
    }
    return true;
}

bool ReplaySource::Next(std::string& line) {
    if (Empty()) return false;
    line = m_lines[m_index++];
    return true;
}

bool ReplaySource::NextDue(double elapsedMs, std::string& line) {
    if (Empty()) return false;
    if (m_offsetsMs[m_index] > elapsedMs) return false;
    return Next(line);
}
