// ============================================================
// 转录视图：建行 / 物化 / 折叠 / 待确认气泡 / 空状态引导
// ------------------------------------------------------------
// 为什么单独一个文件：中栏转录树是**控件最多、结构最深**的一棵（工具卡带
//   CollapsiblePanel、思维链带展开态、ask 行带回答态、notice 行带时刻）。
//   它和流式解包（AppStream）是两件事：那边只管"收到什么"，这边只管
//   "长什么样"。混在一起时，改一个气泡圆角要滚过 300 行协议分支。
// ★ 纪律：本文件**只碰中栏转录树**（`m_content` / `m_rows` / `m_pendingPromptViews`）；
//   会话列表归 AppSession.cpp。
//   ★★ 2026-10-27：**行宽不在这里算**。唯一口是 `TranscriptLayoutWidth()`（版心宽）
//     与 `TranscriptCanvasX()`（居中左边界），摆位在 `Relayout`（AppHud.cpp）。
//     本文件只在 list 模式下给新行一个**初始**宽度（`ContentWidth`），随后由
//     `Relayout` 统一改成版心宽——别把它当第二份真相源。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1d）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "ECDI/Widget/ScrollBar.h"
#include "AppInternal.h"
#include "ThemeUsing.h"
#include "Transcript.h"
#include "UiKit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Core/Logger.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Platform/PlatformWindow.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 跨 TU 共享符号（定义见 UiCommon.cpp；v6 后台作业展示口径）
using harness::internal::JobStatusLabel;
using harness::internal::JobHumanBytes;

// 跨 TU 共享符号（定义见 AppInternal.h / UiCommon.cpp）
using harness::internal::AskNeedsFold;
using harness::internal::AskTitle;
using harness::internal::ContentWidth;
using harness::internal::Pick;
using harness::internal::ReasoningTitle;
using harness::internal::UnknownKindText;
using harness::internal::kCanvasPadX;
using harness::internal::kListRowHeight;
using harness::internal::kProbeBg;
using harness::internal::kProbeInk;
using harness::internal::kProposalLineH;

void HarnessApp::AddRow(std::unique_ptr<ECDI::Widget> row, const RowRec& rec,
                        const std::string& blockId, size_t timelineIdx) {
    RowRec stored = rec;
    stored.panel = row.get();
    // ★★ 0c：行与时间线条目**成对**登记。下标由调用方**显式给出**而不是取
    //   `rows.size()`——重建时若某条目建不出行（块缺失即跳过），两者就会错位，
    //   而"视图状态写回模型"依赖这个下标指对地方（错位 = 把 A 的展开态写到 B 上）。
    stored.timelineIdx = timelineIdx;
    if (m_options.rowModeList) {
        stored.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
    }
    m_sess->rows.push_back(stored);
    m_content->AddChild(std::move(row));
    // ★★ 2026-11-01（TODO-07 第二步）：存**下标**，不再存 `&rows.back()`。
    //   指向 deque 元素的指针在 push_back 后确实还有效（那条不变量成立），但它把
    //   「行表被清空」变成了一个**静默悬垂指针**；下标配合越界检查只会变成
    //   nullptr ⇒ 失效表现为一次显式判空。取行统一走 `FindBlockRow()`。
    if (!blockId.empty()) m_sess->blockRows[blockId] = m_sess->rows.size() - 1;
    MaterializePendingPrompts();  // keep transient rows physically after canonical rows.
    // settle 后新增的行（发送/停止/回答反馈）没有逐 tick 的 Relayout 可蹭——
    // 排一个补重排脉冲，让定时器把它布局出来
    m_relayoutPulse = std::max(m_relayoutPulse, 2);
}

// ★★ 2026-11-01（TODO-07 第二步）：`blockRows` 的**唯一**读取入口。
//   为什么必须有这一层：`blockRows` 存的是 `rows` 的下标，而下标只在「两次重建之间」
//   有效（重建时两张表成对清空）。直接 `find` 后解引用等于把这条时序约定散到每个调用点；
//   收进一个函数后，失效只剩一种表现——返回 nullptr。
HarnessApp::RowRec* HarnessApp::FindBlockRow(const std::string& blockId) {
    const auto it = m_sess->blockRows.find(blockId);
    if (it == m_sess->blockRows.end()) return nullptr;
    if (it->second >= m_sess->rows.size()) return nullptr;   // 越界 = 行表已重建
    return &m_sess->rows[it->second];
}

// ★★ 0c（2026-10-19）：行构造拆成**两层**。
//   `BuildBlockRow` = **纯函数**：只吃 `Block` 值 + `m_options`，产出控件树并填 `rec`；
//     不追加时间线、不发协议、不写全局状态 ⇒ **可以被重放任意多次**。
//   `CreateBlockRow` = 增量入口：建行 → 追加时间线条目 → 绑回调 → 探针捕获。
//   为何必须拆：重建视图要能"把同一个块再画一次"，而增量入口带副作用（时间线会翻倍）。
//   为何拆得开：本分支历来只读 `block` 的字段（不读会话状态），故纯化是**零成本**的。
//
//   ★ 重建保真度的一个隐含要求：**标题必须是 `block` 当前值的函数**。
//     增量路径里标题由两段代码写（`block_start` 建行时给初值、`block_end` 到达时改写），
//     重建只有一次机会 ⇒ 那两段必须收敛成同一个函数，否则重建出来的是「初值标题」
//     （工具卡会永远停在「等待结果」，而这正是历史上那个误导 bug 的形态）。
//     故 `ToolTitle()` 收口两处，`ReasoningTitle()` 早已是同款收口。
std::string ToolTitle(const Block& block, const Block* call) {
    std::string base;
    if (block.kind == "tool_call") {
        base = block.name.empty() ? "tool_call" : block.name;
    } else if (!block.ended) {
        base = "等待结果";            // 结果块还没结束：不知道是哪个工具的结论
    } else if (call && !call->name.empty()) {
        base = call->name;            // 结果块：工具名来自 forId 引用的 call 块
    } else {
        base = "tool";
    }
    if (block.ended) {
        // ★ REQ-02（2026-10-13）：`summary` 曾在此被吞——`tool_call` 的 `block_end`
        //   也会带 `summary`（协议 §3.5 允许），两类都必须落地。
        if (!block.summary.empty()) {
            base += " · " + block.summary;
        } else if (!block.ok) {
            base += " · 失败";
        }
    }
    // ★ 2026-10-18：`▸`(U+25B8) 两主题 face 都无字形 ⇒ `›`(U+203A)（同右向尖括号语义）
    return "› " + base;
}

// ★★ 协议 v6（2026-11-05）：工具行标题的**唯一出口**（ToolTitle + 后台状态后缀）。
//   增量（block_end / 折叠切换）与重建（MaterializeOne）都走这里；两处各拼一次
//   必然漂移，而漂移的表现就是"重建后标题少一段"——探针 A/B 的判据正是它。
std::string HarnessApp::ToolTitleWithJob(const Block& block) const {
    const Block* call = (block.kind == "tool_result")
                            ? m_sess->transcript.FindBlock(block.forId)
                            : nullptr;
    std::string title = ToolTitle(block, call);
    if (!m_sess) return title;
    auto it = m_sess->jobsByCall.find(block.id);
    if (it != m_sess->jobsByCall.end()) {
        title += " · " + it->second.id + " " + JobStatusLabel(it->second.status);
    }
    return title;
}

