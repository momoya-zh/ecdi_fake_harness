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

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    RowRec& ref = m_sess->rows.back();
    m_content->AddChild(std::move(row));
    if (!blockId.empty()) m_sess->blockRows[blockId] = &ref;
    MaterializePendingPrompts();  // keep transient rows physically after canonical rows.
    // settle 后新增的行（发送/停止/回答反馈）没有逐 tick 的 Relayout 可蹭——
    // 排一个补重排脉冲，让定时器把它布局出来
    m_relayoutPulse = std::max(m_relayoutPulse, 2);
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

std::unique_ptr<ECDI::Widget> HarnessApp::BuildBlockRow(const Block& block, RowRec& rec) {
    auto panel = std::make_unique<ECDI::Panel>();

    // ★ 0c：**每一**条块行都登记自己的块 id（此前只有 reasoning/tool 两支登记）。
    //   两个后果，都不是小事：
    //   ① 重建探针靠"有没有 blockId"区分「块行」与「通知行」——chat 气泡没有 id 时
    //      会被当成通知，于是负对照只比到可折叠行（实测 5/19/126/500，而真实错位量
    //      是 0/42/218/1998）⇒ **负对照失去了判别力**，正向全等也就不算证据。
    //   ② 行不知道自己来自哪个块，任何"按块回查"的诊断都只能靠猜。
    rec.blockId = block.id;

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
            box->SetFont(harness::SansFont(harness::kFontBody));
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
        label->SetFont(harness::SansFont(harness::kFontBody));
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
        // ★★ P4 G 尖峰（--selectable-rows）：assistant 正文行用只读 TextBox——
        //   「编辑 no-op 但方向键/选区/Ctrl+A/C 照常」（TextBox.h:50 契约）⇒ 转录
        //   可选中/复制不需要框架新能力。视觉与 Label 版同形：透明底/无边框/
        //   无 caret/同字体同色。默认关闭 ⇒ 基线路径零变化。
        if (m_options.selectableRows) {
            auto box = std::make_unique<ECDI::TextBox>();
            rec.label = box.get();
            box->SetText(block.text);
            box->SetReadOnly(true);
            box->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
            box->SetFont(harness::SansFont(harness::kFontBody));
            box->SetStyle(ECDI::TextBoxStyleOverride{
                .background = plainBg,
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
        label->SetFont(harness::SansFont(harness::kFontBody));   // 同 user 分支：统一中文 face
        if (block.kind == "system") label->SetTextColor(Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
        if (block.kind == "error") label->SetTextColor(Pick(m_options.probeContrast, kErrorDark, kProbeInk));
        panel->AddChild(std::move(label));
        return panel;
    }

    // ★★ 思维链卡（协议 v2 §7，2026-10-14）——**默认收起**，点击标题展开，箭头翻转。
    //   ★ 为何必须有这条**显式分支**：`reasoning` 一旦进入 `IsKnownKind`，
    //     `block.unknownKind` 就是 **false** ⇒ 它会**直接掉到本函数末尾的工具卡分支**，
    //     而那个分支的 else 把标题写死成「等待结果」——**正是 2026-10-13 修掉的那个 bug
    //     换个方式复活**（思维链又一次伪装成"正在等的工具结果"）。加 kind 必须同时加分支。
    //   ★ 复用 `CollapsiblePanel`（与工具卡同机制，不新造控件）：
    //     折叠动画、内容可见性、展开基准全由框架既有能力承担。
    //   ★★ 2026-10-27：思维链改为透明无边框、无悬停变暗、无点击按下变色的纯文本样式。
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

        // 标题 = 单一真相源（箭头两态）
        // ★ 0c：箭头取自 `rec.expanded`（重建时先由条目回填）——否则重建出来的行
        //   一律显示收起态箭头，而控件实际是展开的（读数与屏幕当场打架）。
        auto header = std::make_unique<ECDI::Button>(ReasoningTitle(block, rec.expanded));
        rec.headerBtn = header.get();
        header->SetTextColor(Pick(m_options.probeContrast, kReasoningInk, kProbeInk));
        {
            ECDI::Font f = harness::SansFont(12.0f);
            header->SetFont(f);
        }
        header->SetStyle(ECDI::ButtonStyleOverride{
            .background = plainBg,
            .border = plainBg,
            .borderWidth = 0.0f,
            .cornerRadius = 0.0f,
            .pressedBackground = plainBg,
            .hoverBackground = plainBg,
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
            ECDI::Font f = harness::SansFont(12.0f);
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
        label->SetFont(harness::SansFont(harness::kFontBody));   // 统一中文 face
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
    auto header = std::make_unique<ECDI::Button>(ToolTitle(block, call));
    rec.headerBtn = header.get();
    // 结束且失败 ⇒ 标题转错误色（增量路径在 `block_end` 做，重建在这里做，同一判据）
    header->SetTextColor((block.ended && !block.ok)
                             ? kErrorDark
                             : Pick(m_options.probeContrast, kDarkMuted, kProbeInk));
    {
        ECDI::Font f = harness::SansFont(12.0f);
        header->SetFont(f);
    }
    header->SetStyle(ECDI::ButtonStyleOverride{
        .background = toolPlainBg,
        .border = toolPlainBg,
        .borderWidth = 0.0f,
        .cornerRadius = 0.0f,
        .pressedBackground = toolPlainBg,
        .hoverBackground = toolPlainBg,
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
        pv->SetFont(harness::SansFont(12.0f));
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
    auto label = std::make_unique<ECDI::Label>(bodyText);
    rec.label = label.get();
    label->SetTextColor(Pick(m_options.probeContrast, kDarkInk, kProbeInk));
    label->SetWordWrap(true);   // Phase 29：工具体按 bodyW 断行（JSON/日志长串）
    {
        ECDI::Font f = harness::MonoFont(12.0f);
        label->SetFont(f);
    }
    // Phase 30 批二：CollapsiblePanel 收起态钉住内容可见性（#51④）——
    // 收起溢绘已由框架修复，应用侧 SetVisible workaround 退役。
    rec.body->GetContent()->AddChild(std::move(label));
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
                const Block* call =
                    (b->kind == "tool_result") ? m_sess->transcript.FindBlock(b->forId) : nullptr;
                stored.headerBtn->SetText(ToolTitle(*b, call));
            }
        }
    }
    m_relayoutPulse = 12;   // ~400ms：覆盖 200ms 折叠动画
    // ★★ Step 5：展开态也进 sidecar（同样是"只对历史会话"，理由同 MarkAnswered）。
    if (m_sess && m_sess->originKind == "replay-history") SaveUiState(*m_sess);
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

    // ★ 重建会让内容重新排一遍 ⇒ 不刻意恢复的话视图会跳回顶部。
    //   "换主题/切会话之后回到刚才读到的地方"是**可用性**要求，不是装饰：
    //   转录一长，重排后跳顶等于让人重新找位置。
    const int keepScroll = m_scroll ? m_scroll->GetScrollOffsetY() : 0;

    // ① 先让**指针表**归零，再动控件树：否则 `Relayout()` 会顺着 `rows` 里
    //    已经指向已删控件的裸指针走（悬垂读）。顺序反了就是 UAF，不是风格问题。
    m_sess->rows.clear();
    m_sess->blockRows.clear();
    if (m_content) {
        // `RemoveChild` 返回所有权；逐个摘掉即完成子树销毁（`Widget::RemoveChild`
        // 内部走 `Window::ForgetSubtree`，焦点/捕获/悬停指针一并清掉）
        while (m_content->GetChildCount() > 0) {
            m_content->RemoveChild(m_content->GetChildAt(0));
        }
    }
    // ★★ P4 C：引导卡挂在 m_content 上，随上面的拆树一并析构——指针必须立刻
    //   归零（末尾 Relayout → RefreshEmptyGuide 会按需重建）。
    m_hud.emptyGuide = nullptr;

    // ② 按模型重放（或按调用方给的**错误**次序重放——那正是负对照要的形状）
    if (!orderOverride) {
        MaterializeRows();
    } else {
        // ★ 负对照专用：**只**照块表建块行（不建通知/问句行、不按条目回填状态）
        //   ——这正是"只拿 `Transcript::m_blocks` 重建视图"这个错误实现的样子。
        //   `timelineIdx` 给一个越界哨兵值：它**不**参与视图状态写回
        //   （负对照要复现的是错误实现，不该顺手把状态写坏）。
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

    // ★ 重建后必须自己收尾：重排（几何是新的）再把滚动位置放回去。
    //   让调用方记得补这一步是设计要求里最容易漏的一环，故内置。
    Relayout();
    if (m_scroll) m_scroll->SetContentOffset(0, keepScroll);
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
    const bool foldable = AskNeedsFold(entry.text);
    rec.expanded = entry.expanded;   // 展开态**只认模型**（默认值由 CreateAskRow 定）

    auto header = std::make_unique<ECDI::Button>(AskTitle(entry.text, rec.expanded, foldable));
    rec.headerBtn = header.get();
    header->SetTextColor(kAskQInk);
    header->SetFont(harness::SansFont(13.0f));
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
        label->SetFont(harness::SansFont(13.0f));
        label->SetWordWrap(true);   // ★ 换行是本轮改造的核心：原来这条路径上完全没有
        // 内容容器（裸 Widget，不画底），与工具卡/思维链同一挂法
        body->GetContent()->AddChild(std::move(label));
        body->SetExpanded(rec.expanded);   // 未挂树时瞬时生效（无动画）
        panel->AddChild(std::move(body));
    }

    auto allow = std::make_unique<ECDI::Button>("允许");
    rec.allowBtn = allow.get();
    allow->SetTextColor(kWarnInk);
    {
        ECDI::Font f = harness::SansFont(12.0f);
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
        ECDI::Font f = harness::SansFont(12.0f);
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
    concl->SetFont(harness::SansFont(12.0f));
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
    // ★★ 2026-10-27：默认展开态**在这里**定（不放在 BuildAskRow 里推导）——
    //   重建路径走 `BuildAskRow(entry, rec)` 时只认 `entry.expanded`，若两处各自
    //   推一次"默认值"，模型与视图就会在建行那一瞬不一致：重建探针会读到
    //   `expanded` 从 1 变 0（= 收起了），而屏幕上其实先展开后收起。
    //   ★ 短问句 `AskNeedsFold == false` ⇒ 恒收起，且 BuildAskRow 干脆不建折叠体。
    entry.expanded = AskNeedsFold(entry.text);
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
        label->SetFont(harness::SansFont(harness::kFontBody));
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
    // ★★ Step 5：把结论落到 sidecar（**只有历史打开的会话**才写——启动时 `--replay`
    //   的会话是跑批基线，写它等于让"再跑一次"与"第一次跑"不同）。
    if (m_sess->originKind == "replay-history") SaveUiState(*m_sess);
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
        title->SetSize(400, 20);
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
            sub->SetSize(400, 16);
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
            sub->SetSize(400, 16);
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
    m_hud.emptyGuide->SetSize(w, 128);
    // 横向与正文列同一左偏移（按行宽居中，两侧留白恒等）
    m_hud.emptyGuide->SetPosition(TranscriptCanvasX(), 0);
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
        const int needH = 128;
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
