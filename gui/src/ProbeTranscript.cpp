// ============================================================
// 转录区探针：命中门控 / 思维链折叠 / 整树重建 / 权限答案 / 选中复制 / 删除提供商
// ------------------------------------------------------------
// 判据形态：全部为「A==B」重建比对，且每条都必须带**负对照**（负对照不红 = 探针是瞎的）。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1a）。函数体与切出前**逐字节相同**，
//   门 2（tools/split_verify.py）可比；唯一改动是 include 区与文件位置。
// ============================================================

#include "App.h"
#include "Credentials.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Layout/Layout.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"


void HarnessApp::RunHitProbe() {
    if (!m_window || !m_promptBar) return;
    ECDI::Button* send = m_promptBar->sendButton();
    if (!send) return;

    // 坐标一律用 ECDI 的 `GetAbsolutePosition()`（父链累加，含内容偏移）——
    // ★ 不手算父链、不硬编码偏移（手算必漏某一层；同「不硬编码 DPI/边框」纪律）。
    auto centerOf = [](ECDI::Widget* w, int& x, int& y) {
        const ECDI::Point abs = w->GetAbsolutePosition();
        x = static_cast<int>(abs.x) + w->GetWidth() / 2;
        y = static_cast<int>(abs.y) + w->GetHeight() / 2;
    };
    int cx = 0, cy = 0;
    centerOf(send, cx, cy);

    // ② 负对照点：**输入框中心**——同一时刻、同一段代码路径、且**必命中一个活控件**
    //   （比「移到窗口外」强：窗口外命中 nullptr 无法区分「门控生效」与「探针失效」）。
    int nx = cx, ny = cy;
    ECDI::TextBox* input = m_promptBar->input();
    if (input) centerOf(input, nx, ny);

    m_probeState.hitProbeActive = true;   // 此后 SendPrompt 只计数、零副作用

    // ★ 保存焦点并在探针结束后**复原**：合成的点击会走 `Application::OnMouseButtonDown`
    //   里的 `SetFocusedWidget(...)`，把焦点从输入框挪到按钮（`Button::CanFocus() == true`）
    //   ⇒ `TextBox::OnFocusLost` 会 `StopTimer(kCaretBlinkTimer)` ⇒ **光标闪烁重绘消失**。
    //   实测后果：探针开启时 paints 由 17 掉到 15（确定复现，5/5 次）。这不是本探针要测的
    //   量，属**仪器污染实验**（条 57③）⇒ 必须复原，否则「探针关闭 = 零行为变化」不成立。
    ECDI::Widget* const focusBefore = m_window->GetFocusedWidget();

    auto scene = [&](const char* name, int x, int y) {
        HitProbeRec rec;
        rec.scene = name;
        rec.px = x;
        rec.py = y;
        rec.sendEnabled = send->IsEnabled();
        ECDI::Widget* hit = m_window->GetRootWidget().HitTest(x, y);
        rec.hitIsNull = (hit == nullptr);
        rec.hitIsSend = (hit == send);
        // ★ 禁用时 `HitTest` **不是**返回 nullptr——按钮被跳过，落点回落到祖先
        //   （RootWidget，其 `ContainsPoint` 覆盖整个客户区）⇒ 必须**如实记录落点**，
        //   否则「命中者 != 发送按钮」这句话在读数里看不出"落到了哪"。
        rec.hitIsRoot = (hit == &m_window->GetRootWidget());
        rec.hitConsumes = (hit != nullptr && hit->ConsumesMouseInput());
        rec.hitClass = WidgetKindName(hit);
        // 点击是否达：先取计数 → 合成 Down+Up → 看 SendPrompt 有没有被进入
        const int before = m_probeState.hitProbeSendCalls;
        SynthesizeClickAt(x, y);
        rec.clickReached = (m_probeState.hitProbeSendCalls > before);
        m_probeState.hitProbeLog.push_back(std::move(rec));
    };

    // ① **正对照**：真实状态 + 按钮中心 ⇒ 应「命中即它 ∧ 点击达」
    //    （这一条同时是「探针本身有效」的证明——没有它，④ 的「不达」不可解读，条 81b）
    scene("as-is", cx, cy);
    // ② **负对照**：同一状态 + 输入框中心 ⇒ 必命中 TextBox、但**不得**达 SendPrompt
    scene("off-point", nx, ny);
    // ③④ **单变量对照**：同一点、只翻 enabled
    send->SetEnabled(true);
    scene("forced-enabled", cx, cy);
    send->SetEnabled(false);
    scene("forced-disabled", cx, cy);
    // ★ 复原：把使能位还给 state 驱动的结果（探针不得留下状态）。
    //   下一次 RefreshHud 会按 state 重新驱动，此处只是不留"半路"状态。
    const bool active = (m_sess->coreState == "running" || m_sess->coreState == "waiting");
    send->SetEnabled(!active);
    if (m_promptBar->stopButton()) m_promptBar->stopButton()->SetEnabled(active);

    // ★ 焦点复原（见上：不清干净会改变 caret 闪烁重绘 ⇒ 污染 paints/frames 读数）
    m_window->SetFocusedWidget(focusBefore);

    m_probeState.hitProbeActive = false;
}

