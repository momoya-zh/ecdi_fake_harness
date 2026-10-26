// ============================================================
// 设置面板 / 模型热切换 / 凭据缓存（P3.6 + P4）
// ------------------------------------------------------------
// 为什么单独一个文件：设置链是**唯一会读环境变量、写配置文件、碰凭据库**的
//   产品路径（OnSettingsSave 一处就有 105 行）。它与转录/布局完全无关，
//   却和「模型 chip 显示什么」这种高频 UI 改动相邻——独立出来两边都清爽。
// ★ 纪律：`OnSettingsSave` 的**校验顺序**是用户可见行为（先报哪个错），
//   任何重排都要当作行为变更，走门 4 而不是当重构。
// ★ 2026-10-25：从 App.cpp 切出（Stage 1c）。函数体逐字节相同。
// ============================================================

#include "App.h"
#include "AppConfig.h"
#include "Credentials.h"   // creds::（API key 存取）
#include "AppInternal.h"
#include "ThemeUsing.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"   // ECDI::UTF8ToWide（日志与配置路径）
// ★ 2026-10-25（Stage 3 刀③）：App.h 不再传递包含控件头，用到就自己包。
#include "ECDI/Widget/ScrollView.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

void HarnessApp::ToggleMoreMenu() { SetMoreMenuOpen(!m_moreMenuOpen); }

void HarnessApp::SetMoreMenuOpen(bool open) {
    // ★ 幂等：同值不做事。否则每次重建都 Arrange 一次就白掉一帧。
    if (m_moreMenuOpen == open) return;
    m_moreMenuOpen = open;
    if (m_hud.moreMenu) {
        m_hud.moreMenu->SetVisible(open);
        // ★ 必须重排**父容器**（`counting`）：布局跳过隐藏子（C-VIS-1），
        //   但「跳过」只在 Arrange 时发生。不 Arrange 的话菜单条消失后
        //   那个 34px 的槽位会留着——转录区凭空缩进一块。
        if (ECDI::Widget* parent = m_hud.moreMenu->GetParent()) parent->Arrange();
        Relayout();
        RequestRepaint();
    }
}

void HarnessApp::ApplySettingsVisibility() {
    // 唯一的投影路径：BuildUi 首帧与 RebuildUi 重建都走这里——
    // 「投影只存在于一条路径」是 2026-10-21 起始幻影（34px 空洞）的教训。
    if (!m_hud.settingsPanel) return;
    m_hud.settingsPanel->SetVisible(m_settingsVisible);
    if (m_hud.settingsMask) m_hud.settingsMask->SetVisible(m_settingsVisible);
    // ★★ P4 F（行为变化点）：不再隐藏 scroll/composer——设置是浮在视口上的
    //   模态层，流式输出在半透明遮罩后继续可见/继续滚动（用户验收过原型）。
    //   --settings-probe 的「腾槽」判据已同步迁移为「scroll 必须仍可见」。
    // 三件套互斥（menu/settings）：开设置合菜单——菜单还是槽位控件，两者同开
    //   会一个占槽一个浮层，视觉关系说不清。
    if (m_settingsVisible && m_moreMenuOpen) SetMoreMenuOpen(false);
    // ★ P4 Phase I：开设置合预览（两套模态不同开；反向在 ApplyPreviewVisibility）。
    if (m_settingsVisible && m_previewOpen) SetPreviewVisible(false);
    LayoutSettingsOverlay();
}

