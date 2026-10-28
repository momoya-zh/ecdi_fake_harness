// ============================================================
// 流式接收：协议行解包 / 唤醒通路 / tick 派发 / 自动关闭
// ------------------------------------------------------------
// 为什么单独一个文件：这条链是**协议正确性的落点**——一行 jsonl 进来，
//   走哪条分支、什么时候标 live EOF、什么时候判「断流」、探针的派发链插在哪，
//   全在这里。它与"控件长什么样"无关（唯一例外是 CreateBlockRow/CreateAskRow
//   两个建行入口，那是协议事件与视图的唯一接缝）。
// ★ 纪律：`RouteLastMessage` 里**不许**直接摆控件——一律经建行入口。
//   （现状里它调 CreateBlockRow/CreateAskRow/AddNoticeRow，这条一直守着。）
// ★ 2026-10-25：从 App.cpp 切出（Stage 1d）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "Json.h"
#include "ThemeUsing.h"
#include "Transcript.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Core/Logger.h"
// Window.h 只前置声明 PlatformWindow；这里要调它的 StartTimer/SetTitle ⇒ 完整类型。
#include "ECDI/Platform/PlatformWindow.h"
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ScrollView.h"

#include <string>
#include <vector>

// 跨 TU 共享符号（定义见 UiCommon.cpp）
using harness::internal::kProtocolMaxSupported;   // RouteLastMessage 判 hello.protocol 上限
using harness::internal::ReasoningTitle;
using harness::internal::UnknownKindText;

// 冻结活动会话的 timed 锚点：记下暂停时刻（幂等——已暂停则不动）。
void HarnessApp::PauseActiveTimedClock() {
    if (!m_sess || !m_options.timed) return;
    if (!m_sess->timedPaused) {
        m_sess->timedPaused = true;
        m_sess->timedPausedAt = std::chrono::steady_clock::now();
    }
}

// 恢复某会话的 timed 锚点：把锚点整体后移"被冻结了多久"。
//   ★ 为何需要补偿：elapsedMs = now - anchor。会话被切走期间 anchor 不动而 now 前进，
//     切回来若不补偿，elapsedMs 会凭空多出"离开的那段墙上时间" ⇒ 一次性抽干夹具。
void HarnessApp::ResumeSessionTimedClock(Session& s) {
    if (!m_options.timed) return;
    if (s.timedPaused) {
        const auto pausedFor = std::chrono::steady_clock::now() - s.timedPausedAt;
        s.timedAnchor += pausedFor;
        s.timedPaused = false;
    }
}

void HarnessApp::MarkLiveEof() {
    m_sess->liveEof = true;   // core 退出（stdin EOF 或 bye 收尾）
    FailPendingOperations("core EOF / 断连");
    m_hudDirty = true;
    RefreshHud();
}

void HarnessApp::InstallWakeHandler(Session& session) {
    if (!session.child || !m_application) return;
    // ★ 值捕获 `app`：读线程不该去读 `m_application` 这个由 UI 线程写的裸指针
    //   （没有同步、语义上就是 data race）。这次读发生在**本函数**（UI 线程）、
    //   只读一次，之后这个指针由 lambda 自己持有。
    ECDI::Application* const app = m_application;
    session.child->SetWakeHandler([this, app]() -> bool {
        // ★ 本 lambda 跑在**读线程**上：除了 `PostToUi`，这里不许碰任何 UI 状态。
        try {
            return app->PostToUi([this] { DrainActiveLive(true); });
        } catch (...) {
            // `Application.h:83` 明写：入队分配失败会**抛** `bad_alloc`（框架不吞）。
            // 这里吞掉并返回 false ⇒ `ChildSource` 复位在途标志、下一行再试，
            // 期间 tick 兜底取数 ⇒ 最坏是 33ms 延迟，而不是把读线程炸掉。
            return false;
        }
    });
    if (m_options.asyncProbe) session.child->EnableLineStamps(true);
}

void HarnessApp::InstallWakeHandlers() {
    if (m_options.legacyPoll) return;   // 对照世界：不装唤醒，逐字保留 8ms 拉取
    for (auto& session : m_sessions) InstallWakeHandler(*session);
}

