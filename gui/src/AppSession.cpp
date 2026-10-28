// ============================================================
// 会话生命周期与侧栏会话列表（新建 / 切换 / 删除 / 归档 / 分组建行）
// ------------------------------------------------------------
// 为什么单独一个文件：会话是**唯一有"生命周期"的状态**——core 子进程、
//   协议写入端、工作区归属、转录行集合都挂在它上面。切换会话要拆掉一棵树
//   再建另一棵，这段逻辑以前和 tick 混在 5400 行里，改任何一处都得先确认
//   "这是会话的还是流式的"。
// ★ 纪律：本文件**只碰侧栏会话列表**（`m_sessionList`）这一棵子树；
//   中栏转录树归 AppTranscript.cpp。两棵树的可见性投影都在 Relayout（AppHud）。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1d）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppConfig.h"
#include "AppInternal.h"
#include "ChildSource.h"
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
#include "ECDI/Core/UTF8.h"   // ByteOffsetToCodepointIndex（中间省略按码点算）
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ScrollView.h"

#include <cmath>   // std::floor（ElideBudgetCp 预算算术）
#include <string>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// 跨 TU 共享符号（定义见 AppInternal.h / PathUtil.cpp）
using harness::internal::PathToUtf8;
using harness::internal::kSidebarGroupH;
using harness::internal::kSidebarListW;
using harness::internal::kSidebarMaxRows;
using harness::internal::kSidebarNameW;
using harness::internal::kSidebarRowGap;
using harness::internal::kSidebarRowH;
using harness::internal::kSidebarRowW;

// ★★ 2026-10-20：路径的**中间**省略（保留头与尾）。
//   为什么必须保留尾部：`ECDI::Label` 对超宽文本是**尾部丢弃**（单行 DrawText 裁剪），
//     而两个工作区的区别几乎总在叶子段（`.../proj` vs `.../other`）——尾部一丢，
//     两个分组标题在屏幕上就是同一行，“按工作区区分”退化成“按前缀区分”。
//   为什么这里**不**逐行测量：本函数在 `RebuildSessionList` 的建树路径上，任何
//     `MeasureText` 都会进 `m_evalMeasureCalls`（评估仪器）⇒ 每次侧栏重建都会
//     污染 measure/paints 读数（既有基线里就有 measureCalls/measureChars）。
//     ⇒ 改用**一次性校准 + 码点预算**（分组标题恒为等宽字体，ASCII 每码点同宽）：
//     首次建行时量一次参考串求出单码点宽与省略号宽（`ElideBudgetCp()`，走
//     `m_window->GetTextMeasurer()`，仅 +2 次调用且在 cull-probe 读数快照处补偿），
//     之后每行的省略都是纯算术。`--sidebar-probe` 侧的实测（MeasureText 后比对
//     可用宽）仍是设计中的验证器——2026-10-26 它第一次真正咬住了东西：固定预算
//     `22 码点` 在 125% 缩放下量出 134.4px > 128px（详见 App.h 修法 D 注释）。

std::string ElideMiddleByCodepoints(const std::string& text, size_t maxCp) {
    if (text.empty()) return text;
    const size_t total = ECDI::ByteOffsetToCodepointIndex(text, text.size());
    if (total <= maxCp) return text;   // 放得下就**原样**返回（零改动即零回归）
    if (maxCp < 4) {
        // 退化：连 `X…Y` 都放不下，只能留尾部（尾部是唯一有信息量的一端）
        return text.substr(ECDI::CodepointIndexToByteOffset(text, total - maxCp));
    }
    // 头 1/4、尾 3/4 的**修正版**（2026-10-26）：头部封顶 3 码点 = 盘根 "D:/"。
    //   原因：修法 D 让预算随真实度量收缩（125% 缩放下 128px 宽 → 19），固定 1/4
    //   会把尾部压到 14 < 15（`workspace-alpha` 的长度）⇒ 叶子段被切——而「尾部
    //   至少保留叶子段」是中间省略的**存在理由**（见上方 ★★ 注释），不是可牺牲项。
    //   封顶 3 后：19 预算 → tail 15（叶子恰好整体存活）；100% 的 24 预算 →
    //   tail 20（比旧规则还多留）。尾部信息更密（末级目录 + 叶子段），多偏尾合理。
    const size_t headCp = std::min<size_t>(3, maxCp / 4);
    const size_t tailCp = maxCp - headCp - 1;   // 减 1 = 省略号本身占一个码点
    const std::string head =
        text.substr(0, ECDI::CodepointIndexToByteOffset(text, headCp));
    const std::string tail =
        text.substr(ECDI::CodepointIndexToByteOffset(text, total - tailCp));
    return head + "\xe2\x80\xa6" + tail;   // U+2026 HORIZONTAL ELLIPSIS
}

// 侧栏标题的省略预算（码点数）——修法 D 的落点（背景见 App.h 成员注释）。
//   预算推导：shown = (cp−1) 个等宽字符 + 1 个省略号，约束 (cp−1)·w + w… ≤ labelW
//   ⇒ cp = floor((labelW − w…) / w) + 1。再留 1px 安全余量吸收浮点/GDI 取整误差
//   （125% 实测正好压线 128.0，不余量就会被 0.1px 级误差翻红——探针已验证过一次）。
size_t HarnessApp::ElideBudgetCp(int labelW) {
    if (m_elideCharW < 0.0f) {   // 惰性校准：整个进程生命周期只走一次
        if (m_window) {
            ECDI::TextMeasurer& measurer = m_window->GetTextMeasurer();
            const ECDI::Font font = harness::MonoFont(harness::kFontMicro);
            const size_t before = m_measureProbe ? m_measureProbe->MeasureCalls() : 0;
            const float w10 = measurer.MeasureText(font, "MMMMMMMMMM").width;
            m_elideEllipsisW = measurer.MeasureText(font, "\xe2\x80\xa6").width;
            const size_t after = m_measureProbe ? m_measureProbe->MeasureCalls() : 0;
            m_elideMeasureOverhead = after - before;
            if (w10 > 0.0f) m_elideCharW = w10 / 10.0f;   // 量 10 个求均值，抗单字取整
        }
        if (m_elideCharW <= 0.0f) m_elideCharW = 0.0f;   // 校准失败 ⇒ 走兜底常数
    }
    if (m_elideCharW <= 0.0f) {
        // 兜底（量不到测量器，理论不可达）：回到旧固定预算，宁多省不溢出。
        return labelW >= 150 ? 22 : 21;
    }
    const float budget =
        std::floor((static_cast<float>(labelW) - m_elideEllipsisW - 1.0f) / m_elideCharW) + 1.0f;
    return budget < 4.0f ? 4 : static_cast<size_t>(budget);   // 4 = ElideMiddle 的 X…Y 下限
}

