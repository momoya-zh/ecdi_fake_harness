#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace json {
namespace {

class Parser {
public:
    Parser(const std::string& text, std::string& error)
        : m_text(text), m_error(error) {}

    bool Run(Value& out) {
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        if (m_pos != m_text.size()) {
            return Fail("trailing characters after JSON value");
        }
        return true;
    }

private:
    bool Fail(const char* message) {
        if (m_error.empty()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s (at byte %zu)", message, m_pos);
            m_error = buf;
        }
        return false;
    }

    void SkipWs() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_pos;
            else break;
        }
    }

    bool Peek(char& c) {
        if (m_pos >= m_text.size()) return false;
        c = m_text[m_pos];
        return true;
    }

    bool ParseValue(Value& out) {
        char c = 0;
        if (!Peek(c)) return Fail("unexpected end of input");
        switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': out.type = Value::Type::String; return ParseString(out.text);
            case 't': return ParseLiteral(out, "true", Value::Type::Bool, true);
            case 'f': return ParseLiteral(out, "false", Value::Type::Bool, false);
            case 'n': return ParseNull(out);
            default:  return ParseNumber(out);
        }
    }

    bool ParseLiteral(Value& out, const char* literal, Value::Type type, bool value) {
        const size_t len = std::char_traits<char>::length(literal);
        if (m_text.compare(m_pos, len, literal) != 0) return Fail("invalid literal");
        m_pos += len;
        out.type = type;
        out.boolean = value;
        return true;
    }

    bool ParseNull(Value& out) {
        if (m_text.compare(m_pos, 4, "null") != 0) return Fail("invalid literal");
        m_pos += 4;
        out.type = Value::Type::Null;
        return true;
    }

    bool ParseNumber(Value& out) {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
        while (m_pos < m_text.size() &&
               ((m_text[m_pos] >= '0' && m_text[m_pos] <= '9') || m_text[m_pos] == '.' ||
                m_text[m_pos] == 'e' || m_text[m_pos] == 'E' || m_text[m_pos] == '+' ||
                m_text[m_pos] == '-')) {
            ++m_pos;
        }
        if (m_pos == start) return Fail("expected a value");
        try {
            out.number = std::stod(m_text.substr(start, m_pos - start));
        } catch (...) {
            return Fail("invalid number");
        }
        out.type = Value::Type::Number;
        return true;
    }

    bool ParseString(std::string& out) {
        if (m_text[m_pos] != '"') return Fail("expected string");
        ++m_pos;
        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_pos >= m_text.size()) return Fail("unterminated escape");
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ParseHex4(cp)) return false;
                    // 代理对 → UTF-8（协议文本可能含 BMP 外字符）
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 1 < m_text.size() &&
                        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                        m_pos += 2;
                        uint32_t low = 0;
                        if (!ParseHex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return Fail("unknown escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseHex4(uint32_t& out) {
        if (m_pos + 4 > m_text.size()) return Fail("truncated \\u escape");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("invalid \\u escape");
        }
        out = value;
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseObject(Value& out) {
        out.type = Value::Type::Object;
        ++m_pos;   // '{'
        SkipWs();
        char c = 0;
        if (!Peek(c)) return Fail("unterminated object");
        if (c == '}') { ++m_pos; return true; }
        while (true) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            char sep = 0;
            if (!Peek(sep) || sep != ':') return Fail("expected ':'");
            ++m_pos;
            SkipWs();
            Value item;
            if (!ParseValue(item)) return false;
            out.object.emplace(std::move(key), std::move(item));
            SkipWs();
            if (!Peek(sep)) return Fail("unterminated object");
            if (sep == ',') { ++m_pos; continue; }
            if (sep == '}') { ++m_pos; return true; }
            return Fail("expected ',' or '}'");
        }
    }

    bool ParseArray(Value& out) {
        out.type = Value::Type::Array;
        ++m_pos;   // '['
        SkipWs();
        char c = 0;
        if (!Peek(c)) return Fail("unterminated array");
        if (c == ']') { ++m_pos; return true; }
        while (true) {
            SkipWs();
            Value item;
            if (!ParseValue(item)) return false;
            out.array.push_back(std::move(item));
            SkipWs();
            char sep = 0;
            if (!Peek(sep)) return Fail("unterminated array");
            if (sep == ',') { ++m_pos; continue; }
            if (sep == ']') { ++m_pos; return true; }
            return Fail("expected ',' or ']'");
        }
    }

    const std::string& m_text;
    std::string& m_error;
    size_t m_pos = 0;
};

