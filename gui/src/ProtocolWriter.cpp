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
    json::Value proto; proto.type = json::Value::Type::Number; proto.number = 3;
    json::Value gui; gui.type = json::Value::Type::String; gui.text = guiVersion;
    msg.object["t"] = t;
    msg.object["protocol"] = proto;
    msg.object["gui"] = gui;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Prompt(const std::string& text, const std::string& clientId) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t;   t.type = json::Value::Type::String;  t.text = "prompt";
    json::Value txt; txt.type = json::Value::Type::String; txt.text = text;
    msg.object["t"] = t;
    json::Value client; client.type = json::Value::Type::String; client.text = clientId;
    msg.object["text"] = txt;
    msg.object["client_id"] = client;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Workspace(const std::string& requestId, const std::string& path) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t; t.type = json::Value::Type::String; t.text = "workspace";
    json::Value id; id.type = json::Value::Type::String; id.text = requestId;
    json::Value p; p.type = json::Value::Type::String; p.text = path;
    msg.object["t"] = t;
    msg.object["request_id"] = id;
    msg.object["path"] = p;
    return SendWith(std::move(msg));
}

bool ProtocolWriter::Model(const std::string& requestId, const std::string& provider,
                           const std::string& model, int context, int maxOutput) {
    json::Value msg;
    msg.type = json::Value::Type::Object;
    json::Value t; t.type = json::Value::Type::String; t.text = "model";
    json::Value id; id.type = json::Value::Type::String; id.text = requestId;
    json::Value p; p.type = json::Value::Type::String; p.text = provider;
    msg.object["t"] = t;
    msg.object["id"] = id;
    msg.object["provider"] = p;
    // ★ 2026-10-27：空模型名**不写**字段（而不是写空串）——写空串会让 core
    //   把"没指定"当成"指定了空模型"，与省略字段的语义正好相反。
    if (!model.empty()) {
        json::Value mv; mv.type = json::Value::Type::String; mv.text = model;
        msg.object["model"] = mv;
    }
    // ★★ 2026-10-27（v4 第三步）：`context` / `maxOutput` **总是写**，包括 0。
    //   为什么改（原来是 `> 0` 才写）：v4 起两个参数跟着**模型**走，同一家下
    //   M1 标了 200K、M2 没标。省略字段的语义是"沿用上一个模型的值" ⇒ 切到 M2
    //   后 core 仍按 M1 的 200K 当分母（百分比是假的），max_tokens 也照旧发 M1 的
    //   上限（端点可能 400）。显式写 0 的语义是**清零**（未知），core 侧对应
    //   `wire.GuiMsg.Context/MaxOutput` 改成指针才能区分这两件事。
    {
        json::Value c; c.type = json::Value::Type::Number;
        c.number = static_cast<double>(context > 0 ? context : 0);
        msg.object["context"] = c;
    }
    {
        json::Value m; m.type = json::Value::Type::Number;
        m.number = static_cast<double>(maxOutput > 0 ? maxOutput : 0);
        msg.object["maxOutput"] = m;
    }
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