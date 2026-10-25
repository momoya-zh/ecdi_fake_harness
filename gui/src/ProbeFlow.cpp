// ============================================================
// 交互流探针：Enter 发送 / 输入框长高 / 跳到最新 / 全局快捷键 / 设置面板 / 工作区
// ------------------------------------------------------------
// 判据形态：合成真实事件走完整派发链（SynthesizeKey / OnEvent），再回读模型位与控件位。
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
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"

// 跨 TU 共享符号（定义见 AppInternal.h / PathUtil.cpp）
using harness::internal::kPinThreshold;
using harness::internal::WideFromUtf8Path;
using harness::internal::PathToUtf8;


// ★★ 2026-10-21：**Enter 发送 / Shift+Enter 换行**的取证（--enter-probe）。
//
// 【它要回答什么】改前输入框是单行的（`SetSingleLine(true)`），Enter 被基类当
//   no-op 吞掉，Shift+Enter 同样（单行模式下 `InsertText("\n")` 不执行）
//   ⇒ 聊天框**只能点按钮发**。这条缺陷在任何既有读数里都不可见：`blocks` 只
//   反映"发了几条"，而没有任何探针**按过键**。
//
// 【为什么每一相都要合成真按键】直接调 `PromptBar::Submit()` 会把
//   "这个按键真的派给了输入框吗" 绕过——而那正是本步要证明的东西。
//   同 `--hit-probe` / `--sidebar-probe` 的点击臂纪律。
//
// 【"发出去了"怎么读——这一条我第一版写错了，记在这里】
//   第一版去数 `timeline` 里 `kind=="user"` 的块，断言"发送相要比之前多 1"。
//   实测 `enter-sends` 红了（1 -> 1），而**错的不是实现**：回放模式下
//   `SendPrompt` 走的是 `AddNoticeRow("[prompt 仅记录] …")`——它只追加一条
//   **通知行**，永远不产生 user 块（转录的唯一作者是 core，见 PROTOCOL:244）。
//   ⇒ 判据改成读**它真正做的事**：发送 = `timeline` 里多出一条 Notice。
//   （顺带说明为什么不能用 `timeline.size()` 整体：抑制相若也让它变，就分不清
//     "发了一条"和"多了一行别的"。所以两面都记：sent 相比 Notice 数 +1。）
void HarnessApp::RunEnterProbe() {
    if (!m_sess || !m_promptBar || !m_promptBar->input()) {
        m_enterProbeDone = true;
        return;
    }
    // ★ 只在回放模式：live 下发送会真的写进子进程管道，探针不该制造真副作用。
    if (!m_sess->replay) { m_enterProbeDone = true; return; }

    ECDI::TextBox* const input = m_promptBar->input();

    // 数 Notice 条目：回放模式下"发送"的可观测副作用就是这一行。
    //   ★ 不数 `kind=="user"` 的块——回放模式永远不会有新的（transcript 只由
    //     core 产生，GUI 不是作者）。第一版正是错在这里。
    auto noticeCount = [&]() -> size_t {
        size_t n = 0;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Notice) ++n;
        }
        return n;
    };

    auto snap = [&](const char* phase, size_t before, bool sent) {
        EnterProbeRec rec;
        rec.phase = phase;
        rec.promptCount = noticeCount();     // 字段名沿用；语义 = "发送副作用条数"
        rec.inputText = input->GetText();
        rec.noticeRows = rec.promptCount;
        rec.sent = sent;
        // 判据：
        //   · 发送相：Notice 必须 = before+1，且输入框必须空
        //   · 抑制相：Notice 必须 = before
        std::string bad;
        if (sent) {
            if (rec.promptCount != before + 1) {
                bad = "expected one new prompt record (" + std::to_string(before) + " -> " +
                      std::to_string(rec.promptCount) + ")";
            }
            if (!rec.inputText.empty()) {
                if (!bad.empty()) bad += "; ";
                bad += "input was not cleared after sending";
            }
        } else {
            if (rec.promptCount != before) {
                bad = "a prompt was sent although it should not be (" +
                      std::to_string(before) + " -> " +
                      std::to_string(rec.promptCount) + ")";
            }
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        m_enterProbeLog.push_back(std::move(rec));
    };

    // 焦点守卫：合成按键之前记下焦点，收尾复原（同 `RunHitProbe` 的收尾纪律）。
    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // ① enter-sends：有文字 ⇒ Enter 必须发出去，且框被清空。
    {
        const size_t before = noticeCount();
        input->SetText("probe-enter-sends");
        input->MoveCaretToEnd();
        if (m_window) m_window->SetFocusedWidget(input);   // 前置：焦点必须在框里
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/false);
        snap("enter-sends", before, /*sent=*/true);
    }

    // ② shift-enter-newline：Shift+Enter 必须**不**发送，且文本真的多一个换行。
    {
        const size_t before = noticeCount();
        input->SetText("line1");
        input->MoveCaretToEnd();
        if (m_window) m_window->SetFocusedWidget(input);
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/true);
        snap("shift-enter-newline", before, /*sent=*/false);
        // ★ 额外判据：换行真的进去了（抑制相只证明"没发"，不证明"换了行"——
        //   一个把 Shift+Enter 整个吞掉的实现同样满足"没发"）。
        EnterProbeRec& rec = m_enterProbeLog.back();
        if (rec.inputText.find('\n') == std::string::npos) {
            rec.matched = false;
            if (!rec.firstDivergence.empty()) rec.firstDivergence += "; ";
            rec.firstDivergence += "Shift+Enter did not insert a newline (text=" +
                                   rec.inputText + ")";
        }
        input->SetText("");
    }

    // ③ enter-empty：空框按 Enter ⇒ 不发送。
    {
        const size_t before = noticeCount();
        input->SetText("");
        if (m_window) m_window->SetFocusedWidget(input);
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/false);
        snap("enter-empty", before, /*sent=*/false);
    }

    // ④ enter-whitespace：只有空白 ⇒ 不发送。
    //   ★ 这一相比 ③ 强：`!text.empty()` 判不出"看起来有内容"的空白串，
    //     而用户敲了 Shift+Enter 再回删，剩下的正好就是 "\n"。
    {
        const size_t before = noticeCount();
        input->SetText("  \n ");
        if (m_window) m_window->SetFocusedWidget(input);
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/false);
        snap("enter-whitespace", before, /*sent=*/false);
        input->SetText("");
    }

    // ⑤ enter-unfocused：★ 负对照——焦点不在输入框时，同一个合成按键必须**不**发送。
    //   若这一相也发了，说明"发送"根本不取决于事件派给谁 ⇒ 前面四相证明不了什么。
    {
        const size_t before = noticeCount();
        input->SetText("probe-not-focused");
        input->MoveCaretToEnd();
        // 把焦点交给发送按钮（一个 CanFocus 的别的控件）
        if (m_window && m_promptBar->sendButton()) {
            m_window->SetFocusedWidget(m_promptBar->sendButton());
        }
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/false);
        snap("enter-unfocused", before, /*sent=*/false);
        input->SetText("");
    }

    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_enterProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ 2026-10-21：输入框自动长高探针（--grow-probe）。
