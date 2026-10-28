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
#include <set>
#include <string>
#include <vector>

namespace {

// 去掉首尾空白（含全角空格 U+3000，它的 UTF-8 是 E3 80 80）。
// ★ 为什么需要：识别名/显示名是**用户输入**，`"  deepseek"` 与 `"deepseek"`
//   在用户眼里是同一个名字，在配置里却是两家 ⇒ 保存前必须归一。
std::string TrimCopy(const std::string& s) {
    auto isSpaceAt = [&s](size_t i, size_t& len) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { len = 1; return true; }
        if (c == 0xE3 && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            static_cast<unsigned char>(s[i + 2]) == 0x80) {
            len = 3;
            return true;
        }
        return false;
    };
    size_t begin = 0;
    size_t step = 0;
    while (begin < s.size() && isSpaceAt(begin, step)) begin += step;
    size_t end = s.size();
    while (end > begin) {
        // 从尾部回退一个 UTF-8 码点再判（否则会落在多字节字符中间）
        size_t cpBegin = end - 1;
        while (cpBegin > begin && (static_cast<unsigned char>(s[cpBegin]) & 0xC0) == 0x80) {
            --cpBegin;
        }
        if (!isSpaceAt(cpBegin, step) || cpBegin + step != end) break;
        end = cpBegin;
    }
    return s.substr(begin, end - begin);
}

// 按 UTF-8 **码点**截断并加省略号（不切半个字符）。
// ★ 为什么显示侧要自己截：显示名不限长（规格 9.5.2 只约束 id），而设置列表行
//   只有 500px、GDI 是尾部硬裁 ⇒ 裁在哪不可控，可能切出一个半个汉字。
std::string ClampDisplay(const std::string& s, size_t maxCp) {
    size_t cp = 0;
    size_t i = 0;
    while (i < s.size() && cp < maxCp) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        ++cp;
    }
    if (i >= s.size()) return s;
    return s.substr(0, i) + "…";
}

}   // namespace

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
    // ★★ 2026-10-27（用户反馈「内容贴左、右边空一大片」）：sheet 宽度**跟着内容走**。
    //   原来上限恒 620，而字段列只有 520 ⇒ 左边距 14、右边距 86，整块内容看着
    //   像没对齐（实测截图）。现在上限 = 字段列宽 + 2×面板 padding ⇒ 左右边距
    //   相等，内容天然居中；窗口窄时仍按 `rw - 80` 收窄（字段区本来就能横向滚动）。
    constexpr int kSettingsContentW = 520;   // = BuildSettingsSheet 里各子控件的宽
    constexpr int kSettingsPad = 14;         // = panel 的 VerticalLayout padding
    const int w = std::clamp(rw - 80, 280, kSettingsContentW + 2 * kSettingsPad);
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
                         " --model=" + def->ActiveModelName() +
                         " --provider-id=" + def->id;
    if (def->ActiveContext() > 0) {
        m_options.coreArgs += " --context=" + std::to_string(def->ActiveContext());
    }
    if (def->ActiveMaxOutput() > 0) {
        m_options.coreArgs += " --max-output=" + std::to_string(def->ActiveMaxOutput());
    }
    m_options.fixtureName = def->ActiveModelName() + " (live)";
    return true;
}

