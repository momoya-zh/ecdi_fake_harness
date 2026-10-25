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
        // ★★ REQ-04 机读证据（2026-10-14）：状态翻转轨迹。
        //   每条 = {core state, 发送使能, 停止使能, 发送标题}——**只在变化时**记，
        //   故可逐行核对协议 §6「发送/停止只由 state 驱动」。
        //   ★ 标题文本来自 `Button::GetText()` **直接回读**（不靠像素/截图）。
        json += "  \"stateProbe\": [";
        for (size_t i = 0; i < m_probeState.stateProbeLog.size(); ++i) {
            const StateProbeRec& r = m_probeState.stateProbeLog[i];
            json += (i ? ", " : "");
            json += "{\"state\": \"" + r.coreState + "\", \"sendEnabled\": " +
                    (r.sendEnabled ? "true" : "false") + ", \"stopEnabled\": " +
                    (r.stopEnabled ? "true" : "false") + ", \"sendTitle\": \"" +
                    r.sendTitle + "\"}";
        }
        json += "],\n";
    }
    if (m_options.hitProbe && !m_probeState.hitProbeLog.empty()) {
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
    if (m_options.reasoningProbe && !m_probeState.reasoningProbeLog.empty()) {
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
    // ★ 流式态标题（**建行时刻**抓的，见 App.h 注释：事后回读永远是终态）
    if (m_options.reasoningProbe && !m_probeState.reasoningTitleStreaming.empty()) {
        json += "  \"reasoningStreamingTitle\": \"" + m_probeState.reasoningTitleStreaming + "\",\n";
    }
    if (m_options.orderProbe && !m_sess->transcript.Order().empty()) {
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
    if (m_options.rebuildProbe && !m_probeState.rebuildProbeLog.empty()) {
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
    if (m_options.sessionProbe && !m_probeState.sessionProbeLog.empty()) {
        // ★★ Step 0d 机读证据（2026-10-19）：新建/切换会话的行为轨迹。
        //   判据（跑批脚本执行）：
        //     · new.sessionCount == baseline.sessionCount + 1（真的多了一个会话）
        //     · new.blockRowCount == 0（**负对照**：新会话不含旧转录的块行——
        //       若它非零，说明新会话抄了旧会话的模型，"新建"是假的）
        //     · back.activeIndex == 0 ∧ back.rowCount == baseline.rowCount（切回无损）
        json += "  \"sessionProbe\": [";
        for (size_t i = 0; i < m_probeState.sessionProbeLog.size(); ++i) {
            const SessionProbeRec& r = m_probeState.sessionProbeLog[i];
            json += (i ? ", " : "");
            char b[256];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"sessionCount\": %zu, \"activeIndex\": %zu, "
                          "\"rowCount\": %zu, \"blockRowCount\": %zu}",
                          r.phase.c_str(), r.sessionCount, r.activeIndex,
                          r.rowCount, r.blockRowCount);
            json += b;
        }
        json += "],\n";
    }
    if (m_options.sidebarProbe && !m_probeState.sidebarProbeLog.empty()) {
        // ★★ Step 1 机读证据（2026-10-19）：侧栏列表的 A==B 轨迹。
        //   判据（跑批脚本执行）：
        //     · rebuild.matched == true ∧ rebuild-again.matched == true
        //       （原地重建两次都无损 ⇒ 列表是模型的纯函数）
        //     · negative.matched == **false**（负对照：逆序必须被抓出来。
        //       若它为 true，说明比对函数没在看次序，上面两条 matched 不作数）
        //     · new.rowCount == baseline.rowCount + 1 ∧ new.sessionCount == +1
        //     · deleted.rowCount == baseline.rowCount ∧ deleted.transcriptRows == baseline.transcriptRows
        //       （删掉会话后列表回到原状，且**活动会话的转录一位没变**）
        json += "  \"sidebarProbe\": [";
        // ★ 自带一份转义（与 rebuildProbe 段那个 `esc` 同款，但**不能复用**：
        //   它是那段 `if` 块内的局部 lambda，作用域到 `],\n` 就结束了）。
        //   会话 label 含夹具文件名，可能是任意 UTF-8；`firstDivergence` 里还会拼
        //   行文本 ⇒ 不转义就会让整份摘要不再是合法 JSON（跑批脚本读它会整份失败）。
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
        for (size_t i = 0; i < m_probeState.sidebarProbeLog.size(); ++i) {
            const SidebarProbeRec& r = m_probeState.sidebarProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"rowCount\": %zu, \"sessionCount\": %zu, "
                          "\"activeIndex\": %zu, \"selectedRows\": %zu, "
                          "\"selectedIndex\": %zu, \"transcriptRows\": %zu, "
                          "\"sidebarVisible\": %s, \"midWidth\": %d, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.rowCount, r.sessionCount, r.activeIndex,
                          r.selectedRows, r.selectedIndex, r.transcriptRows,
                          r.sidebarVisible ? "true" : "false", r.midWidth,
                          r.matched ? "true" : "false",
                          esc(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        json += "  \"menuProbe\": [";
        // 第五份转义 lambda（各段作用域独立，不能复用别段的局部 lambda）
        auto escM = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.menuProbeLog.size(); ++i) {
            const MenuProbeRec& r = m_probeState.menuProbeLog[i];
            json += (i ? ", " : "");
            char b[768];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"modelOpen\": %s, \"widgetVisible\": %s, "
                          "\"headBottom\": %d, \"scrollTop\": %d, \"gap\": %d, "
                          "\"midWidth\": %d, \"transcriptRows\": %zu, \"timelineLen\": %zu, "
                          "\"exportLines\": %zu, \"modelBlockLines\": %zu, "
                          "\"noticeRows\": %zu, \"exportHasNotice\": %s, "
                          "\"exportWrote\": %s, \"exportBytes\": %zu, \"exportLfOnly\": %s, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.modelOpen ? "true" : "false",
                          r.widgetVisible ? "true" : "false",
                          r.headBottom, r.scrollTop, r.gap, r.midWidth,
                          r.transcriptRows, r.timelineLen,
                          r.exportLines, r.modelBlockLines, r.noticeRows,
                          r.exportHasNotice ? "true" : "false",
                          r.exportWrote ? "true" : "false", r.exportBytes,
                          r.exportLfOnly ? "true" : "false",
                          r.matched ? "true" : "false",
                          escM(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P4 Phase A（--jump-probe）：「跳到最新」按钮判据。
    //   判据：三相连跑 matched == true（贴底隐藏 / 上翻可见且离开贴底带 / 点击回底隐藏）。
    if (m_options.jumpProbe && !m_probeState.jumpProbeLog.empty()) {
        json += "  \"jumpProbe\": [";
        for (size_t i = 0; i < m_probeState.jumpProbeLog.size(); ++i) {
            const JumpProbeRec& r = m_probeState.jumpProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"visible\": %s, \"offset\": %d, "
                          "\"maxOffset\": %d, \"matched\": %s, \"note\": \"%s\"}",
                          r.phase.c_str(), r.visible ? "true" : "false",
                          r.offset, r.maxOffset,
                          r.matched ? "true" : "false", r.note.c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P4 Phase B（--key-probe）：全局快捷键判据。
    //   判据：全部相位 matched == true（两态 Ctrl+B 往返 / Esc 双向 / 优先级分层 /
    //   Ctrl+N / passthrough 负对照）。
    if (m_options.keyProbe && !m_probeState.keyProbeLog.empty()) {
        json += "  \"keyProbe\": [";
        for (size_t i = 0; i < m_probeState.keyProbeLog.size(); ++i) {
            const KeyProbeRec& r = m_probeState.keyProbeLog[i];
            json += (i ? ", " : "");
            char b[640];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"sidebarVisible\": %s, "
                          "\"sidebarShown\": %s, \"settingsVisible\": %s, "
                          "\"settingsShown\": %s, \"menuOpen\": %s, "
                          "\"menuShown\": %s, \"sessionCount\": %zu, "
                          "\"inputText\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                          r.phase.c_str(),
                          r.sidebarVisible ? "true" : "false",
                          r.sidebarShown ? "true" : "false",
                          r.settingsVisible ? "true" : "false",
                          r.settingsShown ? "true" : "false",
                          r.menuOpen ? "true" : "false",
                          r.menuShown ? "true" : "false",
                          r.sessionCount, r.inputText.c_str(),
                          r.matched ? "true" : "false", r.note.c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P4 Phase C（--empty-probe）：空状态引导判据。
    //   判据：全部相位 matched == true（回放负对照两相 + firstRun 正对照四相，
    //   按会话形态分流，一个进程只跑其中一支）。
    if (m_options.emptyProbe && !m_probeState.emptyProbeLog.empty()) {
        json += "  \"emptyProbe\": [";
        for (size_t i = 0; i < m_probeState.emptyProbeLog.size(); ++i) {
            const EmptyProbeRec& r = m_probeState.emptyProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"settingsVisible\": %s, "
                          "\"guideExists\": %s, \"guideVisible\": %s, "
                          "\"rowCount\": %zu, \"matched\": %s, \"note\": \"%s\"}",
                          r.phase.c_str(),
                          r.settingsVisible ? "true" : "false",
                          r.guideExists ? "true" : "false",
                          r.guideVisible ? "true" : "false",
                          r.rowCount, r.matched ? "true" : "false",
                          r.note.c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P4 Phase H（--model-probe）：模型浮窗 + 用量判据。
    if (m_options.modelProbe && !m_probeState.modelProbeLog.empty()) {
        json += "  \"modelProbe\": [";
        for (size_t i = 0; i < m_probeState.modelProbeLog.size(); ++i) {
            const ModelProbeRec& r = m_probeState.modelProbeLog[i];
            json += (i ? ", " : "");
            char b[640];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"popupVisible\": %s, "
                          "\"listRows\": %zu, \"ctxKnown\": %s, "
                          "\"chipText\": \"%s\", \"matched\": %s, \"note\": \"%s\"}",
                          r.phase.c_str(), r.popupVisible ? "true" : "false",
                          r.listRows, r.ctxKnown ? "true" : "false",
                          r.chipText.c_str(), r.matched ? "true" : "false",
                          r.note.c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P4 Phase G（--select-probe）：选中复制判据。
    if (m_options.selectProbe && !m_probeState.selectProbeLog.empty()) {
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
    // ★★ P4（--delprov-probe）：删除所选判据。
    if (m_options.delProvProbe && !m_probeState.delProvProbeLog.empty()) {
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
    // ★★ 2026-10-21：HUD 覆盖率（--hud-probe）。
    //   判据：hud-built.matched == true（读过的字段全部建过）
    //         ∧ hud-built-negative.matched == true（打掉一个指针必须被看见）
    if (m_options.hudProbe && !m_probeState.hudProbeLog.empty()) {
        json += "  \"hudProbe\": [";
        auto escH2 = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += static_cast<char>(c);
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_probeState.hudProbeLog.size(); ++i) {
            const HudProbeRec& r = m_probeState.hudProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"touched\": %zu, \"unbuilt\": %zu, "
                          "\"unbuiltNames\": \"%s\", \"matched\": %s, "
                          "\"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.touched, r.unbuilt,
                          escH2(r.unbuiltNames).c_str(),
                          r.matched ? "true" : "false",
                          escH2(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }
    // ★★ P3.5（--settings-probe）：设置面板 + 凭据/配置/env 注入判据。
    //   判据：全部相 matched == true（first-run / save / session-live /
    //   env-check / cleanup）。★ 默认关闭 ⇒ 摘要里连 settingsProbe 键都不出现。
    if (m_options.settingsProbe && !m_probeState.settingsProbeLog.empty()) {
        json += "  \"settingsProbe\": [";
        auto escS = [](const std::string& in) {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in) {
                switch (c) {
                    case '"': out += "\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out += static_cast<char>(c);
                }
            }
            return out;
        };
        for (size_t i = 0; i < m_probeState.settingsProbeLog.size(); ++i) {
            const SettingsProbeRec& r = m_probeState.settingsProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"matched\": %s, "
                          "\"firstDivergence\": \"%s\"}",
                          r.phase.c_str(),
                          r.matched ? "true" : "false",
                          escS(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        json += "  \"enterProbe\": [";
        auto escE = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.enterProbeLog.size(); ++i) {
            const EnterProbeRec& r = m_probeState.enterProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"promptCount\": %zu, "
                          "\"inputText\": \"%s\", \"noticeRows\": %zu, "
                          "\"sent\": %s, \"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.promptCount, escE(r.inputText).c_str(),
                          r.noticeRows, r.sent ? "true" : "false",
                          r.matched ? "true" : "false",
                          escE(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        json += "  \"growProbe\": [";
        auto escG = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.growProbeLog.size(); ++i) {
            const GrowProbeRec& r = m_probeState.growProbeLog[i];
            json += (i ? ", " : "");
            char b[512];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"rootH\": %d, \"maxInputH\": %d, "
                          "\"inputH\": %d, \"barH\": %d, \"lines\": %d, "
                          "\"hintY\": %d, \"buttonY\": %d, \"matched\": %s, "
                          "\"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.rootH, r.maxInputH, r.inputH, r.barH, r.lines,
                          r.hintY, r.buttonY,
                          r.matched ? "true" : "false",
                          escG(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        json += "  \"resizeProbe\": [";
        auto escR = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.resizeProbeLog.size(); ++i) {
            const ResizeProbeRec& r = m_probeState.resizeProbeLog[i];
            json += (i ? ", " : "");
            char b[640];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"rootW\": %d, \"rootH\": %d, "
                          "\"scrollW\": %d, \"scrollH\": %d, \"midW\": %d, "
                          "\"captionW\": %d, \"maxOffsetX\": %d, "
                          "\"rowWMin\": %d, \"rowWMax\": %d, \"rows\": %zu, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.rootW, r.rootH, r.scrollW, r.scrollH,
                          r.midW, r.captionW, r.maxOffsetX, r.rowWMin, r.rowWMax, r.rows,
                          r.matched ? "true" : "false",
                          escR(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        // ★ 顶层两个标量：这一次进程最终停在什么主题、整树重建过几次。
        //   只在探针开启时写 ⇒ 默认摘要**逐字节不变**（本仓铁律）。
        json += std::string("  \"theme\": \"") + (harness::g_themeLight ? "light" : "dark")
                + "\",\n";
        json += "  \"uiRebuilds\": " + std::to_string(m_uiRebuilds) + ",\n";
        json += "  \"themeProbe\": [";
        // ★ 第三份转义 lambda（前两份都在各自 if 块作用域内，不能复用）
        auto escT = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.themeProbeLog.size(); ++i) {
            const ThemeProbeRec& r = m_probeState.themeProbeLog[i];
            json += (i ? ", " : "");
            char b[768];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"light\": %s, \"uiRebuilds\": %zu, "
                          "\"paints\": %zu, \"frames\": %zu, \"frameUsTotal\": %zu, "
                          "\"sidebarRows\": %d, \"transcriptRows\": %d, "
                          "\"inputText\": \"%s\", \"sidebarVisible\": %s, "
                          "\"activeIndex\": %zu, \"sessionCount\": %zu, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.light ? "true" : "false", r.uiRebuilds,
                          r.paints, r.frames, r.frameUsTotal, r.sidebarRows, r.transcriptRows,
                          escT(r.inputText).c_str(), r.sidebarVisible ? "true" : "false",
                          r.activeIndex, r.sessionCount,
                          r.matched ? "true" : "false", escT(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
    }

    // ★★ Step 5：历史会话探针。同样**只在开启时写**（默认摘要逐字节不变）。
    if (m_options.historyProbe && !m_probeState.historyProbeLog.empty()) {
        json += "  \"historyProbe\": [";
        // 第四份转义 lambda（前三份都在各自 if 块作用域内）
        auto escH = [](const std::string& in) {
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
        for (size_t i = 0; i < m_probeState.historyProbeLog.size(); ++i) {
            const HistoryProbeRec& r = m_probeState.historyProbeLog[i];
            json += (i ? ", " : "");
            char b[1024];
            std::snprintf(b, sizeof(b),
                          "{\"phase\": \"%s\", \"filesOnDisk\": %zu, \"rowsOnScreen\": %zu, "
                          "\"openedLabel\": \"%s\", \"originKind\": \"%s\", "
                          "\"blocks\": %zu, \"deltas\": %zu, \"messages\": %zu, "
                          "\"atEnd\": %s, \"askRows\": %zu, \"answeredRows\": %zu, "
                          "\"modelAnswers\": %zu, \"sidecarExisted\": %s, "
                          "\"sidecarWritten\": %s, \"sidecarRemoved\": %s, "
                          "\"matched\": %s, \"firstDivergence\": \"%s\"}",
                          r.phase.c_str(), r.filesOnDisk, r.rowsOnScreen,
                          escH(r.openedLabel).c_str(), escH(r.originKind).c_str(),
                          r.blocks, r.deltas, r.messages,
                          r.atEnd ? "true" : "false", r.askRows, r.answeredRows,
                          r.modelAnswers,
                          r.sidecarExisted ? "true" : "false",
                          r.sidecarWritten ? "true" : "false",
                          r.sidecarRemoved ? "true" : "false",
                          r.matched ? "true" : "false", escH(r.firstDivergence).c_str());
            json += b;
        }
        json += "],\n";
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
        json::Value records;
        records.type = json::Value::Type::Array;
        auto putString = [](json::Value& object, const char* key, const std::string& value) {
            json::Value field;
            field.type = json::Value::Type::String;
            field.text = value;
            object.object[key] = std::move(field);
        };
        size_t expectedMismatches = 0;
        bool checksPassed = true;
        for (const WorkspaceProbeRec& rec : m_probeState.workspaceProbeLog) {
            json::Value item;
            item.type = json::Value::Type::Object;
            putString(item, "phase", rec.phase);
            putString(item, "clientId", rec.clientId);
            putString(item, "path", rec.path);
            putString(item, "pendingId", rec.pendingId);
            json::Value count;
            count.type = json::Value::Type::Number;
            count.number = static_cast<double>(rec.canonicalMatches);
            item.object["canonicalMatches"] = std::move(count);
            json::Value send;
            send.type = json::Value::Type::Bool;
            send.boolean = rec.sendEnabled;
            item.object["sendEnabled"] = std::move(send);
            json::Value matched;
            matched.type = json::Value::Type::Bool;
            matched.boolean = rec.matched;
            item.object["matched"] = std::move(matched);
            json::Value expected;
            expected.type = json::Value::Type::Bool;
            expected.boolean = rec.expectedMismatch;
            // ★ §7 判据的机器读数：控件上的实际文案 + 形状判据。
            putString(item, "bubbleText", rec.bubbleText);
            {
                json::Value shape;
                shape.type = json::Value::Type::Bool;
                shape.boolean = rec.bubbleShapeOk;
                item.object["bubbleShapeOk"] = std::move(shape);
            }
            item.object["expectedMismatch"] = std::move(expected);
            records.array.push_back(std::move(item));
            if (rec.expectedMismatch) {
                ++expectedMismatches;
                checksPassed = checksPassed && !rec.matched && rec.phase == "negative";
            } else {
                checksPassed = checksPassed && rec.matched;
            }
        }
        const bool probePassed = m_probeState.workspaceProbeDone && m_probeState.workspaceProbeLog.size() == 9 &&
                                 expectedMismatches == 1 && checksPassed;
        json += "  \"workspaceProbe\": " + records.Dump() + ",\n";
        json += std::string("  \"workspaceProbePassed\": ") +
                (probePassed ? "true,\n" : "false,\n");
    }
    // ★★ REQ-03 机读读数（2026-10-24）。判据在跑批脚本里，见 `.scratch/probe_async_negctl.py`：
    //   · `--legacy-poll` 世界：`avgBlockedUs` ≈ 8000（空闲 tick 把 8ms 等满）
    //   · 默认（推）世界：`avgBlockedUs` ≈ 0，且 `linesViaWake` 占绝对多数
    //   · `maxLinesPerCallback` ≤ 256（预算生效；去掉预算这个数会跳到数千）
    //   · `latencyP99Us` 与 tick 周期（33000µs）脱钩——延迟不再由渲染节奏决定
    //   ★ 默认关闭 ⇒ 摘要里连 `asyncProbe` 键都不出现（本仓铁律）。
    if (m_options.asyncProbe) {
        uint64_t p50 = 0;
        uint64_t p99 = 0;
        if (!m_probeState.apLatencyUs.empty()) {
            std::vector<uint64_t> sorted = m_probeState.apLatencyUs;
            std::sort(sorted.begin(), sorted.end());
            p50 = sorted[sorted.size() / 2];
            p99 = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
        }
        const size_t total = m_probeState.apLinesWake + m_probeState.apLinesTick;
        const uint64_t avgBlockedUs =
            m_probeState.apBlockedTicks ? (m_probeState.apBlockedUs / m_probeState.apBlockedTicks) : 0;
        // ★ 延迟分布桶：P99 单看一个数会被尾部样本带偏，而"尾巴是偶发还是系统性的"
        //   决定它是不是缺陷。桶边界取 1ms / 5ms / 33ms(=tick 周期)——
        //   ★ 超过 33ms 的样本说明延迟**仍由 tick 决定**（= 推这条路没生效）。
        uint64_t latMax = 0;
        size_t le1ms = 0;
        size_t le5ms = 0;
        size_t le33ms = 0;
        size_t gt33ms = 0;
        for (uint64_t us : m_probeState.apLatencyUs) {
            if (us > latMax) latMax = us;
            if (us <= 1000) ++le1ms;
            else if (us <= 5000) ++le5ms;
            else if (us <= 33000) ++le33ms;
            else ++gt33ms;
        }
        json += std::string("  \"asyncProbe\": {\"mode\": \"") +
                (m_options.legacyPoll ? "legacy" : "async") + "\"";
        json += ", \"ticks\": " + std::to_string(m_probeState.apTicks);
        json += ", \"blockedTicks\": " + std::to_string(m_probeState.apBlockedTicks);
        json += ", \"avgBlockedUs\": " + std::to_string(avgBlockedUs);
        json += ", \"maxBlockedUs\": " + std::to_string(m_probeState.apMaxBlockedUs);
        json += ", \"linesTotal\": " + std::to_string(total);
        json += ", \"linesViaWake\": " + std::to_string(m_probeState.apLinesWake);
        json += ", \"linesViaTick\": " + std::to_string(m_probeState.apLinesTick);
        json += ", \"callbacks\": " + std::to_string(m_probeState.apCallbacks);
        // ★ 投递次数必须问**源本身**（`ChildSource::WakePosts()`，只统计成功投递）。
        //   第一版这里打印的是 App 自己的一个原子量，而它从来没被加过 ⇒ 恒为 0，
        //   看上去像"一次都没投递"——那正是 `--hud-probe` 抓的「读了但没写」同族。
        json += ", \"posts\": " +
                std::to_string(m_sess->child ? m_sess->child->WakePosts() : 0);
        json += ", \"maxLinesPerCallback\": " + std::to_string(m_probeState.apMaxLinesPerCallback);
        json += ", \"latencySamples\": " + std::to_string(m_probeState.apLatencyUs.size());
        json += ", \"latencyP50Us\": " + std::to_string(p50);
        json += ", \"latencyP99Us\": " + std::to_string(p99);
        json += ", \"latencyMaxUs\": " + std::to_string(latMax);
        json += ", \"latLe1ms\": " + std::to_string(le1ms);
        json += ", \"latLe5ms\": " + std::to_string(le5ms);
        json += ", \"latLe33ms\": " + std::to_string(le33ms);
        json += ", \"latGt33ms\": " + std::to_string(gt33ms);
        json += ", \"eofSeen\": " + std::string(m_sess->liveEof ? "true" : "false");
        json += "},\n";
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
