// ============================================================
// 工作区协商与提示词提交（协议 §7 / §8）
// ------------------------------------------------------------
// 为什么单独一个文件：工作区是**协议状态机**的一段——core 请求 → 用户选目录
//   → 回 workspace → 拿到 workspaceCap 才允许写盘。它与「谁在建控件」无关，
//   只做状态迁移与消息投递；把这段留在 App.cpp 里，改 tick 时要跨过它。
// ★ 纪律：这里的每个函数都**不碰控件树**（唯一的例外是 ChooseWorkspaceFolder
//   弹系统对话框）。建行/建控件属于 1d 的转录与 HUD。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1c）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "ThemeUsing.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>     // SHBrowseForFolderW（选工作区目录）
#include <objbase.h>
#ifdef DrawText
#undef DrawText
#endif

#include <filesystem>
#include <string>
#include <vector>

// 跨 TU 共享符号（定义见 PathUtil.cpp）
using harness::internal::PathToUtf8;
using harness::internal::Utf8FromWidePath;

// P1.5：发送路径（发送按钮与 --auto-prompt 共用）
bool HarnessApp::SendPrompt(const std::string& text) {
    // ★★ 2026-10-27 修复（实测反馈「点空态卡的建议，发出去的气泡是空的」）：
    //   **进函数第一件事是拷贝**。调用方之一是空状态引导卡里的建议按钮，而发送
    //   路径会在本函数的**栈内**就把它拆掉：
    //     SendPrompt → PostPrompt → MaterializePendingPrompts → RefreshEmptyGuide
    //     （判定「已不空」⇒ `RemoveChild(emptyGuide)` ⇒ 按钮与它的 lambda 一起析构）
    //   ⇒ 形参 `text` 引用的是那个 lambda 的捕获串，走到 `writer->Prompt(text, …)`
    //     时它已经悬空 ⇒ 发给 core 的是垃圾字节 ⇒ core 回显的 `user` 块没有内容
    //     ⇒ 正式气泡是空的（而 `pending.text` 早一步拷贝过，所以"发送中"那个
    //     临时气泡**有**字——正是实测看到的先后两个样子）。
    //   ★ 这是**一直存在**的悬空引用，不是新引入：以前读到的是尚未被覆盖的旧字节，
    //     "碰巧"正确；今天的内存布局一变（新增的临时串/局部量）就现形了。
    //   ★ 一处拷贝覆盖全部调用方，比每个回调各自 `std::string owned = text;` 可靠
    //     （后者漏一个就是同一类 bug 复发）。
    const std::string owned = text;
    // ★ --hit-probe：探针合成点击时只计数、零副作用，保持旧读数。
    if (m_probeState.hitProbeActive) {
        ++m_probeState.hitProbeSendCalls;
        return true;
    }
    // ★★ L2 持久化：拦截只读会话发送
    if (m_sess && m_sess->originKind == "journal") {
        AddNoticeRow("[只读会话不可发送消息，请点击「继续」开启新对话]", kWarn);
        Relayout();
        RequestRepaint();
        return false;
    }
    if (m_sess->writer) {
        return !PostPrompt(owned).empty();
    }
    AddNoticeRow("[prompt 仅记录] " + owned, kDarkMuted);   // 回放：只记录
    Relayout();
    RequestRepaint();
    return true;
}

std::string HarnessApp::PostPrompt(const std::string& text, bool allowBusyForProbe) {
    if (!m_sess || !m_sess->writer || m_sess->disconnected ||
        m_sess->protocolIncompatible || m_sess->protocolVer < 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        !m_sess->pendingWorkspaceId.empty()) {
        return {};
    }
    if (allowBusyForProbe) {
        if (m_sess->coreState != "running" || HasUnconfirmedPrompt()) return {};
    } else if (m_sess->coreState != "idle" || HasUnconfirmedPrompt()) {
        return {};
    }

    const std::string clientId = "p" + std::to_string(m_sess->nextClientId++);
    // ★★ m09443：发了 prompt ⇒ 临时会话转正。
    m_sess->temporary = false;
    if (m_sess->firstPromptText.empty()) {
        m_sess->firstPromptText = text;
    }
    if (!m_sess->journalPromoted && !m_sess->workspacePath.empty()) {
        PromoteSessionJournal(*m_sess, text);
    }
    PendingPrompt pending;
    pending.clientId = clientId;
    pending.text = text;
    m_sess->pendingPrompts.push_back(std::move(pending));

    // Materialize and paint the optimistic row before touching the reverse-channel pipe.
    MaterializePendingPrompts();
    Relayout();
    FollowTailIfPinned();
    RefreshHud();
    RequestRepaint();

    if (!m_sess->writer->Prompt(text, clientId)) {
        FailPendingOperations("prompt 写入失败，连接未确认");
    }
    return clientId;
}