// 打开设置面板（**唯一入口**）：预填非秘密字段 + 打开浮层。
// ★★ 2026-10-27（v4 第三步）：为什么提成函数——面板有两个入口（「更多 → 设置」与
//   空态引导卡上的「打开设置」），原来只有前者预填。后者只 `SetSettingsVisible(true)`，
//   于是从引导卡进面板时字段全空；引入模型子列表后更明显：模型区连"先选提供商"
//   的说明都没有，因为 `m_editingProviderId` 也是空的。
//   ★ key 字段**永不回填**——凭据库里有值时 hint 说明"留空保持不变"。
void HarnessApp::OpenSettingsPanel() {
    // ★ §9.5：每次打开都是一次新的编辑会话 ⇒ 清掉上一次的待确认改名。
    m_pendingRenameFrom.clear();
    m_pendingRenameTo.clear();
    if (m_hud.settingsBase) {
        const ProviderConfig* edit = m_appConfig.Find(m_editingProviderId);
        if (!edit) edit = m_appConfig.DefaultProvider();
        if (edit) {
            m_editingProviderId = edit->id;
            // ★ §9.5：两个身份字段也要预填。识别名预填的是**当前值**（用户要能
            //   看见自己给它起的名字），显示名空则回落 id——空着让用户填也合法，
            //   但预填 id 能少一次输入。
            if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText(edit->id);
            if (m_hud.settingsProviderName) {
                m_hud.settingsProviderName->SetText(ProviderDisplayName(*edit));
            }
            m_hud.settingsBase->SetText(edit->baseUrl);
            m_hud.settingsModel->SetText(edit->ActiveModelName());
            // ★ v4 第三步：编辑目标是这家的**激活模型**（字段区预填的就是它）。
            m_editingModelName = edit->ActiveModelName();
            if (m_hud.settingsContext) {
                m_hud.settingsContext->SetText(
                    edit->ActiveContext() > 0 ? std::to_string(edit->ActiveContext()) : "");
            }
            if (m_hud.settingsMaxOut) {
                m_hud.settingsMaxOut->SetText(
                    edit->ActiveMaxOutput() > 0 ? std::to_string(edit->ActiveMaxOutput()) : "");
            }
            RebuildSettingsProviderList();
        }
        // ★ 放在 if(edit) 之外：一家都没有（首启）时也要把模型区刷成空态说明，
        //   否则那里留一块构造期的高 26 空面板。
        RebuildSettingsModelList();
        m_hud.settingsKey->SetText("");
        if (m_providerKeys.empty()) {
            m_hud.settingsHint->SetText("填写提供商连接信息，保存后连接");
        } else {
            m_hud.settingsHint->SetText("已保存的密钥不回显——留空则保持不变");
        }
    }
    SetSettingsVisible(true);
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

    // ── ★★ §9.5（2026-10-27）：识别名由用户**声明**，不再从 baseUrl 派生 ──
    //   留空 ⇒ 用 host 兜底（旧派生行为降级为「新增时的初值」，见 AppConfig.h）。
    std::string providerId = TrimCopy(
        m_hud.settingsProviderId ? m_hud.settingsProviderId->GetText() : std::string());
    std::string providerName = TrimCopy(
        m_hud.settingsProviderName ? m_hud.settingsProviderName->GetText() : std::string());
    if (providerId.empty()) providerId = AppConfig::ProviderIdFromBaseUrl(baseUrl);
    std::string idWhy;
    if (!ValidateProviderId(providerId, idWhy)) {
        setHint("识别名" + idWhy, kErrorDark);
        return;
    }
    if (providerName.empty()) providerName = providerId;   // 不引入"无名"状态
    // 唯一性：**sanitize 后大写**相等即冲突（规格 9.5.2）。按原字符串判会放过
    //   `abc`/`ABC`、`a-b`/`a.b` 这两类——它们在环境变量里是**同一个名字**，
    //   用户会看到"保存成功"，而其中一家的 key 被静默顶掉。
    {
        const std::string sanNew = EnvSanitizeProviderId(providerId);
        for (const auto& e : m_appConfig.providers) {
            if (!m_editingProviderId.empty() && e.id == m_editingProviderId) continue;
            if (EnvSanitizeProviderId(e.id) != sanNew) continue;
            setHint("识别名 " + e.id + " 已存在（要改它先在列表选中）", kErrorDark);
            return;
        }
    }

    // ★ 改名判定：编辑目标存在且 id 变了。`m_editingProviderId` 可能指向一份
    //   已经被删掉的配置 ⇒ 用 Find 复核，别拿一个幽灵当改名源（否则会去搬一个
    //   不存在的凭据，并把一条本来正常的保存变成失败）。
    std::string oldId;
    std::string oldTarget;
    if (!m_editingProviderId.empty() && m_editingProviderId != providerId) {
        if (const ProviderConfig* op = m_appConfig.Find(m_editingProviderId)) {
            oldId = m_editingProviderId;
            oldTarget = op->credentialTarget.empty()
                            ? AppConfig::CredentialTargetFor(oldId)
                            : op->credentialTarget;
        }
    }
    // ① 确认（规格 9.5.3 第 1 步）：**面板内二次确认**——第一击只问不做。
    //   形态选面板内而不是独立弹窗：仓里没有通用 modal，为一个低频动作引入
    //   遮罩/焦点/ESC 一整套，代价远大于收益；而「保存」按钮本来就在手上。
    //   命中条件 = (旧 id, 新 id) 与上次**完全一致**：用户中途改了任一处就重新问，
    //   否则会出现"确认过 A→B，实际执行了 A→C"。
    if (!oldId.empty() &&
        (m_pendingRenameFrom != oldId || m_pendingRenameTo != providerId)) {
        m_pendingRenameFrom = oldId;
        m_pendingRenameTo = providerId;
        // ★ 文案预算 520px（hint 是 520×18 单行、不换行、超长直接裁）。
        //   ⇒ **可执行的信息放最前**（"再点保存确认"），两个识别名放中间，
        //   "（迁移凭据）"垫底——真被裁掉时，用户仍知道下一步该做什么。
        //   实测：常态 id 400px；两个 24 字符的长 id 约 504px（刚好压线）。
        setHint("再点保存确认：识别名 " + oldId + "→" + providerId + "（迁移凭据）", kWarn);
        return;
    }

    const std::string target = AppConfig::CredentialTargetFor(providerId);

    // ② 凭据：key 非空 → 覆盖写；key 为空且已有 → 保持不变（轮换语义）；
    //    key 为空且凭据库无 → 环境变量兜底（§8.5.6）。
    //    ★ 顺序即优先级：凭据库永远先于环境变量。反过来的话，用户在面板里填的值
    //      会被一个他自己都忘了的 env 静默盖掉——那是最难诊断的一类失效。
    //    ★ §9.5.3 改名：key 框留空时的「保持不变」指的是**旧目标名下那把**，
    //      所以要先搬到新目标名（否则改名瞬间 = key 丢了）。
    bool keyFromEnv = false;
    bool wroteTarget = false;     // 本次是否往**新**目标名写过（失败回滚用）
    bool migratedKey = false;     // 新目标名的值来自旧目标名
    bool envNameStale = false;    // 改名后仍靠旧名的环境变量供 key（提示用）
    std::string envKey;
    std::string migrated;
    if (!key.empty()) {
        std::string credError;
        if (!creds::Write(ECDI::UTF8ToWide(target), key, credError)) {
            setHint("凭据写入失败：" + credError, kErrorDark);
            return;
        }
        wroteTarget = true;
    } else {
        bool haveValue = false;
        if (!oldTarget.empty()) {
            std::string readError;
            if (creds::Read(ECDI::UTF8ToWide(oldTarget), migrated, readError)) {
                std::string credError;
                if (!creds::Write(ECDI::UTF8ToWide(target), migrated, credError)) {
                    setHint("凭据迁移失败：" + credError, kErrorDark);
                    return;
                }
                wroteTarget = true;
                migratedKey = true;
                haveValue = true;
            }
        }
        if (!haveValue && !creds::Exists(ECDI::UTF8ToWide(target))) {
            if (ReadEnvProviderKey(providerId, envKey)) {
                keyFromEnv = true;
            } else if (!oldId.empty() && ReadEnvProviderKey(oldId, envKey)) {
                // 改名后变量名也变了（`ECDI_PROVIDER_<SAN>_KEY`）。env 是**来源**，
                // 搬不了——只能照旧值用，并明确告诉用户"变量名还是旧的"。
                // 不这么做的话，纯 env 供 key 的提供商一改名就永远保存不了。
                keyFromEnv = true;
                envNameStale = true;
            } else {
                // ★ 文案长度受 `settingsHint` 限制（520×18 单行、不换行、超长直接裁）：
                //   既有提示最长 476px，这里按同一预算写（实测 464px）。把变量名放在
                //   **最前**——它是唯一可执行的信息，被裁掉的必须是后缀。
                //   ★ 报的是**新**变量名：迁移之后用户要配的就是它。
                setHint("缺少环境变量 " + ProviderKeyEnvName(providerId), kErrorDark);
                return;
            }
        }
    }

    // ③ 解析 core 可执行文件（结果写进配置 ⇒ 下次无参启动不依赖 cwd）。
    std::filesystem::path coreExe;
    if (!ResolveCoreExecutable(coreExe)) {
        setHint("找不到 ecdi-core.exe（先构建：go build -o core/bin/ecdi-core.exe ./cmd/ecdi-core）",
                kErrorDark);
        return;
    }

    // ④ 写配置（v4 多提供商 + provider 1—N 模型；结构上无密钥字段）。
    ProviderConfig pc;
    pc.id = providerId;
    pc.name = providerName;
    pc.baseUrl = baseUrl;
    // ★★ v4（2026-10-27）第三步：合并来源是**正在编辑的那一条**，不是 `oldId`。
    //   `oldId` 只在改名时非空 ⇒ 拿它当合并源的话，普通保存（改个容量、加个模型）
    //   根本不进这段，`pc.models` 就只剩表单这一个，而 Save 走 `Upsert` 整条替换
    //   ⇒ 同家其它模型被静默冲掉（这正是拆层后最容易踩的一脚）。
    //   规则：表单这个模型**顶替**正在编辑的那个（`m_editingModelName`），其余原样保序。
    const std::string mergeFrom = !oldId.empty() ? oldId : m_editingProviderId;
    if (!mergeFrom.empty()) {
        if (const ProviderConfig* e = m_appConfig.Find(mergeFrom)) {
            for (const auto& m : e->models) {
                // ① 正在编辑的那个 ⇒ 让位给表单值（改容量 / 改名都走这里）。
                if (!m_editingModelName.empty() && m.name == m_editingModelName) continue;
                // ② 与表单新名字撞车（把 A 改名成已存在的 B）⇒ 拒绝。放过去的话
                //    models 里会出现两个同名项，而 activeModel/弹窗都按名字找
                //    ⇒ 永远只有第一个能被切到，第二个是删不掉的影子。
                if (m.name == model) {
                    setHint("该提供商下已有模型 " + model + "（换个名字，或先在列表里选它）",
                            kErrorDark);
                    return;
                }
                pc.models.push_back(m);
            }
        }
    }
    pc.models.push_back(ModelConfig{model, context, maxOutput});
    pc.activeModel = model;
    pc.credentialTarget = target;
    // ★ 改名必须**就地替换**：`Upsert` 按新 id 找不到会追加，旧项会留下来变成
    //   一家"打不通的幽灵"（凭据已被搬走、id 又指向不存在的端点参数）。
    //   就地替换同时保住了「providers 顺序 = 编辑序」。
    ProviderConfig beforeRename;   // 回滚快照（仅在 oldId 非空时有意义）
    const std::string defaultBefore = m_appConfig.defaultProviderId;
    bool renamed = false;
    if (!oldId.empty()) {
        for (auto& e : m_appConfig.providers) {
            if (e.id != oldId) continue;
            beforeRename = e;
            e = pc;
            renamed = true;
            break;
        }
    }
    if (!renamed) m_appConfig.Upsert(pc);
    m_appConfig.defaultProviderId = providerId;
    if (!key.empty()) {
        m_providerKeys[providerId] = key;
    } else if (migratedKey) {
        m_providerKeys[providerId] = migrated;
    } else if (keyFromEnv) {
        // env 来的 key 也进缓存：spawn 时要靠它注入 ECDI_CORE_KEY（无 key 的 core 直接
        // exit 2），「切走再切回」也靠它。只在内存，不落任何文件。
        m_providerKeys[providerId] = envKey;
    }
    if (!oldId.empty()) m_providerKeys.erase(oldId);
    m_appConfig.corePath = coreExe.generic_string();
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        // ★★ 规格 9.5.3「任一步失败 ⇒ 全量回滚，禁止配置文件已改、凭据没迁」。
        //   这里反过来也一样：凭据已搬、配置没落盘 ⇒ 必须把新目标名删掉并还原
        //   内存里的配置项，否则下次启动会按**旧** id 找凭据，而它已经不在了。
        if (wroteTarget) {
            std::string ignored;
            creds::Delete(ECDI::UTF8ToWide(target), ignored);
        }
        if (renamed) {
            for (auto& e : m_appConfig.providers) {
                if (e.id != providerId) continue;
                e = beforeRename;
                break;
            }
        }
        // 默认提供商也还原：它可能指向**别的**一家，不能想当然写成改名源。
        m_appConfig.defaultProviderId = defaultBefore;
        setHint("配置写入失败：" + saveError, kErrorDark);
        return;
    }
    m_editingProviderId = providerId;
    // ★ v4 第三步：保存成功后「编辑目标」= 刚保存的这个模型（它就是新的激活项）。
    //   不更新的话，下一次保存会把 m_editingModelName（旧名）当成要替换的目标，
    //   于是"再存一次"变成"加一个同名的新模型"。
    m_editingModelName = model;
    RebuildSettingsModelList();
    m_pendingRenameFrom.clear();
    m_pendingRenameTo.clear();

    // ⑤ 删旧目标名：**放在配置落盘之后**。规格 9.5.3 原文把它排在落盘之前，
    //   这里刻意调换——先删旧再落盘，一旦 Save 失败就是「旧凭据没了、新配置没写」
    //   的双向丢失；而先写新、后删旧，最坏只留一个**孤儿旧目标名**（可诊断、无损）。
    //   删失败不阻断连接：配置与凭据都已正确，孤儿只是卫生问题，但要如实说出来。
    bool orphanOldTarget = false;
    if (!oldId.empty() && !oldTarget.empty()) {
        std::string delError;
        if (!creds::Delete(ECDI::UTF8ToWide(oldTarget), delError)) orphanOldTarget = true;
    }
    // 改名的结果走**通知行**，不写 hint：hint 住在面板里，而成功路径紧接着就
    //   关面板 ⇒ 写进 hint 等于没写（用户看不到）。这也是既有那几条「已保存并
    //   连接…」其实只在探针里可读的原因——它们从来没被画出来过。
    //   ★ 三件事都要能看见：改名本身 / env 变量名没跟着改（否则下次会莫名缺 key）/
    //     旧目标名残留（卫生问题，不影响功能）。
    const auto emitRenameNotice = [&]() {
        if (!renamed) return;
        std::string note = "[已改名：" + oldId + " → " + providerId + "]";
        if (envNameStale) note += " [环境变量仍是旧名 " + ProviderKeyEnvName(oldId) + "]";
        if (orphanOldTarget) note += " [旧凭据未删净]";
        AddNoticeRow(note, kDarkMuted);
    };

    // ⑥ 连接：无 live 会话 ⇒ 新建（env 注入走统一路径）；已有支持热切换的
    //    live 会话 ⇒ 发 model 切换请求（I11：排队语义由 RequestModelSwitch 处理）。
    // ★ 提示语带 key 来源（§8.5.6）：env 兜底是**隐式**来源，不写出来的话，
    //   用户下次打开面板只会看到「key 框是空的」却连得上，无从判断为什么。
    // ★ 文案预算 520px（hint 是 520×18 单行、不换行、超长直接裁；既有最长 476px）。
    //   带来源的三条实测 239 / 490 / 453px —— 已到预算边缘，所以 env 分支连
    //   「/ 模型名」「当前…会话」都省掉；再加修饰词就会被裁。
    const bool hasLive = m_sess && m_sess->originKind == "live" && m_sess->writer;
    // ★ 改名后**不能**走热切换：core 的提供商注册表在 spawn 时按 id 建好，
    //   运行中的 core 不认识新 id ⇒ 发过去只会拿到 ok:false。改走重开会话
    //   （新 core 用新 id 起）——规格 9.5.3 第 5 步的「按 cap 决定」在这里
    //   由"改名"直接决定，不看 cap。
    const bool liveOnRenamed = !oldId.empty() && m_sess && m_sess->providerId == oldId;
    if (hasLive && m_sess->hasModelCap && !liveOnRenamed) {
        auto keyIt = m_providerKeys.find(providerId);
        if (keyIt != m_providerKeys.end()) {
            m_coreApiKey = keyIt->second;   // 新 provider 成为默认 ⇒ ECDI_CORE_KEY 跟随
        }
        setHint(keyFromEnv ? "已保存；正在切换到 " + providerId + "（key 来自环境变量）"
                           : "已保存；正在切换到 " + providerId + " / " + model,
                kDarkMuted);
        SetSettingsVisible(false);
        emitRenameNotice();
        // ★ v4：切到刚保存的那个模型（`model` 是表单值，可能不是该家的激活项）。
        RequestModelSwitch(providerId, model);
        return;
    }
    if (hasLive && !m_sess->hasModelCap && !liveOnRenamed) {
        setHint(keyFromEnv
                    ? "已保存；core 不支持热切换，重启后生效（key 来自环境变量）"
                    : "已保存；当前 core 不支持热切换（cap 未声明），重启会话后生效",
                kDarkMuted);
        SetSettingsVisible(false);
        emitRenameNotice();
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
    setHint(keyFromEnv ? "已保存并连接；key 来自环境变量"
                       : "已保存并连接：" + providerId + " / " + model,
            kDarkMuted);
    SetSettingsVisible(false);
    m_modelListDirty = true;
    // ★ 放在**切到新会话之后**：通知行写的是活动会话的转录，切走前写会落到
    //   用户已经离开的那个会话里（等于没看见）。
    emitRenameNotice();
}

