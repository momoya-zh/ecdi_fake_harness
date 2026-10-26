// ============================================================
// 评估摘要落盘（退出时写 `--out <json>`）
// ------------------------------------------------------------
// 为什么单独一个文件：这是 App.cpp 里**最长的单个函数**（1082 行），
//   内容是 20 段「某探针开着 ⇒ 往 json 里追加它那几行读数」首尾相接。
//   它和产品逻辑没有交集（只读状态、只写文件），却是每次改 tick/UI 时
//   绕不过去的 1000 行——所以第一刀切它。
//
// ★ 纪律（Stage 4 会动这里，先写清楚）：
//   摘要的**字段顺序**是逐字节基线的一部分（evals/ 84 份 + tools/regress.py
//   都按它判读）。任何「把 if 块抽成函数」的重排都会改变输出字节序 ⇒
//   Stage 4 拆分时必须保持拼接顺序不变，改完用门 4 逐 key 比对。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1b）。函数体与切出前逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "Json.h"
#include "ThemeUsing.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Logger.h"

// 跨 TU 共享符号（定义见 AppInternal.h）
using harness::internal::kProtocolMaxSupported;

void HarnessApp::WriteSummary() {
    if (m_summaryWritten) return;
    m_summaryWritten = true;

    // 退出时刻全量刷新缓存：settle 后 CacheEvalMetrics 不再随 tick 执行，
    // 若不在此补刷，摘要里的 paints/probe 读数会冻结在 settle 时刻（undercount
    // 光标闪烁 / HUD 刷新产生的后续帧）。此时窗口与探针仍存活（application 栈对象未析构）。
    CacheEvalMetrics();

    char buf[512];
    std::string json = "{\n";
    json += "  \"fixture\": \"" + m_options.fixtureName + "\",\n";
    json += std::string("  \"mode\": \"") + (m_options.timed ? "timed" : "instant") + "\",\n";
    json += std::string("  \"rowMode\": \"") + (m_options.rowModeList ? "listlayout" : "autosize") +
            "\",\n";
    json += "  \"ecdiCommit\": \"" + m_options.ecdiCommit + "\",\n";
    json += "  \"backend\": \"" + m_backendName + "\",\n";
    // ★★ 0d：多会话时摘要只记**退出时刻的活动会话**——这件事必须显式写出来。
    //   实测教训（2026-10-19）：窗口还开着的时候点了「＋ 新建会话」，活动会话切到
    //   空会话，落盘的就是 blocks=0/deltas=0/messages=0 —— 看起来与"跑失败了"
    //   **完全一样**（与 protocolIncompatible 那条注释同族：空摘要不得伪装成功摘要）。
    //   ★ 只在 sessionCount > 1 时写这三个字段：单会话运行的摘要**逐字节不变**，
    //     所有既有基线（p32 起）仍可比——这是本仓的铁律，不能为解释一个交互场景破例。
    if (m_sessions.size() > 1) {
        std::snprintf(buf, sizeof(buf), "  \"sessionCount\": %zu,\n", m_sessions.size());
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"activeSessionIndex\": %zu,\n", m_activeIndex);
        json += buf;
        json += "  \"activeSessionLabel\": \"" + m_sess->label + "\",\n";
    }
    std::snprintf(buf, sizeof(buf), "  \"blocks\": %zu,\n", m_sess->transcript.Blocks());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"deltas\": %zu,\n", m_sess->transcript.Deltas());
    json += buf;
    // ★ 注意：这是**夹具总行数**（文件里有多少行），不是"本次消费了多少行"。
    //   被 §9 拒绝时它仍是全量（文件没变），故判读「是否真的拒绝」要看
    //   `blocks`（消费后才产生）与 `protocolIncompatible`，不能看本字段。
    std::snprintf(buf, sizeof(buf), "  \"messages\": %zu,\n",
                  m_sess->replay ? m_sess->replay->Total() : m_sess->liveLines);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"invalidations\": %zu,\n", m_invalidations);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"paints\": %zu,\n", m_evalPaints);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"frameCount\": %zu,\n", m_evalFrames);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"maxPaintUs\": %llu,\n",
                  static_cast<unsigned long long>(m_evalMaxUs));
    json += buf;
    const uint64_t avg = m_evalFrames > 0 ? m_evalSumUs / m_evalFrames : 0;
    std::snprintf(buf, sizeof(buf), "  \"avgPaintUs\": %llu,\n",
                  static_cast<unsigned long long>(avg));
    json += buf;
    if (m_options.cullProbe && m_probe) {
        // Phase 27 探针读数：实际到达后端的绘制命令（视口剔除后）+ 执行端帧时
        std::snprintf(buf, sizeof(buf), "  \"probeFrames\": %zu,\n", m_probeState.evalProbeFrames);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"paintCommands\": %zu,\n", m_probeState.evalProbeCommands);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"maxFrameCommands\": %zu,\n", m_probeState.evalProbeMax);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"avgFrameCommands\": %zu,\n", m_probeState.evalProbeAvg);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"maxExecuteUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMaxExecUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"avgExecuteUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalAvgExecUs));
        json += buf;
        json += "  \"commandMix\": {\"rect\": " + std::to_string(m_probe->RectCommands()) +
                ", \"text\": " + std::to_string(m_probe->TextCommands()) +
                ", \"line\": " + std::to_string(m_probe->LineCommands()) +
                ", \"arc\": " + std::to_string(m_probe->ArcCommands()) +
                ", \"roundedRect\": " + std::to_string(m_probe->RoundedRectCommands()) +
                ", \"image\": " + std::to_string(m_probe->ImageCommands()) +
                ", \"pushClip\": " + std::to_string(m_probe->PushClipCommands()) +
                ", \"popClip\": " + std::to_string(m_probe->PopClipCommands()) +
                ", \"focusRect\": " + std::to_string(m_probe->FocusRectCommands()) + "},\n";
    }
    if (m_options.cullProbe && m_measureProbe) {
        // 测量链读数：调用次数 / 累计墙钟 / 单次峰值 / 累计字符（us/char 归一化用）
        std::snprintf(buf, sizeof(buf), "  \"measureCalls\": %zu,\n", m_evalMeasureCalls);
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureTotalUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMeasureTotalUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureMaxUs\": %llu,\n",
                      static_cast<unsigned long long>(m_evalMeasureMaxUs));
        json += buf;
        std::snprintf(buf, sizeof(buf), "  \"measureChars\": %zu,\n",
                      m_measureProbe->TotalChars());
        json += buf;
    }
    if (m_options.stateProbe && !m_probeState.stateProbeLog.empty()) {
        AppendStateProbeSummary(json);
    }
    if (m_options.hitProbe && !m_probeState.hitProbeLog.empty()) {
        AppendHitProbeSummary(json);
    }
    if (m_options.reasoningProbe && !m_probeState.reasoningProbeLog.empty()) {
        AppendReasoningProbeSummary(json);
    }
    // ★ 流式态标题（**建行时刻**抓的，见 App.h 注释：事后回读永远是终态）
    if (m_options.reasoningProbe && !m_probeState.reasoningTitleStreaming.empty()) {
        json += "  \"reasoningStreamingTitle\": \"" + m_probeState.reasoningTitleStreaming + "\",\n";
    }
    if (m_options.orderProbe && !m_sess->transcript.Order().empty()) {
        AppendOrderProbeSummary(json);
    }
    if (m_options.rebuildProbe && !m_probeState.rebuildProbeLog.empty()) {
        AppendRebuildProbeSummary(json);
    }
    if (m_options.sessionProbe && !m_probeState.sessionProbeLog.empty()) {
        AppendSessionProbeSummary(json);
    }
    if (m_options.sidebarProbe && !m_probeState.sidebarProbeLog.empty()) {
        AppendSidebarProbeSummary(json);
    }
    // ★★ Step 3 机读证据（2026-10-19）：ask 的**答案内容**是否进模型、是否上屏。
    //   判据（跑批脚本执行）：
    //     · baseline.matched == true（未答：answer 空、两钮都可点 —— 负对照⑥）
    //     · answered.answerModel 与 answered.answerOnScreen 都为 "allow"
    //       （模型与屏幕**各自独立**读出同一个结论，不是同义反复）
    //     · answered.buttonsDisabled == true
    //     · reclick.matched == true（再点同一个钮零变化）
    //     · forward.matched == true 且 forward.answerOnScreen == "allow"
    //       （重建后结论仍在屏上 —— 这就是本步要修的缺陷的直检）
    //     · negative.matched == **false**（负对照：改答案后重建必须报出差异）
    //     · restored.matched == true（改回去必须重新相等 —— 单变量纪律）
    if (m_options.answerProbe && !m_probeState.answerProbeLog.empty()) {
        AppendAnswerProbeSummary(json);
    }
    // ★★ 2026-10-21：中栏头 `↗`/`···` 的机读证据（--menu-probe）。
    //   判据（跑批脚本执行，每条都带负对照，见 `RunMenuProbe` 头注）：
    //     · menu-closed.matched == true（首帧菜单关着且不占槽）
    //     · menu-open.matched == true（点钮 ⇒ 模型翻 ∧ 转录区下移正好 menuH）
    //     · menu-toggle-back.matched == true（再点 ⇒ 回到原顶边）
    //     · menu-clear.matched == true（清空视图只重画，不动模型/行数）
    //     · export-text.matched == true（导出源是 timeline 而非块表）
    //     · export-file.matched == true（真的落盘、逐字节相等、LF）
    //   ★ 默认关闭 ⇒ 摘要里连 `menuProbe` 键都不出现（本仓铁律）。
    if (m_options.menuProbe && !m_probeState.menuProbeLog.empty()) {
        AppendMenuProbeSummary(json);
    }
    // ★★ P4 Phase A（--jump-probe）：「跳到最新」按钮判据。
    //   判据：三相连跑 matched == true（贴底隐藏 / 上翻可见且离开贴底带 / 点击回底隐藏）。
    if (m_options.jumpProbe && !m_probeState.jumpProbeLog.empty()) {
        AppendJumpProbeSummary(json);
    }
    // ★★ P4 Phase B（--key-probe）：全局快捷键判据。
    //   判据：全部相位 matched == true（两态 Ctrl+B 往返 / Esc 双向 / 优先级分层 /
    //   Ctrl+N / passthrough 负对照）。
    if (m_options.keyProbe && !m_probeState.keyProbeLog.empty()) {
        AppendKeyProbeSummary(json);
    }
    // ★★ P4 Phase C（--empty-probe）：空状态引导判据。
    //   判据：全部相位 matched == true（回放负对照两相 + firstRun 正对照四相，
    //   按会话形态分流，一个进程只跑其中一支）。
    if (m_options.emptyProbe && !m_probeState.emptyProbeLog.empty()) {
        AppendEmptyProbeSummary(json);
    }
    // ★★ P4 Phase H（--model-probe）：模型浮窗 + 用量判据。
    if (m_options.modelProbe && !m_probeState.modelProbeLog.empty()) {
        AppendModelProbeSummary(json);
    }
    // ★★ P4 Phase G（--select-probe）：选中复制判据。
    if (m_options.selectProbe && !m_probeState.selectProbeLog.empty()) {
        AppendSelectProbeSummary(json);
    }
    // ★★ P4（--delprov-probe）：删除所选判据。
    if (m_options.delProvProbe && !m_probeState.delProvProbeLog.empty()) {
        AppendDelProvProbeSummary(json);
    }
    // ★★ P4 Phase I（--preview-probe）判据：regex-cases 全对 · open-ok 行内容
    //   逐字节 · escape-rejected / no-workspace 拒读（IO 纪律核心相）· missing
    //   有 notice · too-large / line-cap 双闸 · close-esc 关闭。
    if (m_options.previewProbe && !m_probeState.previewProbeLog.empty()) {
        AppendPreviewProbeSummary(json);
    }
    // ★★ 2026-10-21：HUD 覆盖率（--hud-probe）。
    //   判据：hud-built.matched == true（读过的字段全部建过）
    //         ∧ hud-built-negative.matched == true（打掉一个指针必须被看见）
    if (m_options.hudProbe && !m_probeState.hudProbeLog.empty()) {
        AppendHudProbeSummary(json);
    }
    // ★★ P3.5（--settings-probe）：设置面板 + 凭据/配置/env 注入判据。
    //   判据：全部相 matched == true（first-run / save / session-live /
    //   env-check / cleanup）。★ 默认关闭 ⇒ 摘要里连 settingsProbe 键都不出现。
    if (m_options.settingsProbe && !m_probeState.settingsProbeLog.empty()) {
        AppendSettingsProbeSummary(json);
    }

    // ★★ 2026-10-21：Enter 发送的机读证据（--enter-probe）。
    //   判据（跑批脚本执行，见 `RunEnterProbe` 头注）：
    //     · enter-sends.matched          == true（真发出去了且框被清空）
    //     · shift-enter-newline.matched  == true（没发 + 真换了行）
    //     · enter-empty.matched          == true（空框不发）
    //     · enter-whitespace.matched     == true（只有空白也不发）
    //     · enter-unfocused.matched      == true（负对照：焦点不在框里就不发；
    //                                       若这条红了，前四相证明不了任何事）
    //   ★ 默认关闭 ⇒ 摘要里连 `enterProbe` 键都不出现（本仓铁律）。
    if (m_options.enterProbe && !m_probeState.enterProbeLog.empty()) {
        AppendEnterProbeSummary(json);
    }
    // ★★ 2026-10-21：输入框自动长高的机读证据（--grow-probe）。
    //   判据（跑批脚本执行，见 `RunGrowProbe` 头注）：
    //     · baseline.matched     == true（单行基准高为正，否则后面是空真）
    //     · type-newline.matched == true（Shift+Enter 真按键后**真的变高**——接线判据）
    //     · grow.matched         == true（3 行 > 1 行，且兄弟控件**没被压住**）
    //     · max-clamp.matched    == true（30 行封顶当前 root 高度对应的动态上限）
    //     · resize-adapt.matched == true（实际 resize 后上限与输入框高度一起变化）
    //     · resize-return.matched == true（窗口与输入框高度都回到 resize 前）
    //     · send-reset.matched   == true（发出后回到单行高——`SetText` 路径的同步判据）
    //     · shrink.matched       == true（删掉换行后**变矮**；★ 唯一能打红
    //                              `max(0,·)` 陷阱的方向）
    //     · exact-fit.matched    == true（恰好装下时没有内部滚动条——取整方向判据）
    //   ★ 默认关闭 ⇒ 摘要里连 `growProbe` 键都不出现（本仓铁律）。
    if (m_options.growProbe && !m_probeState.growProbeLog.empty()) {
        AppendGrowProbeSummary(json);
    }
    //   判据（跑批脚本执行，见 `RunResizeProbe` 头注）：
    //     · baseline.matched  == true（有行可测，否则后面是空真）
    //     · grow.matched      == true（每行都变宽 + 视口变高 + 标题栏 == root 宽）
    //     · shrink.matched    == true（每行都变窄 + **maxOffsetX == 0** + 标题栏跟上）
    //     · roundtrip.matched == true（行宽逐位回到 baseline）
    //     · negative.matched  == true（只有 Arrange、没有 Relayout ⇒ 行宽不变；
    //                            若这一条红了，说明前面三条证明不了任何事）
    //   ★ 默认关闭 ⇒ 摘要里连 `resizeProbe` 键都不出现（本仓铁律）。
    if (m_options.resizeProbe && !m_probeState.resizeProbeLog.empty()) {
        AppendResizeProbeSummary(json);
    }
    // ★★ Step 4 机读证据（2026-10-19）：整树重建 + 运行期主题切换。
    //   判据（跑批脚本执行）：
    //     · baseline.matched == true（前置成立：侧栏/转录都非空，否则"全等"是空真）
    //     · rebuild.matched == true 且 rebuild.paints == baseline.paints
    //       （**恰好相等**：重建期间不该有任何重绘；变小=计数器被清零、变大=偷重绘）
    //     · rebuild-again.matched == true（幂等）
    //     · toggle.matched == true 且 toggle.light != baseline.light
    //       （主题真的翻了，且四路内容一处不差 —— 换的是颜色不是内容）
    //     · toggle-back.matched == true
    //     · negative.matched == **false** 且 firstDivergence 含 "sidebarVisible"
    //       （负对照：模型侧状态确实进了比对面）
    //     · restored.matched == true（单变量纪律）
    //     · uiRebuilds 单调 1→2→3→4→5
    if (m_options.themeProbe && !m_probeState.themeProbeLog.empty()) {
        AppendThemeProbeSummary(json);
    }

    // ★★ Step 5：历史会话探针。同样**只在开启时写**（默认摘要逐字节不变）。
    if (m_options.historyProbe && !m_probeState.historyProbeLog.empty()) {
        AppendHistoryProbeSummary(json);
    }

    std::snprintf(buf, sizeof(buf), "  \"seqGaps\": %zu,\n", m_sess->transcript.SeqGaps());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"violations\": %zu,\n", m_sess->transcript.Violations());
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"unknownTypes\": %zu,\n", m_sess->transcript.UnknownTypes());
    json += buf;
    // ★ 未知 block_start.kind（2026-10-13）：与 unknownTypes **并列而非合并**——
    //   前者是信封层 `t`（I5 明文覆盖），后者是 `kind`（协议 v2 起由 I8 覆盖）。
    //   ★ v2（2026-10-14）：`reasoning` 是**已知** kind ⇒ 不再计入本项
    //     （cot 夹具的读数由 12 变 0，正是这条生效的证据）。
    std::snprintf(buf, sizeof(buf), "  \"unknownKinds\": %zu,\n", m_sess->transcript.UnknownKinds());
    json += buf;
    // ★ 协议 §9（v2）：如实记下 core 声明的版本与本 GUI 的上限，
    //   以及**是否因版本不兼容而拒绝连接**——否则一次被拒的运行会留下
    //   一份"看起来跑完了"的空摘要。
    std::snprintf(buf, sizeof(buf), "  \"protocolDeclared\": %d,\n", m_sess->protocolVer);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolMaxSupported\": %d,\n",
                  kProtocolMaxSupported);
    json += buf;
    std::snprintf(buf, sizeof(buf), "  \"protocolIncompatible\": %s,\n",
                  m_sess->protocolIncompatible ? "true" : "false");
    json += buf;
    // ★ 协议 v2 §3.1：上下文容量的读数（0 = core 未声明 ⇒ 百分比显示 `—`）
    std::snprintf(buf, sizeof(buf), "  \"contextLimit\": %lld,\n",
                  static_cast<long long>(m_sess->contextLimit));
    json += buf;
    if (m_options.workspaceProbe) {
        AppendWorkspaceProbeSummary(json);
    }
    // ★★ REQ-03 机读读数（2026-10-24）。判据在跑批脚本里，见 `.scratch/probe_async_negctl.py`：
    //   · `--legacy-poll` 世界：`avgBlockedUs` ≈ 8000（空闲 tick 把 8ms 等满）
    //   · 默认（推）世界：`avgBlockedUs` ≈ 0，且 `linesViaWake` 占绝对多数
    //   · `maxLinesPerCallback` ≤ 256（预算生效；去掉预算这个数会跳到数千）
    //   · `latencyP99Us` 与 tick 周期（33000µs）脱钩——延迟不再由渲染节奏决定
    //   ★ 默认关闭 ⇒ 摘要里连 `asyncProbe` 键都不出现（本仓铁律）。
    if (m_options.asyncProbe) {
        AppendAsyncProbeSummary(json);
    }
    // ★ 上下文占用读数（分子/分母都如实输出，便于复核百分比是否算对）
    std::snprintf(buf, sizeof(buf), "  \"usageInput\": %lld\n",
                  static_cast<long long>(m_sess->usageIn));
    json += buf;
    json += "}\n";

    if (!m_options.outPath.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_options.outPath.parent_path(), ec);
        std::ofstream out(m_options.outPath, std::ios::binary);
        if (out) {
            out << json;
        } else {
            ECDI::Logger::Log(ECDI::LogLevel::Warning,
                              L"WriteSummary: cannot open output file");
        }
    }
}