void AppendEscaped(std::string& out, const std::string& text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;   // UTF-8 原样透出
        }
    }
    out.push_back('"');
}

void AppendPretty(const Value& value, int indent, int depth, std::string& out);

// 紧凑序列化（协议 §1.2：流里不允许 pretty-print；一行一个 JSON 对象）
void AppendCompact(const Value& value, std::string& out) {
    switch (value.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += value.boolean ? "true" : "false"; break;
        case Value::Type::Number: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", value.number);
            out += buf;
            break;
        }
        case Value::Type::String: AppendEscaped(out, value.text); break;
        case Value::Type::Array: {
            out += '[';
            for (size_t i = 0; i < value.array.size(); ++i) {
                if (i) out += ',';
                AppendCompact(value.array[i], out);
            }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            size_t i = 0;
            for (const auto& [key, item] : value.object) {
                if (i++) out += ',';
                AppendEscaped(out, key);
                out += ':';
                AppendCompact(item, out);
            }
            out += '}';
            break;
        }
    }
}

}   // namespace

const Value* Value::Find(const char* key) const {
    if (type != Type::Object) return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string Value::GetString(const char* key, const std::string& fallback) const {
    const Value* v = Find(key);
    return (v && v->type == Type::String) ? v->text : fallback;
}

double Value::GetNumber(const char* key, double fallback) const {
    const Value* v = Find(key);
    return (v && v->type == Type::Number) ? v->number : fallback;
}

bool Value::GetBool(const char* key, bool fallback) const {
    const Value* v = Find(key);
    return (v && v->type == Type::Bool) ? v->boolean : fallback;
}

std::string Value::Pretty(int indent) const {
    std::string out;
    AppendPretty(*this, indent, 0, out);
    return out;
}

bool Parse(const std::string& input, Value& out, std::string& error) {
    error.clear();
    out = Value{};
    Parser parser(input, error);
    return parser.Run(out);
}

namespace {

void AppendPretty(const Value& value, int indent, int depth, std::string& out) {
    const std::string pad(static_cast<size_t>(indent * (depth + 1)), ' ');
    const std::string padEnd(static_cast<size_t>(indent * depth), ' ');
    switch (value.type) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += value.boolean ? "true" : "false"; break;
        case Value::Type::Number: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", value.number);
            out += buf;
            break;
        }
        case Value::Type::String: AppendEscaped(out, value.text); break;
        case Value::Type::Array: {
            if (value.array.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < value.array.size(); ++i) {
                out += pad;
                AppendPretty(value.array[i], indent, depth + 1, out);
                if (i + 1 < value.array.size()) out += ',';
                out += '\n';
            }
            out += padEnd + "]";
            break;
        }
        case Value::Type::Object: {
            if (value.object.empty()) { out += "{}"; break; }
            out += "{\n";
            size_t i = 0;
            for (const auto& [key, item] : value.object) {
                out += pad;
                AppendEscaped(out, key);
                out += ": ";
                AppendPretty(item, indent, depth + 1, out);
                if (++i < value.object.size()) out += ',';
                out += '\n';
            }
            out += padEnd + "}";
            break;
        }
    }
}

}   // namespace

std::string Value::Dump() const {
    std::string out;
    AppendCompact(*this, out);
    return out;
}

}   // namespace json