std::string HarnessApp::ProviderDisplayName(const ProviderConfig& pc) {
    // 界面**唯一**的取名入口（规格 9.5.2：name 空 ⇒ 回落 id，不引入"无名"状态）。
    return pc.name.empty() ? pc.id : pc.name;
}

std::string HarnessApp::ProviderDisplayLabel(const std::string& providerId) const {
    // ★ 配置里没有这个 id 时原样返回：历史会话、刚被删掉的提供商、回放夹具都会
    //   走到这里。返回空串会让副标题/卡片出现「 › deepseek-chat」这种半截话，
    //   比显示一个裸 id 难读得多。
    if (const ProviderConfig* pc = m_appConfig.Find(providerId)) {
        return ProviderDisplayName(*pc);
    }
    return providerId;
}

bool HarnessApp::ValidateProviderId(const std::string& id, std::string& why) {
    // 规格 9.5.2 的字符集。**不用**更宽松的规则：id 会变成环境变量片段
    // （非字母数字 → '_'），也会进凭据目标名，所以能用的字符越少越好。
    if (id.empty()) {
        why = "不能为空";
        return false;
    }
    if (id.size() > 64) {
        why = "太长（≤64 字节）";
        return false;
    }
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) {
            why = "只能含字母数字和 . _ -（无空白/中文/斜杠）";
            return false;
        }
    }
    return true;
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