// ★★ P4 F：遮罩 + 居中 sheet 的几何。★ 修复（实测反馈）：**遮罩盖全窗**
//   （mask+sheet 挂 root 末尾，画过 caption/counting 全部；此前挂 m_scroll
//   只盖转录视口）——sheet 尺寸改按 root 算。浮层无父布局 ⇒ SetSize 后必须
//   自己 Arrange（ArrangeInternal 递归驱动整棵子树布局——字段区由此摆位），
//   **再 UpdateContentExtent**（字段包围盒 → content extent；漏了它 extent=0，
//   字段区不显示——首版实测反馈的根因）。
//   ★ root.Arrange()（HarnessRootLayout）**跳过浮层子**——浮层几何完全归
//   本函数管，布局与浮层互不干扰。
void HarnessApp::LayoutSettingsOverlay() {
    if (!m_hud.settingsPanel) return;
    ECDI::Widget& root = m_window->GetRootWidget();
    const int rw = root.GetWidth();
    const int rh = root.GetHeight();
    if (m_hud.settingsMask) {
        m_hud.settingsMask->SetSize(rw, rh);
        m_hud.settingsMask->SetPosition(0, 0);
    }
    const int w = std::clamp(rw - 80, 280, 620);
    const int h = std::clamp(rh - 80, 240, 560);
    m_hud.settingsPanel->SetSize(w, h);
    m_hud.settingsPanel->SetPosition((rw - w) / 2, (rh - h) / 2);
    m_hud.settingsPanel->Arrange();
    if (m_hud.settingsSv) {
        m_hud.settingsSv->UpdateContentExtent();   // 字段包围盒 → 滚动范围
    }
}

void HarnessApp::SetSettingsVisible(bool visible) {
    if (m_settingsVisible == visible &&
        (!m_hud.settingsPanel || m_hud.settingsPanel->IsVisible() == visible)) {
        return;   // 幂等（同 SetSidebarVisible）
    }
    m_settingsVisible = visible;   // ① 模型侧先落
    ApplySettingsVisibility();     // ② 再投影
    Relayout();
    RequestRepaint();
}

bool HarnessApp::ResolveCoreExecutable(std::filesystem::path& out) const {
    namespace fs = std::filesystem;
    // ① 配置覆盖（保存时把解析结果写回配置 ⇒ 后续启动不再依赖 cwd）
    if (!m_appConfig.corePath.empty()) {
        if (fs::exists(m_appConfig.corePath)) {
            out = m_appConfig.corePath;
            return true;
        }
        return false;
    }
    // ②③ 从 cwd 与 exe 目录**向上找祖先**：core/bin/ecdi-core.exe。
    //   ★ 为什么必须向上走（2026-10-24 用户实测抓到）：双击 exe 的默认 cwd =
    //     exe 所在目录（gui/build/Release），仓库根布局的 core/bin/ 在**三级之上**
    //     ⇒ 只查 cwd 和 exeDir 两个固定点，双击场景永远找不到 ⇒ 自动连接失败、
    //     首启面板里保存也失败（同一函数）⇒ "会话没有输入框"的死胡同。
    //     祖先上溯（≤6 级）同时覆盖：仓库根跑 / Release 目录双击 / 安装布局平铺。
    auto findUpward = [](const fs::path& start) -> std::filesystem::path {
        fs::path dir = fs::absolute(start);
        for (int i = 0; i < 6 && !dir.empty(); ++i) {
            const fs::path cand = dir / "core" / "bin" / "ecdi-core.exe";
            std::error_code ec;
            if (fs::exists(cand, ec) && !fs::is_directory(cand, ec)) {
                return fs::weakly_canonical(cand, ec);
            }
            const fs::path flat = dir / "ecdi-core.exe";
            if (fs::exists(flat, ec) && !fs::is_directory(flat, ec)) {
                return fs::weakly_canonical(flat, ec);
            }
            const fs::path parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
        return {};
    };
    if (std::filesystem::path found = findUpward("."); !found.empty()) {
        out = found;
        return true;
    }
    wchar_t exeDirBuf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exeDirBuf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        if (std::filesystem::path found = findUpward(fs::path(exeDirBuf).parent_path());
            !found.empty()) {
            out = found;
            return true;
        }
    }
    return false;
}

