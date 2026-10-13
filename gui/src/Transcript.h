#pragma once
// 块模型 + 协议不变量记账（规格 §4.3）。
// Transcript 只管解析与不变量；UI 行为由 App 按 LastMessage() 路由。

#include "Json.h"

#include <map>
#include <string>

// 一个转录块：block_start .. block_end 之间可被 delta 追加（I6：end 后只读）
struct Block {
    std::string id;
    std::string kind;         // user / assistant / tool_call / tool_result / system / error
    std::string name;         // tool_call 的工具名
    std::string argsPretty;   // tool_call 的 args 美化文本
    std::string forId;        // tool_result 引用的 tool_call id（I7）
    std::string text;         // block_start.text + 顺序拼接的全部 delta
    bool ok = false;          // block_end.ok
    std::string summary;      // block_end.summary
    bool ended = false;
    // ★★ 待澄清的 kind（2026-10-13）：`kind` 不在 §3.3 的六个已知值里。
    //   动机：harness 面向的 agent 用**开源模型**时会传**思维链**
    //   （`reasoning` / `thinking` / …），而协议 §3.3 的枚举里没有它的位置
    //   ⇒ 此前这类块**静默**掉进工具卡分支、标题写死「等待结果」
    //   （实测截图见 `.scratch/kind-probe.png`）。本字段记录该事实供记账与中性渲染。
    bool unknownKind = false;
};

class Transcript {
public:
    // 解析一行 JSONL 并更新块表。
    // 返回 false = 本行不可用（解析失败 / I1 / I2 拒绝），App 不得再路由它；
    // error 非空 = 有需显式提示的协议事件（I4 缺口等）——I4 时消息本身仍被处理。
    bool OnLine(const std::string& jsonLine, std::string& error);

    const Block* FindBlock(const std::string& id) const;
    const json::Value& LastMessage() const { return m_last; }

    // 评估读数（规格 §6.3）
    size_t SeqGaps() const { return m_seqGaps; }          // I4 命中数
    size_t Blocks() const { return m_blocks.size(); }     // 已启动的块数
    size_t Deltas() const { return m_deltas; }
    size_t Violations() const { return m_violations; }    // 全部不变量命中数
    size_t UnknownTypes() const { return m_unknownTypes; }
    // ★★ 未知 `kind` 的块数（2026-10-13 新增）。
    //   与 `UnknownTypes()` **并列而非合并**——两者管辖不同的判别符：
    //     · `UnknownTypes` = 信封层的 `t` 未知（I5 明文覆盖）；
    //     · `UnknownKinds` = `block_start.kind` 未知（**协议未规定**，属空白）。
    //   合并会让「协议空白」被误读成「I5 已覆盖」。
    size_t UnknownKinds() const { return m_unknownKinds; }
    // 已知 kind 集合的判据（供 App 与 validate 口径一致）
    [[nodiscard]] static bool IsKnownKind(const std::string& kind);

private:
    std::map<std::string, Block> m_blocks;
    json::Value m_last;
    size_t m_seqGaps = 0;
    size_t m_deltas = 0;
    size_t m_violations = 0;
    size_t m_unknownTypes = 0;
    size_t m_unknownKinds = 0;   // ★ 未知 block_start.kind 计数（2026-10-13）
    long long m_lastSeq = -1;
    bool m_seenSeq = false;
};
