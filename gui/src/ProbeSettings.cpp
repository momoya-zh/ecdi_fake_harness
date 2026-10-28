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
namespace {
// §8.5.6「环境变量作为兜底来源」的两个假端点：`.invalid` 是 RFC 2606 保留 TLD，
//   永不解析到真实服务；两个 host 不同 ⇒ sanitize 后的变量名不同 ⇒ 正负对照互不污染。
constexpr const char* kProbeEnvKeyUrl = "https://probe-env-key.invalid/v1";
constexpr const char* kProbeEnvMissingUrl = "https://probe-env-missing.invalid/v1";
constexpr const char* kProbeEnvModel = "probe-env-model";
// §9.5 改名相：源提供商由探针自己造（回放跑法下 case 0 那家根本不存在），
// 目标识别名刻意**不是任何 host**——它就是"id 不再从地址派生"的活证据。
constexpr const char* kProbeRenameSrcUrl = "https://probe-rename-src.invalid/v1";
constexpr const char* kProbeRenameSrcId = "probe-rename-src";
constexpr const char* kProbeRenameSrcName = "probe 改名前";
constexpr const char* kProbeRenameKey = "sk-probe-rename-not-a-real-key";
constexpr const char* kProbeRenameToId = "probe-renamed";
constexpr const char* kProbeRenameToName = "probe 改名后";
// ★★ v4 第三步（2026-10-27）：多模型相（追加 / 删除）用的那一家。
//   为什么必须真造一家：`OnSettingsSave` 的合并逻辑是**按已存在的条目**合并的
//   （表单这个顶替编辑目标、其余保序保留），没有"已存在的第一个模型"就测不到
//   "第二个模型有没有把它冲掉"——而那正是 Save 走 `Upsert` 整条替换时最容易
//   静默发生的损坏（配置看着还有一家，模型只剩一个）。
constexpr const char* kProbeMultiUrl = "https://probe-multi.invalid/v1";
constexpr const char* kProbeMultiId = "probe-multi";
constexpr const char* kProbeMultiKey = "sk-probe-multi-not-a-real-key";
constexpr const char* kProbeMultiModel1 = "probe-multi-1";
constexpr const char* kProbeMultiModel2 = "probe-multi-2";

std::string ReadWholeFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}   // namespace
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

    // ★★ §9.5（2026-10-27）：探针直接 SetText 填字段、绕过 UI，所以「新增提供商」
    //   那一步（清编辑目标 + 清两个身份字段 + 清待确认改名）也得自己补上。
    //   漏了它，一次针对**另一家** URL 的保存会被判成改名（编辑目标还是上一家），
    //   于是保存变成"再点一次确认"——判据会静默退化成「保存没生效」，那是最难
    //   分辨的一种假红。
    auto startNewProvider = [this] {
        m_editingProviderId.clear();
        // ★ v4 第三步：新增提供商 = 它名下还没有模型 ⇒ 模型编辑目标一并清掉
        //   （留着上一家的模型名，保存时会被当成"替换掉那个模型"而不是新增）。
        m_editingModelName.clear();
        m_pendingRenameFrom.clear();
        m_pendingRenameTo.clear();
        if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText("");
        if (m_hud.settingsProviderName) m_hud.settingsProviderName->SetText("");
    };
    auto clickSave = [this] {
        if (!m_hud.settingsPanel || !m_hud.settingsPanel->IsVisible() || !m_hud.settingsSave) {
            return false;
        }
        const ECDI::Point abs = m_hud.settingsSave->GetAbsolutePosition();
        SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.settingsSave->GetWidth() / 2,
                          static_cast<int>(abs.y) + m_hud.settingsSave->GetHeight() / 2);
        return true;
    };
    // save 相写入的那家提供商的识别名（改名相的**源**）。与 case 0 同一算式。

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
                SendModelSwitch(m_sess->providerId, m_sess->modelName);
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
            // ★★ 2026-10-27：ack 到了**还不够**，模型名必须真的对上。
            //   只判"ack 到达"时，把 `model` 字段传成任何非空串（甚至传成 provider
            //   id）都会绿：core 照单全收、ack 回那个错值，而判据根本不看值。
            //   这条是 `model` 字段唯一的端到端判据——松了，它就等于没测。
            if (bad.empty() && m_sess) {
                const ProviderConfig* pc = m_appConfig.Find(m_sess->providerId);
                if (!pc) {
                    bad = "switch-ok: provider not in config: " + m_sess->providerId;
                } else if (!pc->ActiveModelName().empty() &&
                           m_sess->modelName != pc->ActiveModelName()) {
                    // ★ 文案里**不能有双引号**：settingsProbe 的 firstDivergence 走
                    //   `escS`，那个转义器不转义引号（摘要格式的既有契约）⇒ 带引号
                    //   会把整份摘要 JSON 弄坏，regress 在 json.loads 处崩掉，
                    //   于是"判据红了"变成"门自己炸了"。方括号同样可读。
                    bad = "model ack mismatch: want [" + pc->ActiveModelName() + "], got [" +
                          m_sess->modelName + "]";
                }
            }
            report("switch-ok", bad);
        }
        // ★★ 2026-10-27：原来这里是 `SendModelSwitch("unknown-host.invalid")`，
        //   而那条路径在 `!pc`（配置里查不到）分支就 AddNoticeRow + return 了——
        //   **请求从来没发出去**。case 3 判的是 `pendingModelId` 为空，于是
        //   switch-bad 恒绿：注释写着「core 应回 ok:false」，实际 core 一次都没
        //   收到过。这里直接走写通道，让 core 真的拒一次。
        //   ★ 回放源没有写通道（Send 返回 false）⇒ 清掉在途 id，本相在该跑法下
        //     自述"不适用"，与原来一样绿——判别力由 live 跑法提供（§2e）。
        if (m_sess && m_sess->writer) {
            const std::string id = "m" + std::to_string(m_sess->writer->LastSeqSent() + 1);
            m_sess->pendingModelId = id;
            m_probeState.settingsProbeBadSwitchSent =
                m_sess->writer->Model(id, "unknown-host.invalid", "", 0, 0);
            if (!m_probeState.settingsProbeBadSwitchSent) {
                m_sess->pendingModelId.clear();
            }
        }
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
            // ★★ 2026-10-27：ack 到达**还不够**，必须确认它是 ok:false。
            //   `pendingModelId` 的清空是无条件的（ok:true 也清）⇒ 只看它，core
            //   哪怕回了个错绿的 ack 也照样通过。真判据是时间线上那条失败通知
            //   （ok:false 分支唯一的可观察产物）。
            //   ★ 只在"请求真发出去过"时判：回放源没有写通道，扫不到通知是必然的，
            //     那不是功能坏了，是这一相在该跑法下不适用（见 ProbeState 注释）。
            if (bad.empty() && m_sess && m_probeState.settingsProbeBadSwitchSent) {
                bool sawFailure = false;
                for (const auto& e : m_sess->timeline) {
                    if (e.kind == TimelineEntry::Kind::Notice &&
                        e.text.find("模型切换失败") != std::string::npos) {
                        sawFailure = true;
                        break;
                    }
                }
                if (!sawFailure) {
                    bad = "no failure notice in timeline "
                          "(core answered ok:true for an unknown provider?)";
                }
            }
            report("switch-bad", bad);
        }
        // ⑥ key-env（正对照，§8.5.6）：凭据库无该提供商、**只**设环境变量
        //   ⇒ 保存必须被接受，且 key 不得被写进凭据库（env 是来源，不是存储）。
        //   ★ 面板在 save 相之后是隐藏的（模态浮层）。必须先重新打开再填字段，
        //     否则点击落在一个不可见控件上，判据会静默变成「保存没生效」——
        //     那是最难分辨的一种假红。
        SetSettingsVisible(true);
        startNewProvider();
        const std::string envProviderId = AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl);
        SetEnvironmentVariableW(ECDI::UTF8ToWide(ProviderKeyEnvName(envProviderId)).c_str(),
                                L"sk-probe-env-source-not-a-real-key");
        m_hud.settingsBase->SetText(kProbeEnvKeyUrl);
        m_hud.settingsModel->SetText(kProbeEnvModel);
        m_hud.settingsKey->SetText("");   // 关键：key 框留空，只靠环境变量
        m_probeState.settingsProbeStage = 4;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 4: {
        // ★ 前置断言：面板必须真的可见。不可见时点击落在转录上，判据会退化成
        //   「保存没生效」——把「仪器没摆好」误报成「功能坏了」（假红）。
        const bool panelUp = m_hud.settingsPanel && m_hud.settingsPanel->IsVisible();
        if (panelUp) {
            const ECDI::Point abs = m_hud.settingsSave->GetAbsolutePosition();
            SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.settingsSave->GetWidth() / 2,
                              static_cast<int>(abs.y) + m_hud.settingsSave->GetHeight() / 2);
        }
        const std::string envProviderId = AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl);
        {
            std::string bad;
            if (!panelUp) {
                bad = "settings panel not visible before env-key save (probe precondition)";
            }
            const std::string hint = m_hud.settingsHint ? m_hud.settingsHint->GetText() : "";
            if (ReadWholeFile(AppConfig::DefaultPath()).find(kProbeEnvKeyUrl) == std::string::npos) {
                bad = "save rejected an env-sourced key (config has no such provider)";
            }
            if (creds::Exists(ECDI::UTF8ToWide(
                    AppConfig::CredentialTargetFor(envProviderId)))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "env-sourced save wrote a credential (env must stay a source, not storage)";
            }
            if (!m_providerKeys.count(envProviderId)) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "env key was not cached (spawn would start core without a key)";
            }
            if (hint.find("环境变量") == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "hint does not name the key source (source must be visible)";
            }
            report("env-key-ok", bad);
        }
        // ⑦ key-env（负对照）：清掉环境变量 ⇒ 同样的空 key 必须**被拒**，
        //   且配置里不得留下这个提供商（失败点在建表之前，不许有半成品）。
        SetSettingsVisible(true);
        SetEnvironmentVariableW(
            ECDI::UTF8ToWide(ProviderKeyEnvName(envProviderId)).c_str(), nullptr);
        startNewProvider();
        m_hud.settingsBase->SetText(kProbeEnvMissingUrl);
        m_hud.settingsModel->SetText(kProbeEnvModel);
        m_hud.settingsKey->SetText("");
        m_probeState.settingsProbeStage = 5;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 5: {
        if (m_hud.settingsPanel && m_hud.settingsPanel->IsVisible() && m_hud.settingsSave) {
            const ECDI::Point abs = m_hud.settingsSave->GetAbsolutePosition();
            SynthesizeClickAt(static_cast<int>(abs.x) + m_hud.settingsSave->GetWidth() / 2,
                              static_cast<int>(abs.y) + m_hud.settingsSave->GetHeight() / 2);
        }
        {
            std::string bad;
            const std::string hint = m_hud.settingsHint ? m_hud.settingsHint->GetText() : "";
            // 判据用**变量全名**而不是「也未设置」这种措辞：措辞会随文案调整而漂移，
            // 变量名是用户唯一能照着做的信息，它必须原样出现。
            const std::string missingName = ProviderKeyEnvName(
                AppConfig::ProviderIdFromBaseUrl(kProbeEnvMissingUrl));
            if (hint.find(missingName) == std::string::npos) {
                bad = "missing-key hint does not name " + missingName + " (hint=" + hint + ")";
            }
            if (ReadWholeFile(AppConfig::DefaultPath()).find(kProbeEnvMissingUrl) !=
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "config accepted a provider with no key at all";
            }
            report("env-key-missing", bad);
        }
        // ⑧ rename 前置：**自己造一家有凭据的提供商**，再改它的名。
        //   ★ 为什么不用 case 0 存的那家：回放跑法下 first-run/save 是 designed-red
        //     （面板根本没开，点击落在转录上），那家提供商在这条跑法里**不存在**。
        //     依赖它的判据会以"旧凭据不见了"这种假红出现，看起来像功能坏了。
        //   ★ 为什么必须有凭据：凭据搬迁是改名的核心契约，没有旧凭据就测不到"搬"。
        SetSettingsVisible(true);
        startNewProvider();
        if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText(kProbeRenameSrcId);
        if (m_hud.settingsProviderName) {
            m_hud.settingsProviderName->SetText(kProbeRenameSrcName);
        }
        m_hud.settingsBase->SetText(kProbeRenameSrcUrl);
        m_hud.settingsModel->SetText("probe-model");
        m_hud.settingsKey->SetText(kProbeRenameKey);
        m_probeState.settingsProbeStage = 6;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 6: {
        // ⑨ rename-src：造出来的那家必须真的落盘 + 真的有凭据。
        //   单独一相是为了让「仪器没摆好」与「改名坏了」可分辨——混进下一相的话，
        //   一个"提供商没建成"的实现会以"改名没生效"的样子报出来。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before creating the rename source"
                      " (probe precondition)";
            }
            const std::string text = ReadWholeFile(AppConfig::DefaultPath());
            if (text.find(std::string("\"id\":\"") + kProbeRenameSrcId + "\"") ==
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "rename source provider was not written to the config";
            }
            if (!creds::Exists(ECDI::UTF8ToWide(
                    AppConfig::CredentialTargetFor(kProbeRenameSrcId)))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "rename source has no credential (nothing to migrate)";
            }
            report("rename-src", bad);
        }
        // ⑩ rename 前置：编辑目标 = 那家，识别名改成 kProbeRenameToId，key 留空。
        SetSettingsVisible(true);
        m_editingProviderId = kProbeRenameSrcId;
        m_pendingRenameFrom.clear();
        m_pendingRenameTo.clear();
        if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText(kProbeRenameToId);
        if (m_hud.settingsProviderName) m_hud.settingsProviderName->SetText(kProbeRenameToName);
        m_hud.settingsBase->SetText(kProbeRenameSrcUrl);   // 端点不动，只改身份
        m_hud.settingsModel->SetText("probe-model");
        m_hud.settingsKey->SetText("");   // ★ 留空 ⇒ 必须把旧凭据搬过来（轮换语义）
        // 快照「点击前」的配置文件：后面两相的判据都要拿它当比对面。
        m_probeState.settingsProbeConfigSnapshot = ReadWholeFile(AppConfig::DefaultPath());
        m_probeState.settingsProbeStage = 7;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 7: {
        // ⑪ rename-confirm（正对照，规格 9.5.3 第 1 步）：第一击**只问不做**。
        //   ★ 判据必须是"什么都没变"。只查「hint 里有'再点保存确认'」是自欺——
        //     一个"问了但也顺手改了"的实现同样能过，而那正是最危险的形态
        //     （配置已改、凭据没迁，下次无参启动静默失效）。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before rename confirm (probe precondition)";
            }
            const std::string hint = m_hud.settingsHint ? m_hud.settingsHint->GetText() : "";
            if (hint.find("再点保存确认") == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "first click did not ask for confirmation (hint=" + hint + ")";
            }
            if (ReadWholeFile(AppConfig::DefaultPath()) !=
                m_probeState.settingsProbeConfigSnapshot) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "first click already rewrote the config"
                       " (must ask before touching anything)";
            }
            if (creds::Exists(ECDI::UTF8ToWide(
                    AppConfig::CredentialTargetFor(kProbeRenameToId)))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "first click already wrote the new credential target";
            }
            if (!creds::Exists(ECDI::UTF8ToWide(
                    AppConfig::CredentialTargetFor(kProbeRenameSrcId)))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "old credential vanished before the rename was confirmed";
            }
            report("rename-confirm", bad);
        }
        m_probeState.settingsProbeStage = 8;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 8: {
        // ⑫ rename-ok（正对照，规格 9.5.3 第 2–5 步）：第二击真的执行。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before rename apply (probe precondition)";
            }
            const std::string oldId = kProbeRenameSrcId;
            std::string key;
            std::string credError;
            if (!creds::Read(ECDI::UTF8ToWide(AppConfig::CredentialTargetFor(kProbeRenameToId)),
                             key, credError)) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "credential was not migrated to the new target: " + credError;
            } else if (key != kProbeRenameKey) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "migrated credential value mismatch";
            }
            if (creds::Exists(ECDI::UTF8ToWide(AppConfig::CredentialTargetFor(oldId)))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "old credential target left behind";
            }
            const std::string text = ReadWholeFile(AppConfig::DefaultPath());
            const std::string newIdField = std::string("\"id\":\"") + kProbeRenameToId + "\"";
            const std::string oldIdField = std::string("\"id\":\"") + oldId + "\"";
            // ★ 判据必须带 `"id":"` 前缀：baseUrl 里仍然含旧 host（端点没换，只换了
            //   身份），裸搜旧 id 会永远命中——那条判据就永远红/永远绿，没有判别力。
            if (text.find(newIdField) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "config does not carry the new provider id";
            }
            if (text.find(oldIdField) != std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "old provider id survived the rename";
            }
            if (text.find(std::string("\"name\":\"") + kProbeRenameToName + "\"") ==
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "display name was not persisted (v3 field missing)";
            }
            // ★ 2026-10-27：版本号取自 `AppConfig::kCurrentVersion`，不再写死。
            //   上一版硬编码 `"version":3`，配置升到 v4 后这条判据立刻变红——
            //   版本号散落在两处，就必然有一处会被忘掉。
            if (text.find("\"version\":" + std::to_string(AppConfig::kCurrentVersion)) ==
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "config version did not upgrade to " +
                       std::to_string(AppConfig::kCurrentVersion);
            }
            if (text.find(std::string("\"defaultProviderId\":\"") + kProbeRenameToId + "\"") ==
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "defaultProviderId did not follow the rename";
            }
            report("rename-ok", bad);
        }
        // ⑬ rename-conflict 前置（负对照，规格 9.5.2）：识别名改成**另一家已有的** id。
        //   撞的是 env-key 那家（case 4 建的），不是刚改名的这家——唯一性校验必须
        //   跨提供商生效，撞自己（正在编辑的那家）是被豁免的。
        SetSettingsVisible(true);
        if (m_hud.settingsProviderId) {
            m_hud.settingsProviderId->SetText(
                AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl));
        }
        m_hud.settingsKey->SetText("");
        m_probeState.settingsProbeConfigSnapshot = ReadWholeFile(AppConfig::DefaultPath());
        m_probeState.settingsProbeStage = 9;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 9: {
        // ⑭ rename-conflict：撞已有的另一家 ⇒ 保存被**拒**且配置文件逐字节不变。
        //   这是「唯一性校验」唯一的负对照：没有它，一个"静默覆盖"的实现照样全绿。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before conflict save (probe precondition)";
            }
            const std::string hint = m_hud.settingsHint ? m_hud.settingsHint->GetText() : "";
            if (hint.find("已存在") == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "conflicting id was not rejected (hint=" + hint + ")";
            }
            if (ReadWholeFile(AppConfig::DefaultPath()) !=
                m_probeState.settingsProbeConfigSnapshot) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "rejected save still rewrote the config";
            }
            if (creds::Exists(ECDI::UTF8ToWide(AppConfig::CredentialTargetFor(
                    AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl))))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "rejected save wrote a credential";
            }
            report("rename-conflict", bad);
        }
        // ⑬ multi 前置：造一家**只有一个模型**的提供商（下一相往它追加第二个）。
        //   ★ 为什么单独造一家而不是复用上面那家：上面那家的 id 在改名相里变过，
        //     拿它当"同家多模型"的载体，一旦改名出问题，多模型两相会跟着一起红——
        //     那就分不清"改名坏了"还是"多模型坏了"。
        SetSettingsVisible(true);
        startNewProvider();
        if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText(kProbeMultiId);
        if (m_hud.settingsProviderName) m_hud.settingsProviderName->SetText("probe multi");
        m_hud.settingsBase->SetText(kProbeMultiUrl);
        m_hud.settingsModel->SetText(kProbeMultiModel1);
        if (m_hud.settingsContext) m_hud.settingsContext->SetText("64000");
        if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("2048");
        m_hud.settingsKey->SetText(kProbeMultiKey);
        m_probeState.settingsProbeStage = 10;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 10: {
        // ⑭ multi-add：第一个模型落盘。单独一相是为了让「仪器没摆好」与
        //   「多模型坏了」可分辨——混进下一相的话，一个"提供商根本没建成"的
        //   实现会以"追加没生效"的样子报出来。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before multi-model setup (probe precondition)";
            }
            const std::string text = ReadWholeFile(AppConfig::DefaultPath());
            if (text.find(std::string("\"id\":\"") + kProbeMultiId + "\"") == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "multi-model provider was not created";
            }
            if (text.find(kProbeMultiModel1) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "first model missing from the config";
            }
            report("multi-add", bad);
        }
        // ⑮ 前置：**新增模型**（= 点「新增模型」按钮的效果：编辑目标清空），
        //   同一个提供商下填第二个模型；key 留空（已存过 ⇒ 保持不变）。
        SetSettingsVisible(true);
        m_editingModelName.clear();   // ★ 这一行就是「新增模型」按钮的全部语义
        m_hud.settingsModel->SetText(kProbeMultiModel2);
        if (m_hud.settingsContext) m_hud.settingsContext->SetText("128000");
        if (m_hud.settingsMaxOut) m_hud.settingsMaxOut->SetText("4096");
        m_hud.settingsKey->SetText("");
        m_probeState.settingsProbeStage = 11;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 11: {
        // ⑯ multi-merge：第二个模型必须**追加**，第一个不能被冲掉。
        //   这是 v4 第三步最容易踩的一脚：`OnSettingsSave` 走 `Upsert`（整条替换），
        //   合并源若取错（比如只认改名时的 oldId），普通保存就只剩表单这一个模型。
        const bool clicked = clickSave();
        {
            std::string bad;
            if (!clicked) {
                bad = "settings panel not visible before second-model save (probe precondition)";
            }
            const std::string text = ReadWholeFile(AppConfig::DefaultPath());
            if (text.find(kProbeMultiModel1) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "adding a second model dropped the first one";
            }
            if (text.find(kProbeMultiModel2) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "second model was not saved";
            }
            if (text.find(std::string("\"activeModel\":\"") + kProbeMultiModel2 + "\"") ==
                std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "activeModel did not follow the saved model";
            }
            report("multi-merge", bad);
        }
        // ⑰ 前置：删除刚加的那个模型（点模型行 ⇒ 编辑目标 = 它）。
        SetSettingsVisible(true);
        m_editingModelName = kProbeMultiModel2;
        OnSettingsDeleteModel();   // 同步落盘；判据在下一相读文件
        m_probeState.settingsProbeStage = 12;
        m_probeState.settingsProbeNextAt = now + settle;
        return;
    }
    case 12: {
        // ⑱ multi-del：只删被选中的那个，同家另一个必须还在。
        //   ★ 这条同时钉住「至少保留一个」的反面：删到只剩一个时再删必须被拒
        //     （下面单独一步做），否则会留下"有提供商但没有模型"的死配置。
        {
            std::string bad;
            const std::string text = ReadWholeFile(AppConfig::DefaultPath());
            if (text.find(std::string("\"id\":\"") + kProbeMultiId + "\"") == std::string::npos) {
                bad = "deleting a model removed the whole provider";
            }
            if (text.find(kProbeMultiModel2) != std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "deleted model is still in the config";
            }
            if (text.find(kProbeMultiModel1) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "deleting one model dropped its sibling";
            }
            // 最后一个模型不许删（否则 core 侧"切过去必 ok:false"）
            m_editingModelName = kProbeMultiModel1;
            OnSettingsDeleteModel();
            const std::string after = ReadWholeFile(AppConfig::DefaultPath());
            if (after.find(kProbeMultiModel1) == std::string::npos) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "the last remaining model was deleted (provider left unusable)";
            }
            report("multi-del", bad);
        }
        // ⑥ cleanup：删探针建的凭据与配置文件（幂等）。
        {
            std::string bad;
            // ★ 要删**四个**目标名：改名后凭据已搬到 kProbeRenameToId 名下，只删
            //   旧名会把它留在库里（那正是"探针泄漏"的老形态）；case 0 那条
            //   （无参启动跑法下才存在）也要收掉；多模型相自己造的那家同理。
            const std::string targets[4] = {
                AppConfig::CredentialTargetFor(kProbeRenameSrcId),
                AppConfig::CredentialTargetFor(kProbeRenameToId),
                AppConfig::CredentialTargetFor(kProbeMultiId),
                AppConfig::CredentialTargetFor(AppConfig::ProviderIdFromBaseUrl(
                    m_options.settingsUrl.empty() ? "https://probe-settings.invalid/v1"
                                                  : m_options.settingsUrl))};
            for (const std::string& target : targets) {
                std::string credError;
                if (!creds::Delete(ECDI::UTF8ToWide(target), credError)) {
                    bad += std::string(bad.empty() ? "" : "; ") +
                           "credential cleanup failed: " + credError;
                }
            }
            // 正对照那条**不该**有凭据（env 来源不落库）；留着就是泄漏，必须报出来。
            const std::string envTarget = AppConfig::CredentialTargetFor(
                AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl));
            if (creds::Exists(ECDI::UTF8ToWide(envTarget))) {
                bad += std::string(bad.empty() ? "" : "; ") +
                       "env-sourced credential left behind in the vault";
                std::string ignored;
                creds::Delete(ECDI::UTF8ToWide(envTarget), ignored);
            }
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide(ProviderKeyEnvName(
                    AppConfig::ProviderIdFromBaseUrl(kProbeEnvKeyUrl))).c_str(), nullptr);
            // ★ 2026-10-26：删除目标必须是**隔离路径**。`AppConfig::DefaultPath()` 是
            //   "本次进程生效路径"——没传 `--config` 时它就是用户的真实配置，删掉即丢全部提供商。
            //   main.cpp 已拒绝无 `--config` 的启动，这里再断言一次：将来若有人绕过那道门
            //   （或挪动 SetPathForTesting 的调用时机），本条会 red，而不是悄悄删掉用户配置。
            //   ★ 这也是本仓"仪器藏了假设"的第 N 例：cleanup 曾靠 `remove` 不设 error 而恒绿。
            const std::filesystem::path inUse = AppConfig::DefaultPath();
            if (inUse == AppConfig::DefaultDir() / L"config.json") {
                bad = "refused: cleanup target is the real config path"
                      " (--config isolation not in effect)";
            } else {
                std::error_code ec;
                std::filesystem::remove(inUse, ec);
            }
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

// ── 摘要段（Stage 4：从 WriteSummary 搬出，块体逐字未改）──────────────────
void HarnessApp::AppendSettingsProbeSummary(std::string& json) {
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

void HarnessApp::AppendWorkspaceProbeSummary(std::string& json) {
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