// ── ★★ 协议 v2 §7 思维链折叠行为的机读取证（--reasoning-probe）────────────
// 用户口径是**渲染行为**：默认不渲染正文、点标题展开、箭头由朝下变朝上。
// 截图只能证明"某一刻像素长这样"，**不能**证明"点一下真的翻了、再点一下真的回去了"
// ——后者才是"可交互"的定义。故沿用 `--hit-probe` 的同一套纪律：
//   · 合成点击走**真实派发链**（不注入系统输入、不动光标）；
//   · 观测 = 标题文本（箭头+两态文本）、`IsExpanded()`、内容容器 `IsVisible()`、行高。
//
// ★★ 为什么必须是**跨 tick 状态机**（第一版在一个 tick 里做完，读数错了两处）：
//   ① 折叠动画 200ms，而 `SetContentVisible(false)` 在动画的 **onFinished** 里
//      （`CollapsiblePanel.cpp:110-115`）⇒ 点完**立刻**读会读到"内容仍可见"，
//      把正确实现误判成"没折叠"。这是**动画中间态**，不是终态。
//   ② 标题两态（`思考中`/`思考过程`）取决于 `block_end` 是否到达；`--instant` 在同一
//      tick 抽干整个夹具 ⇒ 事后再读**永远**是 `思考过程`，`思考中` 那一态**根本采不到**。
//   ⇒ 分步推进：每次 tick 只做一步，步间等待 > 动画时长；两态标题在**真实事件时刻**抓。
void HarnessApp::RunReasoningProbe() {
    if (!m_window) return;

    RowRec* target = nullptr;
    for (RowRec& r : m_sess->rows) {
        if (r.reasoning && r.headerBtn && r.body) {
            target = &r;
            break;
        }
    }
    if (!target) { m_probeState.reasoningProbeDone = true; return; }

    // 步进节流：给折叠动画（200ms）留足时间，确保读到的是**终态**
    const auto now = std::chrono::steady_clock::now();
    if (m_probeState.reasoningProbeStep > 0 && now < m_probeState.reasoningProbeNextAt) return;
    const auto settle = std::chrono::milliseconds(400);   // > 200ms 动画

    auto snapshot = [&](const char* phase) {
        ReasoningProbeRec rec;
        rec.phase = phase;
        rec.headerText = target->headerBtn->GetText();
        rec.expanded = target->body->IsExpanded();
        rec.contentVisible = target->body->GetContent()->IsVisible();
        rec.panelH = target->panel->GetHeight();
        m_probeState.reasoningProbeLog.push_back(std::move(rec));
    };

    switch (m_probeState.reasoningProbeStep) {
        case 0:
            // ① 默认态：必须**收起**且正文**不可见**（用户口径「默认应当不渲染」）
            snapshot("default");
            m_probeState.reasoningProbeStep = 1;
            m_probeState.reasoningProbeNextAt = now + settle;
            return;
        case 1: {
            // ② 点标题中心一次 —— 走真实 HitTest + 派发链（与 --hit-probe 同源）
            const ECDI::Point abs = target->headerBtn->GetAbsolutePosition();
            const int cx = static_cast<int>(abs.x) + target->headerBtn->GetWidth() / 2;
            const int cy = static_cast<int>(abs.y) + target->headerBtn->GetHeight() / 2;
            SynthesizeClickAt(cx, cy);
            m_probeState.reasoningProbeStep = 2;
            m_probeState.reasoningProbeNextAt = now + settle;
            return;
        }
        case 2:
            // ③ 展开终态：必须 expanded ∧ 内容可见 ∧ 箭头为 ▲
            Relayout();   // 动画结束后补一次布局，让行高落到终态
            snapshot("after-click-1");
            m_probeState.reasoningProbeStep = 3;
            m_probeState.reasoningProbeNextAt = now + settle;
            return;
        case 3: {
            // ④ 再点一次：必须**回到收起**（箭头必须翻回去，不能单向）
            const ECDI::Point abs = target->headerBtn->GetAbsolutePosition();
            const int cx = static_cast<int>(abs.x) + target->headerBtn->GetWidth() / 2;
            const int cy = static_cast<int>(abs.y) + target->headerBtn->GetHeight() / 2;
            SynthesizeClickAt(cx, cy);
            m_probeState.reasoningProbeStep = 4;
            m_probeState.reasoningProbeNextAt = now + settle;
            return;
        }
        case 4:
            Relayout();
            snapshot("after-click-2");
            m_probeState.reasoningProbeStep = 5;
            m_probeState.reasoningProbeDone = true;   // 全部采完 ⇒ 可以退出
            if (m_application) m_application->Exit();
            return;
        default:
            m_probeState.reasoningProbeDone = true;
            return;
    }
}

