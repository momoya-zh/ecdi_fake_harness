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
    // ①b 工具折叠体（2026-11-07，G 计划收尾）：展开 → 焦点给体部 TextBox →
    //    Ctrl+A/C ⇒ 剪贴板 == 体部全文。展开走 ToggleRow——「点击路径与探针共用
    //    的唯一入口」（见其注释：探针自写一遍切换就是第二份实现，测的是它自己）。
    //    【牙齿】体部若仍是 Label（--selectable-rows 未铺到 tool 行或被改回），
    //    dynamic_cast 落空 ⇒ 本相 matched=false ⇒ 门红——这正是本次要守的缺口。
    //    只读 TextBox 的复制与正文行同一条 TextBox.h:50 契约，无需新断言面。
    {
        RowRec* toolRow = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (!r.tool || !r.label) continue;
            if (dynamic_cast<ECDI::TextBox*>(r.label)) { toolRow = &r; break; }
        }
        SelectProbeRec rec;
        rec.phase = "tool-select-and-copy";
        if (!toolRow) {
            rec.matched = false;
            rec.note = "no tool row with TextBox body (selectable rows not extended to tool rows?)";
            m_probeState.selectProbeLog.push_back(std::move(rec));
        } else {
            ToggleRow(*toolRow, /*reasoning=*/false);
            const std::string toolText = toolRow->label->GetText();
            m_window->SetFocusedWidget(toolRow->label);
            SynthesizeKey(ECDI::KeyCode::A, false, ECDI::KeyModifier::Ctrl);
            SynthesizeKey(ECDI::KeyCode::C, false, ECDI::KeyModifier::Ctrl);
            rec.clipText = readClip();
            rec.matched = (rec.clipText == toolText);
            if (!rec.matched) rec.note = "clipboard != tool body full text";
            m_probeState.selectProbeLog.push_back(std::move(rec));
        }
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