//
// 【为什么这条不能只测函数】`PromptBar::DesiredInputHeight` 算得对、
//   `SyncInputHeight` 也写进去了，仍然可能是"没人调用"——0c 记下的那个家族
//   type-newline / send-reset 用 `SynthesizeKey` 验证真实派发；clamp / shrink phases 设置
//   明确文本状态验证尺寸策略。resize-adapt 走 `ProbeResizeClient` 的真实窗口事件链，
//   不手动调用 `SyncInputHeight()`，以验证 resize 回调确实更新了输入框高度。
//
// 【为什么 `shrink` 相是这个功能唯一的负对照】最诱人的错误实现是用
//   `GetMaxScrollOffset() + GetTextAreaHeight()` 当内容高（`TextBox.cpp:541` 的
//   `contentExtent` 式子）。那个式子带 `max(0, ...)` ⇒ **不溢出时恒等于视口高**。
//   用它的实现在所有"变高"相里**全绿**，只有删掉换行时才暴露：
//   框会停在最大高度再也不下来。故 `shrink` 不是补充，它是唯一能打红这个陷阱的方向。
//
// 【为什么还要 `exact-fit` 相】行高是 float（雅黑 13px ≈ 19~20），而
//   `Widget::AutoSize` 是 `static_cast<int>` **截断**（`Widget.cpp`）。
//   若本实现也截断，则"恰好装下"会变成"差一点装下" ⇒ `GetMaxScrollOffset > 0`
//   ⇒ 输入框里冒出一条**不该有的内部滚动条**，且最后一行被切掉零点几像素。
//   判据 = 恰好 N 行时 `GetMaxScrollOffset()` 必须 == 0。
void HarnessApp::RunGrowProbe() {
    if (!m_sess || !m_promptBar || !m_promptBar->input()) {
        m_growProbeDone = true;
        return;
    }
    // 只在回放模式：live 下发送会真的写进子进程管道，探针不该制造真副作用。
    if (!m_sess->replay) { m_growProbeDone = true; return; }

    ECDI::TextBox* const input = m_promptBar->input();
    PromptBar* const bar = m_promptBar;

    // 行数 = `\n` 数 + 1（与产品实现同一口径；探针要独立算一遍，否则它只是在读回自己的实现）
    auto linesOf = [](const std::string& text) {
        int n = 1;
        for (char c : text) if (c == '\n') ++n;
        return n;
    };
    // Independent oracle: validate the UI policy rather than reading the product getter back.
    auto expectedMaxForRootHeight = [](int rootH) {
        if (rootH <= 0) return PromptBar::kInputMaxH;
        const float scaled = static_cast<float>(rootH) * (PromptBar::kInputMaxH / 700.0f);
        return std::clamp(static_cast<int>(std::round(scaled)), 46, 260);
    };

    auto snap = [&](const char* phase, const std::string& why) {
        GrowProbeRec rec;
        rec.phase = phase;
        rec.rootH = m_window ? m_window->GetRootWidget().GetHeight() : 0;
        rec.maxInputH = bar->MaxInputHeight();
        rec.inputH = bar->inputHeight();
        rec.barH = bar->GetHeight();
        rec.lines = linesOf(input->GetText());
        // 提示行 / 发送按钮的位置：判"输入框变高后有没有把它们压住"。
        //   ★ 这两个 Y 在本轮之前是硬编码 66 / 58，输入框一变高就会与它重叠——
        //     而那是一次**没有任何读数会报**的重叠（控件都在、只是叠在一起）。
        rec.hintY = -1;
        rec.buttonY = -1;
        if (ECDI::Widget* hint = bar->GetChildAt(0)) {
            // GetChildAt(0) = 输入框；提示行与按钮按 AddChild 顺序排在其后
            (void)hint;
        }
        for (size_t i = 0; i < bar->GetChildCount(); ++i) {
            ECDI::Widget* child = bar->GetChildAt(i);
            if (child == input) continue;
            if (dynamic_cast<ECDI::Button*>(child) && rec.buttonY < 0) {
                rec.buttonY = child->GetY();
            } else if (dynamic_cast<ECDI::Label*>(child)) {
                rec.hintY = child->GetY();
            }
        }
        rec.matched = why.empty();
        rec.firstDivergence = why;
        m_growProbeLog.push_back(std::move(rec));
    };

    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // 单行基准高：由**产品路径**给出（不是探针自己按行高算的），
    //   这样后面的判据全部相对于它，字体/DPI 变化不会让判据变脆。
    input->SetText("");
    bar->SyncInputHeight();
    const int singleH = bar->inputHeight();
    {
        std::string why;
        if (singleH <= 0) why = "single-line height is not positive";
        snap("baseline", why);
    }

    // ① type-newline：Shift+Enter 打**真按键** ⇒ 必须真的变高。
    //   ★ 这一相考的是"键盘 → 文本变化 → OnTextChanged → SyncInputHeight"整条链。
    {
        input->SetText("line1");
        input->MoveCaretToEnd();
        if (m_window) m_window->SetFocusedWidget(input);
        const int before = bar->inputHeight();
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/true);
        std::string why;
        if (input->GetText().find('\n') == std::string::npos) {
            why = "Shift+Enter did not insert a newline";
        } else if (bar->inputHeight() <= before) {
            why = "the box did not grow after a newline (" + std::to_string(before) + " -> " +
                  std::to_string(bar->inputHeight()) + ")";
        }
        snap("type-newline", why);
    }

    // ② grow：3 行必须比 1 行高，且提示行/按钮**跟着下移**（不是被压住）。
    {
        input->SetText("aa\nbb\ncc");
        bar->SyncInputHeight();
        std::string why;
        const int h3 = bar->inputHeight();
        if (h3 <= singleH) {
            why = "3 lines are not taller than 1 line (" + std::to_string(singleH) + " -> " +
                  std::to_string(h3) + ")";
        }
        // 压住判据：提示行与按钮的 Y 必须 >= 输入框底边
        const int inputBottom = input->GetY() + input->GetHeight();
        for (size_t i = 0; i < bar->GetChildCount() && why.empty(); ++i) {
            ECDI::Widget* child = bar->GetChildAt(i);
            if (child == input) continue;
            if (child->GetY() < inputBottom) {
                why = "a sibling is overlapped by the taller input (sibling y=" +
                      std::to_string(child->GetY()) + " < input bottom=" +
                      std::to_string(inputBottom) + ")";
            }
        }
        snap("grow", why);
    }

    // ③ max-clamp：30 行必须封顶当前 root 高度对应的动态上限。
    const int baseRootH = m_window ? m_window->GetRootWidget().GetHeight() : 0;
    const int expectedBaseMax = expectedMaxForRootHeight(baseRootH);
    {
        std::string many;
        for (int i = 0; i < 30; ++i) {
            if (i) many += '\n';
            many += "x";
        }
        input->SetText(many);
        bar->SyncInputHeight();
        std::string why;
        if (bar->MaxInputHeight() != expectedBaseMax) {
            why = "reported cap at rootH=" + std::to_string(baseRootH) + " is " +
                  std::to_string(bar->MaxInputHeight()) + " (expected " +
                  std::to_string(expectedBaseMax) + ")";
        } else if (bar->inputHeight() != expectedBaseMax) {
            why = "30 lines are not clamped to adaptive max at rootH=" +
                  std::to_string(baseRootH) + " (inputH=" +
                  std::to_string(bar->inputHeight()) + " != " +
                  std::to_string(expectedBaseMax) + ")";
        }
        snap("max-clamp", why);
    }

    // ④ resize-adapt：30 行保持不变，真实改变窗口客户区高度；不手动同步输入框。
    {
        ECDI::Size baseClient{};
        if (m_window) baseClient = m_window->GetPlatformWindow().GetClientSize();
        const int baseClientW = static_cast<int>(baseClient.width);
        const int baseClientH = static_cast<int>(baseClient.height);
        int targetClientH = baseClientH + 140;
        if (expectedMaxForRootHeight(baseRootH + targetClientH - baseClientH) == expectedBaseMax) {
            targetClientH = baseClientH;
            for (int delta = 140; delta <= baseClientH - kMinClientH; delta += 140) {
                const int candidateClientH = baseClientH - delta;
                const int candidateRootH = baseRootH - delta;
                if (expectedMaxForRootHeight(candidateRootH) != expectedBaseMax) {
                    targetClientH = candidateClientH;
                    break;
                }
            }
        }

        std::string why;
        if (!m_window || baseClientW <= 0 || baseClientH <= 0) {
            why = "cannot exercise adaptive cap without a valid platform client size";
            snap("resize-adapt", why);
            snap("resize-return", why);
        } else {
            ProbeResizeClient(baseClientW, targetClientH);
            const int resizedRootH = m_window->GetRootWidget().GetHeight();
            const int expectedResizedMax = expectedMaxForRootHeight(resizedRootH);
            if (resizedRootH == baseRootH) {
                why = "window resize did not change root height (" +
                      std::to_string(baseRootH) + ")";
            } else if (expectedResizedMax == expectedBaseMax) {
                why = "test resize did not cross an adaptive cap step (" +
                      std::to_string(expectedBaseMax) + " -> " +
                      std::to_string(expectedResizedMax) + ")";
            } else if (bar->MaxInputHeight() != expectedResizedMax) {
                why = "reported cap did not follow resized root height (" +
                      std::to_string(bar->MaxInputHeight()) + " != " +
                      std::to_string(expectedResizedMax) + ")";
            } else if (bar->inputHeight() != expectedResizedMax) {
                why = "30-line input did not follow resized cap (" +
                      std::to_string(bar->inputHeight()) + " != " +
                      std::to_string(expectedResizedMax) + ")";
            }
            snap("resize-adapt", why);

            ProbeResizeClient(baseClientW, baseClientH);
            const int returnedRootH = m_window->GetRootWidget().GetHeight();
            const int expectedReturnedMax = expectedMaxForRootHeight(returnedRootH);
            std::string returnWhy;
            if (returnedRootH != baseRootH) {
                returnWhy = "root height did not return after probe resize (" +
                            std::to_string(baseRootH) + " -> " +
                            std::to_string(returnedRootH) + ")";
            } else if (bar->MaxInputHeight() != expectedReturnedMax ||
                       bar->inputHeight() != expectedReturnedMax) {
                returnWhy = "input cap/height did not return with the window (max=" +
                            std::to_string(bar->MaxInputHeight()) + ", input=" +
                            std::to_string(bar->inputHeight()) + ", expected=" +
                            std::to_string(expectedReturnedMax) + ")";
            }
            snap("resize-return", returnWhy);
        }
    }

    // ⑥ send-reset：Enter 发出 ⇒ 高度回到单行。
    //   ★ 这一相考的是 `SetText` 路径：`SetText` 不走 `OnTextChanged`（D7 契约），
    //     所以"发送后清空"这个副作用**必须**在 App 侧显式同步一次。
    //     漏了它的症状：消息发完，框里空了但仍然占着 3 行高，看起来像卡住。
    {
        input->SetText("a\nb\nc");
        bar->SyncInputHeight();
        const int tall = bar->inputHeight();
        input->MoveCaretToEnd();
        if (m_window) m_window->SetFocusedWidget(input);
        SynthesizeKey(ECDI::KeyCode::Enter, /*shift=*/false);
        std::string why;
        if (!input->GetText().empty()) {
            why = "Enter did not send (text is still there)";
        } else if (bar->inputHeight() != singleH) {
            why = "the box did not return to single-line height after sending (" +
                  std::to_string(tall) + " -> " + std::to_string(bar->inputHeight()) +
                  ", expected " + std::to_string(singleH) + ")";
        }
        snap("send-reset", why);
    }

    // ⑦ shrink：删掉换行后必须**变矮**。
    //   ★★ 这是唯一能打红 `max(0, ...)` 陷阱的方向（见本函数头注）。
    {
        input->SetText("aa\nbb\ncc");
        bar->SyncInputHeight();
        const int tall = bar->inputHeight();
        input->SetText("aa");
        bar->SyncInputHeight();
        std::string why;
        if (bar->inputHeight() >= tall) {
            why = "the box did not shrink after the newlines were removed (" +
                  std::to_string(tall) + " -> " + std::to_string(bar->inputHeight()) + ")";
        }
        if (bar->inputHeight() != singleH) {
            if (!why.empty()) why += "; ";
            why += "shrink did not land on the single-line height (" +
                   std::to_string(bar->inputHeight()) + " != " + std::to_string(singleH) + ")";
        }
        snap("shrink", why);
    }

    // ⑧ exact-fit：恰好装下时，`GetMaxScrollOffset()` 必须 == 0。
    //   ★ 取整方向判据（`ceil` vs 截断）。截断实现会在这里多出一条内部滚动条。
    {
        input->SetText("a\nb\nc");
        bar->SyncInputHeight();
        std::string why;
        // GetMaxScrollOffset 是 private；`GetScrollOffsetY`/滚动条可见性是公开观察口。
        //   ★ 用滚动条的可见性做判据：`SyncScrollBar` 的判据正是
        //     `!singleLine && GetMaxScrollOffset() > 0`（`TextBox.cpp:540`），
        //     故"恰好装下 ⇒ 条不可见"与"偏移为 0"是同一件事的两个可观测面。
        if (ECDI::ScrollBar* vbar = input->GetVerticalScrollBar()) {
            if (vbar->IsVisible()) {
                why = "a scrollbar appeared although the content should fit exactly "
                      "(rounding direction is wrong — truncation instead of ceil)";
            }
        }
        if (input->GetScrollOffsetY() != 0.0f) {
            if (!why.empty()) why += "; ";
            why += "scroll offset is not zero (" + std::to_string(input->GetScrollOffsetY()) + ")";
        }
        snap("exact-fit", why);
    }

    // 收尾：清干净，恢复焦点（同 `RunHitProbe` / `RunEnterProbe` 的收尾纪律）。
    input->SetText("");
    bar->SyncInputHeight();
    if (m_window) m_window->SetFocusedWidget(focusBefore);

    m_growProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4 Phase A（--jump-probe，2026-10-25）：「跳到最新」按钮判据。
