// ============================================================
// 外框探针：菜单条 / 窗口拉伸 / HUD 覆盖率 / 空状态引导 / 模型浮窗
// ------------------------------------------------------------
// 判据形态：几何回读（顶边/槽位/行高）+ 显隐状态机 + 覆盖率反查。
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


// ★★ 2026-10-21：中栏头 `↗`（导出）/ `···`（菜单）的取证。
//
// 要回答的问题：**这两个钮点下去真的有事发生吗，而且是「对」的那件事吗？**
//   改之前它们是空回调（`(void)tip; return;`），而这一点在**任何**既有读数里
//   都看不出来：侧栏探针只碰侧栏、主题探针只碰重建，中栏头从来没有判据。
//
// 判据设计（每条都带一条能让它变红的负对照）：
//   ① menu-closed   模型 false ∧ 控件不可见 ∧ **不占槽**（scrollTop == headBottom）
//      ★ 为什么不能只看 `IsVisible()`：布局只在 Arrange 时跳过隐藏子（C-VIS-1）。
//        只改可见性不重排，`IsVisible()` 是 false 而屏幕上仍留着 34px 空洞——
//        这正是本次实测抓到首帧缺陷的同一形状（见 `BuildMainArea` 的注释）。
//   ② menu-open     点 `···` ⇒ 模型 true ∧ 可见 ∧ scrollTop 下移**正好** headH
//      ★ "正好"而不是"变大了"：重复撑开/撑开两次都会让"变大了"成立。
//   ③ menu-toggle-back  再点一次必须回到 ① 的顶边（不是"窄了"就行）
//   ④ menu-clear    点「清空视图」⇒ 行数不变 ∧ `timeline` 长度不变
//      ★ 负对照在 ⑤：若实现走成"清空模型"，两个读数会一起掉；而"只重画视图"
//        必须两个都不动。两条都记，失败时一眼看出错在哪一侧。
//   ⑤ export-text   导出文本的块行数 == `Transcript::Blocks()`（同源）
//      ★ **关键负对照**：导出里必须出现某条**通知**的原文，而通知**不在块表里**
//        ⇒ 这条同时证明了"导出源是 timeline"且"块表确实没有它"。
//        只断言"导出非空"是恒真的，什么也证明不了。
//   ⑥ export-file   真的落盘 ∧ 与 `BuildTranscriptExportText()` 逐字节相等 ∧ 只有 LF
//      ★ 走的是**同一个** `WriteTranscriptExport()`，绕开的只有"路径从哪儿来"。
//        `GetSaveFileNameW` 那个模态对话框**没有被覆盖**，这是诚实的缺口。
void HarnessApp::RunMenuProbe() {
    if (!m_sess) { m_probeState.menuProbeDone = true; return; }
    // 只在回放模式：live 下点「清空视图」/导出都安全，但 live 的转录在持续增长，
    //   两次快照之间必然不同 ⇒ `timeline` 长度判据会假失败。
    if (!m_sess->replay) { m_probeState.menuProbeDone = true; return; }
    if (!m_hud.moreMenu || !m_hud.moreToggle || !m_hud.exportToggle || !m_content) {
        m_probeState.menuProbeDone = true;
        return;
    }

    // 会话头 = 菜单条的**上一个兄弟**（`counting` 的子节点顺序是 head / menu / scroll）。
    //   ★ 用结构关系取，不用硬编码下标：下标会在有人插入控件时静默指错。
    ECDI::Widget* const head = m_hud.moreMenu->GetParent()
                                   ? m_hud.moreMenu->GetParent()->GetChildAt(0)
                                   : nullptr;

    auto snap = [&](const char* phase) {
        MenuProbeRec rec;
        rec.phase = phase;
        rec.modelOpen = m_moreMenuOpen;
        rec.widgetVisible = m_hud.moreMenu->IsVisible();
        rec.headBottom = head ? head->GetAbsolutePosition().y + head->GetHeight() : 0;
        rec.scrollTop = m_scroll ? static_cast<int>(m_scroll->GetAbsolutePosition().y) : 0;
        rec.gap = rec.scrollTop - rec.headBottom;
        rec.midWidth = m_counting ? m_counting->GetWidth() : 0;
        rec.transcriptRows = m_sess->rows.size();
        rec.timelineLen = m_sess->timeline.size();
        rec.modelBlockLines = m_sess->transcript.Blocks();
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Notice) ++rec.noticeRows;
        }
        m_probeState.menuProbeLog.push_back(std::move(rec));
    };
    auto expect = [&](const char* phase,
                      std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    // 焦点守卫（同 `RunSidebarProbe` 的收尾理由）：合成点击会把焦点交给被点的 Button
    //   ⇒ caret 闪烁重绘多画几帧 ⇒ paints/frames 被这份探针污染。
    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;
    auto clickCentreOf = [&](ECDI::Widget* w) -> bool {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + w->GetWidth() / 2,
                          static_cast<int>(abs.y) + w->GetHeight() / 2);
        return true;
    };

    // ① baseline：**首帧**（已经过若干 tick）菜单必须是关的、且不占槽。
    //   ★ 这条是本次实测缺陷的直检：改动前这里 gap == 34（菜单条可见）。
    expect("menu-closed", {{!m_moreMenuOpen, "model says the menu is open"},
                           {!m_hud.moreMenu->IsVisible(), "menu bar is visible"},
                           {head != nullptr, "conversation head not found"}});
    // ★ gap 的判据要等 snap 之后才能读到，故单独判（失败时把它写进差异里）。
    {
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        const int menuH = m_hud.moreMenu->GetHeight();
        if (rec.gap != 0) {
            rec.matched = false;
            if (!rec.firstDivergence.empty()) rec.firstDivergence += "; ";
            rec.firstDivergence += "closed menu still takes a slot (gap=" +
                                   std::to_string(rec.gap) + ", menuH=" +
                                   std::to_string(menuH) + ")";
        }
    }

    // ② menu-open：点真钮（走 `Application::OnEvent` 派发链，不是直接调 ToggleMoreMenu）
    const int closedGap = m_probeState.menuProbeLog.back().gap;
    const int menuH = m_hud.moreMenu->GetHeight();
    const bool clickedOpen = clickCentreOf(m_hud.moreToggle);
    expect("menu-open", {{clickedOpen, "menu toggle click did not dispatch"},
                         {m_moreMenuOpen, "model did not flip to open"},
                         {m_hud.moreMenu->IsVisible(), "menu bar still hidden"}});
    {
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        if (rec.gap != closedGap + menuH) {
            rec.matched = false;
            if (!rec.firstDivergence.empty()) rec.firstDivergence += "; ";
            rec.firstDivergence += "open menu did not push the transcript down by exactly " +
                                   std::to_string(menuH) + " (gap " +
                                   std::to_string(closedGap) + " -> " +
                                   std::to_string(rec.gap) + ")";
        }
    }

    // ③ menu-toggle-back：再点一次必须回到原顶边（"窄了"不算返回）
    const bool clickedClose = clickCentreOf(m_hud.moreToggle);
    expect("menu-toggle-back", {{clickedClose, "second toggle click did not dispatch"},
                                 {!m_moreMenuOpen, "model did not flip back to closed"},
                                 {!m_hud.moreMenu->IsVisible(), "menu bar still visible"}});
    {
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        if (rec.gap != closedGap) {
            rec.matched = false;
            if (!rec.firstDivergence.empty()) rec.firstDivergence += "; ";
            rec.firstDivergence += "gap did not return to " + std::to_string(closedGap) +
                                   " (got " + std::to_string(rec.gap) + ")";
        }
    }

    // ④ menu-clear：点菜单里的「清空视图」——先开菜单，找到那个钮，再点。
    //   ★ 必须**重新开一次**菜单：③ 已经把它合上了，而菜单项在合上时不可点
    //     （`Hidden` ⇒ 不参与 HitTest）——这正是"菜单关着就点不到"的正确行为。
    {
        const size_t rowsBefore = m_sess->rows.size();
        const size_t timelineBefore = m_sess->timeline.size();
        clickCentreOf(m_hud.moreToggle);          // 开
        ECDI::Button* clear = nullptr;
        for (size_t c = 0; c < m_hud.moreMenu->GetChildCount(); ++c) {
            if (auto* b = dynamic_cast<ECDI::Button*>(m_hud.moreMenu->GetChildAt(c))) {
                if (b->GetText() == "清空视图") { clear = b; break; }
            }
        }
        const bool clicked = clear && clickCentreOf(clear);
        expect("menu-clear", {{clear != nullptr, "no 清空视图 button in the menu bar"},
                              {clicked, "menu item click did not dispatch"},
                              // ★ 菜单项点完必须自己合上（否则"下一步还能点"是错觉）
                              {!m_moreMenuOpen, "menu did not close after an item was used"},
                              {m_sess->rows.size() == rowsBefore,
                               "clear-view changed the number of transcript rows"},
                              {m_sess->timeline.size() == timelineBefore,
                               "clear-view mutated the model timeline (it must only repaint)"}});
    }

    // ⑤ export-click ★★ 这是本探针**唯一**能证明 `↗` 不是空回调的判据。
    //
    //   ★ 为什么不能只测 ⑤/⑥ 那两个函数：`BuildTranscriptExportText()` /
    //     `WriteTranscriptExport()` 都是**直接被探针调用**的。若 `↗` 的回调仍是
    //     那个空壳（`(void)tip; return;`），两个函数照样跑得好好的，判据全绿——
    //     这正是 0c `buildTitle` 的同族陷阱：**函数对了，钮没接上**。
    //   ★ 所以这一臂必须**点真钮**（走 `Application::OnEvent` 派发链）。
    //   ★ 障碍是保存对话框（`GetSaveFileNameW` 是模态循环，点上去会把探针挂死）。
    //     解法是把 `m_probeState.menuProbeExportPath` 预置成一个临时路径 ⇒ `ExportTranscript()`
    //     跳过对话框、直接落盘。**绕过去的只有"路径从哪儿来"**，回调、按钮、
    //     写盘都是真的。未被覆盖的仍是对话框本身（诚实缺口，写在这里）。
    //   ★ 判据：点之前文件不存在 ⇒ 点之后**存在且非空**。
    //     若 `↗` 是空回调，文件不会出现 ⇒ 这一条立刻红。
    {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
        const std::filesystem::path target =
            dir / ("harness-menu-click-" +
                   std::to_string(static_cast<unsigned long long>(
                       std::chrono::steady_clock::now().time_since_epoch().count())) +
                   ".txt");
        std::filesystem::remove(target, ec);
        const bool absentBefore = !std::filesystem::exists(target);

        m_probeState.menuProbeExportPath = target;    // 探针专用：让回调跳过对话框
        const size_t noticesBefore = m_sess->timeline.size();
        const bool clicked = clickCentreOf(m_hud.exportToggle);
        m_probeState.menuProbeExportPath.clear();

        std::error_code ec2;
        const bool exists = std::filesystem::exists(target, ec2);
        const auto size = exists ? std::filesystem::file_size(target, ec2) : 0u;
        // ★ 导出成功必须**在屏上**说一声（否则用户不知道文件去哪了）——
        //   通知行是唯一反馈，故它必须真的多出一行。
        const bool noticeAdded = m_sess->timeline.size() == noticesBefore + 1;

        expect("export-click", {{absentBefore, "setup: the target file already existed"},
                                {clicked, "export button click did not dispatch"},
                                {exists, "clicking the export button wrote no file "
                                         "(the callback is probably still empty)"},
                                {size > 0, "exported file is empty"},
                                {noticeAdded, "export did not report itself on screen"}});
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        rec.exportWrote = exists;
        rec.exportBytes = static_cast<size_t>(size);
        std::filesystem::remove(target, ec2);
        // ★ 收尾把那条「已导出」通知从模型里摘掉：探针不许把自己的副作用
        //   留在会话里（它与 `RunSidebarProbe` 收尾复原焦点是同一条纪律）。
        if (noticeAdded && !m_sess->timeline.empty()) {
            m_sess->timeline.pop_back();
            RebuildTranscriptView();
        }
    }

    // ⑥ export-text：导出**按 timeline 的顺序**逐条重现，且真的含通知原文
    //
    //   ★ 第一版判据是「以 `[` 开头的行数 == `Transcript::Blocks()`」，实测立刻红了
    //     （demo 夹具 9 vs 8）。查下来**错的是判据，不是实现**：`[ask]` 行也以
    //     `[` 开头，而 ask 不是块 ⇒ 这条判据把问句行算成了块。
    //     这是"量出来的东西不是想量的东西"的典型形状，故换掉而不是放宽。
    //
    //   ★ 新判据读的是**顺序**，而不是数数：
    //     · 导出正文里，`timeline` 的每一条都必须**依次**出现——这一条同时钉住了
    //       ① 内容来自 timeline（块的正文原样搬过去）② 顺序是到达序（不是字典序）。
    //       0a 已经量过：字典序在 cot/realistic/scale 上与到达序差 42/218/1998 处，
    //       若导出改走块表，这条判据会在那三份夹具上立刻变红。
    //     · **通知原文必须出现**，而通知**不在块表里** ⇒ 这是 timeline 独有的事实。
    //       负对照：`FindBlock(通知文本) == nullptr`（块 id 是 `bN`，不可能撞上）。
    //   ★ 用「找下一个位置」的单调游标而不是 `find()`：重复文本（同一句话出现两次）
    //     会让 `find()` 永远命中第一处，顺序判据退化成存在判据。
    {
        const std::string text = BuildTranscriptExportText();
        size_t cursor = 0;
        size_t missing = 0;
        std::string missingWhat;
        for (const TimelineEntry& e : m_sess->timeline) {
            std::string needle;
            if (e.kind == TimelineEntry::Kind::Block) {
                const Block* block = m_sess->transcript.FindBlock(e.id);
                if (block && !block->text.empty()) needle = block->text;
            } else if (e.kind == TimelineEntry::Kind::Ask) {
                needle = e.text;
            } else {
                needle = e.text;
            }
            if (needle.empty()) continue;   // 空正文的块无法定位，跳过而不是记账
            const size_t at = text.find(needle, cursor);
            if (at == std::string::npos) {
                ++missing;
                if (missingWhat.empty()) missingWhat = needle.substr(0, 24);
                continue;
            }
            cursor = at + needle.size();
        }
        // 找一条真实的通知原文（模型里 Notice 条目的 text），断言它出现在导出里。
        std::string someNotice;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Notice && !e.text.empty()) {
                someNotice = e.text;
                break;
            }
        }
        // ★ 负对照：块表里**查不到**这条通知 ⇒ "导出含通知"只能来自 timeline。
        const bool noticeAbsentFromBlocks =
            someNotice.empty() || m_sess->transcript.FindBlock(someNotice) == nullptr;
        const bool hasNotice = !someNotice.empty() &&
                               text.find(someNotice) != std::string::npos;

        snap("export-text");
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        rec.exportLines = m_sess->timeline.size() - missing;
        rec.exportHasNotice = hasNotice;
        rec.noticeRows = 0;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Notice) ++rec.noticeRows;
        }
        rec.matched = false;   // 先置红，下面几条都过了才转绿（fail-closed）
        std::string bad;
        if (missing != 0) {
            bad = "export lost or reordered " + std::to_string(missing) +
                  " timeline entries (first: " + missingWhat + ")";
        }
        if (someNotice.empty()) {
            if (!bad.empty()) bad += "; ";
            bad += "this session has no notice row, so the export source cannot be proven";
        } else if (!hasNotice) {
            if (!bad.empty()) bad += "; ";
            bad += "export is missing the notice text (source is probably the block table)";
        }
        if (!noticeAbsentFromBlocks) {
            if (!bad.empty()) bad += "; ";
            bad += "negative control broken: the notice text is also a block id";
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // ⑦ export-file：真的落盘、内容逐字节相等、行尾只有 LF
    {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
        m_probeState.menuProbeTempFile = dir / ("harness-menu-probe-" +
                                     std::to_string(static_cast<unsigned long long>(
                                         std::chrono::steady_clock::now().time_since_epoch().count())) +
                                     ".txt");
        std::string err;
        const bool wrote = WriteTranscriptExport(m_probeState.menuProbeTempFile, &err);
        const std::string expected = BuildTranscriptExportText();
        std::string actual;
        size_t bytes = expected.size();
        bool lfOnly = false;
        if (wrote) {
            std::ifstream in(m_probeState.menuProbeTempFile, std::ios::binary);
            actual.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            bytes = actual.size();
            lfOnly = actual.find('\r') == std::string::npos;
        }
        snap("export-file");
        MenuProbeRec& rec = m_probeState.menuProbeLog.back();
        rec.exportWrote = wrote;
        rec.exportBytes = bytes;
        rec.exportLfOnly = lfOnly;
        std::string bad;
        if (!wrote) {
            bad = "WriteTranscriptExport returned false (" + err + ")";
        } else {
            if (actual != expected) {
                bad = "written file differs from BuildTranscriptExportText()";
            }
            if (!lfOnly) {
                if (!bad.empty()) bad += "; ";
                bad += "written file contains CR (expected LF only)";
            }
            if (bytes == 0) {
                if (!bad.empty()) bad += "; ";
                bad += "written file is empty";
            }
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // 收尾：删掉探针造的临时文件（仓库/夹具目录保持零改动）
    if (!m_probeState.menuProbeTempFile.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_probeState.menuProbeTempFile, ec);
    }
    if (m_window) m_window->SetFocusedWidget(focusBefore);
    // 菜单恢复成关（探针不许把状态留在屏上）
    SetMoreMenuOpen(false);

    m_probeState.menuProbeDone = true;
    if (m_application) m_application->Exit();
}

void HarnessApp::RunResizeProbe() {
    if (!m_window || !m_sess || !m_scroll || !m_content) {
        m_probeState.resizeProbeDone = true;
        return;
    }
    // 只在回放模式：live 的转录在持续增长，两次快照之间行数会变（同 `RunMenuProbe`）。
    if (!m_sess->replay) { m_probeState.resizeProbeDone = true; return; }

    auto snap = [&](const char* phase) {
        ResizeProbeRec rec;
        rec.phase = phase;
        const ECDI::Widget& root = m_window->GetRootWidget();
        rec.rootW = root.GetWidth();
        rec.rootH = root.GetHeight();
        rec.scrollW = m_scroll->GetWidth();
        rec.scrollH = m_scroll->GetHeight();
        rec.midW = m_counting ? m_counting->GetWidth() : 0;
        rec.captionW = m_caption ? m_caption->GetWidth() : 0;
        rec.maxOffsetX = m_scroll->GetMaxOffsetX();
        rec.rows = m_sess->rows.size();
        // 行宽的极值：所有可见行必须一起变宽。空表时留 0（判据会因此变红，如实）。
        rec.rowWMin = 0;
        rec.rowWMax = 0;
        bool first = true;
        for (const RowRec& r : m_sess->rows) {
            if (!r.panel) continue;
            const int w = r.panel->GetWidth();
            if (first) {
                rec.rowWMin = rec.rowWMax = w;
                first = false;
            } else {
                rec.rowWMin = std::min(rec.rowWMin, w);
                rec.rowWMax = std::max(rec.rowWMax, w);
            }
        }
        m_probeState.resizeProbeLog.push_back(std::move(rec));
    };
    auto expect = [&](const char* phase,
                      std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    const ECDI::Size baseSize = m_window->GetPlatformWindow().GetClientSize();
    // ★ `Size` 是 float（`Core/Size.h`）⇒ 全程按 int 用，先量化一次。
    struct ClientBox { int width = 0; int height = 0; };
    const ClientBox base{static_cast<int>(baseSize.width),
                         static_cast<int>(baseSize.height)};

    // ① baseline：前置条件 —— 必须有行可测，否则"一起变宽"是空真。
    expect("baseline", {{m_sess->rows.size() >= 1, "no transcript row to measure"},
                        {base.width > 0, "client size is zero"}});
    const ResizeProbeRec baseRec = m_probeState.resizeProbeLog.back();

    // ② grow：客户区 +280/+140。每一行都必须变宽，且标题栏跟上。
    ProbeResizeClient(base.width + 280, base.height + 140);
    expect("grow", {{m_window->GetRootWidget().GetWidth() == base.width + 280,
                     "root did not take the new width"},
                    {baseRec.rows == 0 || m_sess->rows.size() == baseRec.rows,
                     "resizing changed the number of rows (it must not touch the model)"}});
    {
        ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
        std::string bad = rec.firstDivergence;
        auto add = [&](const std::string& s) {
            if (!bad.empty()) bad += "; ";
            bad += s;
        };
        // ★★ 判据是「行宽**跟随**视口宽」，不是「最小行宽必须变大」。
        //   第一版写的是 `rowWMin` 必须变大，实测立刻红了（148 -> 148），
        //   而**错的是判据**：短行（如 `› read_file`）是**按内容定宽**的，
        //   视口变宽时它本来就不该变——那正是"气泡贴合文字"的正确外观。
        //   ⇒ 改成量**通栏行**（rowWMax）与视口的关系：两者之差是内容宽度里
        //     固定的那部分留白（画布留白 + 滚动条），它必须**不随窗口变化**。
        //     实测该差值在 1120/1400/800 三种宽度下都是 72，是一个不变量。
        const int baseSlack = baseRec.scrollW - baseRec.rowWMax;
        const int nowSlack = rec.scrollW - rec.rowWMax;
        if (nowSlack != baseSlack) {
            add("the full-width row stopped following the viewport (slack " +
                std::to_string(baseSlack) + " -> " + std::to_string(nowSlack) + ")");
        }
        if (rec.rowWMax <= baseRec.rowWMax) {
            add("the full-width row did not get wider (" + std::to_string(baseRec.rowWMax) +
                " -> " + std::to_string(rec.rowWMax) + ")");
        }
        if (rec.captionW != rec.rootW) {
            add("caption width " + std::to_string(rec.captionW) +
                " != root width " + std::to_string(rec.rootW));
        }
        if (rec.scrollH <= baseRec.scrollH) {
            add("transcript viewport did not grow taller (" + std::to_string(baseRec.scrollH) +
                " -> " + std::to_string(rec.scrollH) + ")");
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // ③ shrink：客户区 −240/−140。行必须变窄，且**不许**出现横向滚动条。
    //   ★ 步长必须选在**下限之上**：base 是 1120，减 240 得 880 == 下限。
    //     第一版写的是 −320（= 800），加了最小尺寸之后这个尺寸会被钳回 880，
    //     于是 `root == base-320` 这条判据必然红——**红得对，是探针的布置错了**：
    //     它想测"缩小"，却请求了一个产品不允许的尺寸。要测"低于下限"那是
    //     第 ⑥ 相（min-size）的命题，两个命题不能混在同一相里。
    ProbeResizeClient(base.width - 240, base.height - 140);
    expect("shrink", {{m_window->GetRootWidget().GetWidth() == base.width - 240,
                       "root did not take the smaller width"}});
    {
        ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
        std::string bad = rec.firstDivergence;
        auto add = [&](const std::string& s) {
            if (!bad.empty()) bad += "; ";
            bad += s;
        };
        const int baseSlack = baseRec.scrollW - baseRec.rowWMax;
        const int nowSlack = rec.scrollW - rec.rowWMax;
        if (nowSlack != baseSlack) {
            add("the full-width row did not shrink with the viewport (slack " +
                std::to_string(baseSlack) + " -> " + std::to_string(nowSlack) + ")");
        }
        if (rec.rowWMax >= baseRec.rowWMax) {
            add("the full-width row did not get narrower (" + std::to_string(baseRec.rowWMax) +
                " -> " + std::to_string(rec.rowWMax) + ")");
        }
        // ★ 这条是本次缺陷的机器可读直检：横滚条 = 行比视口宽。
        if (rec.maxOffsetX != 0) {
            add("horizontal scrollbar appeared (maxOffsetX=" +
                std::to_string(rec.maxOffsetX) + ") -- rows are wider than the viewport");
        }
        if (rec.captionW != rec.rootW) {
            add("caption width " + std::to_string(rec.captionW) +
                " != root width " + std::to_string(rec.rootW));
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // ④ roundtrip：回到原尺寸 ⇒ 逐位等于 baseline（不是"差不多"）。
    ProbeResizeClient(base.width, base.height);
    expect("roundtrip", {{m_window->GetRootWidget().GetWidth() == baseRec.rootW,
                          "root width did not return"},
                         {m_scroll->GetWidth() == baseRec.scrollW,
                          "viewport width did not return"},
                         {m_scroll->GetHeight() == baseRec.scrollH,
                          "viewport height did not return"}});
    {
        ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
        std::string bad = rec.firstDivergence;
        auto add = [&](const std::string& s) {
            if (!bad.empty()) bad += "; ";
            bad += s;
        };
        if (rec.rowWMax != baseRec.rowWMax || rec.rowWMin != baseRec.rowWMin) {
            add("row widths did not return bit-for-bit (was " +
                std::to_string(baseRec.rowWMin) + ".." + std::to_string(baseRec.rowWMax) +
                ", now " + std::to_string(rec.rowWMin) + ".." +
                std::to_string(rec.rowWMax) + ")");
        }
        if (rec.rowWMin != baseRec.rowWMin) {
            // ★ 短行也必须逐位回来：它们按内容定宽，**不该**被视口宽度影响。
            //   若这一条红了，说明重排把短行也拉成了通栏（气泡不再贴合文字）。
            add("the content-sized row changed width across a roundtrip (" +
                std::to_string(baseRec.rowWMin) + " -> " + std::to_string(rec.rowWMin) + ")");
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // ⑤ negative（单变量纪律）：只把 root 改窄，**不**走 `OnWindowResized` 那条链。
    //   判据 = 视口窄了，而行宽**没**跟着变（`scrollW - rowWMax` 不再等于 baseline
    //   的那个不变量）。若这一条红了，说明行宽是 `Arrange` 顺手改的，而不是我们
    //   那条重排链做的 ⇒ 上面三条证明不了任何事。
    //   ★ 用 `root.SetSize` 而不是 `ProbeResizeClient`：后者会派发真事件。
    {
        ECDI::Widget& root = m_window->GetRootWidget();
        root.SetSize(baseRec.rootW - 240, baseRec.rootH);
        root.Arrange();                       // 只重排，**不**调 Relayout()
        expect("negative", {{m_sess->rows.size() == baseRec.rows,
                             "setup: rows changed"}});
        ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
        std::string bad = rec.firstDivergence;
        auto add = [&](const std::string& s) {
            if (!bad.empty()) bad += "; ";
            bad += s;
        };
        // 前置：视口必须真的窄了（否则这条负对照没有自变量）。
        if (rec.scrollW >= baseRec.scrollW) {
            add("setup: the viewport did not narrow, so the negative control has no variable");
        }
        const int baseSlack = baseRec.scrollW - baseRec.rowWMax;
        const int nowSlack = rec.scrollW - rec.rowWMax;
        if (nowSlack == baseSlack) {
            add("rows followed the viewport WITHOUT OnWindowResized "
                "(slack stayed " + std::to_string(baseSlack) +
                ") -- the earlier criteria prove nothing");
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        // 复原：把窗口设回原尺寸，让那条链把一切摆回去。
        ProbeResizeClient(base.width, base.height);
        root.Arrange();
        Relayout();
    }

    // ⑥ min-size（2026-10-23 改写：ECDI REQ-13 落地后的**两相**取证）
    //   【改前】这一相证明的是「我们把它推回来了」——同义反复：实现写的就是
    //     `max(880, x)`，再断言结果 >= 880 恒真；它证明的是那三行 C++。
    //   【现在】下限在框架侧、**应用尺寸之前**生效，于是同一个请求可以跑两遍：
    //     · `min-size`     —— 产品配置（建窗处登记的下限）下，640×360 必须被夹到
    //       **逐位等于**下限 ⇒ 同时证明「产品真的登记了下限」与「框架真的在拦」。
    //     · `min-size-off` —— 下限清零后，同一请求必须**真的**落成 640×360
    //       ⇒ 证明上一相的"没落下去"归因于下限，而不是窗口本来就没变小（对照世界）。
    //   ★ 两相都走 `ProbeResizeClient`（真 `SetBounds` → 真 `WM_SIZE` → 真回调），
    //     检验的是"经过整条事件链之后"的根控件尺寸，不是某个函数的返回值。
    //   ★ 顺序必须 ①→②：②会清掉下限，产品读数只能在它之前取。收尾恢复下限与 baseline。
    {
        // ① 产品配置：请求 640×360（远低于 880×460）⇒ 必须被夹到下限
        ProbeResizeClient(640, 360);
        expect("min-size", {{m_window->GetRootWidget().GetWidth() >= kMinClientW,
                             "root is below the floor -- the minimum size did not hold"},
                            {m_window->GetRootWidget().GetHeight() >= kMinClientH,
                             "root height is below the floor"}});
        {
            ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
            std::string bad = rec.firstDivergence;
            auto add = [&](const std::string& s) {
                if (!bad.empty()) bad += "; ";
                bad += s;
            };
            if (rec.rootW < kMinClientW) {
                add("root width " + std::to_string(rec.rootW) + " is below the floor " +
                    std::to_string(kMinClientW) + " -- the minimum size did not take effect");
            }
            if (rec.rootH < kMinClientH) {
                add("root height " + std::to_string(rec.rootH) + " is below the floor " +
                    std::to_string(kMinClientH));
            }
            // ★ 头部完整性：中栏必须 >= 头部需要的 444（这条才是"下限为什么是 874"本身）
            constexpr int kHeadNeed = 2 * 14 + 300 + 3 * 28 + 4 * 8;   // = 444
            if (rec.midW < kHeadNeed) {
                add("middle column " + std::to_string(rec.midW) + " < head requirement " +
                    std::to_string(kHeadNeed) + " -- the last head button would be clipped");
            }
            // ★ 逐位等于下限（不是"大于等于"）：`ptMinTrackSize` 的语义是**精确夹到**下限，
            //   落在 800 / 1000 之类的值说明拦住它的不是下限本身（比如某处另有夹子）。
            if (rec.rootW != kMinClientW) {
                add("root width " + std::to_string(rec.rootW) + " != floor " +
                    std::to_string(kMinClientW) +
                    " (the floor clamps exactly; something else is also resizing)");
            }
            if (rec.rootH != kMinClientH) {
                add("root height " + std::to_string(rec.rootH) + " != floor " +
                    std::to_string(kMinClientH));
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
        }

        // ② 对照世界：下限清零（框架契约：运行期有效，`<= 0` = 该轴不设下限）
        m_window->SetMinimumSize(0, 0);
        ProbeResizeClient(640, 360);
        expect("min-size-off", {{m_window->GetRootWidget().GetWidth() == 640,
                                 "setup: with the floor cleared the request did NOT land at 640 -- "
                                 "the probe cannot produce a below-floor world, so the previous phase proves nothing"},
                                {m_window->GetRootWidget().GetHeight() == 360,
                                 "setup: with the floor cleared the request did NOT land at 360"}});
        {
            ResizeProbeRec& rec = m_probeState.resizeProbeLog.back();
            std::string bad = rec.firstDivergence;
            auto add = [&](const std::string& s) {
                if (!bad.empty()) bad += "; ";
                bad += s;
            };
            // ★ 断言方向与上一相**相反**：两条相反断言同时成立，才排除"下限不存在"
            //   与"下限无处不在"这两种退化情形。
            if (rec.rootW != 640) {
                add("root width " + std::to_string(rec.rootW) + " != 640 with the floor cleared");
            }
            if (rec.rootH != 360) {
                add("root height " + std::to_string(rec.rootH) + " != 360 with the floor cleared");
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
        }

        // 收尾：下限登记回产品配置（②把它清了），再复原到 baseline 尺寸
        m_window->SetMinimumSize(kMinClientW, kMinClientH);
        ProbeResizeClient(base.width, base.height);
        m_window->GetRootWidget().Arrange();
        Relayout();
    }

    m_probeState.resizeProbeDone = true;
    if (m_application) m_application->Exit();
}

void HarnessApp::RunHudProbe() {
    if (!m_sess || !m_window) { m_probeState.hudProbeDone = true; return; }

    auto report = [&](const char* phase, size_t touched,
                      const std::vector<std::string>& unbuilt) {
        HudProbeRec rec;
        rec.phase = phase;
        rec.touched = touched;
        rec.unbuilt = unbuilt.size();
        for (size_t i = 0; i < unbuilt.size(); ++i) {
            if (i) rec.unbuiltNames += ",";
            rec.unbuiltNames += unbuilt[i];
        }
        m_probeState.hudProbeLog.push_back(std::move(rec));
    };

    // ① hud-built：全部建好时，集合必须为空。
    {
        m_probeState.hudTouched.clear();
        m_probeState.hudAudit = true;
        RefreshHud();                       // 触发每一个读取点
        const std::vector<std::string> unbuilt = HudUnbuilt();
        const size_t touched = m_probeState.hudTouched.size();
        m_probeState.hudAudit = false;
        report("hud-built", touched, unbuilt);
        HudProbeRec& rec = m_probeState.hudProbeLog.back();
        std::string bad;
        // 前置：必须真的读过东西（否则"集合为空"是空真——一次都没碰就当通过）
        if (touched < 15) {
            bad = "setup: only " + std::to_string(touched) +
                  " HUD reads were recorded; the audit is probably not wired";
        }
        if (!unbuilt.empty()) {
            if (!bad.empty()) bad += "; ";
            bad += "HUD fields are read by RefreshHud but never built: " + rec.unbuiltNames;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    }

    // ② hud-built-negative：人为打掉一个**确定会读**的指针（`stateChip`），
    //    集合必须非空且正好包含它。★ 用完立刻复原（探针不许把状态留在屏上）。
    {
        ECDI::Label* const saved = m_hud.stateChip;
        m_hud.stateChip = nullptr;          // probe-only corruption
        m_probeState.hudTouched.clear();
        m_probeState.hudAudit = true;
        RefreshHud();
        const std::vector<std::string> unbuilt = HudUnbuilt();
        m_probeState.hudAudit = false;
        m_hud.stateChip = saved;            // 复原（先复原再看结论，避免提前返回漏复原）
        report("hud-built-negative", m_probeState.hudTouched.size(), unbuilt);
        HudProbeRec& rec = m_probeState.hudProbeLog.back();
        std::string bad;
        const bool sawIt = std::find(unbuilt.begin(), unbuilt.end(), "stateChip") !=
                           unbuilt.end();
        if (unbuilt.empty()) {
            bad = "negative control saw nothing -- the audit cannot detect a missing HUD "
                  "pointer, so the `hud-built` result proves nothing";
        } else if (!sawIt) {
            bad = "negative control did not name `stateChip`; it reported: " + rec.unbuiltNames;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        // 复原之后再刷一次，保证屏幕上那个读数不是空的
        RefreshHud();
    }

    m_probeState.hudProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4 Phase C（--empty-probe，2026-10-25）：空状态引导判据。
// 同一开关按会话形态分流（互斥进程，一个进程只会走到一支）：
//   · 回放形态（settle 分支，rows>0）—— 负对照：加载后/Ctrl+N 后引导都不得存在；
//   · firstRun 空壳形态（settle 分支，rows==0）—— 正对照：关设置 ⇒ 引导出现；
//     合成点击「打开设置」⇒ 设置重开；AddNoticeRow ⇒ 引导拆除。
// ★ 分流判据用 firstRun+空行而不用 source：空壳会话的 source 是「Load 失败的
//   ReplaySource」非 nullptr（CreateSessionFromOptions 的 else 分支无条件挂
//   replay 指针）⇒ !source 分支永远轮不到它；它走的就是 replay-settle 路径。
void HarnessApp::RunEmptyProbe() {
    auto snap = [&](const char* phase) -> EmptyProbeRec& {
        EmptyProbeRec rec;
        rec.phase = phase;
        rec.settingsVisible = m_settingsVisible;
        rec.guideExists = (m_hud.emptyGuide != nullptr);
        rec.guideVisible =
            rec.guideExists && m_hud.emptyGuide->IsVisible() && m_scroll &&
            m_scroll->IsVisible();   // 父链语义：scroll 藏着时引导不算可见
        rec.rowCount = m_sess ? m_sess->rows.size() : 0;
        m_probeState.emptyProbeLog.push_back(std::move(rec));
        return m_probeState.emptyProbeLog.back();
    };

    if (m_sess && m_sess->replay && !m_options.firstRun) {
        // ── 回放形态：负对照 ──
        {
            EmptyProbeRec& r = snap("replay-loaded");
            r.matched = !r.guideExists && r.rowCount > 0;
            if (!r.matched) r.note = "empty guide must not exist on loaded replay";
        }
        // Ctrl+N 的空会话带 notice（诚实提示行）⇒ 引导同样不得出现。
        SynthesizeKey(ECDI::KeyCode::N, false, ECDI::KeyModifier::Ctrl);
        {
            EmptyProbeRec& r = snap("after-new-session");
            r.matched = !r.guideExists && r.rowCount > 0;
            if (!r.matched) r.note = "guide must stay hidden on notice-bearing new session";
        }
        m_probeState.emptyProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }

    // ── firstRun 空壳形态：正对照（设置面板在构造期已打开）──
    if (!m_options.firstRun || !m_sess || !m_sess->rows.empty() || !m_sess->replay) {
        // 非空壳会话（理论到不了——防御：如实退出，不造读数）。
        m_probeState.emptyProbeDone = true;
        return;
    }
    {
        EmptyProbeRec& r = snap("firstrun-open");
        r.matched = r.settingsVisible && r.guideExists;
        if (!r.matched) r.note = "firstRun should open settings and build the guide";
    }
    // Ctrl+, 关设置（真实派发链）⇒ scroll 恢复可见 ∧ 引导可见。
    SynthesizeKey(ECDI::KeyCode::Comma, false, ECDI::KeyModifier::Ctrl);
    {
        EmptyProbeRec& r = snap("closed-settings");
        r.matched = !r.settingsVisible && r.guideExists && r.guideVisible;
        if (!r.matched) r.note = "closing settings should reveal the guide";
    }
    // 合成点击引导上的「打开设置」⇒ 设置重开（接线判据）。
    // ★ 按钮位置不猜（VerticalLayout 摆位）：从引导卡子控件里现找 Button，
    //   用它的 GetAbsolutePosition 取中心（同 4274 的「不手算父链」纪律）。
    if (m_hud.emptyGuide) {
        for (size_t i = 0; i < m_hud.emptyGuide->GetChildCount(); ++i) {
            ECDI::Widget* child = m_hud.emptyGuide->GetChildAt(i);
            if (!dynamic_cast<ECDI::Button*>(child)) continue;
            const ECDI::Point abs = child->GetAbsolutePosition();
            const int cx = static_cast<int>(abs.x) + child->GetWidth() / 2;
            const int cy = static_cast<int>(abs.y) + child->GetHeight() / 2;
            // ★ 命中者回读（取证：点击前屏幕上这一点真正属于谁——把「点不到」
            //   从猜测变成读数）。
            const ECDI::Widget* hit =
                m_window ? m_window->GetRootWidget().HitTest(cx, cy) : nullptr;
            SynthesizeClickAt(cx, cy);
            // ★ 命中者回读（保留：点不到时把「谁接了这个点」变成读数，不靠猜）。
            m_probeState.emptyProbeClickTarget = WidgetKindName(hit);
            break;
        }
    }
    {
        EmptyProbeRec& r = snap("click-reopens");
        r.matched = r.settingsVisible;
        if (!r.matched) {
            r.note = "guide button should reopen settings; hit=" +
                     (m_probeState.emptyProbeClickTarget.empty() ? "(none)" : m_probeState.emptyProbeClickTarget);
        }
    }
    SynthesizeKey(ECDI::KeyCode::Comma, false, ECDI::KeyModifier::Ctrl);
    {
        EmptyProbeRec& r = snap("closed-again");
        r.matched = !r.settingsVisible && r.guideExists;
        if (!r.matched) r.note = "settings should close again";
    }
    // notice 到达 ⇒ 引导拆除（AddNoticeRow 是 notice 渲染唯一入口；合成协议
    // notice 需要真 core，firstRun 场景没有——妥协直接调它，渲染路径仍是真的）。
    AddNoticeRow("empty-probe: notice", kDarkMuted);
    {
        EmptyProbeRec& r = snap("dismiss-on-notice");
        r.matched = !r.guideExists && r.rowCount == 1;
        if (!r.matched) r.note = "notice must tear the guide down";
    }
    m_probeState.emptyProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ P4 Phase H（--model-probe，2026-10-25）：模型浮窗 + 用量判据。
// ★ 只在回放模式：live 会点击真的发 model 切换（探针不制造真副作用）。
// ★ 分母判据用 cot 夹具跑（hello.context=128000 ⇒ v4 ctxCapacity 上屏；
//   demo 夹具无 context ⇒ ctxPct 恒 "—"，判据不成立）。
void HarnessApp::RunModelProbe() {
    if (!m_sess || !m_sess->replay) { m_probeState.modelProbeDone = true; return; }
    auto countRows = [&]() {
        size_t n = 0;
        if (m_hud.modelPopupList) {
            for (size_t i = 0; i < m_hud.modelPopupList->GetChildCount(); ++i) {
                if (dynamic_cast<ECDI::Button*>(m_hud.modelPopupList->GetChildAt(i))) {
                    ++n;
                }
            }
        }
        return n;
    };
    auto snap = [&](const char* phase) -> ModelProbeRec& {
        ModelProbeRec rec;
        rec.phase = phase;
        rec.popupVisible = m_hud.modelPopup && m_hud.modelPopup->IsVisible();
        rec.listRows = countRows();
        rec.ctxKnown = m_hud.ctxPct && m_hud.ctxPct->GetText() != "—" &&
                       m_hud.ctxPct->GetText() != "%";
        rec.chipText = m_hud.modelChip ? m_hud.modelChip->GetText() : "";
        m_probeState.modelProbeLog.push_back(std::move(rec));
        return m_probeState.modelProbeLog.back();
    };
    const size_t noticeBefore = m_sess->timeline.size();

    // ① 分母上屏（P3.6 遗留接线的修复判据：v4 ctxCapacity 进百分比）。
    {
        ModelProbeRec& r = snap("ctx-denominator");
        r.matched = r.ctxKnown;
        if (!r.matched) r.note = "ctxPct still em-dash (denominator not on screen)";
    }
    // ② chip 接线。
    {
        ModelProbeRec& r = snap("chip-text");
        r.matched = r.chipText.find("\xE2\x96\xBE") != std::string::npos;   // ▾
        if (!r.matched) r.note = "chip text lacks dropdown marker";
    }
    // ③ Ctrl+M 开 → Esc 关（优先级：设置 > 浮窗 > 菜单——浮窗层独立可关）。
    SynthesizeKey(ECDI::KeyCode::M, false, ECDI::KeyModifier::Ctrl);
    {
        ModelProbeRec& r = snap("ctrl-m-open");
        r.matched = r.popupVisible;
        if (!r.matched) r.note = "Ctrl+M did not open the popup";
    }
    const size_t allRows = countRows();
    // ④ 搜索过滤（相对判据：不依赖跑批机配置的具体家数）。
    //   ★ 探针只能 SetText + 手动重建——TextBox 的 D7 契约「SetText 不走
    //   OnTextChanged」，真实键入才触发回调（grow-probe 同坑已记）；过滤逻辑
    //   本身（RebuildModelPopupList 读搜索词）仍是被测的真实路径。
    if (m_hud.modelSearch) {
        m_hud.modelSearch->SetText("__no_such_provider__");
        RebuildModelPopupList();
        ModelProbeRec& r = snap("search-filter");
        r.matched = (r.listRows == 0);
        if (!r.matched) r.note = "filter matched everything (filter not wired)";
        m_hud.modelSearch->SetText("");
        RebuildModelPopupList();
        ModelProbeRec& r2 = snap("search-clear");
        r2.matched = (r2.listRows == allRows);
        if (!r2.matched) r2.note = "clearing filter did not restore rows";
    }
    // ⑤ 合成点击第一行 ⇒ 浮窗关 ∧ 回放会话被如实拒绝（writer==null ⇒ notice）。
    if (allRows > 0 && m_hud.modelPopupList) {
        for (size_t i = 0; i < m_hud.modelPopupList->GetChildCount(); ++i) {
            ECDI::Button* row =
                dynamic_cast<ECDI::Button*>(m_hud.modelPopupList->GetChildAt(i));
            if (!row) continue;
            const ECDI::Point abs = row->GetAbsolutePosition();
            SynthesizeClickAt(static_cast<int>(abs.x) + row->GetWidth() / 2,
                              static_cast<int>(abs.y) + row->GetHeight() / 2);
            break;
        }
        ModelProbeRec& r = snap("click-row");
        bool noticed = false;
        for (size_t i = noticeBefore; i < m_sess->timeline.size(); ++i) {
            const TimelineEntry& e = m_sess->timeline[i];
            if (e.kind == TimelineEntry::Kind::Notice &&
                e.text.find("\xE4\xB8\x8D\xE6\x98\xAF live") != std::string::npos) {
                noticed = true;   // 「不是 live」
            }
        }
        r.matched = !r.popupVisible && noticed;
        if (!r.matched) r.note = "popup should close and replay session be rejected";
    } else {
        ModelProbeRec& r = snap("click-row-skipped");
        r.matched = true;
        r.note = "no providers configured on this machine";
    }
    SetModelPopupOpen(false);
    m_probeState.modelProbeDone = true;
    if (m_application) m_application->Exit();
}

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendMenuProbeSummary(std::string& json) {
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

void HarnessApp::AppendEmptyProbeSummary(std::string& json) {
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

void HarnessApp::AppendModelProbeSummary(std::string& json) {
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

void HarnessApp::AppendHudProbeSummary(std::string& json) {
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

void HarnessApp::AppendResizeProbeSummary(std::string& json) {
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