// ★★ 协议 v6：把一个作业的当前状态画到它的 tool_call 行上。
//   ★ 标题走 ToolTitleWithJob（单一出口）；正文 = 工具参数 + 作业头 + 尾部输出。
//   ★ 没有 tail 时说「（暂无输出）」而不是空白——空白会被读成"卡片坏了"。
void HarnessApp::ApplyJobToRow(RowRec& row, const std::string& callId) {
    if (!m_sess) return;
    auto it = m_sess->jobsByCall.find(callId);
    if (it == m_sess->jobsByCall.end()) return;
    const JobView& v = it->second;

    if (row.headerBtn) {
        if (const Block* b = m_sess->transcript.FindBlock(callId)) {
            row.headerBtn->SetText(ToolTitleWithJob(*b));
        } else {
            row.headerBtn->SetText("› " + v.command + " · " + v.id + " " +
                                   JobStatusLabel(v.status));
        }
    }
    if (!row.label) return;

    std::string body;
    if (const Block* b = m_sess->transcript.FindBlock(callId)) {
        body = b->argsPretty.empty() ? "{}" : b->argsPretty;
    } else {
        body = "{}";
    }
    body += "\n\n── 后台 " + v.id + " · " + JobStatusLabel(v.status) + " · 输出 " +
            JobHumanBytes(v.bytes);
    if (v.dropped > 0) body += " · 已丢弃 " + JobHumanBytes(v.dropped);
    body += " ──\n";
    body += v.tail.empty() ? "（暂无输出）" : v.tail;
    if (v.status != "running") body += "\n[退出码 " + std::to_string(v.exitCode) + "]";
    row.label->SetText(body);
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildBlockRow(const Block& block, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();

    // ★ 0c：**每一**条块行都登记自己的块 id（此前只有 reasoning/tool 两支登记）。
    //   两个后果，都不是小事：
    //   ① 重建探针靠"有没有 blockId"区分「块行」与「通知行」——chat 气泡没有 id 时
    //      会被当成通知，于是负对照只比到可折叠行（实测 5/19/126/500，而真实错位量
    //      是 0/42/218/1998）⇒ **负对照失去了判别力**，正向全等也就不算证据。
    //   ② 行不知道自己来自哪个块，任何"按块回查"的诊断都只能靠猜。
    rec.blockId = block.id;
    rec.ended = block.ended;

    // ★★ 协议 v5（2026-11-04）：子代理的行右移一档，最左侧挂一条归属竖线。
    //   ★ 缩进是**一个常数**而不是 `24 * depth`：协议 §3.12 把深度钉死为 1
    //     （子代理不得再开子代理），算出来的 depth 只可能是 0 或 1 ⇒ 不存在
    //     「深度算错」这类判据，也就不该写一个会算错的式子。
    //   ★ 竖线进 `panel` 的子表（x=0，宽 3）：行内文字从 `kRowPadX`(10) 起排，
    //     两者不重叠；宽度/高度在 `Relayout` 里下发（行高测量后才知道）。
    if (!block.agentId.empty()) {
        rec.indent = harness::internal::kAgentIndent;
        rec.agentTag = block.agentId;
        auto gutter = std::make_unique<ECDI::Panel>();
        rec.gutter = gutter.get();
        gutter->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kLine, kProbeInk),
            .cornerRadius = 1.0f});
        panel->AddChild(std::move(gutter));
    }

    if (block.kind == "user") {
        rec.alignRight = true;
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kUserBorder});
        // ★★ P4 G：user 行同款只读 TextBox 化（见下方 assistant 分支注释）。
        if (m_options.selectableRows) {
            auto box = std::make_unique<ECDI::TextBox>();
            rec.label = box.get();
            box->SetText(block.text);
            box->SetReadOnly(true);
            box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            box->SetFont(harness::SansFont(harness::kFontBody, 400));
            box->SetStyle(ECDI::TextBoxStyleOverride{
                .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
                .caretWidth = 0.0f,
                .padding = 0.0f,
                .cornerRadius = 0.0f,
                .borderWidth = 0.0f});
            panel->AddChild(std::move(box));
            return panel;
        }
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        // ★ 正文必须走主题字体工厂：此前不 SetFont ⇒ family 为空 ⇒ 落 SimSun，
        //   而侧栏/标题走雅黑 ⇒ **同一界面两种中文字形**（浅色下尤其割裂）。
        label->SetFont(harness::SansFont(harness::kFontBody, 400));
        panel->AddChild(std::move(label));
        return panel;
    }

    if (block.kind == "assistant" || block.kind == "system" || block.kind == "error") {
        // ★★ 2026-10-27：智能体正文（及 system/error）改为透明无边框纯文本直出样式
        //   （底色对齐画布 kContentBg、0 描边、0 圆角）。同一刀随后把工具行与权限门
        //   也拍平 ⇒ 整份转录里**只有用户消息**保留气泡（那是"我说的"，需要区分）。
        const ECDI::Color plainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = plainBg,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = plainBg});

        // ★★ 富内容 B+（2026-11-08）：判据从「含 Table」推广为「含任何块级构造」。
        //   ★ 这一步是块级全套真正的**入口开关**：解析器早就会产 Heading/CodeBlock/
        //     ListItem/Quote/Hr 了，但这里若仍只认 Table，那些段**永远不会被渲染**，
        //     而且不报错、不崩——只是原文照旧显示。判据本身收在 Transcript.cpp 一处
        //     （`HasMarkdownBlocks`），漏改一处的形态从「静默」变成「编译不过」。
        if (block.kind == "assistant" && block.ended) {
            auto segs = ParseMarkdownSegments(block.text);
            if (HasMarkdownBlocks(segs)) {
                rec.hasMarkdown = true;
                BuildAssistantMarkdownWidgets(*panel, rec, segs);
                return panel;
            }
        }
        // ★★ P4 G 尖峰（--selectable-rows）：assistant 正文行用只读 TextBox——
        //   「编辑 no-op 但方向键/选区/Ctrl+A/C 照常」（TextBox.h:50 契约）⇒ 转录
        //   可选中/复制不需要框架新能力。视觉与 Label 版同形：透明底/无边框/
        //   无 caret/同字体同色。默认关闭 ⇒ 基线路径零变化。
        if (m_options.selectableRows) {
            auto box = std::make_unique<ECDI::TextBox>();
            rec.label = box.get();
            box->SetReadOnly(true);
            box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            box->SetFont(harness::SansFont(harness::kFontBody, 400));
            box->SetStyle(ECDI::TextBoxStyleOverride{
                .background = plainBg,
                .caretWidth = 0.0f,
                .padding = 0.0f,
                .cornerRadius = 0.0f,
                .borderWidth = 0.0f});
            if (block.kind == "system") box->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
            if (block.kind == "error") box->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
            // ★ 2026-11-09（§H）：一度**只有**「有块级构造」的 assistant 行才会走
            //   `BuildAssistantMarkdownWidgets`（那里的段控件才是富文本）。普通段落
            //   ——也就是绝大多数回答——走的就是这条单控件路径。不在这里接，
            //   「`git status` 加粗」这类行内样式在真机上永远不会出现。
            ApplyInlineText(*box, block.text);
            panel->AddChild(std::move(box));
        } else {
            auto label = std::make_unique<ECDI::Label>();
            rec.label = label.get();
            label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            label->SetFont(harness::SansFont(harness::kFontBody, 400));   // 同 user 分支：统一中文 face
            if (block.kind == "system") label->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
            if (block.kind == "error") label->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
            ApplyInlineText(*label, block.text);
            panel->AddChild(std::move(label));
        }

        if (block.kind == "assistant") {
            auto copy = std::make_unique<ECDI::Button>("复制");
            rec.copyBtn = copy.get();
            copy->SetVisible(false);   // 由 SyncTurnCopyButtons 统一控制仅在每轮回答末尾显示
            copy->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
            copy->SetFont(harness::SansFont(12.0f));
            copy->SetStyle(ECDI::ButtonStyleOverride{
                .background = plainBg,
                .border = plainBg,
                .borderWidth = 0.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            const std::string bid = block.id;
            auto* copyRaw = copy.get();
            copy->SetOnClick([this, bid, copyRaw] { CopyTurnTextByBlockId(bid, copyRaw); });
            panel->AddChild(std::move(copy));
        }
        return panel;
    }

    // ★★ 思维链卡（协议 v2 §7，2026-10-14）——**默认收起**，点击标题展开，箭头翻转。
    //   ★ 为何必须有这条**显式分支**：`reasoning` 一旦进入 `IsKnownKind`，
    //     `block.unknownKind` 就是 **false** ⇒ 它会**直接掉到本函数末尾的工具卡分支**，
    //     而那个分支的 else 把标题写死成「等待结果」——**正是 2026-10-13 修掉的那个 bug
    //     换个方式复活**（思维链又一次伪装成"正在等的工具结果"）。加 kind 必须同时加分支。
    //   ★ 复用 `CollapsiblePanel`（与工具卡同机制，不新造控件）：
    //     折叠动画、内容可见性、展开基准全由框架既有能力承担。
    //   ★★ 2026-10-27：思维链改为透明无边框的纯文本样式（当时含「无悬停变暗、
    //     无点击按下变色」）。**2026-11-03 C 批修订**：悬停/按下反馈恢复——用户
    //     反馈「向下的尖括号难用」，根因就是整条 header 可点却零反馈、看不出能点。
    //     无边框保持不变；反馈走 `kReasoningHover`/`kReasoningPressed` 梯级 + tooltip。
    if (block.kind == "reasoning") {
        rec.reasoning = true;
        rec.ended = block.ended;
        rec.blockId = block.id;
        const ECDI::Color plainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = plainBg,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = plainBg});

        // ★★ 2026-11-04（用户裁决 A+C）：`TipButton` → `SpinnerButton`。
        //   它只在**标题右侧空白处**多画一个 90° 弧（自绘，`DrawArc`），
        //   其余（tooltip / hover / 点击展开）全部继承自 TipButton，零行为变化。
        //   ★ 为什么这里只给 `rec.spinner` 一个弱指针、不在这里启动动画：
        //     开停由 **tick** 按 `r.reasoning && !r.ended` 统一收敛（AppHud）。
        //     在回调/建行期启动就得额外维护"块结束了没有"的第二份真相。
        auto header = std::make_unique<uikit::SpinnerButton>(
            ReasoningTitle(block, rec.expanded), "点击展开 / 收起",
            [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
            [this](uikit::TipButton*) { HideTooltip(); });
        rec.headerBtn = header.get();
        rec.spinner = header.get();
        header->SetTextColor(Pick(m_options.probeContrast, kReasoningInk, kProbeInk));
        {
            const ECDI::Font f = harness::SansFont(14.0f);
            header->SetFont(f);
            // ★ 同一个字体交给 spinner：它要按**绘制侧同一度量**把环摆在标题右缘
            //   （ECDI 没有 GetFont，故这里显式下发；两处同一个值，不可能漂移）。
            header->SetSpinFont(f);
        }
        header->SetStyle(ECDI::ButtonStyleOverride{
            .background = plainBg,
            .border = plainBg,
            .borderWidth = 0.0f,
            .cornerRadius = 0.0f,
            .pressedBackground = Pick(m_options.probeContrast, kReasoningPressed, kProbeBg),
            .hoverBackground = Pick(m_options.probeContrast, kReasoningHover, kProbeBg),
            .textAlignment = ECDI::TextAlignment::Left});
        panel->AddChild(std::move(header));

        auto body = std::make_unique<ECDI::CollapsiblePanel>();
        rec.body = body.get();
        rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
        rec.body->SetStyle(ECDI::PanelStyleOverride{
            .background = plainBg,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = plainBg});
        auto label = std::make_unique<ECDI::Label>(block.text);
        rec.label = label.get();
        // 展开后是**用户主动要读**的内容 ⇒ 用常规 ink（不再弱化）
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetWordWrap(true);   // 思维链很长，必须按宽断行
        {
            ECDI::Font f = harness::SansFont(14.0f, 400);   // ★ 正文体：UI 默认 700，这里显式 400
            label->SetFont(f);
        }
        rec.body->GetContent()->AddChild(std::move(label));
        panel->AddChild(std::move(body));
        return panel;
    }

    // ★★ 未知 `kind` 的**中性落点**（2026-10-13）。
    //   问题（实测取证，见 `.scratch/kind-probe.png` / `cot-findings.md`）：
    //     §3.3 只列六个 kind，而 I5 只管信封层的 `t` ⇒ 未知 `kind` **不被任何不变量覆盖**。
    //     此前的三分支写法让它们**静默掉进工具卡分支**，而该分支的 else 把标题写死成
    //     「等待结果」——那正是 `tool_result` 在 `block_end` 到达**前**的占位标题
    //     ⇒ **思维链块与「正在等的工具结果」视觉上无法区分**（不是难看，是**误导**）。
    //   ★ 归属 **harness 侧**（ECDI 只是忠实渲染调用方给的控件树）。
    //   ★ 处置 = **中性渲染**：弱化色 + 明写 `[未知 kind: xxx]` + 普通气泡（不折叠、
    //     不占工具卡的位置）。理由：既保住 I5 的前向兼容精神（内容仍被渲染、不被丢弃），
    //     又不让它**伪装成有语义的形态**。
    //   ★ 思维链（`reasoning` / `thinking` …）正是这条路径的主要真实来源——
    //     给它一个**一等公民的位置**需要协议 v2（`kind:"reasoning"`），
    //     在那之前，中性渲染是「不误导」的最小正确行为。
    if (block.unknownKind) {
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            .borderColor = kWarn});   // 琥珀描边：视觉上明确「这不是普通助手文本」
        // 前缀显式标出不认识的 kind；正文仍按原样展示（不丢内容）
        std::string shown = UnknownKindText(block);
        auto label = std::make_unique<ECDI::Label>(shown);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kWarn, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody, 400));   // 统一中文 face
        panel->AddChild(std::move(label));
        return panel;
    }

    // tool_call / tool_result：可折叠行（默认折叠；标题按钮 + CollapsiblePanel 展开体）
    // ★★ 2026-10-27：**去卡片化**。此前这里是"底色 + 圆角 + 1px 描边"的卡片，
    //   而同一轮改造已经把 assistant/reasoning 改成透明无边框纯文本 ⇒ 转录里
    //   只剩下工具行和权限门还浮着一层卡，读起来像两套设计语言。
    //   现在整份转录是**一篇文档**：只有用户消息保留气泡（那是"我说的"，
    //   需要与智能体的输出区分），工具调用/思维链/正文一律平铺。
    //   ★ 可识别性没丢：`›` 前缀 + 折叠体缩进 + 等宽正文仍是工具行的特征，
    //     只是不再靠"描边 + 圆角 + 底色"这三件卡片外衣。
    //   ★ 为什么连 hover/pressed 也去掉：见 reasoning 分支——整行通栏变暗比
    //     "没有 hover"更刺眼；点击的反馈是**展开体真的出现**，那比配色更明确。
    rec.tool = true;
    rec.blockId = block.id;
    const ECDI::Color toolPlainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = toolPlainBg,
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f,
        .borderColor = toolPlainBg});

    std::string bodyText;
    if (block.kind == "tool_call") {
        bodyText = block.argsPretty.empty() ? "{}" : block.argsPretty;
    } else {
        bodyText = block.text.empty() ? block.summary : block.text;
    }

    const Block* call = (block.kind == "tool_result")
                            ? m_sess->transcript.FindBlock(block.forId)
                            : nullptr;
    // ★★ 2026-11-03 C 批（同思维链头）：Button → TipButton + 悬停/按下梯级。
    //   根因同一条：整条 header 可点（BindCollapsible → ToggleRow）却零反馈。
    auto header = std::make_unique<uikit::TipButton>(
        ToolTitle(block, call), "点击展开 / 收起",
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    rec.headerBtn = header.get();
    // 结束且失败 ⇒ 标题转错误色（增量路径在 `block_end` 做，重建在这里做，同一判据）
    header->SetTextColor((block.ended && !block.ok)
                             ? kErrorDark
                             : Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    {
        ECDI::Font f = harness::SansFont(14.0f);
        header->SetFont(f);
    }
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = toolPlainBg,
        .border = toolPlainBg,
        .borderWidth = 0.0f,
        .cornerRadius = 0.0f,
        .pressedBackground = Pick(m_options.probeContrast, kToolHeaderPressed, kProbeBg),
        .hoverBackground = Pick(m_options.probeContrast, kToolHeaderHover, kProbeBg),
        .textAlignment = ECDI::TextAlignment::Left});   // Phase 30 △3：卡头贴左（hi-fi .tool>.h）
    // ── P4 Phase I：标题旁「⤢」预览按钮（§11 入口经评审修订——标题按钮的点击
    //   已被折叠占用，BindCollapsible → ToggleRow）。仅当标题文本匹配路径形态时
    //   才创建（PreviewPathFromText 与探针共用同一实现——判据只有一份）。
    const std::string previewTarget = PreviewPathFromText(ToolTitle(block, call));
    if (!previewTarget.empty()) {
        auto headRow = std::make_unique<ECDI::Panel>();
        headRow->SetLayout(std::make_unique<ECDI::HorizontalLayout>(6, true, 0));
        // 底色与标题一致（Panel 自带不透明底 ⇒ 不显式覆盖就会露主题默认色）
        headRow->SetStyle(ECDI::PanelStyleOverride{
            .background = toolPlainBg,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = toolPlainBg});
        header->SetStretch(1);   // 标题弹性吃剩余宽，⤢ 靠右
        headRow->AddChild(std::move(header));
        auto pv = std::make_unique<ECDI::Button>("⤢");
        pv->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
        pv->SetFont(harness::SansFont(14.0f));
        // ★★ 去卡片化后这里**刻意保留 hover**（与文本标题不同）：`⤢` 是个只有字形
        //   的图标钮，没有文字语义、没有箭头可读，hover 是它唯一的"可点"信号。
        //   文本标题不需要 hover —— 它左边有 `›`/`︿` 箭头，点下去展开体真的出现。
        pv->SetStyle(ECDI::ButtonStyleOverride{
            .background = toolPlainBg,
            .border = toolPlainBg,
            .borderWidth = 0.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kToolHeaderHover});
        pv->SetOnClick([this, target = previewTarget] { OpenPreview(target); });
        headRow->AddChild(std::move(pv));
        panel->AddChild(std::move(headRow));
    } else {
        panel->AddChild(std::move(header));
    }
    auto body = std::make_unique<ECDI::CollapsiblePanel>();
    rec.body = body.get();
    rec.body->SetExpandDirection(ECDI::ExpandDirection::Down);
    // ★ 折叠体自带 Panel 底（默认主题色，不是画布色）⇒ 显式抹平，否则展开后
    //   会在平铺的转录里露出一块"主题默认色"的矩形（去卡片化后尤其明显）。
    rec.body->SetStyle(ECDI::PanelStyleOverride{
        .background = toolPlainBg,
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f,
        .borderColor = toolPlainBg});
    // ★★ 2026-11-07（G 计划收尾）：tool 折叠体可选中队列的最后一块——user/system/
    //   error/assistant/markdown 段落在此前各批已铺（README 的「只到尖峰」欠账行
    //   实为漂移），tool_call/tool_result 的折叠体是真正的剩余缺口，也是日常最想
    //   复制的（工具输出是转录里最长的文本）。体部换只读 TextBox：与 Label 版同形
    //   （透明底/无边框/无 caret/同 mono 字体同色/显式 wrap——SetWordWrap 在
    //   TextWidget 基类，Label/TextBox 通用）。header 是折叠开关，保持 Button 不动。
    //   作业增量路径（ApplyJobToRow 的 label->SetText）对只读 TextBox 同样成立：
    //   只读挡的是用户编辑，不挡程序化 SetText。reasoning 折叠体刻意不进
    //   （思维链默认折叠、复制场景弱），探针与债务行都如实记这个边界。
    if (m_options.selectableRows) {
        auto box = std::make_unique<ECDI::TextBox>();
        rec.label = box.get();
        box->SetText(bodyText);
        box->SetReadOnly(true);
        box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        box->SetWordWrap(true);   // Phase 29：工具体按 bodyW 断行（JSON/日志长串）
        box->SetFont(harness::MonoFont(14.0f));
        box->SetStyle(ECDI::TextBoxStyleOverride{
            .background = toolPlainBg,
            .caretWidth = 0.0f,
            .padding = 0.0f,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f});
        rec.body->GetContent()->AddChild(std::move(box));
    } else {
        auto label = std::make_unique<ECDI::Label>(bodyText);
        rec.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetWordWrap(true);   // Phase 29：工具体按 bodyW 断行（JSON/日志长串）
        {
            ECDI::Font f = harness::MonoFont(14.0f);
            label->SetFont(f);
        }
        rec.body->GetContent()->AddChild(std::move(label));
    }
    // Phase 30 批二：CollapsiblePanel 收起态钉住内容可见性（#51④）——
    // 收起溢绘已由框架修复，应用侧 SetVisible workaround 退役。
    panel->AddChild(std::move(body));
    return panel;
}

// 可折叠行的点击绑定（**构建期与重建期共用**）。
//   ★ 0c 的关键变化：展开态**先写回模型**（`timeline[idx].expanded`），再动控件。
//     在此之前展开态只活在 `RowRec` 上 ⇒ 任何重建都会把用户的展开操作静默回退。
//   ★★ 2026-10-27：去掉 `const Block& block` 形参——它自始至终只被 `(void)block` 掉
//     （标题重算发生在 `ToggleRow` 里回查块），而权限门行**没有 Block**（它来自
//     `TimelineEntry::Kind::Ask`）。带着一个"假装需要"的形参，就没法让 ask 行走同一条
//     绑定路径，结果必然是"另写一份 lambda"= 第二份切换实现（不写回模型、不设
//     `m_relayoutPulse` ⇒ 展开了行高不跟、重建即回退）。
void HarnessApp::BindCollapsible(RowRec& stored) {
    if (!stored.headerBtn) return;
    const bool reasoning = stored.reasoning;
    stored.headerBtn->SetOnClick([this, &stored, reasoning] {
        if (m_options.rowModeList) return;   // 固定行高模式：展开体被后续行覆盖，v1 禁用
        ToggleRow(stored, reasoning);
    });
}

// 展开/收起一行（**点击路径与探针共用的唯一入口**）。
//   ★ 为什么必须抽成函数：0c 的重建探针要构造"用户已经展开过"的状态，而它**不能**
//     合成点击——探针需要在**任意**行上造状态，包括滚动到视口外、HitTest 命不中的行；
//     若探针自己再写一遍"改标志 + 翻箭头 + 重排"，那就是**第二份实现**，测的是它自己。
void HarnessApp::ToggleRow(RowRec& stored, bool reasoning) {
    stored.expanded = !stored.expanded;
    // ★ 视图 → 模型（重建前若来不及扫全表，这一条保证单行状态不丢）
    if (stored.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[stored.timelineIdx].expanded = stored.expanded;
    }
    if (stored.body) stored.body->Toggle();
    // 箭头随展开态翻转（﹀ ⇄ ︿）；文案与 token 副标由**单一真相源**决定，
    // 故回查块（`block_end` 可能已到达 ⇒ 文案应为「已完成」+ tokens）
    if (stored.headerBtn) {
        if (stored.ask) {
            // ★★ 2026-10-27 权限门：标题 = 箭头 + 「权限 · 摘要」，由**模型里的问句**
            //   重算（理由同工具卡那一支：不能置空、也不能凭 `RowRec` 拼一份——两份必漂移）。
            //   ★ 摘要走 `AskSummary` 的列宽截断，与建行时同一个函数 ⇒ 展开/收起
            //     只是箭头翻转，文本一位不变。
            if (stored.timelineIdx < m_sess->timeline.size()) {
                const TimelineEntry& e = m_sess->timeline[stored.timelineIdx];
                stored.headerBtn->SetText(AskTitle(e.text, stored.expanded, AskNeedsFold(e.text)));
            }
        } else if (const Block* b = m_sess->transcript.FindBlock(stored.blockId)) {
            if (reasoning) {
                stored.headerBtn->SetText(ReasoningTitle(*b, stored.expanded));
            } else {
                // ★ 修 2026-10-19：此前工具卡这一支传的是 `std::string()`
                //   ⇒ **点一下工具卡，标题就被清空**（卡片从此没有名字）。
                //   与"箭头两态"同一个道理：标题必须由单一真相源重算，不能置空。
                //   ★ v6：单一真相源升级为 ToolTitleWithJob——后台作业的终态/状态
                //     也在这里重贴，否则点一下折叠就把后台状态抹掉了。
                stored.headerBtn->SetText(ToolTitleWithJob(*b));
            }
        }
    }
    m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画
    // ★★ Step 5：展开态也进 sidecar（门槛见 `CanSaveUiState`；理由同 MarkAnswered）。
    if (CanSaveUiState()) SaveUiState(*m_sess);
    Relayout();             // 立即重排一次（settle 后定时器路径不再经过 Relayout）
    RequestRepaint();
}

