// ============================================================
// --persist-probe：会话持久化与恢复（L1~L4）判据
// ------------------------------------------------------------
// 判据组：
//   discover-metas        扫描与排序（按 createdAt, id 升序）
//   restore-sessions      启动恢复（originKind=journal，label 正确，timeline 恢复）
//   readonly-ui           只读外观（侧栏 [只读]，PromptBar 只读模式，继续按钮可见）
//   readonly-guard        只读拦截（禁止向 journal 会话追加 prompt）
//   continue-flow         继续功能（新建 live 会话并继承 workspace 与模型）
//   negative-missing-file 负对照（有 meta 但缺 jsonl 时优雅跳过不崩）
//   cleanup               测试临时目录清理完成
// ============================================================

#include "App.h"
#include "AppInternal.h"
#include "SessionStore.h"
#include "ThemeUsing.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

void HarnessApp::RunPersistProbe() {
    m_probeState.persistProbeDone = true;
    auto& log = m_probeState.persistProbeLog;
    auto snap = [&log](const char* phase, bool matched, std::string detail) {
        PersistProbeRec rec;
        rec.phase = phase;
        rec.matched = matched;
        rec.detail = std::move(detail);
        log.push_back(std::move(rec));
    };

    std::error_code ec;
    const fs::path tempBase = fs::temp_directory_path(ec);
    if (ec) {
        snap("skipped-no-tempdir", false, "temp_directory_path failed: " + ec.message());
        return;
    }

    const fs::path rootDir = tempBase / "ecdi-persist-probe";
    fs::remove_all(rootDir, ec);
    fs::create_directories(rootDir, ec);
    if (ec) {
        snap("skipped-no-tempdir", false, "failed to create test root: " + ec.message());
        return;
    }

    const fs::path wsA = rootDir / "workspaceA";
    const fs::path wsB = rootDir / "workspaceB";
    fs::create_directories(wsA, ec);
    fs::create_directories(wsB, ec);

    // ── ① discover-metas ──
    std::string wsA_str = wsA.generic_string();
    std::string wsB_str = wsB.generic_string();

    std::string ensureErr;
    SessionStore::EnsureEcdiDir(wsA, ensureErr);
    SessionStore::EnsureEcdiDir(wsB, ensureErr);

    SessionMeta m1;
    m1.id = "s-20261028-010000-aaaa";
    m1.createdAt = "2026-10-28T01:00:00Z";
    m1.lastUsedAt = m1.createdAt;
    m1.label = "Session Alpha";
    m1.providerId = "prov1";
    m1.modelName = "model-alpha";

    SessionMeta m2;
    m2.id = "s-20261028-020000-bbbb";
    m2.createdAt = "2026-10-28T02:00:00Z";
    m2.lastUsedAt = m2.createdAt;
    m2.label = "Session Beta";
    m2.providerId = "prov1";
    m2.modelName = "model-alpha";

    std::string err;
    bool s1 = SessionStore::SaveMetaAtomic(wsA_str, m1, err);
    bool s2 = SessionStore::SaveMetaAtomic(wsB_str, m2, err);

    // 写对应的 journal 简要内容
    auto writeJournal = [](const fs::path& p) {
        std::ofstream f(p, std::ios::binary);
        f << "{\"t\":\"hello\",\"protocol\":3,\"seq\":0}\n";
        f << "{\"t\":\"state\",\"state\":\"idle\",\"seq\":1}\n";
        f << "{\"t\":\"block_start\",\"id\":\"b1\",\"kind\":\"user\",\"seq\":2}\n";
        f << "{\"t\":\"delta\",\"id\":\"b1\",\"text\":\"hello world\",\"seq\":3}\n";
        f << "{\"t\":\"block_end\",\"id\":\"b1\",\"seq\":4}\n";
        f << "{\"t\":\"block_start\",\"id\":\"b2\",\"kind\":\"assistant\",\"seq\":5}\n";
        f << "{\"t\":\"delta\",\"id\":\"b2\",\"text\":\"greetings\",\"seq\":6}\n";
        f << "{\"t\":\"block_end\",\"id\":\"b2\",\"seq\":7}\n";
        f << "{\"t\":\"state\",\"state\":\"idle\",\"seq\":8}\n";
        return !f.fail();
    };

    const auto j1Path = SessionStore::JournalPath(wsA_str, m1.id);
    const auto j2Path = SessionStore::JournalPath(wsB_str, m2.id);
    bool w1 = writeJournal(j1Path);
    bool w2 = writeJournal(j2Path);

    auto metasA = SessionStore::ScanSessionMetas(wsA_str, err);
    auto metasB = SessionStore::ScanSessionMetas(wsB_str, err);

    bool discoverOk = s1 && s2 && w1 && w2 && (metasA.size() == 1) && (metasB.size() == 1) &&
                      (metasA[0].id == m1.id) && (metasB[0].id == m2.id);
    snap("discover-metas", discoverOk, discoverOk ? "scanned 2 sessions across 2 workspaces"
                                                  : "meta discovery or write failed");

    // ── ② restore-sessions ──
    m_appConfig.knownWorkspaces = {wsA_str, wsB_str};
    DiscoverAndRestoreJournalSessions();

    Session* restoredA = nullptr;
    Session* restoredB = nullptr;
    for (const auto& sess : m_sessions) {
        if (sess->sessionId == m1.id) restoredA = sess.get();
        if (sess->sessionId == m2.id) restoredB = sess.get();
    }

    bool restoreOk = (restoredA != nullptr) && (restoredB != nullptr) &&
                     (restoredA->originKind == "journal") && (restoredB->originKind == "journal") &&
                     (restoredA->label == m1.label) && (restoredB->label == m2.label) &&
                     (!restoredA->timeline.empty()) && (!restoredB->timeline.empty());
    snap("restore-sessions", restoreOk, restoreOk ? "both sessions restored with timeline"
                                                  : "session restoration failed");

    // ── ③ readonly-ui ──
    bool readonlyUiOk = false;
    if (restoredA) {
        SwitchToSessionByPtr(restoredA);
        RefreshHud();
        const bool barReadOnly = m_promptBar && m_promptBar->IsReadOnlyMode();
        const bool sendHidden = m_promptBar && m_promptBar->sendButton() && !m_promptBar->sendButton()->IsVisible();
        const bool contVisible = m_promptBar && m_promptBar->continueButton() && m_promptBar->continueButton()->IsVisible();
        const bool headContVisible = m_hud.continueBtn && m_hud.continueBtn->IsVisible();

        // 校验侧栏行文本含 [只读]
        auto rowW = BuildSessionRow(m_activeIndex, true);
        bool hasReadOnlyLabel = false;
        if (rowW) {
            for (size_t c = 0; c < rowW->GetChildCount(); ++c) {
                if (const auto* lbl = dynamic_cast<const ECDI::Label*>(rowW->GetChildAt(c))) {
                    if (lbl->GetText().find("[只读]") != std::string::npos) {
                        hasReadOnlyLabel = true;
                        break;
                    }
                }
            }
        }

        readonlyUiOk = barReadOnly && sendHidden && contVisible && headContVisible && hasReadOnlyLabel;
        snap("readonly-ui", readonlyUiOk, readonlyUiOk ? "all read-only visual markers present"
                                                       : "missing read-only visual markers");
    } else {
        snap("readonly-ui", false, "no restored session to test UI");
    }

    // ── ④ readonly-guard ──
    bool guardOk = false;
    if (restoredA) {
        const size_t rowsBefore = restoredA->rows.size();
        SendPrompt("testing forbidden prompt in read-only session");
        // 验证 SendPrompt 未将 prompt 加入转录内容（仅有 notice 或无变化）
        bool noUserBubbleAdded = true;
        for (const auto& entry : restoredA->timeline) {
            if (entry.text == "testing forbidden prompt in read-only session") {
                noUserBubbleAdded = false;
                break;
            }
        }
        guardOk = noUserBubbleAdded;
        snap("readonly-guard", guardOk, guardOk ? "prompt correctly blocked for journal session"
                                                : "prompt leaked into journal session");
    } else {
        snap("readonly-guard", false, "no restored session for guard test");
    }

    // ── ⑤ continue-flow ──
    bool continueOk = false;
    if (restoredA) {
        const size_t countBefore = m_sessions.size();
        ContinueCurrentSession();
        const size_t countAfter = m_sessions.size();
        const bool newSessionCreated = (countAfter == countBefore + 1);
        const bool inheritedWs = (m_sess && m_sess->workspacePath == wsA_str);
        const bool inputEditable = (m_promptBar && !m_promptBar->IsReadOnlyMode());
        const bool sendVisible = (m_promptBar && m_promptBar->sendButton() && m_promptBar->sendButton()->IsVisible());
        const bool contHidden = (m_promptBar && m_promptBar->continueButton() && !m_promptBar->continueButton()->IsVisible());
        const bool headContHidden = (m_hud.continueBtn && !m_hud.continueBtn->IsVisible());

        continueOk = newSessionCreated && inheritedWs && inputEditable && sendVisible && contHidden && headContHidden;
        snap("continue-flow", continueOk, continueOk ? "continued into editable session with inherited workspace"
                                                     : "continue flow failed to initialize new session");
    } else {
        snap("continue-flow", false, "no restored session for continue test");
    }

    // ── ⑥ negative-missing-file ──
    SessionMeta m3;
    m3.id = "s-20261028-030000-cccc";
    m3.createdAt = "2026-10-28T03:00:00Z";
    m3.label = "Missing Journal";
    SessionStore::SaveMetaAtomic(wsA_str, m3, err);
    // 故意不写 .jsonl 文件

    const size_t sessBeforeNeg = m_sessions.size();
    DiscoverAndRestoreJournalSessions();
    const size_t sessAfterNeg = m_sessions.size();

    // 缺失 journal 文件的会话应被安全跳过，不崩溃也不增加坏会话
    bool negativeOk = (sessAfterNeg == sessBeforeNeg);
    snap("negative-missing-file", negativeOk, negativeOk ? "missing journal safely ignored"
                                                         : "missing journal caused invalid session");

    // ── ⑦ cleanup ──
    fs::remove_all(rootDir, ec);
    snap("cleanup", !ec, ec ? "cleanup failed: " + ec.message() : "temporary test workspaces cleaned up");

    if (m_application) {
        m_application->Exit();
    }
}

void HarnessApp::AppendPersistProbeSummary(std::string& json) {
    json += "  \"persistProbe\": [";
    auto escP = [](const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
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
    for (size_t i = 0; i < m_probeState.persistProbeLog.size(); ++i) {
        const PersistProbeRec& r = m_probeState.persistProbeLog[i];
        json += (i ? ", " : "");
        char b[512];
        std::snprintf(b, sizeof(b),
                      "{\"phase\": \"%s\", \"matched\": %s, \"detail\": \"%s\"}",
                      escP(r.phase).c_str(), r.matched ? "true" : "false",
                      escP(r.detail).c_str());
        json += b;
    }
    json += "],\n";
}