std::string HarnessApp::ProviderKeyEnvName(const std::string& providerId) {
    // 与 core 的启动扫描同名（main.go 扫 `ECDI_PROVIDER_*_KEY`）——core 侧零改动。
    // 名字里带 sanitize 后的 id：这就是 §9.5 那条「唯一性要按 sanitize 后判」的同一处约束。
    return "ECDI_PROVIDER_" + EnvSanitizeProviderId(providerId) + "_KEY";
}

bool HarnessApp::ReadEnvProviderKey(const std::string& providerId, std::string& out) {
    // ★ 只读**本进程继承来的**环境变量，不读注册表：用户设了但当前进程没继承到
    //   （setx 之后旧 shell 起的 GUI）时，这里必须返回 false——让上层如实报缺失，
    //   而不是给一个「看起来配好了」的假象（陈旧值陷阱，规格 §8.5.6）。
    // ★ 已知边界：`AppSession` 每次 spawn 也会把这些变量写进**本进程**环境
    //   （R-5 的启动环境层，值取自凭据库）。所以「凭据库里的 key 被外部删掉、
    //   但本进程早先注入过」时，这里仍能读到——值是对的、spawn 也确实会用它，
    //   故判据不受影响；只是「来源」标签描述的是**spawn 时用哪里的值**，
    //   不是这条值最初由谁录入。不做来源追踪是刻意的：那需要启动快照 + 探针钩子，
    //   而收益只覆盖一个自愈场景（重启后该变量自然消失）。
    const std::wstring name = ECDI::UTF8ToWide(ProviderKeyEnvName(providerId));
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(name.c_str(), buf, 1024);
    if (n == 0 || n >= 1024) return false;   // 不存在 / 超长（setx 的 1024 截断同源）
    std::string value = ECDI::WideToUTF8(std::wstring(buf, n));
    if (value.empty()) return false;
    out = std::move(value);
    return true;
}