void HarnessApp::RunRebuildProbe() {
    if (!m_window || !m_sess->replay) return;
    if (m_options.rowModeList) {
        // 固定行高模式下"行高随展开变化"这件事本就不成立，探针无从比较 ⇒ 如实不开跑
        m_probeState.rebuildProbeDone = true;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_probeState.rebuildProbeStep > 0 && now < m_probeState.rebuildProbeNextAt) return;
    const auto settle = std::chrono::milliseconds(400);   // > 200ms 折叠动画

    switch (m_probeState.rebuildProbeStep) {
        case 0: {
            // ① 造**必有**区分力的前置条件：真的展开若干可折叠行、真的答掉若干权限门。
            //   ★ 走 `ToggleRow` / `MarkAnswered`（用户点下去走的**同一条**路径），
            //     不直接改标志：否则探针测的是"我照着我写的方式再写一遍对不对"。
            //   ★ 样本数封顶：每次 ToggleRow 都会 Relayout（O(行数)），scale 夹具 2000 行
            //     若全展开就是 O(n²)。4 个样本已足够判"状态保不保得住"。
            const int kMaxExpandSamples = 4;
            const int kMaxAnswerSamples = 2;
            // ★ 顺手把「展开/收起一次之后标题是否还完整」也留成读数。
            //   为什么必须单独记：0c 里修的那个 bug（工具卡点一下标题被写成空串，
            //   因为回调给非 reasoning 分支传的是 `std::string()`）**不会**让重建比对
            //   失败——重建前后标题都是空的，A == B 照样成立。丢失发生在"点击那一刻"，
            //   所以只有把点击前后的标题都记下来才看得见。
            //   ★★ 取样必须**按种类凑齐**：只取"前 4 个可折叠行"在 cot 上全是思维链行
            //      （夹具里 12 reasoning / 4 tool_call / 4 tool_result，思维链全在前面），
            //      于是刚修的工具卡分支**一个样本都没有**——读数 0 看着像"没问题"，
            //      实际是"没测"。先取工具卡，再补思维链。
            const auto sampleOne = [&](RowRec& r) {
                const std::string beforeT = r.headerBtn->GetText();
                ToggleRow(r, r.reasoning);
                const std::string afterT = r.headerBtn->GetText();
                if (m_probeState.rebuildTitleSample.empty()) {
                    m_probeState.rebuildTitleSample = beforeT;
                    m_probeState.rebuildTitleAfter = afterT;
                }
                if (afterT.empty() && !beforeT.empty()) ++m_probeState.rebuildTitleEmptied;
                ++m_probeState.rebuildExpandedSamples;
            };
            for (RowRec& r : m_sess->rows) {   // 第一轮：工具卡（钉住那个 bug）
                if (m_probeState.rebuildExpandedSamples >= 2) break;
                if (r.tool && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {   // 第二轮：思维链卡
                if (m_probeState.rebuildExpandedSamples >= kMaxExpandSamples) break;
                if (r.reasoning && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {   // 兜底：别的可折叠行
                if (m_probeState.rebuildExpandedSamples >= kMaxExpandSamples) break;
                if (!r.tool && !r.reasoning && r.body && r.headerBtn && !r.expanded) sampleOne(r);
            }
            for (RowRec& r : m_sess->rows) {
                if (m_probeState.rebuildAnsweredSamples >= kMaxAnswerSamples) break;
                if (r.ask && !r.answered) {
                    MarkAnswered(r, "allow");
                    ++m_probeState.rebuildAnsweredSamples;
                }
            }
            // 模型里记下的通知条数（"通知进模型"这件事的承重性证据：
            //   若模型不记通知，重建就会**静默**少这么多行）
            m_probeState.rebuildNoticeCount = 0;
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Notice) ++m_probeState.rebuildNoticeCount;
            }
            // ★ 滚动这一项也要**先造出判别力**：夹具抽干后视图停在顶部
            //   （offset=0，而 scale 的 maxOffset 有 10 万），此时"重建后有没有
            //   跳回顶部"这一问恒真——0 == 0。先把视图真的滚到中间再重建，
            //   读数才有意义（否则又是一次"看着全等其实没测"）。
            if (m_scroll && m_scroll->GetMaxOffsetY() > 200) {
                m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY() / 3);
            }
            m_probeState.rebuildScrollBefore = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
            m_probeState.rebuildProbeBefore = SnapshotRows();
            m_probeState.rebuildProbeStep = 1;
            m_probeState.rebuildProbeNextAt = now + settle;
            return;
        }
        case 1: {
            // ② forward：照 `timeline` 重建 ⇒ 判据 A == B（必须全等）
            RebuildTranscriptView();
            m_probeState.rebuildProbeLog.push_back(
                CompareRows("forward", m_probeState.rebuildProbeBefore, SnapshotRows(), false));
            m_probeState.rebuildProbeStep = 2;
            m_probeState.rebuildProbeNextAt = now + settle;
            return;
        }
        case 2: {
            // ③ 幂等：**再**重建一次，仍须与 A 全等。
            //   这一项专抓"状态只落到控件、没落回模型"：第一次重建屏幕上看着对，
            //   第二次就会塌回收起态，而单看 ② 是看不出来的（② 的 B 是第一次的产物）。
            RebuildTranscriptView();
            m_probeState.rebuildProbeLog.push_back(
                CompareRows("forward-again", m_probeState.rebuildProbeBefore, SnapshotRows(), false));
            m_probeState.rebuildProbeStep = 3;
            m_probeState.rebuildProbeNextAt = now + settle;
            return;
        }
        case 3: {
            // ④ 负对照：**只**按块表、按 id 字典序重建（= 只拿 `Transcript::m_blocks`
            //   遍历这种错误实现的等价物；`std::map` 迭代序就是 `std::less<std::string>`
            //   序，故这里 `std::sort` 出来的次序与之逐元素相同）。
            //   预期：orderMismatches = 字典序错位量（cot 42 / realistic 218 /
            //   scale 1998 / demo 0）。为 0 ⇒ 探针没有判别力，② 的全等不算证据。
            std::vector<std::string> lex = m_sess->transcript.Order();
            std::sort(lex.begin(), lex.end());
            RebuildTranscriptView(&lex);
            m_probeState.rebuildProbeLog.push_back(
                CompareRows("lexicographic", m_probeState.rebuildProbeBefore, SnapshotRows(), true));
            m_probeState.rebuildProbeStep = 4;
            m_probeState.rebuildProbeNextAt = now + settle;
            return;
        }
        case 4: {
            m_probeState.rebuildProbeDone = true;
            if (m_application) m_application->Exit();
            return;
        }
        default:
            m_probeState.rebuildProbeDone = true;
            return;
    }
}

// ★★ Step 3：`answer` 进模型的取证。
//
// 这条探针要回答的问题只有一个：**"当时允许还是拒绝"这件事，重建之后还在不在屏上？**
//   · 改之前：模型只存 `answered`（bool）⇒ 重建后两个钮都灰，读者无从判断结论。
//     A==B 探针也抓不到——它比的是文字与位标志，而第一版的 `MaterializeOne` 连位标志
//     都回填了（`stored.answered = true`），于是"丢结论"在读数上是**空的**。
//     这是 0c `buildTitle` 缺陷的同族：**比对全等，屏幕不一样**。
//
// 判据设计（为什么每条都不可省）：
//   ① 模型侧 `answer` 逐字等于点击的 choice —— 结论真的进了模型；
//   ② 屏幕侧 `answerOnScreen` 从**两个钮的文字色**反推 —— 结论真的到了用户眼前，
//      且这条读数**不查模型**（否则与 ① 同义反复，同 SnapshotRows 的纪律）；
//   ③ forward：重建前后行快照 A==B（含新增的 answer 段）；
//   ④ negative：把模型里的答案改成另一个值再重建，快照比对必须**不**相等，
//      且 firstDivergence 必须指向 `answer` 字段 —— 证明 ③ 的相等不是"两边都没读"；
//   ⑤ 复原：把答案改回去，必须重新相等 —— 证明差异来自那一个自变量（单变量纪律）；
//   ⑥ 未答的权限门：answer 为空、两钮**都还能点** —— 负对照，证明"置灰"只发生在答过之后。
void HarnessApp::RunAnswerProbe() {
    if (!m_sess) { m_probeState.answerProbeDone = true; return; }
    // ★ 只在回放模式：live 下点击「允许」会真的 `writer->Answer()` 发给 core，
    //   探针不该在测量里制造真副作用（仪器污染实验）。
    if (!m_sess->replay) { m_probeState.answerProbeDone = true; return; }

    // 找第一个权限门：**记 timelineIdx 而不是 RowRec***，因为重建会清空 rows，
    // 拿着的引用会立刻悬垂。timelineIdx 是模型侧坐标，重建不动它。
    size_t askTimeline = static_cast<size_t>(-1);
    for (const RowRec& r : m_sess->rows) {
        if (r.ask) { askTimeline = r.timelineIdx; break; }
    }
    if (askTimeline == static_cast<size_t>(-1) || askTimeline >= m_sess->timeline.size()) {
        // 夹具里没有 ask ⇒ 诚实记一条"没有可测的权限门"，别静默跳过
        AnswerProbeRec rec;
        rec.phase = "no-ask";
        rec.firstDivergence = "(fixture has no ask row)";
        rec.matched = false;
        m_probeState.answerProbeLog.push_back(std::move(rec));
        m_probeState.answerProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }

    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // ★★ 视口陷阱（第一版只有 demo 通过，就栽在这里）：夹具里的 ask 可以有很多个
    //   （demo 1 / cot 2 / realistic 11 / scale 62），而转录重放完默认停在**底部**
    //   ⇒ 第一个 ask 行通常在视口**之上**。
    //   而 `Widget::HitTest` 对 ScrollView 这一类做了 `ClipsChildren() && !ContainsRect`
    //   ⇒ 落在裁剪区外的坐标在**子树里**找不到目标，于是一路回落到兄弟节点
    //   （滚动区下方的 composer / 空白）。合成点击**不报错、也没有任何变化**，
    //   读数看上去就是"回调没接上"——一个纯粹由探针位置造成的假缺陷。
    //   demo 的 1 个 ask 恰好落在视口内，所以首轮只有它通过：**单夹具通过不等于判据成立**。
    auto bringIntoView = [&]() {
        if (!m_scroll) return;
        for (RowRec& r : m_sess->rows) {
            if (!r.ask || r.timelineIdx != askTimeline || !r.panel) continue;
            const int y = r.panel->GetY();
            const int h = r.panel->GetHeight();
            const int viewH = m_scroll->GetHeight();
            const int cur = m_scroll->GetScrollOffsetY();
            if (y < cur + 8) {
                m_scroll->SetContentOffset(0, std::max(0, y - 8));
            } else if (y + h > cur + viewH - 8) {
                m_scroll->SetContentOffset(0, std::max(0, y + h - viewH + 8));
            }
            return;
        }
    };
    bringIntoView();

    auto askRow = [&]() -> RowRec* {
        for (RowRec& r : m_sess->rows) {
            if (r.ask && r.timelineIdx == askTimeline) return &r;
        }
        return nullptr;
    };
    auto askRowCount = [&]() {
        size_t n = 0;
        for (const RowRec& r : m_sess->rows) if (r.ask) ++n;
        return n;
    };
    auto answeredCount = [&]() {
        size_t n = 0;
        for (const RowRec& r : m_sess->rows) if (r.ask && r.answered) ++n;
        return n;
    };
    auto modelAnswers = [&]() {
        size_t n = 0;
        for (const auto& e : m_sess->timeline) if (!e.answer.empty()) ++n;
        return n;
    };
    auto modelAnswer = [&]() { return m_sess->timeline[askTimeline].answer; };
    // ★★ P4 D 改版：屏幕侧独立读数改从**结论行文本**反推（原手法读两钮文字色
    //   ——两钮已隐藏，配色不再是屏幕事实；文本反而是更直接的独立读数，
    //   仍不查模型）。未答 = 结论行隐藏、两钮可见。
    auto screenAnswer = [&]() -> std::string {
        const RowRec* r = askRow();
        if (!r) return "unknown";
        const bool buttonsUp = r->allowBtn && r->allowBtn->IsVisible() &&
                               r->denyBtn && r->denyBtn->IsVisible();
        if (buttonsUp && (!r->conclusion || !r->conclusion->IsVisible())) {
            return "";   // 未答：两钮都在、结论行不在
        }
        if (r->conclusion && r->conclusion->IsVisible()) {
            const std::string t = r->conclusion->GetText();
            if (t.find("\xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8") != std::string::npos)
                return "allow";   // 已允许
            if (t.find("\xE5\xB7\xB2\xE6\x8B\x92\xE7\xBB\x9D") != std::string::npos)
                return "deny";    // 已拒绝
            return "unknown";     // 答了但结论不认识
        }
        return "unknown";
    };
    auto buttonsDisabled = [&]() {
        const RowRec* r = askRow();
        if (!r || !r->allowBtn || !r->denyBtn) return false;
        // ★ P4 D：「不可再点」= 禁用或隐藏（HitTest 对两者都早退）。
        return (!r->allowBtn->IsEnabled() || !r->allowBtn->IsVisible()) &&
               (!r->denyBtn->IsEnabled() || !r->denyBtn->IsVisible());
    };

    auto snap = [&](const char* phase) {
        AnswerProbeRec rec;
        rec.phase = phase;
        rec.askRows = askRowCount();
        rec.answeredRows = answeredCount();
        rec.modelAnswers = modelAnswers();
        rec.answerModel = modelAnswer();
        rec.answerOnScreen = screenAnswer();
        rec.buttonsDisabled = buttonsDisabled();
        m_probeState.answerProbeLog.push_back(std::move(rec));
    };
    auto expect = [&](const char* phase, std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        AnswerProbeRec& rec = m_probeState.answerProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };
    auto clickCentreOf = [&](ECDI::Widget* w) {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + w->GetWidth() / 2,
                          static_cast<int>(abs.y) + w->GetHeight() / 2);
        return true;
    };

    // ① 未答态（负对照⑥）：答案为空、两钮都还能点。
    snap("baseline");
    {
        AnswerProbeRec& rec = m_probeState.answerProbeLog.back();
        const RowRec* r = askRow();
        const bool bothEnabled = r && r->allowBtn && r->denyBtn &&
                                 r->allowBtn->IsEnabled() && r->denyBtn->IsEnabled();
        rec.matched = (rec.askRows >= 1) && (rec.answerModel.empty()) &&
                      (rec.answerOnScreen.empty()) && bothEnabled;
        if (!rec.matched) {
            rec.firstDivergence = "baseline should be unanswered with both buttons enabled";
        }
    }

    // ② 点「允许」（真实派发链）⇒ 结论进模型 + 上屏 + 两钮置灰
    {
        clickCentreOf(askRow() ? askRow()->allowBtn : nullptr);
        expect("answered", {{askRow() != nullptr, "ask row vanished"},
                            {modelAnswer() == "allow", "model answer != allow"},
                            {screenAnswer() == "allow", "screen does not show allow"},
                            {buttonsDisabled(), "buttons still enabled after answering"}});
    }

    // ③ 再点一次同一个钮 ⇒ 必须**零变化**（禁用控件已退出命中；不得重复记账）
    {
        const size_t rowsBefore = m_sess->rows.size();
        const size_t noticesBefore = m_sess->timeline.size();
        clickCentreOf(askRow() ? askRow()->allowBtn : nullptr);
        expect("reclick", {{modelAnswer() == "allow", "answer changed on second click"},
                           {screenAnswer() == "allow", "screen changed on second click"},
                           {m_sess->rows.size() == rowsBefore, "second click added a row"},
                           {m_sess->timeline.size() == noticesBefore, "second click added an entry"}});
    }

    // ④ forward：重建 ⇒ 行快照 A==B，且结论仍在（这条是本步的核心判据）
    std::vector<std::string> answeredRows;
    {
        answeredRows = SnapshotRows();
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("forward", answeredRows, SnapshotRows(), false);
        const bool conditionsOk = (cmp.mismatches == 0) && modelAnswer() == "allow" &&
                                  screenAnswer() == "allow" && buttonsDisabled();
        expect("forward", {{conditionsOk, "forward rebuild lost the answer or changed rows"}});
        // ★ 差异文本只在失败时写：无条件覆盖会把 `expect` 记下的"哪一条没满足"
        //   换成一句与条件无关的比对摘要，排查的人反而看不到断言的失败原因。
        if (!conditionsOk && m_probeState.answerProbeLog.back().firstDivergence.empty()) {
            m_probeState.answerProbeLog.back().firstDivergence = cmp.firstDivergence;
        }
    }

    // ⑤ negative：把模型里的答案改成 `deny` 再重建 ⇒ 比对必须**不**相等，
    //    且首个分歧必须指向 `answer` 字段（否则 ④ 的相等可能只是"两边都没读这段"）。
    //   ★ 负对照的"通过"= 看见了差异，故这里 `matched` 记**false**是正确的读数，
    //     不能把它当失败——说明文字写进 `firstDivergence` 前缀。
    {
        m_sess->timeline[askTimeline].answer = "deny";
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("negative", answeredRows, SnapshotRows(), false);
        snap("negative");   // ★ 先 push 再取引用：push_back 可能重分配，先取引用会悬垂
        AnswerProbeRec& rec = m_probeState.answerProbeLog.back();
        const bool sawDifference = (cmp.mismatches != 0);
        // ★ 用 `find("answer [")` 而不是 `rfind("answer", 0)`：后者只在**位置 0** 匹配，
        //   而差异文本形如 `idx=7 A/q1 -> A/q1 @answer [allow] -> [deny]`，
        //   字段名永远不在开头 ⇒ 会把"差异正确指向 answer"误判成"没指向"。
        //   （第一版就是这个 bug：四个夹具的 negative 相全部报
        //    `divergence not in the answer field: ... @answer [...]`——差异明明就在 answer 上。）
        const bool pointedAtAnswer = cmp.firstDivergence.find("answer [") != std::string::npos;
        const bool screenFollowed = (screenAnswer() == "deny");
        rec.matched = false;   // 负对照的期望就是"不匹配"
        if (sawDifference && pointedAtAnswer && screenFollowed) {
            rec.firstDivergence = "ok(expected divergence): " + cmp.firstDivergence;
        } else if (!sawDifference) {
            rec.firstDivergence = "negative control saw no difference -- snapshot ignores answer";
        } else if (!pointedAtAnswer) {
            rec.firstDivergence = "divergence not in the answer field: " + cmp.firstDivergence;
        } else {
            rec.firstDivergence += "screen did not follow the mutated answer";
        }
    }

    // ⑥ 复原：答案改回 `allow` ⇒ 必须重新与 forward 快照相等（单变量纪律）
    {
        m_sess->timeline[askTimeline].answer = "allow";
        RebuildTranscriptView();
        const RebuildProbeRec cmp = CompareRows("restored", answeredRows, SnapshotRows(), false);
        const bool conditionsOk = (cmp.mismatches == 0) && modelAnswer() == "allow" &&
                                  screenAnswer() == "allow" && buttonsDisabled();
        expect("restored", {{conditionsOk, "restore did not return to the forward state"}});
        if (!conditionsOk && m_probeState.answerProbeLog.back().firstDivergence.empty()) {
            m_probeState.answerProbeLog.back().firstDivergence = cmp.firstDivergence;
        }
    }

    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_probeState.answerProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4 Phase G（--select-probe）：--selectable-rows 的功能判据。
// 【它要回答什么】只读 TextBox 行真的**可选中、可复制**（TextBox.h:50 契约的
//   实证）——焦点给行、合成 Ctrl+A/Ctrl+C（真实派发链），回读剪贴板 == 块全文。
// 【负对照】焦点在 composer 输入框时同一组按键复制到的是输入框内容（证明
//   复制走的是焦点控件的单收件人路径，不是探针直接捅剪贴板）。
void HarnessApp::RunSelectProbe() {
    if (!m_sess || !m_sess->replay || !m_window) { m_probeState.selectProbeDone = true; return; }
    // 找第一个正文行（非 tool/reasoning/ask）且控件是 TextBox。
    ECDI::TextBox* row = nullptr;
    std::string fullText;
    for (const RowRec& r : m_sess->rows) {
        if (r.tool || r.reasoning || r.ask || !r.label) continue;
        if (!(row = dynamic_cast<ECDI::TextBox*>(r.label))) continue;
        fullText = r.label->GetText();
        break;
    }
    if (!row) {
        SelectProbeRec rec;
        rec.phase = "skipped-no-selectable-row";
        rec.matched = false;
        rec.note = "run with --selectable-rows";
        m_probeState.selectProbeLog.push_back(std::move(rec));
        m_probeState.selectProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    auto readClip = [&]() {
        return m_window->GetPlatformWindow().GetClipboardText();
    };
    // ① 焦点给行 → Ctrl+A 全选 → Ctrl+C 复制。
    ECDI::Widget* const focusBefore = m_window->GetFocusedWidget();
    m_window->SetFocusedWidget(row);
    SynthesizeKey(ECDI::KeyCode::A, false, ECDI::KeyModifier::Ctrl);
    SynthesizeKey(ECDI::KeyCode::C, false, ECDI::KeyModifier::Ctrl);
    {
        SelectProbeRec rec;
        rec.phase = "select-and-copy";
        rec.clipText = readClip();
        rec.matched = (rec.clipText == fullText);
        if (!rec.matched) rec.note = "clipboard != block full text";
        m_probeState.selectProbeLog.push_back(std::move(rec));
    }
    // ② 负对照：焦点回输入框、放点文本，同组按键 ⇒ 剪贴板 = 输入框内容。
    if (m_promptBar && m_promptBar->input()) {
        ECDI::TextBox* input = m_promptBar->input();
        input->SetText("__focus_elsewhere__");
        m_window->SetFocusedWidget(input);
        SynthesizeKey(ECDI::KeyCode::A, false, ECDI::KeyModifier::Ctrl);
        SynthesizeKey(ECDI::KeyCode::C, false, ECDI::KeyModifier::Ctrl);
        SelectProbeRec rec;
        rec.phase = "negative-focus-elsewhere";
        rec.clipText = readClip();
        rec.matched = (rec.clipText == "__focus_elsewhere__");
        if (!rec.matched) rec.note = "copy should follow the focused widget";
        m_probeState.selectProbeLog.push_back(std::move(rec));
        input->SetText("");
    }
    if (focusBefore) m_window->SetFocusedWidget(focusBefore);
    m_probeState.selectProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4（--delprov-probe，2026-10-25）：「删除所选」崩溃修复的判据。
// 复现条件 = --config 指向隔离配置（真实反馈场景：文件里 8 份同 id 累积）。
// 判据按 providers 数量分流：8 份同 id ⇒ Load 去重剩 1 ⇒ 点删除走「至少保留
// 一个」不崩；2 家 ⇒ 删所选一个实例剩 1。跑完全程不崩本身就是核心判据
// （修复前：按 id 全删光 ⇒ 空向量 [0] 越界 UB ⇒ 未响应/崩溃）。
void HarnessApp::RunDelProvProbe() {
    if (!m_sess || !m_sess->replay || !m_hud.settingsPanel) {
        m_probeState.delProvProbeDone = true;
        return;
    }
    if (m_appConfig.providers.empty()) {
        // 空配置（--config 路径错/文件不存在）⇒ 如实记跳过，不许在 front() 上 UB。
        DelProvProbeRec rec;
        rec.phase = "skipped-empty-config";
        rec.matched = false;
        rec.note = "no providers loaded -- check --config path";
        m_probeState.delProvProbeLog.push_back(std::move(rec));
        m_probeState.delProvProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    auto snap = [&](const char* phase) -> DelProvProbeRec& {
        DelProvProbeRec rec;
        rec.phase = phase;
        rec.providersBefore = m_appConfig.providers.size();
        rec.providersAfter = rec.providersBefore;   // 调用方按需覆盖
        rec.hint = m_hud.settingsHint ? m_hud.settingsHint->GetText() : "";
        m_probeState.delProvProbeLog.push_back(std::move(rec));
        return m_probeState.delProvProbeLog.back();
    };
    // ★★ §9.5.4（2026-10-27）：v2 → v3 迁移的**唯一真实入口**。
    //   regress 的 `write_probe_config` 写的是 `"version": 2` 且**不带 name**，
    //   所以载入之后每家都必须 `name == id`——「老配置观感不变、凭据不断、
    //   defaultProviderId 语义不动」的全部含义就是这个等式。
    //   ★ 这条只能挂在这个探针上：它是全仓唯一会载入 v2 文件的地方
    //     （settings 探针的配置文件是从**不存在**开始的）。
    {
        std::string bad;
        for (const auto& pc : m_appConfig.providers) {
            if (pc.name.empty()) {
                bad = "provider " + pc.id + " has no display name (v2 fallback broken)";
                break;
            }
            if (pc.name != pc.id) {
                bad = "v2 provider " + pc.id + " got name '" + pc.name +
                      "' (must fall back to its id)";
                break;
            }
        }
        DelProvProbeRec rec;
        rec.phase = "load-migrate";
        rec.providersBefore = m_appConfig.providers.size();
        rec.providersAfter = rec.providersBefore;
        rec.matched = bad.empty();
        rec.note = bad.empty() ? "v2 config: name falls back to id for every provider" : bad;
        m_probeState.delProvProbeLog.push_back(std::move(rec));
    }
    auto clickButtonWithText = [&](ECDI::Widget* root, const char* text) {
        // ★★ 2026-10-25：设置面板的「提供商区」收进 ScrollView 之后，「删除所选」
        //   从 `panel → row → button`（两层）变成 `panel → sv → content → row →
        //   button`（四层）——原来的固定两层下探会**找不到按钮**，判据会误报
        //   "delete button not found"。⇒ 改成有界广度优先（深度 4，不遍历整棵树）。
        //   语义不变：仍是「按文本找按钮 → 用它的绝对中心合成点击」，不猜坐标。
        std::vector<ECDI::Widget*> level{root};
        for (int depth = 0; depth < 4 && !level.empty(); ++depth) {
            std::vector<ECDI::Widget*> next;
            for (ECDI::Widget* node : level) {
                if (!node) continue;
                for (size_t i = 0; i < node->GetChildCount(); ++i) {
                    ECDI::Widget* c = node->GetChildAt(i);
                    if (ECDI::Button* b = dynamic_cast<ECDI::Button*>(c)) {
                        if (b->GetText() == text) {
                            const ECDI::Point abs = b->GetAbsolutePosition();
                            SynthesizeClickAt(static_cast<int>(abs.x) + b->GetWidth() / 2,
                                              static_cast<int>(abs.y) + b->GetHeight() / 2);
                            return true;
                        }
                    }
                    next.push_back(c);
                }
            }
            level.swap(next);
        }
        return false;
    };

    // ① 打开设置。
    SynthesizeKey(ECDI::KeyCode::Comma, false, ECDI::KeyModifier::Ctrl);
    {
        DelProvProbeRec& r = snap("settings-opened");
        r.matched = m_settingsVisible && m_hud.settingsPanel->IsVisible();
        if (!r.matched) r.note = "settings did not open";
    }
    // ② Load 去重判据（8 份同 id ⇒ 1 份；2 家不同 id ⇒ 保持 2）。
    const size_t loaded = m_appConfig.providers.size();
    // ③ 选中第一家（列表第一个 Button 子——不按文本匹配：marker（●/○/[编辑中]）
    //   随会话状态变，脆）→ 点「删除所选」→ 全程不崩 + 数量守恒。
    if (m_hud.settingsProviderList) {
        for (size_t i = 0; i < m_hud.settingsProviderList->GetChildCount(); ++i) {
            if (ECDI::Button* row =
                    dynamic_cast<ECDI::Button*>(m_hud.settingsProviderList->GetChildAt(i))) {
                const ECDI::Point abs = row->GetAbsolutePosition();
                SynthesizeClickAt(static_cast<int>(abs.x) + row->GetWidth() / 2,
                                  static_cast<int>(abs.y) + row->GetHeight() / 2);
                break;
            }
        }
    }
    const bool deleted =
        clickButtonWithText(m_hud.settingsPanel, "\xE5\x88\xA0\xE9\x99\xA4\xE6\x89\x80\xE9\x80\x89");   // 删除所选
    {
        DelProvProbeRec& r = snap("after-delete-click");
        r.providersAfter = m_appConfig.providers.size();
        if (!deleted) {
            r.matched = false;
            r.note = "delete button not found/clicked";
        } else if (loaded == 1) {
            // 仅一家（去重后）：删除被拒绝且程序活着。
            r.matched = (r.providersAfter == 1);
            if (!r.matched) r.note = "sole provider must be kept";
        } else {
            // 多家：删所选**一个实例**。
            r.matched = (r.providersAfter == loaded - 1);
            if (!r.matched) r.note = "should remove exactly one instance";
        }
    }
    SetSettingsVisible(false);
    m_probeState.delProvProbeDone = true;
    if (m_application) m_application->Exit();
}

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendHitProbeSummary(std::string& json) {
    // ★★ REQ-04 **边界**机读证据（2026-10-14）：命中门控受控对照。
    //   读法：`hitIsSend` = HitTest 是否返回发送按钮本身；`clickReached` = 合成
    //   Down+Up 后 SendPrompt 是否被进入。判据 = 正对照全 true ∧ forced-disabled 全 false。
    json += "  \"hitProbe\": [";
    for (size_t i = 0; i < m_probeState.hitProbeLog.size(); ++i) {
        const HitProbeRec& r = m_probeState.hitProbeLog[i];
        json += (i ? ", " : "");
        char b[320];
        std::snprintf(b, sizeof(b),
                      "{\"scene\": \"%s\", \"px\": %d, \"py\": %d, "
                      "\"sendEnabled\": %s, \"hitIsSend\": %s, \"hitIsNull\": %s, "
                      "\"hitIsRoot\": %s, \"hitConsumes\": %s, "
                      "\"hitClass\": \"%s\", \"clickReached\": %s}",
                      r.scene.c_str(), r.px, r.py,
                      r.sendEnabled ? "true" : "false",
                      r.hitIsSend ? "true" : "false",
                      r.hitIsNull ? "true" : "false",
                      r.hitIsRoot ? "true" : "false",
                      r.hitConsumes ? "true" : "false",
                      r.hitClass.c_str(),
                      r.clickReached ? "true" : "false");
        json += b;
    }
    json += "],\n";
}

void HarnessApp::AppendReasoningProbeSummary(std::string& json) {
    // ★★ 协议 v2 §7 机读证据（2026-10-14）：思维链折叠**行为**轨迹。
    //   判据 = default 必须 expanded=false ∧ contentVisible=false（默认不渲染正文）；
    //          after-click-1 必须 expanded=true（点开了）；
    //          after-click-2 必须回到 false（可逆，不是单向）。
    //   箭头不单列字段——它在 `headerText` 里（`﹀` 收起 / `︿` 展开），
    //   与用户可见文本**逐字相同**，避免"读数说箭头对了、屏幕上是另一个字"。
    json += "  \"reasoningProbe\": [";
    for (size_t i = 0; i < m_probeState.reasoningProbeLog.size(); ++i) {
        const ReasoningProbeRec& r = m_probeState.reasoningProbeLog[i];
        json += (i ? ", " : "");
        char b[256];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"headerText\": \"%s\", "
                      "\"expanded\": %s, \"contentVisible\": %s, \"panelH\": %d}",
                      r.phase.c_str(), r.headerText.c_str(),
                      r.expanded ? "true" : "false",
                      r.contentVisible ? "true" : "false", r.panelH);
        json += b;
    }
    json += "],\n";
}

void HarnessApp::AppendOrderProbeSummary(std::string& json) {
    // ★★ Step 0a 机读证据（2026-10-19）：块**到达顺序**原文。
    //   为什么必须是数组而不是一个「顺序对不对」的布尔：探针若自己也算一遍
    //   `sorted(...)` 再比，比对逻辑就是**被测对象自己**——错的地方会一起错。
    //   故这里只如实吐出 `Order()` 的内容，判据留给跑批脚本对着夹具独立算。
    //   读法：本数组必须与夹具里 `block_start` 的出现次序**逐元素相同**。
    json += "  \"blockOrder\": [";
    const std::vector<std::string>& order = m_sess->transcript.Order();
    for (size_t i = 0; i < order.size(); ++i) {
        json += (i ? ", \"" : "\"");
        json += order[i];
        json += "\"";
    }
    json += "],\n";
}

void HarnessApp::AppendRebuildProbeSummary(std::string& json) {
    char buf[512];
    // ★★ Step 0c 机读证据（2026-10-19）：照模型重建视图的逐字段比对。
    //   判据（跑批脚本执行，不由本函数自评）：
    //     · forward / forward-again 的 mismatches 必须 == 0（视图重放无损）
    //     · lexicographic 的 orderMismatches 必须 == 字典序错位量
    //       （cot 42 / realistic 218 / scale 1998 / demo 0）——负对照，
    //       为 0 说明探针看不出顺序错，于是上面那条"全等"没有意义
    //     · rebuildNoticeCount > 0 证明"通知进模型"是承重的（否则重建少这么多行）
    //     · expanded/answeredSamples > 0 证明"状态被保住"这条断言不是空断言
    json += "  \"rebuildProbe\": [";
    // ★ `firstDivergence` 现在会带上**行文本**（标题/正文），而转录里的文字是
    //   JSON 里来的、可能含引号 ⇒ 必须转义，否则整个摘要不是一个合法 JSON，
    //   跑批脚本读它会**整份**失败（比少一个字段严重得多）。
    auto esc = [](const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char e[8];
                        std::snprintf(e, sizeof(e), "\\u%04x", c);
                        out += e;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    };
    for (size_t i = 0; i < m_probeState.rebuildProbeLog.size(); ++i) {
        const RebuildProbeRec& r = m_probeState.rebuildProbeLog[i];
        json += (i ? ", " : "");
        char b[768];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"rowCount\": %zu, \"beforeCount\": %zu, "
                      "\"timelineCount\": %zu, \"mismatches\": %zu, "
                      "\"orderMismatches\": %zu, \"firstDivergence\": \"%s\", "
                      "\"scrollOffset\": %d, \"maxScrollOffset\": %d}",
                      r.phase.c_str(), r.rowCount, r.beforeCount, r.timelineCount,
                      r.mismatches, r.orderMismatches, esc(r.firstDivergence).c_str(),
                      r.scrollOffset, r.maxScrollOffset);
        json += b;
    }
    json += "],\n";
    std::snprintf(buf, sizeof(buf), "  \"rebuildNoticeCount\": %zu,\n", m_probeState.rebuildNoticeCount);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"rebuildTimelineCount\": %zu,\n",
                  m_sess->timeline.size());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"rebuildExpandedSamples\": %d,\n",
                  m_probeState.rebuildExpandedSamples);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"rebuildAnsweredSamples\": %d,\n",
                  m_probeState.rebuildAnsweredSamples);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"rebuildScrollBefore\": %d,\n",
                  m_probeState.rebuildScrollBefore);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"rebuildRowCount\": %zu,\n", m_sess->rows.size());
    json += buf;
    // ★ 点击一次后标题是否还完整（0c 修的工具卡标题被清空那个 bug 的专属读数）
    std::snprintf(buf, sizeof(buf), "  \"rebuildTitleEmptied\": %d,\n",
                  m_probeState.rebuildTitleEmptied);
    json += buf;
    json += "  \"rebuildTitleSample\": \"" + esc(m_probeState.rebuildTitleSample) + "\",\n";
    json += "  \"rebuildTitleAfter\": \"" + esc(m_probeState.rebuildTitleAfter) + "\",\n";
}

