// ============================================================
// 历史会话（磁盘 .jsonl 扫描 / 列表 / 打开）与 UI 状态持久化
// ------------------------------------------------------------
// 为什么单独一个文件：这两件事共享同一个前提——**读写用户磁盘**。
//   历史列表靠扫 `<state>/history/*.jsonl` 建行；UI 状态（窗口尺寸、侧栏
//   可见性、最近会话）落在同一个 state 目录。拆开会让「路径怎么拼」
//   在两处各写一遍，于是合成一个文件：一个 TU 内的路径规则只有一份。
// ★ 纪律：历史目录的**文件枚举顺序**必须确定性（按文件名排序）——
//   探针（RunHistoryProbe）按顺序比对行，乱序会变成假回归。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1c）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "Json.h"
#include "ThemeUsing.h"
#include "UiKit.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Core/Logger.h"
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/CollapsiblePanel.h"
#include "ECDI/Widget/ScrollView.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// 跨 TU 共享符号（定义见 AppInternal.h）
using harness::internal::kSidebarListW;
using harness::internal::kSidebarMaxRows;

using harness::internal::kSidebarRowGap;
using harness::internal::kSidebarRowH;
using harness::internal::kSidebarRowW;

std::filesystem::path HarnessApp::HistoryRoot() const {
    namespace fs = std::filesystem;
    // ① 从当前夹具路径往上找：`<ancestor>/protocol/fixtures` 存在就用它。
    //    为什么从夹具路径找而不是从 cwd 找：跑批脚本常常在仓库根跑，cwd 恰好对；
    //    但用户也可能从别处启动、只给一个绝对 `--replay`，此时 cwd 是错的，
    //    而夹具自己的位置永远是对的（它必然在仓库里）。
    std::error_code ec;
    if (!m_options.replayPath.empty()) {
        fs::path p = fs::absolute(m_options.replayPath, ec);
        if (ec) { p = m_options.replayPath; ec.clear(); }
        for (fs::path dir = p.parent_path(); !dir.empty(); dir = dir.parent_path()) {
            const fs::path probe = dir / "protocol" / "fixtures";
            if (fs::is_directory(probe, ec) && !ec) return probe;
            ec.clear();
            if (dir == dir.root_path()) break;
        }
    }
    // ② 退到 cwd 相对路径（开发时最常见的形态）。
    const fs::path cwdProbe = fs::path("protocol") / "fixtures";
    if (fs::is_directory(cwdProbe, ec) && !ec) return fs::absolute(cwdProbe, ec);
    return std::filesystem::path();
}

std::vector<std::filesystem::path> HarnessApp::FindHistoryFiles() const {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const fs::path root = HistoryRoot();
    if (root.empty()) return out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path& p = it->path();
        // ★ 跳过 `__pycache__`：`cot/` 下真实存在（生成器跑过就会有），
        //   它不是夹具、也不该出现在历史里。
        if (p.filename() == "__pycache__") { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (p.extension() != ".jsonl") continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [&root](const fs::path& a, const fs::path& b) {
        return a.lexically_relative(root).generic_string()
             < b.lexically_relative(root).generic_string();
    });
    return out;
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildHistoryRow(const std::filesystem::path& file) {
    // ★ 行根同样是 Button：`Panel::ContainsPoint` 恒 false（`Panel.cpp:34-40`），
    //   可点的行根只能是 Button（同 `BuildSessionRow` 的理由）。
    auto row = std::make_unique<ECDI::Button>();
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = kPanel2,
        .border = kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,
        .foreground = kDarkInk});
    // 显示**相对仓库夹具根的路径**（`realistic/realistic-session.jsonl`），
    // 而不是裸文件名：六份夹具里 `cot-session.jsonl` 与 `demo-session.jsonl`
    // 不重名，但真实使用中同名文件很常见，裸名会让人点错。
    std::string shown = file.filename().generic_string();
    const std::filesystem::path root = HistoryRoot();
    if (!root.empty()) {
        const std::filesystem::path rel = file.lexically_relative(root);
        if (!rel.empty()) shown = rel.generic_string();
    }
    auto label = uikit::Label(shown, kSidebarRowW - 16, 16, kDarkInk, harness::kFontSmall);
    label->SetPosition(8, (kSidebarRowH - 16) / 2);
    // 禁用 ⇒ 不参与命中（点行中心一定命中行根），且 `IsEnabled` 无绘制消费者 ⇒ 视觉零变化。
    label->SetEnabled(false);
    row->AddChild(std::move(label));
    // ★ 回调**只置脏**，真打开留到 tick：本按钮此刻正被派发链持有，
    //   而"打开"会 `RemoveChild` 掉整棵侧栏/转录区（同 `m_sidebarDirty` 的 UAF 理由）。
    row->SetOnClick([this, file] {
        m_historyOpenPending = file;
        m_historyDirty = false;   // 打开会连列表一起重建，不必再单独刷一次
    });
    return row;
}

