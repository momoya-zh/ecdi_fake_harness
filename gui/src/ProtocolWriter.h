#pragma once
// gui→core 消息构造（协议 §4；P1.5 反向通道）。
// 持 gui 侧 seq（从 0 起严格递增——与 core 侧同规则），经 MessageSource::Send 发出。
// ★ 不自行判断状态合法性（协议 §6 把按钮使能交给 state 驱动）——调用方负责。

#include "Json.h"

#include <string>

class MessageSource;

class ProtocolWriter {
public:
    explicit ProtocolWriter(MessageSource& sink) : m_sink(sink) {}

    bool Hello(const std::string& guiVersion);                     // seq 0（收到 core hello 后）
    bool Prompt(const std::string& text, const std::string& clientId); // 仅 idle
    bool Workspace(const std::string& requestId, const std::string& path); // 仅 idle
    bool Cancel();                                                 // running
    bool Answer(const std::string& askId, const std::string& choice);  // waiting
    bool Bye();                                                    // 关窗收尾

    long long LastSeqSent() const { return m_seq; }
    size_t SentCount() const { return m_sent; }

private:
    bool SendWith(json::Value payload);   // 注入 seq/ts → Send → 递增

    MessageSource& m_sink;
    long long m_seq = 0;
    size_t m_sent = 0;
};