void HarnessApp::CacheProviderKeys() {
    // 凭据库 → m_providerKeys（已有缓存的不覆盖——设置面板刚写入的值更新）。
    // ★ 2026-10-27（§8.5.6）：凭据库没有时回落到环境变量。否则「只用环境变量供 key」
    //   的提供商永远进不了这张表 ⇒ AppSession 不注入它的 URL ⇒ core 注册表里没有它
    //   ⇒ 切过去恒回「未知提供商」。兜底必须在**同一处**发生，不能只修保存路径。
    for (const auto& pc : m_appConfig.providers) {
        if (m_providerKeys.count(pc.id)) continue;
        std::string key;
        std::string err;
        if (creds::Read(ECDI::UTF8ToWide(pc.credentialTarget), key, err)) {
            m_providerKeys[pc.id] = key;
            continue;
        }
        std::string envKey;
        if (ReadEnvProviderKey(pc.id, envKey)) {
            m_providerKeys[pc.id] = envKey;
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
    addRow("提供商", s && !s->providerId.empty() ? ProviderDisplayLabel(s->providerId) : "—");
    addRow("模型", s && !s->modelName.empty() ? s->modelName : "—");
    if (s && s->ctxCapacity > 0) {
        addRow("容量", std::to_string(s->ctxCapacity) + " tok");
    } else {
        addRow("容量", "—");
    }
    const ProviderConfig* pc =
        s ? m_appConfig.Find(s->providerId) : nullptr;
    addRow("输出上限",
           pc && pc->ActiveMaxOutput() > 0 ? std::to_string(pc->ActiveMaxOutput()) : "—");
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
    // 同名检测：跨**提供商**相同 model 名 ⇒ 标黄 + 「同名×N」（同名跨家切换容易
    // 误点，这是唯一的防呆读数）。
    // ★ v4（2026-10-27）：按 `模型名 → 出现它的 provider 集合` 统计。同一家挂两个
    //   同名模型不该计入——那不会造成跨家误点，标黄反而是假警报。
    std::map<std::string, std::set<std::string>> owners;
    for (const auto& pc : m_appConfig.providers) {
        for (const auto& m : pc.models) owners[m.name].insert(pc.id);
    }
    // ★ §9.5：分组标签是**显示名**，同名两家再补 id 副标（规格 9.5.2）。
    std::map<std::string, int> nameCounts;
    for (const auto& pc : m_appConfig.providers) ++nameCounts[ProviderDisplayName(pc)];
    const std::string activeProv = m_sess ? m_sess->providerId : std::string();
    const std::string activeModel = m_sess ? m_sess->modelName : std::string();
    // ★ 列表区可用高：弹窗固定 272，搜索框占 40、底注在 250 ⇒ 210px。
    //   超出的行不画，由底注说明"还有几项"。**不改弹窗几何**：它是不滚动的固定
    //   浮窗，加滚动等于换一套交互；等"提供商多到装不下"成为真实反馈再说。
    constexpr int kListMaxH = 210;
    int y = 0;
    int rows = 0;
    int hidden = 0;
    for (const auto& pc : m_appConfig.providers) {
        // ★ 搜索命中：provider 的 id/显示名，或**任一**模型名。
        const bool provHit = q.empty() ||
                             pc.id.find(q) != std::string::npos ||
                             pc.name.find(q) != std::string::npos;
        if (y + 22 > kListMaxH) {
            hidden += static_cast<int>(pc.models.size());
            continue;
        }
        const std::string disp = ProviderDisplayName(pc);
        std::string groupText = ClampDisplay(disp, 22);
        if (nameCounts[disp] > 1) groupText += " (" + ClampDisplay(pc.id, 22) + ")";
        auto group = std::make_unique<ECDI::Label>(groupText);
        group->SetTextColor(kDarkMuted);
        group->SetFont(harness::SansFont(harness::kFontMicro));
        group->SetSize(320, 16);
        group->SetPosition(0, y);
        y += 18;
        m_hud.modelPopupList->AddChild(std::move(group));
        // ★ v4：一家 N 行，**每行一个模型**（v3 是一 provider 一行）。
        for (const auto& m : pc.models) {
            if (!provHit && m.name.find(q) == std::string::npos) continue;
            if (y + 28 > kListMaxH) {
                ++hidden;
                continue;
            }
            const bool isActive = (pc.id == activeProv && m.name == activeModel);
            const int dup = static_cast<int>(owners[m.name].size());
            std::string text = (isActive ? "● " : "  ") + m.name;
            if (m.context > 0) text += " · " + std::to_string(m.context / 1000) + "K";
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
            row->SetOnClick([this, pid = pc.id, name = m.name] {
                SetModelPopupOpen(false);
                RequestModelSwitch(pid, name);
            });
            m_hud.modelPopupList->AddChild(std::move(row));
            ++rows;
        }
    }
    // 底注：溢出时说明还剩几项，否则回到常规说明。
    if (m_hud.modelPopupFoot) {
        m_hud.modelPopupFoot->SetText(
            hidden > 0
                ? "另有 " + std::to_string(hidden) + " 项未显示——用搜索缩小范围"
                : "仅 idle 发出 · 运行中自动排队（本轮输出完生效）");
    }
    if (rows == 0) {
        auto l = std::make_unique<ECDI::Label>("（无匹配——换个关键词，或去设置里加模型）");
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
    // ★ §9.5：chip 显示**显示名**（空回落 id）。chip 定宽 200px，显示名比
    //   派生 id（一长串域名）短得多——这条改动顺带修好了「chip 文字被裁」。
    std::string text = m_sess->providerId.empty()
                           ? "—"
                           : (ClampDisplay(ProviderDisplayLabel(m_sess->providerId), 18) +
                              " › " + (m_sess->modelName.empty() ? "?" : m_sess->modelName));
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
    // ★ §9.5：显示名可重复 ⇒ 同名两家在列表里必须能分辨（规格 9.5.2
    //   「凡出现同名的地方必须带副标」）。同名计数按**显示名**统计。
    std::map<std::string, int> nameCounts;
    for (const auto& pc : m_appConfig.providers) ++nameCounts[ProviderDisplayName(pc)];
    for (const auto& pc : m_appConfig.providers) {
        std::string marker = "○ ";
        if (pc.id == liveId) marker = "● ";   // ★ P4：用守卫式 liveId（原裸解引用 m_sess）
        if (pc.id == m_editingProviderId) marker += "[编辑中] ";
        // 行宽 500px（Button 定宽，GDI 尾部硬裁）⇒ 自己按码点截，别让 GDI 切半个汉字。
        const std::string disp = ProviderDisplayName(pc);
        std::string rowText = marker + ClampDisplay(disp, 22);
        if (nameCounts[disp] > 1) rowText += " (" + ClampDisplay(pc.id, 22) + ")";
        rowText += " · " + ClampDisplay(pc.ActiveModelName(), 24);
        auto row = std::make_unique<ECDI::Button>(rowText);
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
            // ★ 换编辑目标 = 上一次的待确认改名作废（否则会拿 A→B 的确认去执行 A→C）。
            m_pendingRenameFrom.clear();
            m_pendingRenameTo.clear();
            if (const ProviderConfig* sel = m_appConfig.Find(pid)) {
                // ★ v4 第三步：编辑目标换成这家的**激活模型**——字段区预填的就是它，
                //   模型子列表的「编辑中」标记也落在它上面（两者必须同时改，否则
                //   列表标着 A 而字段里是 B，保存时替换谁就说不清了）。
                m_editingModelName = sel->ActiveModelName();
                if (m_hud.settingsProviderId) m_hud.settingsProviderId->SetText(sel->id);
                if (m_hud.settingsProviderName) {
                    m_hud.settingsProviderName->SetText(ProviderDisplayName(*sel));
                }
                if (m_hud.settingsBase) {
                    m_hud.settingsBase->SetText(sel->baseUrl);
                    m_hud.settingsModel->SetText(sel->ActiveModelName());
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(
                            sel->ActiveContext() > 0 ? std::to_string(sel->ActiveContext()) : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(
                            sel->ActiveMaxOutput() > 0 ? std::to_string(sel->ActiveMaxOutput())
                                                       : "");
                    }
                    if (m_hud.settingsKey) m_hud.settingsKey->SetText("");
                    if (m_hud.settingsHint) {
                        // ★ 2026-10-27（§8.5.6）：把 key 来源写在提示里。凭据库与环境变量
                        //   是两个不同去处，「空输入框但能连上」必须能解释得清；
                        //   「无」也要明说——否则用户看不出保存会失败。
                        // ★ 这里只报**类别**不报变量全名：hint 是 520px 单行，
                        //   带全名会被裁（全名在保存失败的提示里给）。
                        std::string src = "无（保存前需填 key）";
                        if (creds::Exists(ECDI::UTF8ToWide(sel->credentialTarget))) {
                            src = "凭据库";
                        } else {
                            std::string envKey;
                            if (ReadEnvProviderKey(sel->id, envKey)) src = "环境变量";
                        }
                        m_hud.settingsHint->SetText(
                            "编辑 " + pid + "——key 留空保持不变；来源：" + src);
                    }
                }
            }
            RebuildSettingsProviderList();
            RebuildSettingsModelList();   // 编辑目标变了 ⇒ 模型子列表跟着换
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

// ── ★★ v4 第三步（2026-10-27）：模型子列表 ─────────────────────────────────
//   语义：列出**编辑目标那一家**挂的模型；点一行 = 把它填进下方字段、并记为编辑目标。
//   为什么必须有这张表（而不是"字段里那个就是全部"）：v4 的模型是**表**，字段一次
//   只装得下一个 ⇒ 没有它，用户既看不出这家有几个模型，也无从删掉其中一个。
//   拆层前"一个 provider 一个模型"，删提供商等于删模型，所以那时不需要它。
void HarnessApp::RebuildSettingsModelList() {
    if (!m_hud.settingsModelList) return;
    while (m_hud.settingsModelList->GetChildCount() > 0) {
        m_hud.settingsModelList->RemoveChild(m_hud.settingsModelList->GetChildAt(0));
    }
    const ProviderConfig* pc = m_appConfig.Find(m_editingProviderId);
    if (!pc) {
        auto tip = std::make_unique<ECDI::Label>("先在上方列表选择提供商，这里会列出它挂的模型");
        tip->SetTextColor(kDarkMuted);
        tip->SetFont(harness::SansFont(harness::kFontSmall));
        tip->SetSize(500, 20);
        m_hud.settingsModelList->AddChild(std::move(tip));
    } else if (pc->models.empty()) {
        // 正常路径不会到这（Load/迁移都保证至少一项）；留着是为了让"配置被手工清空"
        //   有个可读的样子，而不是一块空白面板。
        auto tip = std::make_unique<ECDI::Label>("这家还没有模型——填下方模型名，保存后出现在这里");
        tip->SetTextColor(kDarkMuted);
        tip->SetFont(harness::SansFont(harness::kFontSmall));
        tip->SetSize(500, 20);
        m_hud.settingsModelList->AddChild(std::move(tip));
    } else {
        for (const auto& m : pc->models) {
            // ★ 文案预算 500px（Button 定宽，超长由 GDI 尾部硬裁）：
            //   marker≈20 + 名≤22 字符(≈154) + 「[编辑中]」(≈73) + 两段容量(≈170)
            //   ≈ 420px，留得住余量。名按码点截，别让 GDI 切半个汉字。
            std::string text = (m.name == pc->activeModel ? "● " : "○ ");
            text += ClampDisplay(m.name, 22);
            if (m.name == m_editingModelName) text += "  [编辑中]";
            text += " · 上下文 " + (m.context > 0 ? std::to_string(m.context) : std::string("未设"));
            text += " · 输出 " +
                    (m.maxOutput > 0 ? std::to_string(m.maxOutput) : std::string("默认"));
            auto row = std::make_unique<ECDI::Button>(text);
            // 「编辑中」标黄：它决定保存时**替换谁**，看错就是改错模型。
            row->SetTextColor(m.name == m_editingModelName ? kWarn : kDarkInk);
            row->SetFont(harness::SansFont(harness::kFontSmall));
            row->SetStyle(ECDI::ButtonStyleOverride{
                .background = kPanel2,
                .border = kLine,
                .borderWidth = 1.0f,
                .cornerRadius = kRadiusBtn,
                .pressedBackground = kToolHeaderPressed,
                .hoverBackground = kToolHeaderHover});
            row->SetSize(500, 24);
            row->SetOnClick([this, name = m.name] {
                m_editingModelName = name;
                const ProviderConfig* cur = m_appConfig.Find(m_editingProviderId);
                if (!cur) return;
                for (const auto& mm : cur->models) {
                    if (mm.name != name) continue;
                    if (m_hud.settingsModel) m_hud.settingsModel->SetText(mm.name);
                    if (m_hud.settingsContext) {
                        m_hud.settingsContext->SetText(mm.context > 0 ? std::to_string(mm.context)
                                                                     : "");
                    }
                    if (m_hud.settingsMaxOut) {
                        m_hud.settingsMaxOut->SetText(mm.maxOutput > 0
                                                          ? std::to_string(mm.maxOutput)
                                                          : "");
                    }
                    break;
                }
                if (m_hud.settingsHint) {
                    m_hud.settingsHint->SetText("编辑模型 " + name + "——保存后它成为激活模型");
                }
                RebuildSettingsModelList();
            });
            m_hud.settingsModelList->AddChild(std::move(row));
        }
    }
    {
        const int rows = static_cast<int>(m_hud.settingsModelList->GetChildCount());
        int listH = 20;   // 空态/单行提示
        if (pc && !pc->models.empty()) {
            listH = rows * 28 - 4;   // 每行 24 + 间隙 4（与提供商列表同一算式）
            if (listH > 108) listH = 108;   // 3 行起钳住，不撑破面板
        }
        m_hud.settingsModelList->SetSize(500, listH);
    }
    // ★ 顺序同 RebuildSettingsProviderList：先 extent 再 Arrange（反了最后一行会被裁在视口外）。
    if (m_hud.settingsSv) m_hud.settingsSv->UpdateContentExtent();
    if (ECDI::Widget* parent = m_hud.settingsModelList->GetParent()) parent->Arrange();
}

// 删除「正在编辑的模型」。为什么单独一个函数：它要动**已落盘**的配置（Save），
//   而面板里其余按钮都只改内存 ⇒ 失败回滚、至少留一个、激活项改嫁这三件事
//   必须写在一起才看得清顺序。
void HarnessApp::OnSettingsDeleteModel() {
    if (!m_hud.settingsHint) return;
    const auto setHint = [this](const std::string& text, const ECDI::Color& color) {
        m_hud.settingsHint->SetText(text);
        m_hud.settingsHint->SetTextColor(color);
        RequestRepaint();
    };
    if (m_editingProviderId.empty()) {
        setHint("先在列表中选择提供商", kErrorDark);
        return;
    }
    if (m_editingModelName.empty()) {
        setHint("先在模型列表中选择要删除的模型", kErrorDark);
        return;
    }
    ProviderConfig* target = nullptr;
    for (auto& e : m_appConfig.providers) {
        if (e.id == m_editingProviderId) { target = &e; break; }
    }
    if (!target) {
        setHint("编辑目标已不在配置中——重新选一次", kErrorDark);
        return;
    }
    int hit = -1;
    for (size_t i = 0; i < target->models.size(); ++i) {
        if (target->models[i].name == m_editingModelName) { hit = static_cast<int>(i); break; }
    }
    if (hit < 0) {
        setHint("模型 " + m_editingModelName + " 已不在该提供商下", kErrorDark);
        return;
    }
    // ★ 至少留一个：core 侧"provider 没有模型"= 这条路彻底不可用（切过去必 ok:false，
    //   因为拿不到模型名）。要清空请删提供商，那是有明确语义的另一个动作。
    if (target->models.size() <= 1) {
        setHint("每家至少保留一个模型（要清空请删提供商）", kErrorDark);
        return;
    }
    const ProviderConfig before = *target;   // 回滚快照（Save 失败要还原内存）
    const std::string removedName = target->models[static_cast<size_t>(hit)].name;
    target->models.erase(target->models.begin() + hit);
    // 激活项改嫁：删掉的正好是激活模型时不能留着悬空名字（Load 会回落 models[0]，
    //   但那要等下次启动才生效——本次会话仍按旧名显示）。
    if (target->activeModel == removedName) target->activeModel = target->models.front().name;
    std::string saveError;
    if (!m_appConfig.Save(saveError)) {
        *target = before;
        setHint("配置写入失败：" + saveError, kErrorDark);
        return;
    }
    // ★★ 字段必须跟着改嫁到**新的激活模型**，不能留着刚删掉那个名字：
    //   保存的语义是"表单这个模型优先"，留着旧名 ⇒ 用户一次误点「保存并连接」
    //   就把刚删掉的模型原样加回来（删了又回来，最难解释的一类 bug）。
    m_editingModelName = target->activeModel;
    if (m_hud.settingsModel) m_hud.settingsModel->SetText(target->activeModel);
    if (m_hud.settingsContext) {
        m_hud.settingsContext->SetText(target->ActiveContext() > 0
                                           ? std::to_string(target->ActiveContext())
                                           : "");
    }
    if (m_hud.settingsMaxOut) {
        m_hud.settingsMaxOut->SetText(target->ActiveMaxOutput() > 0
                                          ? std::to_string(target->ActiveMaxOutput())
                                          : "");
    }
    RebuildSettingsProviderList();   // 行尾「· 激活模型」可能变了
    RebuildSettingsModelList();
    m_modelListDirty = true;
    setHint("已删除模型 " + removedName + "（当前激活：" + target->activeModel + "）", kDarkMuted);
}

void HarnessApp::RequestModelSwitch(const std::string& providerId, const std::string& modelName) {
    if (!m_sess || !m_sess->writer) {
        AddNoticeRow("[当前会话不是 live——先连接提供商]", kDarkMuted);
        return;
    }
    if (!m_sess->hasModelCap) {
        AddNoticeRow("[当前 core 未声明 model 能力，不能热切换]", kDarkMuted);
        return;
    }
    // ★ v4：比 (provider, model) 两个字段。只比 provider 会让同一家换模型
    //   被判「已是当前模型」而静默拦掉——拆层前就是这条把多模型挡在门外的。
    const ProviderConfig* pc = m_appConfig.Find(providerId);
    const std::string target =
        modelName.empty() ? (pc ? pc->ActiveModelName() : std::string()) : modelName;
    if (m_sess->providerId == providerId && m_sess->modelName == target &&
        m_sess->pendingModelId.empty()) {
        AddNoticeRow("[已是当前模型] " + providerId + " / " + target, kDarkMuted);
        return;
    }
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) {
        // ★ I11 排队语义：running/waiting 时**不发**——等上一个模型完整输出完。
        m_sess->queuedModelProvider = providerId;
        m_sess->queuedModelName = target;
        AddNoticeRow("[模型切换已排队：" + providerId + " / " + target + "（本轮结束后生效）]",
                     kDarkMuted);
        return;
    }
    SendModelSwitch(providerId, target);
}

void HarnessApp::SendModelSwitch(const std::string& providerId, const std::string& modelName) {
    const ProviderConfig* pc = m_appConfig.Find(providerId);
    if (!pc) {
        AddNoticeRow("[切换目标不在配置中] " + providerId, kErrorDark);
        return;
    }
    // ★ v4：模型名与两个容量参数都取**该模型**的，不是"该家激活模型"的。
    //   传空 ⇒ 回落激活项（老调用点 / 探针用）。
    const std::string model = modelName.empty() ? pc->ActiveModelName() : modelName;
    int context = 0;
    int maxOutput = 0;
    for (const auto& m : pc->models) {
        if (m.name != model) continue;
        context = m.context;
        maxOutput = m.maxOutput;
        break;
    }
    const std::string id = "m" + std::to_string(m_sess->writer->LastSeqSent() + 1);
    m_sess->pendingModelId = id;
    if (!m_sess->writer->Model(id, providerId, model, context, maxOutput)) {
        m_sess->pendingModelId.clear();
        AddNoticeRow("[切换请求发送失败]", kErrorDark);
        return;
    }
    AddNoticeRow("[切换请求已发送] " + providerId + " / " + model, kDarkMuted);
}

void HarnessApp::SendPendingModelSwitchIfIdle() {
    if (!m_sess || !m_sess->writer || m_sess->queuedModelProvider.empty()) return;
    if (m_sess->coreState != "idle" || !m_sess->pendingWorkspaceId.empty()) return;
    const std::string provider = m_sess->queuedModelProvider;
    const std::string model = m_sess->queuedModelName;
    m_sess->queuedModelProvider.clear();
    m_sess->queuedModelName.clear();
    SendModelSwitch(provider, model);
}