void HarnessApp::DrainActiveLive(bool fromWake) {
    if (!m_sess || !m_sess->child) return;

    // ★ 预算 = **行数与时间谁先到**。行数防「2000 块一次抽干」（那会冻住一整帧），
    //   时间防慢机（同样的行数在低端机上可能要几十 ms，帧预算就没了）。
    //   ★ 没取完的部分由 `RearmWake()` **立刻再投一次**——不能指望"下一行到达"
    //     顺带唤醒：数据可能早就全在队列里了，不会有下一行。
    static constexpr size_t kMaxLinesPerCallback = 256;
    static constexpr uint64_t kBudgetUs = 4000;

    const auto drainStart = std::chrono::steady_clock::now();
    size_t handled = 0;
    std::string line;
    while (handled < kMaxLinesPerCallback) {
        if (handled > 0 &&
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - drainStart)
                                      .count()) >= kBudgetUs) {
            break;
        }
        std::chrono::steady_clock::time_point stamp;
        if (!m_sess->child->NextWithinStamped(line, 0, stamp)) break;
        if (m_options.asyncProbe && stamp.time_since_epoch().count() != 0) {
            // 入队 → 现在。0 计数的 stamp = 该行没有戳（探针没开 / 戳之前入队），
            // **跳过而不是补 now()**——补 now() 会伪造一个"零延迟"读数。
            m_probeState.apLatencyUs.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - stamp)
                    .count()));
        }
        ++m_sess->liveLines;
        HandleLine(line);
        ++handled;
    }

    if (m_options.asyncProbe) {
        if (fromWake) {
            m_probeState.apLinesWake += handled;
            ++m_probeState.apCallbacks;
            if (handled > m_probeState.apMaxLinesPerCallback) m_probeState.apMaxLinesPerCallback = handled;
        } else {
            m_probeState.apLinesTick += handled;
        }
    }

    // ★ 复位**所有** live 源的唤醒标志，不只是活动会话的那个：
    //   被切走的会话（冻结中）不 drain，但它的读线程可能已经 arm 过了；
    //   不复位 ⇒ 它切回活动时标志恒 true ⇒ 永不投递 ⇒ 只能靠 33ms 兜底，
    //   而这个后果**没有任何读数会报**（探针只看活动会话）。
    for (auto& session : m_sessions) {
        if (session->child) session->child->RearmWake();
    }

    if (handled > 0) {
        Relayout();
        FollowTailIfPinned();
        CacheEvalMetrics();
        RefreshHud();
        RequestRepaint();
    } else if (m_sess->child->Eof() && !m_sess->liveEof) {
        MarkLiveEof();
    }
}

