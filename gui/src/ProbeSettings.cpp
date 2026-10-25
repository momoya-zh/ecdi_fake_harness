// ============================================================
// 配置与工作区探针：设置面板保存链 / 凭据注入 / 工作区协商
// ------------------------------------------------------------
// 为什么从 ProbeFlow 再切出来：这两个探针是**唯一会碰磁盘与进程环境**的
//   （写配置文件、读凭据库、设 env、拉真子进程），和 Enter/Grow/Jump/Key
//   那四个纯 UI 交互探针混在一个文件里，改一个要滚过另一个的全部仪器代码。
//   切完两个文件都在 1200 行以内。
// ★ 2026-10-25：从 ProbeFlow.cpp 切出（Stage 1a）。函数体逐字节相同。
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

// ★★ 2026-10-21：HUD 覆盖探针（--hud-probe）。
//
// 【它要回答什么】`Hud` 里那些指针，**建树时真的都赋过值吗**？
//   这一类缺陷的形状是"读了但从不写"：`RefreshHud` 里
//   `if (m_hud.X) { ...SetText(...) }` 看着像是"有就更新"，而建树时没赋值
//   就永远是假分支——代码正确、屏幕空白、**所有既有读数都看不见**。
//   实测到三个实例：`backendChip`（屏幕上从来没有）、`sessSession` /
//   `sessCore`（Step 1 之后不再建）。三个都已处理，但那只是修实例。
//
// 【判据】`hud-built`：把审计打开，跑一次 `RefreshHud`，然后要求
//   **"被读过但从未被赋值"的字段集合为空**。
//   失败时把字段名原样写出来——"少了哪个"必须能直接读到，否则这条判据
//   只会告诉你有问题，不告诉你问题在哪。
//
// 【负对照 `hud-built-negative`】光有一条"集合为空"证明不了判据有判别力：
//   一个永远返回空集合的实现同样满足它。所以再跑一相：**人为把一个指针置空、
//   再跑 `RefreshHud`**，此时集合必须**非空**且包含那个字段名。
//   若这一相也返回空，说明判据是瞎的（`--sidebar-probe` 的逆序负对照同族）。
// ★★ P3.5（--settings-probe）：设置面板 + 凭据/配置/env 注入判据。
//   前置：`--config <temp>/probe-config.json`（**指向不存在的文件**）且**无**
//   `--core`/`--replay` ⇒ 应用必然从首启分支进来（设置面板可见）。
//   跨 tick：save 相之后 hello 要下一 tick 才从队列里出来（与 theme-probe 同因）。
//   ★ 相位设计见规格 §8.5.4；负对照在 .scratch/probe_settings_negctl.py。
void HarnessApp::RunSettingsProbe() {
    auto report = [this](const char* phase, const std::string& bad) {
        SettingsProbeRec rec;
        rec.phase = phase;
        rec.matched = bad.empty();
        rec.firstDivergence = bad;
        m_probeState.settingsProbeLog.push_back(std::move(rec));
    };
    const auto now = std::chrono::steady_clock::now();
    const auto settle = std::chrono::milliseconds(400);

    switch (m_probeState.settingsProbeStage) {
    case 0: {
        // ① first-run：面板必须可见、scroll/composer 必须腾了槽、字段必须为空。
        {
            std::string bad;
            if (!m_hud.settingsPanel || !m_hud.settingsPanel->IsVisible()) {
                bad = "settings panel not visible on first run (config file absent)";
            }
            // ★ 几何判据：IsVisible() 只是标志——0×0 的面板"可见"但画不出任何东西
            //   （2026-10-25 用户实测：首启中栏全空白）。宽度/高度必须同时回读。
            if (m_hud.settingsPanel &&
                (m_hud.settingsPanel->GetWidth() < 400 ||
                 m_hud.settingsPanel->GetHeight() < 300)) {
                bad = bad.empty()
                          ? "settings panel has degenerate geometry (" +
                                std::to_string(m_hud.settingsPanel->GetWidth()) + "x" +
                                std::to_string(m_hud.settingsPanel->GetHeight()) +
                                ") -- invisible despite IsVisible()"
                          : bad + "; panel degenerate geometry";
            }
            // ★ 独立判（不能写成 !bad.empty() && ——那会让"面板默认可见"的
            //   实现连带着跳过本条，投影缺失（M4）就永远不可观测）。
            // ★★ P4 F 判据迁移（行为变化点，方案 §8 验收明写）：设置=模态浮层
            //   ——转录与 composer **必须仍可见**（流式在遮罩后继续），遮罩可见。
            if (m_scroll && !m_scroll->IsVisible()) {
                bad = bad.empty()
                          ? "scroll must stay visible under the modal mask (P4 F)"
                          : bad + "; scroll hidden under settings";
            }
            if (m_promptBar && !m_promptBar->IsVisible()) {
                bad = bad.empty() ? "composer must stay visible (P4 F)"
                                  : bad + "; composer hidden under settings";
            }
            if (m_hud.settingsMask && !m_hud.settingsMask->IsVisible()) {
                bad = bad.empty() ? "modal mask not visible on first run"
                                  : bad + "; mask not visible";
            }
            if (bad.empty() &&
                (!m_hud.settingsBase || !m_hud.settingsBase->GetText().empty() ||
                 !m_hud.settingsKey || !m_hud.settingsKey->GetText().empty())) {
                bad = "first-run fields must start empty (no prefill of secrets)";
            }
            report("first-run", bad);
        }
        // ② save：真填字段（SetText）+ 真点保存（SynthesizeClickAt 走派发链）。
        //    探针默认值是假端点 + 假 key（offline 判据不碰真凭据）；
        //    `--settings-url/--settings-key/--settings-model` 可覆盖（真提供商实测用）。
        const std::string url = m_options.settingsUrl.empty()
                                    ? "https://probe-settings.invalid/v1"
                                    : m_options.settingsUrl;
        m_probeState.settingsProbeKey = m_options.settingsKey.empty()
                                 ? "sk-probe-settings-not-a-real-key"
                                 : m_options.settingsKey;
        const std::string model = m_options.settingsModel.empty()
                                      ? "probe-model"
                                      : m_options.settingsModel;
        m_hud.settingsBase->SetText(url);
        m_hud.settingsKey->SetText(m_probeState.settingsProbeKey);
        m_hud.settingsModel->SetText(model);
        const ECDI::Point abs = m_hud.settingsSave->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.settingsSave->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.settingsSave->GetHeight() / 2);
        // ③ 同 tick 内判据：配置文件落盘（含 BaseURL/模型，**不含 key**）+ 凭据库
        //    真有值。save 回调是同步的（spawn 也在其中），此刻全都该已发生。
        {
            std::string bad;
            std::ifstream in(AppConfig::DefaultPath(), std::ios::binary);
            if (!in) {
                bad = "config file was not written by save";
            } else {
                std::ostringstream ss;
                ss << in.rdbuf();
                const std::string text = ss.str();
                if (text.find(m_probeState.settingsProbeKey) != std::string::npos) {
                    bad = "LEAK: the API key text appears in the config file";
                }
                if (text.find(url) == std::string::npos ||
                    text.find(model) == std::string::npos) {
                    bad += std::string(bad.empty() ? "" : "; ") +
                           "config is missing baseUrl/model";
                }
                if (text.find("apiKey") != std::string::npos ||
                    text.find("api_key") != std::string::npos ||
                    text.find("secret") != std::string::npos) {
                    bad += std::string(bad.empty() ? "" : "; ") +
                           "config carries a secret-named field (structural leak)";
                }
            }
            std::string credError;
            std::string roundtrip;
            const std::string target = AppConfig::CredentialTargetFor(
                AppConfig::ProviderIdFromBaseUrl(url));
            if (!creds::Read(ECDI::UTF8ToWide(target), roundtrip, credError)) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "credential roundtrip failed: " + credError;
            } else if (roundtrip != m_probeState.settingsProbeKey) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "credential roundtrip value mismatch";
            }
            report("save", bad);
        }
        m_probeState.settingsProbeStage = 1;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 1: {
        // ④ session-live：save 建的 live 会话在上一 tick 拉起，hello 应已入列。
        //   ★ 轮询而非一锤定音（2026-10-24 实测：固定 400ms 窗口在新 exe 首启
        //     偶发 LinesReceived==0 —— 进程冷启动抖动，不是产品缺陷）；
        //     5s 宽限期内每 tick 回来看，超时才判红。
        {
            std::string bad;
            // 轮询门：save 后 400ms 起判；5s 内 lines==0 ⇒ 等下一 tick（不写报告）
            if (m_sess && m_sess->originKind == "live" && m_sess->child &&
                m_sess->child->LinesReceived() == 0 &&
                now < m_probeState.settingsProbeNextAt + std::chrono::seconds(5)) {
                return;
            }
            if (!m_sess || m_sess->originKind != "live") {
                bad = "active session is not live after save (sessions=" +
                      std::to_string(m_sessions.size()) +
                      ", hasSess=" + (m_sess ? "yes" : "no") +
                      ", originKind=" + (m_sess ? m_sess->originKind : "<null>") +
                      ", hint=" + (m_hud.settingsHint ? m_hud.settingsHint->GetText() : "<null>") +
                      ")";
            } else if (!m_sess->child || m_sess->child->LinesReceived() == 0) {
                ChildSource* c = m_sess->child;
                bad = "core produced no lines after save (spawn or handshake broken)";
                if (c) {
                    bad += " [child: lines=" + std::to_string(c->LinesReceived());
                    bad += " exited=" + std::string(c->Exited() ? "yes" : "no");
                    bad += " exitCode=" + std::to_string(c->ExitCode());
                    bad += " wakePosts=" + std::to_string(c->WakePosts());
                    bad += " writer=" + std::string(m_sess->writer ? "yes" : "no");
                    bad += " protocolVer=" + std::to_string(m_sess->protocolVer) + "]";
                }
            }
            report("session-live", bad);
        }
        // ⑤ env-check：**启动环境层**的正负对照——
        //   正对照：SetEnvironmentVariableW 注入 ⇒ ecdi-core 应发出 hello（启动即发，不碰 API）；
        //   负对照：清掉变量 ⇒ 无 key 的 core 应即退（exit 2），hello 永不到来。
        //   ★ 两条一起才能排除两个退化情形：「变量没真的进环境」（正对照红）与
        //     「GUI 进程环境本来就带着 key」（负对照红——跑批环境不得预置该变量）。
        //   ★ 结束时按 m_coreApiKey 恢复现场（探针不许改产品状态）。
        {
            std::string bad;
            std::filesystem::path coreExe;
            if (!ResolveCoreExecutable(coreExe)) {
                bad = "no ecdi-core.exe found for env-check";
            } else {
                const std::string coreArgs =
                    "--base-url=https://probe-settings.invalid --model=probe-model --quiet";
                // 正对照
                SetEnvironmentVariableW(L"ECDI_CORE_KEY", L"probe-env-check-value");
                {
                    ChildSource probe;
                    std::string err;
                    if (probe.Start(coreExe, coreArgs, err)) {
                        std::string line;
                        bool gotHello = false;
                        for (int i = 0; i < 100 && !gotHello; ++i) {
                            if (probe.NextWithin(line, 100)) {
                                if (line.find("\"t\":\"hello\"") != std::string::npos) {
                                    gotHello = true;
                                }
                            } else if (probe.Eof()) {
                                break;
                            }
                        }
                        probe.Stop(800);
                        if (!gotHello) {
                            bad = "core spawned with injected key but no hello arrived";
                        }
                    } else {
                        bad = "env-check spawn failed: " + err;
                    }
                }
                // 负对照
                SetEnvironmentVariableW(L"ECDI_CORE_KEY", nullptr);
                {
                    ChildSource probe;
                    std::string err;
                    if (probe.Start(coreExe, coreArgs, err)) {
                        std::string line;
                        bool gotHello = false;
                        for (int i = 0; i < 100 && !gotHello; ++i) {
                            if (probe.NextWithin(line, 100)) {
                                if (line.find("\"t\":\"hello\"") != std::string::npos) {
                                    gotHello = true;
                                }
                            } else if (probe.Eof()) {
                                break;
                            }
                        }
                        probe.Stop(800);
                        if (gotHello) {
                            bad += std::string(bad.empty() ? "" : "; ") +
                                   "negative control: hello arrived WITHOUT the injected "
                                   "variable (parent environment leaks ECDI_CORE_KEY?)";
                        }
                    } else {
                        bad += std::string(bad.empty() ? "" : "; ") +
                               "negative-control spawn failed: " + err;
                    }
                }
                // 恢复现场
                if (!m_coreApiKey.empty()) {
                    SetEnvironmentVariableW(L"ECDI_CORE_KEY",
                                            ECDI::UTF8ToWide(m_coreApiKey).c_str());
                } else {
                    SetEnvironmentVariableW(L"ECDI_CORE_KEY", nullptr);
                }
            }
            report("env-check", bad);
        }
        // ⑤b switch-ok：向**当前**提供商发 model 切换（同 id）⇒ ack ok:true，
        //    providerId 不变。验收的是 v4 消息回环（请求→core→ack→GUI 状态）。
        {
            std::string bad;
            if (!m_sess || !m_sess->hasModelCap) {
                bad = "live session lacks model cap (protocol 4 handshake broken)";
            } else {
                SendModelSwitch(m_sess->providerId);
            }
            report("switch-ok", bad);
        }
        m_probeState.settingsProbeStage = 2;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 2: {
        // ⑤c switch-ok 的确认判定（5s 宽限轮询，同 session-live）+
        //    switch-bad（未知提供商 ⇒ core 应回 ok:false）。
        {
            std::string bad;
            if (!m_sess->pendingModelId.empty()) {
                if (now < m_probeState.settingsProbeNextAt + std::chrono::seconds(5)) {
                    return;
                }
                bad = "model ack never arrived for same-provider switch";
            }
            report("switch-ok", bad);
        }
        SendModelSwitch("unknown-host.invalid");
        m_probeState.settingsProbeStage = 3;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 3: {
        // ⑤d switch-bad 判定：ok:false ⇒ 通知「模型切换失败」；在途 id 已清。
        {
            std::string bad;
            if (!m_sess->pendingModelId.empty()) {
                if (now < m_probeState.settingsProbeNextAt + std::chrono::seconds(5)) {
                    return;
                }
                bad = "model ack never arrived for unknown-provider switch";
            }
            report("switch-bad", bad);
        }
        // ⑥ cleanup：删探针建的凭据与配置文件（幂等）。
        {
            std::string bad;
            const std::string target = AppConfig::CredentialTargetFor(
                AppConfig::ProviderIdFromBaseUrl(
                    m_options.settingsUrl.empty()
                        ? "https://probe-settings.invalid/v1"
                        : m_options.settingsUrl));
            std::string credError;
            if (!creds::Delete(ECDI::UTF8ToWide(target), credError)) {
                bad = "credential cleanup failed: " + credError;
            }
            std::error_code ec;
            std::filesystem::remove(AppConfig::DefaultPath(), ec);
            report("cleanup", bad);
        }
        m_probeState.settingsProbeDone = true;
        if (m_application) m_application->Exit();
        return;
    }
    default:
        m_probeState.settingsProbeDone = true;
            if (m_application) m_application->Exit();
        return;
    }
}