void HarnessApp::CreateBlockRow(const Block& block) {
    RowRec rec;
    auto panel = BuildBlockRow(block, rec);

    // ★★ 0c：**先**登记模型条目，**再**交给 `AddRow`——
    //   `AddRow` 用 `rows.size()` 当条目的下标，两者等长同序才成立。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Block;
    entry.id = block.id;
    entry.expanded = false;
    m_sess->timeline.push_back(entry);

    AddRow(std::move(panel), rec, block.id, m_sess->timeline.size() - 1);
    if (m_sess->rows.empty()) return;
    RowRec& stored = m_sess->rows.back();

    // ★ 在**建行时刻**抓流式态标题（`block_start` 刚到、`block_end` 还没来）。
    //   为何必须此刻抓：`--instant` 在同一 tick 抽干夹具 ⇒ 事后回读永远是
    //   「已完成」，`▼ 思考中` 那一态**根本采不到**（首版就栽在这里）。
    if (m_options.reasoningProbe && stored.reasoning && m_probeState.reasoningTitleStreaming.empty()) {
        m_probeState.reasoningTitleStreaming = stored.headerBtn->GetText();
    }
    // 只有可折叠行需要绑回调（普通气泡/未知 kind 无展开体）
    if (stored.body && stored.headerBtn) BindCollapsible(stored);
}

// 视图 → 模型：把 `rows` 上的临场状态（展开/已答）写回 `timeline`。
//   ★ 为何要"扫全表"而不是只靠回调里那句写回：重建可能由**别的**路径触发
//     （换主题、切会话、探针），那些路径不经过点击回调。重建前统一同步一次，
//     才能保证"用户看到的"与"模型记下的"在拆树那一刻一致。
void HarnessApp::SyncRowViewState() {
    for (RowRec& r : m_sess->rows) {
        if (r.timelineIdx >= m_sess->timeline.size()) continue;
        TimelineEntry& entry = m_sess->timeline[r.timelineIdx];
        if (r.body) entry.expanded = r.expanded;
        if (r.ask) entry.answered = r.answered;
    }
}

// 把一条时间线条目落成一行（构建期与重建期**共用同一个**分派点）。
//   ★ 重建期**绝不**执行副作用：不重发 answer、不重跑自动应答、不追加通知。
void HarnessApp::MaterializeOne(size_t idx, const TimelineEntry& entry) {
    // ★ idx = 本条目在 `timeline` 中的位置，直接当作新行的 `timelineIdx`：
    //   重建期**不**追加条目、**不**发副作用，行与条目一一对应。
    (void)idx;
    switch (entry.kind) {
        case TimelineEntry::Kind::Block: {
            const Block* block = m_sess->transcript.FindBlock(entry.id);
            if (!block) return;   // 模型里指向一个不存在的块：如实跳过，不编造
            RowRec rec;
            rec.expanded = entry.expanded;   // ★ 先回填状态再建行：标题箭头据此定态
            auto panel = BuildBlockRow(*block, rec);
            // ★★ 协议 v6：重建时把后台作业状态贴回这张工具卡（模型 jobsByCall
            //   是唯一来源；磁盘 journal 重放路径同样经过 RouteLastMessage）。
            ApplyJobToRow(rec, block->id);
            AddRow(std::move(panel), rec, block->id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.expanded = entry.expanded;
            if (stored.body && stored.headerBtn) {
                BindCollapsible(stored);
                // ★ 展开态要真的落到控件上（`expanded` 标志只影响行高计算，
                //   折叠体的内容可见性由控件自己管）
                if (entry.expanded) stored.body->SetExpanded(true);
            }
            return;
        }
        case TimelineEntry::Kind::Ask: {
            RowRec rec;
            auto panel = BuildAskRow(entry, rec);
            AddRow(std::move(panel), rec, entry.id, idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.expanded = entry.expanded;   // 与 Block 行同款：展开态只认模型
            stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
            stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });
            // ★★ 2026-10-27：折叠头绑定走与建行期**同一个入口**——否则重建出来的
            //   权限门点了没反应（外观一样、行为没了，是最难发现的一类重建缺陷）。
            //   `SetExpanded` 是幂等的（`BuildAskRow` 已按 `entry.expanded` 建过），
            //   这里再下发一次只是把"折叠体呈现"与"行高计算用的标志"钉在同一处。
            if (stored.body && stored.headerBtn) {
                BindCollapsible(stored);
                stored.body->SetExpanded(entry.expanded);
            }
            // ★ 已答状态**静默回填**：只置灰、不重发 answer（否则重建 = 重放一次决策，
            //   在 live 会话里就是往 core 又答了一遍 —— 那是会改协议状态的真副作用）。
            // ★★ Step 3：走**同一条**视觉路径（`ApplyAnsweredLook`），答案取
            //   `entry.answer`。第一版这里只有两行 `SetEnabled(false)` ⇒ 重建出来的
            //   权限门与"刚答完"不同形（配色不还原），而 A==B 比的是文字与位标志，
            //   看不见配色 ⇒ 又一次"比对全等但屏幕不一样"（同 0c 的 buildTitle 缺陷）。
            if (entry.answered) {
                stored.answered = true;
                ApplyAnsweredLook(stored, entry.answer, entry.answerAt);
            }
            return;
        }
        case TimelineEntry::Kind::Notice: {
            RowRec rec;
            auto panel = BuildNoticeRow(entry.text, entry.color, rec);
            AddRow(std::move(panel), rec, std::string(), idx);
            return;
        }
        case TimelineEntry::Kind::Fold: {
            RowRec rec;
            auto panel = BuildFoldRow(entry, rec);
            AddRow(std::move(panel), rec, std::string(), idx);
            if (m_sess->rows.empty()) return;
            RowRec& stored = m_sess->rows.back();
            stored.expanded = entry.expanded;
            // ★ 点击绑走 `ToggleTurn`（**唯一**的回合展开/收起入口）——探针也从
            //   这里造状态，不另写一份"翻标志 + 重排"。
            //   ★★ 捕获**下标**、不捕获 `RowRec&`：`ToggleTurn` 内部会整表重建，
            //     行的 `vector` 会重新分配 ⇒ 捕获引用就是一个悬空指针
            //     （第一版就是这么崩的：EXIT 0xC0000005，回放一结束即崩）。
            //     下标在重建前后含义不变（`timeline` 只有本特性会改序，且改序时
            //     连行表一起重建）。
            const size_t foldIdx = stored.timelineIdx;
            if (stored.headerBtn) {
                stored.headerBtn->SetOnClick([this, foldIdx] { ToggleTurn(foldIdx); });
            }
            return;
        }
    }
}

void HarnessApp::MaterializeRows() {
    for (size_t i = 0; i < m_sess->timeline.size(); ++i) {
        MaterializeOne(i, m_sess->timeline[i]);
    }
}

// Level 1 重建：拆掉转录区的全部行控件，再按模型重放一遍。
//   ★ 只碰 `m_content` 的子树 ⇒ `CountingPanel`/HUD/侧栏/检查器指针全部存活，
//     评估读数不中断（整树重建才会打死计数器，那是 Level 2，留给 0e）。
//   ★ 拆之前必须先把视图状态同步进模型，否则等于"照着一份更旧的模型重建"。
void HarnessApp::RebuildTranscriptView(const std::vector<std::string>* orderOverride) {
    SyncRowViewState();
    m_pendingPromptViews.clear();

    const int keepScroll = m_scroll ? m_scroll->GetScrollOffsetY() : 0;

    m_sess->rows.clear();
    m_sess->blockRows.clear();
    if (m_content) {
        while (m_content->GetChildCount() > 0) {
            m_content->RemoveChild(m_content->GetChildAt(0));
        }
    }
    m_hud.emptyGuide = nullptr;

    if (!orderOverride) {
        MaterializeRows();
    } else {
        const size_t kNoTimeline = static_cast<size_t>(-1);
        for (const std::string& id : *orderOverride) {
            const Block* block = m_sess->transcript.FindBlock(id);
            if (!block) continue;
            RowRec rec;
            auto panel = BuildBlockRow(*block, rec);
            AddRow(std::move(panel), rec, block->id, kNoTimeline);
            if (m_sess->rows.empty()) continue;
            RowRec& stored = m_sess->rows.back();
            if (stored.body && stored.headerBtn) BindCollapsible(stored);
        }
    }
    MaterializePendingPrompts();
    // ★★ 2026-11-06：折叠可见性必须在 `Relayout` **之前**落地——`Relayout` 是
    //   几何函数，它只读取 `rows[*].foldHidden`（跳过被折叠的行），不写它。
    ApplyFoldVisibility();

    Relayout();
    if (m_scroll) m_scroll->SetContentOffset(0, keepScroll);
}

// ═══════════════════════════════════════════════════════════════════════
// ★★ 2026-11-06：回合级折叠（"回答结束后把前面的 read_file / grep 收起来"）
//
//   需求：转录里 `read_file` / `grep` 这类过程行是一行行平铺打印的，等最终回答
//   出来以后，用户要往下翻很久才能看到结论。要求是**回答一结束就把这一段过程收成一条**。
//
//   ★ 与既有的"单行折叠"（工具行默认收起）的关系：那条折叠只收**一行自己的正文**，
//     标题还在屏上占一行。一个回合最多 9 行过程（`realistic` 实测），所以摊开后
//     仍然要滚很久。本功能收的是**整段**：过程行全部隐藏，只留一条摘要。
//
//   ★★ 三个不可让步的约束（都来自本仓已踩过的坑）：
//     ① **状态进模型**（`TimelineEntry`）。只挂 `RowRec` ⇒ 换主题/切会话一次，
//        整个回合又摊开（同 `expanded` / `answered` 那条老坑）。
//        ★ 而且状态挂在**那条 `user` 条目**上（`turnCollapsed`），不是摘要条上：
//          摘要条每次 `RetagTurns` 都会被拆掉重建（见下），状态挂在会被重建的
//          东西上就是 0c 的老坑原样复发。
//     ② **摘要文案只算一次**并存进条目（`foldSummary`）。回合的成员会随新消息增长，
//        若在渲染期现算，一段已经折起来的回合，副标题会在屏幕上自己变。
//     ③ **摘要条在两种状态下都存在**（收起 `› `／展开 `▾ `）。第一版是"展开后
//        摘要条消失"，结果是展开一个回合以后**没有任何把手能把它再收回去**——
//        用户只能滚到下一轮。常驻一条是这条约束的唯一可行形状。
//
//   ★ 实现形状：**插入并常驻**（不是替换）。`RetagTurns` 先把上一轮的 `Kind::Fold`
//     全部摘掉、重算每回合的过程集合，再给每个够格的回合**新建**一条摘要条目、
//     由 `ApplyTurnInserts` 插到它的 `user` 行之后；过程行只翻 `foldHidden`。
//     ★ 代价必须说清：插入会在 `state:idle` 让 `timeline.size()` **+1/回合**，
//       于是 `--rebuild-probe` 的 `rebuildTimelineCount` 读数口径随之改变（那支
//       守的是"重建不增删条目"）。本仓选择接受这个代价，因为"替换式"在约束 ③
//       上无解；两边的张力记在这里，而不是假装不存在。
//     ★ 不够两条过程行的回合**不折**：一条工具行本来就只剩一行标题，再加一条
//       摘要条等于**多一行 + 多一次点击**，净亏。
//     ★ 回合里有任何块还在流（`!b->ended`）就整轮不折：折了会把正在写的那一行
//       藏掉，而「已完成 / 耗时」当场就是假话。
// ═══════════════════════════════════════════════════════════════════════

// 一个回合的成员判定：**只有** user 行开新回合，回合内的过程行是
// tool_call / tool_result / reasoning / 中间 assistant，外加**已答**的权限门。
//   ★★ 2026-11-07 用户裁决（翻案）：已答的权限门是历史事实，与 tool_result 同属
//     "这一回合发生过什么"，随回合一起收起；展开后结论（● 已允许/已拒绝）由模型
//     `answered/answer/answerAt` 经 `ApplyAnsweredLook` 原样还原，不丢审计信息。
//     **pending** 的门仍绝不收起——那是待决交互，藏起来等于把决策 UI 藏掉
//     （由 `RetagTurns` 的 anyStreaming 一挡）。
//   ★ notice 仍不收：通知行是 GUI 自己的诊断输出，不是回合的过程。
bool HarnessApp::IsUserEntry(const TimelineEntry& e) const {
    if (e.kind != TimelineEntry::Kind::Block || e.id.empty()) return false;
    const Block* b = m_sess->transcript.FindBlock(e.id);
    return b && b->kind == "user";
}

// 一个回合的摘要文案。**纯函数**（只读参数区间内的模型），故可被探针单独调用。
//   ★ 不带箭头：状态箭头（›/▾）由**唯一**调用方 `RetagTurns` 按折叠态拼——
//     此前本函数内部也拼一个 "› "，和调用方的前缀叠成「› › 已完成」（实测双箭头）。
std::string HarnessApp::TurnSummary(size_t begin, size_t end) const {
    size_t steps = 0;
    size_t asks = 0;   // 已答授权数（2026-11-07：已答权限门随回合折叠，摘要如实报数）
    long long firstTs = 0;
    long long lastTs = 0;
    for (size_t i = begin; i < end && i < m_sess->timeline.size(); ++i) {
        const TimelineEntry& e = m_sess->timeline[i];
        if (e.kind == TimelineEntry::Kind::Ask) {
            if (!e.answer.empty()) ++asks;   // pending 的门不进摘要（它根本不该出现在已折回合里）
            continue;
        }
        if (e.kind != TimelineEntry::Kind::Block || e.id.empty()) continue;
        const Block* b = m_sess->transcript.FindBlock(e.id);
        if (!b || b->kind == "user") continue;
        if (b->kind == "tool_call") ++steps;
        // 耗时口径：块自己的信封 `ts`（core 作者时刻）。缺一个就不显示——不做
        // "用现有最大的那个代替"（那是编造读数，同 `tokens` 的纪律）。
        if (b->tsMs > 0) {
            if (firstTs == 0 || b->tsMs < firstTs) firstTs = b->tsMs;
            if (b->tsMs > lastTs) lastTs = b->tsMs;
        }
    }
    std::string out = "已完成";
    if (steps > 0) {
        out += " \xC2\xB7 " + std::to_string(steps) + " \xE6\xAD\xA5";   // " 步"
    }
    if (asks > 0) {
        out += " \xC2\xB7 " + std::to_string(asks) + " \xE6\x8E\x88\xE6\x9D\x83";   // " 授权"
    }
    if (firstTs > 0 && lastTs > firstTs) {
        char buf[48];
        const double secs = static_cast<double>(lastTs - firstTs) / 1000.0;
        std::snprintf(buf, sizeof(buf), " \xC2\xB7 %.1fs", secs);
        out += buf;
    }
    return out;
}

