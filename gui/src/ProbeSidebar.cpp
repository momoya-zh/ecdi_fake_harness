// ============================================================
// 侧栏 / 会话 / 历史 / 主题探针
// ------------------------------------------------------------
// 判据形态：列表重建比对（含逆序负对照）、磁盘副作用回读、整树重建后主题令牌复查。
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
using harness::internal::kSidebarRowH;
using harness::internal::kSidebarGroupH;


// ═══════════════════════════════════════════════════════════════════════════
// ★★ Step 1：侧栏列表的 A==B 探针（--sidebar-probe）
//
// 为什么侧栏需要**自己**一套判据：0c 的 `SnapshotRows` 只看 `m_sess->rows`
//   —— 那是转录区。侧栏错了它一个字都读不到。而侧栏恰恰是"视图与模型的对应
//   关系"最容易分叉的地方：列表说 2 个会话、点下去切到第 1 个，转录区完全正常。
//
// 六条读数，每条都配了**能被推翻的形态**：
//   ① baseline      起点快照（模型会话数 / 列表行数 / 选中项）
//   ② rebuild       原地重建一次 ⇒ 必须与 baseline 逐行相同（matched=true）
//   ③ rebuild-again 再重建一次 ⇒ 幂等（防"重建时顺手改了状态"）
//   ④ negative      **负对照**：喂逆序下标重建 ⇒ 必须报**不匹配**。
//                   这一条是整个探针的"灵敏度证明"——若它也 matched，
//                   说明比对函数根本没在看次序，②③ 的 matched 就一文不值。
//                   （同 0a 的字典序教训：没有负对照的正对照是自证）
//   ⑤ new           新建一个会话并重建 ⇒ 列表行数 +1、会话数 +1
//   ⑥ deleted       删掉那个新会话 ⇒ 列表行数回到 baseline、
//                   **活动会话的转录行数一位都不变**（负对照：删别人不能动我）
// ═══════════════════════════════════════════════════════════════════════════
void HarnessApp::RunSidebarProbe() {
    if (!m_sess) { m_probeState.sidebarProbeDone = true; return; }
    // ★ 只在回放模式：live 下 NewSession 会真拉 core 子进程，探针不该制造真进程。
    if (!m_sess->replay) { m_probeState.sidebarProbeDone = true; return; }
    if (!m_hud.sessList) { m_probeState.sidebarProbeDone = true; return; }

    auto snap = [&](const char* phase) {
        SidebarProbeRec rec;
        rec.phase = phase;
        rec.sessionCount = m_sessions.size();
        rec.activeIndex = m_activeIndex;
        rec.transcriptRows = m_sess->rows.size();
        // 控件侧：列表内容视图里的直接子节点数（= 画出来的行数）
        const ECDI::Widget& content = m_hud.sessList->GetContentView();
        rec.rowCount = content.GetChildCount();
        // 选中项：行内第一个 Label 的文字色 == kAccentInk 即视为选中
        //   （ECDI 的 Button/Panel 都没有 style getter，文字色是**唯一**可读回的
        //     选中标记 —— 见 `BuildSessionRow` 里那段注释，别把它当纯装饰改掉）
        rec.selectedRows = 0;
        rec.selectedIndex = static_cast<size_t>(-1);
        for (size_t i = 0; i < content.GetChildCount(); ++i) {
            const ECDI::Widget* row = content.GetChildAt(i);
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl == nullptr) continue;
                if (lbl->GetTextColor() == kAccentInk) {
                    ++rec.selectedRows;
                    rec.selectedIndex = i;
                }
                break;   // 只看行内第一个 Label（名称），圆点是 Panel、`×` 是 Button
            }
        }
        // ★ Step 2：布局层读数。只看 `IsVisible()` 会被"隐藏了但没重排、
        //   中栏没吃到那 200px"骗过（0c 的"重建了但没重排"同族缺陷）。
        rec.sidebarVisible = m_hud.sidebar ? m_hud.sidebar->IsVisible() : false;
        rec.midWidth = m_counting ? m_counting->GetWidth() : 0;
        m_probeState.sidebarProbeLog.push_back(std::move(rec));
    };

    // ★ 焦点守卫：合成点击会把焦点交给被点的 Button（`Application::OnMouseButtonDown`
    //   第一件事就是 `SetFocusedWidget`），焦点框与 caret 闪烁都会多画几帧 ⇒
    //   paints/frames 读数被这份探针污染。收尾必须还回去（同 `RunHitProbe`）。
    ECDI::Widget* const focusBefore = m_window ? m_window->GetFocusedWidget() : nullptr;

    // ★★ 点击臂：**走真实派发链**点列表行/`×`，而不是直接调 SwitchToSession/DeleteSessionAt。
    //   理由（这是 Step 1 之前完全空缺的那一类判据）：前面几相只证明"重建画得对"，
    //   却没有任何一条读数证明**点上去真的会切、会删**——画对了但回调没接上，
    //   在读数上与前几相完全同形。0c 在转录区正是栽在这里（buildTitle 重建完
    //   标题空了，而当时没有任何判据看得见）。
    auto rowAt = [&](size_t i) -> ECDI::Widget* {
        ECDI::Widget& content = m_hud.sessList->GetContentView();
        return i < content.GetChildCount() ? content.GetChildAt(i) : nullptr;
    };
    auto clickCentreOf = [&](ECDI::Widget* w) {
        if (!w) return false;
        const ECDI::Point abs = w->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + w->GetWidth() / 2,
                          static_cast<int>(abs.y) + w->GetHeight() / 2);
        // 派发返回 ⇒ 已离开事件栈 ⇒ 此时拆树安全（回调里只置了 `m_sidebarDirty`）
        if (m_sidebarDirty) { m_sidebarDirty = false; RebuildSessionList(); }
        return true;
    };
    auto deleteButtonOf = [&](size_t i) -> ECDI::Button* {
        ECDI::Widget* row = rowAt(i);
        if (!row) return nullptr;
        // ★★ 2026-10-20：非归档行现在有两个按钮（「↓」归档 + 「×」删除）。
        //   必须按文字找「×」，不能取第一个 Button（那会拿到「↓」）。
        for (size_t c = 0; c < row->GetChildCount(); ++c) {
            if (auto* b = dynamic_cast<ECDI::Button*>(row->GetChildAt(c))) {
                if (b->GetText() == "×") return b;
            }
        }
        return nullptr;
    };
    // 把"这一相该看到什么"写成显式期望，失败时给出**哪一条没满足**（别只报 false）
    auto expect = [&](const char* phase,
                      std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        SidebarProbeRec& rec = m_probeState.sidebarProbeLog.back();
        std::string bad;
        for (const auto& c : conds) {
            if (c.first) continue;
            if (!bad.empty()) bad += "; ";
            bad += c.second;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    // ① 起点
    RebuildSessionList();
    m_sidebarDirty = false;
    snap("baseline");
    const std::vector<std::string> baseList = SnapshotSessionList();
    const size_t baseRows = m_sess->rows.size();

    // ② 原地重建 ⇒ A==B
    RebuildSessionList();
    {
        const SidebarCompare cmp = CompareSessionList(baseList, SnapshotSessionList());
        snap("rebuild");
        m_probeState.sidebarProbeLog.back().matched = cmp.matched;
        m_probeState.sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
    }

    // ③ 再重建一次 ⇒ 幂等（与 ② 的快照比，而不是与 ① 比：
    //   若 ② 已经把状态改坏了，③ 与 ① 比会同时报两处，定位反而更难）
    {
        const std::vector<std::string> afterRebuild = SnapshotSessionList();
        RebuildSessionList();
        const SidebarCompare cmp = CompareSessionList(afterRebuild, SnapshotSessionList());
        snap("rebuild-again");
        m_probeState.sidebarProbeLog.back().matched = cmp.matched;
        m_probeState.sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
    }

    // ④ 新建会话 ⇒ 列表行数 +1、会话数 +1。
    //   ★ 顺序说明：负对照（⑤）需要**两个以上**会话才有自由度，所以新建排在它前面。
    //     demo 夹具默认只有 1 个会话，若把负对照放在新建之前，它就永远是
    //     "逆序 == 正序"的退化情形——那条读数会写着 matched=true，读者会误以为
    //     负对照通过了，而实际上它根本没被检验过。
    NewSession();
    RebuildSessionList();   // 探针不在事件派发栈上，直接建是安全的
    snap("new");
    const size_t emptyRows = m_sess->rows.size();   // 空会话的转录行数（= 1，那条 notice）

    // ⑥ ★★ 点击臂：Step 1 之前这一类判据**完全空缺**——前面几相只证明"重建画得对"，
    //   没有任何一条读数证明"点上去真的会切"。而"画对了但回调没接上"在读数上
    //   与前几相完全同形（0c 在转录区正是这样骗过了当时的判据）。
    //   判据三件必须同时成立：活动下标变了 ∧ 选中行跟着变 ∧ **转录行数跟着换**。
    //   只查下标会被"切了但转录没重建"骗过；只查转录会被"两会话行数恰好相同"骗过。
    auto selectedIndex = [&]() -> long long {
        ECDI::Widget& content = m_hud.sessList->GetContentView();
        for (size_t i = 0; i < content.GetChildCount(); ++i) {
            const ECDI::Widget* row = content.GetChildAt(i);
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl == nullptr) continue;
                if (lbl->GetTextColor() == kAccentInk) return static_cast<long long>(i);
                break;
            }
        }
        return -1;
    };

    // ⑤ 点第 2 行（空会话）→ 点第 1 行（原会话）：两次都必须真的换过去
    const bool clickedAway = clickCentreOf(rowAt(1));
    expect("click-empty", {{clickedAway, "click did not dispatch"},
                           {m_activeIndex == 1, "activeIndex!=1"},
                           {selectedIndex() == 1, "selected row != 1"},
                           {m_sess->rows.size() == emptyRows, "transcript did not follow"}});
    const bool clickedBack = clickCentreOf(rowAt(0));
    expect("click-back", {{clickedBack, "click did not dispatch"},
                          {m_activeIndex == 0, "activeIndex!=0"},
                          {selectedIndex() == 0, "selected row != 0"},
                          {m_sess->rows.size() == baseRows, "transcript did not follow back"}});
    // ★ 负对照（点击臂专用）：点**行内 `×` 之外**的空白处必须切行，而点 `×` 必须**不**切行。
    //   后者的读法见 ⑦（删除臂）——若 `×` 的点击漏到父行，`SwitchToSessionByPtr` 会因为
    //   目标已被 erase 而静默 no-op（地址比对兜住了），故此处不重复断言，靠 ⑦ 的形状判。

    // ⑥ ★ 负对照：逆序重建。判据是 **matched == false**（探针必须能看见次序错）。
    //   ★ 只改**次序**这一个自变量：选中态仍按真实活动会话判定（`orderOverride`
    //     不再强制 false），否则差异里混进了"选中态被造没了"，就分不清探针抓到的
    //     是次序错还是选中错——单变量纪律（同 REQ-01/03/04 的教训）。
    if (m_sessions.size() >= 2) {
        // ★ 快照必须在**紧邻**逆序重建之前抓：点击臂刚把活动会话从 1 号切回 0 号，
        //   若沿用 `new` 相那份快照，选中态会天然差一格，比对报出的差异就与"次序"
        //   无关了（第一版正是这么写错的，读数里出现
        //   `restore-after-negative failed: row 0 selected [0] -> [1]` —— 错的是快照时机，
        //   不是实现）。
        const std::vector<std::string> twoList = SnapshotSessionList();
        std::vector<size_t> rev;
        for (size_t i = m_sessions.size(); i > 0; --i) rev.push_back(i - 1);
        RebuildSessionList(&rev);
        const SidebarCompare cmp = CompareSessionList(twoList, SnapshotSessionList());
        snap("negative");
        m_probeState.sidebarProbeLog.back().matched = cmp.matched;
        m_probeState.sidebarProbeLog.back().firstDivergence = cmp.firstDivergence;
        // 复原：负对照只借用一次列表，别把逆序留在屏上（⑦ 的读数会受它影响）
        RebuildSessionList();
        const SidebarCompare restored = CompareSessionList(twoList, SnapshotSessionList());
        if (!restored.matched) {
            // 复原失败 = 重建不是纯函数（第一次的差异被留在了状态里）⇒ 必须显式暴露
            m_probeState.sidebarProbeLog.back().firstDivergence =
                "restore-after-negative failed: " + restored.firstDivergence;
            m_probeState.sidebarProbeLog.back().matched = true;   // 仍记负对照通过，但差异里写明复原失败
        }
    } else {
        // 理论上到不了这里（④ 之后至少 2 个会话）；留一条诚实读数而不是静默跳过。
        snap("negative");
        m_probeState.sidebarProbeLog.back().matched = true;
        m_probeState.sidebarProbeLog.back().firstDivergence =
            "(no degrees of freedom: fewer than 2 sessions)";
    }

    // ⑦ ★★ 删除臂 A：点**非活动**会话的 `×`。
    //   用户口径（ECDI 实施方案第 ④ 条）：「删非活动时保留当前转录与活动会话不变，
    //   只调整列表和活动下标」。⇒ 判据是**四条同时成立**，其中 transcriptRows
    //   不变是真正的负对照：删错了会话（把活动那个删了）会让转录行数变。
    //   且这一臂同时验证了「嵌套 `×` 的点击不会漏到父行」：若漏了，父行会先
    //   `SwitchToSession` 切到被删的会话，随后 erase 让它变成空转录 ⇒ transcriptRows 会掉。
    {
        // 当前状态：0=原会话（活动，有转录）1=新建空会话。删 1 号（非活动）。
        ECDI::Button* delBtn = deleteButtonOf(1);
        const bool clicked = delBtn && clickCentreOf(delBtn);
        expect("delete-inactive", {{clicked, "no delete button on row 1"},
                                   {m_sessions.size() == 1, "sessionCount != 1"},
                                   {m_hud.sessList->GetContentView().GetChildCount() == 1,
                                    "rowCount != 1"},
                                   {m_activeIndex == 0, "activeIndex != 0"},
                                   {m_sess->rows.size() == baseRows,
                                    "active transcript changed by deleting inactive"}});
    }

    // ⑧ ★★ 删除臂 B：点**活动**会话的 `×` ⇒ 必须先切邻居再删，且列表/活动下标都收敛。
    //   这条是顺序错误的唯一直检：若实现先 erase 再 SwitchToSession，
    //   切换时读的是已析构的 Session（UAF，`rows` 里全是裸指针）。
    NewSession();
    RebuildSessionList();
    {
        const bool activeIsNew = (m_activeIndex == 1 && m_sessions.size() == 2);
        ECDI::Button* delBtn = deleteButtonOf(1);
        const bool clicked = delBtn && clickCentreOf(delBtn);
        expect("delete-active", {{activeIsNew, "setup: new session not active"},
                                 {clicked, "no delete button on row 1"},
                                 {m_sessions.size() == 1, "sessionCount != 1"},
                                 {m_hud.sessList->GetContentView().GetChildCount() == 1,
                                  "rowCount != 1"},
                                 {m_activeIndex == 0, "did not fall back to neighbour"},
                                 {m_sess->rows.size() == baseRows,
                                  "transcript did not come back to survivor"}});
    }

    // ⑧b ★★ 2026-10-27：删除臂 C——点**活动的临时会话**的 `×`。
    //   这一臂之前**完全空缺**（全仓搜 `temporary` 在探针里零命中），而它正是用户
    //   实测到的崩溃形态：`DeleteSessionAt(i)` → `SwitchToSession(邻居)` →
    //   `MaybeDeleteTemporarySession` 把 **target 自己**摘掉（临时会话切走即消失）
    //   ⇒ 传进来的 `i` 已失效，按下标再 erase 一次 = **双删**：删错会话 / erase(end())
    //   ⇒ `size() - 1` 下溢成 SIZE_MAX ⇒ `m_sessions[SIZE_MAX]` 解引用，实测退出码
    //   0xC0000005 与 0xC0000374。崩在探针里 ⇒ 这一次运行根本没有 JSON 输出，门直接红。
    //   ★ 造临时会话用「直接置标志」而不是走工作区分组的「＋」：`temporary` 是
    //     `NewTemporarySession` 设的唯一标志（AppWorkspace.cpp:230），而 demo 夹具
    //     没有 workspace 能力，走 ＋ 需要先拉起 live core——那是人工验收，不是回放门。
    //     被删的是哪个布尔值不影响被测的那条**派发链**（× → DeleteSessionAt）。
    NewSession();
    RebuildSessionList();
    {
        const bool activeIsTemp = (m_activeIndex == 1 && m_sessions.size() == 2);
        m_sess->temporary = true;
        ECDI::Button* delBtn = deleteButtonOf(1);
        const bool clicked = delBtn && clickCentreOf(delBtn);
        expect("delete-active-temporary",
               {{activeIsTemp, "setup: new session not active"},
                {clicked, "no delete button on row 1"},
                {m_sessions.size() == 1, "sessionCount != 1 (double erase?)"},
                {m_hud.sessList->GetContentView().GetChildCount() == 1, "rowCount != 1"},
                {m_activeIndex == 0, "did not fall back to neighbour"},
                {m_sess == m_sessions[m_activeIndex].get(), "m_sess and m_activeIndex disagree"},
                {!m_sess->temporary, "survivor became the temporary one"},
                {m_sess->rows.size() == baseRows, "transcript did not come back to survivor"}});
    }

    // ⑧c ★★ 2026-10-27：在**活动的临时会话**上点「＋ 新建会话」。
    //   这是 ⑧b 的**同族另一半**：`NewSession()` 先算 `idx = size()-1`，再
    //   `SwitchToSession(idx)`，**之后**用 `m_sessions[idx]` 补发 hello——而
    //   `MaybeDeleteTemporarySession` 恰好在中间把表缩短了一格 ⇒ `idx` 越界。
    //   回放模式下那条 hello 分支走不到，但 `SwitchToSession` 自己那句
    //   `m_sess = m_sessions[index].get()` 同样吃旧下标 ⇒ 这一臂在回放里就有区分力
    //   （旧实现：`m_activeIndex` 落在表外，`m_sessions[2]`（size 2）直接 0xC0000005）。
    NewSession();
    RebuildSessionList();
    {
        m_sess->temporary = true;              // 活动会话 = 临时且没发过 prompt
        ECDI::Button* newBtn = nullptr;
        if (m_hud.sidebar) {
            for (size_t c = 0; c < m_hud.sidebar->GetChildCount(); ++c) {
                auto* b = dynamic_cast<ECDI::Button*>(m_hud.sidebar->GetChildAt(c));
                if (b && b->GetText() == "＋ 新建会话") { newBtn = b; break; }
            }
        }
        const bool clicked = newBtn && clickCentreOf(newBtn);
        expect("new-while-temporary-active",
               {{clicked, "no 「＋ 新建会话」 button on sidebar"},
                {m_sessions.size() == 2, "sessionCount != 2 (temp 未消失或多删)"},
                {m_activeIndex == 1, "activeIndex 没跟着前移（旧下标残留）"},
                {m_sess == m_sessions[m_activeIndex].get(), "m_sess and m_activeIndex disagree"},
                {!m_sess->temporary, "新建的会话继承了 temporary"},
                {m_sess->rows.size() == emptyRows, "transcript not the fresh empty session"},
                {m_hud.sessList->GetContentView().GetChildCount() == 2, "rowCount != 2"}});
        // 收尾：把这一臂新建的会话删掉，让 ⑨ 仍在「只剩 1 个会话」的前条件下跑。
        DeleteSessionAt(m_sessions.size() - 1);
        RebuildSessionList();
    }

    // ⑨ ★ 最后一条：只有一个会话时**不允许删**（`DeleteSessionAt` 返回 false）。
    //   「删到零会话」会让 `m_sess == nullptr`，而全仓 200+ 处调用点假定非空。
    //   判据 = 点了也不掉行（不是"按钮不存在"——这里造的是"按钮在但删不动"，
    //   因为 canDelete 是按 size>1 判的，1 个会话时 `×` 根本不会被建出来。
    //   ⇒ 诚实的读法是：1 个会话时行内没有按钮，故 buttons == 0 且点不到。
    {
        ECDI::Widget* row = rowAt(0);
        const bool noButton = (row != nullptr && row->GetChildCount() == 2);
        expect("single-session", {{noButton, "row still offers a delete button"},
                                  {m_sessions.size() == 1, "sessionCount != 1"},
                                  {m_sess != nullptr, "active session became null"}});
    }

    // ⑩ ★★ Step 2：侧栏收起/展开臂。
    //   ★ 这一臂必须**点顶栏 `≡`** 而不是直接调 `SetSidebarVisible`：Step 2 之前
    //     `≡` 是个 `Label`（点不到），改成 Button 之后"回调接上了没有"才第一次
    //     成为可检验的命题——直接调函数会把这条判据整个绕过去。
    //   ★ 判据四条同时成立，缺一条就会被某种错误实现骗过：
    //     · sidebarVisible 翻转 —— 显隐真的改了；
    //     · **中栏宽度 +200/-200** —— 布局真的重排了（只改可见性不 Arrange 会在这里露馅，
    //       而 IsVisible() 仍然是对的 ⇒ 光看可见性不够）；
    //     · 侧栏宽 200 —— 常量没被折叠过程改掉（它不是 CollapsiblePanel，不该有中间尺寸）；
    //     · 列表行数不变 —— 收起/展开**不能**动会话模型（这条是负对照：
    //       若实现走成"收起 = 清空列表"，行数会掉到 0，而其余三条依然可能成立）。
    if (m_hud.sidebar && m_hud.sidebarToggle && m_counting) {
        const int expandedMid = m_counting->GetWidth();
        const int sideWidth = m_hud.sidebar->GetWidth();
        const size_t rowsBefore = m_hud.sessList->GetContentView().GetChildCount();

        // 收起：点顶栏 `≡`
        const bool clickedCollapse = clickCentreOf(m_hud.sidebarToggle);
        expect("collapse", {{clickedCollapse, "toggle click did not dispatch"},
                            {!m_sidebarVisible, "model still says visible"},
                            {!m_hud.sidebar->IsVisible(), "sidebar still visible"},
                            {m_counting->GetWidth() == expandedMid + sideWidth,
                             "mid column did not reclaim the sidebar width"},
                            {m_hud.sidebar->GetWidth() == sideWidth, "sidebar width changed"},
                            {m_hud.sessList->GetContentView().GetChildCount() == rowsBefore,
                             "collapse disturbed the session list"}});

        // 展开：再点一次，必须**回到原宽度**（不是"变宽了"就行——那可能是重复加宽）
        const bool clickedExpand = clickCentreOf(m_hud.sidebarToggle);
        expect("expand", {{clickedExpand, "toggle click did not dispatch"},
                          {m_sidebarVisible, "model still says hidden"},
                          {m_hud.sidebar->IsVisible(), "sidebar still hidden"},
                          {m_counting->GetWidth() == expandedMid,
                           "mid column did not return to its original width"},
                          {m_hud.sessList->GetContentView().GetChildCount() == rowsBefore,
                           "expand disturbed the session list"}});
    }

    // ⑪ ★★ 2026-10-20：检查器藏/显臂。
    //   这块在 GPT 完成前是**零判据**的：`≡` 的回调只改一个布尔 + `SetVisible`，
    //   一旦父容器 `Arrange()` 没跑，屏幕不对而所有既有探针全绿。
    //   形态照抄 ⑩：以**中栏宽度**为读据（检查器 230px，藏起来中栏必须宽出正好 230）。
    //   ★ 同样必须点真钮：直接调 `SetVisible` 会把“回调接上了没有”绕过去。
    //   ★ 第三条判据是负对照：藏检查器**不能**动转录行数——若实现走成“重搭整棵树”，
    //     中栏变宽仍然成立，但行数会变。
    if (m_hud.inspector && m_hud.inspectorToggle && m_counting) {
        const int midBefore = m_counting->GetWidth();
        const int inspWidth = m_hud.inspector->GetWidth();
        const size_t txBefore = m_sess->rows.size();

        const bool clickedHide = clickCentreOf(m_hud.inspectorToggle);
        expect("inspector-hide", {{clickedHide, "inspector toggle did not dispatch"},
                                  {!m_inspectorVisible, "model still says visible"},
                                  {!m_hud.inspector->IsVisible(), "inspector still visible"},
                                  {m_counting->GetWidth() == midBefore + inspWidth,
                                   "mid column did not reclaim the inspector width"},
                                  {m_sess->rows.size() == txBefore,
                                   "hiding the inspector disturbed the transcript"}});

        const bool clickedShow = clickCentreOf(m_hud.inspectorToggle);
        expect("inspector-show", {{clickedShow, "inspector toggle did not dispatch"},
                                   {m_inspectorVisible, "model still says hidden"},
                                   {m_hud.inspector->IsVisible(), "inspector still hidden"},
                                   {m_counting->GetWidth() == midBefore,
                                    "mid column did not return to its original width"},
                                   {m_sess->rows.size() == txBefore,
                                    "showing the inspector disturbed the transcript"}});
    }

    // ⑫ ★★ 2026-10-20：工作区分组臂。
    //   ★ 为何必须在**回放**里造工作区：回放会话 `writer == nullptr` ⇒
    //     `MaybeStartWorkspaceSync` 首行就 return ⇒ 回放会话永远拿不到真实 ack。
    //     但分组渲染本身与 core 无关——它读的是模型侧的外键。所以这里用**真实入口**
    //     （`AcquireWorkspace` + `AssignSessionWorkspace`，与 workspace ack 里那两行
    //     是同一对函数）把模型推到「两个工作区各挂一个会话」，再验视图。
    //     造模型用真函数而不是手写 id 字符串：否则测的是探针自己的常量。
    //   ★ 第一条判据先验**前一个状态**（没工作区时平铺）：否则后面的分组断言
    //     证明不了“平铺是回退项”，只能证明“分组能用”。
    //   ★ 标题序（后建在上）是用户裁决 m08553 的唯一可机检形式：它不是“最近
    //     使用”也不是“字母序”，只能与**创建序逆序**逐字相等。
    {
        const size_t flatRows = m_hud.sessList->GetContentView().GetChildCount();
        const size_t flatSessions = m_sessions.size();

        NewSession();                 // 回放空会话：不拉子进程
        RebuildSessionList();
        // ★★ 探针用的两个路径**故意很长，且只在叶子段不同**——
        //   这正是截图抓到的缺陷形态：两个工作区只差最后一段，而 `Label` 是**尾弃**，
        //   尾部一丢屏幕上一模一样。用短路径（如 `D:/probe/workspace-b`）就根本
        //   触发不到裁剪 ⇒ 这条判据会变成永真的空转。
        Workspace* wsA = AcquireWorkspace("D:/probe/very-long-parent-directory/workspace-alpha");
        Workspace* wsB = AcquireWorkspace("D:/probe/very-long-parent-directory/workspace-beta");
        AssignSessionWorkspace(*m_sessions[0], wsA);
        AssignSessionWorkspace(*m_sessions[1], wsB);
        RebuildSessionList();

        auto firstLabel = [&](size_t i) -> const ECDI::Label* {
            ECDI::Widget* row = rowAt(i);
            if (!row) return nullptr;
            for (size_t c = 0; c < row->GetChildCount(); ++c) {
                const auto* lbl = dynamic_cast<const ECDI::Label*>(row->GetChildAt(c));
                if (lbl != nullptr) return lbl;
            }
            return nullptr;
        };
        auto rowTitle = [&](size_t i) -> std::string {
            const ECDI::Label* lbl = firstLabel(i);
            return lbl ? lbl->GetText() : std::string();
        };
        auto endsWith = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() &&
                   s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        // 行类判据 = 行根类型（会话行必须是 Button：`Panel::ContainsPoint` 恒 false）
        auto rowIsSession = [&](size_t i) {
            return dynamic_cast<const ECDI::Button*>(rowAt(i)) != nullptr;
        };

        const bool twoWorkspaces = (m_workspaces.size() == 2 && wsA && wsB);
        expect("grouping", {{flatRows == flatSessions,
                             "flat fallback was not drawn before any workspace existed"},
                            {twoWorkspaces, "setup: workspaces were not created"},
                            {m_hud.sessList->GetContentView().GetChildCount() == 4,
                             "rowCount != 4 (2 titles + 2 sessions)"},
                            {!rowIsSession(0) && rowIsSession(1) && !rowIsSession(2) &&
                                 rowIsSession(3),
                             "row kinds are not title/session/title/session"},
                            // ★ 判据从“整串相等”改成“尾部相等”：标题现在带中间省略，
                            //   整串比会随预算常量变脆；尾叶子段才是用户靠它区分的东西。
                            {endsWith(rowTitle(0), "workspace-beta"),
                             "later-created workspace is not on top (or its tail was clipped)"},
                            {endsWith(rowTitle(2), "workspace-alpha"),
                             "earlier-created workspace is not below (or its tail was clipped)"},
                            {rowTitle(0) != rowTitle(2),
                             "the two groups render as the same text"},
                            {rowAt(0) && rowAt(1) && rowAt(0)->GetHeight() == kSidebarGroupH &&
                                 rowAt(1)->GetHeight() == kSidebarRowH,
                             "title and session rows are not visually distinguishable"},
                            {selectedIndex() == 1, "grouping lost the active-session marker"},
                            // ★★ m09443：标题行现在有「＋」按钮（不是「×」）。
                            //   旧判据 `deleteButtonOf(0) == nullptr` 会因「＋」的存在而翻红。
                            //   新判据：标题行的按钮（如果有）文字必须是「＋」，不能是「×」。
                            {deleteButtonOf(0) == nullptr ||
                                 deleteButtonOf(0)->GetText() == "＋",
                             "a group title offered a delete button instead of ＋"}});

        // ⑭ ★★ 2026-10-20（截图抓到的那一类）：分组标题必须真的**放得下**。
        //   为何必须量宽而不是只比字符串：裁剪发生在**绘制层**（`Label` → 单行 DrawText），
        //   `GetText()` 读回来的仍是完整串 ⇒ 一条只读模型的判据会全绿而屏幕是错的。
        //   这是 0c `buildTitle` 缺陷的同族：**比对全等，屏幕不一样**。
        //   判据三条缺一条都不成立：
        //     ① 实测宽 ≤ 可用宽（否则右侧会被无声切掉）；
        //     ② 尾部真的保留了（否则“区别”正好被切掉，分组退化成按前缀区分）；
        //     ③ 负对照：**未省略的原串**实测必须超宽——否则说明这些路径根本不需要省略，
        //        ① 就是恒真的空转（“没裁剪”不是因为我们做对了，而是因为没触发）。
        {
            const ECDI::Label* title0 = firstLabel(0);
            // 非 const：`MeasureText` 是虚函数且非 const（测量器可能内部缓存）。
            ECDI::TextMeasurer& measurer = m_window->GetTextMeasurer();
            const ECDI::Font font = harness::MonoFont(harness::kFontMicro);
            const std::string shown = rowTitle(0);
            const float shownW =
                shown.empty() ? 0.0f : measurer.MeasureText(font, shown).width;
            const float availW = title0 ? static_cast<float>(title0->GetWidth()) : 0.0f;
            const float rawW = wsB ? measurer.MeasureText(font, wsB->path).width : 0.0f;
            // ★ 2026-10-26：翻红时只有定性 detail 没法诊断（昨晚绿今稳定红的环境漂移
            //   就是这么卡住的）——宽值三条全进 detail，数值可见才谈得上归属。
            char winfo[220];
            std::snprintf(winfo, sizeof(winfo),
                          "group title is wider than its label (paint layer would clip the "
                          "tail) [shownW=%.1f availW=%.1f rawW=%.1f over=%.1f]",
                          shownW, availW, rawW, shownW - availW);
            expect("group-title-fit",
                   {{title0 != nullptr, "row 0 has no title label"},
                    {shownW <= availW, winfo},
                    {endsWith(shown, "workspace-beta"),
                     "the distinguishing tail did not survive elision"},
                    {shown.size() < (wsB ? wsB->path.size() : 0u),
                     "title was not actually elided"},
                    {rawW > availW,
                     "negative control: the raw path fits, so this phase proves nothing"}});
        }

        // ⑬ ★ 分组臂的负对照：只改**一个自变量**（wsB 的路径），
        //   快照必须在 `row 0 name` 上失配，且只在那里。
        //   若分组标题不读 `Workspace::path`（比如读了会话标签），这条会静默变成
        //   “改了什么都没变”——而所有其它相依然全绿。
        const std::vector<std::string> beforeRename = SnapshotSessionList();
        const std::string savedPath = wsB ? wsB->path : std::string();
        if (wsB) wsB->path = "D:/probe/workspace-b-renamed";   // probe-only corruption
        RebuildSessionList();
        {
            const SidebarCompare cmp =
                CompareSessionList(beforeRename, SnapshotSessionList());
            snap("group-negative");
            m_probeState.sidebarProbeLog.back().matched =
                !cmp.matched && cmp.firstDivergence.rfind("row 0 name", 0) == 0;
            m_probeState.sidebarProbeLog.back().firstDivergence =
                cmp.matched ? "(group negative control saw no difference)"
                            : cmp.firstDivergence;
        }
        // 复原（同 ⑥ 的纪律：负对照不能把状态留在屏上），并验复原真的回到原样
        if (wsB) wsB->path = savedPath;
        RebuildSessionList();
        const SidebarCompare restoredGroup =
            CompareSessionList(beforeRename, SnapshotSessionList());
        if (!restoredGroup.matched) {
            m_probeState.sidebarProbeLog.back().firstDivergence =
                "restore-after-group-negative failed: " + restoredGroup.firstDivergence;
        }

        // ⑮ ★★ 2026-10-20：归档臂。
        //   此时状态：2 个会话（0=wsA, 1=wsB），活动 = 1。
        //   归档 = 会话留在 m_sessions 原位（下标不动），只是从主列表隐藏、移到「归档」区。
        {
            // 先确认当前状态：2 个会话都可见，活动 = 1。
            const size_t rowsBeforeArchive = m_hud.sessList->GetContentView().GetChildCount();
            const size_t activeBefore = m_activeIndex;
            const size_t baseTxRows = m_sess->rows.size();

            // ⑮a 归档**非活动**会话（会话 0）。
            //   判据：主列表行数 -1（会话 0 消失）∧ 总行数 +2（「归档」标题 + 归档行）
            //   ∧ 活动会话不变 ∧ 转录行数不变。
            const bool archivedOk = ArchiveSessionAt(0);
            RebuildSessionList();
            const size_t rowsAfterArchive = m_hud.sessList->GetContentView().GetChildCount();
            expect("archive-inactive",
                   {{archivedOk, "ArchiveSessionAt(0) returned false"},
                    {m_sessions[0]->archived, "model flag not set"},
                    {rowsAfterArchive == rowsBeforeArchive - 1 + 2,
                     "main list should lose 1 row and gain 2 (归档 title + archived row)"},
                    {m_activeIndex == activeBefore,
                     "archiving a non-active session must not change the active session"},
                    {m_sess->rows.size() == baseTxRows,
                     "archiving disturbed the active session's transcript"}});

            // ⑮b 归档**活动**会话（会话 1）。
            //   判据：活动指针先切到邻居（会话 0 已归档 ⇒ 没有非归档邻居）⇒
            //   应该**拒绝**（返回 false），因为不能归档最后一个可见会话。
            //   ★ 这是「禁删到零会话」的同族纪律：活动指针必须指向用户能看见的会话。
            const bool archiveActiveRejected = !ArchiveSessionAt(1);
            RebuildSessionList();
            expect("archive-active-rejected",
                   {{archiveActiveRejected,
                     "archiving the last visible session must be rejected"},
                    {!m_sessions[1]->archived,
                     "the last visible session was archived anyway"}});

            // ⑮c 恢复：点「归档」区的会话 0 行 ⇒ 取消归档 + 切到它。
            //   判据：archived=false ∧ 活动 = 0 ∧ 主列表行数回到归档前。
            const bool restoredOk = RestoreSessionAt(0);
            RebuildSessionList();
            const size_t rowsAfterRestore = m_hud.sessList->GetContentView().GetChildCount();
            expect("restore",
                   {{restoredOk, "RestoreSessionAt(0) returned false"},
                    {!m_sessions[0]->archived, "model flag not cleared"},
                    {m_activeIndex == 0,
                     "restore must switch to the restored session"},
                    {rowsAfterRestore == rowsBeforeArchive,
                     "main list row count did not return to pre-archive state"}});

            // ⑮d 归档负对照：手动翻一个 archived 标志，快照必须失配。
            const std::vector<std::string> beforeCorrupt = SnapshotSessionList();
            m_sessions[0]->archived = true;   // probe-only corruption
            RebuildSessionList();
            {
                const SidebarCompare cmp =
                    CompareSessionList(beforeCorrupt, SnapshotSessionList());
                snap("archive-negative");
                m_probeState.sidebarProbeLog.back().matched = !cmp.matched;
                m_probeState.sidebarProbeLog.back().firstDivergence =
                    cmp.matched ? "(archive negative control saw no difference)"
                                : cmp.firstDivergence;
            }
            m_sessions[0]->archived = false;  // restore
            RebuildSessionList();
        }
    }

    // ★ 焦点复原：合成点击把焦点交给了被点的 Button，留下的焦点框与 caret 闪烁
    //   会多画几帧 ⇒ paints/frames 读数被污染（同 RunHitProbe 的收尾）。
    if (m_window) m_window->SetFocusedWidget(focusBefore);
    m_sidebarDirty = false;

    m_probeState.sidebarProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ Step 0d：多会话探针（--session-probe）。同步跑完——三个读数（会话数/活动下标/