// 按当前 CLI 选项建一个会话，push 进 m_sessions 并返回（**不**激活）。
//   ★ 这段就是原构造函数体：--core ⇒ 拉起子进程（真 live），否则夹具回放。
//   抽成方法是为了让 `NewSession()` 能复用同一套源创建逻辑。
//   ★ 返回 nullptr = live 启动失败（不 push 坏会话）；调用方必须判空。
HarnessApp::Session* HarnessApp::CreateSessionFromOptions() {
    auto session = std::make_unique<Session>();
    Session& s = *session;

    // ── 源选择（P1.5）：--core <exe> ⇒ 子进程真路径；否则夹具回放 ──
    if (!m_options.corePath.empty()) {
        auto child = std::make_unique<ChildSource>();
        std::string error;
        // ★ P3.5 D5 实施版：key 走**启动环境层**——GUI 进程
        //   SetEnvironmentVariableW("ECDI_CORE_KEY", …)，子进程经 lpEnvironment =
        //   nullptr 的标准继承拿到。不经协议、不经命令行、不经配置文件；
        //   与 dsh 的 launching-environment 层同构（调研记录见当日工作日志）。
        //   ★ 手搓环境块（父环境复制 + 覆盖 + CreateProcessW 显式传入）实测在
        //     当前启动环境下恒报 87（ERROR_INVALID_PARAMETER），已废弃——偏差与
        //     未决问题记在规格 §8.5.4 R-5。
        //   ★ 保留不"用完即清"：每个新会话（＋按钮/临时会话）都要再拉一个 core。
        //     key 留在 GUI 进程内存属可接受形态（同一信任域，positioning §4）；
        //     纪律是**绝不**进日志/摘要/转录。
        // ★ P3.6：为**每一家**提供商注入 ECDI_PROVIDER_<SAN>_{ID,URL,KEY,MODEL}
        //   （core 按同一 sanitize 规则对齐；model 切换按 id 寻址）。
        //   ECDI_CORE_KEY 仍指向初始提供商（独立调试兼容）。
        // ★ 2026-10-27：补 `_MODEL`。缺了它，core 注册表里除初始提供商之外
        //   每条的模型名都是空串 ⇒ 热切换过去后请求体 `"model": ""` 端点必报错。
        //   这是「一个端点一个密钥带多个模型」在协议层能成立的前提。
        CacheProviderKeys();
        for (const auto& pc : m_appConfig.providers) {
            const auto keyIt = m_providerKeys.find(pc.id);
            if (keyIt == m_providerKeys.end()) continue;
            const std::string san = EnvSanitizeProviderId(pc.id);
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_ID").c_str(),
                ECDI::UTF8ToWide(pc.id).c_str());
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_URL").c_str(),
                ECDI::UTF8ToWide(pc.baseUrl).c_str());
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_KEY").c_str(),
                ECDI::UTF8ToWide(keyIt->second).c_str());
            // 传空串 = 删除变量（Win32 语义），正好表达「这家没有模型名」。
            // ★ v4：注入的是**激活模型**——core 的注册表仍是「一 provider 一个
            //   模型名」，多模型切换靠 `model` 消息的 `model` 字段覆盖。
            SetEnvironmentVariableW(
                ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_MODEL").c_str(),
                ECDI::UTF8ToWide(pc.ActiveModelName()).c_str());
        }
        if (!m_coreApiKey.empty()) {
            SetEnvironmentVariableW(L"ECDI_CORE_KEY", ECDI::UTF8ToWide(m_coreApiKey).c_str());
        }
        if (!child->Start(m_options.corePath, m_options.coreArgs, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ChildSource: " + error));
            // 源启动失败 ⇒ 不 push（避免留下一个拉不动的会话），如实返回 nullptr
            return nullptr;
        }
        s.child = child.get();
        s.source = std::move(child);
        s.writer = std::make_unique<ProtocolWriter>(*s.source);
        s.label = m_options.corePath.filename().generic_string() + " (live)";
        // ★ Step 5：来源必须是**显式字段**，不能再靠"corePath 空不空"推断——
        //   同一个进程里此后可以同时有 live / 启动 replay / 历史 replay。
        s.originKind = "live";
        s.originPath = m_options.corePath;
        // ★ P4 H：模型名预填（hello 不带 model 字段；ack 到达前 chip 显示它）。
        if (const ProviderConfig* def = m_appConfig.DefaultProvider()) {
            s.modelName = def->ActiveModelName();
        }
        if (m_sessions.empty()) {
            // 首个 live 会话：把 fixtureName 也定下来（窗口标题 / 摘要用）
            m_options.fixtureName = s.label;
        }
    } else {
        auto replay = std::make_unique<ReplaySource>();
        std::string error;
        if (!replay->Load(m_options.replayPath, error)) {
            ECDI::Logger::Log(ECDI::LogLevel::Error,
                              ECDI::UTF8ToWide("ReplaySource: " + error));
        }
        s.pendingTruncated = replay->TruncatedError();
        s.replay = replay.get();
        s.source = std::move(replay);
        s.label = m_options.fixtureName;
        // ★ Step 5：`--replay` 启动的会话与"点开的历史"是两种来源。
        //   区分它们有实际后果：只有后者会在打开时读 sidecar（前者是用户
        //   显式指定的夹具，跑批基线必须可复现，不能被历史状态污染）。
        s.originKind = "replay";
        s.originPath = m_options.replayPath;
    }

    // 会话名：首个用 fixtureName；后续在 NewSession 里改成"会话 N"。
    if (s.label.empty()) s.label = "会话 " + std::to_string(m_sessions.size() + 1);

    // ★★ L1 持久化：GUI 分配全局稳定 ID 与初始元数据
    if (s.sessionId.empty()) {
        s.sessionId = SessionStore::GenerateSessionId();
    }
    s.meta.id = s.sessionId;
    s.meta.createdAt = SessionStore::CurrentUtcTimestamp();
    s.meta.lastUsedAt = s.meta.createdAt;
    s.meta.label = s.label;
    s.meta.providerId = s.providerId;
    s.meta.modelName = s.modelName;
    s.meta.contextLimit = s.contextLimit;

    m_sessions.push_back(std::move(session));
    // ★★ REQ-03：运行期新建的 live 会话也要接上唤醒。构造期 `m_application` 还是空
    //   ⇒ 本调用在那里是 no-op，由 `Run()` 的 `InstallWakeHandlers()` 统一补装。
    InstallWakeHandler(*m_sessions.back());
    return m_sessions.back().get();
}