// 把 `timeline` 重切成回合并算出折叠态。**唯一**写 `Kind::Fold` 的地方。
//   ★ 幂等：每次都从折叠态字段重新推 `foldHidden` 集合，绝不"在上一次结果上
//     继续翻"——那样一次失败的调用会永久留下半折叠状态。
//   ★ 无副作用的窗口很窄：本函数只改 `timeline` 的标志与摘要条条目，
//     不碰 `rows`；真插入了摘要条（下标移动）时调用方必须跟着整表重建。
//   ★★ 2026-11-07 订正返回值语义：从「`insertIdx > 0`」（只要有任一合格回合就
//     true ⇒ 每个 `state:idle` 都整表重建——展开/收起卡顿的放大器）收成
//     **真变化**：与调用前快照逐条比对，摘要条集合、`foldable`/`foldHidden`
//     标志、摘要文案全等 ⇒ false ⇒ 调用方免付重建。快照在 ① 的 erase 之前取。
bool HarnessApp::RetagTurns() {
    if (!m_sess) return false;
    auto& tl = m_sess->timeline;

    // ⓪ 变化快照（本函数会写的全部字段）。
    struct FoldDerived {
        TimelineEntry::Kind kind;
        bool foldable;
        bool foldHidden;
        std::string summary;
    };
    std::vector<FoldDerived> before;
    before.reserve(tl.size());
    for (const TimelineEntry& e : tl) {
        before.push_back({e.kind, e.foldable, e.foldHidden, e.foldSummary});
    }

    // ① 先拆掉上一次算出来的摘要条（本函数是摘要条的**唯一**作者，所以
    //    "先全拆、再全建"是最不会漂移的形状）。
    //   ★ 折叠态 `turnCollapsed` 挂在 **user 条目**上，不在摘要条上——摘要条每次
    //     重算都会被重建，状态挂在它上面就会在重算时丢（这正是 0c 的老坑：
    //     状态挂在会被重建的东西上）。
    for (size_t i = 0; i < tl.size();) {
        if (tl[i].kind == TimelineEntry::Kind::Fold) {
            tl.erase(tl.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (TimelineEntry& e : tl) {
        e.foldable = false;
        e.foldHidden = false;
    }

    // ② 逐个回合重算。回合边界：user 行开新回合（与 `SyncTurnCopyButtons` 同口径）。
    std::vector<TimelineEntry> inserts;   // (位置在 user 之后) → 摘要条
    size_t i = 0;
    while (i < tl.size()) {
        if (!IsUserEntry(tl[i])) {
            ++i;
            continue;
        }
        const size_t userIdx = i;
        const size_t begin = i + 1;
        size_t end = begin;
        while (end < tl.size() && !IsUserEntry(tl[end])) ++end;

        // 过程行 = 工具/思维链 + 中间 assistant。
        //   ★ 最后一条**已结束且非空**的 assistant 是结论，不收（判据与
        //     `SyncTurnCopyButtons` 挑复制按钮那处同源——两份判据必然漂移，
        //     漂移的表现是"复制按钮挂在一条已经收起来的行上"）。
        size_t lastAssistant = static_cast<size_t>(-1);
        bool anyStreaming = false;
        for (size_t k = begin; k < end; ++k) {
            const TimelineEntry& e = tl[k];
            // ★★ 2026-11-07：pending 权限门在场 ⇒ 回合仍活跃。权限门的决策 UI
            //   （完整问句 + 允许/拒绝两钮）绝不能被收起来；且"等待授权"时用户
            //   点旧回合的摘要条也会走到这里（`ToggleTurn` 经 `RetagTurns` 重算），
            //   必须把含待决门的这一回合整体挡住。
            if (e.kind == TimelineEntry::Kind::Ask && !e.answered) {
                anyStreaming = true;
                break;
            }
            if (e.kind != TimelineEntry::Kind::Block || e.id.empty()) continue;
            const Block* b = m_sess->transcript.FindBlock(e.id);
            if (!b) continue;
            if (!b->ended) anyStreaming = true;
            if (b->kind == "assistant" && b->ended && !b->text.empty()) lastAssistant = k;
        }
        // ★ 回合还在流式（或有待决权限门）⇒ 不折。折了会把正在写的那一行藏起来，
        //   而且摘要条的"已完成 / 耗时"在那一刻本来就是假的。
        if (anyStreaming) {
            i = end;
            continue;
        }

        std::vector<size_t> proc;
        for (size_t k = begin; k < end; ++k) {
            if (k == lastAssistant) continue;   // 结论不折
            const TimelineEntry& e = tl[k];
            // ★★ 2026-11-07（用户裁决）：**已答**的权限门随回合一起收起。走到这里
            //   的 ask 必然已答（pending 已在上面 anyStreaming 一挡）；展开后结论由
            //   `answered/answer/answerAt` 经 `ApplyAnsweredLook` 原样还原，不丢审计。
            if (e.kind == TimelineEntry::Kind::Ask) {
                if (!e.answer.empty()) proc.push_back(k);
                continue;
            }
            if (e.kind != TimelineEntry::Kind::Block || e.id.empty()) continue;
            const Block* b = m_sess->transcript.FindBlock(e.id);
            if (!b || b->kind == "user") continue;
            if (b->kind == "tool_call" || b->kind == "tool_result" ||
                b->kind == "reasoning" || b->kind == "assistant") {
                proc.push_back(k);
            }
        }

        // 少于两条过程行不值得收——一条工具行本来就是收起态（只有标题一行），
        // 再套一层摘要条只会**多**一行、多一次点击，比现状更差。
        if (proc.size() < 2) {
            i = end;
            continue;
        }

        for (size_t k : proc) tl[k].foldable = true;

        // ③ 摘要条**始终存在**（折叠时它是"展开"的把手，展开时它是"收起"的把手）。
        //   ★ 为什么不让它在展开时消失：那样展开以后就没有任何入口再把它收起来
        //     （用户要一路翻回回合开头去找），而"收起"恰恰是这个特性存在的理由。
        //   ★ 文案里因此带箭头两态，与工具行标题同一套约定（`ReasoningTitle`）。
        TimelineEntry foldRow;
        foldRow.kind = TimelineEntry::Kind::Fold;
        foldRow.foldSummary = (tl[userIdx].turnCollapsed ? "\xE2\x80\xBA " : "\xE2\x96\xBE ")
                              + TurnSummary(begin, end);
        inserts.push_back(foldRow);

        // ④ 折叠时隐藏过程行；展开时全部可见。
        if (tl[userIdx].turnCollapsed) {
            for (size_t k : proc) tl[k].foldHidden = true;
        }

        i = end;
    }

    // ⑤ 把摘要条插进 `user` 之后（`ApplyTurnInserts` 负责按下标改序）。
    //   ★ 插入会移动其后条目的下标 ⇒ 一旦真插了（摘要条集合变了），调用方
    //     **必须**紧跟着整表重建，让 `rows[i].timelineIdx == i` 重新成立。
    ApplyTurnInserts(inserts);

    // ⑥ 与 ⓪ 的快照逐条比对：全等 ⇒ 本调用在模型上零变化 ⇒ false（调用方的
    //   `state:idle` 闸门据此免付整表重建）。`foldSummary` 是纯函数重算的 ⇒
    //   未变的回合逐字节相同，不会误报。
    if (tl.size() != before.size()) return true;
    for (size_t i = 0; i < tl.size(); ++i) {
        const TimelineEntry& e = tl[i];
        const FoldDerived& b = before[i];
        if (e.kind != b.kind || e.foldable != b.foldable ||
            e.foldHidden != b.foldHidden || e.foldSummary != b.summary) {
            return true;
        }
    }
    return false;
}

// 把摘要条插到对应 `user` 行之后。**与 `RetagTurns` 配套的唯一插入点**。
//   ★ 单列一个函数是为了让"按下标改序"这件事只有一处实现：`timeline` 的下标
//     是 `rows[i].timelineIdx` 的另一半，两处各改一遍必然错位。
bool HarnessApp::ApplyTurnInserts(const std::vector<TimelineEntry>& inserts) {
    auto& tl = m_sess->timeline;
    if (inserts.empty()) return false;
    size_t insertIdx = 0;
    for (size_t i = 0; i < tl.size() && insertIdx < inserts.size(); ++i) {
        if (!IsUserEntry(tl[i])) continue;
        // 这条 user 之后必须有过程行才该有摘要条——判据与 `RetagTurns` 同源：
        // 摘要是"这个回合的过程行 >= 2"，此处用 `foldable` 反查（已由上面标好）。
        bool hasProc = false;
        for (size_t k = i + 1; k < tl.size(); ++k) {
            if (IsUserEntry(tl[k])) break;
            if (tl[k].foldable) { hasProc = true; break; }
        }
        if (!hasProc) continue;
        tl.insert(tl.begin() + static_cast<std::ptrdiff_t>(i + 1), inserts[insertIdx]);
        ++insertIdx;
        ++i;   // 跳过刚插入的摘要条本身
    }
    return insertIdx > 0;
}

// 折叠/展开一个回合（点击摘要条与探针共用的**唯一**入口）。
//   ★ `foldIdx` = 摘要条（`Kind::Fold`）在 `timeline` 里的下标。
//   ★ 状态翻的是**摘要条前面那条 `user` 条目**上的 `turnCollapsed`——摘要条自己
//     每次 `RetagTurns` 都会被拆掉重建，状态挂在它上面会在重算时丢（0c 的老坑：
//     状态不能挂在会被重建的东西上）。
//   ★★ 2026-11-07：**快路径，不重建**。点击时摘要条已在 ⇒ `RetagTurns` 重算只翻
//     `foldHidden` / `foldSummary`，条目集合与下标不动 ⇒ `ApplyFoldVisibility`
//     （App.h 注释本就预设它是本函数的调用者）+ 重贴摘要条文案即可收场。
//     此前每次点击都 `RebuildTranscriptView`：新建全部行控件 ⇒ ECDI 文本测量
//     缓存（C28-5 / C29-10，挂在控件实例上）全灭 ⇒ 全文档重测——这正是
//     "展开/收起卡顿"与当年长文本卡顿同代价形状的根因。
void HarnessApp::ToggleTurn(size_t foldIdx) {
    if (!m_sess || foldIdx >= m_sess->timeline.size()) return;
    if (m_sess->timeline[foldIdx].kind != TimelineEntry::Kind::Fold) return;

    // 往回找这条摘要条所属的回合（紧邻的上一条 `user` 条目）。
    size_t owner = static_cast<size_t>(-1);
    for (size_t k = foldIdx; k-- > 0;) {
        if (IsUserEntry(m_sess->timeline[k])) {
            owner = k;
            break;
        }
    }
    if (owner == static_cast<size_t>(-1)) return;

    // ★ 翻的是**折叠态**，不是可见性。可见性（`foldHidden` / 控件 SetVisible）
    //   一律由 `RetagTurns` + `ApplyFoldVisibility` 从折叠态重算——否则"翻标志 +
    //   改控件"两处各记一份，重建一次就会互相覆盖（同 0c 的展开态缺陷）。
    m_sess->timeline[owner].turnCollapsed = !m_sess->timeline[owner].turnCollapsed;

    // ★ 结构稳定性判据：`timeline` 上只有 `Kind::Fold` 条目会被 ① erase / ⑤ insert，
    //   其余条目绝不动位 ⇒ 只要**摘要条的位置序列**重算前后一致，所有下标
    //   （`rows[i].timelineIdx`、按钮回调捕获的 `foldIdx`）就都仍然成立，快路径合法。
    std::vector<size_t> slotsBefore;
    for (size_t k = 0; k < m_sess->timeline.size(); ++k) {
        if (m_sess->timeline[k].kind == TimelineEntry::Kind::Fold) slotsBefore.push_back(k);
    }
    RetagTurns();   // 返回值（可见性变没变）不用：点击必然翻可见性
    std::vector<size_t> slotsAfter;
    for (size_t k = 0; k < m_sess->timeline.size(); ++k) {
        if (m_sess->timeline[k].kind == TimelineEntry::Kind::Fold) slotsAfter.push_back(k);
    }
    if (slotsBefore != slotsAfter) {
        // 兜底（正常流到不了）：重算移动了摘要条 ⇒ 下标失效，只有整表重建能让
        // `rows[i].timelineIdx == i` 重新成立。慢，但正确性优先于快。
        RebuildTranscriptView();
        m_relayoutPulse = 12;
        RequestRepaint();
        return;
    }

    ApplyFoldVisibility();   // 唯一可见性同步点：`foldHidden` → 控件 SetVisible
    // 摘要条文案重贴：箭头 ›⇄▾ 由 `RetagTurns` 重算进 `foldSummary`（单一真相源
    // 在模型，控件只是投影）。未变的条重贴同文，代价是单行 SetText。
    for (RowRec& r : m_sess->rows) {
        if (r.foldBar && r.headerBtn && r.timelineIdx < m_sess->timeline.size()) {
            r.headerBtn->SetText(m_sess->timeline[r.timelineIdx].foldSummary);
        }
    }
    m_relayoutPulse = 12;   // ~400ms：覆盖内容高度跳变
    Relayout();             // 立即重排（同 ToggleRow：settle 后定时器路径不经过 Relayout）
    RequestRepaint();
}

// 把模型的折叠态落到视图上。**唯一的可见性同步点**。
//   ★ 为什么单独一个函数、且**不**放进 `Relayout`：`Relayout` 也被 resize /
//     换主题 / 侧栏开合这些"模型没变"的路径调用；在里面写模型/控件可见性，
//     等于让几何函数变成状态作者（`ToggleRow` 那条注释同款纪律）。
void HarnessApp::ApplyFoldVisibility() {
    if (!m_sess) return;
    for (RowRec& r : m_sess->rows) {
        if (r.timelineIdx >= m_sess->timeline.size()) continue;
        const TimelineEntry& e = m_sess->timeline[r.timelineIdx];
        r.foldable = e.foldable;
        r.foldHidden = e.foldHidden;
        r.foldBar = (e.kind == TimelineEntry::Kind::Fold);
        if (r.panel) r.panel->SetVisible(!e.foldHidden);
    }
}

// ── ask 的结构化提案（协议 v5 §3.6 可选 detail，2026-11-05）────────

// ParseAskDetail：`ask.detail` → AskDetail。**渲染数据，不是不变量对象**：
// 缺失 / 非对象 / `tool` 为空 ⇒ `has=false`（整行走 v4 问句路径——GUI 兼容
// 义务，PROTOCOL §3.6）。已知 `tool` 之外的新值同样回落：渲染器不猜语义。
// ★ 成员函数而非自由函数：AskDetail 是 private 嵌套类型，只有成员摸得到。
HarnessApp::AskDetail HarnessApp::ParseAskDetail(const json::Value& msg) {
    AskDetail d;
    const json::Value* det = msg.Find("detail");
    if (!det || !det->IsObject()) return d;
    d.tool = det->GetString("tool");
    if (d.tool.empty()) return d;
    d.has = true;
    d.path = det->GetString("path");
    d.diff = det->GetString("diff");
    d.replaceAll = det->GetBool("replaceAll", false);
    d.command = det->GetString("command");
    d.cwd = det->GetString("cwd");
    d.note = det->GetString("note");
    d.task = det->GetString("task");
    d.name = det->GetString("name");
    d.timeoutSeconds = static_cast<int>(det->GetNumber("timeoutSeconds", 0.0));
    if (const json::Value* tools = det->Find("tools"); tools && tools->IsArray()) {
        for (const auto& t : tools->array) d.tools.push_back(t.text);
    }
    // mcp（2026-11-06）：server/toolName + argsKv（[{k,v}]，值是 core 文本化好的
    // 紧凑 JSON）。字段缺失 ⇒ 空串/空数组——渲染分支按「（无）」兜底，不回落。
    d.server = det->GetString("server");
    d.toolName = det->GetString("toolName");
    if (const json::Value* kv = det->Find("argsKv"); kv && kv->IsArray()) {
        for (const auto& e : kv->array) {
            if (!e.IsObject()) continue;
            d.argsKv.emplace_back(e.GetString("k"), e.GetString("v"));
        }
    }
    return d;
}

// AppendProposalLines：提案行逐条建进折叠体内容容器（VerticalLayout 纵排）。
//   ★ 行 = 定高 `kProposalLineH` 的 mono Label，**不折行**（裁剪保对齐——diff
//     的缩进就是语义，PROTOCOL §7）。宽在 Relayout 期统一下发（lastWrapW 门闸）。
//   ★ 配色（§7 规格）：`+` = ok 色 · `-` = error 色 · `@@`/`---`/`+++`/`…` = 弱化 ·
//     上下文 = 正文。
//   ★ `task` 全文在这里按 200 码点截断：完整任务在 tool_call 的 args 卡与问句里
//     都有（delegationAskPrompt 带摘要），提案行的职责是"看得清授权了什么"。
namespace {
// 按 \n 切行（末尾换行不产生空尾元素）——提案 diff 与 core 侧 splitLines 同语义。
std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> lines;
    if (s.empty()) return lines;
    size_t start = 0;
    while (true) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}
}   // namespace
void HarnessApp::AppendProposalLines(const AskDetail& d,
                                     ECDI::Widget& content, RowRec::ProposalRec& out) {
    auto addLine = [&content, &out](const std::string& text, const ECDI::Color& color) {
        auto l = std::make_unique<ECDI::Label>(text);
        l->SetTextColor(color);
        l->SetFont(harness::MonoFont(13.0f));
        l->SetWordWrap(false);
        l->SetSize(100, kProposalLineH);   // 宽为占位——Relayout 期按行宽统一下发
        out.lines.push_back(l.get());
        content.AddChild(std::move(l));
    };
    auto clipRunes = [](const std::string& s, size_t n) {
        std::string out;
        size_t cp = 0;
        for (size_t i = 0; i < s.size();) {
            if (cp >= n) {
                out += "…";
                break;
            }
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            out += s.substr(i, len);
            i += len;
            cp++;
        }
        return out;
    };

    if (d.tool == "write_file" || d.tool == "edit_file") {
        for (const auto& line : SplitLines(d.diff)) {
            const ECDI::Color& color = !line.empty() && line[0] == '+' ? kOkDark
                                     : !line.empty() && line[0] == '-' ? kErrorDark
                                     : (!line.empty() && (line[0] == '@' || line[0] == '…')) ||
                                           line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0
                                         ? kDarkMuted
                                         : kDarkInk;
            addLine(line, color);
            if (!line.empty()) {
                ++out.diffLines;   // 空行不建（SplitLines 已去尾空；防御式再判）
                if (line[0] == '+') out.plus++;
                if (line[0] == '-') out.minus++;
            }
        }
        return;
    }
    if (d.tool == "run_command") {
        addLine("命令  " + d.command, kDarkInk);
        addLine("目录  " + d.cwd, kDarkMuted);
        addLine("超时  " + std::to_string(d.timeoutSeconds) + " 秒", kDarkMuted);
        // ★ note 由 core 按换行符分好行（core/cmd/ecdi-core/sandbox.go）：
        //   提案行不折行，单行 1000px 的说明会被 wrapW≈688px 裁掉右 1/3。
        out.kvLines = 3;
        for (const auto& line : SplitLines(d.note)) {
            addLine(line, kDarkMuted);
            ++out.kvLines;
        }
        return;
    }
    if (d.tool == "task") {
        addLine("任务  " + clipRunes(d.task, 200), kDarkInk);
        if (!d.name.empty()) addLine("代理  " + d.name, kDarkMuted);
        std::string tools = "工具  ";
        for (size_t i = 0; i < d.tools.size(); ++i) {
            if (i) tools += " / ";
            tools += d.tools[i];
        }
        if (d.tools.empty()) tools += "（无）";
        addLine(tools, kDarkMuted);
        addLine("预算  " + std::to_string(d.timeoutSeconds) + " 秒（轮数见问句）", kDarkMuted);
        out.kvLines = 3 + (d.name.empty() ? 0 : 1);
        return;
    }
    if (d.tool == "mcp") {
        // MCP 工具调用（2026-11-06）：真实工具名 + 参数键值行——授权的对象是
        // 「某服务的某个工具」，参数才是决策依据，逐键一行（core 已排好序）。
        addLine("服务  " + d.server, kDarkMuted);
        addLine("工具  " + d.toolName, kDarkInk);
        out.kvLines = 2;
        if (d.argsKv.empty()) {
            addLine("参数  （无）", kDarkMuted);
            ++out.kvLines;
            return;
        }
        for (const auto& [k, v] : d.argsKv) {
            addLine("参数  " + k + " = " + v, kDarkInk);
            ++out.kvLines;
        }
        return;
    }
    // 未知 tool：has=true 但不认识——发一行中性说明，不猜语义（§3.6 回落义务）。
    addLine("（提案类型 " + d.tool + "）", kDarkMuted);
    out.kvLines = 1;
}

// 权限门行：**纯构造**（重建可重放）。问句与 id 都来自模型条目，不是来自"刚到的消息"
//   ——重建时那条消息早已不存在，若从消息取就等于"重建不出问句"。
// ★★ 2026-10-27：长问句改折叠卡（实测反馈「权限门遇上长参数十分混乱」）。
//   病因：问句原来是一条**不换行的 `Label`**——`Relayout` 里 `AutoSize()` 取单行
//   extent，两个按钮又绝对定位在 `innerW-160` / `innerW-82` ⇒ 问句一长就穿过按钮、
//   冲出卡片右边界、再被 ScrollView 裁掉。三个病灶叠在同一条不换行的文本上。
//   修法借思维链那一整套（`CollapsiblePanel` + header 按钮 + 折叠体），零新机制；
//   但两处**刻意不同**（理由见 AppInternal.h 的 AskTitle 注释）：
//     · **默认展开**——权限门是"必须决策"，收起等于把决策依据藏起来；
//     · 两个按钮留在 header 行、**不进折叠体**——`--answer-probe` 靠控件的绝对中心点
//       合成点击（`ProbeTranscript.cpp` 的 `clickCentreOf`），按钮一旦随折叠体隐藏就必红，
//       真用户也可能收起后找不到「允许」。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildAskRow(const TimelineEntry& entry, RowRec& rec) {
    // ★★ 2026-10-27：**去卡片化**（与工具行同一刀）。原来这里是"暖色底 + 圆角 +
    //   1px 描边"的卡片；现在与工具行/正文一样平铺在画布上。
    //   ★ 权限门凭什么还"跳出来"？靠三件**非卡片**的东西，它们才是决策信号：
    //     ① 暖色问句文字（`kAskQInk`，整份转录里只有它和错误行不是灰/黑）；
    //     ② 两个**带底色的按钮**（允许 = 暖黄实心，拒绝 = 描边）——控件可以留圆角，
    //        它们是按钮不是卡片；
    //     ③ 折叠体里是完整的原始问句（长参数），不再被单行裁掉。
    //   ★ 刻意**不**保留一个"淡淡的底"：半吊子的底色会同时失去卡片的边界感和
    //     文档的干净，实测观感最差。要回退成卡片只需把下面三行换回
    //     `kAskBg / kRadiusTool / 1px kAskBorder`（token 仍在 Theme.h 里）。
    auto panel = std::make_unique<ECDI::Panel>();
    const ECDI::Color askPlainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
    panel->SetStyle(ECDI::PanelStyleOverride{
        .background = askPlainBg, .cornerRadius = 0.0f,
        .borderWidth = 0.0f, .borderColor = askPlainBg});
    rec.ask = true;
    rec.askId = entry.id;   // P1.5：answer 回执要带 id

    // 短问句 ⇒ **不建折叠体**：此时摘要即全文，建了就是把同一句话显示第二遍。
    //   ★ 判据是纯列宽估算（`AskNeedsFold`），**零测量**——权限门可能在流式路径上
    //     高频重建，任何 `MeasureText` 都会进 `m_evalMeasureCalls`（评估仪器口径）。
    //   ★ 这也保证了"只有一套几何"：`bodyH` 为 0 时 `Relayout` 那条折叠分支自然退化，
    //     不需要"短问句走另一支"的第二套摆位。
    //   ★★ v5（2026-11-05）：带 `detail` ⇒ **恒建折叠体**——提案卡的存在意义就是
    //     让 diff/作用域可审阅；"短问句不需要折叠体"对它不成立（问句只是摘要行）。
    const bool foldable = entry.askDetail.has || AskNeedsFold(entry.text);
    rec.expanded = entry.expanded;   // 展开态**只认模型**（默认值由 CreateAskRow 定）

    auto header = std::make_unique<ECDI::Button>(AskTitle(entry.text, rec.expanded, foldable));
    rec.headerBtn = header.get();
    header->SetTextColor(kAskQInk);
    header->SetFont(harness::SansFont(15.0f));
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = askPlainBg,
        .border = askPlainBg,
        .borderWidth = 0.0f,
        .cornerRadius = kRadiusBtn,
        // ★ 去卡片化后**保留** hover/pressed 的暖色（与工具行不同）：
        //   权限门是"必须决策"的阻断点，hover 一亮等于再说一次"这里要你点"；
        //   而工具行的点击反馈是展开体出现，不需要靠变色。
        //   借用同色系的 `kAskBorder`：换成 `kPanel2` 那种冷灰，hover 一瞬整行
        //   会从暖色跳成冷色——色相跳比"没有 hover"更刺眼。
        .pressedBackground = kAskBorder,
        .hoverBackground = kAskBorder,
        .textAlignment = ECDI::TextAlignment::Left});
    // ★ 短问句：没有折叠体可开合 ⇒ 头**不可点**。否则它是个「按下去有反馈、
    //   松开什么都不发生」的假按钮——比不可点更让人困惑。
    //   （`Button` 不画 disabled 态，所以视觉与可点态一致，只是退出命中。）
    if (!foldable) header->SetEnabled(false);
    panel->AddChild(std::move(header));

    if (foldable) {
        auto body = std::make_unique<ECDI::CollapsiblePanel>();
        rec.body = body.get();
        body->SetExpandDirection(ECDI::ExpandDirection::Down);
        // ★ 同工具行：折叠体自带主题默认底，不抹平就会在平铺的转录里露一块异色。
        body->SetStyle(ECDI::PanelStyleOverride{
            .background = askPlainBg,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = askPlainBg});
        auto label = std::make_unique<ECDI::Label>("权限 " + entry.text);
        rec.label = label.get();
        label->SetTextColor(kAskQInk);
        label->SetFont(harness::SansFont(15.0f, 400));   // ★ 问句是折叠长文（正文档 400），头仍是 UI 700
        label->SetWordWrap(true);   // ★ 换行是本轮改造的核心：原来这条路径上完全没有
        // 内容容器（裸 Widget，不画底），与工具卡/思维链同一挂法
        body->GetContent()->AddChild(std::move(label));
        // ★★ v5（2026-11-05）：`detail` 在场 ⇒ 问句行下面接提案行（diff / 键值）。
        //   内容容器挂 VerticalLayout 让 N 行自动纵排（Widget::SetLayout 在基类面）；
        //   行高定死 `kProposalLineH`（mono 不折行，裁剪保对齐——diff 的缩进是语义）。
        //   行数与 +/- 统计**建行期**定死进 `rec.proposal`（Relayout 只做乘法）。
        if (entry.askDetail.has) {
            ECDI::Widget* content = body->GetContent();
            content->SetLayout(std::make_unique<ECDI::VerticalLayout>(0, false, 0));
            AppendProposalLines(entry.askDetail, *content, rec.proposal);
        }
        body->SetExpanded(rec.expanded);   // 未挂树时瞬时生效（无动画）
        panel->AddChild(std::move(body));
    }

    auto allow = std::make_unique<ECDI::Button>("允许");
    rec.allowBtn = allow.get();
    allow->SetTextColor(kWarnInk);
    {
        ECDI::Font f = harness::SansFont(14.0f);
        allow->SetFont(f);
    }
    allow->SetStyle(ECDI::ButtonStyleOverride{
        .background = kWarn,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kWarn,
        .hoverBackground = kAllowHover});
    panel->AddChild(std::move(allow));

    auto deny = std::make_unique<ECDI::Button>("拒绝");
    rec.denyBtn = deny.get();
    deny->SetTextColor(kDenyInk);
    {
        ECDI::Font f = harness::SansFont(14.0f);
        deny->SetFont(f);
    }
    deny->SetStyle(ECDI::ButtonStyleOverride{
        .background = kBtnDenyBg,
        .border = kAskBorder,
        .borderWidth = 1.0f,
        .cornerRadius = kRadiusBtn,
        .hoverBackground = kDenyHover});
    panel->AddChild(std::move(deny));

    // ★★ P4 Phase D：已答结论行（预建、隐藏；ApplyAnsweredLook 填充并显示）。
    auto concl = std::make_unique<ECDI::Label>();
    rec.conclusion = concl.get();
    concl->SetVisible(false);
    concl->SetTextColor(kOkDark);
    concl->SetFont(harness::SansFont(14.0f));
    panel->AddChild(std::move(concl));
    return panel;
}