// ── 块级全套解析探针（--md-probe）─────────────────────────────────────
// ★★ 2026-11-08（富内容 B+ / rich-content-plan §G）：
//   ★ 这一套只判**纯函数**，不建窗口、不读夹具 ⇒ 在任何 --replay 下都能跑，
//     且与渲染成败解耦：解析对了渲染错了，这里照样 PASS（那是渲染相该抓的）。
//   ★ 每一相都成对：**正臂**（构造被识别成对的段种）+ **负臂**（长得像但**不该**被识别）。
//     只测正臂的探针会奖励「过度解析」——把正文里的 `---` 当分隔线也能全绿。
//   ★ 最后一相 "no-construct-identity" 是**基线不变量**的锚：不含任何块级构造的文本
//     必须原封不动返回单 Paragraph，这是 7 份既有夹具不变的根因。
void HarnessApp::RunMdProbe() {
    if (m_probeState.mdProbeDone) return;
    m_probeState.mdProbeDone = true;

    auto add = [&](const std::string& phase, bool ok, const std::string& note) {
        MdProbeRec rec;
        rec.phase = phase;
        rec.matched = ok;
        rec.note = ok ? "" : note;
        m_probeState.mdProbeLog.push_back(std::move(rec));
    };

    // 1. heading：h1/h2/h3 + 前后段落；负臂：`#hashtag`（无空格）不是标题
    {
        const std::string src =
            "引言文字\n\n"
            "# 一级标题\n\n"
            "## 二级标题\n\n"
            "### 三级标题\n\n"
            "结尾\n\n"
            "#没空格不是标题";
        auto segs = ParseMarkdownSegments(src);
        int h = 0;
        std::string levels;
        bool lastIsPara = false;
        for (const auto& s : segs) {
            if (s.kind == MarkdownSegmentKind::Heading) {
                ++h;
                levels += std::to_string(s.level);
            }
        }
        lastIsPara = !segs.empty() && segs.back().kind == MarkdownSegmentKind::Paragraph &&
                     segs.back().text.find("#没空格") != std::string::npos;
        bool ok = (h == 3) && (levels == "123") && lastIsPara;
        add("heading", ok,
            "h=" + std::to_string(h) + " levels=" + levels +
                " lastIsPara=" + (lastIsPara ? "1" : "0"));
    }

    // 2. fence：代码块内的 `#`/`-`/`|` 不得二次解析；lang 要摘出来
    {
        const std::string src =
            "```cpp\n"
            "# 这不是标题\n"
            "- 这不是列表\n"
            "| 这不是 | 表格 |\n"
            "```\n";
        auto segs = ParseMarkdownSegments(src);
        bool ok = segs.size() == 1 && segs[0].kind == MarkdownSegmentKind::CodeBlock &&
                  segs[0].lang == "cpp" &&
                  segs[0].text.find("# 这不是标题") != std::string::npos &&
                  segs[0].text.find("| 这不是 | 表格 |") != std::string::npos;
        add("fence", ok, "segs=" + std::to_string(segs.size()));
    }

    // 3. fence-unclosed：未闭合围栏整段回退为原文（不静默丢弃、不假装代码块）
    {
        const std::string src = "```json\n{\"a\": 1}\n";
        auto segs = ParseMarkdownSegments(src);
        bool ok = segs.size() == 1 && segs[0].kind == MarkdownSegmentKind::Paragraph &&
                  segs[0].text.find("```json") != std::string::npos &&
                  segs[0].text.find("{\"a\": 1}") != std::string::npos;
        add("fence-unclosed", ok, "segs=" + std::to_string(segs.size()));
    }

    // 4. list：有序/无序、原文序号不重排、嵌套 depth
    {
        const std::string src =
            "- 顶层无序\n"
            "  - 二级无序\n"
            "1. 第一\n"
            "2. 第二\n"
            "3. 第三";
        auto segs = ParseMarkdownSegments(src);
        int items = 0, ordered = 0, depth1 = 0;
        std::string markers;
        for (const auto& s : segs) {
            if (s.kind == MarkdownSegmentKind::ListItem) {
                ++items;
                if (s.ordered) {
                    ++ordered;
                    markers += std::to_string(s.marker);
                }
                if (s.depth == 1) ++depth1;
            }
        }
        bool ok = items == 5 && ordered == 3 && markers == "123" && depth1 == 1;
        add("list", ok,
            "items=" + std::to_string(items) + " ordered=" + std::to_string(ordered) +
                " markers=" + markers + " depth1=" + std::to_string(depth1));
    }

    // 5. quote：连续 > 行合并成一段，`>` 已剥离
    {
        const std::string src = "> 第一行引用\n> 第二行引用\n\n正常段落";
        auto segs = ParseMarkdownSegments(src);
        bool ok = segs.size() == 2 && segs[0].kind == MarkdownSegmentKind::Quote &&
                  segs[0].text == "第一行引用\n第二行引用" &&
                  segs[1].kind == MarkdownSegmentKind::Paragraph;
        add("quote", ok, "segs=" + std::to_string(segs.size()));
    }

    // 6. hr：`---`/`***`/`___` 是分隔线；负臂 `- 列表项` 与 `**粗体**` 不是
    {
        const std::string src =
            "上面\n\n"
            "---\n\n"
            "中间\n\n"
            "***\n\n"
            "下面\n\n"
            "___\n\n"
            "- 这是列表不是分隔线\n\n"
            "**这是粗体不是分隔线**";
        auto segs = ParseMarkdownSegments(src);
        int hr = 0;
        for (const auto& s : segs) {
            if (s.kind == MarkdownSegmentKind::Hr) ++hr;
        }
        // 负臂（★ 2026-11-08 修正）：这两行**不是**分隔线，但各自的归属不同——
        //   `- 这是列表不是分隔线` 是 ListItem（行首 `- ` + 后面有正文 ⇒ 列表项），
        //   `**这是粗体不是分隔线**` 是 Paragraph（`**` 不是分隔线字符，也不是列表）。
        //   原来的判据要求两者都留在 Paragraph，把「列表项」误判成了解析器缺陷——
        //   是**探针写错了**，不是解析器错。判据改为各自就位、且 Hr 数量不多不少。
        bool listOk = false;
        bool paraOk = false;
        for (const auto& s : segs) {
            if (s.kind == MarkdownSegmentKind::ListItem &&
                s.text.find("这是列表不是分隔线") != std::string::npos) {
                listOk = true;
            }
            if (s.kind == MarkdownSegmentKind::Paragraph &&
                s.text.find("**这是粗体不是分隔线**") != std::string::npos) {
                paraOk = true;
            }
        }
        bool ok = hr == 3 && listOk && paraOk;
        add("hr", ok,
            "hr=" + std::to_string(hr) + " listOk=" + (listOk ? "1" : "0") +
                " paraOk=" + (paraOk ? "1" : "0"));
    }

    // 7. no-construct-identity：**基线不变量**——无构造文本必须逐字等于原文
    {
        // ★ 刻意混入长得像块级、但不构成块级的三行：缩进代码、行内 `|`、行内 `#`
        const std::string src =
            "这是一段普通正文，包含 `- 行内短横` 与 `# 行内井号`。\n"
            "缩进四空格的普通行不应变成代码块：\n"
            "    这行是缩进正文\n"
            "还有一行含 | 竖线但下一行不是分隔行";
        auto segs = ParseMarkdownSegments(src);
        bool ok = segs.size() == 1 && segs[0].kind == MarkdownSegmentKind::Paragraph &&
                  segs[0].text == src;
        add("no-construct-identity", ok,
            "segs=" + std::to_string(segs.size()) +
                " identical=" + ((!segs.empty() && segs[0].text == src) ? "1" : "0"));
    }

    // 8. 混合：表格 + 标题 + 列表共存时各自成段、顺序保持
    {
        const std::string src =
            "## 小节标题\n\n"
            "| 列A | 列B |\n"
            "| --- | --- |\n"
            "| 1 | 2 |\n\n"
            "- 列表项";
        auto segs = ParseMarkdownSegments(src);
        bool ok = segs.size() == 3 &&
                  segs[0].kind == MarkdownSegmentKind::Heading &&
                  segs[1].kind == MarkdownSegmentKind::Table &&
                  segs[2].kind == MarkdownSegmentKind::ListItem;
        std::string kinds;
        for (const auto& s : segs) {
            kinds += std::to_string(static_cast<int>(s.kind));
        }
        add("mixed-order", ok, "segs=" + std::to_string(segs.size()) + " kinds=" + kinds);
    }

    // 9. render-blocks：**渲染侧**判据——真走一遍 BuildBlockRow，数落地的控件。
    //   ★ 为什么纯函数判据不够：解析对了不等于渲染了。入口开关若仍只认 Table，
    //     解析器会产出正确的段，但 `BuildAssistantMarkdownWidgets` 根本不会被调用——
    //     纯函数探针照样全绿。这一相就是抓那种「解析正确、屏幕上什么都没有」。
    {
        Block mockBlock;
        mockBlock.id = "mock_blocks";
        mockBlock.kind = "assistant";
        mockBlock.text =
            "# 标题\n\n"
            "正文段落\n\n"
            "```cpp\nint x = 1;\n```\n\n"
            "- 列表项\n"
            "1. 有序项\n\n"
            "> 引用\n\n"
            "---\n";
        mockBlock.ended = true;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        // 段种计数（渲染侧真实落地的段）
        int nHead = 0, nCode = 0, nList = 0, nQuote = 0, nHr = 0, nPara = 0;
        for (const auto& m : mockRec.segMeta) {
            switch (m.kind) {
                case MarkdownSegmentKind::Heading: ++nHead; break;
                case MarkdownSegmentKind::CodeBlock: ++nCode; break;
                case MarkdownSegmentKind::ListItem: ++nList; break;
                case MarkdownSegmentKind::Quote: ++nQuote; break;
                case MarkdownSegmentKind::Hr: ++nHr; break;
                case MarkdownSegmentKind::Paragraph: ++nPara; break;
                default: break;
            }
        }
        // ★ 判据必须同时含「都有」和「顺序对」：只数个数会让「全排错位」也过。
        bool metaMatchesSegs = (mockRec.segMeta.size() == mockRec.segments.size());
        bool ok = mockRec.hasMarkdown && metaMatchesSegs &&
                  nHead == 1 && nCode == 1 && nList == 2 && nQuote == 1 && nHr == 1 &&
                  nPara == 1;
        add("render-blocks", ok,
            "hasMd=" + std::string(mockRec.hasMarkdown ? "1" : "0") +
                " segs=" + std::to_string(mockRec.segments.size()) +
                " meta=" + std::to_string(mockRec.segMeta.size()) +
                " h=" + std::to_string(nHead) + " code=" + std::to_string(nCode) +
                " list=" + std::to_string(nList) + " quote=" + std::to_string(nQuote) +
                " hr=" + std::to_string(nHr) + " para=" + std::to_string(nPara));
    }

    // 10. render-negative：**负对照**——不含块级构造时不得走复合渲染。
    //   ★ 没有这一相，"render-blocks 全绿" 不能证明入口开关有判别力：
    //     一个「永远走复合渲染」的实现也能让上面全绿，但会把普通段落也切碎。
    {
        Block mockBlock;
        mockBlock.id = "mock_plain";
        mockBlock.kind = "assistant";
        mockBlock.text = "这是一段完全普通的正文，没有任何块级构造。\n第二行也是。";
        mockBlock.ended = true;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        bool ok = !mockRec.hasMarkdown && mockRec.label != nullptr &&
                  mockRec.segments.empty() && mockRec.segMeta.empty();
        add("render-negative", ok,
            "hasMd=" + std::string(mockRec.hasMarkdown ? "1" : "0") +
                " segs=" + std::to_string(mockRec.segments.size()));
    }

    // ══ 11–20：段内内联样式（富内容 §H，2026-11-09）══════════════════════
    // ★ 与块级相同一条纪律：**正臂 + 负臂成对**，且每相断言 clean 全文与 spans 的
    //   逐字段值。只断言「有 span」的探针会奖励「随便加粗任意东西」的实现。
    // ★ `inline-cp-cjk` 专测**码点索引**：emoji（4 字节）+ 中文（3 字节）混排，
    //   按字节算偏移的实现会在这里露馅，而纯 ASCII 的相全绿——这是本批最容易错的地方。
    // ★ 颜色用哨兵值（`FromRGBA8(1,2,3)`）而不是主题色：解析器不认主题，
    //   探针也不该依赖主题表；哨兵色能同时证明「颜色是透传的」而不是写死的。
    {
        const ECDI::Color sentinel = ECDI::Color::FromRGBA8(1, 2, 3);
        InlineCodeStyle cs;
        cs.family = "Consolas";
        cs.color = sentinel;

        // 11. inline-bold：`**` 成对 ⇒ 去标记 + 一个 weight=700 区间，且**不夹带**其它维度
        {
            const std::string src = "这是**粗体**文字";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "这是粗体文字" && r.spans.size() == 1 &&
                      r.spans[0].startCp == 2 && r.spans[0].cpCount == 2 &&
                      r.spans[0].style.weight.has_value() && *r.spans[0].style.weight == 700 &&
                      !r.spans[0].style.family.has_value() &&
                      !r.spans[0].style.foreground.has_value();
            add("inline-bold", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 12. inline-code：反引号 ⇒ 去标记 + family/foreground 双维（哨兵色透传）
        {
            const std::string src = "运行 `git status` 看看";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "运行 git status 看看" && r.spans.size() == 1 &&
                      r.spans[0].startCp == 3 && r.spans[0].cpCount == 10 &&
                      r.spans[0].style.family.has_value() && *r.spans[0].style.family == "Consolas" &&
                      r.spans[0].style.foreground.has_value() && *r.spans[0].style.foreground == sentinel &&
                      !r.spans[0].style.weight.has_value();
            add("inline-code", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 13. inline-mixed：嵌套（粗体里的 code）必须摊平成 3 个**不重叠**区间
        //     `**加粗含 `code` 结尾**` → [0,4)粗 / [4,4)粗+code / [8,3)粗
        {
            const std::string src = "**加粗含 `code` 结尾**";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "加粗含 code 结尾" && r.spans.size() == 3 &&
                      r.spans[0].startCp == 0 && r.spans[0].cpCount == 4 &&
                      r.spans[1].startCp == 4 && r.spans[1].cpCount == 4 &&
                      r.spans[2].startCp == 8 && r.spans[2].cpCount == 3;
            if (ok) {
                ok = r.spans[0].style.weight.has_value() && !r.spans[0].style.family.has_value() &&
                     r.spans[1].style.weight.has_value() && r.spans[1].style.family.has_value() &&
                     r.spans[2].style.weight.has_value() && !r.spans[2].style.family.has_value();
            }
            add("inline-mixed", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 14. inline-unclosed：未闭合 ⇒ **逐字等于原文**（含星号），零样式
        {
            const std::string src = "**未闭合的粗体与 `未闭合的代码";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == src && r.spans.empty();
            add("inline-unclosed", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 15. inline-escape：反斜杠转义 ⇒ 标记变字面且反斜杠不保留
        {
            const std::string src = "\\*\\*不是粗体\\*\\* 与 \\`不是代码\\`";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "**不是粗体** 与 `不是代码`" && r.spans.empty();
            add("inline-escape", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 16. inline-negative：负臂——长得像标记但**不该**生效的三类
        //     ① `2 ** 3` 两侧空白（flanking）  ② `snake_case_name` 词内下划线（intraword）
        //     ③ `****` 零内容
        {
            const std::string src = "计算 2 ** 3 与 snake_case_name 还有 **** 结束";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == src && r.spans.empty();
            add("inline-negative", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 17. inline-cp-cjk：★ 码点索引专项——emoji(1 cp / 4 字节) + 中文(1 cp / 3 字节)
        //     源 `😀**粗**A` ⇒ clean `😀粗A`，唯一区间 startCp=1 cpCount=1
        //     按字节算偏移的实现会得到 startCp=4（4 字节 emoji）⇒ 本相变红。
        {
            const std::string src = "\xF0\x9F\x98\x80" "**粗**A";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "\xF0\x9F\x98\x80" "粗A" && r.spans.size() == 1 &&
                      r.spans[0].startCp == 1 && r.spans[0].cpCount == 1;
            add("inline-cp-cjk", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 18. inline-code-literal：code span 内不解析（`R5`）——`**x**` 是字面
        {
            const std::string src = "用 `**x**` 表示通配";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "用 **x** 表示通配" && r.spans.size() == 1 &&
                      r.spans[0].startCp == 2 && r.spans[0].cpCount == 5 &&
                      r.spans[0].style.weight.has_value() == false &&
                      r.spans[0].style.family.has_value();
            add("inline-code-literal", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 19. inline-identity：**基线不变量**——无构造 ⇒ clean 逐字等于原文、零 spans。
        //     与块级 "no-construct-identity" 互补：那条保夹具的分段数，这条保字面内容。
        {
            const std::string src =
                "普通正文里出现 4 * 5 = 20 与 3 _ 4，还有未闭合的单引号 ` 和星号 *。";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == src && r.spans.empty();
            add("inline-identity", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 20. render-inline：**渲染侧**判据——真走一遍建行，读控件上的文本。
        //     ★ 为什么必须有这一相：解析全绿不等于屏幕上没星号。`ApplyInlineText`
        //       若没接到这条路径（无块级构造的普通段落），解析器照样全过，
        //       而控件上仍是 `**粗体**` 原文。判据 = 控件 `GetText()` 已是干净文本。
        {
            Block mockBlock;
            mockBlock.id = "mock_inline";
            mockBlock.kind = "assistant";
            mockBlock.text = "这是**粗体**与 `code` 混合的正文。";
            mockBlock.ended = true;

            RowRec mockRec;
            auto rowPanel = BuildBlockRow(mockBlock, mockRec);
            const std::string got = mockRec.label ? mockRec.label->GetText() : std::string();
            bool ok = mockRec.label != nullptr && !mockRec.hasMarkdown &&
                      got == "这是粗体与 code 混合的正文。";
            add("render-inline", ok, "got='" + got + "'");
        }

        // 21. render-inline-negative：负对照——纯文本行不得被改写（逐字等于原文）
        {
            Block mockBlock;
            mockBlock.id = "mock_inline_neg";
            mockBlock.kind = "assistant";
            mockBlock.text = "一行没有任何标记的普通正文";
            mockBlock.ended = true;

            RowRec mockRec;
            auto rowPanel = BuildBlockRow(mockBlock, mockRec);
            const std::string got = mockRec.label ? mockRec.label->GetText() : std::string();
            bool ok = mockRec.label != nullptr && got == mockBlock.text && !mockRec.hasMarkdown;
            add("render-inline-negative", ok, "got='" + got + "'");
        }

        // 22. inline-italic：单 `*` 与单 `_` ⇒ 去标记 + italic=true 单维覆盖
        {
            const std::string src = "这是*星号斜体*与_下划线斜体_文字";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "这是星号斜体与下划线斜体文字" && r.spans.size() == 2 &&
                      r.spans[0].startCp == 2 && r.spans[0].cpCount == 4 &&
                      r.spans[0].style.italic.has_value() && *r.spans[0].style.italic == true &&
                      !r.spans[0].style.weight.has_value() &&
                      !r.spans[0].style.family.has_value() &&
                      r.spans[1].startCp == 7 && r.spans[1].cpCount == 5 &&
                      r.spans[1].style.italic.has_value() && *r.spans[1].style.italic == true &&
                      !r.spans[1].style.weight.has_value() &&
                      !r.spans[1].style.family.has_value();
            add("inline-italic", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 23. inline-bold-italic：`***` 复合与 `**` 里嵌 `*` 嵌套扁平化
        {
            const std::string src = "这是***粗斜体***与**粗体*斜体*结尾**";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "这是粗斜体与粗体斜体结尾" && r.spans.size() == 4 &&
                      r.spans[0].startCp == 2 && r.spans[0].cpCount == 3 &&
                      r.spans[0].style.weight.has_value() && *r.spans[0].style.weight == 700 &&
                      r.spans[0].style.italic.has_value() && *r.spans[0].style.italic == true &&
                      r.spans[1].startCp == 6 && r.spans[1].cpCount == 2 &&
                      r.spans[1].style.weight.has_value() && *r.spans[1].style.weight == 700 &&
                      !r.spans[1].style.italic.has_value() &&
                      r.spans[2].startCp == 8 && r.spans[2].cpCount == 2 &&
                      r.spans[2].style.weight.has_value() && *r.spans[2].style.weight == 700 &&
                      r.spans[2].style.italic.has_value() && *r.spans[2].style.italic == true &&
                      r.spans[3].startCp == 10 && r.spans[3].cpCount == 2 &&
                      r.spans[3].style.weight.has_value() && *r.spans[3].style.weight == 700 &&
                      !r.spans[3].style.italic.has_value();
            add("inline-bold-italic", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 24. inline-italic-negative：斜体负臂——空白乘号、词内下划线、未闭合与转义
        {
            const std::string src = "算式 4 * 5 = 20 与变量 a_b_c，还有未闭合 *星号 与转义 \\*字面\\*";
            const InlineRuns r = ParseInlineRuns(src, cs);
            bool ok = r.text == "算式 4 * 5 = 20 与变量 a_b_c，还有未闭合 *星号 与转义 *字面*" &&
                      r.spans.empty();
            add("inline-italic-negative", ok,
                "text='" + r.text + "' spans=" + std::to_string(r.spans.size()));
        }

        // 25. render-inline-italic：渲染侧控件 GetText() 读取干净文本
        {
            Block mockBlock;
            mockBlock.id = "mock_inline_italic";
            mockBlock.kind = "assistant";
            mockBlock.text = "包含*斜体*与***粗斜体***的正文。";
            mockBlock.ended = true;

            RowRec mockRec;
            auto rowPanel = BuildBlockRow(mockBlock, mockRec);
            const std::string got = mockRec.label ? mockRec.label->GetText() : std::string();
            bool ok = mockRec.label != nullptr && !mockRec.hasMarkdown &&
                      got == "包含斜体与粗斜体的正文。";
            add("render-inline-italic", ok, "got='" + got + "'");
        }
    }

    // 26. render-list-inline：列表项 marker 分离、正文内联展开与多行缩进续行合并
    //   验证：
    //   ① 列表项经 BuildBlockRow 真实落成两层控件结构（第 0 孩子为 marker Label，第 1 孩子为正文 TextWidget）；
    //   ② 第一项正文（**第一步**）正确展开内联样式（文字含「第一步」、无原始星号「**」）；
    //   ③ 第二项正文合并缩进续行（包含换行符与续行正文「这是缩进续行」）；
    //   ④ 序号 marker 文本分别为 "1." 与 "2."，与正文分离挂载，且元数据 ordered/marker 对应。
    {
        Block mockBlock;
        mockBlock.id = "mock_list_inline";
        mockBlock.kind = "assistant";
        mockBlock.text =
            "1. **第一步**：测试列表项行内样式展开与分离结构\n"
            "2. 第二步：测试多行正文缩进续行合并\n"
            "   这是缩进续行，用来拉长正文形成多行\n";
        mockBlock.ended = true;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        bool ok = false;
        std::string note;
        if (mockRec.hasMarkdown && mockRec.segments.size() == 2 && mockRec.segMeta.size() == 2) {
            ECDI::Widget* w0 = mockRec.segments[0];
            ECDI::Widget* w1 = mockRec.segments[1];
            if (w0 && w0->GetChildCount() >= 2 && w1 && w1->GetChildCount() >= 2) {
                auto* mk0 = dynamic_cast<ECDI::Label*>(w0->GetChildAt(0));
                auto* body0 = dynamic_cast<ECDI::TextWidget*>(w0->GetChildAt(1));
                auto* mk1 = dynamic_cast<ECDI::Label*>(w1->GetChildAt(0));
                auto* body1 = dynamic_cast<ECDI::TextWidget*>(w1->GetChildAt(1));
                if (mk0 && body0 && mk1 && body1) {
                    const bool mk0Ok = (mk0->GetText() == "1.");
                    const bool mk1Ok = (mk1->GetText() == "2.");
                    const bool body0Ok = (body0->GetText().find("第一步") != std::string::npos &&
                                          body0->GetText().find("**") == std::string::npos);
                    const bool body1Ok = (body1->GetText().find("第二步") != std::string::npos &&
                                          body1->GetText().find("这是缩进续行") != std::string::npos &&
                                          body1->GetText().find("\n") != std::string::npos);
                    const bool meta0Ok = (mockRec.segMeta[0].kind == MarkdownSegmentKind::ListItem &&
                                          mockRec.segMeta[0].ordered && mockRec.segMeta[0].marker == 1);
                    const bool meta1Ok = (mockRec.segMeta[1].kind == MarkdownSegmentKind::ListItem &&
                                          mockRec.segMeta[1].ordered && mockRec.segMeta[1].marker == 2);
                    ok = mk0Ok && mk1Ok && body0Ok && body1Ok && meta0Ok && meta1Ok;
                    note = "mk0=" + mk0->GetText() + " mk1=" + mk1->GetText() +
                           " b0NoAsterisk=" + std::to_string(body0->GetText().find("**") == std::string::npos) +
                           " b1HasCont=" + std::to_string(body1->GetText().find("这是缩进续行") != std::string::npos);
                }
            }
        }
        add("render-list-inline", ok, note);
    }

    // 27. render-selectable-rich：--selectable-rows 下 ListItem / Quote 正文同步走只读 TextBox + 内联去标记
    {
        const bool prevSelectable = m_options.selectableRows;
        m_options.selectableRows = true;

        Block mockBlock;
        mockBlock.id = "mock_selectable_rich";
        mockBlock.kind = "assistant";
        mockBlock.text =
            "- **列表项**内容\n\n"
            "> *引用块*内容\n";
        mockBlock.ended = true;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        m_options.selectableRows = prevSelectable;

        bool listIsTextBox = false;
        bool quoteIsTextBox = false;
        std::string listText;
        std::string quoteText;
        for (size_t i = 0; i < mockRec.segments.size() && i < mockRec.segMeta.size(); ++i) {
            ECDI::Widget* segW = mockRec.segments[i];
            if (!segW || segW->GetChildCount() < 2) continue;
            if (mockRec.segMeta[i].kind == MarkdownSegmentKind::ListItem) {
                if (auto* tb = dynamic_cast<ECDI::TextBox*>(segW->GetChildAt(1))) {
                    listIsTextBox = true;
                    listText = tb->GetText();
                }
            } else if (mockRec.segMeta[i].kind == MarkdownSegmentKind::Quote) {
                if (auto* tb = dynamic_cast<ECDI::TextBox*>(segW->GetChildAt(1))) {
                    quoteIsTextBox = true;
                    quoteText = tb->GetText();
                }
            }
        }
        const bool ok = listIsTextBox && quoteIsTextBox &&
                        listText == "列表项内容" && quoteText == "引用块内容";
        add("render-selectable-rich", ok,
            "listTB=" + std::to_string(listIsTextBox) + " list='" + listText +
                "' quoteTB=" + std::to_string(quoteIsTextBox) + " quote='" + quoteText + "'");
    }

    // 28. render-codeblock-extent：Relayout 后代码块 ScrollView 真实测高且长行触发横向滚动
    {
        bool ok = false;
        std::string note = "missing sess/scroll/window";
        if (m_sess && m_scroll && m_window) {
            Block mockBlock;
            mockBlock.id = "mock_code_extent";
            mockBlock.kind = "assistant";
            mockBlock.text =
                "```cpp\n"
                "int a = 1;\n"
                "int b = 2;\n"
                "std::string very_long_identifier_for_horizontal_scroll_probe_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ = \"ok\";\n"
                "```";
            mockBlock.ended = true;

            RowRec rec;
            auto panel = BuildBlockRow(mockBlock, rec);
            rec.panel = panel.get();
            m_scroll->GetContentView().AddChild(std::move(panel));
            m_sess->rows.push_back(std::move(rec));
            Relayout();

            const RowRec& added = m_sess->rows.back();
            for (size_t i = 0; i < added.segments.size() && i < added.segMeta.size(); ++i) {
                if (added.segMeta[i].kind != MarkdownSegmentKind::CodeBlock) continue;
                ECDI::Widget* card = added.segments[i];
                if (!card || card->GetChildCount() < 2) break;
                auto* sv = dynamic_cast<ECDI::ScrollView*>(card->GetChildAt(1));
                if (!sv) break;
                const int extW = static_cast<int>(sv->GetContentExtent().width);
                const int extH = static_cast<int>(sv->GetContentExtent().height);
                const int svW = sv->GetWidth();
                const int cardH = card->GetHeight();
                ok = (extW > svW && extH > 40 && cardH > 60);
                note = "ext=" + std::to_string(extW) + "x" + std::to_string(extH) +
                       " svW=" + std::to_string(svW) + " cardH=" + std::to_string(cardH);
                break;
            }
        }
        add("render-codeblock-extent", ok, note);
    }

    if (m_application) m_application->Exit();
}

void HarnessApp::AppendMdProbeSummary(std::string& json) {
    json += "  \"mdProbe\": [";
    for (size_t i = 0; i < m_probeState.mdProbeLog.size(); ++i) {
        const MdProbeRec& r = m_probeState.mdProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}

// ── Markdown 表格渲染探针（--table-probe）─────────────────────
void HarnessApp::RunTableProbe() {
    if (m_probeState.tableProbeDone) return;
    m_probeState.tableProbeDone = true;

    // 1. parse-pure：标准管道表格、对齐、前后段落
    {
        TableProbeRec rec;
        rec.phase = "parse-pure";
        const std::string src =
            "前置段落\n\n"
            "| 特性 | 状态 | 覆盖率 |\n"
            "| :--- | :---: | ---: |\n"
            "| 软折行 | 已闭合 | 100% |\n"
            "| 浮层迁移 | 已闭合 | 100% |\n\n"
            "后置段落";

        auto segs = ParseMarkdownSegments(src);
        std::string errNote;
        if (segs.size() != 3) {
            errNote = "segs.size=" + std::to_string(segs.size());
        } else if (segs[0].kind != MarkdownSegmentKind::Paragraph || segs[0].text != "前置段落") {
            errNote = "seg[0] text='" + segs[0].text + "'";
        } else if (segs[1].kind != MarkdownSegmentKind::Table) {
            errNote = "seg[1] not table";
        } else if (segs[2].kind != MarkdownSegmentKind::Paragraph || segs[2].text != "后置段落") {
            errNote = "seg[2] text='" + segs[2].text + "'";
        } else {
            const auto& tbl = segs[1].table;
            if (tbl.headers.size() != 3 || tbl.headers[0] != "特性" || tbl.headers[1] != "状态" || tbl.headers[2] != "覆盖率") {
                errNote = "headers mismatch";
            } else if (tbl.alignments.size() != 3 || tbl.alignments[0] != MarkdownAlign::Left ||
                       tbl.alignments[1] != MarkdownAlign::Center || tbl.alignments[2] != MarkdownAlign::Right) {
                errNote = "alignments mismatch";
            } else if (tbl.rows.size() != 2 || tbl.rows[0][0] != "软折行" || tbl.rows[0][1] != "已闭合" || tbl.rows[0][2] != "100%" ||
                       tbl.rows[1][0] != "浮层迁移" || tbl.rows[1][1] != "已闭合" || tbl.rows[1][2] != "100%") {
                errNote = "rows mismatch";
            }
        }
        rec.matched = errNote.empty();
        if (!rec.matched) rec.note = errNote;
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 2. parse-non-table：非表格文本安全退化
    {
        TableProbeRec rec;
        rec.phase = "parse-non-table";
        const std::string nonTable =
            "这是一段普通文本\n"
            "| 看起来像表头 | 但下一行没有分隔线 |\n"
            "所以不是合法表格\n";
        auto segs = ParseMarkdownSegments(nonTable);
        bool ok = (segs.size() == 1 && segs[0].kind == MarkdownSegmentKind::Paragraph && segs[0].text == nonTable);
        rec.matched = ok;
        if (!ok) rec.note = "non-table should fallback to single paragraph";
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 3. parse-escaped-pipe：转义 \|
    {
        TableProbeRec rec;
        rec.phase = "parse-escaped-pipe";
        const std::string escaped =
            "| 表达式 | 含义 |\n"
            "| --- | --- |\n"
            "| a \\| b | 或操作 |\n";
        auto segs = ParseMarkdownSegments(escaped);
        bool ok = (segs.size() == 1 && segs[0].kind == MarkdownSegmentKind::Table);
        if (ok) {
            const auto& tbl = segs[0].table;
            ok = (tbl.rows.size() == 1 && tbl.rows[0].size() == 2 && tbl.rows[0][0] == "a | b" && tbl.rows[0][1] == "或操作");
        }
        rec.matched = ok;
        if (!ok) rec.note = "escaped pipe mismatch";
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 4. streaming-fallback：流式未结束不切分
    {
        TableProbeRec rec;
        rec.phase = "streaming-fallback";
        Block mockBlock;
        mockBlock.id = "mock_stream";
        mockBlock.kind = "assistant";
        mockBlock.text = "| A | B |\n| --- | --- |\n| 1 | 2 |\n";
        mockBlock.ended = false;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        bool ok = (mockRec.hasTable() == false && mockRec.label != nullptr);
        rec.matched = ok;
        if (!ok) rec.note = "streaming block must not split table prematurely";
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 5. block-end-upgrade：结束块升级为 ScrollView 表格控件
    {
        TableProbeRec rec;
        rec.phase = "block-end-upgrade";
        Block mockBlock;
        mockBlock.id = "mock_ended";
        mockBlock.kind = "assistant";
        mockBlock.text = "前缀\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\n后缀";
        mockBlock.ended = true;

        RowRec mockRec;
        auto rowPanel = BuildBlockRow(mockBlock, mockRec);
        bool ok = (mockRec.hasTable() == true && mockRec.segments.size() >= 2);
        if (ok) {
            bool foundSv = false;
            for (auto* w : mockRec.segments) {
                if (dynamic_cast<ECDI::ScrollView*>(w)) {
                    foundSv = true;
                    break;
                }
            }
            ok = foundSv;
        }
        rec.matched = ok;
        if (!ok) rec.note = "ended block must assemble table scrollview";
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 6. wide-scroll-container：超宽表格 content extent 超过版心封顶
    {
        TableProbeRec rec;
        rec.phase = "wide-scroll-container";
        MarkdownTable wideTable;
        for (int i = 0; i < 8; ++i) {
            wideTable.headers.push_back("超长字段标题列 " + std::to_string(i + 1));
            wideTable.alignments.push_back(MarkdownAlign::Left);
        }
        std::vector<std::string> rowCells;
        for (int i = 0; i < 8; ++i) {
            rowCells.push_back("单元格长内容数据项 " + std::to_string(i + 1));
        }
        wideTable.rows.push_back(rowCells);

        RowRec::TableRec trec;
        auto tableWidget = BuildTableWidget(wideTable, trec);
        auto* sv = dynamic_cast<ECDI::ScrollView*>(tableWidget.get());
        bool ok = (sv != nullptr);
        if (ok) {
            const int extentW = static_cast<int>(sv->GetContentExtent().width);
            ok = (extentW > 760);
        }
        rec.matched = ok;
        if (!ok) rec.note = "wide table content extent must exceed transcript layout max width";
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    // 7. table-inline-cells：表头与单元格接通 ApplyInlineText 且按去标记文本量自然列宽
    {
        TableProbeRec rec;
        rec.phase = "table-inline-cells";
        MarkdownTable styled;
        styled.headers = {"**指令**", "`状态`"};
        styled.alignments = {MarkdownAlign::Left, MarkdownAlign::Center};
        styled.rows = {{"运行 `git status`", "*正常*"}};

        MarkdownTable plain;
        plain.headers = {"指令", "状态"};
        plain.alignments = {MarkdownAlign::Left, MarkdownAlign::Center};
        plain.rows = {{"运行 git status", "正常"}};

        RowRec::TableRec sRec;
        auto sW = BuildTableWidget(styled, sRec);
        RowRec::TableRec pRec;
        auto pW = BuildTableWidget(plain, pRec);
        (void)sW;
        (void)pW;

        const bool textOk =
            sRec.headerLabels.size() == 2 && sRec.headerLabels[0] && sRec.headerLabels[1] &&
            sRec.headerLabels[0]->GetText() == "指令" &&
            sRec.headerLabels[1]->GetText() == "状态" &&
            sRec.cellLabels.size() == 1 && sRec.cellLabels[0].size() == 2 &&
            sRec.cellLabels[0][0] && sRec.cellLabels[0][1] &&
            sRec.cellLabels[0][0]->GetText() == "运行 git status" &&
            sRec.cellLabels[0][1]->GetText() == "正常";
        const bool widthOk = (sRec.naturalW == pRec.naturalW);
        rec.matched = textOk && widthOk;
        rec.note = "textOk=" + std::to_string(textOk) +
                   " widthOk=" + std::to_string(widthOk);
        m_probeState.tableProbeLog.push_back(std::move(rec));
    }

    if (m_application) m_application->Exit();
}

void HarnessApp::AppendTableProbeSummary(std::string& json) {
    json += "  \"tableProbe\": [";
    for (size_t i = 0; i < m_probeState.tableProbeLog.size(); ++i) {
        const TableProbeRec& r = m_probeState.tableProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}

// ★★ 按轮次合并复制体系（--copy-probe）：仅在每轮智能体回答末尾显示复制按钮，复制本轮全部 assistant 文本
void HarnessApp::RunCopyProbe() {
    if (!m_sess || !m_sess->replay || !m_window) {
        m_probeState.copyProbeDone = true;
        return;
    }

    auto readClip = [&]() {
        return m_window->GetPlatformWindow().GetClipboardText();
    };

    auto clickBtn = [&](ECDI::Button* b, ECDI::Widget* rowPanel = nullptr) {
        if (!b) return false;
        if (m_scroll && rowPanel) {
            const int panelY = rowPanel->GetY();
            m_scroll->SetContentOffset(0, std::min(panelY, m_scroll->GetMaxOffsetY()));
        }
        const ECDI::Point abs = b->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + b->GetWidth() / 2,
                          static_cast<int>(abs.y) + b->GetHeight() / 2);
        return true;
    };

    // 1. turn-button-placement：用户消息、工具调用/结果、思考块、权限门均无复制按钮；
    //    每轮对话只有最后一个已完成的 assistant 块显示复制按钮，中间 assistant 块隐藏复制按钮。
    {
        CopyProbeRec rec;
        rec.phase = "turn-button-placement";
        bool noNonAssistantCopyBtn = true;
        int visibleAsstCopyBtns = 0;
        int hiddenIntermediateAsstBtns = 0;
        for (const RowRec& r : m_sess->rows) {
            const bool isAsst = (!r.alignRight && !r.tool && !r.reasoning && !r.ask && !r.blockId.empty());
            if (!isAsst) {
                if (r.copyBtn != nullptr) {
                    noNonAssistantCopyBtn = false;
                }
            } else if (r.copyBtn) {
                if (r.copyBtn->IsVisible()) {
                    ++visibleAsstCopyBtns;
                } else {
                    ++hiddenIntermediateAsstBtns;
                }
            }
        }
        rec.matched = (noNonAssistantCopyBtn && visibleAsstCopyBtns > 0);
        if (!rec.matched) {
            rec.note = "noNonAsst=" + std::to_string(noNonAssistantCopyBtn) +
                       " visible=" + std::to_string(visibleAsstCopyBtns) +
                       " hiddenIntermediate=" + std::to_string(hiddenIntermediateAsstBtns);
        } else {
            rec.note = "visibleTurns=" + std::to_string(visibleAsstCopyBtns) +
                       " hiddenIntermediate=" + std::to_string(hiddenIntermediateAsstBtns);
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    // 2. copy-turn-merged：点击首轮末尾 assistant 的复制按钮，验证复制内容合并了本轮全部 assistant 文本，
    //    且不包含 user 消息或 tool_call/tool_result 过程性内容。
    {
        CopyProbeRec rec;
        rec.phase = "copy-turn-merged";
        RowRec* firstTurnLastAsst = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (!r.alignRight && !r.tool && !r.reasoning && !r.ask && r.copyBtn && r.copyBtn->IsVisible()) {
                firstTurnLastAsst = &r;
                break;
            }
        }
        if (!firstTurnLastAsst) {
            rec.matched = false;
            rec.note = "no visible turn copyBtn";
        } else {
            const std::string expected = GetTurnCopyText(firstTurnLastAsst->blockId);
            clickBtn(firstTurnLastAsst->copyBtn, firstTurnLastAsst->panel);
            const std::string clip = readClip();
            const bool textMatched = (clip == expected && !clip.empty());
            const bool btnUpdated = (firstTurnLastAsst->copyBtn->GetText() == "已复制");
            rec.matched = (textMatched && btnUpdated);
            if (!rec.matched) {
                rec.note = "textMatched=" + std::to_string(textMatched) +
                           " btnUpdated=" + std::to_string(btnUpdated);
            }
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    // 3. copy-turn-with-table：若存在包含 Markdown 表格的轮次（哪怕表格在该轮中间块），
    //    点击该轮末尾复制按钮也能完整复制出 Markdown 表格源码与后续结论。
    {
        CopyProbeRec rec;
        rec.phase = "copy-turn-with-table";
        RowRec* tableTurnEndRow = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (!r.alignRight && !r.tool && !r.reasoning && !r.ask && r.copyBtn && r.copyBtn->IsVisible()) {
                const std::string turnText = GetTurnCopyText(r.blockId);
                if (turnText.find('|') != std::string::npos) {
                    tableTurnEndRow = &r;
                    break;
                }
            }
        }
        if (!tableTurnEndRow) {
            rec.phase = "skipped-no-table-turn";
            rec.matched = true;
            rec.note = "run with fixture containing markdown table";
        } else {
            const std::string expected = GetTurnCopyText(tableTurnEndRow->blockId);
            clickBtn(tableTurnEndRow->copyBtn, tableTurnEndRow->panel);
            const std::string clip = readClip();
            const bool hasPipe = (clip.find('|') != std::string::npos);
            const bool textMatched = (clip == expected && !clip.empty());
            rec.matched = (textMatched && hasPipe);
            if (!rec.matched) {
                rec.note = "textMatched=" + std::to_string(textMatched) +
                           " hasPipe=" + std::to_string(hasPipe);
            }
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    // 4. streaming-hidden：当轮次内有块处于未完成流式态（!ended）时，该轮复制按钮隐藏；完成后恢复显示。
    {
        CopyProbeRec rec;
        rec.phase = "streaming-hidden";
        RowRec* targetRow = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (!r.alignRight && !r.tool && !r.reasoning && !r.ask && r.copyBtn && r.copyBtn->IsVisible()) {
                targetRow = &r;
                break;
            }
        }
        if (!targetRow) {
            rec.matched = false;
            rec.note = "no target row";
        } else {
            targetRow->ended = false;
            SyncTurnCopyButtons();
            const bool hiddenWhileStreaming = !targetRow->copyBtn->IsVisible();
            targetRow->ended = true;
            SyncTurnCopyButtons();
            const bool visibleAfterEnd = targetRow->copyBtn->IsVisible();
            rec.matched = (hiddenWhileStreaming && visibleAfterEnd);
            if (!rec.matched) {
                rec.note = "hiddenWhileStreaming=" + std::to_string(hiddenWhileStreaming) +
                           " visibleAfterEnd=" + std::to_string(visibleAfterEnd);
            }
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    // 5. copy-all-assistant：顶栏菜单「复制全部」仅复制所有轮次的智能体回答文本
    {
        CopyProbeRec rec;
        rec.phase = "copy-all-assistant";
        CopyAllTranscriptText();
        const std::string clip = readClip();
        const std::string expected = BuildAllAssistantCopyText();
        const bool noToolTag = (clip.find("[tool_call]") == std::string::npos &&
                                clip.find("[tool_result]") == std::string::npos &&
                                clip.find("[user]") == std::string::npos);
        rec.matched = (!clip.empty() && clip == expected && noToolTag);
        if (!rec.matched) {
            rec.note = "clip len=" + std::to_string(clip.size()) +
                       " expected len=" + std::to_string(expected.size()) +
                       " noToolTag=" + std::to_string(noToolTag);
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    // 6. copy-turn-anchor：锚点断言（**非自洽**）。copy-turn-merged 的 expected 由
    //    GetTurnCopyText 本人生成，clip==expected 只证明「按钮把函数结果写进剪贴板」，
    //    合并逻辑若错则两边一起错（自洽 ≠ 正确）。本相的期望值直接来自夹具原文：
    //    合并结果必须包含首轮首个 assistant 块的已知开头，且不得混入 user 消息。
    //    skipped 判据也只查模型里的块原文（不经过合并函数），避免真缺陷被掩成 skipped。
    {
        CopyProbeRec rec;
        rec.phase = "copy-turn-anchor";
        // protocol/fixtures/realistic/realistic-session.jsonl：b1（assistant）/ b0（user）
        const std::string kAnchor = "先说结论：**判据必须是严格大于零**";
        const std::string kUserAnchor = "我在整理渲染管线的资料";
        const Block* b1 = m_sess->transcript.FindBlock("b1");
        RowRec* firstTurnLastAsst = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (!r.alignRight && !r.tool && !r.reasoning && !r.ask && r.copyBtn && r.copyBtn->IsVisible()) {
                firstTurnLastAsst = &r;
                break;
            }
        }
        if (!b1 || b1->text.find(kAnchor) == std::string::npos) {
            rec.phase = "skipped-anchor-not-in-fixture";
            rec.matched = true;
            rec.note = "run with realistic fixture for anchor assertions";
        } else if (!firstTurnLastAsst) {
            rec.matched = false;
            rec.note = "no visible turn copyBtn";
        } else {
            clickBtn(firstTurnLastAsst->copyBtn, firstTurnLastAsst->panel);
            const std::string clip = readClip();
            const bool hasAnchor = (clip.find(kAnchor) != std::string::npos);
            const bool noUser = (clip.find(kUserAnchor) == std::string::npos);
            rec.matched = (hasAnchor && noUser);
            if (!rec.matched) {
                rec.note = "hasAnchor=" + std::to_string(hasAnchor) +
                           " noUser=" + std::to_string(noUser);
            }
        }
        m_probeState.copyProbeLog.push_back(std::move(rec));
    }

    m_probeState.copyProbeDone = true;
    if (m_application) m_application->Exit();
}

void HarnessApp::AppendCopyProbeSummary(std::string& json) {
    json += "  \"copyProbe\": [";
    for (size_t i = 0; i < m_probeState.copyProbeLog.size(); ++i) {
        const CopyProbeRec& r = m_probeState.copyProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                      r.phase.c_str(), r.matched ? "true" : "false", r.note.c_str());
        json += b;
    }
    json += "],\n";
}