//
// 【它要回答什么】① 按钮显隐与贴底态**恒一致**（不变量）；② 点击**真的回底**；
//   ③ 上翻后按钮**真的出现**（滚轮路径接上了刷新）。
// ★ 全部走**真实派发链**（OnEvent 合成滚轮 / SynthesizeClickAt 合成点击），
//     不直接调 SetContentOffset/RefreshJumpButton——直接调会把"事件真的会触发
//     按钮刷新吗"整个绕过（同 SynthesizeKey 的纪律）。
// 【为什么不假设"加载完 = 贴底"】instant 装载大夹具是**一个 tick 填满全部行**，
//   填之前 offset=0 不在底部 ⇒ FollowTailIfPinned 判非 pinned、不跟随——这是
//   既有行为（也正因如此大夹具加载后停在顶部，需要本按钮"点得回"）。
//   故探针以「点击到尾」为起点造出贴底态，再验上翻/回打的往返闭环。
void HarnessApp::RunJumpProbe() {
    if (!m_sess || !m_sess->replay || !m_scroll || !m_hud.jumpLatest) {
        m_jumpProbeDone = true;
        return;
    }
    auto snap = [&](const char* phase) {
        JumpProbeRec rec;
        rec.phase = phase;
        rec.offset = m_scroll->GetScrollOffsetY();
        rec.maxOffset = m_scroll->GetMaxOffsetY();
        rec.visible = m_hud.jumpLatest->IsVisible();
        // 不变量：显隐 == 离开贴底带 ∧ 有可滚内容。
        const bool pinned = rec.offset >= rec.maxOffset - kPinThreshold;
        rec.matched = (rec.visible == (!pinned && rec.maxOffset > 0));
        if (!rec.matched) rec.note = "visibility diverges from pinned state";
        m_jumpProbeLog.push_back(std::move(rec));
        return m_jumpProbeLog.back().matched;
    };
    auto clickJumpButton = [&]() {
        ECDI::Button* const btn = m_hud.jumpLatest;
        const ECDI::Point bAbs = btn->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(bAbs.x) + btn->GetWidth() / 2,
                          static_cast<int>(bAbs.y) + btn->GetHeight() / 2);
    };
    // 焦点复原同 RunHitProbe 的收尾纪律（点击把焦点挪到按钮 ⇒ caret 闪烁停 ⇒
    // paints 相位扰动）。
    ECDI::Widget* const focusBefore =
        m_window ? m_window->GetFocusedWidget() : nullptr;

    const int maxY0 = m_scroll->GetMaxOffsetY();
    if (maxY0 <= kPinThreshold + 2 * m_scroll->GetScrollStep()) {
        // 夹具内容装不满视口（或差一两滚就没滚动范围）⇒ 往返闭环造不出区分力，
        // 如实记 skipped（不许静默绿——那是"探针没跑"伪装成"判据通过"）。
        JumpProbeRec rec;
        rec.phase = "skipped-short-fixture";
        rec.offset = m_scroll->GetScrollOffsetY();
        rec.maxOffset = maxY0;
        rec.visible = m_hud.jumpLatest->IsVisible();
        rec.matched = false;
        rec.note = "fixture content fits viewport; no scroll range to probe";
        m_jumpProbeLog.push_back(std::move(rec));
        m_jumpProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }

    // ① 初始一致性（无论停在顶部还是尾部，显隐必须与贴底态一致）。
    snap("initial-consistency");

    // ② 点击回底：从任何状态点按钮 ⇒ offset 到底 ∧ 按钮隐藏。
    clickJumpButton();
    if (snap("after-click-tail")) {
        JumpProbeRec& r = m_jumpProbeLog.back();
        if (r.offset != r.maxOffset) {
            r.matched = false;
            r.note = "click did not jump to tail";
        }
    }

    // ③ 合成两次向上滚轮（各 40px，共 80 > kPinThreshold=60）⇒ 按钮出现 ∧
    //   离开贴底带。滚轮走 OnEvent——HarnessApplication::OnMouseWheel 先让基类
    //   滚、再刷新按钮，这正是被测路径。
    const ECDI::Point abs = m_scroll->GetAbsolutePosition();
    const int cx = static_cast<int>(abs.x) + m_scroll->GetWidth() / 2;
    const int cy = static_cast<int>(abs.y) + m_scroll->GetHeight() / 2;
    for (int i = 0; i < 2; ++i) {
        ECDI::MouseWheelEvent wheel(m_window, cx, cy, +120);
        m_application->OnEvent(wheel);
    }
    if (snap("after-wheel-up")) {
        JumpProbeRec& r = m_jumpProbeLog.back();
        if (r.offset >= r.maxOffset - kPinThreshold) {
            r.matched = false;
            r.note = "wheel did not move offset away from tail";
        }
    }

    // ④ 再点一次：往返闭环（回底 ∧ 隐藏）。
    clickJumpButton();
    if (snap("after-click-again")) {
        JumpProbeRec& r = m_jumpProbeLog.back();
        if (r.offset != r.maxOffset) {
            r.matched = false;
            r.note = "click did not jump to tail";
        }
    }

    if (m_window && focusBefore) m_window->SetFocusedWidget(focusBefore);
    m_jumpProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4 Phase B（--key-probe，2026-10-25）：全局快捷键判据。
