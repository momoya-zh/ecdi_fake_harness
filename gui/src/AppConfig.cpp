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
    // ★★ §9.5 起这里**不再是身份来源**：只是「新增时识别名留空」的兜底初值
    //   （host 恰好是个还行的默认名）。真要改身份，改的是配置里的 `id` 字段。
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

// ── v4：激活模型（三个访问器等价于 v3 的 model/context/maxOutput 字段）─────
// ★ 回落规则只有一条：`activeModel` 找不到 ⇒ `models[0]`。不引入"无激活"状态
//   ——那会让"这个 provider 能不能用"多出一个中间态，而 UI 与探针都得再判一次。
const ModelConfig* ProviderConfig::Active() const {
    if (models.empty()) return nullptr;
    for (const auto& m : models) {
        if (m.name == activeModel) return &m;
    }
    return &models[0];
}

ModelConfig* ProviderConfig::MutableActive() {
    if (models.empty()) models.push_back(ModelConfig{});
    for (auto& m : models) {
        if (m.name == activeModel) return &m;
    }
    return &models[0];
}

std::string ProviderConfig::ActiveModelName() const {
    const ModelConfig* m = Active();
    return m ? m->name : std::string();
}

int ProviderConfig::ActiveContext() const {
    const ModelConfig* m = Active();
    return m ? m->context : 0;
}