void HarnessApp::CreateAskRow(const json::Value& msg) {
    // ★★ 0c：问句进模型。此前它只存在于这条 json 消息里 ⇒ 任何重建都会**
    //    把权限门变成一问句都没有的空卡**（按钮还在，但不知道在问什么）。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Ask;
    entry.id = msg.GetString("id");
    entry.text = msg.GetString("question");
    // ★★ v5（2026-11-05）：结构化提案进模型（重建同构的前提，见 TimelineEntry 注释）。
    entry.askDetail = ParseAskDetail(msg);
    // ★★ 2026-10-27：默认展开态**在这里**定（不放在 BuildAskRow 里推导）——
    //   重建路径走 `BuildAskRow(entry, rec)` 时只认 `entry.expanded`，若两处各自
    //   推一次"默认值"，模型与视图就会在建行那一瞬不一致：重建探针会读到
    //   `expanded` 从 1 变 0（= 收起了），而屏幕上其实先展开后收起。
    //   ★ 短问句 `AskNeedsFold == false` ⇒ 恒收起，且 BuildAskRow 干脆不建折叠体。
    //   ★ v5：带 detail ⇒ 默认**展开**（审批时刻必须看得见 diff——PROTOCOL §7）。
    entry.expanded = entry.askDetail.has || AskNeedsFold(entry.text);
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildAskRow(m_sess->timeline.back(), rec);
    AddRow(std::move(panel), rec, entry.id, m_sess->timeline.size() - 1);

    RowRec& stored = m_sess->rows.back();
    stored.allowBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "allow"); });
    stored.denyBtn->SetOnClick([this, &stored] { MarkAnswered(stored, "deny"); });
    // ★★ 2026-10-27：折叠头与工具卡/思维链走**同一条**绑定路径（`BindCollapsible`
    //   → `ToggleRow`）。若在这里另写一份 lambda，就成了第二份"切换实现"——
    //   它不会写回模型、不会设 `m_relayoutPulse`，表现为"展开了但行高没变、重建即回退"。
    if (stored.body && stored.headerBtn) BindCollapsible(stored);

    if (!m_options.autoAnswer.empty()) {
        MarkAnswered(stored, m_options.autoAnswer);   // 无人值守：立即回答（协议 §8.3）
    } else if (m_options.timed) {
        m_sess->waitForAnswer = true;   // timed 回放停在 waiting，直到用户点选
    }
}

void HarnessApp::MaterializePendingPrompts() {
    if (!m_content || !m_sess) return;
    // Transient rows are intentionally rebuilt rather than inserted into canonical row/timeline state.
    for (const PendingPromptView& view : m_pendingPromptViews) {
        if (view.row.panel) m_content->RemoveChild(view.row.panel);
    }
    m_pendingPromptViews.clear();

    for (const PendingPrompt& pending : m_sess->pendingPrompts) {
        auto panel = std::make_unique<ECDI::Panel>();
        RowRec row;
        row.alignRight = true;
        row.label = nullptr;
        std::string display;
        // ★★ 协议 §7（原文）：待确认气泡「与已确认 `user` 气泡**同位置同形状**，只多一个
        //   弱化后缀 `· 发送中`」；失败态「后缀改 `· 未确认`，描边转为错误色」。
        //   ★ 2026-10-20 修正：此前是**换行**追加「未确认 · 等待 core 接受」/「未确认：<原因>」，
        //     那会改变气泡高度与形状（违反「同形状」），文案也不是规范里的那两个后缀；
        //     listlayout 又自创了 `[未确认] ` / `[失败] ` 前缀。现在一律只用同行后缀。
        display = pending.text;
        display += pending.status == "sending" ? " · 发送中" : " · 未确认";
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = Pick(m_options.probeContrast, kUserBg, kProbeBg),
            .cornerRadius = kRadiusCard,
            .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
            // ★ 协议 §7：失败态「描边转为错误色」；发送中仍用 user 气泡的常规描边
            //   （“同形状”也包括边框，只有颜色在失败时换）。
            .borderColor = pending.status == "sending" ? kUserBorder : kErrorDark});
        auto label = std::make_unique<ECDI::Label>(display);
        row.label = label.get();
        label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        label->SetFont(harness::SansFont(harness::kFontBody, 400));
        panel->AddChild(std::move(label));
        row.panel = panel.get();
        if (m_options.rowModeList) row.panel->SetSize(ContentWidth(m_scroll), kListRowHeight);
        m_content->AddChild(std::move(panel));
        PendingPromptView view;
        view.clientId = pending.clientId;
        view.row = row;
        m_pendingPromptViews.push_back(std::move(view));
    }
    // ★★ P4 C：pending 视图增删后刷新空状态引导（AddRow 尾部也经此处 ⇒
    //   所有「行到达」路径一并覆盖——任何行/pending 出现即拆引导卡）。
    RefreshEmptyGuide();
}

// ★★ P4 Phase D：本地时刻 "HH:MM"（answerAt 用——审计信息，分钟粒度够用）。
static std::string NowHm() {
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    if (localtime_s(&tmv, &now) != 0) return std::string();
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}

