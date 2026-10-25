#include "AppConfig.h"

#include "Json.h"

#include <Windows.h>
// SHGetKnownFolderPath（FOLDERID_RoamingAppData）
#include <shlobj.h>

#include <fstream>
#include <sstream>

namespace {

std::filesystem::path& MutablePath() {
    static std::filesystem::path p;   // 空 = 用默认
    return p;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

}   // namespace

std::filesystem::path AppConfig::DefaultDir() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        std::filesystem::path dir(raw);
        CoTaskMemFree(raw);
        return dir / L"ecdi-fake-harness";
    }
    // 拿不到 APPDATA（异常环境）：退到当前目录下的同名目录——功能可用，
    // 但要在 error 路径里可见，不能静默换地方。
    return std::filesystem::current_path() / "ecdi-fake-harness";
}

std::filesystem::path AppConfig::DefaultPath() {
    if (!MutablePath().empty()) return MutablePath();
    return DefaultDir() / "config.json";
}

void AppConfig::SetPathForTesting(const std::filesystem::path& p) {
    MutablePath() = p;
}

std::string AppConfig::ProviderIdFromBaseUrl(const std::string& baseUrl) {
    // host = "://" 之后到第一个 '/'（或结尾）。
    const auto scheme = baseUrl.find("://");
    const size_t start = (scheme == std::string::npos) ? 0 : scheme + 3;
    size_t end = baseUrl.find('/', start);
    if (end == std::string::npos) end = baseUrl.size();
    std::string host = baseUrl.substr(start, end - start);
    // 去掉端口与用户信息可能残留的分隔
    const auto colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    if (host.empty()) return "default";
    return host;
}

std::string AppConfig::CredentialTargetFor(const std::string& providerId) {
    // 命名空间（规格 §8.5 D4）：ECDI.FakeHarness.v1.Provider.<provider-id>
    return "ECDI.FakeHarness.v1.Provider." + providerId;
}

const ProviderConfig* AppConfig::DefaultProvider() const {
    if (providers.empty()) return nullptr;
    for (const auto& pc : providers) {
        if (pc.id == defaultProviderId) return &pc;
    }
    return &providers[0];
}

const ProviderConfig* AppConfig::Find(const std::string& id) const {
    for (const auto& pc : providers) {
        if (pc.id == id) return &pc;
    }
    return nullptr;
}

void AppConfig::Upsert(const ProviderConfig& pc) {
    for (auto& existing : providers) {
        if (existing.id == pc.id) {
            existing = pc;
            return;
        }
    }
    providers.push_back(pc);
}

