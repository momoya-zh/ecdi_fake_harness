#pragma once
// 块模型 + 协议不变量记账（规格 §4.3）。
// Transcript 只管解析与不变量；UI 行为由 App 按 LastMessage() 路由。

#include "Json.h"

#include <map>
#include <string>
#include <vector>

// 一个转录块：block_start .. block_end 之间可被 delta 追加（I6：end 后只读）
struct Block {
    std::string id;
    std::string clientId;      // 协议 v3 user 回显关联键
    std::string kind;         // user / assistant / reasoning / tool_call / tool_result / system / error
    std::string name;         // tool_call 的工具名
    std::string argsPretty;   // tool_call 的 args 美化文本
    std::string forId;        // tool_result 引用的 tool_call id（I7）
    std::string text;         // block_start.text + 顺序拼接的全部 delta
    bool ok = false;          // block_end.ok
    std::string summary;      // block_end.summary
    // ★★ 协议 v2 §3.5（2026-10-14）：该块的 token 数（**可选**——`block_end.tokens`）。
    //   用途：`reasoning` 卡的标题副标（`▼ 已完成 · 1843 tokens`）。
    //   ★ 0 = 「core 没给」⇒ **不显示**，且**不得**用字符数冒充（那是编造读数）。
    //   ★ 放在 `block_end` 而非 `block_start` 是协议的决定：token 数只有内容产完才知道。
    long long tokens = 0;
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

    // ★★ 到达顺序（2026-10-19，Step 0a）：`m_blocks` 是 **std::map**，
    //   遍历顺序是 **id 字典序**，而块 id 是 `b0 b1 … b43` 这种**不补零**的编号
    //   ⇒ 字典序 ≠ 到达序。实测（.scratch/order_fixture_scan.py，六份夹具全扫）：
    //     · demo        8 块 —— 两者恰好一致（b1..b8 位数相同）；
    //     · cot        44 块 —— **42** 个位置不同，首个分歧 b2(到达) vs b10(字典序)；
    //     · realistic 220 块 —— 218 处； · scale 2000 块 —— 1998 处。
    //   ⇒ 谁「照着块表重建视图」，就会打乱转录，而且**看不出是错的**：
    //     每块内容都对，只是次序错（比空屏更危险）。
    //   ★ 故单列一条顺序旁路，**不动 std::map 本身**——`FindBlock` 在 delta 每行
    //     都被调用，改成有序容器等于拿 O(log n) 换一份本来就已经知道的顺序。
    //   ★ 只增不减：I1（重复 block_start）在入表前就 return ⇒ 不会重复入列。
    const std::vector<std::string>& Order() const { return m_order; }

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
    long long LastSeq() const { return m_lastSeq; }
    // 已知 kind 集合的判据（供 App 与 validate 口径一致）
    [[nodiscard]] static bool IsKnownKind(const std::string& kind);

private:
    std::map<std::string, Block> m_blocks;
    // ★ 到达顺序（与 m_blocks 的字典序**无关**）：重建视图只许按本序列，
    //   按 m_blocks 遍历即等于按 id 字典序重排转录（2026-10-19 Step 0a）。
    std::vector<std::string> m_order;
    json::Value m_last;
    size_t m_seqGaps = 0;
    size_t m_deltas = 0;
    size_t m_violations = 0;
    size_t m_unknownTypes = 0;
    size_t m_unknownKinds = 0;   // ★ 未知 block_start.kind 计数（2026-10-13）
    long long m_lastSeq = -1;
    bool m_seenSeq = false;
};