// ＋ 新建会话的真实行为（0d）。
//   ★ live 模式：再拉起一个 core 子进程 —— 这才是"真的新建一个会话"。
//   ★ 回放模式：没有 core 会对着一个空会话说话，回放夹具是一次性的。硬塞一个
//     假回复就是伪造读数（本仓最守不住的线）。故如实建一个**空会话**并说明情况——
//     "能点、有状态、切得过去"，但绝不假装 core 回了话。
void HarnessApp::NewSession() {
    // 先把当前活动会话的 timed 时钟冻结（否则它被切走后还在按墙上时间"到期"）
    PauseActiveTimedClock();

    const bool live = !m_options.corePath.empty();
    Session* created = nullptr;   // ★ 要活到 SwitchToSession **之后**再用（见下面的越界说明）
    if (live) {
        // 真新建：拉一个新 core。创建失败（nullptr）⇒ 如实提示，活动会话不变。
        created = CreateSessionFromOptions();
        if (!created) {
            AddNoticeRow("[新建会话失败：core 启动失败，见日志]", kErrorDark);
            Relayout();
            RequestRepaint();
            ResumeSessionTimedClock(*m_sess);
            return;
        }
        created->label = "会话 " + std::to_string(m_sessions.size());
        // ★★ 2026-10-20（用户裁决 m08553）：继承**活动会话**的工作区。
        //   ★ 必须在 `SwitchToSession(idx)` 之前抓：那之后 `m_sess` 已经指向新会话，
        //     `InheritedWorkspacePath()` 会去读新建会话自己的（空）外键，永远继承不到东西。
        //   ★ 也必须在此刻抓（而不是等 hello 回来时现查）：hello 是异步到达的，
        //     到那时活动会话可能已被用户又切走了——继承口径必须钉在"点新建的那一刻"。
        created->workspaceInheritedPath = InheritedWorkspacePath();
    } else {
        // 回放模式：建一个空会话（无 source ⇒ 不拉取；OnReplayTick 见空 source 直接返回）
        auto session = std::make_unique<Session>();
        session->label = "会话 " + std::to_string(m_sessions.size() + 1);
        // ★ Step 5：空会话是**第四类来源**。它没有 originPath ⇒ 没有 sidecar。
        session->originKind = "new";
        m_sessions.push_back(std::move(session));
    }

    const size_t idx = m_sessions.size() - 1;
    SwitchToSession(idx);

    if (live) {
        // ★ 协议 §4：GUI 在收到 core 的 hello 之后回 hello（seq 0）。首会话在 Run() 里发；
        //   运行期新建的会话必须在这里补发，否则新 core 永远等不到握手。
        // ★★ 2026-10-27：**不能**沿用上面那个 `idx`。`SwitchToSession` 内部会调
        //   `MaybeDeleteTemporarySession`——若切走的活动会话是临时会话，表会短一格，
        //   `m_sessions[idx]` 正好越界（与 `NewTemporarySession` 同族缺陷，
        //   实测 0xC0000005）。`created` 是刚建的那个对象，被摘掉的是**旧**活动会话，
        //   所以这个指针一定还有效。
        Session& s = *created;
        if (s.writer) s.writer->Hello("0.0.1");
        if (m_options.timed) s.timedAnchor = std::chrono::steady_clock::now();
    } else {
        // 回放空会话：给一条诚实提示（不伪造 core 回复）
        AddNoticeRow("[新建会话：回放模式无 core，空会话——切回上一个会话看历史]", kDarkMuted);
        Relayout();
        RequestRepaint();
    }
    // ★ Step 1：会话数变了 ⇒ 列表必须重建（新行 + 每行的 `×` 可用性都可能变）。
    //   只置脏：本函数可能由「＋ 新建会话」按钮的点击回调进来（派发栈上，不能拆树）。
    m_sidebarDirty = true;
}