//   行数）都是同步值，不像重建那样要等折叠动画沉降，故不需要跨 tick 状态机。
void HarnessApp::RunSessionProbe() {
    if (!m_sess) { m_probeState.sessionProbeDone = true; return; }
    // ★ 只在回放模式跑（契约见 App.h 声明处）：live 下 NewSession 会真拉一个 core
    //   子进程，探针不该在测量里制造真进程。目前调用点只在 replay settled 分支，
    //   已间接保证；这里再显式挡一道，防止将来有人从别处调用它。
    if (!m_sess->replay) { m_probeState.sessionProbeDone = true; return; }

    auto snap = [&](const char* phase) {
        SessionProbeRec rec;
        rec.phase = phase;
        rec.sessionCount = m_sessions.size();
        rec.activeIndex = m_activeIndex;
        rec.rowCount = m_sess->rows.size();
        // 块行数（不含通知/问句行）——负对照的关键：新会话的块行数必须为 0。
        rec.blockRowCount = 0;
        for (const RowRec& r : m_sess->rows) {
            if (r.tool || r.reasoning || (!r.blockId.empty() && !r.ask)) ++rec.blockRowCount;
        }
        m_probeState.sessionProbeLog.push_back(std::move(rec));
    };

    snap("baseline");                 // ① 起点：当前会话数 / 行数
    NewSession();                     // ② 真的新建一个会话（回放模式 ⇒ 空会话）
    snap("new");                      //    读：会话数应 +1；新会话块行数应为 0
    SwitchToSession(0);               // ③ 切回会话 0
    snap("back");                     //    读：活动会话应回到 0；行数应回到 baseline
    m_probeState.sessionProbeDone = true;
    if (m_application) m_application->Exit();
}