void HarnessApp::RebuildHistoryList() {
    if (!m_hud.historyList) return;
    if (!m_historyScanned) {
        m_historyFiles = FindHistoryFiles();
        m_historyScanned = true;
    }
    ECDI::Widget& content = m_hud.historyList->GetContentView();
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }
    const size_t n = m_historyFiles.size();
    const size_t rowStride = static_cast<size_t>(kSidebarRowH + kSidebarRowGap);
    int y = 0;
    for (size_t i = 0; i < n; ++i) {
        auto row = BuildHistoryRow(m_historyFiles[i]);
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += static_cast<int>(rowStride);
    }
    const int totalH = n > 0 ? static_cast<int>(n * rowStride) - kSidebarRowGap : 0;
    const int visibleH = std::min(totalH, kSidebarMaxRows * static_cast<int>(rowStride)
                                              - kSidebarRowGap);
    m_hud.historyList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.historyList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.historyList->SetContentOffset(0, m_hud.historyList->GetScrollOffsetY());
    if (m_hud.historyCount) {
        m_hud.historyCount->SetText(std::to_string(n) + " 个历史文件");
    }
    if (ECDI::Widget* parent = m_hud.historyList->GetParent()) parent->Arrange();
}

std::filesystem::path HarnessApp::UiStatePath(const Session& s) const {
    // ★ 与夹具**同目录**、名字加 `.ui.json` 后缀：`demo-session.jsonl` →
    //   `demo-session.jsonl.ui.json`。同目录是为了"夹具搬走，状态跟着走"；
    //   加后缀而不是另建目录，是为了让 `rm *.jsonl` 这种清理动作**不会**顺手删掉状态。
    // ★ 不用 `evals/` 里那种 `<x>.jsonl.json`：那是**评估摘要**（旧产物，来自 `f243579`），
    //   与本状态文件语义完全不同，同名会让两者互相覆盖。
    if (s.originPath.empty()) return std::filesystem::path();
    std::filesystem::path p = s.originPath;
    p += ".ui.json";
    return p;
}

bool HarnessApp::LoadUiState(Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value root;
    std::string error;
    if (!json::Parse(text, root, error)) {
        // 坏状态文件不该让会话打不开：如实记一条日志，按"没有决策记录"继续。
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: " + error + " (" +
                                           path.generic_string() + ")"));
        return false;
    }
    // answers: { "<ask id>": "allow" | "deny" }
    if (const json::Value* answers = root.Find("answers");
        answers && answers->IsObject()) {
        // ★★ P4 D：answerTimes（id → "HH:MM"）是可选键——旧 sidecar 没有 ⇒
        //   answerAt 留空，重建的结论行如实不带时刻段。
        const json::Value* times = root.Find("answerTimes");
        for (const auto& kv : answers->object) {
            if (!kv.second.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Ask) continue;
                if (entry.id != kv.first) continue;
                entry.answered = true;
                entry.answer = kv.second.text;
                if (times && times->IsObject()) {
                    const auto it = times->object.find(kv.first);
                    if (it != times->object.end() && it->second.IsString()) {
                        entry.answerAt = it->second.text;
                    }
                }
            }
        }
    }
    // expanded: ["b1", ...]（只记展开的，收起是默认态）
    if (const json::Value* expanded = root.Find("expanded");
        expanded && expanded->IsArray()) {
        for (const json::Value& id : expanded->array) {
            if (!id.IsString()) continue;
            for (TimelineEntry& entry : s.timeline) {
                if (entry.kind != TimelineEntry::Kind::Block) continue;
                if (entry.id == id.text) entry.expanded = true;
            }
        }
    }
    return true;
}