bool HarnessApp::HasUnconfirmedPrompt() const {
    if (!m_sess) return false;
    return std::any_of(m_sess->pendingPrompts.begin(), m_sess->pendingPrompts.end(),
                       [](const PendingPrompt& p) { return p.status == "sending"; });
}

void HarnessApp::FailPrompt(const std::string& clientId, const std::string& reason) {
    if (!m_sess || clientId.empty()) return;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.clientId != clientId || pending.status != "sending") continue;
        pending.status = "failed";
        pending.failure = reason.empty() ? "core 未确认" : reason;
        MaterializePendingPrompts();
        Relayout();
        FollowTailIfPinned();
        RefreshHud();
        RequestRepaint();
        return;
    }
}

void HarnessApp::FailPendingOperations(const std::string& reason) {
    if (!m_sess) return;
    m_sess->disconnected = true;
    const std::string detail = reason.empty() ? "连接已断开" : reason;
    for (PendingPrompt& pending : m_sess->pendingPrompts) {
        if (pending.status == "sending") {
            pending.status = "failed";
            pending.failure = detail;
        }
    }
    if (!m_sess->pendingWorkspaceId.empty()) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区未确认：" + detail;
    }
    MaterializePendingPrompts();
    Relayout();
    RefreshHud();
    RequestRepaint();
}

bool HarnessApp::RequestWorkspace(const std::string& path, bool initialSync) {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer < 3 ||
        !m_sess->workspaceCap || m_sess->protocolIncompatible || m_sess->disconnected ||
        path.empty() || m_sess->coreState != "idle" ||
        !m_sess->pendingWorkspaceId.empty()) {
        return false;
    }
    if (initialSync && m_sess->workspaceSyncStarted) return false;
    if (initialSync) m_sess->workspaceSyncStarted = true;

    const std::string requestId = "w" + std::to_string(m_sess->nextWorkspaceRequestId++);
    m_sess->pendingWorkspaceId = requestId;
    m_sess->pendingWorkspaceInitialSync = initialSync;
    m_sess->workspaceStatus = "等待 core 确认…";
    RefreshHud();
    if (!m_sess->writer->Workspace(requestId, path)) {
        m_sess->pendingWorkspaceId.clear();
        m_sess->pendingWorkspaceInitialSync = false;
        m_sess->workspaceStatus = "工作区请求发送失败";
        FailPendingOperations("workspace 写入失败，连接未确认");
        return false;
    }
    return true;
}

// ── ★★ 2026-10-20：工作区注册表（模型侧实现）────────────────────────────
// 三条不变量都在这里维护（设计注见 `App.h` 的 `struct Workspace`）：
//   ① **只追加**：`m_workspaces` 的下标即创建序，永不删除、永不重排 ⇒
//      「创建序固定」这条用户裁决不需要额外排序字段，下标本身就是证据；
//   ② **按路径去重**：同一目录的第二个会话复用同一个实体（否则侧栏会画出
//      两个同路径的标题行，而用户看到的会是"我的会话莫名其妙分成两组"）；
//   ③ **只由 core 确认过的路径创建**：`AcquireWorkspace` 的唯一调用点在 workspace
//      ack 的 `ok:true` 分支 ⇒ 注册表里不存在"GUI 自己猜的路径"。
HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