bool HarnessApp::TryAutoConnectFromConfig() {
    std::string error;
    if (!m_appConfig.Load(error)) {
        // 文件不存在 = 干净首启；存在但坏 = 要说清楚（非敏感）
        ECDI::Logger::Log(ECDI::LogLevel::Warning, ECDI::UTF8ToWide("AppConfig: " + error));
        return false;
    }
    CacheProviderKeys();
    // 默认提供商 = defaultProviderId（v2）/ 迁移后的唯一提供商（v1）。
    const ProviderConfig* def = m_appConfig.DefaultProvider();
    if (!def) return false;
    auto keyIt = m_providerKeys.find(def->id);
    if (keyIt == m_providerKeys.end()) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("no credential for default provider " + def->id));
        return false;
    }
    std::filesystem::path coreExe;
    if (!ResolveCoreExecutable(coreExe)) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning,
                          ECDI::UTF8ToWide("ecdi-core.exe not found (config/cwd/exe ancestors)"));
        return false;
    }
    // 把解析结果回写配置（下次启动不依赖 cwd）；失败不致命——本次仍可连。
    m_appConfig.corePath = coreExe.generic_string();
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        ECDI::Logger::Log(ECDI::LogLevel::Warning, ECDI::UTF8ToWide("AppConfig save: " + saveError));
    }
    m_coreApiKey = keyIt->second;
    m_options.corePath = coreExe;
    m_options.coreArgs = "--base-url=" + def->baseUrl +
                         " --model=" + def->model +
                         " --provider-id=" + def->id;
    if (def->context > 0) {
        m_options.coreArgs += " --context=" + std::to_string(def->context);
    }
    if (def->maxOutput > 0) {
        m_options.coreArgs += " --max-output=" + std::to_string(def->maxOutput);
    }
    m_options.fixtureName = def->model + " (live)";
    return true;
}