void HarnessApp::MarkAnswered(RowRec& rec, const std::string& choice) {
    if (rec.answered) return;
    rec.answered = true;
    // ★ 0c：已答进模型（重建时回填成"置灰"而不是"可再答一次"）
    // ★★ Step 3：**答的是什么**也进模型。0c 只存了 `answered`（bool），于是任何
    //   重建之后"当时允许还是拒绝"就丢了——两个钮都置灰，读者无从判断。
    //   权限门是**授权决定**，不是一个"已处理"标记：丢掉结论等于把历史抹成空白。
    if (rec.timelineIdx < m_sess->timeline.size()) {
        m_sess->timeline[rec.timelineIdx].answered = true;
        m_sess->timeline[rec.timelineIdx].answer = choice;
        m_sess->timeline[rec.timelineIdx].answerAt = NowHm();
    }
    rec.answer = choice;
    // ★ 视觉走**同一条**路径（见 `ApplyAnsweredLook` 的注释：首答与重建必须同形，
    //   否则"刚答完"和"重建后"长得不同，而 A==B 看不见配色差异）。
    ApplyAnsweredLook(rec, choice,
                      rec.timelineIdx < m_sess->timeline.size()
                          ? m_sess->timeline[rec.timelineIdx].answerAt
                          : std::string());
    m_sess->waitForAnswer = false;
    if (m_sess->writer) {
        // live：真发 answer（协议 §4：需 id + choice；I3 的解除信号）
        if (!rec.askId.empty()) {
            m_sess->writer->Answer(rec.askId, choice);
        }
    }
    // ★★ 2026-10-27：问句文本的来源从**控件**改成**模型**。权限门改造后，短问句
    //   不再建折叠体 ⇒ `rec.label` 为 null（旧写法会往权限历史里塞一串空字符串）；
    //   长问句的 label 在折叠体里，拿它当"标题"语义也不对。模型里的 `text` 才是
    //   原始问句，两条路径都能取到，且不带重复的「权限 」前缀（卡片标题已经叫权限历史）。
    std::string asked = rec.label ? rec.label->GetText() : std::string();
    if (rec.timelineIdx < m_sess->timeline.size()) {
        asked = m_sess->timeline[rec.timelineIdx].text;
    }
    m_sess->askHist.emplace_back(choice, asked);
    if (m_sess->askHist.size() > 16) m_sess->askHist.pop_front();
    // ★★ Step 5：把结论落到 sidecar（门槛见 `CanSaveUiState`——`--replay` 跑批基线
    //   仍排除，写它等于让"再跑一次"与"第一次跑"不同；live 转正会话 2026-11-07 放开）。
    if (CanSaveUiState()) SaveUiState(*m_sess);
    m_hudDirty = true;
    AddNoticeRow("[answer] " + choice, kDarkMuted);
    RequestRepaint();
}

// ★★ Step 3：已答权限门的视觉。**首答与重建共用**（这是本函数存在的唯一理由）。
//
// ★★ P4 Phase D（2026-10-25）改版：两钮**隐藏** + 行内结论标记
//   （`● 已允许 HH:MM` 绿 / `● 已拒绝 HH:MM` 红）。
//   · 原「选中者留色、另一个灰化」只把结论编码在配色里（审计事实读不出来），
//     且 ECDI Button 不画 disabled 态——两个死按钮留在行里占位却不可点。
//   · 结论标记是文本 ⇒ 探针可从 `conclusion->GetText()` **独立于模型**读回
//     （原手法读两钮文字色，钮一隐藏即失效——本版判据反而更直接）。
//   · `when` 为空 = 旧 sidecar 重建（无时刻记录）——如实省略时刻段，不编造。
//   · 未答/重建未答：conclusion 隐藏、两钮可见可点（幂等）。
void HarnessApp::ApplyAnsweredLook(RowRec& rec, const std::string& choice,
                                   const std::string& when) {
    if (rec.allowBtn) {
        rec.allowBtn->SetEnabled(false);   // 双保险：不可点（命中早退）且不可见
        rec.allowBtn->SetVisible(false);
    }
    if (rec.denyBtn) {
        rec.denyBtn->SetEnabled(false);
        rec.denyBtn->SetVisible(false);
    }
    if (!rec.conclusion) return;
    // 记下视图侧结论（`SnapshotRows` 只读 RowRec，不回查模型——见字段注释）
    rec.answer = choice;
    const bool allowPicked = (choice == "allow");
    const bool denyPicked = (choice == "deny");
    const std::string whenSeg = when.empty() ? std::string() : (" " + when);
    if (allowPicked) {
        rec.conclusion->SetTextColor(kOkDark);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8" + whenSeg);
    } else if (denyPicked) {
        rec.conclusion->SetTextColor(kErrorDark);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE6\x8B\x92\xE7\xBB\x9D" + whenSeg);
    } else {
        // 未知 choice（协议外的值）：如实呈现「答了但答案不认识」，不编造正常态。
        rec.conclusion->SetTextColor(kDarkMuted);
        rec.conclusion->SetText("\xE2\x97\x8F \xE5\xB7\xB2\xE5\x9B\x9E\xE7\xAD\x94\xEF\xBC\x88\xE6\x9C\xAA\xE7\x9F\xA5\xE7\xBB\x93\xE8\xAE\xBA\xEF\xBC\x89" + whenSeg);
    }
    rec.conclusion->SetVisible(true);
}

// 通知行：**纯构造**（重建可重放）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildNoticeRow(const std::string& text,
                                                        const ECDI::Color& color, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    auto label = std::make_unique<ECDI::Label>(text);
    rec.label = label.get();
    label->SetTextColor(color);
    label->SetFont(harness::SansFont(harness::kFontSmall));   // 统一中文 face（原先落 SimSun）
    panel->AddChild(std::move(label));
    return panel;
}

// ── ★★ 2026-11-06：回合摘要条 ───────────────────────────────────────────
// 一条摘要条取代了这个回合原本**第一条过程行**的位置：一行可点的标题，写着
// "› 已完成 · 5 步 · 2.3s"。点击 = `ToggleTurn`（展开回整段）。
//   ★ 为什么做成 `TipButton` 而不是 `Label`：它必须可点、有 hover/pressed 反馈。
//     用 `Label` + 手写命中测试会变成第二个"可点控件"实现（本仓禁止）。
//   ★ 为什么对齐方式跟工具行标题一样是 Left：摘要条与它取代的那条工具行标题
//     在同一个槽位上下切换，两者左边界必须一致，否则一点开就"跳一下"。
//   ★ 为什么不用 `CollapsiblePanel`：折叠行没有"正文"，展开是把**别的行**放出来，
//     而不是把本行的 body 撑开。用 CollapsiblePanel 会让本行凭空多出一个空 body。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildFoldRow(const TimelineEntry& entry, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStyle(ECDI::PanelStyleOverride{ .background = ECDI::Color(0, 0, 0, 0) });

    const std::string text = entry.foldSummary.empty()
                                 ? std::string("\xE2\x80\xBA \xE5\xB7\xB2\xE5\xAE\x8C\xE6\x88\x90")
                                 : entry.foldSummary;
    auto header = std::make_unique<uikit::TipButton>(
        text, std::string("点击展开这一回合"),
        [this](uikit::TipButton* b) { ShowTooltipFor(b, b->tip()); },
        [this](uikit::TipButton*) { HideTooltip(); });
    rec.headerBtn = header.get();
    header->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    header->SetFont(harness::SansFont(harness::kFontSmall));
    // ★ 透明底 + 左对齐：摘要条与它取代的那条工具行标题同槽位上下切换，
    //   底色或左边界一有差别，点开就会"跳一下"。
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = ECDI::Color(0, 0, 0, 0),
        .border = ECDI::Color(0, 0, 0, 0),
        .borderWidth = 0.0f,
        .cornerRadius = 0.0f,
        .pressedBackground = Pick(m_options.probeContrast, kToolHeaderPressed, kProbeBg),
        .hoverBackground = Pick(m_options.probeContrast, kToolHeaderHover, kProbeBg),
        .textAlignment = ECDI::TextAlignment::Left});
    panel->AddChild(std::move(header));
    return panel;
}

// ── ★★ P4 Phase C（2026-10-25）：空状态引导 ──────────────────────────
// 修的死胡同：首启（firstRun）关掉设置面板后，转录区一行都没有、屏幕全空。
// 空转录 = 「引导卡」的展示条件：一句话 + 出口（live 会话给建议指令、无 core
// 给「打开设置」）。任何行（含 notice）/pending 气泡到达即拆——回放/Ctrl+N 的
// 空会话带 notice，天然不触发（方案 §5 的「回放/harness 场景不弹」由此成立）。
bool HarnessApp::TranscriptIsEmpty() const {
    return m_sess && m_sess->rows.empty() && m_pendingPromptViews.empty();
}

void HarnessApp::RefreshEmptyGuide() {
    if (!m_content || !m_sess) return;
    if (!TranscriptIsEmpty()) {
        if (m_hud.emptyGuide) {
            m_content->RemoveChild(m_hud.emptyGuide);   // 返回所有权，丢弃即析构
            m_hud.emptyGuide = nullptr;
            // ★ 不 RequestRepaint：拆除发生在「行/pending 到达」的 tick 里，
            //   调用方（relayoutPulse / Relayout 链）必然带着重绘——再请求一次
            //   会让 invalidations 漂 +1（demo 实测 8→10，违反 §0.3 读数纪律）。
        }
        return;
    }
    if (!m_hud.emptyGuide) {
        auto panel = std::make_unique<ECDI::Panel>();
        panel->SetStyle(ECDI::PanelStyleOverride{
            .background = kPanel2,
            .cornerRadius = harness::kRadiusCard,
            .borderWidth = 1.0f,
            .borderColor = kLine});
        panel->SetLayout(std::make_unique<ECDI::VerticalLayout>(8, false, 18));
        auto title = std::make_unique<ECDI::Label>("开始新对话");
        title->SetTextColor(kDarkInk);
        title->SetFont(harness::SansFont(harness::kFontTitle));
        title->SetSize(460, 22);
        panel->AddChild(std::move(title));
        auto mkBtn = [](const std::string& label) {
            auto b = std::make_unique<ECDI::Button>(label);
            b->SetTextColor(kDarkInk);
            b->SetFont(harness::SansFont(harness::kFontSmall));
            b->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = harness::kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            return b;
        };
        // 出口分两形态：live 会话给建议指令（点击 = SendPrompt，pending 气泡
        // 一出现本卡即拆）；无 core（firstRun 空壳 / 回放空会话）给「打开设置」。
        if (m_sess->source && m_sess->source->IsLive()) {
            auto sub = std::make_unique<ECDI::Label>("输入消息，或从下面的建议开始：");
            sub->SetTextColor(kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontSmall));
            sub->SetSize(460, 18);
            panel->AddChild(std::move(sub));
            auto row = std::make_unique<ECDI::Panel>();
            row->SetLayout(std::make_unique<ECDI::HorizontalLayout>(8, false, 0));
            const char* prompts[] = {"介绍这个工作区里有什么", "帮我梳理最近的改动"};
            for (const char* p : prompts) {
                auto b = mkBtn(p);
                b->SetSize(170, 30);
                std::string text = p;
                b->SetOnClick([this, text] { SendPrompt(text); });
                row->AddChild(std::move(b));
            }
            row->SetSize(356, 30);
            panel->AddChild(std::move(row));
        } else {
            auto sub = std::make_unique<ECDI::Label>(
                "还没有连接任何提供商——先在设置里配好 BaseURL 和 API Key。");
            sub->SetTextColor(kDarkMuted);
            sub->SetFont(harness::SansFont(harness::kFontSmall));
            sub->SetSize(460, 18);
            panel->AddChild(std::move(sub));
            auto b = mkBtn("打开设置");
            b->SetSize(110, 30);
            // ★ 2026-10-27（v4 第三步）：走统一入口——这里原来只 `SetSettingsVisible(true)`，
            //   于是从引导卡进面板时字段全空、模型子列表也是空的（两个入口行为不一致）。
            b->SetOnClick([this] { OpenSettingsPanel(); });
            panel->AddChild(std::move(b));
        }
        panel->SetPosition(kCanvasPadX, 0);
        m_hud.emptyGuide = panel.get();
        m_content->AddChild(std::move(panel));
        // ★ 不 RequestRepaint：建卡的路径（首帧 BuildUi / 关设置的
        //   ApplySettingsVisibility→SetVisible 内建 Invalidate / tick 重排链）
        //   都自带整窗重绘——额外请求会污染 invalidations（demo 实测 +1）。
    }
    // 宽度跟随（resize/侧栏切换/设置开合）：空转录时 Relayout 的行循环零迭代，
    // extent 收敛到 0——本卡不进 extent，由这里自己摆。
    // ★★ 2026-10-27：宽走 `TranscriptLayoutWidth()`（版心）而不是 `ContentWidth`
    //   （视口）——检视栏收起时视口宽出 230px，若让这张卡跟着变宽，它就会与正文
    //   列宽不一致、并压到右侧留白区里（切换检视栏时肉眼可见的宽度跳变）。
    const int w = std::max(200, TranscriptLayoutWidth() - 2 * kCanvasPadX);
    // ★★ 2026-11-01（收口「太空」）：垂直居中。此前恒 y=0 ⇒ 卡片贴在中栏顶部，
    //   下面一屏（实测 395px）全是纯背景，读起来像"内容没画完"而不是"空状态"。
    //   ★ 居中位是**相对视口**算的，不是相对内容：空态下内容就是这张卡，
    //     extent 也跟着卡片底边走（见下），所以两者不会打架（不会因此冒竖条）。
    //   ★ 视口高度取 `m_scroll->GetHeight()`：它由中栏那条 VerticalLayout 的
    //     stretch 分配，是本函数此刻唯一可信的"能看见多高"。
    constexpr int kGuideH = 140;
    const int viewH = m_scroll ? m_scroll->GetHeight() : 0;
    const int guideY = std::max(0, (viewH - kGuideH) / 2);
    m_hud.emptyGuide->SetSize(w, kGuideH);
    // 横向与正文列同一左偏移（按行宽居中，两侧留白恒等）
    m_hud.emptyGuide->SetPosition(TranscriptCanvasX(), guideY);
    // ★ 必须显式 Arrange：本卡是浮在 content 里的独立块（不进内容流），
    //   没有父布局会替它摆位——`SetSize` 只改自身矩形，子控件仍停在上一轮的
    //   坐标上（首帧即 (0,0) 全叠在一起）。此前卡片被 extent=0 裁掉、从没真正
    //   上过屏，所以这条一直没暴露。
    m_hud.emptyGuide->Arrange();
    // ★★ 2026-10-25 修复（实测反馈「没有数据的时候上面空一大片」）：
    //   上面那句只摆了卡，但**没给内容节点留高度**——空转录时 `Relayout` 末尾
    //   `SetContentExtent(contentW, max(0, y - kRowGap))` 算出的是 0，内容节点
    //   高 0 ⇒ 卡片虽在树里、`IsVisible()` 也是 true，却被 ScrollView 的视口
    //   整块裁掉，屏幕上什么都看不到（`--empty-probe` 只查可见位，所以一直绿）。
    //   ⇒ 空态下把 extent 抬到卡片底边：这张卡就是此刻唯一的"内容"。
    if (m_scroll) {
        const ECDI::Size ext = m_scroll->GetContentExtent();
        // ★ 2026-11-01：卡片不再贴顶 ⇒ 内容高度必须抬到**卡片底边**（y + h），
        //   否则居中下来的卡片会被视口从底部裁掉一半（此前恒等于卡片高是
        //   因为 y 恒 0，两个量恰好相等，改成居中后就不再相等了）。
        const int needH = guideY + kGuideH;
        if (static_cast<int>(ext.height) < needH) {
            // ★★ 2026-10-27：宽用 `canvasX + w`（内容列右边界），不是 `w + 2×pad`。
            //   后者比真实右边界多出 2×pad − canvasX ⇒ 占满档会超出视口 ⇒ 恒出一条
            //   横向滚动条（空态下最刺眼：整屏没内容却有横条）。
            m_scroll->SetContentExtent(
                std::max(ContentWidth(m_scroll), TranscriptCanvasX() + w), needH);
        }
    }
}

void HarnessApp::AddNoticeRow(const std::string& text, const ECDI::Color& color) {
    // ★★ 0c：通知进模型。此前它**只**是控件 —— 而通知恰好承载全部协议健康信息
    //   （I4/I5 缺口、截断报错、协议 §9 拒连、hello/bye 摘要、发送/回答回执）。
    //   ⇒ 任何重建都会把"这个会话被截断过""协议版本不兼容被拒"这类信息**静默抹掉**，
    //     屏幕看起来只是"少了几行灰字"，而丢掉的是**这次运行是否可信**的判据。
    TimelineEntry entry;
    entry.kind = TimelineEntry::Kind::Notice;
    entry.text = text;
    entry.color = color;
    m_sess->timeline.push_back(entry);

    RowRec rec;
    auto panel = BuildNoticeRow(text, color, rec);
    AddRow(std::move(panel), rec, std::string(), m_sess->timeline.size() - 1);
}