bool AppConfig::Load(std::string& error) {
    const auto path = DefaultPath();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "no config file";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    json::Value root;
    std::string parseError;
    if (!json::Parse(text, root, parseError) || !root.IsObject()) {
        error = "config is not valid JSON: " + parseError;
        return false;
    }
    const auto* ver = root.Find("version");
    if (!ver || !ver->IsNumber()) {
        error = "config version missing";
        return false;
    }
    const int fileVersion = static_cast<int>(ver->number);
    if (fileVersion != 1 && fileVersion != 2) {
        error = "config version unsupported: " + std::to_string(fileVersion);
        return false;
    }
    if (const auto* core = root.Find("corePath"); core && core->IsString()) {
        corePath = core->text;
    }

    // 单提供商的载入/自愈逻辑 v1 与 v2 共用（"credentialTarget 缺失 ⇒ 从
    // baseUrl 重建"——派生字段不是真相，真相是 baseUrl；命名空间演进后自愈）。
    auto fillProvider = [](const json::Value& prov, ProviderConfig& pc) {
        const auto getStr = [&prov](const char* k, std::string& out) {
            const auto* v = prov.Find(k);
            if (v && v->IsString()) out = v->text;
        };
        getStr("id", pc.id);
        getStr("baseUrl", pc.baseUrl);
        getStr("model", pc.model);
        getStr("credentialTarget", pc.credentialTarget);
        if (const auto* ctx = prov.Find("context"); ctx && ctx->IsNumber()) {
            pc.context = static_cast<int>(ctx->number);
        }
        if (const auto* mo = prov.Find("maxOutput"); mo && mo->IsNumber()) {
            pc.maxOutput = static_cast<int>(mo->number);
        }
        if (pc.credentialTarget.empty()) {
            pc.id = ProviderIdFromBaseUrl(pc.baseUrl);
            pc.credentialTarget = CredentialTargetFor(pc.id);
        }
    };

    // v1 单提供商 ⇒ 迁移成 providers[0]（原文件不动，保存时升级为 v2）。
    if (fileVersion == 1) {
        const auto* prov = root.Find("provider");
        if (!prov || !prov->IsObject()) {
            error = "config has no provider object";
            return false;
        }
        ProviderConfig pc;
        fillProvider(*prov, pc);
        if (pc.baseUrl.empty() || pc.model.empty()) {
            error = "config provider is missing baseUrl or model";
            return false;
        }
        providers.push_back(pc);
        defaultProviderId = pc.id;
        return true;
    }

    // v2：providers 数组 + defaultProviderId。坏行跳过（不否决整份配置）。
    // ★★ P4 修复（2026-10-25，实测反馈连带）：**同 id 去重（留最后一份）**——
    //   id 由 BaseURL host 派生，历史文件里同端点多份同 id 累积（实测 8 份）
    //   ⇒「按 id 删除/编辑/切换」语义全被打穿。去重走既有 Upsert（同 id 更新），
    //   下次 Save 落盘即收敛。
    const auto* arr = root.Find("providers");
    if (!arr || !arr->IsArray() || arr->array.empty()) {
        error = "config has no providers array";
        return false;
    }
    for (const auto& prov : arr->array) {
        if (!prov.IsObject()) continue;
        ProviderConfig pc;
        fillProvider(prov, pc);
        if (pc.baseUrl.empty() || pc.model.empty()) continue;
        Upsert(pc);
    }
    if (providers.empty()) {
        error = "config providers array has no usable entry";
        return false;
    }
    if (const auto* def = root.Find("defaultProviderId"); def && def->IsString()) {
        defaultProviderId = def->text;
    }
    if (defaultProviderId.empty()) defaultProviderId = providers[0].id;
    return true;
}

bool AppConfig::Save(std::string& error) const {
    namespace fs = std::filesystem;
    const auto path = DefaultPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // 目录建不出来也要报出来（TEMP 指到不存在目录会 abort 的坑在 core 侧见过）
    if (ec) {
        error = "cannot create config dir: " + ec.message();
        return false;
    }

    json::Value root;
    root.type = json::Value::Type::Object;
    {
        json::Value v;
        v.type = json::Value::Type::Number;
        v.number = static_cast<double>(version);
        root.object["version"] = std::move(v);
    }
    json::Value arr;
    arr.type = json::Value::Type::Array;
    for (const auto& pc : providers) {
        json::Value prov;
        prov.type = json::Value::Type::Object;
        auto putStr = [&prov](const char* k, const std::string& sv) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = sv;
            prov.object[k] = std::move(v);
        };
        auto putNum = [&prov](const char* k, int n) {
            json::Value v;
            v.type = json::Value::Type::Number;
            v.number = static_cast<double>(n);
            prov.object[k] = std::move(v);
        };
        putStr("id", pc.id);
        putStr("baseUrl", pc.baseUrl);
        putStr("model", pc.model);
        putNum("context", pc.context);
        putNum("maxOutput", pc.maxOutput);
        putStr("credentialTarget", pc.credentialTarget);
        arr.array.push_back(std::move(prov));
    }
    root.object["providers"] = std::move(arr);
    {
        json::Value v;
        v.type = json::Value::Type::String;
        v.text = defaultProviderId;
        root.object["defaultProviderId"] = std::move(v);
    }
    if (!corePath.empty()) {
        json::Value v;
        v.type = json::Value::Type::String;
        v.text = corePath;
        root.object["corePath"] = std::move(v);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write config file: " + WideToUtf8(path.wstring());
        return false;
    }
    out << root.Dump() << "\n";
    return true;
}