void HarnessApp::OnSettingsSave() {
    if (!m_hud.settingsBase || !m_hud.settingsKey || !m_hud.settingsModel ||
        !m_hud.settingsHint) {
        return;
    }
    const auto setHint = [this](const std::string& text, const ECDI::Color& color) {
        m_hud.settingsHint->SetText(text);
        m_hud.settingsHint->SetTextColor(color);
        RequestRepaint();
    };
    const std::string baseUrl = m_hud.settingsBase->GetText();
    const std::string key = m_hud.settingsKey->GetText();
    const std::string model = m_hud.settingsModel->GetText();
    const std::string contextText = m_hud.settingsContext ? m_hud.settingsContext->GetText() : "";
    const std::string maxOutText = m_hud.settingsMaxOut ? m_hud.settingsMaxOut->GetText() : "";
    if (baseUrl.empty() || model.empty()) {
        setHint("BaseURL 和模型名不能为空", kErrorDark);
        return;
    }
    int context = 0;
    if (!contextText.empty()) context = std::atoi(contextText.c_str());
    int maxOutput = 0;
    if (!maxOutText.empty()) maxOutput = std::atoi(maxOutText.c_str());
    const std::string providerId = AppConfig::ProviderIdFromBaseUrl(baseUrl);
    const std::string target = AppConfig::CredentialTargetFor(providerId);

    // ① 凭据：key 非空 → 覆盖写；key 为空且已有 → 保持不变（轮换语义）。
    if (!key.empty()) {
        std::string credError;
        if (!creds::Write(ECDI::UTF8ToWide(target), key, credError)) {
            setHint("凭据写入失败：" + credError, kErrorDark);
            return;
        }
    } else if (!creds::Exists(ECDI::UTF8ToWide(target))) {
        setHint("API Key 为空（凭据库中也没有该提供商的记录）", kErrorDark);
        return;
    }

    // ② 解析 core 可执行文件（结果写进配置 ⇒ 下次无参启动不依赖 cwd）。
    std::filesystem::path coreExe;
    if (!ResolveCoreExecutable(coreExe)) {
        setHint("找不到 ecdi-core.exe（先构建：go build -o core/bin/ecdi-core.exe ./cmd/ecdi-core）",
                kErrorDark);
        return;
    }

    // ③ 写配置（v2 多提供商 Upsert；结构上无密钥字段——AppConfig.h 防泄漏注释）。
    ProviderConfig pc;
    pc.id = providerId;
    pc.baseUrl = baseUrl;
    pc.model = model;
    pc.context = context;
    pc.maxOutput = maxOutput;
    pc.credentialTarget = target;
    m_appConfig.Upsert(pc);
    m_appConfig.defaultProviderId = providerId;
    if (!m_coreApiKey.empty() && m_providerKeys.find(providerId) == m_providerKeys.end()) {
        // 旧配置迁移路径：m_coreApiKey 属于原默认提供商时不得张冠李戴
    }
    if (!key.empty()) m_providerKeys[providerId] = key;
    m_appConfig.corePath = coreExe.generic_string();
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        setHint("配置写入失败：" + saveError, kErrorDark);
        return;
    }
    m_editingProviderId = providerId;

    // ④ 连接：无 live 会话 ⇒ 新建（env 注入走统一路径）；已有支持热切换的
    //    live 会话 ⇒ 发 model 切换请求（I11：排队语义由 RequestModelSwitch 处理）。
    const bool hasLive = m_sess && m_sess->originKind == "live" && m_sess->writer;
    if (hasLive && m_sess->hasModelCap) {
        auto keyIt = m_providerKeys.find(providerId);
        if (keyIt != m_providerKeys.end()) {
            m_coreApiKey = keyIt->second;   // 新 provider 成为默认 ⇒ ECDI_CORE_KEY 跟随
        }
        setHint("已保存；正在切换到 " + providerId + " / " + model, kDarkMuted);
        SetSettingsVisible(false);
        RequestModelSwitch(providerId);
        return;
    }
    if (hasLive && !m_sess->hasModelCap) {
        setHint("已保存；当前 core 不支持热切换（cap 未声明），重启会话后生效", kDarkMuted);
        SetSettingsVisible(false);
        return;
    }
    {
        auto keyIt = m_providerKeys.find(providerId);
        if (keyIt != m_providerKeys.end()) m_coreApiKey = keyIt->second;
        m_options.corePath = coreExe;
        m_options.coreArgs = "--base-url=" + baseUrl + " --model=" + model +
                             " --provider-id=" + providerId;
        if (context > 0) m_options.coreArgs += " --context=" + std::to_string(context);
        if (maxOutput > 0) m_options.coreArgs += " --max-output=" + std::to_string(maxOutput);
        Session* live = CreateSessionFromOptions();
        if (!live) {
            setHint("core 启动失败（见 core-stderr 日志）", kErrorDark);
            return;
        }
        SwitchToSessionByPtr(live);
    }
    setHint("已保存并连接：" + providerId + " / " + model, kDarkMuted);
    SetSettingsVisible(false);
    m_modelListDirty = true;
}

std::string HarnessApp::EnvSanitizeProviderId(const std::string& id) {
    // 与 core 的 sanitizeProviderID 同一条规则（非字母数字 → '_'，大写）。
    // 两份实现是必须的（环境变量名不能带 '.'），规则只此一条，注释互指。
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9');
        out.push_back(ok ? static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c) : '_');
    }
    return out;
}

void HarnessApp::CacheProviderKeys() {
    // 凭据库 → m_providerKeys（已有缓存的不覆盖——设置面板刚写入的值更新）。
    for (const auto& pc : m_appConfig.providers) {
        if (m_providerKeys.count(pc.id)) continue;
        std::string key;
        std::string err;
        if (creds::Read(ECDI::UTF8ToWide(pc.credentialTarget), key, err)) {
            m_providerKeys[pc.id] = key;
        }
    }
}