void HarnessApp::SaveUiState(const Session& s) {
    const std::filesystem::path path = UiStatePath(s);
    if (path.empty()) return;
    json::Value root;
    root.type = json::Value::Type::Object;
    root.object["version"] = json::Value();       // 先占位，下面逐步覆盖
    root.object["version"].type = json::Value::Type::Number;
    root.object["version"].number = 1.0;
    root.object["answers"].type = json::Value::Type::Object;
    root.object["answerTimes"].type = json::Value::Type::Object;
    root.object["expanded"].type = json::Value::Type::Array;
    bool anyAnswerTime = false;
    for (const TimelineEntry& entry : s.timeline) {
        if (entry.kind == TimelineEntry::Kind::Ask && !entry.answer.empty()) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.answer;
            root.object["answers"].object[entry.id] = v;
            if (!entry.answerAt.empty()) {
                json::Value t;
                t.type = json::Value::Type::String;
                t.text = entry.answerAt;
                root.object["answerTimes"].object[entry.id] = t;
                anyAnswerTime = true;
            }
        } else if (entry.kind == TimelineEntry::Kind::Block && entry.expanded) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = entry.id;
            root.object["expanded"].array.push_back(v);
        }
    }
    // ★ 无任何时刻记录（旧状态文件升级后首写）就不落空键——读侧按缺键处理。
    if (!anyAnswerTime) root.object.erase("answerTimes");
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ui-state: 写入失败 " + path.generic_string()));
        return;
    }
    out << root.Pretty(2) << "\n";
}

void HarnessApp::ApplyUiStateToView(Session& s) {
    // ★ 为什么需要这一步（顺序陷阱）：`RebuildTranscriptView` 的第一行是
    //   `SyncRowViewState()`（视图 → 模型）。刚抽干出来的行全是 `expanded=false` /
    //   `answered=false` ⇒ 若只把 sidecar 写进模型就重建，同步会**立刻把刚读进来的
    //   状态覆盖掉**（模型被更旧的视图抹平）。故先把同一批状态也写进视图行。
    for (RowRec& r : s.rows) {
        if (r.timelineIdx >= s.timeline.size()) continue;
        const TimelineEntry& entry = s.timeline[r.timelineIdx];
        if (r.ask && entry.answered) {
            r.answered = true;
            ApplyAnsweredLook(r, entry.answer, entry.answerAt);   // 同一条视觉路径（Step 3）
        }
        if (r.body && entry.expanded) {
            r.expanded = true;
            r.body->SetExpanded(true);
        }
    }
}

