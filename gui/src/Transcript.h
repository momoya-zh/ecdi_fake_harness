#pragma once
// 块模型 + 协议不变量记账（规格 §4.3）。
// Transcript 只管解析与不变量；UI 行为由 App 按 LastMessage() 路由。

#include "Json.h"

// ★★ 2026-11-09（富内容 §H）：段内内联样式的类型来源。Transcript.h 从此依赖
//   两个 ECDI 头——这是**有意的方向**：`TextSpan` 的码点契约就是本仓要产出的
//   数据结构，抄一份本地镜像会让「码点 vs 字节」这个最容易错的地方出现第二份真相。
#include "ECDI/Core/Color.h"          // InlineCodeStyle::color
#include "ECDI/Widget/TextSpan.h"     // TextSpan / SpanStyle（码点索引空间）

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
    // ★★ 协议 v5 §3.3（2026-11-04）：子代理归属（**可选字段**）。
    //   `agentId` 空 = 根代理的块（v3/v4 的每一块都是这样）。
    //   `parentId` = 发起这次委派的 `tool_call` 块 id（I13）；只在带 agent 时有意义。
    //   ★ 为什么归属不做成第八个 `kind`：`kind` 答「这段文字是干什么的」，
    //     `agent` 答「谁说的」，两者正交。合并会逼每个渲染分支去枚举 kind×agent 的组合，
    //     而 I8（未知 kind 必须中性渲染）也就不再保护任何东西。
    std::string agentId;
    std::string parentId;
    // ★★ 待澄清的 kind（2026-10-13）：`kind` 不在 §3.3 的六个已知值里。
    //   动机：harness 面向的 agent 用**开源模型**时会传**思维链**
    //   （`reasoning` / `thinking` / …），而协议 §3.3 的枚举里没有它的位置
    //   ⇒ 此前这类块**静默**掉进工具卡分支、标题写死「等待结果」
    //   （实测截图见 `.scratch/kind-probe.png`）。本字段记录该事实供记账与中性渲染。
    bool unknownKind = false;
    // ★★ 2026-11-06：本块 `block_start` 的 `ts`（自 1970-01-01Z 的毫秒）。
    //   用途**唯一**：回合折叠的「· 1.2s」副标——一个回合从第一条过程块到最后一条
    //   结论块跨了多久，只能由块自己的时刻求差得出。
    //   ★ 0 = 「core 没给 / 解析不了」⇒ **不显示耗时**，且**不得**用帧时刻或行高
    //     倒推冒充（同 `tokens` 那条纪律：不把没测到的写成测到了）。协议 §1 说
    //     core→gui 的 `ts` 必填，但 hostile 夹具与手写回放都可能缺，故必须留 0 态。
    long long tsMs = 0;
};

// ★★ 协议 v5 §3.12（2026-11-04）：子代理的生命周期（`t:"agent"` 控制面消息）。
//   ★ 它**不是转录块**：不进 `m_blocks` / `m_order`、没有 `block_end`、不占块 id 空间。
//     子代理产出的内容仍然走普通的 `block_*` 消息，只是带 `agent` 归属字段。
//   ★ 为什么不复用 `state`：`state` 是**全局**状态机，GUI 的发送/停止门控由它驱动
//     （「只由 state 驱动」这条一个字都不能改）。子代理状态是展示信息，混进去会让
//     「能不能发下一条 prompt」这种判断掺进一个与之无关的维度。
struct AgentInfo {
    std::string id;
    std::string name;
    std::string parentCall;   // start：发起委派的 tool_call 块 id（与块的 parent 同值）
    std::string task;         // start：委派任务原文（权限门里展示过的同一段）
    std::string model;        // start（可选）：仅显示
    int depth = 0;            // start：恒 1
    bool started = false;
    bool ended = false;
    bool ok = false;          // end
    std::string summary;      // end：回给主代理的同一段结论
    int turns = 0;            // end
    long long inTokens = 0;   // end
    long long outTokens = 0;  // end
    long long elapsedMs = 0;  // end
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
    // ★★ v5：带 `agent` 归属的块数（与 `--agent-probe` 的缩进行数应逐条相等——
    //   一个归属块 ⇔ 一行缩进。两边不等就是「模型里有、视图里没有」或反之）。
    size_t AgentBlocks() const { return m_agentBlocks; }
    // ★★ v5：`agent` 生命周期自身的记账（I12 的 GUI 侧读数）。
    //   ★ **只记账不拒绝**：协议的判定权在 `validate.py` / core，GUI 收到一条
    //     错序消息的正确反应是照常渲染（前向兼容），而不是把整条流判死。
    const std::map<std::string, AgentInfo>& Agents() const { return m_agents; }
    size_t AgentViolations() const { return m_agentViolations; }
    void FinalizeIncompleteBlocks();
    size_t MaxClientIdIndex() const;
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
    std::map<std::string, AgentInfo> m_agents;   // ★ v5 子代理生命周期（id → 状态）
    size_t m_agentBlocks = 0;                    // ★ v5 带 agent 归属的块数
    size_t m_agentViolations = 0;                // ★ v5 I12 的 GUI 侧记账
    long long m_lastSeq = -1;
    bool m_seenSeq = false;
};