void HarnessApp::AppendAnswerProbeSummary(std::string& json) {
    json += "  \"answerProbe\": [";
    // ★ 自带一份转义（作用域同 sidebarProbe 段：不能复用别段的局部 lambda）
    auto escA = [](const std::string& in) {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char e[8];
                        std::snprintf(e, sizeof(e), "\\u%04x", c);
                        out += e;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    };
    for (size_t i = 0; i < m_probeState.answerProbeLog.size(); ++i) {
        const AnswerProbeRec& r = m_probeState.answerProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"askRows\": %zu, \"answeredRows\": %zu, "
                      "\"modelAnswers\": %zu, \"answerModel\": \"%s\", "
                      "\"answerOnScreen\": \"%s\", \"buttonsDisabled\": %s, "
                      "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                      r.phase.c_str(), r.askRows, r.answeredRows, r.modelAnswers,
                      escA(r.answerModel).c_str(), escA(r.answerOnScreen).c_str(),
                      r.buttonsDisabled ? "true" : "false",
                      r.matched ? "true" : "false",
                      escA(r.firstDivergence).c_str());
        json += b;
    }
    json += "],\n";
}

void HarnessApp::AppendSelectProbeSummary(std::string& json) {
    // clipHead 只为诊断：按字节截断会切进多字节字符（实测 JSON 解析炸）——
    // 回退到 UTF-8 序列边界；块文本可能含引号/换行（会破坏 JSON 字面量），
    // 一并压成空格。
    auto truncUtf8 = [](const std::string& s) {
        // 数码点截断（≤48 码点），终点必落在完整序列后。
        std::string t;
        int cp = 0;
        for (size_t i = 0; i < s.size() && cp < 48; ++i, ++cp) {
            const unsigned char c = s[i];
            size_t len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
            if (i + len > s.size()) break;   // 尾部残缺序列：丢弃
            t += s.substr(i, len);
            i += len - 1;
        }
        for (char& c : t) {
            if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        return t;
    };
    json += "  \"selectProbe\": [";
    for (size_t i = 0; i < m_probeState.selectProbeLog.size(); ++i) {
        const SelectProbeRec& r = m_probeState.selectProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"clipHead\": \"%s\", "
                      "\"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), truncUtf8(r.clipText).c_str(),
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}

void HarnessApp::AppendDelProvProbeSummary(std::string& json) {
    json += "  \"delProvProbe\": [";
    for (size_t i = 0; i < m_probeState.delProvProbeLog.size(); ++i) {
        const DelProvProbeRec& r = m_probeState.delProvProbeLog[i];
        json += (i ? ", " : "");
        char b[640];
        // ★ 无 hint 字段——中文按字节截断会切出非法 UTF-8（G 相同坑），
        //   note 已含失败原因。
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"before\": %zu, \"after\": %zu, "
                      "\"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.providersBefore, r.providersAfter,
                      r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}

