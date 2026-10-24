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
    if (!ver || !ver->IsNumber() || static_cast<int>(ver->number) != 1) {
        error = "config version missing or unsupported";
        return false;
    }
    const auto* prov = root.Find("provider");
    if (!prov || !prov->IsObject()) {
        error = "config has no provider object";
        return false;
    }
    const auto getStr = [prov](const char* k, std::string& out) {
        const auto* v = prov->Find(k);
        if (v && v->IsString()) out = v->text;
    };
    getStr("id", provider.id);
    getStr("baseUrl", provider.baseUrl);
    getStr("model", provider.model);
    getStr("credentialTarget", provider.credentialTarget);
    const auto* ctx = prov->Find("context");
    if (ctx && ctx->IsNumber()) provider.context = static_cast<int>(ctx->number);
    if (const auto* core = root.Find("corePath"); core && core->IsString()) {
        corePath = core->text;
    }
    // credentialTarget 不存在时按当前命名空间规则**重建**（派生字段不是真相，
    // 真相是 baseUrl；这样旧配置在命名空间演进后仍能自愈）。
    if (provider.credentialTarget.empty()) {
        provider.id = ProviderIdFromBaseUrl(provider.baseUrl);
        provider.credentialTarget = CredentialTargetFor(provider.id);
    }
    if (provider.baseUrl.empty() || provider.model.empty()) {
        error = "config provider is missing baseUrl or model";
        return false;
    }
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
    json::Value prov;
    prov.type = json::Value::Type::Object;
    auto putStr = [&prov](const char* k, const std::string& s) {
        json::Value v;
        v.type = json::Value::Type::String;
        v.text = s;
        prov.object[k] = std::move(v);
    };
    auto putNum = [&prov](const char* k, int n) {
        json::Value v;
        v.type = json::Value::Type::Number;
        v.number = static_cast<double>(n);
        prov.object[k] = std::move(v);
    };
    putStr("id", provider.id);
    putStr("baseUrl", provider.baseUrl);
    putStr("model", provider.model);
    putNum("context", provider.context);
    putStr("credentialTarget", provider.credentialTarget);
    root.object["provider"] = std::move(prov);
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