// ★★ Step 5：打开历史会话的取证。
//
// 这条探针要回答三个问题，每个都必须能被反驳：
//   ① **列表是真的从磁盘扫出来的吗？** —— 磁盘文件数 == 屏幕行数（探针自己扫一遍，
//      不只信 UI）。负对照：两者不等就必须报出来。
//   ② **"打开"是真的走回放消费吗？** —— 打开后的 blocks/deltas/messages 必须与
//      **独立**再喂一遍同一文件（`DigestReplayFile`）完全相等，且 `replay->Empty()`
//      为真（真的跳到末尾了）。若实现改成"一次读到底、GUI 自己造 Block"，
//      坏文件那几份（hostile/）的读数会立刻不等，且 seqGaps 变成 0。
//   ③ **GUI 侧状态能存能读回来吗？** —— 答一次 ⇒ sidecar 落盘；重新打开同一文件 ⇒
//      模型里 answer 非空、屏幕上那个钮仍是常态色。**缺失时**则必须如实提示
//      「此会话没有决策记录」，而不是静默当成"从未回答过"（Step 3 修的就是这条病）。
void HarnessApp::RunHistoryProbe() {
    if (!m_sess) { m_probeState.historyProbeDone = true; return; }
    if (!m_sess->replay) { m_probeState.historyProbeDone = true; return; }
    if (!m_hud.historyList) { m_probeState.historyProbeDone = true; return; }
    if (m_probeState.historyProbeStep > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_probeState.historyProbeNextAt) return;
    }
    const auto settle = std::chrono::milliseconds(300);

    auto rowsOnScreen = [&]() -> size_t {
        return m_hud.historyList->GetContentView().GetChildCount();
    };
    auto snap = [&](const char* phase) {
        HistoryProbeRec rec;
        rec.phase = phase;
        rec.filesOnDisk = FindHistoryFiles().size();
        rec.rowsOnScreen = rowsOnScreen();
        if (m_sess) {
            rec.openedLabel = m_sess->label;
            rec.originKind = m_sess->originKind;
            rec.blocks = m_sess->transcript.Blocks();
            rec.deltas = m_sess->transcript.Deltas();
            rec.messages = m_sess->replay ? m_sess->replay->Total() : 0;
            rec.atEnd = m_sess->replay ? m_sess->replay->Empty() : false;
            for (const RowRec& r : m_sess->rows) {
                if (!r.ask) continue;
                ++rec.askRows;
                if (r.answered) ++rec.answeredRows;
            }
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) ++rec.modelAnswers;
            }
        }
        m_probeState.historyProbeLog.push_back(std::move(rec));
    };
    auto finish = [&](const char* phase, std::vector<std::pair<bool, const char*>> conds) {
        snap(phase);
        HistoryProbeRec& rec = m_probeState.historyProbeLog.back();
        std::string bad;
        for (const auto& [ok, what] : conds) {
            if (ok) continue;
            if (!bad.empty()) bad += "; ";
            bad += what;
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
    };

    switch (m_probeState.historyProbeStep) {
    case 0: {
        // ── ① list：磁盘 == 屏幕，且至少有一个可点的目标 ──
        RebuildHistoryList();   // 直接重建，保证计数是最新的
        const size_t onDisk = FindHistoryFiles().size();
        // 目标文件：**优先当前 --replay 的那个文件**——这样每个夹具测的都是自己，
        //   且 `list` 相与 `cleanup` 相的活动会话读数可以直接当对照
        //   （cot 打开 cot、scale 打开 scale，判别力比"永远打开 demo"强得多）。
        //   ★ 若当前会话不是文件来源（live / 空会话），退到 demo（它有权限门，
        //     sidecar 三态才有意义），再退到第一个。
        m_probeState.historyProbeFile.clear();
        const std::filesystem::path currentFile =
            (m_sess->originPath.empty() || !m_sess->originPath.has_extension())
                ? std::filesystem::path()
                : m_sess->originPath;
        if (!currentFile.empty()) {
            // ★ 必须规范化再比：`--replay` 给的是相对路径（`protocol/fixtures/...`），
            //   而 `FindHistoryFiles` 返回的是绝对路径。字典序比较会永远不等。
            std::error_code ec;
            const std::filesystem::path want = std::filesystem::weakly_canonical(currentFile, ec);
            for (const std::filesystem::path& f : m_historyFiles) {
                std::error_code ec2;
                if (std::filesystem::weakly_canonical(f, ec2) == want) {
                    m_probeState.historyProbeFile = f;
                    break;
                }
            }
        }
        if (m_probeState.historyProbeFile.empty()) {
            for (const std::filesystem::path& f : m_historyFiles) {
                if (f.filename() == "demo-session.jsonl") { m_probeState.historyProbeFile = f; break; }
            }
        }
        if (m_probeState.historyProbeFile.empty() && !m_historyFiles.empty()) {
            m_probeState.historyProbeFile = m_historyFiles.front();
        }
        m_probeState.historyProbeDigest = DigestReplayFile(m_probeState.historyProbeFile);
        const bool existed = !m_probeState.historyProbeFile.empty() &&
            std::filesystem::is_regular_file(m_probeState.historyProbeFile.generic_string() + ".ui.json");
        finish("list", {{onDisk > 0, "no fixture found on disk"},
                        {rowsOnScreen() == onDisk, "row count != file count on disk"},
                        {!m_probeState.historyProbeFile.empty(), "no target file chosen"},
                        {m_probeState.historyProbeDigest.loaded && m_probeState.historyProbeDigest.blocks > 0,
                         "digest of target file is empty"}});
        m_probeState.historyProbeLog.back().sidecarExisted = existed;
        m_probeState.historyProbeStep = 1;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now();   // 立刻进下一步
        return;
    }
    case 1: {
        // ── ② 点真实的历史行（走真实派发链，不是直接调 OpenHistorySession）──
        size_t targetRow = 0;
        for (size_t i = 0; i < m_historyFiles.size(); ++i) {
            if (m_historyFiles[i] == m_probeState.historyProbeFile) { targetRow = i; break; }
        }
        ECDI::Widget* row = m_hud.historyList->GetContentView().GetChildAt(targetRow);
        if (!row) {
            finish("open", {{false, "history row widget missing"}});
            m_probeState.historyProbeStep = 9;
            m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
            return;
        }
        // ★★ 视口陷阱（Step 3 的同族，这里换了个容器）：历史区最多显示 4 行，
        //   第 5、6 个文件（realistic / scale）在裁剪区外。`Widget::HitTest` 对
        //   `ClipsChildren()` 的子树有 `!ContainsRect` 早退 ⇒ 落在裁剪区外的坐标
        //   在子树里找不到目标、**静默回落到别处**：点击不报错、也没有任何变化，
        //   读数伪装成"打开逻辑坏了"。故先把目标行滚进视口。
        {
            const int y = row->GetY();
            const int h = row->GetHeight();
            const int viewH = m_hud.historyList->GetHeight();
            const int cur = m_hud.historyList->GetScrollOffsetY();
            if (y < cur || y + h > cur + viewH) {
                m_hud.historyList->SetContentOffset(0, std::max(0, y));
            }
        }
        const ECDI::Point abs = row->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + row->GetWidth() / 2,
                          static_cast<int>(abs.y) + row->GetHeight() / 2);
        m_probeState.historyProbeStep = 2;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 2: {
        // ── ③ open：确实是"历史"来源、真的跳到末尾、内容与独立回放一致 ──
        const ReplayDigest& d = m_probeState.historyProbeDigest;
        finish("open", {
            {m_sess->originKind == "replay-history", "originKind is not replay-history"},
            {m_sess->label == m_probeState.historyProbeFile.filename().generic_string(),
             "opened session label != file name"},
            {m_sess->replay && m_sess->replay->Empty(), "replay not drained to the end"},
            {m_sess->transcript.Blocks() == d.blocks, "blocks != independent digest"},
            {m_sess->transcript.Deltas() == d.deltas, "deltas != independent digest"},
            {(m_sess->replay ? m_sess->replay->Total() : 0) == d.messages,
             "messages != independent digest"},
        });
        m_probeState.historyProbeStep = 3;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 3: {
        // ── ④ sidecar-missing：新夹具没有状态文件 ⇒ 权限门必须**未答**，
        //      且必须有一条如实的提示（不能假装"从未问过"）。
        const bool noAnswersYet = [&] {
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) return false;
            }
            return true;
        }();
        const bool noticeSaid = [&] {
            for (const TimelineEntry& e : m_sess->timeline) {
                if (e.kind != TimelineEntry::Kind::Notice) continue;
                if (e.text.find("没有决策记录") != std::string::npos) return true;
            }
            return false;
        }();
        bool anyAsk = false;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        std::vector<std::pair<bool, const char*>> conds;
        conds.push_back({noAnswersYet, "model already has answers before any click"});
        if (anyAsk) {
            conds.push_back({noticeSaid, "missing sidecar was not announced"});
        }
        finish("sidecar-missing", conds);
        m_probeState.historyProbeLog.back().sidecarExisted = false;
        m_probeState.historyProbeStep = 4;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 4: {
        // ── ⑤ sidecar-write：答一次 ⇒ 状态必须落盘，且内容里真有这个 ask id ──
        ECDI::Button* allow = nullptr;
        for (RowRec& r : m_sess->rows) {
            if (r.ask && r.allowBtn) { allow = r.allowBtn; break; }
        }
        if (!allow) {
            // 目标夹具没有权限门 ⇒ 这一相与下一相无意义，如实记下而不是静默跳过。
            finish("sidecar-write", {{false, "(target fixture has no ask row)"}});
            m_probeState.historyProbeStep = 7;
            m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
            return;
        }
        // ★★ 同一个视口陷阱的第三次出场：历史会话是**直接拉到末尾**打开的，
        //   第一个权限门通常远在视口之上（cot 2 个 ask、realistic 11 个、
        //   scale 62 个；只有 demo 的 1 个恰好在屏幕附近）。点击落在 ScrollView
        //   裁剪区外时 `HitTest` 早退、**零变化零报错**，读数会显示
        //   "sidecar 没写"——但真正没发生的是"点击"。先把它滚进来。
        for (RowRec& r : m_sess->rows) {
            if (!r.ask || !r.panel) continue;
            const int y = r.panel->GetY();
            const int h = r.panel->GetHeight();
            const int viewH = m_scroll ? m_scroll->GetHeight() : 0;
            const int cur = m_scroll ? m_scroll->GetScrollOffsetY() : 0;
            if (m_scroll && (y < cur || y + h > cur + viewH)) {
                m_scroll->SetContentOffset(0, std::max(0, y - 8));
            }
            break;
        }
        const ECDI::Point abs = allow->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + allow->GetWidth() / 2,
                          static_cast<int>(abs.y) + allow->GetHeight() / 2);
        m_probeState.historyProbeStep = 5;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 5: {
        const std::filesystem::path statePath = UiStatePath(*m_sess);
        const bool written = !statePath.empty() &&
                             std::filesystem::is_regular_file(statePath);
        bool hasAnswer = false;
        if (written) {
            std::ifstream in(statePath, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
            hasAnswer = text.find("\"allow\"") != std::string::npos;
        }
        finish("sidecar-write", {{written, "sidecar file was not written"},
                                 {hasAnswer, "sidecar does not contain the answer"},
                                 {m_sess->originKind == "replay-history",
                                  "write happened for a non-history session"}});
        m_probeState.historyProbeLog.back().sidecarWritten = written;
        m_probeState.historyProbeStep = 6;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 6: {
        // ── ⑥ sidecar-roundtrip：重新打开同一文件 ⇒ 结论必须回来 ──
        //   ★ 直接调 `OpenHistorySession`（此刻不在派发栈上，安全），
        //     而不是再点一次行：点行这一相已在 ② 验过，这里测的是**读回**。
        OpenHistorySession(m_probeState.historyProbeFile);
        m_probeState.historyProbeStep = 7;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 7: {
        size_t modelAnswers = 0;
        size_t screenAnswered = 0;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask && !e.answer.empty()) ++modelAnswers;
        }
        for (const RowRec& r : m_sess->rows) {
            if (r.ask && r.answered) ++screenAnswered;
        }
        std::vector<std::pair<bool, const char*>> conds;
        // 若目标夹具根本没有 ask 行（⑤ 已记过），此相不作断言（如实写进差异里）。
        bool anyAsk = false;
        for (const TimelineEntry& e : m_sess->timeline) {
            if (e.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        if (!anyAsk) {
            conds.push_back({false, "(target fixture has no ask row)"});
        } else {
            conds.push_back({modelAnswers >= 1, "answer was not restored into the model"});
            conds.push_back({screenAnswered >= 1, "answer was not restored onto the screen"});
        }
        finish("sidecar-roundtrip", conds);
        // ── 收尾：把探针造出来的副作用清干净（工件目录不留脏文件/脏会话）──
        //   ★ 路径必须**直接从被打开的文件**算：此刻 `m_sess` 还是历史会话，
        //     但下面会把它删掉，删完 `m_sess` 就指回原会话（originPath 是
        //     `--replay` 那个文件）⇒ 用 `UiStatePath(*m_sess)` 会算错路径、
        //     把用户的 sidecar 留下来、还删不掉自己的。
        const std::filesystem::path statePath =
            m_probeState.historyProbeFile.empty()
                ? std::filesystem::path()
                : std::filesystem::path(m_probeState.historyProbeFile.generic_string() + ".ui.json");
        const bool created = !m_probeState.historyProbeLog.front().sidecarExisted;
        if (created && !statePath.empty()) {
            std::error_code ec;
            std::filesystem::remove(statePath, ec);
            m_probeState.historyProbeLog.back().sidecarRemoved = !ec;
        }
        // 删掉探针新建的会话（从尾部删，`DeleteSessionAt` 会先切邻居再 erase）
        while (m_sessions.size() > 1) {
            DeleteSessionAt(m_sessions.size() - 1);
        }
        m_sidebarDirty = true;
        m_probeState.historyProbeStep = 8;
        m_probeState.historyProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 8: {
        // ── ⑦ 收尾读数：工作区真的干净了（会话数回到 1、sidecar 不再存在）──
        const bool cleanSessions = m_sessions.size() == 1;
        bool cleanSidecar = true;
        if (!m_probeState.historyProbeFile.empty()) {
            cleanSidecar = !std::filesystem::is_regular_file(
                m_probeState.historyProbeFile.generic_string() + ".ui.json");
        }
        const bool created = !m_probeState.historyProbeLog.front().sidecarExisted;
        finish("cleanup", {{cleanSessions, "sessions left behind by the probe"},
                           {cleanSidecar || !created, "sidecar left behind by the probe"}});
        m_probeState.historyProbeStep = 9;
        m_probeState.historyProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    default:
        m_probeState.historyProbeDone = true;
        return;
    }
}

// ★★ Step 4 / 0e（2026-10-19）：整树重建 + 运行期主题切换的取证。
//   【与前面五个探针的形状差异】那些是"一步做完"的（同一个 tick 里造前置、重建、比对），
//   因为局部重建是同步的。本探针不行：
//     ① 主题切换的正确入口是**点顶栏那个 Button**，而回调只置 `m_uiRebuildPending`
//        （派发栈上不能拆树）⇒ 重建要等到**下一个 tick 的开头**才发生；
//     ② 要证明"重建真的发生了"，必须读 `m_uiRebuilds` **在重建之后**的值。
//   ⇒ 故写成跨 tick 状态机（`settle` 与 `RunRebuildProbe` 同款 400ms：足够让
//     重建落地 + 一帧画完 + HUD 刷完）。
//   【判据的骨架】所有 rebuild 类相都比 `m_probeState.themeUiBefore`（baseline 四路快照）：
//     · `matched == true`  —— 侧栏/转录/输入框/状态标记四路一处不差；
//     · `paints` **恰好等于** baseline —— 这是"仪器没被实验打坏"的唯一读数；
//     · `frames`/`frameUsTotal` 恰好相等 —— `SeedCounters` 真的把历史搬回来了。
//   【负对照】`negative` 相把 `m_sidebarVisible` 单独改掉再重建：四路比对应**报出**
//     `state: sidebarVisible [1] -> [0]`。它证明 ①②③ 的相等不是"量具瞎了"。
void HarnessApp::RunThemeProbe() {
    if (!m_sess) { m_probeState.themeProbeDone = true; return; }
    // ★ 只在回放模式：live 下重建会... 其实也安全（child 是 Session 的，不受控件树影响），
    //   但 live 的转录在持续增长 ⇒ 两次快照之间必然不同，`matched` 会假失败。
    //   探针测的是"重建保状态"，不是"重建和流式数据赛跑"，故限定回放。
    if (!m_sess->replay) { m_probeState.themeProbeDone = true; return; }
    if (!m_hud.themeToggle) { m_probeState.themeProbeDone = true; return; }

    if (m_probeState.themeProbeStep > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now < m_probeState.themeProbeNextAt) return;
    }
    const auto settle = std::chrono::milliseconds(400);

    auto snap = [&](const char* phase) {
        ThemeProbeRec rec;
        rec.phase = phase;
        rec.light = harness::g_themeLight;
        rec.uiRebuilds = m_uiRebuilds;
        rec.paints = m_counting ? m_counting->PaintCount() : 0;
        rec.frames = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        rec.sidebarRows = m_hud.sessList
                              ? static_cast<int>(m_hud.sessList->GetContentView().GetChildCount())
                              : 0;
        rec.transcriptRows = static_cast<int>(m_sess->rows.size());
        rec.inputText = (m_promptBar && m_promptBar->input())
                            ? m_promptBar->input()->GetText() : std::string();
        rec.sidebarVisible = (m_hud.sidebar != nullptr) && m_hud.sidebar->IsVisible();
        rec.activeIndex = m_activeIndex;
        rec.sessionCount = m_sessions.size();
        m_probeState.themeProbeLog.push_back(std::move(rec));
    };
    // 记一条并把四路比对结果写进去（`paints` 等三项仪器读数单独判）
    auto snapCompare = [&](const char* phase, size_t paintsBefore, size_t framesBefore,
                           size_t frameUsBefore) {
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_probeState.themeUiBefore, after);
        snap(phase);
        ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
        std::string bad = cmp.matched ? std::string() : cmp.firstDivergence;
        // ★ 仪器读数：重建期间不该有任何重绘 ⇒ 必须**恰好**相等。
        //   变大/变小都是缺陷，故不用 ">=" 之类的宽容判据。
        if (after.paints != paintsBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "paints " + std::to_string(paintsBefore) + " -> "
                   + std::to_string(after.paints);
        }
        if (after.frames != framesBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "frameCount " + std::to_string(framesBefore) + " -> "
                   + std::to_string(after.frames);
        }
        if (after.frameUsTotal != frameUsBefore) {
            if (!bad.empty()) bad += "; ";
            bad += "frameUsTotal " + std::to_string(frameUsBefore) + " -> "
                   + std::to_string(after.frameUsTotal);
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        rec.paints = after.paints;
        rec.frames = after.frames;
        rec.frameUsTotal = after.frameUsTotal;
    };

    switch (m_probeState.themeProbeStep) {
    case 0: {
        // ── ① baseline：抓四路快照 + 仪器读数，然后**同步**做一次重建 ──
        //   ★ 这里可以同步重建：本函数跑在 `OnReplayTick` 里（timer 回调，不在派发栈上）。
        //     而 `toggle` 相不行——那一相必须走真实点击，故分了两条路径。
        m_probeState.themeUiBefore = SnapshotUi();
        m_probeState.themePaintsBefore = m_probeState.themeUiBefore.paints;
        m_probeState.themeFramesBefore = m_probeState.themeUiBefore.frames;
        snap("baseline");
        {
            ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
            // baseline 的判据是"前置条件成立"：必须有 ≥1 个会话行、≥1 个转录行，
            // 否则"重建后都一样"只说明两边都是空的（探针是瞎的）。
            rec.matched = (rec.sidebarRows >= 1) && (rec.transcriptRows >= 1);
            if (!rec.matched) {
                rec.firstDivergence = "baseline has nothing to preserve (sidebar/transcript empty)";
            }
        }
        // 造判别力：给输入框塞一段草稿（这是唯一无法从模型重建的状态，
        // 若探针不主动造它，"草稿被搬过去了"永远是空真）。
        if (m_promptBar && m_promptBar->input()) {
            m_promptBar->input()->SetText("draft-not-sent");
            m_promptBar->input()->MoveCaretToEnd();
            // ★ 塞完草稿要重抓快照：`m_probeState.themeUiBefore` 必须包含这段草稿，
            //   否则重建后"草稿还在"这条就成了拿空串对比空串。
            m_probeState.themeUiBefore = SnapshotUi();
            m_probeState.themePaintsBefore = m_probeState.themeUiBefore.paints;
            m_probeState.themeFramesBefore = m_probeState.themeUiBefore.frames;
        }
        RebuildUi();
        // ★ 重建后立刻比对（同一 tick，未经过任何重绘）
        snapCompare("rebuild", m_probeState.themePaintsBefore, m_probeState.themeFramesBefore,
                    m_probeState.themeUiBefore.frameUsTotal);
        // ★ 额外：`uiRebuilds` 必须真的 +1（这是"重建发生了"的机读证据；
        //   若忘了 ++，后面的 toggle 相还会通过，但"重建"其实一次都没跑）。
        if (m_probeState.themeProbeLog.back().uiRebuilds != 1) {
            m_probeState.themeProbeLog.back().matched = false;
            m_probeState.themeProbeLog.back().firstDivergence +=
                "; uiRebuilds expected 1, got " + std::to_string(m_probeState.themeProbeLog.back().uiRebuilds);
        }
        m_probeState.themeProbeStep = 1;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 1: {
        // ── ② rebuild-again：再同步重建一次 ⇒ 幂等（第二次也得全等） ──
        const size_t p = m_counting ? m_counting->PaintCount() : 0;
        const size_t f = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        uint64_t t = 0;
        if (m_counting) for (uint64_t us : m_counting->FrameDurationsUs()) t += us;
        RebuildUi();
        snapCompare("rebuild-again", p, f, static_cast<size_t>(t));
        m_probeState.themeProbeStep = 2;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 2: {
        // ── ③ toggle：**真实点击**顶栏「主题」钮 ⇒ 下一个 tick 才会重建 ──
        //   ★ 这一相是本步的命题本身：运行期换主题。
        //   ★ 用 `SynthesizeClickAt` 走真实派发链（`Application::OnEvent`），
        //     不是直接调 `RebuildUi`——否则"按钮接上了没有"这件事没被验证
        //     （0c 的 `buildTitle` 正是这样骗过判据的：画对了，回调没接）。
        const ECDI::Point abs = m_hud.themeToggle->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.themeToggle->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.themeToggle->GetHeight() / 2);
        m_probeState.themeProbeStep = 3;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 3: {
        // ── ④ 读数：重建必须真的发生（uiRebuilds 2→3）且真的换色 ──
        snap("toggle");
        {
            ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
            std::string bad;
            // 注意：`m_probeState.themeUiBefore.light` 是深色时的值；翻转后应相反
            if (rec.light == m_probeState.themeUiBefore.light) bad += "theme did not flip";
            if (rec.uiRebuilds != 3) {
                if (!bad.empty()) bad += "; ";
                bad += "uiRebuilds expected 3, got " + std::to_string(rec.uiRebuilds);
            }
            // ★ 主题真的换了 ⇒ 侧栏/转录的四路快照**结构**仍必须相同
            //   （换的是颜色，不是内容。`SnapshotSessionList`/`SnapshotRows` 都只记
            //   文字与几何、不记颜色，故此处相等是正确判据；颜色由 `light` 字段证明）。
            const UiSnapshot after = SnapshotUi();
            const SidebarCompare cmp = CompareUi(m_probeState.themeUiBefore, after);
            if (!cmp.matched) {
                if (!bad.empty()) bad += "; ";
                bad += cmp.firstDivergence;
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
            rec.sidebarRows = static_cast<int>(after.sidebar.size());
            rec.transcriptRows = static_cast<int>(after.transcript.size());
            rec.inputText = after.inputText;
        }
        m_probeState.themeProbeStep = 4;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 4: {
        // ── ⑤ toggle-back：再点一次 ⇒ 回到深色，且状态一致 ──
        const ECDI::Point abs = m_hud.themeToggle->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.themeToggle->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.themeToggle->GetHeight() / 2);
        m_probeState.themeProbeStep = 5;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 5: {
        snap("toggle-back");
        {
            ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
            std::string bad;
            if (rec.light != m_probeState.themeUiBefore.light) bad += "theme did not flip back";
            if (rec.uiRebuilds != 4) {
                if (!bad.empty()) bad += "; ";
                bad += "uiRebuilds expected 4, got " + std::to_string(rec.uiRebuilds);
            }
            const UiSnapshot after = SnapshotUi();
            const SidebarCompare cmp = CompareUi(m_probeState.themeUiBefore, after);
            if (!cmp.matched) {
                if (!bad.empty()) bad += "; ";
                bad += cmp.firstDivergence;
            }
            rec.matched = bad.empty();
            rec.firstDivergence = bad;
        }
        m_probeState.themeProbeStep = 6;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 6: {
        // ── ⑥ negative：单独改 `m_sidebarVisible`（不改任何控件）再重建 ──
        //   ★ 这是"比对面能看见模型侧状态"的唯一证据：若 `CompareUi` 只看控件，
        //     这条差异会被漏掉（新树默认全可见，重建后侧栏又是可见的 ⇒ 控件本身
        //     看不出"模型里说要收起"）。故断言**必须**报出 `state: sidebarVisible`。
        const bool savedSidebar = m_sidebarVisible;
        m_sidebarVisible = false;
        // 不清脏标志重建：`RebuildUi` 的 ⑩ 会用 `m_sidebarVisible` 恢复成隐藏
        const size_t p = m_counting ? m_counting->PaintCount() : 0;
        const size_t f = m_counting ? m_counting->FrameDurationsUs().size() : 0;
        uint64_t t = 0;
        if (m_counting) for (uint64_t us : m_counting->FrameDurationsUs()) t += us;
        RebuildUi();
        // ★ 现在侧栏是**隐藏**的 ⇒ 与 baseline（可见）比对必须不等，
        //   且分歧必须指向 sidebarVisible。
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_probeState.themeUiBefore, after);
        snap("negative");
        ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
        rec.paints = p;   // 仪器读数对本相无意义（此处只测模型状态是否进比对）
        rec.frames = f;
        rec.frameUsTotal = static_cast<size_t>(t);
        const bool sawDifference = !cmp.matched;
        const bool pointedAtSidebar =
            cmp.firstDivergence.find("sidebarVisible") != std::string::npos;
        // ★ 负对照的"通过"= 看见了差异 ⇒ `matched` 记 false 是**正确读数**。
        rec.matched = false;
        if (sawDifference && pointedAtSidebar) {
            rec.firstDivergence = "ok(expected divergence): " + cmp.firstDivergence;
        } else if (!sawDifference) {
            rec.firstDivergence = "negative control saw no difference -- snapshot ignores state";
        } else {
            rec.firstDivergence = "divergence not in sidebarVisible: " + cmp.firstDivergence;
        }
        // 复原为原状态（单变量纪律：negative 之后必须能回到 baseline）
        m_sidebarVisible = savedSidebar;
        if (m_hud.sidebar) m_hud.sidebar->SetVisible(savedSidebar);
        if (m_hud.sidebar) {
            if (ECDI::Widget* parent = m_hud.sidebar->GetParent()) parent->Arrange();
        }
        m_probeState.themeProbeStep = 7;
        m_probeState.themeProbeNextAt = std::chrono::steady_clock::now() + settle;
        return;
    }
    case 7: {
        // ── ⑦ restored：确认回到 baseline（含侧栏可见性、主题、仪器读数） ──
        const UiSnapshot after = SnapshotUi();
        const SidebarCompare cmp = CompareUi(m_probeState.themeUiBefore, after);
        snap("restored");
        ThemeProbeRec& rec = m_probeState.themeProbeLog.back();
        std::string bad = cmp.matched ? std::string() : cmp.firstDivergence;
        if (after.light != m_probeState.themeUiBefore.light) {
            if (!bad.empty()) bad += "; ";
            bad += "theme not restored";
        }
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        m_probeState.themeProbeStep = 8;
        m_probeState.themeProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    default:
        m_probeState.themeProbeDone = true;
        return;
    }
}

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendSessionProbeSummary(std::string& json) {
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

void HarnessApp::AppendSidebarProbeSummary(std::string& json) {
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
        char b[4096];
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

void HarnessApp::AppendThemeProbeSummary(std::string& json) {
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

void HarnessApp::AppendHistoryProbeSummary(std::string& json) {
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

