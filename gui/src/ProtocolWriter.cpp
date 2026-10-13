#include "ProtocolWriter.h"

#include "MessageSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdio>
#include <utility>

namespace {

// ts 一律 UTC ISO-8601 毫秒（协议 §2；gui→core 可省，但填上更利于诊断）
std::string NowIso() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  st.wMilliseconds);
    return buf;
}

}   // namespace

// 发一条消息：注入 seq/ts → Send。Send 失败不入账（seq 只在真正发出后递增）。
bool ProtocolWriter::SendWith(json::Value payload) {
    json::Value seq;
    seq.type = json::Value::Type::Number;
    seq.number = static_cast<double>(m_seq);
    json::Value ts;
    ts.type = json::Value::Type::String;
    ts.text = NowIso();
    payload.object["seq"] = seq;
    payload.object["ts"] = ts;

    if (!m_sink.Send(payload.Dump())) {
        return false;
    }
    ++m_seq;
    ++m_sent;
    return true;
}

bool ProtocolWriter::Hello(const std::string& guiVersion) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t;   t.type = json::Value::Type::String;  t.text = "hello";
    json::Value proto; proto.type = json::Value::Type::Number; proto.number = 1;
    json::Value gui; gui.type = json::Value::Type::String; gui.text = guiVersion;
    msg.object["t"] = t;
    msg.object["protocol"] = proto;
    msg.object["gui"] = gui;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Prompt(const std::string& text) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t;   t.type = json::Value::Type::String;  t.text = "prompt";
    json::Value txt; txt.type = json::Value::Type::String; txt.text = text;
    msg.object["t"] = t;
    msg.object["text"] = txt;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Cancel() {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t; t.type = json::Value::Type::String; t.text = "cancel";
    msg.object["t"] = t;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Answer(const std::string& askId, const std::string& choice) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t;   t.type = json::Value::Type::String;  t.text = "answer";
    json::Value id;  id.type = json::Value::Type::String; id.text = askId;
    json::Value ch;  ch.type = json::Value::Type::String; ch.text = choice;
    msg.object["t"] = t;
    msg.object["id"] = id;
    msg.object["choice"] = ch;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Bye() {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t; t.type = json::Value::Type::String; t.text = "bye";
    msg.object["t"] = t;
    return SendWith(std::move(msg));
}