bool HarnessApp::OpenHistorySession(const std::filesystem::path& file) {
    std::string error;
    auto replay = std::make_unique<ReplaySource>();
    if (!replay->Load(file, error)) {
        // ★ 如实报错，不建半好会话（半好会话会让"打不开"看起来像"打开了一个空会话"）。
        AddNoticeRow("[打开失败] " + file.filename().generic_string() + " — " + error,
                     kErrorDark);
        Relayout();
        RequestRepaint();
        return false;
    }

    PauseActiveTimedClock();

    auto session = std::make_unique<Session>();
    Session& s = *session;
    s.pendingTruncated = replay->TruncatedError();
    s.replay = replay.get();
    s.source = std::move(replay);
    s.label = file.filename().generic_string();
    // ★★ 来源必须记下来：这是本步新增的第三类会话（live / 启动时 --replay / 历史）。
    //   没有它，后续两个 tick 里就分不清"这个会话该不该读 sidecar"。
    s.originKind = "replay-history";
    s.originPath = file;
    m_sessions.push_back(std::move(session));
    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);   // 内部会 RebuildTranscriptView（此刻还是空的，无妨）

    // ── 跳到末尾：把源抽干（复用 `DrainInstant` 的消费语义）──
    //   ★ 为什么必须走 `HandleLine`（而不是直接读行数）：坏文件的读数
    //     （seqGaps/unknownKinds/protocolIncompatible）全在消费路径上产生。
    std::string line;
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    m_sess->instantDone = true;   // 已抽干 ⇒ 别让 DrainInstant 再跑一遍
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }

    // ── GUI 侧状态（sidecar）──
    const bool hadState = LoadUiState(*m_sess);
    if (hadState) {
        ApplyUiStateToView(*m_sess);
    } else {
        // ★ 缺失时必须**说出来**，不能静默当作"未回答"：Step 3 修的正是
        //   "历史把授权决定抹平成未回答"这条病，换个位置不能复发。
        bool anyAsk = false;
        for (const TimelineEntry& entry : m_sess->timeline) {
            if (entry.kind == TimelineEntry::Kind::Ask) { anyAsk = true; break; }
        }
        if (anyAsk) {
            AddNoticeRow("[此会话没有决策记录：磁盘上没有 " +
                         UiStatePath(*m_sess).filename().generic_string() +
                         "，权限门一律按未回答显示]", kDarkMuted);
        }
    }

    // 抽干后再重建一次：此刻 timeline 才有内容，且 sidecar 已写进模型与视图。
    RebuildTranscriptView();
    Relayout();
    // ★★ 2026-10-26（缺陷修复）：打开历史会话的契约是「直接跳到末尾」（Step 5 注释）。
    //   但 `RebuildTranscriptView` 把 offset 恢复到建会话时的 `prevOffset=0`，随后
    //   `FollowTailIfPinned` 判 `0 >= maxY - 60` 对长会话恒为 false ⇒ 视图停在最顶部、
    //   右下角浮出「↓ 跳到最新」，与「打开即在末尾」相悖。显式滚到底部再刷新跳转钮。
    if (m_scroll) {
        m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY());
    }
    FollowTailIfPinned();
    m_sidebarDirty = true;    // 会话列表要多出一行
    m_hudDirty = true;
    RefreshHud();
    RequestRepaint();
    return true;
}

void HarnessApp::DiscoverAndRestoreJournalSessions() {
    struct DiscoveredSession {
        std::string workspacePath;
        SessionMeta meta;
    };
    std::vector<DiscoveredSession> allSessions;

    // 1. 扫描全部已知工作区
    for (const auto& wsPath : m_appConfig.knownWorkspaces) {
        if (wsPath.empty()) continue;
        std::string err;
        auto metas = SessionStore::ScanSessionMetas(wsPath, err);
        for (auto& m : metas) {
            allSessions.push_back({wsPath, std::move(m)});
        }
    }

    // 2. 按 (createdAt, id) 升序全局排序
    std::sort(allSessions.begin(), allSessions.end(), [](const DiscoveredSession& a, const DiscoveredSession& b) {
        if (a.meta.createdAt != b.meta.createdAt) return a.meta.createdAt < b.meta.createdAt;
        return a.meta.id < b.meta.id;
    });

    // 3. 先按顺序建立全部工作区（保持下标即创建序，确保确定性去重，事实 §9.1）
    for (const auto& ds : allSessions) {
        Workspace* ws = AcquireWorkspace(ds.workspacePath);
        if (ws) {
            std::error_code ec;
            if (!std::filesystem::exists(ds.workspacePath, ec) || !std::filesystem::is_directory(ds.workspacePath, ec)) {
                ws->unavailable = true;
            }
        }
    }
    for (const auto& wsPath : m_appConfig.knownWorkspaces) {
        if (wsPath.empty()) continue;
        Workspace* ws = AcquireWorkspace(wsPath);
        if (ws) {
            std::error_code ec;
            if (!std::filesystem::exists(wsPath, ec) || !std::filesystem::is_directory(wsPath, ec)) {
                ws->unavailable = true;
            }
        }
    }

    // 4. 逐个恢复会话（已存在则跳过，保持幂等性）
    for (const auto& ds : allSessions) {
        auto existing = std::find_if(m_sessions.begin(), m_sessions.end(),
                                     [&](const std::unique_ptr<Session>& s) {
                                         return s && s->sessionId == ds.meta.id;
                                     });
        if (existing != m_sessions.end()) {
            continue;
        }
        const auto journalPath = SessionStore::JournalPath(ds.workspacePath, ds.meta.id);
        std::error_code ec;
        if (!std::filesystem::exists(journalPath, ec)) {
            continue;
        }
        RestoreSingleJournalSession(ds.workspacePath, ds.meta, journalPath);
    }
}