void HarnessApp::ContinueCurrentSession() {
    if (!m_sess) return;

    // ★★ L2 持久化：若当前会话为已恢复的 journal 只读会话，且指定了 corePath，并且不在 persistProbe 模式下，
    // 原位续接（Resume）历史会话
    if (m_sess->originKind == "journal" && !m_options.corePath.empty() && !m_options.persistProbe) {
        if (ResumeSession(*m_sess)) {
            return;
        }
    }

    const std::string wsPath = m_sess->workspacePath;
    const std::string provId = m_sess->meta.providerId;
    const std::string model = m_sess->meta.modelName;

    // 1. 获取工作区实体
    Workspace* ws = nullptr;
    if (!wsPath.empty()) {
        ws = AcquireWorkspace(wsPath);
    }
    const std::string wsId = ws ? ws->id : std::string();

    // 2. 新建会话（live 下优先按工作区拉起新会话）
    if (!m_options.corePath.empty() && !wsId.empty()) {
        NewTemporarySession(wsId);
    } else {
        NewSession();
        if (ws && m_sess) {
            AssignSessionWorkspace(*m_sess, ws);
            m_sess->workspacePath = ws->path;
        }
    }

    // 3. 继承提供商与模型（若模型仍在配置中）
    if (!provId.empty() && !model.empty() && m_sess) {
        const ProviderConfig* pc = m_appConfig.Find(provId);
        bool found = false;
        if (pc) {
            for (const auto& m : pc->models) {
                if (m.name == model) {
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            if (m_sess->writer && m_sess->protocolVer >= 4) {
                SendModelSwitch(provId, model);
            } else {
                m_sess->queuedModelProvider = provId;
                m_sess->queuedModelName = model;
                m_sess->providerId = provId;
                m_sess->modelName = model;
            }
        }
    }

    // 4. 输入框获焦
    if (m_window && m_promptBar && m_promptBar->input()) {
        m_window->SetFocusedWidget(m_promptBar->input());
    }
    m_sidebarDirty = true;
    m_hudDirty = true;
    RefreshHud();
    RebuildMemoryList();
    RequestRepaint();
}

bool HarnessApp::ResumeSession(Session& s) {
    if (m_options.corePath.empty() || s.sessionId.empty()) return false;
    PauseActiveTimedClock();

    auto child = std::make_unique<ChildSource>();
    std::string error;

    CacheProviderKeys();
    for (const auto& pc : m_appConfig.providers) {
        const auto keyIt = m_providerKeys.find(pc.id);
        if (keyIt == m_providerKeys.end()) continue;
        const std::string san = EnvSanitizeProviderId(pc.id);
        SetEnvironmentVariableW(
            ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_ID").c_str(),
            ECDI::UTF8ToWide(pc.id).c_str());
        SetEnvironmentVariableW(
            ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_URL").c_str(),
            ECDI::UTF8ToWide(pc.baseUrl).c_str());
        SetEnvironmentVariableW(
            ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_KEY").c_str(),
            ECDI::UTF8ToWide(keyIt->second).c_str());
        SetEnvironmentVariableW(
            ECDI::UTF8ToWide("ECDI_PROVIDER_" + san + "_MODEL").c_str(),
            ECDI::UTF8ToWide(pc.ActiveModelName()).c_str());
    }
    if (!m_coreApiKey.empty()) {
        SetEnvironmentVariableW(L"ECDI_CORE_KEY", ECDI::UTF8ToWide(m_coreApiKey).c_str());
    }

    std::string args = m_options.coreArgs;
    args += " --resume " + s.sessionId;
    if (!s.workspacePath.empty()) {
        args += " --workspace \"" + s.workspacePath + "\"";
    }

    if (!child->Start(m_options.corePath, args, error)) {
        ECDI::Logger::Log(ECDI::LogLevel::Error,
                          ECDI::UTF8ToWide("ResumeSession Start failed: " + error));
        return false;
    }

    s.child = child.get();
    s.source = std::move(child);
    s.writer = std::make_unique<ProtocolWriter>(*s.source);
    s.originKind = "live";
    s.journalPromoted = true;

    if (!s.journal || !s.journal->IsOpen()) {
        s.journal = std::make_unique<SessionStore::JournalWriter>();
        std::string jerr;
        s.journal->Open(SessionStore::JournalPath(s.workspacePath, s.sessionId), jerr);
    }

    InstallWakeHandler(s);
    if (m_promptBar) {
        m_promptBar->SetReadOnlyMode(false);
    }
    if (m_hud.continueBtn) {
        m_hud.continueBtn->SetVisible(false);
    }
    if (m_window && m_promptBar && m_promptBar->input()) {
        m_window->SetFocusedWidget(m_promptBar->input());
    }
    m_sidebarDirty = true;
    m_hudDirty = true;
    RefreshHud();
    RebuildMemoryList();
    RequestRepaint();
    return true;
}

// 切换活动会话（0d）：拆当前视图 → 指向目标会话 → 按目标会话模型重建。
void HarnessApp::SwitchToSession(size_t index) {
    if (index >= m_sessions.size()) return;
    if (index == m_activeIndex && m_sess == m_sessions[index].get()) return;   // 已在该会话

    // ★★ 2026-10-20：临时会话在切走时消失（用户裁决 m09443）。
    //   必须在改 `m_activeIndex` **之前**：此时 `m_sess` 还指着旧会话，
    //   才能判断"旧会话是不是临时的"。
    // ★★ 2026-10-27：但那次摘除会让传进来的 `index` **漂**（它后面的元素整体前移；
    //   若目标本来就是最后一项，`index` 直接越界）。所以先抓住目标对象的指针，
    //   摘完之后按身份重解下标——不再假设 `index` 还有效。
    Session* want = m_sessions[index].get();

    MaybeDeleteTemporarySession();

    // ① 冻结当前活动会话的 timed 时钟（切走 ⇒ 它不该继续"到期"）
    PauseActiveTimedClock();
    // ①b ★ 在**还指着旧会话**时把视图状态同步进旧会话的模型。
    //    顺序要点：RebuildTranscriptView 内部也会 SyncRowViewState，但那时 m_sess
    //    已指向新会话——同步的是新会话的旧行，旧会话的状态就没人管了。眼下不出事
    //    只是因为 ToggleRow/MarkAnswered 每次点击都已写回模型；把正确性押在
    //    "别处恰好也写了"上是本仓最反对的形状，故在这里显式补上。
    SyncRowViewState();

    // ② 切换活动指针。m_sess 是全部 200+ 处调用点的"当前会话"锚，
    //    改这一个指针，OnReplayTick/RefreshHud/… 立即指向新会话。
    //    `want` 不是旧 `m_sess`（`index != m_activeIndex`，否则上面已经早返回），
    //    所以它一定没被 MaybeDeleteTemporarySession 摘掉 ⇒ 必然能在表里找到。
    const auto wantIt = std::find_if(
        m_sessions.begin(), m_sessions.end(),
        [want](const std::unique_ptr<Session>& p) { return p.get() == want; });
    if (wantIt == m_sessions.end()) return;
    m_activeIndex = static_cast<size_t>(wantIt - m_sessions.begin());
    m_sess = want;

    // ③ 按新会话的模型重建转录视图（复用 0c 的 RebuildTranscriptView）。
    //    ★ 这正是 0c 存在的全部理由：没有"从模型重建"，切会话就无处安放视图。
    //    传 nullptr ⇒ 按 timeline 正向重建（不是负对照）。
    //    ★ 顺带清掉与旧会话绑定的渲染缓存：环色档位（否则新会话首帧可能不重绘）。
    m_ctxRingLevel = -1;
    RebuildTranscriptView();

    // ④ 恢复新会话的 timed 时钟（补偿它被冻结的墙上时间）
    ResumeSessionTimedClock(*m_sess);

    // ⑤ HUD / 进度 / 使能全部跟着新会话刷新
    // ★ Step 1：活动项变了 ⇒ 侧栏的选中态也变了。同样只置脏、不在这里重建
    //   （本函数可能从行点击的回调里进来，那时拆列表 = 拆掉正在执行回调的那个 Button）。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RefreshHud();
    RebuildMemoryList();
    RequestRepaint();
}

std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionRow(size_t index, bool active) {
    // ★ 纯函数：只读 m_sessions[index] 与传入的 active，不写任何成员。
    auto row = std::make_unique<ECDI::Button>();
    const Session* session = m_sessions[index].get();
    // ★★ 2026-10-20：归档。非归档行有「↓」(归档) + 「×」(删除)；
    //   归档行有「↩」(恢复) + 「×」(删除，若 canDelete)。
    const bool canDelete = m_sessions.size() > 1;
    const bool isArchived = session->archived;

    // 选中态 = 三种视觉同时变（底色/圆点/文字色），但**只有文字色可读回**：
    //   ECDI 的 Button/Panel 都没有 style getter（`Button::SetStyle` 只写不读），
    //   所以探针要靠 `Label::GetTextColor()` 判"这一行是不是选中项"。
    //   ⇒ 文字色不是纯装饰，它同时承担**读数**职责，改配色时别把它改没了。
    row->SetSize(kSidebarRowW, kSidebarRowH);
    row->SetStyle(ECDI::ButtonStyleOverride{
        .background = active ? kUserBg : kPanel2,
        .border = active ? harness::kDarkAccent : kLine,
        .borderWidth = 1.0f,
        .cornerRadius = 8.0f,
        .textAlignment = ECDI::TextAlignment::Left,   // 行根不带文字，显式给值防依赖零值
        .foreground = active ? kAccentInk : kDarkInk});

    auto dot = uikit::Dot(7, active ? harness::kDarkAccent : kLine);
    dot->SetPosition(8, (kSidebarRowH - 7) / 2);
    row->AddChild(std::move(dot));

    // 名称 = `Session::label`（「会话 N」/ 夹具名 /「xxx (live)」）。
    // ★ 不做截断：文本原样进控件，探针才能拿它跟模型逐字比对（A==B 判据）；
    //   装不下的部分由渲染层的控件矩形裁剪（视觉上是截断，数据上没有）。
    // ★ 宽度随「有没有 ×」变：`×` 占 18 + 右边距 6 ⇒ 有删除钮时名称只能到 104，
    //   没有时（只剩一个会话）可以一直铺到行右边距。写死 104 会让单会话下的
    //   `demo-session.jsonl` 尾部被裁掉一个字符——**探针读文字读不到这件事**
    //   （`GetText()` 返回完整文本，裁剪发生在绘制层），只能靠截图发现。
    // ★ 宽度随「有没有按钮」变：两个按钮占 20+18+6=44 ⇒ 名称到 90；
    //   一个按钮占 18+6=24 ⇒ 名称到 110；无按钮 ⇒ 铺到行右边距。
    const int nameW = (canDelete || isArchived)
        ? ((canDelete && !isArchived) || (isArchived && canDelete) ? 90 : 110)
        : (kSidebarRowW - 22 - 2);
    std::string rowLabel = session->label.empty() ? "会话" : session->label;
    if (session->originKind == "journal") {
        rowLabel += " [只读]";
    }
    auto name = uikit::Label(rowLabel,
                             nameW, 16,
                             active ? kAccentInk : (session->originKind == "journal" ? kDarkMuted : kDarkInk),
                             harness::kFontSmall);
    name->SetPosition(22, (kSidebarRowH - 16) / 2);
    // ★ 名称标签禁用 ⇒ 它**不参与命中**（`Widget::HitTest` 的 `!IsEnabled()` 早退，
    //   `Widget.cpp:321`），鼠标落点直接归行根 Button。两个后果都是想要的：
    //   ① 行根的 hover/press 视觉真的会亮（若 Label 抢了命中，`UpdateHoverState`
    //      只通知 Label，祖先收不到 `OnMouseEnter` ⇒ 整行点上去毫无反馈）；
    //   ② 探针/自动化点"行中心"时命中的一定是行根，读数不依赖文本是否恰好被点中。
    //   ★ `IsEnabled` 全库只 gate 命中 / 焦点收集 / 键派发，**无任何绘制消费者**
    //     （`Widget.h:197`）⇒ 禁用不会让文字变灰，视觉零变化。
    name->SetEnabled(false);
    row->AddChild(std::move(name));

    // ★★ 2026-10-20：归档行的按钮布局。
    //   非归档 + canDelete：「↓」(归档) 在 x=114，「×」(删除) 在 x=134。
    //   归档 + canDelete：「↩」(恢复) 在 x=114，「×」(删除) 在 x=134。
    //   归档 + !canDelete：「↩」(恢复) 在 x=134（唯一按钮）。
    if (isArchived) {
        auto restore = std::make_unique<ECDI::Button>("↩");
        restore->SetSize(18, 18);
        const int restoreX = canDelete ? (kSidebarRowW - 44) : (kSidebarRowW - 24);
        restore->SetPosition(restoreX, (kSidebarRowH - 18) / 2);
        restore->SetFont(harness::SansFont(harness::kFontSmall));
        restore->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        restore->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    RestoreSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(restore));
    } else if (canDelete) {
        auto archive = std::make_unique<ECDI::Button>("↓");
        archive->SetSize(18, 18);
        archive->SetPosition(kSidebarRowW - 44, (kSidebarRowH - 18) / 2);
        archive->SetFont(harness::SansFont(harness::kFontSmall));
        archive->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        archive->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    ArchiveSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(archive));
    }

    if (canDelete) {
        // ★ `×`(U+00D7) 是 symcand 实测两主题都有字形的少数符号之一
        //   （`✕`(U+2715)/`✖` 在浅色 YaHei 缺字形 ⇒ 会画成空白）。
        auto del = std::make_unique<ECDI::Button>("×");
        del->SetSize(18, 18);
        del->SetPosition(kSidebarRowW - 24, (kSidebarRowH - 18) / 2);
        del->SetFont(harness::SansFont(harness::kFontSmall));
        del->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        // ★ 捕获的是**会话身份**（Session*），不是 index：删除会让下标整体前移，
        //   捕获下标的行会"点了第 2 行却删掉第 3 个会话"。指针由 unique_ptr 持有、
        //   从不搬动 ⇒ 点击时现查下标才是对的（查不到就什么都不做，见下）。
        del->SetOnClick([this, session] {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    DeleteSessionAt(i);
                    break;
                }
            }
        });
        row->AddChild(std::move(del));
    }

    // 整行点击 = 切到该会话（非归档）或恢复（归档）。同样按身份解析下标。
    row->SetOnClick([this, session] {
        if (session->archived) {
            for (size_t i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions[i].get() == session) {
                    RestoreSessionAt(i);
                    break;
                }
            }
        } else {
            SwitchToSessionByPtr(session);
        }
    });
    return row;
}