//
// 【它要回答什么】① Ctrl 组合在焦点控件（输入框）手里时**仍然生效**——
//   拦截发生在 Application::OnKeyDown 派发之前；② 模型位与控件位 A==B；
//   ③ Esc 按优先级逐层关；④ 未列入全局表的键照常到达焦点控件（不悬空拦截）。
// ★ 全部走 SynthesizeKey（真实派发链，拦截器在链上被真实穿过）。
void HarnessApp::RunKeyProbe() {
    if (!m_sess || !m_sess->replay) { m_keyProbeDone = true; return; }
    auto snap = [&](const char* phase) -> KeyProbeRec& {
        KeyProbeRec rec;
        rec.phase = phase;
        rec.sidebarVisible = m_sidebarVisible;
        rec.sidebarShown = m_hud.sidebar ? m_hud.sidebar->IsVisible() : false;
        rec.settingsVisible = m_settingsVisible;
        rec.settingsShown = m_hud.settingsPanel ? m_hud.settingsPanel->IsVisible() : false;
        rec.menuOpen = m_moreMenuOpen;
        rec.menuShown = m_hud.moreMenu ? m_hud.moreMenu->IsVisible() : false;
        rec.sessionCount = m_sessions.size();
        m_keyProbeLog.push_back(std::move(rec));
        return m_keyProbeLog.back();
    };

    // 前置：任何按键之前，三对模型位/控件位必须已经一致——否则后面的 A==B 全免谈。
    {
        KeyProbeRec& r = snap("baseline-consistency");
        r.matched = (r.sidebarVisible == r.sidebarShown) &&
                    (r.settingsVisible == r.settingsShown) &&
                    (r.menuOpen == r.menuShown);
        if (!r.matched) r.note = "model/widget visibility diverge before any key";
    }
    const bool sidebarAtStart = m_sidebarVisible;
    const size_t sessionsAtStart = m_sessions.size();
    ECDI::TextBox* const input = m_promptBar ? m_promptBar->input() : nullptr;
    ECDI::Widget* const focusBefore =
        m_window ? m_window->GetFocusedWidget() : nullptr;

    // ① Ctrl+B 两态往返（焦点在输入框 / 在别处）。
    if (m_window && input) m_window->SetFocusedWidget(input);
    SynthesizeKey(ECDI::KeyCode::B, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-b-focused");
        r.matched = (m_sidebarVisible != sidebarAtStart) &&
                    (r.sidebarVisible == r.sidebarShown);
        if (!r.matched) r.note = "sidebar did not toggle (or A!=B) while input focused";
    }
    SynthesizeKey(ECDI::KeyCode::B, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-b-focused-back");
        r.matched = (m_sidebarVisible == sidebarAtStart) &&
                    (r.sidebarVisible == r.sidebarShown);
        if (!r.matched) r.note = "sidebar did not toggle back";
    }
    // 焦点移出输入框（顶栏按钮——Button 可聚焦、且不在转录子树里）。
    if (m_window && m_hud.sidebarToggle) m_window->SetFocusedWidget(m_hud.sidebarToggle);
    SynthesizeKey(ECDI::KeyCode::B, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-b-unfocused");
        r.matched = (m_sidebarVisible != sidebarAtStart) &&
                    (r.sidebarVisible == r.sidebarShown);
        if (!r.matched) r.note = "sidebar did not toggle with focus outside input";
    }
    SynthesizeKey(ECDI::KeyCode::B, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-b-unfocused-back");
        r.matched = (m_sidebarVisible == sidebarAtStart) &&
                    (r.sidebarVisible == r.sidebarShown);
        if (!r.matched) r.note = "sidebar did not toggle back (unfocused)";
    }

    // ② Ctrl+, 开设置 → Esc 关（A==B 双向）。
    SynthesizeKey(ECDI::KeyCode::Comma, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-comma");
        r.matched = m_settingsVisible && (r.settingsVisible == r.settingsShown);
        if (!r.matched) r.note = "Ctrl+Comma did not open settings (or A!=B)";
    }
    SynthesizeKey(ECDI::KeyCode::Escape);
    {
        KeyProbeRec& r = snap("esc-settings");
        r.matched = !m_settingsVisible && (r.settingsVisible == r.settingsShown);
        if (!r.matched) r.note = "Esc did not close settings (or A!=B)";
    }

    // ③ Esc 优先级：设置+菜单同开 ⇒ 第一次 Esc 只关设置（负对照：无分层时
    //   这两相读数区分不出"一次全关"）⇒ 第二次关菜单。
    SynthesizeKey(ECDI::KeyCode::Comma, false, ECDI::KeyModifier::Ctrl);
    SetMoreMenuOpen(true);
    SynthesizeKey(ECDI::KeyCode::Escape);
    {
        KeyProbeRec& r = snap("esc-priority-1");
        r.matched = !m_settingsVisible && m_moreMenuOpen &&
                    (r.menuOpen == r.menuShown);
        if (!r.matched) r.note = "first Esc should close settings but keep menu";
    }
    SynthesizeKey(ECDI::KeyCode::Escape);
    {
        KeyProbeRec& r = snap("esc-priority-2");
        r.matched = !m_moreMenuOpen && !m_settingsVisible &&
                    (r.menuOpen == r.menuShown);
        if (!r.matched) r.note = "second Esc should close menu";
    }

    // ④ Ctrl+N：新建会话（回放模式 = 空会话，不拉真进程）⇒ 会话数 +1。
    SynthesizeKey(ECDI::KeyCode::N, false, ECDI::KeyModifier::Ctrl);
    {
        KeyProbeRec& r = snap("ctrl-n");
        r.matched = (m_sessions.size() == sessionsAtStart + 1);
        if (!r.matched) r.note = "Ctrl+N did not create a session";
    }

    // ⑤ passthrough 负对照：焦点回输入框填字后按 Backspace ⇒ 文本真的少一字符
    //   ——证明未列入全局表的键照常派给焦点控件（拦截器不悬空）。
    if (input) {
        if (m_window) m_window->SetFocusedWidget(input);
        input->SetText("ab");
        input->MoveCaretToEnd();
        SynthesizeKey(ECDI::KeyCode::Backspace);
        KeyProbeRec& r = snap("passthrough");
        r.inputText = input->GetText();
        r.matched = (r.inputText == "a");
        if (!r.matched) r.note = "Backspace did not reach the TextBox";
        input->SetText("");
    }

    // 收尾：还原所有被探针改过的全局态（侧栏/设置/菜单/焦点）。
    SetSettingsVisible(false);
    SetMoreMenuOpen(false);
    SetSidebarVisible(sidebarAtStart);
    if (m_window && focusBefore) m_window->SetFocusedWidget(focusBefore);
    m_keyProbeDone = true;
    if (m_application) m_application->Exit();
}


