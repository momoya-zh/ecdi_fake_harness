// ============================================================
// 探针基座：合成输入 / 视图快照 / 逐字段比对 / 夹具摘要
// ------------------------------------------------------------
// 为什么单独一个文件：这 9 个函数是**所有探针共用的仪器零件**——它们不含任何判据，只负责「把视图抓成可比的形状」和「把差异指出来」。
//   探针本体（Run*Probe）住在 ProbeTranscript/Sidebar/Chrome/Flow，四者都只依赖本文件 + 产品侧函数。
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


void HarnessApp::SynthesizeClickAt(int x, int y) {
    // ★ 走**真实入口** `Application::OnEvent`——与系统消息到达后的路径完全同源
    //   （`Window::OnEvent` → `Application::OnEvent` → 具名 handler）。
    //   `m_application` 是 Run() 栈上的 HarnessApplication（Application 子类）。
    if (!m_window || !m_application) return;
    ECDI::MouseButtonDownEvent down(m_window, x, y, ECDI::MouseButton::Left);
    m_application->OnEvent(down);
    ECDI::MouseButtonUpEvent up(m_window, x, y, ECDI::MouseButton::Left);
    m_application->OnEvent(up);
}

// ★★ 2026-10-21：合成一次真实按键（Enter 发送探针用）。
//   ★ 与 `SynthesizeClickAt` 同一纪律：走 `Application::OnEvent`，不直接调
//     `PromptBar::Submit()`。直接调会把"这个按键真的派给了输入框吗"整个绕过——
//     那正是要检验的命题（回调接没接上与函数对不对是两件事）。
//   ★ 键盘是**单收件人**语义（`Application::OnKeyDown` 的注释：不冒泡），
//     所以这里合成的事件只会交给当前焦点控件 ⇒ 焦点在哪就是自变量。
void HarnessApp::SynthesizeKey(ECDI::KeyCode code, bool shift,
                               ECDI::KeyModifier extra) {
    if (!m_window || !m_application) return;
    ECDI::KeyModifier mod = extra;
    if (shift) mod = static_cast<ECDI::KeyModifier>(
        static_cast<int>(mod) | static_cast<int>(ECDI::KeyModifier::Shift));
    ECDI::KeyDownEvent down(m_window, code, mod);
    m_application->OnEvent(down);
}

std::string HarnessApp::WidgetKindName(const ECDI::Widget* w) {
    if (w == nullptr) return "(null)";
    // 只区分本探针用得到的几类——为的是让读数**自解释**（"命中者是谁"）
    // ★ 顺序敏感：具体类在前（Button 是 TextWidget 子类）。
    if (dynamic_cast<const ECDI::Button*>(w)) return "Button";
    if (dynamic_cast<const ECDI::TextBox*>(w)) return "TextBox";
    if (dynamic_cast<const ECDI::Label*>(w)) return "Label";
    if (dynamic_cast<const ECDI::Panel*>(w)) return "Panel";
    return "Widget";
}

