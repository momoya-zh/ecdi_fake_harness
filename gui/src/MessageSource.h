#pragma once
// core→gui 一行消息的来源（规格 §4.1；P1.5 起有两个实现）。
// 实现：ReplaySource（夹具回放）/ ChildSource（子进程真路径）。

#include <string>

class MessageSource {
public:
    virtual ~MessageSource() = default;

    // 阻塞读取下一条 core→gui 行；EOF 或致命错误返回 false。
    virtual bool Next(std::string& line) = 0;

    // 活体源（子进程）为 true——UI 措辞/进度显示据此区分（回放显示 n/N，
    // live 显示已收条数）。默认 false ⇒ 既有实现零改动。
    virtual bool IsLive() const { return false; }

    // 反向通道：写一条 gui→core 消息（无尾随换行，由实现补）。
    // 回放源不支持 ⇒ 默认 false（回放模式下按钮仍可用，仅记录到转录）。
    virtual bool Send(const std::string& line) {
        (void)line;
        return false;
    }
};