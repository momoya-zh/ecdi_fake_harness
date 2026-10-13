#pragma once
// 极简 JSON（规格 §3：解析归应用，不进 ECDI；零第三方）。
// 覆盖协议所需子集：object / array / string / number / bool / null。

#include <map>
#include <string>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;                       // Type::String
    std::vector<Value> array;               // Type::Array
    std::map<std::string, Value> object;    // Type::Object

    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // 对象成员查找；非对象或键缺失返回 nullptr
    const Value* Find(const char* key) const;

    // 便捷读取（键缺失 / 类型不符 → fallback）
    std::string GetString(const char* key, const std::string& fallback = std::string()) const;
    double GetNumber(const char* key, double fallback = 0.0) const;
    bool GetBool(const char* key, bool fallback = false) const;

    // 美化输出（tool_call 的 args 展开体用；indent = 每层缩进空格数）
    std::string Pretty(int indent = 2) const;

    // 紧凑序列化：一行一个 JSON 对象（协议 §1.2——流里不允许 pretty-print）。
    // 反向通道（gui→core）用；转义与 Pretty 共用同一份 AppendEscaped。
    std::string Dump() const;
};

// 解析整段 JSON 文本；失败返回 false，error 为 ASCII 诊断串
bool Parse(const std::string& input, Value& out, std::string& error);

}   // namespace json