// ★★ 2026-10-20：工作区分组标题行。
//   两种行长得不一样，但**只差在尺寸与颜色**，不需新控件类型：
//   `SnapshotSessionList` 靠“行根是不是 Button”区分两类行（第 5 段 `kind`）。
std::unique_ptr<ECDI::Widget> HarnessApp::BuildSessionGroupRow(const std::string& title,
                                                               const std::string& workspaceId) {
    auto row = std::make_unique<ECDI::Panel>();
    row->SetSize(kSidebarRowW, kSidebarGroupH);
    // 透明底 + 无边框：标题是分隔，不是按钮。透明度用 alpha=0（与 `×` 子钮同一手法）。
    row->SetStyle(ECDI::PanelStyleOverride{.background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
                                           .cornerRadius = 0.0f,
                                           .borderWidth = 0.0f});

    // 标题文字 = 工作区路径（= core 回显的规范化结果，不是 GUI 自己拼的）。
    //   ★ 用 Mono 字体：路径里的 `l/1/I`、`O/0` 必须一眼可分，否则两个相似路径的
    //     工作区分组在屏幕上就等于同一行（这正是“按工作区区分”要解决的那个问题）。
    //   ★★ 必须**自己**做中间省略（`ElideMiddleByCodepoints`）：`ECDI::Label` 对超宽
    //     文本是**尾部丢弃**——截图实测 `D:/Project/ECDI_fake_harness` 被画成
    //     `D:/Project/ECDI_fake_harn`（少了 3 个字），而两个工作区的差异几乎总在叶子段。
    //     尾部一丢，“按工作区区分”就退化成“按前缀区分”：两个都在 `D:/Project/` 下的
    //     工作区在屏幕上会是同一行。把省略放在中间，尾部至少保留叶子段。
    // ★ 有「＋」按钮时标题宽度让出 22px（按钮 18 + 间隙 4）。
    const int labelW = workspaceId.empty() ? (kSidebarRowW - 8) : (kSidebarRowW - 30);
    // ★★ 省略预算 = 实测校准（修法 D，DPI 可移植）：预算天然跟着 labelW 走——
    //   有按钮时可用宽少 22px，预算自动少 ~3-4 码点，不再需要手工 −1 补丁。
    const size_t elideCp = ElideBudgetCp(labelW);
    const std::string shown = ElideMiddleByCodepoints(title, elideCp);
    auto label = uikit::Label(shown, labelW, kSidebarGroupH - 4, kDarkMuted,
                              harness::kFontMicro, true);
    label->SetPosition(8, 2);
    // 禁用：标题不参与命中（`Panel::ContainsPoint` 已经恒 false，这里只是让
    //   “文字不是交互对象”也成立；无绘制消费者 ⇒ 不变灰）。
    label->SetEnabled(false);
    row->AddChild(std::move(label));

    // ★★ 「＋」按钮：拉起该工作区的临时会话（m09443）。
    //   行根是 Panel（`ContainsPoint` 恒 false）⇒ 只有这个子 Button 能被点到。
    //   回调捕获 workspaceId（稳定字符串），不捕获下标。
    if (!workspaceId.empty()) {
        auto plus = std::make_unique<ECDI::Button>();
        plus->SetText("＋");
        plus->SetSize(18, 18);
        plus->SetPosition(kSidebarRowW - 24, (kSidebarGroupH - 18) / 2);
        plus->SetFont(harness::SansFont(harness::kFontSmall));
        plus->SetStyle(ECDI::ButtonStyleOverride{
            .background = ECDI::Color::FromRGBA8(0, 0, 0, 0),
            .borderWidth = 0.0f,
            .cornerRadius = 4.0f,
            .pressedBackground = kLine,
            .hoverBackground = kLine,
            .textAlignment = ECDI::TextAlignment::Center,
            .foreground = kDarkMuted});
        plus->SetOnClick([this, workspaceId] {
            NewTemporarySession(workspaceId);
        });
        row->AddChild(std::move(plus));
    }
    return row;
}