void HarnessApp::OnReplayTick() {
    // ★★ Step 1：侧栏列表的**唯一重建点**。
    //   为什么必须在 `if (!m_sess->source) return;` **之前**：回放模式新建的空会话
    //   `source == nullptr`，本函数会在下一行直接 return —— 把重建放在后面就意味着
    //   "新建会话后侧栏永远不刷新"（列表说 1 个、模型已经 2 个）。
    //   为什么放在 tick 里而不是回调里：见 `m_sidebarDirty` 的声明注释（派发栈 UAF）。
    //   ★ timer 回调不在事件派发栈上 ⇒ 此刻 `RemoveChild` 拆树是安全的。
    if (m_sidebarDirty) {
        m_sidebarDirty = false;
        RebuildSessionList();
        // ★★ 2026-10-20：「项目」段与「会话列表」同一条链：活动会话变了
        //   ⇒ 工作区可能变了 ⇒ 目录内容要跟着刷。
        RebuildProjectList();
        RequestRepaint();
    }
    // ★★ Step 4：整树重建的**唯一执行点**（同族的派发栈 UAF 理由见
    //   `m_uiRebuildPending` 的声明注释）。放在 `if (!m_sess->source) return;`
    //   **之前**：空会话（回放模式新建的）也要能换主题。
    //   ★ 顺序上先于侧栏重建之后无所谓——两者不会同时置脏（同一 tick 内至多一个来源），
    //     但若真同时发生，整树重建会把旧列表一起丢掉，故它必须在后。
    if (m_uiRebuildPending) {
        m_uiRebuildPending = false;
        RebuildUi();
        RequestRepaint();
    }
    // ★★ Step 5：历史区在 tick 里重建（行数是扫盘来的，回调里不能扫也不能拆树）。
    if (m_historyDirty) {
        m_historyDirty = false;
        RebuildHistoryList();
        RequestRepaint();
    }
    // ★ P3.6：检查器模型列表重建（同族派发栈纪律；守卫之前 ⇒ 空会话也能刷新）。
    if (m_modelListDirty) {
        m_modelListDirty = false;
        RebuildModelList();
        RebuildSettingsProviderList();
    }
    // ★★ Step 5：打开历史会话（延迟到此：派发栈已退，拆树/抽干都安全）。
    if (!m_historyOpenPending.empty()) {
        const std::filesystem::path file = m_historyOpenPending;
        m_historyOpenPending.clear();
        OpenHistorySession(file);   // 内部会置 m_sidebarDirty + RequestRepaint
    }
    if (!m_sess->source) {
        if (m_options.workspaceProbe && !m_probeState.workspaceProbeDone) RunWorkspaceProbe();
        return;   // 源启动失败（Run 已报错退出路径）/ 0d 空会话：无可拉取
    }
    if (m_options.workspaceProbe && !m_sess->source->IsLive()) {
        RunWorkspaceProbe();  // Probe requires a live core; report unavailable instead of hanging on replay.
        return;
    }
    if (m_sess->source->IsLive()) {
        // ── ★★ REQ-03（2026-10-24）：两条通路，只有一条是本次工作的对象 ──
        //   · `--legacy-poll` = **拉**：逐字保留改动前的形状（每 tick 首行等 8ms）。
        //     它是对照世界，也是回滚开关。没有它，「推更快」这句话就没有对照物。
        //   · 默认 = **推**：读线程已经用 `Application::PostToUi` 投递了唤醒，
        //     数据由 `DrainActiveLive(true)` 在消息循环里取；tick 这边只做
        //     **非阻塞**兜底——投递可能失败（关闭中 / 唤醒失败 / `bad_alloc`），
        //     兜底保证最坏情况退化成「33ms 内取到」，而不是永远不取。
        if (m_options.asyncProbe) ++m_probeState.apTicks;
        if (m_options.legacyPoll) {
            bool gotAny = false;
            const int budgetMs = 8;
            std::string line;
            for (;;) {
                const auto waitStart = std::chrono::steady_clock::now();
                std::chrono::steady_clock::time_point stamp;
                const bool got = m_sess->child->NextWithinStamped(
                    line, gotAny ? 0 : budgetMs, stamp);
                if (got && m_options.asyncProbe && stamp.time_since_epoch().count() != 0) {
                    // ★ 对照世界也用**同一套仪器**：只改唤醒机制这一个自变量，
                    //   不能连"怎么量"都换了——那会让两组读数不可比。
                    m_probeState.apLatencyUs.push_back(static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - stamp)
                            .count()));
                }
                // ★ 只把**带超时**的那次等待计入「阻塞」：`gotAny` 之后的 0ms 调用
                //   根本不等，把它算进来会把代价记成 0（自欺）。
                if (!gotAny && m_options.asyncProbe) {
                    const uint64_t us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - waitStart)
                            .count());
                    ++m_probeState.apBlockedTicks;
                    m_probeState.apBlockedUs += us;
                    if (us > m_probeState.apMaxBlockedUs) m_probeState.apMaxBlockedUs = us;
                }
                if (!got) break;
                ++m_sess->liveLines;
                if (m_options.asyncProbe) ++m_probeState.apLinesTick;
                HandleLine(line);
                gotAny = true;
            }
            if (gotAny) {
                Relayout();
                FollowTailIfPinned();
                CacheEvalMetrics();
                RefreshHud();
                RequestRepaint();
            } else if (m_sess->child->Eof() && !m_sess->liveEof) {
                MarkLiveEof();   // core 退出（stdin EOF 或 bye 收尾）
            }
        } else {
            DrainActiveLive(false);   // 非阻塞兜底（含 EOF 检测）
        }
        // ★★ 2026-10-27：live 分支也要吃 `m_hudDirty`。
        //   以前只有下面「replay 已 settle」那条分支（`if (m_hudDirty) RefreshHud()`）
        //   消费这个脏位 ⇒ live 空闲（core 不再吐行）时，**纯 UI 驱动**的模型变化没有
        //   任何人刷 HUD：点会话行的「×」删掉一个会话后，侧栏「N 个会话」和头栏
        //   「会话 i/n」一直停在删除前的值（实测 1s 与 7s 两张截图逐字相同，
        //   而 `--out` 摘要里的 `sessionCount` 已经是删后的值），要等 core 下一行才纠正。
        if (m_hudDirty) {
            CacheEvalMetrics();
            RefreshHud();
        }
        if (m_options.workspaceProbe && !m_probeState.workspaceProbeDone) RunWorkspaceProbe();
        // ★★ P3.5 设置探针跨 tick 的另一半：save 之后活动会话已是 live ⇒
        //   tick 走**本分支**（replay settle 分支走不到）——stage 0 在 save 那一 tick
        //   已产出 first-run + save 两相，stage 1+ 只能在这里推进。
        //   ★ 教训同 reasoning-probe 的"第一版只放末尾"：探针的跨 tick 推进点
        //     必须铺满**所有**能让它所在的会话形态，缺一个就是永远停在那一拍。
        if (m_options.settingsProbe && !m_probeState.settingsProbeDone) RunSettingsProbe();
        return;
    }

    if (m_sess->replay->Empty() && !m_sess->waitForAnswer) {
        if (m_sess->replaySettled) {
            if (m_relayoutPulse > 0) {   // 折叠/展开动画、settle 后新增行：几何持续跟随
                --m_relayoutPulse;
                Relayout();
                FollowTailIfPinned();
                RequestRepaint();
            }
            if (m_hudDirty) {   // 已拉完：只在有新读数时刷一次 HUD，不再制造重绘
                CacheEvalMetrics();
                RefreshHud();
            }
            // ★★ 2026-10-25（Stage 4）：探针派发表。**顺序即契约**，见下。
            //
            //   为什么改成表：这 19 条形状完全一致
            //   （`if (m_options.x && !m_probeState.xDone) RunX();`），以前是手抄的
            //   19 份样板；加一个探针要改三处（开关 / done 标志 / 调用），漏一处就是
            //   「探针一步没跑」而摘要看起来只是少了几个键。现在只有一行。
            //
            //   ★ 顺序为什么不能乱（都是踩过的）：
            //     · 同步组里多条探针收尾会**同步 `Exit()`**，排在前面的先跑；
            //       反过来说，任何一个同步探针跑到就 Exit，排在它后面的同 tick 就没机会了
            //       ⇒ 这也是为什么这些探针「互斥使用」（同一进程只开一个 `--*-probe`）。
            //     · 跨 tick 组（settings / theme / history）必须排在同步组**之后**：
            //       它们靠后续 tick 推进，若前面有同步探针 Exit，它们永远等不到下一拍。
            //     · 第一版把 reasoning 只放在函数末尾 ⇒ 夹具拉完后本函数已 `return`，
            //       状态机永远停在 step 0（只有 settle 那一 tick 采到 default）。
            struct ProbeStep {
                bool CliOptions::*enabled;   // 哪个 --xxx-probe 开关
                bool ProbeState::*done;      // 对应的「已跑过」标志
                void (HarnessApp::*run)();   // 跑它
            };
            static constexpr ProbeStep kSettleSteps[] = {
                // ── 同步组：跑完即 Exit ──
                {&CliOptions::reasoningProbe, &ProbeState::reasoningProbeDone, &HarnessApp::RunReasoningProbe},
                {&CliOptions::rebuildProbe, &ProbeState::rebuildProbeDone, &HarnessApp::RunRebuildProbe},
                {&CliOptions::sessionProbe, &ProbeState::sessionProbeDone, &HarnessApp::RunSessionProbe},
                {&CliOptions::sidebarProbe, &ProbeState::sidebarProbeDone, &HarnessApp::RunSidebarProbe},
                {&CliOptions::answerProbe, &ProbeState::answerProbeDone, &HarnessApp::RunAnswerProbe},
                {&CliOptions::menuProbe, &ProbeState::menuProbeDone, &HarnessApp::RunMenuProbe},
                {&CliOptions::resizeProbe, &ProbeState::resizeProbeDone, &HarnessApp::RunResizeProbe},
                {&CliOptions::enterProbe, &ProbeState::enterProbeDone, &HarnessApp::RunEnterProbe},
                {&CliOptions::growProbe, &ProbeState::growProbeDone, &HarnessApp::RunGrowProbe},
                {&CliOptions::valignProbe, &ProbeState::valignProbeDone, &HarnessApp::RunValignProbe},
                {&CliOptions::hudProbe, &ProbeState::hudProbeDone, &HarnessApp::RunHudProbe},
                {&CliOptions::jumpProbe, &ProbeState::jumpProbeDone, &HarnessApp::RunJumpProbe},
                {&CliOptions::keyProbe, &ProbeState::keyProbeDone, &HarnessApp::RunKeyProbe},
                {&CliOptions::emptyProbe, &ProbeState::emptyProbeDone, &HarnessApp::RunEmptyProbe},
                {&CliOptions::modelProbe, &ProbeState::modelProbeDone, &HarnessApp::RunModelProbe},
                {&CliOptions::selectProbe, &ProbeState::selectProbeDone, &HarnessApp::RunSelectProbe},
                {&CliOptions::delProvProbe, &ProbeState::delProvProbeDone, &HarnessApp::RunDelProvProbe},
                {&CliOptions::previewProbe, &ProbeState::previewProbeDone, &HarnessApp::RunPreviewProbe},
                // ── 跨 tick 组：靠后续 tick 推进，必须排在同步组之后 ──
                {&CliOptions::settingsProbe, &ProbeState::settingsProbeDone, &HarnessApp::RunSettingsProbe},
                {&CliOptions::themeProbe, &ProbeState::themeProbeDone, &HarnessApp::RunThemeProbe},
                {&CliOptions::historyProbe, &ProbeState::historyProbeDone, &HarnessApp::RunHistoryProbe},
            };
            for (const ProbeStep& step : kSettleSteps) {
                if (m_options.*step.enabled && !(m_probeState.*step.done)) (this->*step.run)();
            }
            return;
        }
        m_sess->replaySettled = true;
    }
    if (m_options.timed) {
        DrainTimed();
    } else {
        DrainInstant();
    }
    Relayout();
    FollowTailIfPinned();
    if (m_sess->replaySettled && m_options.scrollSweepStep > 0 && !m_sweepStarted && m_scroll) {
        // --scroll-sweep：等夹具拉完且尾随完成后，从顶部开始逐 tick 扫描到底
        m_sweepStarted = true;
        m_scroll->SetContentOffset(0, 0);
        if (m_window) {
            m_window->GetPlatformWindow().StartTimer(kSweepTimer, 33);
        }
    }
    CacheEvalMetrics();
    RefreshHud();
    // ★★ --hit-probe（REQ-04 边界）：夹具拉完 + `Relayout()` 之后**跑一次**。
    //   位置理由：此刻几何已定（按钮位置/尺寸是探针坐标的来源），
    //   且只跑一次（否则 30Hz 每 tick 都合成点击，读数被自身重复污染）。
    if (m_options.hitProbe && m_sess->replaySettled && !m_probeState.hitProbeDone) {
        m_probeState.hitProbeDone = true;
        RunHitProbe();
    }
    // ★ 思维链折叠行为取证（协议 v2 §7）：跨 tick 状态机（每次只推进一步，
    //   步间等过 200ms 折叠动画）⇒ 不能像 --hit-probe 那样一次跑完。
    if (m_options.reasoningProbe && m_sess->replaySettled && !m_probeState.reasoningProbeDone) {
        RunReasoningProbe();
    }
    // 注：`--rebuild-probe` 的推进在**上面 settle 分支里**（本函数到尾就 `return` 了，
    //   这里再写一遍是死代码；第一版正是写在这里，所以探针一步都没跑）。
    RequestRepaint();
}

