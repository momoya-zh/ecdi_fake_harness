// AppConfig v3 → v4 → v5 迁移判据（2026-10-28，v5 增加 knownWorkspaces）。
//
// 为什么必须单独测：迁移的失败方式**全部是静默的**——模型名丢了、context 归零、
// id 被重算（凭据目标名跟着变 ⇒ key 断）、defaultProviderId 指向不存在的项、
// 已知工作区被清空。这些都不会崩，只会在某天"切过去用不了"。所以逐字段钉。
//
// ★ 没进 `regress.py`：它需要 MSVC 编译一个独立小二进制，而 regress 至今不假设
//   工具链可用（它只跑现成 exe）。**改 `ProviderConfig` / `AppConfig::Load/Save`
//   的任何结构，都要手工跑这一条**——否则迁移坏了没人知道。
//
// 编译 + 运行（Git Bash；`MSYS2_ARG_CONV_EXCL` 不加会被 MSYS 改写 `/Fo` `/I`）：
//   export MSYS2_ARG_CONV_EXCL="*"
//   cl /nologo /EHsc /std:c++17 /utf-8 /I"gui/src" /Fo".scratch\\" \
//      /Fe".scratch\appconfig-migrate-test.exe" \
//      tools/appconfig-migrate-test.cpp gui/src/AppConfig.cpp gui/src/Json.cpp \
//      /link shell32.lib ole32.lib
//   ./.scratch/appconfig-migrate-test.exe
//
// 判据：
//   1. v3 顶层三元组折叠成 models[0]，activeModel = 那个名字
//   2. id / credentialTarget 逐字节不变（否则 Credential Manager 里那把 key 就断了）
//   3. defaultProviderId 语义不变
//   4. Save 写 v4：version=4 + models[] + activeModel + **双写**顶层三元组
//   5. v4 往返幂等（Load→Save→Load 关键字段全等；二次 Save 逐字节相同）
//   6. activeModel 指向不存在的模型 ⇒ 回落 models[0]（不留"无激活"状态）
//   7. 多模型往返：两个模型都在、顺序不变、激活指针与双写顶层三元组都跟激活值
//      （2026-10-27 第三步补：双写若恒取 models[0]，旧读取路径会拿到另一个模型）
#include "AppConfig.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_fail = 0;

void Check(const char* name, bool ok, const std::string& detail = {}) {
    std::printf("  %s %-42s %s\n", ok ? "ok  " : "FAIL", name, detail.c_str());
    if (!ok) ++g_fail;
}

bool Has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

void WriteFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "ecdi-appconfig-v4-test.json";
    AppConfig::SetPathForTesting(path);

    // ── 1. v3 文件 → 迁移 ────────────────────────────────────────
    fs::remove(path);
    WriteFile(path, R"({
  "version": 3,
  "providers": [
    {"id": "zhipu", "name": "智谱", "baseUrl": "https://a.invalid/v4",
     "model": "glm-4.6", "context": 200000, "maxOutput": 32000,
     "credentialTarget": "ECDI.FakeHarness.v1.Provider.zhipu"},
    {"id": "ds", "name": "DeepSeek", "baseUrl": "https://b.invalid/v1",
     "model": "deepseek-chat", "context": 128000, "maxOutput": 8192,
     "credentialTarget": "ECDI.FakeHarness.v1.Provider.ds"}
  ],
  "defaultProviderId": "ds"
})");

    AppConfig cfg;
    std::string err;
    const bool loaded = cfg.Load(err);
    Check("1a v3 Load 成功", loaded, err);
    if (!loaded) return 1;

    Check("1b providers 条数", cfg.providers.size() == 2,
          std::to_string(cfg.providers.size()));
    const ProviderConfig& p0 = cfg.providers[0];
    Check("1c models 折叠成 1 项", p0.models.size() == 1,
          std::to_string(p0.models.size()));
    Check("1d 模型名", p0.ActiveModelName() == "glm-4.6", p0.ActiveModelName());
    Check("1e context 未丢", p0.ActiveContext() == 200000,
          std::to_string(p0.ActiveContext()));
    Check("1f maxOutput 未丢", p0.ActiveMaxOutput() == 32000,
          std::to_string(p0.ActiveMaxOutput()));
    Check("1g activeModel 已设", p0.activeModel == "glm-4.6", p0.activeModel);

    // 2. 凭据契约：这两个字符串一变，Credential Manager 里的 key 就找不到
    Check("2a id 不变", p0.id == "zhipu", p0.id);
    Check("2b credentialTarget 不变",
          p0.credentialTarget == "ECDI.FakeHarness.v1.Provider.zhipu",
          p0.credentialTarget);

    // 3. 默认提供商
    const ProviderConfig* def = cfg.DefaultProvider();
    Check("3  defaultProviderId 不变", def && def->id == "ds",
          def ? def->id : "<null>");

    // ── 4. Save 落 v5 ───────────────────────────────────────────
    Check("4a Save 成功", cfg.Save(err), err);
    const std::string text = ReadFile(path);
    Check("4b version=5", Has(text, "\"version\":5"), text.substr(0, 40));
    Check("4c 有 models 数组", Has(text, "\"models\":["));
    Check("4d 有 activeModel", Has(text, "\"activeModel\":\"glm-4.6\""));
    Check("4e 双写顶层 model", Has(text, "\"model\":\"glm-4.6\""));
    Check("4f 双写顶层 context", Has(text, "\"context\":200000"));
    Check("4g 保留 credentialTarget",
          Has(text, "\"credentialTarget\":\"ECDI.FakeHarness.v1.Provider.zhipu\""));
    Check("4h 有 knownWorkspaces", Has(text, "\"knownWorkspaces\":["));

    // ── 5. v4 往返幂等 ──────────────────────────────────────────
    AppConfig again;
    const bool reloaded = again.Load(err);
    Check("5a v4 重新载入", reloaded, err);
    if (reloaded) {
        Check("5b 条数一致", again.providers.size() == 2);
        Check("5c 模型名一致",
              again.providers[0].ActiveModelName() == "glm-4.6",
              again.providers[0].ActiveModelName());
        Check("5d context 一致", again.providers[0].ActiveContext() == 200000);
        Check("5e activeModel 一致", again.providers[0].activeModel == "glm-4.6");
        Check("5f credentialTarget 一致",
              again.providers[0].credentialTarget ==
                  "ECDI.FakeHarness.v1.Provider.zhipu");
        // 二次 Save 的文本应与第一次逐字节相同（否则每次启动都在改文件）
        Check("5g 二次 Save 成功", again.Save(err), err);
        Check("5h 二次 Save 逐字节相同", ReadFile(path) == text);
    }

    // ── 6. activeModel 悬空 ⇒ 回落首项 ──────────────────────────
    WriteFile(path, R"({
  "version": 4,
  "providers": [
    {"id": "x", "name": "X", "baseUrl": "https://x.invalid/v1",
     "credentialTarget": "ECDI.FakeHarness.v1.Provider.x",
     "models": [{"name": "m1", "context": 1000, "maxOutput": 10},
                {"name": "m2", "context": 2000, "maxOutput": 20}],
     "activeModel": "deleted-model"}
  ],
  "defaultProviderId": "x"
})");
    AppConfig dangling;
    const bool ok6 = dangling.Load(err);
    Check("6a 悬空 activeModel 仍可载入", ok6, err);
    if (ok6) {
        Check("6b 回落到 models[0]", dangling.providers[0].ActiveModelName() == "m1",
              dangling.providers[0].ActiveModelName());
        Check("6c 两个模型都在", dangling.providers[0].models.size() == 2);
        Check("6d 回落项的参数跟着走",
              dangling.providers[0].ActiveContext() == 1000,
              std::to_string(dangling.providers[0].ActiveContext()));
    }

    // ── 7. 多模型往返：两个模型都要活下来，激活项指向**第二个** ────────
    //   为什么单列一条（2026-10-27，v4 第三步）：Save 是「models[] + 双写顶层
    //   三元组」，而顶层三元组取自 `Active()`。若双写取错（比如恒取 models[0]），
    //   只认顶层字段的旧读取路径会拿到**另一个模型**的名字与容量——配置看着是
    //   v4，行为却退回 v3。这条把「两个模型都在」「激活指针没漂」「双写跟激活值」
    //   三件事钉在一起。
    {
        AppConfig multi;
        multi.version = AppConfig::kCurrentVersion;
        multi.defaultProviderId = "multi";
        ProviderConfig mp;
        mp.id = "multi";
        mp.name = "Multi";
        mp.baseUrl = "https://multi.invalid/v1";
        mp.credentialTarget = AppConfig::CredentialTargetFor("multi");
        mp.models.push_back(ModelConfig{"m1", 1000, 10});
        mp.models.push_back(ModelConfig{"m2", 2000, 20});
        mp.activeModel = "m2";
        multi.providers.push_back(mp);
        Check("7a 多模型 Save 成功", multi.Save(err), err);
        const std::string t7 = ReadFile(path);
        Check("7b 两个模型都写进去", Has(t7, "\"m1\"") && Has(t7, "\"m2\""));
        Check("7c activeModel = m2", Has(t7, "\"activeModel\":\"m2\""));
        Check("7d 双写顶层 model 取激活值", Has(t7, "\"model\":\"m2\""));
        Check("7e 双写顶层 context 取激活值", Has(t7, "\"context\":2000"));
        Check("7f 双写顶层 maxOutput 取激活值", Has(t7, "\"maxOutput\":20"));
        AppConfig back;
        const bool ok7 = back.Load(err);
        Check("7g 多模型重新载入", ok7, err);
        if (ok7) {
            Check("7h 两个模型都在", back.providers[0].models.size() == 2,
                  std::to_string(back.providers[0].models.size()));
            Check("7i 顺序保持 m1 在前",
                  back.providers[0].models.size() == 2 &&
                      back.providers[0].models[0].name == "m1" &&
                      back.providers[0].models[1].name == "m2");
            Check("7j 激活指针仍指 m2",
                  back.providers[0].ActiveModelName() == "m2",
                  back.providers[0].ActiveModelName());
            Check("7k 激活容量跟着 m2", back.providers[0].ActiveContext() == 2000,
                  std::to_string(back.providers[0].ActiveContext()));
            // 往返幂等：再存一次不得改文件（否则每次启动都在动配置）
            Check("7l 多模型二次 Save 成功", back.Save(err), err);
            Check("7m 多模型二次 Save 逐字节相同", ReadFile(path) == t7);
        }
    }

    // ── 8. v4 -> v5 迁移与 knownWorkspaces 往返 ───────────────────
    WriteFile(path, R"({
  "version": 4,
  "providers": [
    {"id": "p4", "name": "P4", "baseUrl": "https://p4.invalid/v1",
     "credentialTarget": "ECDI.FakeHarness.v1.Provider.p4",
     "models": [{"name": "m4", "context": 4000, "maxOutput": 40}],
     "activeModel": "m4"}
  ],
  "defaultProviderId": "p4"
})");
    {
        AppConfig v4cfg;
        const bool ok8 = v4cfg.Load(err);
        Check("8a v4 (无 knownWorkspaces) 载入成功", ok8, err);
        if (ok8) {
            Check("8b knownWorkspaces 初始为空", v4cfg.knownWorkspaces.empty());
            v4cfg.AddKnownWorkspace("D:/Project/ECDI");
            v4cfg.AddKnownWorkspace("D:/Project/ECDI");  // 重复添加测试去重
            v4cfg.AddKnownWorkspace("C:/Workspace/Foo");
            Check("8c AddKnownWorkspace 去重后为 2 条", v4cfg.knownWorkspaces.size() == 2,
                  std::to_string(v4cfg.knownWorkspaces.size()));
            Check("8d Save 升级为 v5 成功", v4cfg.Save(err), err);
            const std::string t8 = ReadFile(path);
            Check("8e 保存后 version=5", Has(t8, "\"version\":5"));
            Check("8f 包含添加的工作区", Has(t8, "D:/Project/ECDI") && Has(t8, "C:/Workspace/Foo"));

            AppConfig v5cfg;
            const bool ok8reload = v5cfg.Load(err);
            Check("8g v5 重新载入成功", ok8reload, err);
            if (ok8reload) {
                Check("8h 保持 2 条工作区", v5cfg.knownWorkspaces.size() == 2);
                Check("8i 路径顺序一致", v5cfg.knownWorkspaces[0] == "D:/Project/ECDI" &&
                                          v5cfg.knownWorkspaces[1] == "C:/Workspace/Foo");
                v5cfg.RemoveKnownWorkspace("D:/Project/ECDI");
                Check("8j 移除后剩 1 条", v5cfg.knownWorkspaces.size() == 1 &&
                                           v5cfg.knownWorkspaces[0] == "C:/Workspace/Foo");
            }
        }
    }

    fs::remove(path);
    std::printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