std::vector<size_t> HarnessApp::GroupTitlesForDisplay() const {
    std::vector<size_t> order;
    order.reserve(m_workspaces.size());
    // 创建序 = `m_workspaces` 下标序（只追加、不删、不重排）；显示序 = 它的逆序。
    //   `rbegin/rend` 而不是 `std::sort`：排序需要一个键，而这里根本没有键——
    //   唯一的规则就是“后建在上”，那就是逆序本身。
    for (auto it = m_workspaces.rbegin(); it != m_workspaces.rend(); ++it) {
        order.push_back(static_cast<size_t>(m_workspaces.rend() - it - 1));
    }
    return order;
}

void HarnessApp::RebuildSessionList(const std::vector<size_t>* orderOverride) {
    if (!m_hud.sessList) return;
    ECDI::Widget& content = m_hud.sessList->GetContentView();

    // ★ 与 RebuildTranscriptView 同一条纪律：拆干净再装，不做增量比对。
    //   `RemoveChild` 会先 `ForgetSubtree` 注销焦点/捕获/hover 再交回所有权
    //   （`Widget.cpp` RemoveChild），故这里不需要手工清理状态。
    while (content.GetChildCount() > 0) {
        content.RemoveChild(content.GetChildAt(0));
    }

    const size_t n = m_sessions.size();
    // ★★ 2026-10-20：行高不再统一（分组标题 `kSidebarGroupH`=18、会话行 `kSidebarRowH`=28）
    //   ⇒ 位置只能逐行累加。再用「下标 × 步进」会把标题按会话行高排，越往下偏得越多。
    int y = 0;
    int lastGap = 0;
    auto place = [&](std::unique_ptr<ECDI::Widget> row, int h) {
        row->SetPosition(0, y);
        content.AddChild(std::move(row));
        y += h + kSidebarRowGap;
        lastGap = kSidebarRowGap;
    };

    if (m_workspaces.empty()) {
        // ★ 一个工作区都没有 ⇒ **平铺**，与分组功能出现之前逐字节同形。
        //   为什么不画一个“未归属工作区”标题凑数：没有工作区实体时那行标题只是
        //   在给“什么都没有”起名字，纯装饰；何况回放会话**永远不会**有工作区
        //   （`MaybeStartWorkspaceSync` 首行 `!writer` 就 return），给它们编一个
        //   “未归属”分组等于在界面上宣告一个不存在的关系。
        for (size_t i = 0; i < n; ++i) {
            size_t idx = i;
            if (orderOverride) {
                if (orderOverride->size() != n) break;   // 形状不对就不画（探针会给同长度）
                idx = (*orderOverride)[i];
                if (idx >= n) break;
            }
            if (m_sessions[idx]->archived) continue;  // ★★ 归档会话不进主列表
            const bool active = (idx == m_activeIndex);
            place(BuildSessionRow(idx, active), kSidebarRowH);
        }
    } else {
        // ★ 分组：标题按**创建序逆序**（后建在上，用户裁决 m08553），组内会话按
        //   `m_sessions` 序（创建序正序）。两个方向都是固定的，不按“最近使用”重排——
        //   任何动态排序都会让同一个工作区在两次刷新之间换位置，用户会以为它是两个。
        //   ★ 分组模式下 `orderOverride` **不生效**：它表达的是“会话次序”，
        //     而分组结构下会话次序由“属于哪个组”决定。分组模式的负对照改用
        //     “换工作区路径”（见 `RunSidebarProbe` ⑬），只改一个自变量。
        std::vector<bool> shown(n, false);
        for (size_t wi : GroupTitlesForDisplay()) {
            const Workspace& ws = *m_workspaces[wi];
            const std::string title = ws.unavailable ? (ws.path + " [不可用]") : ws.path;
            place(BuildSessionGroupRow(title, ws.id), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (m_sessions[i]->workspaceId != ws.id) continue;
                if (m_sessions[i]->archived) { shown[i] = true; continue; }  // ★★ 归档会话不进主列表
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
                shown[i] = true;
            }
        }
        bool anyOrphan = false;
        for (size_t i = 0; i < n; ++i) {
            if (!shown[i] && !m_sessions[i]->archived) { anyOrphan = true; break; }
        }
        if (anyOrphan) {
            // ★ 外键指向一个不存在的工作区也落在这里（`WorkspaceById` 返回 nullptr 时
            //   就是这么处理的，两处口径一致）：宁可显示“未归属”，也不能把它悄悄
            //   塞进第一个工作区——那会把归错桶伪装成正常。
            place(BuildSessionGroupRow("未归属工作区", std::string()), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (shown[i] || m_sessions[i]->archived) continue;
                place(BuildSessionRow(i, i == m_activeIndex), kSidebarRowH);
            }
        }
    }

    // ★★ 2026-10-20：「归档」区。有归档会话时才画标题 + 行。
    //   归档会话留在 m_sessions 原位（下标不动），只是从主列表隐藏、移到这里。
    //   行用 BuildSessionRow（archived=true 分支画「↩」+「×」）。
    {
        bool anyArchived = false;
        for (size_t i = 0; i < n; ++i) {
            if (m_sessions[i]->archived) { anyArchived = true; break; }
        }
        if (anyArchived) {
            place(BuildSessionGroupRow("归档", std::string()), kSidebarGroupH);
            for (size_t i = 0; i < n; ++i) {
                if (!m_sessions[i]->archived) continue;
                place(BuildSessionRow(i, false), kSidebarRowH);
            }
        }
    }
    const int totalH = y > 0 ? y - lastGap : 0;
    // 高度上限仍按“4 个会话行”折算成像素：分组标题不算一行（它是分隔，不是条目），
    //   否则两个工作区就会把可见区吃掉一半。
    const int visibleH = std::min(totalH, kSidebarMaxRows * (kSidebarRowH + kSidebarRowGap)
                                              - kSidebarRowGap);
    // ★ 先 SetSize（决定 viewport）再 SetContentExtent（决定要不要出条）：
    //   反过来的话第一帧会按旧 viewport 判一次"不需要滚动条"，出条后行宽被挤。
    m_hud.sessList->SetSize(kSidebarListW, std::max(visibleH, kSidebarRowH));
    m_hud.sessList->SetContentExtent(kSidebarRowW, std::max(totalH, kSidebarRowH));
    m_hud.sessList->SetContentOffset(0, m_hud.sessList->GetScrollOffsetY());

    // 列表高度是**变**的（会话越多越高，到 4 行封顶）⇒ 侧栏那条 VerticalLayout
    // 必须重排一次，否则下面的「项目」段还停在旧位置。
    if (ECDI::Widget* parent = m_hud.sessList->GetParent()) parent->Arrange();
}