void HarnessApp::RebuildModelList() {
    if (!m_hud.modelList) return;
    while (m_hud.modelList->GetChildCount() > 0) {
        m_hud.modelList->RemoveChild(m_hud.modelList->GetChildAt(0));
    }
    // ★★ P4 H：右栏改**只读卡**（当前 提供商/模型/容量/输出上限）——切换入口
    //   收敛到顶栏 chip 浮窗（P3.6 的逐行切换列表退役；两处入口必然漂移）。
    const Session* s = m_sess;
    auto addRow = [this](const std::string& label, const std::string& value) {
        auto row = std::make_unique<ECDI::Panel>();
        row->SetSize(200, 18);
        auto l = std::make_unique<ECDI::Label>(label);
        l->SetTextColor(kDarkMuted);
        l->SetFont(harness::SansFont(harness::kFontMicro));
        l->SetSize(70, 16);
        l->SetPosition(0, 1);
        row->AddChild(std::move(l));
        auto v = std::make_unique<ECDI::Label>(value);
        v->SetTextColor(kDarkInk);
        v->SetFont(harness::SansFont(harness::kFontMicro));
        v->SetSize(130, 16);
        v->SetPosition(72, 1);
        row->AddChild(std::move(v));
        m_hud.modelList->AddChild(std::move(row));
    };
    addRow("提供商", s && !s->providerId.empty() ? s->providerId : "—");
    addRow("模型", s && !s->modelName.empty() ? s->modelName : "—");
    if (s && s->ctxCapacity > 0) {
        addRow("容量", std::to_string(s->ctxCapacity) + " tok");
    } else {
        addRow("容量", "—");
    }
    const ProviderConfig* pc =
        s ? m_appConfig.Find(s->providerId) : nullptr;
    addRow("输出上限",
           pc && pc->maxOutput > 0 ? std::to_string(pc->maxOutput) : "—");
    if (ECDI::Widget* parent = m_hud.modelList->GetParent()) parent->Arrange();
    RequestRepaint();
}

void HarnessApp::SetModelPopupOpen(bool open) {
    if (m_modelPopupOpen == open &&
        (!m_hud.modelPopup || m_hud.modelPopup->IsVisible() == open)) {
        return;   // 幂等（同 SetSidebarVisible）
    }
    m_modelPopupOpen = open;
    if (!m_hud.modelPopup) return;
    m_hud.modelPopup->SetVisible(open);
    if (open) {
        if (m_moreMenuOpen) SetMoreMenuOpen(false);   // 互斥：两处下拉不同开
        if (m_hud.modelSearch) m_hud.modelSearch->SetText("");
        RebuildModelPopupList();
        // 顶部下拉、右上角对齐（视口坐标系）。
        m_hud.modelPopup->SetPosition(
            std::max(0, m_scroll->GetWidth() - m_hud.modelPopup->GetWidth() - 12), 0);
    }
}

void HarnessApp::RebuildModelPopupList() {
    if (!m_hud.modelPopupList) return;
    while (m_hud.modelPopupList->GetChildCount() > 0) {
        m_hud.modelPopupList->RemoveChild(m_hud.modelPopupList->GetChildAt(0));
    }
    const std::string q = m_hud.modelSearch ? m_hud.modelSearch->GetText() : "";
    // 同名检测：跨提供商相同 model 名 ⇒ 标黄 + 「同名×N」（同名跨家切换容易
    // 误点，这是唯一的防呆读数）。
    std::map<std::string, int> modelCounts;
    for (const auto& pc : m_appConfig.providers) ++modelCounts[pc.model];
    const std::string active = m_sess ? m_sess->providerId : std::string();
    int y = 0;
    int rows = 0;
    for (const auto& pc : m_appConfig.providers) {
        const bool idHit = q.empty() ||
                           pc.id.find(q) != std::string::npos ||
                           pc.model.find(q) != std::string::npos;
        if (!idHit) continue;
        auto group = std::make_unique<ECDI::Label>(
            (pc.id == active ? "● " : "") + pc.id);
        group->SetTextColor(kDarkMuted);
        group->SetFont(harness::SansFont(harness::kFontMicro));
        group->SetSize(320, 16);
        group->SetPosition(0, y);
        y += 18;
        m_hud.modelPopupList->AddChild(std::move(group));
        const int dup = modelCounts[pc.model];
        std::string text = pc.model;
        if (pc.context > 0) {
            text += " · " + std::to_string(pc.context / 1000) + "K";
        }
        if (dup > 1) text += "  同名×" + std::to_string(dup);
        auto row = std::make_unique<ECDI::Button>(text);
        row->SetTextColor(dup > 1 ? kWarn : kDarkInk);   // 同名标黄
        row->SetFont(harness::SansFont(harness::kFontSmall));
        row->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kToolHeaderHover});
        row->SetSize(320, 24);
        row->SetPosition(0, y);
        y += 28;
        const std::string pid = pc.id;
        row->SetOnClick([this, pid] {
            SetModelPopupOpen(false);
            RequestModelSwitch(pid);
        });
        m_hud.modelPopupList->AddChild(std::move(row));
        ++rows;
    }
    if (rows == 0) {
        auto l = std::make_unique<ECDI::Label>("（无匹配——用设置添加提供商）");
        l->SetTextColor(kDarkMuted);
        l->SetFont(harness::SansFont(harness::kFontSmall));
        l->SetSize(320, 20);
        l->SetPosition(0, y);
        m_hud.modelPopupList->AddChild(std::move(l));
    }
    RequestRepaint();
}