bool HarnessApp::RestoreSingleJournalSession(const std::string& wsPath, const SessionMeta& meta,
                                             const std::filesystem::path& journalPath) {
    std::string error;
    auto replay = std::make_unique<ReplaySource>();
    if (!replay->Load(journalPath, error)) {
        return false;
    }

    auto session = std::make_unique<Session>();
    Session& s = *session;
    s.pendingTruncated = replay->TruncatedError();
    s.replay = replay.get();
    s.source = std::move(replay);
    s.label = meta.label.empty() ? meta.id : meta.label;
    s.sessionId = meta.id;
    s.originKind = "journal";
    s.originPath = journalPath;
    s.meta = meta;
    s.archived = meta.archived;
    s.providerId = meta.providerId;
    s.modelName = meta.modelName;
    s.contextLimit = meta.contextLimit;
    s.workspacePath = wsPath;
    s.journalPromoted = true;
    s.temporary = false;

    Workspace* ws = WorkspaceByPath(wsPath);
    if (ws) {
        AssignSessionWorkspace(s, ws);
    }

    m_sessions.push_back(std::move(session));
    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);

    // 抽干内容
    std::string line;
    while (m_sess->source->Next(line)) {
        HandleLine(line);
        if (m_sess->protocolIncompatible) break;
    }
    m_sess->instantDone = true;
    if (!m_sess->pendingTruncated.empty()) {
        AddNoticeRow(m_sess->pendingTruncated, kErrorDark);
        m_sess->pendingTruncated.clear();
    }

    // 历史转录恢复收尾：闭合上次异常中断遗留的未完结块，复位运行态并对齐 client_id / lastSeq
    m_sess->transcript.FinalizeIncompleteBlocks();
    m_sess->coreState = "idle";
    m_sess->waitForAnswer = false;
    m_sess->nextClientId = std::max(m_sess->nextClientId, m_sess->transcript.MaxClientIdIndex() + 1);
    if (m_sess->transcript.LastSeq() > m_sess->meta.lastSeq) {
        SyncSessionMeta(*m_sess);
    }

    // 恢复 sidecar
    const bool hadState = LoadUiState(*m_sess);
    if (hadState) {
        ApplyUiStateToView(*m_sess);
    }

    // 校验模型是否仍在全局配置中（事实 §9.2）
    const ProviderConfig* pc = m_appConfig.Find(meta.providerId);
    bool modelFound = false;
    if (pc) {
        for (const auto& m : pc->models) {
            if (m.name == meta.modelName) {
                modelFound = true;
                break;
            }
        }
    }
    if (!modelFound && !meta.modelName.empty()) {
        AddNoticeRow("[原模型 " + meta.modelName + " 已不在提供商 " +
                     (meta.providerId.empty() ? "默认" : meta.providerId) +
                     " 的模型表中；本会话按只读显示]", kDarkMuted);
    }

    RebuildTranscriptView();
    if (m_scroll) {
        m_scroll->SetContentOffset(0, m_scroll->GetMaxOffsetY());
    }
    FollowTailIfPinned();
    m_sidebarDirty = true;
    m_hudDirty = true;
    return true;
}