std::vector<std::string> HarnessApp::SnapshotSessionList() const {
    std::vector<std::string> out;
    if (!m_hud.sessList) return out;
    const ECDI::Widget& content = m_hud.sessList->GetContentView();
    for (size_t i = 0; i < content.GetChildCount(); ++i) {
        const ECDI::Widget* row = content.GetChildAt(i);
        if (row == nullptr) continue;
        std::string name;
        std::string selected = "0";
        size_t buttons = 0;
        for (size_t c = 0; c < row->GetChildCount(); ++c) {
            const ECDI::Widget* child = row->GetChildAt(c);
            if (dynamic_cast<const ECDI::Button*>(child)) ++buttons;
            if (const auto* lbl = dynamic_cast<const ECDI::Label*>(child)) {
                // 第一个 Label 就是名称（圆点是 Panel、`×` 是 Button，都不满足本分支）
                if (name.empty()) {
                    name = lbl->GetText();
                    selected = (lbl->GetTextColor() == kAccentInk) ? "1" : "0";
                }
            }
        }
        // 字段序：名称 · 选中 · 行内按钮数（×）· 行矩形 · ★行类（kind）。
        // 行矩形也算视图的一部分：「画了几行」和「画在哪」都能被抓出来。
        // ★★ 2026-10-20：`kind` **只能追加在尾部**。若插在中间，所有历史读数会在
        //   字段位上静默串味（旧日志把 geometry 当 kind 读，不报错但全错）。
        //   判据来源 = 行根类型：会话行是 Button（`Panel::ContainsPoint` 恒 false，
        //   可点行的根只能是 Button），分组标题行是 Panel。这就是两类行在视图层的
        //   唯一区别，不靠尺寸（尺寸会随主题变）。
        char geo[64];
        std::snprintf(geo, sizeof(geo), "%d,%d,%d,%d", row->GetX(), row->GetY(),
                      row->GetWidth(), row->GetHeight());
        const char* kind = dynamic_cast<const ECDI::Button*>(row) ? "session" : "group";
        out.push_back(name + '\x1f' + selected + '\x1f' + std::to_string(buttons) + '\x1f'
                      + geo + '\x1f' + kind);
    }
    return out;
}

HarnessApp::SidebarCompare HarnessApp::CompareSessionList(
    const std::vector<std::string>& before, const std::vector<std::string>& after) const {
    static const char* kFieldName[] = {"name", "selected", "buttons", "geometry", "kind"};
    SidebarCompare cmp;
    if (before.size() != after.size()) {
        cmp.matched = false;
        cmp.firstDivergence = "rowCount " + std::to_string(before.size()) + " -> "
                              + std::to_string(after.size());
        return cmp;
    }
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] == after[i]) continue;
        cmp.matched = false;
        // 逐字段定位第一处差异（与转录区 CompareRows 同款可读性：别只报"不一样"）
        auto split = [](const std::string& s) {
            std::vector<std::string> parts;
            std::string cur;
            for (char ch : s) {
                if (ch == '\x1f') { parts.push_back(cur); cur.clear(); }
                else cur.push_back(ch);
            }
            parts.push_back(cur);
            return parts;
        };
        const std::vector<std::string> a = split(before[i]);
        const std::vector<std::string> b = split(after[i]);
        std::string detail = "row " + std::to_string(i);
        // 段落数必须与 `kFieldName` 同长：写死 4 会让新增的 `kind` 永远不参与比对，
        //   跟“没加”一样（而且不报错）。
        constexpr size_t kFieldCount = sizeof(kFieldName) / sizeof(kFieldName[0]);
        for (size_t f = 0; f < a.size() && f < b.size() && f < kFieldCount; ++f) {
            if (a[f] == b[f]) continue;
            detail += std::string(" ") + kFieldName[f] + " [" + a[f] + "] -> [" + b[f] + "]";
            break;
        }
        cmp.firstDivergence = detail;
        return cmp;
    }
    return cmp;
}