void HarnessApp::UpdateModelChip() {
    if (!m_hud.modelChip || !m_sess) return;
    std::string text = m_sess->providerId.empty()
                           ? "—"
                           : (m_sess->providerId + " › " +
                              (m_sess->modelName.empty() ? "?" : m_sess->modelName));
    // ★ 用量常驻（v4 分母 ctxCapacity——P3.6 只写不读的接线在这里补上）：
    //   分子与右栏百分比同口径（usageIn）。
    if (m_sess->ctxCapacity > 0 && m_sess->usageIn >= 0) {
        const long long pct =
            (m_sess->usageIn * 100 + m_sess->ctxCapacity / 2) / m_sess->ctxCapacity;
        text += " · " + std::to_string(pct) + "%";
    }
    // ★ 用 `﹀`(U+FE40，与思维链卡同款，Microsoft YaHei 有字形，不画 `□` 缺字块)。
    text += " \xEF\xB9\x80";
    m_hud.modelChip->SetText(text);
}

void HarnessApp::RebuildSettingsProviderList() {
    if (!m_hud.settingsProviderList) return;
    while (m_hud.settingsProviderList->GetChildCount() > 0) {
        m_hud.settingsProviderList->RemoveChild(m_hud.settingsProviderList->GetChildAt(0));
    }
    const std::string liveId = m_sess ? m_sess->providerId : std::string();
    for (const auto& pc : m_appConfig.providers) {
        std::string marker = "○ ";
        if (pc.id == liveId) marker = "● ";   // ★ P4：用守卫式 liveId（原裸解引用 m_sess）
        if (pc.id == m_editingProviderId) marker += "[编辑中] ";
        auto row = std::make_unique<ECDI::Button>(
            marker + pc.id + " · " + pc.model);
        row->SetTextColor(kDarkInk);
        row->SetFont(harness::SansFont(harness::kFontSmall));
        row->SetStyle(ECDI::ButtonStyleOverride{
            .background = kPanel2,
            .border = kLine,
            .borderWidth = 1.0f,
            .cornerRadius = kRadiusBtn,
            .pressedBackground = kToolHeaderPressed,
            .hoverBackground = kToolHeaderHover});
        row->SetSize(500, 24);
        const std::string pid = pc.id;
        row->SetOnClick([this, pid] {
            m_editingProviderId = pid;
            if (const ProviderConfig* sel = m_appConfig.Find(pid)) {
                if (m_hud.settingsBase) {
                    m_hud.settingsBase->SetText(sel->baseUrl);
                    m_hud.settingsModel->SetText(sel->model);
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(
                            sel->context > 0 ? std::to_string(sel->context) : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(
                            sel->maxOutput > 0 ? std::to_string(sel->maxOutput) : "");
                    }
                    if (m_hud.settingsKey) m_hud.settingsKey->SetText("");
                    if (m_hud.settingsHint) {
                        m_hud.settingsHint->SetText(
                            "编辑 " + pid + "——key 留空保持已存凭据不变");
                    }
                }
            }
            RebuildSettingsProviderList();
        });
        m_hud.settingsProviderList->AddChild(std::move(row));
    }
    // ★★ 2026-10-25 修复（实测反馈「上面空一大片」）：容器原来恒 108 高，
    //   空配置（首启）时就是一块纯空白——用户第一眼看到的是「设置页顶上
    //   空了一大片」，而不是「这里该有个提供商」。⇒ 空态给一行说明，高度
    //   改按行数收敛（空态 20，每行 24+4，4 行起钳到原 108）。
    if (m_appConfig.providers.empty()) {
        auto tip = std::make_unique<ECDI::Label>(
            "还没有提供商——填写下方字段，保存后出现在这里");
        tip->SetTextColor(kDarkMuted);
        tip->SetFont(harness::SansFont(harness::kFontSmall));
        tip->SetSize(500, 20);
        m_hud.settingsProviderList->AddChild(std::move(tip));
    }
    {
        const int rows = static_cast<int>(m_hud.settingsProviderList->GetChildCount());
        int listH = 20;   // 空态：一行说明
        if (!m_appConfig.providers.empty()) {
            listH = rows * 28 - 4;   // 每行 24 + 间隙 4
            if (listH > 108) listH = 108;   // 4 行起钳到原固定高（不撑破面板）
        }
        m_hud.settingsProviderList->SetSize(500, listH);
    }
    // ★ 顺序：先更新 extent（content 尺寸变了），再 Arrange（按新尺寸摆位）。
    //   反了的话 Arrange 用的是旧 extent，最后一行会被裁在视口外。
    if (m_hud.settingsSv) m_hud.settingsSv->UpdateContentExtent();
    if (ECDI::Widget* parent = m_hud.settingsProviderList->GetParent()) {
        parent->Arrange();
    }
}