// ── Markdown 表格渲染与控件组装（富内容二期）─────────────────────
// ★★ 2026-11-08（富内容 B+）：装配分支扩到块级全套。
//   ★ 纪律：**每种段种都必须往 `rec.segMeta` 推一条**，且与 `rec.segments`
//     严格同序同长。`Relayout` 靠 index 对齐两者取段间距——少推一条会让后面
//     所有段的间距**整体错位一格**，而且不报错（只是越来越挤或越来越松）。
// ── 段内内联样式接入（富内容 §H，2026-11-09）─────────────────────────────
// ★ 三条正文路径共用这一个函数：复合渲染的段控件（`makeText`）/ 无块级构造时的
//   只读 TextBox / 同位置的 Label。三处各写一份的后果不是编译错，是**静默降级**
//   ——不崩、不报错，只是星号还留在屏幕上。块级全套当年「解析对了但没渲染」
//   就是这一类形态，所以这次把入口收成一处。
// ★ 无行内构造时走原来的 `SetText`（逐字节同旧行为）⇒ 7 份既有夹具的渲染路径不变。
// ★ 行内 code 的颜色取主题强调蓝；`--probe-contrast` 下退化为正文色——那是**对比度
//   量具**，给行内 code 上一种中间亮度只会污染读数，不构成"更清晰的界面"。
void HarnessApp::ApplyInlineText(ECDI::TextWidget& widget, const std::string& text) const {
    InlineCodeStyle codeStyle;
    codeStyle.family = harness::kMonoFamily;
    codeStyle.color = Pick(m_options.probeContrast, kDarkAccent, kProbeInk);

    const InlineRuns runs = ParseInlineRuns(text, codeStyle);
    if (HasInlineStyles(runs)) {
        widget.SetStyledText(runs.text, runs.spans);
        return;
    }
    widget.SetText(text);
}

// 建一个「正文型」文本控件：Paragraph / Heading / ListItem / Quote 共用，
// 只有字体、颜色、底色、缩进不同。★ selectable 开关沿用二期裁决（只读 TextBox
// ⇒ 可选中/复制不需要框架新能力）。
std::unique_ptr<ECDI::Widget> HarnessApp::MakeAssistantTextWidget(const std::string& text,
                                                                  const ECDI::Font& font,
                                                                  const ECDI::Color& color,
                                                                  const ECDI::Color& bg) const {
    if (m_options.selectableRows) {
        auto box = std::make_unique<ECDI::TextBox>();
        box->SetReadOnly(true);
        box->SetTextColor(color);
        box->SetFont(font);
        box->SetStyle(ECDI::TextBoxStyleOverride{
            .background = bg,
            .caretWidth = 0.0f,
            .padding = 0.0f,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f});
        // ★ 2026-11-09（§H）：段内样式在这里接进来。**必须在 SetFont 之后**——
        //   span 只覆盖维度，基字体/基色仍来自控件本身，顺序反了就会有一帧用错基准。
        ApplyInlineText(*box, text);
        return box;
    }
    auto label = std::make_unique<ECDI::Label>();
    label->SetTextColor(color);
    label->SetFont(font);
    ApplyInlineText(*label, text);
    return label;
}

void HarnessApp::BuildAssistantMarkdownWidgets(ECDI::Panel& panel, RowRec& rec,
                                              const std::vector<MarkdownSegment>& segs) {
    const ECDI::Color plainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
    rec.segments.clear();
    rec.tables.clear();
    rec.segMeta.clear();
    rec.label = nullptr;

    auto pushMeta = [&rec](MarkdownSegmentKind k) -> RowRec::SegMeta& {
        rec.segMeta.push_back(RowRec::SegMeta{});
        rec.segMeta.back().kind = k;
        return rec.segMeta.back();
    };

    auto makeText = [&](const std::string& text, const ECDI::Font& font,
                        const ECDI::Color& color) -> std::unique_ptr<ECDI::Widget> {
        return MakeAssistantTextWidget(text, font, color, plainBg);
    };

    for (size_t si = 0; si < segs.size(); ++si) {
        const auto& seg = segs[si];
        if (seg.kind == MarkdownSegmentKind::Paragraph) {
            if (seg.text.empty()) continue;
            auto w = makeText(seg.text, harness::SansFont(harness::kFontBody, 400),
                              Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            // `rec.label` 是 TextWidget*（行高/换行判决要用），makeText 返回 Widget*
            // ⇒ 建树期它必定是 TextWidget 派生（TextBox 或 Label），显式转型。
            if (!rec.label) rec.label = dynamic_cast<ECDI::TextWidget*>(w.get());
            rec.segments.push_back(w.get());
            pushMeta(seg.kind);
            panel.AddChild(std::move(w));
        } else if (seg.kind == MarkdownSegmentKind::Heading) {
            if (seg.text.empty()) continue;
            // 标题：按 level 给字号与字重。★ 正文一律 400（长文累眼），
            //   标题是短文本 ⇒ 走 UI 默认 700，层级靠**字号**拉开。
            const float size = (seg.level <= 1) ? 19.0f : (seg.level == 2 ? 16.0f : 14.0f);
            auto w = makeText(seg.text, harness::SansFont(size, 700),
                              Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            rec.segments.push_back(w.get());
            RowRec::SegMeta& m = pushMeta(seg.kind);
            m.level = seg.level;
            panel.AddChild(std::move(w));
        } else if (seg.kind == MarkdownSegmentKind::CodeBlock) {
            // 围栏代码块：带底色/边框的卡 + 语言标 + 复制钮 + 横向滚动。
            auto cb = BuildCodeBlockWidget(seg);
            if (cb) {
                rec.segments.push_back(cb.get());
                pushMeta(seg.kind);
                panel.AddChild(std::move(cb));
            }
        } else if (seg.kind == MarkdownSegmentKind::ListItem) {
            // 列表项：marker 与正文**分开摆**（悬挂缩进）——一行一个控件，
            //   marker 用固定槽宽右对齐，正文从槽后起排。
            const std::string markerText =
                seg.ordered ? (std::to_string(seg.marker) + ".") : std::string("\xe2\x80\xa2");
            auto w = BuildListItemWidget(markerText, seg.text, seg.depth);
            if (w) {
                rec.segments.push_back(w.get());
                RowRec::SegMeta& m = pushMeta(seg.kind);
                m.depth = seg.depth;
                m.ordered = seg.ordered;
                m.marker = seg.marker;
                // 列表末项：下一段不是列表项 ⇒ 本项是末项（下方间距更大，收束整组）
                const bool nextIsList =
                    (si + 1 < segs.size() && segs[si + 1].kind == MarkdownSegmentKind::ListItem);
                m.lastOfList = !nextIsList;
                panel.AddChild(std::move(w));
            }
        } else if (seg.kind == MarkdownSegmentKind::Quote) {
            // 引用块：左侧 3px 强调竖条 + 缩进正文（同原型）
            auto w = BuildQuoteWidget(seg.text);
            if (w) {
                rec.segments.push_back(w.get());
                pushMeta(seg.kind);
                panel.AddChild(std::move(w));
            }
        } else if (seg.kind == MarkdownSegmentKind::Hr) {
            auto w = BuildHrWidget();
            if (w) {
                rec.segments.push_back(w.get());
                pushMeta(seg.kind);
                panel.AddChild(std::move(w));
            }
        } else if (seg.kind == MarkdownSegmentKind::Table) {
            RowRec::TableRec trec;
            auto tableWidget = BuildTableWidget(seg.table, trec);
            if (tableWidget) {
                rec.tables.push_back(std::move(trec));
                rec.segments.push_back(tableWidget.get());
                pushMeta(seg.kind);
                panel.AddChild(std::move(tableWidget));
            }
        }
    }

    auto copy = std::make_unique<ECDI::Button>("复制");
    rec.copyBtn = copy.get();
    copy->SetVisible(false);   // 由 SyncTurnCopyButtons 统一控制仅在每轮回答末尾显示
    copy->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    copy->SetFont(harness::SansFont(12.0f));
    copy->SetStyle(ECDI::ButtonStyleOverride{
        .background = plainBg,
        .border = plainBg,
        .borderWidth = 0.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    const std::string bid = rec.blockId;
    auto* copyRaw = copy.get();
    copy->SetOnClick([this, bid, copyRaw] { CopyTurnTextByBlockId(bid, copyRaw); });
    panel.AddChild(std::move(copy));
}

// ── 块级全套控件装配（富内容 B+，2026-11-08）─────────────────────────
// ★ 代码块横滚条厚度（4px，与表格同口径——用户 2026-11-01 裁决「全仓条宽统一」）。
//   ★ 定义在**使用处之前**：`kTableBarThickness` 在表格段那边、位置更靠后，
//     直接引用会编译不过（这一点是编译报错教我的，不是我看出来的）。
constexpr int kCodeBarThickness = 4;
// ★ 设计纪律（三条，都写过一次亏）：
//   1. **不做旁路测量**：建树期控件未挂窗，`AutoSize()` 拿不到测量器（preferred 回落 0）。
//      一切宽/高都留给 `Relayout` 在挂窗后定稿（同表格「建树只建、布局定稿」）。
//   2. **行高必须够装下最后一行**：控制高 < 行盒高会裁掉末行 1px
//      （2026-11-01 实际发生过）。下面所有高度都由 `uikit::LineBox` / `AutoSize` 得来，
//      不写死 14/16 这类魔法数。
//   3. **间距逐段给出**：由 `RowRec::SegMeta` 携带，`Relayout` 按段种取值；
//      不在控件里藏 padding（否则同一段在「首段/中间段/末段」三种位置无法区分）。

// 代码块：卡（边框 + 代码底色）+ 顶部条（语言标 + 复制钮）+ 横向滚动代码区。
//   ★ 用户 2026-11-08 裁决：超出版心走**横向滚动 + 复制钮**（不软折行）——
//     代码缩进是语义的一部分，软折行会毁掉对齐结构。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildCodeBlockWidget(const MarkdownSegment& seg) {
    const ECDI::Color codeBg = Pick(m_options.probeContrast, kCardBg, kProbeBg);
    const ECDI::Color lineCol = Pick(m_options.probeContrast, kLine, kProbeInk);

    auto card = std::make_unique<ECDI::Panel>();
    card->SetStyle(ECDI::PanelStyleOverride{
        .background = codeBg,
        .cornerRadius = kRadiusCard,
        .borderWidth = 1.0f,
        .borderColor = lineCol});

    // 顶部条：语言标（左）+ 复制钮（右）
    auto bar = std::make_unique<ECDI::Panel>();
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = codeBg,
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});

    const std::string langText = seg.lang.empty() ? std::string("text") : seg.lang;
    auto langLabel = std::make_unique<ECDI::Label>(langText);
    langLabel->SetFont(harness::SansFont(harness::kFontMicro, 400));
    langLabel->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));

    // 复制钮：★ 复制的是**代码正文**（不含围栏与语言标），否则贴到别处不能直接跑。
    auto copyBtn = std::make_unique<ECDI::Button>("复制");
    copyBtn->SetFont(harness::SansFont(12.0f, 400));
    copyBtn->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    copyBtn->SetStyle(ECDI::ButtonStyleOverride{
        .background = codeBg,
        .border = codeBg,
        .borderWidth = 0.0f,
        .cornerRadius = kRadiusBtn,
        .pressedBackground = kToolHeaderPressed,
        .hoverBackground = kToolHeaderHover});
    const std::string code = seg.text;
    copyBtn->SetOnClick([this, code, copyBtnRaw = copyBtn.get()] {
        if (m_window) {
            m_window->GetPlatformWindow().SetClipboardText(code);
        }
        copyBtnRaw->SetText("已复制");
    });

    bar->AddChild(std::move(langLabel));
    bar->AddChild(std::move(copyBtn));

    // 代码区：横向滚动（超宽可拖），内容按原样保留空白与换行
    //   ★ API 照表格先例：`GetContentView().AddChild` + `SetContentExtent` 显式更新；
    //     滚动条厚度走 `ScrollBarStyleOverride{.thickness}`（ScrollBar 没有 SetThickness）。
    auto scroll = std::make_unique<ECDI::ScrollView>();
    scroll->SetScrollStep(28);
    if (auto* hb = scroll->GetHorizontalScrollBar()) {
        hb->SetStyle(ECDI::ScrollBarStyleOverride{.thickness = kCodeBarThickness});
    }
    if (auto* vb = scroll->GetVerticalScrollBar()) {
        vb->SetStyle(ECDI::ScrollBarStyleOverride{.thickness = kCodeBarThickness});
    }
    auto codeLabel = std::make_unique<ECDI::Label>(code);
    codeLabel->SetFont(harness::MonoFont(harness::kFontMicro));
    codeLabel->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
    scroll->GetContentView().AddChild(std::move(codeLabel));
    // ★ extent 先按「内容自然尺寸」占位，真正的宽由 Relayout 期实测后重下（见下）。
    //   这里给一个非零值，避免 ScrollView 在首次 ApplyLayout 时把内容裁成 0 宽。
    scroll->SetContentExtent(1, 1);

    card->AddChild(std::move(bar));
    card->AddChild(std::move(scroll));
    return card;
}

// 列表项：marker 槽（定宽）+ 正文。悬挂缩进 = 正文左边界越过 marker。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildListItemWidget(const std::string& marker,
                                                             const std::string& text, int depth) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetStyle(ECDI::PanelStyleOverride{
        .background = ECDI::Color(0, 0, 0, 0),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});

    // ★ marker 用 muted 色（原型 `.md-li::marker { color: var(--muted) }`）：
    //   视觉上退到后面，正文才是主体。槽宽定 26 是为了 `10.` 这类两位数不挤压正文。
    auto mk = std::make_unique<ECDI::Label>(marker);
    mk->SetFont(harness::SansFont(harness::kFontBody, 400));
    mk->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));

    const ECDI::Color plainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
    auto body = MakeAssistantTextWidget(
        text, harness::SansFont(harness::kFontBody, 400),
        Pick(m_options.probeContrast, kDarkInk, kProbeInk), plainBg);

    row->AddChild(std::move(mk));
    row->AddChild(std::move(body));
    // ★ depth 不进控件也不进文本：它由 `RowRec::SegMeta::depth` 携带，
    //   Relayout 从 meta 取——避免复制时带出缩进空格，也避免给控件加 tag 字段。
    (void)depth;
    return row;
}

// 引用块：左侧 3px 强调竖条 + 缩进正文（原型：3px bar + 12px 左 padding + muted 文本）
std::unique_ptr<ECDI::Widget> HarnessApp::BuildQuoteWidget(const std::string& text) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetStyle(ECDI::PanelStyleOverride{
        .background = ECDI::Color(0, 0, 0, 0),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});

    auto bar = std::make_unique<ECDI::Panel>();
    bar->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kLine, kProbeInk),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});

    const ECDI::Color plainBg = Pick(m_options.probeContrast, kContentBg, kProbeBg);
    auto body = MakeAssistantTextWidget(
        text, harness::SansFont(harness::kFontBody, 400),
        Pick(m_options.probeContrast, kDarkMuted, kProbeInk), plainBg);

    row->AddChild(std::move(bar));
    row->AddChild(std::move(body));
    return row;
}

// 分隔线：1px 横线，撑满版心
std::unique_ptr<ECDI::Widget> HarnessApp::BuildHrWidget() {
    auto line = std::make_unique<ECDI::Panel>();
    line->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kLine, kProbeInk),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f});
    return line;
}

// ★ 表格滚动条薄化（2026-11-01 用户裁决）：4px = 默认厚度 12 的 1/3。
//   双条都要设（设计裁决：全仓条宽统一，见 `kScrollBarThickness` 的注释）。
//   ★★ 2026-11-01 订正（ECDI `#84` / Phase 43，`daed47c`）：本条原写作「上游
//   `ScrollView::BarThickness()` 读**竖条**厚度做双轴占位计算，只薄横条会留 8px
//   空占位逼出竖条」——那描述的是**上游缺陷**，现已修（两轴各取各的厚度）。
//   ⇒ 现在只薄横条也不会再留幻影占位；两条仍同设，是为了与本仓其余 ScrollView
//   观感一致，不是为了躲 bug。★ 建树期设样式仍然安全：随后由父容器布局的首次
//   `SetSize` 走一遍 `ApplyLayout`（改厚度本身不触发重排）。
constexpr int kTableBarThickness = 4;