void HarnessApp::OnAutoClose() {
    if (m_application) m_application->Exit();
}

// --scroll-sweep 步进：每次前进 step px；扫到底即退出（摘要由 Run() 返回后落盘）
void HarnessApp::OnSweepTick() {
    if (!m_scroll || m_sweepDone) return;
    const int maxY = m_scroll->GetMaxOffsetY();
    const int y = m_scroll->GetScrollOffsetY() + m_options.scrollSweepStep;
    if (y >= maxY) {
        m_scroll->SetContentOffset(0, maxY);
        m_sweepDone = true;
    } else {
        m_scroll->SetContentOffset(0, y);
    }
    CacheEvalMetrics();
    RefreshHud();
    RequestRepaint();
    if (m_sweepDone && m_application) {
        m_application->Exit();
    }
}

void HarnessApp::DrainInstant() {
    if (m_sess->instantDone) return;
    m_sess->instantDone = true;
    std::string line;
    // ★ 协议 §9：版本不兼容 ⇒ **停止消费**（"拒绝连接"的实质）。
    //   只在 `HandleLine` 返回后检查：`Exit()` 只让消息循环退出，**不打断本循环**，
    //   而 `--instant` 会在同一 tick 抽干整个夹具 ⇒ 若不在此处 break，
    //   "拒绝"就只是打印一行提示、随后照常把整条流处理完（实测：protocol:3 的
    //   夹具仍读到 blocks=44）——那不是拒绝，是"边抱怨边照做"。
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);   // 协议 §1.2：截断必须显式报错
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::DrainTimed() {
    if (m_sess->waitForAnswer) return;   // I3：ask 未答前 core 不再发消息
    if (m_sess->protocolIncompatible) return;   // ★ 协议 §9：版本不兼容 ⇒ 不再消费
    // ★ 0d：会话被切走（冻结）期间不拉取——它不在屏上，"到期"没有意义。
    if (m_sess->timedPaused) return;
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  m_sess->timedAnchor)
            .count();
    std::string line;
    while (m_sess->replay->NextDue(elapsedMs, line)) {
        HandleLine(line);
        if (m_sess->waitForAnswer) break;   // ask 已显示，停在 waiting（协议 §8.3）
        if (m_sess->protocolIncompatible) break;   // ★ 协议 §9：版本不兼容 ⇒ 停止消费
    }
    if (m_sess->replay->Empty() && !m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }
}