std::vector<std::string> HarnessApp::SnapshotRows() const {
    std::vector<std::string> out;
    out.reserve(m_sess->rows.size());
    for (const RowRec& r : m_sess->rows) {
        // 行种类：A=权限门 / B=块行 / N=通知行
        //   ★ 判据必须只用 `RowRec` 上**已经存在**的信息，不能再回头问模型
        //     ——否则重建前后两侧会拿到同一份答案，比对就恒等。
        const char* kind = nullptr;
        const std::string* id = nullptr;
        if (r.ask) {
            kind = "A";
            id = &r.askId;
        } else if (r.tool || r.reasoning || !r.blockId.empty()) {
            kind = "B";
            id = &r.blockId;
        } else {
            kind = "N";
        }
        std::string line;
        line += kind;
        line += '\x1f';
        if (id) line += *id;
        line += '\x1f';
        line += r.expanded ? "1" : "0";
        line += '\x1f';
        line += r.answered ? "1" : "0";
        line += '\x1f';
        line += r.headerBtn ? r.headerBtn->GetText() : std::string();
        line += '\x1f';
        if (r.label) line += r.label->GetText();
        line += '\x1f';
        // ★★ Step 3：`answer` 段放**最后**——既有字段的编号一位不动，
        //   `CompareRows` 的 `kFieldName[]` 只需在尾部追加一个名字；
        //   放在中间会让 "expanded/answered/header/label" 全部错位，
        //   历史读数与解读脚本会静默串味。
        line += r.answer;
        out.push_back(std::move(line));
    }
    return out;
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建的比对面。
//   与侧栏/转录两套快照的关系是**聚合**而非替代：这里不重写字段格式，直接复用
//   两个既有量具（`SnapshotSessionList` / `SnapshotRows`），只额外抓三样它们看不见的
//   东西——输入框草稿、侧栏可见性、以及"现在是什么主题"。
//   ★ 为什么必须四路并联：整树重建的风险不是"某一处画错了"，而是"某处状态没被搬过去"。
//     只查侧栏会漏掉转录的展开态；只查转录会漏掉输入框里的草稿；只查控件会漏掉
//     `m_sidebarVisible` 这种只存在于模型里的状态（它没有对应的"控件属性"可读）。
UiSnapshot HarnessApp::SnapshotUi() const {
    UiSnapshot snap;
    snap.sidebar = SnapshotSessionList();
    snap.transcript = SnapshotRows();
    snap.inputText = (m_promptBar && m_promptBar->input())
                         ? m_promptBar->input()->GetText() : std::string();
    snap.sidebarVisible = (m_hud.sidebar != nullptr) && m_hud.sidebar->IsVisible();
    snap.light = harness::g_themeLight;
    snap.activeIndex = m_activeIndex;
    snap.sessionCount = m_sessions.size();
    // 仪器读数也进快照：便于在失败时看出"是不是重建把计数器搞坏了"。
    snap.paints = m_counting ? m_counting->PaintCount() : 0;
    snap.frames = m_counting ? m_counting->FrameDurationsUs().size() : 0;
    uint64_t total = 0;
    if (m_counting) {
        for (uint64_t us : m_counting->FrameDurationsUs()) total += us;
    }
    snap.frameUsTotal = static_cast<size_t>(total);
    return snap;
}

HarnessApp::SidebarCompare HarnessApp::CompareUi(const UiSnapshot& before,
                                                 const UiSnapshot& after) const {
    // 侧栏：直接复用 `CompareSessionList`（它会逐字段报 name/selected/buttons/geometry）。
    SidebarCompare side = CompareSessionList(before.sidebar, after.sidebar);
    if (!side.matched) {
        side.firstDivergence = "sidebar: " + side.firstDivergence;
        return side;
    }

    // 转录区：直接复用 `CompareRows`（`mismatches` 才是它的判据，`firstDivergence`
    // 由它自己的字段表产出）。★ 不用 blockOnly：整树重建要保住**全部**行。
    const RebuildProbeRec rows = CompareRows("ui", before.transcript, after.transcript, false);
    if (rows.mismatches != 0) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "transcript: " + rows.firstDivergence;
        return cmp;
    }

    // 输入框草稿。★ 这条最容易漏：`PromptBar` 的 TextBox 是**用户输入**，
    //   它不在 `timeline` 里、也不在任何 Snapshot 里，重建时若不显式搬运就会静默丢失。
    if (before.inputText != after.inputText) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "input: [" + before.inputText + "] -> [" + after.inputText + "]";
        return cmp;
    }

    // 状态标记：全部是**只存在于模型/句柄层**的事实，没有任何一个是"画出来的"。
    if (before.sidebarVisible != after.sidebarVisible) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = std::string("state: sidebarVisible [")
                              + (before.sidebarVisible ? "1" : "0") + "] -> ["
                              + (after.sidebarVisible ? "1" : "0") + "]";
        return cmp;
    }
    if (before.activeIndex != after.activeIndex) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "state: activeIndex " + std::to_string(before.activeIndex)
                              + " -> " + std::to_string(after.activeIndex);
        return cmp;
    }
    if (before.sessionCount != after.sessionCount) {
        SidebarCompare cmp;
        cmp.matched = false;
        cmp.firstDivergence = "state: sessionCount " + std::to_string(before.sessionCount)
                              + " -> " + std::to_string(after.sessionCount);
        return cmp;
    }
    return SidebarCompare{};
}