// ── Markdown 管道表格数据结构与纯函数解析器（富内容二期）─────────────────────
// 纯函数解析器收敛于本单元（零副作用、可直接在探针与渲染管线中测试）。

enum class MarkdownAlign {
    Left,
    Center,
    Right
};

struct MarkdownTable {
    std::vector<std::string> headers;
    std::vector<MarkdownAlign> alignments;
    std::vector<std::vector<std::string>> rows;
};

// ★★ 2026-11-08（富内容 B+，rich-content-plan §G）：块级全套段种。
//   ★ 为什么是「段种」而不是「样式位」：ECDI 一控件一套字体（`TextWidget.h`
//     的 TextStyle 唯一来源）⇒ 每个段种迟早要落成**独立控件**，字体各给各的。
//     所以段种必须携带着自己的**全部渲染参数**，而不是靠一个 style 位回去查表。
//   ★ 链接刻意**不在**本枚举里：REQ-19-B（OpenExternal）仍开放，用户 2026-11-08
//     裁定「等 REQ-19-B 落地再做链接」⇒ 本批 `[x](y)` 按原文进 Paragraph，
//     不解析、不假装支持。加进来却点不开，比不渲染更糟。
enum class MarkdownSegmentKind {
    Paragraph,
    Table,
    Heading,      // 标题：level ∈ [1,6]，text = 去掉 # 前缀与首尾空白的正文
    CodeBlock,    // 围栏代码块：lang = 信息串（可为空），text = 围栏内原文（保留换行与缩进）
    ListItem,     // 列表的**一项**：ordered/marker/depth + text；连续项由解析器各自产出一条
    Quote,        // 引用块：text = 合并后的正文（`>` 已剥离）
    Hr            // 分隔线：`---` / `***` / `___`
};

struct MarkdownSegment {
    MarkdownSegmentKind kind = MarkdownSegmentKind::Paragraph;
    std::string text;             // Paragraph / Heading / CodeBlock / ListItem / Quote 有效
    MarkdownTable table;          // kind == Table 时有效

    // ── 块级全套（2026-11-08）的参数位 ─────────────────────────────
    //   ★ 只用与 kind 相关的字段；其余保持默认值。这样段结构仍是**值语义**，
    //     探针可直接逐字段断言，不需要 setter/getter 那一层。
    int level = 0;                // Heading：[1,6]（ClampHeadingLevel 已夹紧）
    std::string lang;             // CodeBlock：信息串（` ```json ` → "json"），可空
    bool ordered = false;         // ListItem：有序（`1.`）还是无序（`-`/`*`/`+`）
    int marker = 0;               // ListItem：有序项的**原文序号**（不重排，见解析器注释）
    int depth = 0;                // ListItem：0 = 顶层；缩进 2 空格 = 1 级（见 kListIndentSpaces）
};

// 纯函数解析器（两块职责，均为**零副作用**、可直接在探针与渲染管线中调用）：
//   1. 识别标准 GitHub Flavored Markdown 管道表格（Pipe Table）；
//   2. 识别块级全套：标题 / 围栏代码块 / 列表项 / 引用块 / 分隔线。
// 当文本**不含任何**上述构造时，返回单个 Paragraph 片段（内容逐字等于原 text），
// 与未解析等价 —— 这是本仓的**基线不变量**：7 份既有夹具的渲染路径一字不改。
// ★ 链接不解析（REQ-19-B 未落地，用户 2026-11-08 裁决）。
std::vector<MarkdownSegment> ParseMarkdownSegments(const std::string& text);