int ProviderConfig::ActiveMaxOutput() const {
    const ModelConfig* m = Active();
    return m ? m->maxOutput : 0;
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

void AppConfig::AddKnownWorkspace(const std::string& path) {
    if (path.empty()) return;
    for (const auto& existing : knownWorkspaces) {
        if (existing == path) return;
    }
    knownWorkspaces.push_back(path);
}

void AppConfig::RemoveKnownWorkspace(const std::string& path) {
    if (path.empty()) return;
    for (auto it = knownWorkspaces.begin(); it != knownWorkspaces.end(); ++it) {
        if (*it == path) {
            knownWorkspaces.erase(it);
            return;
        }
    }
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
    // ★ §9.5：v3 = 加 `name`（显示名）。老版本照旧可读（迁移在 fillProvider 里，
    //   只补默认值、不改 id/credentialTarget）。
    // ★ v4（2026-10-27）：`model/context/maxOutput` → `models[]` + `activeModel`。
    //   迁移同样在 fillProvider 里（折叠顶层三元组），id/凭据目标/defaultProviderId
    //   全不动 ⇒ 老配置的 key 不断、默认提供商不变。
    // ★ 上限取 `kCurrentVersion`（单一真相）：更高版本的文件由**更老**的二进制
    //   拒绝，这条永远成立，不需要每次升级都回来加一个数字。
    if (fileVersion < 1 || fileVersion > AppConfig::kCurrentVersion) {
        error = "config version unsupported: " + std::to_string(fileVersion);
        return false;
    }
    if (const auto* core = root.Find("corePath"); core && core->IsString()) {
        corePath = core->text;
    }

    knownWorkspaces.clear();
    if (const auto* kw = root.Find("knownWorkspaces"); kw && kw->IsArray()) {
        for (const auto& item : kw->array) {
            if (item.IsString() && !item.text.empty()) {
                knownWorkspaces.push_back(item.text);
            }
        }
    }

    // 单提供商的载入/自愈逻辑 v1 与 v2 共用（"credentialTarget 缺失 ⇒ 从
    // baseUrl 重建"——派生字段不是真相，真相是 baseUrl；命名空间演进后自愈）。
    auto fillProvider = [](const json::Value& prov, ProviderConfig& pc) {
        const auto getStr = [&prov](const char* k, std::string& out) {
            const auto* v = prov.Find(k);
            if (v && v->IsString()) out = v->text;
        };
        getStr("id", pc.id);
        getStr("name", pc.name);
        getStr("baseUrl", pc.baseUrl);
        getStr("credentialTarget", pc.credentialTarget);
        getStr("activeModel", pc.activeModel);

        // ★★ v4：先读 `models` 数组（新形态）；没有就**折叠**顶层三元组
        //   （v1/v2/v3 的形态）。两条路都落到 `models`，之后代码只认 models
        //   ——不允许"顶层字段还活着"的第二真相。
        if (const auto* arr = prov.Find("models"); arr && arr->IsArray()) {
            for (const auto& mv : arr->array) {
                if (!mv.IsObject()) continue;
                ModelConfig mc;
                if (const auto* n = mv.Find("name"); n && n->IsString()) mc.name = n->text;
                if (const auto* c = mv.Find("context"); c && c->IsNumber()) {
                    mc.context = static_cast<int>(c->number);
                }
                if (const auto* mo = mv.Find("maxOutput"); mo && mo->IsNumber()) {
                    mc.maxOutput = static_cast<int>(mo->number);
                }
                // 无名模型跳过：请求体 `"model": ""` 端点必报错（见 core 侧同条纪律）
                if (!mc.name.empty()) pc.models.push_back(std::move(mc));
            }
        }
        if (pc.models.empty()) {
            ModelConfig mc;
            getStr("model", mc.name);
            if (const auto* ctx = prov.Find("context"); ctx && ctx->IsNumber()) {
                mc.context = static_cast<int>(ctx->number);
            }
            if (const auto* mo = prov.Find("maxOutput"); mo && mo->IsNumber()) {
                mc.maxOutput = static_cast<int>(mo->number);
            }
            if (!mc.name.empty()) pc.models.push_back(std::move(mc));
        }
        // activeModel 空 / 指向已不存在的模型 ⇒ 回落首项（不留"无激活"状态）
        if (pc.activeModel.empty() && !pc.models.empty()) {
            pc.activeModel = pc.models[0].name;
        }

        if (pc.credentialTarget.empty()) {
            pc.id = ProviderIdFromBaseUrl(pc.baseUrl);
            pc.credentialTarget = CredentialTargetFor(pc.id);
        }
        // ★ §9.5：v1/v2 没有 `name` ⇒ 回落 id（观感不变、不引入"无名"状态）。
        //   ★ 必须放在**自愈块之后**：自愈会重写 id，name 得跟着最终 id 走。
        if (pc.name.empty()) pc.name = pc.id;
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
        if (pc.baseUrl.empty() || pc.models.empty()) {
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
    //   ★ §9.5 起 id 由用户声明（唯一），所以这条去重**语义变正了**：同 id 就是
    //     同一家，合并即正确。历史文件里的派生 id 重复仍靠它收敛——保留不动。
    const auto* arr = root.Find("providers");
    if (!arr || !arr->IsArray() || arr->array.empty()) {
        error = "config has no providers array";
        return false;
    }
    for (const auto& prov : arr->array) {
        if (!prov.IsObject()) continue;
        ProviderConfig pc;
        fillProvider(prov, pc);
        if (pc.baseUrl.empty() || pc.models.empty()) continue;
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
        putStr("name", pc.name);   // ★ v3（§9.5）：显示名。这里**不**替空值兜底——
                                   //   「空 ⇒ 回落 id」是界面侧的读取规则（ProviderDisplayName），
                                   //   存储层把两者都如实写下，回滚/对账才有得比。
        putStr("baseUrl", pc.baseUrl);
        // ★★ v4：模型进数组（真身）。
        {
            json::Value marr;
            marr.type = json::Value::Type::Array;
            for (const auto& m : pc.models) {
                json::Value mv;
                mv.type = json::Value::Type::Object;
                json::Value mn; mn.type = json::Value::Type::String; mn.text = m.name;
                json::Value mc; mc.type = json::Value::Type::Number;
                mc.number = static_cast<double>(m.context);
                json::Value mo; mo.type = json::Value::Type::Number;
                mo.number = static_cast<double>(m.maxOutput);
                mv.object["name"] = std::move(mn);
                mv.object["context"] = std::move(mc);
                mv.object["maxOutput"] = std::move(mo);
                marr.array.push_back(std::move(mv));
            }
            prov.object["models"] = std::move(marr);
        }
        putStr("activeModel", pc.activeModel);
        // ★ 双写：顶层三元组 = **激活模型**的值。真身是 `models[]`，这三行是给
        //   "只认顶层字段"的旧读取路径留的降级口（也方便肉眼对账）。两者都从
        //   `Active()` 生成 ⇒ 结构上不可能各说各话。
        putStr("model", pc.ActiveModelName());
        putNum("context", pc.ActiveContext());
        putNum("maxOutput", pc.ActiveMaxOutput());
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
    {
        json::Value kwArr;
        kwArr.type = json::Value::Type::Array;
        for (const auto& ws : knownWorkspaces) {
            json::Value v;
            v.type = json::Value::Type::String;
            v.text = ws;
            kwArr.array.push_back(std::move(v));
        }
        root.object["knownWorkspaces"] = std::move(kwArr);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write config file: " + WideToUtf8(path.wstring());
        return false;
    }
    out << root.Dump() << "\n";
    return true;
}