RebuildProbeRec HarnessApp::CompareRows(const char* phase,
                                                    const std::vector<std::string>& before,
                                                    const std::vector<std::string>& after,
                                                    bool blockOnly) const {
    RebuildProbeRec rec;
    rec.phase = phase;

    // ① 「只比块行」时两侧都先做**同一次**投影。
    //   否则"少了通知行"会被读成"次序错"——两种损失混在一个数里就没法归因。
    auto project = [](const std::vector<std::string>& rows) {
        std::vector<std::string> out;
        for (const std::string& r : rows) {
            if (!r.empty() && r[0] == 'B') out.push_back(r);
        }
        return out;
    };
    const std::vector<std::string> b = blockOnly ? project(before) : before;
    const std::vector<std::string> a = blockOnly ? project(after) : after;

    rec.beforeCount = b.size();
    rec.rowCount = a.size();
    rec.timelineCount = m_sess->timeline.size();
    // 滚动位置：重建后由 `RebuildTranscriptView` 自己恢复，这里读**恢复之后**的值，
    // 与摘要里的 `rebuildScrollBefore` 对读即可判"有没有跳回顶部"。
    rec.scrollOffset = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
    rec.maxScrollOffset = m_scroll ? m_scroll->GetMaxOffsetY() : 0;

    // 身份 = 前两段（种类 + 标识）。身份不同 ⇒ 次序/条目错，单独计数。
    auto identity = [](const std::string& s) {
        const size_t p1 = s.find('\x1f');
        if (p1 == std::string::npos) return s;
        const size_t p2 = s.find('\x1f', p1 + 1);
        if (p2 == std::string::npos) return s;
        return s.substr(0, p2);
    };
    // ★ 分歧必须报到**字段**一级：负对照的首个分歧常是 `B/b1 -> B/b1`（身份相同、
    //   展开态不同），只印身份就是一条**假线索**——排查的人会以为次序没错。
    //   字段名按 `SnapshotRows` 的拼装次序编号。
    static const char* kFieldName[] = {"kind", "id", "expanded", "answered", "header", "label", "answer"};
    auto firstDiffField = [](const std::string& x, const std::string& y) {
        std::vector<std::string> fx;
        std::vector<std::string> fy;
        auto split = [](const std::string& s, std::vector<std::string>& out) {
            size_t start = 0;
            while (true) {
                const size_t p = s.find('\x1f', start);
                if (p == std::string::npos) {
                    out.push_back(s.substr(start));
                    return;
                }
                out.push_back(s.substr(start, p - start));
                start = p + 1;
            }
        };
        split(x, fx);
        split(y, fy);
        for (size_t i = 0; i < std::max(fx.size(), fy.size()); ++i) {
            const std::string& vx = i < fx.size() ? fx[i] : std::string();
            const std::string& vy = i < fy.size() ? fy[i] : std::string();
            if (vx == vy) continue;
            std::string field = i < sizeof(kFieldName) / sizeof(kFieldName[0]) ? kFieldName[i]
                                                                             : "field" + std::to_string(i);
            auto clip = [](const std::string& v) {
                return v.size() > 28 ? v.substr(0, 28) + "…" : v;
            };
            return field + " [" + clip(vx) + "] -> [" + clip(vy) + "]";
        }
        return std::string("(fields equal)");
    };
    auto show = [&](const std::string& s) {
        std::string t = identity(s);
        for (char& c : t) {
            if (c == '\x1f') c = '/';
        }
        return t;
    };

    const size_t n = std::max(b.size(), a.size());
    for (size_t i = 0; i < n; ++i) {
        const bool hasB = i < b.size();
        const bool hasA = i < a.size();
        if (!hasB || !hasA) {
            ++rec.mismatches;   // 长度差也计入（少一行 / 多一行）
            if (rec.firstDivergence.empty()) {
                rec.firstDivergence = "idx=" + std::to_string(i) + " len(" +
                                      std::to_string(b.size()) + " vs " +
                                      std::to_string(a.size()) + ")";
            }
            continue;
        }
        if (b[i] == a[i]) continue;
        ++rec.mismatches;
        if (identity(b[i]) != identity(a[i])) ++rec.orderMismatches;
        if (rec.firstDivergence.empty()) {
            rec.firstDivergence = "idx=" + std::to_string(i) + " " + show(b[i]) + " -> " +
                                  show(a[i]) + " @" + firstDiffField(b[i], a[i]);
        }
    }
    return rec;
}