const HarnessApp::Workspace* HarnessApp::WorkspaceById(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (const auto& ws : m_workspaces) {
        if (ws && ws->id == id) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::WorkspaceByPath(const std::string& path) {
    if (path.empty()) return nullptr;
    // ★ 字符串完全相等，不用 `std::filesystem::equivalent()`：入参已经是 core 回显的
    //   规范化路径（§3.9：绝对、`/` 分隔、无尾随 `/`、无 `.`/`..`）。用 equivalent
    //   会把 `D:/a` 与 `d:/A` 判成同一处——那是替 core 改写它自己的规范化结果。
    for (const auto& ws : m_workspaces) {
        if (ws && ws->path == path) return ws.get();
    }
    return nullptr;
}

HarnessApp::Workspace* HarnessApp::AcquireWorkspace(const std::string& path) {
    if (path.empty()) return nullptr;
    if (Workspace* existing = WorkspaceByPath(path)) return existing;
    auto ws = std::make_unique<Workspace>();
    // ★ ID 形如 `w1`/`w2`…，与协议侧的 `request_id`（`w1`/`w2`…）**同名但不同域**：
    //   前者是 GUI 模型里的工作区实体，后者是单次请求的关联键。两者都由自增计数器
    //   产生、都从 1 开始，所以一个会话的 `pendingWorkspaceId` 常与它即将获得的
    //   `workspaceId` 字形相似——这不是缺陷（探针读的是 `Workspace::path`），
    //   但**别在代码里把两者互相当作判据**。
    ws->id = "w" + std::to_string(m_nextWorkspaceId++);
    ws->path = path;
    ws->requestedPath = path;
    m_workspaces.push_back(std::move(ws));
    return m_workspaces.back().get();
}

void HarnessApp::AssignSessionWorkspace(Session& session, Workspace* workspace) {
    session.workspaceId = workspace ? workspace->id : std::string();
}

std::string HarnessApp::InheritedWorkspacePath() const {
    if (!m_sess) return {};
    const Workspace* ws = WorkspaceById(m_sess->workspaceId);
    if (!ws) return {};
    // ★ 只读 `Workspace::path`（已确认路径），不读 `Session::workspacePath`：
    //   后者在"已发请求、等 ack"的窗口里可能还留着旧值，而在"确认失败"之后
    //   会保留失败前的值——两种情况都不是"这个会话现在在哪"的可靠答案。
    return ws->path;
}

void HarnessApp::NewTemporarySession(const std::string& workspaceId) {
    // 只允许 live 模式（有 core 才能"执行任务"）。
    if (m_options.corePath.empty()) return;
    const Workspace* ws = WorkspaceById(workspaceId);
    if (!ws) return;

    Session* created = CreateSessionFromOptions();
    if (!created) {
        AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
        Relayout();
        RequestRepaint();
        return;
    }
    created->label = "会话 " + std::to_string(m_sessions.size());
    created->temporary = true;
    // 外键在创建时就设好——路径已经确认过了，工作区实体已经存在，
    // 不需要等 ack（与 `MaybeStartWorkspaceSync` 的继承口径不同：
    // 那里是"还没确认"，这里是"已经确认"）。
    created->workspaceId = workspaceId;
    created->workspacePath = ws->path;
    created->workspaceSyncComplete = true;
    created->workspaceStatus = "已由 core 确认";

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);
    // ★★ 2026-10-27：用 `created` 而不是 `m_sessions[idx]`。上一个活动会话若是
    //   没发过 prompt 的临时会话，`SwitchToSession` 里的 `MaybeDeleteTemporarySession`
    //   会把它摘掉 ⇒ 表短一格 ⇒ `m_sessions[idx]` 越界（实测 0xC0000005：
    //   删掉最后一个非临时会话后再点工作区「＋」）。`created` 是新建的那个对象，
    //   被摘掉的是**旧**活动会话，两者不可能是同一个 ⇒ 指针一定有效。
    Session& s = *created;
    if (s.writer) s.writer->Hello("0.0.1");
    if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    m_sidebarDirty = true;
}

void HarnessApp::MaybeDeleteTemporarySession() {
    if (!m_sess || !m_sess->temporary) return;
    // ★ 只删"没发过 prompt"的临时会话：发过 prompt 的已经转正（`PostPrompt`
    //   里把 `temporary` 清掉了）。
    //   ★ 用 `m_activeIndex` 定位：此时 `m_sess` 还指着旧会话（`SwitchToSession`
    //     在改 `m_activeIndex` 之前调用本函数）。
    const size_t idx = m_activeIndex;
    if (idx >= m_sessions.size()) return;
    // 与 `DeleteSessionAt` 同一条纪律：禁删到零会话。
    if (m_sessions.size() <= 1) return;
    // 删除前不需要先切邻居——`SwitchToSession` 马上就会改 `m_activeIndex`。
    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(idx));
    // 修正活动下标（与 `DeleteSessionAt` 同逻辑）。
    if (m_activeIndex > idx) --m_activeIndex;
    if (m_activeIndex >= m_sessions.size()) m_activeIndex = m_sessions.size() - 1;
    m_sess = m_sessions[m_activeIndex].get();
    m_sidebarDirty = true;
    m_hudDirty = true;
}