bool HarnessApp::SwitchToSessionByPtr(const Session* target) {
    if (target == nullptr) return false;
    for (size_t i = 0; i < m_sessions.size(); ++i) {
        // ★ 只比地址、不解引用：这一行可能在会话刚被删掉、列表还没重建的那一帧里
        //   被点到（时序见 RebuildSessionList 头注的 UAF 段），此时 target 已悬垂，
        //   任何解引用都是 UB；比地址是安全的，查不到就什么都不做。
        if (m_sessions[i].get() == target) {
            SwitchToSession(i);
            return true;
        }
    }
    return false;
}

bool HarnessApp::DeleteSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    // ★ 禁止删到零会话：`m_sess` 变空指针会让全仓 200+ 处 `m_sess->` 解引用崩溃。
    //   "支持零会话"是一个独立的架构决定（每个调用点加判空），不该被一个删除按钮带进来。
    if (m_sessions.size() <= 1) return false;

    Session* target = m_sessions[index].get();
    const bool deletingActive = (index == m_activeIndex);
    if (deletingActive) {
        // ★★ 顺序不能换：先把活动指针挪到邻居（SwitchToSession 内部会
        //    SyncRowViewState → 重建转录区 → 完成切换），**之后**才 erase。
        //    反过来的话，`SwitchToSession` 会在旧 Session 已被销毁的情况下
        //    去读它的 rows/timeline（那里存的是裸指针）⇒ UAF。
        const size_t neighbor = (index + 1 < m_sessions.size()) ? index + 1 : index - 1;
        SwitchToSession(neighbor);
    }

    // ★★ 2026-10-27：删的是「当前活动的**临时**会话」时，上面那次 SwitchToSession
    //   里的 MaybeDeleteTemporarySession 已经把 `target` 自己摘掉了（临时会话切走即消失）
    //   ⇒ 进来时的 `index` 这时指向的是**另一个会话**（表短了 1）或干脆是 `end()`。
    //   按下标继续 erase 会删错一个会话；表被清空后 `size() - 1` 下溢成 SIZE_MAX，
    //   下一行 `m_sessions[SIZE_MAX]` 就是 0xC0000005。⇒ 一律按**对象身份**找，
    //   找不到就说明已经删过了，什么都不做（用户点的 × 已经生效，不该再动别人）。
    //   取证：`.scratch/_repro_tempdel3.py` 七条 live 序列，修前 5 条 0xC0000005，修后 0 条。
    const auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                                 [target](const std::unique_ptr<Session>& p) { return p.get() == target; });
    if (it != m_sessions.end()) {
        // ★★ 2026-10-28：删除已落盘会话时同步清理磁盘四件套（事实 §2）
        if (!target->workspacePath.empty() && !target->sessionId.empty() && target->journalPromoted) {
            std::string delErr;
            SessionStore::DeleteSession(target->workspacePath, target->sessionId, delErr);
        }
        m_sessions.erase(it);
    }

    // 活动下标同样按身份重取：`m_sess` 一定还在这张表里（切换后指向邻居，或者本来就没切），
    // 而 erase + MaybeDelete 一共可能前移一次，±1 算术扛不住两次变动。
    for (size_t i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].get() == m_sess) {
            m_activeIndex = i;
            break;
        }
    }

    // ★ 不在事件派发栈上拆树（见本段头注的 UAF 段）：只置脏，重建交给下一个 tick。
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

// ★★ 2026-10-20：归档 / 恢复（m00001 第 6 步残留）。
//   归档 = 「不活动但可打开且不占资源」。会话留在 m_sessions 原位（下标不动），
//   只是从侧栏主列表隐藏、移到「归档」区。
bool HarnessApp::ArchiveSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    Session* target = m_sessions[index].get();
    if (target->archived) return false;  // 已归档，不重复操作

    const bool archivingActive = (index == m_activeIndex);
    if (archivingActive) {
        // ★ 归档活动会话 ⇒ 先切到邻居（同 DeleteSessionAt 的纪律）。
        //   邻居必须是**非归档**的：切到一个归档会话会让活动指针指向
        //   一个不在主列表里的会话，HUD 会显示用户看不到的东西。
        size_t neighbor = static_cast<size_t>(-1);
        for (size_t i = index + 1; i < m_sessions.size(); ++i) {
            if (!m_sessions[i]->archived) { neighbor = i; break; }
        }
        if (neighbor == static_cast<size_t>(-1)) {
            for (size_t i = 0; i < index; ++i) {
                if (!m_sessions[i]->archived) { neighbor = i; break; }
            }
        }
        if (neighbor == static_cast<size_t>(-1)) {
            // ★ 所有其他会话都已归档 ⇒ 不能归档最后一个可见会话。
            //   理由同「禁删到零会话」：活动指针必须指向一个用户能看见的会话。
            return false;
        }
        // ★ 若被归档的是临时会话，先取消临时标志：否则 SwitchToSession 里的
        //   MaybeDeleteTemporarySession 会把它**删掉**而不是归档。
        target->temporary = false;
        SwitchToSession(neighbor);
    }

    target->archived = true;
    target->meta.archived = true;
    SyncSessionMeta(*target);
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}

bool HarnessApp::RestoreSessionAt(size_t index) {
    if (index >= m_sessions.size()) return false;
    Session* target = m_sessions[index].get();
    if (!target->archived) return false;  // 未归档，不重复操作

    target->archived = false;
    target->meta.archived = false;
    SyncSessionMeta(*target);
    // ★ 恢复 = 取消归档 + 切到它（用户点「↩」的心智模型是「打开它」）。
    SwitchToSession(index);
    m_sidebarDirty = true;
    m_hudDirty = true;
    RequestRepaint();
    return true;
}