void HarnessApp::HandleLine(const std::string& line) {
    std::string notice;
    const bool ok = m_sess->transcript.OnLine(line, notice);
    if (!notice.empty()) AddNoticeRow(notice, kErrorDark);   // I4 / I5 等显式提示
    if (ok) RouteLastMessage();
}

void HarnessApp::RouteLastMessage() {
    const json::Value& msg = m_sess->transcript.LastMessage();
    const std::string t = msg.GetString("t");

    if (t == "block_start") {
        if (const Block* block = m_sess->transcript.FindBlock(msg.GetString("id"))) {
            if (block->kind == "user" && !block->clientId.empty()) {
                const std::string clientId = block->clientId;
                for (auto it = m_pendingPromptViews.begin(); it != m_pendingPromptViews.end(); ++it) {
                    if (it->clientId == clientId) {
                        if (m_content && it->row.panel) m_content->RemoveChild(it->row.panel);
                        m_pendingPromptViews.erase(it);
                        break;
                    }
                }
                auto& pending = m_sess->pendingPrompts;
                pending.erase(std::remove_if(pending.begin(), pending.end(),
                    [&](const PendingPrompt& p) { return p.clientId == clientId; }), pending.end());
            }
            CreateBlockRow(*block);
        }
        return;
    }
    if (t == "delta") {
        const std::string id = msg.GetString("id");
        const auto row = m_sess->blockRows.find(id);
        const Block* block = m_sess->transcript.FindBlock(id);
        if (row != m_sess->blockRows.end() && block && row->second->label) {
            // ★ 未知 kind 的行有**前缀**（`[未知 kind: xxx]`）——不能整段覆盖，
            //   否则流式 delta 一到就把前缀擦掉（2026-10-13 实测：截图里 r2 的
            //   `[未知 kind: thinking]` 前缀正是这样丢的）。此处保持与
            //   `CreateBlockRow` 同一拼法。
            if (block->unknownKind) {
                row->second->label->SetText(UnknownKindText(*block));
            } else {
                row->second->label->SetText(block->text);   // 合并：只追加到文本缓冲，不逐字符刷新
            }
        }
        return;
    }
    if (t == "block_end") {
        const std::string id = msg.GetString("id");
        const Block* block = m_sess->transcript.FindBlock(id);
        const auto row = m_sess->blockRows.find(id);
        if (row == m_sess->blockRows.end() || !row->second->headerBtn) {
            return;
        }
        // ★ 协议 v2 §7：思维链块结束时标题改为「**已完成**」（用户裁决）。
        //   ★ **箭头不变**——箭头只由展开态决定，与是否结束无关。
        //     （我一度连箭头一起换成 `▶`，被截图证明违背用户明确给出的规格，已撤回。）
        //   ★ token 副标也在此刻出现（`tokens` 是 `block_end` 才有的字段）。
        //   ★ 展开态也要更新文案：用户看到的是「已完成」+ `︿`。
        //     （2026-10-18 订正：此处原文写 `▲`，但思维链标题的箭头单一真相源是
        //      `ReasoningTitle()`，用的正是用户指定的钝角 `﹀`/`︿`，不是 `▲`。）
        if (row->second->reasoning && block) {
            row->second->ended = true;
            if (row->second->headerBtn) {
                row->second->headerBtn->SetText(
                    ReasoningTitle(*block, row->second->expanded));
            }
            RequestRepaint();
            return;
        }
        // ★ REQ-02（2026-10-13）：`tool_call` 的 `summary` 此前被吞——原条件只认
        //   `tool_result`，而假 core 在 **`tool_call` 的 `block_end`** 里就发了
        //   `summary:"已发起"`（协议 §3.5 允许），于是那条摘要无处落地。
        //   修正为两类都更新标题：`tool_call` 用自身 name，`tool_result` 用 forId 引用的工具名。
        const bool isResult = (block && block->kind == "tool_result");
        const bool isCall = (block && block->kind == "tool_call");
        if (isResult) {
            // hi-fi：结果行标题 = 工具名（来自 forId 引用的 call 块）+ ok/失败摘要
            // ★ 2026-10-18：`▸`(U+25B8) 在两个主题的 face 里**没有字形**
            //   （`.scratch/symcand.py` 两轮实测：GDI 靠字体链接偷画、GL 直接空白）
            //   ⇒ 换成 `›`(U+203A)，同为右向尖括号，两主题都真有。
            std::string title = "› ";
            if (const Block* call = m_sess->transcript.FindBlock(block->forId)) {
                title += call->name.empty() ? "tool" : call->name;
            } else {
                title += "tool";
            }
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
        } else if (isCall) {
            // 工具调用：标题 = 工具名（+ 可选摘要）——此前 summary 在此被丢弃
            // ★ 2026-10-18：`▸` → `›`（字形可用性实测，见上方 isResult 分支注释）
            std::string title = "› " + (block->name.empty() ? "tool_call" : block->name);
            if (!block->summary.empty()) {
                title += " · ";
                title += block->summary;
            } else if (!block->ok) {
                title += " · 失败";
            }
            row->second->headerBtn->SetText(title);
            if (!block->ok) row->second->headerBtn->SetTextColor(kErrorDark);
        }
        return;
    }
    if (t == "ask") {
        CreateAskRow(msg);
        return;
    }
    if (t == "error") {
        const std::string clientId = msg.GetString("client_id");
        const std::string message = msg.GetString("message", "core error");
        const json::Value* fatalField = msg.Find("fatal");
        // ★ 协议 §3.8：`fatal` 缺省 = **false**。缺字段不是「致命」——§9 的宽容原则
        //   （「宽容比拒连更安全」）同样适用于这里：一个漏写字段的 core 不该被判死，
        //   真致命的 core 会自己退出，EOF 仍会兜住。
        const bool fatal = fatalField && fatalField->IsBool() && fatalField->boolean;
        const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
            m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                return p.clientId == clientId && p.status == "sending";
            });
        const bool correlated = !clientId.empty() && pending != m_sess->pendingPrompts.end();
        if (correlated) FailPrompt(clientId, message);
        if (fatal) {
            FailPendingOperations(message);
            AddNoticeRow("[fatal error] " + message, kErrorDark);
        } else if (!correlated) {
            AddNoticeRow("[error] " + message, kErrorDark);
        }
        return;
    }
    if (t == "hello") {
        m_sess->sessionId = msg.GetString("session");
        m_sess->coreVer = msg.GetString("core");
        const json::Value* protocol = msg.Find("protocol");
        m_sess->protocolVer = protocol && protocol->IsNumber()
            ? static_cast<int>(protocol->number) : 0;
        const json::Value* caps = msg.Find("caps");
        bool capsValid = caps && caps->IsArray();
        bool hasWorkspace = false;
        std::string joined;
        if (capsValid) {
            for (const auto& cap : caps->array) {
                if (!cap.IsString()) {
                    capsValid = false;
                    break;
                }
                if (!joined.empty()) joined += ", ";
                joined += cap.text;
                if (cap.text == "workspace") hasWorkspace = true;
            }
        }
        bool hasModelCap = false;
        if (capsValid) {
            for (const auto& cap : caps->array) {
                if (cap.text == "model") hasModelCap = true;
            }
        }
        m_sess->capsStr = capsValid ? joined : std::string();
        m_sess->workspaceCap = capsValid && hasWorkspace;
        m_sess->hasModelCap = capsValid && hasModelCap;
        // spawn 时 GUI 已知道初始提供商（--provider-id 与配置一致）
        if (m_sess->providerId.empty()) {
            if (const auto* def = m_appConfig.DefaultProvider()) {
                m_sess->providerId = def->id;
            }
        }
        if (m_sess->protocolVer > kProtocolMaxSupported) {
            // ★★ 协议 §9：只有 core 声明的主版本**高于**本 GUI 上限才拒连（不做降级猜测）。
            //   缺字段按 v1 宽容；**低于**上限从来不是拒绝理由（§9 无此条）。
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "[协议不兼容] core 声明 protocol %d，本 GUI 支持到 %d"
                          " —— 按 §9 拒绝连接（不做降级猜测）",
                          m_sess->protocolVer, kProtocolMaxSupported);
            AddNoticeRow(buf, kErrorDark);
            m_sess->protocolIncompatible = true;
            m_sess->disconnected = true;
            m_hudDirty = true;
            if (m_application) m_application->Exit();   // 不进入渲染路径
            return;
        }
        // ★★ 协议 v3 §3.1：caps 缺 `workspace` **不是**拒连条件，而是「GUI 不得显示
        //   工作区选择器」⇒ 隐藏整段工作区交互，但**连接与发送照常**。
        //   ★ 2026-10-20 修正：旧实现把 `!workspaceCap` 也写进上面的拒连条件，
        //     而两份 hostile 夹具的 hello 恰好是 `protocol:3` + caps 无 workspace
        //     ⇒ 它们一行都没被解析（blocks=0/violations=0），I1/I4/I5 的负对照
        //     全部空转。规范从没有「必须声明 workspace 才能连接」这一条。
        if (!m_sess->workspaceCap) {
            m_sess->workspaceStatus = "core 未声明 workspace 能力";
        }
        if (const json::Value* ctx = msg.Find("context");
            ctx && ctx->IsNumber() && ctx->number > 0) {
            m_sess->contextLimit = static_cast<long long>(ctx->number);
            // ★★ P4 H 修复（P3.6 bug #2）：ctxCapacity 的 v4 逐轮分母在此**填充后**
            //   迁移——原实现把迁移放在填充之前（迁进去的恒 0），hello.context
            //   与 usage.context 的覆盖形同虚设（百分比/环一直拿不到分母）。
            if (m_sess->ctxCapacity == 0) {
                m_sess->ctxCapacity = static_cast<int>(ctx->number);
            }
        }
        m_hudDirty = true;
        if (m_devMode) {
            AddNoticeRow("core " + msg.GetString("core") + " · session " +
                             msg.GetString("session") + " · protocol " +
                             std::to_string(m_sess->protocolVer), kDarkMuted);
        }
        // The v3 core starts in idle and does not emit a redundant initial state line.
        if (m_sess->coreState.empty()) m_sess->coreState = "idle";
        MaybeStartWorkspaceSync();
        return;
    }
    if (t == "state") {
        m_sess->coreState = msg.GetString("value");
        m_hudDirty = true;
        if (m_sess->coreState == "idle") {
            MaybeStartWorkspaceSync();
            SendPendingModelSwitchIfIdle();   // ★ P3.6：排队的模型切换在 idle 兑现
        }
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "workspace") {
        const std::string requestId = msg.GetString("request_id");
        const json::Value* requestField = msg.Find("request_id");
        if (!requestField || !requestField->IsString() || requestId.empty()) {
            if (!m_sess->pendingWorkspaceId.empty()) {
                const bool initialSync = m_sess->pendingWorkspaceInitialSync;
                m_sess->pendingWorkspaceId.clear();
                m_sess->pendingWorkspaceInitialSync = false;
                m_sess->workspaceStatus = "工作区确认无效：缺少 request_id";
                if (initialSync && !m_sess->workspaceSyncComplete) {
                    m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
                }
                RefreshHud();
            }
            AddNoticeRow("[workspace ack 缺少 request_id]", kErrorDark);
            return;
        }
        if (m_sess->pendingWorkspaceId.empty() || requestId != m_sess->pendingWorkspaceId) {
            AddNoticeRow("[workspace ack 不匹配] " + requestId, kErrorDark);
            return;
        }
        const bool initialSync = m_sess->pendingWorkspaceInitialSync;
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        const json::Value* ok = msg.Find("ok");
        if (!ok || !ok->IsBool()) {
            m_sess->workspaceStatus = "工作区确认无效：ok 必须是 boolean";
        } else if (ok->boolean) {
            const json::Value* path = msg.Find("path");
            if (!path || !path->IsString() || path->text.empty()) {
                m_sess->workspaceStatus = "工作区确认无效：缺少实际路径";
            } else {
                m_sess->workspacePath = path->text;
                m_sess->workspaceSyncComplete = true;
                m_sess->workspaceStatus = "已由 core 确认";
                // ★★ 2026-10-20：确认成功 = 「这个会话现在属于这条路径那个工作区」。
                //   ★ 注册只发生在这里（不在请求时）：工作区的路径定义是 **core 回显的
                //     规范化结果**，不是 GUI 自己发出去的那个字符串。在请求时注册就会
                //     把一个未经确认的路径写进注册表，而“注册表里的路径”正是侧栏
                //     告诉用户「这个会话在哪」的唯一依据——那等于自己给自己发证据。
                //   ★ `AcquireWorkspace` 按路径去重：两个会话被确认到同一目录时共享
                //     同一个工作区实体（侧栏不会出现两个同路径的标题行）。
                //   ★ 归属变了 ⇒ 侧栏的**分组结构**也变了，必须重画（不是只刷 HUD 的
                //     两行文字）。`m_sidebarDirty` 只在 tick 顶部被消费（OnReplayTick），
                //     而本分支正好就在 tick 里（HandleLine ← OnReplayTick），不在派发栈上，
                //     置脏是安全的。
                //     漏了这一行的实测症状：新会话的 ack 到达后侧栏**从不重建**，
                //     于是一个已经确认到路径的会话永远挂在“未归属工作区”下面，
                //     而顶部的工作区块却正确地显示着同一个路径——自相矛盾的界面。
                AssignSessionWorkspace(*m_sess, AcquireWorkspace(path->text));
                m_sidebarDirty = true;
            }
        } else {
            m_sess->workspaceStatus = "切换失败：" + msg.GetString("message", "core 拒绝请求");
        }
        if (initialSync && !m_sess->workspaceSyncComplete) {
            // Initial sync failure leaves the GUI gated until an explicit folder request succeeds.
            m_sess->workspaceStatus = "初始工作区未确认：" + m_sess->workspaceStatus;
        }
        m_hudDirty = true;
        RefreshHud();
        MaybeSendAutoPrompt();
        return;
    }
    if (t == "usage") {
        m_sess->usageTurn = static_cast<int>(msg.GetNumber("turn"));
        m_sess->usageIn = static_cast<long long>(msg.GetNumber("input"));
        m_sess->usageOut = static_cast<long long>(msg.GetNumber("output"));
        // ★ v4 §3.7：可选 context = 当下分母（按轮覆盖 hello.context）。
        if (const json::Value* c = msg.Find("context");
            c && c->IsNumber() && c->number > 0) {
            m_sess->ctxCapacity = static_cast<int>(c->number);
            m_sess->contextLimit = static_cast<long long>(c->number);
        }
        m_hudDirty = true;
        return;
    }
    if (t == "model") {
        // ★ v4 §3.11：model 确认（I10 同族：id 匹配 + ok 布尔 + ok:false 带 message）
        const std::string ackId = msg.GetString("id");
        if (m_sess->pendingModelId.empty() || ackId != m_sess->pendingModelId) {
            AddNoticeRow("[model ack 不匹配] " + ackId, kErrorDark);
            return;
        }
        m_sess->pendingModelId.clear();
        const json::Value* ok = msg.Find("ok");
        if (!ok || !ok->IsBool()) {
            AddNoticeRow("[model ack 无效：ok 必须是 boolean]", kErrorDark);
            return;
        }
        if (!ok->boolean) {
            AddNoticeRow("[模型切换失败] " + msg.GetString("message", "（无原因）"),
                         kErrorDark);
        } else {
            m_sess->providerId = msg.GetString("provider", m_sess->providerId);
            m_sess->modelName = msg.GetString("model", m_sess->modelName);
            // ★★ 2026-10-27（v4 第三步）：ack 的 `context` **0 也要认**。
            //   v4 起容量跟着模型走，新模型没标容量时 core 会回 context:0（= 未知）
            //   ⇒ 这里必须把它落到 0，否则 GUI 保留上一个模型的分母，显示的百分比
            //   是拿 A 的容量除 B 的用量算出来的（最难发现的一类假读数）。
            //   ★ 只有**字段缺席**才不覆盖（老 core 不发这个字段）。
            if (const json::Value* c = msg.Find("context"); c && c->IsNumber()) {
                m_sess->ctxCapacity = static_cast<int>(c->number);
                m_sess->contextLimit = static_cast<long long>(c->number);
            }
            AddNoticeRow("[已切换] " + m_sess->providerId + " / " +
                             msg.GetString("model", "?"), kDarkMuted);
            m_modelListDirty = true;
            UpdateModelChip();   // ★ P4 H：chip 常驻数据跟着切换走
        }
        m_hudDirty = true;
        return;
    }
    if (t == "bye") {
        FailPendingOperations("core 已发送 bye");
        AddNoticeRow("会话结束 (" + msg.GetString("reason") + ", code " +
                         std::to_string(static_cast<int>(msg.GetNumber("code"))) + ")",
                     kDarkMuted);
        return;
    }
    // state / usage：上移到 HUD，转录不渲染
}