void HarnessApp::MaybeStartWorkspaceSync() {
    if (!m_sess || !m_sess->writer || m_sess->protocolVer < 3 ||
        !m_sess->workspaceCap || m_sess->workspaceSyncStarted ||
        m_sess->disconnected || m_sess->coreState != "idle") {
        return;
    }
    std::error_code ec;
    // ★★ 2026-10-20（用户裁决 m08553）：新会话**继承活动会话的工作区**。
    //   改动前这里恒用 `std::filesystem::current_path()`，即 GUI 进程的 cwd；
    //   而 `ChildSource::Start` 的 `CreateProcessW` 又不传 `lpCurrentDirectory`
    //   （子进程也继承 GUI cwd）⇒ 用户在会话 A 里把工作区切到 `D:/Project` 之后
    //   点「＋ 新建会话」，会话 B 会**静默**回到启动 GUI 的那个目录。屏幕上看不出
    //   这件事（两行会话长得一样、工作区段显示的是活动会话的值），只有对照
    //   core 的 stderr 才能发现——所以这条不是"体验优化"，是**纠错**。
    std::string initialPath = m_sess->workspaceInheritedPath;
    if (initialPath.empty()) {
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (ec) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "无法读取启动工作目录：" + ec.message();
            RefreshHud();
            return;
        }
        initialPath = PathToUtf8(cwd);
    }
    // ★ 继承路径只消费一次：若这次请求被 core 拒绝，**不**自动改用 cwd 重试。
    //   被拒不等于"路径不存在"（core 可能因自己的原因拒），而擅自换一个目录重问
    //   会让屏幕上的路径与用户以为的那个不同——那正是 §3.9 要避免的"GUI 猜"。
    m_sess->workspaceInheritedPath.clear();
    if (!RequestWorkspace(initialPath, true)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceSyncStarted = true;
            m_sess->workspaceStatus = "启动工作区请求未能发送";
            RefreshHud();
        }
    }
}

void HarnessApp::MaybeSendAutoPrompt() {
    if (!m_sess || m_options.autoPrompt.empty() || m_sess->autoPromptSent ||
        m_options.workspaceProbe) {
        return;
    }
    if (!m_sess->writer) {
        m_sess->autoPromptSent = true;
        SendPrompt(m_options.autoPrompt);
        return;
    }
    if (m_sess->protocolVer < 3 ||
        (m_sess->workspaceCap && !m_sess->workspaceSyncComplete) ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty() ||
        m_sess->disconnected) {
        return;
    }
    m_sess->autoPromptSent = true;
    (void)PostPrompt(m_options.autoPrompt);
}

void HarnessApp::ChooseWorkspaceFolder() {
    if (!m_sess || !m_sess->writer || !m_sess->workspaceCap ||
        m_sess->protocolVer < 3 || m_sess->disconnected ||
        m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        // ★★ P4 修复（实测反馈「直接进去选择文件夹不能选择」）：
        //   ① 首启空壳会话（无 live）此前整段**静默 return**——按钮看起来是死的。
        //     现在如实说原因（同 RequestModelSwitch 的「不是 live」风格）。
        //   ② `protocolVer != 3` 是 P3.6 升 v4 后没更新的拦路守卫——v4 core 的
        //     live 会话同样被静默拦死。workspace 能力 v3 引入、v4 保留 ⇒ 改 `< 3`。
        if (m_sess && !m_sess->writer) {
            AddNoticeRow("[选择工作区需要已连接的 live 会话——先在设置里连接提供商]",
                         kDarkMuted);
            Relayout();
            RequestRepaint();
        }
        return;
    }
    BROWSEINFOW browse{};
    browse.lpszTitle = L"选择工作区文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST item = SHBrowseForFolderW(&browse);
    if (!item) return;
    wchar_t selected[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(item, selected) != FALSE;
    CoTaskMemFree(item);
    if (!ok) {
        m_sess->workspaceStatus = "无法读取所选文件夹";
        RefreshHud();
        return;
    }
    const std::string path = Utf8FromWidePath(selected);
    if (path.empty() || !RequestWorkspace(path, false)) {
        if (!m_sess->disconnected) {
            m_sess->workspaceStatus = "工作区请求未能发送";
            RefreshHud();
        }
    }
}