void HarnessApp::RequestModelSwitch(const std::string& providerId) {
    if (!m_sess || !m_sess->writer) {
        AddNoticeRow("[当前会话不是 live——先连接提供商]", kDarkMuted);
        return;
    }
    if (!m_sess->hasModelCap) {
        AddNoticeRow("[当前 core 未声明 model 能力，不能热切换]", kDarkMuted);
        return;
    }
    if (m_sess->providerId == providerId && m_sess->pendingModelId.empty()) {
        AddNoticeRow("[已是当前模型] " + providerId, kDarkMuted);
        return;
    }
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        // ★ I11 排队语义：running/waiting 时**不发**——等上一个模型完整输出完。
        m_sess->queuedModelProvider = providerId;
        AddNoticeRow("[模型切换已排队：" + providerId + "（本轮结束后生效）]", kDarkMuted);
        return;
    }
    SendModelSwitch(providerId);
}

void HarnessApp::SendModelSwitch(const std::string& providerId) {
    const ProviderConfig* pc = m_appConfig.Find(providerId);
    if (!pc) {
        AddNoticeRow("[切换目标不在配置中] " + providerId, kErrorDark);
        return;
    }
    const std::string id = "m" + std::to_string(m_sess->writer->LastSeqSent() + 1);
    m_sess->pendingModelId = id;
    if (!m_sess->writer->Model(id, providerId, pc->context, pc->maxOutput)) {
        m_sess->pendingModelId.clear();
        AddNoticeRow("[切换请求发送失败]", kErrorDark);
        return;
    }
    AddNoticeRow("[切换请求已发送] " + providerId + " / " + pc->model, kDarkMuted);
}

void HarnessApp::SendPendingModelSwitchIfIdle() {
    if (!m_sess || !m_sess->writer || m_sess->queuedModelProvider.empty()) return;
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) return;
    const std::string provider = m_sess->queuedModelProvider;
    m_sess->queuedModelProvider.clear();
    SendModelSwitch(provider);
}