// ★★ 2026-11-08（富内容 B+）：**是否值得走复合渲染**的唯一判据。
//   ★ 为什么要一个独立函数而不是各处自己 for 一遍：这个判定原先在两处各写了一份
//     「找 Table」（`AppTranscript.cpp:261` 重建期 / `AppStream.cpp:581` 流式结束期），
//     加块级时必须同步改两处；漏改一处的后果是**静默不渲染**（不报错、不崩、只是
//     标题还是原样）。收成一处后，漏改的形态从「静默」变成「编译不过」。
//   ★ 判据口径：**只要有一个非 Paragraph 段**就算。这与 `ParseMarkdownSegments`
//     的基线不变量（无非 Paragraph 段 ⇒ 单 Paragraph 等于原文）严格互补——
//     两边同时成立意味着「判定为 false 的行，其解析结果恰好等于原文」。
[[nodiscard]] bool HasMarkdownBlocks(const std::vector<MarkdownSegment>& segs);

// ═══════════════════════════════════════════════════════════════════════
// 段内内联样式（富内容 §H，2026-11-09）
// ═══════════════════════════════════════════════════════════════════════
// ★★ 能力来源：ECDI `#88` / Phase 44 交付 `TextWidget::SetStyledText(text, spans)`，
//   span 契约见 `ECDI/Widget/TextSpan.h`（**码点索引空间**，非字节偏移）。
// ★★ 本仓消费的维度：
//     粗体 `**x**` / `__x__`      → `SpanStyle::weight = 700`
//     斜体 `*x*` / `_x_`          → `SpanStyle::italic = true`（Phase 45）
//     粗斜体 `***x***` / `___x___` → `weight = 700` + `italic = true`
//     行内 code `` `x` ``          → `SpanStyle::family` + `foreground`
//   `size` 覆盖虽然 Phase 45 已开，但本批**留位不用**（保版心行高稳定）。
//   `background` 框架没有（`SpanStyle` 无此字段）⇒ 行内 code 画不出底色，
//   已登记 `REQ-21`，见 `docs/ecdi-capability-requests.md`。
//
// ★ 为什么解析器住在这里而不是渲染侧：与 `ParseMarkdownSegments` 同一条纪律——
//   纯函数、零副作用、探针可直接调。渲染侧只负责「把 spans 交给控件」。
// ★ 为什么行内 code 的字体族与颜色是**入参**而不是写死：解析器不该知道主题色；
//   写死会让 `--probe-contrast` 双模式与将来换主题都要回来改解析器。

/// 行内 code 的视觉参数（由调用方按主题给；解析器只做拼接，不做取色）
struct InlineCodeStyle {
    std::string  family;                    // 等宽族（本仓恒 `kMonoFamily` = "Consolas"）
    ECDI::Color  color;                     // 行内 code 前景色
};

/// 行内解析结果：干净文本 + 已规范化的样式区间
/// @details `text` 是**剥掉标记后**的文本（`**粗**` → `粗`，`*斜*` → `斜`）；`spans` 与 `text`
///          同一码点空间、有序、无重叠（相邻同样式已合并）。
///          `spans.empty()` ⇔ 没有任何行内构造 ⇔ 调用方应走原 `SetText` 路径。
struct InlineRuns {
    std::string                  text;
    std::vector<ECDI::TextSpan>  spans;

    [[nodiscard]] bool HasStyles() const noexcept { return !spans.empty(); }
};

/// 纯函数行内解析器（零副作用）
/// @details 支持：`**b**` / `__b__`（粗体）、`*i*` / `_i_`（斜体）、`***bi***`（粗斜体）、
///          `` `c` ``（行内 code）、反斜杠转义（`\*` `\_` `` \` `` `\\` ⇒ 字面字符，反斜杠本身不保留）。
///          规则细节见 `Transcript.cpp` 实现处的注释块（flanking / intraword /
///          未闭合回落 / code 内不解析），每一条都对应 `--md-probe` 的一个相。
[[nodiscard]] InlineRuns ParseInlineRuns(const std::string& text, const InlineCodeStyle& codeStyle);

/// 行内构造的存在性判据（与 `HasMarkdownBlocks` 同一条纪律：单一真相源）
/// @details 供渲染侧决定「要不要走 SetStyledText」，**不**重复解析逻辑。
[[nodiscard]] bool HasInlineStyles(const InlineRuns& runs);