// 直接回放某个文件到末尾，返回读数（**不**动活动会话、不建行、不碰 UI）。
ReplayDigest HarnessApp::DigestReplayFile(
    const std::filesystem::path& file) const {
    ReplayDigest d;
    if (file.empty()) return d;
    ReplaySource source;
    std::string error;
    if (!source.Load(file, error)) return d;
    d.loaded = true;
    d.messages = source.Total();   // 与摘要里 `messages` 同口径：夹具总行数
    Transcript transcript;
    std::string line;
    std::string lineError;
    while (source.Next(line)) {
        transcript.OnLine(line, lineError);
        lineError.clear();
    }
    d.blocks = transcript.Blocks();
    d.deltas = transcript.Deltas();
    return d;
}

// ★★ 2026-10-21：窗口拉伸的布局判据（--resize-probe）。
//
// 这条探针存在的理由（与 `--menu-probe` 同一族）：尺寸变化的失败在**所有既有
//   读数里都不可见**——`blocks`/`deltas`/`seqGaps` 与窗口多大毫无关系，而截图只能
//   证明"某一刻看起来还行"。改前的实测：1400x900 下转录行仍是 1120 时的宽度，
//   800x560 下行比视口还宽并出现横向滚动条。两条症状都在像素里，但判据必须在**读数**里。
//
// 判据设计（每条都有能变红的负对照）：
//   · grow      —— 客户区变大 ⇒ **每一行**都变宽；`m_scroll` 变高；标题栏跟着变宽
//     ★ 用"行的最小/最大宽度"而不是"某一行"：只测一行会被"恰好那一行是通栏工具卡"
//       骗过（工具卡的宽度来自 `innerW`，用户气泡来自文本测量，两条路径不同）。
//   · shrink    —— 客户区变小 ⇒ 行宽都变小，且 **`GetMaxOffsetX() == 0`**
//     ★ 这是"行比视口宽"的**机器可读**证据：横向滚动条出现本身就是那条缺陷。
//   · roundtrip —— 回到原尺寸 ⇒ 行宽**逐位**回到原值（不是"差不多"）。
//     ★ 它同时防"每次 resize 都累积一点误差"这类不收敛的实现。
//   · negative  —— 只把 `root` 改窄、**不**调 `OnWindowResized` ⇒ 行宽必须不变。
//     ★ 这条是单变量纪律：证明"行宽跟着窗口变"是那条重排链做的，而不是
//       `Arrange` 自己顺手完成的（否则上面三条可能恒真）。
void HarnessApp::ProbeResizeClient(int width, int height) {
    if (!m_window) return;
    // 走真正的平台调用：`SetBounds` → `SetWindowPos` → `WM_SIZE` → `Window::OnResized`
    //   → `HarnessApplication::OnWindowResized`。不直接 `root.SetSize`——那会跳过
    //   整个事件链，把"事件真的到达了吗"这个命题绕过（同"合成点击 vs 直接调函数"）。
    ECDI::Rect b = m_window->GetPlatformWindow().GetBounds();
    if (b.width <= 0.0f) return;
    // GetBounds 是**总尺寸**（含边框）；把差值保出来再把客户区尺寸换算回去。
    const ECDI::Size client = m_window->GetPlatformWindow().GetClientSize();
    const float dw = b.width - client.width;
    const float dh = b.height - client.height;
    b.width = static_cast<float>(width) + dw;
    b.height = static_cast<float>(height) + dh;
    m_window->GetPlatformWindow().SetBounds(b);
}

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendAsyncProbeSummary(std::string& json) {
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