std::unique_ptr<ECDI::Widget> HarnessApp::BuildTableWidget(const MarkdownTable& table,
                                                          RowRec::TableRec& out) {
    if (table.headers.empty()) return nullptr;

    const size_t numCols = table.headers.size();
    std::vector<int> colWidths(numCols, 64);
    const int colPadX = 14;
    const int headerH = 34;
    const int rowH = 30;

    auto estimateWidth = [](const std::string& str) -> int {
        int w = 0;
        for (size_t i = 0; i < str.size(); ) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            if (c < 0x80) {
                w += 8;
                i += 1;
            } else if ((c & 0xe0) == 0xc0) {
                w += 14;
                i += 2;
            } else if ((c & 0xf0) == 0xe0) {
                w += 14;
                i += 3;
            } else {
                w += 14;
                i += 4;
            }
        }
        return w;
    };

    const ECDI::Font bodyFont = harness::SansFont(harness::kFontBody, 400);
    InlineCodeStyle inlineCodeStyle;
    inlineCodeStyle.family = harness::kMonoFamily;
    inlineCodeStyle.color = Pick(m_options.probeContrast, kDarkAccent, kProbeInk);
    auto strippedText = [&](const std::string& raw) -> std::string {
        const InlineRuns runs = ParseInlineRuns(raw, inlineCodeStyle);
        return HasInlineStyles(runs) ? runs.text : raw;
    };
    // 建表时 Label 尚未挂窗，AutoSize 拿不到测量器（preferred 回落当前尺寸=0）⇒ 显式实测设尺寸
    auto measureWidth = [&](const std::string& str) -> int {
        if (!m_window) return estimateWidth(str);
        const float w = m_window->GetTextMeasurer().MeasureText(bodyFont, str).width;
        return static_cast<int>(w + 0.999f);
    };
    int lineH = 16;
    if (m_window) {
        const float h = m_window->GetTextMeasurer().MeasureText(bodyFont, "Ag").height;
        lineH = static_cast<int>(h + 0.999f);
    }

    for (size_t c = 0; c < numCols; ++c) {
        int w = measureWidth(strippedText(table.headers[c])) + 2 * colPadX;
        if (w > colWidths[c]) colWidths[c] = w;
    }

    for (const auto& r : table.rows) {
        for (size_t c = 0; c < numCols && c < r.size(); ++c) {
            int w = measureWidth(strippedText(r[c])) + 2 * colPadX;
            if (w > colWidths[c]) colWidths[c] = w;
        }
    }

    int totalTableW = 0;
    for (int w : colWidths) totalTableW += w;
    totalTableW = std::max(120, totalTableW);

    // 建表期几何只是**初值**（单行自然宽）：折行/行高/列线由 LayoutTableInterior 定稿
    const int totalTableH = headerH + static_cast<int>(table.rows.size()) * rowH + 2;

    auto tableCard = std::make_unique<ECDI::Panel>();
    tableCard->SetSize(totalTableW, totalTableH);
    tableCard->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kCardBg, kProbeBg),
        .cornerRadius = 6.0f,
        .borderWidth = m_options.probeContrast ? 0.0f : 1.0f,
        .borderColor = kLine});
    out.card = tableCard.get();

    auto headerBar = std::make_unique<ECDI::Panel>();
    headerBar->SetSize(totalTableW, headerH);
    headerBar->SetPosition(0, 0);
    headerBar->SetStyle(ECDI::PanelStyleOverride{
        .background = Pick(m_options.probeContrast, kPanel2, kProbeBg),
        .cornerRadius = 0.0f,
        .borderWidth = 0.0f,
        .borderColor = kLine});
    out.headerBar = headerBar.get();
    tableCard->AddChild(std::move(headerBar));

    auto lineStyle = []() {
        return ECDI::PanelStyleOverride{
            .background = kLine,
            .cornerRadius = 0.0f,
            .borderWidth = 0.0f,
            .borderColor = kLine};
    };

    out.headerLabels.assign(numCols, nullptr);
    out.cellLabels.assign(table.rows.size(), std::vector<ECDI::Label*>(numCols, nullptr));
    out.zebra.assign(table.rows.size(), nullptr);
    out.rowLines.assign(table.rows.size(), nullptr);
    out.colLines.assign(numCols > 0 ? numCols - 1 : 0, nullptr);
    out.alignments = table.alignments;
    out.naturalW = colWidths;
    out.colPadX = colPadX;
    out.lineH = lineH;

    int curColX = 0;
    for (size_t c = 0; c < numCols; ++c) {
        const int colW = colWidths[c];
        auto hLabel = std::make_unique<ECDI::Label>();
        hLabel->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
        hLabel->SetFont(bodyFont);
        ApplyInlineText(*hLabel, table.headers[c]);
        const int labelW = measureWidth(hLabel->GetText());
        const int labelH = lineH;
        hLabel->SetSize(labelW, labelH);
        int lx = curColX + colPadX;
        const MarkdownAlign align = (c < table.alignments.size()) ? table.alignments[c] : MarkdownAlign::Left;
        if (align == MarkdownAlign::Center) {
            lx = curColX + std::max(0, (colW - labelW) / 2);
        } else if (align == MarkdownAlign::Right) {
            lx = curColX + std::max(0, colW - labelW - colPadX);
        }
        const int ly = (headerH - labelH) / 2;
        hLabel->SetPosition(lx, ly);
        out.headerLabels[c] = hLabel.get();
        tableCard->AddChild(std::move(hLabel));

        curColX += colW;
    }

    auto headerLine = std::make_unique<ECDI::Panel>();
    headerLine->SetSize(totalTableW, 1);
    headerLine->SetPosition(0, headerH);
    headerLine->SetStyle(lineStyle());
    out.headerLine = headerLine.get();
    tableCard->AddChild(std::move(headerLine));

    // T2：全高竖向列线（列界一条 1px Panel，与行高无关 ⇒ 折行后仍连续）
    curColX = 0;
    for (size_t c = 0; c + 1 < numCols; ++c) {
        curColX += colWidths[c];
        auto colLine = std::make_unique<ECDI::Panel>();
        colLine->SetSize(1, totalTableH);
        colLine->SetPosition(curColX, 0);
        colLine->SetStyle(lineStyle());
        out.colLines[c] = colLine.get();
        tableCard->AddChild(std::move(colLine));
    }

    for (size_t r = 0; r < table.rows.size(); ++r) {
        const int curRowY = headerH + 1 + static_cast<int>(r) * rowH;

        if (r % 2 == 1) {
            auto zebra = std::make_unique<ECDI::Panel>();
            zebra->SetSize(totalTableW, rowH);
            zebra->SetPosition(0, curRowY);
            zebra->SetStyle(ECDI::PanelStyleOverride{
                .background = Pick(m_options.probeContrast,
                                   ECDI::Color::FromRGBA8(128, 128, 128, 12),
                                   kProbeBg),
                .cornerRadius = 0.0f,
                .borderWidth = 0.0f,
                .borderColor = kLine});
            out.zebra[r] = zebra.get();
            tableCard->AddChild(std::move(zebra));
        }

        curColX = 0;
        const auto& rowData = table.rows[r];
        for (size_t c = 0; c < numCols; ++c) {
            const int colW = colWidths[c];
            const std::string text = (c < rowData.size()) ? rowData[c] : "";
            auto cLabel = std::make_unique<ECDI::Label>();
            cLabel->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            cLabel->SetFont(bodyFont);
            ApplyInlineText(*cLabel, text);
            const int labelW = measureWidth(cLabel->GetText());
            const int labelH = lineH;
            cLabel->SetSize(labelW, labelH);
            int lx = curColX + colPadX;
            const MarkdownAlign align = (c < table.alignments.size()) ? table.alignments[c] : MarkdownAlign::Left;
            if (align == MarkdownAlign::Center) {
                lx = curColX + std::max(0, (colW - labelW) / 2);
            } else if (align == MarkdownAlign::Right) {
                lx = curColX + std::max(0, colW - labelW - colPadX);
            }
            const int ly = curRowY + (rowH - labelH) / 2;
            cLabel->SetPosition(lx, ly);
            out.cellLabels[r][c] = cLabel.get();
            tableCard->AddChild(std::move(cLabel));

            curColX += colW;
        }

        if (r + 1 < table.rows.size()) {
            auto rowLine = std::make_unique<ECDI::Panel>();
            rowLine->SetSize(totalTableW, 1);
            rowLine->SetPosition(0, curRowY + rowH - 1);
            rowLine->SetStyle(lineStyle());
            out.rowLines[r] = rowLine.get();
            tableCard->AddChild(std::move(rowLine));
        }
    }

    auto sv = std::make_unique<ECDI::ScrollView>();
    sv->SetScrollStep(28);
    if (auto* hsb = sv->GetHorizontalScrollBar()) {
        hsb->SetStyle(ECDI::ScrollBarStyleOverride{.thickness = kTableBarThickness});
    }
    if (auto* vsb = sv->GetVerticalScrollBar()) {
        vsb->SetStyle(ECDI::ScrollBarStyleOverride{.thickness = kTableBarThickness});
    }
    sv->GetContentView().AddChild(std::move(tableCard));
    sv->SetContentExtent(totalTableW, totalTableH);
    out.sv = sv.get();

    return sv;
}

// ★★ T1+T2（2026-11-01）：表格内部布局定稿。建表期不知道版心宽 ⇒ 列宽/折行/行高
//   只能在 Relayout 期算；控件已挂窗 ⇒ wrap 态 `AutoSize` = 「宽钉死、高求解」。
//   列宽策略：注水分配——短列钉自然宽、真长列分余宽、保底 kMinColW 后仍超版心才交横滚。
void HarnessApp::LayoutTableInterior(RowRec::TableRec& t, int availW) {
    if (!t.sv || !t.card || t.naturalW.empty()) return;
    const size_t numCols = t.naturalW.size();

    std::vector<int> colW = t.naturalW;
    long long sum = 0;
    for (int w : colW) sum += w;
    if (sum > availW && availW > 0) {
        // 注水：自然宽放得进人均份额的列钉死自然宽（短列不白白折行），
        // 只剩真长列分余宽；分完仍低于保底才取保底、超出交横滚。
        // ★ 比例缩+统一保底于 2026-11-01 人验否决：保底把短列顶回、偷走长列宽度
        //   ⇒ 总宽反而超版心出横条（样张第 4 列被推出视口）。
        constexpr int kMinColW = 48;
        std::vector<char> fixed(numCols, 0);
        int remaining = availW;
        int unfixed = static_cast<int>(numCols);
        for (;;) {
            const int share = remaining / std::max(1, unfixed);
            bool progressed = false;
            for (size_t c = 0; c < numCols; ++c) {
                if (!fixed[c] && t.naturalW[c] <= share) {
                    fixed[c] = 1;
                    colW[c] = t.naturalW[c];
                    remaining -= t.naturalW[c];
                    --unfixed;
                    progressed = true;
                }
            }
            if (!progressed || unfixed == 0) break;
        }
        if (unfixed > 0) {
            const int share = std::max(kMinColW, remaining / unfixed);
            for (size_t c = 0; c < numCols; ++c) {
                if (!fixed[c]) colW[c] = share;
            }
        }
    }
    int totalW = 0;
    for (int w : colW) totalW += w;
    totalW = std::max(120, totalW);

    std::vector<int> colX(numCols + 1, 0);
    for (size_t c = 0; c < numCols; ++c) colX[c + 1] = colX[c] + colW[c];

    const ECDI::Font bodyFont = harness::SansFont(harness::kFontBody, 400);
    auto measureWidth = [&](const std::string& str) -> int {
        if (!m_window) return 0;
        const float w = m_window->GetTextMeasurer().MeasureText(bodyFont, str).width;
        return static_cast<int>(w + 0.999f);
    };

    auto wrapLabel = [&](ECDI::Label* lb, size_t c) -> int {
        if (!lb) return t.lineH;
        lb->SetWordWrap(true);
        lb->SetSize(std::max(20, colW[c] - 2 * t.colPadX), t.lineH);
        lb->AutoSize();
        return lb->GetHeight();
    };

    int maxHeadH = t.lineH;
    for (size_t c = 0; c < numCols && c < t.headerLabels.size(); ++c) {
        maxHeadH = std::max(maxHeadH, wrapLabel(t.headerLabels[c], c));
    }
    const int headerH = std::max(34, maxHeadH + 2 * t.padY);

    const size_t numRows = t.cellLabels.size();
    std::vector<int> rowH(numRows, 30);
    for (size_t r = 0; r < numRows; ++r) {
        int maxCellH = t.lineH;
        for (size_t c = 0; c < numCols && c < t.cellLabels[r].size(); ++c) {
            maxCellH = std::max(maxCellH, wrapLabel(t.cellLabels[r][c], c));
        }
        rowH[r] = std::max(30, maxCellH + 2 * t.padY);
    }

    int totalH = headerH + 1;
    for (int h : rowH) totalH += h;
    totalH += 1;

    // ECDI Label 无逐行对齐接缝 ⇒ 折行（多行）单元格回落左对齐；单行才按列对齐摆墨宽
    auto placeLabel = [&](ECDI::Label* lb, size_t c, int top, int boxH) {
        if (!lb) return;
        const int innerW = std::max(20, colW[c] - 2 * t.colPadX);
        const int lh = lb->GetHeight();
        int lx = colX[c] + t.colPadX;
        if (lh <= t.lineH) {
            const int ink = std::min(innerW, measureWidth(lb->GetText()));
            const MarkdownAlign align = (c < t.alignments.size()) ? t.alignments[c] : MarkdownAlign::Left;
            if (align == MarkdownAlign::Center) {
                lx = colX[c] + std::max(0, (colW[c] - ink) / 2);
            } else if (align == MarkdownAlign::Right) {
                lx = colX[c] + std::max(0, colW[c] - ink - t.colPadX);
            }
            lb->SetSize(ink, lh);
        } else {
            lb->SetSize(innerW, lh);
        }
        lb->SetPosition(lx, top + (boxH - lh) / 2);
    };

    if (t.headerBar) t.headerBar->SetSize(totalW, headerH);
    if (t.headerLine) {
        t.headerLine->SetSize(totalW, 1);
        t.headerLine->SetPosition(0, headerH);
    }
    for (size_t c = 0; c < numCols && c < t.headerLabels.size(); ++c) {
        placeLabel(t.headerLabels[c], c, 0, headerH);
    }

    int y = headerH + 1;
    for (size_t r = 0; r < numRows; ++r) {
        if (r < t.zebra.size() && t.zebra[r]) {
            t.zebra[r]->SetSize(totalW, rowH[r]);
            t.zebra[r]->SetPosition(0, y);
        }
        for (size_t c = 0; c < numCols && c < t.cellLabels[r].size(); ++c) {
            placeLabel(t.cellLabels[r][c], c, y, rowH[r]);
        }
        if (r < t.rowLines.size() && t.rowLines[r]) {
            t.rowLines[r]->SetSize(totalW, 1);
            t.rowLines[r]->SetPosition(0, y + rowH[r] - 1);
        }
        y += rowH[r];
    }

    for (size_t b = 0; b + 1 < numCols && b < t.colLines.size(); ++b) {
        if (!t.colLines[b]) continue;
        t.colLines[b]->SetSize(1, totalH);
        t.colLines[b]->SetPosition(colX[b + 1], 0);
    }

    t.card->SetSize(totalW, totalH);
    t.sv->SetContentExtent(totalW, totalH);
}

void HarnessApp::UpgradeAssistantRowToMarkdown(RowRec& rec, const Block& block,
                                              const std::vector<MarkdownSegment>& segs) {
    if (!rec.panel) return;
    auto* panel = dynamic_cast<ECDI::Panel*>(rec.panel);
    if (!panel) return;

    while (panel->GetChildCount() > 0) {
        panel->RemoveChild(panel->GetChildAt(0));
    }
    rec.hasMarkdown = true;
    rec.blockId = block.id;
    BuildAssistantMarkdownWidgets(*panel, rec, segs);
}