void HarnessApp::RunWorkspaceProbe() {
    if (!m_options.workspaceProbe || m_probeState.workspaceProbeDone || !m_sess) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_probeState.workspaceProbeDeadline == std::chrono::steady_clock::time_point{}) {
        m_probeState.workspaceProbeDeadline = now + std::chrono::seconds(80);
    }
    auto sendEnabled = [&]() {
        return m_promptBar && m_promptBar->sendButton() && m_promptBar->sendButton()->IsEnabled();
    };
    auto canonicalCount = [&](const std::string& clientId) {
        size_t count = 0;
        for (const TimelineEntry& entry : m_sess->timeline) {
            if (entry.kind != TimelineEntry::Kind::Block) continue;
            const Block* block = m_sess->transcript.FindBlock(entry.id);
            if (block && block->kind == "user" && block->clientId == clientId) ++count;
        }
        return count;
    };
    auto append = [&](const std::string& phase, const std::string& clientId,
                      size_t matches, bool matched, bool expectedMismatch) {
        WorkspaceProbeRec rec;
        rec.phase = phase;
        rec.clientId = clientId;
        rec.path = m_sess->workspacePath;
        rec.pendingId = m_sess->pendingWorkspaceId;
        rec.canonicalMatches = matches;
        rec.sendEnabled = sendEnabled();
        rec.matched = matched;
        rec.expectedMismatch = expectedMismatch;
        rec.bubbleText = m_probeState.workspaceProbeBubbleText;
        rec.bubbleShapeOk = m_probeState.workspaceProbeBubbleShapeOk;
        m_probeState.workspaceProbeLog.push_back(std::move(rec));
    };
    auto stopFailed = [&](const std::string& phase, const std::string& clientId, size_t matches) {
        append(phase, clientId, matches, false, false);
        m_probeState.workspaceProbeDone = true;
        if (m_application) m_application->Exit();
    };
    auto equivalent = [](const std::string& path, const std::filesystem::path& expected) {
        if (path.empty()) return false;
        std::error_code ec;
        const bool same = std::filesystem::equivalent(std::filesystem::path(WideFromUtf8Path(path)), expected, ec);
        return same && !ec;
    };
    // ★★ 协议 §7 判据：待确认气泡「与已确认 `user` 气泡同位置同形状，只多一个弱化后缀」。
    //   “同形状”的可测形式 = **去掉后缀必须逐字等于正文**，且**不含换行**——旧实现的
    //   `\n未确认 · 等待 core 接受` 正是靠换行改变气泡高度，违的也是这一条。
    //   ★ 读的是控件（`row.label->GetText()`），不是模型：模型里从来不存后缀，
    //     只对模型下断言等于自证（与 Step 3 的 ReadBack 纪律同族）。
    //   ★ 描边颜色读不回来（ECDI::Panel 无 style getter），所以那一半只能靠截图取证；
    //     这里只钉住文案与形状。
    auto bubbleProbe = [&](const std::string& clientId) {
        m_probeState.workspaceProbeBubbleText.clear();
        m_probeState.workspaceProbeBubbleShapeOk = false;
        const auto view = std::find_if(m_pendingPromptViews.begin(), m_pendingPromptViews.end(),
            [&](const PendingPromptView& v) { return v.clientId == clientId; });
        if (view == m_pendingPromptViews.end() || !view->row.label) return;
        const std::string text = view->row.label->GetText();
        m_probeState.workspaceProbeBubbleText = text;
        const auto model = std::find_if(m_sess->pendingPrompts.begin(), m_sess->pendingPrompts.end(),
            [&](const PendingPrompt& p) { return p.clientId == clientId; });
        if (model == m_sess->pendingPrompts.end()) return;
        const std::string suffix = model->status == "sending" ? " \u00b7 \u53d1\u9001\u4e2d"
                                                             : " \u00b7 \u672a\u786e\u8ba4";
        m_probeState.workspaceProbeBubbleShapeOk =
            text.find('\n') == std::string::npos &&
            text.size() >= suffix.size() &&
            text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0 &&
            text == model->text + suffix;
    };

    if (now >= m_probeState.workspaceProbeDeadline) {
        stopFailed("timeout", m_probeState.workspaceProbeRejectedId, 0);
        return;
    }
    if (!m_sess->writer) {
        stopFailed("unavailable", {}, 0);
        return;
    }
    if (m_sess->protocolIncompatible ||
        (m_sess->disconnected && m_probeState.workspaceProbeStep != 11 && m_probeState.workspaceProbeStep != 12)) {
        stopFailed("disconnected", {}, 0);
        return;
    }

    switch (m_probeState.workspaceProbeStep) {
        case 0: {
            if (!m_sess->workspaceSyncComplete || !m_sess->pendingWorkspaceId.empty()) return;
            std::error_code ec;
            const std::filesystem::path cwd = std::filesystem::current_path(ec);
            if (ec) {
                stopFailed("baseline", {}, 0);
                return;
            }
            m_probeState.workspaceProbeOriginalPath = m_sess->workspacePath;
            const bool baselineOk = equivalent(m_sess->workspacePath, cwd);
            append("baseline", {}, 0, baselineOk, false);
            if (!baselineOk) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            std::filesystem::path target = cwd.parent_path();
            if (target.empty() || target == cwd) target = std::filesystem::temp_directory_path(ec);
            if (ec || target.empty() || target == cwd ||
                !std::filesystem::is_directory(target, ec) || ec) {
                stopFailed("pick-ok", {}, 0);
                return;
            }
            m_probeState.workspaceProbeTargetPath = PathToUtf8(target);
            const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
            if (ec || temp.empty()) {
                stopFailed("pick-fail", {}, 0);
                return;
            }
            m_probeState.workspaceProbeFailPath = PathToUtf8(
                temp / ("dsh-v3-workspace-missing-" + std::to_string(now.time_since_epoch().count())));
            m_probeState.workspaceProbeStep = 1;
            return;
        }
        case 1: {
            const bool sent = RequestWorkspace(m_probeState.workspaceProbeTargetPath, false);
            m_probeState.workspaceProbeWorkspaceId = m_sess->pendingWorkspaceId;
            const bool locked = sent && !m_probeState.workspaceProbeWorkspaceId.empty() && !sendEnabled() &&
                                m_sess->workspacePath == m_probeState.workspaceProbeOriginalPath;
            append("pending-lock", {}, 0, locked, false);
            if (!locked) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 2;
            return;
        }
        case 2: {
            if (!m_sess->pendingWorkspaceId.empty()) return;
            const bool ok = m_sess->workspaceStatus == "已由 core 确认" &&
                            equivalent(m_sess->workspacePath,
                                       std::filesystem::path(WideFromUtf8Path(m_probeState.workspaceProbeTargetPath)));
            append("pick-ok", {}, 0, ok, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 3;
            return;
        }
        case 3: {
            if (!RequestWorkspace(m_probeState.workspaceProbeFailPath, false)) {
                stopFailed("pick-fail", {}, 0);
                return;
            }
            m_probeState.workspaceProbeWorkspaceId = m_sess->pendingWorkspaceId;
            m_probeState.workspaceProbeStep = 4;
            return;
        }
        case 4: {
            if (!m_sess->pendingWorkspaceId.empty()) return;
            const bool pathPreserved = equivalent(m_sess->workspacePath,
                                                  std::filesystem::path(WideFromUtf8Path(m_probeState.workspaceProbeTargetPath)));
            const bool failedAck = m_sess->workspaceStatus.find("切换失败：") == 0;
            const bool ok = pathPreserved && failedAck && sendEnabled();
            append("pick-fail", {}, 0, ok, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 5;
            return;
        }
        case 5: {
            m_probeState.workspaceProbeAcceptedId = PostPrompt("workspace probe accepted bubble");
            if (m_probeState.workspaceProbeAcceptedId.empty()) {
                stopFailed("bubble-flying", {}, 0);
                return;
            }
            const size_t matches = canonicalCount(m_probeState.workspaceProbeAcceptedId);
            const bool pending = std::any_of(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_probeState.workspaceProbeAcceptedId && p.status == "sending";
                });
            const auto transientView = std::find_if(m_pendingPromptViews.begin(),
                m_pendingPromptViews.end(), [&](const PendingPromptView& view) {
                    return view.clientId == m_probeState.workspaceProbeAcceptedId;
                });
            const bool rendered = transientView != m_pendingPromptViews.end() &&
                                  transientView->row.panel && transientView->row.label;
            bubbleProbe(m_probeState.workspaceProbeAcceptedId);
            const bool ok = pending && rendered && matches == 0 && !sendEnabled() &&
                            m_probeState.workspaceProbeBubbleShapeOk;
            append("bubble-flying", m_probeState.workspaceProbeAcceptedId, matches, ok, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 6;
            return;
        }
        case 6: {
            const size_t matches = canonicalCount(m_probeState.workspaceProbeAcceptedId);
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_probeState.workspaceProbeAcceptedId;
                });
            if (pending != m_sess->pendingPrompts.end() && pending->status == "failed") {
                stopFailed("bubble-confirmed", m_probeState.workspaceProbeAcceptedId, matches);
                return;
            }
            if (matches == 0 || pending != m_sess->pendingPrompts.end()) return;
            const bool ok = matches == 1;
            // 已确认后临时气泡应当**已不存在**：这里如实记为 shapeOk=false / 空文案。
            bubbleProbe(m_probeState.workspaceProbeAcceptedId);
            append("bubble-confirmed", m_probeState.workspaceProbeAcceptedId, matches, ok, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 7;
            return;
        }
        case 7: {
            if (m_sess->coreState != "running") return;
            m_probeState.workspaceProbeRejectedId = PostPrompt("workspace probe correlated rejection", true);
            if (m_probeState.workspaceProbeRejectedId.empty()) {
                stopFailed("bubble-failed", {}, 0);
                return;
            }
            m_probeState.workspaceProbeStep = 8;
            return;
        }
        case 8: {
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_probeState.workspaceProbeRejectedId;
                });
            const size_t matches = canonicalCount(m_probeState.workspaceProbeRejectedId);
            if (pending == m_sess->pendingPrompts.end() || pending->status == "sending") {
                if (matches == 0) return;
                stopFailed("bubble-failed", m_probeState.workspaceProbeRejectedId, matches);
                return;
            }
            const bool ok = pending->status == "failed" && matches == 0;
            bubbleProbe(m_probeState.workspaceProbeRejectedId);
            const bool shaped = m_probeState.workspaceProbeBubbleShapeOk;
            append("bubble-failed", m_probeState.workspaceProbeRejectedId, matches, ok && shaped, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 9;
            return;
        }
        case 9: {
            // Start a real turn so the following forced prompt is queued while core is busy.
            m_probeState.workspaceProbeAcceptedId = PostPrompt("workspace probe disconnect trigger");
            if (m_probeState.workspaceProbeAcceptedId.empty()) {
                stopFailed("disconnect-trigger", {}, 0);
                return;
            }
            m_probeState.workspaceProbeStep = 10;
            return;
        }
        case 10: {
            if (m_sess->coreState != "running" || canonicalCount(m_probeState.workspaceProbeAcceptedId) != 1) return;
            m_probeState.workspaceProbeRejectedId = PostPrompt("workspace probe disconnect before ack", true);
            if (m_probeState.workspaceProbeRejectedId.empty() || !m_sess->child) {
                stopFailed("disconnect-unconfirmed", m_probeState.workspaceProbeRejectedId, 0);
                return;
            }
            const size_t matches = canonicalCount(m_probeState.workspaceProbeRejectedId);
            const bool pending = std::any_of(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_probeState.workspaceProbeRejectedId && p.status == "sending";
                });
            if (!pending || matches != 0) {
                stopFailed("disconnect-unconfirmed", m_probeState.workspaceProbeRejectedId, matches);
                return;
            }
            m_probeState.workspaceProbeStep = 11;
            m_sess->child->Stop(0);  // deterministic EOF before the busy core can answer the queued prompt.
            return;
        }
        case 11: {
            if (!m_sess->disconnected) return;
            const auto pending = std::find_if(m_sess->pendingPrompts.begin(),
                m_sess->pendingPrompts.end(), [&](const PendingPrompt& p) {
                    return p.clientId == m_probeState.workspaceProbeRejectedId;
                });
            const auto view = std::find_if(m_pendingPromptViews.begin(),
                m_pendingPromptViews.end(), [&](const PendingPromptView& item) {
                    return item.clientId == m_probeState.workspaceProbeRejectedId;
                });
            const size_t matches = canonicalCount(m_probeState.workspaceProbeRejectedId);
            const bool failedView = view != m_pendingPromptViews.end() &&
                                    view->row.panel && view->row.label;
            const bool ok = pending != m_sess->pendingPrompts.end() &&
                            pending->status == "failed" && matches == 0 && failedView &&
                            !sendEnabled();
            // ★ 这一相用的是**新的** clientId（step 10 重新赋了值），所以必须重新读
            //   控件；不重读就会把上一相 p2 的文案当成 p4 的读数（实测过）。
            bubbleProbe(m_probeState.workspaceProbeRejectedId);
            const bool shaped = m_probeState.workspaceProbeBubbleShapeOk;
            append("disconnect-unconfirmed", m_probeState.workspaceProbeRejectedId, matches, ok && shaped, false);
            if (!ok) {
                m_probeState.workspaceProbeDone = true;
                if (m_application) m_application->Exit();
                return;
            }
            m_probeState.workspaceProbeStep = 12;
            return;
        }
        case 12: {
            const std::string before = m_sess->workspacePath;
            const std::filesystem::path expected =
                std::filesystem::path(WideFromUtf8Path(m_probeState.workspaceProbeTargetPath));
            m_sess->workspacePath = m_probeState.workspaceProbeOriginalPath;  // probe-only local corruption
            RefreshHud();
            const bool expectedMismatch = equivalent(before, expected) &&
                                          !equivalent(m_sess->workspacePath, expected);
            // ★★ §7 判据的负对照：光有正相“匹配”证明不了什么——一个永远返回
            //   `true` 的判据也会匹配。把气泡文案改成旧实现那种**带换行**的形态
            //   （`\n未确认 · …`），判据必须翻红，否则前面的 `bubbleShapeOk`
            //   根本没有区分能力。判完立即恢复原文案。
            bool shapeNegControl = false;
            {
                const auto view = std::find_if(m_pendingPromptViews.begin(), m_pendingPromptViews.end(),
                    [&](const PendingPromptView& v) { return v.clientId == m_probeState.workspaceProbeRejectedId; });
                if (view != m_pendingPromptViews.end() && view->row.label) {
                    const std::string saved = view->row.label->GetText();
                    view->row.label->SetText(saved + "\n\u672a\u786e\u8ba4 \u00b7 \u7b49\u5f85 core \u63a5\u53d7");
                    bubbleProbe(m_probeState.workspaceProbeRejectedId);
                    shapeNegControl = !m_probeState.workspaceProbeBubbleShapeOk;
                    view->row.label->SetText(saved);
                    // ★ 不把 shapeOk 改回 true：日志里应当留下「负对照确实看见了
                    //   换行」这个读数，而不是抹掉证据好看。
                }
            }
            append("negative", {}, 0, false, expectedMismatch && shapeNegControl);
            m_probeState.workspaceProbeDone = true;
            if (m_application) m_application->Exit();
            return;
        }
        default:
            stopFailed("invalid-step", {}, 0);
            return;
    }
}
